/* ===-------- intrin.h ---------------------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

/* Only include this if we're compiling for the windows platform. */
#ifndef _MSC_VER
#include_next <intrin.h>
#else

#ifndef __INTRIN_H
#define __INTRIN_H

#include <intrin0.h>

/* First include the standard intrinsics. */
#if defined(__i386__) || (defined(__x86_64__) && !defined(__arm64ec__))
#include <x86intrin.h>
#endif

#if defined(__arm__)
#include <armintr.h>
#endif

#if defined(__aarch64__) || defined(__arm64ec__)
#include <arm64intr.h>
#endif

/* For the definition of jmp_buf. */
#if __STDC_HOSTED__
#include <setjmp.h>
#endif

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__))

#if __x86_64__
#define __LPTRINT_TYPE__ __int64
#else
#define __LPTRINT_TYPE__ long
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__MMX__)
/* And the random ones that aren't in those files. */
__m64 _m_from_float(float);
float _m_to_float(__m64);
#endif

/* Other assorted instruction intrinsics. */
void __addfsbyte(unsigned long, unsigned char);
void __addfsdword(unsigned long, unsigned long);
void __addfsword(unsigned long, unsigned short);
void __code_seg(const char *);
void __cpuid(int[4], int);
void __cpuidex(int[4], int, int);
__int64 __emul(int, int);
unsigned __int64 __emulu(unsigned int, unsigned int);
unsigned int __getcallerseflags(void);
void __halt(void);
unsigned char __inbyte(unsigned short);
void __inbytestring(unsigned short, unsigned char *, unsigned long);
void __incfsbyte(unsigned long);
void __incfsdword(unsigned long);
void __incfsword(unsigned long);
unsigned long __indword(unsigned short);
void __indwordstring(unsigned short, unsigned long *, unsigned long);
void __int2c(void);
void __invlpg(void *);
unsigned short __inword(unsigned short);
void __inwordstring(unsigned short, unsigned short *, unsigned long);
void __lidt(void *);
unsigned __int64 __ll_lshift(unsigned __int64, int);
__int64 __ll_rshift(__int64, int);
void __movsb(unsigned char *, unsigned char const *, size_t);
void __movsd(unsigned long *, unsigned long const *, size_t);
void __movsw(unsigned short *, unsigned short const *, size_t);
void __nop(void);
void __nvreg_restore_fence(void);
void __nvreg_save_fence(void);
void __outbyte(unsigned short, unsigned char);
void __outbytestring(unsigned short, unsigned char *, unsigned long);
void __outdword(unsigned short, unsigned long);
void __outdwordstring(unsigned short, unsigned long *, unsigned long);
void __outword(unsigned short, unsigned short);
void __outwordstring(unsigned short, unsigned short *, unsigned long);
unsigned long __readcr0(void);
unsigned long __readcr2(void);
unsigned __LPTRINT_TYPE__ __readcr3(void);
unsigned long __readcr4(void);
unsigned long __readcr8(void);
unsigned int __readdr(unsigned int);
#ifdef __i386__
unsigned char __readfsbyte(unsigned long);
unsigned short __readfsword(unsigned long);
unsigned long __readfsdword(unsigned long);
unsigned __int64 __readfsqword(unsigned long);
#endif
unsigned __int64 __readmsr(unsigned long);
unsigned __int64 __readpmc(unsigned long);
unsigned long __segmentlimit(unsigned long);
void __sidt(void *);
void __stosb(unsigned char *, unsigned char, size_t);
void __stosd(unsigned long *, unsigned long, size_t);
void __stosw(unsigned short *, unsigned short, size_t);
void __svm_clgi(void);
void __svm_invlpga(void *, int);
void __svm_skinit(int);
void __svm_stgi(void);
void __svm_vmload(size_t);
void __svm_vmrun(size_t);
void __svm_vmsave(size_t);
void __ud2(void);
unsigned __int64 __ull_rshift(unsigned __int64, int);
void __vmx_off(void);
void __vmx_vmptrst(unsigned __int64 *);
void __wbinvd(void);
void __writecr0(unsigned int);
void __writecr3(unsigned __INTPTR_TYPE__);
void __writecr4(unsigned int);
void __writecr8(unsigned int);
void __writedr(unsigned int, unsigned int);
void __writefsbyte(unsigned long, unsigned char);
void __writefsdword(unsigned long, unsigned long);
void __writefsqword(unsigned long, unsigned __int64);
void __writefsword(unsigned long, unsigned short);
void __writemsr(unsigned long, unsigned __int64);
void *_AddressOfReturnAddress(void);
unsigned char _bittest(long const *, long);
unsigned char _bittestandcomplement(long *, long);
unsigned char _bittestandreset(long *, long);
unsigned char _bittestandset(long *, long);
void __cdecl _disable(void);
void __cdecl _enable(void);
long _InterlockedAddLargeStatistic(__int64 volatile *_Addend, long _Value);
unsigned char _interlockedbittestandreset(long volatile *, long);
unsigned char _interlockedbittestandset(long volatile *, long);
void *_InterlockedCompareExchangePointer_HLEAcquire(void *volatile *, void *,
                                                    void *);
void *_InterlockedCompareExchangePointer_HLERelease(void *volatile *, void *,
                                                    void *);
long _InterlockedExchangeAdd_HLEAcquire(long volatile *, long);
long _InterlockedExchangeAdd_HLERelease(long volatile *, long);
__int64 _InterlockedExchangeAdd64_HLEAcquire(__int64 volatile *, __int64);
__int64 _InterlockedExchangeAdd64_HLERelease(__int64 volatile *, __int64);
void _ReadBarrier(void);
unsigned int _rorx_u32(unsigned int, const unsigned int);
int _sarx_i32(int, unsigned int);
#if __STDC_HOSTED__
int __cdecl _setjmp(jmp_buf);
#endif
unsigned int _shlx_u32(unsigned int, unsigned int);
unsigned int _shrx_u32(unsigned int, unsigned int);
void _Store_HLERelease(long volatile *, long);
void _Store64_HLERelease(__int64 volatile *, __int64);
void _StorePointer_HLERelease(void *volatile *, void *);
void _WriteBarrier(void);
unsigned __int32 xbegin(void);
void _xend(void);

/* These additional intrinsics are turned on in x64/amd64/x86_64 mode. */
#if defined(__x86_64__) && !defined(__arm64ec__)
void __addgsbyte(unsigned long, unsigned char);
void __addgsdword(unsigned long, unsigned long);
void __addgsqword(unsigned long, unsigned __int64);
void __addgsword(unsigned long, unsigned short);
void __faststorefence(void);
void __incgsbyte(unsigned long);
void __incgsdword(unsigned long);
void __incgsqword(unsigned long);
void __incgsword(unsigned long);
void __movsq(unsigned long long *, unsigned long long const *, size_t);
unsigned char __readgsbyte(unsigned long);
unsigned long __readgsdword(unsigned long);
unsigned __int64 __readgsqword(unsigned long);
unsigned short __readgsword(unsigned long);
void __stosq(unsigned __int64 *, unsigned __int64, size_t);
unsigned char __vmx_on(unsigned __int64 *);
unsigned char __vmx_vmclear(unsigned __int64 *);
unsigned char __vmx_vmlaunch(void);
unsigned char __vmx_vmptrld(unsigned __int64 *);
unsigned char __vmx_vmread(size_t, size_t *);
unsigned char __vmx_vmresume(void);
unsigned char __vmx_vmwrite(size_t, size_t);
void __writegsbyte(unsigned long, unsigned char);
void __writegsdword(unsigned long, unsigned long);
void __writegsqword(unsigned long, unsigned __int64);
void __writegsword(unsigned long, unsigned short);
unsigned char _bittest64(__int64 const *, __int64);
unsigned char _bittestandcomplement64(__int64 *, __int64);
unsigned char _bittestandreset64(__int64 *, __int64);
unsigned char _bittestandset64(__int64 *, __int64);
long _InterlockedAnd_np(long volatile *_Value, long _Mask);
short _InterlockedAnd16_np(short volatile *_Value, short _Mask);
__int64 _InterlockedAnd64_np(__int64 volatile *_Value, __int64 _Mask);
char _InterlockedAnd8_np(char volatile *_Value, char _Mask);
unsigned char _interlockedbittestandreset64(__int64 volatile *, __int64);
unsigned char _interlockedbittestandset64(__int64 volatile *, __int64);
long _InterlockedCompareExchange_np(long volatile *_Destination, long _Exchange,
                                    long _Comparand);
unsigned char _InterlockedCompareExchange128_np(__int64 volatile *_Destination,
                                                __int64 _ExchangeHigh,
                                                __int64 _ExchangeLow,
                                                __int64 *_ComparandResult);
short _InterlockedCompareExchange16_np(short volatile *_Destination,
                                       short _Exchange, short _Comparand);
__int64 _InterlockedCompareExchange64_np(__int64 volatile *_Destination,
                                         __int64 _Exchange, __int64 _Comparand);
void *_InterlockedCompareExchangePointer_np(void *volatile *_Destination,
                                            void *_Exchange, void *_Comparand);
long _InterlockedOr_np(long volatile *_Value, long _Mask);
short _InterlockedOr16_np(short volatile *_Value, short _Mask);
__int64 _InterlockedOr64_np(__int64 volatile *_Value, __int64 _Mask);
char _InterlockedOr8_np(char volatile *_Value, char _Mask);
long _InterlockedXor_np(long volatile *_Value, long _Mask);
short _InterlockedXor16_np(short volatile *_Value, short _Mask);
__int64 _InterlockedXor64_np(__int64 volatile *_Value, __int64 _Mask);
char _InterlockedXor8_np(char volatile *_Value, char _Mask);
unsigned __int64 _rorx_u64(unsigned __int64, const unsigned int);
__int64 _sarx_i64(__int64, unsigned int);
unsigned __int64 _shlx_u64(unsigned __int64, unsigned int);
unsigned __int64 _shrx_u64(unsigned __int64, unsigned int);
__int64 __mulh(__int64, __int64);
unsigned __int64 __umulh(unsigned __int64, unsigned __int64);
__int64 _mul128(__int64, __int64, __int64 *);

#endif /* __x86_64__ */

/*----------------------------------------------------------------------------*\
|* movs, stos
\*----------------------------------------------------------------------------*/

#if defined(__i386__) || (defined(__x86_64__) && !defined(__arm64ec__))
static __inline__ void __DEFAULT_FN_ATTRS __movsb(unsigned char *__dst,
                                                  unsigned char const *__src,
                                                  size_t __n) {
#if defined(__x86_64__)
  __asm__ __volatile__("rep movsb"
                       : "+D"(__dst), "+S"(__src), "+c"(__n)
                       :
                       : "memory");
#else
  __asm__ __volatile__("xchg {%%esi, %1|%1, esi}\n"
                       "rep movsb\n"
                       "xchg {%%esi, %1|%1, esi}"
                       : "+D"(__dst), "+r"(__src), "+c"(__n)
                       :
                       : "memory");
#endif
}
static __inline__ void __DEFAULT_FN_ATTRS __movsd(unsigned long *__dst,
                                                  unsigned long const *__src,
                                                  size_t __n) {
#if defined(__x86_64__)
  __asm__ __volatile__("rep movs{l|d}"
                       : "+D"(__dst), "+S"(__src), "+c"(__n)
                       :
                       : "memory");
#else
  __asm__ __volatile__("xchg {%%esi, %1|%1, esi}\n"
                       "rep movs{l|d}\n"
                       "xchg {%%esi, %1|%1, esi}"
                       : "+D"(__dst), "+r"(__src), "+c"(__n)
                       :
                       : "memory");
#endif
}
static __inline__ void __DEFAULT_FN_ATTRS __movsw(unsigned short *__dst,
                                                  unsigned short const *__src,
                                                  size_t __n) {
#if defined(__x86_64__)
  __asm__ __volatile__("rep movsw"
                       : "+D"(__dst), "+S"(__src), "+c"(__n)
                       :
                       : "memory");
#else
  __asm__ __volatile__("xchg {%%esi, %1|%1, esi}\n"
                       "rep movsw\n"
                       "xchg {%%esi, %1|%1, esi}"
                       : "+D"(__dst), "+r"(__src), "+c"(__n)
                       :
                       : "memory");
#endif
}
static __inline__ void __DEFAULT_FN_ATTRS __stosd(unsigned long *__dst,
                                                  unsigned long __x,
                                                  size_t __n) {
  __asm__ __volatile__("rep stos{l|d}"
                       : "+D"(__dst), "+c"(__n)
                       : "a"(__x)
                       : "memory");
}
static __inline__ void __DEFAULT_FN_ATTRS __stosw(unsigned short *__dst,
                                                  unsigned short __x,
                                                  size_t __n) {
  __asm__ __volatile__("rep stosw"
                       : "+D"(__dst), "+c"(__n)
                       : "a"(__x)
                       : "memory");
}
#endif
#if defined(__x86_64__) && !defined(__arm64ec__)
static __inline__ void __DEFAULT_FN_ATTRS __movsq(
    unsigned long long *__dst, unsigned long long const *__src, size_t __n) {
  __asm__ __volatile__("rep movsq"
                       : "+D"(__dst), "+S"(__src), "+c"(__n)
                       :
                       : "memory");
}
static __inline__ void __DEFAULT_FN_ATTRS __stosq(unsigned __int64 *__dst,
                                                  unsigned __int64 __x,
                                                  size_t __n) {
  __asm__ __volatile__("rep stosq" : "+D"(__dst), "+c"(__n) : "a"(__x)
                       : "memory");
}
#endif

/*----------------------------------------------------------------------------*\
|* Misc
\*----------------------------------------------------------------------------*/
#if defined(__i386__) || (defined(__x86_64__) && !defined(__arm64ec__))
static __inline__ void __DEFAULT_FN_ATTRS __halt(void) {
  __asm__ volatile("hlt");
}

static inline unsigned char __inbyte(unsigned short port) {
  unsigned char ret;
  __asm__ __volatile__("inb %w1, %b0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline unsigned short __inword(unsigned short port) {
  unsigned short ret;
  __asm__ __volatile__("inw %w1, %w0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline unsigned long __indword(unsigned short port) {
  unsigned long ret;
  __asm__ __volatile__("inl %w1, %k0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline void __outbyte(unsigned short port, unsigned char data) {
  __asm__ __volatile__("outb %b0, %w1" : : "a"(data), "Nd"(port));
}

static inline void __outword(unsigned short port, unsigned short data) {
  __asm__ __volatile__("outw %w0, %w1" : : "a"(data), "Nd"(port));
}

static inline void __outdword(unsigned short port, unsigned long data) {
  __asm__ __volatile__("outl %k0, %w1" : : "a"(data), "Nd"(port));
}
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
static __inline__ void __DEFAULT_FN_ATTRS __nop(void) {
  __asm__ volatile("nop");
}
#endif

/*----------------------------------------------------------------------------*\
|* MS AArch64 specific
\*----------------------------------------------------------------------------*/
#if defined(__aarch64__) || defined(__arm64ec__)
unsigned __int64 __getReg(int);
long _InterlockedAdd(long volatile *Addend, long Value);
__int64 _InterlockedAdd64(__int64 volatile *Addend, __int64 Value);
__int64 _ReadStatusReg(int);
void _WriteStatusReg(int, __int64);

unsigned short __cdecl _byteswap_ushort(unsigned short val);
unsigned long __cdecl _byteswap_ulong (unsigned long val);
unsigned __int64 __cdecl _byteswap_uint64(unsigned __int64 val);

__int64 __mulh(__int64 __a, __int64 __b);
unsigned __int64 __umulh(unsigned __int64 __a, unsigned __int64 __b);

void __break(int);

void __writex18byte(unsigned long offset, unsigned char data);
void __writex18word(unsigned long offset, unsigned short data);
void __writex18dword(unsigned long offset, unsigned long data);
void __writex18qword(unsigned long offset, unsigned __int64 data);

unsigned char __readx18byte(unsigned long offset);
unsigned short __readx18word(unsigned long offset);
unsigned long __readx18dword(unsigned long offset);
unsigned __int64 __readx18qword(unsigned long offset);

double _CopyDoubleFromInt64(__int64);
float _CopyFloatFromInt32(__int32);
__int32 _CopyInt32FromFloat(float);
__int64 _CopyInt64FromDouble(double);

unsigned int _CountLeadingOnes(unsigned long);
unsigned int _CountLeadingOnes64(unsigned __int64);
unsigned int _CountLeadingSigns(long);
unsigned int _CountLeadingSigns64(__int64);
unsigned int _CountOneBits(unsigned long);
unsigned int _CountOneBits64(unsigned __int64);

unsigned int __hlt(unsigned int, ...);

void __cdecl __prefetch(const void *);

#endif

/*----------------------------------------------------------------------------*\
|* Privileged intrinsics
\*----------------------------------------------------------------------------*/
#if defined(__i386__) || (defined(__x86_64__) && !defined(__arm64ec__))
static __inline__ unsigned __int64 __DEFAULT_FN_ATTRS
__readmsr(unsigned long __register) {
  // Loads the contents of a 64-bit model specific register (MSR) specified in
  // the ECX register into registers EDX:EAX. The EDX register is loaded with
  // the high-order 32 bits of the MSR and the EAX register is loaded with the
  // low-order 32 bits. If less than 64 bits are implemented in the MSR being
  // read, the values returned to EDX:EAX in unimplemented bit locations are
  // undefined.
  unsigned long __edx;
  unsigned long __eax;
  __asm__ ("rdmsr" : "=d"(__edx), "=a"(__eax) : "c"(__register));
  return (((unsigned __int64)__edx) << 32) | (unsigned __int64)__eax;
}

static __inline__ unsigned __LPTRINT_TYPE__ __DEFAULT_FN_ATTRS __readcr3(void) {
  unsigned __LPTRINT_TYPE__ __cr3_val;
  __asm__ __volatile__(
                       "mov {%%cr3, %0|%0, cr3}"
                       : "=r"(__cr3_val)
                       :
                       : "memory");
  return __cr3_val;
}

static __inline__ void __DEFAULT_FN_ATTRS
__writecr3(unsigned __INTPTR_TYPE__ __cr3_val) {
  __asm__ ("mov {%0, %%cr3|cr3, %0}" : : "r"(__cr3_val) : "memory");
}
#endif

#ifdef __cplusplus
}
#endif

#undef __LPTRINT_TYPE__

#undef __DEFAULT_FN_ATTRS

#endif /* __INTRIN_H */
#endif /* _MSC_VER */
