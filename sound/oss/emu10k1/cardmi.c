/*
 **********************************************************************
 *     sblive_mi.c - MIDI UART input HAL for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        clean up
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
#include <linux/jiffies.h>

#include "hwaccess.h"
#include "8010.h"
#include "cardmi.h"
#include "irqmgr.h"


static int emu10k1_mpuin_callback(struct emu10k1_mpuin *card_mpuin, u32 msg, unsigned long data, u32 bytesvalid);

static int sblive_miStateInit(struct emu10k1_mpuin *);
static int sblive_miStateEntry(struct emu10k1_mpuin *, u8);
static int sblive_miStateParse(struct emu10k1_mpuin *, u8);
static int sblive_miState3Byte(struct emu10k1_mpuin *, u8);
static int sblive_miState3ByteKey(struct emu10k1_mpuin *, u8);
static int sblive_miState3ByteVel(struct emu10k1_mpuin *, u8);
static int sblive_miState2Byte(struct emu10k1_mpuin *, u8);
static int sblive_miState2ByteKey(struct emu10k1_mpuin *, u8);
static int sblive_miStateSysCommon2(struct emu10k1_mpuin *, u8);
static int sblive_miStateSysCommon2Key(struct emu10k1_mpuin *, u8);
static int sblive_miStateSysCommon3(struct emu10k1_mpuin *, u8);
static int sblive_miStateSysCommon3Key(struct emu10k1_mpuin *, u8);
static int sblive_miStateSysCommon3Vel(struct emu10k1_mpuin *, u8);
static int sblive_miStateSysExNorm(struct emu10k1_mpuin *, u8);
static int sblive_miStateSysReal(struct emu10k1_mpuin *, u8);


static struct {
	int (*Fn) (struct emu10k1_mpuin *, u8);
} midistatefn[] = {

	{
	sblive_miStateParse}, {
	sblive_miState3Byte},	/* 0x8n, 0x9n, 0xAn, 0xBn, 0xEn */
	{
	sblive_miState3ByteKey},	/* Byte 1                       */
	{
	sblive_miState3ByteVel},	/* Byte 2                       */
	{
	sblive_miState2Byte},	/* 0xCn, 0xDn                   */
	{
	sblive_miState2ByteKey},	/* Byte 1                       */
	{
	sblive_miStateSysCommon2},	/* 0xF1 , 0xF3                  */
	{
	sblive_miStateSysCommon2Key},	/* 0xF1 , 0xF3, Byte 1          */
	{
	sblive_miStateSysCommon3},	/* 0xF2                         */
	{
	sblive_miStateSysCommon3Key},	/* 0xF2 , Byte 1                */
	{
	sblive_miStateSysCommon3Vel},	/* 0xF2 , Byte 2                */
	{
	sblive_miStateSysExNorm},	/* 0xF0, 0xF7, Normal mode      */
	{
	sblive_miStateSysReal}	/* 0xF4 - 0xF6 ,0xF8 - 0xFF     */
};


/* Installs the IRQ handler for the MPU in port                 */

/* and initialize parameters                                    */

int emu10k1_mpuin_open(struct emu10k1_card *card, struct midi_openinfo *openinfo)
{
	struct emu10k1_mpuin *card_mpuin = card->mpuin;

	DPF(2, "emu10k1_mpuin_open\n");

	if (!(card_mpuin->status & FLAGS_AVAILABLE))
		return -1;

	/* Copy open info and mark channel as in use */
	card_mpuin->openinfo = *openinfo;
	card_mpuin->status &= ~FLAGS_AVAILABLE;	/* clear */
	card_mpuin->status |= FLAGS_READY;	/* set */
	card_mpuin->status &= ~FLAGS_MIDM_STARTED;	/* clear */
	card_mpuin->firstmidiq = NULL;
	card_mpuin->lastmidiq = NULL;
	card_mpuin->qhead = 0;
	card_mpuin->qtail = 0;

	sblive_miStateInit(card_mpuin);

	emu10k1_mpu_reset(card);
	emu10k1_mpu_acquire(card);

	return 0;
}

int emu10k1_mpuin_close(struct emu10k1_card *card)
{
	struct emu10k1_mpuin *card_mpuin = card->mpuin;

	DPF(2, "emu10k1_mpuin_close()\n");

	/* Check if there are pending input SysEx buffers */
	if (card_mpuin->firstmidiq != NULL) {
		ERROR();
		return -1;
	}

	/* Disable RX interrupt */
	emu10k1_irq_disable(card, card->is_audigy ? A_INTE_MIDIRXENABLE : INTE_MIDIRXENABLE);

	emu10k1_mpu_release(card);

	card_mpuin->status |= FLAGS_AVAILABLE;	/* set */
	card_mpuin->status &= ~FLAGS_MIDM_STARTED;	/* clear */

	return 0;
}

/* Adds MIDI buffer to local queue list                         */

int emu10k1_mpuin_add_buffer(struct emu10k1_mpuin *card_mpuin, struct midi_hdr *midihdr)
{
	struct midi_queue *midiq;
	unsigned long flags;

	DPF(2, "emu10k1_mpuin_add_buffer()\n");

	/* Update MIDI buffer flags */
	midihdr->flags |= MIDIBUF_INQUEUE;	/* set */
	midihdr->flags &= ~MIDIBUF_DONE;	/* clear */

	if ((midiq = (struct midi_queue *) kmalloc(sizeof(struct midi_queue), GFP_ATOMIC)) == NULL) {
		/* Message lost */
		return -1;
	}

	midiq->next = NULL;
	midiq->qtype = 1;
	midiq->length = midihdr->bufferlength;
	midiq->sizeLeft = midihdr->bufferlength;
	midiq->midibyte = midihdr->data;
	midiq->refdata = (unsigned long) midihdr;

	spin_lock_irqsave(&card_mpuin->lock, flags);

	if (card_mpuin->firstmidiq == NULL) {
		card_mpuin->firstmidiq = midiq;
		card_mpuin->lastmidiq = midiq;
	} else {
		(card_mpuin->lastmidiq)->next = midiq;
		card_mpuin->lastmidiq = midiq;
	}

	spin_unlock_irqrestore(&card_mpuin->lock, flags);

	return 0;
}

/* First set the Time Stamp if MIDI IN has not started.         */

/* Then enable RX Irq.                                          */

int emu10k1_mpuin_start(struct emu10k1_card *card)
{
	struct emu10k1_mpuin *card_mpuin = card->mpuin;
	u8 dummy;

	DPF(2, "emu10k1_mpuin_start()\n");

	/* Set timestamp if not set */
	if (card_mpuin->status & FLAGS_MIDM_STARTED) {
		DPF(2, "Time Stamp not changed\n");
	} else {
		while (!emu10k1_mpu_read_data(card, &dummy));

		card_mpuin->status |= FLAGS_MIDM_STARTED;	/* set */

		/* Set new time stamp */
		card_mpuin->timestart = (jiffies * 1000) / HZ;
		DPD(2, "New Time Stamp = %d\n", card_mpuin->timestart);

		card_mpuin->qhead = 0;
		card_mpuin->qtail = 0;

		emu10k1_irq_enable(card, card->is_audigy ? A_INTE_MIDIRXENABLE : INTE_MIDIRXENABLE);
	}

	return 0;
}

/* Disable the RX Irq.  If a partial recorded buffer            */

/* exist, send it up to IMIDI level.                            */

int emu10k1_mpuin_stop(struct emu10k1_card *card)
{
	struct emu10k1_mpuin *card_mpuin = card->mpuin;
	struct midi_queue *midiq;
	unsigned long flags;

	DPF(2, "emu10k1_mpuin_stop()\n");

	emu10k1_irq_disable(card, card->is_audigy ? A_INTE_MIDIRXENABLE : INTE_MIDIRXENABLE);

	card_mpuin->status &= ~FLAGS_MIDM_STARTED;	/* clear */

	if (card_mpuin->firstmidiq) {
		spin_lock_irqsave(&card_mpuin->lock, flags);

		midiq = card_mpuin->firstmidiq;
		if (midiq != NULL) {
			if (midiq->sizeLeft == midiq->length)
				midiq = NULL;
			else {
				card_mpuin->firstmidiq = midiq->next;
				if (card_mpuin->firstmidiq == NULL)
					card_mpuin->lastmidiq = NULL;
			}
		}

		spin_unlock_irqrestore(&card_mpuin->lock, flags);

		if (midiq) {
			emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INLONGERROR, (unsigned long) midiq, 0);
			kfree(midiq);
		}
	}

	return 0;
}

/* Disable the RX Irq.  If any buffer                           */

/* exist, send it up to IMIDI level.                            */
int emu10k1_mpuin_reset(struct emu10k1_card *card)
{
	struct emu10k1_mpuin *card_mpuin = card->mpuin;
	struct midi_queue *midiq;

	DPF(2, "emu10k1_mpuin_reset()\n");

	emu10k1_irq_disable(card, card->is_audigy ? A_INTE_MIDIRXENABLE : INTE_MIDIRXENABLE);

	while (card_mpuin->firstmidiq) {
		midiq = card_mpuin->firstmidiq;
		card_mpuin->firstmidiq = midiq->next;

		if (midiq->sizeLeft == midiq->length)
			emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INLONGDATA, (unsigned long) midiq, 0);
		else
			emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INLONGERROR, (unsigned long) midiq, 0);

		kfree(midiq);
	}

	card_mpuin->lastmidiq = NULL;
	card_mpuin->status &= ~FLAGS_MIDM_STARTED;

	return 0;
}

/* Passes the message with the data back to the client          */

/* via IRQ & DPC callbacks to Ring 3                            */
static int emu10k1_mpuin_callback(struct emu10k1_mpuin *card_mpuin, u32 msg, unsigned long data, u32 bytesvalid)
{
	unsigned long timein;
	struct midi_queue *midiq;
	unsigned long callback_msg[3];
	struct midi_hdr *midihdr;

	/* Called during ISR. The data & code touched are:
	 * 1. card_mpuin
	 * 2. The function to be called
	 */

	timein = card_mpuin->timein;
	if (card_mpuin->timestart <= timein)
		callback_msg[0] = timein - card_mpuin->timestart;
	else
		callback_msg[0] = (~0x0L - card_mpuin->timestart) + timein;

	if (msg == ICARDMIDI_INDATA || msg == ICARDMIDI_INDATAERROR) {
		callback_msg[1] = data;
		callback_msg[2] = bytesvalid;
		DPD(2, "emu10k1_mpuin_callback: midimsg = %#lx\n", data);
	} else {
		midiq = (struct midi_queue *) data;
		midihdr = (struct midi_hdr *) midiq->refdata;

		callback_msg[1] = midiq->length - midiq->sizeLeft;
		callback_msg[2] = midiq->refdata;
		midihdr->flags &= ~MIDIBUF_INQUEUE;
		midihdr->flags |= MIDIBUF_DONE;

		midihdr->bytesrecorded = midiq->length - midiq->sizeLeft;
	}

	/* Notify client that Sysex buffer has been sent */
	emu10k1_midi_callback(msg, card_mpuin->openinfo.refdata, callback_msg);

	return 0;
}

void emu10k1_mpuin_bh(unsigned long refdata)
{
	u8 data;
	unsigned idx;
	struct emu10k1_mpuin *card_mpuin = (struct emu10k1_mpuin *) refdata;
	unsigned long flags;

	while (card_mpuin->qhead != card_mpuin->qtail) {
		spin_lock_irqsave(&card_mpuin->lock, flags);
		idx = card_mpuin->qhead;
		data = card_mpuin->midiq[idx].data;
		card_mpuin->timein = card_mpuin->midiq[idx].timein;
		idx = (idx + 1) % MIDIIN_MAX_BUFFER_SIZE;
		card_mpuin->qhead = idx;
		spin_unlock_irqrestore(&card_mpuin->lock, flags);

		sblive_miStateEntry(card_mpuin, data);
	}

	return;
}

/* IRQ callback handler routine for the MPU in port */

int emu10k1_mpuin_irqhandler(struct emu10k1_card *card)
{
	unsigned idx;
	unsigned count;
	u8 MPUIvalue;
	struct emu10k1_mpuin *card_mpuin = card->mpuin;

	/* IRQ service routine. The data and code touched are:
	 * 1. card_mpuin
	 */

	count = 0;
	idx = card_mpuin->qtail;

	while (1) {
		if (emu10k1_mpu_read_data(card, &MPUIvalue) < 0) {
			break;
		} else {
			++count;
			card_mpuin->midiq[idx].data = MPUIvalue;
			card_mpuin->midiq[idx].timein = (jiffies * 1000) / HZ;
			idx = (idx + 1) % MIDIIN_MAX_BUFFER_SIZE;
		}
	}

	if (count) {
		card_mpuin->qtail = idx;

		tasklet_hi_schedule(&card_mpuin->tasklet);
	}

	return 0;
}

/*****************************************************************************/

/*   Supporting functions for Midi-In Interpretation State Machine           */

/*****************************************************************************/

/* FIXME: This should be a macro */
static int sblive_miStateInit(struct emu10k1_mpuin *card_mpuin)
{
	card_mpuin->status = 0;	/* For MIDI running status */
	card_mpuin->fstatus = 0;	/* For 0xFn status only */
	card_mpuin->curstate = STIN_PARSE;
	card_mpuin->laststate = STIN_PARSE;
	card_mpuin->data = 0;
	card_mpuin->timestart = 0;
	card_mpuin->timein = 0;

	return 0;
}

/* FIXME: This should be a macro */
static int sblive_miStateEntry(struct emu10k1_mpuin *card_mpuin, u8 data)
{
	return midistatefn[card_mpuin->curstate].Fn(card_mpuin, data);
}

static int sblive_miStateParse(struct emu10k1_mpuin *card_mpuin, u8 data)
{
	switch (data & 0xf0) {
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		card_mpuin->curstate = STIN_3BYTE;
		break;

	case 0xC0:
	case 0xD0:
		card_mpuin->curstate = STIN_2BYTE;
		break;

	case 0xF0:
		/* System messages do not affect the previous running status! */
		switch (data & 0x0f) {
		case 0x0:
			card_mpuin->laststate = card_mpuin->curstate;
			card_mpuin->curstate = STIN_SYS_EX_NORM;

			if (card_mpuin->firstmidiq) {
				struct midi_queue *midiq;

				midiq = card_mpuin->firstmidiq;
				*midiq->midibyte = data;
				--midiq->sizeLeft;
				++midiq->midibyte;
			}

			return CTSTATUS_NEXT_BYTE;

		case 0x7:
			emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATAERROR, 0xf7, 0);
			return -1;

		case 0x2:
			card_mpuin->laststate = card_mpuin->curstate;
			card_mpuin->curstate = STIN_SYS_COMMON_3;
			break;

		case 0x1:
		case 0x3:
			card_mpuin->laststate = card_mpuin->curstate;
			card_mpuin->curstate = STIN_SYS_COMMON_2;
			break;

		default:
			/* includes 0xF4 - 0xF6, 0xF8 - 0xFF */
			return midistatefn[STIN_SYS_REAL].Fn(card_mpuin, data);
		}

		break;

	default:
		DPF(2, "BUG: default case hit\n");
		return -1;
	}

	return midistatefn[card_mpuin->curstate].Fn(card_mpuin, data);
}

static int sblive_miState3Byte(struct emu10k1_mpuin *card_mpuin, u8 data)
{
	u8 temp = data & 0xf0;

	if (temp < 0x80) {
		return midistatefn[STIN_3BYTE_KEY].Fn(card_mpuin, data);
	} else if (temp <= 0xe0 && temp != 0xc0 && temp != 0xd0) {
		card_mpuin->status = data;
		card_mpuin->curstate = STIN_3BYTE_KEY;

		return CTSTATUS_NEXT_BYTE;
	}

	return midistatefn[STIN_PARSE].Fn(card_mpuin, data);
}

static int sblive_miState3ByteKey(struct emu10k1_mpuin *card_mpuin, u8 data)
/* byte 1 */
{
	unsigned long tmp;

	if (data > 0x7f) {
		/* Real-time messages check */
		if (data > 0xf7)
			return midistatefn[STIN_SYS_REAL].Fn(card_mpuin, data);

		/* Invalid data! */
		DPF(2, "Invalid data!\n");

		card_mpuin->curstate = STIN_PARSE;
		tmp = ((unsigned long) data) << 8;
		tmp |= (unsigned long) card_mpuin->status;

		emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATAERROR, tmp, 0);

		return -1;
	}

	card_mpuin->data = data;
	card_mpuin->curstate = STIN_3BYTE_VEL;

	return CTSTATUS_NEXT_BYTE;
}

static int sblive_miState3ByteVel(struct emu10k1_mpuin *card_mpuin, u8 data)
/* byte 2 */
{
	unsigned long tmp;

	if (data > 0x7f) {
		/* Real-time messages check */
		if (data > 0xf7)
			return midistatefn[STIN_SYS_REAL].Fn(card_mpuin, data);

		/* Invalid data! */
		DPF(2, "Invalid data!\n");

		card_mpuin->curstate = STIN_PARSE;
		tmp = ((unsigned long) data) << 8;
		tmp |= card_mpuin->data;
		tmp = tmp << 8;
		tmp |= (unsigned long) card_mpuin->status;

		emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATAERROR, tmp, 0);

		return -1;
	}

	card_mpuin->curstate = STIN_3BYTE;
	tmp = (unsigned long) data;
	tmp = tmp << 8;
	tmp |= (unsigned long) card_mpuin->data;
	tmp = tmp << 8;
	tmp |= (unsigned long) card_mpuin->status;

	emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATA, tmp, 3);

	return 0;
}

static int sblive_miState2Byte(struct emu10k1_mpuin *card_mpuin, u8 data)
{
	u8 temp = data & 0xf0;

	if ((temp == 0xc0) || (temp == 0xd0)) {
		card_mpuin->status = data;
		card_mpuin->curstate = STIN_2BYTE_KEY;

		return CTSTATUS_NEXT_BYTE;
	}

	if (temp < 0x80)
		return midistatefn[STIN_2BYTE_KEY].Fn(card_mpuin, data);

	return midistatefn[STIN_PARSE].Fn(card_mpuin, data);
}

static int sblive_miState2ByteKey(struct emu10k1_mpuin *card_mpuin, u8 data)
/* byte 1 */
{
	unsigned long tmp;

	if (data > 0x7f) {
		/* Real-time messages check */
		if (data > 0xf7)
			return midistatefn[STIN_SYS_REAL].Fn(card_mpuin, data);

		/* Invalid data! */
		DPF(2, "Invalid data!\n");

		card_mpuin->curstate = STIN_PARSE;
		tmp = (unsigned long) data;
		tmp = tmp << 8;
		tmp |= (unsigned long) card_mpuin->status;

		emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATAERROR, tmp, 0);

		return -1;
	}

	card_mpuin->curstate = STIN_2BYTE;
	tmp = (unsigned long) data;
	tmp = tmp << 8;
	tmp |= (unsigned long) card_mpuin->status;

	emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATA, tmp, 2);

	return 0;
}

static int sblive_miStateSysCommon2(struct emu10k1_mpuin *card_mpuin, u8 data)
{
	card_mpuin->fstatus = data;
	card_mpuin->curstate = STIN_SYS_COMMON_2_KEY;

	return CTSTATUS_NEXT_BYTE;
}

static int sblive_miStateSysCommon2Key(struct emu10k1_mpuin *card_mpuin, u8 data)
/* byte 1 */
{
	unsigned long tmp;

	if (data > 0x7f) {
		/* Real-time messages check */
		if (data > 0xf7)
			return midistatefn[STIN_SYS_REAL].Fn(card_mpuin, data);

		/* Invalid data! */
		DPF(2, "Invalid data!\n");

		card_mpuin->curstate = card_mpuin->laststate;
		tmp = (unsigned long) data;
		tmp = tmp << 8;
		tmp |= (unsigned long) card_mpuin->fstatus;

		emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATAERROR, tmp, 0);

		return -1;
	}

	card_mpuin->curstate = card_mpuin->laststate;
	tmp = (unsigned long) data;
	tmp = tmp << 8;
	tmp |= (unsigned long) card_mpuin->fstatus;

	emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATA, tmp, 2);

	return 0;
}

static int sblive_miStateSysCommon3(struct emu10k1_mpuin *card_mpuin, u8 data)
{
	card_mpuin->fstatus = data;
	card_mpuin->curstate = STIN_SYS_COMMON_3_KEY;

	return CTSTATUS_NEXT_BYTE;
}

static int sblive_miStateSysCommon3Key(struct emu10k1_mpuin *card_mpuin, u8 data)
/* byte 1 */
{
	unsigned long tmp;

	if (data > 0x7f) {
		/* Real-time messages check */
		if (data > 0xf7)
			return midistatefn[STIN_SYS_REAL].Fn(card_mpuin, data);

		/* Invalid data! */
		DPF(2, "Invalid data!\n");

		card_mpuin->curstate = card_mpuin->laststate;
		tmp = (unsigned long) data;
		tmp = tmp << 8;
		tmp |= (unsigned long) card_mpuin->fstatus;

		emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATAERROR, tmp, 0);

		return -1;
	}

	card_mpuin->data = data;
	card_mpuin->curstate = STIN_SYS_COMMON_3_VEL;

	return CTSTATUS_NEXT_BYTE;
}

static int sblive_miStateSysCommon3Vel(struct emu10k1_mpuin *card_mpuin, u8 data)
/* byte 2 */
{
	unsigned long tmp;

	if (data > 0x7f) {
		/* Real-time messages check */
		if (data > 0xf7)
			return midistatefn[STIN_SYS_REAL].Fn(card_mpuin, data);

		/* Invalid data! */
		DPF(2, "Invalid data!\n");

		card_mpuin->curstate = card_mpuin->laststate;
		tmp = (unsigned long) data;
		tmp = tmp << 8;
		tmp |= (unsigned long) card_mpuin->data;
		tmp = tmp << 8;
		tmp |= (unsigned long) card_mpuin->fstatus;

		emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATAERROR, tmp, 0);

		return -1;
	}

	card_mpuin->curstate = card_mpuin->laststate;
	tmp = (unsigned long) data;
	tmp = tmp << 8;
	tmp |= (unsigned long) card_mpuin->data;
	tmp = tmp << 8;
	tmp |= (unsigned long) card_mpuin->fstatus;

	emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATA, tmp, 3);

	return 0;
}

static int sblive_miStateSysExNorm(struct emu10k1_mpuin *card_mpuin, u8 data)
{
	unsigned long flags;

	if ((data > 0x7f) && (data != 0xf7)) {
		/* Real-time messages check */
		if (data > 0xf7)
			return midistatefn[STIN_SYS_REAL].Fn(card_mpuin, data);

		/* Invalid Data! */
		DPF(2, "Invalid data!\n");

		card_mpuin->curstate = card_mpuin->laststate;

		if (card_mpuin->firstmidiq) {
			struct midi_queue *midiq;

			midiq = card_mpuin->firstmidiq;
			*midiq->midibyte = data;
			--midiq->sizeLeft;
			++midiq->midibyte;

			spin_lock_irqsave(&card_mpuin->lock, flags);

			card_mpuin->firstmidiq = midiq->next;
			if (card_mpuin->firstmidiq == NULL)
				card_mpuin->lastmidiq = NULL;

			spin_unlock_irqrestore(&card_mpuin->lock, flags);

			emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INLONGERROR, (unsigned long) midiq, 0);

			kfree(midiq);
		}

		return -1;
	}

	if (card_mpuin->firstmidiq) {
		struct midi_queue *midiq;

		midiq = card_mpuin->firstmidiq;
		*midiq->midibyte = data;
		--midiq->sizeLeft;
		++midiq->midibyte;
	}

	if (data == 0xf7) {
		/* End of Sysex buffer */
		/* Send down the buffer */

		card_mpuin->curstate = card_mpuin->laststate;

		if (card_mpuin->firstmidiq) {
			struct midi_queue *midiq;

			midiq = card_mpuin->firstmidiq;

			spin_lock_irqsave(&card_mpuin->lock, flags);

			card_mpuin->firstmidiq = midiq->next;
			if (card_mpuin->firstmidiq == NULL)
				card_mpuin->lastmidiq = NULL;

			spin_unlock_irqrestore(&card_mpuin->lock, flags);

			emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INLONGDATA, (unsigned long) midiq, 0);

			kfree(midiq);
		}

		return 0;
	}

	if (card_mpuin->firstmidiq) {
		struct midi_queue *midiq;

		midiq = card_mpuin->firstmidiq;

		if (midiq->sizeLeft == 0) {
			/* Special case */

			spin_lock_irqsave(&card_mpuin->lock, flags);

			card_mpuin->firstmidiq = midiq->next;
			if (card_mpuin->firstmidiq == NULL)
				card_mpuin->lastmidiq = NULL;

			spin_unlock_irqrestore(&card_mpuin->lock, flags);

			emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INLONGDATA, (unsigned long) midiq, 0);

			kfree(midiq);

			return CTSTATUS_NEXT_BYTE;
		}
	}

	return CTSTATUS_NEXT_BYTE;
}

static int sblive_miStateSysReal(struct emu10k1_mpuin *card_mpuin, u8 data)
{
	emu10k1_mpuin_callback(card_mpuin, ICARDMIDI_INDATA, data, 1);

	return CTSTATUS_NEXT_BYTE;
}
