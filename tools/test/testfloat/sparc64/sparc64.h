/*-
 * Copyright (c) 2010 by Peter Jeremy <peterjeremy@acm.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*----------------------------------------------------------------------------
| One of the macros `BIGENDIAN' or `LITTLEENDIAN' must be defined.
*----------------------------------------------------------------------------*/
#define BIGENDIAN

/*----------------------------------------------------------------------------
| The macro `BITS64' can be defined to indicate that 64-bit integer types are
| supported by the compiler.
*----------------------------------------------------------------------------*/
#define BITS64

/*----------------------------------------------------------------------------
| Each of the following `typedef's defines the most convenient type that holds
| integers of at least as many bits as specified.  For example, `uint8' should
| be the most convenient type that can hold unsigned integers of as many as
| 8 bits.  The `flag' type must be able to hold either a 0 or 1.  For most
| implementations of C, `flag', `uint8', and `int8' should all be `typedef'ed
| to the same as `int'.
*----------------------------------------------------------------------------*/
typedef int flag;
typedef int uint8;
typedef int int8;
typedef int uint16;
typedef int int16;
typedef unsigned int uint32;
typedef signed int int32;
#ifdef BITS64
typedef unsigned long int uint64;
typedef signed long int int64;
#endif

/*----------------------------------------------------------------------------
| Each of the following `typedef's defines a type that holds integers
| of _exactly_ the number of bits specified.  For instance, for most
| implementation of C, `bits16' and `sbits16' should be `typedef'ed to
| `unsigned short int' and `signed short int' (or `short int'), respectively.
*----------------------------------------------------------------------------*/
typedef unsigned char bits8;
typedef signed char sbits8;
typedef unsigned short int bits16;
typedef signed short int sbits16;
typedef unsigned int bits32;
typedef signed int sbits32;
#ifdef BITS64
typedef unsigned long int bits64;
typedef signed long int sbits64;
#endif

#ifdef BITS64
/*----------------------------------------------------------------------------
| The `LIT64' macro takes as its argument a textual integer literal and
| if necessary ``marks'' the literal as having a 64-bit integer type.
| For example, the GNU C Compiler (`gcc') requires that 64-bit literals be
| appended with the letters `LL' standing for `long long', which is `gcc's
| name for the 64-bit integer type.  Some compilers may allow `LIT64' to be
| defined as the identity macro:  `#define LIT64( a ) a'.
*----------------------------------------------------------------------------*/
#define LIT64( a ) a##L
#endif

/*----------------------------------------------------------------------------
| The macro `INLINE' can be used before functions that should be inlined.  If
| a compiler does not support explicit inlining, this macro should be defined
| to be `static'.
*----------------------------------------------------------------------------*/
#define INLINE extern inline
