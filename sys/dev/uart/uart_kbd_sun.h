/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 * $FreeBSD$
 */

/* keyboard commands (host->kbd) */
#define	SKBD_CMD_RESET		0x01
#define	SKBD_CMD_BELLON		0x02
#define	SKBD_CMD_BELLOFF	0x03
#define	SKBD_CMD_CLICKON	0x0a
#define	SKBD_CMD_CLICKOFF	0x0b
#define	SKBD_CMD_SETLED		0x0e
#define	SKBD_CMD_LAYOUT		0x0f

/* keyboard responses (kbd->host) */
#define	SKBD_RSP_RESET_OK	0x04	/* normal reset status for type 4/5/6 */
#define	SKBD_RSP_IDLE		0x7f	/* no keys down */
#define	SKBD_RSP_LAYOUT		0xfe	/* layout follows */
#define	SKBD_RSP_RESET		0xff	/* reset status follows */

#define	SKBD_LED_NUMLOCK	0x01
#define	SKBD_LED_COMPOSE	0x02
#define	SKBD_LED_SCROLLLOCK	0x04
#define	SKBD_LED_CAPSLOCK	0x08

#define	SKBD_STATE_RESET	0
#define	SKBD_STATE_LAYOUT	1
#define	SKBD_STATE_GETKEY	2

/* keyboard types */
#define	KB_SUN2		2		/* type 2 keyboard */
#define	KB_SUN3		3		/* type 3 keyboard */
#define	KB_SUN4		4		/* type 4/5/6 keyboard */

#define	SKBD_KEY_RELEASE	0x80
#define	SKBD_KEY_CHAR(c)	((c) & 0x7f)
