/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
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

#ifndef	__UTILS_H
#define	__UTILS_H

struct cmd_param
{
	char	name[64];
	char	value[64];
};

char *param_get_string(struct cmd_param *, const char *);
int param_get_int(struct cmd_param *, const char *);
int param_get_intx(struct cmd_param *, const char *);
int param_get_boolean(struct cmd_param *, const char *);
int param_has_value(struct cmd_param *, const char *);
int param_get_count(struct cmd_param *);
void perrorf(const char *, ...);
void hexdumpoffset(uint8_t *, int, int);
void hexdump(uint8_t *, int);
void *xmalloc(size_t);

/* Command handlers */
int nand_read(struct cmd_param *);
int nand_write(struct cmd_param *);
int nand_read_oob(struct cmd_param *);
int nand_write_oob(struct cmd_param *);
int nand_erase(struct cmd_param *);
int nand_info(struct cmd_param *);

#endif	/* __UTILS_H */
