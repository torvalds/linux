/* $FreeBSD$ */
/*-
 * Copyright (c) 2013 Hans Petter Selasky. All rights reserved.
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
 */

#ifndef _SYSINIT_H_
#define	_SYSINIT_H_

struct sysinit_data {
	uint8_t	b_keyword_name[64];
	uint8_t	b_debug_info[128];
	uint8_t	b_global_type[128];
	uint8_t	b_global_name[128];
	uint8_t	b_file_name[256];
	uint32_t dw_endian32;
	uint32_t dw_msb_value;		/* must be non-zero, else entry is skipped */
	uint32_t dw_lsb_value;
	uint32_t dw_file_line;
}	__attribute__((__packed__));

#define	SYSINIT_ENTRY(uniq, keyword, msb, lsb, g_type, g_name, debug)	\
	static const struct sysinit_data sysinit_##uniq			\
	__attribute__((__section__(".debug.sysinit"),			\
		__used__, __aligned__(1))) = {				\
	.b_keyword_name = { keyword },					\
	.b_debug_info = { debug },					\
	.b_global_type = { g_type },					\
	.b_global_name = { g_name },					\
	.b_file_name = { __FILE__ },					\
	.dw_endian32 = 0x76543210U,					\
	.dw_msb_value = (msb),						\
	.dw_lsb_value = (lsb),						\
	.dw_file_line = __LINE__					\
}

#endif				/* _SYSINIT_H_ */
