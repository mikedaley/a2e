#pragma once

#include <string>
#include <queue>
#include <cstdint>

class emulator;

/**
 * PasteHandler - Handles pasting text into the emulator as keyboard input
 *
 * Text is queued and fed character by character to the emulator's keyboard
 * input, simulating typing. Characters are converted to uppercase (Apple II
 * standard) and newlines are converted to carriage returns.
 */
class PasteHandler
{
public:
  /**
   * Add text to the paste queue
   * @param text Text to paste
   */
  static void paste(const std::string& text);

  /**
   * Process next character in queue if keyboard is ready
   * Call this regularly (e.g., each frame)
   * @param emu Reference to the emulator
   */
  static void update(emulator& emu);

  /**
   * Check if paste is in progress
   * @return true if there are characters waiting to be pasted
   */
  static bool isPasting();

  /**
   * Clear the paste queue
   */
  static void clear();

private:
  static void convertToUpperCase(char& c);
  static void handleNewlines(char& c);
  
  static std::queue<uint8_t> paste_queue_;
};
