// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Creative Labs, Inc.
 *  Routines for control of EMU10K1 chips
 *
 *  Copyright (c) by James Courtier-Dutton <James@superbug.co.uk>
 *      Added support for Audigy 2 Value.
 *  	Added EMU 1010 support.
 *  	General bug fixes and enhancements.
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 */

#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>


#include <sound/core.h>
#include <sound/emu10k1.h>
#include <linux/firmware.h>
#include "p16v.h"
#include "tina2.h"
#include "p17v.h"


#define HANA_FILENAME "emu/hana.fw"
#define DOCK_FILENAME "emu/audio_dock.fw"
#define EMU1010B_FILENAME "emu/emu1010b.fw"
#define MICRO_DOCK_FILENAME "emu/micro_dock.fw"
#define EMU0404_FILENAME "emu/emu0404.fw"
#define EMU1010_NOTEBOOK_FILENAME "emu/emu1010_notebook.fw"

MODULE_FIRMWARE(HANA_FILENAME);
MODULE_FIRMWARE(DOCK_FILENAME);
MODULE_FIRMWARE(EMU1010B_FILENAME);
MODULE_FIRMWARE(MICRO_DOCK_FILENAME);
MODULE_FIRMWARE(EMU0404_FILENAME);
MODULE_FIRMWARE(EMU1010_NOTEBOOK_FILENAME);


/*************************************************************************
 * EMU10K1 init / done
 *************************************************************************/

void snd_emu10k1_voice_init(struct snd_emu10k1 *emu, int ch)
{
	snd_emu10k1_ptr_write(emu, DCYSUSV, ch, 0);
	snd_emu10k1_ptr_write(emu, IP, ch, 0);
	snd_emu10k1_ptr_write(emu, VTFT, ch, VTFT_FILTERTARGET_MASK);
	snd_emu10k1_ptr_write(emu, CVCF, ch, CVCF_CURRENTFILTER_MASK);
	snd_emu10k1_ptr_write(emu, PTRX, ch, 0);
	snd_emu10k1_ptr_write(emu, CPF, ch, 0);
	snd_emu10k1_ptr_write(emu, CCR, ch, 0);

	snd_emu10k1_ptr_write(emu, PSST, ch, 0);
	snd_emu10k1_ptr_write(emu, DSL, ch, 0x10);
	snd_emu10k1_ptr_write(emu, CCCA, ch, 0);
	snd_emu10k1_ptr_write(emu, Z1, ch, 0);
	snd_emu10k1_ptr_write(emu, Z2, ch, 0);
	snd_emu10k1_ptr_write(emu, FXRT, ch, 0x32100000);

	snd_emu10k1_ptr_write(emu, ATKHLDM, ch, 0);
	snd_emu10k1_ptr_write(emu, DCYSUSM, ch, 0);
	snd_emu10k1_ptr_write(emu, IFATN, ch, IFATN_FILTERCUTOFF_MASK | IFATN_ATTENUATION_MASK);
	snd_emu10k1_ptr_write(emu, PEFE, ch, 0);
	snd_emu10k1_ptr_write(emu, FMMOD, ch, 0);
	snd_emu10k1_ptr_write(emu, TREMFRQ, ch, 24);	/* 1 Hz */
	snd_emu10k1_ptr_write(emu, FM2FRQ2, ch, 24);	/* 1 Hz */
	snd_emu10k1_ptr_write(emu, TEMPENV, ch, 0);

	/*** these are last so OFF prevents writing ***/
	snd_emu10k1_ptr_write(emu, LFOVAL2, ch, 0);
	snd_emu10k1_ptr_write(emu, LFOVAL1, ch, 0);
	snd_emu10k1_ptr_write(emu, ATKHLDV, ch, 0);
	snd_emu10k1_ptr_write(emu, ENVVOL, ch, 0);
	snd_emu10k1_ptr_write(emu, ENVVAL, ch, 0);

	/* Audigy extra stuffs */
	if (emu->audigy) {
		snd_emu10k1_ptr_write(emu, A_CSBA, ch, 0);
		snd_emu10k1_ptr_write(emu, A_CSDC, ch, 0);
		snd_emu10k1_ptr_write(emu, A_CSFE, ch, 0);
		snd_emu10k1_ptr_write(emu, A_CSHG, ch, 0);
		snd_emu10k1_ptr_write(emu, A_FXRT1, ch, 0x03020100);
		snd_emu10k1_ptr_write(emu, A_FXRT2, ch, 0x3f3f3f3f);
		snd_emu10k1_ptr_write(emu, A_SENDAMOUNTS, ch, 0);
	}
}

static const unsigned int spi_dac_init[] = {
		0x00ff,
		0x02ff,
		0x0400,
		0x0520,
		0x0600,
		0x08ff,
		0x0aff,
		0x0cff,
		0x0eff,
		0x10ff,
		0x1200,
		0x1400,
		0x1480,
		0x1800,
		0x1aff,
		0x1cff,
		0x1e00,
		0x0530,
		0x0602,
		0x0622,
		0x1400,
};

static const unsigned int i2c_adc_init[][2] = {
	{ 0x17, 0x00 }, /* Reset */
	{ 0x07, 0x00 }, /* Timeout */
	{ 0x0b, 0x22 },  /* Interface control */
	{ 0x0c, 0x22 },  /* Master mode control */
	{ 0x0d, 0x08 },  /* Powerdown control */
	{ 0x0e, 0xcf },  /* Attenuation Left  0x01 = -103dB, 0xff = 24dB */
	{ 0x0f, 0xcf },  /* Attenuation Right 0.5dB steps */
	{ 0x10, 0x7b },  /* ALC Control 1 */
	{ 0x11, 0x00 },  /* ALC Control 2 */
	{ 0x12, 0x32 },  /* ALC Control 3 */
	{ 0x13, 0x00 },  /* Noise gate control */
	{ 0x14, 0xa6 },  /* Limiter control */
	{ 0x15, ADC_MUX_2 },  /* ADC Mixer control. Mic for A2ZS Notebook */
};

static int snd_emu10k1_init(struct snd_emu10k1 *emu, int enable_ir)
{
	unsigned int silent_page;
	int ch;
	u32 tmp;

	/* disable audio and lock cache */
	outl(HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE_MASK |
		HCFG_MUTEBUTTONENABLE, emu->port + HCFG);

	/* reset recording buffers */
	snd_emu10k1_ptr_write(emu, MICBS, 0, ADCBS_BUFSIZE_NONE);
	snd_emu10k1_ptr_write(emu, MICBA, 0, 0);
	snd_emu10k1_ptr_write(emu, FXBS, 0, ADCBS_BUFSIZE_NONE);
	snd_emu10k1_ptr_write(emu, FXBA, 0, 0);
	snd_emu10k1_ptr_write(emu, ADCBS, 0, ADCBS_BUFSIZE_NONE);
	snd_emu10k1_ptr_write(emu, ADCBA, 0, 0);

	/* disable channel interrupt */
	outl(0, emu->port + INTE);
	snd_emu10k1_ptr_write(emu, CLIEL, 0, 0);
	snd_emu10k1_ptr_write(emu, CLIEH, 0, 0);

	/* disable stop on loop end */
	snd_emu10k1_ptr_write(emu, SOLEL, 0, 0);
	snd_emu10k1_ptr_write(emu, SOLEH, 0, 0);

	if (emu->audigy) {
		/* set SPDIF bypass mode */
		snd_emu10k1_ptr_write(emu, SPBYPASS, 0, SPBYPASS_FORMAT);
		/* enable rear left + rear right AC97 slots */
		snd_emu10k1_ptr_write(emu, AC97SLOT, 0, AC97SLOT_REAR_RIGHT |
				      AC97SLOT_REAR_LEFT);
	}

	/* init envelope engine */
	for (ch = 0; ch < NUM_G; ch++)
		snd_emu10k1_voice_init(emu, ch);

	snd_emu10k1_ptr_write(emu, SPCS0, 0, emu->spdif_bits[0]);
	snd_emu10k1_ptr_write(emu, SPCS1, 0, emu->spdif_bits[1]);
	snd_emu10k1_ptr_write(emu, SPCS2, 0, emu->spdif_bits[2]);

	if (emu->card_capabilities->emu_model) {
	} else if (emu->card_capabilities->ca0151_chip) { /* audigy2 */
		/* Hacks for Alice3 to work independent of haP16V driver */
		/* Setup SRCMulti_I2S SamplingRate */
		snd_emu10k1_ptr_write(emu, A_I2S_CAPTURE_RATE, 0, A_I2S_CAPTURE_96000);

		/* Setup SRCSel (Enable Spdif,I2S SRCMulti) */
		snd_emu10k1_ptr20_write(emu, SRCSel, 0, 0x14);
		/* Setup SRCMulti Input Audio Enable */
		/* Use 0xFFFFFFFF to enable P16V sounds. */
		snd_emu10k1_ptr20_write(emu, SRCMULTI_ENABLE, 0, 0xFFFFFFFF);

		/* Enabled Phased (8-channel) P16V playback */
		outl(0x0201, emu->port + HCFG2);
		/* Set playback routing. */
		snd_emu10k1_ptr20_write(emu, CAPTURE_P16V_SOURCE, 0, 0x78e4);
	} else if (emu->card_capabilities->ca0108_chip) { /* audigy2 Value */
		/* Hacks for Alice3 to work independent of haP16V driver */
		dev_info(emu->card->dev, "Audigy2 value: Special config.\n");
		/* Setup SRCMulti_I2S SamplingRate */
		snd_emu10k1_ptr_write(emu, A_I2S_CAPTURE_RATE, 0, A_I2S_CAPTURE_96000);

		/* Setup SRCSel (Enable Spdif,I2S SRCMulti) */
		snd_emu10k1_ptr20_write(emu, P17V_SRCSel, 0, 0x14);

		/* Setup SRCMulti Input Audio Enable */
		snd_emu10k1_ptr20_write(emu, P17V_MIXER_I2S_ENABLE, 0, 0xFF000000);

		/* Setup SPDIF Out Audio Enable */
		/* The Audigy 2 Value has a separate SPDIF out,
		 * so no need for a mixer switch
		 */
		snd_emu10k1_ptr20_write(emu, P17V_MIXER_SPDIF_ENABLE, 0, 0xFF000000);

		tmp = inw(emu->port + A_IOCFG) & ~0x8; /* Clear bit 3 */
		outw(tmp, emu->port + A_IOCFG);
	}
	if (emu->card_capabilities->spi_dac) { /* Audigy 2 ZS Notebook with DAC Wolfson WM8768/WM8568 */
		int size, n;

		size = ARRAY_SIZE(spi_dac_init);
		for (n = 0; n < size; n++)
			snd_emu10k1_spi_write(emu, spi_dac_init[n]);

		snd_emu10k1_ptr20_write(emu, 0x60, 0, 0x10);
		/* Enable GPIOs
		 * GPIO0: Unknown
		 * GPIO1: Speakers-enabled.
		 * GPIO2: Unknown
		 * GPIO3: Unknown
		 * GPIO4: IEC958 Output on.
		 * GPIO5: Unknown
		 * GPIO6: Unknown
		 * GPIO7: Unknown
		 */
		outw(0x76, emu->port + A_IOCFG); /* Windows uses 0x3f76 */
	}
	if (emu->card_capabilities->i2c_adc) { /* Audigy 2 ZS Notebook with ADC Wolfson WM8775 */
		int size, n;

		snd_emu10k1_ptr20_write(emu, P17V_I2S_SRC_SEL, 0, 0x2020205f);
		tmp = inw(emu->port + A_IOCFG);
		outw(tmp | 0x4, emu->port + A_IOCFG);  /* Set bit 2 for mic input */
		tmp = inw(emu->port + A_IOCFG);
		size = ARRAY_SIZE(i2c_adc_init);
		for (n = 0; n < size; n++)
			snd_emu10k1_i2c_write(emu, i2c_adc_init[n][0], i2c_adc_init[n][1]);
		for (n = 0; n < 4; n++) {
			emu->i2c_capture_volume[n][0] = 0xcf;
			emu->i2c_capture_volume[n][1] = 0xcf;
		}
	}


	snd_emu10k1_ptr_write(emu, PTB, 0, emu->ptb_pages.addr);
	snd_emu10k1_ptr_write(emu, TCB, 0, 0);	/* taken from original driver */
	snd_emu10k1_ptr_write(emu, TCBS, 0, TCBS_BUFFSIZE_256K);	/* taken from original driver */

	silent_page = (emu->silent_page.addr << emu->address_mode) | (emu->address_mode ? MAP_PTI_MASK1 : MAP_PTI_MASK0);
	for (ch = 0; ch < NUM_G; ch++) {
		snd_emu10k1_ptr_write(emu, MAPA, ch, silent_page);
		snd_emu10k1_ptr_write(emu, MAPB, ch, silent_page);
	}

	if (emu->card_capabilities->emu_model) {
		outl(HCFG_AUTOMUTE_ASYNC |
			HCFG_EMU32_SLAVE |
			HCFG_AUDIOENABLE, emu->port + HCFG);
	/*
	 *  Hokay, setup HCFG
	 *   Mute Disable Audio = 0
	 *   Lock Tank Memory = 1
	 *   Lock Sound Memory = 0
	 *   Auto Mute = 1
	 */
	} else if (emu->audigy) {
		if (emu->revision == 4) /* audigy2 */
			outl(HCFG_AUDIOENABLE |
			     HCFG_AC3ENABLE_CDSPDIF |
			     HCFG_AC3ENABLE_GPSPDIF |
			     HCFG_AUTOMUTE | HCFG_JOYENABLE, emu->port + HCFG);
		else
			outl(HCFG_AUTOMUTE | HCFG_JOYENABLE, emu->port + HCFG);
	/* FIXME: Remove all these emu->model and replace it with a card recognition parameter,
	 * e.g. card_capabilities->joystick */
	} else if (emu->model == 0x20 ||
	    emu->model == 0xc400 ||
	    (emu->model == 0x21 && emu->revision < 6))
		outl(HCFG_LOCKTANKCACHE_MASK | HCFG_AUTOMUTE, emu->port + HCFG);
	else
		/* With on-chip joystick */
		outl(HCFG_LOCKTANKCACHE_MASK | HCFG_AUTOMUTE | HCFG_JOYENABLE, emu->port + HCFG);

	if (enable_ir) {	/* enable IR for SB Live */
		if (emu->card_capabilities->emu_model) {
			;  /* Disable all access to A_IOCFG for the emu1010 */
		} else if (emu->card_capabilities->i2c_adc) {
			;  /* Disable A_IOCFG for Audigy 2 ZS Notebook */
		} else if (emu->audigy) {
			u16 reg = inw(emu->port + A_IOCFG);
			outw(reg | A_IOCFG_GPOUT2, emu->port + A_IOCFG);
			udelay(500);
			outw(reg | A_IOCFG_GPOUT1 | A_IOCFG_GPOUT2, emu->port + A_IOCFG);
			udelay(100);
			outw(reg, emu->port + A_IOCFG);
		} else {
			unsigned int reg = inl(emu->port + HCFG);
			outl(reg | HCFG_GPOUT2, emu->port + HCFG);
			udelay(500);
			outl(reg | HCFG_GPOUT1 | HCFG_GPOUT2, emu->port + HCFG);
			udelay(100);
			outl(reg, emu->port + HCFG);
		}
	}

	if (emu->card_capabilities->emu_model) {
		;  /* Disable all access to A_IOCFG for the emu1010 */
	} else if (emu->card_capabilities->i2c_adc) {
		;  /* Disable A_IOCFG for Audigy 2 ZS Notebook */
	} else if (emu->audigy) {	/* enable analog output */
		u16 reg = inw(emu->port + A_IOCFG);
		outw(reg | A_IOCFG_GPOUT0, emu->port + A_IOCFG);
	}

	if (emu->address_mode == 0) {
		/* use 16M in 4G */
		outl(inl(emu->port + HCFG) | HCFG_EXPANDED_MEM, emu->port + HCFG);
	}

	return 0;
}

static void snd_emu10k1_audio_enable(struct snd_emu10k1 *emu)
{
	/*
	 *  Enable the audio bit
	 */
	outl(inl(emu->port + HCFG) | HCFG_AUDIOENABLE, emu->port + HCFG);

	/* Enable analog/digital outs on audigy */
	if (emu->card_capabilities->emu_model) {
		;  /* Disable all access to A_IOCFG for the emu1010 */
	} else if (emu->card_capabilities->i2c_adc) {
		;  /* Disable A_IOCFG for Audigy 2 ZS Notebook */
	} else if (emu->audigy) {
		outw(inw(emu->port + A_IOCFG) & ~0x44, emu->port + A_IOCFG);

		if (emu->card_capabilities->ca0151_chip) { /* audigy2 */
			/* Unmute Analog now.  Set GPO6 to 1 for Apollo.
			 * This has to be done after init ALice3 I2SOut beyond 48KHz.
			 * So, sequence is important. */
			outw(inw(emu->port + A_IOCFG) | 0x0040, emu->port + A_IOCFG);
		} else if (emu->card_capabilities->ca0108_chip) { /* audigy2 value */
			/* Unmute Analog now. */
			outw(inw(emu->port + A_IOCFG) | 0x0060, emu->port + A_IOCFG);
		} else {
			/* Disable routing from AC97 line out to Front speakers */
			outw(inw(emu->port + A_IOCFG) | 0x0080, emu->port + A_IOCFG);
		}
	}

#if 0
	{
	unsigned int tmp;
	/* FIXME: the following routine disables LiveDrive-II !! */
	/* TOSLink detection */
	emu->tos_link = 0;
	tmp = inl(emu->port + HCFG);
	if (tmp & (HCFG_GPINPUT0 | HCFG_GPINPUT1)) {
		outl(tmp|0x800, emu->port + HCFG);
		udelay(50);
		if (tmp != (inl(emu->port + HCFG) & ~0x800)) {
			emu->tos_link = 1;
			outl(tmp, emu->port + HCFG);
		}
	}
	}
#endif

	snd_emu10k1_intr_enable(emu, INTE_PCIERRORENABLE);
}

int snd_emu10k1_done(struct snd_emu10k1 *emu)
{
	int ch;

	outl(0, emu->port + INTE);

	/*
	 *  Shutdown the chip
	 */
	for (ch = 0; ch < NUM_G; ch++)
		snd_emu10k1_ptr_write(emu, DCYSUSV, ch, 0);
	for (ch = 0; ch < NUM_G; ch++) {
		snd_emu10k1_ptr_write(emu, VTFT, ch, 0);
		snd_emu10k1_ptr_write(emu, CVCF, ch, 0);
		snd_emu10k1_ptr_write(emu, PTRX, ch, 0);
		snd_emu10k1_ptr_write(emu, CPF, ch, 0);
	}

	/* reset recording buffers */
	snd_emu10k1_ptr_write(emu, MICBS, 0, 0);
	snd_emu10k1_ptr_write(emu, MICBA, 0, 0);
	snd_emu10k1_ptr_write(emu, FXBS, 0, 0);
	snd_emu10k1_ptr_write(emu, FXBA, 0, 0);
	snd_emu10k1_ptr_write(emu, FXWC, 0, 0);
	snd_emu10k1_ptr_write(emu, ADCBS, 0, ADCBS_BUFSIZE_NONE);
	snd_emu10k1_ptr_write(emu, ADCBA, 0, 0);
	snd_emu10k1_ptr_write(emu, TCBS, 0, TCBS_BUFFSIZE_16K);
	snd_emu10k1_ptr_write(emu, TCB, 0, 0);
	if (emu->audigy)
		snd_emu10k1_ptr_write(emu, A_DBG, 0, A_DBG_SINGLE_STEP);
	else
		snd_emu10k1_ptr_write(emu, DBG, 0, EMU10K1_DBG_SINGLE_STEP);

	/* disable channel interrupt */
	snd_emu10k1_ptr_write(emu, CLIEL, 0, 0);
	snd_emu10k1_ptr_write(emu, CLIEH, 0, 0);
	snd_emu10k1_ptr_write(emu, SOLEL, 0, 0);
	snd_emu10k1_ptr_write(emu, SOLEH, 0, 0);

	/* disable audio and lock cache */
	outl(HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE_MASK | HCFG_MUTEBUTTONENABLE, emu->port + HCFG);
	snd_emu10k1_ptr_write(emu, PTB, 0, 0);

	return 0;
}

/*************************************************************************
 * ECARD functional implementation
 *************************************************************************/

/* In A1 Silicon, these bits are in the HC register */
#define HOOKN_BIT		(1L << 12)
#define HANDN_BIT		(1L << 11)
#define PULSEN_BIT		(1L << 10)

#define EC_GDI1			(1 << 13)
#define EC_GDI0			(1 << 14)

#define EC_NUM_CONTROL_BITS	20

#define EC_AC3_DATA_SELN	0x0001L
#define EC_EE_DATA_SEL		0x0002L
#define EC_EE_CNTRL_SELN	0x0004L
#define EC_EECLK		0x0008L
#define EC_EECS			0x0010L
#define EC_EESDO		0x0020L
#define EC_TRIM_CSN		0x0040L
#define EC_TRIM_SCLK		0x0080L
#define EC_TRIM_SDATA		0x0100L
#define EC_TRIM_MUTEN		0x0200L
#define EC_ADCCAL		0x0400L
#define EC_ADCRSTN		0x0800L
#define EC_DACCAL		0x1000L
#define EC_DACMUTEN		0x2000L
#define EC_LEDN			0x4000L

#define EC_SPDIF0_SEL_SHIFT	15
#define EC_SPDIF1_SEL_SHIFT	17
#define EC_SPDIF0_SEL_MASK	(0x3L << EC_SPDIF0_SEL_SHIFT)
#define EC_SPDIF1_SEL_MASK	(0x7L << EC_SPDIF1_SEL_SHIFT)
#define EC_SPDIF0_SELECT(_x)	(((_x) << EC_SPDIF0_SEL_SHIFT) & EC_SPDIF0_SEL_MASK)
#define EC_SPDIF1_SELECT(_x)	(((_x) << EC_SPDIF1_SEL_SHIFT) & EC_SPDIF1_SEL_MASK)
#define EC_CURRENT_PROM_VERSION 0x01	/* Self-explanatory.  This should
					 * be incremented any time the EEPROM's
					 * format is changed.  */

#define EC_EEPROM_SIZE		0x40	/* ECARD EEPROM has 64 16-bit words */

/* Addresses for special values stored in to EEPROM */
#define EC_PROM_VERSION_ADDR	0x20	/* Address of the current prom version */
#define EC_BOARDREV0_ADDR	0x21	/* LSW of board rev */
#define EC_BOARDREV1_ADDR	0x22	/* MSW of board rev */

#define EC_LAST_PROMFILE_ADDR	0x2f

#define EC_SERIALNUM_ADDR	0x30	/* First word of serial number.  The
					 * can be up to 30 characters in length
					 * and is stored as a NULL-terminated
					 * ASCII string.  Any unused bytes must be
					 * filled with zeros */
#define EC_CHECKSUM_ADDR	0x3f	/* Location at which checksum is stored */


/* Most of this stuff is pretty self-evident.  According to the hardware
 * dudes, we need to leave the ADCCAL bit low in order to avoid a DC
 * offset problem.  Weird.
 */
#define EC_RAW_RUN_MODE		(EC_DACMUTEN | EC_ADCRSTN | EC_TRIM_MUTEN | \
				 EC_TRIM_CSN)


#define EC_DEFAULT_ADC_GAIN	0xC4C4
#define EC_DEFAULT_SPDIF0_SEL	0x0
#define EC_DEFAULT_SPDIF1_SEL	0x4

/**************************************************************************
 * @func Clock bits into the Ecard's control latch.  The Ecard uses a
 *  control latch will is loaded bit-serially by toggling the Modem control
 *  lines from function 2 on the E8010.  This function hides these details
 *  and presents the illusion that we are actually writing to a distinct
 *  register.
 */

static void snd_emu10k1_ecard_write(struct snd_emu10k1 *emu, unsigned int value)
{
	unsigned short count;
	unsigned int data;
	unsigned long hc_port;
	unsigned int hc_value;

	hc_port = emu->port + HCFG;
	hc_value = inl(hc_port) & ~(HOOKN_BIT | HANDN_BIT | PULSEN_BIT);
	outl(hc_value, hc_port);

	for (count = 0; count < EC_NUM_CONTROL_BITS; count++) {

		/* Set up the value */
		data = ((value & 0x1) ? PULSEN_BIT : 0);
		value >>= 1;

		outl(hc_value | data, hc_port);

		/* Clock the shift register */
		outl(hc_value | data | HANDN_BIT, hc_port);
		outl(hc_value | data, hc_port);
	}

	/* Latch the bits */
	outl(hc_value | HOOKN_BIT, hc_port);
	outl(hc_value, hc_port);
}

/**************************************************************************
 * @func Set the gain of the ECARD's CS3310 Trim/gain controller.  The
 * trim value consists of a 16bit value which is composed of two
 * 8 bit gain/trim values, one for the left channel and one for the
 * right channel.  The following table maps from the Gain/Attenuation
 * value in decibels into the corresponding bit pattern for a single
 * channel.
 */

static void snd_emu10k1_ecard_setadcgain(struct snd_emu10k1 *emu,
					 unsigned short gain)
{
	unsigned int bit;

	/* Enable writing to the TRIM registers */
	snd_emu10k1_ecard_write(emu, emu->ecard_ctrl & ~EC_TRIM_CSN);

	/* Do it again to insure that we meet hold time requirements */
	snd_emu10k1_ecard_write(emu, emu->ecard_ctrl & ~EC_TRIM_CSN);

	for (bit = (1 << 15); bit; bit >>= 1) {
		unsigned int value;

		value = emu->ecard_ctrl & ~(EC_TRIM_CSN | EC_TRIM_SDATA);

		if (gain & bit)
			value |= EC_TRIM_SDATA;

		/* Clock the bit */
		snd_emu10k1_ecard_write(emu, value);
		snd_emu10k1_ecard_write(emu, value | EC_TRIM_SCLK);
		snd_emu10k1_ecard_write(emu, value);
	}

	snd_emu10k1_ecard_write(emu, emu->ecard_ctrl);
}

static int snd_emu10k1_ecard_init(struct snd_emu10k1 *emu)
{
	unsigned int hc_value;

	/* Set up the initial settings */
	emu->ecard_ctrl = EC_RAW_RUN_MODE |
			  EC_SPDIF0_SELECT(EC_DEFAULT_SPDIF0_SEL) |
			  EC_SPDIF1_SELECT(EC_DEFAULT_SPDIF1_SEL);

	/* Step 0: Set the codec type in the hardware control register
	 * and enable audio output */
	hc_value = inl(emu->port + HCFG);
	outl(hc_value | HCFG_AUDIOENABLE | HCFG_CODECFORMAT_I2S, emu->port + HCFG);
	inl(emu->port + HCFG);

	/* Step 1: Turn off the led and deassert TRIM_CS */
	snd_emu10k1_ecard_write(emu, EC_ADCCAL | EC_LEDN | EC_TRIM_CSN);

	/* Step 2: Calibrate the ADC and DAC */
	snd_emu10k1_ecard_write(emu, EC_DACCAL | EC_LEDN | EC_TRIM_CSN);

	/* Step 3: Wait for awhile;   XXX We can't get away with this
	 * under a real operating system; we'll need to block and wait that
	 * way. */
	snd_emu10k1_wait(emu, 48000);

	/* Step 4: Switch off the DAC and ADC calibration.  Note
	 * That ADC_CAL is actually an inverted signal, so we assert
	 * it here to stop calibration.  */
	snd_emu10k1_ecard_write(emu, EC_ADCCAL | EC_LEDN | EC_TRIM_CSN);

	/* Step 4: Switch into run mode */
	snd_emu10k1_ecard_write(emu, emu->ecard_ctrl);

	/* Step 5: Set the analog input gain */
	snd_emu10k1_ecard_setadcgain(emu, EC_DEFAULT_ADC_GAIN);

	return 0;
}

static int snd_emu10k1_cardbus_init(struct snd_emu10k1 *emu)
{
	unsigned long special_port;
	__always_unused unsigned int value;

	/* Special initialisation routine
	 * before the rest of the IO-Ports become active.
	 */
	special_port = emu->port + 0x38;
	value = inl(special_port);
	outl(0x00d00000, special_port);
	value = inl(special_port);
	outl(0x00d00001, special_port);
	value = inl(special_port);
	outl(0x00d0005f, special_port);
	value = inl(special_port);
	outl(0x00d0007f, special_port);
	value = inl(special_port);
	outl(0x0090007f, special_port);
	value = inl(special_port);

	snd_emu10k1_ptr20_write(emu, TINA2_VOLUME, 0, 0xfefefefe); /* Defaults to 0x30303030 */
	/* Delay to give time for ADC chip to switch on. It needs 113ms */
	msleep(200);
	return 0;
}

static int snd_emu1010_load_firmware_entry(struct snd_emu10k1 *emu,
				     const struct firmware *fw_entry)
{
	int n, i;
	u16 reg;
	u8 value;
	__always_unused u16 write_post;
	unsigned long flags;

	if (!fw_entry)
		return -EIO;

	/* The FPGA is a Xilinx Spartan IIE XC2S50E */
	/* On E-MU 0404b it is a Xilinx Spartan III XC3S50 */
	/* GPIO7 -> FPGA PGMN
	 * GPIO6 -> FPGA CCLK
	 * GPIO5 -> FPGA DIN
	 * FPGA CONFIG OFF -> FPGA PGMN
	 */
	spin_lock_irqsave(&emu->emu_lock, flags);
	outw(0x00, emu->port + A_GPIO); /* Set PGMN low for 100uS. */
	write_post = inw(emu->port + A_GPIO);
	udelay(100);
	outw(0x80, emu->port + A_GPIO); /* Leave bit 7 set during netlist setup. */
	write_post = inw(emu->port + A_GPIO);
	udelay(100); /* Allow FPGA memory to clean */
	for (n = 0; n < fw_entry->size; n++) {
		value = fw_entry->data[n];
		for (i = 0; i < 8; i++) {
			reg = 0x80;
			if (value & 0x1)
				reg = reg | 0x20;
			value = value >> 1;
			outw(reg, emu->port + A_GPIO);
			write_post = inw(emu->port + A_GPIO);
			outw(reg | 0x40, emu->port + A_GPIO);
			write_post = inw(emu->port + A_GPIO);
		}
	}
	/* After programming, set GPIO bit 4 high again. */
	outw(0x10, emu->port + A_GPIO);
	write_post = inw(emu->port + A_GPIO);
	spin_unlock_irqrestore(&emu->emu_lock, flags);

	return 0;
}

/* firmware file names, per model, init-fw and dock-fw (optional) */
static const char * const firmware_names[5][2] = {
	[EMU_MODEL_EMU1010] = {
		HANA_FILENAME, DOCK_FILENAME
	},
	[EMU_MODEL_EMU1010B] = {
		EMU1010B_FILENAME, MICRO_DOCK_FILENAME
	},
	[EMU_MODEL_EMU1616] = {
		EMU1010_NOTEBOOK_FILENAME, MICRO_DOCK_FILENAME
	},
	[EMU_MODEL_EMU0404] = {
		EMU0404_FILENAME, NULL
	},
};

static int snd_emu1010_load_firmware(struct snd_emu10k1 *emu, int dock,
				     const struct firmware **fw)
{
	const char *filename;
	int err;

	if (!*fw) {
		filename = firmware_names[emu->card_capabilities->emu_model][dock];
		if (!filename)
			return 0;
		err = request_firmware(fw, filename, &emu->pci->dev);
		if (err)
			return err;
	}

	return snd_emu1010_load_firmware_entry(emu, *fw);
}

static void emu1010_firmware_work(struct work_struct *work)
{
	struct snd_emu10k1 *emu;
	u32 tmp, tmp2, reg;
	int err;

	emu = container_of(work, struct snd_emu10k1,
			   emu1010.firmware_work.work);
	if (emu->card->shutdown)
		return;
#ifdef CONFIG_PM_SLEEP
	if (emu->suspend)
		return;
#endif
	snd_emu1010_fpga_read(emu, EMU_HANA_IRQ_STATUS, &tmp); /* IRQ Status */
	snd_emu1010_fpga_read(emu, EMU_HANA_OPTION_CARDS, &reg); /* OPTIONS: Which cards are attached to the EMU */
	if (reg & EMU_HANA_OPTION_DOCK_OFFLINE) {
		/* Audio Dock attached */
		/* Return to Audio Dock programming mode */
		dev_info(emu->card->dev,
			 "emu1010: Loading Audio Dock Firmware\n");
		snd_emu1010_fpga_write(emu, EMU_HANA_FPGA_CONFIG,
				       EMU_HANA_FPGA_CONFIG_AUDIODOCK);
		err = snd_emu1010_load_firmware(emu, 1, &emu->dock_fw);
		if (err < 0)
			goto next;

		snd_emu1010_fpga_write(emu, EMU_HANA_FPGA_CONFIG, 0);
		snd_emu1010_fpga_read(emu, EMU_HANA_IRQ_STATUS, &tmp);
		dev_info(emu->card->dev,
			 "emu1010: EMU_HANA+DOCK_IRQ_STATUS = 0x%x\n", tmp);
		/* ID, should read & 0x7f = 0x55 when FPGA programmed. */
		snd_emu1010_fpga_read(emu, EMU_HANA_ID, &tmp);
		dev_info(emu->card->dev,
			 "emu1010: EMU_HANA+DOCK_ID = 0x%x\n", tmp);
		if ((tmp & 0x1f) != 0x15) {
			/* FPGA failed to be programmed */
			dev_info(emu->card->dev,
				 "emu1010: Loading Audio Dock Firmware file failed, reg = 0x%x\n",
				 tmp);
			goto next;
		}
		dev_info(emu->card->dev,
			 "emu1010: Audio Dock Firmware loaded\n");
		snd_emu1010_fpga_read(emu, EMU_DOCK_MAJOR_REV, &tmp);
		snd_emu1010_fpga_read(emu, EMU_DOCK_MINOR_REV, &tmp2);
		dev_info(emu->card->dev, "Audio Dock ver: %u.%u\n", tmp, tmp2);
		/* Sync clocking between 1010 and Dock */
		/* Allow DLL to settle */
		msleep(10);
		/* Unmute all. Default is muted after a firmware load */
		snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_UNMUTE);
	} else if (!reg && emu->emu1010.last_reg) {
		/* Audio Dock removed */
		dev_info(emu->card->dev, "emu1010: Audio Dock detached\n");
		/* The hardware auto-mutes all, so we unmute again */
		snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_UNMUTE);
	}

 next:
	emu->emu1010.last_reg = reg;
	if (!emu->card->shutdown)
		schedule_delayed_work(&emu->emu1010.firmware_work,
				      msecs_to_jiffies(1000));
}

/*
 * Current status of the driver:
 * ----------------------------
 * 	* only 44.1/48kHz supported (the MS Win driver supports up to 192 kHz)
 * 	* PCM device nb. 2:
 *		16 x 16-bit playback - snd_emu10k1_fx8010_playback_ops
 * 		16 x 32-bit capture - snd_emu10k1_capture_efx_ops
 */
static int snd_emu10k1_emu1010_init(struct snd_emu10k1 *emu)
{
	unsigned int i;
	u32 tmp, tmp2, reg;
	int err;

	dev_info(emu->card->dev, "emu1010: Special config.\n");

	/* Mute, and disable audio and lock cache, just in case.
	 * Proper init follows in snd_emu10k1_init(). */
	outl(HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE_MASK, emu->port + HCFG);

	/* Disable 48Volt power to Audio Dock */
	snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_PWR, 0);

	/* ID, should read & 0x7f = 0x55. (Bit 7 is the IRQ bit) */
	snd_emu1010_fpga_read(emu, EMU_HANA_ID, &reg);
	dev_dbg(emu->card->dev, "reg1 = 0x%x\n", reg);
	if ((reg & 0x3f) == 0x15) {
		/* FPGA netlist already present so clear it */
		/* Return to programming mode */

		snd_emu1010_fpga_write(emu, EMU_HANA_FPGA_CONFIG, EMU_HANA_FPGA_CONFIG_HANA);
	}
	snd_emu1010_fpga_read(emu, EMU_HANA_ID, &reg);
	dev_dbg(emu->card->dev, "reg2 = 0x%x\n", reg);
	if ((reg & 0x3f) == 0x15) {
		/* FPGA failed to return to programming mode */
		dev_info(emu->card->dev,
			 "emu1010: FPGA failed to return to programming mode\n");
		return -ENODEV;
	}
	dev_info(emu->card->dev, "emu1010: EMU_HANA_ID = 0x%x\n", reg);

	err = snd_emu1010_load_firmware(emu, 0, &emu->firmware);
	if (err < 0) {
		dev_info(emu->card->dev, "emu1010: Loading Firmware failed\n");
		return err;
	}

	/* ID, should read & 0x7f = 0x55 when FPGA programmed. */
	snd_emu1010_fpga_read(emu, EMU_HANA_ID, &reg);
	if ((reg & 0x3f) != 0x15) {
		/* FPGA failed to be programmed */
		dev_info(emu->card->dev,
			 "emu1010: Loading Hana Firmware file failed, reg = 0x%x\n",
			 reg);
		return -ENODEV;
	}

	dev_info(emu->card->dev, "emu1010: Hana Firmware loaded\n");
	snd_emu1010_fpga_read(emu, EMU_HANA_MAJOR_REV, &tmp);
	snd_emu1010_fpga_read(emu, EMU_HANA_MINOR_REV, &tmp2);
	dev_info(emu->card->dev, "emu1010: Hana version: %u.%u\n", tmp, tmp2);
	/* Enable 48Volt power to Audio Dock */
	snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_PWR, EMU_HANA_DOCK_PWR_ON);

	snd_emu1010_fpga_read(emu, EMU_HANA_OPTION_CARDS, &reg);
	dev_info(emu->card->dev, "emu1010: Card options = 0x%x\n", reg);
	/* Optical -> ADAT I/O  */
	emu->emu1010.optical_in = 1; /* IN_ADAT */
	emu->emu1010.optical_out = 1; /* OUT_ADAT */
	tmp = (emu->emu1010.optical_in ? EMU_HANA_OPTICAL_IN_ADAT : EMU_HANA_OPTICAL_IN_SPDIF) |
		(emu->emu1010.optical_out ? EMU_HANA_OPTICAL_OUT_ADAT : EMU_HANA_OPTICAL_OUT_SPDIF);
	snd_emu1010_fpga_write(emu, EMU_HANA_OPTICAL_TYPE, tmp);
	/* Set no attenuation on Audio Dock pads. */
	emu->emu1010.adc_pads = 0x00;
	snd_emu1010_fpga_write(emu, EMU_HANA_ADC_PADS, emu->emu1010.adc_pads);
	/* Unmute Audio dock DACs, Headphone source DAC-4. */
	snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_MISC, EMU_HANA_DOCK_PHONES_192_DAC4);
	/* DAC PADs. */
	emu->emu1010.dac_pads = EMU_HANA_DOCK_DAC_PAD1 | EMU_HANA_DOCK_DAC_PAD2 |
				EMU_HANA_DOCK_DAC_PAD3 | EMU_HANA_DOCK_DAC_PAD4;
	snd_emu1010_fpga_write(emu, EMU_HANA_DAC_PADS, emu->emu1010.dac_pads);
	/* SPDIF Format. Set Consumer mode, 24bit, copy enable */
	snd_emu1010_fpga_write(emu, EMU_HANA_SPDIF_MODE, EMU_HANA_SPDIF_MODE_RX_INVALID);
	/* MIDI routing */
	snd_emu1010_fpga_write(emu, EMU_HANA_MIDI_IN, EMU_HANA_MIDI_INA_FROM_HAMOA | EMU_HANA_MIDI_INB_FROM_DOCK2);
	snd_emu1010_fpga_write(emu, EMU_HANA_MIDI_OUT, EMU_HANA_MIDI_OUT_DOCK2 | EMU_HANA_MIDI_OUT_SYNC2);
	/* IRQ Enable: All on */
	/* snd_emu1010_fpga_write(emu, EMU_HANA_IRQ_ENABLE, 0x0f); */
	/* IRQ Enable: All off */
	snd_emu1010_fpga_write(emu, EMU_HANA_IRQ_ENABLE, 0x00);

	emu->emu1010.internal_clock = 1; /* 48000 */
	/* Default WCLK set to 48kHz. */
	snd_emu1010_fpga_write(emu, EMU_HANA_DEFCLOCK, EMU_HANA_DEFCLOCK_48K);
	/* Word Clock source, Internal 48kHz x1 */
	snd_emu1010_fpga_write(emu, EMU_HANA_WCLOCK, EMU_HANA_WCLOCK_INT_48K);
	/* snd_emu1010_fpga_write(emu, EMU_HANA_WCLOCK, EMU_HANA_WCLOCK_INT_48K | EMU_HANA_WCLOCK_4X); */
	/* Audio Dock LEDs. */
	snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_LEDS_2, EMU_HANA_DOCK_LEDS_2_LOCK | EMU_HANA_DOCK_LEDS_2_48K);

#if 0
	/* For 96kHz */
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_0, EMU_SRC_HAMOA_ADC_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_1, EMU_SRC_HAMOA_ADC_RIGHT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_4, EMU_SRC_HAMOA_ADC_LEFT2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_5, EMU_SRC_HAMOA_ADC_RIGHT2);
#endif
#if 0
	/* For 192kHz */
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_0, EMU_SRC_HAMOA_ADC_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_1, EMU_SRC_HAMOA_ADC_RIGHT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_2, EMU_SRC_HAMOA_ADC_LEFT2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_3, EMU_SRC_HAMOA_ADC_RIGHT2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_4, EMU_SRC_HAMOA_ADC_LEFT3);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_5, EMU_SRC_HAMOA_ADC_RIGHT3);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_6, EMU_SRC_HAMOA_ADC_LEFT4);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_7, EMU_SRC_HAMOA_ADC_RIGHT4);
#endif
#if 1
	/* For 48kHz */
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_0, EMU_SRC_DOCK_MIC_A1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_1, EMU_SRC_DOCK_MIC_B1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_2, EMU_SRC_HAMOA_ADC_LEFT2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_3, EMU_SRC_HAMOA_ADC_LEFT2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_4, EMU_SRC_DOCK_ADC1_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_5, EMU_SRC_DOCK_ADC1_RIGHT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_6, EMU_SRC_DOCK_ADC2_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_7, EMU_SRC_DOCK_ADC2_RIGHT1);
	/* Pavel Hofman - setting defaults for 8 more capture channels
	 * Defaults only, users will set their own values anyways, let's
	 * just copy/paste.
	 */

	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_8, EMU_SRC_DOCK_MIC_A1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_9, EMU_SRC_DOCK_MIC_B1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_A, EMU_SRC_HAMOA_ADC_LEFT2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_B, EMU_SRC_HAMOA_ADC_LEFT2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_C, EMU_SRC_DOCK_ADC1_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_D, EMU_SRC_DOCK_ADC1_RIGHT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_E, EMU_SRC_DOCK_ADC2_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_F, EMU_SRC_DOCK_ADC2_RIGHT1);
#endif
#if 0
	/* Original */
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_4, EMU_SRC_HANA_ADAT);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_5, EMU_SRC_HANA_ADAT + 1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_6, EMU_SRC_HANA_ADAT + 2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_7, EMU_SRC_HANA_ADAT + 3);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_8, EMU_SRC_HANA_ADAT + 4);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_9, EMU_SRC_HANA_ADAT + 5);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_A, EMU_SRC_HANA_ADAT + 6);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_B, EMU_SRC_HANA_ADAT + 7);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_C, EMU_SRC_DOCK_MIC_A1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_D, EMU_SRC_DOCK_MIC_B1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_E, EMU_SRC_HAMOA_ADC_LEFT2);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE2_EMU32_F, EMU_SRC_HAMOA_ADC_LEFT2);
#endif
	for (i = 0; i < 0x20; i++) {
		/* AudioDock Elink <- Silence */
		snd_emu1010_fpga_link_dst_src_write(emu, 0x0100 + i, EMU_SRC_SILENCE);
	}
	for (i = 0; i < 4; i++) {
		/* Hana SPDIF Out <- Silence */
		snd_emu1010_fpga_link_dst_src_write(emu, 0x0200 + i, EMU_SRC_SILENCE);
	}
	for (i = 0; i < 7; i++) {
		/* Hamoa DAC <- Silence */
		snd_emu1010_fpga_link_dst_src_write(emu, 0x0300 + i, EMU_SRC_SILENCE);
	}
	for (i = 0; i < 7; i++) {
		/* Hana ADAT Out <- Silence */
		snd_emu1010_fpga_link_dst_src_write(emu, EMU_DST_HANA_ADAT + i, EMU_SRC_SILENCE);
	}
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE_I2S0_LEFT, EMU_SRC_DOCK_ADC1_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE_I2S0_RIGHT, EMU_SRC_DOCK_ADC1_RIGHT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE_I2S1_LEFT, EMU_SRC_DOCK_ADC2_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE_I2S1_RIGHT, EMU_SRC_DOCK_ADC2_RIGHT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE_I2S2_LEFT, EMU_SRC_DOCK_ADC3_LEFT1);
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_ALICE_I2S2_RIGHT, EMU_SRC_DOCK_ADC3_RIGHT1);
	snd_emu1010_fpga_write(emu, EMU_HANA_UNMUTE, EMU_UNMUTE);

#if 0
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_HAMOA_DAC_LEFT1, EMU_SRC_ALICE_EMU32B + 2); /* ALICE2 bus 0xa2 */
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_HAMOA_DAC_RIGHT1, EMU_SRC_ALICE_EMU32B + 3); /* ALICE2 bus 0xa3 */
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_HANA_SPDIF_LEFT1, EMU_SRC_ALICE_EMU32A + 2); /* ALICE2 bus 0xb2 */
	snd_emu1010_fpga_link_dst_src_write(emu,
		EMU_DST_HANA_SPDIF_RIGHT1, EMU_SRC_ALICE_EMU32A + 3); /* ALICE2 bus 0xb3 */
#endif
	/* Default outputs */
	if (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616) {
		/* 1616(M) cardbus default outputs */
		/* ALICE2 bus 0xa0 */
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC1_LEFT1, EMU_SRC_ALICE_EMU32A + 0);
		emu->emu1010.output_source[0] = 17;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC1_RIGHT1, EMU_SRC_ALICE_EMU32A + 1);
		emu->emu1010.output_source[1] = 18;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC2_LEFT1, EMU_SRC_ALICE_EMU32A + 2);
		emu->emu1010.output_source[2] = 19;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC2_RIGHT1, EMU_SRC_ALICE_EMU32A + 3);
		emu->emu1010.output_source[3] = 20;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC3_LEFT1, EMU_SRC_ALICE_EMU32A + 4);
		emu->emu1010.output_source[4] = 21;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC3_RIGHT1, EMU_SRC_ALICE_EMU32A + 5);
		emu->emu1010.output_source[5] = 22;
		/* ALICE2 bus 0xa0 */
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_MANA_DAC_LEFT, EMU_SRC_ALICE_EMU32A + 0);
		emu->emu1010.output_source[16] = 17;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_MANA_DAC_RIGHT, EMU_SRC_ALICE_EMU32A + 1);
		emu->emu1010.output_source[17] = 18;
	} else {
		/* ALICE2 bus 0xa0 */
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC1_LEFT1, EMU_SRC_ALICE_EMU32A + 0);
		emu->emu1010.output_source[0] = 21;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC1_RIGHT1, EMU_SRC_ALICE_EMU32A + 1);
		emu->emu1010.output_source[1] = 22;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC2_LEFT1, EMU_SRC_ALICE_EMU32A + 2);
		emu->emu1010.output_source[2] = 23;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC2_RIGHT1, EMU_SRC_ALICE_EMU32A + 3);
		emu->emu1010.output_source[3] = 24;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC3_LEFT1, EMU_SRC_ALICE_EMU32A + 4);
		emu->emu1010.output_source[4] = 25;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC3_RIGHT1, EMU_SRC_ALICE_EMU32A + 5);
		emu->emu1010.output_source[5] = 26;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC4_LEFT1, EMU_SRC_ALICE_EMU32A + 6);
		emu->emu1010.output_source[6] = 27;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_DAC4_RIGHT1, EMU_SRC_ALICE_EMU32A + 7);
		emu->emu1010.output_source[7] = 28;
		/* ALICE2 bus 0xa0 */
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_PHONES_LEFT1, EMU_SRC_ALICE_EMU32A + 0);
		emu->emu1010.output_source[8] = 21;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_PHONES_RIGHT1, EMU_SRC_ALICE_EMU32A + 1);
		emu->emu1010.output_source[9] = 22;
		/* ALICE2 bus 0xa0 */
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_SPDIF_LEFT1, EMU_SRC_ALICE_EMU32A + 0);
		emu->emu1010.output_source[10] = 21;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_DOCK_SPDIF_RIGHT1, EMU_SRC_ALICE_EMU32A + 1);
		emu->emu1010.output_source[11] = 22;
		/* ALICE2 bus 0xa0 */
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_SPDIF_LEFT1, EMU_SRC_ALICE_EMU32A + 0);
		emu->emu1010.output_source[12] = 21;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_SPDIF_RIGHT1, EMU_SRC_ALICE_EMU32A + 1);
		emu->emu1010.output_source[13] = 22;
		/* ALICE2 bus 0xa0 */
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HAMOA_DAC_LEFT1, EMU_SRC_ALICE_EMU32A + 0);
		emu->emu1010.output_source[14] = 21;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HAMOA_DAC_RIGHT1, EMU_SRC_ALICE_EMU32A + 1);
		emu->emu1010.output_source[15] = 22;
		/* ALICE2 bus 0xa0 */
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_ADAT, EMU_SRC_ALICE_EMU32A + 0);
		emu->emu1010.output_source[16] = 21;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_ADAT + 1, EMU_SRC_ALICE_EMU32A + 1);
		emu->emu1010.output_source[17] = 22;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_ADAT + 2, EMU_SRC_ALICE_EMU32A + 2);
		emu->emu1010.output_source[18] = 23;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_ADAT + 3, EMU_SRC_ALICE_EMU32A + 3);
		emu->emu1010.output_source[19] = 24;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_ADAT + 4, EMU_SRC_ALICE_EMU32A + 4);
		emu->emu1010.output_source[20] = 25;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_ADAT + 5, EMU_SRC_ALICE_EMU32A + 5);
		emu->emu1010.output_source[21] = 26;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_ADAT + 6, EMU_SRC_ALICE_EMU32A + 6);
		emu->emu1010.output_source[22] = 27;
		snd_emu1010_fpga_link_dst_src_write(emu,
			EMU_DST_HANA_ADAT + 7, EMU_SRC_ALICE_EMU32A + 7);
		emu->emu1010.output_source[23] = 28;
	}

	return 0;
}
/*
 *  Create the EMU10K1 instance
 */

#ifdef CONFIG_PM_SLEEP
static int alloc_pm_buffer(struct snd_emu10k1 *emu);
static void free_pm_buffer(struct snd_emu10k1 *emu);
#endif

static void snd_emu10k1_free(struct snd_card *card)
{
	struct snd_emu10k1 *emu = card->private_data;

	if (emu->port) {	/* avoid access to already used hardware */
		snd_emu10k1_fx8010_tram_setup(emu, 0);
		snd_emu10k1_done(emu);
		snd_emu10k1_free_efx(emu);
	}
	if (emu->card_capabilities->emu_model == EMU_MODEL_EMU1010) {
		/* Disable 48Volt power to Audio Dock */
		snd_emu1010_fpga_write(emu, EMU_HANA_DOCK_PWR, 0);
	}
	cancel_delayed_work_sync(&emu->emu1010.firmware_work);
	release_firmware(emu->firmware);
	release_firmware(emu->dock_fw);
	snd_util_memhdr_free(emu->memhdr);
	if (emu->silent_page.area)
		snd_dma_free_pages(&emu->silent_page);
	if (emu->ptb_pages.area)
		snd_dma_free_pages(&emu->ptb_pages);
	vfree(emu->page_ptr_table);
	vfree(emu->page_addr_table);
#ifdef CONFIG_PM_SLEEP
	free_pm_buffer(emu);
#endif
}

static const struct snd_emu_chip_details emu_chip_details[] = {
	/* Audigy 5/Rx SB1550 */
	/* Tested by michael@gernoth.net 28 Mar 2015 */
	/* DSP: CA10300-IAT LF
	 * DAC: Cirrus Logic CS4382-KQZ
	 * ADC: Philips 1361T
	 * AC97: Sigmatel STAC9750
	 * CA0151: None
	 */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x10241102,
	 .driver = "Audigy2", .name = "SB Audigy 5/Rx [SB1550]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .spk71 = 1,
	 .adc_1361t = 1,  /* 24 bit capture instead of 16bit */
	 .ac97_chip = 1},
	/* Audigy4 (Not PRO) SB0610 */
	/* Tested by James@superbug.co.uk 4th April 2006 */
	/* A_IOCFG bits
	 * Output
	 * 0: ?
	 * 1: ?
	 * 2: ?
	 * 3: 0 - Digital Out, 1 - Line in
	 * 4: ?
	 * 5: ?
	 * 6: ?
	 * 7: ?
	 * Input
	 * 8: ?
	 * 9: ?
	 * A: Green jack sense (Front)
	 * B: ?
	 * C: Black jack sense (Rear/Side Right)
	 * D: Yellow jack sense (Center/LFE/Side Left)
	 * E: ?
	 * F: ?
	 *
	 * Digital Out/Line in switch using A_IOCFG bit 3 (0x08)
	 * 0 - Digital Out
	 * 1 - Line in
	 */
	/* Mic input not tested.
	 * Analog CD input not tested
	 * Digital Out not tested.
	 * Line in working.
	 * Audio output 5.1 working. Side outputs not working.
	 */
	/* DSP: CA10300-IAT LF
	 * DAC: Cirrus Logic CS4382-KQZ
	 * ADC: Philips 1361T
	 * AC97: Sigmatel STAC9750
	 * CA0151: None
	 */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x10211102,
	 .driver = "Audigy2", .name = "SB Audigy 4 [SB0610]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .spk71 = 1,
	 .adc_1361t = 1,  /* 24 bit capture instead of 16bit */
	 .ac97_chip = 1} ,
	/* Audigy 2 Value AC3 out does not work yet.
	 * Need to find out how to turn off interpolators.
	 */
	/* Tested by James@superbug.co.uk 3rd July 2005 */
	/* DSP: CA0108-IAT
	 * DAC: CS4382-KQ
	 * ADC: Philips 1361T
	 * AC97: STAC9750
	 * CA0151: None
	 */
	/*
	 * A_IOCFG Input (GPIO)
	 * 0x400  = Front analog jack plugged in. (Green socket)
	 * 0x1000 = Rear analog jack plugged in. (Black socket)
	 * 0x2000 = Center/LFE analog jack plugged in. (Orange socket)
	 * A_IOCFG Output (GPIO)
	 * 0x60 = Sound out of front Left.
	 * Win sets it to 0xXX61
	 */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x10011102,
	 .driver = "Audigy2", .name = "SB Audigy 2 Value [SB0400]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .spk71 = 1,
	 .ac97_chip = 1} ,
	/* Audigy 2 ZS Notebook Cardbus card.*/
	/* Tested by James@superbug.co.uk 6th November 2006 */
	/* Audio output 7.1/Headphones working.
	 * Digital output working. (AC3 not checked, only PCM)
	 * Audio Mic/Line inputs working.
	 * Digital input not tested.
	 */
	/* DSP: Tina2
	 * DAC: Wolfson WM8768/WM8568
	 * ADC: Wolfson WM8775
	 * AC97: None
	 * CA0151: None
	 */
	/* Tested by James@superbug.co.uk 4th April 2006 */
	/* A_IOCFG bits
	 * Output
	 * 0: Not Used
	 * 1: 0 = Mute all the 7.1 channel out. 1 = unmute.
	 * 2: Analog input 0 = line in, 1 = mic in
	 * 3: Not Used
	 * 4: Digital output 0 = off, 1 = on.
	 * 5: Not Used
	 * 6: Not Used
	 * 7: Not Used
	 * Input
	 *      All bits 1 (0x3fxx) means nothing plugged in.
	 * 8-9: 0 = Line in/Mic, 2 = Optical in, 3 = Nothing.
	 * A-B: 0 = Headphones, 2 = Optical out, 3 = Nothing.
	 * C-D: 2 = Front/Rear/etc, 3 = nothing.
	 * E-F: Always 0
	 *
	 */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x20011102,
	 .driver = "Audigy2", .name = "Audigy 2 ZS Notebook [SB0530]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .ca_cardbus_chip = 1,
	 .spi_dac = 1,
	 .i2c_adc = 1,
	 .spk71 = 1} ,
	/* This is MAEM8950 "Mana" */
	/* Attach MicroDock[M] to make it an E-MU 1616[m]. */
	/* Does NOT support sync daughter card (obviously). */
	/* Tested by James@superbug.co.uk 4th Nov 2007. */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x42011102,
	 .driver = "Audigy2", .name = "E-mu 1010 Notebook [MAEM8950]",
	 .id = "EMU1010",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .ca_cardbus_chip = 1,
	 .spk71 = 1 ,
	 .emu_model = EMU_MODEL_EMU1616},
	/* Tested by James@superbug.co.uk 4th Nov 2007. */
	/* This is MAEM8960 "Hana3", 0202 is MAEM8980 */
	/* Attach 0202 daughter card to make it an E-MU 1212m, OR a
	 * MicroDock[M] to make it an E-MU 1616[m]. */
	/* Does NOT support sync daughter card. */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x40041102,
	 .driver = "Audigy2", .name = "E-mu 1010b PCI [MAEM8960]",
	 .id = "EMU1010",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .spk71 = 1,
	 .emu_model = EMU_MODEL_EMU1010B}, /* EMU 1010 new revision */
	/* Tested by Maxim Kachur <mcdebugger@duganet.ru> 17th Oct 2012. */
	/* This is MAEM8986, 0202 is MAEM8980 */
	/* Attach 0202 daughter card to make it an E-MU 1212m, OR a
	 * MicroDockM to make it an E-MU 1616m. The non-m
	 * version was never sold with this card, but should
	 * still work. */
	/* Does NOT support sync daughter card. */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x40071102,
	 .driver = "Audigy2", .name = "E-mu 1010 PCIe [MAEM8986]",
	 .id = "EMU1010",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .spk71 = 1,
	 .emu_model = EMU_MODEL_EMU1010B}, /* EMU 1010 PCIe */
	/* Tested by James@superbug.co.uk 8th July 2005. */
	/* This is MAEM8810 "Hana", 0202 is MAEM8820 "Hamoa" */
	/* Attach 0202 daughter card to make it an E-MU 1212m, OR an
	 * AudioDock[M] to make it an E-MU 1820[m]. */
	/* Supports sync daughter card. */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x40011102,
	 .driver = "Audigy2", .name = "E-mu 1010 [MAEM8810]",
	 .id = "EMU1010",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .spk71 = 1,
	 .emu_model = EMU_MODEL_EMU1010}, /* EMU 1010 old revision */
	/* This is MAEM8852 "HanaLiteLite" */
	/* Supports sync daughter card. */
	/* Tested by oswald.buddenhagen@gmx.de Mar 2023. */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x40021102,
	 .driver = "Audigy2", .name = "E-mu 0404b PCI [MAEM8852]",
	 .id = "EMU0404",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .spk71 = 1,
	 .emu_model = EMU_MODEL_EMU0404}, /* EMU 0404 new revision */
	/* This is MAEM8850 "HanaLite" */
	/* Supports sync daughter card. */
	/* Tested by James@superbug.co.uk 20-3-2007. */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x40021102,
	 .driver = "Audigy2", .name = "E-mu 0404 [MAEM8850]",
	 .id = "EMU0404",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .spk71 = 1,
	 .emu_model = EMU_MODEL_EMU0404}, /* EMU 0404 */
	/* EMU0404 PCIe */
	/* Does NOT support sync daughter card. */
	{.vendor = 0x1102, .device = 0x0008, .subsystem = 0x40051102,
	 .driver = "Audigy2", .name = "E-mu 0404 PCIe [MAEM8984]",
	 .id = "EMU0404",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .spk71 = 1,
	 .emu_model = EMU_MODEL_EMU0404}, /* EMU 0404 PCIe ver_03 */
	{.vendor = 0x1102, .device = 0x0008,
	 .driver = "Audigy2", .name = "SB Audigy 2 Value [Unknown]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0108_chip = 1,
	 .ac97_chip = 1} ,
	/* Tested by James@superbug.co.uk 3rd July 2005 */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x20071102,
	 .driver = "Audigy2", .name = "SB Audigy 4 PRO [SB0380]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1,
	 .ac97_chip = 1} ,
	/* Tested by shane-alsa@cm.nu 5th Nov 2005 */
	/* The 0x20061102 does have SB0350 written on it
	 * Just like 0x20021102
	 */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x20061102,
	 .driver = "Audigy2", .name = "SB Audigy 2 [SB0350b]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1,
	 .invert_shared_spdif = 1,	/* digital/analog switch swapped */
	 .ac97_chip = 1} ,
	/* 0x20051102 also has SB0350 written on it, treated as Audigy 2 ZS by
	   Creative's Windows driver */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x20051102,
	 .driver = "Audigy2", .name = "SB Audigy 2 ZS [SB0350a]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1,
	 .invert_shared_spdif = 1,	/* digital/analog switch swapped */
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x20021102,
	 .driver = "Audigy2", .name = "SB Audigy 2 ZS [SB0350]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1,
	 .invert_shared_spdif = 1,	/* digital/analog switch swapped */
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x20011102,
	 .driver = "Audigy2", .name = "SB Audigy 2 ZS [SB0360]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1,
	 .invert_shared_spdif = 1,	/* digital/analog switch swapped */
	 .ac97_chip = 1} ,
	/* Audigy 2 */
	/* Tested by James@superbug.co.uk 3rd July 2005 */
	/* DSP: CA0102-IAT
	 * DAC: CS4382-KQ
	 * ADC: Philips 1361T
	 * AC97: STAC9721
	 * CA0151: Yes
	 */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x10071102,
	 .driver = "Audigy2", .name = "SB Audigy 2 [SB0240]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1,
	 .adc_1361t = 1,  /* 24 bit capture instead of 16bit */
	 .ac97_chip = 1} ,
	/* Audigy 2 Platinum EX */
	/* Win driver sets A_IOCFG output to 0x1c00 */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x10051102,
	 .driver = "Audigy2", .name = "Audigy 2 Platinum EX [SB0280]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1} ,
	/* Dell OEM/Creative Labs Audigy 2 ZS */
	/* See ALSA bug#1365 */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x10031102,
	 .driver = "Audigy2", .name = "SB Audigy 2 ZS [SB0353]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1,
	 .invert_shared_spdif = 1,	/* digital/analog switch swapped */
	 .ac97_chip = 1} ,
	/* Audigy 2 Platinum */
	/* Win driver sets A_IOCFG output to 0xa00 */
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x10021102,
	 .driver = "Audigy2", .name = "SB Audigy 2 Platinum [SB0240P]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spk71 = 1,
	 .spdif_bug = 1,
	 .invert_shared_spdif = 1,	/* digital/analog switch swapped */
	 .adc_1361t = 1,  /* 24 bit capture instead of 16bit. Fixes ALSA bug#324 */
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0004, .revision = 0x04,
	 .driver = "Audigy2", .name = "SB Audigy 2 [Unknown]",
	 .id = "Audigy2",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ca0151_chip = 1,
	 .spdif_bug = 1,
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x00531102,
	 .driver = "Audigy", .name = "SB Audigy 1 [SB0092]",
	 .id = "Audigy",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x00521102,
	 .driver = "Audigy", .name = "SB Audigy 1 ES [SB0160]",
	 .id = "Audigy",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .spdif_bug = 1,
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0004, .subsystem = 0x00511102,
	 .driver = "Audigy", .name = "SB Audigy 1 [SB0090]",
	 .id = "Audigy",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0004,
	 .driver = "Audigy", .name = "Audigy 1 [Unknown]",
	 .id = "Audigy",
	 .emu10k2_chip = 1,
	 .ca0102_chip = 1,
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x100a1102,
	 .driver = "EMU10K1", .name = "SB Live! 5.1 [SB0220]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x806b1102,
	 .driver = "EMU10K1", .name = "SB Live! [SB0105]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x806a1102,
	 .driver = "EMU10K1", .name = "SB Live! Value [SB0103]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80691102,
	 .driver = "EMU10K1", .name = "SB Live! Value [SB0101]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	/* Tested by ALSA bug#1680 26th December 2005 */
	/* note: It really has SB0220 written on the card, */
	/* but it's SB0228 according to kx.inf */
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80661102,
	 .driver = "EMU10K1", .name = "SB Live! 5.1 Dell OEM [SB0228]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	/* Tested by Thomas Zehetbauer 27th Aug 2005 */
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80651102,
	 .driver = "EMU10K1", .name = "SB Live! 5.1 [SB0220]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80641102,
	 .driver = "EMU10K1", .name = "SB Live! 5.1",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	/* Tested by alsa bugtrack user "hus" bug #1297 12th Aug 2005 */
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80611102,
	 .driver = "EMU10K1", .name = "SB Live! 5.1 [SB0060]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 2, /* ac97 is optional; both SBLive 5.1 and platinum
			  * share the same IDs!
			  */
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80511102,
	 .driver = "EMU10K1", .name = "SB Live! Value [CT4850]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	/* SB Live! Platinum */
	/* Win driver sets A_IOCFG output to 0 */
	/* Tested by Jonathan Dowland <jon@dow.land> Apr 2023. */
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80401102,
	 .driver = "EMU10K1", .name = "SB Live! Platinum [CT4760P]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80321102,
	 .driver = "EMU10K1", .name = "SB Live! Value [CT4871]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80311102,
	 .driver = "EMU10K1", .name = "SB Live! Value [CT4831]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80281102,
	 .driver = "EMU10K1", .name = "SB Live! Value [CT4870]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	/* Tested by James@superbug.co.uk 3rd July 2005 */
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80271102,
	 .driver = "EMU10K1", .name = "SB Live! Value [CT4832]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80261102,
	 .driver = "EMU10K1", .name = "SB Live! Value [CT4830]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80231102,
	 .driver = "EMU10K1", .name = "SB PCI512 [CT4790]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x80221102,
	 .driver = "EMU10K1", .name = "SB Live! Value [CT4780]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x40011102,
	 .driver = "EMU10K1", .name = "E-mu APS [PC545]",
	 .id = "APS",
	 .emu10k1_chip = 1,
	 .ecard = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x00211102,
	 .driver = "EMU10K1", .name = "SB Live! [CT4620]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002, .subsystem = 0x00201102,
	 .driver = "EMU10K1", .name = "SB Live! Value [CT4670]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{.vendor = 0x1102, .device = 0x0002,
	 .driver = "EMU10K1", .name = "SB Live! [Unknown]",
	 .id = "Live",
	 .emu10k1_chip = 1,
	 .ac97_chip = 1,
	 .sblive51 = 1} ,
	{ } /* terminator */
};

/*
 * The chip (at least the Audigy 2 CA0102 chip, but most likely others, too)
 * has a problem that from time to time it likes to do few DMA reads a bit
 * beyond its normal allocation and gets very confused if these reads get
 * blocked by a IOMMU.
 *
 * This behaviour has been observed for the first (reserved) page
 * (for which it happens multiple times at every playback), often for various
 * synth pages and sometimes for PCM playback buffers and the page table
 * memory itself.
 *
 * As a workaround let's widen these DMA allocations by an extra page if we
 * detect that the device is behind a non-passthrough IOMMU.
 */
static void snd_emu10k1_detect_iommu(struct snd_emu10k1 *emu)
{
	struct iommu_domain *domain;

	emu->iommu_workaround = false;

	domain = iommu_get_domain_for_dev(emu->card->dev);
	if (!domain || domain->type == IOMMU_DOMAIN_IDENTITY)
		return;

	dev_notice(emu->card->dev,
		   "non-passthrough IOMMU detected, widening DMA allocations");
	emu->iommu_workaround = true;
}

int snd_emu10k1_create(struct snd_card *card,
		       struct pci_dev *pci,
		       unsigned short extin_mask,
		       unsigned short extout_mask,
		       long max_cache_bytes,
		       int enable_ir,
		       uint subsystem)
{
	struct snd_emu10k1 *emu = card->private_data;
	int idx, err;
	int is_audigy;
	size_t page_table_size;
	__le32 *pgtbl;
	unsigned int silent_page;
	const struct snd_emu_chip_details *c;

	/* enable PCI device */
	err = pcim_enable_device(pci);
	if (err < 0)
		return err;

	card->private_free = snd_emu10k1_free;
	emu->card = card;
	spin_lock_init(&emu->reg_lock);
	spin_lock_init(&emu->emu_lock);
	spin_lock_init(&emu->spi_lock);
	spin_lock_init(&emu->i2c_lock);
	spin_lock_init(&emu->voice_lock);
	spin_lock_init(&emu->synth_lock);
	spin_lock_init(&emu->memblk_lock);
	mutex_init(&emu->fx8010.lock);
	INIT_LIST_HEAD(&emu->mapped_link_head);
	INIT_LIST_HEAD(&emu->mapped_order_link_head);
	emu->pci = pci;
	emu->irq = -1;
	emu->synth = NULL;
	emu->get_synth_voice = NULL;
	INIT_DELAYED_WORK(&emu->emu1010.firmware_work, emu1010_firmware_work);
	/* read revision & serial */
	emu->revision = pci->revision;
	pci_read_config_dword(pci, PCI_SUBSYSTEM_VENDOR_ID, &emu->serial);
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &emu->model);
	dev_dbg(card->dev,
		"vendor = 0x%x, device = 0x%x, subsystem_vendor_id = 0x%x, subsystem_id = 0x%x\n",
		pci->vendor, pci->device, emu->serial, emu->model);

	for (c = emu_chip_details; c->vendor; c++) {
		if (c->vendor == pci->vendor && c->device == pci->device) {
			if (subsystem) {
				if (c->subsystem && (c->subsystem == subsystem))
					break;
				else
					continue;
			} else {
				if (c->subsystem && (c->subsystem != emu->serial))
					continue;
				if (c->revision && c->revision != emu->revision)
					continue;
			}
			break;
		}
	}
	if (c->vendor == 0) {
		dev_err(card->dev, "emu10k1: Card not recognised\n");
		return -ENOENT;
	}
	emu->card_capabilities = c;
	if (c->subsystem && !subsystem)
		dev_dbg(card->dev, "Sound card name = %s\n", c->name);
	else if (subsystem)
		dev_dbg(card->dev, "Sound card name = %s, "
			"vendor = 0x%x, device = 0x%x, subsystem = 0x%x. "
			"Forced to subsystem = 0x%x\n",	c->name,
			pci->vendor, pci->device, emu->serial, c->subsystem);
	else
		dev_dbg(card->dev, "Sound card name = %s, "
			"vendor = 0x%x, device = 0x%x, subsystem = 0x%x.\n",
			c->name, pci->vendor, pci->device,
			emu->serial);

	if (!*card->id && c->id)
		strscpy(card->id, c->id, sizeof(card->id));

	is_audigy = emu->audigy = c->emu10k2_chip;

	snd_emu10k1_detect_iommu(emu);

	/* set addressing mode */
	emu->address_mode = is_audigy ? 0 : 1;
	/* set the DMA transfer mask */
	emu->dma_mask = emu->address_mode ? EMU10K1_DMA_MASK : AUDIGY_DMA_MASK;
	if (dma_set_mask_and_coherent(&pci->dev, emu->dma_mask) < 0) {
		dev_err(card->dev,
			"architecture does not support PCI busmaster DMA with mask 0x%lx\n",
			emu->dma_mask);
		return -ENXIO;
	}
	if (is_audigy)
		emu->gpr_base = A_FXGPREGBASE;
	else
		emu->gpr_base = FXGPREGBASE;

	err = pci_request_regions(pci, "EMU10K1");
	if (err < 0)
		return err;
	emu->port = pci_resource_start(pci, 0);

	emu->max_cache_pages = max_cache_bytes >> PAGE_SHIFT;

	page_table_size = sizeof(u32) * (emu->address_mode ? MAXPAGES1 :
					 MAXPAGES0);
	if (snd_emu10k1_alloc_pages_maybe_wider(emu, page_table_size,
						&emu->ptb_pages) < 0)
		return -ENOMEM;
	dev_dbg(card->dev, "page table address range is %.8lx:%.8lx\n",
		(unsigned long)emu->ptb_pages.addr,
		(unsigned long)(emu->ptb_pages.addr + emu->ptb_pages.bytes));

	emu->page_ptr_table = vmalloc(array_size(sizeof(void *),
						 emu->max_cache_pages));
	emu->page_addr_table = vmalloc(array_size(sizeof(unsigned long),
						  emu->max_cache_pages));
	if (!emu->page_ptr_table || !emu->page_addr_table)
		return -ENOMEM;

	if (snd_emu10k1_alloc_pages_maybe_wider(emu, EMUPAGESIZE,
						&emu->silent_page) < 0)
		return -ENOMEM;
	dev_dbg(card->dev, "silent page range is %.8lx:%.8lx\n",
		(unsigned long)emu->silent_page.addr,
		(unsigned long)(emu->silent_page.addr +
				emu->silent_page.bytes));

	emu->memhdr = snd_util_memhdr_new(emu->max_cache_pages * PAGE_SIZE);
	if (!emu->memhdr)
		return -ENOMEM;
	emu->memhdr->block_extra_size = sizeof(struct snd_emu10k1_memblk) -
		sizeof(struct snd_util_memblk);

	pci_set_master(pci);

	// The masks are not used for Audigy.
	// FIXME: these should come from the card_capabilites table.
	if (extin_mask == 0)
		extin_mask = 0x3fcf;  // EXTIN_*
	if (extout_mask == 0)
		extout_mask = 0x7fff;  // EXTOUT_*
	emu->fx8010.extin_mask = extin_mask;
	emu->fx8010.extout_mask = extout_mask;
	emu->enable_ir = enable_ir;

	if (emu->card_capabilities->ca_cardbus_chip) {
		err = snd_emu10k1_cardbus_init(emu);
		if (err < 0)
			return err;
	}
	if (emu->card_capabilities->ecard) {
		err = snd_emu10k1_ecard_init(emu);
		if (err < 0)
			return err;
	} else if (emu->card_capabilities->emu_model) {
		err = snd_emu10k1_emu1010_init(emu);
		if (err < 0)
			return err;
	} else {
		/* 5.1: Enable the additional AC97 Slots. If the emu10k1 version
			does not support this, it shouldn't do any harm */
		snd_emu10k1_ptr_write(emu, AC97SLOT, 0,
					AC97SLOT_CNTR|AC97SLOT_LFE);
	}

	/* initialize TRAM setup */
	emu->fx8010.itram_size = (16 * 1024)/2;
	emu->fx8010.etram_pages.area = NULL;
	emu->fx8010.etram_pages.bytes = 0;

	/* irq handler must be registered after I/O ports are activated */
	if (devm_request_irq(&pci->dev, pci->irq, snd_emu10k1_interrupt,
			     IRQF_SHARED, KBUILD_MODNAME, emu))
		return -EBUSY;
	emu->irq = pci->irq;
	card->sync_irq = emu->irq;

	/*
	 *  Init to 0x02109204 :
	 *  Clock accuracy    = 0     (1000ppm)
	 *  Sample Rate       = 2     (48kHz)
	 *  Audio Channel     = 1     (Left of 2)
	 *  Source Number     = 0     (Unspecified)
	 *  Generation Status = 1     (Original for Cat Code 12)
	 *  Cat Code          = 12    (Digital Signal Mixer)
	 *  Mode              = 0     (Mode 0)
	 *  Emphasis          = 0     (None)
	 *  CP                = 1     (Copyright unasserted)
	 *  AN                = 0     (Audio data)
	 *  P                 = 0     (Consumer)
	 */
	emu->spdif_bits[0] = emu->spdif_bits[1] =
		emu->spdif_bits[2] = SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
		SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
		SPCS_GENERATIONSTATUS | 0x00001200 |
		0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT;

	/* Clear silent pages and set up pointers */
	memset(emu->silent_page.area, 0, emu->silent_page.bytes);
	silent_page = emu->silent_page.addr << emu->address_mode;
	pgtbl = (__le32 *)emu->ptb_pages.area;
	for (idx = 0; idx < (emu->address_mode ? MAXPAGES1 : MAXPAGES0); idx++)
		pgtbl[idx] = cpu_to_le32(silent_page | idx);

	/* set up voice indices */
	for (idx = 0; idx < NUM_G; idx++)
		emu->voices[idx].number = idx;

	err = snd_emu10k1_init(emu, enable_ir);
	if (err < 0)
		return err;
#ifdef CONFIG_PM_SLEEP
	err = alloc_pm_buffer(emu);
	if (err < 0)
		return err;
#endif

	/*  Initialize the effect engine */
	err = snd_emu10k1_init_efx(emu);
	if (err < 0)
		return err;
	snd_emu10k1_audio_enable(emu);

#ifdef CONFIG_SND_PROC_FS
	snd_emu10k1_proc_init(emu);
#endif
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static const unsigned char saved_regs[] = {
	CPF, PTRX, CVCF, VTFT, Z1, Z2, PSST, DSL, CCCA, CCR, CLP,
	FXRT, MAPA, MAPB, ENVVOL, ATKHLDV, DCYSUSV, LFOVAL1, ENVVAL,
	ATKHLDM, DCYSUSM, LFOVAL2, IP, IFATN, PEFE, FMMOD, TREMFRQ, FM2FRQ2,
	TEMPENV, ADCCR, FXWC, MICBA, ADCBA, FXBA,
	MICBS, ADCBS, FXBS, CDCS, GPSCS, SPCS0, SPCS1, SPCS2,
	SPBYPASS, AC97SLOT, CDSRCS, GPSRCS, ZVSRCS, MICIDX, ADCIDX, FXIDX,
	0xff /* end */
};
static const unsigned char saved_regs_audigy[] = {
	A_ADCIDX, A_MICIDX, A_FXWC1, A_FXWC2, A_EHC,
	A_FXRT2, A_SENDAMOUNTS, A_FXRT1,
	0xff /* end */
};

static int alloc_pm_buffer(struct snd_emu10k1 *emu)
{
	int size;

	size = ARRAY_SIZE(saved_regs);
	if (emu->audigy)
		size += ARRAY_SIZE(saved_regs_audigy);
	emu->saved_ptr = vmalloc(array3_size(4, NUM_G, size));
	if (!emu->saved_ptr)
		return -ENOMEM;
	if (snd_emu10k1_efx_alloc_pm_buffer(emu) < 0)
		return -ENOMEM;
	if (emu->card_capabilities->ca0151_chip &&
	    snd_p16v_alloc_pm_buffer(emu) < 0)
		return -ENOMEM;
	return 0;
}

static void free_pm_buffer(struct snd_emu10k1 *emu)
{
	vfree(emu->saved_ptr);
	snd_emu10k1_efx_free_pm_buffer(emu);
	if (emu->card_capabilities->ca0151_chip)
		snd_p16v_free_pm_buffer(emu);
}

void snd_emu10k1_suspend_regs(struct snd_emu10k1 *emu)
{
	int i;
	const unsigned char *reg;
	unsigned int *val;

	val = emu->saved_ptr;
	for (reg = saved_regs; *reg != 0xff; reg++)
		for (i = 0; i < NUM_G; i++, val++)
			*val = snd_emu10k1_ptr_read(emu, *reg, i);
	if (emu->audigy) {
		for (reg = saved_regs_audigy; *reg != 0xff; reg++)
			for (i = 0; i < NUM_G; i++, val++)
				*val = snd_emu10k1_ptr_read(emu, *reg, i);
	}
	if (emu->audigy)
		emu->saved_a_iocfg = inw(emu->port + A_IOCFG);
	emu->saved_hcfg = inl(emu->port + HCFG);
}

void snd_emu10k1_resume_init(struct snd_emu10k1 *emu)
{
	if (emu->card_capabilities->ca_cardbus_chip)
		snd_emu10k1_cardbus_init(emu);
	if (emu->card_capabilities->ecard)
		snd_emu10k1_ecard_init(emu);
	else if (emu->card_capabilities->emu_model)
		snd_emu10k1_emu1010_init(emu);
	else
		snd_emu10k1_ptr_write(emu, AC97SLOT, 0, AC97SLOT_CNTR|AC97SLOT_LFE);
	snd_emu10k1_init(emu, emu->enable_ir);
}

void snd_emu10k1_resume_regs(struct snd_emu10k1 *emu)
{
	int i;
	const unsigned char *reg;
	unsigned int *val;

	snd_emu10k1_audio_enable(emu);

	/* resore for spdif */
	if (emu->audigy)
		outw(emu->saved_a_iocfg, emu->port + A_IOCFG);
	outl(emu->saved_hcfg, emu->port + HCFG);

	val = emu->saved_ptr;
	for (reg = saved_regs; *reg != 0xff; reg++)
		for (i = 0; i < NUM_G; i++, val++)
			snd_emu10k1_ptr_write(emu, *reg, i, *val);
	if (emu->audigy) {
		for (reg = saved_regs_audigy; *reg != 0xff; reg++)
			for (i = 0; i < NUM_G; i++, val++)
				snd_emu10k1_ptr_write(emu, *reg, i, *val);
	}
}
#endif
