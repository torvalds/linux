/*
 * linux/include/asm-mips/tx4938/tx4938_mips.h
 * Generic bitmask definitions
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */

#ifndef TX4938_TX4938_MIPS_H
#define TX4938_TX4938_MIPS_H
#ifndef __ASSEMBLY__

#define reg_rd08(r)    ((u8 )(*((vu8 *)(r))))
#define reg_rd16(r)    ((u16)(*((vu16*)(r))))
#define reg_rd32(r)    ((u32)(*((vu32*)(r))))
#define reg_rd64(r)    ((u64)(*((vu64*)(r))))

#define reg_wr08(r,v)  ((*((vu8 *)(r)))=((u8 )(v)))
#define reg_wr16(r,v)  ((*((vu16*)(r)))=((u16)(v)))
#define reg_wr32(r,v)  ((*((vu32*)(r)))=((u32)(v)))
#define reg_wr64(r,v)  ((*((vu64*)(r)))=((u64)(v)))

typedef volatile __signed char vs8;
typedef volatile unsigned char vu8;

typedef volatile __signed short vs16;
typedef volatile unsigned short vu16;

typedef volatile __signed int vs32;
typedef volatile unsigned int vu32;

typedef s8 s08;
typedef vs8 vs08;

typedef u8 u08;
typedef vu8 vu08;

#if (_MIPS_SZLONG == 64)

typedef volatile __signed__ long vs64;
typedef volatile unsigned long vu64;

#else

typedef volatile __signed__ long long vs64;
typedef volatile unsigned long long vu64;

#endif
#endif
#endif
