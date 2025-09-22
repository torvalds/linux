//===-- llvm/BinaryFormat/XCOFF.cpp - The XCOFF file format -----*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

using namespace llvm;

#define SMC_CASE(A)                                                            \
  case XCOFF::XMC_##A:                                                         \
    return #A;
StringRef XCOFF::getMappingClassString(XCOFF::StorageMappingClass SMC) {
  switch (SMC) {
    SMC_CASE(PR)
    SMC_CASE(RO)
    SMC_CASE(DB)
    SMC_CASE(GL)
    SMC_CASE(XO)
    SMC_CASE(SV)
    SMC_CASE(SV64)
    SMC_CASE(SV3264)
    SMC_CASE(TI)
    SMC_CASE(TB)
    SMC_CASE(RW)
    SMC_CASE(TC0)
    SMC_CASE(TC)
    SMC_CASE(TD)
    SMC_CASE(DS)
    SMC_CASE(UA)
    SMC_CASE(BS)
    SMC_CASE(UC)
    SMC_CASE(TL)
    SMC_CASE(UL)
    SMC_CASE(TE)
#undef SMC_CASE
  }

  // TODO: need to add a test case for "Unknown" and other SMC.
  return "Unknown";
}

#define RELOC_CASE(A)                                                          \
  case XCOFF::A:                                                               \
    return #A;
StringRef XCOFF::getRelocationTypeString(XCOFF::RelocationType Type) {
  switch (Type) {
    RELOC_CASE(R_POS)
    RELOC_CASE(R_RL)
    RELOC_CASE(R_RLA)
    RELOC_CASE(R_NEG)
    RELOC_CASE(R_REL)
    RELOC_CASE(R_TOC)
    RELOC_CASE(R_TRL)
    RELOC_CASE(R_TRLA)
    RELOC_CASE(R_GL)
    RELOC_CASE(R_TCL)
    RELOC_CASE(R_REF)
    RELOC_CASE(R_BA)
    RELOC_CASE(R_BR)
    RELOC_CASE(R_RBA)
    RELOC_CASE(R_RBR)
    RELOC_CASE(R_TLS)
    RELOC_CASE(R_TLS_IE)
    RELOC_CASE(R_TLS_LD)
    RELOC_CASE(R_TLS_LE)
    RELOC_CASE(R_TLSM)
    RELOC_CASE(R_TLSML)
    RELOC_CASE(R_TOCU)
    RELOC_CASE(R_TOCL)
  }
  return "Unknown";
}
#undef RELOC_CASE

#define LANG_CASE(A)                                                           \
  case XCOFF::TracebackTable::A:                                               \
    return #A;

StringRef XCOFF::getNameForTracebackTableLanguageId(
    XCOFF::TracebackTable::LanguageID LangId) {
  switch (LangId) {
    LANG_CASE(C)
    LANG_CASE(Fortran)
    LANG_CASE(Pascal)
    LANG_CASE(Ada)
    LANG_CASE(PL1)
    LANG_CASE(Basic)
    LANG_CASE(Lisp)
    LANG_CASE(Cobol)
    LANG_CASE(Modula2)
    LANG_CASE(Rpg)
    LANG_CASE(PL8)
    LANG_CASE(Assembly)
    LANG_CASE(Java)
    LANG_CASE(ObjectiveC)
    LANG_CASE(CPlusPlus)
  }
  return "Unknown";
}
#undef LANG_CASE

Expected<SmallString<32>> XCOFF::parseParmsType(uint32_t Value,
                                                unsigned FixedParmsNum,
                                                unsigned FloatingParmsNum) {
  SmallString<32> ParmsType;
  int Bits = 0;
  unsigned ParsedFixedNum = 0;
  unsigned ParsedFloatingNum = 0;
  unsigned ParsedNum = 0;
  unsigned ParmsNum = FixedParmsNum + FloatingParmsNum;

  // In the function PPCFunctionInfo::getParmsType(), when there are no vector
  // parameters, the 31st bit of ParmsType is always zero even if it indicates a
  // floating point parameter. The parameter type information is lost. There
  // are only 8 GPRs used for parameters passing, the floating parameters
  // also occupy GPRs if there are available, so the 31st bit can never be a
  // fixed parameter. At the same time, we also do not know whether the zero of
  // the 31st bit indicates a float or double parameter type here. Therefore, we
  // ignore the 31st bit.
  while (Bits < 31 && ParsedNum < ParmsNum) {
    if (++ParsedNum > 1)
      ParmsType += ", ";
    if ((Value & TracebackTable::ParmTypeIsFloatingBit) == 0) {
      // Fixed parameter type.
      ParmsType += "i";
      ++ParsedFixedNum;
      Value <<= 1;
      ++Bits;
    } else {
      if ((Value & TracebackTable::ParmTypeFloatingIsDoubleBit) == 0)
        // Float parameter type.
        ParmsType += "f";
      else
        // Double parameter type.
        ParmsType += "d";
      ++ParsedFloatingNum;
      Value <<= 2;
      Bits += 2;
    }
  }

  // We have more parameters than the 32 Bits could encode.
  if (ParsedNum < ParmsNum)
    ParmsType += ", ...";

  if (Value != 0u || ParsedFixedNum > FixedParmsNum ||
      ParsedFloatingNum > FloatingParmsNum)
    return createStringError(errc::invalid_argument,
                             "ParmsType encodes can not map to ParmsNum "
                             "parameters in parseParmsType.");
  return ParmsType;
}

SmallString<32> XCOFF::getExtendedTBTableFlagString(uint8_t Flag) {
  SmallString<32> Res;

  if (Flag & ExtendedTBTableFlag::TB_OS1)
    Res += "TB_OS1 ";
  if (Flag & ExtendedTBTableFlag::TB_RESERVED)
    Res += "TB_RESERVED ";
  if (Flag & ExtendedTBTableFlag::TB_SSP_CANARY)
    Res += "TB_SSP_CANARY ";
  if (Flag & ExtendedTBTableFlag::TB_OS2)
    Res += "TB_OS2 ";
  if (Flag & ExtendedTBTableFlag::TB_EH_INFO)
    Res += "TB_EH_INFO ";
  if (Flag & ExtendedTBTableFlag::TB_LONGTBTABLE2)
    Res += "TB_LONGTBTABLE2 ";

  // Two of the bits that haven't got used in the mask.
  if (Flag & 0x06)
    Res += "Unknown ";

  // Pop the last space.
  Res.pop_back();
  return Res;
}

Expected<SmallString<32>>
XCOFF::parseParmsTypeWithVecInfo(uint32_t Value, unsigned FixedParmsNum,
                                 unsigned FloatingParmsNum,
                                 unsigned VectorParmsNum) {
  SmallString<32> ParmsType;

  unsigned ParsedFixedNum = 0;
  unsigned ParsedFloatingNum = 0;
  unsigned ParsedVectorNum = 0;
  unsigned ParsedNum = 0;
  unsigned ParmsNum = FixedParmsNum + FloatingParmsNum + VectorParmsNum;

  for (int Bits = 0; Bits < 32 && ParsedNum < ParmsNum; Bits += 2) {
    if (++ParsedNum > 1)
      ParmsType += ", ";

    switch (Value & TracebackTable::ParmTypeMask) {
    case TracebackTable::ParmTypeIsFixedBits:
      ParmsType += "i";
      ++ParsedFixedNum;
      break;
    case TracebackTable::ParmTypeIsVectorBits:
      ParmsType += "v";
      ++ParsedVectorNum;
      break;
    case TracebackTable::ParmTypeIsFloatingBits:
      ParmsType += "f";
      ++ParsedFloatingNum;
      break;
    case TracebackTable::ParmTypeIsDoubleBits:
      ParmsType += "d";
      ++ParsedFloatingNum;
      break;
    default:
      assert(false && "Unrecognized bits in ParmsType.");
    }
    Value <<= 2;
  }

  // We have more parameters than the 32 Bits could encode.
  if (ParsedNum < ParmsNum)
    ParmsType += ", ...";

  if (Value != 0u || ParsedFixedNum > FixedParmsNum ||
      ParsedFloatingNum > FloatingParmsNum || ParsedVectorNum > VectorParmsNum)
    return createStringError(
        errc::invalid_argument,
        "ParmsType encodes can not map to ParmsNum parameters "
        "in parseParmsTypeWithVecInfo.");

  return ParmsType;
}

Expected<SmallString<32>> XCOFF::parseVectorParmsType(uint32_t Value,
                                                      unsigned ParmsNum) {
  SmallString<32> ParmsType;
  unsigned ParsedNum = 0;
  for (int Bits = 0; ParsedNum < ParmsNum && Bits < 32; Bits += 2) {
    if (++ParsedNum > 1)
      ParmsType += ", ";
    switch (Value & TracebackTable::ParmTypeMask) {
    case TracebackTable::ParmTypeIsVectorCharBit:
      ParmsType += "vc";
      break;

    case TracebackTable::ParmTypeIsVectorShortBit:
      ParmsType += "vs";
      break;

    case TracebackTable::ParmTypeIsVectorIntBit:
      ParmsType += "vi";
      break;

    case TracebackTable::ParmTypeIsVectorFloatBit:
      ParmsType += "vf";
      break;
    }

    Value <<= 2;
  }

  // We have more parameters than the 32 Bits could encode.
  if (ParsedNum < ParmsNum)
    ParmsType += ", ...";

  if (Value != 0u)
    return createStringError(errc::invalid_argument,
                             "ParmsType encodes more than ParmsNum parameters "
                             "in parseVectorParmsType.");
  return ParmsType;
}
