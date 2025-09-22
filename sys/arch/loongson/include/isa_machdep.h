/*	$OpenBSD: isa_machdep.h,v 1.2 2010/05/08 21:59:56 miod Exp $	*/

/*
 * Copyright (c) 2007 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_MACHINE_ISA_MACHDEP_H_
#define	_MACHINE_ISA_MACHDEP_H_

#include <machine/bus.h>

#define	__NO_ISA_INTR_CHECK

typedef	struct mips_isa_chipset *isa_chipset_tag_t;

struct mips_isa_chipset {
	void	*ic_v;

	void	(*ic_attach_hook)(struct device *, struct device *,
		    struct isabus_attach_args *);
	void	*(*ic_intr_establish)(void *, int, int, int, int (*)(void *),
		    void *, char *);
	void	(*ic_intr_disestablish)(void *, void *);
};

#define	isa_attach_hook(p, s, iba)					\
    (*(iba)->iba_ic->ic_attach_hook)((p), (s), (iba))
#define	isa_intr_establish(c, i, t, l, f, a, n)				\
    (*(c)->ic_intr_establish)((c)->ic_v, (i), (t), (l), (f), (a), (n))
#define	isa_intr_disestablish(c, h)					\
    (*(c)->ic_intr_disestablish)((c)->ic_v, (h))

void	loongson_generic_isa_attach_hook(struct device *, struct device *,
	    struct isabus_attach_args *);
void	loongson_isa_specific_eoi(int);
void	loongson_set_isa_imr(uint);

#endif
