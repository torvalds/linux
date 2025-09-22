/* $OpenBSD: cia_pci.c,v 1.14 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: cia_pci.c,v 1.25 2000/06/29 08:58:46 mrg Exp $ */

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

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/ciareg.h>
#include <alpha/pci/ciavar.h>

void		cia_attach_hook(struct device *, struct device *,
		    struct pcibus_attach_args *);
int		cia_bus_maxdevs(void *, int);
pcitag_t	cia_make_tag(void *, int, int, int);
void		cia_decompose_tag(void *, pcitag_t, int *, int *,
		    int *);
int		cia_conf_size(void *, pcitag_t);
pcireg_t	cia_conf_read(void *, pcitag_t, int);
void		cia_conf_write(void *, pcitag_t, int, pcireg_t);

void
cia_pci_init(pci_chipset_tag_t pc, void *v)
{

	pc->pc_conf_v = v;
	pc->pc_attach_hook = cia_attach_hook;
	pc->pc_bus_maxdevs = cia_bus_maxdevs;
	pc->pc_make_tag = cia_make_tag;
	pc->pc_decompose_tag = cia_decompose_tag;
	pc->pc_conf_size = cia_conf_size;
	pc->pc_conf_read = cia_conf_read;
	pc->pc_conf_write = cia_conf_write;
}

void
cia_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
cia_bus_maxdevs(void *cpv, int busno)
{

	return 32;
}

pcitag_t
cia_make_tag(void *cpv, int b, int d, int f)
{

	return (b << 16) | (d << 11) | (f << 8);
}

void
cia_decompose_tag(void *cpv, pcitag_t tag, int *bp, int *dp, int *fp)
{

	if (bp != NULL)
		*bp = (tag >> 16) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 11) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 8) & 0x7;
}

int
cia_conf_size(void *cpv, pcitag_t tag)
{
	return PCI_CONFIG_SPACE_SIZE;
}

pcireg_t
cia_conf_read(void *cpv, pcitag_t tag, int offset)
{
	struct cia_config *ccp = cpv;
	pcireg_t *datap, data;
	int s, secondary, ba;
	u_int32_t old_cfg, errbits;

#ifdef __GNUC__
	s = 0;					/* XXX gcc -Wuninitialized */
	old_cfg = 0;				/* XXX gcc -Wuninitialized */
#endif

	/*
	 * Some (apparently common) revisions of EB164 and AlphaStation
	 * firmware do the Wrong thing with PCI master and target aborts,
	 * which are caused by accessing the configuration space of devices
	 * that don't exist (for example).
	 *
	 * To work around this, we clear the CIA error register's PCI
	 * master and target abort bits before touching PCI configuration
	 * space and check it afterwards.  If it indicates a master or target
	 * abort, the device wasn't there so we return 0xffffffff.
	 */
	REGVAL(CIA_CSR_CIA_ERR) = CIA_ERR_RCVD_MAS_ABT|CIA_ERR_RCVD_TAR_ABT;
	alpha_mb();
	alpha_pal_draina();	

	/* secondary if bus # != 0 */
	pci_decompose_tag(&ccp->cc_pc, tag, &secondary, NULL, NULL);
	if (secondary) {
		s = splhigh();
		old_cfg = REGVAL(CIA_CSR_CFG);
		alpha_mb();
		REGVAL(CIA_CSR_CFG) = old_cfg | 0x1;
		alpha_mb();
	}

	/*
	 * We just inline the BWX support, since this is the only
	 * difference between BWX and swiz for config space.
	 */
	if (ccp->cc_flags & CCF_PCI_USE_BWX) {
		if (secondary) {
			datap =
			    (pcireg_t *)ALPHA_PHYS_TO_K0SEG(CIA_EV56_BWCONF1 |
				tag | (offset & ~0x03));
		} else {
			datap =
			    (pcireg_t *)ALPHA_PHYS_TO_K0SEG(CIA_EV56_BWCONF0 |
				tag | (offset & ~0x03));
		}
	} else {
		datap = (pcireg_t *)ALPHA_PHYS_TO_K0SEG(CIA_PCI_CONF |
		    tag << 5UL |				/* XXX */
		    (offset & ~0x03) << 5 |			/* XXX */
		    0 << 5 |					/* XXX */
		    0x3 << 3);					/* XXX */
	}
	data = (pcireg_t)-1;
	alpha_mb();
	if (!(ba = badaddr(datap, sizeof *datap)))
		data = *datap;
	alpha_mb();
	alpha_mb();

	if (secondary) {
		alpha_mb();
		REGVAL(CIA_CSR_CFG) = old_cfg;
		alpha_mb();
		splx(s);
	}

	alpha_pal_draina();	
	alpha_mb();
	errbits = REGVAL(CIA_CSR_CIA_ERR);
	if (errbits & (CIA_ERR_RCVD_MAS_ABT|CIA_ERR_RCVD_TAR_ABT)) {
		ba = 1;
		data = 0xffffffff;
	}

	if (errbits) {
		REGVAL(CIA_CSR_CIA_ERR) = errbits;
		alpha_mb();
		alpha_pal_draina();
	}

#if 0
	printf("cia_conf_read: tag 0x%lx, reg 0x%lx -> %x @ %p%s\n", tag, reg,
	    data, datap, ba ? " (badaddr)" : "");
#endif

	return data;
}

void
cia_conf_write(void *cpv, pcitag_t tag, int offset, pcireg_t data)
{
	struct cia_config *ccp = cpv;
	pcireg_t *datap;
	int s, secondary;
	u_int32_t old_cfg;

#ifdef __GNUC__
	s = 0;					/* XXX gcc -Wuninitialized */
	old_cfg = 0;				/* XXX gcc -Wuninitialized */
#endif

	/* secondary if bus # != 0 */
	pci_decompose_tag(&ccp->cc_pc, tag, &secondary, NULL, NULL);
	if (secondary) {
		s = splhigh();
		old_cfg = REGVAL(CIA_CSR_CFG);
		alpha_mb();
		REGVAL(CIA_CSR_CFG) = old_cfg | 0x1;
		alpha_mb();
	}

	/*
	 * We just inline the BWX support, since this is the only
	 * difference between BWX and swiz for config space.
	 */
	if (ccp->cc_flags & CCF_PCI_USE_BWX) {
		if (secondary) {
			datap =
			    (pcireg_t *)ALPHA_PHYS_TO_K0SEG(CIA_EV56_BWCONF1 |
				tag | (offset & ~0x03));
		} else {
			datap =
			    (pcireg_t *)ALPHA_PHYS_TO_K0SEG(CIA_EV56_BWCONF0 |
				tag | (offset & ~0x03));
		}
	} else {
		datap = (pcireg_t *)ALPHA_PHYS_TO_K0SEG(CIA_PCI_CONF |
		    tag << 5UL |				/* XXX */
		    (offset & ~0x03) << 5 |			/* XXX */
		    0 << 5 |					/* XXX */
		    0x3 << 3);					/* XXX */
	}
	alpha_mb();
	*datap = data;
	alpha_mb();
	alpha_mb();

	if (secondary) {
		alpha_mb();
		REGVAL(CIA_CSR_CFG) = old_cfg;
		alpha_mb();
		splx(s);
	}

#if 0
	printf("cia_conf_write: tag 0x%lx, reg 0x%lx -> 0x%x @ %p\n", tag,
	    reg, data, datap);
#endif
}
