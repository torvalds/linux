/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 HighPoint Technologies, Inc.
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
#ifndef _ACCESS601_H_
#define _ACCESS601_H_

#ifndef FOR_DEMO

void HPTLIBAPI BeepOn(MV_BUS_ADDR_T BaseAddr);
void HPTLIBAPI BeepOff(MV_BUS_ADDR_T BaseAddr);
UCHAR HPTLIBAPI check_protect_circuit(MV_BUS_ADDR_T BaseAddr);

#ifdef SUPPORT_FAIL_LED
void HPTLIBAPI set_fail_led(MV_SATA_ADAPTER *pAdapter, UCHAR channel, UCHAR state);
void HPTLIBAPI set_fail_leds(MV_SATA_ADAPTER *pAdapter, UCHAR mask);
#else 
#define set_fail_led(pAdapter, channel, state)
#define set_fail_leds(pAdapter, mask)
#endif

int HPTLIBAPI sx508x_ioctl(MV_SATA_ADAPTER *pSataAdapter, UCHAR *indata, ULONG inlen,
			UCHAR *outdata, ULONG maxoutlen, ULONG *poutlen);

MV_BOOLEAN HPTLIBAPI sx508x_flash_access(MV_SATA_ADAPTER *pSataAdapter,
			MV_U32 offset, void *value, int size, int reading);
#else 

#define BeepOn(addr)
#define BeepOff(addr)
#define check_protect_circuit(addr) 1
#define set_fail_led(pAdapter, channel, state)
#define set_fail_leds(pAdapter, mask)
#define sx508x_ioctl(pSataAdapter, indata, inlen, outdata, maxoutlen, poutlen) 0
#define sx508x_flash_access(pSataAdapter, offset, value, size, reading) 0

#endif

#endif
