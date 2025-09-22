/*	$OpenBSD: riscv_cpu_intc.h,v 1.4 2024/05/22 05:51:49 jsg Exp $	*/

/*
 * Copyright (c) 2020 Mars Li <mengshi.li.mars@gmai..com>
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

#ifndef _RISCV_CPU_INTC_H_
#define _RISCV_CPU_INTC_H_

void	*riscv_intc_intr_establish(int, int, int (*func)(void *),
		void *, char *);
void	riscv_intc_intr_disestablish(void *cookie);

#endif /* _RISCV_CPU_INTC_H_ */

