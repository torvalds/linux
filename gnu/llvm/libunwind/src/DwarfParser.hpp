//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Parses DWARF CFIs (FDEs and CIEs).
//
//===----------------------------------------------------------------------===//

#ifndef __DWARF_PARSER_HPP__
#define __DWARF_PARSER_HPP__

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "libunwind.h"
#include "dwarf2.h"
#include "Registers.hpp"

#include "config.h"

namespace libunwind {

/// CFI_Parser does basic parsing of a CFI (Call Frame Information) records.
/// See DWARF Spec for details:
///    http://refspecs.linuxbase.org/LSB_3.1.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
///
template <typename A>
class CFI_Parser {
public:
  typedef typename A::pint_t pint_t;

  /// Information encoded in a CIE (Common Information Entry)
  struct CIE_Info {
    pint_t    cieStart;
    pint_t    cieLength;
    pint_t    cieInstructions;
    uint8_t   pointerEncoding;
    uint8_t   lsdaEncoding;
    uint8_t   personalityEncoding;
    uint8_t   personalityOffsetInCIE;
    pint_t    personality;
    uint32_t  codeAlignFactor;
    int       dataAlignFactor;
    bool      isSignalFrame;
    bool      fdesHaveAugmentationData;
    uint8_t   returnAddressRegister;
#if defined(_LIBUNWIND_TARGET_AARCH64)
    bool      addressesSignedWithBKey;
    bool      mteTaggedFrame;
#endif
  };

  /// Information about an FDE (Frame Description Entry)
  struct FDE_Info {
    pint_t  fdeStart;
    pint_t  fdeLength;
    pint_t  fdeInstructions;
    pint_t  pcStart;
    pint_t  pcEnd;
    pint_t  lsda;
  };

  enum {
    kMaxRegisterNumber = _LIBUNWIND_HIGHEST_DWARF_REGISTER
  };
  enum RegisterSavedWhere {
    kRegisterUnused,
    kRegisterUndefined,
    kRegisterInCFA,
    kRegisterInCFADecrypt, // sparc64 specific
    kRegisterOffsetFromCFA,
    kRegisterInRegister,
    kRegisterAtExpression,
    kRegisterIsExpression
  };
  struct RegisterLocation {
    RegisterSavedWhere location;
    bool initialStateSaved;
    int64_t value;
  };
  /// Information about a frame layout and registers saved determined
  /// by "running" the DWARF FDE "instructions"
  struct PrologInfo {
    uint32_t          cfaRegister;
    int32_t           cfaRegisterOffset;  // CFA = (cfaRegister)+cfaRegisterOffset
    int64_t           cfaExpression;      // CFA = expression
    uint32_t          spExtraArgSize;
    RegisterLocation  savedRegisters[kMaxRegisterNumber + 1];
    enum class InitializeTime { kLazy, kNormal };

    // When saving registers, this data structure is lazily initialized.
    PrologInfo(InitializeTime IT = InitializeTime::kNormal) {
      if (IT == InitializeTime::kNormal)
        memset(this, 0, sizeof(*this));
    }
    void checkSaveRegister(uint64_t reg, PrologInfo &initialState) {
      if (!savedRegisters[reg].initialStateSaved) {
        initialState.savedRegisters[reg] = savedRegisters[reg];
        savedRegisters[reg].initialStateSaved = true;
      }
    }
    void setRegister(uint64_t reg, RegisterSavedWhere newLocation,
                     int64_t newValue, PrologInfo &initialState) {
      checkSaveRegister(reg, initialState);
      savedRegisters[reg].location = newLocation;
      savedRegisters[reg].value = newValue;
    }
    void setRegisterLocation(uint64_t reg, RegisterSavedWhere newLocation,
                             PrologInfo &initialState) {
      checkSaveRegister(reg, initialState);
      savedRegisters[reg].location = newLocation;
    }
    void setRegisterValue(uint64_t reg, int64_t newValue,
                          PrologInfo &initialState) {
      checkSaveRegister(reg, initialState);
      savedRegisters[reg].value = newValue;
    }
    void restoreRegisterToInitialState(uint64_t reg, PrologInfo &initialState) {
      if (savedRegisters[reg].initialStateSaved)
        savedRegisters[reg] = initialState.savedRegisters[reg];
      // else the register still holds its initial state
    }
  };

  struct PrologInfoStackEntry {
    PrologInfoStackEntry(PrologInfoStackEntry *n, const PrologInfo &i)
        : next(n), info(i) {}
    PrologInfoStackEntry *next;
    PrologInfo info;
  };

  struct RememberStack {
    PrologInfoStackEntry *entry;
    RememberStack() : entry(nullptr) {}
    ~RememberStack() {
#if defined(_LIBUNWIND_REMEMBER_CLEANUP_NEEDED)
      // Clean up rememberStack. Even in the case where every
      // DW_CFA_remember_state is paired with a DW_CFA_restore_state,
      // parseInstructions can skip restore opcodes if it reaches the target PC
      // and stops interpreting, so we have to make sure we don't leak memory.
      while (entry) {
        PrologInfoStackEntry *next = entry->next;
        _LIBUNWIND_REMEMBER_FREE(entry);
        entry = next;
      }
#endif
    }
  };

  static bool findFDE(A &addressSpace, pint_t pc, pint_t ehSectionStart,
                      size_t sectionLength, pint_t fdeHint, FDE_Info *fdeInfo,
                      CIE_Info *cieInfo);
  static const char *decodeFDE(A &addressSpace, pint_t fdeStart,
                               FDE_Info *fdeInfo, CIE_Info *cieInfo,
                               bool useCIEInfo = false);
  static bool parseFDEInstructions(A &addressSpace, const FDE_Info &fdeInfo,
                                   const CIE_Info &cieInfo, pint_t upToPC,
                                   int arch, PrologInfo *results);

  static const char *parseCIE(A &addressSpace, pint_t cie, CIE_Info *cieInfo);
};

/// Parse a FDE into a CIE_Info and an FDE_Info. If useCIEInfo is
/// true, treat cieInfo as already-parsed CIE_Info (whose start offset
/// must match the one specified by the FDE) rather than parsing the
/// one indicated within the FDE.
template <typename A>
const char *CFI_Parser<A>::decodeFDE(A &addressSpace, pint_t fdeStart,
                                     FDE_Info *fdeInfo, CIE_Info *cieInfo,
                                     bool useCIEInfo) {
  pint_t p = fdeStart;
  pint_t cfiLength = (pint_t)addressSpace.get32(p);
  p += 4;
  if (cfiLength == 0xffffffff) {
    // 0xffffffff means length is really next 8 bytes
    cfiLength = (pint_t)addressSpace.get64(p);
    p += 8;
  }
  if (cfiLength == 0)
    return "FDE has zero length"; // zero terminator
  uint32_t ciePointer = addressSpace.get32(p);
  if (ciePointer == 0)
    return "FDE is really a CIE"; // this is a CIE not an FDE
  pint_t nextCFI = p + cfiLength;
  pint_t cieStart = p - ciePointer;
  if (useCIEInfo) {
    if (cieInfo->cieStart != cieStart)
      return "CIE start does not match";
  } else {
    const char *err = parseCIE(addressSpace, cieStart, cieInfo);
    if (err != NULL)
      return err;
  }
  p += 4;
  // Parse pc begin and range.
  pint_t pcStart =
      addressSpace.getEncodedP(p, nextCFI, cieInfo->pointerEncoding);
  pint_t pcRange =
      addressSpace.getEncodedP(p, nextCFI, cieInfo->pointerEncoding & 0x0F);
  // Parse rest of info.
  fdeInfo->lsda = 0;
  // Check for augmentation length.
  if (cieInfo->fdesHaveAugmentationData) {
    pint_t augLen = (pint_t)addressSpace.getULEB128(p, nextCFI);
    pint_t endOfAug = p + augLen;
    if (cieInfo->lsdaEncoding != DW_EH_PE_omit) {
      // Peek at value (without indirection).  Zero means no LSDA.
      pint_t lsdaStart = p;
      if (addressSpace.getEncodedP(p, nextCFI, cieInfo->lsdaEncoding & 0x0F) !=
          0) {
        // Reset pointer and re-parse LSDA address.
        p = lsdaStart;
        fdeInfo->lsda =
            addressSpace.getEncodedP(p, nextCFI, cieInfo->lsdaEncoding);
      }
    }
    p = endOfAug;
  }
  fdeInfo->fdeStart = fdeStart;
  fdeInfo->fdeLength = nextCFI - fdeStart;
  fdeInfo->fdeInstructions = p;
  fdeInfo->pcStart = pcStart;
  fdeInfo->pcEnd = pcStart + pcRange;
  return NULL; // success
}

/// Scan an eh_frame section to find an FDE for a pc
template <typename A>
bool CFI_Parser<A>::findFDE(A &addressSpace, pint_t pc, pint_t ehSectionStart,
                            size_t sectionLength, pint_t fdeHint,
                            FDE_Info *fdeInfo, CIE_Info *cieInfo) {
  //fprintf(stderr, "findFDE(0x%llX)\n", (long long)pc);
  pint_t p = (fdeHint != 0) ? fdeHint : ehSectionStart;
  const pint_t ehSectionEnd = (sectionLength == SIZE_MAX)
                                  ? static_cast<pint_t>(-1)
                                  : (ehSectionStart + sectionLength);
  while (p < ehSectionEnd) {
    pint_t currentCFI = p;
    //fprintf(stderr, "findFDE() CFI at 0x%llX\n", (long long)p);
    pint_t cfiLength = addressSpace.get32(p);
    p += 4;
    if (cfiLength == 0xffffffff) {
      // 0xffffffff means length is really next 8 bytes
      cfiLength = (pint_t)addressSpace.get64(p);
      p += 8;
    }
    if (cfiLength == 0)
      return false; // zero terminator
    uint32_t id = addressSpace.get32(p);
    if (id == 0) {
      // Skip over CIEs.
      p += cfiLength;
    } else {
      // Process FDE to see if it covers pc.
      pint_t nextCFI = p + cfiLength;
      uint32_t ciePointer = addressSpace.get32(p);
      pint_t cieStart = p - ciePointer;
      // Validate pointer to CIE is within section.
      if ((ehSectionStart <= cieStart) && (cieStart < ehSectionEnd)) {
        if (parseCIE(addressSpace, cieStart, cieInfo) == NULL) {
          p += 4;
          // Parse pc begin and range.
          pint_t pcStart =
              addressSpace.getEncodedP(p, nextCFI, cieInfo->pointerEncoding);
          pint_t pcRange = addressSpace.getEncodedP(
              p, nextCFI, cieInfo->pointerEncoding & 0x0F);
          // Test if pc is within the function this FDE covers.
          if ((pcStart < pc) && (pc <= pcStart + pcRange)) {
            // parse rest of info
            fdeInfo->lsda = 0;
            // check for augmentation length
            if (cieInfo->fdesHaveAugmentationData) {
              pint_t augLen = (pint_t)addressSpace.getULEB128(p, nextCFI);
              pint_t endOfAug = p + augLen;
              if (cieInfo->lsdaEncoding != DW_EH_PE_omit) {
                // Peek at value (without indirection).  Zero means no LSDA.
                pint_t lsdaStart = p;
                if (addressSpace.getEncodedP(
                        p, nextCFI, cieInfo->lsdaEncoding & 0x0F) != 0) {
                  // Reset pointer and re-parse LSDA address.
                  p = lsdaStart;
                  fdeInfo->lsda = addressSpace
                      .getEncodedP(p, nextCFI, cieInfo->lsdaEncoding);
                }
              }
              p = endOfAug;
            }
            fdeInfo->fdeStart = currentCFI;
            fdeInfo->fdeLength = nextCFI - currentCFI;
            fdeInfo->fdeInstructions = p;
            fdeInfo->pcStart = pcStart;
            fdeInfo->pcEnd = pcStart + pcRange;
            return true;
          } else {
            // pc is not in begin/range, skip this FDE
          }
        } else {
          // Malformed CIE, now augmentation describing pc range encoding.
        }
      } else {
        // malformed FDE.  CIE is bad
      }
      p = nextCFI;
    }
  }
  return false;
}

/// Extract info from a CIE
template <typename A>
const char *CFI_Parser<A>::parseCIE(A &addressSpace, pint_t cie,
                                    CIE_Info *cieInfo) {
  cieInfo->pointerEncoding = 0;
  cieInfo->lsdaEncoding = DW_EH_PE_omit;
  cieInfo->personalityEncoding = 0;
  cieInfo->personalityOffsetInCIE = 0;
  cieInfo->personality = 0;
  cieInfo->codeAlignFactor = 0;
  cieInfo->dataAlignFactor = 0;
  cieInfo->isSignalFrame = false;
  cieInfo->fdesHaveAugmentationData = false;
#if defined(_LIBUNWIND_TARGET_AARCH64)
  cieInfo->addressesSignedWithBKey = false;
  cieInfo->mteTaggedFrame = false;
#endif
  cieInfo->cieStart = cie;
  pint_t p = cie;
  pint_t cieLength = (pint_t)addressSpace.get32(p);
  p += 4;
  pint_t cieContentEnd = p + cieLength;
  if (cieLength == 0xffffffff) {
    // 0xffffffff means length is really next 8 bytes
    cieLength = (pint_t)addressSpace.get64(p);
    p += 8;
    cieContentEnd = p + cieLength;
  }
  if (cieLength == 0)
    return NULL;
  // CIE ID is always 0
  if (addressSpace.get32(p) != 0)
    return "CIE ID is not zero";
  p += 4;
  // Version is always 1 or 3
  uint8_t version = addressSpace.get8(p);
  if ((version != 1) && (version != 3))
    return "CIE version is not 1 or 3";
  ++p;
  // save start of augmentation string and find end
  pint_t strStart = p;
  while (addressSpace.get8(p) != 0)
    ++p;
  ++p;
  // parse code alignment factor
  cieInfo->codeAlignFactor = (uint32_t)addressSpace.getULEB128(p, cieContentEnd);
  // parse data alignment factor
  cieInfo->dataAlignFactor = (int)addressSpace.getSLEB128(p, cieContentEnd);
  // parse return address register
  uint64_t raReg = (version == 1) ? addressSpace.get8(p++)
                                  : addressSpace.getULEB128(p, cieContentEnd);
  assert(raReg < 255 && "return address register too large");
  cieInfo->returnAddressRegister = (uint8_t)raReg;
  // parse augmentation data based on augmentation string
  const char *result = NULL;
  if (addressSpace.get8(strStart) == 'z') {
    // parse augmentation data length
    addressSpace.getULEB128(p, cieContentEnd);
    for (pint_t s = strStart; addressSpace.get8(s) != '\0'; ++s) {
      switch (addressSpace.get8(s)) {
      case 'z':
        cieInfo->fdesHaveAugmentationData = true;
        break;
      case 'P':
        cieInfo->personalityEncoding = addressSpace.get8(p);
        ++p;
        cieInfo->personalityOffsetInCIE = (uint8_t)(p - cie);
        cieInfo->personality = addressSpace
            .getEncodedP(p, cieContentEnd, cieInfo->personalityEncoding);
        break;
      case 'L':
        cieInfo->lsdaEncoding = addressSpace.get8(p);
        ++p;
        break;
      case 'R':
        cieInfo->pointerEncoding = addressSpace.get8(p);
        ++p;
        break;
      case 'S':
        cieInfo->isSignalFrame = true;
        break;
#if defined(_LIBUNWIND_TARGET_AARCH64)
      case 'B':
        cieInfo->addressesSignedWithBKey = true;
        break;
      case 'G':
        cieInfo->mteTaggedFrame = true;
        break;
#endif
      default:
        // ignore unknown letters
        break;
      }
    }
  }
  cieInfo->cieLength = cieContentEnd - cieInfo->cieStart;
  cieInfo->cieInstructions = p;
  return result;
}


/// "run" the DWARF instructions and create the abstract PrologInfo for an FDE
template <typename A>
bool CFI_Parser<A>::parseFDEInstructions(A &addressSpace,
                                         const FDE_Info &fdeInfo,
                                         const CIE_Info &cieInfo, pint_t upToPC,
                                         int arch, PrologInfo *results) {
  // Alloca is used for the allocation of the rememberStack entries. It removes
  // the dependency on new/malloc but the below for loop can not be refactored
  // into functions. Entry could be saved during the processing of a CIE and
  // restored by an FDE.
  RememberStack rememberStack;

  struct ParseInfo {
    pint_t instructions;
    pint_t instructionsEnd;
    pint_t pcoffset;
  };

  ParseInfo parseInfoArray[] = {
      {cieInfo.cieInstructions, cieInfo.cieStart + cieInfo.cieLength,
       (pint_t)(-1)},
      {fdeInfo.fdeInstructions, fdeInfo.fdeStart + fdeInfo.fdeLength,
       upToPC - fdeInfo.pcStart}};

  for (const auto &info : parseInfoArray) {
    pint_t p = info.instructions;
    pint_t instructionsEnd = info.instructionsEnd;
    pint_t pcoffset = info.pcoffset;
    pint_t codeOffset = 0;

    // initialState initialized as registers in results are modified. Use
    // PrologInfo accessor functions to avoid reading uninitialized data.
    PrologInfo initialState(PrologInfo::InitializeTime::kLazy);

    _LIBUNWIND_TRACE_DWARF("parseFDEInstructions(instructions=0x%0" PRIx64
                           ")\n",
                           static_cast<uint64_t>(instructionsEnd));

    // see DWARF Spec, section 6.4.2 for details on unwind opcodes
    while ((p < instructionsEnd) && (codeOffset < pcoffset)) {
      uint64_t reg;
      uint64_t reg2;
      int64_t offset;
      uint64_t length;
      uint8_t opcode = addressSpace.get8(p);
      uint8_t operand;

      ++p;
      switch (opcode) {
      case DW_CFA_nop:
        _LIBUNWIND_TRACE_DWARF("DW_CFA_nop\n");
        break;
      case DW_CFA_set_loc:
        codeOffset = addressSpace.getEncodedP(p, instructionsEnd,
                                              cieInfo.pointerEncoding);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_set_loc\n");
        break;
      case DW_CFA_advance_loc1:
        codeOffset += (addressSpace.get8(p) * cieInfo.codeAlignFactor);
        p += 1;
        _LIBUNWIND_TRACE_DWARF("DW_CFA_advance_loc1: new offset=%" PRIu64 "\n",
                               static_cast<uint64_t>(codeOffset));
        break;
      case DW_CFA_advance_loc2:
        codeOffset += (addressSpace.get16(p) * cieInfo.codeAlignFactor);
        p += 2;
        _LIBUNWIND_TRACE_DWARF("DW_CFA_advance_loc2: new offset=%" PRIu64 "\n",
                               static_cast<uint64_t>(codeOffset));
        break;
      case DW_CFA_advance_loc4:
        codeOffset += (addressSpace.get32(p) * cieInfo.codeAlignFactor);
        p += 4;
        _LIBUNWIND_TRACE_DWARF("DW_CFA_advance_loc4: new offset=%" PRIu64 "\n",
                               static_cast<uint64_t>(codeOffset));
        break;
      case DW_CFA_offset_extended:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        offset = (int64_t)addressSpace.getULEB128(p, instructionsEnd) *
                 cieInfo.dataAlignFactor;
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_offset_extended DWARF unwind, reg too big");
          return false;
        }
        results->setRegister(reg, kRegisterInCFA, offset, initialState);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_offset_extended(reg=%" PRIu64 ", "
                               "offset=%" PRId64 ")\n",
                               reg, offset);
        break;
      case DW_CFA_restore_extended:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_restore_extended DWARF unwind, reg too big");
          return false;
        }
        results->restoreRegisterToInitialState(reg, initialState);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_restore_extended(reg=%" PRIu64 ")\n",
                               reg);
        break;
      case DW_CFA_undefined:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_undefined DWARF unwind, reg too big");
          return false;
        }
        results->setRegisterLocation(reg, kRegisterUndefined, initialState);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_undefined(reg=%" PRIu64 ")\n", reg);
        break;
      case DW_CFA_same_value:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_same_value DWARF unwind, reg too big");
          return false;
        }
        // <rdar://problem/8456377> DW_CFA_same_value unsupported
        // "same value" means register was stored in frame, but its current
        // value has not changed, so no need to restore from frame.
        // We model this as if the register was never saved.
        results->setRegisterLocation(reg, kRegisterUnused, initialState);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_same_value(reg=%" PRIu64 ")\n", reg);
        break;
      case DW_CFA_register:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        reg2 = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_register DWARF unwind, reg too big");
          return false;
        }
        if (reg2 > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_register DWARF unwind, reg2 too big");
          return false;
        }
        results->setRegister(reg, kRegisterInRegister, (int64_t)reg2,
                             initialState);
        _LIBUNWIND_TRACE_DWARF(
            "DW_CFA_register(reg=%" PRIu64 ", reg2=%" PRIu64 ")\n", reg, reg2);
        break;
      case DW_CFA_remember_state: {
        // Avoid operator new because that would be an upward dependency.
        // Avoid malloc because it needs heap allocation.
        PrologInfoStackEntry *entry =
            (PrologInfoStackEntry *)_LIBUNWIND_REMEMBER_ALLOC(
                sizeof(PrologInfoStackEntry));
        if (entry != NULL) {
          entry->next = rememberStack.entry;
          entry->info = *results;
          rememberStack.entry = entry;
        } else {
          return false;
        }
        _LIBUNWIND_TRACE_DWARF("DW_CFA_remember_state\n");
        break;
      }
      case DW_CFA_restore_state:
        if (rememberStack.entry != NULL) {
          PrologInfoStackEntry *top = rememberStack.entry;
          *results = top->info;
          rememberStack.entry = top->next;
          _LIBUNWIND_REMEMBER_FREE(top);
        } else {
          return false;
        }
        _LIBUNWIND_TRACE_DWARF("DW_CFA_restore_state\n");
        break;
      case DW_CFA_def_cfa:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        offset = (int64_t)addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0("malformed DW_CFA_def_cfa DWARF unwind, reg too big");
          return false;
        }
        results->cfaRegister = (uint32_t)reg;
        results->cfaRegisterOffset = (int32_t)offset;
        _LIBUNWIND_TRACE_DWARF("DW_CFA_def_cfa(reg=%" PRIu64 ", offset=%" PRIu64
                               ")\n",
                               reg, offset);
        break;
      case DW_CFA_def_cfa_register:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_def_cfa_register DWARF unwind, reg too big");
          return false;
        }
        results->cfaRegister = (uint32_t)reg;
        _LIBUNWIND_TRACE_DWARF("DW_CFA_def_cfa_register(%" PRIu64 ")\n", reg);
        break;
      case DW_CFA_def_cfa_offset:
        results->cfaRegisterOffset =
            (int32_t)addressSpace.getULEB128(p, instructionsEnd);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_def_cfa_offset(%d)\n",
                               results->cfaRegisterOffset);
        break;
      case DW_CFA_def_cfa_expression:
        results->cfaRegister = 0;
        results->cfaExpression = (int64_t)p;
        length = addressSpace.getULEB128(p, instructionsEnd);
        assert(length < static_cast<pint_t>(~0) && "pointer overflow");
        p += static_cast<pint_t>(length);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_def_cfa_expression(expression=0x%" PRIx64
                               ", length=%" PRIu64 ")\n",
                               results->cfaExpression, length);
        break;
      case DW_CFA_expression:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_expression DWARF unwind, reg too big");
          return false;
        }
        results->setRegister(reg, kRegisterAtExpression, (int64_t)p,
                             initialState);
        length = addressSpace.getULEB128(p, instructionsEnd);
        assert(length < static_cast<pint_t>(~0) && "pointer overflow");
        p += static_cast<pint_t>(length);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_expression(reg=%" PRIu64 ", "
                               "expression=0x%" PRIx64 ", "
                               "length=%" PRIu64 ")\n",
                               reg, results->savedRegisters[reg].value, length);
        break;
      case DW_CFA_offset_extended_sf:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_offset_extended_sf DWARF unwind, reg too big");
          return false;
        }
        offset = addressSpace.getSLEB128(p, instructionsEnd) *
                 cieInfo.dataAlignFactor;
        results->setRegister(reg, kRegisterInCFA, offset, initialState);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_offset_extended_sf(reg=%" PRIu64 ", "
                               "offset=%" PRId64 ")\n",
                               reg, offset);
        break;
      case DW_CFA_def_cfa_sf:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        offset = addressSpace.getSLEB128(p, instructionsEnd) *
                 cieInfo.dataAlignFactor;
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_def_cfa_sf DWARF unwind, reg too big");
          return false;
        }
        results->cfaRegister = (uint32_t)reg;
        results->cfaRegisterOffset = (int32_t)offset;
        _LIBUNWIND_TRACE_DWARF("DW_CFA_def_cfa_sf(reg=%" PRIu64 ", "
                               "offset=%" PRId64 ")\n",
                               reg, offset);
        break;
      case DW_CFA_def_cfa_offset_sf:
        results->cfaRegisterOffset =
            (int32_t)(addressSpace.getSLEB128(p, instructionsEnd) *
                      cieInfo.dataAlignFactor);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_def_cfa_offset_sf(%d)\n",
                               results->cfaRegisterOffset);
        break;
      case DW_CFA_val_offset:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG(
              "malformed DW_CFA_val_offset DWARF unwind, reg (%" PRIu64
              ") out of range\n",
              reg);
          return false;
        }
        offset = (int64_t)addressSpace.getULEB128(p, instructionsEnd) *
                 cieInfo.dataAlignFactor;
        results->setRegister(reg, kRegisterOffsetFromCFA, offset, initialState);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_val_offset(reg=%" PRIu64 ", "
                               "offset=%" PRId64 "\n",
                               reg, offset);
        break;
      case DW_CFA_val_offset_sf:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_val_offset_sf DWARF unwind, reg too big");
          return false;
        }
        offset = addressSpace.getSLEB128(p, instructionsEnd) *
                 cieInfo.dataAlignFactor;
        results->setRegister(reg, kRegisterOffsetFromCFA, offset, initialState);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_val_offset_sf(reg=%" PRIu64 ", "
                               "offset=%" PRId64 "\n",
                               reg, offset);
        break;
      case DW_CFA_val_expression:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0(
              "malformed DW_CFA_val_expression DWARF unwind, reg too big");
          return false;
        }
        results->setRegister(reg, kRegisterIsExpression, (int64_t)p,
                             initialState);
        length = addressSpace.getULEB128(p, instructionsEnd);
        assert(length < static_cast<pint_t>(~0) && "pointer overflow");
        p += static_cast<pint_t>(length);
        _LIBUNWIND_TRACE_DWARF("DW_CFA_val_expression(reg=%" PRIu64 ", "
                               "expression=0x%" PRIx64 ", length=%" PRIu64
                               ")\n",
                               reg, results->savedRegisters[reg].value, length);
        break;
      case DW_CFA_GNU_args_size:
        length = addressSpace.getULEB128(p, instructionsEnd);
        results->spExtraArgSize = (uint32_t)length;
        _LIBUNWIND_TRACE_DWARF("DW_CFA_GNU_args_size(%" PRIu64 ")\n", length);
        break;
      case DW_CFA_GNU_negative_offset_extended:
        reg = addressSpace.getULEB128(p, instructionsEnd);
        if (reg > kMaxRegisterNumber) {
          _LIBUNWIND_LOG0("malformed DW_CFA_GNU_negative_offset_extended DWARF "
                          "unwind, reg too big");
          return false;
        }
        offset = (int64_t)addressSpace.getULEB128(p, instructionsEnd) *
                 cieInfo.dataAlignFactor;
        results->setRegister(reg, kRegisterInCFA, -offset, initialState);
        _LIBUNWIND_TRACE_DWARF(
            "DW_CFA_GNU_negative_offset_extended(%" PRId64 ")\n", offset);
        break;

#if defined(_LIBUNWIND_TARGET_AARCH64) || defined(_LIBUNWIND_TARGET_SPARC) || \
    defined(_LIBUNWIND_TARGET_SPARC64)
        // The same constant is used to represent different instructions on
        // AArch64 (negate_ra_state) and SPARC (window_save).
        static_assert(DW_CFA_AARCH64_negate_ra_state == DW_CFA_GNU_window_save,
                      "uses the same constant");
      case DW_CFA_AARCH64_negate_ra_state:
        switch (arch) {
#if defined(_LIBUNWIND_TARGET_AARCH64)
        case REGISTERS_ARM64: {
          int64_t value =
              results->savedRegisters[UNW_AARCH64_RA_SIGN_STATE].value ^ 0x1;
          results->setRegisterValue(UNW_AARCH64_RA_SIGN_STATE, value,
                                    initialState);
          _LIBUNWIND_TRACE_DWARF("DW_CFA_AARCH64_negate_ra_state\n");
        } break;
#endif

#if defined(_LIBUNWIND_TARGET_SPARC)
        // case DW_CFA_GNU_window_save:
        case REGISTERS_SPARC:
          _LIBUNWIND_TRACE_DWARF("DW_CFA_GNU_window_save()\n");
          for (reg = UNW_SPARC_O0; reg <= UNW_SPARC_O7; reg++) {
            results->setRegister(reg, kRegisterInRegister,
                                 ((int64_t)reg - UNW_SPARC_O0) + UNW_SPARC_I0,
                                 initialState);
          }

          for (reg = UNW_SPARC_L0; reg <= UNW_SPARC_I7; reg++) {
            results->setRegister(reg, kRegisterInCFA,
                                 ((int64_t)reg - UNW_SPARC_L0) * 4,
                                 initialState);
          }
          break;
#endif

#if defined(_LIBUNWIND_TARGET_SPARC64)
        // case DW_CFA_GNU_window_save:
        case REGISTERS_SPARC64:
          // Don't save %o0-%o7 on sparc64.
          // https://reviews.llvm.org/D32450#736405

          for (reg = UNW_SPARC_L0; reg <= UNW_SPARC_I7; reg++) {
            if (reg == UNW_SPARC_I7)
              results->setRegister(
                  reg, kRegisterInCFADecrypt,
                  static_cast<int64_t>((reg - UNW_SPARC_L0) * sizeof(pint_t)),
                  initialState);
            else
              results->setRegister(
                  reg, kRegisterInCFA,
                  static_cast<int64_t>((reg - UNW_SPARC_L0) * sizeof(pint_t)),
                  initialState);
          }
          _LIBUNWIND_TRACE_DWARF("DW_CFA_GNU_window_save\n");
          break;
#endif
        }
        break;

#else
        (void)arch;
#endif

      default:
        operand = opcode & 0x3F;
        switch (opcode & 0xC0) {
        case DW_CFA_offset:
          reg = operand;
          if (reg > kMaxRegisterNumber) {
            _LIBUNWIND_LOG("malformed DW_CFA_offset DWARF unwind, reg (%" PRIu64
                           ") out of range",
                           reg);
            return false;
          }
          offset = (int64_t)addressSpace.getULEB128(p, instructionsEnd) *
                   cieInfo.dataAlignFactor;
          results->setRegister(reg, kRegisterInCFA, offset, initialState);
          _LIBUNWIND_TRACE_DWARF("DW_CFA_offset(reg=%d, offset=%" PRId64 ")\n",
                                 operand, offset);
          break;
        case DW_CFA_advance_loc:
          codeOffset += operand * cieInfo.codeAlignFactor;
          _LIBUNWIND_TRACE_DWARF("DW_CFA_advance_loc: new offset=%" PRIu64 "\n",
                                 static_cast<uint64_t>(codeOffset));
          break;
        case DW_CFA_restore:
          reg = operand;
          if (reg > kMaxRegisterNumber) {
            _LIBUNWIND_LOG(
                "malformed DW_CFA_restore DWARF unwind, reg (%" PRIu64
                ") out of range",
                reg);
            return false;
          }
          results->restoreRegisterToInitialState(reg, initialState);
          _LIBUNWIND_TRACE_DWARF("DW_CFA_restore(reg=%" PRIu64 ")\n",
                                 static_cast<uint64_t>(operand));
          break;
        default:
          _LIBUNWIND_TRACE_DWARF("unknown CFA opcode 0x%02X\n", opcode);
          return false;
        }
      }
    }
  }
  return true;
}

} // namespace libunwind

#endif // __DWARF_PARSER_HPP__
