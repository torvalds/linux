/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Olivier Houchard.  All rights reserved.
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
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * $FreeBSD$
 */

#ifndef OMAP4_SMC_H_
#define OMAP4_SMC_H_
/* Define the various function IDs used by the OMAP4 */
#define L2CACHE_WRITE_DEBUG_REG		0x100
#define L2CACHE_CLEAN_INV_RANG		0x101
#define L2CACHE_WRITE_CTRL_REG		0x102
#define READ_AUX_CORE_REGS		0x103
#define MODIFY_AUX_CORE_0		0x104
#define WRITE_AUX_CORE_1		0x105
#define READ_WKG_CTRL_REG		0x106
#define CLEAR_WKG_CTRL_REG		0x107
#define SET_POWER_STATUS_REG		0x108
#define WRITE_AUXCTRL_REG		0x109
#define LOCKDOWN_TLB			0x10a
#define SELECT_TLB_ENTRY_FOR_WRITE	0x10b
#define READ_TLB_VA_ENTRY		0x10c
#define WRITE_TLB_VA_ENTRY		0x10d
#define READ_TLB_PA_ENTRY		0x10e
#define WRITE_TLB_PA_ENTRY		0x10f
#define READ_TLB_ATTR_ENTRY		0x110
#define WRITE_TLB_ATTR_ENTRY		0x111
#define WRITE_LATENCY_CTRL_REG		0x112
#define WRITE_PREFETCH_CTRL_REG		0x113
#endif /* OMAP4_SMC_H_ */
