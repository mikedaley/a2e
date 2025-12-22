#include "utils/paste_handler.hpp"
#include "emulator/emulator.hpp"

std::queue<uint8_t> PasteHandler::paste_queue_;

void PasteHandler::paste(const std::string& text)
{
  // Clear existing queue
  clear();

  // Process and queue each character
  for (char c : text)
  {
    convertToUpperCase(c);
    handleNewlines(c);
    paste_queue_.push(static_cast<uint8_t>(c));
  }
}

void PasteHandler::update(emulator& emu)
{
  if (paste_queue_.empty())
  {
    return;
  }

  // Only send next character if keyboard strobe is clear
  // (previous key has been read by the Apple II)
  if (emu.isKeyboardStrobeSet())
  {
    return;  // Wait for Apple II to read the current key
  }

  // Send the next character from the queue
  uint8_t c = paste_queue_.front();
  paste_queue_.pop();
  
  // Feed the character to the keyboard
  emu.keyDown(c);
}

bool PasteHandler::isPasting()
{
  return !paste_queue_.empty();
}

void PasteHandler::clear()
{
  while (!paste_queue_.empty())
  {
    paste_queue_.pop();
  }
}

void PasteHandler::convertToUpperCase(char& c)
{
  // Apple II BASIC expects uppercase letters
  if (c >= 'a' && c <= 'z')
  {
    c = c - 'a' + 'A';
  }
}

void PasteHandler::handleNewlines(char& c)
{
  // Convert newlines to carriage return (Apple II standard)
  if (c == '\n' || c == '\r')
  {
    c = 0x0D;
  }
}
