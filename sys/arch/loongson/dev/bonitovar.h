/*	$OpenBSD: bonitovar.h,v 1.6 2014/03/27 22:16:03 miod Exp $	*/
/*	$NetBSD: bonitovar.h,v 1.4 2008/04/28 20:23:28 martin Exp $	*/

/*-
 * Copyright (c) 2001, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LOONGSON_DEV_BONITOVAR_H_
#define	_LOONGSON_DEV_BONITOVAR_H_

struct bonito_cfg_hook;
struct extent;

struct bonito_config {
	int		bc_adbase;	/* AD line base for config access */

	/* Prototype interrupt and GPIO registers. */
	uint32_t	bc_gpioIE;
	uint32_t	bc_intEdge;
	uint32_t	bc_intSteer;
	uint32_t	bc_intPol;

	/* PCI Attach hook for the first bus */
	void		(*bc_attach_hook)(pci_chipset_tag_t);
	
	/* PCI Interrupt Assignment for the first bus */
	int		(*bc_intr_map)(int, int, int);
};

struct bonito_softc {
	struct device			 sc_dev;
	int				 sc_compatible;	/* real Bonito hw */
	const struct bonito_config	*sc_bonito;
	struct mips_pci_chipset		 sc_pc;

	/* PCI Configuration Space access hooks */
	SLIST_HEAD(, bonito_cfg_hook)	 sc_hook;
};

#ifdef _KERNEL
void	 bonito_intr_disestablish(void *);
void	*bonito_intr_establish(int, int, int, int (*)(void *), void *,
	    const char *);
int	 bonito_pci_hook(pci_chipset_tag_t, void *,
	    int (*)(void *, pci_chipset_tag_t, pcitag_t, int, pcireg_t *),
	    int (*)(void *, pci_chipset_tag_t, pcitag_t, int, pcireg_t));
int	 bonito_print(void *, const char *);
struct extent
	*bonito_get_resource_extent(pci_chipset_tag_t, int);
void	 bonito_setintrmask(int);

void	 bonito_early_setup(void);
#endif /* _KERNEL */

#endif /* _LOONGSON_DEV_BONITOVAR_H_ */
