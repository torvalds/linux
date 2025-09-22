/*	$OpenBSD: cz.c,v 1.30 2024/11/05 18:58:59 miod Exp $ */
/*	$NetBSD: cz.c,v 1.15 2001/01/20 19:10:36 thorpej Exp $	*/

/*-
 * Copyright (c) 2000 Zembu Labs, Inc.
 * All rights reserved.
 *
 * Authors: Jason R. Thorpe <thorpej@zembu.com>
 *          Bill Studenmund <wrstuden@zembu.com>
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
 *	This product includes software developed by Zembu Labs, Inc.
 * 4. Neither the name of Zembu Labs nor the names of its employees may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ZEMBU LABS, INC. ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAR-
 * RANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DIS-
 * CLAIMED.  IN NO EVENT SHALL ZEMBU LABS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Cyclades-Z series multi-port serial adapter driver for NetBSD.
 *
 * Some notes:
 *
 *	- The Cyclades-Z has fully automatic hardware (and software!)
 *	  flow control.  We only utilize RTS/CTS flow control here,
 *	  and it is implemented in a very simplistic manner.  This
 *	  may be an area of future work.
 *
 *	- The PLX can map the either the board's RAM or host RAM
 *	  into the MIPS's memory window.  This would enable us to
 *	  use less expensive (for us) memory reads/writes to host
 *	  RAM, rather than time-consuming reads/writes to PCI
 *	  memory space.  However, the PLX can only map a 0-128M
 *	  window, so we would have to ensure that the DMA address
 *	  of the host RAM fits there.  This is kind of a pain,
 *	  so we just don't bother right now.
 *
 *	- In a perfect world, we would use the autoconfiguration
 *	  mechanism to attach the TTYs that we find.  However,
 *	  that leads to somewhat icky looking autoconfiguration
 *	  messages (one for every TTY, up to 64 per board!).  So
 *	  we don't do it that way, but assign minors as if there
 *	  were the max of 64 ports per board.
 *
 *	- We don't bother with PPS support here.  There are so many
 *	  ports, each with a large amount of buffer space, that the
 *	  normal mode of operation is to poll the boards regularly
 *	  (generally, every 20ms or so).  This makes this driver
 *	  unsuitable for PPS, as the latency will be generally too
 *	  high.
 */
/*
 * This driver inspired by the FreeBSD driver written by Brian J. McGovern
 * for FreeBSD 3.2.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/time.h>
#include <sys/syslog.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/czreg.h>

#include <dev/pci/plx9060reg.h>
#include <dev/pci/plx9060var.h>

#include <dev/microcode/cyclades/cyzfirm.h>

#define	CZ_DRIVER_VERSION	0x20000411

#define CZ_POLL_MS			20

/* These are the interrupts we always use. */
#define	CZ_INTERRUPTS							\
	(C_IN_MDSR | C_IN_MRI | C_IN_MRTS | C_IN_MCTS | C_IN_TXBEMPTY |	\
	 C_IN_TXFEMPTY | C_IN_TXLOWWM | C_IN_RXHIWM | C_IN_RXNNDT |	\
	 C_IN_MDCD | C_IN_PR_ERROR | C_IN_FR_ERROR | C_IN_OVR_ERROR |	\
	 C_IN_RXOFL | C_IN_IOCTLW | C_IN_RXBRK)

/*
 * cztty_softc:
 *
 *	Per-channel (TTY) state.
 */
struct cztty_softc {
	struct cz_softc *sc_parent;
	struct tty *sc_tty;

	struct timeout sc_diag_to;

	int sc_channel;			/* Also used to flag unattached chan */
#define CZTTY_CHANNEL_DEAD	-1

	bus_space_tag_t sc_chan_st;	/* channel space tag */
	bus_space_handle_t sc_chan_sh;	/* channel space handle */
	bus_space_handle_t sc_buf_sh;	/* buffer space handle */

	u_int sc_overflows,
	      sc_parity_errors,
	      sc_framing_errors,
	      sc_errors;

	int sc_swflags;

	u_int32_t sc_rs_control_dtr,
		  sc_chanctl_hw_flow,
		  sc_chanctl_comm_baud,
		  sc_chanctl_rs_control,
		  sc_chanctl_comm_data_l,
		  sc_chanctl_comm_parity;
};

/*
 * cz_softc:
 *
 *	Per-board state.
 */
struct cz_softc {
	struct device cz_dev;		/* generic device info */
	struct plx9060_config cz_plx;	/* PLX 9060 config info */
	bus_space_tag_t cz_win_st;	/* window space tag */
	bus_space_handle_t cz_win_sh;	/* window space handle */
	struct timeout cz_timeout;	/* timeout for polling-mode */

	void *cz_ih;			/* interrupt handle */

	u_int32_t cz_mailbox0;		/* our MAILBOX0 value */
	int cz_nchannels;		/* number of channels */
	int cz_nopenchan;		/* number of open channels */
	struct cztty_softc *cz_ports;	/* our array of ports */

	bus_addr_t cz_fwctl;		/* offset of firmware control */
};

int	cz_match(struct device *, void *, void *);
void	cz_attach(struct device *, struct device *, void *);
int	cz_wait_pci_doorbell(struct cz_softc *, char *);

const struct cfattach cz_ca = {
	sizeof(struct cz_softc), cz_match, cz_attach
};

void	cz_reset_board(struct cz_softc *);
int	cz_load_firmware(struct cz_softc *);

int	cz_intr(void *);
void	cz_poll(void *);
int	cztty_transmit(struct cztty_softc *, struct tty *);
int	cztty_receive(struct cztty_softc *, struct tty *);

struct	cztty_softc * cztty_getttysoftc(dev_t dev);
int	cztty_findmajor(void);
int	cztty_major;
int	cztty_attached_ttys;

cdev_decl(cztty);

void    czttystart(struct tty *tp);
int	czttyparam(struct tty *tp, struct termios *t);
void    cztty_shutdown(struct cztty_softc *sc);
void	cztty_modem(struct cztty_softc *sc, int onoff);
void	cztty_break(struct cztty_softc *sc, int onoff);
void	tiocm_to_cztty(struct cztty_softc *sc, u_long how, int ttybits);
int	cztty_to_tiocm(struct cztty_softc *sc);
void	cztty_diag(void *arg);

struct cfdriver cz_cd = {
	NULL, "cz", DV_TTY
};

/*
 * Macros to read and write the PLX.
 */
#define	CZ_PLX_READ(cz, reg)						\
	bus_space_read_4((cz)->cz_plx.plx_st, (cz)->cz_plx.plx_sh, (reg))
#define	CZ_PLX_WRITE(cz, reg, val)					\
	bus_space_write_4((cz)->cz_plx.plx_st, (cz)->cz_plx.plx_sh,	\
	    (reg), (val))

/*
 * Macros to read and write the FPGA.  We must already be in the FPGA
 * window for this.
 */
#define	CZ_FPGA_READ(cz, reg)						\
	bus_space_read_4((cz)->cz_win_st, (cz)->cz_win_sh, (reg))
#define	CZ_FPGA_WRITE(cz, reg, val)					\
	bus_space_write_4((cz)->cz_win_st, (cz)->cz_win_sh, (reg), (val))

/*
 * Macros to read and write the firmware control structures in board RAM.
 */
#define	CZ_FWCTL_READ(cz, off)						\
	bus_space_read_4((cz)->cz_win_st, (cz)->cz_win_sh,		\
	    (cz)->cz_fwctl + (off))

#define	CZ_FWCTL_WRITE(cz, off, val)					\
	bus_space_write_4((cz)->cz_win_st, (cz)->cz_win_sh,		\
	    (cz)->cz_fwctl + (off), (val))

/*
 * Convenience macros for cztty routines.  PLX window MUST be to RAM.
 */
#define CZTTY_CHAN_READ(sc, off)					\
	bus_space_read_4((sc)->sc_chan_st, (sc)->sc_chan_sh, (off))

#define CZTTY_CHAN_WRITE(sc, off, val)					\
	bus_space_write_4((sc)->sc_chan_st, (sc)->sc_chan_sh,		\
	    (off), (val))

#define CZTTY_BUF_READ(sc, off)						\
	bus_space_read_4((sc)->sc_chan_st, (sc)->sc_buf_sh, (off))

#define CZTTY_BUF_WRITE(sc, off, val)					\
	bus_space_write_4((sc)->sc_chan_st, (sc)->sc_buf_sh,		\
	    (off), (val))

/*
 * Convenience macros.
 */
#define	CZ_WIN_RAM(cz)							\
do {									\
	CZ_PLX_WRITE((cz), PLX_LAS0BA, LOCAL_ADDR0_RAM);		\
	delay(100);							\
} while (0)

#define	CZ_WIN_FPGA(cz)							\
do {									\
	CZ_PLX_WRITE((cz), PLX_LAS0BA, LOCAL_ADDR0_FPGA);		\
	delay(100);							\
} while (0)

/*****************************************************************************
 * Cyclades-Z controller code starts here...
 *****************************************************************************/

/*
 * cz_match:
 *
 *	Determine if the given PCI device is a Cyclades-Z board.
 */
int
cz_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_CYCLADES &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_CYCLADES_CYCLOMZ_2)
		return (1);
	return (0);
}

/*
 * cz_attach:
 *
 *	A Cyclades-Z board was found; attach it.
 */
void
cz_attach(struct device *parent, struct device *self, void *aux)
{
	struct cz_softc *cz = (void *) self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	struct cztty_softc *sc;
	struct tty *tp;
	int i;

	cz->cz_plx.plx_pc = pa->pa_pc;
	cz->cz_plx.plx_tag = pa->pa_tag;

	if (pci_mapreg_map(pa, PLX_PCI_RUNTIME_MEMADDR,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &cz->cz_plx.plx_st, &cz->cz_plx.plx_sh, NULL, NULL, 0) != 0) {
		printf(": unable to map PLX registers\n");
		return;
	}
	if (pci_mapreg_map(pa, PLX_PCI_LOCAL_ADDR0,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &cz->cz_win_st, &cz->cz_win_sh, NULL, NULL, 0) != 0) {
		printf(": unable to map device window\n");
		return;
	}

	cz->cz_mailbox0 = CZ_PLX_READ(cz, PLX_MAILBOX0);
	cz->cz_nopenchan = 0;

	/*
	 * Make sure that the board is completely stopped.
	 */
	CZ_WIN_FPGA(cz);
	CZ_FPGA_WRITE(cz, FPGA_CPU_STOP, 0);

	/*
	 * Load the board's firmware.
	 */
	if (cz_load_firmware(cz) != 0)
		return;

	/*
	 * Now that we're ready to roll, map and establish the interrupt
	 * handler.
	 */
	if (pci_intr_map(pa, &ih) != 0) {
		/*
		 * The common case is for Cyclades-Z boards to run
		 * in polling mode, and thus not have an interrupt
		 * mapped for them.  Don't bother reporting that
		 * the interrupt is not mappable, since this isn't
		 * really an error.
		 */
		cz->cz_ih = NULL;
		goto polling_mode;
	} else {
		intrstr = pci_intr_string(pa->pa_pc, ih);
		cz->cz_ih = pci_intr_establish(pc, ih, IPL_TTY,
			    cz_intr, cz, cz->cz_dev.dv_xname);
	}
	if (cz->cz_ih == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		/* We will fall-back on polling mode. */
	} else
		printf(": %s\n", intrstr);

 polling_mode:
	if (cz->cz_ih == NULL) {
		timeout_set(&cz->cz_timeout, cz_poll, cz);
		printf("%s: polling mode, %d ms interval\n",
		    cz->cz_dev.dv_xname, CZ_POLL_MS);
	}

	if (cztty_major == 0)
		cztty_major = cztty_findmajor();
	/*
	 * Allocate sufficient pointers for the children and
	 * attach them.  Set all ports to a reasonable initial
	 * configuration while we're at it:
	 *
	 *	disabled
	 *	8N1
	 *	default baud rate
	 *	hardware flow control.
	 */
	CZ_WIN_RAM(cz);

	if (cz->cz_nchannels == 0) {
		/* No channels?  No more work to do! */
		return;
	}

	cz->cz_ports = mallocarray(cz->cz_nchannels,
	    sizeof(struct cztty_softc), M_DEVBUF, M_WAITOK | M_ZERO);
	cztty_attached_ttys += cz->cz_nchannels;

	for (i = 0; i < cz->cz_nchannels; i++) {
		sc = &cz->cz_ports[i];

		sc->sc_channel = i;
		sc->sc_chan_st = cz->cz_win_st;
		sc->sc_parent = cz;

		if (bus_space_subregion(cz->cz_win_st, cz->cz_win_sh,
		    cz->cz_fwctl + ZFIRM_CHNCTL_OFF(i, 0),
		    ZFIRM_CHNCTL_SIZE, &sc->sc_chan_sh)) {
			printf("%s: unable to subregion channel %d control\n",
			    cz->cz_dev.dv_xname, i);
			sc->sc_channel = CZTTY_CHANNEL_DEAD;
			continue;
		}
		if (bus_space_subregion(cz->cz_win_st, cz->cz_win_sh,
		    cz->cz_fwctl + ZFIRM_BUFCTL_OFF(i, 0),
		    ZFIRM_BUFCTL_SIZE, &sc->sc_buf_sh)) {
			printf("%s: unable to subregion channel %d buffer\n",
			    cz->cz_dev.dv_xname, i);
			sc->sc_channel = CZTTY_CHANNEL_DEAD;
			continue;
		}

		timeout_set(&sc->sc_diag_to, cztty_diag, sc);

		tp = ttymalloc(0);
		tp->t_dev = makedev(cztty_major,
		    (cz->cz_dev.dv_unit * ZFIRM_MAX_CHANNELS) + i);
		tp->t_oproc = czttystart;
		tp->t_param = czttyparam;

		sc->sc_tty = tp;

		CZTTY_CHAN_WRITE(sc, CHNCTL_OP_MODE, C_CH_DISABLE);
		CZTTY_CHAN_WRITE(sc, CHNCTL_INTR_ENABLE, CZ_INTERRUPTS);
		CZTTY_CHAN_WRITE(sc, CHNCTL_SW_FLOW, 0);
		CZTTY_CHAN_WRITE(sc, CHNCTL_FLOW_XON, 0x11);
		CZTTY_CHAN_WRITE(sc, CHNCTL_FLOW_XOFF, 0x13);
		CZTTY_CHAN_WRITE(sc, CHNCTL_COMM_BAUD, TTYDEF_SPEED);
		CZTTY_CHAN_WRITE(sc, CHNCTL_COMM_PARITY, C_PR_NONE);
		CZTTY_CHAN_WRITE(sc, CHNCTL_COMM_DATA_L, C_DL_CS8 | C_DL_1STOP);
		CZTTY_CHAN_WRITE(sc, CHNCTL_COMM_FLAGS, 0);
		CZTTY_CHAN_WRITE(sc, CHNCTL_HW_FLOW, C_RS_CTS | C_RS_RTS);
		CZTTY_CHAN_WRITE(sc, CHNCTL_RS_CONTROL, 0);
	}
}

/*
 * cz_reset_board:
 *
 *	Reset the board via the PLX.
 */
void
cz_reset_board(struct cz_softc *cz)
{
	u_int32_t reg;

	reg = CZ_PLX_READ(cz, PLX_CONTROL);
	CZ_PLX_WRITE(cz, PLX_CONTROL, reg | CONTROL_SWR);
	delay(1000);

	CZ_PLX_WRITE(cz, PLX_CONTROL, reg);
	delay(1000);

	/* Now reload the PLX from its EEPROM. */
	reg = CZ_PLX_READ(cz, PLX_CONTROL);
	CZ_PLX_WRITE(cz, PLX_CONTROL, reg | CONTROL_RELOADCFG);
	delay(1000);
	CZ_PLX_WRITE(cz, PLX_CONTROL, reg);
}

/*
 * cz_load_firmware:
 *
 *	Load the ZFIRM firmware into the board's RAM and start it
 *	running.
 */
int
cz_load_firmware(struct cz_softc *cz)
{
	struct zfirm_header *zfh;
	struct zfirm_config *zfc;
	struct zfirm_block *zfb, *zblocks;
	const u_int8_t *cp;
	const char *board;
	u_int32_t fid;
	int i, j, nconfigs, nblocks, nbytes;

	zfh = (struct zfirm_header *) cycladesz_firmware;

	/* Find the config header. */
	if (letoh32(zfh->zfh_configoff) & (sizeof(u_int32_t) - 1)) {
		printf("%s: bad ZFIRM config offset: 0x%x\n",
		    cz->cz_dev.dv_xname, letoh32(zfh->zfh_configoff));
		return (EIO);
	}
	zfc = (struct zfirm_config *)(cycladesz_firmware +
	    letoh32(zfh->zfh_configoff));
	nconfigs = letoh32(zfh->zfh_nconfig);

	/* Locate the correct configuration for our board. */
	for (i = 0; i < nconfigs; i++, zfc++) {
		if (letoh32(zfc->zfc_mailbox) == cz->cz_mailbox0 &&
		    letoh32(zfc->zfc_function) == ZFC_FUNCTION_NORMAL)
			break;
	}
	if (i == nconfigs) {
		printf("%s: unable to locate config header\n",
		    cz->cz_dev.dv_xname);
		return (EIO);
	}

	nblocks = letoh32(zfc->zfc_nblocks);
	zblocks = (struct zfirm_block *)(cycladesz_firmware +
	    letoh32(zfh->zfh_blockoff));

	/*
	 * 8Zo ver. 1 doesn't have an FPGA.  Load it on all others if
	 * necessary.
	 */
	if (cz->cz_mailbox0 != MAILBOX0_8Zo_V1
#if 0
	    && ((CZ_PLX_READ(cz, PLX_CONTROL) & CONTROL_FPGA_LOADED) == 0)
#endif
								) {
#ifdef CZ_DEBUG
		printf("%s: Loading FPGA...", cz->cz_dev.dv_xname);
#endif
		CZ_WIN_FPGA(cz);
		for (i = 0; i < nblocks; i++) {
			/* zfb = zblocks + letoh32(zfc->zfc_blocklist[i]) ?? */
			zfb = &zblocks[letoh32(zfc->zfc_blocklist[i])];
			if (letoh32(zfb->zfb_type) == ZFB_TYPE_FPGA) {
				nbytes = letoh32(zfb->zfb_size);
				cp = &cycladesz_firmware[
				    letoh32(zfb->zfb_fileoff)];
				for (j = 0; j < nbytes; j++, cp++) {
					bus_space_write_1(cz->cz_win_st,
					    cz->cz_win_sh, 0, *cp);
					/* FPGA needs 30-100us to settle. */
					delay(10);
				}
			}
		}
#ifdef CZ_DEBUG
		printf("done\n");
#endif
	}

	/* Now load the firmware. */
	CZ_WIN_RAM(cz);

	for (i = 0; i < nblocks; i++) {
		/* zfb = zblocks + letoh32(zfc->zfc_blocklist[i]) ?? */
		zfb = &zblocks[letoh32(zfc->zfc_blocklist[i])];
		if (letoh32(zfb->zfb_type) == ZFB_TYPE_FIRMWARE) {
			const u_int32_t *lp;
			u_int32_t ro = letoh32(zfb->zfb_ramoff);
			nbytes = letoh32(zfb->zfb_size);
			lp = (const u_int32_t *)
			    &cycladesz_firmware[letoh32(zfb->zfb_fileoff)];
			for (j = 0; j < nbytes; j += 4, lp++) {
				bus_space_write_4(cz->cz_win_st, cz->cz_win_sh,
				    ro + j, letoh32(*lp));
				delay(10);
			}
		}
	}

	/* Now restart the MIPS. */
	CZ_WIN_FPGA(cz);
	CZ_FPGA_WRITE(cz, FPGA_CPU_START, 0);

	/* Wait for the MIPS to start, then report the results. */
	CZ_WIN_RAM(cz);

#ifdef CZ_DEBUG
	printf("%s: waiting for MIPS to start", cz->cz_dev.dv_xname);
#endif
	for (i = 0; i < 100; i++) {
		fid = bus_space_read_4(cz->cz_win_st, cz->cz_win_sh,
		    ZFIRM_SIG_OFF);
		if (fid == ZFIRM_SIG) {
			/* MIPS has booted. */
			break;
		} else if (fid == ZFIRM_HLT) {
			/*
			 * The MIPS has halted, usually due to a power
			 * shortage on the expansion module.
			 */
			printf("%s: MIPS halted; possible power supply "
			    "problem\n", cz->cz_dev.dv_xname);
			return (EIO);
		} else {
#ifdef CZ_DEBUG
			if ((i % 8) == 0)
				printf(".");
#endif
			delay(250000);
		}
	}
#ifdef CZ_DEBUG
	printf("\n");
#endif
	if (i == 100) {
		CZ_WIN_FPGA(cz);
		printf("%s: MIPS failed to start; wanted 0x%08x got 0x%08x\n",
		    cz->cz_dev.dv_xname, ZFIRM_SIG, fid);
		printf("%s: FPGA ID 0x%08x, FPGA version 0x%08x\n",
		    cz->cz_dev.dv_xname, CZ_FPGA_READ(cz, FPGA_ID),
		    CZ_FPGA_READ(cz, FPGA_VERSION));
		return (EIO);
	}

	/*
	 * Locate the firmware control structures.
	 */
	cz->cz_fwctl = bus_space_read_4(cz->cz_win_st, cz->cz_win_sh,
	    ZFIRM_CTRLADDR_OFF);
#ifdef CZ_DEBUG
	printf("%s: FWCTL structure at offset 0x%08lx\n",
	    cz->cz_dev.dv_xname, cz->cz_fwctl);
#endif

	CZ_FWCTL_WRITE(cz, BRDCTL_C_OS, C_OS_BSD);
	CZ_FWCTL_WRITE(cz, BRDCTL_DRVERSION, CZ_DRIVER_VERSION);

	cz->cz_nchannels = CZ_FWCTL_READ(cz, BRDCTL_NCHANNEL);

	switch (cz->cz_mailbox0) {
	case MAILBOX0_8Zo_V1:
		board = "Cyclades-8Zo ver. 1";
		break;

	case MAILBOX0_8Zo_V2:
		board = "Cyclades-8Zo ver. 2";
		break;

	case MAILBOX0_Ze_V1:
		board = "Cyclades-Ze";
		break;

	default:
		board = "unknown Cyclades Z-series";
		break;
	}

	fid = CZ_FWCTL_READ(cz, BRDCTL_FWVERSION);
	printf("%s: %s, ", cz->cz_dev.dv_xname, board);
	if (cz->cz_nchannels == 0)
		printf("no channels attached, ");
	else
		printf("%d channels (ttyCZ%04d..ttyCZ%04d), ",
		    cz->cz_nchannels, cztty_attached_ttys,
		    cztty_attached_ttys + (cz->cz_nchannels - 1));
	printf("firmware %x.%x.%x\n",
	    (fid >> 8) & 0xf, (fid >> 4) & 0xf, fid & 0xf);

	return (0);
}

/*
 * cz_poll:
 *
 * This card doesn't do interrupts, so scan it for activity every CZ_POLL_MS
 * ms.
 */
void
cz_poll(void *arg)
{
	int s = spltty();
	struct cz_softc *cz = arg;

	cz_intr(cz);
	timeout_add_msec(&cz->cz_timeout, CZ_POLL_MS);

	splx(s);
}

/*
 * cz_intr:
 *
 *	Interrupt service routine.
 *
 * We either are receiving an interrupt directly from the board, or we are
 * in polling mode and it's time to poll.
 */
int
cz_intr(void *arg)
{
	int	rval = 0;
	u_int	command, channel, param;
	struct	cz_softc *cz = arg;
	struct	cztty_softc *sc;
	struct	tty *tp;

	while ((command = (CZ_PLX_READ(cz, PLX_LOCAL_PCI_DOORBELL) & 0xff))) {
		rval = 1;
		channel = CZ_FWCTL_READ(cz, BRDCTL_FWCMD_CHANNEL);
		param = CZ_FWCTL_READ(cz, BRDCTL_FWCMD_PARAM);

		/* now clear this interrupt, possibly enabling another */
		CZ_PLX_WRITE(cz, PLX_LOCAL_PCI_DOORBELL, command);

		if (cz->cz_ports == NULL) {
#ifdef CZ_DEBUG
			printf("%s: interrupt on channel %d, but no channels\n",
			    cz->cz_dev.dv_xname, channel);
#endif
			continue;
		}

		sc = &cz->cz_ports[channel];

		if (sc->sc_channel == CZTTY_CHANNEL_DEAD)
			break;

		tp = sc->sc_tty;

		switch (command) {
		case C_CM_TXFEMPTY:		/* transmit cases */
		case C_CM_TXBEMPTY:
		case C_CM_TXLOWWM:
		case C_CM_INTBACK:
			if (!ISSET(tp->t_state, TS_ISOPEN)) {
#ifdef CZ_DEBUG
				printf("%s: tx intr on closed channel %d\n",
				    cz->cz_dev.dv_xname, channel);
#endif
				break;
			}

			if (cztty_transmit(sc, tp)) {
				/*
				 * Do wakeup stuff here.
				 */
				ttwakeup(tp);
				wakeup(tp);
			}
			break;

		case C_CM_RXNNDT:		/* receive cases */
		case C_CM_RXHIWM:
		case C_CM_INTBACK2:		/* from restart ?? */
#if 0
		case C_CM_ICHAR:
#endif
			if (!ISSET(tp->t_state, TS_ISOPEN)) {
				CZTTY_BUF_WRITE(sc, BUFCTL_RX_GET,
				    CZTTY_BUF_READ(sc, BUFCTL_RX_PUT));
				break;
			}

			if (cztty_receive(sc, tp)) {
				/*
				 * Do wakeup stuff here.
				 */
				ttwakeup(tp);
				wakeup(tp);
			}
			break;

		case C_CM_MDCD:
			if (!ISSET(tp->t_state, TS_ISOPEN))
				break;

			(void) (*linesw[tp->t_line].l_modem)(tp,
			    ISSET(C_RS_DCD, CZTTY_CHAN_READ(sc,
			    CHNCTL_RS_STATUS)));
			break;

		case C_CM_MDSR:
		case C_CM_MRI:
		case C_CM_MCTS:
		case C_CM_MRTS:
			break;

		case C_CM_IOCTLW:
			break;

		case C_CM_PR_ERROR:
			sc->sc_parity_errors++;
			goto error_common;

		case C_CM_FR_ERROR:
			sc->sc_framing_errors++;
			goto error_common;

		case C_CM_OVR_ERROR:
			sc->sc_overflows++;
 error_common:
			if (sc->sc_errors++ == 0)
				timeout_add_sec(&sc->sc_diag_to, 60);
			break;

		case C_CM_RXBRK:
			if (!ISSET(tp->t_state, TS_ISOPEN))
				break;

			/*
			 * A break is a \000 character with TTY_FE error
			 * flags set. So TTY_FE by itself works.
			 */
			(*linesw[tp->t_line].l_rint)(TTY_FE, tp);
			ttwakeup(tp);
			wakeup(tp);
			break;

		default:
#ifdef CZ_DEBUG
			printf("%s: channel %d: Unknown interrupt 0x%x\n",
			    cz->cz_dev.dv_xname, sc->sc_channel, command);
#endif
			break;
		}
	}

	return (rval);
}

/*
 * cz_wait_pci_doorbell:
 *
 *	Wait for the pci doorbell to be clear - wait for pending
 *	activity to drain.
 */
int
cz_wait_pci_doorbell(struct cz_softc *cz, char *wstring)
{
	int	error;

	while (CZ_PLX_READ(cz, PLX_PCI_LOCAL_DOORBELL)) {
		error = tsleep_nsec(cz, TTIPRI | PCATCH, wstring,
		    MSEC_TO_NSEC(10));
		if ((error != 0) && (error != EWOULDBLOCK))
			return (error);
	}
	return (0);
}

/*****************************************************************************
 * Cyclades-Z TTY code starts here...
 *****************************************************************************/

#define CZTTYDIALOUT_MASK	0x80

#define	CZTTY_CZ(sc)		((sc)->sc_parent)

#define	CZTTY_SOFTC(dev)	cztty_getttysoftc(dev)

struct cztty_softc *
cztty_getttysoftc(dev_t dev)
{
	int i, j, k, u = minor(dev) & ~CZTTYDIALOUT_MASK;
	struct cz_softc *cz;

	for (i = 0, j = 0; i < cz_cd.cd_ndevs; i++) {
		k = j;
		cz = (struct cz_softc *)device_lookup(&cz_cd, i);
		if (cz == NULL)
			continue;
		if (cz->cz_ports == NULL)
			continue;
		j += cz->cz_nchannels;
		if (j > u)
			break;
	}

	if (i >= cz_cd.cd_ndevs)
		return (NULL);
	else
		return (&cz->cz_ports[u - k]);
}

int
cztty_findmajor(void)
{
	int	maj;

	for (maj = 0; maj < nchrdev; maj++) {
		if (cdevsw[maj].d_open == czttyopen)
			break;
	}

	return (maj == nchrdev) ? 0 : maj;
}

/*
 * czttytty:
 *
 *	Return a pointer to our tty.
 */
struct tty *
czttytty(dev_t dev)
{
	struct cztty_softc *sc = CZTTY_SOFTC(dev);

#ifdef DIAGNOSTIC
	if (sc == NULL)
		panic("czttytty");
#endif

	return (sc->sc_tty);
}

/*
 * cztty_shutdown:
 *
 *	Shut down a port.
 */
void
cztty_shutdown(struct cztty_softc *sc)
{
	struct cz_softc *cz = CZTTY_CZ(sc);
	struct tty *tp = sc->sc_tty;
	int s;

	s = spltty();

	/* Clear any break condition set with TIOCSBRK. */
	cztty_break(sc, 0);

	/*
	 * Hang up if necessary.  Wait a bit, so the other side has time to
	 * notice even if we immediately open the port again.
	 */
	if (ISSET(tp->t_cflag, HUPCL)) {
		cztty_modem(sc, 0);
		tsleep_nsec(tp, TTIPRI, ttclos, SEC_TO_NSEC(1));
	}

	/* Disable the channel. */
	cz_wait_pci_doorbell(cz, "czdis");
	CZTTY_CHAN_WRITE(sc, CHNCTL_OP_MODE, C_CH_DISABLE);
	CZ_FWCTL_WRITE(cz, BRDCTL_HCMD_CHANNEL, sc->sc_channel);
	CZ_PLX_WRITE(cz, PLX_PCI_LOCAL_DOORBELL, C_CM_IOCTL);

	if ((--cz->cz_nopenchan == 0) && (cz->cz_ih == NULL)) {
#ifdef CZ_DEBUG
		printf("%s: Disabling polling\n", cz->cz_dev.dv_xname);
#endif
		timeout_del(&cz->cz_timeout);
	}

	splx(s);
}

/*
 * czttyopen:
 *
 *	Open a Cyclades-Z serial port.
 */
int
czttyopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct cztty_softc *sc = CZTTY_SOFTC(dev);
	struct cz_softc *cz;
	struct tty *tp;
	int s, error;

	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_channel == CZTTY_CHANNEL_DEAD)
		return (ENXIO);

	cz = CZTTY_CZ(sc);
	tp = sc->sc_tty;

	if (ISSET(tp->t_state, TS_ISOPEN) &&
	    ISSET(tp->t_state, TS_XCLUDE) &&
	    suser(p) != 0)
		return (EBUSY);

	s = spltty();

	/*
	 * Do the following iff this is a first open.
	 */
	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		struct termios t;

		tp->t_dev = dev;

		/* If we're turning things on, enable interrupts */
		if ((cz->cz_nopenchan++ == 0) && (cz->cz_ih == NULL)) {
#ifdef CZ_DEBUG
			printf("%s: Enabling polling.\n",
			    cz->cz_dev.dv_xname);
#endif
			timeout_add_msec(&cz->cz_timeout, CZ_POLL_MS);
		}

		/*
		 * Enable the channel.  Don't actually ring the
		 * doorbell here; czttyparam() will do it for us.
		 */
		cz_wait_pci_doorbell(cz, "czopen");

		CZTTY_CHAN_WRITE(sc, CHNCTL_OP_MODE, C_CH_ENABLE);

		/*
		 * Initialize the termios status to the defaults.  Add in the
		 * sticky bits from TIOCSFLAGS.
		 */
		t.c_ispeed = 0;
		t.c_ospeed = TTYDEF_SPEED;
		t.c_cflag = TTYDEF_CFLAG;
		if (ISSET(sc->sc_swflags, TIOCFLAG_CLOCAL))
			SET(t.c_cflag, CLOCAL);
		if (ISSET(sc->sc_swflags, TIOCFLAG_CRTSCTS))
			SET(t.c_cflag, CRTSCTS);

		/*
		 * Reset the input and output rings.  Do this before
		 * we call czttyparam(), as that function enables
		 * the channel.
		 */
		CZTTY_BUF_WRITE(sc, BUFCTL_RX_GET,
		    CZTTY_BUF_READ(sc, BUFCTL_RX_PUT));
		CZTTY_BUF_WRITE(sc, BUFCTL_TX_PUT,
		    CZTTY_BUF_READ(sc, BUFCTL_TX_GET));

		/* Make sure czttyparam() will see changes. */
		tp->t_ospeed = 0;
		(void) czttyparam(tp, &t);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		ttychars(tp);
		ttsetwater(tp);

		/*
		 * Turn on DTR.  We must always do this, even if carrier is not
		 * present, because otherwise we'd have to use TIOCSDTR
		 * immediately after setting CLOCAL, which applications do not
		 * expect.  We always assert DTR while the device is open
		 * unless explicitly requested to deassert it.
		 */
		cztty_modem(sc, 1);
	}

	splx(s);

	error = (*linesw[tp->t_line].l_open)(dev, tp, p);
	if (error)
		goto bad;

	return (0);

 bad:
	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		/*
		 * We failed to open the device, and nobody else had it opened.
		 * Clean up the state as appropriate.
		 */
		cztty_shutdown(sc);
	}

	return (error);
}

/*
 * czttyclose:
 *
 *	Close a Cyclades-Z serial port.
 */
int
czttyclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct cztty_softc *sc = CZTTY_SOFTC(dev);
	struct tty *tp = sc->sc_tty;

	/* XXX This is for cons.c. */
	if (!ISSET(tp->t_state, TS_ISOPEN))
		return (0);

	(*linesw[tp->t_line].l_close)(tp, flags, p);
	ttyclose(tp);

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		/*
		 * Although we got a last close, the device may still be in
		 * use; e.g. if this was the dialout node, and there are still
		 * processes waiting for carrier on the non-dialout node.
		 */
		cztty_shutdown(sc);
	}

	return (0);
}

/*
 * czttyread:
 *
 *	Read from a Cyclades-Z serial port.
 */
int
czttyread(dev_t dev, struct uio *uio, int flags)
{
	struct cztty_softc *sc = CZTTY_SOFTC(dev);
	struct tty *tp = sc->sc_tty;

	return ((*linesw[tp->t_line].l_read)(tp, uio, flags));
}

/*
 * czttywrite:
 *
 *	Write to a Cyclades-Z serial port.
 */
int
czttywrite(dev_t dev, struct uio *uio, int flags)
{
	struct cztty_softc *sc = CZTTY_SOFTC(dev);
	struct tty *tp = sc->sc_tty;

	return ((*linesw[tp->t_line].l_write)(tp, uio, flags));
}

/*
 * czttyioctl:
 *
 *	Perform a control operation on a Cyclades-Z serial port.
 */
int
czttyioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct cztty_softc *sc = CZTTY_SOFTC(dev);
	struct tty *tp = sc->sc_tty;
	int s, error;

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	error = 0;

	s = spltty();

	switch (cmd) {
	case TIOCSBRK:
		cztty_break(sc, 1);
		break;

	case TIOCCBRK:
		cztty_break(sc, 0);
		break;

	case TIOCGFLAGS:
		*(int *)data = sc->sc_swflags;
		break;

	case TIOCSFLAGS:
		error = suser(p);
		if (error)
			break;
		sc->sc_swflags = *(int *)data;
		break;

	case TIOCSDTR:
		cztty_modem(sc, 1);
		break;

	case TIOCCDTR:
		cztty_modem(sc, 0);
		break;

	case TIOCMSET:
	case TIOCMBIS:
	case TIOCMBIC:
		tiocm_to_cztty(sc, cmd, *(int *)data);
		break;

	case TIOCMGET:
		*(int *)data = cztty_to_tiocm(sc);
		break;

	default:
		error = ENOTTY;
		break;
	}

	splx(s);

	return (error);
}

/*
 * cztty_break:
 *
 *	Set or clear BREAK on a port.
 */
void
cztty_break(struct cztty_softc *sc, int onoff)
{
	struct cz_softc *cz = CZTTY_CZ(sc);

	cz_wait_pci_doorbell(cz, "czbreak");

	CZ_FWCTL_WRITE(cz, BRDCTL_HCMD_CHANNEL, sc->sc_channel);
	CZ_PLX_WRITE(cz, PLX_PCI_LOCAL_DOORBELL,
	    onoff ? C_CM_SET_BREAK : C_CM_CLR_BREAK);
}

/*
 * cztty_modem:
 *
 *	Set or clear DTR on a port.
 */
void
cztty_modem(struct cztty_softc *sc, int onoff)
{
	struct cz_softc *cz = CZTTY_CZ(sc);

	if (sc->sc_rs_control_dtr == 0)
		return;

	cz_wait_pci_doorbell(cz, "czmod");

	if (onoff)
		sc->sc_chanctl_rs_control |= sc->sc_rs_control_dtr;
	else
		sc->sc_chanctl_rs_control &= ~sc->sc_rs_control_dtr;
	CZTTY_CHAN_WRITE(sc, CHNCTL_RS_CONTROL, sc->sc_chanctl_rs_control);

	CZ_FWCTL_WRITE(cz, BRDCTL_HCMD_CHANNEL, sc->sc_channel);
	CZ_PLX_WRITE(cz, PLX_PCI_LOCAL_DOORBELL, C_CM_IOCTLM);
}

/*
 * tiocm_to_cztty:
 *
 *	Process TIOCM* ioctls.
 */
void
tiocm_to_cztty(struct cztty_softc *sc, u_long how, int ttybits)
{
	struct cz_softc *cz = CZTTY_CZ(sc);
	u_int32_t czttybits;

	czttybits = 0;
	if (ISSET(ttybits, TIOCM_DTR))
		SET(czttybits, C_RS_DTR);
	if (ISSET(ttybits, TIOCM_RTS))
		SET(czttybits, C_RS_RTS);

	cz_wait_pci_doorbell(cz, "cztiocm");

	switch (how) {
	case TIOCMBIC:
		CLR(sc->sc_chanctl_rs_control, czttybits);
		break;

	case TIOCMBIS:
		SET(sc->sc_chanctl_rs_control, czttybits);
		break;

	case TIOCMSET:
		CLR(sc->sc_chanctl_rs_control, C_RS_DTR | C_RS_RTS);
		SET(sc->sc_chanctl_rs_control, czttybits);
		break;
	}

	CZTTY_CHAN_WRITE(sc, CHNCTL_RS_CONTROL, sc->sc_chanctl_rs_control);

	CZ_FWCTL_WRITE(cz, BRDCTL_HCMD_CHANNEL, sc->sc_channel);
	CZ_PLX_WRITE(cz, PLX_PCI_LOCAL_DOORBELL, C_CM_IOCTLM);
}

/*
 * cztty_to_tiocm:
 *
 *	Process the TIOCMGET ioctl.
 */
int
cztty_to_tiocm(struct cztty_softc *sc)
{
	struct cz_softc *cz = CZTTY_CZ(sc);
	u_int32_t rs_status, op_mode;
	int ttybits = 0;

	cz_wait_pci_doorbell(cz, "cztty");

	rs_status = CZTTY_CHAN_READ(sc, CHNCTL_RS_STATUS);
	op_mode = CZTTY_CHAN_READ(sc, CHNCTL_OP_MODE);

	if (ISSET(rs_status, C_RS_RTS))
		SET(ttybits, TIOCM_RTS);
	if (ISSET(rs_status, C_RS_CTS))
		SET(ttybits, TIOCM_CTS);
	if (ISSET(rs_status, C_RS_DCD))
		SET(ttybits, TIOCM_CAR);
	if (ISSET(rs_status, C_RS_DTR))
		SET(ttybits, TIOCM_DTR);
	if (ISSET(rs_status, C_RS_RI))
		SET(ttybits, TIOCM_RNG);
	if (ISSET(rs_status, C_RS_DSR))
		SET(ttybits, TIOCM_DSR);

	if (ISSET(op_mode, C_CH_ENABLE))
		SET(ttybits, TIOCM_LE);

	return (ttybits);
}

/*
 * czttyparam:
 *
 *	Set Cyclades-Z serial port parameters from termios.
 *
 *	XXX Should just copy the whole termios after making
 *	XXX sure all the changes could be done.
 */
int
czttyparam(struct tty *tp, struct termios *t)
{
	struct cztty_softc *sc = CZTTY_SOFTC(tp->t_dev);
	struct cz_softc *cz = CZTTY_CZ(sc);
	u_int32_t rs_status;
	int ospeed, cflag;

	ospeed = t->c_ospeed;
	cflag = t->c_cflag;

	/* Check requested parameters. */
	if (ospeed < 0)
		return (EINVAL);
	if (t->c_ispeed && t->c_ispeed != ospeed)
		return (EINVAL);

	if (ISSET(sc->sc_swflags, TIOCFLAG_SOFTCAR)) {
		SET(cflag, CLOCAL);
		CLR(cflag, HUPCL);
	}

	/*
	 * If there were no changes, don't do anything.  This avoids dropping
	 * input and improves performance when all we did was frob things like
	 * VMIN and VTIME.
	 */
	if (tp->t_ospeed == ospeed &&
	    tp->t_cflag == cflag)
		return (0);

	/* Data bits. */
	sc->sc_chanctl_comm_data_l = 0;
	switch (t->c_cflag & CSIZE) {
	case CS5:
		sc->sc_chanctl_comm_data_l |= C_DL_CS5;
		break;

	case CS6:
		sc->sc_chanctl_comm_data_l |= C_DL_CS6;
		break;

	case CS7:
		sc->sc_chanctl_comm_data_l |= C_DL_CS7;
		break;

	case CS8:
		sc->sc_chanctl_comm_data_l |= C_DL_CS8;
		break;
	}

	/* Stop bits. */
	if (t->c_cflag & CSTOPB) {
		if ((sc->sc_chanctl_comm_data_l & C_DL_CS) == C_DL_CS5)
			sc->sc_chanctl_comm_data_l |= C_DL_15STOP;
		else
			sc->sc_chanctl_comm_data_l |= C_DL_2STOP;
	} else
		sc->sc_chanctl_comm_data_l |= C_DL_1STOP;

	/* Parity. */
	if (t->c_cflag & PARENB) {
		if (t->c_cflag & PARODD)
			sc->sc_chanctl_comm_parity = C_PR_ODD;
		else
			sc->sc_chanctl_comm_parity = C_PR_EVEN;
	} else
		sc->sc_chanctl_comm_parity = C_PR_NONE;

	/*
	 * Initialize flow control pins depending on the current flow control
	 * mode.
	 */
	if (ISSET(t->c_cflag, CRTSCTS)) {
		sc->sc_rs_control_dtr = C_RS_DTR;
		sc->sc_chanctl_hw_flow = C_RS_CTS | C_RS_RTS;
	} else if (ISSET(t->c_cflag, MDMBUF)) {
		sc->sc_rs_control_dtr = 0;
		sc->sc_chanctl_hw_flow = C_RS_DCD | C_RS_DTR;
	} else {
		/*
		 * If no flow control, then always set RTS.  This will make
		 * the other side happy if it mistakenly thinks we're doing
		 * RTS/CTS flow control.
		 */
		sc->sc_rs_control_dtr = C_RS_DTR | C_RS_RTS;
		sc->sc_chanctl_hw_flow = 0;
		if (ISSET(sc->sc_chanctl_rs_control, C_RS_DTR))
			SET(sc->sc_chanctl_rs_control, C_RS_RTS);
		else
			CLR(sc->sc_chanctl_rs_control, C_RS_RTS);
	}

	/* Baud rate. */
	sc->sc_chanctl_comm_baud = ospeed;

	/* Copy to tty. */
	tp->t_ispeed =  0;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	/*
	 * Now load the channel control structure.
	 */

	cz_wait_pci_doorbell(cz, "czparam");

	CZTTY_CHAN_WRITE(sc, CHNCTL_COMM_BAUD, sc->sc_chanctl_comm_baud);
	CZTTY_CHAN_WRITE(sc, CHNCTL_COMM_DATA_L, sc->sc_chanctl_comm_data_l);
	CZTTY_CHAN_WRITE(sc, CHNCTL_COMM_PARITY, sc->sc_chanctl_comm_parity);
	CZTTY_CHAN_WRITE(sc, CHNCTL_HW_FLOW, sc->sc_chanctl_hw_flow);
	CZTTY_CHAN_WRITE(sc, CHNCTL_RS_CONTROL, sc->sc_chanctl_rs_control);

	CZ_FWCTL_WRITE(cz, BRDCTL_HCMD_CHANNEL, sc->sc_channel);
	CZ_PLX_WRITE(cz, PLX_PCI_LOCAL_DOORBELL, C_CM_IOCTLW);

	cz_wait_pci_doorbell(cz, "czparam");

	CZ_FWCTL_WRITE(cz, BRDCTL_HCMD_CHANNEL, sc->sc_channel);
	CZ_PLX_WRITE(cz, PLX_PCI_LOCAL_DOORBELL, C_CM_IOCTLM);

	cz_wait_pci_doorbell(cz, "czparam");

	/*
	 * Update the tty layer's idea of the carrier bit, in case we changed
	 * CLOCAL.  We don't hang up here; we only do that by explicit
	 * request.
	 */
	rs_status = CZTTY_CHAN_READ(sc, CHNCTL_RS_STATUS);
	(void) (*linesw[tp->t_line].l_modem)(tp, ISSET(rs_status, C_RS_DCD));

	return (0);
}

/*
 * czttystart:
 *
 *	Start or restart transmission.
 */
void
czttystart(struct tty *tp)
{
	struct cztty_softc *sc = CZTTY_SOFTC(tp->t_dev);
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY | TS_TIMEOUT | TS_TTSTOP))
		goto out;

	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0)
		goto out;

	cztty_transmit(sc, tp);
 out:
	splx(s);
}

/*
 * czttystop:
 *
 *	Stop output, e.g., for ^S or output flush.
 */
int
czttystop(struct tty *tp, int flag)
{

	/*
	 * XXX We don't do anything here, yet.  Mostly, I don't know
	 * XXX exactly how this should be implemented on this device.
	 * XXX We've given a big chunk of data to the MIPS already,
	 * XXX and I don't know how we request the MIPS to stop sending
	 * XXX the data.  So, punt for now.  --thorpej
	 */
	return (0);
}

/*
 * cztty_diag:
 *
 *	Issue a scheduled diagnostic message.
 */
void
cztty_diag(void *arg)
{
	struct cztty_softc *sc = arg;
	struct cz_softc *cz = CZTTY_CZ(sc);
	u_int overflows, parity_errors, framing_errors;
	int s;

	s = spltty();

	overflows = sc->sc_overflows;
	sc->sc_overflows = 0;

	parity_errors = sc->sc_parity_errors;
	sc->sc_parity_errors = 0;

	framing_errors = sc->sc_framing_errors;
	sc->sc_framing_errors = 0;

	sc->sc_errors = 0;

	splx(s);

	log(LOG_WARNING,
	    "%s: channel %d: %u overflow%s, %u parity, %u framing error%s\n",
	    cz->cz_dev.dv_xname, sc->sc_channel,
	    overflows, overflows == 1 ? "" : "s",
	    parity_errors,
	    framing_errors, framing_errors == 1 ? "" : "s");
}

/*
 * tx and rx ring buffer size macros:
 *
 * The transmitter and receiver both use ring buffers. For each one, there
 * is a get (consumer) and a put (producer) offset. The get value is the
 * next byte to be read from the ring, and the put is the next one to be
 * put into the ring.  get == put means the ring is empty.
 *
 * For each ring, the firmware controls one of (get, put) and this driver
 * controls the other. For transmission, this driver updates put to point
 * past the valid data, and the firmware moves get as bytes are sent. Likewise
 * for receive, the driver controls put, and this driver controls get.
 */
#define	TX_MOVEABLE(g, p, s)	(((g) > (p)) ? ((g) - (p) - 1) : ((s) - (p)))
#define RX_MOVEABLE(g, p, s)	(((g) > (p)) ? ((s) - (g)) : ((p) - (g)))

/*
 * cztty_transmit()
 *
 * Look at the tty for this port and start sending.
 */
int
cztty_transmit(struct cztty_softc *sc, struct tty *tp)
{
	struct cz_softc *cz = CZTTY_CZ(sc);
	u_int move, get, put, size, address;
#ifdef HOSTRAMCODE
	int error, done = 0;
#else
	int done = 0;
#endif

	size	= CZTTY_BUF_READ(sc, BUFCTL_TX_BUFSIZE);
	get	= CZTTY_BUF_READ(sc, BUFCTL_TX_GET);
	put	= CZTTY_BUF_READ(sc, BUFCTL_TX_PUT);
	address	= CZTTY_BUF_READ(sc, BUFCTL_TX_BUFADDR);

	while ((tp->t_outq.c_cc > 0) && ((move = TX_MOVEABLE(get, put, size)))){
#ifdef HOSTRAMCODE
		if (0) {
			move = min(tp->t_outq.c_cc, move);
			error = q_to_b(&tp->t_outq, 0, move);
			if (error != move) {
				printf("%s: channel %d: error moving to "
				    "transmit buf\n", cz->cz_dev.dv_xname,
				    sc->sc_channel);
				move = error;
			}
		} else {
#endif
			move = min(ndqb(&tp->t_outq, 0), move);
			bus_space_write_region_1(cz->cz_win_st, cz->cz_win_sh,
			    address + put, tp->t_outq.c_cf, move);
			ndflush(&tp->t_outq, move);
#ifdef HOSTRAMCODE
		}
#endif

		put = ((put + move) % size);
		done = 1;
	}
	if (done) {
		CZTTY_BUF_WRITE(sc, BUFCTL_TX_PUT, put);
	}
	return (done);
}

int
cztty_receive(struct cztty_softc *sc, struct tty *tp)
{
	struct cz_softc *cz = CZTTY_CZ(sc);
	u_int get, put, size, address;
	int done = 0, ch;

	size	= CZTTY_BUF_READ(sc, BUFCTL_RX_BUFSIZE);
	get	= CZTTY_BUF_READ(sc, BUFCTL_RX_GET);
	put	= CZTTY_BUF_READ(sc, BUFCTL_RX_PUT);
	address	= CZTTY_BUF_READ(sc, BUFCTL_RX_BUFADDR);

	while ((get != put) && ((tp->t_canq.c_cc + tp->t_rawq.c_cc) < tp->t_hiwat)) {
#ifdef HOSTRAMCODE
		if (hostram)
			ch = ((char *)fifoaddr)[get];
		} else {
#endif
			ch = bus_space_read_1(cz->cz_win_st, cz->cz_win_sh,
			    address + get);
#ifdef HOSTRAMCODE
		}
#endif
		(*linesw[tp->t_line].l_rint)(ch, tp);
		get = (get + 1) % size;
		done = 1;
	}
	if (done) {
		CZTTY_BUF_WRITE(sc, BUFCTL_RX_GET, get);
	}
	return (done);
}
