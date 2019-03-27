/* Copyright (c) 2008-2011 Freescale Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __CTYPE_EXT_H
#define __CTYPE_EXT_H


#if defined(NCSW_LINUX) && defined(__KERNEL__) || defined(NCSW_FREEBSD)
/*
 * NOTE! This ctype does not handle EOF like the standard C
 * library is required to.
 */

#define _U    0x01    /* upper */
#define _L    0x02    /* lower */
#define _D    0x04    /* digit */
#define _C    0x08    /* cntrl */
#define _P    0x10    /* punct */
#define _S    0x20    /* white space (space/lf/tab) */
#define _X    0x40    /* hex digit */
#define _SP   0x80    /* hard space (0x20) */

extern unsigned char _ctype[];

#define __ismask(x) (_ctype[(int)(unsigned char)(x)])

#define isalnum(c)    ((__ismask(c)&(_U|_L|_D)) != 0)
#define isalpha(c)    ((__ismask(c)&(_U|_L)) != 0)
#define iscntrl(c)    ((__ismask(c)&(_C)) != 0)
#define isdigit(c)    ((__ismask(c)&(_D)) != 0)
#define isgraph(c)    ((__ismask(c)&(_P|_U|_L|_D)) != 0)
#define islower(c)    ((__ismask(c)&(_L)) != 0)
#define isprint(c)    ((__ismask(c)&(_P|_U|_L|_D|_SP)) != 0)
#define ispunct(c)    ((__ismask(c)&(_P)) != 0)
#define isspace(c)    ((__ismask(c)&(_S)) != 0)
#define isupper(c)    ((__ismask(c)&(_U)) != 0)
#define isxdigit(c)   ((__ismask(c)&(_D|_X)) != 0)

#define isascii(c) (((unsigned char)(c))<=0x7f)
#define toascii(c) (((unsigned char)(c))&0x7f)

static __inline__ unsigned char __tolower(unsigned char c)
{
    if (isupper(c))
        c -= 'A'-'a';
    return c;
}

static __inline__ unsigned char __toupper(unsigned char c)
{
    if (islower(c))
        c -= 'a'-'A';
    return c;
}

#define tolower(c) __tolower(c)
#define toupper(c) __toupper(c)

#else
#include <ctype.h>
#endif /* defined(NCSW_LINUX) && defined(__KERNEL__) || defined(NCSW_FREEBSD) */


#endif /* __CTYPE_EXT_H */
