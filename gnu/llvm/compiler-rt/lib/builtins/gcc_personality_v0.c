//===-- gcc_personality_v0.c - Implement __gcc_personality_v0 -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"
#include <stddef.h>

#include <unwind.h>
#if defined(__arm__) && !defined(__ARM_DWARF_EH__) &&                          \
    !defined(__USING_SJLJ_EXCEPTIONS__)
// When building with older compilers (e.g. clang <3.9), it is possible that we
// have a version of unwind.h which does not provide the EHABI declarations
// which are quired for the C personality to conform to the specification.  In
// order to provide forward compatibility for such compilers, we re-declare the
// necessary interfaces in the helper to permit a standalone compilation of the
// builtins (which contains the C unwinding personality for historical reasons).
#include "unwind-ehabi-helpers.h"
#endif

#if defined(__SEH__) && !defined(__USING_SJLJ_EXCEPTIONS__)
#include <windows.h>
#include <winnt.h>

EXCEPTION_DISPOSITION _GCC_specific_handler(PEXCEPTION_RECORD, void *, PCONTEXT,
                                            PDISPATCHER_CONTEXT,
                                            _Unwind_Personality_Fn);
#endif

// Pointer encodings documented at:
//   http://refspecs.freestandards.org/LSB_1.3.0/gLSB/gLSB/ehframehdr.html

#define DW_EH_PE_omit 0xff // no data follows

#define DW_EH_PE_absptr 0x00
#define DW_EH_PE_uleb128 0x01
#define DW_EH_PE_udata2 0x02
#define DW_EH_PE_udata4 0x03
#define DW_EH_PE_udata8 0x04
#define DW_EH_PE_sleb128 0x09
#define DW_EH_PE_sdata2 0x0A
#define DW_EH_PE_sdata4 0x0B
#define DW_EH_PE_sdata8 0x0C

#define DW_EH_PE_pcrel 0x10
#define DW_EH_PE_textrel 0x20
#define DW_EH_PE_datarel 0x30
#define DW_EH_PE_funcrel 0x40
#define DW_EH_PE_aligned 0x50
#define DW_EH_PE_indirect 0x80 // gcc extension

// read a uleb128 encoded value and advance pointer
static size_t readULEB128(const uint8_t **data) {
  size_t result = 0;
  size_t shift = 0;
  unsigned char byte;
  const uint8_t *p = *data;
  do {
    byte = *p++;
    result |= (byte & 0x7f) << shift;
    shift += 7;
  } while (byte & 0x80);
  *data = p;
  return result;
}

// read a pointer encoded value and advance pointer
static uintptr_t readEncodedPointer(const uint8_t **data, uint8_t encoding) {
  const uint8_t *p = *data;
  uintptr_t result = 0;

  if (encoding == DW_EH_PE_omit)
    return 0;

  // first get value
  switch (encoding & 0x0F) {
  case DW_EH_PE_absptr:
    result = *((const uintptr_t *)p);
    p += sizeof(uintptr_t);
    break;
  case DW_EH_PE_uleb128:
    result = readULEB128(&p);
    break;
  case DW_EH_PE_udata2:
    result = *((const uint16_t *)p);
    p += sizeof(uint16_t);
    break;
  case DW_EH_PE_udata4:
    result = *((const uint32_t *)p);
    p += sizeof(uint32_t);
    break;
  case DW_EH_PE_udata8:
    result = *((const uint64_t *)p);
    p += sizeof(uint64_t);
    break;
  case DW_EH_PE_sdata2:
    result = *((const int16_t *)p);
    p += sizeof(int16_t);
    break;
  case DW_EH_PE_sdata4:
    result = *((const int32_t *)p);
    p += sizeof(int32_t);
    break;
  case DW_EH_PE_sdata8:
    result = *((const int64_t *)p);
    p += sizeof(int64_t);
    break;
  case DW_EH_PE_sleb128:
  default:
    // not supported
    compilerrt_abort();
    break;
  }

  // then add relative offset
  switch (encoding & 0x70) {
  case DW_EH_PE_absptr:
    // do nothing
    break;
  case DW_EH_PE_pcrel:
    result += (uintptr_t)(*data);
    break;
  case DW_EH_PE_textrel:
  case DW_EH_PE_datarel:
  case DW_EH_PE_funcrel:
  case DW_EH_PE_aligned:
  default:
    // not supported
    compilerrt_abort();
    break;
  }

  // then apply indirection
  if (encoding & DW_EH_PE_indirect) {
    result = *((const uintptr_t *)result);
  }

  *data = p;
  return result;
}

#if defined(__arm__) && !defined(__USING_SJLJ_EXCEPTIONS__) &&                 \
    !defined(__ARM_DWARF_EH__) && !defined(__SEH__)
#define USING_ARM_EHABI 1
_Unwind_Reason_Code __gnu_unwind_frame(struct _Unwind_Exception *,
                                       struct _Unwind_Context *);
#endif

static inline _Unwind_Reason_Code
continueUnwind(struct _Unwind_Exception *exceptionObject,
               struct _Unwind_Context *context) {
#if USING_ARM_EHABI
  // On ARM EHABI the personality routine is responsible for actually
  // unwinding a single stack frame before returning (ARM EHABI Sec. 6.1).
  if (__gnu_unwind_frame(exceptionObject, context) != _URC_OK)
    return _URC_FAILURE;
#endif
  return _URC_CONTINUE_UNWIND;
}

// The C compiler makes references to __gcc_personality_v0 in
// the dwarf unwind information for translation units that use
// __attribute__((cleanup(xx))) on local variables.
// This personality routine is called by the system unwinder
// on each frame as the stack is unwound during a C++ exception
// throw through a C function compiled with -fexceptions.
#if __USING_SJLJ_EXCEPTIONS__
// the setjump-longjump based exceptions personality routine has a
// different name
COMPILER_RT_ABI _Unwind_Reason_Code __gcc_personality_sj0(
    int version, _Unwind_Action actions, uint64_t exceptionClass,
    struct _Unwind_Exception *exceptionObject, struct _Unwind_Context *context)
#elif USING_ARM_EHABI
// The ARM EHABI personality routine has a different signature.
COMPILER_RT_ABI _Unwind_Reason_Code __gcc_personality_v0(
    _Unwind_State state, struct _Unwind_Exception *exceptionObject,
    struct _Unwind_Context *context)
#elif defined(__SEH__)
static _Unwind_Reason_Code __gcc_personality_imp(
    int version, _Unwind_Action actions, uint64_t exceptionClass,
    struct _Unwind_Exception *exceptionObject, struct _Unwind_Context *context)
#else
COMPILER_RT_ABI _Unwind_Reason_Code __gcc_personality_v0(
    int version, _Unwind_Action actions, uint64_t exceptionClass,
    struct _Unwind_Exception *exceptionObject, struct _Unwind_Context *context)
#endif
{
  // Since C does not have catch clauses, there is nothing to do during
  // phase 1 (the search phase).
#if USING_ARM_EHABI
  // After resuming from a cleanup we should also continue on to the next
  // frame straight away.
  if ((state & _US_ACTION_MASK) != _US_UNWIND_FRAME_STARTING)
#else
  if (actions & _UA_SEARCH_PHASE)
#endif
    return continueUnwind(exceptionObject, context);

  // There is nothing to do if there is no LSDA for this frame.
  const uint8_t *lsda = (uint8_t *)_Unwind_GetLanguageSpecificData(context);
  if (lsda == (uint8_t *)0)
    return continueUnwind(exceptionObject, context);

  uintptr_t pc = (uintptr_t)_Unwind_GetIP(context) - 1;
  uintptr_t funcStart = (uintptr_t)_Unwind_GetRegionStart(context);
  uintptr_t pcOffset = pc - funcStart;

  // Parse LSDA header.
  uint8_t lpStartEncoding = *lsda++;
  if (lpStartEncoding != DW_EH_PE_omit) {
    readEncodedPointer(&lsda, lpStartEncoding);
  }
  uint8_t ttypeEncoding = *lsda++;
  if (ttypeEncoding != DW_EH_PE_omit) {
    readULEB128(&lsda);
  }
  // Walk call-site table looking for range that includes current PC.
  uint8_t callSiteEncoding = *lsda++;
  size_t callSiteTableLength = readULEB128(&lsda);
  const uint8_t *callSiteTableStart = lsda;
  const uint8_t *callSiteTableEnd = callSiteTableStart + callSiteTableLength;
  const uint8_t *p = callSiteTableStart;
  while (p < callSiteTableEnd) {
    uintptr_t start = readEncodedPointer(&p, callSiteEncoding);
    size_t length = readEncodedPointer(&p, callSiteEncoding);
    size_t landingPad = readEncodedPointer(&p, callSiteEncoding);
    readULEB128(&p); // action value not used for C code
    if (landingPad == 0)
      continue; // no landing pad for this entry
    if ((start <= pcOffset) && (pcOffset < (start + length))) {
      // Found landing pad for the PC.
      // Set Instruction Pointer to so we re-enter function
      // at landing pad. The landing pad is created by the compiler
      // to take two parameters in registers.
      _Unwind_SetGR(context, __builtin_eh_return_data_regno(0),
                    (uintptr_t)exceptionObject);
      _Unwind_SetGR(context, __builtin_eh_return_data_regno(1), 0);
      _Unwind_SetIP(context, (funcStart + landingPad));
      return _URC_INSTALL_CONTEXT;
    }
  }

  // No landing pad found, continue unwinding.
  return continueUnwind(exceptionObject, context);
}

#if defined(__SEH__) && !defined(__USING_SJLJ_EXCEPTIONS__)
COMPILER_RT_ABI EXCEPTION_DISPOSITION
__gcc_personality_seh0(PEXCEPTION_RECORD ms_exc, void *this_frame,
                       PCONTEXT ms_orig_context, PDISPATCHER_CONTEXT ms_disp) {
  return _GCC_specific_handler(ms_exc, this_frame, ms_orig_context, ms_disp,
                               __gcc_personality_imp);
}
#endif
