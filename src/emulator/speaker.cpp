#include "emulator/speaker.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
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

void Speaker::audioQueueCallback(void* userData,
                                  AudioQueueRef queue,
                                  AudioQueueBufferRef buffer)
{
  auto* speaker = static_cast<Speaker*>(userData);
  auto* out = static_cast<int16_t*>(buffer->mAudioData);
  uint32_t frames = buffer->mAudioDataByteSize / sizeof(int16_t);
  
  {
    std::lock_guard<std::mutex> lock(speaker->buffer_mutex_);
    speaker->fillAudioBuffer(frames, out);
  }
  
  // Re-enqueue the buffer
  AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

void Speaker::fillAudioBuffer(uint32_t framesPerBuffer, int16_t* out)
{
  // Calculate available samples
  size_t writeIdx = write_pos_.load();
  size_t readIdx = read_pos_.load();
  
  uint32_t available = (writeIdx >= readIdx)
                         ? (writeIdx - readIdx)
                         : (buffer_size_ - readIdx + writeIdx);

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
    for (uint32_t i = 0; i < framesPerBuffer; i++)
    {
      size_t rIdx = read_pos_.load();
      size_t wIdx = write_pos_.load();
      
      if (rIdx != wIdx)
      {
        out[i] = audio_buffer_[rIdx];
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

  // Set up the audio format
  AudioStreamBasicDescription format = {};
  format.mSampleRate = SAMPLE_RATE;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  format.mBitsPerChannel = BITS_PER_SAMPLE;
  format.mChannelsPerFrame = CHANNELS;
  format.mBytesPerFrame = (BITS_PER_SAMPLE / 8) * CHANNELS;
  format.mFramesPerPacket = 1;
  format.mBytesPerPacket = format.mBytesPerFrame;

  // Create the audio queue with NULL run loop - uses internal audio thread
  OSStatus status = AudioQueueNewOutput(
    &format,
    audioQueueCallback,
    this,
    NULL,
    NULL,
    0,
    &audioQueue_
  );

  if (status != noErr)
  {
    std::cerr << "AudioQueueNewOutput error: " << status << std::endl;
    return false;
  }

  // Allocate and enqueue buffers
  uint32_t bufferSize = FRAMES_PER_BUFFER * sizeof(int16_t);
  for (int i = 0; i < NUM_BUFFERS; i++)
  {
    status = AudioQueueAllocateBuffer(audioQueue_, bufferSize, &audioBuffers_[i]);
    if (status != noErr)
    {
      std::cerr << "AudioQueueAllocateBuffer error: " << status << std::endl;
      AudioQueueDispose(audioQueue_, true);
      audioQueue_ = nullptr;
      return false;
    }
    
    // Initialize buffer with silence
    audioBuffers_[i]->mAudioDataByteSize = bufferSize;
    memset(audioBuffers_[i]->mAudioData, 0, bufferSize);
    
    // Enqueue the buffer
    status = AudioQueueEnqueueBuffer(audioQueue_, audioBuffers_[i], 0, nullptr);
    if (status != noErr)
    {
      std::cerr << "AudioQueueEnqueueBuffer error: " << status << std::endl;
      AudioQueueDispose(audioQueue_, true);
      audioQueue_ = nullptr;
      return false;
    }
  }

  // Pre-fill ring buffer before starting
  read_pos_ = 0;
  write_pos_ = buffer_size_ / 2;

  // Start the audio queue
  status = AudioQueueStart(audioQueue_, nullptr);
  if (status != noErr)
  {
    std::cerr << "AudioQueueStart error: " << status << std::endl;
    AudioQueueDispose(audioQueue_, true);
    audioQueue_ = nullptr;
    return false;
  }

  initialized_ = true;
  std::cout << "Speaker initialized with CoreAudio (sample rate: " << SAMPLE_RATE << " Hz)" << std::endl;
  return true;
}

void Speaker::shutdown()
{
  if (!initialized_)
  {
    return;
  }

  initialized_ = false;

  if (audioQueue_)
  {
    AudioQueueStop(audioQueue_, true);
    AudioQueueDispose(audioQueue_, true);
    audioQueue_ = nullptr;
  }
  
  // Clear buffer pointers (memory freed by AudioQueueDispose)
  for (int i = 0; i < NUM_BUFFERS; i++)
  {
    audioBuffers_[i] = nullptr;
  }
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
  
  // AudioQueue callback handles sending samples - nothing else to do here
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
