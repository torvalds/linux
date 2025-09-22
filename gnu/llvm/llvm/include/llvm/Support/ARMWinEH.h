//===-- llvm/Support/ARMWinEH.h - Windows on ARM EH Constants ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ARMWINEH_H
#define LLVM_SUPPORT_ARMWINEH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace ARM {
namespace WinEH {
enum class RuntimeFunctionFlag {
  RFF_Unpacked,       /// unpacked entry
  RFF_Packed,         /// packed entry
  RFF_PackedFragment, /// packed entry representing a fragment
  RFF_Reserved,       /// reserved
};

enum class ReturnType {
  RT_POP,             /// return via pop {pc} (L flag must be set)
  RT_B,               /// 16-bit branch
  RT_BW,              /// 32-bit branch
  RT_NoEpilogue,      /// no epilogue (fragment)
};

/// RuntimeFunction - An entry in the table of procedure data (.pdata)
///
/// This is ARM specific, but the Function Start RVA, Flag and
/// ExceptionInformationRVA fields work identically for ARM64.
///
///  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
///  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
/// +---------------------------------------------------------------+
/// |                     Function Start RVA                        |
/// +-------------------+-+-+-+-----+-+---+---------------------+---+
/// |    Stack Adjust   |C|L|R| Reg |H|Ret|   Function Length   |Flg|
/// +-------------------+-+-+-+-----+-+---+---------------------+---+
///
/// Flag : 2-bit field with the following meanings:
///   - 00 = packed unwind data not used; reamining bits point to .xdata record
///   - 01 = packed unwind data
///   - 10 = packed unwind data, function assumed to have no prologue; useful
///          for function fragments that are discontiguous with the start of the
///          function
///   - 11 = reserved
/// Function Length : 11-bit field providing the length of the entire function
///                   in bytes, divided by 2; if the function is greater than
///                   4KB, a full .xdata record must be used instead
/// Ret : 2-bit field indicating how the function returns
///   - 00 = return via pop {pc} (the L bit must be set)
///   - 01 = return via 16-bit branch
///   - 10 = return via 32-bit branch
///   - 11 = no epilogue; useful for function fragments that may only contain a
///          prologue but the epilogue is elsewhere
/// H : 1-bit flag indicating whether the function "homes" the integer parameter
///     registers (r0-r3), allocating 16-bytes on the stack
/// Reg : 3-bit field indicating the index of the last saved non-volatile
///       register.  If the R bit is set to 0, then only integer registers are
///       saved (r4-rN, where N is 4 + Reg).  If the R bit is set to 1, then
///       only floating-point registers are being saved (d8-dN, where N is
///       8 + Reg).  The special case of the R bit being set to 1 and Reg equal
///       to 7 indicates that no registers are saved.
/// R : 1-bit flag indicating whether the non-volatile registers are integer or
///     floating-point.  0 indicates integer, 1 indicates floating-point.  The
///     special case of the R-flag being set and Reg being set to 7 indicates
///     that no non-volatile registers are saved.
/// L : 1-bit flag indicating whether the function saves/restores the link
///     register (LR)
/// C : 1-bit flag indicating whether the function includes extra instructions
///     to setup a frame chain for fast walking.  If this flag is set, r11 is
///     implicitly added to the list of saved non-volatile integer registers.
/// Stack Adjust : 10-bit field indicating the number of bytes of stack that are
///                allocated for this function.  Only values between 0x000 and
///                0x3f3 can be directly encoded.  If the value is 0x3f4 or
///                greater, then the low 4 bits have special meaning as follows:
///                - Bit 0-1
///                  indicate the number of words' of adjustment (1-4), minus 1
///                - Bit 2
///                  indicates if the prologue combined adjustment into push
///                - Bit 3
///                  indicates if the epilogue combined adjustment into pop
///
/// RESTRICTIONS:
///   - IF C is SET:
///     + L flag must be set since frame chaining requires r11 and lr
///     + r11 must NOT be included in the set of registers described by Reg
///   - IF Ret is 0:
///     + L flag must be set

// NOTE: RuntimeFunction is meant to be a simple class that provides raw access
// to all fields in the structure.  The accessor methods reflect the names of
// the bitfields that they correspond to.  Although some obvious simplifications
// are possible via merging of methods, it would prevent the use of this class
// to fully inspect the contents of the data structure which is particularly
// useful for scenarios such as llvm-readobj to aid in testing.

class RuntimeFunction {
public:
  const support::ulittle32_t BeginAddress;
  const support::ulittle32_t UnwindData;

  RuntimeFunction(const support::ulittle32_t *Data)
    : BeginAddress(Data[0]), UnwindData(Data[1]) {}

  RuntimeFunction(const support::ulittle32_t BeginAddress,
                  const support::ulittle32_t UnwindData)
    : BeginAddress(BeginAddress), UnwindData(UnwindData) {}

  RuntimeFunctionFlag Flag() const {
    return RuntimeFunctionFlag(UnwindData & 0x3);
  }

  uint32_t ExceptionInformationRVA() const {
    assert(Flag() == RuntimeFunctionFlag::RFF_Unpacked &&
           "unpacked form required for this operation");
    return (UnwindData & ~0x3);
  }

  uint32_t PackedUnwindData() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return (UnwindData & ~0x3);
  }
  uint32_t FunctionLength() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return (((UnwindData & 0x00001ffc) >> 2) << 1);
  }
  ReturnType Ret() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    assert(((UnwindData & 0x00006000) || L()) && "L must be set to 1");
    return ReturnType((UnwindData & 0x00006000) >> 13);
  }
  bool H() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0x00008000) >> 15);
  }
  uint8_t Reg() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0x00070000) >> 16);
  }
  bool R() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0x00080000) >> 19);
  }
  bool L() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0x00100000) >> 20);
  }
  bool C() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    assert(((~UnwindData & 0x00200000) || L()) &&
           "L flag must be set, chaining requires r11 and LR");
    assert(((~UnwindData & 0x00200000) || (Reg() < 7) || R()) &&
           "r11 must not be included in Reg; C implies r11");
    return ((UnwindData & 0x00200000) >> 21);
  }
  uint16_t StackAdjust() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0xffc00000) >> 22);
  }
};

/// PrologueFolding - pseudo-flag derived from Stack Adjust indicating that the
/// prologue has stack adjustment combined into the push
inline bool PrologueFolding(const RuntimeFunction &RF) {
  return RF.StackAdjust() >= 0x3f4 && (RF.StackAdjust() & 0x4);
}
/// Epilogue - pseudo-flag derived from Stack Adjust indicating that the
/// epilogue has stack adjustment combined into the pop
inline bool EpilogueFolding(const RuntimeFunction &RF) {
  return RF.StackAdjust() >= 0x3f4 && (RF.StackAdjust() & 0x8);
}
/// StackAdjustment - calculated stack adjustment in words.  The stack
/// adjustment should be determined via this function to account for the special
/// handling the special encoding when the value is >= 0x3f4.
inline uint16_t StackAdjustment(const RuntimeFunction &RF) {
  uint16_t Adjustment = RF.StackAdjust();
  if (Adjustment >= 0x3f4)
    return (Adjustment & 0x3) + 1;
  return Adjustment;
}

/// SavedRegisterMask - Utility function to calculate the set of saved general
/// purpose (r0-r15) and VFP (d0-d31) registers.
std::pair<uint16_t, uint32_t> SavedRegisterMask(const RuntimeFunction &RF,
                                                bool Prologue = true);

/// RuntimeFunctionARM64 - An entry in the table of procedure data (.pdata)
///
///  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
///  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
/// +---------------------------------------------------------------+
/// |                     Function Start RVA                        |
/// +-----------------+---+-+-------+-----+---------------------+---+
/// |    Frame Size   |CR |H| RegI  |RegF |   Function Length   |Flg|
/// +-----------------+---+-+-------+-----+---------------------+---+
///
/// See https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling
/// for the full reference for this struct.

class RuntimeFunctionARM64 {
public:
  const support::ulittle32_t BeginAddress;
  const support::ulittle32_t UnwindData;

  RuntimeFunctionARM64(const support::ulittle32_t *Data)
      : BeginAddress(Data[0]), UnwindData(Data[1]) {}

  RuntimeFunctionARM64(const support::ulittle32_t BeginAddress,
                       const support::ulittle32_t UnwindData)
      : BeginAddress(BeginAddress), UnwindData(UnwindData) {}

  RuntimeFunctionFlag Flag() const {
    return RuntimeFunctionFlag(UnwindData & 0x3);
  }

  uint32_t ExceptionInformationRVA() const {
    assert(Flag() == RuntimeFunctionFlag::RFF_Unpacked &&
           "unpacked form required for this operation");
    return (UnwindData & ~0x3);
  }

  uint32_t PackedUnwindData() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return (UnwindData & ~0x3);
  }
  uint32_t FunctionLength() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return (((UnwindData & 0x00001ffc) >> 2) << 2);
  }
  uint8_t RegF() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0x0000e000) >> 13);
  }
  uint8_t RegI() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0x000f0000) >> 16);
  }
  bool H() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0x00100000) >> 20);
  }
  uint8_t CR() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0x600000) >> 21);
  }
  uint16_t FrameSize() const {
    assert((Flag() == RuntimeFunctionFlag::RFF_Packed ||
            Flag() == RuntimeFunctionFlag::RFF_PackedFragment) &&
           "packed form required for this operation");
    return ((UnwindData & 0xff800000) >> 23);
  }
};

/// ExceptionDataRecord - An entry in the table of exception data (.xdata)
///
/// The format on ARM is:
///
///  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
///  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
/// +-------+---------+-+-+-+---+-----------------------------------+
/// | C Wrd | Epi Cnt |F|E|X|Ver|         Function Length           |
/// +-------+--------+'-'-'-'---'---+-------------------------------+
/// |    Reserved    |Ex. Code Words|   (Extended Epilogue Count)   |
/// +-------+--------+--------------+-------------------------------+
///
/// The format on ARM64 is:
///
///  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
///  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
/// +---------+---------+-+-+---+-----------------------------------+
/// |  C Wrd  | Epi Cnt |E|X|Ver|         Function Length           |
/// +---------+------+--'-'-'---'---+-------------------------------+
/// |    Reserved    |Ex. Code Words|   (Extended Epilogue Count)   |
/// +-------+--------+--------------+-------------------------------+
///
/// Function Length : 18-bit field indicating the total length of the function
///                   in bytes divided by 2.  If a function is larger than
///                   512KB, then multiple pdata and xdata records must be used.
/// Vers : 2-bit field describing the version of the remaining structure.  Only
///        version 0 is currently defined (values 1-3 are not permitted).
/// X : 1-bit field indicating the presence of exception data
/// E : 1-bit field indicating that the single epilogue is packed into the
///     header
/// F : 1-bit field indicating that the record describes a function fragment
///     (implies that no prologue is present, and prologue processing should be
///     skipped) (ARM only)
/// Epilogue Count : 5-bit field that differs in meaning based on the E field.
///
///                  If E is set, then this field specifies the index of the
///                  first unwind code describing the (only) epilogue.
///
///                  Otherwise, this field indicates the number of exception
///                  scopes.  If more than 31 scopes exist, then this field and
///                  the Code Words field must both be set to 0 to indicate that
///                  an extension word is required.
/// Code Words : 4-bit (5-bit on ARM64) field that specifies the number of
///              32-bit words needed to contain all the unwind codes.  If more
///              than 15 words (31 words on ARM64) are required, then this field
///              and the Epilogue Count field must both be set to 0 to indicate
///              that an extension word is required.
/// Extended Epilogue Count, Extended Code Words :
///                          Valid only if Epilog Count and Code Words are both
///                          set to 0.  Provides an 8-bit extended code word
///                          count and 16-bits for epilogue count
///
/// The epilogue scope format on ARM is:
///
///  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
///  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
/// +----------------+------+---+---+-------------------------------+
/// |  Ep Start Idx  | Cond |Res|       Epilogue Start Offset       |
/// +----------------+------+---+-----------------------------------+
///
/// The epilogue scope format on ARM64 is:
///
///  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
///  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
/// +-------------------+-------+---+-------------------------------+
/// |  Ep Start Idx     |  Res  |   Epilogue Start Offset           |
/// +-------------------+-------+-----------------------------------+
///
/// If the E bit is unset in the header, the header is followed by a series of
/// epilogue scopes, which are sorted by their offset.
///
/// Epilogue Start Offset: 18-bit field encoding the offset of epilogue relative
///                        to the start of the function in bytes divided by two
/// Res : 2-bit field reserved for future expansion (must be set to 0)
/// Condition : (ARM only) 4-bit field providing the condition under which the
///             epilogue is executed.  Unconditional epilogues should set this
///             field to 0xe. Epilogues must be entirely conditional or
///             unconditional, and in Thumb-2 mode.  The epilogue begins with
///             the first instruction after the IT opcode.
/// Epilogue Start Index : 8-bit field indicating the byte index of the first
///                        unwind code describing the epilogue
///
///  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
///  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
/// +---------------+---------------+---------------+---------------+
/// | Unwind Code 3 | Unwind Code 2 | Unwind Code 1 | Unwind Code 0 |
/// +---------------+---------------+---------------+---------------+
///
/// Following the epilogue scopes, the byte code describing the unwinding
/// follows.  This is padded to align up to word alignment.  Bytes are stored in
/// little endian.
///
///  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
///  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
/// +---------------------------------------------------------------+
/// |           Exception Handler RVA (requires X = 1)              |
/// +---------------------------------------------------------------+
/// |  (possibly followed by data required for exception handler)   |
/// +---------------------------------------------------------------+
///
/// If the X bit is set in the header, the unwind byte code is followed by the
/// exception handler information.  This constants of one Exception Handler RVA
/// which is the address to the exception handler, followed immediately by the
/// variable length data associated with the exception handler.
///

struct EpilogueScope {
  const support::ulittle32_t ES;

  EpilogueScope(const support::ulittle32_t Data) : ES(Data) {}
  // Same for both ARM and AArch64.
  uint32_t EpilogueStartOffset() const {
    return (ES & 0x0003ffff);
  }

  // Different implementations for ARM and AArch64.
  uint8_t ResARM() const {
    return ((ES & 0x000c0000) >> 18);
  }

  uint8_t ResAArch64() const {
    return ((ES & 0x000f0000) >> 18);
  }

  // Condition is only applicable to ARM.
  uint8_t Condition() const {
    return ((ES & 0x00f00000) >> 20);
  }

  // Different implementations for ARM and AArch64.
  uint8_t EpilogueStartIndexARM() const {
    return ((ES & 0xff000000) >> 24);
  }

  uint16_t EpilogueStartIndexAArch64() const {
    return ((ES & 0xffc00000) >> 22);
  }
};

struct ExceptionDataRecord;
inline size_t HeaderWords(const ExceptionDataRecord &XR);

struct ExceptionDataRecord {
  const support::ulittle32_t *Data;
  bool isAArch64;

  ExceptionDataRecord(const support::ulittle32_t *Data, bool isAArch64) :
    Data(Data), isAArch64(isAArch64) {}

  uint32_t FunctionLength() const {
    return (Data[0] & 0x0003ffff);
  }

  uint32_t FunctionLengthInBytesARM() const {
    return FunctionLength() << 1;
  }

  uint32_t FunctionLengthInBytesAArch64() const {
    return FunctionLength() << 2;
  }

  uint8_t Vers() const {
    return (Data[0] & 0x000C0000) >> 18;
  }

  bool X() const {
    return ((Data[0] & 0x00100000) >> 20);
  }

  bool E() const {
    return ((Data[0] & 0x00200000) >> 21);
  }

  bool F() const {
    assert(!isAArch64 && "Fragments are only supported on ARMv7 WinEH");
    return ((Data[0] & 0x00400000) >> 22);
  }

  uint16_t EpilogueCount() const {
    if (HeaderWords(*this) == 1) {
      if (isAArch64)
        return (Data[0] & 0x07C00000) >> 22;
      return (Data[0] & 0x0f800000) >> 23;
    }
    return Data[1] & 0x0000ffff;
  }

  uint8_t CodeWords() const {
    if (HeaderWords(*this) == 1) {
      if (isAArch64)
        return (Data[0] & 0xf8000000) >> 27;
      return (Data[0] & 0xf0000000) >> 28;
    }
    return (Data[1] & 0x00ff0000) >> 16;
  }

  ArrayRef<support::ulittle32_t> EpilogueScopes() const {
    assert(E() == 0 && "epilogue scopes are only present when the E bit is 0");
    size_t Offset = HeaderWords(*this);
    return ArrayRef(&Data[Offset], EpilogueCount());
  }

  ArrayRef<uint8_t> UnwindByteCode() const {
    const size_t Offset = HeaderWords(*this)
                        + (E() ? 0 :  EpilogueCount());
    const uint8_t *ByteCode =
      reinterpret_cast<const uint8_t *>(&Data[Offset]);
    return ArrayRef(ByteCode, CodeWords() * sizeof(uint32_t));
  }

  uint32_t ExceptionHandlerRVA() const {
    assert(X() && "Exception Handler RVA is only valid if the X bit is set");
    return Data[HeaderWords(*this) + (E() ? 0 : EpilogueCount()) + CodeWords()];
  }

  uint32_t ExceptionHandlerParameter() const {
    assert(X() && "Exception Handler RVA is only valid if the X bit is set");
    return Data[HeaderWords(*this) + (E() ? 0 : EpilogueCount()) + CodeWords() +
                1];
  }
};

inline size_t HeaderWords(const ExceptionDataRecord &XR) {
  if (XR.isAArch64)
    return (XR.Data[0] & 0xffc00000) ? 1 : 2;
  return (XR.Data[0] & 0xff800000) ? 1 : 2;
}
}
}
}

#endif
