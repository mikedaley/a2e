#include "emulator/speaker.hpp"
#include <portaudio.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <cmath>

Speaker::Speaker()
{
  // Calculate buffer size for 1/4 second of audio
  buffer_size_ = SAMPLE_RATE / 4;
  
  // Allocate the audio buffer
  try
  {
    audio_buffer_ = std::make_unique<int16_t[]>(buffer_size_);
    std::fill(audio_buffer_.get(), audio_buffer_.get() + buffer_size_, 0);
  }
  catch (const std::bad_alloc& e)
  {
    std::cerr << "Failed to allocate audio buffer: " << e.what() << std::endl;
  }
}

Speaker::~Speaker()
{
  shutdown();
}

int Speaker::audioCallback(const void* /*inputBuffer*/, void* outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* /*timeInfo*/,
                           PaStreamCallbackFlags /*statusFlags*/,
                           void* userData)
{
  auto* speaker = static_cast<Speaker*>(userData);
  auto* out = static_cast<int16_t*>(outputBuffer);
  
  {
    std::lock_guard<std::mutex> lock(speaker->buffer_mutex_);
    speaker->fillAudioBuffer(framesPerBuffer, out);
  }
  
  return paContinue;
}

void Speaker::fillAudioBuffer(unsigned long framesPerBuffer, int16_t* out)
{
  // Calculate available samples
  size_t writeIdx = write_pos_.load();
  size_t readIdx = read_pos_.load();
  
  uint32_t available = (writeIdx >= readIdx)
                         ? (writeIdx - readIdx)
                         : (buffer_size_ - readIdx + writeIdx);

  static int16_t lastSample = 0;

  // If we don't have enough samples, pad with silence
  if (available < framesPerBuffer)
  {
    // Add silence to prevent underruns
    int samplesToAdd = framesPerBuffer - available + 64;
    for (int i = 0; i < samplesToAdd; i++)
    {
      audio_buffer_[write_pos_] = 0;
      write_pos_ = (write_pos_ + 1) % buffer_size_;
    }
  }

  // Copy samples to output buffer
  size_t currentRead = read_pos_.load();
  size_t currentWrite = write_pos_.load();
  
  if (currentRead + framesPerBuffer <= buffer_size_ && 
      ((currentWrite >= currentRead) ? (currentWrite - currentRead) : (buffer_size_ - currentRead + currentWrite)) >= framesPerBuffer)
  {
    // Fast path: contiguous copy
    memcpy(out, &audio_buffer_[currentRead], framesPerBuffer * sizeof(int16_t));
    read_pos_ = (currentRead + framesPerBuffer) % buffer_size_;
  }
  else
  {
    // Sample-by-sample fallback
    for (unsigned long i = 0; i < framesPerBuffer; i++)
    {
      size_t rIdx = read_pos_.load();
      size_t wIdx = write_pos_.load();
      
      if (rIdx != wIdx)
      {
        lastSample = audio_buffer_[rIdx];
        out[i] = lastSample;
        read_pos_ = (rIdx + 1) % buffer_size_;
      }
      else
      {
        // Buffer underrun - use zero
        out[i] = 0;
      }
    }
  }
}

bool Speaker::initialize()
{
  if (initialized_)
  {
    return true;
  }

  if (!audio_buffer_)
  {
    std::cerr << "Audio buffer not allocated" << std::endl;
    return false;
  }

  PaError err = Pa_Initialize();
  if (err != paNoError)
  {
    std::cerr << "PortAudio init error: " << Pa_GetErrorText(err) << std::endl;
    return false;
  }

  // Set up output parameters
  PaStreamParameters outputParams;
  outputParams.device = Pa_GetDefaultOutputDevice();
  if (outputParams.device == paNoDevice)
  {
    std::cerr << "No default output device found" << std::endl;
    Pa_Terminate();
    return false;
  }

  outputParams.channelCount = CHANNELS;
  outputParams.sampleFormat = paInt16;
  outputParams.suggestedLatency = 0.020;  // 20ms latency
  outputParams.hostApiSpecificStreamInfo = nullptr;

  // Open stream with moderate buffer size
  err = Pa_OpenStream(
    &stream_,
    nullptr,  // no input
    &outputParams,
    SAMPLE_RATE,
    128,      // frames per buffer
    paClipOff,
    &Speaker::audioCallback,
    this
  );

  if (err != paNoError)
  {
    std::cerr << "PortAudio open error: " << Pa_GetErrorText(err) << std::endl;
    Pa_Terminate();
    return false;
  }

  // Pre-fill buffer before starting
  read_pos_ = 0;
  write_pos_ = buffer_size_ / 2;

  // Start the stream
  err = Pa_StartStream(stream_);
  if (err != paNoError)
  {
    std::cerr << "PortAudio start error: " << Pa_GetErrorText(err) << std::endl;
    Pa_CloseStream(stream_);
    Pa_Terminate();
    return false;
  }

  initialized_ = true;
  std::cout << "Speaker initialized (sample rate: " << SAMPLE_RATE << " Hz)" << std::endl;
  return true;
}

void Speaker::shutdown()
{
  if (!initialized_)
  {
    return;
  }

  initialized_ = false;

  if (stream_)
  {
    Pa_StopStream(stream_);
    Pa_CloseStream(stream_);
    stream_ = nullptr;
  }
  
  Pa_Terminate();
}

void Speaker::toggle(uint64_t cycle)
{
  if (!initialized_)
  {
    speaker_state_ = !speaker_state_;
    return;
  }

  // Generate samples up to this cycle with the current state
  if (cycle > last_cpu_cycle_)
  {
    generateSamples(cycle - last_cpu_cycle_);
    last_cpu_cycle_ = cycle;
  }
  
  // Now toggle the state
  speaker_state_ = !speaker_state_;
}

void Speaker::generateSamples(uint64_t cycles_to_process)
{
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  
  double cyclesRemaining = static_cast<double>(cycles_to_process);
  
  while (cyclesRemaining > 0.0)
  {
    // Track speaker state for PWM calculation
    if (speaker_state_)
    {
      high_cycles_++;
    }
    total_cycles_++;
    
    cycle_accumulator_ += 1.0;
    cyclesRemaining -= 1.0;
    
    // When we've accumulated enough cycles for a sample
    if (cycle_accumulator_ >= CYCLES_PER_SAMPLE)
    {
      // Calculate PWM ratio
      double ratio = total_cycles_ > 0
                       ? static_cast<double>(high_cycles_) / static_cast<double>(total_cycles_)
                       : (speaker_state_ ? 1.0 : 0.0);
      
      // Linear interpolation between SPEAKER_LOW and SPEAKER_HIGH
      double sample = SPEAKER_LOW + (ratio * (SPEAKER_HIGH - SPEAKER_LOW));
      
      // Apply volume
      float vol = muted_ ? 0.0f : volume_;
      sample *= vol;
      
      // Convert to 16-bit sample
      int16_t sampleVal = static_cast<int16_t>(sample * 32767.0 + 0.5);
      
      // Store in ring buffer
      size_t nextWrite = (write_pos_ + 1) % buffer_size_;
      if (nextWrite != read_pos_.load())  // Don't overwrite unread data
      {
        audio_buffer_[write_pos_] = sampleVal;
        write_pos_ = nextWrite;
      }
      
      // Reset accumulators
      cycle_accumulator_ -= CYCLES_PER_SAMPLE;
      high_cycles_ = 0;
      total_cycles_ = 0;
    }
  }
}

void Speaker::update(uint64_t current_cycle)
{
  if (!initialized_)
  {
    return;
  }

  // Generate samples up to current cycle
  if (current_cycle > last_cpu_cycle_)
  {
    generateSamples(current_cycle - last_cpu_cycle_);
    last_cpu_cycle_ = current_cycle;
  }
  
  // PortAudio callback handles sending samples - nothing else to do here
}

void Speaker::setVolume(float volume)
{
  volume_ = std::clamp(volume, 0.0f, 1.0f);
}

void Speaker::setMuted(bool muted)
{
  muted_ = muted;
}

void Speaker::reset(uint64_t current_cycle)
{
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    last_cpu_cycle_ = current_cycle;  // Sync to current cycle to avoid processing backlog
    cycle_accumulator_ = 0.0;
    high_cycles_ = 0;
    total_cycles_ = 0;

    // Clear and pre-fill buffer
    if (audio_buffer_)
    {
      std::fill(audio_buffer_.get(), audio_buffer_.get() + buffer_size_, 0);
    }
    read_pos_ = 0;
    write_pos_ = buffer_size_ / 2;
  }
}

float Speaker::getBufferFillPercentage() const
{
  size_t writeIdx = write_pos_.load();
  size_t readIdx = read_pos_.load();
  
  uint32_t available = (writeIdx >= readIdx)
                         ? (writeIdx - readIdx)
                         : (buffer_size_ - readIdx + writeIdx);
  
  return static_cast<float>(available) / static_cast<float>(buffer_size_);
}
