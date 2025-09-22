/*	$OpenBSD: patterns.c,v 1.19 2025/05/20 10:30:41 tobhe Exp $	*/

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/types.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/pcidevs_data.h>

#include "amdgpu_devlist.h"
#include "i915_devlist.h"
#include "radeon_devlist.h"

#include <stdio.h>

#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))

const char *
pci_findvendor(pci_vendor_id_t vendor)
{
	const struct pci_known_vendor *kdp = pci_known_vendors;

        while (kdp->vendorname != NULL) {	/* all have vendor name */
                if (kdp->vendor == vendor)
                        break;
		kdp++;
	}
        return (kdp->vendorname);
}

const char *
pci_findproduct(pci_vendor_id_t vendor, pci_product_id_t product)
{
	const struct pci_known_product *pkp = pci_known_products;

	while (pkp->productname != NULL) {	/* all have product name */
		if (pkp->vendor == vendor && pkp->product == product)
			break;
		pkp++;
	}
	return (pkp->productname);
}

void
print_devices(char driver[], const struct pci_matchid devices[], int items)
{
	const char *v, *p;
	int i;

	for (i = 0; i < items; i++) {
		v = pci_findvendor(devices[i].pm_vid);
		p = pci_findproduct(devices[i].pm_vid,
		    devices[i].pm_pid);
		if ( v && p )
		    printf("%s \"%s %s\"\n", driver, v ? v : "", p ? p : "");
	}
}

int
main(void)
{
	printf("%s\n", "acx");
	printf("%s\n", "amd");
	printf("%s\n", "amd ^cpu0:* AMD");
	printf("%s\n", "amdgpu");
	print_devices("amdgpu", amdgpu_devices, nitems(amdgpu_devices));
	printf("%s\n", "amdgpu ^vga*vendor \"ATI\", unknown product"); 
	printf("%s\n", "amdgpu ^vendor \"ATI\", unknown product*class display");
	printf("%s\n", "amdsev ^\"AMD*Crypto\"");
	printf("%s\n", "amdsev ^\"AMD*PSP\"");
	printf("%s\n", "amdsev ^ccp0");
	printf("%s\n", "amdsev ^psp0");
	printf("%s\n", "apple-boot ^cpu0*Apple");
	printf("%s\n", "arm64-qcom-dtb ^qcgpio0");
	printf("%s\n", "athn");
	printf("%s\n", "bwfm");
	printf("%s\n", "bwi");
	printf("%s\n", "ice");
	printf("%s\n", "intel");
	printf("%s\n", "intel ^cpu0:*Intel");
	printf("%s\n", "inteldrm");
	print_devices("inteldrm", i915_devices, nitems(i915_devices));
	printf("%s\n", "ipw");
	printf("%s\n", "iwi");
	printf("%s\n", "iwm");
	printf("%s\n", "iwn");
	printf("%s\n", "iwx");
	printf("%s\n", "malo");
	printf("%s\n", "mtw");
	printf("%s\n", "mwx");
	printf("%s\n", "ogx");
	printf("%s\n", "otus");
	printf("%s\n", "pgt");
	printf("%s\n", "qcpas");
	printf("%s\n", "qcpas ^ppb0*\"Qualcomm");
	printf("%s\n", "qcpas ^cpu0*\"Qualcomm");
	printf("%s\n", "qwx");
	printf("%s\n", "qwz");
	printf("%s\n", "radeondrm");
	print_devices("radeondrm", radeon_devices, nitems(radeon_devices));
	printf("%s\n", "rsu");
	printf("%s\n", "uath");
	printf("%s\n", "upgt");
	printf("%s\n", "uvideo");
	printf("%s\n", "vmm");
	printf("%s\n", "wpi");

	return 0;
}
