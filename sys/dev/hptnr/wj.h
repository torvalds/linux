/* $Id: wj.h,v 1.1 2007/11/01 03:05:51 gmm Exp $ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 HighPoint Technologies, Inc.
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

#include <dev/hptnr/hptnr_config.h>
 *
 * NVRAM write journaling interface.
 */

#ifndef _WJ_H_
#define _WJ_H_

#if defined(SUPPORT_BBU) || defined(SUPPORT_NVRAM)

void wj_init(PVBUS vbus, void *nvram_addr, HPT_U32 nvram_size);
void *wj_add_entry(PVBUS vbus, PVDEV vd, HPT_LBA lba, HPT_U16 sectors);
void *wj_get_entry(PVBUS vbus, PVDEV *vd_p, HPT_LBA *lba_p, HPT_U16 *sectors_p);
void wj_del_entry(PVBUS vbus, void *handle);
void wj_del_vd(PVBUS vbus, PVDEV vd);
void wj_sync_stamp(PVBUS vbus, PVDEV vd);

#else 

#define wj_add_entry(vbus, vd, lba, sectors) 0
#define wj_get_entry(vbus, vd_p, lba_p, sectors_p) 0
#define wj_del_entry(vbus, handle) 0
#define wj_del_vd(vbus, vd) 0
#define wj_sync_stamp(vbus, vd) 0

#endif

#endif
