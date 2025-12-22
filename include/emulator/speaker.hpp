#pragma once

#include <cstdint>
#include <array>

struct SDL_AudioStream;

/**
 * Speaker - Apple IIe speaker emulation
 *
 * The Apple IIe has a simple 1-bit speaker connected to address $C030.
 * Reading or writing to this address toggles the speaker cone position,
 * producing a click. Rapid toggling at specific frequencies produces tones.
 *
 * This implementation uses a simple sample-accurate approach:
 * - Maintains a circular buffer of samples
 * - Each CPU cycle maps to a fractional sample position
 * - Toggle events set the sample value at the appropriate position
 */
class Speaker
{
public:
  // Audio configuration
  static constexpr int SAMPLE_RATE = 44100;
  static constexpr int CHANNELS = 1;
  
  // Circular buffer holds ~100ms of audio
  static constexpr size_t RING_BUFFER_SIZE = 4096;

  // Apple IIe clock rate for timing calculations
  static constexpr double CPU_CLOCK_HZ = 1023000.0;
  
  // Samples per CPU cycle
  static constexpr double SAMPLES_PER_CYCLE = static_cast<double>(SAMPLE_RATE) / CPU_CLOCK_HZ;

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
   */
  void reset();

  /**
   * Check if audio is initialized
   */
  bool isInitialized() const { return initialized_; }

  /**
   * Get the current speaker state (high or low)
   */
  bool getSpeakerState() const { return speaker_state_; }

private:
  // Audio system state
  bool initialized_ = false;
  SDL_AudioStream* audio_stream_ = nullptr;
  uint32_t audio_device_id_ = 0;

  // Speaker state
  bool speaker_state_ = false;  // Current speaker position (high/low)
  
  // Cycle tracking
  uint64_t base_cycle_ = 0;        // Cycle count at start of current buffer period
  uint64_t last_toggle_cycle_ = 0; // Last cycle when speaker was toggled
  
  // Audio generation
  float volume_ = 0.5f;
  bool muted_ = false;
  
  // Sample position tracking (fractional)
  double sample_position_ = 0.0;
  
  // Ring buffer for audio samples
  std::array<float, RING_BUFFER_SIZE> ring_buffer_;
  size_t write_pos_ = 0;
  size_t samples_pending_ = 0;
  
  // Output buffer for SDL
  std::array<int16_t, 1024> output_buffer_;

  /**
   * Fill the ring buffer with samples up to the given cycle
   */
  void fillBufferToCycle(uint64_t cycle);
  
  /**
   * Convert and send samples to SDL
   */
  void flushToSDL();
};
