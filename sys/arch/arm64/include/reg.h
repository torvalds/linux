/* $OpenBSD: reg.h,v 1.3 2017/04/11 06:52:13 kettenis Exp $ */
/*
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACHINE_REG_H_
#define _MACHINE_REG_H_

struct reg {
	uint64_t	r_reg[30];
	uint64_t	r_lr;
	uint64_t	r_sp;
	uint64_t	r_pc;
	uint64_t	r_spsr;
	uint64_t	r_tpidr;
};

struct fpreg {
	__uint128_t	fp_reg[32];
	uint32_t	fp_sr;
	uint32_t	fp_cr;
};

#endif /* !_MACHINE_REG_H_ */
