#include "ui/debugger_window.hpp"
#include "emulator/emulator.hpp"
#include "emulator/breakpoint_manager.hpp"
#include <MOS6502/Disassembler/Disassembler.hpp>
#include <MOS6502/Disassembler/OpcodeTable.hpp>
#include <imgui.h>
#include <cstdio>
#include <cmath>

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

  ImGui::SetNextWindowSize(ImVec2(700, 800), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(getName(), &open_))
  {
    renderControls();
    ImGui::Separator();
    renderDisassembly();
    ImGui::Separator();
    renderBreakpoints();
  }
  ImGui::End();
}

void debugger_window::renderControls()
{
  bool is_paused = is_paused_callback_();

  // Step Over button (F10)
  if (ImGui::Button("Step Over (F10)") || (is_paused && ImGui::IsKeyPressed(ImGuiKey_F10)))
  {
    auto_follow_pc_ = true;  // Re-enable following when stepping
    step_over_callback_();
  }

  ImGui::SameLine();

  // Run/Pause toggle (F5)
  if (is_paused)
  {
    if (ImGui::Button("Run (F5)") || ImGui::IsKeyPressed(ImGuiKey_F5))
    {
      auto_follow_pc_ = true;  // Re-enable following when running
      run_callback_();
    }
  }
  else
  {
    if (ImGui::Button("Pause (F5)") || ImGui::IsKeyPressed(ImGuiKey_F5))
    {
      pause_callback_();
    }
  }

  ImGui::SameLine();

  // Step Out button (F11)
  if (ImGui::Button("Step Out (F11)") || (is_paused && ImGui::IsKeyPressed(ImGuiKey_F11)))
  {
    auto_follow_pc_ = true;  // Re-enable following when stepping
    step_out_callback_();
  }

  ImGui::SameLine();

  // Auto-follow PC checkbox
  ImGui::Checkbox("Auto-follow PC", &auto_follow_pc_);

  // Current PC display
  ImGui::SameLine();
  ImGui::Text("PC: $%04X", current_pc_);

  // Go to address input
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  static char address_buf[5] = "0000";
  if (ImGui::InputText("##goto", address_buf, sizeof(address_buf),
                      ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase))
  {
    // Input changed - could validate here
  }
  ImGui::SameLine();
  if (ImGui::Button("Go to Address"))
  {
    // Parse hex address
    uint16_t addr = static_cast<uint16_t>(std::strtoul(address_buf, nullptr, 16));
    scroll_to_address_ = addr;
    scroll_pending_ = true;
    auto_follow_pc_ = false;  // Disable auto-follow when manually navigating
    updateDisassemblyCache(addr);
  }
}

void debugger_window::renderDisassembly()
{
  // Resizable splitter for disassembly/breakpoints sections
  static float splitter_height = 450.0f;
  float window_height = ImGui::GetContentRegionAvail().y;

  // Clamp splitter to reasonable bounds
  if (splitter_height < 200.0f) splitter_height = 200.0f;
  if (splitter_height > window_height - 200.0f) splitter_height = window_height - 200.0f;

  if (ImGui::BeginChild("DisassemblyView", ImVec2(0, splitter_height), true))
  {
    // Detect user scrolling when paused - disable auto-follow
    bool is_paused = is_paused_callback_();
    if (is_paused && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
    {
      auto_follow_pc_ = false;
    }

    if (ImGui::BeginTable("Disassembly", 5,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
    {
      ImGui::TableSetupColumn("BP", ImGuiTableColumnFlags_WidthFixed, 40.0f);
      ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 70.0f);
      ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 100.0f);
      ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("R/W", ImGuiTableColumnFlags_WidthFixed, 50.0f);
      ImGui::TableHeadersRow();

      for (const auto& line : disasm_cache_)
      {
        ImGui::TableNextRow();

        // Scroll to this row if needed
        if (scroll_pending_ && line.address == scroll_to_address_)
        {
          ImGui::SetScrollHereY(0.5f);  // Center the row in view
          scroll_pending_ = false;
        }

        // Highlight current PC
        if (line.is_current_pc)
        {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.0f, 0.5f)));
        }

        // Execution breakpoint column
        ImGui::TableNextColumn();
        if (line.has_exec_breakpoint)
        {
          ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "  \u25CF");  // Red filled circle
        }
        else
        {
          ImGui::TextUnformatted("   ");
        }

        // Click to toggle execution breakpoint
        if (ImGui::IsItemClicked())
        {
          handleBreakpointClick(line.address, breakpoint_type::EXECUTION);
        }

        // Address column
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "$%04X", line.address);

        // Bytes column
        ImGui::TableNextColumn();
        char bytes_str[16] = "";
        if (line.byte_count >= 1)
        {
          std::snprintf(bytes_str, sizeof(bytes_str), "%02X", line.bytes[0]);
        }
        if (line.byte_count >= 2)
        {
          std::snprintf(bytes_str + std::strlen(bytes_str),
                       sizeof(bytes_str) - std::strlen(bytes_str),
                       " %02X", line.bytes[1]);
        }
        if (line.byte_count >= 3)
        {
          std::snprintf(bytes_str + std::strlen(bytes_str),
                       sizeof(bytes_str) - std::strlen(bytes_str),
                       " %02X", line.bytes[2]);
        }
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%-9s", bytes_str);

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

        // Read/Write watchpoint column
        ImGui::TableNextColumn();
        bool has_r = line.has_read_breakpoint;
        bool has_w = line.has_write_breakpoint;

        if (has_r || has_w)
        {
          if (has_r && has_w)
          {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "R/W");
          }
          else if (has_r)
          {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "R");
          }
          else
          {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "W");
          }
        }
        else
        {
          ImGui::TextUnformatted("");
        }
      }

      ImGui::EndTable();
    }
  }
  ImGui::EndChild();

  // Resizable splitter bar
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
  ImGui::Button("##splitter", ImVec2(-1, 4.0f));
  ImGui::PopStyleColor(3);

  if (ImGui::IsItemActive())
  {
    splitter_height += ImGui::GetIO().MouseDelta.y;
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
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
      ImGui::TextDisabled("No breakpoints set");
      ImGui::Text("Click on the BP column in disassembly to add execution breakpoints");
    }
    else
    {
      if (ImGui::BeginTable("BreakpointList", 4,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
      {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        std::vector<std::pair<uint16_t, breakpoint_type>> to_remove;

        for (const auto& bp : breakpoints)
        {
          ImGui::TableNextRow();

          ImGui::TableNextColumn();
          char addr_buf[16];
          std::snprintf(addr_buf, sizeof(addr_buf), "$%04X", bp.address);
          if (ImGui::Selectable(addr_buf, false,
                               ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
          {
            scroll_to_address_ = bp.address;
            scroll_pending_ = true;
            auto_follow_pc_ = false;
          }

          ImGui::TableNextColumn();
          const char* type_str = "Exec";
          if (bp.type == breakpoint_type::READ)
            type_str = "Read";
          else if (bp.type == breakpoint_type::WRITE)
            type_str = "Write";

          ImVec4 type_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);  // Red for exec
          if (bp.type == breakpoint_type::READ)
            type_color = ImVec4(0.0f, 1.0f, 0.5f, 1.0f);  // Green for read
          else if (bp.type == breakpoint_type::WRITE)
            type_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange for write

          ImGui::TextColored(type_color, "%s", type_str);

          ImGui::TableNextColumn();
          bool enabled = bp.enabled;
          char checkbox_id[32];
          std::snprintf(checkbox_id, sizeof(checkbox_id), "##enabled_%04X_%d",
                       bp.address, static_cast<int>(bp.type));
          if (ImGui::Checkbox(checkbox_id, &enabled))
          {
            bp_mgr->setEnabled(bp.address, bp.type, enabled);
          }

          ImGui::TableNextColumn();
          char remove_id[32];
          std::snprintf(remove_id, sizeof(remove_id), "Remove##%04X_%d",
                       bp.address, static_cast<int>(bp.type));
          if (ImGui::SmallButton(remove_id))
          {
            to_remove.push_back({bp.address, bp.type});
          }
        }

        // Remove marked breakpoints
        for (const auto& [addr, type] : to_remove)
        {
          bp_mgr->removeBreakpoint(addr, type);
          // Force cache update
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

  // Start a few instructions before center
  // Estimate 3 bytes per instruction, but we'll adjust as we disassemble
  uint16_t address = center_address;
  if (center_address > (disasm_lines_ / 2) * 3)
  {
    address = center_address - (disasm_lines_ / 2) * 3;
  }
  else
  {
    address = 0;
  }

  for (int i = 0; i < disasm_lines_ && address < 0xFFFF; ++i)
  {
    uint8_t opcode = memory_read_callback_(address);
    uint8_t op1 = memory_read_callback_(static_cast<uint16_t>(address + 1));
    uint8_t op2 = memory_read_callback_(static_cast<uint16_t>(address + 2));

    // Get instruction info from opcode table
    const auto& opcode_info = MOS6502::OPCODE_TABLE_CMOS[opcode];
    int byte_count = opcode_info.bytes;

    // Disassemble using MOS6502 disassembler
    std::string instr = MOS6502::Disassembler::disassemble_instruction(
        address, opcode, op1, op2, MOS6502::CPUVariant::CMOS_65C02);

    disasm_line line;
    line.address = address;
    line.bytes[0] = opcode;
    line.bytes[1] = op1;
    line.bytes[2] = op2;
    line.byte_count = byte_count;
    line.instruction = instr;
    line.is_current_pc = (address == current_pc_);
    line.has_exec_breakpoint = bp_mgr ? bp_mgr->hasBreakpoint(address, breakpoint_type::EXECUTION) : false;
    line.has_read_breakpoint = bp_mgr ? bp_mgr->hasBreakpoint(address, breakpoint_type::READ) : false;
    line.has_write_breakpoint = bp_mgr ? bp_mgr->hasBreakpoint(address, breakpoint_type::WRITE) : false;

    disasm_cache_.push_back(line);

    // Move to next instruction
    address += byte_count;
    if (address == 0)
    {
      break;  // Wrapped around
    }
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
