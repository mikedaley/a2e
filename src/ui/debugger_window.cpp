#include "ui/debugger_window.hpp"
#include "emulator/emulator.hpp"
#include "emulator/breakpoint_manager.hpp"
#include <MOS6502/Disassembler/Disassembler.hpp>
#include <MOS6502/Disassembler/OpcodeTable.hpp>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>

namespace {
// Extract just mnemonic + operand from disassembler output
// Input format: "$XXXX: XX XX XX  MNE  operand"
// Positions:     0123456789...    17+
std::string extractMnemonic(const std::string& disasm)
{
  // Fixed prefix: "$XXXX: " (7) + "XX XX XX " (9) + " " (1) = 17 chars
  // Mnemonic starts at position 17
  constexpr size_t mnemonic_start = 17;

  if (disasm.size() <= mnemonic_start)
    return disasm;

  // Skip any leading whitespace after the fixed prefix
  size_t start = mnemonic_start;
  while (start < disasm.size() && std::isspace(static_cast<unsigned char>(disasm[start])))
    ++start;

  // Trim trailing whitespace
  size_t end = disasm.size();
  while (end > start && std::isspace(static_cast<unsigned char>(disasm[end - 1])))
    --end;

  return disasm.substr(start, end - start);
}
}

debugger_window::debugger_window(emulator& emu)
{
  // Set up callbacks to emulator
  get_pc_callback_ = [&emu]()
  {
    return emu.getCPUState().pc;
  };

  memory_read_callback_ = [&emu](uint16_t addr)
  {
    return emu.peekMemory(addr);
  };

  step_over_callback_ = [&emu]()
  {
    emu.stepOver();
  };

  step_out_callback_ = [&emu]()
  {
    emu.stepOut();
  };

  run_callback_ = [&emu]()
  {
    emu.resume();
  };

  pause_callback_ = [&emu]()
  {
    emu.pause();
  };

  is_paused_callback_ = [&emu]()
  {
    return emu.isPaused();
  };

  get_breakpoint_mgr_callback_ = [&emu]()
  {
    return emu.getBreakpointManager();
  };
}

void debugger_window::update(float deltaTime)
{
  if (!open_)
  {
    return;
  }

  uint16_t pc = get_pc_callback_();
  bool pc_changed = (pc != current_pc_);

  // Auto-follow PC if enabled
  if (auto_follow_pc_ && pc_changed)
  {
    scroll_to_address_ = pc;
    scroll_pending_ = true;
  }

  current_pc_ = pc;

  // Update disassembly cache
  if (auto_follow_pc_ && pc_changed)
  {
    // When auto-following, keep cache centered on PC
    updateDisassemblyCache(pc);
  }
  else if (disasm_cache_.empty())
  {
    // Initialize cache if empty
    updateDisassemblyCache(auto_follow_pc_ ? pc : cache_center_address_);
  }
  else
  {
    // Just update the is_current_pc flag without re-centering
    for (auto& line : disasm_cache_)
    {
      line.is_current_pc = (line.address == pc);
    }
  }
}

void debugger_window::render()
{
  if (!open_)
  {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(320, 280), ImVec2(FLT_MAX, FLT_MAX));

  if (ImGui::Begin(getName(), &open_))
  {
    renderControls();
    ImGui::Separator();
    renderDisassembly();
    if (show_breakpoints_)
    {
      ImGui::Separator();
      renderBreakpoints();
    }
  }
  ImGui::End();
}

void debugger_window::renderControls()
{
  bool is_paused = is_paused_callback_();
  float available_width = ImGui::GetContentRegionAvail().x;
  bool is_narrow = available_width < 380;

  // Row 1: Execution controls
  // Run/Pause toggle (F5) - primary action first
  if (is_paused)
  {
    if (ImGui::Button("Run") || ImGui::IsKeyPressed(ImGuiKey_F5))
    {
      auto_follow_pc_ = true;
      run_callback_();
    }
  }
  else
  {
    if (ImGui::Button("Pause") || ImGui::IsKeyPressed(ImGuiKey_F5))
    {
      pause_callback_();
    }
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("F5");

  ImGui::SameLine();

  // Step Over button (F10)
  if (ImGui::Button("Step") || (is_paused && ImGui::IsKeyPressed(ImGuiKey_F10)))
  {
    auto_follow_pc_ = true;
    step_over_callback_();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step Over (F10)");

  ImGui::SameLine();

  // Step Out button (F11)
  if (ImGui::Button("Out") || (is_paused && ImGui::IsKeyPressed(ImGuiKey_F11)))
  {
    auto_follow_pc_ = true;
    step_out_callback_();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step Out (F11)");

  ImGui::SameLine();

  // Current PC display - always visible
  ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "$%04X", current_pc_);

  // Follow checkbox - wrap if narrow
  if (!is_narrow) ImGui::SameLine();
  ImGui::Checkbox("Follow", &auto_follow_pc_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-follow Program Counter");

  // Go to address - same line if wide enough, otherwise wrap
  if (!is_narrow) ImGui::SameLine();

  ImGui::SetNextItemWidth(is_narrow ? -1 : 55);
  static char address_buf[5] = "0000";
  bool enter_pressed = ImGui::InputText("##goto", address_buf, sizeof(address_buf),
      ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase |
      ImGuiInputTextFlags_EnterReturnsTrue);

  if (!is_narrow) ImGui::SameLine();

  if (ImGui::Button("Go") || enter_pressed)
  {
    uint16_t addr = static_cast<uint16_t>(std::strtoul(address_buf, nullptr, 16));
    scroll_to_address_ = addr;
    scroll_pending_ = true;
    auto_follow_pc_ = false;
    updateDisassemblyCache(addr);
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to address");

  ImGui::SameLine();
  if (ImGui::Button(show_breakpoints_ ? "BP" : "bp"))
  {
    show_breakpoints_ = !show_breakpoints_;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(show_breakpoints_ ? "Hide breakpoints" : "Show breakpoints");
}

void debugger_window::renderDisassembly()
{
  float available_height = ImGui::GetContentRegionAvail().y;
  float splitter_height;
  float splitter_size = 6.0f;

  if (show_breakpoints_)
  {
    // Proportional splitter for disassembly/breakpoints sections
    float min_disasm = 100.0f;
    float min_breakpoints = 60.0f;

    float max_ratio = (available_height - min_breakpoints - splitter_size) / available_height;
    float min_ratio = min_disasm / available_height;
    splitter_ratio_ = std::clamp(splitter_ratio_, min_ratio, max_ratio);
    splitter_height = available_height * splitter_ratio_;
  }
  else
  {
    // Use full height when breakpoints hidden
    splitter_height = available_height;
  }

  if (ImGui::BeginChild("DisassemblyView", ImVec2(0, splitter_height), true))
  {
    // Detect user scrolling when paused - disable auto-follow
    bool is_paused = is_paused_callback_();
    if (is_paused && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
    {
      auto_follow_pc_ = false;
    }

    float table_width = ImGui::GetContentRegionAvail().x;
    bool show_bytes = table_width > 340;
    bool show_rw = table_width > 280;
    int num_columns = 2 + (show_bytes ? 1 : 0) + (show_rw ? 1 : 0) + 1;  // BP + Addr + optional + Instruction

    if (ImGui::BeginTable("Disassembly", num_columns,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                         ImGuiTableFlags_NoPadOuterX))
    {
      ImGui::TableSetupColumn("##bp", ImGuiTableColumnFlags_WidthFixed, 20.0f);
      ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 50.0f);
      if (show_bytes)
        ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 72.0f);
      ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
      if (show_rw)
        ImGui::TableSetupColumn("##rw", ImGuiTableColumnFlags_WidthFixed, 28.0f);

      for (const auto& line : disasm_cache_)
      {
        ImGui::TableNextRow();

        // Scroll to this row if needed (either pending scroll or auto-follow PC)
        if (scroll_pending_ && line.address == scroll_to_address_)
        {
          ImGui::SetScrollHereY(0.5f);  // Center the row in view
          scroll_pending_ = false;
        }
        else if (auto_follow_pc_ && line.is_current_pc)
        {
          // Always keep PC row centered when auto-follow is enabled
          ImGui::SetScrollHereY(0.5f);
        }

        // Highlight current PC
        if (line.is_current_pc)
        {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.0f, 0.5f)));
        }

        // Breakpoint column - compact
        ImGui::TableNextColumn();
        if (line.has_exec_breakpoint)
        {
          ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "\u25CF");
        }
        if (ImGui::IsItemClicked())
        {
          handleBreakpointClick(line.address, breakpoint_type::EXECUTION);
        }

        // Address column - color indicates watchpoints when R/W column hidden
        ImGui::TableNextColumn();
        ImVec4 addr_color(0.6f, 0.6f, 1.0f, 1.0f);  // Default blue
        if (!show_rw)
        {
          // Show watchpoint status via address color when narrow
          if (line.has_read_breakpoint && line.has_write_breakpoint)
            addr_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange for R/W
          else if (line.has_read_breakpoint)
            addr_color = ImVec4(0.3f, 1.0f, 0.6f, 1.0f);  // Green for read
          else if (line.has_write_breakpoint)
            addr_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange for write
        }
        ImGui::TextColored(addr_color, "$%04X", line.address);

        // Bytes column - only if wide enough
        if (show_bytes)
        {
          ImGui::TableNextColumn();
          char bytes_str[12];
          switch (line.byte_count)
          {
            case 1: std::snprintf(bytes_str, sizeof(bytes_str), "%02X", line.bytes[0]); break;
            case 2: std::snprintf(bytes_str, sizeof(bytes_str), "%02X %02X", line.bytes[0], line.bytes[1]); break;
            case 3: std::snprintf(bytes_str, sizeof(bytes_str), "%02X %02X %02X", line.bytes[0], line.bytes[1], line.bytes[2]); break;
            default: bytes_str[0] = '\0'; break;
          }
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", bytes_str);
        }

        // Instruction column
        ImGui::TableNextColumn();
        if (line.is_current_pc)
        {
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "%s", line.instruction.c_str());
        }
        else
        {
          ImGui::TextUnformatted(line.instruction.c_str());
        }

        // Read/Write watchpoint column - only if wide enough
        if (show_rw)
        {
          ImGui::TableNextColumn();
          bool has_r = line.has_read_breakpoint;
          bool has_w = line.has_write_breakpoint;
          if (has_r && has_w)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "RW");
          else if (has_r)
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "R");
          else if (has_w)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "W");
        }
      }

      ImGui::EndTable();
    }
  }
  ImGui::EndChild();

  // Resizable splitter bar - only show when breakpoints visible
  if (show_breakpoints_)
  {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
    ImGui::Button("##splitter", ImVec2(-1, splitter_size));
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemActive() && available_height > 0)
    {
      splitter_ratio_ += ImGui::GetIO().MouseDelta.y / available_height;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
  }
}

void debugger_window::renderBreakpoints()
{
  if (ImGui::CollapsingHeader("Breakpoints", ImGuiTreeNodeFlags_DefaultOpen))
  {
    breakpoint_manager* bp_mgr = get_breakpoint_mgr_callback_();
    if (!bp_mgr)
    {
      return;
    }

    const auto& breakpoints = bp_mgr->getBreakpoints();

    if (breakpoints.empty())
    {
      ImGui::TextDisabled("No breakpoints. Click BP column to add.");
    }
    else
    {
      float bp_table_width = ImGui::GetContentRegionAvail().x;
      bool show_type_col = bp_table_width > 220;

      if (ImGui::BeginTable("BreakpointList", show_type_col ? 4 : 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoPadOuterX))
      {
        ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        if (show_type_col)
          ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("##en", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthStretch);

        std::vector<std::pair<uint16_t, breakpoint_type>> to_remove;

        for (const auto& bp : breakpoints)
        {
          ImGui::TableNextRow();

          // Address column - color-coded by type when type column hidden
          ImGui::TableNextColumn();
          ImVec4 type_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red for exec
          if (bp.type == breakpoint_type::READ)
            type_color = ImVec4(0.3f, 1.0f, 0.6f, 1.0f);  // Green for read
          else if (bp.type == breakpoint_type::WRITE)
            type_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange for write

          char addr_buf[8];
          std::snprintf(addr_buf, sizeof(addr_buf), "$%04X", bp.address);

          ImGui::PushStyleColor(ImGuiCol_Text, show_type_col ? ImVec4(0.6f, 0.6f, 1.0f, 1.0f) : type_color);
          if (ImGui::Selectable(addr_buf, false, ImGuiSelectableFlags_AllowOverlap))
          {
            scroll_to_address_ = bp.address;
            scroll_pending_ = true;
            auto_follow_pc_ = false;
          }
          ImGui::PopStyleColor();

          // Type column - only if wide enough
          if (show_type_col)
          {
            ImGui::TableNextColumn();
            const char* type_str = "X";
            if (bp.type == breakpoint_type::READ) type_str = "R";
            else if (bp.type == breakpoint_type::WRITE) type_str = "W";
            ImGui::TextColored(type_color, "%s", type_str);
          }

          // Enable checkbox
          ImGui::TableNextColumn();
          bool enabled = bp.enabled;
          char checkbox_id[32];
          std::snprintf(checkbox_id, sizeof(checkbox_id), "##en_%04X_%d", bp.address, static_cast<int>(bp.type));
          if (ImGui::Checkbox(checkbox_id, &enabled))
          {
            bp_mgr->setEnabled(bp.address, bp.type, enabled);
          }

          // Delete button
          ImGui::TableNextColumn();
          char remove_id[32];
          std::snprintf(remove_id, sizeof(remove_id), "X##%04X_%d", bp.address, static_cast<int>(bp.type));
          if (ImGui::SmallButton(remove_id))
          {
            to_remove.push_back({bp.address, bp.type});
          }
        }

        for (const auto& [addr, type] : to_remove)
        {
          bp_mgr->removeBreakpoint(addr, type);
          updateDisassemblyCache(cache_center_address_);
        }

        ImGui::EndTable();
      }
    }
  }
}

void debugger_window::updateDisassemblyCache(uint16_t center_address)
{
  disasm_cache_.clear();
  cache_center_address_ = center_address;

  breakpoint_manager* bp_mgr = get_breakpoint_mgr_callback_();

  // To center the target address, we need to:
  // 1. Disassemble from well before the center to find instruction boundaries
  // 2. Then select lines to center the target address

  // Start from far enough back to ensure we have enough lines before center
  // Use ~128 bytes back as a safe margin (covers ~40+ instructions minimum)
  uint16_t scan_start = center_address;
  if (center_address > 128)
  {
    scan_start = center_address - 128;
  }
  else
  {
    scan_start = 0;
  }

  // First pass: disassemble forward to build a larger buffer
  std::vector<disasm_line> temp_cache;
  uint16_t address = scan_start;
  int max_scan_lines = disasm_lines_ * 3;  // Scan more than we need

  for (int i = 0; i < max_scan_lines && address <= 0xFFFF; ++i)
  {
    uint8_t opcode = memory_read_callback_(address);
    uint8_t op1 = memory_read_callback_(static_cast<uint16_t>(address + 1));
    uint8_t op2 = memory_read_callback_(static_cast<uint16_t>(address + 2));

    const auto& opcode_info = MOS6502::OPCODE_TABLE_CMOS[opcode];
    int byte_count = opcode_info.bytes;

    std::string instr = MOS6502::Disassembler::disassemble_instruction(
        address, opcode, op1, op2, MOS6502::CPUVariant::CMOS_65C02);

    disasm_line line;
    line.address = address;
    line.bytes[0] = opcode;
    line.bytes[1] = op1;
    line.bytes[2] = op2;
    line.byte_count = byte_count;
    line.instruction = extractMnemonic(instr);
    line.is_current_pc = (address == current_pc_);
    line.has_exec_breakpoint = bp_mgr ? bp_mgr->hasBreakpoint(address, breakpoint_type::EXECUTION) : false;
    line.has_read_breakpoint = bp_mgr ? bp_mgr->hasBreakpoint(address, breakpoint_type::READ) : false;
    line.has_write_breakpoint = bp_mgr ? bp_mgr->hasBreakpoint(address, breakpoint_type::WRITE) : false;

    temp_cache.push_back(line);

    address += byte_count;
    if (address == 0)
    {
      break;  // Wrapped around
    }
  }

  // Find the index of the center address in temp_cache
  int center_index = -1;
  for (int i = 0; i < static_cast<int>(temp_cache.size()); ++i)
  {
    if (temp_cache[i].address == center_address)
    {
      center_index = i;
      break;
    }
    // If we passed the center address (it wasn't on an instruction boundary),
    // use the instruction that contains it
    if (temp_cache[i].address > center_address && center_index < 0)
    {
      center_index = (i > 0) ? i - 1 : 0;
      break;
    }
  }

  if (center_index < 0)
  {
    center_index = 0;
  }

  // Calculate start index to center the target address
  int half_lines = disasm_lines_ / 2;
  int start_index = center_index - half_lines;
  if (start_index < 0)
  {
    start_index = 0;
  }

  // Extract the final cache centered on the target
  int end_index = start_index + disasm_lines_;
  if (end_index > static_cast<int>(temp_cache.size()))
  {
    end_index = static_cast<int>(temp_cache.size());
    // Adjust start if we don't have enough lines after
    start_index = std::max(0, end_index - disasm_lines_);
  }

  for (int i = start_index; i < end_index; ++i)
  {
    disasm_cache_.push_back(temp_cache[i]);
  }
}

void debugger_window::handleBreakpointClick(uint16_t address, breakpoint_type type)
{
  breakpoint_manager* bp_mgr = get_breakpoint_mgr_callback_();
  if (bp_mgr)
  {
    bp_mgr->toggleBreakpoint(address, type);
    // Force cache update to reflect breakpoint change
    updateDisassemblyCache(cache_center_address_);
  }
}
