/*-
 * All Rights Reserved, Copyright (C) Fujitsu Limited 1995
 *
 * This software may be used, modified, copied, distributed, and sold, in
 * both source and binary form provided that the above copyright, these
 * terms and the following disclaimer are retained.  The name of the author
 * and/or the contributor may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND THE CONTRIBUTOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR THE CONTRIBUTOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION.
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 *
 * Device driver for Fujitsu MB86960A/MB86965A based Ethernet cards.
 * Contributed by M. Sekiguchi. <seki@sysrap.cs.fujitsu.co.jp>
 *
 * This version is intended to be a generic template for various
 * MB86960A/MB86965A based Ethernet cards.  It currently supports
 * Fujitsu FMV-180 series for ISA and Allied-Telesis AT1700/RE2000
 * series for ISA, as well as Fujitsu MBH10302 PC Card.
 * There are some currently-
 * unused hooks embedded, which are primarily intended to support
 * other types of Ethernet cards, but the author is not sure whether
 * they are useful.
 *
 * This software is a derivative work of if_ed.c version 1.56 by David
 * Greenman available as a part of FreeBSD 2.0 RELEASE source distribution.
 *
 * The following lines are retained from the original if_ed.c:
 *
 * Copyright (C) 1993, David Greenman. This software may be used, modified,
 *   copied, distributed, and sold, in both source and binary form provided
 *   that the above copyright and these terms are retained. Under no
 *   circumstances is the author responsible for the proper functioning
 *   of this software, nor does the author assume any responsibility
 *   for damages incurred with its use.
 */

/*
 * TODO:
 *  o   To support ISA PnP auto configuration for FMV-183/184.
 *  o   To reconsider mbuf usage.
 *  o   To reconsider transmission buffer usage, including
 *      transmission buffer size (currently 4KB x 2) and pros-and-
 *      cons of multiple frame transmission.
 *  o   To test IPX codes.
 *  o   To test new-bus frontend.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/bpf.h>

#include <dev/fe/mb86960.h>
#include <dev/fe/if_fereg.h>
#include <dev/fe/if_fevar.h>

/*
 * Transmit just one packet per a "send" command to 86960.
 * This option is intended for performance test.  An EXPERIMENTAL option.
 */
#ifndef FE_SINGLE_TRANSMISSION
#define FE_SINGLE_TRANSMISSION 0
#endif

/*
 * Maximum loops when interrupt.
 * This option prevents an infinite loop due to hardware failure.
 * (Some laptops make an infinite loop after PC Card is ejected.)
 */
#ifndef FE_MAX_LOOP
#define FE_MAX_LOOP 0x800
#endif

/*
 * Device configuration flags.
 */

/* DLCR6 settings.  */
#define FE_FLAGS_DLCR6_VALUE	0x007F

/* Force DLCR6 override.  */
#define FE_FLAGS_OVERRIDE_DLCR6	0x0080


devclass_t fe_devclass;

/*
 * Special filter values.
 */
static struct fe_filter const fe_filter_nothing = { FE_FILTER_NOTHING };
static struct fe_filter const fe_filter_all     = { FE_FILTER_ALL };

/* Standard driver entry points.  These can be static.  */
static void		fe_init		(void *);
static void		fe_init_locked	(struct fe_softc *);
static driver_intr_t	fe_intr;
static int		fe_ioctl	(struct ifnet *, u_long, caddr_t);
static void		fe_start	(struct ifnet *);
static void		fe_start_locked	(struct ifnet *);
static void		fe_watchdog	(void *);
static int		fe_medchange	(struct ifnet *);
static void		fe_medstat	(struct ifnet *, struct ifmediareq *);

/* Local functions.  Order of declaration is confused.  FIXME.  */
static int	fe_get_packet	( struct fe_softc *, u_short );
static void	fe_tint		( struct fe_softc *, u_char );
static void	fe_rint		( struct fe_softc *, u_char );
static void	fe_xmit		( struct fe_softc * );
static void	fe_write_mbufs	( struct fe_softc *, struct mbuf * );
static void	fe_setmode	( struct fe_softc * );
static void	fe_loadmar	( struct fe_softc * );

#ifdef DIAGNOSTIC
static void	fe_emptybuffer	( struct fe_softc * );
#endif

/*
 * Fe driver specific constants which relate to 86960/86965.
 */

/* Interrupt masks  */
#define FE_TMASK ( FE_D2_COLL16 | FE_D2_TXDONE )
#define FE_RMASK ( FE_D3_OVRFLO | FE_D3_CRCERR \
		 | FE_D3_ALGERR | FE_D3_SRTPKT | FE_D3_PKTRDY )

/* Maximum number of iterations for a receive interrupt.  */
#define FE_MAX_RECV_COUNT ( ( 65536 - 2048 * 2 ) / 64 )
	/*
	 * Maximum size of SRAM is 65536,
	 * minimum size of transmission buffer in fe is 2x2KB,
	 * and minimum amount of received packet including headers
	 * added by the chip is 64 bytes.
	 * Hence FE_MAX_RECV_COUNT is the upper limit for number
	 * of packets in the receive buffer.
	 */

/*
 * Miscellaneous definitions not directly related to hardware.
 */

/* The following line must be delete when "net/if_media.h" support it.  */
#ifndef IFM_10_FL
#define IFM_10_FL	/* 13 */ IFM_10_5
#endif

#if 0
/* Mapping between media bitmap (in fe_softc.mbitmap) and ifm_media.  */
static int const bit2media [] = {
			IFM_HDX | IFM_ETHER | IFM_AUTO,
			IFM_HDX | IFM_ETHER | IFM_MANUAL,
			IFM_HDX | IFM_ETHER | IFM_10_T,
			IFM_HDX | IFM_ETHER | IFM_10_2,
			IFM_HDX | IFM_ETHER | IFM_10_5,
			IFM_HDX | IFM_ETHER | IFM_10_FL,
			IFM_FDX | IFM_ETHER | IFM_10_T,
	/* More can be come here... */
			0
};
#else
/* Mapping between media bitmap (in fe_softc.mbitmap) and ifm_media.  */
static int const bit2media [] = {
			IFM_ETHER | IFM_AUTO,
			IFM_ETHER | IFM_MANUAL,
			IFM_ETHER | IFM_10_T,
			IFM_ETHER | IFM_10_2,
			IFM_ETHER | IFM_10_5,
			IFM_ETHER | IFM_10_FL,
			IFM_ETHER | IFM_10_T,
	/* More can be come here... */
			0
};
#endif

/*
 * Check for specific bits in specific registers have specific values.
 * A common utility function called from various sub-probe routines.
 */
int
fe_simple_probe (struct fe_softc const * sc,
		 struct fe_simple_probe_struct const * sp)
{
	struct fe_simple_probe_struct const *p;
	int8_t bits;

	for (p  = sp; p->mask != 0; p++) {
	    bits = fe_inb(sc, p->port);
 	    printf("port %d, mask %x, bits %x read %x\n", p->port,
	      p->mask, p->bits, bits);
		if ((bits & p->mask) != p->bits)
			return 0;
	}
	return 1;
}

/* Test if a given 6 byte value is a valid Ethernet station (MAC)
   address.  "Vendor" is an expected vendor code (first three bytes,)
   or a zero when nothing expected.  */
int
fe_valid_Ether_p (u_char const * addr, unsigned vendor)
{
#ifdef FE_DEBUG
	printf("fe?: validating %6D against %06x\n", addr, ":", vendor);
#endif

	/* All zero is not allowed as a vendor code.  */
	if (addr[0] == 0 && addr[1] == 0 && addr[2] == 0) return 0;

	switch (vendor) {
	    case 0x000000:
		/* Legal Ethernet address (stored in ROM) must have
		   its Group and Local bits cleared.  */
		if ((addr[0] & 0x03) != 0) return 0;
		break;
	    case 0x020000:
		/* Same as above, but a local address is allowed in
                   this context.  */
		if (ETHER_IS_MULTICAST(addr)) return 0;
		break;
	    default:
		/* Make sure the vendor part matches if one is given.  */
		if (   addr[0] != ((vendor >> 16) & 0xFF)
		    || addr[1] != ((vendor >>  8) & 0xFF)
		    || addr[2] != ((vendor      ) & 0xFF)) return 0;
		break;
	}

	/* Host part must not be all-zeros nor all-ones.  */
	if (addr[3] == 0xFF && addr[4] == 0xFF && addr[5] == 0xFF) return 0;
	if (addr[3] == 0x00 && addr[4] == 0x00 && addr[5] == 0x00) return 0;

	/* Given addr looks like an Ethernet address.  */
	return 1;
}

/* Fill our softc struct with default value.  */
void
fe_softc_defaults (struct fe_softc *sc)
{
	/* Prepare for typical register prototypes.  We assume a
           "typical" board has <32KB> of <fast> SRAM connected with a
           <byte-wide> data lines.  */
	sc->proto_dlcr4 = FE_D4_LBC_DISABLE | FE_D4_CNTRL;
	sc->proto_dlcr5 = 0;
	sc->proto_dlcr6 = FE_D6_BUFSIZ_32KB | FE_D6_TXBSIZ_2x4KB
		| FE_D6_BBW_BYTE | FE_D6_SBW_WORD | FE_D6_SRAM_100ns;
	sc->proto_dlcr7 = FE_D7_BYTSWP_LH;
	sc->proto_bmpr13 = 0;

	/* Assume the probe process (to be done later) is stable.  */
	sc->stability = 0;

	/* A typical board needs no hooks.  */
	sc->init = NULL;
	sc->stop = NULL;

	/* Assume the board has no software-controllable media selection.  */
	sc->mbitmap = MB_HM;
	sc->defmedia = MB_HM;
	sc->msel = NULL;
}

/* Common error reporting routine used in probe routines for
   "soft configured IRQ"-type boards.  */
void
fe_irq_failure (char const *name, int unit, int irq, char const *list)
{
	printf("fe%d: %s board is detected, but %s IRQ was given\n",
	       unit, name, (irq == NO_IRQ ? "no" : "invalid"));
	if (list != NULL) {
		printf("fe%d: specify an IRQ from %s in kernel config\n",
		       unit, list);
	}
}

/*
 * Hardware (vendor) specific hooks.
 */

/*
 * Generic media selection scheme for MB86965 based boards.
 */
void
fe_msel_965 (struct fe_softc *sc)
{
	u_char b13;

	/* Find the appropriate bits for BMPR13 tranceiver control.  */
	switch (IFM_SUBTYPE(sc->media.ifm_media)) {
	    case IFM_AUTO: b13 = FE_B13_PORT_AUTO | FE_B13_TPTYPE_UTP; break;
	    case IFM_10_T: b13 = FE_B13_PORT_TP   | FE_B13_TPTYPE_UTP; break;
	    default:       b13 = FE_B13_PORT_AUI;  break;
	}

	/* Write it into the register.  It takes effect immediately.  */
	fe_outb(sc, FE_BMPR13, sc->proto_bmpr13 | b13);
}


/*
 * Fujitsu MB86965 JLI mode support routines.
 */

/*
 * Routines to read all bytes from the config EEPROM through MB86965A.
 * It is a MicroWire (3-wire) serial EEPROM with 6-bit address.
 * (93C06 or 93C46.)
 */
static void
fe_strobe_eeprom_jli (struct fe_softc *sc, u_short bmpr16)
{
	/*
	 * We must guarantee 1us (or more) interval to access slow
	 * EEPROMs.  The following redundant code provides enough
	 * delay with ISA timing.  (Even if the bus clock is "tuned.")
	 * Some modification will be needed on faster busses.
	 */
	fe_outb(sc, bmpr16, FE_B16_SELECT);
	fe_outb(sc, bmpr16, FE_B16_SELECT | FE_B16_CLOCK);
	fe_outb(sc, bmpr16, FE_B16_SELECT | FE_B16_CLOCK);
	fe_outb(sc, bmpr16, FE_B16_SELECT);
}

void
fe_read_eeprom_jli (struct fe_softc * sc, u_char * data)
{
	u_char n, val, bit;
	u_char save16, save17;

	/* Save the current value of the EEPROM interface registers.  */
	save16 = fe_inb(sc, FE_BMPR16);
	save17 = fe_inb(sc, FE_BMPR17);

	/* Read bytes from EEPROM; two bytes per an iteration.  */
	for (n = 0; n < JLI_EEPROM_SIZE / 2; n++) {

		/* Reset the EEPROM interface.  */
		fe_outb(sc, FE_BMPR16, 0x00);
		fe_outb(sc, FE_BMPR17, 0x00);

		/* Start EEPROM access.  */
		fe_outb(sc, FE_BMPR16, FE_B16_SELECT);
		fe_outb(sc, FE_BMPR17, FE_B17_DATA);
		fe_strobe_eeprom_jli(sc, FE_BMPR16);

		/* Pass the iteration count as well as a READ command.  */
		val = 0x80 | n;
		for (bit = 0x80; bit != 0x00; bit >>= 1) {
			fe_outb(sc, FE_BMPR17, (val & bit) ? FE_B17_DATA : 0);
			fe_strobe_eeprom_jli(sc, FE_BMPR16);
		}
		fe_outb(sc, FE_BMPR17, 0x00);

		/* Read a byte.  */
		val = 0;
		for (bit = 0x80; bit != 0x00; bit >>= 1) {
			fe_strobe_eeprom_jli(sc, FE_BMPR16);
			if (fe_inb(sc, FE_BMPR17) & FE_B17_DATA)
				val |= bit;
		}
		*data++ = val;

		/* Read one more byte.  */
		val = 0;
		for (bit = 0x80; bit != 0x00; bit >>= 1) {
			fe_strobe_eeprom_jli(sc, FE_BMPR16);
			if (fe_inb(sc, FE_BMPR17) & FE_B17_DATA)
				val |= bit;
		}
		*data++ = val;
	}

#if 0
	/* Reset the EEPROM interface, again.  */
	fe_outb(sc, FE_BMPR16, 0x00);
	fe_outb(sc, FE_BMPR17, 0x00);
#else
	/* Make sure to restore the original value of EEPROM interface
           registers, since we are not yet sure we have MB86965A on
           the address.  */
	fe_outb(sc, FE_BMPR17, save17);
	fe_outb(sc, FE_BMPR16, save16);
#endif

#if 1
	/* Report what we got.  */
	if (bootverbose) {
		int i;
		data -= JLI_EEPROM_SIZE;
		for (i = 0; i < JLI_EEPROM_SIZE; i += 16) {
			if_printf(sc->ifp,
			    "EEPROM(JLI):%3x: %16D\n", i, data + i, " ");
		}
	}
#endif
}

void
fe_init_jli (struct fe_softc * sc)
{
	/* "Reset" by writing into a magic location.  */
	DELAY(200);
	fe_outb(sc, 0x1E, fe_inb(sc, 0x1E));
	DELAY(300);
}


/*
 * SSi 78Q8377A support routines.
 */

/*
 * Routines to read all bytes from the config EEPROM through 78Q8377A.
 * It is a MicroWire (3-wire) serial EEPROM with 8-bit address.  (I.e.,
 * 93C56 or 93C66.)
 *
 * As I don't have SSi manuals, (hmm, an old song again!) I'm not exactly
 * sure the following code is correct...  It is just stolen from the
 * C-NET(98)P2 support routine in FreeBSD(98).
 */

void
fe_read_eeprom_ssi (struct fe_softc *sc, u_char *data)
{
	u_char val, bit;
	int n;
	u_char save6, save7, save12;

	/* Save the current value for the DLCR registers we are about
           to destroy.  */
	save6 = fe_inb(sc, FE_DLCR6);
	save7 = fe_inb(sc, FE_DLCR7);

	/* Put the 78Q8377A into a state that we can access the EEPROM.  */
	fe_outb(sc, FE_DLCR6,
	    FE_D6_BBW_WORD | FE_D6_SBW_WORD | FE_D6_DLC_DISABLE);
	fe_outb(sc, FE_DLCR7,
	    FE_D7_BYTSWP_LH | FE_D7_RBS_BMPR | FE_D7_RDYPNS | FE_D7_POWER_UP);

	/* Save the current value for the BMPR12 register, too.  */
	save12 = fe_inb(sc, FE_DLCR12);

	/* Read bytes from EEPROM; two bytes per an iteration.  */
	for (n = 0; n < SSI_EEPROM_SIZE / 2; n++) {

		/* Start EEPROM access  */
		fe_outb(sc, FE_DLCR12, SSI_EEP);
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL);

		/* Send the following four bits to the EEPROM in the
		   specified order: a dummy bit, a start bit, and
		   command bits (10) for READ.  */
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL                    );
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL | SSI_CLK          );	/* 0 */
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL           | SSI_DAT);
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL | SSI_CLK | SSI_DAT);	/* 1 */
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL           | SSI_DAT);
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL | SSI_CLK | SSI_DAT);	/* 1 */
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL                    );
		fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL | SSI_CLK          );	/* 0 */

		/* Pass the iteration count to the chip.  */
		for (bit = 0x80; bit != 0x00; bit >>= 1) {
		    val = ( n & bit ) ? SSI_DAT : 0;
		    fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL           | val);
		    fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL | SSI_CLK | val);
		}

		/* Read a byte.  */
		val = 0;
		for (bit = 0x80; bit != 0x00; bit >>= 1) {
		    fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL);
		    fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL | SSI_CLK);
		    if (fe_inb(sc, FE_DLCR12) & SSI_DIN)
			val |= bit;
		}
		*data++ = val;

		/* Read one more byte.  */
		val = 0;
		for (bit = 0x80; bit != 0x00; bit >>= 1) {
		    fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL);
		    fe_outb(sc, FE_DLCR12, SSI_EEP | SSI_CSL | SSI_CLK);
		    if (fe_inb(sc, FE_DLCR12) & SSI_DIN)
			val |= bit;
		}
		*data++ = val;

		fe_outb(sc, FE_DLCR12, SSI_EEP);
	}

	/* Reset the EEPROM interface.  (For now.)  */
	fe_outb(sc, FE_DLCR12, 0x00);

	/* Restore the saved register values, for the case that we
           didn't have 78Q8377A at the given address.  */
	fe_outb(sc, FE_DLCR12, save12);
	fe_outb(sc, FE_DLCR7, save7);
	fe_outb(sc, FE_DLCR6, save6);

#if 1
	/* Report what we got.  */
	if (bootverbose) {
		int i;
		data -= SSI_EEPROM_SIZE;
		for (i = 0; i < SSI_EEPROM_SIZE; i += 16) {
			if_printf(sc->ifp,
			    "EEPROM(SSI):%3x: %16D\n", i, data + i, " ");
		}
	}
#endif
}

/*
 * TDK/LANX boards support routines.
 */

/* It is assumed that the CLK line is low and SDA is high (float) upon entry.  */
#define LNX_PH(D,K,N) \
	((LNX_SDA_##D | LNX_CLK_##K) << N)
#define LNX_CYCLE(D1,D2,D3,D4,K1,K2,K3,K4) \
	(LNX_PH(D1,K1,0)|LNX_PH(D2,K2,8)|LNX_PH(D3,K3,16)|LNX_PH(D4,K4,24))

#define LNX_CYCLE_START	LNX_CYCLE(HI,LO,LO,HI, HI,HI,LO,LO)
#define LNX_CYCLE_STOP	LNX_CYCLE(LO,LO,HI,HI, LO,HI,HI,LO)
#define LNX_CYCLE_HI	LNX_CYCLE(HI,HI,HI,HI, LO,HI,LO,LO)
#define LNX_CYCLE_LO	LNX_CYCLE(LO,LO,LO,HI, LO,HI,LO,LO)
#define LNX_CYCLE_INIT	LNX_CYCLE(LO,HI,HI,HI, LO,LO,LO,LO)

static void
fe_eeprom_cycle_lnx (struct fe_softc *sc, u_short reg20, u_long cycle)
{
	fe_outb(sc, reg20, (cycle      ) & 0xFF);
	DELAY(15);
	fe_outb(sc, reg20, (cycle >>  8) & 0xFF);
	DELAY(15);
	fe_outb(sc, reg20, (cycle >> 16) & 0xFF);
	DELAY(15);
	fe_outb(sc, reg20, (cycle >> 24) & 0xFF);
	DELAY(15);
}

static u_char
fe_eeprom_receive_lnx (struct fe_softc *sc, u_short reg20)
{
	u_char dat;

	fe_outb(sc, reg20, LNX_CLK_HI | LNX_SDA_FL);
	DELAY(15);
	dat = fe_inb(sc, reg20);
	fe_outb(sc, reg20, LNX_CLK_LO | LNX_SDA_FL);
	DELAY(15);
	return (dat & LNX_SDA_IN);
}

void
fe_read_eeprom_lnx (struct fe_softc *sc, u_char *data)
{
	int i;
	u_char n, bit, val;
	u_char save20;
	u_short reg20 = 0x14;

	save20 = fe_inb(sc, reg20);

	/* NOTE: DELAY() timing constants are approximately three
           times longer (slower) than the required minimum.  This is
           to guarantee a reliable operation under some tough
           conditions...  Fortunately, this routine is only called
           during the boot phase, so the speed is less important than
           stability.  */

#if 1
	/* Reset the X24C01's internal state machine and put it into
	   the IDLE state.  We usually don't need this, but *if*
	   someone (e.g., probe routine of other driver) write some
	   garbage into the register at 0x14, synchronization will be
	   lost, and the normal EEPROM access protocol won't work.
	   Moreover, as there are no easy way to reset, we need a
	   _manoeuvre_ here.  (It even lacks a reset pin, so pushing
	   the RESET button on the PC doesn't help!)  */
	fe_eeprom_cycle_lnx(sc, reg20, LNX_CYCLE_INIT);
	for (i = 0; i < 10; i++)
		fe_eeprom_cycle_lnx(sc, reg20, LNX_CYCLE_START);
	fe_eeprom_cycle_lnx(sc, reg20, LNX_CYCLE_STOP);
	DELAY(10000);
#endif

	/* Issue a start condition.  */
	fe_eeprom_cycle_lnx(sc, reg20, LNX_CYCLE_START);

	/* Send seven bits of the starting address (zero, in this
	   case) and a command bit for READ.  */
	val = 0x01;
	for (bit = 0x80; bit != 0x00; bit >>= 1) {
		if (val & bit) {
			fe_eeprom_cycle_lnx(sc, reg20, LNX_CYCLE_HI);
		} else {
			fe_eeprom_cycle_lnx(sc, reg20, LNX_CYCLE_LO);
		}
	}

	/* Receive an ACK bit.  */
	if (fe_eeprom_receive_lnx(sc, reg20)) {
		/* ACK was not received.  EEPROM is not present (i.e.,
		   this board was not a TDK/LANX) or not working
		   properly.  */
		if (bootverbose) {
			if_printf(sc->ifp,
			    "no ACK received from EEPROM(LNX)\n");
		}
		/* Clear the given buffer to indicate we could not get
                   any info. and return.  */
		bzero(data, LNX_EEPROM_SIZE);
		goto RET;
	}

	/* Read bytes from EEPROM.  */
	for (n = 0; n < LNX_EEPROM_SIZE; n++) {

		/* Read a byte and store it into the buffer.  */
		val = 0x00;
		for (bit = 0x80; bit != 0x00; bit >>= 1) {
			if (fe_eeprom_receive_lnx(sc, reg20))
				val |= bit;
		}
		*data++ = val;

		/* Acknowledge if we have to read more.  */
		if (n < LNX_EEPROM_SIZE - 1) {
			fe_eeprom_cycle_lnx(sc, reg20, LNX_CYCLE_LO);
		}
	}

	/* Issue a STOP condition, de-activating the clock line.
	   It will be safer to keep the clock line low than to leave
	   it high.  */
	fe_eeprom_cycle_lnx(sc, reg20, LNX_CYCLE_STOP);

    RET:
	fe_outb(sc, reg20, save20);
	
#if 1
	/* Report what we got.  */
	if (bootverbose) {
		data -= LNX_EEPROM_SIZE;
		for (i = 0; i < LNX_EEPROM_SIZE; i += 16) {
			if_printf(sc->ifp,
			     "EEPROM(LNX):%3x: %16D\n", i, data + i, " ");
		}
	}
#endif
}

void
fe_init_lnx (struct fe_softc * sc)
{
	/* Reset the 86960.  Do we need this?  FIXME.  */
	fe_outb(sc, 0x12, 0x06);
	DELAY(100);
	fe_outb(sc, 0x12, 0x07);
	DELAY(100);

	/* Setup IRQ control register on the ASIC.  */
	fe_outb(sc, 0x14, sc->priv_info);
}


/*
 * Ungermann-Bass boards support routine.
 */
void
fe_init_ubn (struct fe_softc * sc)
{
 	/* Do we need this?  FIXME.  */
	fe_outb(sc, FE_DLCR7,
		sc->proto_dlcr7 | FE_D7_RBS_BMPR | FE_D7_POWER_UP);
 	fe_outb(sc, 0x18, 0x00);
 	DELAY(200);

	/* Setup IRQ control register on the ASIC.  */
	fe_outb(sc, 0x14, sc->priv_info);
}


/*
 * Install interface into kernel networking data structures
 */
int
fe_attach (device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);
	struct ifnet *ifp;
	int flags = device_get_flags(dev);
	int b, error;
	
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not ifalloc\n");
		fe_release_resource(dev);
		return (ENOSPC);
	}

	mtx_init(&sc->lock, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->timer, &sc->lock, 0);

	/*
	 * Initialize ifnet structure
	 */
 	ifp->if_softc    = sc;
	if_initname(sc->ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_start    = fe_start;
	ifp->if_ioctl    = fe_ioctl;
	ifp->if_init     = fe_init;
	ifp->if_linkmib  = &sc->mibdata;
	ifp->if_linkmiblen = sizeof (sc->mibdata);

#if 0 /* I'm not sure... */
	sc->mibdata.dot3Compliance = DOT3COMPLIANCE_COLLS;
#endif

	/*
	 * Set fixed interface flags.
	 */
 	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);

#if FE_SINGLE_TRANSMISSION
	/* Override txb config to allocate minimum.  */
	sc->proto_dlcr6 &= ~FE_D6_TXBSIZ
	sc->proto_dlcr6 |=  FE_D6_TXBSIZ_2x2KB;
#endif

	/* Modify hardware config if it is requested.  */
	if (flags & FE_FLAGS_OVERRIDE_DLCR6)
		sc->proto_dlcr6 = flags & FE_FLAGS_DLCR6_VALUE;

	/* Find TX buffer size, based on the hardware dependent proto.  */
	switch (sc->proto_dlcr6 & FE_D6_TXBSIZ) {
	  case FE_D6_TXBSIZ_2x2KB: sc->txb_size = 2048; break;
	  case FE_D6_TXBSIZ_2x4KB: sc->txb_size = 4096; break;
	  case FE_D6_TXBSIZ_2x8KB: sc->txb_size = 8192; break;
	  default:
		/* Oops, we can't work with single buffer configuration.  */
		if (bootverbose) {
			if_printf(sc->ifp,
			     "strange TXBSIZ config; fixing\n");
		}
		sc->proto_dlcr6 &= ~FE_D6_TXBSIZ;
		sc->proto_dlcr6 |=  FE_D6_TXBSIZ_2x2KB;
		sc->txb_size = 2048;
		break;
	}

	/* Initialize the if_media interface.  */
	ifmedia_init(&sc->media, 0, fe_medchange, fe_medstat);
	for (b = 0; bit2media[b] != 0; b++) {
		if (sc->mbitmap & (1 << b)) {
			ifmedia_add(&sc->media, bit2media[b], 0, NULL);
		}
	}
	for (b = 0; bit2media[b] != 0; b++) {
		if (sc->defmedia & (1 << b)) {
			ifmedia_set(&sc->media, bit2media[b]);
			break;
		}
	}
#if 0	/* Turned off; this is called later, when the interface UPs.  */
	fe_medchange(sc);
#endif

	/* Attach and stop the interface. */
	FE_LOCK(sc);
	fe_stop(sc);
	FE_UNLOCK(sc);
	ether_ifattach(sc->ifp, sc->enaddr);

	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
			       NULL, fe_intr, sc, &sc->irq_handle);
	if (error) {
		ether_ifdetach(ifp);
		mtx_destroy(&sc->lock);
		if_free(ifp);
		fe_release_resource(dev);
		return ENXIO;
	}

  	/* Print additional info when attached.  */
 	device_printf(dev, "type %s%s\n", sc->typestr,
		      (sc->proto_dlcr4 & FE_D4_DSC) ? ", full duplex" : "");
	if (bootverbose) {
		int buf, txb, bbw, sbw, ram;

		buf = txb = bbw = sbw = ram = -1;
		switch ( sc->proto_dlcr6 & FE_D6_BUFSIZ ) {
		  case FE_D6_BUFSIZ_8KB:  buf =  8; break;
		  case FE_D6_BUFSIZ_16KB: buf = 16; break;
		  case FE_D6_BUFSIZ_32KB: buf = 32; break;
		  case FE_D6_BUFSIZ_64KB: buf = 64; break;
		}
		switch ( sc->proto_dlcr6 & FE_D6_TXBSIZ ) {
		  case FE_D6_TXBSIZ_2x2KB: txb = 2; break;
		  case FE_D6_TXBSIZ_2x4KB: txb = 4; break;
		  case FE_D6_TXBSIZ_2x8KB: txb = 8; break;
		}
		switch ( sc->proto_dlcr6 & FE_D6_BBW ) {
		  case FE_D6_BBW_BYTE: bbw =  8; break;
		  case FE_D6_BBW_WORD: bbw = 16; break;
		}
		switch ( sc->proto_dlcr6 & FE_D6_SBW ) {
		  case FE_D6_SBW_BYTE: sbw =  8; break;
		  case FE_D6_SBW_WORD: sbw = 16; break;
		}
		switch ( sc->proto_dlcr6 & FE_D6_SRAM ) {
		  case FE_D6_SRAM_100ns: ram = 100; break;
		  case FE_D6_SRAM_150ns: ram = 150; break;
		}
		device_printf(dev, "SRAM %dKB %dbit %dns, TXB %dKBx2, %dbit I/O\n",
			      buf, bbw, ram, txb, sbw);
	}
	if (sc->stability & UNSTABLE_IRQ)
		device_printf(dev, "warning: IRQ number may be incorrect\n");
	if (sc->stability & UNSTABLE_MAC)
		device_printf(dev, "warning: above MAC address may be incorrect\n");
	if (sc->stability & UNSTABLE_TYPE)
		device_printf(dev, "warning: hardware type was not validated\n");

	gone_by_fcp101_dev(dev);

	return 0;
}

int
fe_alloc_port(device_t dev, int size)
{
	struct fe_softc *sc = device_get_softc(dev);
	struct resource *res;
	int rid;

	rid = 0;
	res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
					  size, RF_ACTIVE);
	if (res) {
		sc->port_used = size;
		sc->port_res = res;
		return (0);
	}

	return (ENOENT);
}

int
fe_alloc_irq(device_t dev, int flags)
{
	struct fe_softc *sc = device_get_softc(dev);
	struct resource *res;
	int rid;

	rid = 0;
	res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE | flags);
	if (res) {
		sc->irq_res = res;
		return (0);
	}

	return (ENOENT);
}

void
fe_release_resource(device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);

	if (sc->port_res) {
		bus_release_resource(dev, SYS_RES_IOPORT, 0, sc->port_res);
		sc->port_res = NULL;
	}
	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
		sc->irq_res = NULL;
	}
}

/*
 * Reset interface, after some (hardware) trouble is deteced.
 */
static void
fe_reset (struct fe_softc *sc)
{
	/* Record how many packets are lost by this accident.  */
	if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, sc->txb_sched + sc->txb_count);
	sc->mibdata.dot3StatsInternalMacTransmitErrors++;

	/* Put the interface into known initial state.  */
	fe_stop(sc);
	if (sc->ifp->if_flags & IFF_UP)
		fe_init_locked(sc);
}

/*
 * Stop everything on the interface.
 *
 * All buffered packets, both transmitting and receiving,
 * if any, will be lost by stopping the interface.
 */
void
fe_stop (struct fe_softc *sc)
{

	FE_ASSERT_LOCKED(sc);

	/* Disable interrupts.  */
	fe_outb(sc, FE_DLCR2, 0x00);
	fe_outb(sc, FE_DLCR3, 0x00);

	/* Stop interface hardware.  */
	DELAY(200);
	fe_outb(sc, FE_DLCR6, sc->proto_dlcr6 | FE_D6_DLC_DISABLE);
	DELAY(200);

	/* Clear all interrupt status.  */
	fe_outb(sc, FE_DLCR0, 0xFF);
	fe_outb(sc, FE_DLCR1, 0xFF);

	/* Put the chip in stand-by mode.  */
	DELAY(200);
	fe_outb(sc, FE_DLCR7, sc->proto_dlcr7 | FE_D7_POWER_DOWN);
	DELAY(200);

	/* Reset transmitter variables and interface flags.  */
	sc->ifp->if_drv_flags &= ~(IFF_DRV_OACTIVE | IFF_DRV_RUNNING);
	sc->tx_timeout = 0;
	callout_stop(&sc->timer);
	sc->txb_free = sc->txb_size;
	sc->txb_count = 0;
	sc->txb_sched = 0;

	/* MAR loading can be delayed.  */
	sc->filter_change = 0;

	/* Call a device-specific hook.  */
	if (sc->stop)
		sc->stop(sc);
}

/*
 * Device timeout/watchdog routine. Entered if the device neglects to
 * generate an interrupt after a transmit has been started on it.
 */
static void
fe_watchdog (void *arg)
{
	struct fe_softc *sc = arg;

	FE_ASSERT_LOCKED(sc);

	if (sc->tx_timeout && --sc->tx_timeout == 0) {
		struct ifnet *ifp = sc->ifp;

		/* A "debug" message.  */
		if_printf(ifp, "transmission timeout (%d+%d)%s\n",
		    sc->txb_sched, sc->txb_count,
		    (ifp->if_flags & IFF_UP) ? "" : " when down");
		if (ifp->if_get_counter(ifp, IFCOUNTER_OPACKETS) == 0 &&
		    ifp->if_get_counter(ifp, IFCOUNTER_IPACKETS) == 0)
			if_printf(ifp, "wrong IRQ setting in config?\n");
		fe_reset(sc);
	}
	callout_reset(&sc->timer, hz, fe_watchdog, sc);
}

/*
 * Initialize device.
 */
static void
fe_init (void * xsc)
{
	struct fe_softc *sc = xsc;

	FE_LOCK(sc);
	fe_init_locked(sc);
	FE_UNLOCK(sc);
}

static void
fe_init_locked (struct fe_softc *sc)
{

	/* Start initializing 86960.  */

	/* Call a hook before we start initializing the chip.  */
	if (sc->init)
		sc->init(sc);

	/*
	 * Make sure to disable the chip, also.
	 * This may also help re-programming the chip after
	 * hot insertion of PCMCIAs.
	 */
	DELAY(200);
	fe_outb(sc, FE_DLCR6, sc->proto_dlcr6 | FE_D6_DLC_DISABLE);
	DELAY(200);

	/* Power up the chip and select register bank for DLCRs.  */
	DELAY(200);
	fe_outb(sc, FE_DLCR7,
		sc->proto_dlcr7 | FE_D7_RBS_DLCR | FE_D7_POWER_UP);
	DELAY(200);

	/* Feed the station address.  */
	fe_outblk(sc, FE_DLCR8, IF_LLADDR(sc->ifp), ETHER_ADDR_LEN);

	/* Clear multicast address filter to receive nothing.  */
	fe_outb(sc, FE_DLCR7,
		sc->proto_dlcr7 | FE_D7_RBS_MAR | FE_D7_POWER_UP);
	fe_outblk(sc, FE_MAR8, fe_filter_nothing.data, FE_FILTER_LEN);

	/* Select the BMPR bank for runtime register access.  */
	fe_outb(sc, FE_DLCR7,
		sc->proto_dlcr7 | FE_D7_RBS_BMPR | FE_D7_POWER_UP);

	/* Initialize registers.  */
	fe_outb(sc, FE_DLCR0, 0xFF);	/* Clear all bits.  */
	fe_outb(sc, FE_DLCR1, 0xFF);	/* ditto.  */
	fe_outb(sc, FE_DLCR2, 0x00);
	fe_outb(sc, FE_DLCR3, 0x00);
	fe_outb(sc, FE_DLCR4, sc->proto_dlcr4);
	fe_outb(sc, FE_DLCR5, sc->proto_dlcr5);
	fe_outb(sc, FE_BMPR10, 0x00);
	fe_outb(sc, FE_BMPR11, FE_B11_CTRL_SKIP | FE_B11_MODE1);
	fe_outb(sc, FE_BMPR12, 0x00);
	fe_outb(sc, FE_BMPR13, sc->proto_bmpr13);
	fe_outb(sc, FE_BMPR14, 0x00);
	fe_outb(sc, FE_BMPR15, 0x00);

	/* Enable interrupts.  */
	fe_outb(sc, FE_DLCR2, FE_TMASK);
	fe_outb(sc, FE_DLCR3, FE_RMASK);

	/* Select requested media, just before enabling DLC.  */
	if (sc->msel)
		sc->msel(sc);

	/* Enable transmitter and receiver.  */
	DELAY(200);
	fe_outb(sc, FE_DLCR6, sc->proto_dlcr6 | FE_D6_DLC_ENABLE);
	DELAY(200);

#ifdef DIAGNOSTIC
	/*
	 * Make sure to empty the receive buffer.
	 *
	 * This may be redundant, but *if* the receive buffer were full
	 * at this point, then the driver would hang.  I have experienced
	 * some strange hang-up just after UP.  I hope the following
	 * code solve the problem.
	 *
	 * I have changed the order of hardware initialization.
	 * I think the receive buffer cannot have any packets at this
	 * point in this version.  The following code *must* be
	 * redundant now.  FIXME.
	 *
	 * I've heard a rumore that on some PC Card implementation of
	 * 8696x, the receive buffer can have some data at this point.
	 * The following message helps discovering the fact.  FIXME.
	 */
	if (!(fe_inb(sc, FE_DLCR5) & FE_D5_BUFEMP)) {
		if_printf(sc->ifp,
		    "receive buffer has some data after reset\n");
		fe_emptybuffer(sc);
	}

	/* Do we need this here?  Actually, no.  I must be paranoia.  */
	fe_outb(sc, FE_DLCR0, 0xFF);	/* Clear all bits.  */
	fe_outb(sc, FE_DLCR1, 0xFF);	/* ditto.  */
#endif

	/* Set 'running' flag, because we are now running.   */
	sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	callout_reset(&sc->timer, hz, fe_watchdog, sc);

	/*
	 * At this point, the interface is running properly,
	 * except that it receives *no* packets.  we then call
	 * fe_setmode() to tell the chip what packets to be
	 * received, based on the if_flags and multicast group
	 * list.  It completes the initialization process.
	 */
	fe_setmode(sc);

#if 0
	/* ...and attempt to start output queued packets.  */
	/* TURNED OFF, because the semi-auto media prober wants to UP
           the interface keeping it idle.  The upper layer will soon
           start the interface anyway, and there are no significant
           delay.  */
	fe_start_locked(sc->ifp);
#endif
}

/*
 * This routine actually starts the transmission on the interface
 */
static void
fe_xmit (struct fe_softc *sc)
{
	/*
	 * Set a timer just in case we never hear from the board again.
	 * We use longer timeout for multiple packet transmission.
	 * I'm not sure this timer value is appropriate.  FIXME.
	 */
	sc->tx_timeout = 1 + sc->txb_count;

	/* Update txb variables.  */
	sc->txb_sched = sc->txb_count;
	sc->txb_count = 0;
	sc->txb_free = sc->txb_size;
	sc->tx_excolls = 0;

	/* Start transmitter, passing packets in TX buffer.  */
	fe_outb(sc, FE_BMPR10, sc->txb_sched | FE_B10_START);
}

/*
 * Start output on interface.
 * We make one assumption here:
 *  1) that the IFF_DRV_OACTIVE flag is checked before this code is called
 *     (i.e. that the output part of the interface is idle)
 */
static void
fe_start (struct ifnet *ifp)
{
	struct fe_softc *sc = ifp->if_softc;

	FE_LOCK(sc);
	fe_start_locked(ifp);
	FE_UNLOCK(sc);
}

static void
fe_start_locked (struct ifnet *ifp)
{
	struct fe_softc *sc = ifp->if_softc;
	struct mbuf *m;

#ifdef DIAGNOSTIC
	/* Just a sanity check.  */
	if ((sc->txb_count == 0) != (sc->txb_free == sc->txb_size)) {
		/*
		 * Txb_count and txb_free co-works to manage the
		 * transmission buffer.  Txb_count keeps track of the
		 * used potion of the buffer, while txb_free does unused
		 * potion.  So, as long as the driver runs properly,
		 * txb_count is zero if and only if txb_free is same
		 * as txb_size (which represents whole buffer.)
		 */
		if_printf(ifp, "inconsistent txb variables (%d, %d)\n",
			sc->txb_count, sc->txb_free);
		/*
		 * So, what should I do, then?
		 *
		 * We now know txb_count and txb_free contradicts.  We
		 * cannot, however, tell which is wrong.  More
		 * over, we cannot peek 86960 transmission buffer or
		 * reset the transmission buffer.  (In fact, we can
		 * reset the entire interface.  I don't want to do it.)
		 *
		 * If txb_count is incorrect, leaving it as-is will cause
		 * sending of garbage after next interrupt.  We have to
		 * avoid it.  Hence, we reset the txb_count here.  If
		 * txb_free was incorrect, resetting txb_count just loses
		 * some packets.  We can live with it.
		 */
		sc->txb_count = 0;
	}
#endif

	/*
	 * First, see if there are buffered packets and an idle
	 * transmitter - should never happen at this point.
	 */
	if ((sc->txb_count > 0) && (sc->txb_sched == 0)) {
		if_printf(ifp, "transmitter idle with %d buffered packets\n",
		       sc->txb_count);
		fe_xmit(sc);
	}

	/*
	 * Stop accepting more transmission packets temporarily, when
	 * a filter change request is delayed.  Updating the MARs on
	 * 86960 flushes the transmission buffer, so it is delayed
	 * until all buffered transmission packets have been sent
	 * out.
	 */
	if (sc->filter_change) {
		/*
		 * Filter change request is delayed only when the DLC is
		 * working.  DLC soon raise an interrupt after finishing
		 * the work.
		 */
		goto indicate_active;
	}

	for (;;) {

		/*
		 * See if there is room to put another packet in the buffer.
		 * We *could* do better job by peeking the send queue to
		 * know the length of the next packet.  Current version just
		 * tests against the worst case (i.e., longest packet).  FIXME.
		 *
		 * When adding the packet-peek feature, don't forget adding a
		 * test on txb_count against QUEUEING_MAX.
		 * There is a little chance the packet count exceeds
		 * the limit.  Assume transmission buffer is 8KB (2x8KB
		 * configuration) and an application sends a bunch of small
		 * (i.e., minimum packet sized) packets rapidly.  An 8KB
		 * buffer can hold 130 blocks of 62 bytes long...
		 */
		if (sc->txb_free
		    < ETHER_MAX_LEN - ETHER_CRC_LEN + FE_DATA_LEN_LEN) {
			/* No room.  */
			goto indicate_active;
		}

#if FE_SINGLE_TRANSMISSION
		if (sc->txb_count > 0) {
			/* Just one packet per a transmission buffer.  */
			goto indicate_active;
		}
#endif

		/*
		 * Get the next mbuf chain for a packet to send.
		 */
		IF_DEQUEUE(&sc->ifp->if_snd, m);
		if (m == NULL) {
			/* No more packets to send.  */
			goto indicate_inactive;
		}

		/*
		 * Copy the mbuf chain into the transmission buffer.
		 * txb_* variables are updated as necessary.
		 */
		fe_write_mbufs(sc, m);

		/* Start transmitter if it's idle.  */
		if ((sc->txb_count > 0) && (sc->txb_sched == 0))
			fe_xmit(sc);

		/*
		 * Tap off here if there is a bpf listener,
		 * and the device is *not* in promiscuous mode.
		 * (86960 receives self-generated packets if 
		 * and only if it is in "receive everything"
		 * mode.)
		 */
		if (!(sc->ifp->if_flags & IFF_PROMISC))
			BPF_MTAP(sc->ifp, m);

		m_freem(m);
	}

  indicate_inactive:
	/*
	 * We are using the !OACTIVE flag to indicate to
	 * the outside world that we can accept an
	 * additional packet rather than that the
	 * transmitter is _actually_ active.  Indeed, the
	 * transmitter may be active, but if we haven't
	 * filled all the buffers with data then we still
	 * want to accept more.
	 */
	sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	return;

  indicate_active:
	/*
	 * The transmitter is active, and there are no room for
	 * more outgoing packets in the transmission buffer.
	 */
	sc->ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	return;
}

/*
 * Drop (skip) a packet from receive buffer in 86960 memory.
 */
static void
fe_droppacket (struct fe_softc * sc, int len)
{
	int i;

	/*
	 * 86960 manual says that we have to read 8 bytes from the buffer
	 * before skip the packets and that there must be more than 8 bytes
	 * remaining in the buffer when issue a skip command.
	 * Remember, we have already read 4 bytes before come here.
	 */
	if (len > 12) {
		/* Read 4 more bytes, and skip the rest of the packet.  */
		if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
		{
			(void) fe_inb(sc, FE_BMPR8);
			(void) fe_inb(sc, FE_BMPR8);
			(void) fe_inb(sc, FE_BMPR8);
			(void) fe_inb(sc, FE_BMPR8);
		}
		else
		{
			(void) fe_inw(sc, FE_BMPR8);
			(void) fe_inw(sc, FE_BMPR8);
		}
		fe_outb(sc, FE_BMPR14, FE_B14_SKIP);
	} else {
		/* We should not come here unless receiving RUNTs.  */
		if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
		{
			for (i = 0; i < len; i++)
				(void) fe_inb(sc, FE_BMPR8);
		}
		else
		{
			for (i = 0; i < len; i += 2)
				(void) fe_inw(sc, FE_BMPR8);
		}
	}
}

#ifdef DIAGNOSTIC
/*
 * Empty receiving buffer.
 */
static void
fe_emptybuffer (struct fe_softc * sc)
{
	int i;
	u_char saved_dlcr5;

#ifdef FE_DEBUG
	if_printf(sc->ifp, "emptying receive buffer\n");
#endif

	/*
	 * Stop receiving packets, temporarily.
	 */
	saved_dlcr5 = fe_inb(sc, FE_DLCR5);
	fe_outb(sc, FE_DLCR5, sc->proto_dlcr5);
	DELAY(1300);

	/*
	 * When we come here, the receive buffer management may
	 * have been broken.  So, we cannot use skip operation.
	 * Just discard everything in the buffer.
	 */
	if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
	{
		for (i = 0; i < 65536; i++) {
			if (fe_inb(sc, FE_DLCR5) & FE_D5_BUFEMP)
				break;
			(void) fe_inb(sc, FE_BMPR8);
		}
	}
	else
	{
		for (i = 0; i < 65536; i += 2) {
			if (fe_inb(sc, FE_DLCR5) & FE_D5_BUFEMP)
				break;
			(void) fe_inw(sc, FE_BMPR8);
		}
	}

	/*
	 * Double check.
	 */
	if (fe_inb(sc, FE_DLCR5) & FE_D5_BUFEMP) {
		if_printf(sc->ifp,
		    "could not empty receive buffer\n");
		/* Hmm.  What should I do if this happens?  FIXME.  */
	}

	/*
	 * Restart receiving packets.
	 */
	fe_outb(sc, FE_DLCR5, saved_dlcr5);
}
#endif

/*
 * Transmission interrupt handler
 * The control flow of this function looks silly.  FIXME.
 */
static void
fe_tint (struct fe_softc * sc, u_char tstat)
{
	int left;
	int col;

	/*
	 * Handle "excessive collision" interrupt.
	 */
	if (tstat & FE_D0_COLL16) {

		/*
		 * Find how many packets (including this collided one)
		 * are left unsent in transmission buffer.
		 */
		left = fe_inb(sc, FE_BMPR10);
		if_printf(sc->ifp, "excessive collision (%d/%d)\n",
		       left, sc->txb_sched);

		/*
		 * Clear the collision flag (in 86960) here
		 * to avoid confusing statistics.
		 */
		fe_outb(sc, FE_DLCR0, FE_D0_COLLID);

		/*
		 * Restart transmitter, skipping the
		 * collided packet.
		 *
		 * We *must* skip the packet to keep network running
		 * properly.  Excessive collision error is an
		 * indication of the network overload.  If we
		 * tried sending the same packet after excessive
		 * collision, the network would be filled with
		 * out-of-time packets.  Packets belonging
		 * to reliable transport (such as TCP) are resent
		 * by some upper layer.
		 */
		fe_outb(sc, FE_BMPR11, FE_B11_CTRL_SKIP | FE_B11_MODE1);

		/* Update statistics.  */
		sc->tx_excolls++;
	}

	/*
	 * Handle "transmission complete" interrupt.
	 */
	if (tstat & FE_D0_TXDONE) {

		/*
		 * Add in total number of collisions on last
		 * transmission.  We also clear "collision occurred" flag
		 * here.
		 *
		 * 86960 has a design flaw on collision count on multiple
		 * packet transmission.  When we send two or more packets
		 * with one start command (that's what we do when the
		 * transmission queue is crowded), 86960 informs us number
		 * of collisions occurred on the last packet on the
		 * transmission only.  Number of collisions on previous
		 * packets are lost.  I have told that the fact is clearly
		 * stated in the Fujitsu document.
		 *
		 * I considered not to mind it seriously.  Collision
		 * count is not so important, anyway.  Any comments?  FIXME.
		 */

		if (fe_inb(sc, FE_DLCR0) & FE_D0_COLLID) {

			/* Clear collision flag.  */
			fe_outb(sc, FE_DLCR0, FE_D0_COLLID);

			/* Extract collision count from 86960.  */
			col = fe_inb(sc, FE_DLCR4);
			col = (col & FE_D4_COL) >> FE_D4_COL_SHIFT;
			if (col == 0) {
				/*
				 * Status register indicates collisions,
				 * while the collision count is zero.
				 * This can happen after multiple packet
				 * transmission, indicating that one or more
				 * previous packet(s) had been collided.
				 *
				 * Since the accurate number of collisions
				 * has been lost, we just guess it as 1;
				 * Am I too optimistic?  FIXME.
				 */
				col = 1;
			}
			if_inc_counter(sc->ifp, IFCOUNTER_COLLISIONS, col);
			if (col == 1)
				sc->mibdata.dot3StatsSingleCollisionFrames++;
			else
				sc->mibdata.dot3StatsMultipleCollisionFrames++;
			sc->mibdata.dot3StatsCollFrequencies[col-1]++;
		}

		/*
		 * Update transmission statistics.
		 * Be sure to reflect number of excessive collisions.
		 */
		col = sc->tx_excolls;
		if_inc_counter(sc->ifp, IFCOUNTER_OPACKETS, sc->txb_sched - col);
		if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, col);
		if_inc_counter(sc->ifp, IFCOUNTER_COLLISIONS, col * 16);
		sc->mibdata.dot3StatsExcessiveCollisions += col;
		sc->mibdata.dot3StatsCollFrequencies[15] += col;
		sc->txb_sched = 0;

		/*
		 * The transmitter is no more active.
		 * Reset output active flag and watchdog timer.
		 */
		sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		sc->tx_timeout = 0;

		/*
		 * If more data is ready to transmit in the buffer, start
		 * transmitting them.  Otherwise keep transmitter idle,
		 * even if more data is queued.  This gives receive
		 * process a slight priority.
		 */
		if (sc->txb_count > 0)
			fe_xmit(sc);
	}
}

/*
 * Ethernet interface receiver interrupt.
 */
static void
fe_rint (struct fe_softc * sc, u_char rstat)
{
	u_short len;
	u_char status;
	int i;

	/*
	 * Update statistics if this interrupt is caused by an error.
	 * Note that, when the system was not sufficiently fast, the
	 * receive interrupt might not be acknowledged immediately.  If
	 * one or more errornous frames were received before this routine
	 * was scheduled, they are ignored, and the following error stats
	 * give less than real values.
	 */
	if (rstat & (FE_D1_OVRFLO | FE_D1_CRCERR | FE_D1_ALGERR | FE_D1_SRTPKT)) {
		if (rstat & FE_D1_OVRFLO)
			sc->mibdata.dot3StatsInternalMacReceiveErrors++;
		if (rstat & FE_D1_CRCERR)
			sc->mibdata.dot3StatsFCSErrors++;
		if (rstat & FE_D1_ALGERR)
			sc->mibdata.dot3StatsAlignmentErrors++;
#if 0
		/* The reference MAC receiver defined in 802.3
		   silently ignores short frames (RUNTs) without
		   notifying upper layer.  RFC 1650 (dot3 MIB) is
		   based on the 802.3, and it has no stats entry for
		   RUNTs...  */
		if (rstat & FE_D1_SRTPKT)
			sc->mibdata.dot3StatsFrameTooShorts++; /* :-) */
#endif
		if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
	}

	/*
	 * MB86960 has a flag indicating "receive queue empty."
	 * We just loop, checking the flag, to pull out all received
	 * packets.
	 *
	 * We limit the number of iterations to avoid infinite-loop.
	 * The upper bound is set to unrealistic high value.
	 */
	for (i = 0; i < FE_MAX_RECV_COUNT * 2; i++) {

		/* Stop the iteration if 86960 indicates no packets.  */
		if (fe_inb(sc, FE_DLCR5) & FE_D5_BUFEMP)
			return;

		/*
		 * Extract a receive status byte.
		 * As our 86960 is in 16 bit bus access mode, we have to
		 * use inw() to get the status byte.  The significant
		 * value is returned in lower 8 bits.
		 */
		if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
		{
			status = fe_inb(sc, FE_BMPR8);
			(void) fe_inb(sc, FE_BMPR8);
		}
		else
		{
			status = (u_char) fe_inw(sc, FE_BMPR8);
		}	

		/*
		 * Extract the packet length.
		 * It is a sum of a header (14 bytes) and a payload.
		 * CRC has been stripped off by the 86960.
		 */
		if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
		{
			len  =  fe_inb(sc, FE_BMPR8);
			len |= (fe_inb(sc, FE_BMPR8) << 8);
		}
		else
		{
			len = fe_inw(sc, FE_BMPR8);
		}

		/*
		 * AS our 86960 is programed to ignore errored frame,
		 * we must not see any error indication in the
		 * receive buffer.  So, any error condition is a
		 * serious error, e.g., out-of-sync of the receive
		 * buffer pointers.
		 */
		if ((status & 0xF0) != 0x20 ||
		    len > ETHER_MAX_LEN - ETHER_CRC_LEN ||
		    len < ETHER_MIN_LEN - ETHER_CRC_LEN) {
			if_printf(sc->ifp,
			    "RX buffer out-of-sync\n");
			if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
			sc->mibdata.dot3StatsInternalMacReceiveErrors++;
			fe_reset(sc);
			return;
		}

		/*
		 * Go get a packet.
		 */
		if (fe_get_packet(sc, len) < 0) {
			/*
			 * Negative return from fe_get_packet()
			 * indicates no available mbuf.  We stop
			 * receiving packets, even if there are more
			 * in the buffer.  We hope we can get more
			 * mbuf next time.
			 */
			if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
			sc->mibdata.dot3StatsMissedFrames++;
			fe_droppacket(sc, len);
			return;
		}

		/* Successfully received a packet.  Update stat.  */
		if_inc_counter(sc->ifp, IFCOUNTER_IPACKETS, 1);
	}

	/* Maximum number of frames has been received.  Something
           strange is happening here... */
	if_printf(sc->ifp, "unusual receive flood\n");
	sc->mibdata.dot3StatsInternalMacReceiveErrors++;
	fe_reset(sc);
}

/*
 * Ethernet interface interrupt processor
 */
static void
fe_intr (void *arg)
{
	struct fe_softc *sc = arg;
	u_char tstat, rstat;
	int loop_count = FE_MAX_LOOP;

	FE_LOCK(sc);

	/* Loop until there are no more new interrupt conditions.  */
	while (loop_count-- > 0) {
		/*
		 * Get interrupt conditions, masking unneeded flags.
		 */
		tstat = fe_inb(sc, FE_DLCR0) & FE_TMASK;
		rstat = fe_inb(sc, FE_DLCR1) & FE_RMASK;
		if (tstat == 0 && rstat == 0) {
			FE_UNLOCK(sc);
			return;
		}

		/*
		 * Reset the conditions we are acknowledging.
		 */
		fe_outb(sc, FE_DLCR0, tstat);
		fe_outb(sc, FE_DLCR1, rstat);

		/*
		 * Handle transmitter interrupts.
		 */
		if (tstat)
			fe_tint(sc, tstat);

		/*
		 * Handle receiver interrupts
		 */
		if (rstat)
			fe_rint(sc, rstat);

		/*
		 * Update the multicast address filter if it is
		 * needed and possible.  We do it now, because
		 * we can make sure the transmission buffer is empty,
		 * and there is a good chance that the receive queue
		 * is empty.  It will minimize the possibility of
		 * packet loss.
		 */
		if (sc->filter_change &&
		    sc->txb_count == 0 && sc->txb_sched == 0) {
			fe_loadmar(sc);
			sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		}

		/*
		 * If it looks like the transmitter can take more data,
		 * attempt to start output on the interface. This is done
		 * after handling the receiver interrupt to give the
		 * receive operation priority.
		 *
		 * BTW, I'm not sure in what case the OACTIVE is on at
		 * this point.  Is the following test redundant?
		 *
		 * No.  This routine polls for both transmitter and
		 * receiver interrupts.  86960 can raise a receiver
		 * interrupt when the transmission buffer is full.
		 */
		if ((sc->ifp->if_drv_flags & IFF_DRV_OACTIVE) == 0)
			fe_start_locked(sc->ifp);
	}
	FE_UNLOCK(sc);

	if_printf(sc->ifp, "too many loops\n");
}

/*
 * Process an ioctl request. This code needs some work - it looks
 * pretty ugly.
 */
static int
fe_ioctl (struct ifnet * ifp, u_long command, caddr_t data)
{
	struct fe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (command) {

	  case SIOCSIFFLAGS:
		/*
		 * Switch interface state between "running" and
		 * "stopped", reflecting the UP flag.
		 */
		FE_LOCK(sc);
		if (sc->ifp->if_flags & IFF_UP) {
			if ((sc->ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
				fe_init_locked(sc);
		} else {
			if ((sc->ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				fe_stop(sc);
		}

		/*
		 * Promiscuous and/or multicast flags may have changed,
		 * so reprogram the multicast filter and/or receive mode.
		 */
		fe_setmode(sc);
		FE_UNLOCK(sc);

		/* Done.  */
		break;

	  case SIOCADDMULTI:
	  case SIOCDELMULTI:
		/*
		 * Multicast list has changed; set the hardware filter
		 * accordingly.
		 */
		FE_LOCK(sc);
		fe_setmode(sc);
		FE_UNLOCK(sc);
		break;

	  case SIOCSIFMEDIA:
	  case SIOCGIFMEDIA:
		/* Let if_media to handle these commands and to call
		   us back.  */
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	  default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

/*
 * Retrieve packet from receive buffer and send to the next level up via
 * ether_input().
 * Returns 0 if success, -1 if error (i.e., mbuf allocation failure).
 */
static int
fe_get_packet (struct fe_softc * sc, u_short len)
{
	struct ifnet *ifp = sc->ifp;
	struct ether_header *eh;
	struct mbuf *m;

	FE_ASSERT_LOCKED(sc);

	/*
	 * NFS wants the data be aligned to the word (4 byte)
	 * boundary.  Ethernet header has 14 bytes.  There is a
	 * 2-byte gap.
	 */
#define NFS_MAGIC_OFFSET 2

	/*
	 * This function assumes that an Ethernet packet fits in an
	 * mbuf (with a cluster attached when necessary.)  On FreeBSD
	 * 2.0 for x86, which is the primary target of this driver, an
	 * mbuf cluster has 4096 bytes, and we are happy.  On ancient
	 * BSDs, such as vanilla 4.3 for 386, a cluster size was 1024,
	 * however.  If the following #error message were printed upon
	 * compile, you need to rewrite this function.
	 */
#if ( MCLBYTES < ETHER_MAX_LEN - ETHER_CRC_LEN + NFS_MAGIC_OFFSET )
#error "Too small MCLBYTES to use fe driver."
#endif

	/*
	 * Our strategy has one more problem.  There is a policy on
	 * mbuf cluster allocation.  It says that we must have at
	 * least MINCLSIZE (208 bytes on FreeBSD 2.0 for x86) to
	 * allocate a cluster.  For a packet of a size between
	 * (MHLEN - 2) to (MINCLSIZE - 2), our code violates the rule...
	 * On the other hand, the current code is short, simple,
	 * and fast, however.  It does no harmful thing, just waists
	 * some memory.  Any comments?  FIXME.
	 */

	/* Allocate an mbuf with packet header info.  */
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return -1;

	/* Attach a cluster if this packet doesn't fit in a normal mbuf.  */
	if (len > MHLEN - NFS_MAGIC_OFFSET) {
		if (!(MCLGET(m, M_NOWAIT))) {
			m_freem(m);
			return -1;
		}
	}

	/* Initialize packet header info.  */
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = len;

	/* Set the length of this packet.  */
	m->m_len = len;

	/* The following silliness is to make NFS happy */
	m->m_data += NFS_MAGIC_OFFSET;

	/* Get (actually just point to) the header part.  */
	eh = mtod(m, struct ether_header *);

	/* Get a packet.  */
	if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
	{
		fe_insb(sc, FE_BMPR8, (u_int8_t *)eh, len);
	}
	else
	{
		fe_insw(sc, FE_BMPR8, (u_int16_t *)eh, (len + 1) >> 1);
	}

	/* Feed the packet to upper layer.  */
	FE_UNLOCK(sc);
	(*ifp->if_input)(ifp, m);
	FE_LOCK(sc);
	return 0;
}

/*
 * Write an mbuf chain to the transmission buffer memory using 16 bit PIO.
 * Returns number of bytes actually written, including length word.
 *
 * If an mbuf chain is too long for an Ethernet frame, it is not sent.
 * Packets shorter than Ethernet minimum are legal, and we pad them
 * before sending out.  An exception is "partial" packets which are
 * shorter than mandatory Ethernet header.
 */
static void
fe_write_mbufs (struct fe_softc *sc, struct mbuf *m)
{
	u_short length, len;
	struct mbuf *mp;
	u_char *data;
	u_short savebyte;	/* WARNING: Architecture dependent!  */
#define NO_PENDING_BYTE 0xFFFF

	static u_char padding [ETHER_MIN_LEN - ETHER_CRC_LEN - ETHER_HDR_LEN];

#ifdef DIAGNOSTIC
	/* First, count up the total number of bytes to copy */
	length = 0;
	for (mp = m; mp != NULL; mp = mp->m_next)
		length += mp->m_len;

	/* Check if this matches the one in the packet header.  */
	if (length != m->m_pkthdr.len) {
		if_printf(sc->ifp,
		    "packet length mismatch? (%d/%d)\n",
		    length, m->m_pkthdr.len);
	}
#else
	/* Just use the length value in the packet header.  */
	length = m->m_pkthdr.len;
#endif

#ifdef DIAGNOSTIC
	/*
	 * Should never send big packets.  If such a packet is passed,
	 * it should be a bug of upper layer.  We just ignore it.
	 * ... Partial (too short) packets, neither.
	 */
	if (length < ETHER_HDR_LEN ||
	    length > ETHER_MAX_LEN - ETHER_CRC_LEN) {
		if_printf(sc->ifp,
		    "got an out-of-spec packet (%u bytes) to send\n", length);
		if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, 1);
		sc->mibdata.dot3StatsInternalMacTransmitErrors++;
		return;
	}
#endif

	/*
	 * Put the length word for this frame.
	 * Does 86960 accept odd length?  -- Yes.
	 * Do we need to pad the length to minimum size by ourselves?
	 * -- Generally yes.  But for (or will be) the last
	 * packet in the transmission buffer, we can skip the
	 * padding process.  It may gain performance slightly.  FIXME.
	 */
	if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
	{
		len = max(length, ETHER_MIN_LEN - ETHER_CRC_LEN);
		fe_outb(sc, FE_BMPR8,  len & 0x00ff);
		fe_outb(sc, FE_BMPR8, (len & 0xff00) >> 8);
	}
	else
	{
		fe_outw(sc, FE_BMPR8,
			max(length, ETHER_MIN_LEN - ETHER_CRC_LEN));
	}

	/*
	 * Update buffer status now.
	 * Truncate the length up to an even number, since we use outw().
	 */
	if ((sc->proto_dlcr6 & FE_D6_SBW) != FE_D6_SBW_BYTE)
	{
		length = (length + 1) & ~1;
	}
	sc->txb_free -= FE_DATA_LEN_LEN +
	    max(length, ETHER_MIN_LEN - ETHER_CRC_LEN);
	sc->txb_count++;

	/*
	 * Transfer the data from mbuf chain to the transmission buffer.
	 * MB86960 seems to require that data be transferred as words, and
	 * only words.  So that we require some extra code to patch
	 * over odd-length mbufs.
	 */
	if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
	{
		/* 8-bit cards are easy.  */
		for (mp = m; mp != NULL; mp = mp->m_next) {
			if (mp->m_len)
				fe_outsb(sc, FE_BMPR8, mtod(mp, caddr_t),
					 mp->m_len);
		}
	}
	else
	{
		/* 16-bit cards are a pain.  */
		savebyte = NO_PENDING_BYTE;
		for (mp = m; mp != NULL; mp = mp->m_next) {

			/* Ignore empty mbuf.  */
			len = mp->m_len;
			if (len == 0)
				continue;

			/* Find the actual data to send.  */
			data = mtod(mp, caddr_t);

			/* Finish the last byte.  */
			if (savebyte != NO_PENDING_BYTE) {
				fe_outw(sc, FE_BMPR8, savebyte | (*data << 8));
				data++;
				len--;
				savebyte = NO_PENDING_BYTE;
			}

			/* output contiguous words */
			if (len > 1) {
				fe_outsw(sc, FE_BMPR8, (u_int16_t *)data,
					 len >> 1);
				data += len & ~1;
				len &= 1;
			}

			/* Save a remaining byte, if there is one.  */
			if (len > 0)
				savebyte = *data;
		}

		/* Spit the last byte, if the length is odd.  */
		if (savebyte != NO_PENDING_BYTE)
			fe_outw(sc, FE_BMPR8, savebyte);
	}

	/* Pad to the Ethernet minimum length, if the packet is too short.  */
	if (length < ETHER_MIN_LEN - ETHER_CRC_LEN) {
		if ((sc->proto_dlcr6 & FE_D6_SBW) == FE_D6_SBW_BYTE)
		{
			fe_outsb(sc, FE_BMPR8, padding,
				 ETHER_MIN_LEN - ETHER_CRC_LEN - length);
		}
		else
		{
			fe_outsw(sc, FE_BMPR8, (u_int16_t *)padding,
				 (ETHER_MIN_LEN - ETHER_CRC_LEN - length) >> 1);
		}
	}
}

/*
 * Compute the multicast address filter from the
 * list of multicast addresses we need to listen to.
 */
static struct fe_filter
fe_mcaf ( struct fe_softc *sc )
{
	int index;
	struct fe_filter filter;
	struct ifmultiaddr *ifma;

	filter = fe_filter_nothing;
	if_maddr_rlock(sc->ifp);
	CK_STAILQ_FOREACH(ifma, &sc->ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		index = ether_crc32_le(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
#ifdef FE_DEBUG
		if_printf(sc->ifp, "hash(%6D) == %d\n",
			enm->enm_addrlo , ":", index);
#endif

		filter.data[index >> 3] |= 1 << (index & 7);
	}
	if_maddr_runlock(sc->ifp);
	return ( filter );
}

/*
 * Calculate a new "multicast packet filter" and put the 86960
 * receiver in appropriate mode.
 */
static void
fe_setmode (struct fe_softc *sc)
{

	/*
	 * If the interface is not running, we postpone the update
	 * process for receive modes and multicast address filter
	 * until the interface is restarted.  It reduces some
	 * complicated job on maintaining chip states.  (Earlier versions
	 * of this driver had a bug on that point...)
	 *
	 * To complete the trick, fe_init() calls fe_setmode() after
	 * restarting the interface.
	 */
	if (!(sc->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return;

	/*
	 * Promiscuous mode is handled separately.
	 */
	if (sc->ifp->if_flags & IFF_PROMISC) {
		/*
		 * Program 86960 to receive all packets on the segment
		 * including those directed to other stations.
		 * Multicast filter stored in MARs are ignored
		 * under this setting, so we don't need to update it.
		 *
		 * Promiscuous mode in FreeBSD 2 is used solely by
		 * BPF, and BPF only listens to valid (no error) packets.
		 * So, we ignore erroneous ones even in this mode.
		 * (Older versions of fe driver mistook the point.)
		 */
		fe_outb(sc, FE_DLCR5,
			sc->proto_dlcr5 | FE_D5_AFM0 | FE_D5_AFM1);
		sc->filter_change = 0;
		return;
	}

	/*
	 * Turn the chip to the normal (non-promiscuous) mode.
	 */
	fe_outb(sc, FE_DLCR5, sc->proto_dlcr5 | FE_D5_AFM1);

	/*
	 * Find the new multicast filter value.
	 */
	if (sc->ifp->if_flags & IFF_ALLMULTI)
		sc->filter = fe_filter_all;
	else
		sc->filter = fe_mcaf(sc);
	sc->filter_change = 1;

	/*
	 * We have to update the multicast filter in the 86960, A.S.A.P.
	 *
	 * Note that the DLC (Data Link Control unit, i.e. transmitter
	 * and receiver) must be stopped when feeding the filter, and
	 * DLC trashes all packets in both transmission and receive
	 * buffers when stopped.
	 *
	 * To reduce the packet loss, we delay the filter update
	 * process until buffers are empty.
	 */
	if (sc->txb_sched == 0 && sc->txb_count == 0 &&
	    !(fe_inb(sc, FE_DLCR1) & FE_D1_PKTRDY)) {
		/*
		 * Buffers are (apparently) empty.  Load
		 * the new filter value into MARs now.
		 */
		fe_loadmar(sc);
	} else {
		/*
		 * Buffers are not empty.  Mark that we have to update
		 * the MARs.  The new filter will be loaded by feintr()
		 * later.
		 */
	}
}

/*
 * Load a new multicast address filter into MARs.
 *
 * The caller must have acquired the softc lock before fe_loadmar.
 * This function starts the DLC upon return.  So it can be called only
 * when the chip is working, i.e., from the driver's point of view, when
 * a device is RUNNING.  (I mistook the point in previous versions.)
 */
static void
fe_loadmar (struct fe_softc * sc)
{
	/* Stop the DLC (transmitter and receiver).  */
	DELAY(200);
	fe_outb(sc, FE_DLCR6, sc->proto_dlcr6 | FE_D6_DLC_DISABLE);
	DELAY(200);

	/* Select register bank 1 for MARs.  */
	fe_outb(sc, FE_DLCR7, sc->proto_dlcr7 | FE_D7_RBS_MAR | FE_D7_POWER_UP);

	/* Copy filter value into the registers.  */
	fe_outblk(sc, FE_MAR8, sc->filter.data, FE_FILTER_LEN);

	/* Restore the bank selection for BMPRs (i.e., runtime registers).  */
	fe_outb(sc, FE_DLCR7,
		sc->proto_dlcr7 | FE_D7_RBS_BMPR | FE_D7_POWER_UP);

	/* Restart the DLC.  */
	DELAY(200);
	fe_outb(sc, FE_DLCR6, sc->proto_dlcr6 | FE_D6_DLC_ENABLE);
	DELAY(200);

	/* We have just updated the filter.  */
	sc->filter_change = 0;
}

/* Change the media selection.  */
static int
fe_medchange (struct ifnet *ifp)
{
	struct fe_softc *sc = (struct fe_softc *)ifp->if_softc;

#ifdef DIAGNOSTIC
	/* If_media should not pass any request for a media which this
	   interface doesn't support.  */
	int b;

	for (b = 0; bit2media[b] != 0; b++) {
		if (bit2media[b] == sc->media.ifm_media) break;
	}
	if (((1 << b) & sc->mbitmap) == 0) {
		if_printf(sc->ifp,
		    "got an unsupported media request (0x%x)\n",
		    sc->media.ifm_media);
		return EINVAL;
	}
#endif

	/* We don't actually change media when the interface is down.
	   fe_init() will do the job, instead.  Should we also wait
	   until the transmission buffer being empty?  Changing the
	   media when we are sending a frame will cause two garbages
	   on wires, one on old media and another on new.  FIXME */
	FE_LOCK(sc);
	if (sc->ifp->if_flags & IFF_UP) {
		if (sc->msel) sc->msel(sc);
	}
	FE_UNLOCK(sc);

	return 0;
}

/* I don't know how I can support media status callback... FIXME.  */
static void
fe_medstat (struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct fe_softc *sc = ifp->if_softc;

	ifmr->ifm_active = sc->media.ifm_media;
}
