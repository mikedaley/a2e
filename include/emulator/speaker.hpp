#pragma once

#include <cstdint>
#include <array>
#include <mutex>
#include <memory>
#include <atomic>

// Forward declare AudioQueue types
typedef struct OpaqueAudioQueue* AudioQueueRef;
typedef struct AudioQueueBuffer* AudioQueueBufferRef;
struct AudioQueueBufferRefWrapper;

/**
 * Speaker - Apple IIe speaker emulation
 *
 * The Apple IIe has a simple 1-bit speaker connected to address $C030.
 * Reading or writing to this address toggles the speaker cone position,
 * producing a click. Rapid toggling at specific frequencies produces tones.
 *
 * This implementation uses CoreAudio's AudioQueue for reliable audio output:
 * - Ring buffer holds generated samples
 * - AudioQueue callback pulls samples when needed
 * - Uses PWM (pulse-width modulation) to calculate sample values based on
 *   the ratio of high to low speaker states during each sample period
 */
class Speaker
{
public:
  // Audio configuration
  static constexpr int SAMPLE_RATE = 48000;
  static constexpr int CHANNELS = 1;
  static constexpr int BITS_PER_SAMPLE = 16;
  
  // Number of AudioQueue buffers
  static constexpr int NUM_BUFFERS = 3;
  
  // Frames per AudioQueue buffer - larger for more consistent callback timing
  static constexpr int FRAMES_PER_BUFFER = 512;
  
  // Ring buffer for audio samples (~250ms at 48000Hz)
  static constexpr size_t AUDIO_BUFFER_SIZE = 12000;

  // Apple IIe clock rate for timing calculations
  static constexpr double CPU_CLOCK_HZ = 1023000.0;
  
  // CPU cycles per audio sample
  static constexpr double CYCLES_PER_SAMPLE = CPU_CLOCK_HZ / static_cast<double>(SAMPLE_RATE);

  // Speaker amplitude (reduced to prevent clipping)
  static constexpr double SPEAKER_HIGH = +0.25;
  static constexpr double SPEAKER_LOW = -0.25;

  Speaker();
  ~Speaker();

  /**
   * Initialize the audio system
   * @return true if successful
   */
  bool initialize();

  /**
   * Shutdown the audio system
   */
  void shutdown();

  /**
   * Toggle the speaker state (called when $C030 is accessed)
   * @param cycle Current CPU cycle count for timing
   */
  void toggle(uint64_t cycle);

  /**
   * Update audio output - call this regularly (e.g., once per frame)
   * @param current_cycle Current CPU cycle count
   */
  void update(uint64_t current_cycle);

  /**
   * Set the audio volume (0.0 to 1.0)
   */
  void setVolume(float volume);

  /**
   * Get the current volume
   */
  float getVolume() const { return volume_; }

  /**
   * Mute/unmute audio
   */
  void setMuted(bool muted);
  bool isMuted() const { return muted_; }

  /**
   * Reset speaker state (call when focus changes to avoid audio glitches)
   * @param current_cycle Current CPU cycle count to sync to
   */
  void reset(uint64_t current_cycle);

  /**
   * Check if audio is initialized
   */
  bool isInitialized() const { return initialized_; }

  /**
   * Get the current speaker state (high or low)
   */
  bool getSpeakerState() const { return speaker_state_; }

  /**
   * Get the current buffer fill level (0.0 to 1.0)
   * Used for audio-driven timing
   */
  float getBufferFillPercentage() const;

private:
  // AudioQueue callback
  static void audioQueueCallback(void* userData,
                                  AudioQueueRef queue,
                                  AudioQueueBufferRef buffer);

  // Fill audio output buffer from ring buffer
  void fillAudioBuffer(uint32_t framesPerBuffer, int16_t* out);

  // Generate samples for the given number of CPU cycles
  void generateSamples(uint64_t cycles_to_process);

  // Audio system state
  bool initialized_ = false;
  AudioQueueRef audioQueue_ = nullptr;
  AudioQueueBufferRef audioBuffers_[NUM_BUFFERS] = {nullptr};

  // Speaker state
  bool speaker_state_ = false;  // Current speaker position (high/low)
  
  // Cycle tracking - uses delta approach
  uint64_t last_cpu_cycle_ = 0;  // Last cycle count we processed
  
  // Cycle accumulator for sample generation
  double cycle_accumulator_ = 0.0;
  
  // PWM tracking - ratio of high to total cycles per sample
  uint64_t high_cycles_ = 0;
  uint64_t total_cycles_ = 0;
  
  // Audio generation
  float volume_ = 0.5f;
  bool muted_ = false;

  // Ring buffer for audio samples (thread-safe access)
  mutable std::mutex buffer_mutex_;
  std::unique_ptr<int16_t[]> audio_buffer_;
  size_t buffer_size_ = 0;
  std::atomic<size_t> read_pos_{0};
  std::atomic<size_t> write_pos_{0};
};
