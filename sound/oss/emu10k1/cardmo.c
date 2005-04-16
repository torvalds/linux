/*     
 **********************************************************************
 *     cardmo.c - MIDI UART output HAL for emu10k1 driver 
 *     Copyright 1999, 2000 Creative Labs, Inc. 
 * 
 ********************************************************************** 
 * 
 *     Date                 Author          Summary of changes 
 *     ----                 ------          ------------------ 
 *     October 20, 1999     Bertrand Lee    base code release 
 *     November 2, 1999     Alan Cox        cleaned up
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

#include <linux/slab.h>

#include "hwaccess.h"
#include "8010.h"
#include "cardmo.h"
#include "irqmgr.h"

/* Installs the IRQ handler for the MPU out port               *
 * and initialize parameters                                    */

int emu10k1_mpuout_open(struct emu10k1_card *card, struct midi_openinfo *openinfo)
{
	struct emu10k1_mpuout *card_mpuout = card->mpuout;

	DPF(2, "emu10k1_mpuout_open()\n");

	if (!(card_mpuout->status & FLAGS_AVAILABLE))
		return -1;

	/* Copy open info and mark channel as in use */
	card_mpuout->intr = 0;
	card_mpuout->openinfo = *openinfo;
	card_mpuout->status &= ~FLAGS_AVAILABLE;
	card_mpuout->laststatus = 0x80;
	card_mpuout->firstmidiq = NULL;
	card_mpuout->lastmidiq = NULL;

	emu10k1_mpu_reset(card);
	emu10k1_mpu_acquire(card);

	return 0;
}

int emu10k1_mpuout_close(struct emu10k1_card *card)
{
	struct emu10k1_mpuout *card_mpuout = card->mpuout;
	struct midi_queue *midiq;
	struct midi_hdr *midihdr;
	unsigned long flags;

	DPF(2, "emu10k1_mpuout_close()\n");

	emu10k1_irq_disable(card, card->is_audigy ? A_INTE_MIDITXENABLE : INTE_MIDITXENABLE);

	spin_lock_irqsave(&card_mpuout->lock, flags);

	while (card_mpuout->firstmidiq != NULL) {
		midiq = card_mpuout->firstmidiq;
		midihdr = (struct midi_hdr *) midiq->refdata;

		card_mpuout->firstmidiq = midiq->next;

		kfree(midihdr->data);
		kfree(midihdr);
		kfree(midiq);
	}

	card_mpuout->lastmidiq = NULL;

	emu10k1_mpu_release(card);

	card_mpuout->status |= FLAGS_AVAILABLE;

	spin_unlock_irqrestore(&card_mpuout->lock, flags);

	return 0;
}

/* If there isn't enough buffer space, reject Midi Buffer.     *
* Otherwise, disable TX, create object to hold Midi            *
*  uffer, update buffer flags and other parameters             *
* before enabling TX again.                                    */

int emu10k1_mpuout_add_buffer(struct emu10k1_card *card, struct midi_hdr *midihdr)
{
	struct emu10k1_mpuout *card_mpuout = card->mpuout;
	struct midi_queue *midiq;
	unsigned long flags;

	DPF(2, "emu10k1_mpuout_add_buffer()\n");

	if (card_mpuout->state == CARDMIDIOUT_STATE_SUSPEND)
		return 0;

	midihdr->flags |= MIDIBUF_INQUEUE;
	midihdr->flags &= ~MIDIBUF_DONE;

	if ((midiq = (struct midi_queue *) kmalloc(sizeof(struct midi_queue), GFP_KERNEL)) == NULL) {
		/* Message lost */
		return -1;
	}

	midiq->next = NULL;
	midiq->qtype = 1;
	midiq->length = midihdr->bufferlength;
	midiq->sizeLeft = midihdr->bufferlength;
	midiq->midibyte = midihdr->data;

	midiq->refdata = (unsigned long) midihdr;

	spin_lock_irqsave(&card_mpuout->lock, flags);

	if (card_mpuout->firstmidiq == NULL) {
		card_mpuout->firstmidiq = midiq;
		card_mpuout->lastmidiq = midiq;
	} else {
		(card_mpuout->lastmidiq)->next = midiq;
		card_mpuout->lastmidiq = midiq;
	}

	card_mpuout->intr = 0;

	emu10k1_irq_enable(card, card->is_audigy ? A_INTE_MIDITXENABLE : INTE_MIDITXENABLE);

	spin_unlock_irqrestore(&card_mpuout->lock, flags);

	return 0;
}

void emu10k1_mpuout_bh(unsigned long refdata)
{
	struct emu10k1_card *card = (struct emu10k1_card *) refdata;
	struct emu10k1_mpuout *card_mpuout = card->mpuout;
	int cByteSent = 0;
	struct midi_queue *midiq;
	struct midi_queue *doneq = NULL;
	unsigned long flags;

	spin_lock_irqsave(&card_mpuout->lock, flags);

	while (card_mpuout->firstmidiq != NULL) {
		midiq = card_mpuout->firstmidiq;

		while (cByteSent < 4 && midiq->sizeLeft) {
			if (emu10k1_mpu_write_data(card, *midiq->midibyte) < 0) {
				DPF(2, "emu10k1_mpuoutDpcCallback error!!\n");
			} else {
				++cByteSent;
				--midiq->sizeLeft;
				++midiq->midibyte;
			}
		}

		if (midiq->sizeLeft == 0) {
			if (doneq == NULL)
				doneq = midiq;
			card_mpuout->firstmidiq = midiq->next;
		} else
			break;
	}

	if (card_mpuout->firstmidiq == NULL)
		card_mpuout->lastmidiq = NULL;

	if (doneq != NULL) {
		while (doneq != card_mpuout->firstmidiq) {
			unsigned long callback_msg[3];

			midiq = doneq;
			doneq = midiq->next;

			if (midiq->qtype) {
				callback_msg[0] = 0;
				callback_msg[1] = midiq->length;
				callback_msg[2] = midiq->refdata;

				emu10k1_midi_callback(ICARDMIDI_OUTLONGDATA, card_mpuout->openinfo.refdata, callback_msg);
			} else if (((u8) midiq->refdata) < 0xF0 && ((u8) midiq->refdata) > 0x7F)
				card_mpuout->laststatus = (u8) midiq->refdata;

			kfree(midiq);
		}
	}

	if ((card_mpuout->firstmidiq != NULL) || cByteSent) {
		card_mpuout->intr = 0;
		emu10k1_irq_enable(card, card->is_audigy ? A_INTE_MIDITXENABLE : INTE_MIDITXENABLE);
	}

	spin_unlock_irqrestore(&card_mpuout->lock, flags);

	return;
}

int emu10k1_mpuout_irqhandler(struct emu10k1_card *card)
{
	struct emu10k1_mpuout *card_mpuout = card->mpuout;

	DPF(4, "emu10k1_mpuout_irqhandler\n");

	card_mpuout->intr = 1;
	emu10k1_irq_disable(card, card->is_audigy ? A_INTE_MIDITXENABLE : INTE_MIDITXENABLE);

	tasklet_hi_schedule(&card_mpuout->tasklet);

	return 0;
}
