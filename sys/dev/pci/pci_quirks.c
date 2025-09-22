/*	$OpenBSD: pci_quirks.c,v 1.7 2015/07/20 18:05:04 miod Exp $	*/
/*	$NetBSD: pci_quirks.c,v 1.1 1998/05/31 06:03:44 cgd Exp $	*/

/*
 * Copyright (c) 1998 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI Quirk data table and lookup function.
 */

#include <sys/param.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

static const struct pci_quirkdata pci_quirks[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82371FB_ISA,
	    PCI_QUIRK_MULTIFUNCTION },
	{ PCI_VENDOR_CIRRUS, PCI_PRODUCT_CIRRUS_CL_PD6729,
	    PCI_QUIRK_MONOFUNCTION }
};

const struct pci_quirkdata *
pci_lookup_quirkdata(pci_vendor_id_t vendor, pci_product_id_t product)
{
	int i;

	for (i = 0; i < (sizeof pci_quirks / sizeof pci_quirks[0]); i++)
		if (vendor == pci_quirks[i].vendor &&
		    product == pci_quirks[i].product)
			return (&pci_quirks[i]);
	return (NULL);
}
