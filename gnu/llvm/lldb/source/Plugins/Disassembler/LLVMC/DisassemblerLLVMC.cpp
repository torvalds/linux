//===-- DisassemblerLLVMC.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DisassemblerLLVMC.h"

#include "llvm-c/Disassembler.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCDisassembler/MCExternalSymbolizer.h"
#include "llvm/MC/MCDisassembler/MCRelocationInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/AArch64TargetParser.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(DisassemblerLLVMC)

class DisassemblerLLVMC::MCDisasmInstance {
public:
  static std::unique_ptr<MCDisasmInstance>
  Create(const char *triple, const char *cpu, const char *features_str,
         unsigned flavor, DisassemblerLLVMC &owner);

  ~MCDisasmInstance() = default;

  uint64_t GetMCInst(const uint8_t *opcode_data, size_t opcode_data_len,
                     lldb::addr_t pc, llvm::MCInst &mc_inst) const;
  void PrintMCInst(llvm::MCInst &mc_inst, lldb::addr_t pc,
                   std::string &inst_string, std::string &comments_string);
  void SetStyle(bool use_hex_immed, HexImmediateStyle hex_style);
  void SetUseColor(bool use_color);
  bool GetUseColor() const;
  bool CanBranch(llvm::MCInst &mc_inst) const;
  bool HasDelaySlot(llvm::MCInst &mc_inst) const;
  bool IsCall(llvm::MCInst &mc_inst) const;
  bool IsLoad(llvm::MCInst &mc_inst) const;
  bool IsAuthenticated(llvm::MCInst &mc_inst) const;

private:
  MCDisasmInstance(std::unique_ptr<llvm::MCInstrInfo> &&instr_info_up,
                   std::unique_ptr<llvm::MCRegisterInfo> &&reg_info_up,
                   std::unique_ptr<llvm::MCSubtargetInfo> &&subtarget_info_up,
                   std::unique_ptr<llvm::MCAsmInfo> &&asm_info_up,
                   std::unique_ptr<llvm::MCContext> &&context_up,
                   std::unique_ptr<llvm::MCDisassembler> &&disasm_up,
                   std::unique_ptr<llvm::MCInstPrinter> &&instr_printer_up,
                   std::unique_ptr<llvm::MCInstrAnalysis> &&instr_analysis_up);

  std::unique_ptr<llvm::MCInstrInfo> m_instr_info_up;
  std::unique_ptr<llvm::MCRegisterInfo> m_reg_info_up;
  std::unique_ptr<llvm::MCSubtargetInfo> m_subtarget_info_up;
  std::unique_ptr<llvm::MCAsmInfo> m_asm_info_up;
  std::unique_ptr<llvm::MCContext> m_context_up;
  std::unique_ptr<llvm::MCDisassembler> m_disasm_up;
  std::unique_ptr<llvm::MCInstPrinter> m_instr_printer_up;
  std::unique_ptr<llvm::MCInstrAnalysis> m_instr_analysis_up;
};

namespace x86 {

/// These are the three values deciding instruction control flow kind.
/// InstructionLengthDecode function decodes an instruction and get this struct.
///
/// primary_opcode
///    Primary opcode of the instruction.
///    For one-byte opcode instruction, it's the first byte after prefix.
///    For two- and three-byte opcodes, it's the second byte.
///
/// opcode_len
///    The length of opcode in bytes. Valid opcode lengths are 1, 2, or 3.
///
/// modrm
///    ModR/M byte of the instruction.
///    Bits[7:6] indicate MOD. Bits[5:3] specify a register and R/M bits[2:0]
///    may contain a register or specify an addressing mode, depending on MOD.
struct InstructionOpcodeAndModrm {
  uint8_t primary_opcode;
  uint8_t opcode_len;
  uint8_t modrm;
};

/// Determine the InstructionControlFlowKind based on opcode and modrm bytes.
/// Refer to http://ref.x86asm.net/coder.html for the full list of opcode and
/// instruction set.
///
/// \param[in] opcode_and_modrm
///    Contains primary_opcode byte, its length, and ModR/M byte.
///    Refer to the struct InstructionOpcodeAndModrm for details.
///
/// \return
///   The control flow kind of the instruction or
///   eInstructionControlFlowKindOther if the instruction doesn't affect
///   the control flow of the program.
lldb::InstructionControlFlowKind
MapOpcodeIntoControlFlowKind(InstructionOpcodeAndModrm opcode_and_modrm) {
  uint8_t opcode = opcode_and_modrm.primary_opcode;
  uint8_t opcode_len = opcode_and_modrm.opcode_len;
  uint8_t modrm = opcode_and_modrm.modrm;

  if (opcode_len > 2)
    return lldb::eInstructionControlFlowKindOther;

  if (opcode >= 0x70 && opcode <= 0x7F) {
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindCondJump;
    else
      return lldb::eInstructionControlFlowKindOther;
  }

  if (opcode >= 0x80 && opcode <= 0x8F) {
    if (opcode_len == 2)
      return lldb::eInstructionControlFlowKindCondJump;
    else
      return lldb::eInstructionControlFlowKindOther;
  }

  switch (opcode) {
  case 0x9A:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindFarCall;
    break;
  case 0xFF:
    if (opcode_len == 1) {
      uint8_t modrm_reg = (modrm >> 3) & 7;
      if (modrm_reg == 2)
        return lldb::eInstructionControlFlowKindCall;
      else if (modrm_reg == 3)
        return lldb::eInstructionControlFlowKindFarCall;
      else if (modrm_reg == 4)
        return lldb::eInstructionControlFlowKindJump;
      else if (modrm_reg == 5)
        return lldb::eInstructionControlFlowKindFarJump;
    }
    break;
  case 0xE8:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindCall;
    break;
  case 0xCD:
  case 0xCC:
  case 0xCE:
  case 0xF1:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindFarCall;
    break;
  case 0xCF:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindFarReturn;
    break;
  case 0xE9:
  case 0xEB:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindJump;
    break;
  case 0xEA:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindFarJump;
    break;
  case 0xE3:
  case 0xE0:
  case 0xE1:
  case 0xE2:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindCondJump;
    break;
  case 0xC3:
  case 0xC2:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindReturn;
    break;
  case 0xCB:
  case 0xCA:
    if (opcode_len == 1)
      return lldb::eInstructionControlFlowKindFarReturn;
    break;
  case 0x05:
  case 0x34:
    if (opcode_len == 2)
      return lldb::eInstructionControlFlowKindFarCall;
    break;
  case 0x35:
  case 0x07:
    if (opcode_len == 2)
      return lldb::eInstructionControlFlowKindFarReturn;
    break;
  case 0x01:
    if (opcode_len == 2) {
      switch (modrm) {
      case 0xc1:
        return lldb::eInstructionControlFlowKindFarCall;
      case 0xc2:
      case 0xc3:
        return lldb::eInstructionControlFlowKindFarReturn;
      default:
        break;
      }
    }
    break;
  default:
    break;
  }

  return lldb::eInstructionControlFlowKindOther;
}

/// Decode an instruction into opcode, modrm and opcode_len.
/// Refer to http://ref.x86asm.net/coder.html for the instruction bytes layout.
/// Opcodes in x86 are generally the first byte of instruction, though two-byte
/// instructions and prefixes exist. ModR/M is the byte following the opcode
/// and adds additional information for how the instruction is executed.
///
/// \param[in] inst_bytes
///    Raw bytes of the instruction
///
///
/// \param[in] bytes_len
///    The length of the inst_bytes array.
///
/// \param[in] is_exec_mode_64b
///    If true, the execution mode is 64 bit.
///
/// \return
///    Returns decoded instruction as struct InstructionOpcodeAndModrm, holding
///    primary_opcode, opcode_len and modrm byte. Refer to the struct definition
///    for more details.
///    Otherwise if the given instruction is invalid, returns std::nullopt.
std::optional<InstructionOpcodeAndModrm>
InstructionLengthDecode(const uint8_t *inst_bytes, int bytes_len,
                        bool is_exec_mode_64b) {
  int op_idx = 0;
  bool prefix_done = false;
  InstructionOpcodeAndModrm ret = {0, 0, 0};

  // In most cases, the primary_opcode is the first byte of the instruction
  // but some instructions have a prefix to be skipped for these calculations.
  // The following mapping is inspired from libipt's instruction decoding logic
  // in `src/pt_ild.c`
  while (!prefix_done) {
    if (op_idx >= bytes_len)
      return std::nullopt;

    ret.primary_opcode = inst_bytes[op_idx];
    switch (ret.primary_opcode) {
    // prefix_ignore
    case 0x26:
    case 0x2e:
    case 0x36:
    case 0x3e:
    case 0x64:
    case 0x65:
    // prefix_osz, prefix_asz
    case 0x66:
    case 0x67:
    // prefix_lock, prefix_f2, prefix_f3
    case 0xf0:
    case 0xf2:
    case 0xf3:
      op_idx++;
      break;

    // prefix_rex
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4a:
    case 0x4b:
    case 0x4c:
    case 0x4d:
    case 0x4e:
    case 0x4f:
      if (is_exec_mode_64b)
        op_idx++;
      else
        prefix_done = true;
      break;

    // prefix_vex_c4, c5
    case 0xc5:
      if (!is_exec_mode_64b && (inst_bytes[op_idx + 1] & 0xc0) != 0xc0) {
        prefix_done = true;
        break;
      }

      ret.opcode_len = 2;
      ret.primary_opcode = inst_bytes[op_idx + 2];
      ret.modrm = inst_bytes[op_idx + 3];
      return ret;

    case 0xc4:
      if (!is_exec_mode_64b && (inst_bytes[op_idx + 1] & 0xc0) != 0xc0) {
        prefix_done = true;
        break;
      }
      ret.opcode_len = inst_bytes[op_idx + 1] & 0x1f;
      ret.primary_opcode = inst_bytes[op_idx + 3];
      ret.modrm = inst_bytes[op_idx + 4];
      return ret;

    // prefix_evex
    case 0x62:
      if (!is_exec_mode_64b && (inst_bytes[op_idx + 1] & 0xc0) != 0xc0) {
        prefix_done = true;
        break;
      }
      ret.opcode_len = inst_bytes[op_idx + 1] & 0x03;
      ret.primary_opcode = inst_bytes[op_idx + 4];
      ret.modrm = inst_bytes[op_idx + 5];
      return ret;

    default:
      prefix_done = true;
      break;
    }
  } // prefix done

  ret.primary_opcode = inst_bytes[op_idx];
  ret.modrm = inst_bytes[op_idx + 1];
  ret.opcode_len = 1;

  // If the first opcode is 0F, it's two- or three- byte opcodes.
  if (ret.primary_opcode == 0x0F) {
    ret.primary_opcode = inst_bytes[++op_idx]; // get the next byte

    if (ret.primary_opcode == 0x38) {
      ret.opcode_len = 3;
      ret.primary_opcode = inst_bytes[++op_idx]; // get the next byte
      ret.modrm = inst_bytes[op_idx + 1];
    } else if (ret.primary_opcode == 0x3A) {
      ret.opcode_len = 3;
      ret.primary_opcode = inst_bytes[++op_idx];
      ret.modrm = inst_bytes[op_idx + 1];
    } else if ((ret.primary_opcode & 0xf8) == 0x38) {
      ret.opcode_len = 0;
      ret.primary_opcode = inst_bytes[++op_idx];
      ret.modrm = inst_bytes[op_idx + 1];
    } else if (ret.primary_opcode == 0x0F) {
      ret.opcode_len = 3;
      // opcode is 0x0F, no needs to update
      ret.modrm = inst_bytes[op_idx + 1];
    } else {
      ret.opcode_len = 2;
      ret.modrm = inst_bytes[op_idx + 1];
    }
  }

  return ret;
}

lldb::InstructionControlFlowKind GetControlFlowKind(bool is_exec_mode_64b,
                                                    Opcode m_opcode) {
  std::optional<InstructionOpcodeAndModrm> ret;

  if (m_opcode.GetOpcodeBytes() == nullptr || m_opcode.GetByteSize() <= 0) {
    // x86_64 and i386 instructions are categorized as Opcode::Type::eTypeBytes
    return lldb::eInstructionControlFlowKindUnknown;
  }

  // Opcode bytes will be decoded into primary_opcode, modrm and opcode length.
  // These are the three values deciding instruction control flow kind.
  ret = InstructionLengthDecode((const uint8_t *)m_opcode.GetOpcodeBytes(),
                                m_opcode.GetByteSize(), is_exec_mode_64b);
  if (!ret)
    return lldb::eInstructionControlFlowKindUnknown;
  else
    return MapOpcodeIntoControlFlowKind(*ret);
}

} // namespace x86

class InstructionLLVMC : public lldb_private::Instruction {
public:
  InstructionLLVMC(DisassemblerLLVMC &disasm,
                   const lldb_private::Address &address,
                   AddressClass addr_class)
      : Instruction(address, addr_class),
        m_disasm_wp(std::static_pointer_cast<DisassemblerLLVMC>(
            disasm.shared_from_this())) {}

  ~InstructionLLVMC() override = default;

  bool DoesBranch() override {
    VisitInstruction();
    return m_does_branch;
  }

  bool HasDelaySlot() override {
    VisitInstruction();
    return m_has_delay_slot;
  }

  bool IsLoad() override {
    VisitInstruction();
    return m_is_load;
  }

  bool IsAuthenticated() override {
    VisitInstruction();
    return m_is_authenticated;
  }

  DisassemblerLLVMC::MCDisasmInstance *GetDisasmToUse(bool &is_alternate_isa) {
    DisassemblerScope disasm(*this);
    return GetDisasmToUse(is_alternate_isa, disasm);
  }

  size_t Decode(const lldb_private::Disassembler &disassembler,
                const lldb_private::DataExtractor &data,
                lldb::offset_t data_offset) override {
    // All we have to do is read the opcode which can be easy for some
    // architectures
    bool got_op = false;
    DisassemblerScope disasm(*this);
    if (disasm) {
      const ArchSpec &arch = disasm->GetArchitecture();
      const lldb::ByteOrder byte_order = data.GetByteOrder();

      const uint32_t min_op_byte_size = arch.GetMinimumOpcodeByteSize();
      const uint32_t max_op_byte_size = arch.GetMaximumOpcodeByteSize();
      if (min_op_byte_size == max_op_byte_size) {
        // Fixed size instructions, just read that amount of data.
        if (!data.ValidOffsetForDataOfSize(data_offset, min_op_byte_size))
          return false;

        switch (min_op_byte_size) {
        case 1:
          m_opcode.SetOpcode8(data.GetU8(&data_offset), byte_order);
          got_op = true;
          break;

        case 2:
          m_opcode.SetOpcode16(data.GetU16(&data_offset), byte_order);
          got_op = true;
          break;

        case 4:
          m_opcode.SetOpcode32(data.GetU32(&data_offset), byte_order);
          got_op = true;
          break;

        case 8:
          m_opcode.SetOpcode64(data.GetU64(&data_offset), byte_order);
          got_op = true;
          break;

        default:
          m_opcode.SetOpcodeBytes(data.PeekData(data_offset, min_op_byte_size),
                                  min_op_byte_size);
          got_op = true;
          break;
        }
      }
      if (!got_op) {
        bool is_alternate_isa = false;
        DisassemblerLLVMC::MCDisasmInstance *mc_disasm_ptr =
            GetDisasmToUse(is_alternate_isa, disasm);

        const llvm::Triple::ArchType machine = arch.GetMachine();
        if (machine == llvm::Triple::arm || machine == llvm::Triple::thumb) {
          if (machine == llvm::Triple::thumb || is_alternate_isa) {
            uint32_t thumb_opcode = data.GetU16(&data_offset);
            if ((thumb_opcode & 0xe000) != 0xe000 ||
                ((thumb_opcode & 0x1800u) == 0)) {
              m_opcode.SetOpcode16(thumb_opcode, byte_order);
              m_is_valid = true;
            } else {
              thumb_opcode <<= 16;
              thumb_opcode |= data.GetU16(&data_offset);
              m_opcode.SetOpcode16_2(thumb_opcode, byte_order);
              m_is_valid = true;
            }
          } else {
            m_opcode.SetOpcode32(data.GetU32(&data_offset), byte_order);
            m_is_valid = true;
          }
        } else {
          // The opcode isn't evenly sized, so we need to actually use the llvm
          // disassembler to parse it and get the size.
          uint8_t *opcode_data =
              const_cast<uint8_t *>(data.PeekData(data_offset, 1));
          const size_t opcode_data_len = data.BytesLeft(data_offset);
          const addr_t pc = m_address.GetFileAddress();
          llvm::MCInst inst;

          const size_t inst_size =
              mc_disasm_ptr->GetMCInst(opcode_data, opcode_data_len, pc, inst);
          if (inst_size == 0)
            m_opcode.Clear();
          else {
            m_opcode.SetOpcodeBytes(opcode_data, inst_size);
            m_is_valid = true;
          }
        }
      }
      return m_opcode.GetByteSize();
    }
    return 0;
  }

  void AppendComment(std::string &description) {
    if (m_comment.empty())
      m_comment.swap(description);
    else {
      m_comment.append(", ");
      m_comment.append(description);
    }
  }

  lldb::InstructionControlFlowKind
  GetControlFlowKind(const lldb_private::ExecutionContext *exe_ctx) override {
    DisassemblerScope disasm(*this, exe_ctx);
    if (disasm){
      if (disasm->GetArchitecture().GetMachine() == llvm::Triple::x86)
        return x86::GetControlFlowKind(/*is_64b=*/false, m_opcode);
      else if (disasm->GetArchitecture().GetMachine() == llvm::Triple::x86_64)
        return x86::GetControlFlowKind(/*is_64b=*/true, m_opcode);
    }

    return eInstructionControlFlowKindUnknown;
  }

  void CalculateMnemonicOperandsAndComment(
      const lldb_private::ExecutionContext *exe_ctx) override {
    DataExtractor data;
    const AddressClass address_class = GetAddressClass();

    if (m_opcode.GetData(data)) {
      std::string out_string;
      std::string markup_out_string;
      std::string comment_string;
      std::string markup_comment_string;

      DisassemblerScope disasm(*this, exe_ctx);
      if (disasm) {
        DisassemblerLLVMC::MCDisasmInstance *mc_disasm_ptr;

        if (address_class == AddressClass::eCodeAlternateISA)
          mc_disasm_ptr = disasm->m_alternate_disasm_up.get();
        else
          mc_disasm_ptr = disasm->m_disasm_up.get();

        lldb::addr_t pc = m_address.GetFileAddress();
        m_using_file_addr = true;

        const bool data_from_file = disasm->m_data_from_file;
        bool use_hex_immediates = true;
        Disassembler::HexImmediateStyle hex_style = Disassembler::eHexStyleC;

        if (exe_ctx) {
          Target *target = exe_ctx->GetTargetPtr();
          if (target) {
            use_hex_immediates = target->GetUseHexImmediates();
            hex_style = target->GetHexImmediateStyle();

            if (!data_from_file) {
              const lldb::addr_t load_addr = m_address.GetLoadAddress(target);
              if (load_addr != LLDB_INVALID_ADDRESS) {
                pc = load_addr;
                m_using_file_addr = false;
              }
            }
          }
        }

        const uint8_t *opcode_data = data.GetDataStart();
        const size_t opcode_data_len = data.GetByteSize();
        llvm::MCInst inst;
        size_t inst_size =
            mc_disasm_ptr->GetMCInst(opcode_data, opcode_data_len, pc, inst);

        if (inst_size > 0) {
          mc_disasm_ptr->SetStyle(use_hex_immediates, hex_style);

          const bool saved_use_color = mc_disasm_ptr->GetUseColor();
          mc_disasm_ptr->SetUseColor(false);
          mc_disasm_ptr->PrintMCInst(inst, pc, out_string, comment_string);
          mc_disasm_ptr->SetUseColor(true);
          mc_disasm_ptr->PrintMCInst(inst, pc, markup_out_string,
                                     markup_comment_string);
          mc_disasm_ptr->SetUseColor(saved_use_color);

          if (!comment_string.empty()) {
            AppendComment(comment_string);
          }
        }

        if (inst_size == 0) {
          m_comment.assign("unknown opcode");
          inst_size = m_opcode.GetByteSize();
          StreamString mnemonic_strm;
          lldb::offset_t offset = 0;
          lldb::ByteOrder byte_order = data.GetByteOrder();
          switch (inst_size) {
          case 1: {
            const uint8_t uval8 = data.GetU8(&offset);
            m_opcode.SetOpcode8(uval8, byte_order);
            m_opcode_name.assign(".byte");
            mnemonic_strm.Printf("0x%2.2x", uval8);
          } break;
          case 2: {
            const uint16_t uval16 = data.GetU16(&offset);
            m_opcode.SetOpcode16(uval16, byte_order);
            m_opcode_name.assign(".short");
            mnemonic_strm.Printf("0x%4.4x", uval16);
          } break;
          case 4: {
            const uint32_t uval32 = data.GetU32(&offset);
            m_opcode.SetOpcode32(uval32, byte_order);
            m_opcode_name.assign(".long");
            mnemonic_strm.Printf("0x%8.8x", uval32);
          } break;
          case 8: {
            const uint64_t uval64 = data.GetU64(&offset);
            m_opcode.SetOpcode64(uval64, byte_order);
            m_opcode_name.assign(".quad");
            mnemonic_strm.Printf("0x%16.16" PRIx64, uval64);
          } break;
          default:
            if (inst_size == 0)
              return;
            else {
              const uint8_t *bytes = data.PeekData(offset, inst_size);
              if (bytes == nullptr)
                return;
              m_opcode_name.assign(".byte");
              m_opcode.SetOpcodeBytes(bytes, inst_size);
              mnemonic_strm.Printf("0x%2.2x", bytes[0]);
              for (uint32_t i = 1; i < inst_size; ++i)
                mnemonic_strm.Printf(" 0x%2.2x", bytes[i]);
            }
            break;
          }
          m_mnemonics = std::string(mnemonic_strm.GetString());
          return;
        }

        static RegularExpression s_regex(
            llvm::StringRef("[ \t]*([^ ^\t]+)[ \t]*([^ ^\t].*)?"));

        llvm::SmallVector<llvm::StringRef, 4> matches;
        if (s_regex.Execute(out_string, &matches)) {
          m_opcode_name = matches[1].str();
          m_mnemonics = matches[2].str();
        }
        matches.clear();
        if (s_regex.Execute(markup_out_string, &matches)) {
          m_markup_opcode_name = matches[1].str();
          m_markup_mnemonics = matches[2].str();
        }
      }
    }
  }

  bool IsValid() const { return m_is_valid; }

  bool UsingFileAddress() const { return m_using_file_addr; }
  size_t GetByteSize() const { return m_opcode.GetByteSize(); }

  /// Grants exclusive access to the disassembler and initializes it with the
  /// given InstructionLLVMC and an optional ExecutionContext.
  class DisassemblerScope {
    std::shared_ptr<DisassemblerLLVMC> m_disasm;

  public:
    explicit DisassemblerScope(
        InstructionLLVMC &i,
        const lldb_private::ExecutionContext *exe_ctx = nullptr)
        : m_disasm(i.m_disasm_wp.lock()) {
      m_disasm->m_mutex.lock();
      m_disasm->m_inst = &i;
      m_disasm->m_exe_ctx = exe_ctx;
    }
    ~DisassemblerScope() { m_disasm->m_mutex.unlock(); }

    /// Evaluates to true if this scope contains a valid disassembler.
    operator bool() const { return static_cast<bool>(m_disasm); }

    std::shared_ptr<DisassemblerLLVMC> operator->() { return m_disasm; }
  };

  static llvm::StringRef::const_iterator
  ConsumeWhitespace(llvm::StringRef::const_iterator osi,
                    llvm::StringRef::const_iterator ose) {
    while (osi != ose) {
      switch (*osi) {
      default:
        return osi;
      case ' ':
      case '\t':
        break;
      }
      ++osi;
    }

    return osi;
  }

  static std::pair<bool, llvm::StringRef::const_iterator>
  ConsumeChar(llvm::StringRef::const_iterator osi, const char c,
              llvm::StringRef::const_iterator ose) {
    bool found = false;

    osi = ConsumeWhitespace(osi, ose);
    if (osi != ose && *osi == c) {
      found = true;
      ++osi;
    }

    return std::make_pair(found, osi);
  }

  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseRegisterName(llvm::StringRef::const_iterator osi,
                    llvm::StringRef::const_iterator ose) {
    Operand ret;
    ret.m_type = Operand::Type::Register;
    std::string str;

    osi = ConsumeWhitespace(osi, ose);

    while (osi != ose) {
      if (*osi >= '0' && *osi <= '9') {
        if (str.empty()) {
          return std::make_pair(Operand(), osi);
        } else {
          str.push_back(*osi);
        }
      } else if (*osi >= 'a' && *osi <= 'z') {
        str.push_back(*osi);
      } else {
        switch (*osi) {
        default:
          if (str.empty()) {
            return std::make_pair(Operand(), osi);
          } else {
            ret.m_register = ConstString(str);
            return std::make_pair(ret, osi);
          }
        case '%':
          if (!str.empty()) {
            return std::make_pair(Operand(), osi);
          }
          break;
        }
      }
      ++osi;
    }

    ret.m_register = ConstString(str);
    return std::make_pair(ret, osi);
  }

  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseImmediate(llvm::StringRef::const_iterator osi,
                 llvm::StringRef::const_iterator ose) {
    Operand ret;
    ret.m_type = Operand::Type::Immediate;
    std::string str;
    bool is_hex = false;

    osi = ConsumeWhitespace(osi, ose);

    while (osi != ose) {
      if (*osi >= '0' && *osi <= '9') {
        str.push_back(*osi);
      } else if (*osi >= 'a' && *osi <= 'f') {
        if (is_hex) {
          str.push_back(*osi);
        } else {
          return std::make_pair(Operand(), osi);
        }
      } else {
        switch (*osi) {
        default:
          if (str.empty()) {
            return std::make_pair(Operand(), osi);
          } else {
            ret.m_immediate = strtoull(str.c_str(), nullptr, 0);
            return std::make_pair(ret, osi);
          }
        case 'x':
          if (!str.compare("0")) {
            is_hex = true;
            str.push_back(*osi);
          } else {
            return std::make_pair(Operand(), osi);
          }
          break;
        case '#':
        case '$':
          if (!str.empty()) {
            return std::make_pair(Operand(), osi);
          }
          break;
        case '-':
          if (str.empty()) {
            ret.m_negative = true;
          } else {
            return std::make_pair(Operand(), osi);
          }
        }
      }
      ++osi;
    }

    ret.m_immediate = strtoull(str.c_str(), nullptr, 0);
    return std::make_pair(ret, osi);
  }

  // -0x5(%rax,%rax,2)
  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseIntelIndexedAccess(llvm::StringRef::const_iterator osi,
                          llvm::StringRef::const_iterator ose) {
    std::pair<Operand, llvm::StringRef::const_iterator> offset_and_iterator =
        ParseImmediate(osi, ose);
    if (offset_and_iterator.first.IsValid()) {
      osi = offset_and_iterator.second;
    }

    bool found = false;
    std::tie(found, osi) = ConsumeChar(osi, '(', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> base_and_iterator =
        ParseRegisterName(osi, ose);
    if (base_and_iterator.first.IsValid()) {
      osi = base_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ',', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> index_and_iterator =
        ParseRegisterName(osi, ose);
    if (index_and_iterator.first.IsValid()) {
      osi = index_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ',', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator>
        multiplier_and_iterator = ParseImmediate(osi, ose);
    if (index_and_iterator.first.IsValid()) {
      osi = index_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ')', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    Operand product;
    product.m_type = Operand::Type::Product;
    product.m_children.push_back(index_and_iterator.first);
    product.m_children.push_back(multiplier_and_iterator.first);

    Operand index;
    index.m_type = Operand::Type::Sum;
    index.m_children.push_back(base_and_iterator.first);
    index.m_children.push_back(product);

    if (offset_and_iterator.first.IsValid()) {
      Operand offset;
      offset.m_type = Operand::Type::Sum;
      offset.m_children.push_back(offset_and_iterator.first);
      offset.m_children.push_back(index);

      Operand deref;
      deref.m_type = Operand::Type::Dereference;
      deref.m_children.push_back(offset);
      return std::make_pair(deref, osi);
    } else {
      Operand deref;
      deref.m_type = Operand::Type::Dereference;
      deref.m_children.push_back(index);
      return std::make_pair(deref, osi);
    }
  }

  // -0x10(%rbp)
  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseIntelDerefAccess(llvm::StringRef::const_iterator osi,
                        llvm::StringRef::const_iterator ose) {
    std::pair<Operand, llvm::StringRef::const_iterator> offset_and_iterator =
        ParseImmediate(osi, ose);
    if (offset_and_iterator.first.IsValid()) {
      osi = offset_and_iterator.second;
    }

    bool found = false;
    std::tie(found, osi) = ConsumeChar(osi, '(', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> base_and_iterator =
        ParseRegisterName(osi, ose);
    if (base_and_iterator.first.IsValid()) {
      osi = base_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ')', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    if (offset_and_iterator.first.IsValid()) {
      Operand offset;
      offset.m_type = Operand::Type::Sum;
      offset.m_children.push_back(offset_and_iterator.first);
      offset.m_children.push_back(base_and_iterator.first);

      Operand deref;
      deref.m_type = Operand::Type::Dereference;
      deref.m_children.push_back(offset);
      return std::make_pair(deref, osi);
    } else {
      Operand deref;
      deref.m_type = Operand::Type::Dereference;
      deref.m_children.push_back(base_and_iterator.first);
      return std::make_pair(deref, osi);
    }
  }

  // [sp, #8]!
  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseARMOffsetAccess(llvm::StringRef::const_iterator osi,
                       llvm::StringRef::const_iterator ose) {
    bool found = false;
    std::tie(found, osi) = ConsumeChar(osi, '[', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> base_and_iterator =
        ParseRegisterName(osi, ose);
    if (base_and_iterator.first.IsValid()) {
      osi = base_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ',', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> offset_and_iterator =
        ParseImmediate(osi, ose);
    if (offset_and_iterator.first.IsValid()) {
      osi = offset_and_iterator.second;
    }

    std::tie(found, osi) = ConsumeChar(osi, ']', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    Operand offset;
    offset.m_type = Operand::Type::Sum;
    offset.m_children.push_back(offset_and_iterator.first);
    offset.m_children.push_back(base_and_iterator.first);

    Operand deref;
    deref.m_type = Operand::Type::Dereference;
    deref.m_children.push_back(offset);
    return std::make_pair(deref, osi);
  }

  // [sp]
  static std::pair<Operand, llvm::StringRef::const_iterator>
  ParseARMDerefAccess(llvm::StringRef::const_iterator osi,
                      llvm::StringRef::const_iterator ose) {
    bool found = false;
    std::tie(found, osi) = ConsumeChar(osi, '[', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    std::pair<Operand, llvm::StringRef::const_iterator> base_and_iterator =
        ParseRegisterName(osi, ose);
    if (base_and_iterator.first.IsValid()) {
      osi = base_and_iterator.second;
    } else {
      return std::make_pair(Operand(), osi);
    }

    std::tie(found, osi) = ConsumeChar(osi, ']', ose);
    if (!found) {
      return std::make_pair(Operand(), osi);
    }

    Operand deref;
    deref.m_type = Operand::Type::Dereference;
    deref.m_children.push_back(base_and_iterator.first);
    return std::make_pair(deref, osi);
  }

  static void DumpOperand(const Operand &op, Stream &s) {
    switch (op.m_type) {
    case Operand::Type::Dereference:
      s.PutCString("*");
      DumpOperand(op.m_children[0], s);
      break;
    case Operand::Type::Immediate:
      if (op.m_negative) {
        s.PutCString("-");
      }
      s.PutCString(llvm::to_string(op.m_immediate));
      break;
    case Operand::Type::Invalid:
      s.PutCString("Invalid");
      break;
    case Operand::Type::Product:
      s.PutCString("(");
      DumpOperand(op.m_children[0], s);
      s.PutCString("*");
      DumpOperand(op.m_children[1], s);
      s.PutCString(")");
      break;
    case Operand::Type::Register:
      s.PutCString(op.m_register.GetStringRef());
      break;
    case Operand::Type::Sum:
      s.PutCString("(");
      DumpOperand(op.m_children[0], s);
      s.PutCString("+");
      DumpOperand(op.m_children[1], s);
      s.PutCString(")");
      break;
    }
  }

  bool ParseOperands(
      llvm::SmallVectorImpl<Instruction::Operand> &operands) override {
    const char *operands_string = GetOperands(nullptr);

    if (!operands_string) {
      return false;
    }

    llvm::StringRef operands_ref(operands_string);

    llvm::StringRef::const_iterator osi = operands_ref.begin();
    llvm::StringRef::const_iterator ose = operands_ref.end();

    while (osi != ose) {
      Operand operand;
      llvm::StringRef::const_iterator iter;

      if ((std::tie(operand, iter) = ParseIntelIndexedAccess(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseIntelDerefAccess(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseARMOffsetAccess(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseARMDerefAccess(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseRegisterName(osi, ose),
           operand.IsValid()) ||
          (std::tie(operand, iter) = ParseImmediate(osi, ose),
           operand.IsValid())) {
        osi = iter;
        operands.push_back(operand);
      } else {
        return false;
      }

      std::pair<bool, llvm::StringRef::const_iterator> found_and_iter =
          ConsumeChar(osi, ',', ose);
      if (found_and_iter.first) {
        osi = found_and_iter.second;
      }

      osi = ConsumeWhitespace(osi, ose);
    }

    DisassemblerSP disasm_sp = m_disasm_wp.lock();

    if (disasm_sp && operands.size() > 1) {
      // TODO tie this into the MC Disassembler's notion of clobbers.
      switch (disasm_sp->GetArchitecture().GetMachine()) {
      default:
        break;
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        operands[operands.size() - 1].m_clobbered = true;
        break;
      case llvm::Triple::arm:
        operands[0].m_clobbered = true;
        break;
      }
    }

    if (Log *log = GetLog(LLDBLog::Process)) {
      StreamString ss;

      ss.Printf("[%s] expands to %zu operands:\n", operands_string,
                operands.size());
      for (const Operand &operand : operands) {
        ss.PutCString("  ");
        DumpOperand(operand, ss);
        ss.PutCString("\n");
      }

      log->PutString(ss.GetString());
    }

    return true;
  }

  bool IsCall() override {
    VisitInstruction();
    return m_is_call;
  }

protected:
  std::weak_ptr<DisassemblerLLVMC> m_disasm_wp;

  bool m_is_valid = false;
  bool m_using_file_addr = false;
  bool m_has_visited_instruction = false;

  // Be conservative. If we didn't understand the instruction, say it:
  //   - Might branch
  //   - Does not have a delay slot
  //   - Is not a call
  //   - Is not a load
  //   - Is not an authenticated instruction
  bool m_does_branch = true;
  bool m_has_delay_slot = false;
  bool m_is_call = false;
  bool m_is_load = false;
  bool m_is_authenticated = false;

  void VisitInstruction() {
    if (m_has_visited_instruction)
      return;

    DisassemblerScope disasm(*this);
    if (!disasm)
      return;

    DataExtractor data;
    if (!m_opcode.GetData(data))
      return;

    bool is_alternate_isa;
    lldb::addr_t pc = m_address.GetFileAddress();
    DisassemblerLLVMC::MCDisasmInstance *mc_disasm_ptr =
        GetDisasmToUse(is_alternate_isa, disasm);
    const uint8_t *opcode_data = data.GetDataStart();
    const size_t opcode_data_len = data.GetByteSize();
    llvm::MCInst inst;
    const size_t inst_size =
        mc_disasm_ptr->GetMCInst(opcode_data, opcode_data_len, pc, inst);
    if (inst_size == 0)
      return;

    m_has_visited_instruction = true;
    m_does_branch = mc_disasm_ptr->CanBranch(inst);
    m_has_delay_slot = mc_disasm_ptr->HasDelaySlot(inst);
    m_is_call = mc_disasm_ptr->IsCall(inst);
    m_is_load = mc_disasm_ptr->IsLoad(inst);
    m_is_authenticated = mc_disasm_ptr->IsAuthenticated(inst);
  }

private:
  DisassemblerLLVMC::MCDisasmInstance *
  GetDisasmToUse(bool &is_alternate_isa, DisassemblerScope &disasm) {
    is_alternate_isa = false;
    if (disasm) {
      if (disasm->m_alternate_disasm_up) {
        const AddressClass address_class = GetAddressClass();

        if (address_class == AddressClass::eCodeAlternateISA) {
          is_alternate_isa = true;
          return disasm->m_alternate_disasm_up.get();
        }
      }
      return disasm->m_disasm_up.get();
    }
    return nullptr;
  }
};

std::unique_ptr<DisassemblerLLVMC::MCDisasmInstance>
DisassemblerLLVMC::MCDisasmInstance::Create(const char *triple, const char *cpu,
                                            const char *features_str,
                                            unsigned flavor,
                                            DisassemblerLLVMC &owner) {
  using Instance = std::unique_ptr<DisassemblerLLVMC::MCDisasmInstance>;

  std::string Status;
  const llvm::Target *curr_target =
      llvm::TargetRegistry::lookupTarget(triple, Status);
  if (!curr_target)
    return Instance();

  std::unique_ptr<llvm::MCInstrInfo> instr_info_up(
      curr_target->createMCInstrInfo());
  if (!instr_info_up)
    return Instance();

  std::unique_ptr<llvm::MCRegisterInfo> reg_info_up(
      curr_target->createMCRegInfo(triple));
  if (!reg_info_up)
    return Instance();

  std::unique_ptr<llvm::MCSubtargetInfo> subtarget_info_up(
      curr_target->createMCSubtargetInfo(triple, cpu, features_str));
  if (!subtarget_info_up)
    return Instance();

  llvm::MCTargetOptions MCOptions;
  std::unique_ptr<llvm::MCAsmInfo> asm_info_up(
      curr_target->createMCAsmInfo(*reg_info_up, triple, MCOptions));
  if (!asm_info_up)
    return Instance();

  std::unique_ptr<llvm::MCContext> context_up(
      new llvm::MCContext(llvm::Triple(triple), asm_info_up.get(),
                          reg_info_up.get(), subtarget_info_up.get()));
  if (!context_up)
    return Instance();

  std::unique_ptr<llvm::MCDisassembler> disasm_up(
      curr_target->createMCDisassembler(*subtarget_info_up, *context_up));
  if (!disasm_up)
    return Instance();

  std::unique_ptr<llvm::MCRelocationInfo> rel_info_up(
      curr_target->createMCRelocationInfo(triple, *context_up));
  if (!rel_info_up)
    return Instance();

  std::unique_ptr<llvm::MCSymbolizer> symbolizer_up(
      curr_target->createMCSymbolizer(
          triple, nullptr, DisassemblerLLVMC::SymbolLookupCallback, &owner,
          context_up.get(), std::move(rel_info_up)));
  disasm_up->setSymbolizer(std::move(symbolizer_up));

  unsigned asm_printer_variant =
      flavor == ~0U ? asm_info_up->getAssemblerDialect() : flavor;

  std::unique_ptr<llvm::MCInstPrinter> instr_printer_up(
      curr_target->createMCInstPrinter(llvm::Triple{triple},
                                       asm_printer_variant, *asm_info_up,
                                       *instr_info_up, *reg_info_up));
  if (!instr_printer_up)
    return Instance();

  instr_printer_up->setPrintBranchImmAsAddress(true);

  // Not all targets may have registered createMCInstrAnalysis().
  std::unique_ptr<llvm::MCInstrAnalysis> instr_analysis_up(
      curr_target->createMCInstrAnalysis(instr_info_up.get()));

  return Instance(new MCDisasmInstance(
      std::move(instr_info_up), std::move(reg_info_up),
      std::move(subtarget_info_up), std::move(asm_info_up),
      std::move(context_up), std::move(disasm_up), std::move(instr_printer_up),
      std::move(instr_analysis_up)));
}

DisassemblerLLVMC::MCDisasmInstance::MCDisasmInstance(
    std::unique_ptr<llvm::MCInstrInfo> &&instr_info_up,
    std::unique_ptr<llvm::MCRegisterInfo> &&reg_info_up,
    std::unique_ptr<llvm::MCSubtargetInfo> &&subtarget_info_up,
    std::unique_ptr<llvm::MCAsmInfo> &&asm_info_up,
    std::unique_ptr<llvm::MCContext> &&context_up,
    std::unique_ptr<llvm::MCDisassembler> &&disasm_up,
    std::unique_ptr<llvm::MCInstPrinter> &&instr_printer_up,
    std::unique_ptr<llvm::MCInstrAnalysis> &&instr_analysis_up)
    : m_instr_info_up(std::move(instr_info_up)),
      m_reg_info_up(std::move(reg_info_up)),
      m_subtarget_info_up(std::move(subtarget_info_up)),
      m_asm_info_up(std::move(asm_info_up)),
      m_context_up(std::move(context_up)), m_disasm_up(std::move(disasm_up)),
      m_instr_printer_up(std::move(instr_printer_up)),
      m_instr_analysis_up(std::move(instr_analysis_up)) {
  assert(m_instr_info_up && m_reg_info_up && m_subtarget_info_up &&
         m_asm_info_up && m_context_up && m_disasm_up && m_instr_printer_up);
}

uint64_t DisassemblerLLVMC::MCDisasmInstance::GetMCInst(
    const uint8_t *opcode_data, size_t opcode_data_len, lldb::addr_t pc,
    llvm::MCInst &mc_inst) const {
  llvm::ArrayRef<uint8_t> data(opcode_data, opcode_data_len);
  llvm::MCDisassembler::DecodeStatus status;

  uint64_t new_inst_size;
  status = m_disasm_up->getInstruction(mc_inst, new_inst_size, data, pc,
                                       llvm::nulls());
  if (status == llvm::MCDisassembler::Success)
    return new_inst_size;
  else
    return 0;
}

void DisassemblerLLVMC::MCDisasmInstance::PrintMCInst(
    llvm::MCInst &mc_inst, lldb::addr_t pc, std::string &inst_string,
    std::string &comments_string) {
  llvm::raw_string_ostream inst_stream(inst_string);
  llvm::raw_string_ostream comments_stream(comments_string);

  inst_stream.enable_colors(m_instr_printer_up->getUseColor());
  m_instr_printer_up->setCommentStream(comments_stream);
  m_instr_printer_up->printInst(&mc_inst, pc, llvm::StringRef(),
                                *m_subtarget_info_up, inst_stream);
  m_instr_printer_up->setCommentStream(llvm::nulls());

  comments_stream.flush();

  static std::string g_newlines("\r\n");

  for (size_t newline_pos = 0;
       (newline_pos = comments_string.find_first_of(g_newlines, newline_pos)) !=
       comments_string.npos;
       /**/) {
    comments_string.replace(comments_string.begin() + newline_pos,
                            comments_string.begin() + newline_pos + 1, 1, ' ');
  }
}

void DisassemblerLLVMC::MCDisasmInstance::SetStyle(
    bool use_hex_immed, HexImmediateStyle hex_style) {
  m_instr_printer_up->setPrintImmHex(use_hex_immed);
  switch (hex_style) {
  case eHexStyleC:
    m_instr_printer_up->setPrintHexStyle(llvm::HexStyle::C);
    break;
  case eHexStyleAsm:
    m_instr_printer_up->setPrintHexStyle(llvm::HexStyle::Asm);
    break;
  }
}

void DisassemblerLLVMC::MCDisasmInstance::SetUseColor(bool use_color) {
  m_instr_printer_up->setUseColor(use_color);
}

bool DisassemblerLLVMC::MCDisasmInstance::GetUseColor() const {
  return m_instr_printer_up->getUseColor();
}

bool DisassemblerLLVMC::MCDisasmInstance::CanBranch(
    llvm::MCInst &mc_inst) const {
  if (m_instr_analysis_up)
    return m_instr_analysis_up->mayAffectControlFlow(mc_inst, *m_reg_info_up);
  return m_instr_info_up->get(mc_inst.getOpcode())
      .mayAffectControlFlow(mc_inst, *m_reg_info_up);
}

bool DisassemblerLLVMC::MCDisasmInstance::HasDelaySlot(
    llvm::MCInst &mc_inst) const {
  return m_instr_info_up->get(mc_inst.getOpcode()).hasDelaySlot();
}

bool DisassemblerLLVMC::MCDisasmInstance::IsCall(llvm::MCInst &mc_inst) const {
  if (m_instr_analysis_up)
    return m_instr_analysis_up->isCall(mc_inst);
  return m_instr_info_up->get(mc_inst.getOpcode()).isCall();
}

bool DisassemblerLLVMC::MCDisasmInstance::IsLoad(llvm::MCInst &mc_inst) const {
  return m_instr_info_up->get(mc_inst.getOpcode()).mayLoad();
}

bool DisassemblerLLVMC::MCDisasmInstance::IsAuthenticated(
    llvm::MCInst &mc_inst) const {
  const auto &InstrDesc = m_instr_info_up->get(mc_inst.getOpcode());

  // Treat software auth traps (brk 0xc470 + aut key, where 0x70 == 'p', 0xc4
  // == 'a' + 'c') as authenticated instructions for reporting purposes, in
  // addition to the standard authenticated instructions specified in ARMv8.3.
  bool IsBrkC47x = false;
  if (InstrDesc.isTrap() && mc_inst.getNumOperands() == 1) {
    const llvm::MCOperand &Op0 = mc_inst.getOperand(0);
    if (Op0.isImm() && Op0.getImm() >= 0xc470 && Op0.getImm() <= 0xc474)
      IsBrkC47x = true;
  }

  return InstrDesc.isAuthenticated() || IsBrkC47x;
}

DisassemblerLLVMC::DisassemblerLLVMC(const ArchSpec &arch,
                                     const char *flavor_string)
    : Disassembler(arch, flavor_string), m_exe_ctx(nullptr), m_inst(nullptr),
      m_data_from_file(false), m_adrp_address(LLDB_INVALID_ADDRESS),
      m_adrp_insn() {
  if (!FlavorValidForArchSpec(arch, m_flavor.c_str())) {
    m_flavor.assign("default");
  }

  unsigned flavor = ~0U;
  llvm::Triple triple = arch.GetTriple();

  // So far the only supported flavor is "intel" on x86.  The base class will
  // set this correctly coming in.
  if (triple.getArch() == llvm::Triple::x86 ||
      triple.getArch() == llvm::Triple::x86_64) {
    if (m_flavor == "intel") {
      flavor = 1;
    } else if (m_flavor == "att") {
      flavor = 0;
    }
  }

  ArchSpec thumb_arch(arch);
  if (triple.getArch() == llvm::Triple::arm) {
    std::string thumb_arch_name(thumb_arch.GetTriple().getArchName().str());
    // Replace "arm" with "thumb" so we get all thumb variants correct
    if (thumb_arch_name.size() > 3) {
      thumb_arch_name.erase(0, 3);
      thumb_arch_name.insert(0, "thumb");
    } else {
      thumb_arch_name = "thumbv9.3a";
    }
    thumb_arch.GetTriple().setArchName(llvm::StringRef(thumb_arch_name));
  }

  // If no sub architecture specified then use the most recent arm architecture
  // so the disassembler will return all instructions. Without it we will see a
  // lot of unknown opcodes if the code uses instructions which are not
  // available in the oldest arm version (which is used when no sub architecture
  // is specified).
  if (triple.getArch() == llvm::Triple::arm &&
      triple.getSubArch() == llvm::Triple::NoSubArch)
    triple.setArchName("armv9.3a");

  std::string features_str;
  const char *triple_str = triple.getTriple().c_str();

  // ARM Cortex M0-M7 devices only execute thumb instructions
  if (arch.IsAlwaysThumbInstructions()) {
    triple_str = thumb_arch.GetTriple().getTriple().c_str();
    features_str += "+fp-armv8,";
  }

  const char *cpu = "";

  switch (arch.GetCore()) {
  case ArchSpec::eCore_mips32:
  case ArchSpec::eCore_mips32el:
    cpu = "mips32";
    break;
  case ArchSpec::eCore_mips32r2:
  case ArchSpec::eCore_mips32r2el:
    cpu = "mips32r2";
    break;
  case ArchSpec::eCore_mips32r3:
  case ArchSpec::eCore_mips32r3el:
    cpu = "mips32r3";
    break;
  case ArchSpec::eCore_mips32r5:
  case ArchSpec::eCore_mips32r5el:
    cpu = "mips32r5";
    break;
  case ArchSpec::eCore_mips32r6:
  case ArchSpec::eCore_mips32r6el:
    cpu = "mips32r6";
    break;
  case ArchSpec::eCore_mips64:
  case ArchSpec::eCore_mips64el:
    cpu = "mips64";
    break;
  case ArchSpec::eCore_mips64r2:
  case ArchSpec::eCore_mips64r2el:
    cpu = "mips64r2";
    break;
  case ArchSpec::eCore_mips64r3:
  case ArchSpec::eCore_mips64r3el:
    cpu = "mips64r3";
    break;
  case ArchSpec::eCore_mips64r5:
  case ArchSpec::eCore_mips64r5el:
    cpu = "mips64r5";
    break;
  case ArchSpec::eCore_mips64r6:
  case ArchSpec::eCore_mips64r6el:
    cpu = "mips64r6";
    break;
  default:
    cpu = "";
    break;
  }

  if (arch.IsMIPS()) {
    uint32_t arch_flags = arch.GetFlags();
    if (arch_flags & ArchSpec::eMIPSAse_msa)
      features_str += "+msa,";
    if (arch_flags & ArchSpec::eMIPSAse_dsp)
      features_str += "+dsp,";
    if (arch_flags & ArchSpec::eMIPSAse_dspr2)
      features_str += "+dspr2,";
  }

  // If any AArch64 variant, enable latest ISA with all extensions.
  if (triple.isAArch64()) {
    features_str += "+all,";

    if (triple.getVendor() == llvm::Triple::Apple)
      cpu = "apple-latest";
  }

  if (triple.isRISCV()) {
    uint32_t arch_flags = arch.GetFlags();
    if (arch_flags & ArchSpec::eRISCV_rvc)
      features_str += "+c,";
    if (arch_flags & ArchSpec::eRISCV_rve)
      features_str += "+e,";
    if ((arch_flags & ArchSpec::eRISCV_float_abi_single) ==
        ArchSpec::eRISCV_float_abi_single)
      features_str += "+f,";
    if ((arch_flags & ArchSpec::eRISCV_float_abi_double) ==
        ArchSpec::eRISCV_float_abi_double)
      features_str += "+f,+d,";
    if ((arch_flags & ArchSpec::eRISCV_float_abi_quad) ==
        ArchSpec::eRISCV_float_abi_quad)
      features_str += "+f,+d,+q,";
    // FIXME: how do we detect features such as `+a`, `+m`?
    // Turn them on by default now, since everyone seems to use them
    features_str += "+a,+m,";
  }

  // We use m_disasm_up.get() to tell whether we are valid or not, so if this
  // isn't good for some reason, we won't be valid and FindPlugin will fail and
  // we won't get used.
  m_disasm_up = MCDisasmInstance::Create(triple_str, cpu, features_str.c_str(),
                                         flavor, *this);

  llvm::Triple::ArchType llvm_arch = triple.getArch();

  // For arm CPUs that can execute arm or thumb instructions, also create a
  // thumb instruction disassembler.
  if (llvm_arch == llvm::Triple::arm) {
    std::string thumb_triple(thumb_arch.GetTriple().getTriple());
    m_alternate_disasm_up =
        MCDisasmInstance::Create(thumb_triple.c_str(), "", features_str.c_str(),
                                 flavor, *this);
    if (!m_alternate_disasm_up)
      m_disasm_up.reset();

  } else if (arch.IsMIPS()) {
    /* Create alternate disassembler for MIPS16 and microMIPS */
    uint32_t arch_flags = arch.GetFlags();
    if (arch_flags & ArchSpec::eMIPSAse_mips16)
      features_str += "+mips16,";
    else if (arch_flags & ArchSpec::eMIPSAse_micromips)
      features_str += "+micromips,";

    m_alternate_disasm_up = MCDisasmInstance::Create(
        triple_str, cpu, features_str.c_str(), flavor, *this);
    if (!m_alternate_disasm_up)
      m_disasm_up.reset();
  }
}

DisassemblerLLVMC::~DisassemblerLLVMC() = default;

lldb::DisassemblerSP DisassemblerLLVMC::CreateInstance(const ArchSpec &arch,
                                                       const char *flavor) {
  if (arch.GetTriple().getArch() != llvm::Triple::UnknownArch) {
    auto disasm_sp = std::make_shared<DisassemblerLLVMC>(arch, flavor);
    if (disasm_sp && disasm_sp->IsValid())
      return disasm_sp;
  }
  return lldb::DisassemblerSP();
}

size_t DisassemblerLLVMC::DecodeInstructions(const Address &base_addr,
                                             const DataExtractor &data,
                                             lldb::offset_t data_offset,
                                             size_t num_instructions,
                                             bool append, bool data_from_file) {
  if (!append)
    m_instruction_list.Clear();

  if (!IsValid())
    return 0;

  m_data_from_file = data_from_file;
  uint32_t data_cursor = data_offset;
  const size_t data_byte_size = data.GetByteSize();
  uint32_t instructions_parsed = 0;
  Address inst_addr(base_addr);

  while (data_cursor < data_byte_size &&
         instructions_parsed < num_instructions) {

    AddressClass address_class = AddressClass::eCode;

    if (m_alternate_disasm_up)
      address_class = inst_addr.GetAddressClass();

    InstructionSP inst_sp(
        new InstructionLLVMC(*this, inst_addr, address_class));

    if (!inst_sp)
      break;

    uint32_t inst_size = inst_sp->Decode(*this, data, data_cursor);

    if (inst_size == 0)
      break;

    m_instruction_list.Append(inst_sp);
    data_cursor += inst_size;
    inst_addr.Slide(inst_size);
    instructions_parsed++;
  }

  return data_cursor - data_offset;
}

void DisassemblerLLVMC::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "Disassembler that uses LLVM MC to disassemble "
                                "i386, x86_64, ARM, and ARM64.",
                                CreateInstance);

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();
}

void DisassemblerLLVMC::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

int DisassemblerLLVMC::OpInfoCallback(void *disassembler, uint64_t pc,
                                      uint64_t offset, uint64_t size,
                                      int tag_type, void *tag_bug) {
  return static_cast<DisassemblerLLVMC *>(disassembler)
      ->OpInfo(pc, offset, size, tag_type, tag_bug);
}

const char *DisassemblerLLVMC::SymbolLookupCallback(void *disassembler,
                                                    uint64_t value,
                                                    uint64_t *type, uint64_t pc,
                                                    const char **name) {
  return static_cast<DisassemblerLLVMC *>(disassembler)
      ->SymbolLookup(value, type, pc, name);
}

bool DisassemblerLLVMC::FlavorValidForArchSpec(
    const lldb_private::ArchSpec &arch, const char *flavor) {
  llvm::Triple triple = arch.GetTriple();
  if (flavor == nullptr || strcmp(flavor, "default") == 0)
    return true;

  if (triple.getArch() == llvm::Triple::x86 ||
      triple.getArch() == llvm::Triple::x86_64) {
    return strcmp(flavor, "intel") == 0 || strcmp(flavor, "att") == 0;
  } else
    return false;
}

bool DisassemblerLLVMC::IsValid() const { return m_disasm_up.operator bool(); }

int DisassemblerLLVMC::OpInfo(uint64_t PC, uint64_t Offset, uint64_t Size,
                              int tag_type, void *tag_bug) {
  switch (tag_type) {
  default:
    break;
  case 1:
    memset(tag_bug, 0, sizeof(::LLVMOpInfo1));
    break;
  }
  return 0;
}

const char *DisassemblerLLVMC::SymbolLookup(uint64_t value, uint64_t *type_ptr,
                                            uint64_t pc, const char **name) {
  if (*type_ptr) {
    if (m_exe_ctx && m_inst) {
      // std::string remove_this_prior_to_checkin;
      Target *target = m_exe_ctx ? m_exe_ctx->GetTargetPtr() : nullptr;
      Address value_so_addr;
      Address pc_so_addr;
      if (target->GetArchitecture().GetMachine() == llvm::Triple::aarch64 ||
          target->GetArchitecture().GetMachine() == llvm::Triple::aarch64_be ||
          target->GetArchitecture().GetMachine() == llvm::Triple::aarch64_32) {
        if (*type_ptr == LLVMDisassembler_ReferenceType_In_ARM64_ADRP) {
          m_adrp_address = pc;
          m_adrp_insn = value;
          *name = nullptr;
          *type_ptr = LLVMDisassembler_ReferenceType_InOut_None;
          return nullptr;
        }
        // If this instruction is an ADD and
        // the previous instruction was an ADRP and
        // the ADRP's register and this ADD's register are the same,
        // then this is a pc-relative address calculation.
        if (*type_ptr == LLVMDisassembler_ReferenceType_In_ARM64_ADDXri &&
            m_adrp_insn && m_adrp_address == pc - 4 &&
            (*m_adrp_insn & 0x1f) == ((value >> 5) & 0x1f)) {
          uint32_t addxri_inst;
          uint64_t adrp_imm, addxri_imm;
          // Get immlo and immhi bits, OR them together to get the ADRP imm
          // value.
          adrp_imm =
              ((*m_adrp_insn & 0x00ffffe0) >> 3) | ((*m_adrp_insn >> 29) & 0x3);
          // if high bit of immhi after right-shifting set, sign extend
          if (adrp_imm & (1ULL << 20))
            adrp_imm |= ~((1ULL << 21) - 1);

          addxri_inst = value;
          addxri_imm = (addxri_inst >> 10) & 0xfff;
          // check if 'sh' bit is set, shift imm value up if so
          // (this would make no sense, ADRP already gave us this part)
          if ((addxri_inst >> (12 + 5 + 5)) & 1)
            addxri_imm <<= 12;
          value = (m_adrp_address & 0xfffffffffffff000LL) + (adrp_imm << 12) +
                  addxri_imm;
        }
        m_adrp_address = LLDB_INVALID_ADDRESS;
        m_adrp_insn.reset();
      }

      if (m_inst->UsingFileAddress()) {
        ModuleSP module_sp(m_inst->GetAddress().GetModule());
        if (module_sp) {
          module_sp->ResolveFileAddress(value, value_so_addr);
          module_sp->ResolveFileAddress(pc, pc_so_addr);
        }
      } else if (target && !target->GetSectionLoadList().IsEmpty()) {
        target->GetSectionLoadList().ResolveLoadAddress(value, value_so_addr);
        target->GetSectionLoadList().ResolveLoadAddress(pc, pc_so_addr);
      }

      SymbolContext sym_ctx;
      const SymbolContextItem resolve_scope =
          eSymbolContextFunction | eSymbolContextSymbol;
      if (pc_so_addr.IsValid() && pc_so_addr.GetModule()) {
        pc_so_addr.GetModule()->ResolveSymbolContextForAddress(
            pc_so_addr, resolve_scope, sym_ctx);
      }

      if (value_so_addr.IsValid() && value_so_addr.GetSection()) {
        StreamString ss;

        bool format_omitting_current_func_name = false;
        if (sym_ctx.symbol || sym_ctx.function) {
          AddressRange range;
          if (sym_ctx.GetAddressRange(resolve_scope, 0, false, range) &&
              range.GetBaseAddress().IsValid() &&
              range.ContainsLoadAddress(value_so_addr, target)) {
            format_omitting_current_func_name = true;
          }
        }

        // If the "value" address (the target address we're symbolicating) is
        // inside the same SymbolContext as the current instruction pc
        // (pc_so_addr), don't print the full function name - just print it
        // with DumpStyleNoFunctionName style, e.g. "<+36>".
        if (format_omitting_current_func_name) {
          value_so_addr.Dump(&ss, target, Address::DumpStyleNoFunctionName,
                             Address::DumpStyleSectionNameOffset);
        } else {
          value_so_addr.Dump(
              &ss, target,
              Address::DumpStyleResolvedDescriptionNoFunctionArguments,
              Address::DumpStyleSectionNameOffset);
        }

        if (!ss.GetString().empty()) {
          // If Address::Dump returned a multi-line description, most commonly
          // seen when we have multiple levels of inlined functions at an
          // address, only show the first line.
          std::string str = std::string(ss.GetString());
          size_t first_eol_char = str.find_first_of("\r\n");
          if (first_eol_char != std::string::npos) {
            str.erase(first_eol_char);
          }
          m_inst->AppendComment(str);
        }
      }
    }
  }

  // TODO: llvm-objdump sets the type_ptr to the
  // LLVMDisassembler_ReferenceType_Out_* values
  // based on where value_so_addr is pointing, with
  // Mach-O specific augmentations in MachODump.cpp. e.g.
  // see what AArch64ExternalSymbolizer::tryAddingSymbolicOperand
  // handles.
  *type_ptr = LLVMDisassembler_ReferenceType_InOut_None;
  *name = nullptr;
  return nullptr;
}
