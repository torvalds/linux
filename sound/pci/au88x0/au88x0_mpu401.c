// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for control of MPU-401 in UART mode
 *
 *   Modified for the Aureal Vortex based Soundcards
 *   by Manuel Jander (mjande@embedded.cl).
 */

#include <linux/time.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/mpu401.h>
#include "au88x0.h"

/* Check for mpu401 mmio support. */
/* MPU401 legacy support is only provided as a emergency fallback *
 * for older versions of ALSA. Its usage is strongly discouraged. */
#ifndef MPU401_HW_AUREAL
#define VORTEX_MPU401_LEGACY
#endif

/* Vortex MPU401 defines. */
#define MIDI_CLOCK_DIV      0x61
/* Standart MPU401 defines. */
#define MPU401_RESET		0xff
#define MPU401_ENTER_UART	0x3f
#define MPU401_ACK		    0xfe

static int snd_vortex_midi(vortex_t *vortex)
{
	struct snd_rawmidi *rmidi;
	int temp, mode;
	struct snd_mpu401 *mpu;
	unsigned long port;

#ifdef VORTEX_MPU401_LEGACY
	/* EnableHardCodedMPU401Port() */
	/* Enable Legacy MIDI Interface port. */
	port = (0x03 << 5);	/* FIXME: static address. 0x330 */
	temp =
	    (hwread(vortex->mmio, VORTEX_CTRL) & ~CTRL_MIDI_PORT) |
	    CTRL_MIDI_EN | port;
	hwwrite(vortex->mmio, VORTEX_CTRL, temp);
#else
	/* Disable Legacy MIDI Interface port. */
	temp =
	    (hwread(vortex->mmio, VORTEX_CTRL) & ~CTRL_MIDI_PORT) &
	    ~CTRL_MIDI_EN;
	hwwrite(vortex->mmio, VORTEX_CTRL, temp);
#endif
	/* Mpu401UartInit() */
	mode = 1;
	temp = hwread(vortex->mmio, VORTEX_CTRL2) & 0xffff00cf;
	temp |= (MIDI_CLOCK_DIV << 8) | ((mode >> 24) & 0xff) << 4;
	hwwrite(vortex->mmio, VORTEX_CTRL2, temp);
	hwwrite(vortex->mmio, VORTEX_MIDI_CMD, MPU401_RESET);

	/* Check if anything is OK. */
	temp = hwread(vortex->mmio, VORTEX_MIDI_DATA);
	if (temp != MPU401_ACK /*0xfe */ ) {
		dev_err(vortex->card->dev, "midi port doesn't acknowledge!\n");
		return -ENODEV;
	}
	/* Enable MPU401 interrupts. */
	hwwrite(vortex->mmio, VORTEX_IRQ_CTRL,
		hwread(vortex->mmio, VORTEX_IRQ_CTRL) | IRQ_MIDI);

	/* Create MPU401 instance. */
#ifdef VORTEX_MPU401_LEGACY
	if ((temp =
	     snd_mpu401_uart_new(vortex->card, 0, MPU401_HW_MPU401, 0x330,
				 MPU401_INFO_IRQ_HOOK, -1, &rmidi)) != 0) {
		hwwrite(vortex->mmio, VORTEX_CTRL,
			(hwread(vortex->mmio, VORTEX_CTRL) &
			 ~CTRL_MIDI_PORT) & ~CTRL_MIDI_EN);
		return temp;
	}
#else
	port = (unsigned long)(vortex->mmio + VORTEX_MIDI_DATA);
	if ((temp =
	     snd_mpu401_uart_new(vortex->card, 0, MPU401_HW_AUREAL, port,
				 MPU401_INFO_INTEGRATED | MPU401_INFO_MMIO |
				 MPU401_INFO_IRQ_HOOK, -1, &rmidi)) != 0) {
		hwwrite(vortex->mmio, VORTEX_CTRL,
			(hwread(vortex->mmio, VORTEX_CTRL) &
			 ~CTRL_MIDI_PORT) & ~CTRL_MIDI_EN);
		return temp;
	}
	mpu = rmidi->private_data;
	mpu->cport = (unsigned long)(vortex->mmio + VORTEX_MIDI_CMD);
#endif
	/* Overwrite MIDI name */
	snprintf(rmidi->name, sizeof(rmidi->name), "%s MIDI %d", CARD_NAME_SHORT , vortex->card->number);

	vortex->rmidi = rmidi;
	return 0;
}
