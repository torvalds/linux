/*	$OpenBSD: adwlib.c,v 1.30 2024/04/13 23:44:11 jsg Exp $ */
/* $NetBSD: adwlib.c,v 1.20 2000/07/04 04:17:03 itojun Exp $        */

/*
 * Low level routines for the Advanced Systems Inc. SCSI controllers chips
 *
 * Copyright (c) 1998, 1999, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Author: Baldassare Dante Profeta <dante@mclink.it>
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
/*
 * Ported from:
 */
/*
 * advansys.c - Linux Host Driver for AdvanSys SCSI Adapters
 *
 * Copyright (c) 1995-2000 Advanced System Products, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/pci/pcidevs.h>

#include <dev/ic/adwlib.h>
#include <dev/microcode/adw/adwmcode.h>
#include <dev/ic/adw.h>


int AdwRamSelfTest(bus_space_tag_t, bus_space_handle_t, u_int8_t);
int AdwLoadMCode(bus_space_tag_t, bus_space_handle_t, u_int16_t *,
    u_int8_t);
int AdwASC3550Cabling(bus_space_tag_t, bus_space_handle_t, ADW_DVC_CFG *);
int AdwASC38C0800Cabling(bus_space_tag_t, bus_space_handle_t,
    ADW_DVC_CFG *);
int AdwASC38C1600Cabling(bus_space_tag_t, bus_space_handle_t,
    ADW_DVC_CFG *);

u_int16_t AdwGetEEPROMConfig(bus_space_tag_t, bus_space_handle_t,
    ADW_EEPROM *);
void AdwSetEEPROMConfig(bus_space_tag_t, bus_space_handle_t,
    ADW_EEPROM *);
u_int16_t AdwReadEEPWord(bus_space_tag_t, bus_space_handle_t, int);
void AdwWaitEEPCmd(bus_space_tag_t, bus_space_handle_t);

void AdwInquiryHandling(ADW_SOFTC *, ADW_SCSI_REQ_Q *);

void AdwSleepMilliSecond(u_int32_t);
void AdwDelayMicroSecond(u_int32_t);


/*
 * EEPROM Configuration.
 *
 * All drivers should use this structure to set the default EEPROM
 * configuration. The BIOS now uses this structure when it is built.
 * Additional structure information can be found in adwlib.h where
 * the structure is defined.
 */
static const ADW_EEPROM adw_3550_Default_EEPROM = {
	ADW_EEPROM_BIOS_ENABLE,	/* 00 cfg_lsw */
	0x0000,			/* 01 cfg_msw */
	0xFFFF,			/* 02 disc_enable */
	0xFFFF,			/* 03 wdtr_able */
	{ 0xFFFF },		/* 04 sdtr_able */
	0xFFFF,			/* 05 start_motor */
	0xFFFF,			/* 06 tagqng_able */
	0xFFFF,			/* 07 bios_scan */
	0,			/* 08 scam_tolerant */
	7,			/* 09 adapter_scsi_id */
	0,			/*    bios_boot_delay */
	3,			/* 10 scsi_reset_delay */
	0,			/*    bios_id_lun */
	0,			/* 11 termination */
	0,			/*    reserved1 */
	0xFFE7,			/* 12 bios_ctrl */
	{ 0xFFFF },		/* 13 ultra_able */
	{ 0 },			/* 14 reserved2 */
	ADW_DEF_MAX_HOST_QNG,	/* 15 max_host_qng */
	ADW_DEF_MAX_DVC_QNG,	/*    max_dvc_qng */
	0,			/* 16 dvc_cntl */
	{ 0 },			/* 17 bug_fix */
	{ 0,0,0 },		/* 18-20 serial_number[3] */
	0,			/* 21 check_sum */
	{			/* 22-29 oem_name[16] */
	  0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0
	},
	0,			/* 30 dvc_err_code */
	0,			/* 31 adw_err_code */
	0,			/* 32 adw_err_addr */
	0,			/* 33 saved_dvc_err_code */
	0,			/* 34 saved_adw_err_code */
	0			/* 35 saved_adw_err_addr */
};

static const ADW_EEPROM adw_38C0800_Default_EEPROM = {
	ADW_EEPROM_BIOS_ENABLE,	/* 00 cfg_lsw */
	0x0000,			/* 01 cfg_msw */
	0xFFFF,			/* 02 disc_enable */
	0xFFFF,			/* 03 wdtr_able */
	{ 0x4444 },		/* 04 sdtr_speed1 */
	0xFFFF,			/* 05 start_motor */
	0xFFFF,			/* 06 tagqng_able */
	0xFFFF,			/* 07 bios_scan */
	0,			/* 08 scam_tolerant */
	7,			/* 09 adapter_scsi_id */
	0,			/*    bios_boot_delay */
	3,			/* 10 scsi_reset_delay */
	0,			/*    bios_id_lun */
	0,			/* 11 termination_se */
	0,			/*    termination_lvd */
	0xFFE7,			/* 12 bios_ctrl */
	{ 0x4444 },		/* 13 sdtr_speed2 */
	{ 0x4444 },		/* 14 sdtr_speed3 */
	ADW_DEF_MAX_HOST_QNG,	/* 15 max_host_qng */
	ADW_DEF_MAX_DVC_QNG,	/*    max_dvc_qng */
	0,			/* 16 dvc_cntl */
	{ 0x4444 },		/* 17 sdtr_speed4 */
	{ 0,0,0 },		/* 18-20 serial_number[3] */
	0,			/* 21 check_sum */
	{			/* 22-29 oem_name[16] */
	  0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0
	},
	0,			/* 30 dvc_err_code */
	0,			/* 31 adw_err_code */
	0,			/* 32 adw_err_addr */
	0,			/* 33 saved_dvc_err_code */
	0,			/* 34 saved_adw_err_code */
	0,			/* 35 saved_adw_err_addr */
	{			/* 36-55 reserved1[16] */
	  0,0,0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0,0,0
	},
	0,			/* 56 cisptr_lsw */
	0,			/* 57 cisprt_msw */
	PCI_VENDOR_ADVSYS,	/* 58 subsysvid */
	PCI_PRODUCT_ADVSYS_U2W,	/* 59 subsysid */
	{ 0,0,0,0 }		/* 60-63 reserved2[4] */
};

static const ADW_EEPROM adw_38C1600_Default_EEPROM = {
	ADW_EEPROM_BIOS_ENABLE,	/* 00 cfg_lsw */
	0x0000,			/* 01 cfg_msw */
	0xFFFF,			/* 02 disc_enable */
	0xFFFF,			/* 03 wdtr_able */
	{ 0x5555 },		/* 04 sdtr_speed1 */
	0xFFFF,			/* 05 start_motor */
	0xFFFF,			/* 06 tagqng_able */
	0xFFFF,			/* 07 bios_scan */
	0,			/* 08 scam_tolerant */
	7,			/* 09 adapter_scsi_id */
	0,			/*    bios_boot_delay */
	3,			/* 10 scsi_reset_delay */
	0,			/*    bios_id_lun */
	0,			/* 11 termination_se */
	0,			/*    termination_lvd */
	0xFFE7,			/* 12 bios_ctrl */
	{ 0x5555 },		/* 13 sdtr_speed2 */
	{ 0x5555 },		/* 14 sdtr_speed3 */
	ADW_DEF_MAX_HOST_QNG,	/* 15 max_host_qng */
	ADW_DEF_MAX_DVC_QNG,	/*    max_dvc_qng */
	0,			/* 16 dvc_cntl */
	{ 0x5555 },		/* 17 sdtr_speed4 */
	{ 0,0,0 },		/* 18-20 serial_number[3] */
	0,			/* 21 check_sum */
	{			/* 22-29 oem_name[16] */
	  0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0
	},
	0,			/* 30 dvc_err_code */
	0,			/* 31 adw_err_code */
	0,			/* 32 adw_err_addr */
	0,			/* 33 saved_dvc_err_code */
	0,			/* 34 saved_adw_err_code */
	0,			/* 35 saved_adw_err_addr */
	{			/* 36-55 reserved1[16] */
	  0,0,0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0,0,0
	},
	0,			/* 56 cisptr_lsw */
	0,			/* 57 cisprt_msw */
	PCI_VENDOR_ADVSYS,	/* 58 subsysvid */
	PCI_PRODUCT_ADVSYS_U3W, /* 59 subsysid */
	{ 0,0,0,0 }		/* 60-63 reserved2[4] */
};


/*
 * Read the board's EEPROM configuration. Set fields in ADW_SOFTC and
 * ADW_DVC_CFG based on the EEPROM settings. The chip is stopped while
 * all of this is done.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Note: Chip is stopped on entry.
 */
int
AdwInitFromEEPROM(ADW_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	ADW_EEPROM		eep_config;
	u_int16_t		warn_code;
	u_int16_t		sdtr_speed = 0;
	u_int8_t		tid, termination;
	int			i, j;


	warn_code = 0;

	/*
	 * Read the board's EEPROM configuration.
	 *
	 * Set default values if a bad checksum is found.
	 *
	 * XXX - Don't handle big-endian access to EEPROM yet.
	 */
	if (AdwGetEEPROMConfig(iot, ioh, &eep_config) != eep_config.check_sum) {
		warn_code |= ADW_WARN_EEPROM_CHKSUM;

		/*
		 * Set EEPROM default values.
		 */
		switch(sc->chip_type) {
		case ADW_CHIP_ASC3550:
			eep_config = adw_3550_Default_EEPROM;
			break;
		case ADW_CHIP_ASC38C0800:
			eep_config = adw_38C0800_Default_EEPROM;
			break;
		case ADW_CHIP_ASC38C1600:
			eep_config = adw_38C1600_Default_EEPROM;

// XXX	  TODO!!!	if (ASC_PCI_ID2FUNC(sc->cfg.pci_slot_info) != 0) {
			if (sc->cfg.pci_slot_info != 0) {
				u_int8_t lsw_msb;

				lsw_msb = eep_config.cfg_lsw >> 8;
				/*
				 * Set Function 1 EEPROM Word 0 MSB
				 *
				 * Clear the BIOS_ENABLE (bit 14) and
				 * INTAB (bit 11) EEPROM bits.
				 *
				 * Disable Bit 14 (BIOS_ENABLE) to fix
				 * SPARC Ultra 60 and old Mac system booting
				 * problem. The Expansion ROM must
				 * be disabled in Function 1 for these systems.
				 */
				lsw_msb &= ~(((ADW_EEPROM_BIOS_ENABLE |
						ADW_EEPROM_INTAB) >> 8) & 0xFF);
				/*
				 * Set the INTAB (bit 11) if the GPIO 0 input
				 * indicates the Function 1 interrupt line is
				 * wired to INTA.
				 *
				 * Set/Clear Bit 11 (INTAB) from
				 * the GPIO bit 0 input:
				 *   1 - Function 1 intr line wired to INT A.
				 *   0 - Function 1 intr line wired to INT B.
				 *
				 * Note: Adapter boards always have Function 0
				 * wired to INTA.
				 * Put all 5 GPIO bits in input mode and then
				 * read their input values.
				 */
				ADW_WRITE_BYTE_REGISTER(iot, ioh,
							IOPB_GPIO_CNTL, 0);
				if (ADW_READ_BYTE_REGISTER(iot, ioh,
						IOPB_GPIO_DATA) & 0x01) {
					/*
					 * Function 1 interrupt wired to INTA;
					 * Set EEPROM bit.
					 */
					lsw_msb |= (ADW_EEPROM_INTAB >> 8)
							 & 0xFF;
				}
				eep_config.cfg_lsw &= 0x00FF;
				eep_config.cfg_lsw |= lsw_msb << 8;
			}
			break;
		}

		/*
		 * Assume the 6 byte board serial number that was read
		 * from EEPROM is correct even if the EEPROM checksum
		 * failed.
		 */
		for (i = 2, j = 1; i >= 0; i--, j++) {
			eep_config.serial_number[i] =
			    AdwReadEEPWord(iot, ioh, ADW_EEP_DVC_CFG_END - j);
		}

		AdwSetEEPROMConfig(iot, ioh, &eep_config);
	}
	/*
	 * Set sc and sc->cfg variables from the EEPROM configuration
	 * that was read.
	 *
	 * This is the mapping of EEPROM fields to Adw Library fields.
	 */
	sc->wdtr_able = eep_config.wdtr_able;
	if (sc->chip_type == ADW_CHIP_ASC3550) {
		sc->sdtr_able = eep_config.sdtr1.sdtr_able;
		sc->ultra_able = eep_config.sdtr2.ultra_able;
	} else {
		sc->sdtr_speed1 = eep_config.sdtr1.sdtr_speed1;
		sc->sdtr_speed2 = eep_config.sdtr2.sdtr_speed2;
		sc->sdtr_speed3 = eep_config.sdtr3.sdtr_speed3;
		sc->sdtr_speed4 = eep_config.sdtr4.sdtr_speed4;
	}
	sc->ppr_able = 0;
	sc->tagqng_able = eep_config.tagqng_able;
	sc->cfg.disc_enable = eep_config.disc_enable;
	sc->max_host_qng = eep_config.max_host_qng;
	sc->max_dvc_qng = eep_config.max_dvc_qng;
	sc->chip_scsi_id = (eep_config.adapter_scsi_id & ADW_MAX_TID);
	sc->start_motor = eep_config.start_motor;
	sc->scsi_reset_wait = eep_config.scsi_reset_delay;
	sc->bios_ctrl = eep_config.bios_ctrl;
	sc->no_scam = eep_config.scam_tolerant;
	sc->cfg.serial1 = eep_config.serial_number[0];
	sc->cfg.serial2 = eep_config.serial_number[1];
	sc->cfg.serial3 = eep_config.serial_number[2];

	if (sc->chip_type == ADW_CHIP_ASC38C0800 ||
	    sc->chip_type == ADW_CHIP_ASC38C1600) {
		sc->sdtr_able = 0;
		for (tid = 0; tid <= ADW_MAX_TID; tid++) {
			if (tid == 0) {
				sdtr_speed = sc->sdtr_speed1;
			} else if (tid == 4) {
				sdtr_speed = sc->sdtr_speed2;
			} else if (tid == 8) {
				sdtr_speed = sc->sdtr_speed3;
			} else if (tid == 12) {
				sdtr_speed = sc->sdtr_speed4;
			}
			if (sdtr_speed & ADW_MAX_TID) {
				sc->sdtr_able |= (1 << tid);
			}
			sdtr_speed >>= 4;
		}
	}

	/*
	 * Set the host maximum queuing (max. 253, min. 16) and the per device
	 * maximum queuing (max. 63, min. 4).
	 */
	if (eep_config.max_host_qng > ADW_DEF_MAX_HOST_QNG) {
		eep_config.max_host_qng = ADW_DEF_MAX_HOST_QNG;
	} else if (eep_config.max_host_qng < ADW_DEF_MIN_HOST_QNG)
	{
		/* If the value is zero, assume it is uninitialized. */
		if (eep_config.max_host_qng == 0) {
			eep_config.max_host_qng = ADW_DEF_MAX_HOST_QNG;
		} else {
			eep_config.max_host_qng = ADW_DEF_MIN_HOST_QNG;
		}
	}

	if (eep_config.max_dvc_qng > ADW_DEF_MAX_DVC_QNG) {
		eep_config.max_dvc_qng = ADW_DEF_MAX_DVC_QNG;
	} else if (eep_config.max_dvc_qng < ADW_DEF_MIN_DVC_QNG) {
		/* If the value is zero, assume it is uninitialized. */
		if (eep_config.max_dvc_qng == 0) {
			eep_config.max_dvc_qng = ADW_DEF_MAX_DVC_QNG;
		} else {
			eep_config.max_dvc_qng = ADW_DEF_MIN_DVC_QNG;
		}
	}

	/*
	 * If 'max_dvc_qng' is greater than 'max_host_qng', then
	 * set 'max_dvc_qng' to 'max_host_qng'.
	 */
	if (eep_config.max_dvc_qng > eep_config.max_host_qng) {
		eep_config.max_dvc_qng = eep_config.max_host_qng;
	}

	/*
	 * Set ADW_SOFTC 'max_host_qng' and 'max_dvc_qng'
	 * values based on possibly adjusted EEPROM values.
	 */
	sc->max_host_qng = eep_config.max_host_qng;
	sc->max_dvc_qng = eep_config.max_dvc_qng;


	/*
	 * If the EEPROM 'termination' field is set to automatic (0), then set
	 * the ADW_SOFTC.cfg 'termination' field to automatic also.
	 *
	 * If the termination is specified with a non-zero 'termination'
	 * value check that a legal value is set and set the ADW_SOFTC.cfg
	 * 'termination' field appropriately.
	 */

	switch(sc->chip_type) {
	case ADW_CHIP_ASC3550:
		sc->cfg.termination = 0;	/* auto termination */
		switch(eep_config.termination_se) {
		case 3:
			/* Enable manual control with low on / high on. */
			sc->cfg.termination |= ADW_TERM_CTL_L;
		case 2:
			/* Enable manual control with low off / high on. */
			sc->cfg.termination |= ADW_TERM_CTL_H;
		case 1:
			/* Enable manual control with low off / high off. */
			sc->cfg.termination |= ADW_TERM_CTL_SEL;
		case 0:
			break;
		default:
			warn_code |= ADW_WARN_EEPROM_TERMINATION;
		}
		break;

	case ADW_CHIP_ASC38C0800:
	case ADW_CHIP_ASC38C1600:
		switch(eep_config.termination_se) {
		case 0:
			/* auto termination for SE */
			termination = 0;
			break;
		case 1:
			/* Enable manual control with low off / high off. */
			termination = 0;
			break;
		case 2:
			/* Enable manual control with low off / high on. */
			termination = ADW_TERM_SE_HI;
			break;
		case 3:
			/* Enable manual control with low on / high on. */
			termination = ADW_TERM_SE;
			break;
		default:
			/*
			 * The EEPROM 'termination_se' field contains a
			 * bad value. Use automatic termination instead.
			 */
			termination = 0;
			warn_code |= ADW_WARN_EEPROM_TERMINATION;
		}

		switch(eep_config.termination_lvd) {
		case 0:
			/* auto termination for LVD */
			sc->cfg.termination = termination;
			break;
		case 1:
			/* Enable manual control with low off / high off. */
			sc->cfg.termination = termination;
			break;
		case 2:
			/* Enable manual control with low off / high on. */
			sc->cfg.termination = termination | ADW_TERM_LVD_HI;
			break;
		case 3:
			/* Enable manual control with low on / high on. */
			sc->cfg.termination = termination | ADW_TERM_LVD;
			break;
		default:
			/*
			 * The EEPROM 'termination_lvd' field contains a
			 * bad value. Use automatic termination instead.
			 */
			sc->cfg.termination = termination;
			warn_code |= ADW_WARN_EEPROM_TERMINATION;
		}
		break;
	}

	return warn_code;
}


/*
 * Initialize the ASC-3550/ASC-38C0800/ASC-38C1600.
 *
 * On failure return the error code.
 */
int
AdwInitDriver(ADW_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t	error_code;
	int		word;
	int		i;
	u_int16_t	bios_mem[ADW_MC_BIOSLEN/2];	/* BIOS RISC Memory
								0x40-0x8F. */
	u_int16_t	wdtr_able = 0, sdtr_able, ppr_able, tagqng_able;
	u_int8_t	max_cmd[ADW_MAX_TID + 1];
	u_int8_t	tid;


	error_code = 0;

	/*
	 * Save the RISC memory BIOS region before writing the microcode.
	 * The BIOS may already be loaded and using its RISC LRAM region
	 * so its region must be saved and restored.
	 *
	 * Note: This code makes the assumption, which is currently true,
	 * that a chip reset does not clear RISC LRAM.
	 */
	for (i = 0; i < ADW_MC_BIOSLEN/2; i++) {
		ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_BIOSMEM+(2*i), bios_mem[i]);
	}

	/*
	 * Save current per TID negotiated values.
	 */
	switch (sc->chip_type) {
	case ADW_CHIP_ASC3550:
		if (bios_mem[(ADW_MC_BIOS_SIGNATURE-ADW_MC_BIOSMEM)/2]==0x55AA){

			u_int16_t  bios_version, major, minor;

			bios_version = bios_mem[(ADW_MC_BIOS_VERSION -
					ADW_MC_BIOSMEM) / 2];
			major = (bios_version  >> 12) & 0xF;
			minor = (bios_version  >> 8) & 0xF;
			if (major < 3 || (major == 3 && minor == 1)) {
			    /*
			     * BIOS 3.1 and earlier location of
			     * 'wdtr_able' variable.
			     */
			    ADW_READ_WORD_LRAM(iot, ioh, 0x120, wdtr_able);
			} else {
			    ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE,
					    wdtr_able);
			}
		}
		break;

	case ADW_CHIP_ASC38C1600:
		ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_PPR_ABLE, ppr_able);
		/* FALLTHROUGH */
	case ADW_CHIP_ASC38C0800:
		ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE, wdtr_able);
		break;
	}
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_SDTR_ABLE, sdtr_able);
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_TAGQNG_ABLE, tagqng_able);
	for (tid = 0; tid <= ADW_MAX_TID; tid++) {
		ADW_READ_BYTE_LRAM(iot, ioh, ADW_MC_NUMBER_OF_MAX_CMD + tid,
			max_cmd[tid]);
	}

	/*
	 * Perform a RAM Built-In Self Test
	 */
	if((error_code = AdwRamSelfTest(iot, ioh, sc->chip_type))) {
		return error_code;
	}

	/*
	 * Load the Microcode
	 */
	;
	if((error_code = AdwLoadMCode(iot, ioh, bios_mem, sc->chip_type))) {
		return error_code;
	}

	/*
	 * Read microcode version and date.
	 */
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_VERSION_DATE, sc->cfg.mcode_date);
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_VERSION_NUM, sc->cfg.mcode_version);

	/*
	 * If the PCI Configuration Command Register "Parity Error Response
	 * Control" Bit was clear (0), then set the microcode variable
	 * 'control_flag' CONTROL_FLAG_IGNORE_PERR flag to tell the microcode
	 * to ignore DMA parity errors.
	 */
	if (sc->cfg.control_flag & CONTROL_FLAG_IGNORE_PERR) {
		ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_CONTROL_FLAG, word);
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_CONTROL_FLAG,
					word | CONTROL_FLAG_IGNORE_PERR);
	}

	switch (sc->chip_type) {
	case ADW_CHIP_ASC3550:
		/*
		 * For ASC-3550, setting the START_CTL_EMFU [3:2] bits sets a
		 * FIFO threshold of 128 bytes.
		 * This register is only accessible to the host.
		 */
		ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_DMA_CFG0,
				START_CTL_EMFU | READ_CMD_MRM);
		break;

	case ADW_CHIP_ASC38C0800:
		/*
		 * Write 1 to bit 14 'DIS_TERM_DRV' in the SCSI_CFG1 register.
		 * When DIS_TERM_DRV set to 1, C_DET[3:0] will reflect current
		 * cable detection and then we are able to read C_DET[3:0].
		 *
		 * Note: We will reset DIS_TERM_DRV to 0 in the 'Set SCSI_CFG1
		 * Microcode Default Value' section below.
		 */
		ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_SCSI_CFG1,
				ADW_READ_WORD_REGISTER(iot, ioh, IOPW_SCSI_CFG1)
				| ADW_DIS_TERM_DRV);

		/*
		 * For ASC-38C0800, set FIFO_THRESH_80B [6:4] bits and
		 * START_CTL_TH [3:2] bits for the default FIFO threshold.
		 *
		 * Note: ASC-38C0800 FIFO threshold has been changed to
		 * 256 bytes.
		 *
		 * For DMA Errata #4 set the BC_THRESH_ENB bit.
		 */
		ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_DMA_CFG0,
						BC_THRESH_ENB | FIFO_THRESH_80B
						| START_CTL_TH | READ_CMD_MRM);
		break;

	case ADW_CHIP_ASC38C1600:
		/*
		 * Write 1 to bit 14 'DIS_TERM_DRV' in the SCSI_CFG1 register.
		 * When DIS_TERM_DRV set to 1, C_DET[3:0] will reflect current
		 * cable detection and then we are able to read C_DET[3:0].
		 *
		 * Note: We will reset DIS_TERM_DRV to 0 in the 'Set SCSI_CFG1
		 * Microcode Default Value' section below.
		 */
		ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_SCSI_CFG1,
				ADW_READ_WORD_REGISTER(iot, ioh, IOPW_SCSI_CFG1)
				| ADW_DIS_TERM_DRV);

		/*
		 * If the BIOS control flag AIPP (Asynchronous Information
		 * Phase Protection) disable bit is not set, then set the
		 * firmware 'control_flag' CONTROL_FLAG_ENABLE_AIPP bit to
		 * enable AIPP checking and encoding.
		 */
		if ((sc->bios_ctrl & BIOS_CTRL_AIPP_DIS) == 0) {
			ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_CONTROL_FLAG, word);
			ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_CONTROL_FLAG,
					word | CONTROL_FLAG_ENABLE_AIPP);
		}

		/*
		 * For ASC-38C1600 use DMA_CFG0 default values:
		 * FIFO_THRESH_80B [6:4], and START_CTL_TH [3:2].
		 */
		ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_DMA_CFG0,
				FIFO_THRESH_80B | START_CTL_TH | READ_CMD_MRM);
		break;
	}

	/*
	 * Microcode operating variables for WDTR, SDTR, and command tag
	 * queuing will be set in AdwInquiryHandling() based on what a
	 * device reports it is capable of in Inquiry byte 7.
	 *
	 * If SCSI Bus Resets have been disabled, then directly set
	 * SDTR and WDTR from the EEPROM configuration. This will allow
	 * the BIOS and warm boot to work without a SCSI bus hang on
	 * the Inquiry caused by host and target mismatched DTR values.
	 * Without the SCSI Bus Reset, before an Inquiry a device can't
	 * be assumed to be in Asynchronous, Narrow mode.
	 */
	if ((sc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) == 0) {
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE, sc->wdtr_able);
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_ABLE, sc->sdtr_able);
	}

	/*
	 * Set microcode operating variables for SDTR_SPEED1, SDTR_SPEED2,
	 * SDTR_SPEED3, and SDTR_SPEED4 based on the ULTRA EEPROM per TID
	 * bitmask. These values determine the maximum SDTR speed negotiated
	 * with a device.
	 *
	 * The SDTR per TID bitmask overrides the SDTR_SPEED1, SDTR_SPEED2,
	 * SDTR_SPEED3, and SDTR_SPEED4 values so it is safe to set them
	 * without determining here whether the device supports SDTR.
	 */
	switch (sc->chip_type) {
	case ADW_CHIP_ASC3550:
		word = 0;
		for (tid = 0; tid <= ADW_MAX_TID; tid++) {
			if (ADW_TID_TO_TIDMASK(tid) & sc->ultra_able) {
				/* Set Ultra speed for TID 'tid'. */
				word |= (0x3 << (4 * (tid % 4)));
			} else {
				/* Set Fast speed for TID 'tid'. */
				word |= (0x2 << (4 * (tid % 4)));
			}
			/* Check if done with sdtr_speed1. */
			if (tid == 3) {
				ADW_WRITE_WORD_LRAM(iot, ioh,
						ADW_MC_SDTR_SPEED1, word);
				word = 0;
			/* Check if done with sdtr_speed2. */
			} else if (tid == 7) {
				ADW_WRITE_WORD_LRAM(iot, ioh,
						ADW_MC_SDTR_SPEED2, word);
				word = 0;
			/* Check if done with sdtr_speed3. */
			} else if (tid == 11) {
				ADW_WRITE_WORD_LRAM(iot, ioh,
						ADW_MC_SDTR_SPEED3, word);
				word = 0;
			/* Check if done with sdtr_speed4. */
			} else if (tid == 15) {
				ADW_WRITE_WORD_LRAM(iot, ioh,
						ADW_MC_SDTR_SPEED4, word);
				/* End of loop. */
			}
		}

		/*
		 * Set microcode operating variable for the
		 * disconnect per TID bitmask.
		 */
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DISC_ENABLE,
							sc->cfg.disc_enable);
		break;

	case ADW_CHIP_ASC38C0800:
		/* FALLTHROUGH */
	case ADW_CHIP_ASC38C1600:
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DISC_ENABLE,
							sc->cfg.disc_enable);
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_SPEED1,
							sc->sdtr_speed1);
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_SPEED2,
							sc->sdtr_speed2);
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_SPEED3,
							sc->sdtr_speed3);
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_SPEED4,
							sc->sdtr_speed4);
		break;
	}


	/*
	 * Set SCSI_CFG0 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG0 register using this value
	 * after it is started below.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DEFAULT_SCSI_CFG0,
		ADW_PARITY_EN | ADW_QUEUE_128 | ADW_SEL_TMO_LONG |
		ADW_OUR_ID_EN | sc->chip_scsi_id);


	switch(sc->chip_type) {
	case ADW_CHIP_ASC3550:
		error_code = AdwASC3550Cabling(iot, ioh, &sc->cfg);
		break;

	case ADW_CHIP_ASC38C0800:
		error_code = AdwASC38C0800Cabling(iot, ioh, &sc->cfg);
		break;

	case ADW_CHIP_ASC38C1600:
		error_code = AdwASC38C1600Cabling(iot, ioh, &sc->cfg);
		break;
	}
	if(error_code) {
		return error_code;
	}

	/*
	 * Set SEL_MASK Microcode Default Value
	 *
	 * The microcode will set the SEL_MASK register using this value
	 * after it is started below.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DEFAULT_SEL_MASK,
		ADW_TID_TO_TIDMASK(sc->chip_scsi_id));

	/*
	 * Create and Initialize Host->RISC Carrier lists
	 */
	sc->carr_freelist = AdwInitCarriers(sc->sc_dmamap_carrier,
						sc->sc_control->carriers);

	/*
	 * Set-up the Host->RISC Initiator Command Queue (ICQ).
	 */

	if ((sc->icq_sp = sc->carr_freelist) == NULL) {
		return ADW_IERR_NO_CARRIER;
	}
	sc->carr_freelist = ADW_CARRIER_VADDR(sc,
			ADW_GET_CARRP(sc->icq_sp->next_ba));

	/*
	 * The first command issued will be placed in the stopper carrier.
	 */
	sc->icq_sp->next_ba = ADW_CQ_STOPPER;

	/*
	 * Set RISC ICQ physical address start value.
	 */
	ADW_WRITE_DWORD_LRAM(iot, ioh, ADW_MC_ICQ, sc->icq_sp->carr_ba);

	/*
	 * Initialize the COMMA register to the same value otherwise
	 * the RISC will prematurely detect a command is available.
	 */
	if(sc->chip_type == ADW_CHIP_ASC38C1600) {
		ADW_WRITE_DWORD_REGISTER(iot, ioh, IOPDW_COMMA,
							sc->icq_sp->carr_ba);
	}

	/*
	 * Set-up the RISC->Host Initiator Response Queue (IRQ).
	 */
	if ((sc->irq_sp = sc->carr_freelist) == NULL) {
		return ADW_IERR_NO_CARRIER;
	}
	sc->carr_freelist = ADW_CARRIER_VADDR(sc,
			ADW_GET_CARRP(sc->irq_sp->next_ba));

	/*
	 * The first command completed by the RISC will be placed in
	 * the stopper.
	 *
	 * Note: Set 'next_ba' to ADW_CQ_STOPPER. When the request is
	 * completed the RISC will set the ADW_RQ_DONE bit.
	 */
	sc->irq_sp->next_ba = ADW_CQ_STOPPER;

	/*
	 * Set RISC IRQ physical address start value.
	 */
	ADW_WRITE_DWORD_LRAM(iot, ioh, ADW_MC_IRQ, sc->irq_sp->carr_ba);
	sc->carr_pending_cnt = 0;

	ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_INTR_ENABLES,
		(ADW_INTR_ENABLE_HOST_INTR | ADW_INTR_ENABLE_GLOBAL_INTR));
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_CODE_BEGIN_ADDR, word);
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_PC, word);

	/* finally, finally, gentlemen, start your engine */
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_RISC_CSR, ADW_RISC_CSR_RUN);

	/*
	 * Reset the SCSI Bus if the EEPROM indicates that SCSI Bus
	 * Resets should be performed. The RISC has to be running
	 * to issue a SCSI Bus Reset.
	 */
	if (sc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS)
	{
		/*
		 * If the BIOS Signature is present in memory, restore the
		 * BIOS Handshake Configuration Table and do not perform
		 * a SCSI Bus Reset.
		 */
		if (bios_mem[(ADW_MC_BIOS_SIGNATURE - ADW_MC_BIOSMEM)/2] ==
				0x55AA) {
			/*
			 * Restore per TID negotiated values.
			 */
			ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE,
					wdtr_able);
			ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_ABLE,
					sdtr_able);
			ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_TAGQNG_ABLE,
					tagqng_able);
			for (tid = 0; tid <= ADW_MAX_TID; tid++) {
				ADW_WRITE_BYTE_LRAM(iot, ioh,
						ADW_MC_NUMBER_OF_MAX_CMD + tid,
						max_cmd[tid]);
			}
		} else {
			if (AdwResetCCB(sc) != ADW_TRUE) {
				error_code = ADW_WARN_BUSRESET_ERROR;
			}
		}
	}

	return error_code;
}


int
AdwRamSelfTest(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t chip_type)
{
	int		i;
	u_int8_t	byte;


	if ((chip_type == ADW_CHIP_ASC38C0800) ||
	    (chip_type == ADW_CHIP_ASC38C1600)) {
		/*
		 * RAM BIST (RAM Built-In Self Test)
		 *
		 * Address : I/O base + offset 0x38h register (byte).
		 * Function: Bit 7-6(RW) : RAM mode
		 *			    Normal Mode   : 0x00
		 *			    Pre-test Mode : 0x40
		 *			    RAM Test Mode : 0x80
		 *	     Bit 5	 : unused
		 *	     Bit 4(RO)   : Done bit
		 *	     Bit 3-0(RO) : Status
		 *			    Host Error    : 0x08
		 *			    Int_RAM Error : 0x04
		 *			    RISC Error    : 0x02
		 *			    SCSI Error    : 0x01
		 *			    No Error	  : 0x00
		 *
		 * Note: RAM BIST code should be put right here, before loading
		 * the microcode and after saving the RISC memory BIOS region.
		 */

		/*
		 * LRAM Pre-test
		 *
		 * Write PRE_TEST_MODE (0x40) to register and wait for
		 * 10 milliseconds.
		 * If Done bit not set or low nibble not PRE_TEST_VALUE (0x05),
		 * return an error. Reset to NORMAL_MODE (0x00) and do again.
		 * If cannot reset to NORMAL_MODE, return an error too.
		 */
		for (i = 0; i < 2; i++) {
			ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_RAM_BIST,
					PRE_TEST_MODE);
			 /* Wait for 10ms before reading back. */
			AdwSleepMilliSecond(10);
			byte = ADW_READ_BYTE_REGISTER(iot, ioh, IOPB_RAM_BIST);
			if ((byte & RAM_TEST_DONE) == 0 || (byte & 0x0F) !=
					PRE_TEST_VALUE) {
				return ADW_IERR_BIST_PRE_TEST;
			}

			ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_RAM_BIST,
								NORMAL_MODE);
			/* Wait for 10ms before reading back. */
			AdwSleepMilliSecond(10);
			if (ADW_READ_BYTE_REGISTER(iot, ioh, IOPB_RAM_BIST)
			    != NORMAL_VALUE) {
				return ADW_IERR_BIST_PRE_TEST;
			}
		}

		/*
		 * LRAM Test - It takes about 1.5 ms to run through the test.
		 *
		 * Write RAM_TEST_MODE (0x80) to register and wait for
		 * 10 milliseconds.
		 * If Done bit not set or Status not 0, save register byte,
		 * set the err_code, and return an error.
		 */
		ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_RAM_BIST, RAM_TEST_MODE);
		/* Wait for 10ms before checking status. */
		AdwSleepMilliSecond(10);

		byte = ADW_READ_BYTE_REGISTER(iot, ioh, IOPB_RAM_BIST);
		if ((byte & RAM_TEST_DONE)==0 || (byte & RAM_TEST_STATUS)!=0) {
			/* Get here if Done bit not set or Status not 0. */
			return ADW_IERR_BIST_RAM_TEST;
		}

		/* We need to reset back to normal mode after LRAM test passes*/
		ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_RAM_BIST, NORMAL_MODE);
	}

	return 0;
}


int
AdwLoadMCode(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t *bios_mem,
    u_int8_t chip_type)
{
	u_int8_t	*mcode_data = NULL;
	u_int32_t	 mcode_chksum = 0;
	u_int16_t	 mcode_size = 0;
	u_int32_t	sum;
	u_int16_t	code_sum;
	int		begin_addr;
	int		end_addr;
	int		word;
	int		adw_memsize = 0;
	int		adw_mcode_expanded_size;
	int		i, j;


	switch(chip_type) {
	case ADW_CHIP_ASC3550:
		mcode_data = (u_int8_t *)adw_asc3550_mcode_data.mcode_data;
		mcode_chksum = (u_int32_t)adw_asc3550_mcode_data.mcode_chksum;
		mcode_size = (u_int16_t)adw_asc3550_mcode_data.mcode_size;
		adw_memsize = ADW_3550_MEMSIZE;
		break;

	case ADW_CHIP_ASC38C0800:
		mcode_data = (u_int8_t *)adw_asc38C0800_mcode_data.mcode_data;
		mcode_chksum =(u_int32_t)adw_asc38C0800_mcode_data.mcode_chksum;
		mcode_size = (u_int16_t)adw_asc38C0800_mcode_data.mcode_size;
		adw_memsize = ADW_38C0800_MEMSIZE;
		break;

	case ADW_CHIP_ASC38C1600:
		mcode_data = (u_int8_t *)adw_asc38C1600_mcode_data.mcode_data;
		mcode_chksum =(u_int32_t)adw_asc38C1600_mcode_data.mcode_chksum;
		mcode_size = (u_int16_t)adw_asc38C1600_mcode_data.mcode_size;
		adw_memsize = ADW_38C1600_MEMSIZE;
		break;
	}

	/*
	 * Write the microcode image to RISC memory starting at address 0.
	 */
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_RAM_ADDR, 0);

	/* Assume the following compressed format of the microcode buffer:
	 *
	 *  254 word (508 byte) table indexed by byte code followed
	 *  by the following byte codes:
	 *
	 *    1-Byte Code:
	 *	00: Emit word 0 in table.
	 *	01: Emit word 1 in table.
	 *	.
	 *	FD: Emit word 253 in table.
	 *
	 *    Multi-Byte Code:
	 *	FE WW WW: (3 byte code) Word to emit is the next word WW WW.
	 *	FF BB WW WW: (4 byte code) Emit BB count times next word WW WW.
	 */
	word = 0;
	for (i = 253 * 2; i < mcode_size; i++) {
		if (mcode_data[i] == 0xff) {
			for (j = 0; j < mcode_data[i + 1]; j++) {
				ADW_WRITE_WORD_AUTO_INC_LRAM(iot, ioh,
				  (((u_int16_t)mcode_data[i + 3] << 8) |
				  mcode_data[i + 2]));
				word++;
			}
			i += 3;
		} else if (mcode_data[i] == 0xfe) {
			ADW_WRITE_WORD_AUTO_INC_LRAM(iot, ioh,
			    (((u_int16_t)mcode_data[i + 2] << 8) |
			    mcode_data[i + 1]));
			i += 2;
			word++;
		} else {
			ADW_WRITE_WORD_AUTO_INC_LRAM(iot, ioh, (((u_int16_t)
			 mcode_data[(mcode_data[i] * 2) + 1] <<8) |
			 mcode_data[mcode_data[i] * 2]));
			word++;
		}
	}

	/*
	 * Set 'word' for later use to clear the rest of memory and save
	 * the expanded mcode size.
	 */
	word *= 2;
	adw_mcode_expanded_size = word;

	/*
	 * Clear the rest of the Internal RAM.
	 */
	for (; word < adw_memsize; word += 2) {
		ADW_WRITE_WORD_AUTO_INC_LRAM(iot, ioh, 0);
	}

	/*
	 * Verify the microcode checksum.
	 */
	sum = 0;
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_RAM_ADDR, 0);

	for (word = 0; word < adw_mcode_expanded_size; word += 2) {
		sum += ADW_READ_WORD_AUTO_INC_LRAM(iot, ioh);
	}

	if (sum != mcode_chksum) {
		return ADW_IERR_MCODE_CHKSUM;
	}

	/*
	 * Restore the RISC memory BIOS region.
	 */
	for (i = 0; i < ADW_MC_BIOSLEN/2; i++) {
		if(chip_type == ADW_CHIP_ASC3550) {
			ADW_WRITE_BYTE_LRAM(iot, ioh, ADW_MC_BIOSMEM + (2 * i),
								bios_mem[i]);
		} else {
			ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_BIOSMEM + (2 * i),
								bios_mem[i]);
		}
	}

	/*
	 * Calculate and write the microcode code checksum to the microcode
	 * code checksum location ADW_MC_CODE_CHK_SUM (0x2C).
	 */
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_CODE_BEGIN_ADDR, begin_addr);
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_CODE_END_ADDR, end_addr);
	code_sum = 0;
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_RAM_ADDR, begin_addr);
	for (word = begin_addr; word < end_addr; word += 2) {
		code_sum += ADW_READ_WORD_AUTO_INC_LRAM(iot, ioh);
	}
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_CODE_CHK_SUM, code_sum);

	/*
	 * Set the chip type.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_CHIP_TYPE, chip_type);

	return 0;
}


int
AdwASC3550Cabling(bus_space_tag_t iot, bus_space_handle_t ioh, ADW_DVC_CFG *cfg)
{
	u_int16_t	scsi_cfg1;


	/*
	 * Determine SCSI_CFG1 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */

	/* Read current SCSI_CFG1 Register value. */
	scsi_cfg1 = ADW_READ_WORD_REGISTER(iot, ioh, IOPW_SCSI_CFG1);

	/*
	 * If all three connectors are in use in ASC3550, return an error.
	 */
	if ((scsi_cfg1 & CABLE_ILLEGAL_A) == 0 ||
	     (scsi_cfg1 & CABLE_ILLEGAL_B) == 0) {
		return ADW_IERR_ILLEGAL_CONNECTION;
	}

	/*
	 * If the cable is reversed all of the SCSI_CTRL register signals
	 * will be set. Check for and return an error if this condition is
	 * found.
	 */
	if ((ADW_READ_WORD_REGISTER(iot,ioh, IOPW_SCSI_CTRL) & 0x3F07)==0x3F07){
		return ADW_IERR_REVERSED_CABLE;
	}

	/*
	 * If this is a differential board and a single-ended device
	 * is attached to one of the connectors, return an error.
	 */
	if ((scsi_cfg1 & ADW_DIFF_MODE) &&
	    (scsi_cfg1 & ADW_DIFF_SENSE) == 0) {
		return ADW_IERR_SINGLE_END_DEVICE;
	}

	/*
	 * If automatic termination control is enabled, then set the
	 * termination value based on a table listed in a_condor.h.
	 *
	 * If manual termination was specified with an EEPROM setting
	 * then 'termination' was set-up in AdwInitFromEEPROM() and
	 * is ready to be 'ored' into SCSI_CFG1.
	 */
	if (cfg->termination == 0) {
		/*
		 * The software always controls termination by setting
		 * TERM_CTL_SEL.
		 * If TERM_CTL_SEL were set to 0, the hardware would set
		 * termination.
		 */
		cfg->termination |= ADW_TERM_CTL_SEL;

		switch(scsi_cfg1 & ADW_CABLE_DETECT) {
			/* TERM_CTL_H: on, TERM_CTL_L: on */
			case 0x3: case 0x7: case 0xB:
			case 0xD: case 0xE: case 0xF:
				cfg->termination |=
				(ADW_TERM_CTL_H | ADW_TERM_CTL_L);
				break;

			/* TERM_CTL_H: on, TERM_CTL_L: off */
			case 0x1: case 0x5: case 0x9:
			case 0xA: case 0xC:
				cfg->termination |= ADW_TERM_CTL_H;
				break;

			/* TERM_CTL_H: off, TERM_CTL_L: off */
			case 0x2: case 0x6:
				break;
		}
	}

	/*
	 * Clear any set TERM_CTL_H and TERM_CTL_L bits.
	 */
	scsi_cfg1 &= ~ADW_TERM_CTL;

	/*
	 * Invert the TERM_CTL_H and TERM_CTL_L bits and then
	 * set 'scsi_cfg1'. The TERM_POL bit does not need to be
	 * referenced, because the hardware internally inverts
	 * the Termination High and Low bits if TERM_POL is set.
	 */
	scsi_cfg1 |= (ADW_TERM_CTL_SEL | (~cfg->termination & ADW_TERM_CTL));

	/*
	 * Set SCSI_CFG1 Microcode Default Value
	 *
	 * Set filter value and possibly modified termination control
	 * bits in the Microcode SCSI_CFG1 Register Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DEFAULT_SCSI_CFG1,
						ADW_FLTR_DISABLE | scsi_cfg1);

	/*
	 * Set MEM_CFG Microcode Default Value
	 *
	 * The microcode will set the MEM_CFG register using this value
	 * after it is started below.
	 *
	 * MEM_CFG may be accessed as a word or byte, but only bits 0-7
	 * are defined.
	 *
	 * ASC-3550 has 8KB internal memory.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DEFAULT_MEM_CFG,
						ADW_BIOS_EN | ADW_RAM_SZ_8KB);

	return 0;
}


int
AdwASC38C0800Cabling(bus_space_tag_t iot, bus_space_handle_t ioh,
    ADW_DVC_CFG *cfg)
{
	u_int16_t	scsi_cfg1;


	/*
	 * Determine SCSI_CFG1 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */

	/* Read current SCSI_CFG1 Register value. */
	scsi_cfg1 = ADW_READ_WORD_REGISTER(iot, ioh, IOPW_SCSI_CFG1);

	/*
	 * If the cable is reversed all of the SCSI_CTRL register signals
	 * will be set. Check for and return an error if this condition is
	 * found.
	 */
	if ((ADW_READ_WORD_REGISTER(iot,ioh, IOPW_SCSI_CTRL) & 0x3F07)==0x3F07){
		return ADW_IERR_REVERSED_CABLE;
	}

	/*
	 * All kind of combinations of devices attached to one of four
	 * connectors are acceptable except HVD device attached.
	 * For example, LVD device can be attached to SE connector while
	 * SE device attached to LVD connector.
	 * If LVD device attached to SE connector, it only runs up to
	 * Ultra speed.
	 *
	 * If an HVD device is attached to one of LVD connectors, return
	 * an error.
	 * However, there is no way to detect HVD device attached to
	 * SE connectors.
	 */
	if (scsi_cfg1 & ADW_HVD) {
		return ADW_IERR_HVD_DEVICE;
	}

	/*
	 * If either SE or LVD automatic termination control is enabled, then
	 * set the termination value based on a table listed in a_condor.h.
	 *
	 * If manual termination was specified with an EEPROM setting then
	 * 'termination' was set-up in AdwInitFromEEPROM() and is ready
	 * to be 'ored' into SCSI_CFG1.
	 */
	if ((cfg->termination & ADW_TERM_SE) == 0) {
		/* SE automatic termination control is enabled. */
		switch(scsi_cfg1 & ADW_C_DET_SE) {
			/* TERM_SE_HI: on, TERM_SE_LO: on */
			case 0x1: case 0x2: case 0x3:
				cfg->termination |= ADW_TERM_SE;
				break;

			/* TERM_SE_HI: on, TERM_SE_LO: off */
			case 0x0:
				cfg->termination |= ADW_TERM_SE_HI;
				break;
		}
	}

	if ((cfg->termination & ADW_TERM_LVD) == 0) {
		/* LVD automatic termination control is enabled. */
		switch(scsi_cfg1 & ADW_C_DET_LVD) {
			/* TERM_LVD_HI: on, TERM_LVD_LO: on */
			case 0x4: case 0x8: case 0xC:
				cfg->termination |= ADW_TERM_LVD;
				break;

			/* TERM_LVD_HI: off, TERM_LVD_LO: off */
			case 0x0:
				break;
		}
	}

	/*
	 * Clear any set TERM_SE and TERM_LVD bits.
	 */
	scsi_cfg1 &= (~ADW_TERM_SE & ~ADW_TERM_LVD);

	/*
	 * Invert the TERM_SE and TERM_LVD bits and then set 'scsi_cfg1'.
	 */
	scsi_cfg1 |= (~cfg->termination & 0xF0);

	/*
	 * Clear BIG_ENDIAN, DIS_TERM_DRV, Terminator Polarity and
	 * HVD/LVD/SE bits and set possibly modified termination control bits
	 * in the Microcode SCSI_CFG1 Register Value.
	 */
	scsi_cfg1 &= (~ADW_BIG_ENDIAN & ~ADW_DIS_TERM_DRV &
					~ADW_TERM_POL & ~ADW_HVD_LVD_SE);

	/*
	 * Set SCSI_CFG1 Microcode Default Value
	 *
	 * Set possibly modified termination control and reset DIS_TERM_DRV
	 * bits in the Microcode SCSI_CFG1 Register Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DEFAULT_SCSI_CFG1, scsi_cfg1);

	/*
	 * Set MEM_CFG Microcode Default Value
	 *
	 * The microcode will set the MEM_CFG register using this value
	 * after it is started below.
	 *
	 * MEM_CFG may be accessed as a word or byte, but only bits 0-7
	 * are defined.
	 *
	 * ASC-38C0800 has 16KB internal memory.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DEFAULT_MEM_CFG,
						ADW_BIOS_EN | ADW_RAM_SZ_16KB);

	return 0;
}


int
AdwASC38C1600Cabling(bus_space_tag_t iot, bus_space_handle_t ioh,
    ADW_DVC_CFG *cfg)
{
	u_int16_t	scsi_cfg1;


	/*
	 * Determine SCSI_CFG1 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 * Each ASC-38C1600 function has only two cable detect bits.
	 * The bus mode override bits are in IOPB_SOFT_OVER_WR.
	 */

	/* Read current SCSI_CFG1 Register value. */
	scsi_cfg1 = ADW_READ_WORD_REGISTER(iot, ioh, IOPW_SCSI_CFG1);

	/*
	 * If the cable is reversed all of the SCSI_CTRL register signals
	 * will be set. Check for and return an error if this condition is
	 * found.
	 */
	if ((ADW_READ_WORD_REGISTER(iot,ioh, IOPW_SCSI_CTRL) & 0x3F07)==0x3F07){
		return ADW_IERR_REVERSED_CABLE;
	}

	/*
	 * Each ASC-38C1600 function has two connectors. Only an HVD device
	 * cannot be connected to either connector. An LVD device or SE device
	 * may be connected to either connector. If an SE device is connected,
	 * then at most Ultra speed (20 MHz) can be used on both connectors.
	 *
	 * If an HVD device is attached, return an error.
	 */
	if (scsi_cfg1 & ADW_HVD) {
		return ADW_IERR_HVD_DEVICE;
	}

	/*
	 * Each function in the ASC-38C1600 uses only the SE cable detect and
	 * termination because there are two connectors for each function.
	 * Each function may use either LVD or SE mode.
	 * Corresponding the SE automatic termination control EEPROM bits are
	 * used for each function.
	 * Each function has its own EEPROM. If SE automatic control is enabled
	 * for the function, then set the termination value based on a table
	 * listed in adwlib.h.
	 *
	 * If manual termination is specified in the EEPROM for the function,
	 * then 'termination' was set-up in AdwInitFromEEPROM() and is
	 * ready to be 'ored' into SCSI_CFG1.
	 */
	if ((cfg->termination & ADW_TERM_SE) == 0) {
		/* SE automatic termination control is enabled. */
		switch(scsi_cfg1 & ADW_C_DET_SE) {
			/* TERM_SE_HI: on, TERM_SE_LO: on */
			case 0x1: case 0x2: case 0x3:
				cfg->termination |= ADW_TERM_SE;
				break;

			case 0x0:
	/* !!!!TODO!!!! */
//				if (ASC_PCI_ID2FUNC(cfg->pci_slot_info) == 0) {
				/* Function 0 - TERM_SE_HI: off, TERM_SE_LO: off */
//				}
//				else
//				{
				/* Function 1 - TERM_SE_HI: on, TERM_SE_LO: off */
					cfg->termination |= ADW_TERM_SE_HI;
//				}
				break;
			}
	}

	/*
	 * Clear any set TERM_SE bits.
	 */
	scsi_cfg1 &= ~ADW_TERM_SE;

	/*
	 * Invert the TERM_SE bits and then set 'scsi_cfg1'.
	 */
	scsi_cfg1 |= (~cfg->termination & ADW_TERM_SE);

	/*
	 * Clear Big Endian and Terminator Polarity bits and set possibly
	 * modified termination control bits in the Microcode SCSI_CFG1
	 * Register Value.
	 */
	scsi_cfg1 &= (~ADW_BIG_ENDIAN & ~ADW_DIS_TERM_DRV & ~ADW_TERM_POL);

	/*
	 * Set SCSI_CFG1 Microcode Default Value
	 *
	 * Set possibly modified termination control bits in the Microcode
	 * SCSI_CFG1 Register Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DEFAULT_SCSI_CFG1, scsi_cfg1);

	/*
	 * Set MEM_CFG Microcode Default Value
	 *
	 * The microcode will set the MEM_CFG register using this value
	 * after it is started below.
	 *
	 * MEM_CFG may be accessed as a word or byte, but only bits 0-7
	 * are defined.
	 *
	 * ASC-38C1600 has 32KB internal memory.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_DEFAULT_MEM_CFG,
						ADW_BIOS_EN | ADW_RAM_SZ_32KB);

	return 0;
}


/*
 * Read EEPROM configuration into the specified buffer.
 *
 * Return a checksum based on the EEPROM configuration read.
 */
u_int16_t
AdwGetEEPROMConfig(bus_space_tag_t iot, bus_space_handle_t ioh,
    ADW_EEPROM *cfg_buf)
{
	u_int16_t	       wval, chksum;
	u_int16_t	       *wbuf;
	int		    eep_addr;


	wbuf = (u_int16_t *) cfg_buf;
	chksum = 0;

	for (eep_addr = ADW_EEP_DVC_CFG_BEGIN;
		eep_addr < ADW_EEP_DVC_CFG_END;
		eep_addr++, wbuf++) {
		wval = AdwReadEEPWord(iot, ioh, eep_addr);
		chksum += wval;
		*wbuf = wval;
	}

	*wbuf = AdwReadEEPWord(iot, ioh, eep_addr);
	wbuf++;
	for (eep_addr = ADW_EEP_DVC_CTL_BEGIN;
			eep_addr < ADW_EEP_MAX_WORD_ADDR;
			eep_addr++, wbuf++) {
		*wbuf = AdwReadEEPWord(iot, ioh, eep_addr);
	}

	return chksum;
}


/*
 * Read the EEPROM from specified location
 */
u_int16_t
AdwReadEEPWord(bus_space_tag_t iot, bus_space_handle_t ioh, int eep_word_addr)
{
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_CMD,
		ADW_EEP_CMD_READ | eep_word_addr);
	AdwWaitEEPCmd(iot, ioh);

	return ADW_READ_WORD_REGISTER(iot, ioh, IOPW_EE_DATA);
}


/*
 * Wait for EEPROM command to complete
 */
void
AdwWaitEEPCmd(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int eep_delay_ms;


	for (eep_delay_ms = 0; eep_delay_ms < ADW_EEP_DELAY_MS; eep_delay_ms++){
		if (ADW_READ_WORD_REGISTER(iot, ioh, IOPW_EE_CMD) &
				ADW_EEP_CMD_DONE) {
			break;
		}
		AdwSleepMilliSecond(1);
	}

	ADW_READ_WORD_REGISTER(iot, ioh, IOPW_EE_CMD);
}


/*
 * Write the EEPROM from 'cfg_buf'.
 */
void
AdwSetEEPROMConfig(bus_space_tag_t iot, bus_space_handle_t ioh,
    ADW_EEPROM *cfg_buf)
{
	u_int16_t *wbuf;
	u_int16_t addr, chksum;


	wbuf = (u_int16_t *) cfg_buf;
	chksum = 0;

	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_CMD, ADW_EEP_CMD_WRITE_ABLE);
	AdwWaitEEPCmd(iot, ioh);

	/*
	 * Write EEPROM from word 0 to word 20
	 */
	for (addr = ADW_EEP_DVC_CFG_BEGIN;
	     addr < ADW_EEP_DVC_CFG_END; addr++, wbuf++) {
		chksum += *wbuf;
		ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_DATA, *wbuf);
		ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_CMD,
				ADW_EEP_CMD_WRITE | addr);
		AdwWaitEEPCmd(iot, ioh);
		AdwSleepMilliSecond(ADW_EEP_DELAY_MS);
	}

	/*
	 * Write EEPROM checksum at word 21
	 */
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_DATA, chksum);
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_CMD,
			ADW_EEP_CMD_WRITE | addr);
	AdwWaitEEPCmd(iot, ioh);
	wbuf++;        /* skip over check_sum */

	/*
	 * Write EEPROM OEM name at words 22 to 29
	 */
	for (addr = ADW_EEP_DVC_CTL_BEGIN;
	     addr < ADW_EEP_MAX_WORD_ADDR; addr++, wbuf++) {
		ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_DATA, *wbuf);
		ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_CMD,
				ADW_EEP_CMD_WRITE | addr);
		AdwWaitEEPCmd(iot, ioh);
	}

	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_EE_CMD,
			ADW_EEP_CMD_WRITE_DISABLE);
	AdwWaitEEPCmd(iot, ioh);

	return;
}


/*
 * AdwExeScsiQueue() - Send a request to the RISC microcode program.
 *
 *   Allocate a carrier structure, point the carrier to the ADW_SCSI_REQ_Q,
 *   add the carrier to the ICQ (Initiator Command Queue), and tickle the
 *   RISC to notify it a new command is ready to be executed.
 *
 * If 'done_status' is not set to QD_DO_RETRY, then 'error_retry' will be
 * set to SCSI_MAX_RETRY.
 *
 * Return:
 *      ADW_SUCCESS(1) - The request was successfully queued.
 *      ADW_BUSY(0) -    Resource unavailable; Retry again after pending
 *                       request completes.
 *      ADW_ERROR(-1) -  Invalid ADW_SCSI_REQ_Q request structure
 *                       host IC error.
 */
int
AdwExeScsiQueue(ADW_SOFTC *sc, ADW_SCSI_REQ_Q *scsiq)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	ADW_CCB		*ccb;
	long		req_size;
	u_int32_t	req_paddr;
	ADW_CARRIER	*new_carrp;

	/*
	 * The ADW_SCSI_REQ_Q 'target_id' field should never exceed ADW_MAX_TID.
	 */
	if (scsiq->target_id > ADW_MAX_TID) {
		scsiq->host_status = QHSTA_M_INVALID_DEVICE;
		scsiq->done_status = QD_WITH_ERROR;
		return ADW_ERROR;
	}

	/*
	 * Beginning of CRITICAL SECTION: ASSUME splbio() in effect
	 */

	ccb = adw_ccb_phys_kv(sc, scsiq->ccb_ptr);

	/*
	 * Allocate a carrier and initialize fields.
	 */
	if ((new_carrp = sc->carr_freelist) == NULL) {
		return ADW_BUSY;
	}
	sc->carr_freelist = ADW_CARRIER_VADDR(sc,
			ADW_GET_CARRP(new_carrp->next_ba));
	sc->carr_pending_cnt++;

	/*
	 * Set the carrier to be a stopper by setting 'next_ba'
	 * to the stopper value. The current stopper will be changed
	 * below to point to the new stopper.
	 */
	new_carrp->next_ba = ADW_CQ_STOPPER;

	req_size = sizeof(ADW_SCSI_REQ_Q);
	req_paddr = sc->sc_dmamap_control->dm_segs[0].ds_addr +
		ADW_CCB_OFF(ccb) + offsetof(struct adw_ccb, scsiq);

	/* Save physical address of ADW_SCSI_REQ_Q and Carrier. */
	scsiq->scsiq_rptr = req_paddr;

	/*
	 * Every ADW_SCSI_REQ_Q.carr_ba is byte swapped to little-endian
	 * order during initialization.
	 */
	scsiq->carr_ba = sc->icq_sp->carr_ba;
	scsiq->carr_va = sc->icq_sp->carr_ba;

	/*
	 * Use the current stopper to send the ADW_SCSI_REQ_Q command to
	 * the microcode. The newly allocated stopper will become the new
	 * stopper.
	 */
	sc->icq_sp->areq_ba = req_paddr;

	/*
	 * Set the 'next_ba' pointer for the old stopper to be the
	 * physical address of the new stopper. The RISC can only
	 * follow physical addresses.
	 */
	sc->icq_sp->next_ba = new_carrp->carr_ba;

#if ADW_DEBUG
	printf("icq 0x%x, 0x%x, 0x%x, 0x%x\n",
			sc->icq_sp->carr_id,
			sc->icq_sp->carr_ba,
			sc->icq_sp->areq_ba,
			sc->icq_sp->next_ba);
#endif
	/*
	 * Set the host adapter stopper pointer to point to the new carrier.
	 */
	sc->icq_sp = new_carrp;

	if (sc->chip_type == ADW_CHIP_ASC3550 ||
	    sc->chip_type == ADW_CHIP_ASC38C0800) {
		/*
		 * Tickle the RISC to tell it to read its Command Queue Head
		 * pointer.
		 */
		ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_TICKLE, ADW_TICKLE_A);
		if (sc->chip_type == ADW_CHIP_ASC3550) {
			/*
			 * Clear the tickle value. In the ASC-3550 the RISC flag
			 * command 'clr_tickle_a' does not work unless the host
			 * value is cleared.
			 */
			ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_TICKLE,
					ADW_TICKLE_NOP);
		}
	} else if (sc->chip_type == ADW_CHIP_ASC38C1600) {
		/*
		 * Notify the RISC a carrier is ready by writing the physical
		 * address of the new carrier stopper to the COMMA register.
		 */
		ADW_WRITE_DWORD_REGISTER(iot, ioh, IOPDW_COMMA,
				new_carrp->carr_ba);
	}

	/*
	 * End of CRITICAL SECTION: Must be protected within splbio/splx pair
	 */

	return ADW_SUCCESS;
}


void
AdwResetChip(bus_space_tag_t iot, bus_space_handle_t ioh)
{

	/*
	 * Reset Chip.
	 */
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_CTRL_REG,
			ADW_CTRL_REG_CMD_RESET);
	AdwSleepMilliSecond(100);
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_CTRL_REG,
			ADW_CTRL_REG_CMD_WR_IO_REG);
}


/*
 * Reset SCSI Bus and purge all outstanding requests.
 *
 * Return Value:
 *      ADW_TRUE(1) -   All requests are purged and SCSI Bus is reset.
 *      ADW_FALSE(0) -  Microcode command failed.
 *      ADW_ERROR(-1) - Microcode command timed-out. Microcode or IC
 *                      may be hung which requires driver recovery.
 */
int
AdwResetCCB(ADW_SOFTC *sc)
{
	int	    status;

	/*
	 * Send the SCSI Bus Reset idle start idle command which asserts
	 * the SCSI Bus Reset signal.
	 */
	status = AdwSendIdleCmd(sc, (u_int16_t) IDLE_CMD_SCSI_RESET_START, 0L);
	if (status != ADW_TRUE) {
		return status;
	}

	/*
	 * Delay for the specified SCSI Bus Reset hold time.
	 *
	 * The hold time delay is done on the host because the RISC has no
	 * microsecond accurate timer.
	 */
	AdwDelayMicroSecond((u_int16_t) ADW_SCSI_RESET_HOLD_TIME_US);

	/*
	 * Send the SCSI Bus Reset end idle command which de-asserts
	 * the SCSI Bus Reset signal and purges any pending requests.
	 */
	status = AdwSendIdleCmd(sc, (u_int16_t) IDLE_CMD_SCSI_RESET_END, 0L);
	if (status != ADW_TRUE) {
		return status;
	}

	AdwSleepMilliSecond((u_int32_t) sc->scsi_reset_wait * 1000);

	return status;
}


/*
 * Reset chip and SCSI Bus.
 *
 * Return Value:
 *      ADW_TRUE(1) -   Chip re-initialization and SCSI Bus Reset successful.
 *      ADW_FALSE(0) -  Chip re-initialization and SCSI Bus Reset failure.
 */
int
AdwResetSCSIBus(ADW_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int		status;
	u_int16_t	wdtr_able, sdtr_able, ppr_able = 0, tagqng_able;
	u_int8_t	tid, max_cmd[ADW_MAX_TID + 1];
	u_int16_t	bios_sig;


	/*
	 * Save current per TID negotiated values.
	 */
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE, wdtr_able);
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_SDTR_ABLE, sdtr_able);
	if (sc->chip_type == ADW_CHIP_ASC38C1600) {
		ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_PPR_ABLE, ppr_able);
	}
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_TAGQNG_ABLE, tagqng_able);
	for (tid = 0; tid <= ADW_MAX_TID; tid++) {
		ADW_READ_BYTE_LRAM(iot, ioh, ADW_MC_NUMBER_OF_MAX_CMD + tid,
			max_cmd[tid]);
	}

	/*
	 * Force the AdwInitAscDriver() function to perform a SCSI Bus Reset
	 * by clearing the BIOS signature word.
	 * The initialization functions assumes a SCSI Bus Reset is not
	 * needed if the BIOS signature word is present.
	 */
	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_BIOS_SIGNATURE, bios_sig);
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_BIOS_SIGNATURE, 0);

	/*
	 * Stop chip and reset it.
	 */
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_RISC_CSR, ADW_RISC_CSR_STOP);
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_CTRL_REG,
			ADW_CTRL_REG_CMD_RESET);
	AdwSleepMilliSecond(100);
	ADW_WRITE_WORD_REGISTER(iot, ioh, IOPW_CTRL_REG,
			ADW_CTRL_REG_CMD_WR_IO_REG);

	/*
	 * Reset Adw Library error code, if any, and try
	 * re-initializing the chip.
	 * Then translate initialization return value to status value.
	 */
	status = (AdwInitDriver(sc) == 0)? ADW_TRUE : ADW_FALSE;

	/*
	 * Restore the BIOS signature word.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_BIOS_SIGNATURE, bios_sig);

	/*
	 * Restore per TID negotiated values.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE, wdtr_able);
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_ABLE, sdtr_able);
	if (sc->chip_type == ADW_CHIP_ASC38C1600) {
		ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_PPR_ABLE, ppr_able);
	}
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_TAGQNG_ABLE, tagqng_able);
	for (tid = 0; tid <= ADW_MAX_TID; tid++) {
		ADW_WRITE_BYTE_LRAM(iot, ioh, ADW_MC_NUMBER_OF_MAX_CMD + tid,
			max_cmd[tid]);
	}

	return status;
}


/*
 * Adw Library Interrupt Service Routine
 *
 *  This function is called by a driver's interrupt service routine.
 *  The function disables and re-enables interrupts.
 *
 *  Note: AdwISR() can be called when interrupts are disabled or even
 *  when there is no hardware interrupt condition present. It will
 *  always check for completed idle commands and microcode requests.
 *  This is an important feature that shouldn't be changed because it
 *  allows commands to be completed from polling mode loops.
 *
 * Return:
 *   ADW_TRUE(1) - interrupt was pending
 *   ADW_FALSE(0) - no interrupt was pending
 */
int
AdwISR(ADW_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t	int_stat;
	u_int16_t	target_bit;
	ADW_CARRIER	*free_carrp/*, *ccb_carr*/;
	u_int32_t	irq_next_pa;
	ADW_SCSI_REQ_Q	*scsiq;
	ADW_CCB		*ccb;
	int		s;


	s = splbio();

	/* Reading the register clears the interrupt. */
	int_stat = ADW_READ_BYTE_REGISTER(iot, ioh, IOPB_INTR_STATUS_REG);

	if ((int_stat & (ADW_INTR_STATUS_INTRA | ADW_INTR_STATUS_INTRB |
	     ADW_INTR_STATUS_INTRC)) == 0) {
		splx(s);
		return ADW_FALSE;
	}

	/*
	 * Notify the driver of an asynchronous microcode condition by
	 * calling the ADW_SOFTC.async_callback function. The function
	 * is passed the microcode ADW_MC_INTRB_CODE byte value.
	 */
	if (int_stat & ADW_INTR_STATUS_INTRB) {
		u_int8_t intrb_code;

		ADW_READ_BYTE_LRAM(iot, ioh, ADW_MC_INTRB_CODE, intrb_code);

		if (sc->chip_type == ADW_CHIP_ASC3550 ||
	    	    sc->chip_type == ADW_CHIP_ASC38C0800) {
			if (intrb_code == ADW_ASYNC_CARRIER_READY_FAILURE &&
				sc->carr_pending_cnt != 0) {
				ADW_WRITE_BYTE_REGISTER(iot, ioh,
					IOPB_TICKLE, ADW_TICKLE_A);
				if (sc->chip_type == ADW_CHIP_ASC3550) {
					ADW_WRITE_BYTE_REGISTER(iot, ioh,
						IOPB_TICKLE, ADW_TICKLE_NOP);
				}
			}
		}

		if (sc->async_callback != 0) {
		    (*(ADW_ASYNC_CALLBACK)sc->async_callback)(sc, intrb_code);
		}
	}

	/*
	 * Check if the IRQ stopper carrier contains a completed request.
	 */
	while (((irq_next_pa = sc->irq_sp->next_ba) & ADW_RQ_DONE) != 0)
	{
#if ADW_DEBUG
		printf("irq 0x%x, 0x%x, 0x%x, 0x%x\n",
				sc->irq_sp->carr_id,
				sc->irq_sp->carr_ba,
				sc->irq_sp->areq_ba,
				sc->irq_sp->next_ba);
#endif
		/*
		 * Get a pointer to the newly completed ADW_SCSI_REQ_Q
		 * structure.
		 * The RISC will have set 'areq_ba' to a virtual address.
		 *
		 * The firmware will have copied the ADW_SCSI_REQ_Q.ccb_ptr
		 * field to the carrier ADW_CARRIER.areq_ba field.
		 * The conversion below complements the conversion of
		 * ADW_SCSI_REQ_Q.ccb_ptr' in AdwExeScsiQueue().
		 */
		ccb = adw_ccb_phys_kv(sc, sc->irq_sp->areq_ba);
		scsiq = &ccb->scsiq;
		scsiq->ccb_ptr = sc->irq_sp->areq_ba;

		/*
		 * Request finished with good status and the queue was not
		 * DMAed to host memory by the firmware. Set all status fields
		 * to indicate good status.
		 */
		if ((irq_next_pa & ADW_RQ_GOOD) != 0) {
			scsiq->done_status = QD_NO_ERROR;
			scsiq->host_status = scsiq->scsi_status = 0;
			scsiq->data_cnt = 0L;
		}

		/*
		 * Advance the stopper pointer to the next carrier
		 * ignoring the lower four bits. Free the previous
		 * stopper carrier.
		 */
		free_carrp = sc->irq_sp;
		sc->irq_sp = ADW_CARRIER_VADDR(sc, ADW_GET_CARRP(irq_next_pa));

		free_carrp->next_ba = (sc->carr_freelist == NULL) ? 0
					: sc->carr_freelist->carr_ba;
		sc->carr_freelist = free_carrp;
		sc->carr_pending_cnt--;

		target_bit = ADW_TID_TO_TIDMASK(scsiq->target_id);

		/*
		 * Clear request microcode control flag.
		 */
		scsiq->cntl = 0;

		/*
		 * Check Condition handling
		 */
		/*
		 * If the command that completed was a SCSI INQUIRY and
		 * LUN 0 was sent the command, then process the INQUIRY
		 * command information for the device.
		 */
		if (scsiq->done_status == QD_NO_ERROR &&
		    scsiq->cdb[0] == INQUIRY &&
		    scsiq->target_lun == 0) {
			AdwInquiryHandling(sc, scsiq);
		}

		/*
		 * Notify the driver of the completed request by passing
		 * the ADW_SCSI_REQ_Q pointer to its callback function.
		 */
		(*(ADW_ISR_CALLBACK)sc->isr_callback)(sc, scsiq);
		/*
		 * Note: After the driver callback function is called, 'scsiq'
		 * can no longer be referenced.
		 *
		 * Fall through and continue processing other completed
		 * requests...
		 */
	}

	splx(s);

	return ADW_TRUE;
}


/*
 * Send an idle command to the chip and wait for completion.
 *
 * Command completion is polled for once per microsecond.
 *
 * The function can be called from anywhere including an interrupt handler.
 * But the function is not re-entrant, so it uses the splbio/splx()
 * functions to prevent reentrancy.
 *
 * Return Values:
 *   ADW_TRUE - command completed successfully
 *   ADW_FALSE - command failed
 *   ADW_ERROR - command timed out
 */
int
AdwSendIdleCmd(ADW_SOFTC *sc, u_int16_t idle_cmd, u_int32_t idle_cmd_parameter)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t	result;
	u_int32_t	i, j, s;

	s = splbio();

	/*
	 * Clear the idle command status which is set by the microcode
	 * to a non-zero value to indicate when the command is completed.
	 */
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_IDLE_CMD_STATUS, (u_int16_t) 0);

	/*
	 * Write the idle command value after the idle command parameter
	 * has been written to avoid a race condition. If the order is not
	 * followed, the microcode may process the idle command before the
	 * parameters have been written to LRAM.
	 */
	ADW_WRITE_DWORD_LRAM(iot, ioh, ADW_MC_IDLE_CMD_PARAMETER,
			idle_cmd_parameter);
	ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_IDLE_CMD, idle_cmd);

	/*
	 * Tickle the RISC to tell it to process the idle command.
	 */
	ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_TICKLE, ADW_TICKLE_B);
	if (sc->chip_type == ADW_CHIP_ASC3550) {
		/*
		 * Clear the tickle value. In the ASC-3550 the RISC flag
		 * command 'clr_tickle_b' does not work unless the host
		 * value is cleared.
		 */
		ADW_WRITE_BYTE_REGISTER(iot, ioh, IOPB_TICKLE, ADW_TICKLE_NOP);
	}

	/* Wait for up to 100 millisecond for the idle command to timeout. */
	for (i = 0; i < SCSI_WAIT_100_MSEC; i++) {
		/* Poll once each microsecond for command completion. */
		for (j = 0; j < SCSI_US_PER_MSEC; j++) {
			ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_IDLE_CMD_STATUS,
									result);
			if (result != 0) {
				splx(s);
				return result;
			}
			AdwDelayMicroSecond(1);
		}
	}

	splx(s);
	return ADW_ERROR;
}


/*
 * Inquiry Information Byte 7 Handling
 *
 * Handle SCSI Inquiry Command information for a device by setting
 * microcode operating variables that affect WDTR, SDTR, and Tag
 * Queuing.
 */
void
AdwInquiryHandling(ADW_SOFTC *sc, ADW_SCSI_REQ_Q *scsiq)
{
#ifndef FAILSAFE
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t		tid;
	ADW_SCSI_INQUIRY	*inq;
	u_int16_t		tidmask;
	u_int16_t		cfg_word;


	/*
	 * AdwInquiryHandling() requires up to INQUIRY information Byte 7
	 * to be available.
	 *
	 * If less than 8 bytes of INQUIRY information were requested or less
	 * than 8 bytes were transferred, then return. cdb[4] is the request
	 * length and the ADW_SCSI_REQ_Q 'data_cnt' field is set by the
	 * microcode to the transfer residual count.
	 */

	if (scsiq->cdb[4] < 8 || (scsiq->cdb[4] - scsiq->data_cnt) < 8) {
		return;
	}

	tid = scsiq->target_id;

	inq = (ADW_SCSI_INQUIRY *) scsiq->vdata_addr;

	/*
	 * WDTR, SDTR, and Tag Queuing cannot be enabled for old devices.
	 */
	if ((inq->rsp_data_fmt < 2) /*SCSI-1 | CCS*/ &&
	    (inq->ansi_apr_ver < 2)) {
		return;
	} else {
		/*
		 * INQUIRY Byte 7 Handling
		 *
		 * Use a device's INQUIRY byte 7 to determine whether it
		 * supports WDTR, SDTR, and Tag Queuing. If the feature
		 * is enabled in the EEPROM and the device supports the
		 * feature, then enable it in the microcode.
		 */

		tidmask = ADW_TID_TO_TIDMASK(tid);

		/*
		 * Wide Transfers
		 *
		 * If the EEPROM enabled WDTR for the device and the device
		 * supports wide bus (16 bit) transfers, then turn on the
		 * device's 'wdtr_able' bit and write the new value to the
		 * microcode.
		 */
#ifdef SCSI_ADW_WDTR_DISABLE
	if(!(tidmask & SCSI_ADW_WDTR_DISABLE))
#endif /* SCSI_ADW_WDTR_DISABLE */
		if ((sc->wdtr_able & tidmask) && inq->WBus16) {
			ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE,
					cfg_word);
			if ((cfg_word & tidmask) == 0) {
				cfg_word |= tidmask;
				ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE,
						cfg_word);

				/*
				 * Clear the microcode "SDTR negotiation" and
				 * "WDTR negotiation" done indicators for the
				 * target to cause it to negotiate with the new
				 * setting set above.
				 * WDTR when accepted causes the target to enter
				 * asynchronous mode, so SDTR must be negotiated
				 */
				ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_SDTR_DONE,
						cfg_word);
				cfg_word &= ~tidmask;
				ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_DONE,
						cfg_word);
				ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_WDTR_DONE,
						cfg_word);
				cfg_word &= ~tidmask;
				ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_WDTR_DONE,
						cfg_word);
			}
		}

		/*
		 * Synchronous Transfers
		 *
		 * If the EEPROM enabled SDTR for the device and the device
		 * supports synchronous transfers, then turn on the device's
		 * 'sdtr_able' bit. Write the new value to the microcode.
		 */
#ifdef SCSI_ADW_SDTR_DISABLE
	if(!(tidmask & SCSI_ADW_SDTR_DISABLE))
#endif /* SCSI_ADW_SDTR_DISABLE */
		if ((sc->sdtr_able & tidmask) && inq->Sync) {
			ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_SDTR_ABLE,cfg_word);
			if ((cfg_word & tidmask) == 0) {
				cfg_word |= tidmask;
				ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_ABLE,
						cfg_word);

				/*
				 * Clear the microcode "SDTR negotiation"
				 * done indicator for the target to cause it
				 * to negotiate with the new setting set above.
				 */
				ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_SDTR_DONE,
						cfg_word);
				cfg_word &= ~tidmask;
				ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_SDTR_DONE,
						cfg_word);
			}
		}
		/*
		 * If the Inquiry data included enough space for the SPI-3
		 * Clocking field, then check if DT mode is supported.
		 */
		if (sc->chip_type == ADW_CHIP_ASC38C1600 &&
		   (scsiq->cdb[4] >= 57 ||
		   (scsiq->cdb[4] - scsiq->data_cnt) >= 57)) {
			/*
			 * PPR (Parallel Protocol Request) Capable
			 *
			 * If the device supports DT mode, then it must be
			 * PPR capable.
			 * The PPR message will be used in place of the SDTR
			 * and WDTR messages to negotiate synchronous speed
			 * and offset, transfer width, and protocol options.
			 */
			if((inq->Clocking) & INQ_CLOCKING_DT_ONLY){
				ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_PPR_ABLE,
						sc->ppr_able);
				sc->ppr_able |= tidmask;
				ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_PPR_ABLE,
						sc->ppr_able);
			}
		}

		/*
		 * If the EEPROM enabled Tag Queuing for the device and the
		 * device supports Tag Queueing, then turn on the device's
		 * 'tagqng_enable' bit in the microcode and set the microcode
		 * maximum command count to the ADW_SOFTC 'max_dvc_qng'
		 * value.
		 *
		 * Tag Queuing is disabled for the BIOS which runs in polled
		 * mode and would see no benefit from Tag Queuing. Also by
		 * disabling Tag Queuing in the BIOS devices with Tag Queuing
		 * bugs will at least work with the BIOS.
		 */
#ifdef SCSI_ADW_TAGQ_DISABLE
	if(!(tidmask & SCSI_ADW_TAGQ_DISABLE))
#endif /* SCSI_ADW_TAGQ_DISABLE */
		if ((sc->tagqng_able & tidmask) && inq->CmdQue) {
			ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_TAGQNG_ABLE,
					cfg_word);
			cfg_word |= tidmask;
			ADW_WRITE_WORD_LRAM(iot, ioh, ADW_MC_TAGQNG_ABLE,
					cfg_word);

			ADW_WRITE_BYTE_LRAM(iot, ioh,
					ADW_MC_NUMBER_OF_MAX_CMD + tid,
					sc->max_dvc_qng);
		}
	}
#endif /* FAILSAFE */
}


void
AdwSleepMilliSecond(u_int32_t n)
{

	DELAY(n * 1000);
}


void
AdwDelayMicroSecond(u_int32_t n)
{

	DELAY(n);
}

