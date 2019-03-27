/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Stefan Bethke.
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
#ifndef	__ARSWITCH_REG_H__
#define	__ARSWITCH_REG_H__

extern	void arswitch_writedbg(device_t dev, int phy, uint16_t dbg_addr,
	    uint16_t dbg_data);
extern	void arswitch_writemmd(device_t dev, int phy, uint16_t dbg_addr,
	    uint16_t dbg_data);

extern	int arswitch_readreg(device_t dev, int addr);
extern	int arswitch_writereg(device_t dev, int addr, int value);
extern	int arswitch_modifyreg(device_t dev, int addr, int mask, int set);
extern	int arswitch_waitreg(device_t, int, int, int, int);

extern	int arswitch_readreg_lsb(device_t dev, int addr);
extern	int arswitch_readreg_msb(device_t dev, int addr);

extern	int arswitch_writereg_lsb(device_t dev, int addr, int data);
extern	int arswitch_writereg_msb(device_t dev, int addr, int data);

#endif	/* __ARSWITCH_REG_H__ */
