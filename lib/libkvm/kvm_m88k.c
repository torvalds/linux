/*	$OpenBSD: kvm_m88k.c,v 1.8 2021/12/01 21:45:19 deraadt Exp $	*/
/*	$NetBSD: kvm_alpha.c,v 1.2 1995/09/29 03:57:48 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
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

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <nlist.h>
#include <kvm.h>

#include <uvm/uvm_extern.h>

#include <limits.h>
#include <db.h>

#include "kvm_private.h"

void
_kvm_freevtop(kvm_t *kd)
{
	free(kd->vmst);
	kd->vmst = NULL;
}

int
_kvm_initvtop(kvm_t *kd)
{

	return (0);
}

int
_kvm_kvatop(kvm_t *kd, u_long va, paddr_t *pa)
{

	/* don't forget k0seg translations! */
	_kvm_err(kd, 0, "vatop not yet implemented!");
	return (0);
}

off_t
_kvm_pa2off(kvm_t *kd, paddr_t pa)
{
	_kvm_err(kd, 0, "pa2off not yet implemented!");
	return (0);
}
