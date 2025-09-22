/*	$OpenBSD: eisa_machdep.h,v 1.4 2011/03/23 16:54:35 pirofti Exp $	*/

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

#ifndef _MACHINE_EISA_MACHDEP_H_
#define _MACHINE_EISA_MACHDEP_H_

/*
 * Types provided to machine-independent EISA code.
 */
typedef struct hppa_eisa_chipset *eisa_chipset_tag_t;
typedef int eisa_intr_handle_t;

struct hppa_eisa_chipset {
	void	*ec_v;

	void	(*ec_attach_hook)(struct device *, struct device *,
				  struct eisabus_attach_args *);
	int	(*ec_intr_map)(void *, u_int, int *);
	const char *(*ec_intr_string)(void *, int);
	void	*(*ec_intr_establish)(void *, int, int, int,
				      int (*)(void *), void *, const char *);
	void	(*ec_intr_disestablish)(void *, void *);
};

#if 0
#define	EISA_SLOTOFF_PRIMING	EISA_SLOTOFF_VID
#define	EISA_PRIMING_VID(i)	({ (void)(i), 0xff; })
#define	EISA_PRIMING_PID(i)	({ (void)(i), 0x00; })
#endif

/*
 * Functions provided to machine-independent EISA code.
 */
#define	eisa_attach_hook(p, s, a)					\
    (*(a)->eba_ec->ec_attach_hook)((p), (s), (a))
#define	eisa_maxslots(c)	8
#define	eisa_intr_map(c, i, hp)						\
    (*(c)->ec_intr_map)((c)->ec_v, (i), (hp))
#define	eisa_intr_string(c, h)						\
    (*(c)->ec_intr_string)((c)->ec_v, (h))
#define	eisa_intr_establish(c, h, t, l, f, a, nm)			\
    (*(c)->ec_intr_establish)((c)->ec_v, (h), (t), (l), (f), (a), (nm))
#define	eisa_intr_disestablish(c, h)					\
    (*(c)->ec_intr_disestablish)((c)->ec_v, (h))

#endif /* _MACHINE_EISA_MACHDEP_H_ */
