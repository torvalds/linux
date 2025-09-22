/*	$OpenBSD: kbd.c,v 1.2 2023/01/10 17:10:57 miod Exp $	*/
/*	$NetBSD: kbd.c,v 1.1 2013/01/05 17:44:24 tsutsui Exp $	*/

/*
 * Copyright (c) 1992 OMRON Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kbd.c	8.1 (Berkeley) 6/10/93
 */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kbd.c	8.1 (Berkeley) 6/10/93
 */

/*
 * kbd.c -- key-code decoding routine
 *   by A.Fujita, Dec-12-1992
 */

#include <sys/param.h>
#include <luna88k/stand/boot/samachdep.h>
#include <luna88k/stand/boot/kbdreg.h>

const struct kbd_keymap kbd_keymap[] = {
	{ KC_IGNORE,	{ 0,	    0        } },	/*   0 [0x00]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*   1 [0x01]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*   2 [0x02]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*   3 [0x03]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*   4 [0x04]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*   5 [0x05]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*   6 [0x06]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*   7 [0x07]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*   8 [0x08]	      */
	{ KC_CODE,	{ 0x09,     0x09     } },	/*   9 [0x09]	TAB   */
	{ KC_SHIFT,	{ KS_CTRL,  KS_CTRL  } },	/*  10 [0x0A]	CTRL  */
	{ KC_IGNORE,	{ 0,        0        } },	/*  11 [0x0B]	      */
	{ KC_SHIFT,	{ KS_SHIFT, KS_SHIFT } },	/*  12 [0x0C]	SHIFT */
	{ KC_SHIFT,	{ KS_SHIFT, KS_SHIFT } },	/*  13 [0x0D]	SHIFT */
	{ KC_IGNORE,	{ 0,        0        } },	/*  14 [0x0E]	      */
	{ KC_SHIFT,	{ KS_META,  KS_META  } },	/*  15 [0x0F]	META  */
	{ KC_CODE,	{ 0x1B,     0x1B     } },	/*  16 [0x10]	ESC   */
	{ KC_CODE,	{ 0x08,	    0x08     } },	/*  17 [0x11]	BS    */
	{ KC_CODE,	{ 0x0D,	    0x0D     } },	/*  18 [0x12]	CR    */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  19 [0x13]	      */
	{ KC_CODE,	{ 0x20,	    0x20     } },	/*  20 [0x14]	SP    */
	{ KC_CODE,	{ 0x7F,	    0x7F     } },	/*  21 [0x15]	DEL   */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  22 [0x16]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  23 [0x17]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  24 [0x18]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  25 [0x19]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  26 [0x1A]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  27 [0x1B]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  28 [0x1C]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  29 [0x1D]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  30 [0x1E]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  31 [0x1F]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  32 [0x20]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  33 [0x21]	      */
	{ KC_CODE,	{ 0x31,	    0x21     } },	/*  34 [0x22]	 1    */
	{ KC_CODE,	{ 0x32,	    0x22     } },	/*  35 [0x23]	 2    */
	{ KC_CODE,	{ 0x33,	    0x23     } },	/*  36 [0x24]	 3    */
	{ KC_CODE,	{ 0x34,	    0x24     } },	/*  37 [0x25]	 4    */
	{ KC_CODE,	{ 0x35,	    0x25     } },	/*  38 [0x26]	 5    */
	{ KC_CODE,	{ 0x36,	    0x26     } },	/*  39 [0x27]	 6    */
	{ KC_CODE,	{ 0x37,	    0x27     } },	/*  40 [0x28]	 7    */
	{ KC_CODE,	{ 0x38,	    0x28     } },	/*  41 [0x29]	 8    */
	{ KC_CODE,	{ 0x39,	    0x29     } },	/*  42 [0x2A]	 9    */
	{ KC_CODE,	{ 0x30,	    0x30     } },	/*  43 [0x2B]	 0    */
	{ KC_CODE,	{ 0x2D,	    0x3D     } },	/*  44 [0x2C]	 -    */
	{ KC_CODE,	{ 0x5E,	    0x7E     } },	/*  45 [0x2D]	 ^    */
	{ KC_CODE,	{ 0x5C,	    0x7C     } },	/*  46 [0x2E]	 \    */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  47 [0x2F]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  48 [0x30]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  49 [0x31]	      */
	{ KC_CODE,	{ 0x71,	    0x51     } },	/*  50 [0x32]	 q    */
	{ KC_CODE,	{ 0x77,	    0x57     } },	/*  51 [0x33]	 w    */
	{ KC_CODE,	{ 0x65,	    0x45     } },	/*  52 [0x34]	 e    */
	{ KC_CODE,	{ 0x72,	    0x52     } },	/*  53 [0x35]	 r    */
	{ KC_CODE,	{ 0x74,	    0x54     } },	/*  54 [0x36]	 t    */
	{ KC_CODE,	{ 0x79,	    0x59     } },	/*  55 [0x37]	 y    */
	{ KC_CODE,	{ 0x75,	    0x55     } },	/*  56 [0x38]	 u    */
	{ KC_CODE,	{ 0x69,	    0x49     } },	/*  57 [0x39]	 i    */
	{ KC_CODE,	{ 0x6F,	    0x4F     } },	/*  58 [0x3A]	 o    */
	{ KC_CODE,	{ 0x70,	    0x50     } },	/*  59 [0x3B]	 p    */
	{ KC_CODE,	{ 0x40,	    0x60     } },	/*  60 [0x3C]	 @    */
	{ KC_CODE,	{ 0x5B,	    0x7B     } },	/*  61 [0x3D]	 [    */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  62 [0x3E]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  63 [0x3F]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  64 [0x40]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  65 [0x41]	      */
	{ KC_CODE,	{ 0x61,	    0x41     } },	/*  66 [0x42]	 a    */
	{ KC_CODE,	{ 0x73,	    0x53     } },	/*  67 [0x43]	 s    */
	{ KC_CODE,	{ 0x64,	    0x44     } },	/*  68 [0x44]	 d    */
	{ KC_CODE,	{ 0x66,	    0x46     } },	/*  69 [0x45]	 f    */
	{ KC_CODE,	{ 0x67,	    0x47     } },	/*  70 [0x46]	 g    */
	{ KC_CODE,	{ 0x68,	    0x48     } },	/*  71 [0x47]	 h    */
	{ KC_CODE,	{ 0x6A,	    0x4A     } },	/*  72 [0x48]	 j    */
	{ KC_CODE,	{ 0x6B,	    0x4B     } },	/*  73 [0x49]	 k    */
	{ KC_CODE,	{ 0x6C,	    0x4C     } },	/*  74 [0x4A]	 l    */
	{ KC_CODE,	{ 0x3B,	    0x2B     } },	/*  75 [0x4B]	 ;    */
	{ KC_CODE,	{ 0x3A,	    0x2A     } },	/*  76 [0x4C]	 :    */
	{ KC_CODE,	{ 0x5D,	    0x7D     } },	/*  77 [0x4D]	 ]    */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  78 [0x4E]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  79 [0x4F]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  80 [0x50]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  81 [0x51]	      */
	{ KC_CODE,	{ 0x7A,	    0x5A     } },	/*  82 [0x52]	 z    */
	{ KC_CODE,	{ 0x78,	    0x58     } },	/*  83 [0x53]	 x    */
	{ KC_CODE,	{ 0x63,	    0x43     } },	/*  84 [0x54]	 c    */
	{ KC_CODE,	{ 0x76,	    0x56     } },	/*  85 [0x55]	 v    */
	{ KC_CODE,	{ 0x62,	    0x42     } },	/*  86 [0x56]	 b    */
	{ KC_CODE,	{ 0x6E,	    0x4E     } },	/*  87 [0x57]	 n    */
	{ KC_CODE,	{ 0x6D,	    0x4D     } },	/*  88 [0x58]	 m    */
	{ KC_CODE,	{ 0x2C,	    0x3C     } },	/*  89 [0x59]	 ,    */
	{ KC_CODE,	{ 0x2E,	    0x3E     } },	/*  90 [0x5A]	 .    */
	{ KC_CODE,	{ 0x2F,	    0x3F     } },	/*  91 [0x5B]	 /    */
	{ KC_CODE,	{ 0x5F,	    0x5F     } },	/*  92 [0x5C]	 _    */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  93 [0x5D]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  94 [0x5E]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  95 [0x5F]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  96 [0x60]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  97 [0x61]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  98 [0x62]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/*  99 [0x63]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 100 [0x64]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 101 [0x65]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 102 [0x66]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 103 [0x67]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 104 [0x68]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 105 [0x69]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 106 [0x6A]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 107 [0x6B]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 108 [0x6C]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 109 [0x6D]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 110 [0x6E]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 111 [0x6F]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 112 [0x70]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 113 [0x71]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 114 [0x72]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 115 [0x73]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 116 [0x74]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 117 [0x75]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 118 [0x76]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 119 [0x77]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 120 [0x78]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 121 [0x79]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 122 [0x7A]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 123 [0x7B]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 124 [0x7C]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 125 [0x7D]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 126 [0x7E]	      */
	{ KC_IGNORE,	{ 0,	    0        } },	/* 127 [0x7F]	      */
};

int	shift_flag = 0;
int	ctrl_flag  = 0;
int	meta_flag  = 0;

int
kbd_decode(u_char code)
{
	unsigned int c, updown = 0;

	if (code & 0x80)
		updown = 1;

	code &= 0x7F;

	c = kbd_keymap[code].km_type;

	if (c == KC_IGNORE)
		return(KC_IGNORE);

	if ((c == KC_CODE) && updown)
		return(KC_IGNORE);

	if (c == KC_SHIFT) {
		switch(kbd_keymap[code].km_code[0]) {

		case KS_SHIFT:
			shift_flag = 1 - updown;
			break;

		case KS_CTRL:
			ctrl_flag  = 1 - updown;
			break;

		case KS_META:
			meta_flag  = 1 - updown;
			break;
		}

		return(KC_IGNORE);
	}

	if (shift_flag)
		c = kbd_keymap[code].km_code[1];
	else
		c = kbd_keymap[code].km_code[0];

	if (meta_flag)
		c |= 0x80;

	if (ctrl_flag)
		c &= 0x1F;

	return(c);
}
