/*	$OpenBSD: pciidevar.h,v 1.22 2024/05/13 01:15:51 jsg Exp $	*/
/*	$NetBSD: pciidevar.h,v 1.6 2001/01/12 16:04:00 bouyer Exp $	*/

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

#ifndef _DEV_PCI_PCIIDEVAR_H_
#define _DEV_PCI_PCIIDEVAR_H_

/*
 * PCI IDE driver exported software structures.
 *
 * Author: Christopher G. Demetriou, March 2, 1998.
 */

#include <dev/ata/atavar.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcvar.h>

/*
 * While standard PCI IDE controllers only have 2 channels, it is
 * common for PCI SATA controllers to have more.  Here we define
 * the maximum number of channels that any one PCI IDE device can
 * have.
 */
#define PCIIDE_MAX_CHANNELS	4

struct pciide_softc {
	struct wdc_softc	sc_wdcdev;	/* common wdc definitions */
	pci_chipset_tag_t	sc_pc;		/* PCI registers info */
	pcitag_t		sc_tag;
	void			*sc_pci_ih;	/* PCI interrupt handle */
	int			sc_dma_ok;	/* bus-master DMA info */
	bus_space_tag_t		sc_dma_iot;
	bus_space_handle_t	sc_dma_ioh;
	bus_size_t		sc_dma_iosz;
	bus_dma_tag_t		sc_dmat;

	/*
	 * Some controllers might have DMA restrictions other than
	 * the norm.
	 */
	bus_size_t		sc_dma_maxsegsz;
	bus_size_t		sc_dma_boundary;

	/*
	 * Used as a register save space by pciide_activate()
	 * 
	 * sc_save[] is for the 6 pci regs starting at PCI_MAPREG_END + 0x18 --
	 * most IDE chipsets need a subset of those saved.  sc_save2 is for
	 * up to 6 other registers, which specific chips might need saved.
	 */
	pcireg_t		sc_save[6];
	pcireg_t		sc_save2[6];

	/* Chip description */
	const struct pciide_product_desc *sc_pp;
	/* unmap/detach */
	void (*chip_unmap)(struct pciide_softc *, int);
	/* Chip revision */
	int sc_rev;
	/* common definitions */
	struct channel_softc *wdc_chanarray[PCIIDE_MAX_CHANNELS];
	/* internal bookkeeping */
	struct pciide_channel {			/* per-channel data */
		struct channel_softc wdc_channel; /* generic part */
		const char	*name;
		int		hw_ok;		/* hardware mapped & OK? */
		int		compat;		/* is it compat? */
		int             dma_in_progress;
		void		*ih;		/* compat or pci handle */
		bus_space_handle_t ctl_baseioh;	/* ctrl regs blk, native mode */
		/* DMA tables and DMA map for xfer, for each drive */
		struct pciide_dma_maps {
			bus_dmamap_t    dmamap_table;
			struct idedma_table *dma_table;
			bus_dmamap_t    dmamap_xfer;
			int dma_flags;
		} dma_maps[2];
		/*
		 * Some controllers require certain bits to
		 * always be set for proper operation of the
		 * controller.  Set those bits here, if they're
		 * required.
		 */
		uint8_t		idedma_cmd;
	} pciide_channels[PCIIDE_MAX_CHANNELS];

	/* Chip-specific private data */
	void *sc_cookie;
	size_t sc_cookielen;

	/* DMA registers access functions */
	u_int8_t (*sc_dmacmd_read)(struct pciide_softc *, int);
	void (*sc_dmacmd_write)(struct pciide_softc *, int, u_int8_t);
	u_int8_t (*sc_dmactl_read)(struct pciide_softc *, int);
	void (*sc_dmactl_write)(struct pciide_softc *, int, u_int8_t);
	void (*sc_dmatbl_write)(struct pciide_softc *, int, u_int32_t);
};

#define PCIIDE_DMACMD_READ(sc, chan) \
	(sc)->sc_dmacmd_read((sc), (chan))
#define PCIIDE_DMACMD_WRITE(sc, chan, val) \
	(sc)->sc_dmacmd_write((sc), (chan), (val))
#define PCIIDE_DMACTL_READ(sc, chan) \
	(sc)->sc_dmactl_read((sc), (chan))
#define PCIIDE_DMACTL_WRITE(sc, chan, val) \
	(sc)->sc_dmactl_write((sc), (chan), (val))
#define PCIIDE_DMATBL_WRITE(sc, chan, val) \
	(sc)->sc_dmatbl_write((sc), (chan), (val))

int	pciide_mapregs_compat( struct pci_attach_args *,
	    struct pciide_channel *, int, bus_size_t *, bus_size_t *);
int	pciide_mapregs_native(struct pci_attach_args *,
	    struct pciide_channel *, bus_size_t *, bus_size_t *,
	    int (*pci_intr)(void *));
void	pciide_mapreg_dma(struct pciide_softc *,
	    struct pci_attach_args *);
int	pciide_chansetup(struct pciide_softc *, int, pcireg_t);
void	pciide_mapchan(struct pci_attach_args *,
	    struct pciide_channel *, pcireg_t, bus_size_t *, bus_size_t *,
	    int (*pci_intr)(void *));
int	pciide_chan_candisable(struct pciide_channel *);
void	pciide_map_compat_intr( struct pci_attach_args *,
	    struct pciide_channel *, int, int);
void	pciide_unmap_compat_intr( struct pci_attach_args *,
	    struct pciide_channel *, int, int);
int	pciide_compat_intr(void *);
int	pciide_pci_intr(void *);
int	pciide_intr_flag(struct pciide_channel *);

u_int8_t pciide_dmacmd_read(struct pciide_softc *, int);
void	 pciide_dmacmd_write(struct pciide_softc *, int, u_int8_t);
u_int8_t pciide_dmactl_read(struct pciide_softc *, int);
void	 pciide_dmactl_write(struct pciide_softc *, int, u_int8_t);
void	 pciide_dmatbl_write(struct pciide_softc *, int, u_int32_t);

void	 pciide_channel_dma_setup(struct pciide_channel *);
int	 pciide_dma_table_setup(struct pciide_softc *, int, int);
int	 pciide_dma_init(void *, int, int, void *, size_t, int);
void	 pciide_dma_start(void *, int, int);
int	 pciide_dma_finish(void *, int, int, int);
void	 pciide_irqack(struct channel_softc *);
void	 pciide_print_modes(struct pciide_channel *);
void	 pciide_print_channels(int, pcireg_t);

void	 default_chip_unmap(struct pciide_softc *, int);
void	 pciide_unmapreg_dma(struct pciide_softc *);
void	 pciide_chanfree(struct pciide_softc *, int);
void	 pciide_unmap_chan(struct pciide_softc *, struct pciide_channel *, int);
int	 pciide_unmapregs_compat(struct pciide_softc *,
	     struct pciide_channel *);
int	 pciide_unmapregs_native(struct pciide_softc *,
	     struct pciide_channel *);

/*
 * Functions defined by machine-dependent code.
 */

#ifdef __i386__
void gcsc_chip_map(struct pciide_softc *, struct pci_attach_args *);
#endif

/* Attach compat interrupt handler, returning handle or NULL if failed. */
#if !defined(pciide_machdep_compat_intr_establish)
void	*pciide_machdep_compat_intr_establish(struct device *,
	    struct pci_attach_args *, int, int (*)(void *), void *);
void	pciide_machdep_compat_intr_disestablish(pci_chipset_tag_t pc,
	    void *);
#endif

#endif	/* !_DEV_PCI_PCIIDEVAR_H_ */
