/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Simon Burge for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MALTA_YAMON_H_
#define _MALTA_YAMON_H_

#define YAMON_FUNCTION_BASE	0x1fc00500ul

#define YAMON_PRINT_COUNT_OFS	(YAMON_FUNCTION_BASE + 0x04)
#define YAMON_EXIT_OFS		(YAMON_FUNCTION_BASE + 0x20)
#define YAMON_FLUSH_CACHE_OFS	(YAMON_FUNCTION_BASE + 0x2c)
#define YAMON_PRINT_OFS		(YAMON_FUNCTION_BASE + 0x34)
#define YAMON_REG_CPU_ISR_OFS	(YAMON_FUNCTION_BASE + 0x38)
#define YAMON_DEREG_CPU_ISR_OFS	(YAMON_FUNCTION_BASE + 0x3c)
#define YAMON_REG_IC_ISR_OFS	(YAMON_FUNCTION_BASE + 0x40)
#define YAMON_DEREG_IC_ISR_OFS	(YAMON_FUNCTION_BASE + 0x44)
#define YAMON_REG_ESR_OFS	(YAMON_FUNCTION_BASE + 0x48)
#define YAMON_DEREG_ESR_OFS	(YAMON_FUNCTION_BASE + 0x4c)
#define YAMON_GETCHAR_OFS	(YAMON_FUNCTION_BASE + 0x50)
#define YAMON_SYSCON_READ_OFS	(YAMON_FUNCTION_BASE + 0x54)

#define YAMON_FUNC(ofs)		((long)(*(int32_t *)(MIPS_PHYS_TO_KSEG0(ofs))))

typedef void (*t_yamon_print_count)(uint32_t port, char *s, uint32_t count);
#define YAMON_PRINT_COUNT(s, count) \
	((t_yamon_print_count)(YAMON_FUNC(YAMON_PRINT_COUNT_OFS)))(0, s, count)

typedef void (*t_yamon_exit)(uint32_t rc);
#define YAMON_EXIT(rc) ((t_yamon_exit)(YAMON_FUNC(YAMON_EXIT_OFS)))(rc)

typedef void (*t_yamon_print)(uint32_t port, const char *s);
#define YAMON_PRINT(s) ((t_yamon_print)(YAMON_FUNC(YAMON_PRINT_OFS)))(0, s)

typedef int (*t_yamon_getchar)(uint32_t port, char *ch);
#define YAMON_GETCHAR(ch) \
	((t_yamon_getchar)(YAMON_FUNC(YAMON_GETCHAR_OFS)))(0, ch)

typedef int t_yamon_syscon_id;
typedef int (*t_yamon_syscon_read)(t_yamon_syscon_id id, void *param,
				   uint32_t size);
#define YAMON_SYSCON_READ(id, param, size)				\
	((t_yamon_syscon_read)(YAMON_FUNC(YAMON_SYSCON_READ_OFS)))	\
	(id, param, size)

typedef struct {
	char *name;
	char *value;
} yamon_env_t;

#define SYSCON_BOARD_CPU_CLOCK_FREQ_ID	34	/* UINT32 */
#define SYSCON_BOARD_BUS_CLOCK_FREQ_ID	35	/* UINT32 */
#define SYSCON_BOARD_PCI_FREQ_KHZ_ID	36	/* UINT32 */

char*		yamon_getenv(char *name);
uint32_t	yamon_getcpufreq(void);

extern yamon_env_t *fenvp[];

#endif /* _MALTA_YAMON_H_ */
