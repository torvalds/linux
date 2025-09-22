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
#include_next <intrin0.h>
#else

#ifndef __INTRIN0_H
#define __INTRIN0_H

#if defined(__x86_64__) && !defined(__arm64ec__)
#include <adcintrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

unsigned char _BitScanForward(unsigned long *_Index, unsigned long _Mask);
unsigned char _BitScanReverse(unsigned long *_Index, unsigned long _Mask);
void _ReadWriteBarrier(void);

#if defined(__aarch64__) || defined(__arm64ec__)
unsigned int _CountLeadingZeros(unsigned long);
unsigned int _CountLeadingZeros64(unsigned _int64);
unsigned char _InterlockedCompareExchange128_acq(__int64 volatile *_Destination,
                                                 __int64 _ExchangeHigh,
                                                 __int64 _ExchangeLow,
                                                 __int64 *_ComparandResult);
unsigned char _InterlockedCompareExchange128_nf(__int64 volatile *_Destination,
                                                __int64 _ExchangeHigh,
                                                __int64 _ExchangeLow,
                                                __int64 *_ComparandResult);
unsigned char _InterlockedCompareExchange128_rel(__int64 volatile *_Destination,
                                                 __int64 _ExchangeHigh,
                                                 __int64 _ExchangeLow,
                                                 __int64 *_ComparandResult);
#endif

#if defined(__x86_64__) && !defined(__arm64ec__)
unsigned __int64 _umul128(unsigned __int64, unsigned __int64,
                          unsigned __int64 *);
unsigned __int64 __shiftleft128(unsigned __int64 _LowPart,
                                unsigned __int64 _HighPart,
                                unsigned char _Shift);
unsigned __int64 __shiftright128(unsigned __int64 _LowPart,
                                 unsigned __int64 _HighPart,
                                 unsigned char _Shift);
#endif

#if defined(__i386__) || (defined(__x86_64__) && !defined(__arm64ec__))
void _mm_pause(void);
#endif

#if defined(__x86_64__) || defined(__aarch64__)
unsigned char _InterlockedCompareExchange128(__int64 volatile *_Destination,
                                             __int64 _ExchangeHigh,
                                             __int64 _ExchangeLow,
                                             __int64 *_ComparandResult);
#endif

#if defined(__x86_64__) || defined(__arm__) || defined(__aarch64__)
unsigned char _BitScanForward64(unsigned long *_Index, unsigned __int64 _Mask);
unsigned char _BitScanReverse64(unsigned long *_Index, unsigned __int64 _Mask);
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__arm__) ||            \
    defined(__aarch64__)
__int64 _InterlockedDecrement64(__int64 volatile *_Addend);
__int64 _InterlockedExchange64(__int64 volatile *_Target, __int64 _Value);
__int64 _InterlockedExchangeAdd64(__int64 volatile *_Addend, __int64 _Value);
__int64 _InterlockedExchangeSub64(__int64 volatile *_Subend, __int64 _Value);
__int64 _InterlockedIncrement64(__int64 volatile *_Addend);
__int64 _InterlockedOr64(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedXor64(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedAnd64(__int64 volatile *_Value, __int64 _Mask);
#endif

#if defined(__arm__) || defined(__aarch64__) || defined(__arm64ec__)
/*----------------------------------------------------------------------------*\
|* Interlocked Exchange Add
\*----------------------------------------------------------------------------*/
char _InterlockedExchangeAdd8_acq(char volatile *_Addend, char _Value);
char _InterlockedExchangeAdd8_nf(char volatile *_Addend, char _Value);
char _InterlockedExchangeAdd8_rel(char volatile *_Addend, char _Value);
short _InterlockedExchangeAdd16_acq(short volatile *_Addend, short _Value);
short _InterlockedExchangeAdd16_nf(short volatile *_Addend, short _Value);
short _InterlockedExchangeAdd16_rel(short volatile *_Addend, short _Value);
long _InterlockedExchangeAdd_acq(long volatile *_Addend, long _Value);
long _InterlockedExchangeAdd_nf(long volatile *_Addend, long _Value);
long _InterlockedExchangeAdd_rel(long volatile *_Addend, long _Value);
__int64 _InterlockedExchangeAdd64_acq(__int64 volatile *_Addend,
                                      __int64 _Value);
__int64 _InterlockedExchangeAdd64_nf(__int64 volatile *_Addend, __int64 _Value);
__int64 _InterlockedExchangeAdd64_rel(__int64 volatile *_Addend,
                                      __int64 _Value);

/*----------------------------------------------------------------------------*\
|* Interlocked Increment
\*----------------------------------------------------------------------------*/
short _InterlockedIncrement16_acq(short volatile *_Value);
short _InterlockedIncrement16_nf(short volatile *_Value);
short _InterlockedIncrement16_rel(short volatile *_Value);
long _InterlockedIncrement_acq(long volatile *_Value);
long _InterlockedIncrement_nf(long volatile *_Value);
long _InterlockedIncrement_rel(long volatile *_Value);
__int64 _InterlockedIncrement64_acq(__int64 volatile *_Value);
__int64 _InterlockedIncrement64_nf(__int64 volatile *_Value);
__int64 _InterlockedIncrement64_rel(__int64 volatile *_Value);

/*----------------------------------------------------------------------------*\
|* Interlocked Decrement
\*----------------------------------------------------------------------------*/
short _InterlockedDecrement16_acq(short volatile *_Value);
short _InterlockedDecrement16_nf(short volatile *_Value);
short _InterlockedDecrement16_rel(short volatile *_Value);
long _InterlockedDecrement_acq(long volatile *_Value);
long _InterlockedDecrement_nf(long volatile *_Value);
long _InterlockedDecrement_rel(long volatile *_Value);
__int64 _InterlockedDecrement64_acq(__int64 volatile *_Value);
__int64 _InterlockedDecrement64_nf(__int64 volatile *_Value);
__int64 _InterlockedDecrement64_rel(__int64 volatile *_Value);

/*----------------------------------------------------------------------------*\
|* Interlocked And
\*----------------------------------------------------------------------------*/
char _InterlockedAnd8_acq(char volatile *_Value, char _Mask);
char _InterlockedAnd8_nf(char volatile *_Value, char _Mask);
char _InterlockedAnd8_rel(char volatile *_Value, char _Mask);
short _InterlockedAnd16_acq(short volatile *_Value, short _Mask);
short _InterlockedAnd16_nf(short volatile *_Value, short _Mask);
short _InterlockedAnd16_rel(short volatile *_Value, short _Mask);
long _InterlockedAnd_acq(long volatile *_Value, long _Mask);
long _InterlockedAnd_nf(long volatile *_Value, long _Mask);
long _InterlockedAnd_rel(long volatile *_Value, long _Mask);
__int64 _InterlockedAnd64_acq(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedAnd64_nf(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedAnd64_rel(__int64 volatile *_Value, __int64 _Mask);

/*----------------------------------------------------------------------------*\
|* Bit Counting and Testing
\*----------------------------------------------------------------------------*/
unsigned char _interlockedbittestandset_acq(long volatile *_BitBase,
                                            long _BitPos);
unsigned char _interlockedbittestandset_nf(long volatile *_BitBase,
                                           long _BitPos);
unsigned char _interlockedbittestandset_rel(long volatile *_BitBase,
                                            long _BitPos);
unsigned char _interlockedbittestandreset_acq(long volatile *_BitBase,
                                              long _BitPos);
unsigned char _interlockedbittestandreset_nf(long volatile *_BitBase,
                                             long _BitPos);
unsigned char _interlockedbittestandreset_rel(long volatile *_BitBase,
                                              long _BitPos);

/*----------------------------------------------------------------------------*\
|* Interlocked Or
\*----------------------------------------------------------------------------*/
char _InterlockedOr8_acq(char volatile *_Value, char _Mask);
char _InterlockedOr8_nf(char volatile *_Value, char _Mask);
char _InterlockedOr8_rel(char volatile *_Value, char _Mask);
short _InterlockedOr16_acq(short volatile *_Value, short _Mask);
short _InterlockedOr16_nf(short volatile *_Value, short _Mask);
short _InterlockedOr16_rel(short volatile *_Value, short _Mask);
long _InterlockedOr_acq(long volatile *_Value, long _Mask);
long _InterlockedOr_nf(long volatile *_Value, long _Mask);
long _InterlockedOr_rel(long volatile *_Value, long _Mask);
__int64 _InterlockedOr64_acq(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedOr64_nf(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedOr64_rel(__int64 volatile *_Value, __int64 _Mask);

/*----------------------------------------------------------------------------*\
|* Interlocked Xor
\*----------------------------------------------------------------------------*/
char _InterlockedXor8_acq(char volatile *_Value, char _Mask);
char _InterlockedXor8_nf(char volatile *_Value, char _Mask);
char _InterlockedXor8_rel(char volatile *_Value, char _Mask);
short _InterlockedXor16_acq(short volatile *_Value, short _Mask);
short _InterlockedXor16_nf(short volatile *_Value, short _Mask);
short _InterlockedXor16_rel(short volatile *_Value, short _Mask);
long _InterlockedXor_acq(long volatile *_Value, long _Mask);
long _InterlockedXor_nf(long volatile *_Value, long _Mask);
long _InterlockedXor_rel(long volatile *_Value, long _Mask);
__int64 _InterlockedXor64_acq(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedXor64_nf(__int64 volatile *_Value, __int64 _Mask);
__int64 _InterlockedXor64_rel(__int64 volatile *_Value, __int64 _Mask);

/*----------------------------------------------------------------------------*\
|* Interlocked Exchange
\*----------------------------------------------------------------------------*/
char _InterlockedExchange8_acq(char volatile *_Target, char _Value);
char _InterlockedExchange8_nf(char volatile *_Target, char _Value);
char _InterlockedExchange8_rel(char volatile *_Target, char _Value);
short _InterlockedExchange16_acq(short volatile *_Target, short _Value);
short _InterlockedExchange16_nf(short volatile *_Target, short _Value);
short _InterlockedExchange16_rel(short volatile *_Target, short _Value);
long _InterlockedExchange_acq(long volatile *_Target, long _Value);
long _InterlockedExchange_nf(long volatile *_Target, long _Value);
long _InterlockedExchange_rel(long volatile *_Target, long _Value);
__int64 _InterlockedExchange64_acq(__int64 volatile *_Target, __int64 _Value);
__int64 _InterlockedExchange64_nf(__int64 volatile *_Target, __int64 _Value);
__int64 _InterlockedExchange64_rel(__int64 volatile *_Target, __int64 _Value);

/*----------------------------------------------------------------------------*\
|* Interlocked Compare Exchange
\*----------------------------------------------------------------------------*/
char _InterlockedCompareExchange8_acq(char volatile *_Destination,
                                      char _Exchange, char _Comparand);
char _InterlockedCompareExchange8_nf(char volatile *_Destination,
                                     char _Exchange, char _Comparand);
char _InterlockedCompareExchange8_rel(char volatile *_Destination,
                                      char _Exchange, char _Comparand);
short _InterlockedCompareExchange16_acq(short volatile *_Destination,
                                        short _Exchange, short _Comparand);
short _InterlockedCompareExchange16_nf(short volatile *_Destination,
                                       short _Exchange, short _Comparand);
short _InterlockedCompareExchange16_rel(short volatile *_Destination,
                                        short _Exchange, short _Comparand);
long _InterlockedCompareExchange_acq(long volatile *_Destination,
                                     long _Exchange, long _Comparand);
long _InterlockedCompareExchange_nf(long volatile *_Destination, long _Exchange,
                                    long _Comparand);
long _InterlockedCompareExchange_rel(long volatile *_Destination,
                                     long _Exchange, long _Comparand);
__int64 _InterlockedCompareExchange64_acq(__int64 volatile *_Destination,
                                          __int64 _Exchange,
                                          __int64 _Comparand);
__int64 _InterlockedCompareExchange64_nf(__int64 volatile *_Destination,
                                         __int64 _Exchange, __int64 _Comparand);
__int64 _InterlockedCompareExchange64_rel(__int64 volatile *_Destination,
                                          __int64 _Exchange,
                                          __int64 _Comparand);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __INTRIN0_H */
#endif /* _MSC_VER */
