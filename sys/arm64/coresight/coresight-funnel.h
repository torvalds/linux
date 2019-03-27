/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_ARM64_CORESIGHT_CORESIGHT_FUNNEL_H_
#define	_ARM64_CORESIGHT_CORESIGHT_FUNNEL_H_

#define	FUNNEL_FUNCTL		0x000 /* Funnel Control Register */
#define	 FUNCTL_HOLDTIME_SHIFT	8
#define	 FUNCTL_HOLDTIME_MASK	(0xf << FUNCTL_HOLDTIME_SHIFT)
#define	FUNNEL_PRICTL		0x004 /* Priority Control Register */
#define	FUNNEL_ITATBDATA0	0xEEC /* Integration Register, ITATBDATA0 */
#define	FUNNEL_ITATBCTR2	0xEF0 /* Integration Register, ITATBCTR2 */
#define	FUNNEL_ITATBCTR1	0xEF4 /* Integration Register, ITATBCTR1 */
#define	FUNNEL_ITATBCTR0	0xEF8 /* Integration Register, ITATBCTR0 */
#define	FUNNEL_IMCR		0xF00 /* Integration Mode Control Register */
#define	FUNNEL_CTSR		0xFA0 /* Claim Tag Set Register */
#define	FUNNEL_CTCR		0xFA4 /* Claim Tag Clear Register */
#define	FUNNEL_LOCKACCESS	0xFB0 /* Lock Access */
#define	FUNNEL_LOCKSTATUS	0xFB4 /* Lock Status */
#define	FUNNEL_AUTHSTATUS	0xFB8 /* Authentication status */
#define	FUNNEL_DEVICEID		0xFC8 /* Device ID */
#define	FUNNEL_DEVICETYPE	0xFCC /* Device Type Identifier */
#define	FUNNEL_PERIPH4		0xFD0 /* Peripheral ID4 */
#define	FUNNEL_PERIPH5		0xFD4 /* Peripheral ID5 */
#define	FUNNEL_PERIPH6		0xFD8 /* Peripheral ID6 */
#define	FUNNEL_PERIPH7		0xFDC /* Peripheral ID7 */
#define	FUNNEL_PERIPH0		0xFE0 /* Peripheral ID0 */
#define	FUNNEL_PERIPH1		0xFE4 /* Peripheral ID1 */
#define	FUNNEL_PERIPH2		0xFE8 /* Peripheral ID2 */
#define	FUNNEL_PERIPH3		0xFEC /* Peripheral ID3 */
#define	FUNNEL_COMP0		0xFF0 /* Component ID0 */
#define	FUNNEL_COMP1		0xFF4 /* Component ID1 */
#define	FUNNEL_COMP2		0xFF8 /* Component ID2 */
#define	FUNNEL_COMP3		0xFFC /* Component ID3 */

#endif /* !_ARM64_CORESIGHT_CORESIGHT_FUNNEL_H_ */
