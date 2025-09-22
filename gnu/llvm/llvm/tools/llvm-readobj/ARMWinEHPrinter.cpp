//===-- ARMWinEHPrinter.cpp - Windows on ARM EH Data Printer ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Windows on ARM uses a series of serialised data structures (RuntimeFunction)
// to create a table of information for unwinding.  In order to conserve space,
// there are two different ways that this data is represented.
//
// For functions with canonical forms for the prologue and epilogue, the data
// can be stored in a "packed" form.  In this case, the data is packed into the
// RuntimeFunction's remaining 30-bits and can fully describe the entire frame.
//
//        +---------------------------------------+
//        |         Function Entry Address        |
//        +---------------------------------------+
//        |           Packed Form Data            |
//        +---------------------------------------+
//
// This layout is parsed by Decoder::dumpPackedEntry.  No unwind bytecode is
// associated with such a frame as they can be derived from the provided data.
// The decoder does not synthesize this data as it is unnecessary for the
// purposes of validation, with the synthesis being required only by a proper
// unwinder.
//
// For functions that are large or do not match canonical forms, the data is
// split up into two portions, with the actual data residing in the "exception
// data" table (.xdata) with a reference to the entry from the "procedure data"
// (.pdata) entry.
//
// The exception data contains information about the frame setup, all of the
// epilogue scopes (for functions for which there are multiple exit points) and
// the associated exception handler.  Additionally, the entry contains byte-code
// describing how to unwind the function (c.f. Decoder::decodeOpcodes).
//
//        +---------------------------------------+
//        |         Function Entry Address        |
//        +---------------------------------------+
//        |      Exception Data Entry Address     |
//        +---------------------------------------+
//
// This layout is parsed by Decoder::dumpUnpackedEntry.  Such an entry must
// first resolve the exception data entry address.  This structure
// (ExceptionDataRecord) has a variable sized header
// (c.f. ARM::WinEH::HeaderWords) and encodes most of the same information as
// the packed form.  However, because this information is insufficient to
// synthesize the unwinding, there are associated unwinding bytecode which make
// up the bulk of the Decoder.
//
// The decoder itself is table-driven, using the first byte to determine the
// opcode and dispatching to the associated printing routine.  The bytecode
// itself is a variable length instruction encoding that can fully describe the
// state of the stack and the necessary operations for unwinding to the
// beginning of the frame.
//
// The byte-code maintains a 1-1 instruction mapping, indicating both the width
// of the instruction (Thumb2 instructions are variable length, 16 or 32 bits
// wide) allowing the program to unwind from any point in the prologue, body, or
// epilogue of the function.

#include "ARMWinEHPrinter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ARMWinEH.h"
#include "llvm/Support/Format.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support;

namespace llvm {
raw_ostream &operator<<(raw_ostream &OS, const ARM::WinEH::ReturnType &RT) {
  switch (RT) {
  case ARM::WinEH::ReturnType::RT_POP:
    OS << "pop {pc}";
    break;
  case ARM::WinEH::ReturnType::RT_B:
    OS << "bx <reg>";
    break;
  case ARM::WinEH::ReturnType::RT_BW:
    OS << "b.w <target>";
    break;
  case ARM::WinEH::ReturnType::RT_NoEpilogue:
    OS << "(no epilogue)";
    break;
  }
  return OS;
}
}

static std::string formatSymbol(StringRef Name, uint64_t Address,
                                uint64_t Offset = 0) {
  std::string Buffer;
  raw_string_ostream OS(Buffer);

  if (!Name.empty())
    OS << Name << " ";

  if (Offset)
    OS << format("+0x%" PRIX64 " (0x%" PRIX64 ")", Offset, Address);
  else if (!Name.empty())
    OS << format("(0x%" PRIX64 ")", Address);
  else
    OS << format("0x%" PRIX64, Address);

  return OS.str();
}

namespace llvm {
namespace ARM {
namespace WinEH {
const size_t Decoder::PDataEntrySize = sizeof(RuntimeFunction);

// TODO name the uops more appropriately
const Decoder::RingEntry Decoder::Ring[] = {
  { 0x80, 0x00, 1, &Decoder::opcode_0xxxxxxx },  // UOP_STACK_FREE (16-bit)
  { 0xc0, 0x80, 2, &Decoder::opcode_10Lxxxxx },  // UOP_POP (32-bit)
  { 0xf0, 0xc0, 1, &Decoder::opcode_1100xxxx },  // UOP_STACK_SAVE (16-bit)
  { 0xf8, 0xd0, 1, &Decoder::opcode_11010Lxx },  // UOP_POP (16-bit)
  { 0xf8, 0xd8, 1, &Decoder::opcode_11011Lxx },  // UOP_POP (32-bit)
  { 0xf8, 0xe0, 1, &Decoder::opcode_11100xxx },  // UOP_VPOP (32-bit)
  { 0xfc, 0xe8, 2, &Decoder::opcode_111010xx },  // UOP_STACK_FREE (32-bit)
  { 0xfe, 0xec, 2, &Decoder::opcode_1110110L },  // UOP_POP (16-bit)
  { 0xff, 0xee, 2, &Decoder::opcode_11101110 },  // UOP_MICROSOFT_SPECIFIC (16-bit)
                                              // UOP_PUSH_MACHINE_FRAME
                                              // UOP_PUSH_CONTEXT
                                              // UOP_PUSH_TRAP_FRAME
                                              // UOP_REDZONE_RESTORE_LR
  { 0xff, 0xef, 2, &Decoder::opcode_11101111 },  // UOP_LDRPC_POSTINC (32-bit)
  { 0xff, 0xf5, 2, &Decoder::opcode_11110101 },  // UOP_VPOP (32-bit)
  { 0xff, 0xf6, 2, &Decoder::opcode_11110110 },  // UOP_VPOP (32-bit)
  { 0xff, 0xf7, 3, &Decoder::opcode_11110111 },  // UOP_STACK_RESTORE (16-bit)
  { 0xff, 0xf8, 4, &Decoder::opcode_11111000 },  // UOP_STACK_RESTORE (16-bit)
  { 0xff, 0xf9, 3, &Decoder::opcode_11111001 },  // UOP_STACK_RESTORE (32-bit)
  { 0xff, 0xfa, 4, &Decoder::opcode_11111010 },  // UOP_STACK_RESTORE (32-bit)
  { 0xff, 0xfb, 1, &Decoder::opcode_11111011 },  // UOP_NOP (16-bit)
  { 0xff, 0xfc, 1, &Decoder::opcode_11111100 },  // UOP_NOP (32-bit)
  { 0xff, 0xfd, 1, &Decoder::opcode_11111101 },  // UOP_NOP (16-bit) / END
  { 0xff, 0xfe, 1, &Decoder::opcode_11111110 },  // UOP_NOP (32-bit) / END
  { 0xff, 0xff, 1, &Decoder::opcode_11111111 },  // UOP_END
};

// Unwind opcodes for ARM64.
// https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling
const Decoder::RingEntry Decoder::Ring64[] = {
    {0xe0, 0x00, 1, &Decoder::opcode_alloc_s},
    {0xe0, 0x20, 1, &Decoder::opcode_save_r19r20_x},
    {0xc0, 0x40, 1, &Decoder::opcode_save_fplr},
    {0xc0, 0x80, 1, &Decoder::opcode_save_fplr_x},
    {0xf8, 0xc0, 2, &Decoder::opcode_alloc_m},
    {0xfc, 0xc8, 2, &Decoder::opcode_save_regp},
    {0xfc, 0xcc, 2, &Decoder::opcode_save_regp_x},
    {0xfc, 0xd0, 2, &Decoder::opcode_save_reg},
    {0xfe, 0xd4, 2, &Decoder::opcode_save_reg_x},
    {0xfe, 0xd6, 2, &Decoder::opcode_save_lrpair},
    {0xfe, 0xd8, 2, &Decoder::opcode_save_fregp},
    {0xfe, 0xda, 2, &Decoder::opcode_save_fregp_x},
    {0xfe, 0xdc, 2, &Decoder::opcode_save_freg},
    {0xff, 0xde, 2, &Decoder::opcode_save_freg_x},
    {0xff, 0xe0, 4, &Decoder::opcode_alloc_l},
    {0xff, 0xe1, 1, &Decoder::opcode_setfp},
    {0xff, 0xe2, 2, &Decoder::opcode_addfp},
    {0xff, 0xe3, 1, &Decoder::opcode_nop},
    {0xff, 0xe4, 1, &Decoder::opcode_end},
    {0xff, 0xe5, 1, &Decoder::opcode_end_c},
    {0xff, 0xe6, 1, &Decoder::opcode_save_next},
    {0xff, 0xe7, 3, &Decoder::opcode_save_any_reg},
    {0xff, 0xe8, 1, &Decoder::opcode_trap_frame},
    {0xff, 0xe9, 1, &Decoder::opcode_machine_frame},
    {0xff, 0xea, 1, &Decoder::opcode_context},
    {0xff, 0xeb, 1, &Decoder::opcode_ec_context},
    {0xff, 0xec, 1, &Decoder::opcode_clear_unwound_to_call},
    {0xff, 0xfc, 1, &Decoder::opcode_pac_sign_lr},
};

static void printRange(raw_ostream &OS, ListSeparator &LS, unsigned First,
                       unsigned Last, char Letter) {
  if (First == Last)
    OS << LS << Letter << First;
  else
    OS << LS << Letter << First << "-" << Letter << Last;
}

static void printRange(raw_ostream &OS, uint32_t Mask, ListSeparator &LS,
                       unsigned Start, unsigned End, char Letter) {
  int First = -1;
  for (unsigned RI = Start; RI <= End; ++RI) {
    if (Mask & (1 << RI)) {
      if (First < 0)
        First = RI;
    } else {
      if (First >= 0) {
        printRange(OS, LS, First, RI - 1, Letter);
        First = -1;
      }
    }
  }
  if (First >= 0)
    printRange(OS, LS, First, End, Letter);
}

void Decoder::printGPRMask(uint16_t GPRMask) {
  OS << '{';
  ListSeparator LS;
  printRange(OS, GPRMask, LS, 0, 12, 'r');
  if (GPRMask & (1 << 14))
    OS << LS << "lr";
  if (GPRMask & (1 << 15))
    OS << LS << "pc";
  OS << '}';
}

void Decoder::printVFPMask(uint32_t VFPMask) {
  OS << '{';
  ListSeparator LS;
  printRange(OS, VFPMask, LS, 0, 31, 'd');
  OS << '}';
}

ErrorOr<object::SectionRef>
Decoder::getSectionContaining(const COFFObjectFile &COFF, uint64_t VA) {
  for (const auto &Section : COFF.sections()) {
    uint64_t Address = Section.getAddress();
    uint64_t Size = Section.getSize();

    if (VA >= Address && (VA - Address) <= Size)
      return Section;
  }
  return inconvertibleErrorCode();
}

ErrorOr<object::SymbolRef> Decoder::getSymbol(const COFFObjectFile &COFF,
                                              uint64_t VA, bool FunctionOnly) {
  for (const auto &Symbol : COFF.symbols()) {
    Expected<SymbolRef::Type> Type = Symbol.getType();
    if (!Type)
      return errorToErrorCode(Type.takeError());
    if (FunctionOnly && *Type != SymbolRef::ST_Function)
      continue;

    Expected<uint64_t> Address = Symbol.getAddress();
    if (!Address)
      return errorToErrorCode(Address.takeError());
    if (*Address == VA)
      return Symbol;
  }
  return inconvertibleErrorCode();
}

ErrorOr<SymbolRef> Decoder::getRelocatedSymbol(const COFFObjectFile &,
                                               const SectionRef &Section,
                                               uint64_t Offset) {
  for (const auto &Relocation : Section.relocations()) {
    uint64_t RelocationOffset = Relocation.getOffset();
    if (RelocationOffset == Offset)
      return *Relocation.getSymbol();
  }
  return inconvertibleErrorCode();
}

SymbolRef Decoder::getPreferredSymbol(const COFFObjectFile &COFF, SymbolRef Sym,
                                      uint64_t &SymbolOffset) {
  // The symbol resolved by getRelocatedSymbol can be any internal
  // nondescriptive symbol; try to resolve a more descriptive one.
  COFFSymbolRef CoffSym = COFF.getCOFFSymbol(Sym);
  if (CoffSym.getStorageClass() != COFF::IMAGE_SYM_CLASS_LABEL &&
      CoffSym.getSectionDefinition() == nullptr)
    return Sym;
  for (const auto &S : COFF.symbols()) {
    COFFSymbolRef CS = COFF.getCOFFSymbol(S);
    if (CS.getSectionNumber() == CoffSym.getSectionNumber() &&
        CS.getValue() <= CoffSym.getValue() + SymbolOffset &&
        CS.getStorageClass() != COFF::IMAGE_SYM_CLASS_LABEL &&
        CS.getSectionDefinition() == nullptr) {
      uint32_t Offset = CoffSym.getValue() + SymbolOffset - CS.getValue();
      if (Offset <= SymbolOffset) {
        SymbolOffset = Offset;
        Sym = S;
        CoffSym = CS;
        if (CS.isExternal() && SymbolOffset == 0)
          return Sym;
      }
    }
  }
  return Sym;
}

ErrorOr<SymbolRef> Decoder::getSymbolForLocation(
    const COFFObjectFile &COFF, const SectionRef &Section,
    uint64_t OffsetInSection, uint64_t ImmediateOffset, uint64_t &SymbolAddress,
    uint64_t &SymbolOffset, bool FunctionOnly) {
  // Try to locate a relocation that points at the offset in the section
  ErrorOr<SymbolRef> SymOrErr =
      getRelocatedSymbol(COFF, Section, OffsetInSection);
  if (SymOrErr) {
    // We found a relocation symbol; the immediate offset needs to be added
    // to the symbol address.
    SymbolOffset = ImmediateOffset;

    Expected<uint64_t> AddressOrErr = SymOrErr->getAddress();
    if (!AddressOrErr) {
      std::string Buf;
      llvm::raw_string_ostream OS(Buf);
      logAllUnhandledErrors(AddressOrErr.takeError(), OS);
      report_fatal_error(Twine(OS.str()));
    }
    // We apply SymbolOffset here directly. We return it separately to allow
    // the caller to print it as an offset on the symbol name.
    SymbolAddress = *AddressOrErr + SymbolOffset;

    if (FunctionOnly) // Resolve label/section symbols into function names.
      SymOrErr = getPreferredSymbol(COFF, *SymOrErr, SymbolOffset);
  } else {
    // No matching relocation found; operating on a linked image. Try to
    // find a descriptive symbol if possible. The immediate offset contains
    // the image relative address, and we shouldn't add any offset to the
    // symbol.
    SymbolAddress = COFF.getImageBase() + ImmediateOffset;
    SymbolOffset = 0;
    SymOrErr = getSymbol(COFF, SymbolAddress, FunctionOnly);
  }
  return SymOrErr;
}

bool Decoder::opcode_0xxxxxxx(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  uint8_t Imm = OC[Offset] & 0x7f;
  SW.startLine() << format("0x%02x                ; %s sp, #(%u * 4)\n",
                           OC[Offset],
                           static_cast<const char *>(Prologue ? "sub" : "add"),
                           Imm);
  ++Offset;
  return false;
}

bool Decoder::opcode_10Lxxxxx(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  unsigned Link = (OC[Offset] & 0x20) >> 5;
  uint16_t RegisterMask = (Link << (Prologue ? 14 : 15))
                        | ((OC[Offset + 0] & 0x1f) << 8)
                        | ((OC[Offset + 1] & 0xff) << 0);
  assert((~RegisterMask & (1 << 13)) && "sp must not be set");
  assert((~RegisterMask & (1 << (Prologue ? 15 : 14))) && "pc must not be set");

  SW.startLine() << format("0x%02x 0x%02x           ; %s.w ",
                           OC[Offset + 0], OC[Offset + 1],
                           Prologue ? "push" : "pop");
  printGPRMask(RegisterMask);
  OS << '\n';

  Offset += 2;
  return false;
}

bool Decoder::opcode_1100xxxx(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  if (Prologue)
    SW.startLine() << format("0x%02x                ; mov r%u, sp\n",
                             OC[Offset], OC[Offset] & 0xf);
  else
    SW.startLine() << format("0x%02x                ; mov sp, r%u\n",
                             OC[Offset], OC[Offset] & 0xf);
  ++Offset;
  return false;
}

bool Decoder::opcode_11010Lxx(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  unsigned Link = (OC[Offset] & 0x4) >> 2;
  unsigned Count = (OC[Offset] & 0x3);

  uint16_t GPRMask = (Link << (Prologue ? 14 : 15))
                   | (((1 << (Count + 1)) - 1) << 4);

  SW.startLine() << format("0x%02x                ; %s ", OC[Offset],
                           Prologue ? "push" : "pop");
  printGPRMask(GPRMask);
  OS << '\n';

  ++Offset;
  return false;
}

bool Decoder::opcode_11011Lxx(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  unsigned Link = (OC[Offset] & 0x4) >> 2;
  unsigned Count = (OC[Offset] & 0x3) + 4;

  uint16_t GPRMask = (Link << (Prologue ? 14 : 15))
                   | (((1 << (Count + 1)) - 1) << 4);

  SW.startLine() << format("0x%02x                ; %s.w ", OC[Offset],
                           Prologue ? "push" : "pop");
  printGPRMask(GPRMask);
  OS << '\n';

  ++Offset;
  return false;
}

bool Decoder::opcode_11100xxx(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  unsigned High = (OC[Offset] & 0x7);
  uint32_t VFPMask = (((1 << (High + 1)) - 1) << 8);

  SW.startLine() << format("0x%02x                ; %s ", OC[Offset],
                           Prologue ? "vpush" : "vpop");
  printVFPMask(VFPMask);
  OS << '\n';

  ++Offset;
  return false;
}

bool Decoder::opcode_111010xx(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  uint16_t Imm = ((OC[Offset + 0] & 0x03) << 8) | ((OC[Offset + 1] & 0xff) << 0);

  SW.startLine() << format("0x%02x 0x%02x           ; %s.w sp, #(%u * 4)\n",
                           OC[Offset + 0], OC[Offset + 1],
                           static_cast<const char *>(Prologue ? "sub" : "add"),
                           Imm);

  Offset += 2;
  return false;
}

bool Decoder::opcode_1110110L(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  uint16_t GPRMask = ((OC[Offset + 0] & 0x01) << (Prologue ? 14 : 15))
                   | ((OC[Offset + 1] & 0xff) << 0);

  SW.startLine() << format("0x%02x 0x%02x           ; %s ", OC[Offset + 0],
                           OC[Offset + 1], Prologue ? "push" : "pop");
  printGPRMask(GPRMask);
  OS << '\n';

  Offset += 2;
  return false;
}

bool Decoder::opcode_11101110(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  assert(!Prologue && "may not be used in prologue");

  if (OC[Offset + 1] & 0xf0)
    SW.startLine() << format("0x%02x 0x%02x           ; reserved\n",
                             OC[Offset + 0], OC[Offset +  1]);
  else
    SW.startLine()
      << format("0x%02x 0x%02x           ; microsoft-specific (type: %u)\n",
                OC[Offset + 0], OC[Offset + 1], OC[Offset + 1] & 0x0f);

  Offset += 2;
  return false;
}

bool Decoder::opcode_11101111(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  if (OC[Offset + 1] & 0xf0)
    SW.startLine() << format("0x%02x 0x%02x           ; reserved\n",
                             OC[Offset + 0], OC[Offset +  1]);
  else if (Prologue)
    SW.startLine()
      << format("0x%02x 0x%02x           ; str.w lr, [sp, #-%u]!\n",
                OC[Offset + 0], OC[Offset + 1], OC[Offset + 1] << 2);
  else
    SW.startLine()
      << format("0x%02x 0x%02x           ; ldr.w lr, [sp], #%u\n",
                OC[Offset + 0], OC[Offset + 1], OC[Offset + 1] << 2);

  Offset += 2;
  return false;
}

bool Decoder::opcode_11110101(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  unsigned Start = (OC[Offset + 1] & 0xf0) >> 4;
  unsigned End = (OC[Offset + 1] & 0x0f) >> 0;
  uint32_t VFPMask = ((1 << (End + 1 - Start)) - 1) << Start;

  SW.startLine() << format("0x%02x 0x%02x           ; %s ", OC[Offset + 0],
                           OC[Offset + 1], Prologue ? "vpush" : "vpop");
  printVFPMask(VFPMask);
  OS << '\n';

  Offset += 2;
  return false;
}

bool Decoder::opcode_11110110(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  unsigned Start = (OC[Offset + 1] & 0xf0) >> 4;
  unsigned End = (OC[Offset + 1] & 0x0f) >> 0;
  uint32_t VFPMask = ((1 << (End + 1 - Start)) - 1) << (16 + Start);

  SW.startLine() << format("0x%02x 0x%02x           ; %s ", OC[Offset + 0],
                           OC[Offset + 1], Prologue ? "vpush" : "vpop");
  printVFPMask(VFPMask);
  OS << '\n';

  Offset += 2;
  return false;
}

bool Decoder::opcode_11110111(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  uint32_t Imm = (OC[Offset + 1] << 8) | (OC[Offset + 2] << 0);

  SW.startLine() << format("0x%02x 0x%02x 0x%02x      ; %s sp, sp, #(%u * 4)\n",
                           OC[Offset + 0], OC[Offset + 1], OC[Offset + 2],
                           static_cast<const char *>(Prologue ? "sub" : "add"),
                           Imm);

  Offset += 3;
  return false;
}

bool Decoder::opcode_11111000(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  uint32_t Imm = (OC[Offset + 1] << 16)
               | (OC[Offset + 2] << 8)
               | (OC[Offset + 3] << 0);

  SW.startLine()
    << format("0x%02x 0x%02x 0x%02x 0x%02x ; %s sp, sp, #(%u * 4)\n",
              OC[Offset + 0], OC[Offset + 1], OC[Offset + 2], OC[Offset + 3],
              static_cast<const char *>(Prologue ? "sub" : "add"), Imm);

  Offset += 4;
  return false;
}

bool Decoder::opcode_11111001(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  uint32_t Imm = (OC[Offset + 1] << 8) | (OC[Offset + 2] << 0);

  SW.startLine()
    << format("0x%02x 0x%02x 0x%02x      ; %s.w sp, sp, #(%u * 4)\n",
              OC[Offset + 0], OC[Offset + 1], OC[Offset + 2],
              static_cast<const char *>(Prologue ? "sub" : "add"), Imm);

  Offset += 3;
  return false;
}

bool Decoder::opcode_11111010(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  uint32_t Imm = (OC[Offset + 1] << 16)
               | (OC[Offset + 2] << 8)
               | (OC[Offset + 3] << 0);

  SW.startLine()
    << format("0x%02x 0x%02x 0x%02x 0x%02x ; %s.w sp, sp, #(%u * 4)\n",
              OC[Offset + 0], OC[Offset + 1], OC[Offset + 2], OC[Offset + 3],
              static_cast<const char *>(Prologue ? "sub" : "add"), Imm);

  Offset += 4;
  return false;
}

bool Decoder::opcode_11111011(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; nop\n", OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_11111100(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; nop.w\n", OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_11111101(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; bx <reg>\n", OC[Offset]);
  ++Offset;
  return true;
}

bool Decoder::opcode_11111110(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; b.w <target>\n", OC[Offset]);
  ++Offset;
  return true;
}

bool Decoder::opcode_11111111(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  ++Offset;
  return true;
}

// ARM64 unwind codes start here.
bool Decoder::opcode_alloc_s(const uint8_t *OC, unsigned &Offset,
                             unsigned Length, bool Prologue) {
  uint32_t NumBytes = (OC[Offset] & 0x1F) << 4;
  SW.startLine() << format("0x%02x                ; %s sp, #%u\n", OC[Offset],
                           static_cast<const char *>(Prologue ? "sub" : "add"),
                           NumBytes);
  ++Offset;
  return false;
}

bool Decoder::opcode_save_r19r20_x(const uint8_t *OC, unsigned &Offset,
                                   unsigned Length, bool Prologue) {
  uint32_t Off = (OC[Offset] & 0x1F) << 3;
  if (Prologue)
    SW.startLine() << format(
        "0x%02x                ; stp x19, x20, [sp, #-%u]!\n", OC[Offset], Off);
  else
    SW.startLine() << format(
        "0x%02x                ; ldp x19, x20, [sp], #%u\n", OC[Offset], Off);
  ++Offset;
  return false;
}

bool Decoder::opcode_save_fplr(const uint8_t *OC, unsigned &Offset,
                               unsigned Length, bool Prologue) {
  uint32_t Off = (OC[Offset] & 0x3F) << 3;
  SW.startLine() << format(
      "0x%02x                ; %s x29, x30, [sp, #%u]\n", OC[Offset],
      static_cast<const char *>(Prologue ? "stp" : "ldp"), Off);
  ++Offset;
  return false;
}

bool Decoder::opcode_save_fplr_x(const uint8_t *OC, unsigned &Offset,
                                 unsigned Length, bool Prologue) {
  uint32_t Off = ((OC[Offset] & 0x3F) + 1) << 3;
  if (Prologue)
    SW.startLine() << format(
        "0x%02x                ; stp x29, x30, [sp, #-%u]!\n", OC[Offset], Off);
  else
    SW.startLine() << format(
        "0x%02x                ; ldp x29, x30, [sp], #%u\n", OC[Offset], Off);
  ++Offset;
  return false;
}

bool Decoder::opcode_alloc_m(const uint8_t *OC, unsigned &Offset,
                             unsigned Length, bool Prologue) {
  uint32_t NumBytes = ((OC[Offset] & 0x07) << 8);
  NumBytes |= (OC[Offset + 1] & 0xFF);
  NumBytes <<= 4;
  SW.startLine() << format("0x%02x%02x              ; %s sp, #%u\n",
                           OC[Offset], OC[Offset + 1],
                           static_cast<const char *>(Prologue ? "sub" : "add"),
                           NumBytes);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_regp(const uint8_t *OC, unsigned &Offset,
                               unsigned Length, bool Prologue) {
  uint32_t Reg = ((OC[Offset] & 0x03) << 8);
  Reg |= (OC[Offset + 1] & 0xC0);
  Reg >>= 6;
  Reg += 19;
  uint32_t Off = (OC[Offset + 1] & 0x3F) << 3;
  SW.startLine() << format(
      "0x%02x%02x              ; %s x%u, x%u, [sp, #%u]\n",
      OC[Offset], OC[Offset + 1],
      static_cast<const char *>(Prologue ? "stp" : "ldp"), Reg, Reg + 1, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_regp_x(const uint8_t *OC, unsigned &Offset,
                                 unsigned Length, bool Prologue) {
  uint32_t Reg = ((OC[Offset] & 0x03) << 8);
  Reg |= (OC[Offset + 1] & 0xC0);
  Reg >>= 6;
  Reg += 19;
  uint32_t Off = ((OC[Offset + 1] & 0x3F) + 1) << 3;
  if (Prologue)
    SW.startLine() << format(
        "0x%02x%02x              ; stp x%u, x%u, [sp, #-%u]!\n",
        OC[Offset], OC[Offset + 1], Reg,
        Reg + 1, Off);
  else
    SW.startLine() << format(
        "0x%02x%02x              ; ldp x%u, x%u, [sp], #%u\n",
        OC[Offset], OC[Offset + 1], Reg,
        Reg + 1, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_reg(const uint8_t *OC, unsigned &Offset,
                              unsigned Length, bool Prologue) {
  uint32_t Reg = (OC[Offset] & 0x03) << 8;
  Reg |= (OC[Offset + 1] & 0xC0);
  Reg >>= 6;
  Reg += 19;
  uint32_t Off = (OC[Offset + 1] & 0x3F) << 3;
  SW.startLine() << format("0x%02x%02x              ; %s x%u, [sp, #%u]\n",
                           OC[Offset], OC[Offset + 1],
                           static_cast<const char *>(Prologue ? "str" : "ldr"),
                           Reg, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_reg_x(const uint8_t *OC, unsigned &Offset,
                                unsigned Length, bool Prologue) {
  uint32_t Reg = (OC[Offset] & 0x01) << 8;
  Reg |= (OC[Offset + 1] & 0xE0);
  Reg >>= 5;
  Reg += 19;
  uint32_t Off = ((OC[Offset + 1] & 0x1F) + 1) << 3;
  if (Prologue)
    SW.startLine() << format("0x%02x%02x              ; str x%u, [sp, #-%u]!\n",
                             OC[Offset], OC[Offset + 1], Reg, Off);
  else
    SW.startLine() << format("0x%02x%02x              ; ldr x%u, [sp], #%u\n",
                             OC[Offset], OC[Offset + 1], Reg, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_lrpair(const uint8_t *OC, unsigned &Offset,
                                 unsigned Length, bool Prologue) {
  uint32_t Reg = (OC[Offset] & 0x01) << 8;
  Reg |= (OC[Offset + 1] & 0xC0);
  Reg >>= 6;
  Reg *= 2;
  Reg += 19;
  uint32_t Off = (OC[Offset + 1] & 0x3F) << 3;
  SW.startLine() << format("0x%02x%02x              ; %s x%u, lr, [sp, #%u]\n",
                           OC[Offset], OC[Offset + 1],
                           static_cast<const char *>(Prologue ? "stp" : "ldp"),
                           Reg, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_fregp(const uint8_t *OC, unsigned &Offset,
                                unsigned Length, bool Prologue) {
  uint32_t Reg = (OC[Offset] & 0x01) << 8;
  Reg |= (OC[Offset + 1] & 0xC0);
  Reg >>= 6;
  Reg += 8;
  uint32_t Off = (OC[Offset + 1] & 0x3F) << 3;
  SW.startLine() << format("0x%02x%02x              ; %s d%u, d%u, [sp, #%u]\n",
                           OC[Offset], OC[Offset + 1],
                           static_cast<const char *>(Prologue ? "stp" : "ldp"),
                           Reg, Reg + 1, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_fregp_x(const uint8_t *OC, unsigned &Offset,
                                  unsigned Length, bool Prologue) {
  uint32_t Reg = (OC[Offset] & 0x01) << 8;
  Reg |= (OC[Offset + 1] & 0xC0);
  Reg >>= 6;
  Reg += 8;
  uint32_t Off = ((OC[Offset + 1] & 0x3F) + 1) << 3;
  if (Prologue)
    SW.startLine() << format(
        "0x%02x%02x              ; stp d%u, d%u, [sp, #-%u]!\n", OC[Offset],
        OC[Offset + 1], Reg, Reg + 1, Off);
  else
    SW.startLine() << format(
        "0x%02x%02x              ; ldp d%u, d%u, [sp], #%u\n", OC[Offset],
        OC[Offset + 1], Reg, Reg + 1, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_freg(const uint8_t *OC, unsigned &Offset,
                               unsigned Length, bool Prologue) {
  uint32_t Reg = (OC[Offset] & 0x01) << 8;
  Reg |= (OC[Offset + 1] & 0xC0);
  Reg >>= 6;
  Reg += 8;
  uint32_t Off = (OC[Offset + 1] & 0x3F) << 3;
  SW.startLine() << format("0x%02x%02x              ; %s d%u, [sp, #%u]\n",
                           OC[Offset], OC[Offset + 1],
                           static_cast<const char *>(Prologue ? "str" : "ldr"),
                           Reg, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_save_freg_x(const uint8_t *OC, unsigned &Offset,
                                 unsigned Length, bool Prologue) {
  uint32_t Reg = ((OC[Offset + 1] & 0xE0) >> 5) + 8;
  uint32_t Off = ((OC[Offset + 1] & 0x1F) + 1) << 3;
  if (Prologue)
    SW.startLine() << format(
        "0x%02x%02x              ; str d%u, [sp, #-%u]!\n", OC[Offset],
        OC[Offset + 1], Reg, Off);
  else
    SW.startLine() << format(
        "0x%02x%02x              ; ldr d%u, [sp], #%u\n", OC[Offset],
        OC[Offset + 1], Reg, Off);
  Offset += 2;
  return false;
}

bool Decoder::opcode_alloc_l(const uint8_t *OC, unsigned &Offset,
                             unsigned Length, bool Prologue) {
  unsigned Off =
      (OC[Offset + 1] << 16) | (OC[Offset + 2] << 8) | (OC[Offset + 3] << 0);
  Off <<= 4;
  SW.startLine() << format(
      "0x%02x%02x%02x%02x          ; %s sp, #%u\n", OC[Offset], OC[Offset + 1],
      OC[Offset + 2], OC[Offset + 3],
      static_cast<const char *>(Prologue ? "sub" : "add"), Off);
  Offset += 4;
  return false;
}

bool Decoder::opcode_setfp(const uint8_t *OC, unsigned &Offset, unsigned Length,
                           bool Prologue) {
  SW.startLine() << format("0x%02x                ; mov %s, %s\n", OC[Offset],
                           static_cast<const char *>(Prologue ? "fp" : "sp"),
                           static_cast<const char *>(Prologue ? "sp" : "fp"));
  ++Offset;
  return false;
}

bool Decoder::opcode_addfp(const uint8_t *OC, unsigned &Offset, unsigned Length,
                           bool Prologue) {
  unsigned NumBytes = OC[Offset + 1] << 3;
  SW.startLine() << format(
      "0x%02x%02x              ; %s %s, %s, #%u\n", OC[Offset], OC[Offset + 1],
      static_cast<const char *>(Prologue ? "add" : "sub"),
      static_cast<const char *>(Prologue ? "fp" : "sp"),
      static_cast<const char *>(Prologue ? "sp" : "fp"), NumBytes);
  Offset += 2;
  return false;
}

bool Decoder::opcode_nop(const uint8_t *OC, unsigned &Offset, unsigned Length,
                         bool Prologue) {
  SW.startLine() << format("0x%02x                ; nop\n", OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_end(const uint8_t *OC, unsigned &Offset, unsigned Length,
                         bool Prologue) {
  SW.startLine() << format("0x%02x                ; end\n", OC[Offset]);
  ++Offset;
  return true;
}

bool Decoder::opcode_end_c(const uint8_t *OC, unsigned &Offset, unsigned Length,
                           bool Prologue) {
  SW.startLine() << format("0x%02x                ; end_c\n", OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_save_next(const uint8_t *OC, unsigned &Offset,
                               unsigned Length, bool Prologue) {
  if (Prologue)
    SW.startLine() << format("0x%02x                ; save next\n", OC[Offset]);
  else
    SW.startLine() << format("0x%02x                ; restore next\n",
                             OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_save_any_reg(const uint8_t *OC, unsigned &Offset,
                                  unsigned Length, bool Prologue) {
  // Whether the instruction has writeback
  bool Writeback = (OC[Offset + 1] & 0x20) == 0x20;
  // Whether the instruction is paired.  (Paired instructions are required
  // to save/restore adjacent registers.)
  bool Paired = (OC[Offset + 1] & 0x40) == 0x40;
  // The kind of register saved:
  // - 0 is an x register
  // - 1 is the low half of a q register
  // - 2 is a whole q register
  int RegKind = (OC[Offset + 2] & 0xC0) >> 6;
  // Encoded register name (0 -> x0/q0, 1 -> x1/q1, etc.)
  int Reg = OC[Offset + 1] & 0x1F;
  // Encoded stack offset of load/store instruction; decoding varies by mode.
  int StackOffset = OC[Offset + 2] & 0x3F;
  if (Writeback)
    StackOffset++;
  if (!Writeback && !Paired && RegKind != 2)
    StackOffset *= 8;
  else
    StackOffset *= 16;

  SW.startLine() << format("0x%02x%02x%02x            ; ", OC[Offset],
                           OC[Offset + 1], OC[Offset + 2]);

  // Verify the encoding is in a form we understand.  The high bit of the first
  // byte, and mode 3 for the register kind are apparently reserved.  The
  // encoded register must refer to a valid register.
  int MaxReg = 0x1F;
  if (Paired)
    --MaxReg;
  if (RegKind == 0)
    --MaxReg;
  if ((OC[Offset + 1] & 0x80) == 0x80 || RegKind == 3 || Reg > MaxReg) {
    SW.getOStream() << "invalid save_any_reg encoding\n";
    Offset += 3;
    return false;
  }

  if (Paired) {
    if (Prologue)
      SW.getOStream() << "stp ";
    else
      SW.getOStream() << "ldp ";
  } else {
    if (Prologue)
      SW.getOStream() << "str ";
    else
      SW.getOStream() << "ldr ";
  }

  char RegChar = 'x';
  if (RegKind == 1) {
    RegChar = 'd';
  } else if (RegKind == 2) {
    RegChar = 'q';
  }

  if (Paired)
    SW.getOStream() << format("%c%d, %c%d, ", RegChar, Reg, RegChar, Reg + 1);
  else
    SW.getOStream() << format("%c%d, ", RegChar, Reg);

  if (Writeback) {
    if (Prologue)
      SW.getOStream() << format("[sp, #-%d]!\n", StackOffset);
    else
      SW.getOStream() << format("[sp], #%d\n", StackOffset);
  } else {
    SW.getOStream() << format("[sp, #%d]\n", StackOffset);
  }

  Offset += 3;
  return false;
}

bool Decoder::opcode_trap_frame(const uint8_t *OC, unsigned &Offset,
                                unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; trap frame\n", OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_machine_frame(const uint8_t *OC, unsigned &Offset,
                                   unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; machine frame\n",
                           OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_context(const uint8_t *OC, unsigned &Offset,
                             unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; context\n", OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_ec_context(const uint8_t *OC, unsigned &Offset,
                                unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; EC context\n", OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_clear_unwound_to_call(const uint8_t *OC, unsigned &Offset,
                                           unsigned Length, bool Prologue) {
  SW.startLine() << format("0x%02x                ; clear unwound to call\n",
                           OC[Offset]);
  ++Offset;
  return false;
}

bool Decoder::opcode_pac_sign_lr(const uint8_t *OC, unsigned &Offset,
                                 unsigned Length, bool Prologue) {
  if (Prologue)
    SW.startLine() << format("0x%02x                ; pacibsp\n", OC[Offset]);
  else
    SW.startLine() << format("0x%02x                ; autibsp\n", OC[Offset]);
  ++Offset;
  return false;
}

void Decoder::decodeOpcodes(ArrayRef<uint8_t> Opcodes, unsigned Offset,
                            bool Prologue) {
  assert((!Prologue || Offset == 0) && "prologue should always use offset 0");
  const RingEntry* DecodeRing = isAArch64 ? Ring64 : Ring;
  bool Terminated = false;
  for (unsigned OI = Offset, OE = Opcodes.size(); !Terminated && OI < OE; ) {
    for (unsigned DI = 0;; ++DI) {
      if ((isAArch64 && (DI >= std::size(Ring64))) ||
          (!isAArch64 && (DI >= std::size(Ring)))) {
        SW.startLine() << format("0x%02x                ; Bad opcode!\n",
                                 Opcodes.data()[OI]);
        ++OI;
        break;
      }

      if ((Opcodes[OI] & DecodeRing[DI].Mask) == DecodeRing[DI].Value) {
        if (OI + DecodeRing[DI].Length > OE) {
          SW.startLine() << format("Opcode 0x%02x goes past the unwind data\n",
                                    Opcodes[OI]);
          OI += DecodeRing[DI].Length;
          break;
        }
        Terminated =
            (this->*DecodeRing[DI].Routine)(Opcodes.data(), OI, 0, Prologue);
        break;
      }
    }
  }
}

bool Decoder::dumpXDataRecord(const COFFObjectFile &COFF,
                              const SectionRef &Section,
                              uint64_t FunctionAddress, uint64_t VA) {
  ArrayRef<uint8_t> Contents;
  if (COFF.getSectionContents(COFF.getCOFFSection(Section), Contents))
    return false;

  uint64_t SectionVA = Section.getAddress();
  uint64_t Offset = VA - SectionVA;
  const ulittle32_t *Data =
    reinterpret_cast<const ulittle32_t *>(Contents.data() + Offset);

  // Sanity check to ensure that the .xdata header is present.
  // A header is one or two words, followed by at least one word to describe
  // the unwind codes. Applicable to both ARM and AArch64.
  if (Contents.size() - Offset < 8)
    report_fatal_error(".xdata must be at least 8 bytes in size");

  const ExceptionDataRecord XData(Data, isAArch64);
  DictScope XRS(SW, "ExceptionData");
  SW.printNumber("FunctionLength",
                 isAArch64 ? XData.FunctionLengthInBytesAArch64() :
                 XData.FunctionLengthInBytesARM());
  SW.printNumber("Version", XData.Vers());
  SW.printBoolean("ExceptionData", XData.X());
  SW.printBoolean("EpiloguePacked", XData.E());
  if (!isAArch64)
    SW.printBoolean("Fragment", XData.F());
  SW.printNumber(XData.E() ? "EpilogueOffset" : "EpilogueScopes",
                 XData.EpilogueCount());
  uint64_t ByteCodeLength = XData.CodeWords() * sizeof(uint32_t);
  SW.printNumber("ByteCodeLength", ByteCodeLength);

  if ((int64_t)(Contents.size() - Offset - 4 * HeaderWords(XData) -
                (XData.E() ? 0 : XData.EpilogueCount() * 4) -
                (XData.X() ? 8 : 0)) < (int64_t)ByteCodeLength) {
    SW.flush();
    report_fatal_error("Malformed unwind data");
  }

  if (XData.E()) {
    ArrayRef<uint8_t> UC = XData.UnwindByteCode();
    {
      ListScope PS(SW, "Prologue");
      decodeOpcodes(UC, 0, /*Prologue=*/true);
    }
    if (XData.EpilogueCount()) {
      ListScope ES(SW, "Epilogue");
      decodeOpcodes(UC, XData.EpilogueCount(), /*Prologue=*/false);
    }
  } else {
    {
      ListScope PS(SW, "Prologue");
      decodeOpcodes(XData.UnwindByteCode(), 0, /*Prologue=*/true);
    }
    ArrayRef<ulittle32_t> EpilogueScopes = XData.EpilogueScopes();
    ListScope ESS(SW, "EpilogueScopes");
    for (const EpilogueScope ES : EpilogueScopes) {
      DictScope ESES(SW, "EpilogueScope");
      SW.printNumber("StartOffset", ES.EpilogueStartOffset());
      if (!isAArch64)
        SW.printNumber("Condition", ES.Condition());
      SW.printNumber("EpilogueStartIndex",
                     isAArch64 ? ES.EpilogueStartIndexAArch64()
                               : ES.EpilogueStartIndexARM());
      unsigned ReservedMask = isAArch64 ? 0xF : 0x3;
      if ((ES.ES >> 18) & ReservedMask)
        SW.printNumber("ReservedBits", (ES.ES >> 18) & ReservedMask);

      ListScope Opcodes(SW, "Opcodes");
      decodeOpcodes(XData.UnwindByteCode(),
                    isAArch64 ? ES.EpilogueStartIndexAArch64()
                              : ES.EpilogueStartIndexARM(),
                    /*Prologue=*/false);
    }
  }

  if (XData.X()) {
    const uint32_t Parameter = XData.ExceptionHandlerParameter();
    const size_t HandlerOffset = HeaderWords(XData) +
                                 (XData.E() ? 0 : XData.EpilogueCount()) +
                                 XData.CodeWords();

    uint64_t Address, SymbolOffset;
    ErrorOr<SymbolRef> Symbol = getSymbolForLocation(
        COFF, Section, Offset + HandlerOffset * sizeof(uint32_t),
        XData.ExceptionHandlerRVA(), Address, SymbolOffset,
        /*FunctionOnly=*/true);
    if (!Symbol) {
      ListScope EHS(SW, "ExceptionHandler");
      SW.printHex("Routine", Address);
      SW.printHex("Parameter", Parameter);
      return true;
    }

    Expected<StringRef> Name = Symbol->getName();
    if (!Name) {
      std::string Buf;
      llvm::raw_string_ostream OS(Buf);
      logAllUnhandledErrors(Name.takeError(), OS);
      report_fatal_error(Twine(OS.str()));
    }

    ListScope EHS(SW, "ExceptionHandler");
    SW.printString("Routine", formatSymbol(*Name, Address, SymbolOffset));
    SW.printHex("Parameter", Parameter);
  }

  return true;
}

bool Decoder::dumpUnpackedEntry(const COFFObjectFile &COFF,
                                const SectionRef Section, uint64_t Offset,
                                unsigned Index, const RuntimeFunction &RF) {
  assert(RF.Flag() == RuntimeFunctionFlag::RFF_Unpacked &&
         "packed entry cannot be treated as an unpacked entry");

  uint64_t FunctionAddress, FunctionOffset;
  ErrorOr<SymbolRef> Function = getSymbolForLocation(
      COFF, Section, Offset, RF.BeginAddress, FunctionAddress, FunctionOffset,
      /*FunctionOnly=*/true);

  uint64_t XDataAddress, XDataOffset;
  ErrorOr<SymbolRef> XDataRecord = getSymbolForLocation(
      COFF, Section, Offset + 4, RF.ExceptionInformationRVA(), XDataAddress,
      XDataOffset);

  if (!RF.BeginAddress && !Function)
    return false;
  if (!RF.UnwindData && !XDataRecord)
    return false;

  StringRef FunctionName;
  if (Function) {
    Expected<StringRef> FunctionNameOrErr = Function->getName();
    if (!FunctionNameOrErr) {
      std::string Buf;
      llvm::raw_string_ostream OS(Buf);
      logAllUnhandledErrors(FunctionNameOrErr.takeError(), OS);
      report_fatal_error(Twine(OS.str()));
    }
    FunctionName = *FunctionNameOrErr;
  }

  SW.printString("Function",
                 formatSymbol(FunctionName, FunctionAddress, FunctionOffset));

  if (XDataRecord) {
    Expected<StringRef> Name = XDataRecord->getName();
    if (!Name) {
      std::string Buf;
      llvm::raw_string_ostream OS(Buf);
      logAllUnhandledErrors(Name.takeError(), OS);
      report_fatal_error(Twine(OS.str()));
    }

    SW.printString("ExceptionRecord",
                   formatSymbol(*Name, XDataAddress, XDataOffset));

    Expected<section_iterator> SIOrErr = XDataRecord->getSection();
    if (!SIOrErr) {
      // TODO: Actually report errors helpfully.
      consumeError(SIOrErr.takeError());
      return false;
    }
    section_iterator SI = *SIOrErr;

    return dumpXDataRecord(COFF, *SI, FunctionAddress, XDataAddress);
  } else {
    SW.printString("ExceptionRecord", formatSymbol("", XDataAddress));

    ErrorOr<SectionRef> Section = getSectionContaining(COFF, XDataAddress);
    if (!Section)
      return false;

    return dumpXDataRecord(COFF, *Section, FunctionAddress, XDataAddress);
  }
}

bool Decoder::dumpPackedEntry(const object::COFFObjectFile &COFF,
                              const SectionRef Section, uint64_t Offset,
                              unsigned Index, const RuntimeFunction &RF) {
  assert((RF.Flag() == RuntimeFunctionFlag::RFF_Packed ||
          RF.Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
         "unpacked entry cannot be treated as a packed entry");

  uint64_t FunctionAddress, FunctionOffset;
  ErrorOr<SymbolRef> Function = getSymbolForLocation(
      COFF, Section, Offset, RF.BeginAddress, FunctionAddress, FunctionOffset,
      /*FunctionOnly=*/true);

  StringRef FunctionName;
  if (Function) {
    Expected<StringRef> FunctionNameOrErr = Function->getName();
    if (!FunctionNameOrErr) {
      std::string Buf;
      llvm::raw_string_ostream OS(Buf);
      logAllUnhandledErrors(FunctionNameOrErr.takeError(), OS);
      report_fatal_error(Twine(OS.str()));
    }
    FunctionName = *FunctionNameOrErr;
  }

  SW.printString("Function",
                 formatSymbol(FunctionName, FunctionAddress, FunctionOffset));
  SW.printBoolean("Fragment",
                  RF.Flag() == RuntimeFunctionFlag::RFF_PackedFragment);
  SW.printNumber("FunctionLength", RF.FunctionLength());
  SW.startLine() << "ReturnType: " << RF.Ret() << '\n';
  SW.printBoolean("HomedParameters", RF.H());
  SW.printNumber("Reg", RF.Reg());
  SW.printNumber("R", RF.R());
  SW.printBoolean("LinkRegister", RF.L());
  SW.printBoolean("Chaining", RF.C());
  SW.printNumber("StackAdjustment", StackAdjustment(RF) << 2);

  {
    ListScope PS(SW, "Prologue");

    uint16_t GPRMask, VFPMask;
    std::tie(GPRMask, VFPMask) = SavedRegisterMask(RF, /*Prologue=*/true);

    if (StackAdjustment(RF) && !PrologueFolding(RF))
      SW.startLine() << "sub sp, sp, #" << StackAdjustment(RF) * 4 << "\n";
    if (VFPMask) {
      SW.startLine() << "vpush ";
      printVFPMask(VFPMask);
      OS << "\n";
    }
    if (RF.C()) {
      // Count the number of registers pushed below R11
      int FpOffset = 4 * llvm::popcount(GPRMask & ((1U << 11) - 1));
      if (FpOffset)
        SW.startLine() << "add.w r11, sp, #" << FpOffset << "\n";
      else
        SW.startLine() << "mov r11, sp\n";
    }
    if (GPRMask) {
      SW.startLine() << "push ";
      printGPRMask(GPRMask);
      OS << "\n";
    }
    if (RF.H())
      SW.startLine() << "push {r0-r3}\n";
  }

  if (RF.Ret() != ReturnType::RT_NoEpilogue) {
    ListScope PS(SW, "Epilogue");

    uint16_t GPRMask, VFPMask;
    std::tie(GPRMask, VFPMask) = SavedRegisterMask(RF, /*Prologue=*/false);

    if (StackAdjustment(RF) && !EpilogueFolding(RF))
      SW.startLine() << "add sp, sp, #" << StackAdjustment(RF) * 4 << "\n";
    if (VFPMask) {
      SW.startLine() << "vpop ";
      printVFPMask(VFPMask);
      OS << "\n";
    }
    if (GPRMask) {
      SW.startLine() << "pop ";
      printGPRMask(GPRMask);
      OS << "\n";
    }
    if (RF.H()) {
      if (RF.L() == 0 || RF.Ret() != ReturnType::RT_POP)
        SW.startLine() << "add sp, sp, #16\n";
      else
        SW.startLine() << "ldr pc, [sp], #20\n";
    }
    if (RF.Ret() != ReturnType::RT_POP)
      SW.startLine() << RF.Ret() << '\n';
  }

  return true;
}

bool Decoder::dumpPackedARM64Entry(const object::COFFObjectFile &COFF,
                                   const SectionRef Section, uint64_t Offset,
                                   unsigned Index,
                                   const RuntimeFunctionARM64 &RF) {
  assert((RF.Flag() == RuntimeFunctionFlag::RFF_Packed ||
          RF.Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
         "unpacked entry cannot be treated as a packed entry");

  uint64_t FunctionAddress, FunctionOffset;
  ErrorOr<SymbolRef> Function = getSymbolForLocation(
      COFF, Section, Offset, RF.BeginAddress, FunctionAddress, FunctionOffset,
      /*FunctionOnly=*/true);

  StringRef FunctionName;
  if (Function) {
    Expected<StringRef> FunctionNameOrErr = Function->getName();
    if (!FunctionNameOrErr) {
      std::string Buf;
      llvm::raw_string_ostream OS(Buf);
      logAllUnhandledErrors(FunctionNameOrErr.takeError(), OS);
      report_fatal_error(Twine(OS.str()));
    }
    FunctionName = *FunctionNameOrErr;
  }

  SW.printString("Function",
                 formatSymbol(FunctionName, FunctionAddress, FunctionOffset));
  SW.printBoolean("Fragment",
                  RF.Flag() == RuntimeFunctionFlag::RFF_PackedFragment);
  SW.printNumber("FunctionLength", RF.FunctionLength());
  SW.printNumber("RegF", RF.RegF());
  SW.printNumber("RegI", RF.RegI());
  SW.printBoolean("HomedParameters", RF.H());
  SW.printNumber("CR", RF.CR());
  SW.printNumber("FrameSize", RF.FrameSize() << 4);
  ListScope PS(SW, "Prologue");

  // Synthesize the equivalent prologue according to the documentation
  // at https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling,
  // printed in reverse order compared to the docs, to match how prologues
  // are printed for the non-packed case.
  int IntSZ = 8 * RF.RegI();
  if (RF.CR() == 1)
    IntSZ += 8;
  int FpSZ = 8 * RF.RegF();
  if (RF.RegF())
    FpSZ += 8;
  int SavSZ = (IntSZ + FpSZ + 8 * 8 * RF.H() + 0xf) & ~0xf;
  int LocSZ = (RF.FrameSize() << 4) - SavSZ;

  if (RF.CR() == 2 || RF.CR() == 3) {
    SW.startLine() << "mov x29, sp\n";
    if (LocSZ <= 512) {
      SW.startLine() << format("stp x29, lr, [sp, #-%d]!\n", LocSZ);
    } else {
      SW.startLine() << "stp x29, lr, [sp, #0]\n";
    }
  }
  if (LocSZ > 4080) {
    SW.startLine() << format("sub sp, sp, #%d\n", LocSZ - 4080);
    SW.startLine() << "sub sp, sp, #4080\n";
  } else if ((RF.CR() != 3 && RF.CR() != 2 && LocSZ > 0) || LocSZ > 512) {
    SW.startLine() << format("sub sp, sp, #%d\n", LocSZ);
  }
  if (RF.H()) {
    SW.startLine() << format("stp x6, x7, [sp, #%d]\n", SavSZ - 16);
    SW.startLine() << format("stp x4, x5, [sp, #%d]\n", SavSZ - 32);
    SW.startLine() << format("stp x2, x3, [sp, #%d]\n", SavSZ - 48);
    if (RF.RegI() > 0 || RF.RegF() > 0 || RF.CR() == 1) {
      SW.startLine() << format("stp x0, x1, [sp, #%d]\n", SavSZ - 64);
    } else {
      // This case isn't documented; if neither RegI nor RegF nor CR=1
      // have decremented the stack pointer by SavSZ, we need to do it here
      // (as the final stack adjustment of LocSZ excludes SavSZ).
      SW.startLine() << format("stp x0, x1, [sp, #-%d]!\n", SavSZ);
    }
  }
  int FloatRegs = RF.RegF() > 0 ? RF.RegF() + 1 : 0;
  for (int I = (FloatRegs + 1) / 2 - 1; I >= 0; I--) {
    if (I == (FloatRegs + 1) / 2 - 1 && FloatRegs % 2 == 1) {
      // The last register, an odd register without a pair
      SW.startLine() << format("str d%d, [sp, #%d]\n", 8 + 2 * I,
                               IntSZ + 16 * I);
    } else if (I == 0 && RF.RegI() == 0 && RF.CR() != 1) {
      SW.startLine() << format("stp d%d, d%d, [sp, #-%d]!\n", 8 + 2 * I,
                               8 + 2 * I + 1, SavSZ);
    } else {
      SW.startLine() << format("stp d%d, d%d, [sp, #%d]\n", 8 + 2 * I,
                               8 + 2 * I + 1, IntSZ + 16 * I);
    }
  }
  if (RF.CR() == 1 && (RF.RegI() % 2) == 0) {
    if (RF.RegI() == 0)
      SW.startLine() << format("str lr, [sp, #-%d]!\n", SavSZ);
    else
      SW.startLine() << format("str lr, [sp, #%d]\n", IntSZ - 8);
  }
  for (int I = (RF.RegI() + 1) / 2 - 1; I >= 0; I--) {
    if (I == (RF.RegI() + 1) / 2 - 1 && RF.RegI() % 2 == 1) {
      // The last register, an odd register without a pair
      if (RF.CR() == 1) {
        if (I == 0) { // If this is the only register pair
          // CR=1 combined with RegI=1 doesn't map to a documented case;
          // it doesn't map to any regular unwind info opcode, and the
          // actual unwinder doesn't support it.
          SW.startLine() << "INVALID!\n";
        } else
          SW.startLine() << format("stp x%d, lr, [sp, #%d]\n", 19 + 2 * I,
                                   16 * I);
      } else {
        if (I == 0)
          SW.startLine() << format("str x%d, [sp, #-%d]!\n", 19 + 2 * I, SavSZ);
        else
          SW.startLine() << format("str x%d, [sp, #%d]\n", 19 + 2 * I, 16 * I);
      }
    } else if (I == 0) {
      // The first register pair
      SW.startLine() << format("stp x19, x20, [sp, #-%d]!\n", SavSZ);
    } else {
      SW.startLine() << format("stp x%d, x%d, [sp, #%d]\n", 19 + 2 * I,
                               19 + 2 * I + 1, 16 * I);
    }
  }
  // CR=2 is yet undocumented, see
  // https://github.com/MicrosoftDocs/cpp-docs/pull/4202 for upstream
  // progress on getting it documented.
  if (RF.CR() == 2)
    SW.startLine() << "pacibsp\n";
  SW.startLine() << "end\n";

  return true;
}

bool Decoder::dumpProcedureDataEntry(const COFFObjectFile &COFF,
                                     const SectionRef Section, unsigned Index,
                                     ArrayRef<uint8_t> Contents) {
  uint64_t Offset = PDataEntrySize * Index;
  const ulittle32_t *Data =
    reinterpret_cast<const ulittle32_t *>(Contents.data() + Offset);

  const RuntimeFunction Entry(Data);
  DictScope RFS(SW, "RuntimeFunction");
  if (Entry.Flag() == RuntimeFunctionFlag::RFF_Unpacked)
    return dumpUnpackedEntry(COFF, Section, Offset, Index, Entry);
  if (isAArch64) {
    const RuntimeFunctionARM64 EntryARM64(Data);
    return dumpPackedARM64Entry(COFF, Section, Offset, Index, EntryARM64);
  }
  return dumpPackedEntry(COFF, Section, Offset, Index, Entry);
}

void Decoder::dumpProcedureData(const COFFObjectFile &COFF,
                                const SectionRef Section) {
  ArrayRef<uint8_t> Contents;
  if (COFF.getSectionContents(COFF.getCOFFSection(Section), Contents))
    return;

  if (Contents.size() % PDataEntrySize) {
    errs() << ".pdata content is not " << PDataEntrySize << "-byte aligned\n";
    return;
  }

  for (unsigned EI = 0, EE = Contents.size() / PDataEntrySize; EI < EE; ++EI)
    if (!dumpProcedureDataEntry(COFF, Section, EI, Contents))
      break;
}

Error Decoder::dumpProcedureData(const COFFObjectFile &COFF) {
  for (const auto &Section : COFF.sections()) {
    Expected<StringRef> NameOrErr =
        COFF.getSectionName(COFF.getCOFFSection(Section));
    if (!NameOrErr)
      return NameOrErr.takeError();

    if (NameOrErr->starts_with(".pdata"))
      dumpProcedureData(COFF, Section);
  }
  return Error::success();
}
}
}
}
