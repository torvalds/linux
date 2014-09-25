/*****************************************************************************
 *
 * Copyright (C) 2008 Cedric Bregardis <cedric.bregardis@free.fr> and
 * Jean-Christian Hassler <jhassler@free.fr>
 *
 * This file is part of the Audiowerk2 ALSA driver
 *
 * The Audiowerk2 ALSA driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * The Audiowerk2 ALSA driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the Audiowerk2 ALSA driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
 *****************************************************************************/

#define AW2_SAA7146_M

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "saa7146.h"
#include "aw2-saa7146.h"

#include "aw2-tsl.c"

#define WRITEREG(value, addr) writel((value), chip->base_addr + (addr))
#define READREG(addr) readl(chip->base_addr + (addr))

static struct snd_aw2_saa7146_cb_param
 arr_substream_it_playback_cb[NB_STREAM_PLAYBACK];
static struct snd_aw2_saa7146_cb_param
 arr_substream_it_capture_cb[NB_STREAM_CAPTURE];

static int snd_aw2_saa7146_get_limit(int size);

/* chip-specific destructor */
int snd_aw2_saa7146_free(struct snd_aw2_saa7146 *chip)
{
	/* disable all irqs */
	WRITEREG(0, IER);

	/* reset saa7146 */
	WRITEREG((MRST_N << 16), MC1);

	/* Unset base addr */
	chip->base_addr = NULL;

	return 0;
}

void snd_aw2_saa7146_setup(struct snd_aw2_saa7146 *chip,
			   void __iomem *pci_base_addr)
{
	/* set PCI burst/threshold

	   Burst length definition
	   VALUE    BURST LENGTH
	   000      1 Dword
	   001      2 Dwords
	   010      4 Dwords
	   011      8 Dwords
	   100      16 Dwords
	   101      32 Dwords
	   110      64 Dwords
	   111      128 Dwords

	   Threshold definition
	   VALUE    WRITE MODE              READ MODE
	   00       1 Dword of valid data   1 empty Dword
	   01       4 Dwords of valid data  4 empty Dwords
	   10       8 Dwords of valid data  8 empty Dwords
	   11       16 Dwords of valid data 16 empty Dwords */

	unsigned int acon2;
	unsigned int acon1 = 0;
	int i;

	/* Set base addr */
	chip->base_addr = pci_base_addr;

	/* disable all irqs */
	WRITEREG(0, IER);

	/* reset saa7146 */
	WRITEREG((MRST_N << 16), MC1);

	/* enable audio interface */
#ifdef __BIG_ENDIAN
	acon1 |= A1_SWAP;
	acon1 |= A2_SWAP;
#endif
	/* WS0_CTRL, WS0_SYNC: input TSL1, I2S */

	/* At initialization WS1 and WS2 are disabled (configured as input) */
	acon1 |= 0 * WS1_CTRL;
	acon1 |= 0 * WS2_CTRL;

	/* WS4 is not used. So it must not restart A2.
	   This is why it is configured as output (force to low) */
	acon1 |= 3 * WS4_CTRL;

	/* WS3_CTRL, WS3_SYNC: output TSL2, I2S */
	acon1 |= 2 * WS3_CTRL;

	/* A1 and A2 are active and asynchronous */
	acon1 |= 3 * AUDIO_MODE;
	WRITEREG(acon1, ACON1);

	/* The following comes from original windows driver.
	   It is needed to have a correct behavior of input and output
	   simultenously, but I don't know why ! */
	WRITEREG(3 * (BurstA1_in) + 3 * (ThreshA1_in) +
		 3 * (BurstA1_out) + 3 * (ThreshA1_out) +
		 3 * (BurstA2_out) + 3 * (ThreshA2_out), PCI_BT_A);

	/* enable audio port pins */
	WRITEREG((EAP << 16) | EAP, MC1);

	/* enable I2C */
	WRITEREG((EI2C << 16) | EI2C, MC1);
	/* enable interrupts */
	WRITEREG(A1_out | A2_out | A1_in | IIC_S | IIC_E, IER);

	/* audio configuration */
	acon2 = A2_CLKSRC | BCLK1_OEN;
	WRITEREG(acon2, ACON2);

	/* By default use analog input */
	snd_aw2_saa7146_use_digital_input(chip, 0);

	/* TSL setup */
	for (i = 0; i < 8; ++i) {
		WRITEREG(tsl1[i], TSL1 + (i * 4));
		WRITEREG(tsl2[i], TSL2 + (i * 4));
	}

}

void snd_aw2_saa7146_pcm_init_playback(struct snd_aw2_saa7146 *chip,
				       int stream_number,
				       unsigned long dma_addr,
				       unsigned long period_size,
				       unsigned long buffer_size)
{
	unsigned long dw_page, dw_limit;

	/* Configure DMA for substream
	   Configuration informations: ALSA has allocated continuous memory
	   pages. So we don't need to use MMU of saa7146.
	 */

	/* No MMU -> nothing to do with PageA1, we only configure the limit of
	   PageAx_out register */
	/* Disable MMU */
	dw_page = (0L << 11);

	/* Configure Limit for DMA access.
	   The limit register defines an address limit, which generates
	   an interrupt if passed by the actual PCI address pointer.
	   '0001' means an interrupt will be generated if the lower
	   6 bits (64 bytes) of the PCI address are zero. '0010'
	   defines a limit of 128 bytes, '0011' one of 256 bytes, and
	   so on up to 1 Mbyte defined by '1111'. This interrupt range
	   can be calculated as follows:
	   Range = 2^(5 + Limit) bytes.
	 */
	dw_limit = snd_aw2_saa7146_get_limit(period_size);
	dw_page |= (dw_limit << 4);

	if (stream_number == 0) {
		WRITEREG(dw_page, PageA2_out);

		/* Base address for DMA transfert. */
		/* This address has been reserved by ALSA. */
		/* This is a physical address */
		WRITEREG(dma_addr, BaseA2_out);

		/* Define upper limit for DMA access */
		WRITEREG(dma_addr + buffer_size, ProtA2_out);

	} else if (stream_number == 1) {
		WRITEREG(dw_page, PageA1_out);

		/* Base address for DMA transfert. */
		/* This address has been reserved by ALSA. */
		/* This is a physical address */
		WRITEREG(dma_addr, BaseA1_out);

		/* Define upper limit for DMA access */
		WRITEREG(dma_addr + buffer_size, ProtA1_out);
	} else {
		pr_err("aw2: snd_aw2_saa7146_pcm_init_playback: "
		       "Substream number is not 0 or 1 -> not managed\n");
	}
}

void snd_aw2_saa7146_pcm_init_capture(struct snd_aw2_saa7146 *chip,
				      int stream_number, unsigned long dma_addr,
				      unsigned long period_size,
				      unsigned long buffer_size)
{
	unsigned long dw_page, dw_limit;

	/* Configure DMA for substream
	   Configuration informations: ALSA has allocated continuous memory
	   pages. So we don't need to use MMU of saa7146.
	 */

	/* No MMU -> nothing to do with PageA1, we only configure the limit of
	   PageAx_out register */
	/* Disable MMU */
	dw_page = (0L << 11);

	/* Configure Limit for DMA access.
	   The limit register defines an address limit, which generates
	   an interrupt if passed by the actual PCI address pointer.
	   '0001' means an interrupt will be generated if the lower
	   6 bits (64 bytes) of the PCI address are zero. '0010'
	   defines a limit of 128 bytes, '0011' one of 256 bytes, and
	   so on up to 1 Mbyte defined by '1111'. This interrupt range
	   can be calculated as follows:
	   Range = 2^(5 + Limit) bytes.
	 */
	dw_limit = snd_aw2_saa7146_get_limit(period_size);
	dw_page |= (dw_limit << 4);

	if (stream_number == 0) {
		WRITEREG(dw_page, PageA1_in);

		/* Base address for DMA transfert. */
		/* This address has been reserved by ALSA. */
		/* This is a physical address */
		WRITEREG(dma_addr, BaseA1_in);

		/* Define upper limit for DMA access  */
		WRITEREG(dma_addr + buffer_size, ProtA1_in);
	} else {
		pr_err("aw2: snd_aw2_saa7146_pcm_init_capture: "
		       "Substream number is not 0 -> not managed\n");
	}
}

void snd_aw2_saa7146_define_it_playback_callback(unsigned int stream_number,
						 snd_aw2_saa7146_it_cb
						 p_it_callback,
						 void *p_callback_param)
{
	if (stream_number < NB_STREAM_PLAYBACK) {
		arr_substream_it_playback_cb[stream_number].p_it_callback =
		    (snd_aw2_saa7146_it_cb) p_it_callback;
		arr_substream_it_playback_cb[stream_number].p_callback_param =
		    (void *)p_callback_param;
	}
}

void snd_aw2_saa7146_define_it_capture_callback(unsigned int stream_number,
						snd_aw2_saa7146_it_cb
						p_it_callback,
						void *p_callback_param)
{
	if (stream_number < NB_STREAM_CAPTURE) {
		arr_substream_it_capture_cb[stream_number].p_it_callback =
		    (snd_aw2_saa7146_it_cb) p_it_callback;
		arr_substream_it_capture_cb[stream_number].p_callback_param =
		    (void *)p_callback_param;
	}
}

void snd_aw2_saa7146_pcm_trigger_start_playback(struct snd_aw2_saa7146 *chip,
						int stream_number)
{
	unsigned int acon1 = 0;
	/* In aw8 driver, dma transfert is always active. It is
	   started and stopped in a larger "space" */
	acon1 = READREG(ACON1);
	if (stream_number == 0) {
		WRITEREG((TR_E_A2_OUT << 16) | TR_E_A2_OUT, MC1);

		/* WS2_CTRL, WS2_SYNC: output TSL2, I2S */
		acon1 |= 2 * WS2_CTRL;
		WRITEREG(acon1, ACON1);

	} else if (stream_number == 1) {
		WRITEREG((TR_E_A1_OUT << 16) | TR_E_A1_OUT, MC1);

		/* WS1_CTRL, WS1_SYNC: output TSL1, I2S */
		acon1 |= 1 * WS1_CTRL;
		WRITEREG(acon1, ACON1);
	}
}

void snd_aw2_saa7146_pcm_trigger_stop_playback(struct snd_aw2_saa7146 *chip,
					       int stream_number)
{
	unsigned int acon1 = 0;
	acon1 = READREG(ACON1);
	if (stream_number == 0) {
		/* WS2_CTRL, WS2_SYNC: output TSL2, I2S */
		acon1 &= ~(3 * WS2_CTRL);
		WRITEREG(acon1, ACON1);

		WRITEREG((TR_E_A2_OUT << 16), MC1);
	} else if (stream_number == 1) {
		/* WS1_CTRL, WS1_SYNC: output TSL1, I2S */
		acon1 &= ~(3 * WS1_CTRL);
		WRITEREG(acon1, ACON1);

		WRITEREG((TR_E_A1_OUT << 16), MC1);
	}
}

void snd_aw2_saa7146_pcm_trigger_start_capture(struct snd_aw2_saa7146 *chip,
					       int stream_number)
{
	/* In aw8 driver, dma transfert is always active. It is
	   started and stopped in a larger "space" */
	if (stream_number == 0)
		WRITEREG((TR_E_A1_IN << 16) | TR_E_A1_IN, MC1);
}

void snd_aw2_saa7146_pcm_trigger_stop_capture(struct snd_aw2_saa7146 *chip,
					      int stream_number)
{
	if (stream_number == 0)
		WRITEREG((TR_E_A1_IN << 16), MC1);
}

irqreturn_t snd_aw2_saa7146_interrupt(int irq, void *dev_id)
{
	unsigned int isr;
	unsigned int iicsta;
	struct snd_aw2_saa7146 *chip = dev_id;

	isr = READREG(ISR);
	if (!isr)
		return IRQ_NONE;

	WRITEREG(isr, ISR);

	if (isr & (IIC_S | IIC_E)) {
		iicsta = READREG(IICSTA);
		WRITEREG(0x100, IICSTA);
	}

	if (isr & A1_out) {
		if (arr_substream_it_playback_cb[1].p_it_callback != NULL) {
			arr_substream_it_playback_cb[1].
			    p_it_callback(arr_substream_it_playback_cb[1].
					  p_callback_param);
		}
	}
	if (isr & A2_out) {
		if (arr_substream_it_playback_cb[0].p_it_callback != NULL) {
			arr_substream_it_playback_cb[0].
			    p_it_callback(arr_substream_it_playback_cb[0].
					  p_callback_param);
		}

	}
	if (isr & A1_in) {
		if (arr_substream_it_capture_cb[0].p_it_callback != NULL) {
			arr_substream_it_capture_cb[0].
			    p_it_callback(arr_substream_it_capture_cb[0].
					  p_callback_param);
		}
	}
	return IRQ_HANDLED;
}

unsigned int snd_aw2_saa7146_get_hw_ptr_playback(struct snd_aw2_saa7146 *chip,
						 int stream_number,
						 unsigned char *start_addr,
						 unsigned int buffer_size)
{
	long pci_adp = 0;
	size_t ptr = 0;

	if (stream_number == 0) {
		pci_adp = READREG(PCI_ADP3);
		ptr = pci_adp - (long)start_addr;

		if (ptr == buffer_size)
			ptr = 0;
	}
	if (stream_number == 1) {
		pci_adp = READREG(PCI_ADP1);
		ptr = pci_adp - (size_t) start_addr;

		if (ptr == buffer_size)
			ptr = 0;
	}
	return ptr;
}

unsigned int snd_aw2_saa7146_get_hw_ptr_capture(struct snd_aw2_saa7146 *chip,
						int stream_number,
						unsigned char *start_addr,
						unsigned int buffer_size)
{
	size_t pci_adp = 0;
	size_t ptr = 0;
	if (stream_number == 0) {
		pci_adp = READREG(PCI_ADP2);
		ptr = pci_adp - (size_t) start_addr;

		if (ptr == buffer_size)
			ptr = 0;
	}
	return ptr;
}

void snd_aw2_saa7146_use_digital_input(struct snd_aw2_saa7146 *chip,
				       int use_digital)
{
	/* FIXME: switch between analog and digital input does not always work.
	   It can produce a kind of white noise. It seams that received data
	   are inverted sometime (endian inversion). Why ? I don't know, maybe
	   a problem of synchronization... However for the time being I have
	   not found the problem. Workaround: switch again (and again) between
	   digital and analog input until it works. */
	if (use_digital)
		WRITEREG(0x40, GPIO_CTRL);
	else
		WRITEREG(0x50, GPIO_CTRL);
}

int snd_aw2_saa7146_is_using_digital_input(struct snd_aw2_saa7146 *chip)
{
	unsigned int reg_val = READREG(GPIO_CTRL);
	if ((reg_val & 0xFF) == 0x40)
		return 1;
	else
		return 0;
}


static int snd_aw2_saa7146_get_limit(int size)
{
	int limitsize = 32;
	int limit = 0;
	while (limitsize < size) {
		limitsize *= 2;
		limit++;
	}
	return limit;
}
