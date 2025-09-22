//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
// C++ interface to lower levels of libunwind
//===----------------------------------------------------------------------===//

#ifndef __UNWINDCURSOR_HPP__
#define __UNWINDCURSOR_HPP__

#include "cet_unwind.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unwind.h>

#ifdef _WIN32
  #include <windows.h>
  #include <ntverp.h>
#endif
#ifdef __APPLE__
  #include <mach-o/dyld.h>
#endif
#ifdef _AIX
#include <dlfcn.h>
#include <sys/debug.h>
#include <sys/pseg.h>
#endif

#if defined(_LIBUNWIND_TARGET_LINUX) &&                                        \
    (defined(_LIBUNWIND_TARGET_AARCH64) || defined(_LIBUNWIND_TARGET_RISCV) || \
     defined(_LIBUNWIND_TARGET_S390X))
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#define _LIBUNWIND_CHECK_LINUX_SIGRETURN 1
#endif

#include "AddressSpace.hpp"
#include "CompactUnwinder.hpp"
#include "config.h"
#include "DwarfInstructions.hpp"
#include "EHHeaderParser.hpp"
#include "libunwind.h"
#include "libunwind_ext.h"
#include "Registers.hpp"
#include "RWMutex.hpp"
#include "Unwind-EHABI.h"

#if defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)
// Provide a definition for the DISPATCHER_CONTEXT struct for old (Win7 and
// earlier) SDKs.
// MinGW-w64 has always provided this struct.
  #if defined(_WIN32) && defined(_LIBUNWIND_TARGET_X86_64) && \
      !defined(__MINGW32__) && VER_PRODUCTBUILD < 8000
struct _DISPATCHER_CONTEXT {
  ULONG64 ControlPc;
  ULONG64 ImageBase;
  PRUNTIME_FUNCTION FunctionEntry;
  ULONG64 EstablisherFrame;
  ULONG64 TargetIp;
  PCONTEXT ContextRecord;
  PEXCEPTION_ROUTINE LanguageHandler;
  PVOID HandlerData;
  PUNWIND_HISTORY_TABLE HistoryTable;
  ULONG ScopeIndex;
  ULONG Fill0;
};
  #endif

struct UNWIND_INFO {
  uint8_t Version : 3;
  uint8_t Flags : 5;
  uint8_t SizeOfProlog;
  uint8_t CountOfCodes;
  uint8_t FrameRegister : 4;
  uint8_t FrameOffset : 4;
  uint16_t UnwindCodes[2];
};

extern "C" _Unwind_Reason_Code __libunwind_seh_personality(
    int, _Unwind_Action, uint64_t, _Unwind_Exception *,
    struct _Unwind_Context *);

#endif

namespace libunwind {

#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
/// Cache of recently found FDEs.
template <typename A>
class _LIBUNWIND_HIDDEN DwarfFDECache {
  typedef typename A::pint_t pint_t;
public:
  static constexpr pint_t kSearchAll = static_cast<pint_t>(-1);
  static pint_t findFDE(pint_t mh, pint_t pc);
  static void add(pint_t mh, pint_t ip_start, pint_t ip_end, pint_t fde);
  static void removeAllIn(pint_t mh);
  static void iterateCacheEntries(void (*func)(unw_word_t ip_start,
                                               unw_word_t ip_end,
                                               unw_word_t fde, unw_word_t mh));

private:

  struct entry {
    pint_t mh;
    pint_t ip_start;
    pint_t ip_end;
    pint_t fde;
  };

  // These fields are all static to avoid needing an initializer.
  // There is only one instance of this class per process.
  static RWMutex _lock;
#ifdef __APPLE__
  static void dyldUnloadHook(const struct mach_header *mh, intptr_t slide);
  static bool _registeredForDyldUnloads;
#endif
  static entry *_buffer;
  static entry *_bufferUsed;
  static entry *_bufferEnd;
  static entry _initialBuffer[64];
};

template <typename A>
typename DwarfFDECache<A>::entry *
DwarfFDECache<A>::_buffer = _initialBuffer;

template <typename A>
typename DwarfFDECache<A>::entry *
DwarfFDECache<A>::_bufferUsed = _initialBuffer;

template <typename A>
typename DwarfFDECache<A>::entry *
DwarfFDECache<A>::_bufferEnd = &_initialBuffer[64];

template <typename A>
typename DwarfFDECache<A>::entry DwarfFDECache<A>::_initialBuffer[64];

template <typename A>
RWMutex DwarfFDECache<A>::_lock;

#ifdef __APPLE__
template <typename A>
bool DwarfFDECache<A>::_registeredForDyldUnloads = false;
#endif

template <typename A>
typename A::pint_t DwarfFDECache<A>::findFDE(pint_t mh, pint_t pc) {
  pint_t result = 0;
  _LIBUNWIND_LOG_IF_FALSE(_lock.lock_shared());
  for (entry *p = _buffer; p < _bufferUsed; ++p) {
    if ((mh == p->mh) || (mh == kSearchAll)) {
      if ((p->ip_start <= pc) && (pc < p->ip_end)) {
        result = p->fde;
        break;
      }
    }
  }
  _LIBUNWIND_LOG_IF_FALSE(_lock.unlock_shared());
  return result;
}

template <typename A>
void DwarfFDECache<A>::add(pint_t mh, pint_t ip_start, pint_t ip_end,
                           pint_t fde) {
#if !defined(_LIBUNWIND_NO_HEAP)
  _LIBUNWIND_LOG_IF_FALSE(_lock.lock());
  if (_bufferUsed >= _bufferEnd) {
    size_t oldSize = (size_t)(_bufferEnd - _buffer);
    size_t newSize = oldSize * 4;
    // Can't use operator new (we are below it).
    entry *newBuffer = (entry *)malloc(newSize * sizeof(entry));
    memcpy(newBuffer, _buffer, oldSize * sizeof(entry));
    if (_buffer != _initialBuffer)
      free(_buffer);
    _buffer = newBuffer;
    _bufferUsed = &newBuffer[oldSize];
    _bufferEnd = &newBuffer[newSize];
  }
  _bufferUsed->mh = mh;
  _bufferUsed->ip_start = ip_start;
  _bufferUsed->ip_end = ip_end;
  _bufferUsed->fde = fde;
  ++_bufferUsed;
#ifdef __APPLE__
  if (!_registeredForDyldUnloads) {
    _dyld_register_func_for_remove_image(&dyldUnloadHook);
    _registeredForDyldUnloads = true;
  }
#endif
  _LIBUNWIND_LOG_IF_FALSE(_lock.unlock());
#endif
}

template <typename A>
void DwarfFDECache<A>::removeAllIn(pint_t mh) {
  _LIBUNWIND_LOG_IF_FALSE(_lock.lock());
  entry *d = _buffer;
  for (const entry *s = _buffer; s < _bufferUsed; ++s) {
    if (s->mh != mh) {
      if (d != s)
        *d = *s;
      ++d;
    }
  }
  _bufferUsed = d;
  _LIBUNWIND_LOG_IF_FALSE(_lock.unlock());
}

#ifdef __APPLE__
template <typename A>
void DwarfFDECache<A>::dyldUnloadHook(const struct mach_header *mh, intptr_t ) {
  removeAllIn((pint_t) mh);
}
#endif

template <typename A>
void DwarfFDECache<A>::iterateCacheEntries(void (*func)(
    unw_word_t ip_start, unw_word_t ip_end, unw_word_t fde, unw_word_t mh)) {
  _LIBUNWIND_LOG_IF_FALSE(_lock.lock());
  for (entry *p = _buffer; p < _bufferUsed; ++p) {
    (*func)(p->ip_start, p->ip_end, p->fde, p->mh);
  }
  _LIBUNWIND_LOG_IF_FALSE(_lock.unlock());
}
#endif // defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)


#define arrayoffsetof(type, index, field) ((size_t)(&((type *)0)[index].field))

#if defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)
template <typename A> class UnwindSectionHeader {
public:
  UnwindSectionHeader(A &addressSpace, typename A::pint_t addr)
      : _addressSpace(addressSpace), _addr(addr) {}

  uint32_t version() const {
    return _addressSpace.get32(_addr +
                               offsetof(unwind_info_section_header, version));
  }
  uint32_t commonEncodingsArraySectionOffset() const {
    return _addressSpace.get32(_addr +
                               offsetof(unwind_info_section_header,
                                        commonEncodingsArraySectionOffset));
  }
  uint32_t commonEncodingsArrayCount() const {
    return _addressSpace.get32(_addr + offsetof(unwind_info_section_header,
                                                commonEncodingsArrayCount));
  }
  uint32_t personalityArraySectionOffset() const {
    return _addressSpace.get32(_addr + offsetof(unwind_info_section_header,
                                                personalityArraySectionOffset));
  }
  uint32_t personalityArrayCount() const {
    return _addressSpace.get32(
        _addr + offsetof(unwind_info_section_header, personalityArrayCount));
  }
  uint32_t indexSectionOffset() const {
    return _addressSpace.get32(
        _addr + offsetof(unwind_info_section_header, indexSectionOffset));
  }
  uint32_t indexCount() const {
    return _addressSpace.get32(
        _addr + offsetof(unwind_info_section_header, indexCount));
  }

private:
  A                     &_addressSpace;
  typename A::pint_t     _addr;
};

template <typename A> class UnwindSectionIndexArray {
public:
  UnwindSectionIndexArray(A &addressSpace, typename A::pint_t addr)
      : _addressSpace(addressSpace), _addr(addr) {}

  uint32_t functionOffset(uint32_t index) const {
    return _addressSpace.get32(
        _addr + arrayoffsetof(unwind_info_section_header_index_entry, index,
                              functionOffset));
  }
  uint32_t secondLevelPagesSectionOffset(uint32_t index) const {
    return _addressSpace.get32(
        _addr + arrayoffsetof(unwind_info_section_header_index_entry, index,
                              secondLevelPagesSectionOffset));
  }
  uint32_t lsdaIndexArraySectionOffset(uint32_t index) const {
    return _addressSpace.get32(
        _addr + arrayoffsetof(unwind_info_section_header_index_entry, index,
                              lsdaIndexArraySectionOffset));
  }

private:
  A                   &_addressSpace;
  typename A::pint_t   _addr;
};

template <typename A> class UnwindSectionRegularPageHeader {
public:
  UnwindSectionRegularPageHeader(A &addressSpace, typename A::pint_t addr)
      : _addressSpace(addressSpace), _addr(addr) {}

  uint32_t kind() const {
    return _addressSpace.get32(
        _addr + offsetof(unwind_info_regular_second_level_page_header, kind));
  }
  uint16_t entryPageOffset() const {
    return _addressSpace.get16(
        _addr + offsetof(unwind_info_regular_second_level_page_header,
                         entryPageOffset));
  }
  uint16_t entryCount() const {
    return _addressSpace.get16(
        _addr +
        offsetof(unwind_info_regular_second_level_page_header, entryCount));
  }

private:
  A &_addressSpace;
  typename A::pint_t _addr;
};

template <typename A> class UnwindSectionRegularArray {
public:
  UnwindSectionRegularArray(A &addressSpace, typename A::pint_t addr)
      : _addressSpace(addressSpace), _addr(addr) {}

  uint32_t functionOffset(uint32_t index) const {
    return _addressSpace.get32(
        _addr + arrayoffsetof(unwind_info_regular_second_level_entry, index,
                              functionOffset));
  }
  uint32_t encoding(uint32_t index) const {
    return _addressSpace.get32(
        _addr +
        arrayoffsetof(unwind_info_regular_second_level_entry, index, encoding));
  }

private:
  A &_addressSpace;
  typename A::pint_t _addr;
};

template <typename A> class UnwindSectionCompressedPageHeader {
public:
  UnwindSectionCompressedPageHeader(A &addressSpace, typename A::pint_t addr)
      : _addressSpace(addressSpace), _addr(addr) {}

  uint32_t kind() const {
    return _addressSpace.get32(
        _addr +
        offsetof(unwind_info_compressed_second_level_page_header, kind));
  }
  uint16_t entryPageOffset() const {
    return _addressSpace.get16(
        _addr + offsetof(unwind_info_compressed_second_level_page_header,
                         entryPageOffset));
  }
  uint16_t entryCount() const {
    return _addressSpace.get16(
        _addr +
        offsetof(unwind_info_compressed_second_level_page_header, entryCount));
  }
  uint16_t encodingsPageOffset() const {
    return _addressSpace.get16(
        _addr + offsetof(unwind_info_compressed_second_level_page_header,
                         encodingsPageOffset));
  }
  uint16_t encodingsCount() const {
    return _addressSpace.get16(
        _addr + offsetof(unwind_info_compressed_second_level_page_header,
                         encodingsCount));
  }

private:
  A &_addressSpace;
  typename A::pint_t _addr;
};

template <typename A> class UnwindSectionCompressedArray {
public:
  UnwindSectionCompressedArray(A &addressSpace, typename A::pint_t addr)
      : _addressSpace(addressSpace), _addr(addr) {}

  uint32_t functionOffset(uint32_t index) const {
    return UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(
        _addressSpace.get32(_addr + index * sizeof(uint32_t)));
  }
  uint16_t encodingIndex(uint32_t index) const {
    return UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX(
        _addressSpace.get32(_addr + index * sizeof(uint32_t)));
  }

private:
  A &_addressSpace;
  typename A::pint_t _addr;
};

template <typename A> class UnwindSectionLsdaArray {
public:
  UnwindSectionLsdaArray(A &addressSpace, typename A::pint_t addr)
      : _addressSpace(addressSpace), _addr(addr) {}

  uint32_t functionOffset(uint32_t index) const {
    return _addressSpace.get32(
        _addr + arrayoffsetof(unwind_info_section_header_lsda_index_entry,
                              index, functionOffset));
  }
  uint32_t lsdaOffset(uint32_t index) const {
    return _addressSpace.get32(
        _addr + arrayoffsetof(unwind_info_section_header_lsda_index_entry,
                              index, lsdaOffset));
  }

private:
  A                   &_addressSpace;
  typename A::pint_t   _addr;
};
#endif // defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)

class _LIBUNWIND_HIDDEN AbstractUnwindCursor {
public:
  // NOTE: provide a class specific placement deallocation function (S5.3.4 p20)
  // This avoids an unnecessary dependency to libc++abi.
  void operator delete(void *, size_t) {}

  virtual ~AbstractUnwindCursor() {}
  virtual bool validReg(int) { _LIBUNWIND_ABORT("validReg not implemented"); }
  virtual unw_word_t getReg(int) { _LIBUNWIND_ABORT("getReg not implemented"); }
  virtual void setReg(int, unw_word_t) {
    _LIBUNWIND_ABORT("setReg not implemented");
  }
  virtual bool validFloatReg(int) {
    _LIBUNWIND_ABORT("validFloatReg not implemented");
  }
  virtual unw_fpreg_t getFloatReg(int) {
    _LIBUNWIND_ABORT("getFloatReg not implemented");
  }
  virtual void setFloatReg(int, unw_fpreg_t) {
    _LIBUNWIND_ABORT("setFloatReg not implemented");
  }
  virtual int step(bool = false) { _LIBUNWIND_ABORT("step not implemented"); }
  virtual void getInfo(unw_proc_info_t *) {
    _LIBUNWIND_ABORT("getInfo not implemented");
  }
  virtual void jumpto() { _LIBUNWIND_ABORT("jumpto not implemented"); }
  virtual bool isSignalFrame() {
    _LIBUNWIND_ABORT("isSignalFrame not implemented");
  }
  virtual bool getFunctionName(char *, size_t, unw_word_t *) {
    _LIBUNWIND_ABORT("getFunctionName not implemented");
  }
  virtual void setInfoBasedOnIPRegister(bool = false) {
    _LIBUNWIND_ABORT("setInfoBasedOnIPRegister not implemented");
  }
  virtual const char *getRegisterName(int) {
    _LIBUNWIND_ABORT("getRegisterName not implemented");
  }
#ifdef __arm__
  virtual void saveVFPAsX() { _LIBUNWIND_ABORT("saveVFPAsX not implemented"); }
#endif

#ifdef _AIX
  virtual uintptr_t getDataRelBase() {
    _LIBUNWIND_ABORT("getDataRelBase not implemented");
  }
#endif

#if defined(_LIBUNWIND_USE_CET) || defined(_LIBUNWIND_USE_GCS)
  virtual void *get_registers() {
    _LIBUNWIND_ABORT("get_registers not implemented");
  }
#endif
};

#if defined(_LIBUNWIND_SUPPORT_SEH_UNWIND) && defined(_WIN32)

/// \c UnwindCursor contains all state (including all register values) during
/// an unwind.  This is normally stack-allocated inside a unw_cursor_t.
template <typename A, typename R>
class UnwindCursor : public AbstractUnwindCursor {
  typedef typename A::pint_t pint_t;
public:
                      UnwindCursor(unw_context_t *context, A &as);
                      UnwindCursor(CONTEXT *context, A &as);
                      UnwindCursor(A &as, void *threadArg);
  virtual             ~UnwindCursor() {}
  virtual bool        validReg(int);
  virtual unw_word_t  getReg(int);
  virtual void        setReg(int, unw_word_t);
  virtual bool        validFloatReg(int);
  virtual unw_fpreg_t getFloatReg(int);
  virtual void        setFloatReg(int, unw_fpreg_t);
  virtual int         step(bool = false);
  virtual void        getInfo(unw_proc_info_t *);
  virtual void        jumpto();
  virtual bool        isSignalFrame();
  virtual bool        getFunctionName(char *buf, size_t len, unw_word_t *off);
  virtual void        setInfoBasedOnIPRegister(bool isReturnAddress = false);
  virtual const char *getRegisterName(int num);
#ifdef __arm__
  virtual void        saveVFPAsX();
#endif

  DISPATCHER_CONTEXT *getDispatcherContext() { return &_dispContext; }
  void setDispatcherContext(DISPATCHER_CONTEXT *disp) {
    _dispContext = *disp;
    _info.lsda = reinterpret_cast<unw_word_t>(_dispContext.HandlerData);
    if (_dispContext.LanguageHandler) {
      _info.handler = reinterpret_cast<unw_word_t>(__libunwind_seh_personality);
    } else
      _info.handler = 0;
  }

  // libunwind does not and should not depend on C++ library which means that we
  // need our own definition of inline placement new.
  static void *operator new(size_t, UnwindCursor<A, R> *p) { return p; }

private:

  pint_t getLastPC() const { return _dispContext.ControlPc; }
  void setLastPC(pint_t pc) { _dispContext.ControlPc = pc; }
  RUNTIME_FUNCTION *lookUpSEHUnwindInfo(pint_t pc, pint_t *base) {
#ifdef __arm__
    // Remove the thumb bit; FunctionEntry ranges don't include the thumb bit.
    pc &= ~1U;
#endif
    // If pc points exactly at the end of the range, we might resolve the
    // next function instead. Decrement pc by 1 to fit inside the current
    // function.
    pc -= 1;
    _dispContext.FunctionEntry = RtlLookupFunctionEntry(pc,
                                                        &_dispContext.ImageBase,
                                                        _dispContext.HistoryTable);
    *base = _dispContext.ImageBase;
    return _dispContext.FunctionEntry;
  }
  bool getInfoFromSEH(pint_t pc);
  int stepWithSEHData() {
    _dispContext.LanguageHandler = RtlVirtualUnwind(UNW_FLAG_UHANDLER,
                                                    _dispContext.ImageBase,
                                                    _dispContext.ControlPc,
                                                    _dispContext.FunctionEntry,
                                                    _dispContext.ContextRecord,
                                                    &_dispContext.HandlerData,
                                                    &_dispContext.EstablisherFrame,
                                                    NULL);
    // Update some fields of the unwind info now, since we have them.
    _info.lsda = reinterpret_cast<unw_word_t>(_dispContext.HandlerData);
    if (_dispContext.LanguageHandler) {
      _info.handler = reinterpret_cast<unw_word_t>(__libunwind_seh_personality);
    } else
      _info.handler = 0;
    return UNW_STEP_SUCCESS;
  }

  A                   &_addressSpace;
  unw_proc_info_t      _info;
  DISPATCHER_CONTEXT   _dispContext;
  CONTEXT              _msContext;
  UNWIND_HISTORY_TABLE _histTable;
  bool                 _unwindInfoMissing;
};


template <typename A, typename R>
UnwindCursor<A, R>::UnwindCursor(unw_context_t *context, A &as)
    : _addressSpace(as), _unwindInfoMissing(false) {
  static_assert((check_fit<UnwindCursor<A, R>, unw_cursor_t>::does_fit),
                "UnwindCursor<> does not fit in unw_cursor_t");
  static_assert((alignof(UnwindCursor<A, R>) <= alignof(unw_cursor_t)),
                "UnwindCursor<> requires more alignment than unw_cursor_t");
  memset(&_info, 0, sizeof(_info));
  memset(&_histTable, 0, sizeof(_histTable));
  memset(&_dispContext, 0, sizeof(_dispContext));
  _dispContext.ContextRecord = &_msContext;
  _dispContext.HistoryTable = &_histTable;
  // Initialize MS context from ours.
  R r(context);
  RtlCaptureContext(&_msContext);
  _msContext.ContextFlags = CONTEXT_CONTROL|CONTEXT_INTEGER|CONTEXT_FLOATING_POINT;
#if defined(_LIBUNWIND_TARGET_X86_64)
  _msContext.Rax = r.getRegister(UNW_X86_64_RAX);
  _msContext.Rcx = r.getRegister(UNW_X86_64_RCX);
  _msContext.Rdx = r.getRegister(UNW_X86_64_RDX);
  _msContext.Rbx = r.getRegister(UNW_X86_64_RBX);
  _msContext.Rsp = r.getRegister(UNW_X86_64_RSP);
  _msContext.Rbp = r.getRegister(UNW_X86_64_RBP);
  _msContext.Rsi = r.getRegister(UNW_X86_64_RSI);
  _msContext.Rdi = r.getRegister(UNW_X86_64_RDI);
  _msContext.R8 = r.getRegister(UNW_X86_64_R8);
  _msContext.R9 = r.getRegister(UNW_X86_64_R9);
  _msContext.R10 = r.getRegister(UNW_X86_64_R10);
  _msContext.R11 = r.getRegister(UNW_X86_64_R11);
  _msContext.R12 = r.getRegister(UNW_X86_64_R12);
  _msContext.R13 = r.getRegister(UNW_X86_64_R13);
  _msContext.R14 = r.getRegister(UNW_X86_64_R14);
  _msContext.R15 = r.getRegister(UNW_X86_64_R15);
  _msContext.Rip = r.getRegister(UNW_REG_IP);
  union {
    v128 v;
    M128A m;
  } t;
  t.v = r.getVectorRegister(UNW_X86_64_XMM0);
  _msContext.Xmm0 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM1);
  _msContext.Xmm1 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM2);
  _msContext.Xmm2 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM3);
  _msContext.Xmm3 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM4);
  _msContext.Xmm4 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM5);
  _msContext.Xmm5 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM6);
  _msContext.Xmm6 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM7);
  _msContext.Xmm7 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM8);
  _msContext.Xmm8 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM9);
  _msContext.Xmm9 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM10);
  _msContext.Xmm10 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM11);
  _msContext.Xmm11 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM12);
  _msContext.Xmm12 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM13);
  _msContext.Xmm13 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM14);
  _msContext.Xmm14 = t.m;
  t.v = r.getVectorRegister(UNW_X86_64_XMM15);
  _msContext.Xmm15 = t.m;
#elif defined(_LIBUNWIND_TARGET_ARM)
  _msContext.R0 = r.getRegister(UNW_ARM_R0);
  _msContext.R1 = r.getRegister(UNW_ARM_R1);
  _msContext.R2 = r.getRegister(UNW_ARM_R2);
  _msContext.R3 = r.getRegister(UNW_ARM_R3);
  _msContext.R4 = r.getRegister(UNW_ARM_R4);
  _msContext.R5 = r.getRegister(UNW_ARM_R5);
  _msContext.R6 = r.getRegister(UNW_ARM_R6);
  _msContext.R7 = r.getRegister(UNW_ARM_R7);
  _msContext.R8 = r.getRegister(UNW_ARM_R8);
  _msContext.R9 = r.getRegister(UNW_ARM_R9);
  _msContext.R10 = r.getRegister(UNW_ARM_R10);
  _msContext.R11 = r.getRegister(UNW_ARM_R11);
  _msContext.R12 = r.getRegister(UNW_ARM_R12);
  _msContext.Sp = r.getRegister(UNW_ARM_SP);
  _msContext.Lr = r.getRegister(UNW_ARM_LR);
  _msContext.Pc = r.getRegister(UNW_ARM_IP);
  for (int i = UNW_ARM_D0; i <= UNW_ARM_D31; ++i) {
    union {
      uint64_t w;
      double d;
    } d;
    d.d = r.getFloatRegister(i);
    _msContext.D[i - UNW_ARM_D0] = d.w;
  }
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  for (int i = UNW_AARCH64_X0; i <= UNW_ARM64_X30; ++i)
    _msContext.X[i - UNW_AARCH64_X0] = r.getRegister(i);
  _msContext.Sp = r.getRegister(UNW_REG_SP);
  _msContext.Pc = r.getRegister(UNW_REG_IP);
  for (int i = UNW_AARCH64_V0; i <= UNW_ARM64_D31; ++i)
    _msContext.V[i - UNW_AARCH64_V0].D[0] = r.getFloatRegister(i);
#endif
}

template <typename A, typename R>
UnwindCursor<A, R>::UnwindCursor(CONTEXT *context, A &as)
    : _addressSpace(as), _unwindInfoMissing(false) {
  static_assert((check_fit<UnwindCursor<A, R>, unw_cursor_t>::does_fit),
                "UnwindCursor<> does not fit in unw_cursor_t");
  memset(&_info, 0, sizeof(_info));
  memset(&_histTable, 0, sizeof(_histTable));
  memset(&_dispContext, 0, sizeof(_dispContext));
  _dispContext.ContextRecord = &_msContext;
  _dispContext.HistoryTable = &_histTable;
  _msContext = *context;
}


template <typename A, typename R>
bool UnwindCursor<A, R>::validReg(int regNum) {
  if (regNum == UNW_REG_IP || regNum == UNW_REG_SP) return true;
#if defined(_LIBUNWIND_TARGET_X86_64)
  if (regNum >= UNW_X86_64_RAX && regNum <= UNW_X86_64_RIP) return true;
#elif defined(_LIBUNWIND_TARGET_ARM)
  if ((regNum >= UNW_ARM_R0 && regNum <= UNW_ARM_R15) ||
      regNum == UNW_ARM_RA_AUTH_CODE)
    return true;
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  if (regNum >= UNW_AARCH64_X0 && regNum <= UNW_ARM64_X30) return true;
#endif
  return false;
}

template <typename A, typename R>
unw_word_t UnwindCursor<A, R>::getReg(int regNum) {
  switch (regNum) {
#if defined(_LIBUNWIND_TARGET_X86_64)
  case UNW_X86_64_RIP:
  case UNW_REG_IP: return _msContext.Rip;
  case UNW_X86_64_RAX: return _msContext.Rax;
  case UNW_X86_64_RDX: return _msContext.Rdx;
  case UNW_X86_64_RCX: return _msContext.Rcx;
  case UNW_X86_64_RBX: return _msContext.Rbx;
  case UNW_REG_SP:
  case UNW_X86_64_RSP: return _msContext.Rsp;
  case UNW_X86_64_RBP: return _msContext.Rbp;
  case UNW_X86_64_RSI: return _msContext.Rsi;
  case UNW_X86_64_RDI: return _msContext.Rdi;
  case UNW_X86_64_R8: return _msContext.R8;
  case UNW_X86_64_R9: return _msContext.R9;
  case UNW_X86_64_R10: return _msContext.R10;
  case UNW_X86_64_R11: return _msContext.R11;
  case UNW_X86_64_R12: return _msContext.R12;
  case UNW_X86_64_R13: return _msContext.R13;
  case UNW_X86_64_R14: return _msContext.R14;
  case UNW_X86_64_R15: return _msContext.R15;
#elif defined(_LIBUNWIND_TARGET_ARM)
  case UNW_ARM_R0: return _msContext.R0;
  case UNW_ARM_R1: return _msContext.R1;
  case UNW_ARM_R2: return _msContext.R2;
  case UNW_ARM_R3: return _msContext.R3;
  case UNW_ARM_R4: return _msContext.R4;
  case UNW_ARM_R5: return _msContext.R5;
  case UNW_ARM_R6: return _msContext.R6;
  case UNW_ARM_R7: return _msContext.R7;
  case UNW_ARM_R8: return _msContext.R8;
  case UNW_ARM_R9: return _msContext.R9;
  case UNW_ARM_R10: return _msContext.R10;
  case UNW_ARM_R11: return _msContext.R11;
  case UNW_ARM_R12: return _msContext.R12;
  case UNW_REG_SP:
  case UNW_ARM_SP: return _msContext.Sp;
  case UNW_ARM_LR: return _msContext.Lr;
  case UNW_REG_IP:
  case UNW_ARM_IP: return _msContext.Pc;
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  case UNW_REG_SP: return _msContext.Sp;
  case UNW_REG_IP: return _msContext.Pc;
  default: return _msContext.X[regNum - UNW_AARCH64_X0];
#endif
  }
  _LIBUNWIND_ABORT("unsupported register");
}

template <typename A, typename R>
void UnwindCursor<A, R>::setReg(int regNum, unw_word_t value) {
  switch (regNum) {
#if defined(_LIBUNWIND_TARGET_X86_64)
  case UNW_X86_64_RIP:
  case UNW_REG_IP: _msContext.Rip = value; break;
  case UNW_X86_64_RAX: _msContext.Rax = value; break;
  case UNW_X86_64_RDX: _msContext.Rdx = value; break;
  case UNW_X86_64_RCX: _msContext.Rcx = value; break;
  case UNW_X86_64_RBX: _msContext.Rbx = value; break;
  case UNW_REG_SP:
  case UNW_X86_64_RSP: _msContext.Rsp = value; break;
  case UNW_X86_64_RBP: _msContext.Rbp = value; break;
  case UNW_X86_64_RSI: _msContext.Rsi = value; break;
  case UNW_X86_64_RDI: _msContext.Rdi = value; break;
  case UNW_X86_64_R8: _msContext.R8 = value; break;
  case UNW_X86_64_R9: _msContext.R9 = value; break;
  case UNW_X86_64_R10: _msContext.R10 = value; break;
  case UNW_X86_64_R11: _msContext.R11 = value; break;
  case UNW_X86_64_R12: _msContext.R12 = value; break;
  case UNW_X86_64_R13: _msContext.R13 = value; break;
  case UNW_X86_64_R14: _msContext.R14 = value; break;
  case UNW_X86_64_R15: _msContext.R15 = value; break;
#elif defined(_LIBUNWIND_TARGET_ARM)
  case UNW_ARM_R0: _msContext.R0 = value; break;
  case UNW_ARM_R1: _msContext.R1 = value; break;
  case UNW_ARM_R2: _msContext.R2 = value; break;
  case UNW_ARM_R3: _msContext.R3 = value; break;
  case UNW_ARM_R4: _msContext.R4 = value; break;
  case UNW_ARM_R5: _msContext.R5 = value; break;
  case UNW_ARM_R6: _msContext.R6 = value; break;
  case UNW_ARM_R7: _msContext.R7 = value; break;
  case UNW_ARM_R8: _msContext.R8 = value; break;
  case UNW_ARM_R9: _msContext.R9 = value; break;
  case UNW_ARM_R10: _msContext.R10 = value; break;
  case UNW_ARM_R11: _msContext.R11 = value; break;
  case UNW_ARM_R12: _msContext.R12 = value; break;
  case UNW_REG_SP:
  case UNW_ARM_SP: _msContext.Sp = value; break;
  case UNW_ARM_LR: _msContext.Lr = value; break;
  case UNW_REG_IP:
  case UNW_ARM_IP: _msContext.Pc = value; break;
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  case UNW_REG_SP: _msContext.Sp = value; break;
  case UNW_REG_IP: _msContext.Pc = value; break;
  case UNW_AARCH64_X0:
  case UNW_AARCH64_X1:
  case UNW_AARCH64_X2:
  case UNW_AARCH64_X3:
  case UNW_AARCH64_X4:
  case UNW_AARCH64_X5:
  case UNW_AARCH64_X6:
  case UNW_AARCH64_X7:
  case UNW_AARCH64_X8:
  case UNW_AARCH64_X9:
  case UNW_AARCH64_X10:
  case UNW_AARCH64_X11:
  case UNW_AARCH64_X12:
  case UNW_AARCH64_X13:
  case UNW_AARCH64_X14:
  case UNW_AARCH64_X15:
  case UNW_AARCH64_X16:
  case UNW_AARCH64_X17:
  case UNW_AARCH64_X18:
  case UNW_AARCH64_X19:
  case UNW_AARCH64_X20:
  case UNW_AARCH64_X21:
  case UNW_AARCH64_X22:
  case UNW_AARCH64_X23:
  case UNW_AARCH64_X24:
  case UNW_AARCH64_X25:
  case UNW_AARCH64_X26:
  case UNW_AARCH64_X27:
  case UNW_AARCH64_X28:
  case UNW_AARCH64_FP:
  case UNW_AARCH64_LR: _msContext.X[regNum - UNW_ARM64_X0] = value; break;
#endif
  default:
    _LIBUNWIND_ABORT("unsupported register");
  }
}

template <typename A, typename R>
bool UnwindCursor<A, R>::validFloatReg(int regNum) {
#if defined(_LIBUNWIND_TARGET_ARM)
  if (regNum >= UNW_ARM_S0 && regNum <= UNW_ARM_S31) return true;
  if (regNum >= UNW_ARM_D0 && regNum <= UNW_ARM_D31) return true;
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  if (regNum >= UNW_AARCH64_V0 && regNum <= UNW_ARM64_D31) return true;
#else
  (void)regNum;
#endif
  return false;
}

template <typename A, typename R>
unw_fpreg_t UnwindCursor<A, R>::getFloatReg(int regNum) {
#if defined(_LIBUNWIND_TARGET_ARM)
  if (regNum >= UNW_ARM_S0 && regNum <= UNW_ARM_S31) {
    union {
      uint32_t w;
      float f;
    } d;
    d.w = _msContext.S[regNum - UNW_ARM_S0];
    return d.f;
  }
  if (regNum >= UNW_ARM_D0 && regNum <= UNW_ARM_D31) {
    union {
      uint64_t w;
      double d;
    } d;
    d.w = _msContext.D[regNum - UNW_ARM_D0];
    return d.d;
  }
  _LIBUNWIND_ABORT("unsupported float register");
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  return _msContext.V[regNum - UNW_AARCH64_V0].D[0];
#else
  (void)regNum;
  _LIBUNWIND_ABORT("float registers unimplemented");
#endif
}

template <typename A, typename R>
void UnwindCursor<A, R>::setFloatReg(int regNum, unw_fpreg_t value) {
#if defined(_LIBUNWIND_TARGET_ARM)
  if (regNum >= UNW_ARM_S0 && regNum <= UNW_ARM_S31) {
    union {
      uint32_t w;
      float f;
    } d;
    d.f = (float)value;
    _msContext.S[regNum - UNW_ARM_S0] = d.w;
  }
  if (regNum >= UNW_ARM_D0 && regNum <= UNW_ARM_D31) {
    union {
      uint64_t w;
      double d;
    } d;
    d.d = value;
    _msContext.D[regNum - UNW_ARM_D0] = d.w;
  }
  _LIBUNWIND_ABORT("unsupported float register");
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  _msContext.V[regNum - UNW_AARCH64_V0].D[0] = value;
#else
  (void)regNum;
  (void)value;
  _LIBUNWIND_ABORT("float registers unimplemented");
#endif
}

template <typename A, typename R> void UnwindCursor<A, R>::jumpto() {
  RtlRestoreContext(&_msContext, nullptr);
}

#ifdef __arm__
template <typename A, typename R> void UnwindCursor<A, R>::saveVFPAsX() {}
#endif

template <typename A, typename R>
const char *UnwindCursor<A, R>::getRegisterName(int regNum) {
  return R::getRegisterName(regNum);
}

template <typename A, typename R> bool UnwindCursor<A, R>::isSignalFrame() {
  return false;
}

#else  // !defined(_LIBUNWIND_SUPPORT_SEH_UNWIND) || !defined(_WIN32)

/// UnwindCursor contains all state (including all register values) during
/// an unwind.  This is normally stack allocated inside a unw_cursor_t.
template <typename A, typename R>
class UnwindCursor : public AbstractUnwindCursor{
  typedef typename A::pint_t pint_t;
public:
                      UnwindCursor(unw_context_t *context, A &as);
                      UnwindCursor(A &as, void *threadArg);
  virtual             ~UnwindCursor() {}
  virtual bool        validReg(int);
  virtual unw_word_t  getReg(int);
  virtual void        setReg(int, unw_word_t);
  virtual bool        validFloatReg(int);
  virtual unw_fpreg_t getFloatReg(int);
  virtual void        setFloatReg(int, unw_fpreg_t);
  virtual int         step(bool stage2 = false);
  virtual void        getInfo(unw_proc_info_t *);
  virtual void        jumpto();
  virtual bool        isSignalFrame();
  virtual bool        getFunctionName(char *buf, size_t len, unw_word_t *off);
  virtual void        setInfoBasedOnIPRegister(bool isReturnAddress = false);
  virtual const char *getRegisterName(int num);
#ifdef __arm__
  virtual void        saveVFPAsX();
#endif

#ifdef _AIX
  virtual uintptr_t getDataRelBase();
#endif

#if defined(_LIBUNWIND_USE_CET) || defined(_LIBUNWIND_USE_GCS)
  virtual void *get_registers() { return &_registers; }
#endif

  // libunwind does not and should not depend on C++ library which means that we
  // need our own definition of inline placement new.
  static void *operator new(size_t, UnwindCursor<A, R> *p) { return p; }

private:

#if defined(_LIBUNWIND_ARM_EHABI)
  bool getInfoFromEHABISection(pint_t pc, const UnwindInfoSections &sects);

  int stepWithEHABI() {
    size_t len = 0;
    size_t off = 0;
    // FIXME: Calling decode_eht_entry() here is violating the libunwind
    // abstraction layer.
    const uint32_t *ehtp =
        decode_eht_entry(reinterpret_cast<const uint32_t *>(_info.unwind_info),
                         &off, &len);
    if (_Unwind_VRS_Interpret((_Unwind_Context *)this, ehtp, off, len) !=
            _URC_CONTINUE_UNWIND)
      return UNW_STEP_END;
    return UNW_STEP_SUCCESS;
  }
#endif

#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN)
  bool setInfoForSigReturn() {
    R dummy;
    return setInfoForSigReturn(dummy);
  }
  int stepThroughSigReturn() {
    R dummy;
    return stepThroughSigReturn(dummy);
  }
  bool isReadableAddr(const pint_t addr) const;
#if defined(_LIBUNWIND_TARGET_AARCH64)
  bool setInfoForSigReturn(Registers_arm64 &);
  int stepThroughSigReturn(Registers_arm64 &);
#endif
#if defined(_LIBUNWIND_TARGET_RISCV)
  bool setInfoForSigReturn(Registers_riscv &);
  int stepThroughSigReturn(Registers_riscv &);
#endif
#if defined(_LIBUNWIND_TARGET_S390X)
  bool setInfoForSigReturn(Registers_s390x &);
  int stepThroughSigReturn(Registers_s390x &);
#endif
  template <typename Registers> bool setInfoForSigReturn(Registers &) {
    return false;
  }
  template <typename Registers> int stepThroughSigReturn(Registers &) {
    return UNW_STEP_END;
  }
#endif

#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
  bool getInfoFromFdeCie(const typename CFI_Parser<A>::FDE_Info &fdeInfo,
                         const typename CFI_Parser<A>::CIE_Info &cieInfo,
                         pint_t pc, uintptr_t dso_base);
  bool getInfoFromDwarfSection(pint_t pc, const UnwindInfoSections &sects,
                                            uint32_t fdeSectionOffsetHint=0);
  int stepWithDwarfFDE(bool stage2) {
    return DwarfInstructions<A, R>::stepWithDwarf(
        _addressSpace, (pint_t)this->getReg(UNW_REG_IP),
        (pint_t)_info.unwind_info, _registers, _isSignalFrame, stage2);
  }
#endif

#if defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)
  bool getInfoFromCompactEncodingSection(pint_t pc,
                                            const UnwindInfoSections &sects);
  int stepWithCompactEncoding(bool stage2 = false) {
#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
    if ( compactSaysUseDwarf() )
      return stepWithDwarfFDE(stage2);
#endif
    R dummy;
    return stepWithCompactEncoding(dummy);
  }

#if defined(_LIBUNWIND_TARGET_X86_64)
  int stepWithCompactEncoding(Registers_x86_64 &) {
    return CompactUnwinder_x86_64<A>::stepWithCompactEncoding(
        _info.format, _info.start_ip, _addressSpace, _registers);
  }
#endif

#if defined(_LIBUNWIND_TARGET_I386)
  int stepWithCompactEncoding(Registers_x86 &) {
    return CompactUnwinder_x86<A>::stepWithCompactEncoding(
        _info.format, (uint32_t)_info.start_ip, _addressSpace, _registers);
  }
#endif

#if defined(_LIBUNWIND_TARGET_PPC)
  int stepWithCompactEncoding(Registers_ppc &) {
    return UNW_EINVAL;
  }
#endif

#if defined(_LIBUNWIND_TARGET_PPC64)
  int stepWithCompactEncoding(Registers_ppc64 &) {
    return UNW_EINVAL;
  }
#endif


#if defined(_LIBUNWIND_TARGET_AARCH64)
  int stepWithCompactEncoding(Registers_arm64 &) {
    return CompactUnwinder_arm64<A>::stepWithCompactEncoding(
        _info.format, _info.start_ip, _addressSpace, _registers);
  }
#endif

#if defined(_LIBUNWIND_TARGET_MIPS_O32)
  int stepWithCompactEncoding(Registers_mips_o32 &) {
    return UNW_EINVAL;
  }
#endif

#if defined(_LIBUNWIND_TARGET_MIPS_NEWABI)
  int stepWithCompactEncoding(Registers_mips_newabi &) {
    return UNW_EINVAL;
  }
#endif

#if defined(_LIBUNWIND_TARGET_LOONGARCH)
  int stepWithCompactEncoding(Registers_loongarch &) { return UNW_EINVAL; }
#endif

#if defined(_LIBUNWIND_TARGET_SPARC)
  int stepWithCompactEncoding(Registers_sparc &) { return UNW_EINVAL; }
#endif

#if defined(_LIBUNWIND_TARGET_SPARC64)
  int stepWithCompactEncoding(Registers_sparc64 &) { return UNW_EINVAL; }
#endif

#if defined (_LIBUNWIND_TARGET_RISCV)
  int stepWithCompactEncoding(Registers_riscv &) {
    return UNW_EINVAL;
  }
#endif

  bool compactSaysUseDwarf(uint32_t *offset=NULL) const {
    R dummy;
    return compactSaysUseDwarf(dummy, offset);
  }

#if defined(_LIBUNWIND_TARGET_X86_64)
  bool compactSaysUseDwarf(Registers_x86_64 &, uint32_t *offset) const {
    if ((_info.format & UNWIND_X86_64_MODE_MASK) == UNWIND_X86_64_MODE_DWARF) {
      if (offset)
        *offset = (_info.format & UNWIND_X86_64_DWARF_SECTION_OFFSET);
      return true;
    }
    return false;
  }
#endif

#if defined(_LIBUNWIND_TARGET_I386)
  bool compactSaysUseDwarf(Registers_x86 &, uint32_t *offset) const {
    if ((_info.format & UNWIND_X86_MODE_MASK) == UNWIND_X86_MODE_DWARF) {
      if (offset)
        *offset = (_info.format & UNWIND_X86_DWARF_SECTION_OFFSET);
      return true;
    }
    return false;
  }
#endif

#if defined(_LIBUNWIND_TARGET_PPC)
  bool compactSaysUseDwarf(Registers_ppc &, uint32_t *) const {
    return true;
  }
#endif

#if defined(_LIBUNWIND_TARGET_PPC64)
  bool compactSaysUseDwarf(Registers_ppc64 &, uint32_t *) const {
    return true;
  }
#endif

#if defined(_LIBUNWIND_TARGET_AARCH64)
  bool compactSaysUseDwarf(Registers_arm64 &, uint32_t *offset) const {
    if ((_info.format & UNWIND_ARM64_MODE_MASK) == UNWIND_ARM64_MODE_DWARF) {
      if (offset)
        *offset = (_info.format & UNWIND_ARM64_DWARF_SECTION_OFFSET);
      return true;
    }
    return false;
  }
#endif

#if defined(_LIBUNWIND_TARGET_MIPS_O32)
  bool compactSaysUseDwarf(Registers_mips_o32 &, uint32_t *) const {
    return true;
  }
#endif

#if defined(_LIBUNWIND_TARGET_MIPS_NEWABI)
  bool compactSaysUseDwarf(Registers_mips_newabi &, uint32_t *) const {
    return true;
  }
#endif

#if defined(_LIBUNWIND_TARGET_LOONGARCH)
  bool compactSaysUseDwarf(Registers_loongarch &, uint32_t *) const {
    return true;
  }
#endif

#if defined(_LIBUNWIND_TARGET_SPARC)
  bool compactSaysUseDwarf(Registers_sparc &, uint32_t *) const { return true; }
#endif

#if defined(_LIBUNWIND_TARGET_SPARC64)
  bool compactSaysUseDwarf(Registers_sparc64 &, uint32_t *) const {
    return true;
  }
#endif

#if defined (_LIBUNWIND_TARGET_RISCV)
  bool compactSaysUseDwarf(Registers_riscv &, uint32_t *) const {
    return true;
  }
#endif

#endif // defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)

#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
  compact_unwind_encoding_t dwarfEncoding() const {
    R dummy;
    return dwarfEncoding(dummy);
  }

#if defined(_LIBUNWIND_TARGET_X86_64)
  compact_unwind_encoding_t dwarfEncoding(Registers_x86_64 &) const {
    return UNWIND_X86_64_MODE_DWARF;
  }
#endif

#if defined(_LIBUNWIND_TARGET_I386)
  compact_unwind_encoding_t dwarfEncoding(Registers_x86 &) const {
    return UNWIND_X86_MODE_DWARF;
  }
#endif

#if defined(_LIBUNWIND_TARGET_PPC)
  compact_unwind_encoding_t dwarfEncoding(Registers_ppc &) const {
    return 0;
  }
#endif

#if defined(_LIBUNWIND_TARGET_PPC64)
  compact_unwind_encoding_t dwarfEncoding(Registers_ppc64 &) const {
    return 0;
  }
#endif

#if defined(_LIBUNWIND_TARGET_AARCH64)
  compact_unwind_encoding_t dwarfEncoding(Registers_arm64 &) const {
    return UNWIND_ARM64_MODE_DWARF;
  }
#endif

#if defined(_LIBUNWIND_TARGET_ARM)
  compact_unwind_encoding_t dwarfEncoding(Registers_arm &) const {
    return 0;
  }
#endif

#if defined (_LIBUNWIND_TARGET_OR1K)
  compact_unwind_encoding_t dwarfEncoding(Registers_or1k &) const {
    return 0;
  }
#endif

#if defined (_LIBUNWIND_TARGET_HEXAGON)
  compact_unwind_encoding_t dwarfEncoding(Registers_hexagon &) const {
    return 0;
  }
#endif

#if defined (_LIBUNWIND_TARGET_MIPS_O32)
  compact_unwind_encoding_t dwarfEncoding(Registers_mips_o32 &) const {
    return 0;
  }
#endif

#if defined (_LIBUNWIND_TARGET_MIPS_NEWABI)
  compact_unwind_encoding_t dwarfEncoding(Registers_mips_newabi &) const {
    return 0;
  }
#endif

#if defined(_LIBUNWIND_TARGET_LOONGARCH)
  compact_unwind_encoding_t dwarfEncoding(Registers_loongarch &) const {
    return 0;
  }
#endif

#if defined(_LIBUNWIND_TARGET_SPARC)
  compact_unwind_encoding_t dwarfEncoding(Registers_sparc &) const { return 0; }
#endif

#if defined(_LIBUNWIND_TARGET_SPARC64)
  compact_unwind_encoding_t dwarfEncoding(Registers_sparc64 &) const {
    return 0;
  }
#endif

#if defined (_LIBUNWIND_TARGET_RISCV)
  compact_unwind_encoding_t dwarfEncoding(Registers_riscv &) const {
    return 0;
  }
#endif

#if defined (_LIBUNWIND_TARGET_S390X)
  compact_unwind_encoding_t dwarfEncoding(Registers_s390x &) const {
    return 0;
  }
#endif

#endif // defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)

#if defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)
  // For runtime environments using SEH unwind data without Windows runtime
  // support.
  pint_t getLastPC() const { /* FIXME: Implement */ return 0; }
  void setLastPC(pint_t pc) { /* FIXME: Implement */ }
  RUNTIME_FUNCTION *lookUpSEHUnwindInfo(pint_t pc, pint_t *base) {
    /* FIXME: Implement */
    *base = 0;
    return nullptr;
  }
  bool getInfoFromSEH(pint_t pc);
  int stepWithSEHData() { /* FIXME: Implement */ return 0; }
#endif // defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)

#if defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND)
  bool getInfoFromTBTable(pint_t pc, R &registers);
  int stepWithTBTable(pint_t pc, tbtable *TBTable, R &registers,
                      bool &isSignalFrame);
  int stepWithTBTableData() {
    return stepWithTBTable(reinterpret_cast<pint_t>(this->getReg(UNW_REG_IP)),
                           reinterpret_cast<tbtable *>(_info.unwind_info),
                           _registers, _isSignalFrame);
  }
#endif // defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND)

  A               &_addressSpace;
  R                _registers;
  unw_proc_info_t  _info;
  bool             _unwindInfoMissing;
  bool             _isSignalFrame;
#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN)
  bool             _isSigReturn = false;
#endif
};


template <typename A, typename R>
UnwindCursor<A, R>::UnwindCursor(unw_context_t *context, A &as)
    : _addressSpace(as), _registers(context), _unwindInfoMissing(false),
      _isSignalFrame(false) {
  static_assert((check_fit<UnwindCursor<A, R>, unw_cursor_t>::does_fit),
                "UnwindCursor<> does not fit in unw_cursor_t");
  static_assert((alignof(UnwindCursor<A, R>) <= alignof(unw_cursor_t)),
                "UnwindCursor<> requires more alignment than unw_cursor_t");
  memset(&_info, 0, sizeof(_info));
}

template <typename A, typename R>
UnwindCursor<A, R>::UnwindCursor(A &as, void *)
    : _addressSpace(as), _unwindInfoMissing(false), _isSignalFrame(false) {
  memset(&_info, 0, sizeof(_info));
  // FIXME
  // fill in _registers from thread arg
}


template <typename A, typename R>
bool UnwindCursor<A, R>::validReg(int regNum) {
  return _registers.validRegister(regNum);
}

template <typename A, typename R>
unw_word_t UnwindCursor<A, R>::getReg(int regNum) {
  return _registers.getRegister(regNum);
}

template <typename A, typename R>
void UnwindCursor<A, R>::setReg(int regNum, unw_word_t value) {
  _registers.setRegister(regNum, (typename A::pint_t)value);
}

template <typename A, typename R>
bool UnwindCursor<A, R>::validFloatReg(int regNum) {
  return _registers.validFloatRegister(regNum);
}

template <typename A, typename R>
unw_fpreg_t UnwindCursor<A, R>::getFloatReg(int regNum) {
  return _registers.getFloatRegister(regNum);
}

template <typename A, typename R>
void UnwindCursor<A, R>::setFloatReg(int regNum, unw_fpreg_t value) {
  _registers.setFloatRegister(regNum, value);
}

template <typename A, typename R> void UnwindCursor<A, R>::jumpto() {
  _registers.jumpto();
}

#ifdef __arm__
template <typename A, typename R> void UnwindCursor<A, R>::saveVFPAsX() {
  _registers.saveVFPAsX();
}
#endif

#ifdef _AIX
template <typename A, typename R>
uintptr_t UnwindCursor<A, R>::getDataRelBase() {
  return reinterpret_cast<uintptr_t>(_info.extra);
}
#endif

template <typename A, typename R>
const char *UnwindCursor<A, R>::getRegisterName(int regNum) {
  return _registers.getRegisterName(regNum);
}

template <typename A, typename R> bool UnwindCursor<A, R>::isSignalFrame() {
  return _isSignalFrame;
}

#endif // defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)

#if defined(_LIBUNWIND_ARM_EHABI)
template<typename A>
struct EHABISectionIterator {
  typedef EHABISectionIterator _Self;

  typedef typename A::pint_t value_type;
  typedef typename A::pint_t* pointer;
  typedef typename A::pint_t& reference;
  typedef size_t size_type;
  typedef size_t difference_type;

  static _Self begin(A& addressSpace, const UnwindInfoSections& sects) {
    return _Self(addressSpace, sects, 0);
  }
  static _Self end(A& addressSpace, const UnwindInfoSections& sects) {
    return _Self(addressSpace, sects,
                 sects.arm_section_length / sizeof(EHABIIndexEntry));
  }

  EHABISectionIterator(A& addressSpace, const UnwindInfoSections& sects, size_t i)
      : _i(i), _addressSpace(&addressSpace), _sects(&sects) {}

  _Self& operator++() { ++_i; return *this; }
  _Self& operator+=(size_t a) { _i += a; return *this; }
  _Self& operator--() { assert(_i > 0); --_i; return *this; }
  _Self& operator-=(size_t a) { assert(_i >= a); _i -= a; return *this; }

  _Self operator+(size_t a) { _Self out = *this; out._i += a; return out; }
  _Self operator-(size_t a) { assert(_i >= a); _Self out = *this; out._i -= a; return out; }

  size_t operator-(const _Self& other) const { return _i - other._i; }

  bool operator==(const _Self& other) const {
    assert(_addressSpace == other._addressSpace);
    assert(_sects == other._sects);
    return _i == other._i;
  }

  bool operator!=(const _Self& other) const {
    assert(_addressSpace == other._addressSpace);
    assert(_sects == other._sects);
    return _i != other._i;
  }

  typename A::pint_t operator*() const { return functionAddress(); }

  typename A::pint_t functionAddress() const {
    typename A::pint_t indexAddr = _sects->arm_section + arrayoffsetof(
        EHABIIndexEntry, _i, functionOffset);
    return indexAddr + signExtendPrel31(_addressSpace->get32(indexAddr));
  }

  typename A::pint_t dataAddress() {
    typename A::pint_t indexAddr = _sects->arm_section + arrayoffsetof(
        EHABIIndexEntry, _i, data);
    return indexAddr;
  }

 private:
  size_t _i;
  A* _addressSpace;
  const UnwindInfoSections* _sects;
};

namespace {

template <typename A>
EHABISectionIterator<A> EHABISectionUpperBound(
    EHABISectionIterator<A> first,
    EHABISectionIterator<A> last,
    typename A::pint_t value) {
  size_t len = last - first;
  while (len > 0) {
    size_t l2 = len / 2;
    EHABISectionIterator<A> m = first + l2;
    if (value < *m) {
        len = l2;
    } else {
        first = ++m;
        len -= l2 + 1;
    }
  }
  return first;
}

}

template <typename A, typename R>
bool UnwindCursor<A, R>::getInfoFromEHABISection(
    pint_t pc,
    const UnwindInfoSections &sects) {
  EHABISectionIterator<A> begin =
      EHABISectionIterator<A>::begin(_addressSpace, sects);
  EHABISectionIterator<A> end =
      EHABISectionIterator<A>::end(_addressSpace, sects);
  if (begin == end)
    return false;

  EHABISectionIterator<A> itNextPC = EHABISectionUpperBound(begin, end, pc);
  if (itNextPC == begin)
    return false;
  EHABISectionIterator<A> itThisPC = itNextPC - 1;

  pint_t thisPC = itThisPC.functionAddress();
  // If an exception is thrown from a function, corresponding to the last entry
  // in the table, we don't really know the function extent and have to choose a
  // value for nextPC. Choosing max() will allow the range check during trace to
  // succeed.
  pint_t nextPC = (itNextPC == end) ? UINTPTR_MAX : itNextPC.functionAddress();
  pint_t indexDataAddr = itThisPC.dataAddress();

  if (indexDataAddr == 0)
    return false;

  uint32_t indexData = _addressSpace.get32(indexDataAddr);
  if (indexData == UNW_EXIDX_CANTUNWIND)
    return false;

  // If the high bit is set, the exception handling table entry is inline inside
  // the index table entry on the second word (aka |indexDataAddr|). Otherwise,
  // the table points at an offset in the exception handling table (section 5
  // EHABI).
  pint_t exceptionTableAddr;
  uint32_t exceptionTableData;
  bool isSingleWordEHT;
  if (indexData & 0x80000000) {
    exceptionTableAddr = indexDataAddr;
    // TODO(ajwong): Should this data be 0?
    exceptionTableData = indexData;
    isSingleWordEHT = true;
  } else {
    exceptionTableAddr = indexDataAddr + signExtendPrel31(indexData);
    exceptionTableData = _addressSpace.get32(exceptionTableAddr);
    isSingleWordEHT = false;
  }

  // Now we know the 3 things:
  //   exceptionTableAddr -- exception handler table entry.
  //   exceptionTableData -- the data inside the first word of the eht entry.
  //   isSingleWordEHT -- whether the entry is in the index.
  unw_word_t personalityRoutine = 0xbadf00d;
  bool scope32 = false;
  uintptr_t lsda;

  // If the high bit in the exception handling table entry is set, the entry is
  // in compact form (section 6.3 EHABI).
  if (exceptionTableData & 0x80000000) {
    // Grab the index of the personality routine from the compact form.
    uint32_t choice = (exceptionTableData & 0x0f000000) >> 24;
    uint32_t extraWords = 0;
    switch (choice) {
      case 0:
        personalityRoutine = (unw_word_t) &__aeabi_unwind_cpp_pr0;
        extraWords = 0;
        scope32 = false;
        lsda = isSingleWordEHT ? 0 : (exceptionTableAddr + 4);
        break;
      case 1:
        personalityRoutine = (unw_word_t) &__aeabi_unwind_cpp_pr1;
        extraWords = (exceptionTableData & 0x00ff0000) >> 16;
        scope32 = false;
        lsda = exceptionTableAddr + (extraWords + 1) * 4;
        break;
      case 2:
        personalityRoutine = (unw_word_t) &__aeabi_unwind_cpp_pr2;
        extraWords = (exceptionTableData & 0x00ff0000) >> 16;
        scope32 = true;
        lsda = exceptionTableAddr + (extraWords + 1) * 4;
        break;
      default:
        _LIBUNWIND_ABORT("unknown personality routine");
        return false;
    }

    if (isSingleWordEHT) {
      if (extraWords != 0) {
        _LIBUNWIND_ABORT("index inlined table detected but pr function "
                         "requires extra words");
        return false;
      }
    }
  } else {
    pint_t personalityAddr =
        exceptionTableAddr + signExtendPrel31(exceptionTableData);
    personalityRoutine = personalityAddr;

    // ARM EHABI # 6.2, # 9.2
    //
    //  +---- ehtp
    //  v
    // +--------------------------------------+
    // | +--------+--------+--------+-------+ |
    // | |0| prel31 to personalityRoutine   | |
    // | +--------+--------+--------+-------+ |
    // | |      N |      unwind opcodes     | |  <-- UnwindData
    // | +--------+--------+--------+-------+ |
    // | | Word 2        unwind opcodes     | |
    // | +--------+--------+--------+-------+ |
    // | ...                                  |
    // | +--------+--------+--------+-------+ |
    // | | Word N        unwind opcodes     | |
    // | +--------+--------+--------+-------+ |
    // | | LSDA                             | |  <-- lsda
    // | | ...                              | |
    // | +--------+--------+--------+-------+ |
    // +--------------------------------------+

    uint32_t *UnwindData = reinterpret_cast<uint32_t*>(exceptionTableAddr) + 1;
    uint32_t FirstDataWord = *UnwindData;
    size_t N = ((FirstDataWord >> 24) & 0xff);
    size_t NDataWords = N + 1;
    lsda = reinterpret_cast<uintptr_t>(UnwindData + NDataWords);
  }

  _info.start_ip = thisPC;
  _info.end_ip = nextPC;
  _info.handler = personalityRoutine;
  _info.unwind_info = exceptionTableAddr;
  _info.lsda = lsda;
  // flags is pr_cache.additional. See EHABI #7.2 for definition of bit 0.
  _info.flags = (isSingleWordEHT ? 1 : 0) | (scope32 ? 0x2 : 0);  // Use enum?

  return true;
}
#endif

#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
template <typename A, typename R>
bool UnwindCursor<A, R>::getInfoFromFdeCie(
    const typename CFI_Parser<A>::FDE_Info &fdeInfo,
    const typename CFI_Parser<A>::CIE_Info &cieInfo, pint_t pc,
    uintptr_t dso_base) {
  typename CFI_Parser<A>::PrologInfo prolog;
  if (CFI_Parser<A>::parseFDEInstructions(_addressSpace, fdeInfo, cieInfo, pc,
                                          R::getArch(), &prolog)) {
    // Save off parsed FDE info
    _info.start_ip          = fdeInfo.pcStart;
    _info.end_ip            = fdeInfo.pcEnd;
    _info.lsda              = fdeInfo.lsda;
    _info.handler           = cieInfo.personality;
    // Some frameless functions need SP altered when resuming in function, so
    // propagate spExtraArgSize.
    _info.gp                = prolog.spExtraArgSize;
    _info.flags             = 0;
    _info.format            = dwarfEncoding();
    _info.unwind_info       = fdeInfo.fdeStart;
    _info.unwind_info_size  = static_cast<uint32_t>(fdeInfo.fdeLength);
    _info.extra             = static_cast<unw_word_t>(dso_base);
    return true;
  }
  return false;
}

template <typename A, typename R>
bool UnwindCursor<A, R>::getInfoFromDwarfSection(pint_t pc,
                                                const UnwindInfoSections &sects,
                                                uint32_t fdeSectionOffsetHint) {
  typename CFI_Parser<A>::FDE_Info fdeInfo;
  typename CFI_Parser<A>::CIE_Info cieInfo;
  bool foundFDE = false;
  bool foundInCache = false;
  // If compact encoding table gave offset into dwarf section, go directly there
  if (fdeSectionOffsetHint != 0) {
    foundFDE = CFI_Parser<A>::findFDE(_addressSpace, pc, sects.dwarf_section,
                                    sects.dwarf_section_length,
                                    sects.dwarf_section + fdeSectionOffsetHint,
                                    &fdeInfo, &cieInfo);
  }
#if defined(_LIBUNWIND_SUPPORT_DWARF_INDEX)
  if (!foundFDE && (sects.dwarf_index_section != 0)) {
    foundFDE = EHHeaderParser<A>::findFDE(
        _addressSpace, pc, sects.dwarf_index_section,
        (uint32_t)sects.dwarf_index_section_length, &fdeInfo, &cieInfo);
  }
#endif
  if (!foundFDE) {
    // otherwise, search cache of previously found FDEs.
    pint_t cachedFDE = DwarfFDECache<A>::findFDE(sects.dso_base, pc);
    if (cachedFDE != 0) {
      foundFDE =
          CFI_Parser<A>::findFDE(_addressSpace, pc, sects.dwarf_section,
                                 sects.dwarf_section_length,
                                 cachedFDE, &fdeInfo, &cieInfo);
      foundInCache = foundFDE;
    }
  }
  if (!foundFDE) {
    // Still not found, do full scan of __eh_frame section.
    foundFDE = CFI_Parser<A>::findFDE(_addressSpace, pc, sects.dwarf_section,
                                      sects.dwarf_section_length, 0,
                                      &fdeInfo, &cieInfo);
  }
  if (foundFDE) {
    if (getInfoFromFdeCie(fdeInfo, cieInfo, pc, sects.dso_base)) {
      // Add to cache (to make next lookup faster) if we had no hint
      // and there was no index.
      if (!foundInCache && (fdeSectionOffsetHint == 0)) {
  #if defined(_LIBUNWIND_SUPPORT_DWARF_INDEX)
        if (sects.dwarf_index_section == 0)
  #endif
        DwarfFDECache<A>::add(sects.dso_base, fdeInfo.pcStart, fdeInfo.pcEnd,
                              fdeInfo.fdeStart);
      }
      return true;
    }
  }
  //_LIBUNWIND_DEBUG_LOG("can't find/use FDE for pc=0x%llX", (uint64_t)pc);
  return false;
}
#endif // defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)


#if defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)
template <typename A, typename R>
bool UnwindCursor<A, R>::getInfoFromCompactEncodingSection(pint_t pc,
                                              const UnwindInfoSections &sects) {
  const bool log = false;
  if (log)
    fprintf(stderr, "getInfoFromCompactEncodingSection(pc=0x%llX, mh=0x%llX)\n",
            (uint64_t)pc, (uint64_t)sects.dso_base);

  const UnwindSectionHeader<A> sectionHeader(_addressSpace,
                                                sects.compact_unwind_section);
  if (sectionHeader.version() != UNWIND_SECTION_VERSION)
    return false;

  // do a binary search of top level index to find page with unwind info
  pint_t targetFunctionOffset = pc - sects.dso_base;
  const UnwindSectionIndexArray<A> topIndex(_addressSpace,
                                           sects.compact_unwind_section
                                         + sectionHeader.indexSectionOffset());
  uint32_t low = 0;
  uint32_t high = sectionHeader.indexCount();
  uint32_t last = high - 1;
  while (low < high) {
    uint32_t mid = (low + high) / 2;
    //if ( log ) fprintf(stderr, "\tmid=%d, low=%d, high=%d, *mid=0x%08X\n",
    //mid, low, high, topIndex.functionOffset(mid));
    if (topIndex.functionOffset(mid) <= targetFunctionOffset) {
      if ((mid == last) ||
          (topIndex.functionOffset(mid + 1) > targetFunctionOffset)) {
        low = mid;
        break;
      } else {
        low = mid + 1;
      }
    } else {
      high = mid;
    }
  }
  const uint32_t firstLevelFunctionOffset = topIndex.functionOffset(low);
  const uint32_t firstLevelNextPageFunctionOffset =
      topIndex.functionOffset(low + 1);
  const pint_t secondLevelAddr =
      sects.compact_unwind_section + topIndex.secondLevelPagesSectionOffset(low);
  const pint_t lsdaArrayStartAddr =
      sects.compact_unwind_section + topIndex.lsdaIndexArraySectionOffset(low);
  const pint_t lsdaArrayEndAddr =
      sects.compact_unwind_section + topIndex.lsdaIndexArraySectionOffset(low+1);
  if (log)
    fprintf(stderr, "\tfirst level search for result index=%d "
                    "to secondLevelAddr=0x%llX\n",
                    low, (uint64_t) secondLevelAddr);
  // do a binary search of second level page index
  uint32_t encoding = 0;
  pint_t funcStart = 0;
  pint_t funcEnd = 0;
  pint_t lsda = 0;
  pint_t personality = 0;
  uint32_t pageKind = _addressSpace.get32(secondLevelAddr);
  if (pageKind == UNWIND_SECOND_LEVEL_REGULAR) {
    // regular page
    UnwindSectionRegularPageHeader<A> pageHeader(_addressSpace,
                                                 secondLevelAddr);
    UnwindSectionRegularArray<A> pageIndex(
        _addressSpace, secondLevelAddr + pageHeader.entryPageOffset());
    // binary search looks for entry with e where index[e].offset <= pc <
    // index[e+1].offset
    if (log)
      fprintf(stderr, "\tbinary search for targetFunctionOffset=0x%08llX in "
                      "regular page starting at secondLevelAddr=0x%llX\n",
              (uint64_t) targetFunctionOffset, (uint64_t) secondLevelAddr);
    low = 0;
    high = pageHeader.entryCount();
    while (low < high) {
      uint32_t mid = (low + high) / 2;
      if (pageIndex.functionOffset(mid) <= targetFunctionOffset) {
        if (mid == (uint32_t)(pageHeader.entryCount() - 1)) {
          // at end of table
          low = mid;
          funcEnd = firstLevelNextPageFunctionOffset + sects.dso_base;
          break;
        } else if (pageIndex.functionOffset(mid + 1) > targetFunctionOffset) {
          // next is too big, so we found it
          low = mid;
          funcEnd = pageIndex.functionOffset(low + 1) + sects.dso_base;
          break;
        } else {
          low = mid + 1;
        }
      } else {
        high = mid;
      }
    }
    encoding = pageIndex.encoding(low);
    funcStart = pageIndex.functionOffset(low) + sects.dso_base;
    if (pc < funcStart) {
      if (log)
        fprintf(
            stderr,
            "\tpc not in table, pc=0x%llX, funcStart=0x%llX, funcEnd=0x%llX\n",
            (uint64_t) pc, (uint64_t) funcStart, (uint64_t) funcEnd);
      return false;
    }
    if (pc > funcEnd) {
      if (log)
        fprintf(
            stderr,
            "\tpc not in table, pc=0x%llX, funcStart=0x%llX, funcEnd=0x%llX\n",
            (uint64_t) pc, (uint64_t) funcStart, (uint64_t) funcEnd);
      return false;
    }
  } else if (pageKind == UNWIND_SECOND_LEVEL_COMPRESSED) {
    // compressed page
    UnwindSectionCompressedPageHeader<A> pageHeader(_addressSpace,
                                                    secondLevelAddr);
    UnwindSectionCompressedArray<A> pageIndex(
        _addressSpace, secondLevelAddr + pageHeader.entryPageOffset());
    const uint32_t targetFunctionPageOffset =
        (uint32_t)(targetFunctionOffset - firstLevelFunctionOffset);
    // binary search looks for entry with e where index[e].offset <= pc <
    // index[e+1].offset
    if (log)
      fprintf(stderr, "\tbinary search of compressed page starting at "
                      "secondLevelAddr=0x%llX\n",
              (uint64_t) secondLevelAddr);
    low = 0;
    last = pageHeader.entryCount() - 1;
    high = pageHeader.entryCount();
    while (low < high) {
      uint32_t mid = (low + high) / 2;
      if (pageIndex.functionOffset(mid) <= targetFunctionPageOffset) {
        if ((mid == last) ||
            (pageIndex.functionOffset(mid + 1) > targetFunctionPageOffset)) {
          low = mid;
          break;
        } else {
          low = mid + 1;
        }
      } else {
        high = mid;
      }
    }
    funcStart = pageIndex.functionOffset(low) + firstLevelFunctionOffset
                                                              + sects.dso_base;
    if (low < last)
      funcEnd =
          pageIndex.functionOffset(low + 1) + firstLevelFunctionOffset
                                                              + sects.dso_base;
    else
      funcEnd = firstLevelNextPageFunctionOffset + sects.dso_base;
    if (pc < funcStart) {
      _LIBUNWIND_DEBUG_LOG("malformed __unwind_info, pc=0x%llX "
                           "not in second level compressed unwind table. "
                           "funcStart=0x%llX",
                            (uint64_t) pc, (uint64_t) funcStart);
      return false;
    }
    if (pc > funcEnd) {
      _LIBUNWIND_DEBUG_LOG("malformed __unwind_info, pc=0x%llX "
                           "not in second level compressed unwind table. "
                           "funcEnd=0x%llX",
                           (uint64_t) pc, (uint64_t) funcEnd);
      return false;
    }
    uint16_t encodingIndex = pageIndex.encodingIndex(low);
    if (encodingIndex < sectionHeader.commonEncodingsArrayCount()) {
      // encoding is in common table in section header
      encoding = _addressSpace.get32(
          sects.compact_unwind_section +
          sectionHeader.commonEncodingsArraySectionOffset() +
          encodingIndex * sizeof(uint32_t));
    } else {
      // encoding is in page specific table
      uint16_t pageEncodingIndex =
          encodingIndex - (uint16_t)sectionHeader.commonEncodingsArrayCount();
      encoding = _addressSpace.get32(secondLevelAddr +
                                     pageHeader.encodingsPageOffset() +
                                     pageEncodingIndex * sizeof(uint32_t));
    }
  } else {
    _LIBUNWIND_DEBUG_LOG(
        "malformed __unwind_info at 0x%0llX bad second level page",
        (uint64_t)sects.compact_unwind_section);
    return false;
  }

  // look up LSDA, if encoding says function has one
  if (encoding & UNWIND_HAS_LSDA) {
    UnwindSectionLsdaArray<A> lsdaIndex(_addressSpace, lsdaArrayStartAddr);
    uint32_t funcStartOffset = (uint32_t)(funcStart - sects.dso_base);
    low = 0;
    high = (uint32_t)(lsdaArrayEndAddr - lsdaArrayStartAddr) /
                    sizeof(unwind_info_section_header_lsda_index_entry);
    // binary search looks for entry with exact match for functionOffset
    if (log)
      fprintf(stderr,
              "\tbinary search of lsda table for targetFunctionOffset=0x%08X\n",
              funcStartOffset);
    while (low < high) {
      uint32_t mid = (low + high) / 2;
      if (lsdaIndex.functionOffset(mid) == funcStartOffset) {
        lsda = lsdaIndex.lsdaOffset(mid) + sects.dso_base;
        break;
      } else if (lsdaIndex.functionOffset(mid) < funcStartOffset) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    if (lsda == 0) {
      _LIBUNWIND_DEBUG_LOG("found encoding 0x%08X with HAS_LSDA bit set for "
                    "pc=0x%0llX, but lsda table has no entry",
                    encoding, (uint64_t) pc);
      return false;
    }
  }

  // extract personality routine, if encoding says function has one
  uint32_t personalityIndex = (encoding & UNWIND_PERSONALITY_MASK) >>
                              (__builtin_ctz(UNWIND_PERSONALITY_MASK));
  if (personalityIndex != 0) {
    --personalityIndex; // change 1-based to zero-based index
    if (personalityIndex >= sectionHeader.personalityArrayCount()) {
      _LIBUNWIND_DEBUG_LOG("found encoding 0x%08X with personality index %d,  "
                            "but personality table has only %d entries",
                            encoding, personalityIndex,
                            sectionHeader.personalityArrayCount());
      return false;
    }
    int32_t personalityDelta = (int32_t)_addressSpace.get32(
        sects.compact_unwind_section +
        sectionHeader.personalityArraySectionOffset() +
        personalityIndex * sizeof(uint32_t));
    pint_t personalityPointer = sects.dso_base + (pint_t)personalityDelta;
    personality = _addressSpace.getP(personalityPointer);
    if (log)
      fprintf(stderr, "getInfoFromCompactEncodingSection(pc=0x%llX), "
                      "personalityDelta=0x%08X, personality=0x%08llX\n",
              (uint64_t) pc, personalityDelta, (uint64_t) personality);
  }

  if (log)
    fprintf(stderr, "getInfoFromCompactEncodingSection(pc=0x%llX), "
                    "encoding=0x%08X, lsda=0x%08llX for funcStart=0x%llX\n",
            (uint64_t) pc, encoding, (uint64_t) lsda, (uint64_t) funcStart);
  _info.start_ip = funcStart;
  _info.end_ip = funcEnd;
  _info.lsda = lsda;
  _info.handler = personality;
  _info.gp = 0;
  _info.flags = 0;
  _info.format = encoding;
  _info.unwind_info = 0;
  _info.unwind_info_size = 0;
  _info.extra = sects.dso_base;
  return true;
}
#endif // defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)


#if defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)
template <typename A, typename R>
bool UnwindCursor<A, R>::getInfoFromSEH(pint_t pc) {
  pint_t base;
  RUNTIME_FUNCTION *unwindEntry = lookUpSEHUnwindInfo(pc, &base);
  if (!unwindEntry) {
    _LIBUNWIND_DEBUG_LOG("\tpc not in table, pc=0x%llX", (uint64_t) pc);
    return false;
  }
  _info.gp = 0;
  _info.flags = 0;
  _info.format = 0;
  _info.unwind_info_size = sizeof(RUNTIME_FUNCTION);
  _info.unwind_info = reinterpret_cast<unw_word_t>(unwindEntry);
  _info.extra = base;
  _info.start_ip = base + unwindEntry->BeginAddress;
#ifdef _LIBUNWIND_TARGET_X86_64
  _info.end_ip = base + unwindEntry->EndAddress;
  // Only fill in the handler and LSDA if they're stale.
  if (pc != getLastPC()) {
    UNWIND_INFO *xdata = reinterpret_cast<UNWIND_INFO *>(base + unwindEntry->UnwindData);
    if (xdata->Flags & (UNW_FLAG_EHANDLER|UNW_FLAG_UHANDLER)) {
      // The personality is given in the UNWIND_INFO itself. The LSDA immediately
      // follows the UNWIND_INFO. (This follows how both Clang and MSVC emit
      // these structures.)
      // N.B. UNWIND_INFO structs are DWORD-aligned.
      uint32_t lastcode = (xdata->CountOfCodes + 1) & ~1;
      const uint32_t *handler = reinterpret_cast<uint32_t *>(&xdata->UnwindCodes[lastcode]);
      _info.lsda = reinterpret_cast<unw_word_t>(handler+1);
      _dispContext.HandlerData = reinterpret_cast<void *>(_info.lsda);
      _dispContext.LanguageHandler =
          reinterpret_cast<EXCEPTION_ROUTINE *>(base + *handler);
      if (*handler) {
        _info.handler = reinterpret_cast<unw_word_t>(__libunwind_seh_personality);
      } else
        _info.handler = 0;
    } else {
      _info.lsda = 0;
      _info.handler = 0;
    }
  }
#endif
  setLastPC(pc);
  return true;
}
#endif

#if defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND)
// Masks for traceback table field xtbtable.
enum xTBTableMask : uint8_t {
  reservedBit = 0x02, // The traceback table was incorrectly generated if set
                      // (see comments in function getInfoFromTBTable().
  ehInfoBit = 0x08    // Exception handling info is present if set
};

enum frameType : unw_word_t {
  frameWithXLEHStateTable = 0,
  frameWithEHInfo = 1
};

extern "C" {
typedef _Unwind_Reason_Code __xlcxx_personality_v0_t(int, _Unwind_Action,
                                                     uint64_t,
                                                     _Unwind_Exception *,
                                                     struct _Unwind_Context *);
__attribute__((__weak__)) __xlcxx_personality_v0_t __xlcxx_personality_v0;
}

static __xlcxx_personality_v0_t *xlcPersonalityV0;
static RWMutex xlcPersonalityV0InitLock;

template <typename A, typename R>
bool UnwindCursor<A, R>::getInfoFromTBTable(pint_t pc, R &registers) {
  uint32_t *p = reinterpret_cast<uint32_t *>(pc);

  // Keep looking forward until a word of 0 is found. The traceback
  // table starts at the following word.
  while (*p)
    ++p;
  tbtable *TBTable = reinterpret_cast<tbtable *>(p + 1);

  if (_LIBUNWIND_TRACING_UNWINDING) {
    char functionBuf[512];
    const char *functionName = functionBuf;
    unw_word_t offset;
    if (!getFunctionName(functionBuf, sizeof(functionBuf), &offset)) {
      functionName = ".anonymous.";
    }
    _LIBUNWIND_TRACE_UNWINDING("%s: Look up traceback table of func=%s at %p",
                               __func__, functionName,
                               reinterpret_cast<void *>(TBTable));
  }

  // If the traceback table does not contain necessary info, bypass this frame.
  if (!TBTable->tb.has_tboff)
    return false;

  // Structure tbtable_ext contains important data we are looking for.
  p = reinterpret_cast<uint32_t *>(&TBTable->tb_ext);

  // Skip field parminfo if it exists.
  if (TBTable->tb.fixedparms || TBTable->tb.floatparms)
    ++p;

  // p now points to tb_offset, the offset from start of function to TB table.
  unw_word_t start_ip =
      reinterpret_cast<unw_word_t>(TBTable) - *p - sizeof(uint32_t);
  unw_word_t end_ip = reinterpret_cast<unw_word_t>(TBTable);
  ++p;

  _LIBUNWIND_TRACE_UNWINDING("start_ip=%p, end_ip=%p\n",
                             reinterpret_cast<void *>(start_ip),
                             reinterpret_cast<void *>(end_ip));

  // Skip field hand_mask if it exists.
  if (TBTable->tb.int_hndl)
    ++p;

  unw_word_t lsda = 0;
  unw_word_t handler = 0;
  unw_word_t flags = frameType::frameWithXLEHStateTable;

  if (TBTable->tb.lang == TB_CPLUSPLUS && TBTable->tb.has_ctl) {
    // State table info is available. The ctl_info field indicates the
    // number of CTL anchors. There should be only one entry for the C++
    // state table.
    assert(*p == 1 && "libunwind: there must be only one ctl_info entry");
    ++p;
    // p points to the offset of the state table into the stack.
    pint_t stateTableOffset = *p++;

    int framePointerReg;

    // Skip fields name_len and name if exist.
    if (TBTable->tb.name_present) {
      const uint16_t name_len = *(reinterpret_cast<uint16_t *>(p));
      p = reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(p) + name_len +
                                       sizeof(uint16_t));
    }

    if (TBTable->tb.uses_alloca)
      framePointerReg = *(reinterpret_cast<char *>(p));
    else
      framePointerReg = 1; // default frame pointer == SP

    _LIBUNWIND_TRACE_UNWINDING(
        "framePointerReg=%d, framePointer=%p, "
        "stateTableOffset=%#lx\n",
        framePointerReg,
        reinterpret_cast<void *>(_registers.getRegister(framePointerReg)),
        stateTableOffset);
    lsda = _registers.getRegister(framePointerReg) + stateTableOffset;

    // Since the traceback table generated by the legacy XLC++ does not
    // provide the location of the personality for the state table,
    // function __xlcxx_personality_v0(), which is the personality for the state
    // table and is exported from libc++abi, is directly assigned as the
    // handler here. When a legacy XLC++ frame is encountered, the symbol
    // is resolved dynamically using dlopen() to avoid hard dependency from
    // libunwind on libc++abi.

    // Resolve the function pointer to the state table personality if it has
    // not already.
    if (xlcPersonalityV0 == NULL) {
      xlcPersonalityV0InitLock.lock();
      if (xlcPersonalityV0 == NULL) {
        // If libc++abi is statically linked in, symbol __xlcxx_personality_v0
        // has been resolved at the link time.
        xlcPersonalityV0 = &__xlcxx_personality_v0;
        if (xlcPersonalityV0 == NULL) {
          // libc++abi is dynamically linked. Resolve __xlcxx_personality_v0
          // using dlopen().
          const char libcxxabi[] = "libc++abi.a(libc++abi.so.1)";
          void *libHandle;
          // The AIX dlopen() sets errno to 0 when it is successful, which
          // clobbers the value of errno from the user code. This is an AIX
          // bug because according to POSIX it should not set errno to 0. To
          // workaround before AIX fixes the bug, errno is saved and restored.
          int saveErrno = errno;
          libHandle = dlopen(libcxxabi, RTLD_MEMBER | RTLD_NOW);
          if (libHandle == NULL) {
            _LIBUNWIND_TRACE_UNWINDING("dlopen() failed with errno=%d\n",
                                       errno);
            assert(0 && "dlopen() failed");
          }
          xlcPersonalityV0 = reinterpret_cast<__xlcxx_personality_v0_t *>(
              dlsym(libHandle, "__xlcxx_personality_v0"));
          if (xlcPersonalityV0 == NULL) {
            _LIBUNWIND_TRACE_UNWINDING("dlsym() failed with errno=%d\n", errno);
            assert(0 && "dlsym() failed");
          }
          dlclose(libHandle);
          errno = saveErrno;
        }
      }
      xlcPersonalityV0InitLock.unlock();
    }
    handler = reinterpret_cast<unw_word_t>(xlcPersonalityV0);
    _LIBUNWIND_TRACE_UNWINDING("State table: LSDA=%p, Personality=%p\n",
                               reinterpret_cast<void *>(lsda),
                               reinterpret_cast<void *>(handler));
  } else if (TBTable->tb.longtbtable) {
    // This frame has the traceback table extension. Possible cases are
    // 1) a C++ frame that has the 'eh_info' structure; 2) a C++ frame that
    // is not EH aware; or, 3) a frame of other languages. We need to figure out
    // if the traceback table extension contains the 'eh_info' structure.
    //
    // We also need to deal with the complexity arising from some XL compiler
    // versions use the wrong ordering of 'longtbtable' and 'has_vec' bits
    // where the 'longtbtable' bit is meant to be the 'has_vec' bit and vice
    // versa. For frames of code generated by those compilers, the 'longtbtable'
    // bit may be set but there isn't really a traceback table extension.
    //
    // In </usr/include/sys/debug.h>, there is the following definition of
    // 'struct tbtable_ext'. It is not really a structure but a dummy to
    // collect the description of optional parts of the traceback table.
    //
    // struct tbtable_ext {
    //   ...
    //   char alloca_reg;        /* Register for alloca automatic storage */
    //   struct vec_ext vec_ext; /* Vector extension (if has_vec is set) */
    //   unsigned char xtbtable; /* More tbtable fields, if longtbtable is set*/
    // };
    //
    // Depending on how the 'has_vec'/'longtbtable' bit is interpreted, the data
    // following 'alloca_reg' can be treated either as 'struct vec_ext' or
    // 'unsigned char xtbtable'. 'xtbtable' bits are defined in
    // </usr/include/sys/debug.h> as flags. The 7th bit '0x02' is currently
    // unused and should not be set. 'struct vec_ext' is defined in
    // </usr/include/sys/debug.h> as follows:
    //
    // struct vec_ext {
    //   unsigned vr_saved:6;      /* Number of non-volatile vector regs saved
    //   */
    //                             /* first register saved is assumed to be */
    //                             /* 32 - vr_saved                         */
    //   unsigned saves_vrsave:1;  /* Set if vrsave is saved on the stack */
    //   unsigned has_varargs:1;
    //   ...
    // };
    //
    // Here, the 7th bit is used as 'saves_vrsave'. To determine whether it
    // is 'struct vec_ext' or 'xtbtable' that follows 'alloca_reg',
    // we checks if the 7th bit is set or not because 'xtbtable' should
    // never have the 7th bit set. The 7th bit of 'xtbtable' will be reserved
    // in the future to make sure the mitigation works. This mitigation
    // is not 100% bullet proof because 'struct vec_ext' may not always have
    // 'saves_vrsave' bit set.
    //
    // 'reservedBit' is defined in enum 'xTBTableMask' above as the mask for
    // checking the 7th bit.

    // p points to field name len.
    uint8_t *charPtr = reinterpret_cast<uint8_t *>(p);

    // Skip fields name_len and name if they exist.
    if (TBTable->tb.name_present) {
      const uint16_t name_len = *(reinterpret_cast<uint16_t *>(charPtr));
      charPtr = charPtr + name_len + sizeof(uint16_t);
    }

    // Skip field alloc_reg if it exists.
    if (TBTable->tb.uses_alloca)
      ++charPtr;

    // Check traceback table bit has_vec. Skip struct vec_ext if it exists.
    if (TBTable->tb.has_vec)
      // Note struct vec_ext does exist at this point because whether the
      // ordering of longtbtable and has_vec bits is correct or not, both
      // are set.
      charPtr += sizeof(struct vec_ext);

    // charPtr points to field 'xtbtable'. Check if the EH info is available.
    // Also check if the reserved bit of the extended traceback table field
    // 'xtbtable' is set. If it is, the traceback table was incorrectly
    // generated by an XL compiler that uses the wrong ordering of 'longtbtable'
    // and 'has_vec' bits and this is in fact 'struct vec_ext'. So skip the
    // frame.
    if ((*charPtr & xTBTableMask::ehInfoBit) &&
        !(*charPtr & xTBTableMask::reservedBit)) {
      // Mark this frame has the new EH info.
      flags = frameType::frameWithEHInfo;

      // eh_info is available.
      charPtr++;
      // The pointer is 4-byte aligned.
      if (reinterpret_cast<uintptr_t>(charPtr) % 4)
        charPtr += 4 - reinterpret_cast<uintptr_t>(charPtr) % 4;
      uintptr_t *ehInfo =
          reinterpret_cast<uintptr_t *>(*(reinterpret_cast<uintptr_t *>(
              registers.getRegister(2) +
              *(reinterpret_cast<uintptr_t *>(charPtr)))));

      // ehInfo points to structure en_info. The first member is version.
      // Only version 0 is currently supported.
      assert(*(reinterpret_cast<uint32_t *>(ehInfo)) == 0 &&
             "libunwind: ehInfo version other than 0 is not supported");

      // Increment ehInfo to point to member lsda.
      ++ehInfo;
      lsda = *ehInfo++;

      // enInfo now points to member personality.
      handler = *ehInfo;

      _LIBUNWIND_TRACE_UNWINDING("Range table: LSDA=%#lx, Personality=%#lx\n",
                                 lsda, handler);
    }
  }

  _info.start_ip = start_ip;
  _info.end_ip = end_ip;
  _info.lsda = lsda;
  _info.handler = handler;
  _info.gp = 0;
  _info.flags = flags;
  _info.format = 0;
  _info.unwind_info = reinterpret_cast<unw_word_t>(TBTable);
  _info.unwind_info_size = 0;
  _info.extra = registers.getRegister(2);

  return true;
}

// Step back up the stack following the frame back link.
template <typename A, typename R>
int UnwindCursor<A, R>::stepWithTBTable(pint_t pc, tbtable *TBTable,
                                        R &registers, bool &isSignalFrame) {
  if (_LIBUNWIND_TRACING_UNWINDING) {
    char functionBuf[512];
    const char *functionName = functionBuf;
    unw_word_t offset;
    if (!getFunctionName(functionBuf, sizeof(functionBuf), &offset)) {
      functionName = ".anonymous.";
    }
    _LIBUNWIND_TRACE_UNWINDING(
        "%s: Look up traceback table of func=%s at %p, pc=%p, "
        "SP=%p, saves_lr=%d, stores_bc=%d",
        __func__, functionName, reinterpret_cast<void *>(TBTable),
        reinterpret_cast<void *>(pc),
        reinterpret_cast<void *>(registers.getSP()), TBTable->tb.saves_lr,
        TBTable->tb.stores_bc);
  }

#if defined(__powerpc64__)
  // Instruction to reload TOC register "ld r2,40(r1)"
  const uint32_t loadTOCRegInst = 0xe8410028;
  const int32_t unwPPCF0Index = UNW_PPC64_F0;
  const int32_t unwPPCV0Index = UNW_PPC64_V0;
#else
  // Instruction to reload TOC register "lwz r2,20(r1)"
  const uint32_t loadTOCRegInst = 0x80410014;
  const int32_t unwPPCF0Index = UNW_PPC_F0;
  const int32_t unwPPCV0Index = UNW_PPC_V0;
#endif

  // lastStack points to the stack frame of the next routine up.
  pint_t curStack = static_cast<pint_t>(registers.getSP());
  pint_t lastStack = *reinterpret_cast<pint_t *>(curStack);

  if (lastStack == 0)
    return UNW_STEP_END;

  R newRegisters = registers;

  // If backchain is not stored, use the current stack frame.
  if (!TBTable->tb.stores_bc)
    lastStack = curStack;

  // Return address is the address after call site instruction.
  pint_t returnAddress;

  if (isSignalFrame) {
    _LIBUNWIND_TRACE_UNWINDING("Possible signal handler frame: lastStack=%p",
                               reinterpret_cast<void *>(lastStack));

    sigcontext *sigContext = reinterpret_cast<sigcontext *>(
        reinterpret_cast<char *>(lastStack) + STKMINALIGN);
    returnAddress = sigContext->sc_jmpbuf.jmp_context.iar;

    bool useSTKMIN = false;
    if (returnAddress < 0x10000000) {
      // Try again using STKMIN.
      sigContext = reinterpret_cast<sigcontext *>(
          reinterpret_cast<char *>(lastStack) + STKMIN);
      returnAddress = sigContext->sc_jmpbuf.jmp_context.iar;
      if (returnAddress < 0x10000000) {
        _LIBUNWIND_TRACE_UNWINDING("Bad returnAddress=%p from sigcontext=%p",
                                   reinterpret_cast<void *>(returnAddress),
                                   reinterpret_cast<void *>(sigContext));
        return UNW_EBADFRAME;
      }
      useSTKMIN = true;
    }
    _LIBUNWIND_TRACE_UNWINDING("Returning from a signal handler %s: "
                               "sigContext=%p, returnAddress=%p. "
                               "Seems to be a valid address",
                               useSTKMIN ? "STKMIN" : "STKMINALIGN",
                               reinterpret_cast<void *>(sigContext),
                               reinterpret_cast<void *>(returnAddress));

    // Restore the condition register from sigcontext.
    newRegisters.setCR(sigContext->sc_jmpbuf.jmp_context.cr);

    // Save the LR in sigcontext for stepping up when the function that
    // raised the signal is a leaf function. This LR has the return address
    // to the caller of the leaf function.
    newRegisters.setLR(sigContext->sc_jmpbuf.jmp_context.lr);
    _LIBUNWIND_TRACE_UNWINDING(
        "Save LR=%p from sigcontext",
        reinterpret_cast<void *>(sigContext->sc_jmpbuf.jmp_context.lr));

    // Restore GPRs from sigcontext.
    for (int i = 0; i < 32; ++i)
      newRegisters.setRegister(i, sigContext->sc_jmpbuf.jmp_context.gpr[i]);

    // Restore FPRs from sigcontext.
    for (int i = 0; i < 32; ++i)
      newRegisters.setFloatRegister(i + unwPPCF0Index,
                                    sigContext->sc_jmpbuf.jmp_context.fpr[i]);

    // Restore vector registers if there is an associated extended context
    // structure.
    if (sigContext->sc_jmpbuf.jmp_context.msr & __EXTCTX) {
      ucontext_t *uContext = reinterpret_cast<ucontext_t *>(sigContext);
      if (uContext->__extctx->__extctx_magic == __EXTCTX_MAGIC) {
        for (int i = 0; i < 32; ++i)
          newRegisters.setVectorRegister(
              i + unwPPCV0Index, *(reinterpret_cast<v128 *>(
                                     &(uContext->__extctx->__vmx.__vr[i]))));
      }
    }
  } else {
    // Step up a normal frame.

    if (!TBTable->tb.saves_lr && registers.getLR()) {
      // This case should only occur if we were called from a signal handler
      // and the signal occurred in a function that doesn't save the LR.
      returnAddress = static_cast<pint_t>(registers.getLR());
      _LIBUNWIND_TRACE_UNWINDING("Use saved LR=%p",
                                 reinterpret_cast<void *>(returnAddress));
    } else {
      // Otherwise, use the LR value in the stack link area.
      returnAddress = reinterpret_cast<pint_t *>(lastStack)[2];
    }

    // Reset LR in the current context.
    newRegisters.setLR(static_cast<uintptr_t>(NULL));

    _LIBUNWIND_TRACE_UNWINDING(
        "Extract info from lastStack=%p, returnAddress=%p",
        reinterpret_cast<void *>(lastStack),
        reinterpret_cast<void *>(returnAddress));
    _LIBUNWIND_TRACE_UNWINDING("fpr_regs=%d, gpr_regs=%d, saves_cr=%d",
                               TBTable->tb.fpr_saved, TBTable->tb.gpr_saved,
                               TBTable->tb.saves_cr);

    // Restore FP registers.
    char *ptrToRegs = reinterpret_cast<char *>(lastStack);
    double *FPRegs = reinterpret_cast<double *>(
        ptrToRegs - (TBTable->tb.fpr_saved * sizeof(double)));
    for (int i = 0; i < TBTable->tb.fpr_saved; ++i)
      newRegisters.setFloatRegister(
          32 - TBTable->tb.fpr_saved + i + unwPPCF0Index, FPRegs[i]);

    // Restore GP registers.
    ptrToRegs = reinterpret_cast<char *>(FPRegs);
    uintptr_t *GPRegs = reinterpret_cast<uintptr_t *>(
        ptrToRegs - (TBTable->tb.gpr_saved * sizeof(uintptr_t)));
    for (int i = 0; i < TBTable->tb.gpr_saved; ++i)
      newRegisters.setRegister(32 - TBTable->tb.gpr_saved + i, GPRegs[i]);

    // Restore Vector registers.
    ptrToRegs = reinterpret_cast<char *>(GPRegs);

    // Restore vector registers only if this is a Clang frame. Also
    // check if traceback table bit has_vec is set. If it is, structure
    // vec_ext is available.
    if (_info.flags == frameType::frameWithEHInfo && TBTable->tb.has_vec) {

      // Get to the vec_ext structure to check if vector registers are saved.
      uint32_t *p = reinterpret_cast<uint32_t *>(&TBTable->tb_ext);

      // Skip field parminfo if exists.
      if (TBTable->tb.fixedparms || TBTable->tb.floatparms)
        ++p;

      // Skip field tb_offset if exists.
      if (TBTable->tb.has_tboff)
        ++p;

      // Skip field hand_mask if exists.
      if (TBTable->tb.int_hndl)
        ++p;

      // Skip fields ctl_info and ctl_info_disp if exist.
      if (TBTable->tb.has_ctl) {
        // Skip field ctl_info.
        ++p;
        // Skip field ctl_info_disp.
        ++p;
      }

      // Skip fields name_len and name if exist.
      // p is supposed to point to field name_len now.
      uint8_t *charPtr = reinterpret_cast<uint8_t *>(p);
      if (TBTable->tb.name_present) {
        const uint16_t name_len = *(reinterpret_cast<uint16_t *>(charPtr));
        charPtr = charPtr + name_len + sizeof(uint16_t);
      }

      // Skip field alloc_reg if it exists.
      if (TBTable->tb.uses_alloca)
        ++charPtr;

      struct vec_ext *vec_ext = reinterpret_cast<struct vec_ext *>(charPtr);

      _LIBUNWIND_TRACE_UNWINDING("vr_saved=%d", vec_ext->vr_saved);

      // Restore vector register(s) if saved on the stack.
      if (vec_ext->vr_saved) {
        // Saved vector registers are 16-byte aligned.
        if (reinterpret_cast<uintptr_t>(ptrToRegs) % 16)
          ptrToRegs -= reinterpret_cast<uintptr_t>(ptrToRegs) % 16;
        v128 *VecRegs = reinterpret_cast<v128 *>(ptrToRegs - vec_ext->vr_saved *
                                                                 sizeof(v128));
        for (int i = 0; i < vec_ext->vr_saved; ++i) {
          newRegisters.setVectorRegister(
              32 - vec_ext->vr_saved + i + unwPPCV0Index, VecRegs[i]);
        }
      }
    }
    if (TBTable->tb.saves_cr) {
      // Get the saved condition register. The condition register is only
      // a single word.
      newRegisters.setCR(
          *(reinterpret_cast<uint32_t *>(lastStack + sizeof(uintptr_t))));
    }

    // Restore the SP.
    newRegisters.setSP(lastStack);

    // The first instruction after return.
    uint32_t firstInstruction = *(reinterpret_cast<uint32_t *>(returnAddress));

    // Do we need to set the TOC register?
    _LIBUNWIND_TRACE_UNWINDING(
        "Current gpr2=%p",
        reinterpret_cast<void *>(newRegisters.getRegister(2)));
    if (firstInstruction == loadTOCRegInst) {
      _LIBUNWIND_TRACE_UNWINDING(
          "Set gpr2=%p from frame",
          reinterpret_cast<void *>(reinterpret_cast<pint_t *>(lastStack)[5]));
      newRegisters.setRegister(2, reinterpret_cast<pint_t *>(lastStack)[5]);
    }
  }
  _LIBUNWIND_TRACE_UNWINDING("lastStack=%p, returnAddress=%p, pc=%p\n",
                             reinterpret_cast<void *>(lastStack),
                             reinterpret_cast<void *>(returnAddress),
                             reinterpret_cast<void *>(pc));

  // The return address is the address after call site instruction, so
  // setting IP to that simulates a return.
  newRegisters.setIP(reinterpret_cast<uintptr_t>(returnAddress));

  // Simulate the step by replacing the register set with the new ones.
  registers = newRegisters;

  // Check if the next frame is a signal frame.
  pint_t nextStack = *(reinterpret_cast<pint_t *>(registers.getSP()));

  // Return address is the address after call site instruction.
  pint_t nextReturnAddress = reinterpret_cast<pint_t *>(nextStack)[2];

  if (nextReturnAddress > 0x01 && nextReturnAddress < 0x10000) {
    _LIBUNWIND_TRACE_UNWINDING("The next is a signal handler frame: "
                               "nextStack=%p, next return address=%p\n",
                               reinterpret_cast<void *>(nextStack),
                               reinterpret_cast<void *>(nextReturnAddress));
    isSignalFrame = true;
  } else {
    isSignalFrame = false;
  }
  return UNW_STEP_SUCCESS;
}
#endif // defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND)

template <typename A, typename R>
void UnwindCursor<A, R>::setInfoBasedOnIPRegister(bool isReturnAddress) {
#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN)
  _isSigReturn = false;
#endif

  pint_t pc = static_cast<pint_t>(this->getReg(UNW_REG_IP));
#if defined(_LIBUNWIND_ARM_EHABI)
  // Remove the thumb bit so the IP represents the actual instruction address.
  // This matches the behaviour of _Unwind_GetIP on arm.
  pc &= (pint_t)~0x1;
#endif

  // Exit early if at the top of the stack.
  if (pc == 0) {
    _unwindInfoMissing = true;
    return;
  }

  // If the last line of a function is a "throw" the compiler sometimes
  // emits no instructions after the call to __cxa_throw.  This means
  // the return address is actually the start of the next function.
  // To disambiguate this, back up the pc when we know it is a return
  // address.
  if (isReturnAddress)
#if defined(_AIX)
    // PC needs to be a 4-byte aligned address to be able to look for a
    // word of 0 that indicates the start of the traceback table at the end
    // of a function on AIX.
    pc -= 4;
#else
    --pc;
#endif

#if !(defined(_LIBUNWIND_SUPPORT_SEH_UNWIND) && defined(_WIN32)) &&            \
    !defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND)
  // In case of this is frame of signal handler, the IP saved in the signal
  // handler points to first non-executed instruction, while FDE/CIE expects IP
  // to be after the first non-executed instruction.
  if (_isSignalFrame)
    ++pc;
#endif

  // Ask address space object to find unwind sections for this pc.
  UnwindInfoSections sects;
  if (_addressSpace.findUnwindSections(pc, sects)) {
#if defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)
    // If there is a compact unwind encoding table, look there first.
    if (sects.compact_unwind_section != 0) {
      if (this->getInfoFromCompactEncodingSection(pc, sects)) {
  #if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
        // Found info in table, done unless encoding says to use dwarf.
        uint32_t dwarfOffset;
        if ((sects.dwarf_section != 0) && compactSaysUseDwarf(&dwarfOffset)) {
          if (this->getInfoFromDwarfSection(pc, sects, dwarfOffset)) {
            // found info in dwarf, done
            return;
          }
        }
  #endif
        // If unwind table has entry, but entry says there is no unwind info,
        // record that we have no unwind info.
        if (_info.format == 0)
          _unwindInfoMissing = true;
        return;
      }
    }
#endif // defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)

#if defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)
    // If there is SEH unwind info, look there next.
    if (this->getInfoFromSEH(pc))
      return;
#endif

#if defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND)
    // If there is unwind info in the traceback table, look there next.
    if (this->getInfoFromTBTable(pc, _registers))
      return;
#endif

#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
    // If there is dwarf unwind info, look there next.
    if (sects.dwarf_section != 0) {
      if (this->getInfoFromDwarfSection(pc, sects)) {
        // found info in dwarf, done
        return;
      }
    }
#endif

#if defined(_LIBUNWIND_ARM_EHABI)
    // If there is ARM EHABI unwind info, look there next.
    if (sects.arm_section != 0 && this->getInfoFromEHABISection(pc, sects))
      return;
#endif
  }

#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
  // There is no static unwind info for this pc. Look to see if an FDE was
  // dynamically registered for it.
  pint_t cachedFDE = DwarfFDECache<A>::findFDE(DwarfFDECache<A>::kSearchAll,
                                               pc);
  if (cachedFDE != 0) {
    typename CFI_Parser<A>::FDE_Info fdeInfo;
    typename CFI_Parser<A>::CIE_Info cieInfo;
    if (!CFI_Parser<A>::decodeFDE(_addressSpace, cachedFDE, &fdeInfo, &cieInfo))
      if (getInfoFromFdeCie(fdeInfo, cieInfo, pc, 0))
        return;
  }

  // Lastly, ask AddressSpace object about platform specific ways to locate
  // other FDEs.
  pint_t fde;
  if (_addressSpace.findOtherFDE(pc, fde)) {
    typename CFI_Parser<A>::FDE_Info fdeInfo;
    typename CFI_Parser<A>::CIE_Info cieInfo;
    if (!CFI_Parser<A>::decodeFDE(_addressSpace, fde, &fdeInfo, &cieInfo)) {
      // Double check this FDE is for a function that includes the pc.
      if ((fdeInfo.pcStart <= pc) && (pc < fdeInfo.pcEnd))
        if (getInfoFromFdeCie(fdeInfo, cieInfo, pc, 0))
          return;
    }
  }
#endif // #if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)

#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN)
  if (setInfoForSigReturn())
    return;
#endif

  // no unwind info, flag that we can't reliably unwind
  _unwindInfoMissing = true;
}

#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN) &&                               \
    defined(_LIBUNWIND_TARGET_AARCH64)
template <typename A, typename R>
bool UnwindCursor<A, R>::setInfoForSigReturn(Registers_arm64 &) {
  // Look for the sigreturn trampoline. The trampoline's body is two
  // specific instructions (see below). Typically the trampoline comes from the
  // vDSO[1] (i.e. the __kernel_rt_sigreturn function). A libc might provide its
  // own restorer function, though, or user-mode QEMU might write a trampoline
  // onto the stack.
  //
  // This special code path is a fallback that is only used if the trampoline
  // lacks proper (e.g. DWARF) unwind info. On AArch64, a new DWARF register
  // constant for the PC needs to be defined before DWARF can handle a signal
  // trampoline. This code may segfault if the target PC is unreadable, e.g.:
  //  - The PC points at a function compiled without unwind info, and which is
  //    part of an execute-only mapping (e.g. using -Wl,--execute-only).
  //  - The PC is invalid and happens to point to unreadable or unmapped memory.
  //
  // [1] https://github.com/torvalds/linux/blob/master/arch/arm64/kernel/vdso/sigreturn.S
  const pint_t pc = static_cast<pint_t>(this->getReg(UNW_REG_IP));
  // The PC might contain an invalid address if the unwind info is bad, so
  // directly accessing it could cause a SIGSEGV.
  if (!isReadableAddr(pc))
    return false;
  auto *instructions = reinterpret_cast<const uint32_t *>(pc);
  // Look for instructions: mov x8, #0x8b; svc #0x0
  if (instructions[0] != 0xd2801168 || instructions[1] != 0xd4000001)
    return false;

  _info = {};
  _info.start_ip = pc;
  _info.end_ip = pc + 4;
  _isSigReturn = true;
  return true;
}

template <typename A, typename R>
int UnwindCursor<A, R>::stepThroughSigReturn(Registers_arm64 &) {
  // In the signal trampoline frame, sp points to an rt_sigframe[1], which is:
  //  - 128-byte siginfo struct
  //  - ucontext struct:
  //     - 8-byte long (uc_flags)
  //     - 8-byte pointer (uc_link)
  //     - 24-byte stack_t
  //     - 128-byte signal set
  //     - 8 bytes of padding because sigcontext has 16-byte alignment
  //     - sigcontext/mcontext_t
  // [1] https://github.com/torvalds/linux/blob/master/arch/arm64/kernel/signal.c
  const pint_t kOffsetSpToSigcontext = (128 + 8 + 8 + 24 + 128 + 8); // 304

  // Offsets from sigcontext to each register.
  const pint_t kOffsetGprs = 8; // offset to "__u64 regs[31]" field
  const pint_t kOffsetSp = 256; // offset to "__u64 sp" field
  const pint_t kOffsetPc = 264; // offset to "__u64 pc" field

  pint_t sigctx = _registers.getSP() + kOffsetSpToSigcontext;

  for (int i = 0; i <= 30; ++i) {
    uint64_t value = _addressSpace.get64(sigctx + kOffsetGprs +
                                         static_cast<pint_t>(i * 8));
    _registers.setRegister(UNW_AARCH64_X0 + i, value);
  }
  _registers.setSP(_addressSpace.get64(sigctx + kOffsetSp));
  _registers.setIP(_addressSpace.get64(sigctx + kOffsetPc));
  _isSignalFrame = true;
  return UNW_STEP_SUCCESS;
}
#endif // defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN) &&
       // defined(_LIBUNWIND_TARGET_AARCH64)

#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN) &&                               \
    defined(_LIBUNWIND_TARGET_RISCV)
template <typename A, typename R>
bool UnwindCursor<A, R>::setInfoForSigReturn(Registers_riscv &) {
  const pint_t pc = static_cast<pint_t>(getReg(UNW_REG_IP));
  // The PC might contain an invalid address if the unwind info is bad, so
  // directly accessing it could cause a SIGSEGV.
  if (!isReadableAddr(pc))
    return false;
  const auto *instructions = reinterpret_cast<const uint32_t *>(pc);
  // Look for the two instructions used in the sigreturn trampoline
  // __vdso_rt_sigreturn:
  //
  // 0x08b00893 li a7,0x8b
  // 0x00000073 ecall
  if (instructions[0] != 0x08b00893 || instructions[1] != 0x00000073)
    return false;

  _info = {};
  _info.start_ip = pc;
  _info.end_ip = pc + 4;
  _isSigReturn = true;
  return true;
}

template <typename A, typename R>
int UnwindCursor<A, R>::stepThroughSigReturn(Registers_riscv &) {
  // In the signal trampoline frame, sp points to an rt_sigframe[1], which is:
  //  - 128-byte siginfo struct
  //  - ucontext_t struct:
  //     - 8-byte long (__uc_flags)
  //     - 8-byte pointer (*uc_link)
  //     - 24-byte uc_stack
  //     - 8-byte uc_sigmask
  //     - 120-byte of padding to allow sigset_t to be expanded in the future
  //     - 8 bytes of padding because sigcontext has 16-byte alignment
  //     - struct sigcontext uc_mcontext
  // [1]
  // https://github.com/torvalds/linux/blob/master/arch/riscv/kernel/signal.c
  const pint_t kOffsetSpToSigcontext = 128 + 8 + 8 + 24 + 8 + 128;

  const pint_t sigctx = _registers.getSP() + kOffsetSpToSigcontext;
  _registers.setIP(_addressSpace.get64(sigctx));
  for (int i = UNW_RISCV_X1; i <= UNW_RISCV_X31; ++i) {
    uint64_t value = _addressSpace.get64(sigctx + static_cast<pint_t>(i * 8));
    _registers.setRegister(i, value);
  }
  _isSignalFrame = true;
  return UNW_STEP_SUCCESS;
}
#endif // defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN) &&
       // defined(_LIBUNWIND_TARGET_RISCV)

#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN) &&                               \
    defined(_LIBUNWIND_TARGET_S390X)
template <typename A, typename R>
bool UnwindCursor<A, R>::setInfoForSigReturn(Registers_s390x &) {
  // Look for the sigreturn trampoline. The trampoline's body is a
  // specific instruction (see below). Typically the trampoline comes from the
  // vDSO (i.e. the __kernel_[rt_]sigreturn function). A libc might provide its
  // own restorer function, though, or user-mode QEMU might write a trampoline
  // onto the stack.
  const pint_t pc = static_cast<pint_t>(this->getReg(UNW_REG_IP));
  // The PC might contain an invalid address if the unwind info is bad, so
  // directly accessing it could cause a SIGSEGV.
  if (!isReadableAddr(pc))
    return false;
  const auto inst = *reinterpret_cast<const uint16_t *>(pc);
  if (inst == 0x0a77 || inst == 0x0aad) {
    _info = {};
    _info.start_ip = pc;
    _info.end_ip = pc + 2;
    _isSigReturn = true;
    return true;
  }
  return false;
}

template <typename A, typename R>
int UnwindCursor<A, R>::stepThroughSigReturn(Registers_s390x &) {
  // Determine current SP.
  const pint_t sp = static_cast<pint_t>(this->getReg(UNW_REG_SP));
  // According to the s390x ABI, the CFA is at (incoming) SP + 160.
  const pint_t cfa = sp + 160;

  // Determine current PC and instruction there (this must be either
  // a "svc __NR_sigreturn" or "svc __NR_rt_sigreturn").
  const pint_t pc = static_cast<pint_t>(this->getReg(UNW_REG_IP));
  const uint16_t inst = _addressSpace.get16(pc);

  // Find the addresses of the signo and sigcontext in the frame.
  pint_t pSigctx = 0;
  pint_t pSigno = 0;

  // "svc __NR_sigreturn" uses a non-RT signal trampoline frame.
  if (inst == 0x0a77) {
    // Layout of a non-RT signal trampoline frame, starting at the CFA:
    //  - 8-byte signal mask
    //  - 8-byte pointer to sigcontext, followed by signo
    //  - 4-byte signo
    pSigctx = _addressSpace.get64(cfa + 8);
    pSigno = pSigctx + 344;
  }

  // "svc __NR_rt_sigreturn" uses a RT signal trampoline frame.
  if (inst == 0x0aad) {
    // Layout of a RT signal trampoline frame, starting at the CFA:
    //  - 8-byte retcode (+ alignment)
    //  - 128-byte siginfo struct (starts with signo)
    //  - ucontext struct:
    //     - 8-byte long (uc_flags)
    //     - 8-byte pointer (uc_link)
    //     - 24-byte stack_t
    //     - 8 bytes of padding because sigcontext has 16-byte alignment
    //     - sigcontext/mcontext_t
    pSigctx = cfa + 8 + 128 + 8 + 8 + 24 + 8;
    pSigno = cfa + 8;
  }

  assert(pSigctx != 0);
  assert(pSigno != 0);

  // Offsets from sigcontext to each register.
  const pint_t kOffsetPc = 8;
  const pint_t kOffsetGprs = 16;
  const pint_t kOffsetFprs = 216;

  // Restore all registers.
  for (int i = 0; i < 16; ++i) {
    uint64_t value = _addressSpace.get64(pSigctx + kOffsetGprs +
                                         static_cast<pint_t>(i * 8));
    _registers.setRegister(UNW_S390X_R0 + i, value);
  }
  for (int i = 0; i < 16; ++i) {
    static const int fpr[16] = {
      UNW_S390X_F0, UNW_S390X_F1, UNW_S390X_F2, UNW_S390X_F3,
      UNW_S390X_F4, UNW_S390X_F5, UNW_S390X_F6, UNW_S390X_F7,
      UNW_S390X_F8, UNW_S390X_F9, UNW_S390X_F10, UNW_S390X_F11,
      UNW_S390X_F12, UNW_S390X_F13, UNW_S390X_F14, UNW_S390X_F15
    };
    double value = _addressSpace.getDouble(pSigctx + kOffsetFprs +
                                           static_cast<pint_t>(i * 8));
    _registers.setFloatRegister(fpr[i], value);
  }
  _registers.setIP(_addressSpace.get64(pSigctx + kOffsetPc));

  // SIGILL, SIGFPE and SIGTRAP are delivered with psw_addr
  // after the faulting instruction rather than before it.
  // Do not set _isSignalFrame in that case.
  uint32_t signo = _addressSpace.get32(pSigno);
  _isSignalFrame = (signo != 4 && signo != 5 && signo != 8);

  return UNW_STEP_SUCCESS;
}
#endif // defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN) &&
       // defined(_LIBUNWIND_TARGET_S390X)

template <typename A, typename R> int UnwindCursor<A, R>::step(bool stage2) {
  (void)stage2;
  // Bottom of stack is defined is when unwind info cannot be found.
  if (_unwindInfoMissing)
    return UNW_STEP_END;

  // Use unwinding info to modify register set as if function returned.
  int result;
#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN)
  if (_isSigReturn) {
    result = this->stepThroughSigReturn();
  } else
#endif
  {
#if defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND)
    result = this->stepWithCompactEncoding(stage2);
#elif defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)
    result = this->stepWithSEHData();
#elif defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND)
    result = this->stepWithTBTableData();
#elif defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
    result = this->stepWithDwarfFDE(stage2);
#elif defined(_LIBUNWIND_ARM_EHABI)
    result = this->stepWithEHABI();
#else
  #error Need _LIBUNWIND_SUPPORT_COMPACT_UNWIND or \
              _LIBUNWIND_SUPPORT_SEH_UNWIND or \
              _LIBUNWIND_SUPPORT_DWARF_UNWIND or \
              _LIBUNWIND_ARM_EHABI
#endif
  }

  // update info based on new PC
  if (result == UNW_STEP_SUCCESS) {
    this->setInfoBasedOnIPRegister(true);
    if (_unwindInfoMissing)
      return UNW_STEP_END;
  }

  return result;
}

template <typename A, typename R>
void UnwindCursor<A, R>::getInfo(unw_proc_info_t *info) {
  if (_unwindInfoMissing)
    memset(info, 0, sizeof(*info));
  else
    *info = _info;
}

template <typename A, typename R>
bool UnwindCursor<A, R>::getFunctionName(char *buf, size_t bufLen,
                                                           unw_word_t *offset) {
  return _addressSpace.findFunctionName((pint_t)this->getReg(UNW_REG_IP),
                                         buf, bufLen, offset);
}

#if defined(_LIBUNWIND_CHECK_LINUX_SIGRETURN)
template <typename A, typename R>
bool UnwindCursor<A, R>::isReadableAddr(const pint_t addr) const {
  // We use SYS_rt_sigprocmask, inspired by Abseil's AddressIsReadable.

  const auto sigsetAddr = reinterpret_cast<sigset_t *>(addr);
  // We have to check that addr is nullptr because sigprocmask allows that
  // as an argument without failure.
  if (!sigsetAddr)
    return false;
  const auto saveErrno = errno;
  // We MUST use a raw syscall here, as wrappers may try to access
  // sigsetAddr which may cause a SIGSEGV. A raw syscall however is
  // safe. Additionally, we need to pass the kernel_sigset_size, which is
  // different from libc sizeof(sigset_t). For the majority of architectures,
  // it's 64 bits (_NSIG), and libc NSIG is _NSIG + 1.
  const auto kernelSigsetSize = NSIG / 8;
  [[maybe_unused]] const int Result = syscall(
      SYS_rt_sigprocmask, /*how=*/~0, sigsetAddr, nullptr, kernelSigsetSize);
  // Because our "how" is invalid, this syscall should always fail, and our
  // errno should always be EINVAL or an EFAULT. This relies on the Linux
  // kernel to check copy_from_user before checking if the "how" argument is
  // invalid.
  assert(Result == -1);
  assert(errno == EFAULT || errno == EINVAL);
  const auto readable = errno != EFAULT;
  errno = saveErrno;
  return readable;
}
#endif

#if defined(_LIBUNWIND_USE_CET) || defined(_LIBUNWIND_USE_GCS)
extern "C" void *__libunwind_cet_get_registers(unw_cursor_t *cursor) {
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  return co->get_registers();
}
#endif
} // namespace libunwind

#endif // __UNWINDCURSOR_HPP__
