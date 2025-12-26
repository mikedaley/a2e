#include "ui/cpu_window.hpp"
#include "emulator/emulator.hpp"
#include <imgui.h>
#include <cstdio>
#include <cstring>

// Status flag bit positions
static constexpr uint8_t FLAG_C = 0x01; // Carry
static constexpr uint8_t FLAG_Z = 0x02; // Zero
static constexpr uint8_t FLAG_I = 0x04; // Interrupt Disable
static constexpr uint8_t FLAG_D = 0x08; // Decimal Mode
static constexpr uint8_t FLAG_B = 0x10; // Break
static constexpr uint8_t FLAG_U = 0x20; // Unused (always 1)
static constexpr uint8_t FLAG_V = 0x40; // Overflow
static constexpr uint8_t FLAG_N = 0x80; // Negative

// Opcode information for disassembly
struct OpcodeInfo
{
  const char *mnemonic;
  int bytes;
  const char *mode; // Addressing mode format
};

// Simplified opcode table for 65C02
static const OpcodeInfo OPCODES[256] = {
    {"BRK", 1, ""},        {"ORA", 2, "($%02X,X)"}, {"???", 1, ""},          {"???", 1, ""},
    {"TSB", 2, "$%02X"},   {"ORA", 2, "$%02X"},     {"ASL", 2, "$%02X"},     {"RMB0", 2, "$%02X"},
    {"PHP", 1, ""},        {"ORA", 2, "#$%02X"},    {"ASL", 1, "A"},         {"???", 1, ""},
    {"TSB", 3, "$%04X"},   {"ORA", 3, "$%04X"},     {"ASL", 3, "$%04X"},     {"BBR0", 3, "$%02X,$%04X"},
    {"BPL", 2, "$%04X"},   {"ORA", 2, "($%02X),Y"}, {"ORA", 2, "($%02X)"},   {"???", 1, ""},
    {"TRB", 2, "$%02X"},   {"ORA", 2, "$%02X,X"},   {"ASL", 2, "$%02X,X"},   {"RMB1", 2, "$%02X"},
    {"CLC", 1, ""},        {"ORA", 3, "$%04X,Y"},   {"INC", 1, "A"},         {"???", 1, ""},
    {"TRB", 3, "$%04X"},   {"ORA", 3, "$%04X,X"},   {"ASL", 3, "$%04X,X"},   {"BBR1", 3, "$%02X,$%04X"},
    {"JSR", 3, "$%04X"},   {"AND", 2, "($%02X,X)"}, {"???", 1, ""},          {"???", 1, ""},
    {"BIT", 2, "$%02X"},   {"AND", 2, "$%02X"},     {"ROL", 2, "$%02X"},     {"RMB2", 2, "$%02X"},
    {"PLP", 1, ""},        {"AND", 2, "#$%02X"},    {"ROL", 1, "A"},         {"???", 1, ""},
    {"BIT", 3, "$%04X"},   {"AND", 3, "$%04X"},     {"ROL", 3, "$%04X"},     {"BBR2", 3, "$%02X,$%04X"},
    {"BMI", 2, "$%04X"},   {"AND", 2, "($%02X),Y"}, {"AND", 2, "($%02X)"},   {"???", 1, ""},
    {"BIT", 2, "$%02X,X"}, {"AND", 2, "$%02X,X"},   {"ROL", 2, "$%02X,X"},   {"RMB3", 2, "$%02X"},
    {"SEC", 1, ""},        {"AND", 3, "$%04X,Y"},   {"DEC", 1, "A"},         {"???", 1, ""},
    {"BIT", 3, "$%04X,X"}, {"AND", 3, "$%04X,X"},   {"ROL", 3, "$%04X,X"},   {"BBR3", 3, "$%02X,$%04X"},
    {"RTI", 1, ""},        {"EOR", 2, "($%02X,X)"}, {"???", 1, ""},          {"???", 1, ""},
    {"???", 1, ""},        {"EOR", 2, "$%02X"},     {"LSR", 2, "$%02X"},     {"RMB4", 2, "$%02X"},
    {"PHA", 1, ""},        {"EOR", 2, "#$%02X"},    {"LSR", 1, "A"},         {"???", 1, ""},
    {"JMP", 3, "$%04X"},   {"EOR", 3, "$%04X"},     {"LSR", 3, "$%04X"},     {"BBR4", 3, "$%02X,$%04X"},
    {"BVC", 2, "$%04X"},   {"EOR", 2, "($%02X),Y"}, {"EOR", 2, "($%02X)"},   {"???", 1, ""},
    {"???", 1, ""},        {"EOR", 2, "$%02X,X"},   {"LSR", 2, "$%02X,X"},   {"RMB5", 2, "$%02X"},
    {"CLI", 1, ""},        {"EOR", 3, "$%04X,Y"},   {"PHY", 1, ""},          {"???", 1, ""},
    {"???", 1, ""},        {"EOR", 3, "$%04X,X"},   {"LSR", 3, "$%04X,X"},   {"BBR5", 3, "$%02X,$%04X"},
    {"RTS", 1, ""},        {"ADC", 2, "($%02X,X)"}, {"???", 1, ""},          {"???", 1, ""},
    {"STZ", 2, "$%02X"},   {"ADC", 2, "$%02X"},     {"ROR", 2, "$%02X"},     {"RMB6", 2, "$%02X"},
    {"PLA", 1, ""},        {"ADC", 2, "#$%02X"},    {"ROR", 1, "A"},         {"???", 1, ""},
    {"JMP", 3, "($%04X)"}, {"ADC", 3, "$%04X"},     {"ROR", 3, "$%04X"},     {"BBR6", 3, "$%02X,$%04X"},
    {"BVS", 2, "$%04X"},   {"ADC", 2, "($%02X),Y"}, {"ADC", 2, "($%02X)"},   {"???", 1, ""},
    {"STZ", 2, "$%02X,X"}, {"ADC", 2, "$%02X,X"},   {"ROR", 2, "$%02X,X"},   {"RMB7", 2, "$%02X"},
    {"SEI", 1, ""},        {"ADC", 3, "$%04X,Y"},   {"PLY", 1, ""},          {"???", 1, ""},
    {"JMP", 3, "($%04X,X)"},{"ADC", 3, "$%04X,X"},  {"ROR", 3, "$%04X,X"},   {"BBR7", 3, "$%02X,$%04X"},
    {"BRA", 2, "$%04X"},   {"STA", 2, "($%02X,X)"}, {"???", 1, ""},          {"???", 1, ""},
    {"STY", 2, "$%02X"},   {"STA", 2, "$%02X"},     {"STX", 2, "$%02X"},     {"SMB0", 2, "$%02X"},
    {"DEY", 1, ""},        {"BIT", 2, "#$%02X"},    {"TXA", 1, ""},          {"???", 1, ""},
    {"STY", 3, "$%04X"},   {"STA", 3, "$%04X"},     {"STX", 3, "$%04X"},     {"BBS0", 3, "$%02X,$%04X"},
    {"BCC", 2, "$%04X"},   {"STA", 2, "($%02X),Y"}, {"STA", 2, "($%02X)"},   {"???", 1, ""},
    {"STY", 2, "$%02X,X"}, {"STA", 2, "$%02X,X"},   {"STX", 2, "$%02X,Y"},   {"SMB1", 2, "$%02X"},
    {"TYA", 1, ""},        {"STA", 3, "$%04X,Y"},   {"TXS", 1, ""},          {"???", 1, ""},
    {"STZ", 3, "$%04X"},   {"STA", 3, "$%04X,X"},   {"STZ", 3, "$%04X,X"},   {"BBS1", 3, "$%02X,$%04X"},
    {"LDY", 2, "#$%02X"},  {"LDA", 2, "($%02X,X)"}, {"LDX", 2, "#$%02X"},    {"???", 1, ""},
    {"LDY", 2, "$%02X"},   {"LDA", 2, "$%02X"},     {"LDX", 2, "$%02X"},     {"SMB2", 2, "$%02X"},
    {"TAY", 1, ""},        {"LDA", 2, "#$%02X"},    {"TAX", 1, ""},          {"???", 1, ""},
    {"LDY", 3, "$%04X"},   {"LDA", 3, "$%04X"},     {"LDX", 3, "$%04X"},     {"BBS2", 3, "$%02X,$%04X"},
    {"BCS", 2, "$%04X"},   {"LDA", 2, "($%02X),Y"}, {"LDA", 2, "($%02X)"},   {"???", 1, ""},
    {"LDY", 2, "$%02X,X"}, {"LDA", 2, "$%02X,X"},   {"LDX", 2, "$%02X,Y"},   {"SMB3", 2, "$%02X"},
    {"CLV", 1, ""},        {"LDA", 3, "$%04X,Y"},   {"TSX", 1, ""},          {"???", 1, ""},
    {"LDY", 3, "$%04X,X"}, {"LDA", 3, "$%04X,X"},   {"LDX", 3, "$%04X,Y"},   {"BBS3", 3, "$%02X,$%04X"},
    {"CPY", 2, "#$%02X"},  {"CMP", 2, "($%02X,X)"}, {"???", 1, ""},          {"???", 1, ""},
    {"CPY", 2, "$%02X"},   {"CMP", 2, "$%02X"},     {"DEC", 2, "$%02X"},     {"SMB4", 2, "$%02X"},
    {"INY", 1, ""},        {"CMP", 2, "#$%02X"},    {"DEX", 1, ""},          {"WAI", 1, ""},
    {"CPY", 3, "$%04X"},   {"CMP", 3, "$%04X"},     {"DEC", 3, "$%04X"},     {"BBS4", 3, "$%02X,$%04X"},
    {"BNE", 2, "$%04X"},   {"CMP", 2, "($%02X),Y"}, {"CMP", 2, "($%02X)"},   {"???", 1, ""},
    {"???", 1, ""},        {"CMP", 2, "$%02X,X"},   {"DEC", 2, "$%02X,X"},   {"SMB5", 2, "$%02X"},
    {"CLD", 1, ""},        {"CMP", 3, "$%04X,Y"},   {"PHX", 1, ""},          {"STP", 1, ""},
    {"???", 1, ""},        {"CMP", 3, "$%04X,X"},   {"DEC", 3, "$%04X,X"},   {"BBS5", 3, "$%02X,$%04X"},
    {"CPX", 2, "#$%02X"},  {"SBC", 2, "($%02X,X)"}, {"???", 1, ""},          {"???", 1, ""},
    {"CPX", 2, "$%02X"},   {"SBC", 2, "$%02X"},     {"INC", 2, "$%02X"},     {"SMB6", 2, "$%02X"},
    {"INX", 1, ""},        {"SBC", 2, "#$%02X"},    {"NOP", 1, ""},          {"???", 1, ""},
    {"CPX", 3, "$%04X"},   {"SBC", 3, "$%04X"},     {"INC", 3, "$%04X"},     {"BBS6", 3, "$%02X,$%04X"},
    {"BEQ", 2, "$%04X"},   {"SBC", 2, "($%02X),Y"}, {"SBC", 2, "($%02X)"},   {"???", 1, ""},
    {"???", 1, ""},        {"SBC", 2, "$%02X,X"},   {"INC", 2, "$%02X,X"},   {"SMB7", 2, "$%02X"},
    {"SED", 1, ""},        {"SBC", 3, "$%04X,Y"},   {"PLX", 1, ""},          {"???", 1, ""},
    {"???", 1, ""},        {"SBC", 3, "$%04X,X"},   {"INC", 3, "$%04X,X"},   {"BBS7", 3, "$%02X,$%04X"},
};

cpu_window::cpu_window(emulator& emu)
{
  // Set up CPU state callback
  cpu_state_callback_ = [&emu]() -> cpu_state
  {
    auto emu_state = emu.getCPUState();
    cpu_state state;
    state.pc = emu_state.pc;
    state.sp = emu_state.sp;
    state.p = emu_state.p;
    state.a = emu_state.a;
    state.x = emu_state.x;
    state.y = emu_state.y;
    state.total_cycles = emu_state.total_cycles;
    state.initialized = emu_state.initialized;
    return state;
  };

  // Set up memory read callback for stack/disassembly display
  memory_read_callback_ = [&emu](uint16_t address) -> uint8_t
  {
    return emu.readMemory(address);
  };
}

void cpu_window::update([[maybe_unused]] float deltaTime)
{
  if (!open_ || !cpu_state_callback_)
  {
    return;
  }

  prev_state_ = state_;
  state_ = cpu_state_callback_();
}

std::string cpu_window::formatBinary8(uint8_t value)
{
  char buf[9];
  for (int i = 7; i >= 0; --i)
  {
    buf[7 - i] = (value & (1 << i)) ? '1' : '0';
  }
  buf[8] = '\0';
  return std::string(buf);
}

std::string cpu_window::formatBinary16(uint16_t value)
{
  char buf[17];
  for (int i = 15; i >= 0; --i)
  {
    buf[15 - i] = (value & (1 << i)) ? '1' : '0';
  }
  buf[16] = '\0';
  return std::string(buf);
}

std::string cpu_window::disassembleInstruction(uint16_t address)
{
  if (!memory_read_callback_)
  {
    return "???";
  }

  uint8_t opcode = memory_read_callback_(address);
  const OpcodeInfo &info = OPCODES[opcode];
  
  char buf[64];
  
  if (info.bytes == 1)
  {
    if (strlen(info.mode) > 0)
    {
      snprintf(buf, sizeof(buf), "%s %s", info.mnemonic, info.mode);
    }
    else
    {
      snprintf(buf, sizeof(buf), "%s", info.mnemonic);
    }
  }
  else if (info.bytes == 2)
  {
    uint8_t op1 = memory_read_callback_(address + 1);
    // Handle relative branches - calculate target address
    if (opcode == 0x10 || opcode == 0x30 || opcode == 0x50 || opcode == 0x70 ||
        opcode == 0x90 || opcode == 0xB0 || opcode == 0xD0 || opcode == 0xF0 ||
        opcode == 0x80) // BRA
    {
      int8_t offset = static_cast<int8_t>(op1);
      uint16_t target = address + 2 + offset;
      snprintf(buf, sizeof(buf), "%s $%04X", info.mnemonic, target);
    }
    else
    {
      char operand[32];
      snprintf(operand, sizeof(operand), info.mode, op1);
      snprintf(buf, sizeof(buf), "%s %s", info.mnemonic, operand);
    }
  }
  else if (info.bytes == 3)
  {
    uint8_t op1 = memory_read_callback_(address + 1);
    uint8_t op2 = memory_read_callback_(address + 2);
    uint16_t addr16 = static_cast<uint16_t>(op1) | (static_cast<uint16_t>(op2) << 8);
    char operand[32];
    snprintf(operand, sizeof(operand), info.mode, addr16);
    snprintf(buf, sizeof(buf), "%s %s", info.mnemonic, operand);
  }
  else
  {
    snprintf(buf, sizeof(buf), "???");
  }

  return std::string(buf);
}

void cpu_window::render()
{
  if (!open_)
  {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(getName(), &open_, ImGuiWindowFlags_MenuBar))
  {
    // Menu bar for options
    if (ImGui::BeginMenuBar())
    {
      if (ImGui::BeginMenu("View"))
      {
        ImGui::MenuItem("Show Binary", nullptr, &show_binary_);
        ImGui::MenuItem("Show Decimal", nullptr, &show_decimal_);
        ImGui::MenuItem("Show Stack", nullptr, &show_stack_);
        ImGui::MenuItem("Show Disassembly", nullptr, &show_disasm_);
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    if (!state_.initialized)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "CPU not initialized");
      ImGui::End();
      return;
    }

    // Collapsing sections
    renderRegisters();
    
    ImGui::Spacing();
    renderStatusFlags();
    
    ImGui::Spacing();
    renderPerformance();

    if (show_stack_)
    {
      ImGui::Spacing();
      renderStack();
    }

    if (show_disasm_)
    {
      ImGui::Spacing();
      renderDisassembly();
    }
  }
  ImGui::End();
}

void cpu_window::renderRegisters()
{
  ImGui::SetNextItemOpen(registers_section_open_, ImGuiCond_Once);
  if ((registers_section_open_ = ImGui::CollapsingHeader("Registers")))
  {
    // Color for changed values
    ImVec4 changedColor(0.0f, 1.0f, 0.5f, 1.0f);
    ImVec4 normalColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Create a table for clean register display
    if (ImGui::BeginTable("RegisterTable", show_binary_ ? 4 : (show_decimal_ ? 3 : 2), 
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
      // Header
      ImGui::TableSetupColumn("Reg", ImGuiTableColumnFlags_WidthFixed, 40.0f);
      ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      if (show_decimal_)
        ImGui::TableSetupColumn("Dec", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      if (show_binary_)
        ImGui::TableSetupColumn("Binary", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      // Program Counter (16-bit)
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("PC");
      ImGui::TableNextColumn();
      ImGui::TextColored(state_.pc != prev_state_.pc ? changedColor : normalColor, "$%04X", state_.pc);
      if (show_decimal_)
      {
        ImGui::TableNextColumn();
        ImGui::Text("%5u", state_.pc);
      }
      if (show_binary_)
      {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(formatBinary16(state_.pc).c_str());
      }

      // Accumulator
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("A");
      ImGui::TableNextColumn();
      ImGui::TextColored(state_.a != prev_state_.a ? changedColor : normalColor, "$%02X", state_.a);
      if (show_decimal_)
      {
        ImGui::TableNextColumn();
        ImGui::Text("%3u", state_.a);
      }
      if (show_binary_)
      {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(formatBinary8(state_.a).c_str());
      }

      // X Register
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("X");
      ImGui::TableNextColumn();
      ImGui::TextColored(state_.x != prev_state_.x ? changedColor : normalColor, "$%02X", state_.x);
      if (show_decimal_)
      {
        ImGui::TableNextColumn();
        ImGui::Text("%3u", state_.x);
      }
      if (show_binary_)
      {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(formatBinary8(state_.x).c_str());
      }

      // Y Register
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Y");
      ImGui::TableNextColumn();
      ImGui::TextColored(state_.y != prev_state_.y ? changedColor : normalColor, "$%02X", state_.y);
      if (show_decimal_)
      {
        ImGui::TableNextColumn();
        ImGui::Text("%3u", state_.y);
      }
      if (show_binary_)
      {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(formatBinary8(state_.y).c_str());
      }

      // Stack Pointer
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("SP");
      ImGui::TableNextColumn();
      ImGui::TextColored(state_.sp != prev_state_.sp ? changedColor : normalColor, "$%02X", state_.sp);
      if (show_decimal_)
      {
        ImGui::TableNextColumn();
        ImGui::Text("%3u", state_.sp);
      }
      if (show_binary_)
      {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(formatBinary8(state_.sp).c_str());
      }

      ImGui::EndTable();
    }
  }
}

void cpu_window::renderStatusFlags()
{
  ImGui::SetNextItemOpen(flags_section_open_, ImGuiCond_Once);
  if ((flags_section_open_ = ImGui::CollapsingHeader("Status Flags (P)")))
  {
    // Show raw P register value
    ImGui::Text("P = $%02X  (%s)", state_.p, formatBinary8(state_.p).c_str());
    
    ImGui::Spacing();

    // Visual flag display with colored indicators
    ImVec4 setColor(0.0f, 1.0f, 0.3f, 1.0f);     // Green for set
    ImVec4 clearColor(0.5f, 0.5f, 0.5f, 1.0f);   // Gray for clear
    ImVec4 changedColor(1.0f, 1.0f, 0.0f, 1.0f); // Yellow for changed

    struct FlagInfo
    {
      const char *name;
      const char *fullName;
      uint8_t mask;
    };

    static const FlagInfo flags[] = {
        {"N", "Negative", FLAG_N},
        {"V", "Overflow", FLAG_V},
        {"-", "Unused", FLAG_U},
        {"B", "Break", FLAG_B},
        {"D", "Decimal", FLAG_D},
        {"I", "IRQ Disable", FLAG_I},
        {"Z", "Zero", FLAG_Z},
        {"C", "Carry", FLAG_C},
    };

    // Draw flags in a row with visual indicators
    for (int i = 0; i < 8; ++i)
    {
      if (i > 0)
        ImGui::SameLine();

      bool isSet = (state_.p & flags[i].mask) != 0;
      bool wasSet = (prev_state_.p & flags[i].mask) != 0;
      bool changed = isSet != wasSet;

      ImVec4 color = changed ? changedColor : (isSet ? setColor : clearColor);
      
      // Create a button-like appearance for each flag
      ImGui::PushStyleColor(ImGuiCol_Button, isSet ? ImVec4(0.2f, 0.5f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSet ? ImVec4(0.3f, 0.6f, 0.3f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      
      ImGui::Button(flags[i].name, ImVec2(30, 25));
      
      if (ImGui::IsItemHovered())
      {
        ImGui::BeginTooltip();
        ImGui::Text("%s: %s", flags[i].fullName, isSet ? "SET" : "CLEAR");
        ImGui::EndTooltip();
      }
      
      ImGui::PopStyleColor(3);
    }

    // Show flag explanations
    ImGui::Spacing();
    ImGui::TextDisabled("N=Negative V=Overflow B=Break D=Decimal I=IRQ Z=Zero C=Carry");
  }
}

void cpu_window::renderStack()
{
  ImGui::SetNextItemOpen(stack_section_open_, ImGuiCond_Once);
  if ((stack_section_open_ = ImGui::CollapsingHeader("Stack")))
  {
    if (!memory_read_callback_)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Memory callback not set");
      return;
    }

    ImGui::Text("Stack Pointer: $01%02X (depth: %d bytes)", state_.sp, 0xFF - state_.sp);
    ImGui::Spacing();

    // Show top of stack (from SP+1 to $01FF)
    int stack_depth = 0xFF - state_.sp;
    int lines_to_show = stack_depth < 8 ? stack_depth : 8;

    if (lines_to_show == 0)
    {
      ImGui::TextDisabled("Stack is empty");
    }
    else
    {
      if (ImGui::BeginTable("StackTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
      {
        ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < lines_to_show; ++i)
        {
          uint16_t addr = 0x0100 + state_.sp + 1 + i;
          uint8_t value = memory_read_callback_(addr);
          char ascii = (value >= 32 && value < 127) ? static_cast<char>(value) : '.';

          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::Text("$%04X", addr);
          ImGui::TableNextColumn();
          ImGui::Text("$%02X", value);
          ImGui::TableNextColumn();
          ImGui::Text("%c", ascii);
        }
        
        if (stack_depth > 8)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextDisabled("... %d more", stack_depth - 8);
          ImGui::TableNextColumn();
          ImGui::TableNextColumn();
        }

        ImGui::EndTable();
      }
    }
  }
}

void cpu_window::renderDisassembly()
{
  ImGui::SetNextItemOpen(disasm_section_open_, ImGuiCond_Once);
  if ((disasm_section_open_ = ImGui::CollapsingHeader("Disassembly")))
  {
    if (!memory_read_callback_)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Memory callback not set");
      return;
    }

    ImGui::SliderInt("Lines", &disasm_lines_, 5, 20);
    ImGui::Spacing();

    // Current instruction highlight
    ImVec4 currentColor(1.0f, 1.0f, 0.3f, 1.0f);
    ImVec4 normalColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 addrColor(0.6f, 0.6f, 1.0f, 1.0f);
    ImVec4 bytesColor(0.5f, 0.5f, 0.5f, 1.0f);

    uint16_t addr = state_.pc;

    for (int i = 0; i < disasm_lines_; ++i)
    {
      uint8_t opcode = memory_read_callback_(addr);
      const OpcodeInfo &info = OPCODES[opcode];
      
      bool isCurrent = (i == 0);
      
      // Address
      if (isCurrent)
      {
        ImGui::TextColored(currentColor, ">");
        ImGui::SameLine();
      }
      else
      {
        ImGui::TextUnformatted(" ");
        ImGui::SameLine();
      }
      
      ImGui::TextColored(addrColor, "$%04X:", addr);
      ImGui::SameLine();

      // Bytes
      char bytes_str[16] = "";
      if (info.bytes >= 1)
        snprintf(bytes_str, sizeof(bytes_str), "%02X", opcode);
      if (info.bytes >= 2)
        snprintf(bytes_str + strlen(bytes_str), sizeof(bytes_str) - strlen(bytes_str), " %02X", memory_read_callback_(addr + 1));
      if (info.bytes >= 3)
        snprintf(bytes_str + strlen(bytes_str), sizeof(bytes_str) - strlen(bytes_str), " %02X", memory_read_callback_(addr + 2));
      
      ImGui::TextColored(bytesColor, "%-9s", bytes_str);
      ImGui::SameLine();

      // Instruction
      std::string instr = disassembleInstruction(addr);
      ImGui::TextColored(isCurrent ? currentColor : normalColor, "%s", instr.c_str());

      addr += info.bytes;
    }
  }
}

void cpu_window::renderPerformance()
{
  ImGui::SetNextItemOpen(perf_section_open_, ImGuiCond_Once);
  if ((perf_section_open_ = ImGui::CollapsingHeader("Performance")))
  {
    // Calculate cycles per second
    float current_time = ImGui::GetTime();
    float delta_time = current_time - last_update_time_;
    
    if (delta_time >= 0.5f) // Update every 500ms
    {
      uint64_t delta_cycles = state_.total_cycles - last_cycle_count_;
      cycles_per_second_ = static_cast<float>(delta_cycles) / delta_time;
      last_cycle_count_ = state_.total_cycles;
      last_update_time_ = current_time;
    }

    ImGui::Text("Total Cycles: %llu", static_cast<unsigned long long>(state_.total_cycles));
    ImGui::Text("Speed: %.2f MHz", cycles_per_second_ / 1000000.0f);
    
    // Show target speed comparison
    float target_mhz = 1.023f; // Apple IIe runs at ~1.023 MHz
    float speed_percent = (cycles_per_second_ / 1000000.0f) / target_mhz * 100.0f;
    
    ImVec4 speedColor;
    if (speed_percent >= 95.0f && speed_percent <= 105.0f)
      speedColor = ImVec4(0.0f, 1.0f, 0.3f, 1.0f); // Green - on target
    else if (speed_percent >= 80.0f)
      speedColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow - slightly off
    else
      speedColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red - way off
    
    ImGui::TextColored(speedColor, "%.1f%% of target (1.023 MHz)", speed_percent);

    // Progress bar for speed
    ImGui::ProgressBar(speed_percent / 100.0f, ImVec2(-1, 0), "");
  }
}

void cpu_window::loadState(preferences& prefs)
{
  registers_section_open_ = prefs.getBool("window.cpu.registers_open", true);
  flags_section_open_ = prefs.getBool("window.cpu.flags_open", true);
  stack_section_open_ = prefs.getBool("window.cpu.stack_open", true);
  disasm_section_open_ = prefs.getBool("window.cpu.disasm_open", true);
  perf_section_open_ = prefs.getBool("window.cpu.perf_open", true);
  show_binary_ = prefs.getBool("window.cpu.show_binary", true);
  show_decimal_ = prefs.getBool("window.cpu.show_decimal", true);
  show_stack_ = prefs.getBool("window.cpu.show_stack", false);
  show_disasm_ = prefs.getBool("window.cpu.show_disasm", false);
}

void cpu_window::saveState(preferences& prefs)
{
  prefs.setBool("window.cpu.registers_open", registers_section_open_);
  prefs.setBool("window.cpu.flags_open", flags_section_open_);
  prefs.setBool("window.cpu.stack_open", stack_section_open_);
  prefs.setBool("window.cpu.disasm_open", disasm_section_open_);
  prefs.setBool("window.cpu.perf_open", perf_section_open_);
  prefs.setBool("window.cpu.show_binary", show_binary_);
  prefs.setBool("window.cpu.show_decimal", show_decimal_);
  prefs.setBool("window.cpu.show_stack", show_stack_);
  prefs.setBool("window.cpu.show_disasm", show_disasm_);
}
