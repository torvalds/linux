/*	$OpenBSD: apecs_pci.c,v 1.13 2025/06/28 16:04:09 miod Exp $	*/
/*	$NetBSD: apecs_pci.c,v 1.10 1996/11/13 21:13:25 cgd Exp $	*/

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>	/* badaddr() proto */

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/apecsreg.h>
#include <alpha/pci/apecsvar.h>

void		apecs_attach_hook(struct device *, struct device *,
		    struct pcibus_attach_args *);
int		apecs_bus_maxdevs(void *, int);
pcitag_t	apecs_make_tag(void *, int, int, int);
void		apecs_decompose_tag(void *, pcitag_t, int *, int *,
		    int *);
int		apecs_conf_size(void *, pcitag_t);
pcireg_t	apecs_conf_read(void *, pcitag_t, int);
void		apecs_conf_write(void *, pcitag_t, int, pcireg_t);

void
apecs_pci_init(pci_chipset_tag_t pc, void *v)
{

	pc->pc_conf_v = v;
	pc->pc_attach_hook = apecs_attach_hook;
	pc->pc_bus_maxdevs = apecs_bus_maxdevs;
	pc->pc_make_tag = apecs_make_tag;
	pc->pc_decompose_tag = apecs_decompose_tag;
	pc->pc_conf_size = apecs_conf_size;
	pc->pc_conf_read = apecs_conf_read;
	pc->pc_conf_write = apecs_conf_write;
}

void
apecs_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
apecs_bus_maxdevs(void *cpv, int busno)
{

	return 32;
}

pcitag_t
apecs_make_tag(void *cpv, int b, int d, int f)
{

	return (b << 16) | (d << 11) | (f << 8);
}

void
apecs_decompose_tag(void *cpv, pcitag_t tag, int *bp, int *dp, int *fp)
{

	if (bp != NULL)
		*bp = (tag >> 16) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 11) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 8) & 0x7;
}

int
apecs_conf_size(void *cpv, pcitag_t tag)
{
	return PCI_CONFIG_SPACE_SIZE;
}

pcireg_t
apecs_conf_read(void *cpv, pcitag_t tag, int offset)
{
	struct apecs_config *acp = cpv;
	pcireg_t *datap, data;
	int s, secondary, ba;
	int32_t old_haxr2;					/* XXX */

	s = 0;					/* XXX gcc -Wuninitialized */
	old_haxr2 = 0;				/* XXX gcc -Wuninitialized */

	/* secondary if bus # != 0 */
	pci_decompose_tag(&acp->ac_pc, tag, &secondary, NULL, NULL);
	if (secondary) {
		s = splhigh();
		old_haxr2 = REGVAL(EPIC_HAXR2);
		alpha_mb();
		REGVAL(EPIC_HAXR2) = old_haxr2 | 0x1;
		alpha_mb();
	}

	datap = (pcireg_t *)ALPHA_PHYS_TO_K0SEG(APECS_PCI_CONF |
	    tag << 5UL |					/* XXX */
	    (offset & ~0x03) << 5 |				/* XXX */
	    0 << 5 |						/* XXX */
	    0x3 << 3);						/* XXX */
	data = (pcireg_t)-1;
	if (!(ba = badaddr(datap, sizeof *datap)))
		data = *datap;

	if (secondary) {
		alpha_mb();
		REGVAL(EPIC_HAXR2) = old_haxr2;
		alpha_mb();
		splx(s);
	}

#if 0
	printf("apecs_conf_read: tag 0x%lx, reg 0x%lx -> %x @ %p%s\n", tag, reg,
	    data, datap, ba ? " (badaddr)" : "");
#endif

	return data;
}

void
apecs_conf_write(void *cpv, pcitag_t tag, int offset, pcireg_t data)
{
	struct apecs_config *acp = cpv;
	pcireg_t *datap;
	int s, secondary;
	int32_t old_haxr2;					/* XXX */

	s = 0;					/* XXX gcc -Wuninitialized */
	old_haxr2 = 0;				/* XXX gcc -Wuninitialized */

	/* secondary if bus # != 0 */
	pci_decompose_tag(&acp->ac_pc, tag, &secondary, NULL, NULL);
	if (secondary) {
		s = splhigh();
		old_haxr2 = REGVAL(EPIC_HAXR2);
		alpha_mb();
		REGVAL(EPIC_HAXR2) = old_haxr2 | 0x1;
		alpha_mb();
	}

	datap = (pcireg_t *)ALPHA_PHYS_TO_K0SEG(APECS_PCI_CONF |
	    tag << 5UL |					/* XXX */
	    (offset & ~0x03) << 5 |				/* XXX */
	    0 << 5 |						/* XXX */
	    0x3 << 3);						/* XXX */

	alpha_mb();
	*datap = data;
	alpha_mb();
	alpha_mb();

	if (secondary) {
		alpha_mb();
		REGVAL(EPIC_HAXR2) = old_haxr2;	
		alpha_mb();
		splx(s);
	}

#if 0
	printf("apecs_conf_write: tag 0x%lx, reg 0x%lx -> 0x%x @ %p\n", tag,
	    reg, data, datap);
#endif
}
