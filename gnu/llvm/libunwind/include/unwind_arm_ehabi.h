//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
// C++ ABI Level 1 ABI documented at:
//   https://github.com/ARM-software/abi-aa/blob/main/ehabi32/ehabi32.rst
//
//===----------------------------------------------------------------------===//

#ifndef __ARM_EHABI_UNWIND_H__
#define __ARM_EHABI_UNWIND_H__

typedef uint32_t _Unwind_State;

static const _Unwind_State _US_VIRTUAL_UNWIND_FRAME   = 0;
static const _Unwind_State _US_UNWIND_FRAME_STARTING  = 1;
static const _Unwind_State _US_UNWIND_FRAME_RESUME    = 2;
static const _Unwind_State _US_ACTION_MASK            = 3;
/* Undocumented flag for force unwinding. */
static const _Unwind_State _US_FORCE_UNWIND           = 8;

typedef uint32_t _Unwind_EHT_Header;

struct _Unwind_Control_Block;
typedef struct _Unwind_Control_Block _Unwind_Control_Block;
#define _Unwind_Exception _Unwind_Control_Block /* Alias */
typedef uint8_t _Unwind_Exception_Class[8];

struct _Unwind_Control_Block {
  _Unwind_Exception_Class exception_class;
  void (*exception_cleanup)(_Unwind_Reason_Code, _Unwind_Control_Block*);

  /* Unwinder cache, private fields for the unwinder's use */
  struct {
    uint32_t reserved1; /* init reserved1 to 0, then don't touch */
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t reserved5;
  } unwinder_cache;

  /* Propagation barrier cache (valid after phase 1): */
  struct {
    uint32_t sp;
    uint32_t bitpattern[5];
  } barrier_cache;

  /* Cleanup cache (preserved over cleanup): */
  struct {
    uint32_t bitpattern[4];
  } cleanup_cache;

  /* Pr cache (for pr's benefit): */
  struct {
    uint32_t fnstart; /* function start address */
    _Unwind_EHT_Header* ehtp; /* pointer to EHT entry header word */
    uint32_t additional;
    uint32_t reserved1;
  } pr_cache;

  long long int :0; /* Enforce the 8-byte alignment */
} __attribute__((__aligned__(8)));

typedef _Unwind_Reason_Code (*_Unwind_Personality_Fn)(
    _Unwind_State state, _Unwind_Exception *exceptionObject,
    struct _Unwind_Context *context);

#ifdef __cplusplus
extern "C" {
#endif

//
// The following are the base functions documented by the C++ ABI
//
#ifdef __USING_SJLJ_EXCEPTIONS__
extern _Unwind_Reason_Code
    _Unwind_SjLj_RaiseException(_Unwind_Exception *exception_object);
extern void _Unwind_SjLj_Resume(_Unwind_Exception *exception_object);
#else
extern _Unwind_Reason_Code
    _Unwind_RaiseException(_Unwind_Exception *exception_object);
extern void _Unwind_Resume(_Unwind_Exception *exception_object);
#endif
extern void _Unwind_DeleteException(_Unwind_Exception *exception_object);

typedef enum {
  _UVRSC_CORE = 0,  /* integer register */
  _UVRSC_VFP = 1,   /* vfp */
  _UVRSC_WMMXD = 3, /* Intel WMMX data register */
  _UVRSC_WMMXC = 4, /* Intel WMMX control register */
  _UVRSC_PSEUDO = 5 /* Special purpose pseudo register */
} _Unwind_VRS_RegClass;

typedef enum {
  _UVRSD_UINT32 = 0,
  _UVRSD_VFPX = 1,
  _UVRSD_UINT64 = 3,
  _UVRSD_FLOAT = 4,
  _UVRSD_DOUBLE = 5
} _Unwind_VRS_DataRepresentation;

typedef enum {
  _UVRSR_OK = 0,
  _UVRSR_NOT_IMPLEMENTED = 1,
  _UVRSR_FAILED = 2
} _Unwind_VRS_Result;

extern void _Unwind_Complete(_Unwind_Exception* exception_object);

extern _Unwind_VRS_Result
_Unwind_VRS_Get(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t regno, _Unwind_VRS_DataRepresentation representation,
                void *valuep);

extern _Unwind_VRS_Result
_Unwind_VRS_Set(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t regno, _Unwind_VRS_DataRepresentation representation,
                void *valuep);

extern _Unwind_VRS_Result
_Unwind_VRS_Pop(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t discriminator,
                _Unwind_VRS_DataRepresentation representation);

#if defined(_LIBUNWIND_UNWIND_LEVEL1_EXTERNAL_LINKAGE)
#define _LIBUNWIND_EXPORT_UNWIND_LEVEL1 extern
#else
#define _LIBUNWIND_EXPORT_UNWIND_LEVEL1 static __inline__
#endif

// These are de facto helper functions for ARM, which delegate the function
// calls to _Unwind_VRS_Get/Set().  These are not a part of ARM EHABI
// specification, thus these function MUST be inlined.  Please don't replace
// these with the "extern" function declaration; otherwise, the program
// including this <unwind.h> header won't be ABI compatible and will result in
// link error when we are linking the program with libgcc.

_LIBUNWIND_EXPORT_UNWIND_LEVEL1
uintptr_t _Unwind_GetGR(struct _Unwind_Context *context, int index) {
  uintptr_t value = 0;
  _Unwind_VRS_Get(context, _UVRSC_CORE, (uint32_t)index, _UVRSD_UINT32, &value);
  return value;
}

_LIBUNWIND_EXPORT_UNWIND_LEVEL1
void _Unwind_SetGR(struct _Unwind_Context *context, int index,
                   uintptr_t value) {
  _Unwind_VRS_Set(context, _UVRSC_CORE, (uint32_t)index, _UVRSD_UINT32, &value);
}

_LIBUNWIND_EXPORT_UNWIND_LEVEL1
uintptr_t _Unwind_GetIP(struct _Unwind_Context *context) {
  // remove the thumb-bit before returning
  return _Unwind_GetGR(context, 15) & (~(uintptr_t)0x1);
}

_LIBUNWIND_EXPORT_UNWIND_LEVEL1
void _Unwind_SetIP(struct _Unwind_Context *context, uintptr_t value) {
  uintptr_t thumb_bit = _Unwind_GetGR(context, 15) & ((uintptr_t)0x1);
  _Unwind_SetGR(context, 15, value | thumb_bit);
}

#ifdef __cplusplus
}
#endif

#endif // __ARM_EHABI_UNWIND_H__
