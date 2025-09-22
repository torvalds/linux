/*	$OpenBSD: aic7xxx_seeprom.c,v 1.10 2024/09/01 03:08:56 jsg Exp $	*/
/*	$NetBSD: aic7xxx_seeprom.c,v 1.8 2003/05/02 19:12:19 dyoung Exp $	*/

/*
 * Product specific probe and attach routines for:
 *      3940, 2940, aic7895, aic7890, aic7880,
 *      aic7870, aic7860 and aic7850 SCSI controllers
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * This file was originally split off from the PCI code by
 * Jason Thorpe <thorpej@netbsd.org>. This version was split off
 * from the FreeBSD source file aic7xxx_pci.c by Frank van der Linden
 * <fvdl@netbsd.org>
 *
 * $Id: aic7xxx_seeprom.c,v 1.10 2024/09/01 03:08:56 jsg Exp $
 *
 * $FreeBSD: src/sys/dev/aic7xxx/aic7xxx_pci.c,v 1.22 2003/01/20 20:44:55 gibbs Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/reboot.h>		/* for AB_* needed by bootverbose */

#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/aic7xxx_openbsd.h>
#include <dev/ic/aic7xxx_inline.h>

#include <dev/ic/smc93cx6var.h>

#define DEVCONFIG	0x40
#define STPWLEVEL	0x00000002

static void configure_termination(struct ahc_softc *,
				  struct seeprom_descriptor *, u_int, u_int *);
static int verify_seeprom_cksum(struct seeprom_config *sc);

static void ahc_new_term_detect(struct ahc_softc *, int *, int *, int *,
				   int *, int *);
static void aic787X_cable_detect(struct ahc_softc *, int *, int *, int *,
				 int *);
static void aic785X_cable_detect(struct ahc_softc *, int *, int *, int *);
static void write_brdctl(struct ahc_softc *, u_int8_t);
static u_int8_t read_brdctl(struct ahc_softc *);
static void ahc_parse_pci_eeprom(struct ahc_softc *, struct seeprom_config *);

/*
 * Check the external port logic for a serial eeprom
 * and termination/cable detection controls.
 */
void
ahc_check_extport(struct ahc_softc *ahc, u_int *sxfrctl1)
{
	struct	seeprom_descriptor sd;
	struct	seeprom_config *sc;
	int	have_seeprom;
	int	have_autoterm;

	sd.sd_tag = ahc->tag;
	sd.sd_bsh = ahc->bsh;
	sd.sd_regsize = 1;
	sd.sd_control_offset = SEECTL;
	sd.sd_status_offset = SEECTL;
	sd.sd_dataout_offset = SEECTL;
	sc = ahc->seep_config;

	/*
	 * For some multi-channel devices, the c46 is simply too
	 * small to work.  For the other controller types, we can
	 * get our information from either SEEPROM type.  Set the
	 * type to start our probe with accordingly.
	 */
	if (ahc->flags & AHC_LARGE_SEEPROM)
		sd.sd_chip = C56_66;
	else
		sd.sd_chip = C46;

	sd.sd_MS = SEEMS;
	sd.sd_RDY = SEERDY;
	sd.sd_CS = SEECS;
	sd.sd_CK = SEECK;
	sd.sd_DO = SEEDO;
	sd.sd_DI = SEEDI;

	have_seeprom = ahc_acquire_seeprom(ahc, &sd);
	if (have_seeprom) {

		if (bootverbose)
			printf("%s: Reading SEEPROM...", ahc_name(ahc));

		for (;;) {
			u_int start_addr;

			start_addr = 32 * (ahc->channel - 'A');
			have_seeprom = read_seeprom(&sd, (uint16_t *)sc,
							start_addr,
							sizeof(*sc)/2);

			if (have_seeprom)
				have_seeprom = verify_seeprom_cksum(sc);

			if (have_seeprom != 0 || sd.sd_chip == C56_66) {
				if (bootverbose) {
					if (have_seeprom == 0)
						printf ("checksum error\n");
					else
						printf ("done.\n");
				}
				break;
			}
			sd.sd_chip = C56_66;
		}
		ahc_release_seeprom(&sd);
	}

	if (!have_seeprom) {
		/*
		 * Pull scratch ram settings and treat them as
		 * if they are the contents of an seeprom if
		 * the 'ADPT' signature is found in SCB2.
		 * We manually compose the data as 16bit values
		 * to avoid endian issues.
		 */
		ahc_outb(ahc, SCBPTR, 2);
		if (ahc_inb(ahc, SCB_BASE) == 'A'
		 && ahc_inb(ahc, SCB_BASE + 1) == 'D'
		 && ahc_inb(ahc, SCB_BASE + 2) == 'P'
		 && ahc_inb(ahc, SCB_BASE + 3) == 'T') {
			uint16_t *sc_data;
			int	  i;

			sc_data = (uint16_t *)sc;
			for (i = 0; i < 32; i++, sc_data++) {
				int	j;

				j = i * 2;
				*sc_data = ahc_inb(ahc, SRAM_BASE + j)
					 | ahc_inb(ahc, SRAM_BASE + j + 1) << 8;
			}
			have_seeprom = verify_seeprom_cksum(sc);
			if (have_seeprom)
				ahc->flags |= AHC_SCB_CONFIG_USED;
		}
		/*
		 * Clear any SCB parity errors in case this data and
		 * its associated parity was not initialized by the BIOS
		 */
		ahc_outb(ahc, CLRINT, CLRPARERR);
		ahc_outb(ahc, CLRINT, CLRBRKADRINT);
	}

	if (!have_seeprom) {
		if (bootverbose)
			printf("%s: No SEEPROM available.\n", ahc_name(ahc));
		ahc->flags |= AHC_USEDEFAULTS | AHC_NO_BIOS_INIT;
		free(ahc->seep_config, M_DEVBUF, sizeof(*ahc->seep_config));
		ahc->seep_config = NULL;
		sc = NULL;
	} else {
		ahc_parse_pci_eeprom(ahc, sc);
	}

	/*
	 * Cards that have the external logic necessary to talk to
	 * a SEEPROM, are almost certain to have the remaining logic
	 * necessary for auto-termination control.  This assumption
	 * hasn't failed yet...
	 */
	have_autoterm = have_seeprom;

	/*
	 * Some low-cost chips have SEEPROM and auto-term control built
	 * in, instead of using a GAL.  They can tell us directly
	 * if the termination logic is enabled.
	 */
	if ((ahc->features & AHC_SPIOCAP) != 0) {
		if ((ahc_inb(ahc, SPIOCAP) & SSPIOCPS) == 0)
			have_autoterm = FALSE;
	}

	if (have_autoterm) {
		ahc_acquire_seeprom(ahc, &sd);
		configure_termination(ahc, &sd, sc->adapter_control, sxfrctl1);
		ahc_release_seeprom(&sd);
	} else if (have_seeprom) {
		*sxfrctl1 &= ~STPWEN;
		if ((sc->adapter_control & CFSTERM) != 0)
			*sxfrctl1 |= STPWEN;
		if (bootverbose)
			printf("%s: Low byte termination %sabled\n",
			       ahc_name(ahc),
			       (*sxfrctl1 & STPWEN) ? "en" : "dis");
	}
}

static void
ahc_parse_pci_eeprom(struct ahc_softc *ahc, struct seeprom_config *sc)
{
	/*
	 * Put the data we've collected down into SRAM
	 * where ahc_init will find it.
	 */
	int	 i;
	int	 max_targ = sc->max_targets & CFMAXTARG;
	u_int	 scsi_conf;
	uint16_t discenable;
	uint16_t ultraenb;

	discenable = 0;
	ultraenb = 0;
	if ((sc->adapter_control & CFULTRAEN) != 0) {
		/*
		 * Determine if this adapter has a "newstyle"
		 * SEEPROM format.
		 */
		for (i = 0; i < max_targ; i++) {
			if ((sc->device_flags[i] & CFSYNCHISULTRA) != 0) {
				ahc->flags |= AHC_NEWEEPROM_FMT;
				break;
			}
		}
	}

	for (i = 0; i < max_targ; i++) {
		u_int     scsirate;
		uint16_t target_mask;

		target_mask = 0x01 << i;
		if (sc->device_flags[i] & CFDISC)
			discenable |= target_mask;
		if ((ahc->flags & AHC_NEWEEPROM_FMT) != 0) {
			if ((sc->device_flags[i] & CFSYNCHISULTRA) != 0)
				ultraenb |= target_mask;
		} else if ((sc->adapter_control & CFULTRAEN) != 0) {
			ultraenb |= target_mask;
		}
		if ((sc->device_flags[i] & CFXFER) == 0x04
		    && (ultraenb & target_mask) != 0) {
			/* Treat 10MHz as a non-ultra speed */
			sc->device_flags[i] &= ~CFXFER;
			ultraenb &= ~target_mask;
		}
		if ((ahc->features & AHC_ULTRA2) != 0) {
			u_int offset;

			if (sc->device_flags[i] & CFSYNCH)
				offset = MAX_OFFSET_ULTRA2;
			else
				offset = 0;
			ahc_outb(ahc, TARG_OFFSET + i, offset);

			/*
			 * The ultra enable bits contain the
			 * high bit of the ultra2 sync rate
			 * field.
			 */
			scsirate = (sc->device_flags[i] & CFXFER)
				 | ((ultraenb & target_mask) ? 0x8 : 0x0);
			if (sc->device_flags[i] & CFWIDEB)
				scsirate |= WIDEXFER;
		} else {
			scsirate = (sc->device_flags[i] & CFXFER) << 4;
			if (sc->device_flags[i] & CFSYNCH)
				scsirate |= SOFS;
			if (sc->device_flags[i] & CFWIDEB)
				scsirate |= WIDEXFER;
		}
		ahc_outb(ahc, TARG_SCSIRATE + i, scsirate);
	}
	ahc->our_id = sc->brtime_id & CFSCSIID;

	scsi_conf = (ahc->our_id & 0x7);
	if (sc->adapter_control & CFSPARITY)
		scsi_conf |= ENSPCHK;
	if (sc->adapter_control & CFRESETB)
		scsi_conf |= RESET_SCSI;

	ahc->flags |= (sc->adapter_control & CFBOOTCHAN) >> CFBOOTCHANSHIFT;

	if (sc->bios_control & CFEXTEND)
		ahc->flags |= AHC_EXTENDED_TRANS_A;

	if (sc->bios_control & CFBIOSEN)
		ahc->flags |= AHC_BIOS_ENABLED;
	if (ahc->features & AHC_ULTRA
	    && (ahc->flags & AHC_NEWEEPROM_FMT) == 0) {
		/* Should we enable Ultra mode? */
		if (!(sc->adapter_control & CFULTRAEN))
			/* Treat us as a non-ultra card */
			ultraenb = 0;
	}

	if (sc->signature == CFSIGNATURE
	    || sc->signature == CFSIGNATURE2) {
		uint32_t devconfig;

		/* Honor the STPWLEVEL settings */
		devconfig = pci_conf_read(ahc->bd->pc, ahc->bd->tag, DEVCONFIG);
		devconfig &= ~STPWLEVEL;
		if ((sc->bios_control & CFSTPWLEVEL) != 0)
			devconfig |= STPWLEVEL;
		pci_conf_write(ahc->bd->pc, ahc->bd->tag, DEVCONFIG,  devconfig);
	}
	/* Set SCSICONF info */
	ahc_outb(ahc, SCSICONF, scsi_conf);
	ahc_outb(ahc, DISC_DSB, ~(discenable & 0xff));
	ahc_outb(ahc, DISC_DSB + 1, ~((discenable >> 8) & 0xff));
	ahc_outb(ahc, ULTRA_ENB, ultraenb & 0xff);
	ahc_outb(ahc, ULTRA_ENB + 1, (ultraenb >> 8) & 0xff);
}

static void
configure_termination(struct ahc_softc *ahc,
		      struct seeprom_descriptor *sd,
		      u_int adapter_control,
		      u_int *sxfrctl1)
{
	uint8_t brddat;

	brddat = 0;

	/*
	 * Update the settings in sxfrctl1 to match the
	 * termination settings
	 */
	*sxfrctl1 = 0;

	/*
	 * SEECS must be on for the GALS to latch
	 * the data properly.  Be sure to leave MS
	 * on or we will release the seeprom.
	 */
	SEEPROM_OUTB(sd, sd->sd_MS | sd->sd_CS);
	if ((adapter_control & CFAUTOTERM) != 0
	 || (ahc->features & AHC_NEW_TERMCTL) != 0) {
		int internal50_present;
		int internal68_present;
		int externalcable_present;
		int eeprom_present;
		int enableSEC_low;
		int enableSEC_high;
		int enablePRI_low;
		int enablePRI_high;
		int sum;

		enableSEC_low = 0;
		enableSEC_high = 0;
		enablePRI_low = 0;
		enablePRI_high = 0;
		if ((ahc->features & AHC_NEW_TERMCTL) != 0) {
			ahc_new_term_detect(ahc, &enableSEC_low,
					    &enableSEC_high,
					    &enablePRI_low,
					    &enablePRI_high,
					    &eeprom_present);
			if ((adapter_control & CFSEAUTOTERM) == 0) {
				if (bootverbose)
					printf("%s: Manual SE Termination\n",
					       ahc_name(ahc));
				enableSEC_low = (adapter_control & CFSELOWTERM);
				enableSEC_high =
				    (adapter_control & CFSEHIGHTERM);
			}
			if ((adapter_control & CFAUTOTERM) == 0) {
				if (bootverbose)
					printf("%s: Manual LVD Termination\n",
					       ahc_name(ahc));
				enablePRI_low = (adapter_control & CFSTERM);
				enablePRI_high = (adapter_control & CFWSTERM);
			}
			/* Make the table calculations below happy */
			internal50_present = 0;
			internal68_present = 1;
			externalcable_present = 1;
		} else if ((ahc->features & AHC_SPIOCAP) != 0) {
			aic785X_cable_detect(ahc, &internal50_present,
					     &externalcable_present,
					     &eeprom_present);
			/* Can never support a wide connector. */
			internal68_present = 0;
		} else {
			aic787X_cable_detect(ahc, &internal50_present,
					     &internal68_present,
					     &externalcable_present,
					     &eeprom_present);
		}

		if ((ahc->features & AHC_WIDE) == 0)
			internal68_present = 0;

		if (bootverbose
		 && (ahc->features & AHC_ULTRA2) == 0) {
			printf("%s: internal 50 cable %s present",
			       ahc_name(ahc),
			       internal50_present ? "is":"not");

			if ((ahc->features & AHC_WIDE) != 0)
				printf(", internal 68 cable %s present",
				       internal68_present ? "is":"not");
			printf("\n%s: external cable %s present\n",
			       ahc_name(ahc),
			       externalcable_present ? "is":"not");
		}
		if (bootverbose)
			printf("%s: BIOS eeprom %s present\n",
			       ahc_name(ahc), eeprom_present ? "is" : "not");

		if ((ahc->flags & AHC_INT50_SPEEDFLEX) != 0) {
			/*
			 * The 50 pin connector is a separate bus,
			 * so force it to always be terminated.
			 * In the future, perform current sensing
			 * to determine if we are in the middle of
			 * a properly terminated bus.
			 */
			internal50_present = 0;
		}

		/*
		 * Now set the termination based on what
		 * we found.
		 * Flash Enable = BRDDAT7
		 * Secondary High Term Enable = BRDDAT6
		 * Secondary Low Term Enable = BRDDAT5 (7890)
		 * Primary High Term Enable = BRDDAT4 (7890)
		 */
		if ((ahc->features & AHC_ULTRA2) == 0
		 && (internal50_present != 0)
		 && (internal68_present != 0)
		 && (externalcable_present != 0)) {
			printf("%s: Illegal cable configuration!!. "
			       "Only two connectors on the "
			       "adapter may be used at a "
			       "time!\n", ahc_name(ahc));

			/*
			 * Pretend there are no cables in the hope
			 * that having all of the termination on
			 * gives us a more stable bus.
			 */
			internal50_present = 0;
			internal68_present = 0;
			externalcable_present = 0;
		}

		if ((ahc->features & AHC_WIDE) != 0
		 && ((externalcable_present == 0)
		  || (internal68_present == 0)
		  || (enableSEC_high != 0))) {
			brddat |= BRDDAT6;
			if (bootverbose) {
				if ((ahc->flags & AHC_INT50_SPEEDFLEX) != 0)
					printf("%s: 68 pin termination "
					       "Enabled\n", ahc_name(ahc));
				else
					printf("%s: %sHigh byte termination "
					       "Enabled\n", ahc_name(ahc),
					       enableSEC_high ? "Secondary "
							      : "");
			}
		}

		sum = internal50_present + internal68_present
		    + externalcable_present;
		if (sum < 2 || (enableSEC_low != 0)) {
			if ((ahc->features & AHC_ULTRA2) != 0)
				brddat |= BRDDAT5;
			else
				*sxfrctl1 |= STPWEN;
			if (bootverbose) {
				if ((ahc->flags & AHC_INT50_SPEEDFLEX) != 0)
					printf("%s: 50 pin termination "
					       "Enabled\n", ahc_name(ahc));
				else
					printf("%s: %sLow byte termination "
					       "Enabled\n", ahc_name(ahc),
					       enableSEC_low ? "Secondary "
							     : "");
			}
		}

		if (enablePRI_low != 0) {
			*sxfrctl1 |= STPWEN;
			if (bootverbose)
				printf("%s: Primary Low Byte termination "
				       "Enabled\n", ahc_name(ahc));
		}

		/*
		 * Setup STPWEN before setting up the rest of
		 * the termination per the tech note on the U160 cards.
		 */
		ahc_outb(ahc, SXFRCTL1, *sxfrctl1);

		if (enablePRI_high != 0) {
			brddat |= BRDDAT4;
			if (bootverbose)
				printf("%s: Primary High Byte "
				       "termination Enabled\n",
				       ahc_name(ahc));
		}

		write_brdctl(ahc, brddat);

	} else {
		if ((adapter_control & CFSTERM) != 0) {
			*sxfrctl1 |= STPWEN;

			if (bootverbose)
				printf("%s: %sLow byte termination Enabled\n",
				       ahc_name(ahc),
				       (ahc->features & AHC_ULTRA2) ? "Primary "
								    : "");
		}

		if ((adapter_control & CFWSTERM) != 0
		 && (ahc->features & AHC_WIDE) != 0) {
			brddat |= BRDDAT6;
			if (bootverbose)
				printf("%s: %sHigh byte termination Enabled\n",
				       ahc_name(ahc),
				       (ahc->features & AHC_ULTRA2)
				     ? "Secondary " : "");
		}

		/*
		 * Setup STPWEN before setting up the rest of
		 * the termination per the tech note on the U160 cards.
		 */
		ahc_outb(ahc, SXFRCTL1, *sxfrctl1);

		if ((ahc->features & AHC_WIDE) != 0)
			write_brdctl(ahc, brddat);
	}
	SEEPROM_OUTB(sd, sd->sd_MS); /* Clear CS */
}

static void
ahc_new_term_detect(struct ahc_softc *ahc, int *enableSEC_low,
		    int *enableSEC_high, int *enablePRI_low,
		    int *enablePRI_high, int *eeprom_present)
{
	uint8_t brdctl;

	/*
	 * BRDDAT7 = Eeprom
	 * BRDDAT6 = Enable Secondary High Byte termination
	 * BRDDAT5 = Enable Secondary Low Byte termination
	 * BRDDAT4 = Enable Primary high byte termination
	 * BRDDAT3 = Enable Primary low byte termination
	 */
	brdctl = read_brdctl(ahc);
	*eeprom_present = brdctl & BRDDAT7;
	*enableSEC_high = (brdctl & BRDDAT6);
	*enableSEC_low = (brdctl & BRDDAT5);
	*enablePRI_high = (brdctl & BRDDAT4);
	*enablePRI_low = (brdctl & BRDDAT3);
}

static void
aic787X_cable_detect(struct ahc_softc *ahc, int *internal50_present,
		     int *internal68_present, int *externalcable_present,
		     int *eeprom_present)
{
	uint8_t brdctl;

	/*
	 * First read the status of our cables.
	 * Set the rom bank to 0 since the
	 * bank setting serves as a multiplexor
	 * for the cable detection logic.
	 * BRDDAT5 controls the bank switch.
	 */
	write_brdctl(ahc, 0);

	/*
	 * Now read the state of the internal
	 * connectors.  BRDDAT6 is INT50 and
	 * BRDDAT7 is INT68.
	 */
	brdctl = read_brdctl(ahc);
	*internal50_present = (brdctl & BRDDAT6) ? 0 : 1;
	*internal68_present = (brdctl & BRDDAT7) ? 0 : 1;

	/*
	 * Set the rom bank to 1 and determine
	 * the other signals.
	 */
	write_brdctl(ahc, BRDDAT5);

	/*
	 * Now read the state of the external
	 * connectors.  BRDDAT6 is EXT68 and
	 * BRDDAT7 is EPROMPS.
	 */
	brdctl = read_brdctl(ahc);
	*externalcable_present = (brdctl & BRDDAT6) ? 0 : 1;
	*eeprom_present = (brdctl & BRDDAT7) ? 1 : 0;
}

static void
aic785X_cable_detect(struct ahc_softc *ahc, int *internal50_present,
		     int *externalcable_present, int *eeprom_present)
{
	uint8_t brdctl;
	uint8_t spiocap;

	spiocap = ahc_inb(ahc, SPIOCAP);
	spiocap &= ~SOFTCMDEN;
	spiocap |= EXT_BRDCTL;
	ahc_outb(ahc, SPIOCAP, spiocap);
	ahc_outb(ahc, BRDCTL, BRDRW|BRDCS);
	ahc_outb(ahc, BRDCTL, 0);
	brdctl = ahc_inb(ahc, BRDCTL);
	*internal50_present = (brdctl & BRDDAT5) ? 0 : 1;
	*externalcable_present = (brdctl & BRDDAT6) ? 0 : 1;

	*eeprom_present = (ahc_inb(ahc, SPIOCAP) & EEPROM) ? 1 : 0;
}

int
ahc_acquire_seeprom(struct ahc_softc *ahc, struct seeprom_descriptor *sd)
{
	int wait;

	if ((ahc->features & AHC_SPIOCAP) != 0
	    && (ahc_inb(ahc, SPIOCAP) & SEEPROM) == 0)
		return (0);

	/*
	 * Request access of the memory port.  When access is
	 * granted, SEERDY will go high.  We use a 1 second
	 * timeout which should be near 1 second more than
	 * is needed.  Reason: after the chip reset, there
	 * should be no contention.
	 */
	SEEPROM_OUTB(sd, sd->sd_MS);
	wait = 1000;  /* 1 second timeout in msec */
	while (--wait && ((SEEPROM_STATUS_INB(sd) & sd->sd_RDY) == 0)) {
		aic_delay(1000);  /* delay 1 msec */
	}
	if ((SEEPROM_STATUS_INB(sd) & sd->sd_RDY) == 0) {
		SEEPROM_OUTB(sd, 0);
		return (0);
	}
	return(1);
}

void
ahc_release_seeprom(struct seeprom_descriptor *sd)
{
	/* Release access to the memory port and the serial EEPROM. */
	SEEPROM_OUTB(sd, 0);
}

static void
write_brdctl(struct ahc_softc *ahc, uint8_t value)
{
	uint8_t brdctl;

	if ((ahc->chip & AHC_CHIPID_MASK) == AHC_AIC7895) {
		brdctl = BRDSTB;
		if (ahc->channel == 'B')
			brdctl |= BRDCS;
	} else if ((ahc->features & AHC_ULTRA2) != 0) {
		brdctl = 0;
	} else {
		brdctl = BRDSTB|BRDCS;
	}
	ahc_outb(ahc, BRDCTL, brdctl);
	ahc_flush_device_writes(ahc);
	brdctl |= value;
	ahc_outb(ahc, BRDCTL, brdctl);
	ahc_flush_device_writes(ahc);
	if ((ahc->features & AHC_ULTRA2) != 0)
		brdctl |= BRDSTB_ULTRA2;
	else
		brdctl &= ~BRDSTB;
	ahc_outb(ahc, BRDCTL, brdctl);
	ahc_flush_device_writes(ahc);
	if ((ahc->features & AHC_ULTRA2) != 0)
		brdctl = 0;
	else
		brdctl &= ~BRDCS;
	ahc_outb(ahc, BRDCTL, brdctl);
}

static uint8_t
read_brdctl(struct ahc_softc *ahc)
{
	uint8_t brdctl;
	uint8_t value;

	if ((ahc->chip & AHC_CHIPID_MASK) == AHC_AIC7895) {
		brdctl = BRDRW;
		if (ahc->channel == 'B')
			brdctl |= BRDCS;
	} else if ((ahc->features & AHC_ULTRA2) != 0) {
		brdctl = BRDRW_ULTRA2;
	} else {
		brdctl = BRDRW|BRDCS;
	}
	ahc_outb(ahc, BRDCTL, brdctl);
	ahc_flush_device_writes(ahc);
	value = ahc_inb(ahc, BRDCTL);
	ahc_outb(ahc, BRDCTL, 0);
	return (value);
}

static int
verify_seeprom_cksum(struct seeprom_config *sc)
{
	int i;
	int maxaddr;
	uint32_t checksum;
	uint16_t *scarray;

	maxaddr = (sizeof(*sc)/2) - 1;
	checksum = 0;
	scarray = (uint16_t *)sc;

	for (i = 0; i < maxaddr; i++)
		checksum = checksum + scarray[i];
	if (checksum == 0
	 || (checksum & 0xFFFF) != sc->checksum) {
		return (0);
	} else {
		return(1);
	}
}
