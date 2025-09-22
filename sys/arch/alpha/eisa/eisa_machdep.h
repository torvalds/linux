/*	$OpenBSD: eisa_machdep.h,v 1.8 2009/08/22 02:54:50 mk Exp $	*/
/*	$NetBSD: eisa_machdep.h,v 1.1 1996/04/12 05:39:51 cgd Exp $	*/

/*
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Types provided to machine-independent EISA code.
 */
typedef struct alpha_eisa_chipset *eisa_chipset_tag_t;
typedef int eisa_intr_handle_t;

struct alpha_eisa_chipset {
	void	*ec_v;
	int	ec_maxslots;

	void	(*ec_attach_hook)(struct device *, struct device *,
		    struct eisabus_attach_args *);
	int	(*ec_intr_map)(void *, u_int,
		    eisa_intr_handle_t *);
	const char *(*ec_intr_string)(void *, eisa_intr_handle_t);
	void	*(*ec_intr_establish)(void *, eisa_intr_handle_t,
		    int, int, int (*)(void *), void *, const char *);
	void	(*ec_intr_disestablish)(void *, void *);
};

/*
 * Functions provided to machine-independent EISA code.
 */
#define	eisa_attach_hook(p, s, a)					\
    (*(a)->eba_ec->ec_attach_hook)((p), (s), (a))
#define	eisa_maxslots(c)						\
    ((c)->ec_maxslots)
#define	eisa_intr_map(c, i, hp)						\
    (*(c)->ec_intr_map)((c)->ec_v, (i), (hp))
#define	eisa_intr_string(c, h)						\
    (*(c)->ec_intr_string)((c)->ec_v, (h))
#define	eisa_intr_establish(c, h, t, l, f, a, nm)			\
    (*(c)->ec_intr_establish)((c)->ec_v, (h), (t), (l), (f), (a), (nm))
#define	eisa_intr_disestablish(c, h)					\
    (*(c)->ec_intr_disestablish)((c)->ec_v, (h))

/*
 * Internal functions, NOT TO BE USED BY MACHINE-INDEPENDENT CODE!
 */

void	eisa_init(eisa_chipset_tag_t);
