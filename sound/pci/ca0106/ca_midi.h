/* 
 *  Copyright 10/16/2005 Tilman Kranz <tilde@tk-sls.de>
 *  Creative Audio MIDI, for the CA0106 Driver
 *  Version: 0.0.1
 *
 *  Changelog:
 *    See ca_midi.c
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include<linux/spinlock.h>
#include<sound/rawmidi.h>
#include<sound/mpu401.h>

#define CA_MIDI_MODE_INPUT	MPU401_MODE_INPUT
#define CA_MIDI_MODE_OUTPUT	MPU401_MODE_OUTPUT

typedef struct ca_midi ca_midi_t;
struct ca_midi {

	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *substream_input;
	snd_rawmidi_substream_t *substream_output;

	void *dev_id;

	spinlock_t input_lock;
	spinlock_t output_lock;
	spinlock_t open_lock;

	unsigned int channel;

	unsigned int midi_mode;
	int port;
	int tx_enable, rx_enable;
	int ipr_tx, ipr_rx;            
	
	int input_avail, output_ready;
	int ack, reset, enter_uart;

	void (*interrupt)(ca_midi_t *midi, unsigned int status);
	void (*interrupt_enable)(ca_midi_t *midi, int intr);
	void (*interrupt_disable)(ca_midi_t *midi, int intr);

	unsigned char (*read)(ca_midi_t *midi, int idx);
	void (*write)(ca_midi_t *midi, int data, int idx);

	/* get info from dev_id */
	snd_card_t *(*get_dev_id_card)(void *dev_id);
	int (*get_dev_id_port)(void *dev_id);
};

int __devinit ca_midi_init(void *card, ca_midi_t *midi, int device, char *name);


