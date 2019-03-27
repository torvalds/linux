/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * File : qla_dbg.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#ifndef _QL_DBG_H_
#define _QL_DBG_H_

extern uint32_t dbg_level;

extern void qla_dump_buf8(qla_host_t *ha, char *str, void *dbuf,
		uint32_t len);
extern void qla_dump_buf16(qla_host_t *ha, char *str, void *dbuf,
		uint32_t len16);
extern void qla_dump_buf32(qla_host_t *ha, char *str, void *dbuf,
		uint32_t len32);


#define DBG 1

#if DBG

#define QL_DPRINT1(x)	if (dbg_level & 0x0001) device_printf x
#define QL_DPRINT2(x)	if (dbg_level & 0x0002) device_printf x
#define QL_DPRINT4(x)	if (dbg_level & 0x0004) device_printf x
#define QL_DPRINT8(x)	if (dbg_level & 0x0008) device_printf x
#define QL_DPRINT10(x)	if (dbg_level & 0x0010) device_printf x
#define QL_DPRINT20(x)	if (dbg_level & 0x0020) device_printf x
#define QL_DPRINT40(x)	if (dbg_level & 0x0040) device_printf x
#define QL_DPRINT80(x)	if (dbg_level & 0x0080) device_printf x

#define QL_DUMP_BUFFER8(h, s, b, n) if (dbg_level & 0x08000000)\
					qla_dump_buf8(h, s, b, n)
#define QL_DUMP_BUFFER16(h, s, b, n) if (dbg_level & 0x08000000)\
					qla_dump_buf16(h, s, b, n)
#define QL_DUMP_BUFFER32(h, s, b, n) if (dbg_level & 0x08000000)\
					qla_dump_buf32(h, s, b, n)

#else

#define QL_DPRINT1(x)
#define QL_DPRINT2(x)
#define QL_DPRINT4(x)
#define QL_DPRINT8(x)
#define QL_DPRINT10(x)
#define QL_DPRINT20(x)
#define QL_DPRINT40(x)
#define QL_DPRINT80(x)

#define QL_DUMP_BUFFER8(h, s, b, n)
#define QL_DUMP_BUFFER16(h, s, b, n)
#define QL_DUMP_BUFFER32(h, s, b, n)

#endif

#endif /* #ifndef _QL_DBG_H_ */
