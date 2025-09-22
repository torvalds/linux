/*	$OpenBSD: prefix.h,v 1.2 2001/03/29 04:08:21 mickey Exp $	*/

/*
 *  (c) Copyright 1985 HEWLETT-PACKARD COMPANY
 *
 *  To anyone who acknowledges that this file is provided "AS IS"
 *  without any express or implied warranty:
 *      permission to use, copy, modify, and distribute this file
 *  for any purpose is hereby granted without fee, provided that
 *  the above copyright notice and this notice appears in all
 *  copies, and that the name of Hewlett-Packard Company not be
 *  used in advertising or publicity pertaining to distribution
 *  of the software without specific, written prior permission.
 *  Hewlett-Packard Company makes no representations about the
 *  suitability of this software for any purpose.
 */

/*
 * STANDARD INCLUDE FILE FOR MILLICODE
 * Every source file must include this file.
 *
 * Hardware General Registers
 *
 * Frame Offsets (millicode convention!)
 * Used when calling other millicode routines.
 * Stack unwinding is dependent upon these definitions.
 * r31_slot	.equ	-20
 * sr0_slot	.equ	-16
 */

#include <machine/asm.h>

#define DEFINE(name, value)name:	.EQU	value
#ifdef milliext
#ifdef PIC
#define MILLI_BE(lbl) \
  BL    .+8,r1\
  ! ADDIL L%lbl-labl/**/lbl,r1\
  ! .LABEL labl/**/lbl\
  ! BE    R%lbl-labl/**/lbl(sr7,r1)

#define MILLI_BEN(lbl) \
  BL    .+8,r1\
  ! ADDIL L%lbl-labl/**/lbl,r1\
  ! .LABEL labl/**/lbl\
  ! BE,N  R%lbl-labl/**/lbl(sr7,r1)

#define MILLI_BLE(lbl) \
  BL    .+8,r1\
  ! ADDIL L%lbl-labl/**/lbl,r1\
  ! .LABEL labl/**/lbl	\
  ! BLE   R%lbl-labl/**/lbl(sr7,r1)

#define MILLI_BLEN(lbl) \
  BL    .+8,r1\
  ! ADDIL L%lbl-labl/**/lbl,r1\
  ! .LABEL labl/**/lbl\
  ! BLE,N R%lbl-labl/**/lbl(sr7,r1)
#else
#define MILLI_BE(lbl)   BE    lbl(sr7,r0)
#define MILLI_BEN(lbl)  BE,n  lbl(sr7,r0)
#define MILLI_BLE(lbl)	BLE   lbl(sr7,r0)
#define MILLI_BLEN(lbl)	BLE,n lbl(sr7,r0)
#endif

#define MILLIRETN	BE,n  0(sr0,r31)
#define MILLIRET	BE    0(sr0,r31)
#define MILLI_RETN	BE,n  0(sr0,r31)
#define MILLI_RET	BE    0(sr0,r31)

#else
#define MILLI_BE(lbl)	B     lbl
#define MILLI_BEN(lbl)  B,n   lbl
#define MILLI_BLE(lbl)	BL    lbl,r31
#define MILLI_BLEN(lbl)	BL,n  lbl,r31
#define MILLIRETN	BV,n  0(r31)
#define MILLIRET	BV    0(r31)
#define MILLI_RETN	BV,n  0(r31)
#define MILLI_RET	BV    0(r31)
#endif
; VERSION is used wherever ".version" can appear in a routine
;#define VERSION .version
#define VERSION ;
