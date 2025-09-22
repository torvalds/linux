/*	$OpenBSD: md.h,v 1.8 2024/09/04 07:54:51 mglocker Exp $	*/
/*
  (c) Copyright 1986 HEWLETT-PACKARD COMPANY
  To anyone who acknowledges that this file is provided "AS IS"
  without any express or implied warranty:
      permission to use, copy, modify, and distribute this file
  for any purpose is hereby granted without fee, provided that
  the above copyright notice and this notice appears in all
  copies, and that the name of Hewlett-Packard Company not be
  used in advertising or publicity pertaining to distribution
  of the software without specific, written prior permission.
  Hewlett-Packard Company makes no representations about the
  suitability of this software for any purpose.
*/
/* @(#)md.h: Revision: 1.8.88.1 Date: 93/12/07 15:06:33 */

#include <sys/cdefs.h>

/*****************************************************************
 * Multiply/Divide SFU Internal State                            *
 *****************************************************************/
struct mdsfu_register {
    int rslt_hi,
	rslt_lo,
	ovflow;
    };

#define result_hi result->rslt_hi
#define result_lo result->rslt_lo
#define overflow result->ovflow

/*
 *  Constants
 */

#undef FALSE
#define FALSE 0

#undef TRUE
#define TRUE (!FALSE)

#undef NIL
#define NIL 0

#define WORD_LEN 32
#define BIT0 1<<31
#define BIT28 0x8
#define BIT29 0x4
#define BIT30 0x2
#define BIT31 0x1

	/* Simply copy the arguments to the emulated copies of the registers */
#define	mdrr(reg1,reg2,result)	{result_hi = reg1;result_lo = reg2;}

/*
 *  Structures
 */

struct md_state {
	int resulthi,	/* high word of result */
	    resultlo;	/* low word of result */
};

void divsfm(int, int, struct mdsfu_register *);
void divsfr(int, int, struct mdsfu_register *);
void divsim(int, int, struct mdsfu_register *);
void divsir(int, int, struct mdsfu_register *);

void divu(int, int, int, struct mdsfu_register *);
void divufr(unsigned int, unsigned int, struct mdsfu_register *);
void divuir(unsigned int, unsigned int, struct mdsfu_register *);

void mpyaccs(int, int, struct mdsfu_register *);
void mpyaccu(unsigned int, unsigned int, struct mdsfu_register *);
void mpys(int, int, struct mdsfu_register *);
void mpyscv(int, int, struct mdsfu_register *);
void mpyu(unsigned int, unsigned int, struct mdsfu_register *);
void mpyucv(unsigned int, unsigned int, struct mdsfu_register *);

int s_xmpy(int *, int *, struct mdsfu_register *);
int u_xmpy(int *, int *, struct mdsfu_register *);
