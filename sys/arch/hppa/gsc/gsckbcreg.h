/*	$OpenBSD: gsckbcreg.h,v 1.1 2003/01/31 22:50:19 miod Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF MIND,
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Register definitions for the GSC PS/2 compatible keyboard/mouse ports.
 *
 * These definitions attempt to match <dev/ic/i8042reg.h> names although the
 * actual wiring is different.
 */

#define	KBSTATP		12	/* controller status port (I) */
#define	KBS_DIB		0x01	/* data in buffer */
#define	KBS_OCMD	0x02	/* output buffer has command */
#define	KBS_PERR	0x04	/* parity error */
#define	KBS_TERR	0x08	/* transmission error */

#define	KBCMDP		8	/* controller port (O) */
#define	KBCP_ENABLE	0x01	/* enable device */
#define	KBCP_DIAG	0x20	/* diagnostic mode control */

#define	KBDATAP		4	/* data port (I) */
#define	KBOUTP		4	/* data port (O) */

#define	KBIDP		0	/* id port (I) */
#define	ID_KBD		0	/* slot is a keyboard port */
#define	ID_MOUSE	1	/* slot is a mouse port */

#define	KBRESETP	0	/* reset port (O) */

#define	KBMAPSIZE	16	/* size to bus_space_map() */

/*
 * Various command definitions not provided by the existing pckbc code.
 */

#define	KBC_ID		0xF2	/* get device identifier */
#define	KBR_MOUSE_ID	0x00	/* mouse type */
#define	KBR_KBD_ID1	0xAB	/* keyboard type */
#define	KBR_KBD_ID2	0x83

#define	KB_MAX_RETRANS	5	/* maximum number of command retrans attempts */
