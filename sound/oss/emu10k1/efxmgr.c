/*
 **********************************************************************
 *     efxmgr.c
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

#include <linux/bitops.h>
#include "hwaccess.h"
#include "efxmgr.h"

int emu10k1_find_control_gpr(struct patch_manager *mgr, const char *patch_name, const char *gpr_name)
{
        struct dsp_patch *patch;
	struct dsp_rpatch *rpatch;
	char s[PATCH_NAME_SIZE + 4];
	unsigned long *gpr_used;
	int i;

	DPD(2, "emu10k1_find_control_gpr(): %s %s\n", patch_name, gpr_name);

	rpatch = &mgr->rpatch;
	if (!strcmp(rpatch->name, patch_name)) {
		gpr_used = rpatch->gpr_used;
		goto match;
	}

	for (i = 0; i < mgr->current_pages * PATCHES_PER_PAGE; i++) {
		patch = PATCH(mgr, i);
			sprintf(s,"%s", patch->name);

		if (!strcmp(s, patch_name)) {
			gpr_used = patch->gpr_used;
			goto match;
		}
	}

	return -1;

  match:
	for (i = 0; i < NUM_GPRS; i++)
		if (mgr->gpr[i].type == GPR_TYPE_CONTROL &&
		    test_bit(i, gpr_used) &&
		    !strcmp(mgr->gpr[i].name, gpr_name))
			return i;

	return -1;
}

void emu10k1_set_control_gpr(struct emu10k1_card *card, int addr, s32 val, int flag)
{
	struct patch_manager *mgr = &card->mgr;

	DPD(2, "emu10k1_set_control_gpr(): %d %x\n", addr, val);

	if (addr < 0 || addr >= NUM_GPRS)
		return;

	//fixme: once patch manager is up, remember to fix this for the audigy
	if (card->is_audigy) {
		sblive_writeptr(card, A_GPR_BASE + addr, 0, val);
	} else {
		if (flag)
			val += sblive_readptr(card, GPR_BASE + addr, 0);
		if (val > mgr->gpr[addr].max)
			val = mgr->gpr[addr].max;
		else if (val < mgr->gpr[addr].min)
			val = mgr->gpr[addr].min;
		sblive_writeptr(card, GPR_BASE + addr, 0, val);
	}
	
	
}

//TODO: make this configurable:
#define VOLCTRL_CHANNEL SOUND_MIXER_VOLUME
#define VOLCTRL_STEP_SIZE        5

//An internal function for setting OSS mixer controls.
static void emu10k1_set_oss_vol(struct emu10k1_card *card, int oss_mixer,
				unsigned int left, unsigned int right)
{
	extern char volume_params[SOUND_MIXER_NRDEVICES];

	card->ac97->mixer_state[oss_mixer] = (right << 8) | left;

	if (!card->is_aps)
		card->ac97->write_mixer(card->ac97, oss_mixer, left, right);
	
	emu10k1_set_volume_gpr(card, card->mgr.ctrl_gpr[oss_mixer][0], left,
			       volume_params[oss_mixer]);

	emu10k1_set_volume_gpr(card, card->mgr.ctrl_gpr[oss_mixer][1], right,
			       volume_params[oss_mixer]);
}

//FIXME: mute should unmute when pressed a second time
void emu10k1_mute_irqhandler(struct emu10k1_card *card)
{
	int oss_channel = VOLCTRL_CHANNEL;
	int left, right;
	static int val;

	if (val) {
		left = val & 0xff;
		right = (val >> 8) & 0xff;
		val = 0;
	} else {
		val = card->ac97->mixer_state[oss_channel];
		left = 0;
		right = 0;
	}

	emu10k1_set_oss_vol(card, oss_channel, left, right);
}

void emu10k1_volincr_irqhandler(struct emu10k1_card *card)
{
	int oss_channel = VOLCTRL_CHANNEL;
	int left, right;

	left = card->ac97->mixer_state[oss_channel] & 0xff;
	right = (card->ac97->mixer_state[oss_channel] >> 8) & 0xff;

	if ((left += VOLCTRL_STEP_SIZE) > 100)
		left = 100;

	if ((right += VOLCTRL_STEP_SIZE) > 100)
		right = 100;

	emu10k1_set_oss_vol(card, oss_channel, left, right);
}

void emu10k1_voldecr_irqhandler(struct emu10k1_card *card)
{
	int oss_channel = VOLCTRL_CHANNEL;
	int left, right;

	left = card->ac97->mixer_state[oss_channel] & 0xff;
	right = (card->ac97->mixer_state[oss_channel] >> 8) & 0xff;

	if ((left -= VOLCTRL_STEP_SIZE) < 0)
		left = 0;

	if ((right -= VOLCTRL_STEP_SIZE) < 0)
		right = 0;

	emu10k1_set_oss_vol(card, oss_channel, left, right);
}

void emu10k1_set_volume_gpr(struct emu10k1_card *card, int addr, s32 vol, int scale)
{
	struct patch_manager *mgr = &card->mgr;
	unsigned long flags;

	static const s32 log2lin[4] ={           //  attenuation (dB)
		0x7fffffff,                      //       0.0         
		0x7fffffff * 0.840896415253715 , //       1.5          
		0x7fffffff * 0.707106781186548,  //       3.0
		0x7fffffff * 0.594603557501361 , //       4.5
	};

	if (addr < 0)
		return;

	vol = (100 - vol ) * scale / 100;

	// Thanks to the comp.dsp newsgroup for this neat trick:
	vol = (vol >= scale) ? 0 : (log2lin[vol & 3] >> (vol >> 2));

	spin_lock_irqsave(&mgr->lock, flags);
	emu10k1_set_control_gpr(card, addr, vol, 0);
	spin_unlock_irqrestore(&mgr->lock, flags);
}

void emu10k1_dsp_irqhandler(struct emu10k1_card *card)
{
	unsigned long flags;

	if (card->pt.state != PT_STATE_INACTIVE) {
		u32 bc;
		bc = sblive_readptr(card, GPR_BASE + card->pt.intr_gpr, 0);
		if (bc != 0) {
			DPD(3, "pt interrupt, bc = %d\n", bc);
			spin_lock_irqsave(&card->pt.lock, flags);
			card->pt.blocks_played = bc;
			if (card->pt.blocks_played >= card->pt.blocks_copied) {
				DPF(1, "buffer underrun in passthrough playback\n");
				emu10k1_pt_stop(card);
			}
			wake_up_interruptible(&card->pt.wait);
			spin_unlock_irqrestore(&card->pt.lock, flags);
		}
	}
}

