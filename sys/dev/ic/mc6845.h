/*	$OpenBSD: mc6845.h,v 1.5 2005/12/12 12:35:49 mickey Exp $	*/

/*
 * Copyright (c) 1992, 1995 Hellmuth Michaelis and Joerg Wunsch.
 * Copyright (c) 1992, 1993 Brian Dunford-Shore.
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
 *	This product includes software developed by
 *	Hellmuth Michaelis, Brian Dunford-Shore and Joerg Wunsch.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */


#define MONO_BASE	0x3B4		/* crtc index register address mono */
#define CGA_BASE	0x3D4		/* crtc index register address color */

#define	CRTC_ADDR	0x00		/* index register */

#define CRTC_HTOTAL	0x00		/* horizontal total */
#define CRTC_HDISPLE	0x01		/* horizontal display end */
#define CRTC_HBLANKS	0x02		/* horizontal blank start */
#define CRTC_HBLANKE	0x03		/* horizontal blank end */
#define CRTC_HSYNCS	0x04		/* horizontal sync start */
#define CRTC_HSYNCE	0x05		/* horizontal sync end */
#define CRTC_VTOTAL	0x06		/* vertical total */
#define CRTC_OVERFLL	0x07		/* overflow low */
#define CRTC_IROWADDR	0x08		/* initial row address */
#define CRTC_MAXROW	0x09		/* maximum row address */
#define CRTC_CURSTART	0x0A		/* cursor start row address */
#define CRTC_CUREND	0x0B		/* cursor end row address */
#define CRTC_STARTADRH	0x0C		/* linear start address mid */
#define CRTC_STARTADRL	0x0D		/* linear start address low */
#define CRTC_CURSORH	0x0E		/* cursor address mid */
#define CRTC_CURSORL	0x0F		/* cursor address low */
#define CRTC_VSYNCS	0x10		/* vertical sync start */
#define CRTC_VSYNCE	0x11		/* vertical sync end */
#define CRTC_VDE	0x12		/* vertical display end */
#define CRTC_OFFSET	0x13		/* row offset */
#define CRTC_ULOC	0x14		/* underline row address */
#define CRTC_VBSTART	0x15		/* vertical blank start */
#define CRTC_VBEND	0x16		/* vertical blank end */
#define CRTC_MODE	0x17		/* CRTC mode register */
#define CRTC_SPLITL	0x18		/* split screen start low */

/* start of ET4000 extensions */

#define CRTC_RASCAS	0x32		/* ras/cas configuration */
#define CRTC_EXTSTART	0x33		/* extended start address */
#define CRTC_COMPAT6845	0x34		/* 6845 compatibility control */
#define CRTC_OVFLHIGH	0x35		/* overflow high */
#define CRTC_SYSCONF1	0x36		/* video system configuration 1 */
#define CRTC_SYSCONF2	0x36		/* video system configuration 2 */

/* start of WD/Paradise extensions */

#define	CRTC_PR10	0x29		/* r/w unlocking */
#define	CRTC_PR11	0x2A		/* ega switches */
#define	CRTC_PR12	0x2B		/* scratch pad */
#define	CRTC_PR13	0x2C		/* interlace h/2 start */
#define	CRTC_PR14	0x2D		/* interlace h/2 end */
#define	CRTC_PR15	0x2E		/* misc control #1 */
#define	CRTC_PR16	0x2F		/* misc control #2 */
#define	CRTC_PR17	0x30		/* misc control #3 */
					/* 0x31 .. 0x3f reserved */
/* Video 7 */

#define CRTC_V7ID	0x1f		/* identification register */

/* Trident */

#define CRTC_MTEST	0x1e		/* module test register */
#define CRTC_SOFTPROG	0x1f		/* software programming */
#define CRTC_LATCHRDB	0x22		/* latch read back register */
#define CRTC_ATTRSRDB	0x24		/* attribute state read back register*/
#define CRTC_ATTRIRDB	0x26		/* attribute index read back register*/
#define CRTC_HOSTAR	0x27		/* high order start address register */

