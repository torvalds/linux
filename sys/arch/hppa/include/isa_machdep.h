/*	$OpenBSD: isa_machdep.h,v 1.4 2011/03/23 16:54:35 pirofti Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_ISA_MACHDEP_H_
#define _MACHINE_ISA_MACHDEP_H_
/*
 * Types provided to machine-independent ISA code.
 */
typedef struct hppa_isa_chipset *isa_chipset_tag_t;

struct hppa_isa_chipset {
	void	*ic_v;

	void	(*ic_attach_hook)(struct device *, struct device *,
		    struct isabus_attach_args *);
	void	*(*ic_intr_establish)(void *, int, int, int,
		    int (*)(void *), void *, const char *);
	void	(*ic_intr_disestablish)(void *, void *);
	int	(*ic_intr_check)(void *, int, int);
};

/*
 * Functions provided to machine-independent ISA code.
 */
#define	isa_attach_hook(p, s, a)					\
    (*(a)->iba_ic->ic_attach_hook)((p), (s), (a))
#define	isa_intr_establish(c, i, t, l, f, a, nm)			\
    (*(c)->ic_intr_establish)((c)->ic_v, (i), (t), (l), (f), (a), (nm))
#define	isa_intr_disestablish(c, h)					\
    (*(c)->ic_intr_disestablish)((c)->ic_v, (h))
#define isa_intr_check(c, i, t)						\
    (*(c)->ic_intr_check)((c)->ic_v, (i), (t))

#endif /* _MACHINE_ISA_MACHDEP_H_ */
