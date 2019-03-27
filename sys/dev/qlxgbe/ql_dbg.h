/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2016 Qlogic Corporation
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
 * File : ql_dbg.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#ifndef _QL_DBG_H_
#define _QL_DBG_H_

extern void ql_dump_buf8(qla_host_t *ha, const char *str, void *dbuf,
		uint32_t len);
extern void ql_dump_buf16(qla_host_t *ha, const char *str, void *dbuf,
		uint32_t len16);
extern void ql_dump_buf32(qla_host_t *ha, const char *str, void *dbuf,
		uint32_t len32);

#define INJCT_RX_RXB_INVAL				0x00001
#define INJCT_RX_MP_NULL				0x00002
#define INJCT_LRO_RXB_INVAL				0x00003
#define INJCT_LRO_MP_NULL				0x00004
#define INJCT_NUM_HNDLE_INVALID				0x00005
#define INJCT_RDWR_INDREG_FAILURE			0x00006
#define INJCT_RDWR_OFFCHIPMEM_FAILURE			0x00007
#define INJCT_MBX_CMD_FAILURE				0x00008
#define INJCT_HEARTBEAT_FAILURE				0x00009
#define INJCT_TEMPERATURE_FAILURE			0x0000A
#define INJCT_M_GETCL_M_GETJCL_FAILURE			0x0000B
#define INJCT_INV_CONT_OPCODE				0x0000C
#define INJCT_SGL_RCV_INV_DESC_COUNT			0x0000D
#define INJCT_SGL_LRO_INV_DESC_COUNT			0x0000E
#define INJCT_PEER_PORT_FAILURE_ERR_RECOVERY		0x0000F
#define INJCT_TXBUF_MBUF_NON_NULL			0x00010

#ifdef QL_DBG

#define QL_DPRINT1(ha, x)	if (ha->dbg_level & 0x0001) device_printf x
#define QL_DPRINT2(ha, x)	if (ha->dbg_level & 0x0002) device_printf x
#define QL_DPRINT4(ha, x)	if (ha->dbg_level & 0x0004) device_printf x
#define QL_DPRINT8(ha, x)	if (ha->dbg_level & 0x0008) device_printf x
#define QL_DPRINT10(ha, x)	if (ha->dbg_level & 0x0010) device_printf x
#define QL_DPRINT20(ha, x)	if (ha->dbg_level & 0x0020) device_printf x
#define QL_DPRINT40(ha, x)	if (ha->dbg_level & 0x0040) device_printf x
#define QL_DPRINT80(ha, x)	if (ha->dbg_level & 0x0080) device_printf x

#define QL_DUMP_BUFFER8(h, s, b, n) if (h->dbg_level & 0x08000000)\
					qla_dump_buf8(h, s, b, n)
#define QL_DUMP_BUFFER16(h, s, b, n) if (h->dbg_level & 0x08000000)\
					qla_dump_buf16(h, s, b, n)
#define QL_DUMP_BUFFER32(h, s, b, n) if (h->dbg_level & 0x08000000)\
					qla_dump_buf32(h, s, b, n)

#define QL_ASSERT(ha, x, y)		if (!x && !ha->err_inject) panic y
#define QL_ERR_INJECT(ha, val)		(ha->err_inject == val)

#else

#define QL_DPRINT1(ha, x)
#define QL_DPRINT2(ha, x)
#define QL_DPRINT4(ha, x)
#define QL_DPRINT8(ha, x)
#define QL_DPRINT10(ha, x)
#define QL_DPRINT20(ha, x)
#define QL_DPRINT40(ha, x)
#define QL_DPRINT80(ha, x)

#define QL_DUMP_BUFFER8(h, s, b, n)
#define QL_DUMP_BUFFER16(h, s, b, n)
#define QL_DUMP_BUFFER32(h, s, b, n)

#define QL_ASSERT(ha, x, y)
#define QL_ERR_INJECT(ha, val)		0

#endif


#endif /* #ifndef _QL_DBG_H_ */
