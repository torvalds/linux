/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Adaptec, Inc.
 * Copyright (c) 2010-2012 PMC-Sierra, Inc.
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
 *	$FreeBSD$
 */

#ifndef PRINT_BUFFER_SIZE

#define PRINT_BUFFER_SIZE 512

#define HBA_FLAGS_DBG_FLAGS_MASK 	0x0000ffff
#define HBA_FLAGS_DBG_KERNEL_PRINT_B 	0x00000001
#define HBA_FLAGS_DBG_FW_PRINT_B 	0x00000002
#define HBA_FLAGS_DBG_FUNCTION_ENTRY_B 	0x00000004
#define HBA_FLAGS_DBG_FUNCTION_EXIT_B 	0x00000008
#define HBA_FLAGS_DBG_ERROR_B 		0x00000010
#define HBA_FLAGS_DBG_INIT_B 		0x00000020
#define HBA_FLAGS_DBG_OS_COMMANDS_B 	0x00000040
#define HBA_FLAGS_DBG_SCAN_B 		0x00000080
#define HBA_FLAGS_DBG_COALESCE_B 	0x00000100
#define HBA_FLAGS_DBG_IOCTL_COMMANDS_B 	0x00000200
#define HBA_FLAGS_DBG_SYNC_COMMANDS_B 	0x00000400
#define HBA_FLAGS_DBG_COMM_B 		0x00000800
#define HBA_FLAGS_DBG_CSMI_COMMANDS_B 	0x00001000
#define HBA_FLAGS_DBG_AIF_B 		0x00001000
#define HBA_FLAGS_DBG_DEBUG_B 		0x00002000

#define FW_DEBUG_STR_LENGTH_OFFSET 	0x00
#define FW_DEBUG_FLAGS_OFFSET 		0x04
#define FW_DEBUG_BLED_OFFSET 		0x08
#define FW_DEBUG_FLAGS_NO_HEADERS_B 	0x01

struct aac_softc;
extern int aacraid_get_fw_debug_buffer(struct aac_softc *);
extern void aacraid_fw_printf(struct aac_softc *, unsigned long, const char *, ...);
extern void aacraid_fw_print_mem(struct aac_softc *, unsigned long, u_int8_t *,int);
extern int aacraid_sync_command(struct aac_softc *sc, u_int32_t command,
				 u_int32_t arg0, u_int32_t arg1, u_int32_t arg2,
				 u_int32_t arg3, u_int32_t *sp, u_int32_t *r1);

#endif
