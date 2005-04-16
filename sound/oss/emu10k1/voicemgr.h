/*
 **********************************************************************
 *     sblive_voice.h -- EMU Voice Resource Manager header file
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#ifndef _VOICEMGR_H
#define _VOICEMGR_H

#include "hwaccess.h"

/* struct emu_voice.usage flags */
#define VOICE_USAGE_FREE		0x01
#define VOICE_USAGE_MIDI		0x02
#define VOICE_USAGE_PLAYBACK		0x04

/* struct emu_voice.flags flags */
#define VOICE_FLAGS_STEREO		0x02
#define VOICE_FLAGS_16BIT		0x04

struct voice_param
{
	/* FX bus amount send */

	u32 send_routing;
	// audigy only:
	u32 send_routing2;

	u32 send_dcba;
	// audigy only:
	u32 send_hgfe;


	u32 initial_fc;
	u32 fc_target;

	u32 initial_attn;
	u32 volume_target;

	u32 byampl_env_sustain;
	u32 byampl_env_decay;
};

struct voice_mem {
	int emupageindex;
	void *addr[BUFMAXPAGES];
	dma_addr_t dma_handle[BUFMAXPAGES];
	u32 pages;
};

struct emu_voice
{
	struct emu10k1_card *card;
	u8 usage;		/* Free, MIDI, playback */
	u8 num;			/* Voice ID */
	u8 flags;		/* Stereo/mono, 8/16 bit */

	u32 startloop;
	u32 endloop;
	u32 start;

	u32 initial_pitch;
	u32 pitch_target;

	struct voice_param params[2];

	struct voice_mem mem;
};

int emu10k1_voice_alloc_buffer(struct emu10k1_card *, struct voice_mem *, u32);
void emu10k1_voice_free_buffer(struct emu10k1_card *, struct voice_mem *);
int emu10k1_voice_alloc(struct emu10k1_card *, struct emu_voice *);
void emu10k1_voice_free(struct emu_voice *);
void emu10k1_voice_playback_setup(struct emu_voice *);
void emu10k1_voices_start(struct emu_voice *, unsigned int, int);
void emu10k1_voices_stop(struct emu_voice *, int);

#endif /* _VOICEMGR_H */
