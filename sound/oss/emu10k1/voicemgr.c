/*
 **********************************************************************
 *     voicemgr.c - Voice manager for emu10k1 driver
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

#include "voicemgr.h"
#include "8010.h"

#define PITCH_48000 0x00004000
#define PITCH_96000 0x00008000
#define PITCH_85000 0x00007155
#define PITCH_80726 0x00006ba2
#define PITCH_67882 0x00005a82
#define PITCH_57081 0x00004c1c

static u32 emu10k1_select_interprom(struct emu10k1_card *card,
				    struct emu_voice *voice)
{
	if(voice->pitch_target==PITCH_48000)
		return CCCA_INTERPROM_0;
	else if(voice->pitch_target<PITCH_48000)
		return CCCA_INTERPROM_1;
	else  if(voice->pitch_target>=PITCH_96000)
		return CCCA_INTERPROM_0;
	else  if(voice->pitch_target>=PITCH_85000)
		return CCCA_INTERPROM_6;
	else  if(voice->pitch_target>=PITCH_80726)
		return CCCA_INTERPROM_5;
	else  if(voice->pitch_target>=PITCH_67882)
		return CCCA_INTERPROM_4;
	else  if(voice->pitch_target>=PITCH_57081)
		return CCCA_INTERPROM_3;
	else  
		return CCCA_INTERPROM_2;
}


/**
 * emu10k1_voice_alloc_buffer -
 *
 * allocates the memory buffer for a voice. Two page tables are kept for each buffer.
 * One (dma_handle) keeps track of the host memory pages used and the other (virtualpagetable)
 * is passed to the device so that it can do DMA to host memory.
 *
 */
int emu10k1_voice_alloc_buffer(struct emu10k1_card *card, struct voice_mem *mem, u32 pages)
{
	u32 pageindex, pagecount;
	u32 busaddx;
	int i;

	DPD(2, "requested pages is: %d\n", pages);

	if ((mem->emupageindex = emu10k1_addxmgr_alloc(pages * PAGE_SIZE, card)) < 0)
	{
		DPF(1, "couldn't allocate emu10k1 address space\n");
		return -1;
	}

	/* Fill in virtual memory table */
	for (pagecount = 0; pagecount < pages; pagecount++) {
		if ((mem->addr[pagecount] = pci_alloc_consistent(card->pci_dev, PAGE_SIZE, &mem->dma_handle[pagecount]))
			== NULL) {
			mem->pages = pagecount;
			DPF(1, "couldn't allocate dma memory\n");
			return -1;
		}

		DPD(2, "Virtual Addx: %p\n", mem->addr[pagecount]);

		for (i = 0; i < PAGE_SIZE / EMUPAGESIZE; i++) {
			busaddx = (u32) mem->dma_handle[pagecount] + i * EMUPAGESIZE;

			DPD(3, "Bus Addx: %#x\n", busaddx);

			pageindex = mem->emupageindex + pagecount * PAGE_SIZE / EMUPAGESIZE + i;

			((u32 *) card->virtualpagetable.addr)[pageindex] = cpu_to_le32((busaddx * 2) | pageindex);
		}
	}

	mem->pages = pagecount;

	return 0;
}

/**
 * emu10k1_voice_free_buffer -
 *
 * frees the memory buffer for a voice.
 */
void emu10k1_voice_free_buffer(struct emu10k1_card *card, struct voice_mem *mem)
{
	u32 pagecount, pageindex;
	int i;

	if (mem->emupageindex < 0)
		return;

	for (pagecount = 0; pagecount < mem->pages; pagecount++) {
		pci_free_consistent(card->pci_dev, PAGE_SIZE,
					mem->addr[pagecount],
					mem->dma_handle[pagecount]);

		for (i = 0; i < PAGE_SIZE / EMUPAGESIZE; i++) {
			pageindex = mem->emupageindex + pagecount * PAGE_SIZE / EMUPAGESIZE + i;
			((u32 *) card->virtualpagetable.addr)[pageindex] =
				cpu_to_le32(((u32) card->silentpage.dma_handle * 2) | pageindex);
		}
	}

	emu10k1_addxmgr_free(card, mem->emupageindex);
	mem->emupageindex = -1;
}

int emu10k1_voice_alloc(struct emu10k1_card *card, struct emu_voice *voice)
{
	u8 *voicetable = card->voicetable;
	int i;
	unsigned long flags;

	DPF(2, "emu10k1_voice_alloc()\n");

	spin_lock_irqsave(&card->lock, flags);

	if (voice->flags & VOICE_FLAGS_STEREO) {
		for (i = 0; i < NUM_G; i += 2)
			if ((voicetable[i] == VOICE_USAGE_FREE) && (voicetable[i + 1] == VOICE_USAGE_FREE)) {
				voicetable[i] = voice->usage;
				voicetable[i + 1] = voice->usage;
				break;
			}
	} else {
		for (i = 0; i < NUM_G; i++)
			if (voicetable[i] == VOICE_USAGE_FREE) {
				voicetable[i] = voice->usage;
				break;
			}
	}

	spin_unlock_irqrestore(&card->lock, flags);

	if (i >= NUM_G)
		return -1;

	voice->card = card;
	voice->num = i;

	for (i = 0; i < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); i++) {
		DPD(2, " voice allocated -> %d\n", voice->num + i);

		sblive_writeptr_tag(card, voice->num + i, IFATN, 0xffff,
							DCYSUSV, 0,
							VTFT, 0x0000ffff,
							PTRX, 0,
							TAGLIST_END);
	}

	return 0;
}

void emu10k1_voice_free(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;
	int i;
	unsigned long flags;

	DPF(2, "emu10k1_voice_free()\n");

	if (voice->usage == VOICE_USAGE_FREE)
		return;

	for (i = 0; i < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); i++) {
		DPD(2, " voice released -> %d\n", voice->num + i);

		sblive_writeptr_tag(card, voice->num + i, DCYSUSV, 0, 
							VTFT, 0x0000ffff,
							PTRX_PITCHTARGET, 0,
							CVCF, 0x0000ffff,
							//CPF, 0,
							TAGLIST_END);
		
		sblive_writeptr(card, CPF, voice->num + i, 0);
	}

	voice->usage = VOICE_USAGE_FREE;

	spin_lock_irqsave(&card->lock, flags);

	card->voicetable[voice->num] = VOICE_USAGE_FREE;

	if (voice->flags & VOICE_FLAGS_STEREO)
		card->voicetable[voice->num + 1] = VOICE_USAGE_FREE;

	spin_unlock_irqrestore(&card->lock, flags);
}

void emu10k1_voice_playback_setup(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;
	u32 start;
	int i;

	DPF(2, "emu10k1_voice_playback_setup()\n");

	if (voice->flags & VOICE_FLAGS_STEREO) {
		/* Set stereo bit */
		start = 28;
		sblive_writeptr(card, CPF, voice->num, CPF_STEREO_MASK);
		sblive_writeptr(card, CPF, voice->num + 1, CPF_STEREO_MASK);
	} else {
		start = 30;
		sblive_writeptr(card, CPF, voice->num, 0);
	}

	if(!(voice->flags & VOICE_FLAGS_16BIT))
		start *= 2;

	voice->start += start;

	for (i = 0; i < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); i++) {
		if (card->is_audigy) {
			sblive_writeptr(card, A_FXRT1, voice->num + i, voice->params[i].send_routing);
			sblive_writeptr(card, A_FXRT2, voice->num + i, voice->params[i].send_routing2);
			sblive_writeptr(card,  A_SENDAMOUNTS, voice->num + i, voice->params[i].send_hgfe);
		} else {
			sblive_writeptr(card, FXRT, voice->num + i, voice->params[i].send_routing << 16);
		}

		/* Stop CA */
		/* Assumption that PT is already 0 so no harm overwriting */
		sblive_writeptr(card, PTRX, voice->num + i, ((voice->params[i].send_dcba & 0xff) << 8)
				| ((voice->params[i].send_dcba & 0xff00) >> 8));

		sblive_writeptr_tag(card, voice->num + i,
				/* CSL, ST, CA */
				    DSL, voice->endloop | (voice->params[i].send_dcba & 0xff000000),
				    PSST, voice->startloop | ((voice->params[i].send_dcba & 0x00ff0000) << 8),
				    CCCA, (voice->start) |  emu10k1_select_interprom(card,voice) |
				        ((voice->flags & VOICE_FLAGS_16BIT) ? 0 : CCCA_8BITSELECT),
				    /* Clear filter delay memory */
				    Z1, 0,
				    Z2, 0,
				    /* Invalidate maps */
				    MAPA, MAP_PTI_MASK | ((u32) card->silentpage.dma_handle * 2),
				    MAPB, MAP_PTI_MASK | ((u32) card->silentpage.dma_handle * 2),
				/* modulation envelope */
				    CVCF, 0x0000ffff,
				    VTFT, 0x0000ffff,
				    ATKHLDM, 0,
				    DCYSUSM, 0x007f,
				    LFOVAL1, 0x8000,
				    LFOVAL2, 0x8000,
				    FMMOD, 0,
				    TREMFRQ, 0,
				    FM2FRQ2, 0,
				    ENVVAL, 0x8000,
				/* volume envelope */
				    ATKHLDV, 0x7f7f,
				    ENVVOL, 0x8000,
				/* filter envelope */
				    PEFE_FILTERAMOUNT, 0x7f,
				/* pitch envelope */
				    PEFE_PITCHAMOUNT, 0, TAGLIST_END);

		voice->params[i].fc_target = 0xffff;
	}
}

void emu10k1_voices_start(struct emu_voice *first_voice, unsigned int num_voices, int set)
{
	struct emu10k1_card *card = first_voice->card;
	struct emu_voice *voice;
	unsigned int voicenum;
	int j;

	DPF(2, "emu10k1_voices_start()\n");

	for (voicenum = 0; voicenum < num_voices; voicenum++)
	{
		voice = first_voice + voicenum;

		if (!set) {
			u32 cra, ccis, cs, sample;
			if (voice->flags & VOICE_FLAGS_STEREO) {
				cra = 64;
				ccis = 28;
				cs = 4;
			} else {
				cra = 64;
				ccis = 30;
				cs = 2;
			}

			if(voice->flags & VOICE_FLAGS_16BIT) {
				sample = 0x00000000;
			} else {
				sample = 0x80808080;		
				ccis *= 2;
			}

			for(j = 0; j < cs; j++)
	        	        sblive_writeptr(card, CD0 + j, voice->num, sample);

			/* Reset cache */
			sblive_writeptr(card, CCR_CACHEINVALIDSIZE, voice->num, 0);
			if (voice->flags & VOICE_FLAGS_STEREO)
				sblive_writeptr(card, CCR_CACHEINVALIDSIZE, voice->num + 1, 0);

			sblive_writeptr(card, CCR_READADDRESS, voice->num, cra);

			if (voice->flags & VOICE_FLAGS_STEREO)
				sblive_writeptr(card, CCR_READADDRESS, voice->num + 1, cra);

			/* Fill cache */
			sblive_writeptr(card, CCR_CACHEINVALIDSIZE, voice->num, ccis);
		}

		for (j = 0; j < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); j++) {
			sblive_writeptr_tag(card, voice->num + j,
				    IFATN, (voice->params[j].initial_fc << 8) | voice->params[j].initial_attn,
				    VTFT, (voice->params[j].volume_target << 16) | voice->params[j].fc_target,
				    CVCF, (voice->params[j].volume_target << 16) | voice->params[j].fc_target,
				    DCYSUSV, (voice->params[j].byampl_env_sustain << 8) | voice->params[j].byampl_env_decay,
				    TAGLIST_END);
	
			emu10k1_clear_stop_on_loop(card, voice->num + j);
		}
	}


        for (voicenum = 0; voicenum < num_voices; voicenum++)
	{
		voice = first_voice + voicenum;

		for (j = 0; j < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); j++) {
			sblive_writeptr(card, PTRX_PITCHTARGET, voice->num + j, voice->pitch_target);

			if (j == 0)
				sblive_writeptr(card, CPF_CURRENTPITCH, voice->num, voice->pitch_target);

			sblive_writeptr(card, IP, voice->num + j, voice->initial_pitch);
		}
	}
}

void emu10k1_voices_stop(struct emu_voice *first_voice, int num_voices)
{
	struct emu10k1_card *card = first_voice->card;
	struct emu_voice *voice;
	unsigned int voice_num;
	int j;

	DPF(2, "emu10k1_voice_stop()\n");

        for (voice_num = 0; voice_num < num_voices; voice_num++)
	{
		voice = first_voice + voice_num;

		for (j = 0; j < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); j++) {
			sblive_writeptr_tag(card, voice->num + j,
						PTRX_PITCHTARGET, 0,
						CPF_CURRENTPITCH, 0,
						IFATN, 0xffff,
						VTFT, 0x0000ffff,
						CVCF, 0x0000ffff,
						IP, 0,
						TAGLIST_END);
		}
	}
}

