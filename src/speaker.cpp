#include "speaker.hpp"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <cmath>

Speaker::Speaker()
{
  ring_buffer_.fill(0.0f);
  output_buffer_.fill(0);
}

Speaker::~Speaker()
{
  shutdown();
}

bool Speaker::initialize()
{
  if (initialized_)
  {
    return true;
  }

  // Initialize SDL audio subsystem if not already done
  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO))
  {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
      std::cerr << "Failed to initialize SDL audio: " << SDL_GetError() << std::endl;
      return false;
    }
  }

  // Set up audio specification
  SDL_AudioSpec spec;
  spec.freq = SAMPLE_RATE;
  spec.format = SDL_AUDIO_S16;
  spec.channels = CHANNELS;

  // Open audio device first (SDL3 requires this before binding streams)
  audio_device_id_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
  if (audio_device_id_ == 0)
  {
    std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
    return false;
  }

  // Create audio stream
  audio_stream_ = SDL_CreateAudioStream(&spec, &spec);
  if (!audio_stream_)
  {
    std::cerr << "Failed to create audio stream: " << SDL_GetError() << std::endl;
    SDL_CloseAudioDevice(audio_device_id_);
    audio_device_id_ = 0;
    return false;
  }

  // Bind stream to the opened device
  if (!SDL_BindAudioStream(audio_device_id_, audio_stream_))
  {
    std::cerr << "Failed to bind audio stream: " << SDL_GetError() << std::endl;
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
    SDL_CloseAudioDevice(audio_device_id_);
    audio_device_id_ = 0;
    return false;
  }

  initialized_ = true;
  std::cout << "Speaker initialized (sample rate: " << SAMPLE_RATE << " Hz)" << std::endl;
  return true;
}

void Speaker::shutdown()
{
  initialized_ = false;
  
  if (audio_stream_)
  {
    SDL_FlushAudioStream(audio_stream_);
    SDL_UnbindAudioStream(audio_stream_);
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
  }
  if (audio_device_id_ != 0)
  {
    SDL_CloseAudioDevice(audio_device_id_);
    audio_device_id_ = 0;
  }
}

void Speaker::toggle(uint64_t cycle)
{
  if (!initialized_)
  {
    speaker_state_ = !speaker_state_;
    return;
  }

  // Fill buffer up to this cycle with the old state
  fillBufferToCycle(cycle);
  
  // Now toggle the state
  speaker_state_ = !speaker_state_;
  last_toggle_cycle_ = cycle;
}

void Speaker::fillBufferToCycle(uint64_t cycle)
{
  // First call or after reset - just initialize base cycle
  if (base_cycle_ == 0)
  {
    base_cycle_ = cycle;
    sample_position_ = 0.0;
    samples_pending_ = 0;
    write_pos_ = 0;
    return;
  }

  // Handle cycle going backwards (shouldn't happen, but be safe)
  if (cycle < base_cycle_)
  {
    base_cycle_ = cycle;
    sample_position_ = 0.0;
    return;
  }

  // Calculate target sample position
  uint64_t cycles_elapsed = cycle - base_cycle_;
  double target_sample_pos = cycles_elapsed * SAMPLES_PER_CYCLE;
  
  // Calculate how many samples we need
  double samples_needed = target_sample_pos - sample_position_;
  
  // If we're behind or somehow ahead, just sync up without generating audio
  if (samples_needed < 0 || samples_needed > 2000)
  {
    sample_position_ = target_sample_pos;
    return;
  }
  
  // Generate samples from current position to target
  while (sample_position_ < target_sample_pos)
  {
    // Get the sample value based on speaker state
    float sample_value = speaker_state_ ? 1.0f : -1.0f;
    
    // Write to ring buffer
    ring_buffer_[write_pos_] = sample_value;
    write_pos_ = (write_pos_ + 1) % RING_BUFFER_SIZE;
    samples_pending_++;
    
    sample_position_ += 1.0;
    
    // Prevent buffer overflow - flush if getting full
    if (samples_pending_ >= RING_BUFFER_SIZE - 256)
    {
      flushToSDL();
    }
  }
}

void Speaker::update(uint64_t current_cycle)
{
  if (!initialized_ || !audio_stream_)
  {
    return;
  }

  // Fill buffer up to current cycle
  fillBufferToCycle(current_cycle);
  
  // Flush accumulated samples to SDL
  flushToSDL();
  
  // Reset base cycle periodically to prevent overflow issues
  // Do this smoothly by just adjusting the base without clearing audio
  if (current_cycle - base_cycle_ > 10000000)
  {
    uint64_t cycles_elapsed = current_cycle - base_cycle_;
    double samples_generated = cycles_elapsed * SAMPLES_PER_CYCLE;
    
    // Adjust base cycle forward, keeping sample_position_ in sync
    base_cycle_ = current_cycle;
    sample_position_ = sample_position_ - samples_generated;
    if (sample_position_ < 0)
    {
      sample_position_ = 0;
    }
  }
}

void Speaker::flushToSDL()
{
  if (!audio_stream_ || samples_pending_ == 0)
  {
    return;
  }

  // Calculate volume multiplier
  float vol = muted_ ? 0.0f : volume_;
  int16_t amplitude = static_cast<int16_t>(8192 * vol);

  // Process samples in chunks
  size_t read_pos = (write_pos_ + RING_BUFFER_SIZE - samples_pending_) % RING_BUFFER_SIZE;
  
  while (samples_pending_ > 0)
  {
    size_t chunk_size = std::min(samples_pending_, output_buffer_.size());
    
    for (size_t i = 0; i < chunk_size; ++i)
    {
      float sample = ring_buffer_[read_pos];
      output_buffer_[i] = static_cast<int16_t>(sample * amplitude);
      read_pos = (read_pos + 1) % RING_BUFFER_SIZE;
    }
    
    SDL_PutAudioStreamData(audio_stream_, output_buffer_.data(), 
                           chunk_size * sizeof(int16_t));
    
    samples_pending_ -= chunk_size;
  }
}

void Speaker::setVolume(float volume)
{
  volume_ = std::clamp(volume, 0.0f, 1.0f);
}

void Speaker::setMuted(bool muted)
{
  muted_ = muted;
}

void Speaker::reset()
{
  base_cycle_ = 0;
  sample_position_ = 0.0;
  samples_pending_ = 0;
  write_pos_ = 0;
  speaker_state_ = false;
  
  if (audio_stream_)
  {
    SDL_ClearAudioStream(audio_stream_);
  }
}
