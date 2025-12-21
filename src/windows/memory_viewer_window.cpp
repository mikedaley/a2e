#include "windows/memory_viewer_window.hpp"
#include <imgui.h>
#include "windows/imgui_memory_editor.h"

// Context structure passed to the memory editor callbacks
struct MemoryViewerContext
{
  std::function<uint8_t(uint16_t)> read_callback;
  std::function<void(uint16_t, uint8_t)> write_callback;
};

// Static context for callbacks (MemoryEditor uses void* UserData)
static MemoryViewerContext g_context;

// Read callback for MemoryEditor
static ImU8 ReadCallback(const ImU8* /*mem*/, size_t off, void* /*user_data*/)
{
  if (g_context.read_callback)
  {
    return g_context.read_callback(static_cast<uint16_t>(off));
  }
  return 0;
}

// Write callback for MemoryEditor
static void WriteCallback(ImU8* /*mem*/, size_t off, ImU8 d, void* /*user_data*/)
{
  if (g_context.write_callback)
  {
    g_context.write_callback(static_cast<uint16_t>(off), d);
  }
}

memory_viewer_window::memory_viewer_window()
    : mem_edit_(std::make_unique<MemoryEditor>())
{
  setOpen(true);

  // Configure the memory editor
  mem_edit_->ReadOnly = false;
  mem_edit_->Cols = 16;
  mem_edit_->OptShowOptions = false;
  mem_edit_->OptShowDataPreview = false;
  mem_edit_->OptShowAscii = true;
  mem_edit_->OptGreyOutZeroes = true;
  mem_edit_->OptUpperCaseHex = true;
  mem_edit_->OptAddrDigitsCount = 4; // 16-bit addresses

  // Set up callbacks
  mem_edit_->ReadFn = ReadCallback;
  mem_edit_->WriteFn = WriteCallback;
}

memory_viewer_window::~memory_viewer_window() = default;

void memory_viewer_window::setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback)
{
  memory_read_callback_ = std::move(callback);
  g_context.read_callback = memory_read_callback_;
}

void memory_viewer_window::setMemoryWriteCallback(std::function<void(uint16_t, uint8_t)> callback)
{
  memory_write_callback_ = std::move(callback);
  g_context.write_callback = memory_write_callback_;
}

void memory_viewer_window::setBaseAddress(uint16_t address)
{
  if (mem_edit_)
  {
    mem_edit_->GotoAddrAndHighlight(address, address + 1);
  }
}

void memory_viewer_window::render()
{
  if (!open_)
  {
    return;
  }

  if (!memory_read_callback_)
  {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(getName(), &open_))
    {
      ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No memory callback set!");
    }
    ImGui::End();
    return;
  }

  // DrawWindow handles its own Begin/End
  // We pass nullptr for mem_data since we use ReadFn/WriteFn callbacks
  // Size is 64KB (0x10000) for Apple IIe address space
  mem_edit_->DrawWindow(getName(), nullptr, 0x10000, 0x0000);

  // Sync the open state from MemoryEditor
  open_ = mem_edit_->Open;
}
