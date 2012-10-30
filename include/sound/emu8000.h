#ifndef __SOUND_EMU8000_H
#define __SOUND_EMU8000_H
/*
 *  Defines for the emu8000 (AWE32/64)
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *  Copyright (C) 1999-2000 Takashi Iwai <tiwai@suse.de>
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
 */

#include <sound/emux_synth.h>
#include <sound/seq_kernel.h>

/*
 * Hardware parameters.
 */
#define EMU8000_MAX_DRAM (28 * 1024 * 1024) /* Max on-board mem is 28Mb ???*/
#define EMU8000_DRAM_OFFSET 0x200000	/* Beginning of on board ram */
#define EMU8000_CHANNELS   32	/* Number of hardware channels */
#define EMU8000_DRAM_VOICES	30	/* number of normal voices */

/* Flags to set a dma channel to read or write */
#define EMU8000_RAM_READ   0
#define EMU8000_RAM_WRITE  1
#define EMU8000_RAM_CLOSE  2
#define EMU8000_RAM_MODE_MASK	0x03
#define EMU8000_RAM_RIGHT	0x10	/* use 'right' DMA channel */

enum {
	EMU8000_CONTROL_BASS = 0,
	EMU8000_CONTROL_TREBLE,
	EMU8000_CONTROL_CHORUS_MODE,
	EMU8000_CONTROL_REVERB_MODE,
	EMU8000_CONTROL_FM_CHORUS_DEPTH,
	EMU8000_CONTROL_FM_REVERB_DEPTH,
	EMU8000_NUM_CONTROLS,
};

/*
 * Structure to hold all state information for the emu8000 driver.
 *
 * Note 1: The chip supports 32 channels in hardware this is max_channels
 * some of the channels may be used for other things so max_channels is
 * the number in use for wave voices.
 */
struct snd_emu8000 {

	struct snd_emux *emu;

	int index;		/* sequencer client index */
	int seq_ports;		/* number of sequencer ports */
	int fm_chorus_depth;	/* FM OPL3 chorus depth */
	int fm_reverb_depth;	/* FM OPL3 reverb depth */

	int mem_size;		/* memory size */
	unsigned long port1;	/* Port usually base+0 */
	unsigned long port2;	/* Port usually at base+0x400 */
	unsigned long port3;	/* Port usually at base+0x800 */
	struct resource *res_port1;
	struct resource *res_port2;
	struct resource *res_port3;
	unsigned short last_reg;/* Last register command */
	spinlock_t reg_lock;

	int dram_checked;

	struct snd_card *card;		/* The card that this belongs to */

	int chorus_mode;
	int reverb_mode;
	int bass_level;
	int treble_level;

	struct snd_util_memhdr *memhdr;

	spinlock_t control_lock;
	struct snd_kcontrol *controls[EMU8000_NUM_CONTROLS];

	struct snd_pcm *pcm; /* pcm on emu8000 wavetable */

};

/* sequencer device id */
#define SNDRV_SEQ_DEV_ID_EMU8000	"emu8000-synth"


/* exported functions */
int snd_emu8000_new(struct snd_card *card, int device, long port, int seq_ports,
		    struct snd_seq_device **ret);
void snd_emu8000_poke(struct snd_emu8000 *emu, unsigned int port, unsigned int reg,
		      unsigned int val);
unsigned short snd_emu8000_peek(struct snd_emu8000 *emu, unsigned int port,
				unsigned int reg);
void snd_emu8000_poke_dw(struct snd_emu8000 *emu, unsigned int port, unsigned int reg,
			 unsigned int val);
unsigned int snd_emu8000_peek_dw(struct snd_emu8000 *emu, unsigned int port,
				 unsigned int reg);
void snd_emu8000_dma_chan(struct snd_emu8000 *emu, int ch, int mode);

void snd_emu8000_init_fm(struct snd_emu8000 *emu);

void snd_emu8000_update_chorus_mode(struct snd_emu8000 *emu);
void snd_emu8000_update_reverb_mode(struct snd_emu8000 *emu);
void snd_emu8000_update_equalizer(struct snd_emu8000 *emu);
int snd_emu8000_load_chorus_fx(struct snd_emu8000 *emu, int mode, const void __user *buf, long len);
int snd_emu8000_load_reverb_fx(struct snd_emu8000 *emu, int mode, const void __user *buf, long len);

#endif /* __SOUND_EMU8000_H */
