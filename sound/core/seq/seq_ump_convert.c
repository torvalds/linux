// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ALSA sequencer event conversion between UMP and legacy clients
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/ump.h>
#include <sound/ump_msg.h>
#include "seq_ump_convert.h"

/*
 * Upgrade / downgrade value bits
 */
static u8 downscale_32_to_7bit(u32 src)
{
	return src >> 25;
}

static u16 downscale_32_to_14bit(u32 src)
{
	return src >> 18;
}

static u8 downscale_16_to_7bit(u16 src)
{
	return src >> 9;
}

static u16 upscale_7_to_16bit(u8 src)
{
	u16 val, repeat;

	val = (u16)src << 9;
	if (src <= 0x40)
		return val;
	repeat = src & 0x3f;
	return val | (repeat << 3) | (repeat >> 3);
}

static u32 upscale_7_to_32bit(u8 src)
{
	u32 val, repeat;

	val = src << 25;
	if (src <= 0x40)
		return val;
	repeat = src & 0x3f;
	return val | (repeat << 19) | (repeat << 13) |
		(repeat << 7) | (repeat << 1) | (repeat >> 5);
}

static u32 upscale_14_to_32bit(u16 src)
{
	u32 val, repeat;

	val = src << 18;
	if (src <= 0x2000)
		return val;
	repeat = src & 0x1fff;
	return val | (repeat << 5) | (repeat >> 8);
}

static unsigned char get_ump_group(struct snd_seq_client_port *port)
{
	return port->ump_group ? (port->ump_group - 1) : 0;
}

/* create a UMP header */
#define make_raw_ump(port, type) \
	ump_compose(type, get_ump_group(port), 0, 0)

/*
 * UMP -> MIDI1 sequencer event
 */

/* MIDI 1.0 CVM */

/* encode note event */
static void ump_midi1_to_note_ev(const union snd_ump_midi1_msg *val,
				 struct snd_seq_event *ev)
{
	ev->data.note.channel = val->note.channel;
	ev->data.note.note = val->note.note;
	ev->data.note.velocity = val->note.velocity;
}

/* encode one parameter controls */
static void ump_midi1_to_ctrl_ev(const union snd_ump_midi1_msg *val,
				 struct snd_seq_event *ev)
{
	ev->data.control.channel = val->caf.channel;
	ev->data.control.value = val->caf.data;
}

/* encode pitch wheel change */
static void ump_midi1_to_pitchbend_ev(const union snd_ump_midi1_msg *val,
				      struct snd_seq_event *ev)
{
	ev->data.control.channel = val->pb.channel;
	ev->data.control.value = (val->pb.data_msb << 7) | val->pb.data_lsb;
	ev->data.control.value -= 8192;
}

/* encode midi control change */
static void ump_midi1_to_cc_ev(const union snd_ump_midi1_msg *val,
			       struct snd_seq_event *ev)
{
	ev->data.control.channel = val->cc.channel;
	ev->data.control.param = val->cc.index;
	ev->data.control.value = val->cc.data;
}

/* Encoding MIDI 1.0 UMP packet */
struct seq_ump_midi1_to_ev {
	int seq_type;
	void (*encode)(const union snd_ump_midi1_msg *val, struct snd_seq_event *ev);
};

/* Encoders for MIDI1 status 0x80-0xe0 */
static struct seq_ump_midi1_to_ev midi1_msg_encoders[] = {
	{SNDRV_SEQ_EVENT_NOTEOFF,	ump_midi1_to_note_ev},	/* 0x80 */
	{SNDRV_SEQ_EVENT_NOTEON,	ump_midi1_to_note_ev},	/* 0x90 */
	{SNDRV_SEQ_EVENT_KEYPRESS,	ump_midi1_to_note_ev},	/* 0xa0 */
	{SNDRV_SEQ_EVENT_CONTROLLER,	ump_midi1_to_cc_ev},	/* 0xb0 */
	{SNDRV_SEQ_EVENT_PGMCHANGE,	ump_midi1_to_ctrl_ev},	/* 0xc0 */
	{SNDRV_SEQ_EVENT_CHANPRESS,	ump_midi1_to_ctrl_ev},	/* 0xd0 */
	{SNDRV_SEQ_EVENT_PITCHBEND,	ump_midi1_to_pitchbend_ev}, /* 0xe0 */
};

static int cvt_ump_midi1_to_event(const union snd_ump_midi1_msg *val,
				  struct snd_seq_event *ev)
{
	unsigned char status = val->note.status;

	if (status < 0x8 || status > 0xe)
		return 0; /* invalid - skip */
	status -= 8;
	ev->type = midi1_msg_encoders[status].seq_type;
	ev->flags = SNDRV_SEQ_EVENT_LENGTH_FIXED;
	midi1_msg_encoders[status].encode(val, ev);
	return 1;
}

/* MIDI System message */

/* encode one parameter value*/
static void ump_system_to_one_param_ev(const union snd_ump_midi1_msg *val,
				       struct snd_seq_event *ev)
{
	ev->data.control.value = val->system.parm1;
}

/* encode song position */
static void ump_system_to_songpos_ev(const union snd_ump_midi1_msg *val,
				     struct snd_seq_event *ev)
{
	ev->data.control.value = (val->system.parm1 << 7) | val->system.parm2;
}

/* Encoders for 0xf0 - 0xff */
static struct seq_ump_midi1_to_ev system_msg_encoders[] = {
	{SNDRV_SEQ_EVENT_NONE,		NULL},	 /* 0xf0 */
	{SNDRV_SEQ_EVENT_QFRAME,	ump_system_to_one_param_ev}, /* 0xf1 */
	{SNDRV_SEQ_EVENT_SONGPOS,	ump_system_to_songpos_ev}, /* 0xf2 */
	{SNDRV_SEQ_EVENT_SONGSEL,	ump_system_to_one_param_ev}, /* 0xf3 */
	{SNDRV_SEQ_EVENT_NONE,		NULL}, /* 0xf4 */
	{SNDRV_SEQ_EVENT_NONE,		NULL}, /* 0xf5 */
	{SNDRV_SEQ_EVENT_TUNE_REQUEST,	NULL}, /* 0xf6 */
	{SNDRV_SEQ_EVENT_NONE,		NULL}, /* 0xf7 */
	{SNDRV_SEQ_EVENT_CLOCK,		NULL}, /* 0xf8 */
	{SNDRV_SEQ_EVENT_NONE,		NULL}, /* 0xf9 */
	{SNDRV_SEQ_EVENT_START,		NULL}, /* 0xfa */
	{SNDRV_SEQ_EVENT_CONTINUE,	NULL}, /* 0xfb */
	{SNDRV_SEQ_EVENT_STOP,		NULL}, /* 0xfc */
	{SNDRV_SEQ_EVENT_NONE,		NULL}, /* 0xfd */
	{SNDRV_SEQ_EVENT_SENSING,	NULL}, /* 0xfe */
	{SNDRV_SEQ_EVENT_RESET,		NULL}, /* 0xff */
};

static int cvt_ump_system_to_event(const union snd_ump_midi1_msg *val,
				   struct snd_seq_event *ev)
{
	unsigned char status = val->system.status;

	if ((status & 0xf0) != UMP_MIDI1_MSG_REALTIME)
		return 0; /* invalid status - skip */
	status &= 0x0f;
	ev->type = system_msg_encoders[status].seq_type;
	ev->flags = SNDRV_SEQ_EVENT_LENGTH_FIXED;
	if (ev->type == SNDRV_SEQ_EVENT_NONE)
		return 0;
	if (system_msg_encoders[status].encode)
		system_msg_encoders[status].encode(val, ev);
	return 1;
}

/* MIDI 2.0 CVM */

/* encode note event */
static int ump_midi2_to_note_ev(const union snd_ump_midi2_msg *val,
				struct snd_seq_event *ev)
{
	ev->data.note.channel = val->note.channel;
	ev->data.note.note = val->note.note;
	ev->data.note.velocity = downscale_16_to_7bit(val->note.velocity);
	/* correct note-on velocity 0 to 1;
	 * it's no longer equivalent as not-off for MIDI 2.0
	 */
	if (ev->type == SNDRV_SEQ_EVENT_NOTEON &&
	    !ev->data.note.velocity)
		ev->data.note.velocity = 1;
	return 1;
}

/* encode pitch wheel change */
static int ump_midi2_to_pitchbend_ev(const union snd_ump_midi2_msg *val,
				     struct snd_seq_event *ev)
{
	ev->data.control.channel = val->pb.channel;
	ev->data.control.value = downscale_32_to_14bit(val->pb.data);
	ev->data.control.value -= 8192;
	return 1;
}

/* encode midi control change */
static int ump_midi2_to_cc_ev(const union snd_ump_midi2_msg *val,
			      struct snd_seq_event *ev)
{
	ev->data.control.channel = val->cc.channel;
	ev->data.control.param = val->cc.index;
	ev->data.control.value = downscale_32_to_7bit(val->cc.data);
	return 1;
}

/* encode midi program change */
static int ump_midi2_to_pgm_ev(const union snd_ump_midi2_msg *val,
			       struct snd_seq_event *ev)
{
	int size = 1;

	ev->data.control.channel = val->pg.channel;
	if (val->pg.bank_valid) {
		ev->type = SNDRV_SEQ_EVENT_CONTROL14;
		ev->data.control.param = UMP_CC_BANK_SELECT;
		ev->data.control.value = (val->pg.bank_msb << 7) | val->pg.bank_lsb;
		ev[1] = ev[0];
		ev++;
		ev->type = SNDRV_SEQ_EVENT_PGMCHANGE;
		size = 2;
	}
	ev->data.control.value = val->pg.program;
	return size;
}

/* encode one parameter controls */
static int ump_midi2_to_ctrl_ev(const union snd_ump_midi2_msg *val,
				struct snd_seq_event *ev)
{
	ev->data.control.channel = val->caf.channel;
	ev->data.control.value = downscale_32_to_7bit(val->caf.data);
	return 1;
}

/* encode RPN/NRPN */
static int ump_midi2_to_rpn_ev(const union snd_ump_midi2_msg *val,
			       struct snd_seq_event *ev)
{
	ev->data.control.channel = val->rpn.channel;
	ev->data.control.param = (val->rpn.bank << 7) | val->rpn.index;
	ev->data.control.value = downscale_32_to_14bit(val->rpn.data);
	return 1;
}

/* Encoding MIDI 2.0 UMP Packet */
struct seq_ump_midi2_to_ev {
	int seq_type;
	int (*encode)(const union snd_ump_midi2_msg *val, struct snd_seq_event *ev);
};

/* Encoders for MIDI2 status 0x00-0xf0 */
static struct seq_ump_midi2_to_ev midi2_msg_encoders[] = {
	{SNDRV_SEQ_EVENT_NONE,		NULL},			/* 0x00 */
	{SNDRV_SEQ_EVENT_NONE,		NULL},			/* 0x10 */
	{SNDRV_SEQ_EVENT_REGPARAM,	ump_midi2_to_rpn_ev},	/* 0x20 */
	{SNDRV_SEQ_EVENT_NONREGPARAM,	ump_midi2_to_rpn_ev},	/* 0x30 */
	{SNDRV_SEQ_EVENT_NONE,		NULL},			/* 0x40 */
	{SNDRV_SEQ_EVENT_NONE,		NULL},			/* 0x50 */
	{SNDRV_SEQ_EVENT_NONE,		NULL},			/* 0x60 */
	{SNDRV_SEQ_EVENT_NONE,		NULL},			/* 0x70 */
	{SNDRV_SEQ_EVENT_NOTEOFF,	ump_midi2_to_note_ev},	/* 0x80 */
	{SNDRV_SEQ_EVENT_NOTEON,	ump_midi2_to_note_ev},	/* 0x90 */
	{SNDRV_SEQ_EVENT_KEYPRESS,	ump_midi2_to_note_ev},	/* 0xa0 */
	{SNDRV_SEQ_EVENT_CONTROLLER,	ump_midi2_to_cc_ev},	/* 0xb0 */
	{SNDRV_SEQ_EVENT_PGMCHANGE,	ump_midi2_to_pgm_ev},	/* 0xc0 */
	{SNDRV_SEQ_EVENT_CHANPRESS,	ump_midi2_to_ctrl_ev},	/* 0xd0 */
	{SNDRV_SEQ_EVENT_PITCHBEND,	ump_midi2_to_pitchbend_ev}, /* 0xe0 */
	{SNDRV_SEQ_EVENT_NONE,		NULL},			/* 0xf0 */
};

static int cvt_ump_midi2_to_event(const union snd_ump_midi2_msg *val,
				  struct snd_seq_event *ev)
{
	unsigned char status = val->note.status;

	ev->type = midi2_msg_encoders[status].seq_type;
	if (ev->type == SNDRV_SEQ_EVENT_NONE)
		return 0; /* skip */
	ev->flags = SNDRV_SEQ_EVENT_LENGTH_FIXED;
	return midi2_msg_encoders[status].encode(val, ev);
}

/* parse and compose for a sysex var-length event */
static int cvt_ump_sysex7_to_event(const u32 *data, unsigned char *buf,
				   struct snd_seq_event *ev)
{
	unsigned char status;
	unsigned char bytes;
	u32 val;
	int size = 0;

	val = data[0];
	status = ump_sysex_message_status(val);
	bytes = ump_sysex_message_length(val);
	if (bytes > 6)
		return 0; // skip

	if (status == UMP_SYSEX_STATUS_SINGLE ||
	    status == UMP_SYSEX_STATUS_START) {
		buf[0] = UMP_MIDI1_MSG_SYSEX_START;
		size = 1;
	}

	if (bytes > 0)
		buf[size++] = (val >> 8) & 0x7f;
	if (bytes > 1)
		buf[size++] = val & 0x7f;
	val = data[1];
	if (bytes > 2)
		buf[size++] = (val >> 24) & 0x7f;
	if (bytes > 3)
		buf[size++] = (val >> 16) & 0x7f;
	if (bytes > 4)
		buf[size++] = (val >> 8) & 0x7f;
	if (bytes > 5)
		buf[size++] = val & 0x7f;

	if (status == UMP_SYSEX_STATUS_SINGLE ||
	    status == UMP_SYSEX_STATUS_END)
		buf[size++] = UMP_MIDI1_MSG_SYSEX_END;

	ev->type = SNDRV_SEQ_EVENT_SYSEX;
	ev->flags = SNDRV_SEQ_EVENT_LENGTH_VARIABLE;
	ev->data.ext.len = size;
	ev->data.ext.ptr = buf;
	return 1;
}

/* convert UMP packet from MIDI 1.0 to MIDI 2.0 and deliver it */
static int cvt_ump_midi1_to_midi2(struct snd_seq_client *dest,
				  struct snd_seq_client_port *dest_port,
				  struct snd_seq_event *__event,
				  int atomic, int hop)
{
	struct snd_seq_ump_event *event = (struct snd_seq_ump_event *)__event;
	struct snd_seq_ump_event ev_cvt;
	const union snd_ump_midi1_msg *midi1 = (const union snd_ump_midi1_msg *)event->ump;
	union snd_ump_midi2_msg *midi2 = (union snd_ump_midi2_msg *)ev_cvt.ump;

	ev_cvt = *event;
	memset(&ev_cvt.ump, 0, sizeof(ev_cvt.ump));

	midi2->note.type = UMP_MSG_TYPE_MIDI2_CHANNEL_VOICE;
	midi2->note.group = midi1->note.group;
	midi2->note.status = midi1->note.status;
	midi2->note.channel = midi1->note.channel;
	switch (midi1->note.status) {
	case UMP_MSG_STATUS_NOTE_ON:
	case UMP_MSG_STATUS_NOTE_OFF:
		midi2->note.note = midi1->note.note;
		midi2->note.velocity = upscale_7_to_16bit(midi1->note.velocity);
		break;
	case UMP_MSG_STATUS_POLY_PRESSURE:
		midi2->paf.note = midi1->paf.note;
		midi2->paf.data = upscale_7_to_32bit(midi1->paf.data);
		break;
	case UMP_MSG_STATUS_CC:
		midi2->cc.index = midi1->cc.index;
		midi2->cc.data = upscale_7_to_32bit(midi1->cc.data);
		break;
	case UMP_MSG_STATUS_PROGRAM:
		midi2->pg.program = midi1->pg.program;
		break;
	case UMP_MSG_STATUS_CHANNEL_PRESSURE:
		midi2->caf.data = upscale_7_to_32bit(midi1->caf.data);
		break;
	case UMP_MSG_STATUS_PITCH_BEND:
		midi2->pb.data = upscale_14_to_32bit((midi1->pb.data_msb << 7) |
						     midi1->pb.data_lsb);
		break;
	default:
		return 0;
	}

	return __snd_seq_deliver_single_event(dest, dest_port,
					      (struct snd_seq_event *)&ev_cvt,
					      atomic, hop);
}

/* convert UMP packet from MIDI 2.0 to MIDI 1.0 and deliver it */
static int cvt_ump_midi2_to_midi1(struct snd_seq_client *dest,
				  struct snd_seq_client_port *dest_port,
				  struct snd_seq_event *__event,
				  int atomic, int hop)
{
	struct snd_seq_ump_event *event = (struct snd_seq_ump_event *)__event;
	struct snd_seq_ump_event ev_cvt;
	union snd_ump_midi1_msg *midi1 = (union snd_ump_midi1_msg *)ev_cvt.ump;
	const union snd_ump_midi2_msg *midi2 = (const union snd_ump_midi2_msg *)event->ump;
	u16 v;

	ev_cvt = *event;
	memset(&ev_cvt.ump, 0, sizeof(ev_cvt.ump));

	midi1->note.type = UMP_MSG_TYPE_MIDI1_CHANNEL_VOICE;
	midi1->note.group = midi2->note.group;
	midi1->note.status = midi2->note.status;
	midi1->note.channel = midi2->note.channel;
	switch (midi2->note.status) {
	case UMP_MSG_STATUS_NOTE_ON:
	case UMP_MSG_STATUS_NOTE_OFF:
		midi1->note.note = midi2->note.note;
		midi1->note.velocity = downscale_16_to_7bit(midi2->note.velocity);
		break;
	case UMP_MSG_STATUS_POLY_PRESSURE:
		midi1->paf.note = midi2->paf.note;
		midi1->paf.data = downscale_32_to_7bit(midi2->paf.data);
		break;
	case UMP_MSG_STATUS_CC:
		midi1->cc.index = midi2->cc.index;
		midi1->cc.data = downscale_32_to_7bit(midi2->cc.data);
		break;
	case UMP_MSG_STATUS_PROGRAM:
		midi1->pg.program = midi2->pg.program;
		break;
	case UMP_MSG_STATUS_CHANNEL_PRESSURE:
		midi1->caf.data = downscale_32_to_7bit(midi2->caf.data);
		break;
	case UMP_MSG_STATUS_PITCH_BEND:
		v = downscale_32_to_14bit(midi2->pb.data);
		midi1->pb.data_msb = v >> 7;
		midi1->pb.data_lsb = v & 0x7f;
		break;
	default:
		return 0;
	}

	return __snd_seq_deliver_single_event(dest, dest_port,
					      (struct snd_seq_event *)&ev_cvt,
					      atomic, hop);
}

/* convert UMP to a legacy ALSA seq event and deliver it */
static int cvt_ump_to_any(struct snd_seq_client *dest,
			  struct snd_seq_client_port *dest_port,
			  struct snd_seq_event *event,
			  unsigned char type,
			  int atomic, int hop)
{
	struct snd_seq_event ev_cvt[2]; /* up to two events */
	struct snd_seq_ump_event *ump_ev = (struct snd_seq_ump_event *)event;
	/* use the second event as a temp buffer for saving stack usage */
	unsigned char *sysex_buf = (unsigned char *)(ev_cvt + 1);
	unsigned char flags = event->flags & ~SNDRV_SEQ_EVENT_UMP;
	int i, len, err;

	ev_cvt[0] = ev_cvt[1] = *event;
	ev_cvt[0].flags = flags;
	ev_cvt[1].flags = flags;
	switch (type) {
	case UMP_MSG_TYPE_SYSTEM:
		len = cvt_ump_system_to_event((union snd_ump_midi1_msg *)ump_ev->ump,
					      ev_cvt);
		break;
	case UMP_MSG_TYPE_MIDI1_CHANNEL_VOICE:
		len = cvt_ump_midi1_to_event((union snd_ump_midi1_msg *)ump_ev->ump,
					     ev_cvt);
		break;
	case UMP_MSG_TYPE_MIDI2_CHANNEL_VOICE:
		len = cvt_ump_midi2_to_event((union snd_ump_midi2_msg *)ump_ev->ump,
					     ev_cvt);
		break;
	case UMP_MSG_TYPE_DATA:
		len = cvt_ump_sysex7_to_event(ump_ev->ump, sysex_buf, ev_cvt);
		break;
	default:
		return 0;
	}

	for (i = 0; i < len; i++) {
		err = __snd_seq_deliver_single_event(dest, dest_port,
						     &ev_cvt[i], atomic, hop);
		if (err < 0)
			return err;
	}

	return 0;
}

/* Replace UMP group field with the destination and deliver */
static int deliver_with_group_convert(struct snd_seq_client *dest,
				      struct snd_seq_client_port *dest_port,
				      struct snd_seq_ump_event *ump_ev,
				      int atomic, int hop)
{
	struct snd_seq_ump_event ev = *ump_ev;

	/* rewrite the group to the destination port */
	ev.ump[0] &= ~(0xfU << 24);
	/* fill with the new group; the dest_port->ump_group field is 1-based */
	ev.ump[0] |= ((dest_port->ump_group - 1) << 24);

	return __snd_seq_deliver_single_event(dest, dest_port,
					      (struct snd_seq_event *)&ev,
					      atomic, hop);
}

/* apply the UMP event filter; return true to skip the event */
static bool ump_event_filtered(struct snd_seq_client *dest,
			       const struct snd_seq_ump_event *ev)
{
	unsigned char group;

	group = ump_message_group(ev->ump[0]);
	if (ump_is_groupless_msg(ump_message_type(ev->ump[0])))
		return dest->group_filter & (1U << 0);
	/* check the bitmap for 1-based group number */
	return dest->group_filter & (1U << (group + 1));
}

/* Convert from UMP packet and deliver */
int snd_seq_deliver_from_ump(struct snd_seq_client *source,
			     struct snd_seq_client *dest,
			     struct snd_seq_client_port *dest_port,
			     struct snd_seq_event *event,
			     int atomic, int hop)
{
	struct snd_seq_ump_event *ump_ev = (struct snd_seq_ump_event *)event;
	unsigned char type;

	if (snd_seq_ev_is_variable(event))
		return 0; // skip, no variable event for UMP, so far
	if (ump_event_filtered(dest, ump_ev))
		return 0; // skip if group filter is set and matching
	type = ump_message_type(ump_ev->ump[0]);

	if (snd_seq_client_is_ump(dest)) {
		if (snd_seq_client_is_midi2(dest) &&
		    type == UMP_MSG_TYPE_MIDI1_CHANNEL_VOICE)
			return cvt_ump_midi1_to_midi2(dest, dest_port,
						      event, atomic, hop);
		else if (!snd_seq_client_is_midi2(dest) &&
			 type == UMP_MSG_TYPE_MIDI2_CHANNEL_VOICE)
			return cvt_ump_midi2_to_midi1(dest, dest_port,
						      event, atomic, hop);
		/* non-EP port and different group is set? */
		if (dest_port->ump_group &&
		    !ump_is_groupless_msg(type) &&
		    ump_message_group(*ump_ev->ump) + 1 != dest_port->ump_group)
			return deliver_with_group_convert(dest, dest_port,
							  ump_ev, atomic, hop);
		/* copy as-is */
		return __snd_seq_deliver_single_event(dest, dest_port,
						      event, atomic, hop);
	}

	return cvt_ump_to_any(dest, dest_port, event, type, atomic, hop);
}

/*
 * MIDI1 sequencer event -> UMP conversion
 */

/* Conversion to UMP MIDI 1.0 */

/* convert note on/off event to MIDI 1.0 UMP */
static int note_ev_to_ump_midi1(const struct snd_seq_event *event,
				struct snd_seq_client_port *dest_port,
				union snd_ump_midi1_msg *data,
				unsigned char status)
{
	if (!event->data.note.velocity)
		status = UMP_MSG_STATUS_NOTE_OFF;
	data->note.status = status;
	data->note.channel = event->data.note.channel & 0x0f;
	data->note.velocity = event->data.note.velocity & 0x7f;
	data->note.note = event->data.note.note & 0x7f;
	return 1;
}

/* convert CC event to MIDI 1.0 UMP */
static int cc_ev_to_ump_midi1(const struct snd_seq_event *event,
			      struct snd_seq_client_port *dest_port,
			      union snd_ump_midi1_msg *data,
			      unsigned char status)
{
	data->cc.status = status;
	data->cc.channel = event->data.control.channel & 0x0f;
	data->cc.index = event->data.control.param;
	data->cc.data = event->data.control.value;
	return 1;
}

/* convert one-parameter control event to MIDI 1.0 UMP */
static int ctrl_ev_to_ump_midi1(const struct snd_seq_event *event,
				struct snd_seq_client_port *dest_port,
				union snd_ump_midi1_msg *data,
				unsigned char status)
{
	data->caf.status = status;
	data->caf.channel = event->data.control.channel & 0x0f;
	data->caf.data = event->data.control.value & 0x7f;
	return 1;
}

/* convert pitchbend event to MIDI 1.0 UMP */
static int pitchbend_ev_to_ump_midi1(const struct snd_seq_event *event,
				     struct snd_seq_client_port *dest_port,
				     union snd_ump_midi1_msg *data,
				     unsigned char status)
{
	int val = event->data.control.value + 8192;

	val = clamp(val, 0, 0x3fff);
	data->pb.status = status;
	data->pb.channel = event->data.control.channel & 0x0f;
	data->pb.data_msb = (val >> 7) & 0x7f;
	data->pb.data_lsb = val & 0x7f;
	return 1;
}

/* convert 14bit control event to MIDI 1.0 UMP; split to two events */
static int ctrl14_ev_to_ump_midi1(const struct snd_seq_event *event,
				  struct snd_seq_client_port *dest_port,
				  union snd_ump_midi1_msg *data,
				  unsigned char status)
{
	data->cc.status = UMP_MSG_STATUS_CC;
	data->cc.channel = event->data.control.channel & 0x0f;
	data->cc.index = event->data.control.param & 0x7f;
	if (event->data.control.param < 0x20) {
		data->cc.data = (event->data.control.value >> 7) & 0x7f;
		data[1] = data[0];
		data[1].cc.index = event->data.control.param | 0x20;
		data[1].cc.data = event->data.control.value & 0x7f;
		return 2;
	}

	data->cc.data = event->data.control.value & 0x7f;
	return 1;
}

/* convert RPN/NRPN event to MIDI 1.0 UMP; split to four events */
static int rpn_ev_to_ump_midi1(const struct snd_seq_event *event,
			       struct snd_seq_client_port *dest_port,
			       union snd_ump_midi1_msg *data,
			       unsigned char status)
{
	bool is_rpn = (status == UMP_MSG_STATUS_RPN);

	data->cc.status = UMP_MSG_STATUS_CC;
	data->cc.channel = event->data.control.channel & 0x0f;
	data[1] = data[2] = data[3] = data[0];

	data[0].cc.index = is_rpn ? UMP_CC_RPN_MSB : UMP_CC_NRPN_MSB;
	data[0].cc.data = (event->data.control.param >> 7) & 0x7f;
	data[1].cc.index = is_rpn ? UMP_CC_RPN_LSB : UMP_CC_NRPN_LSB;
	data[1].cc.data = event->data.control.param & 0x7f;
	data[2].cc.index = UMP_CC_DATA;
	data[2].cc.data = (event->data.control.value >> 7) & 0x7f;
	data[3].cc.index = UMP_CC_DATA_LSB;
	data[3].cc.data = event->data.control.value & 0x7f;
	return 4;
}

/* convert system / RT message to UMP */
static int system_ev_to_ump_midi1(const struct snd_seq_event *event,
				  struct snd_seq_client_port *dest_port,
				  union snd_ump_midi1_msg *data,
				  unsigned char status)
{
	data->system.status = status;
	return 1;
}

/* convert system / RT message with 1 parameter to UMP */
static int system_1p_ev_to_ump_midi1(const struct snd_seq_event *event,
				     struct snd_seq_client_port *dest_port,
				     union snd_ump_midi1_msg *data,
				     unsigned char status)
{
	data->system.status = status;
	data->system.parm1 = event->data.control.value & 0x7f;
	return 1;
}

/* convert system / RT message with two parameters to UMP */
static int system_2p_ev_to_ump_midi1(const struct snd_seq_event *event,
				     struct snd_seq_client_port *dest_port,
				     union snd_ump_midi1_msg *data,
				     unsigned char status)
{
	data->system.status = status;
	data->system.parm1 = (event->data.control.value >> 7) & 0x7f;
	data->system.parm2 = event->data.control.value & 0x7f;
	return 1;
}

/* Conversion to UMP MIDI 2.0 */

/* convert note on/off event to MIDI 2.0 UMP */
static int note_ev_to_ump_midi2(const struct snd_seq_event *event,
				struct snd_seq_client_port *dest_port,
				union snd_ump_midi2_msg *data,
				unsigned char status)
{
	if (!event->data.note.velocity)
		status = UMP_MSG_STATUS_NOTE_OFF;
	data->note.status = status;
	data->note.channel = event->data.note.channel & 0x0f;
	data->note.note = event->data.note.note & 0x7f;
	data->note.velocity = upscale_7_to_16bit(event->data.note.velocity & 0x7f);
	return 1;
}

/* convert PAF event to MIDI 2.0 UMP */
static int paf_ev_to_ump_midi2(const struct snd_seq_event *event,
			       struct snd_seq_client_port *dest_port,
			       union snd_ump_midi2_msg *data,
			       unsigned char status)
{
	data->paf.status = status;
	data->paf.channel = event->data.note.channel & 0x0f;
	data->paf.note = event->data.note.note & 0x7f;
	data->paf.data = upscale_7_to_32bit(event->data.note.velocity & 0x7f);
	return 1;
}

/* set up the MIDI2 RPN/NRPN packet data from the parsed info */
static void fill_rpn(struct snd_seq_ump_midi2_bank *cc,
		     union snd_ump_midi2_msg *data)
{
	if (cc->rpn_set) {
		data->rpn.status = UMP_MSG_STATUS_RPN;
		data->rpn.bank = cc->cc_rpn_msb;
		data->rpn.index = cc->cc_rpn_lsb;
		cc->rpn_set = 0;
		cc->cc_rpn_msb = cc->cc_rpn_lsb = 0;
	} else {
		data->rpn.status = UMP_MSG_STATUS_NRPN;
		data->rpn.bank = cc->cc_nrpn_msb;
		data->rpn.index = cc->cc_nrpn_lsb;
		cc->nrpn_set = 0;
		cc->cc_nrpn_msb = cc->cc_nrpn_lsb = 0;
	}
	data->rpn.data = upscale_14_to_32bit((cc->cc_data_msb << 7) |
					     cc->cc_data_lsb);
	cc->cc_data_msb = cc->cc_data_lsb = 0;
}

/* convert CC event to MIDI 2.0 UMP */
static int cc_ev_to_ump_midi2(const struct snd_seq_event *event,
			      struct snd_seq_client_port *dest_port,
			      union snd_ump_midi2_msg *data,
			      unsigned char status)
{
	unsigned char channel = event->data.control.channel & 0x0f;
	unsigned char index = event->data.control.param & 0x7f;
	unsigned char val = event->data.control.value & 0x7f;
	struct snd_seq_ump_midi2_bank *cc = &dest_port->midi2_bank[channel];

	/* process special CC's (bank/rpn/nrpn) */
	switch (index) {
	case UMP_CC_RPN_MSB:
		cc->rpn_set = 1;
		cc->cc_rpn_msb = val;
		return 0; // skip
	case UMP_CC_RPN_LSB:
		cc->rpn_set = 1;
		cc->cc_rpn_lsb = val;
		return 0; // skip
	case UMP_CC_NRPN_MSB:
		cc->nrpn_set = 1;
		cc->cc_nrpn_msb = val;
		return 0; // skip
	case UMP_CC_NRPN_LSB:
		cc->nrpn_set = 1;
		cc->cc_nrpn_lsb = val;
		return 0; // skip
	case UMP_CC_DATA:
		cc->cc_data_msb = val;
		return 0; // skip
	case UMP_CC_BANK_SELECT:
		cc->bank_set = 1;
		cc->cc_bank_msb = val;
		return 0; // skip
	case UMP_CC_BANK_SELECT_LSB:
		cc->bank_set = 1;
		cc->cc_bank_lsb = val;
		return 0; // skip
	case UMP_CC_DATA_LSB:
		cc->cc_data_lsb = val;
		if (!(cc->rpn_set || cc->nrpn_set))
			return 0; // skip
		fill_rpn(cc, data);
		return 1;
	}

	data->cc.status = status;
	data->cc.channel = channel;
	data->cc.index = index;
	data->cc.data = upscale_7_to_32bit(event->data.control.value & 0x7f);
	return 1;
}

/* convert one-parameter control event to MIDI 2.0 UMP */
static int ctrl_ev_to_ump_midi2(const struct snd_seq_event *event,
				struct snd_seq_client_port *dest_port,
				union snd_ump_midi2_msg *data,
				unsigned char status)
{
	data->caf.status = status;
	data->caf.channel = event->data.control.channel & 0x0f;
	data->caf.data = upscale_7_to_32bit(event->data.control.value & 0x7f);
	return 1;
}

/* convert program change event to MIDI 2.0 UMP */
static int pgm_ev_to_ump_midi2(const struct snd_seq_event *event,
			       struct snd_seq_client_port *dest_port,
			       union snd_ump_midi2_msg *data,
			       unsigned char status)
{
	unsigned char channel = event->data.control.channel & 0x0f;
	struct snd_seq_ump_midi2_bank *cc = &dest_port->midi2_bank[channel];

	data->pg.status = status;
	data->pg.channel = channel;
	data->pg.program = event->data.control.value & 0x7f;
	if (cc->bank_set) {
		data->pg.bank_valid = 1;
		data->pg.bank_msb = cc->cc_bank_msb;
		data->pg.bank_lsb = cc->cc_bank_lsb;
		cc->bank_set = 0;
		cc->cc_bank_msb = cc->cc_bank_lsb = 0;
	}
	return 1;
}

/* convert pitchbend event to MIDI 2.0 UMP */
static int pitchbend_ev_to_ump_midi2(const struct snd_seq_event *event,
				     struct snd_seq_client_port *dest_port,
				     union snd_ump_midi2_msg *data,
				     unsigned char status)
{
	int val = event->data.control.value + 8192;

	val = clamp(val, 0, 0x3fff);
	data->pb.status = status;
	data->pb.channel = event->data.control.channel & 0x0f;
	data->pb.data = upscale_14_to_32bit(val);
	return 1;
}

/* convert 14bit control event to MIDI 2.0 UMP; split to two events */
static int ctrl14_ev_to_ump_midi2(const struct snd_seq_event *event,
				  struct snd_seq_client_port *dest_port,
				  union snd_ump_midi2_msg *data,
				  unsigned char status)
{
	unsigned char channel = event->data.control.channel & 0x0f;
	unsigned char index = event->data.control.param & 0x7f;
	struct snd_seq_ump_midi2_bank *cc = &dest_port->midi2_bank[channel];
	unsigned char msb, lsb;

	msb = (event->data.control.value >> 7) & 0x7f;
	lsb = event->data.control.value & 0x7f;
	/* process special CC's (bank/rpn/nrpn) */
	switch (index) {
	case UMP_CC_BANK_SELECT:
		cc->cc_bank_msb = msb;
		fallthrough;
	case UMP_CC_BANK_SELECT_LSB:
		cc->bank_set = 1;
		cc->cc_bank_lsb = lsb;
		return 0; // skip
	case UMP_CC_RPN_MSB:
		cc->cc_rpn_msb = msb;
		fallthrough;
	case UMP_CC_RPN_LSB:
		cc->rpn_set = 1;
		cc->cc_rpn_lsb = lsb;
		return 0; // skip
	case UMP_CC_NRPN_MSB:
		cc->cc_nrpn_msb = msb;
		fallthrough;
	case UMP_CC_NRPN_LSB:
		cc->nrpn_set = 1;
		cc->cc_nrpn_lsb = lsb;
		return 0; // skip
	case UMP_CC_DATA:
		cc->cc_data_msb = msb;
		fallthrough;
	case UMP_CC_DATA_LSB:
		cc->cc_data_lsb = lsb;
		if (!(cc->rpn_set || cc->nrpn_set))
			return 0; // skip
		fill_rpn(cc, data);
		return 1;
	}

	data->cc.status = UMP_MSG_STATUS_CC;
	data->cc.channel = channel;
	data->cc.index = index;
	if (event->data.control.param < 0x20) {
		data->cc.data = upscale_7_to_32bit(msb);
		data[1] = data[0];
		data[1].cc.index = event->data.control.param | 0x20;
		data[1].cc.data = upscale_7_to_32bit(lsb);
		return 2;
	}

	data->cc.data = upscale_7_to_32bit(lsb);
	return 1;
}

/* convert RPN/NRPN event to MIDI 2.0 UMP */
static int rpn_ev_to_ump_midi2(const struct snd_seq_event *event,
			       struct snd_seq_client_port *dest_port,
			       union snd_ump_midi2_msg *data,
			       unsigned char status)
{
	data->rpn.status = status;
	data->rpn.channel = event->data.control.channel;
	data->rpn.bank = (event->data.control.param >> 7) & 0x7f;
	data->rpn.index = event->data.control.param & 0x7f;
	data->rpn.data = upscale_14_to_32bit(event->data.control.value & 0x3fff);
	return 1;
}

/* convert system / RT message to UMP */
static int system_ev_to_ump_midi2(const struct snd_seq_event *event,
				  struct snd_seq_client_port *dest_port,
				  union snd_ump_midi2_msg *data,
				  unsigned char status)
{
	return system_ev_to_ump_midi1(event, dest_port,
				      (union snd_ump_midi1_msg *)data,
				      status);
}

/* convert system / RT message with 1 parameter to UMP */
static int system_1p_ev_to_ump_midi2(const struct snd_seq_event *event,
				     struct snd_seq_client_port *dest_port,
				     union snd_ump_midi2_msg *data,
				     unsigned char status)
{
	return system_1p_ev_to_ump_midi1(event, dest_port,
					 (union snd_ump_midi1_msg *)data,
					 status);
}

/* convert system / RT message with two parameters to UMP */
static int system_2p_ev_to_ump_midi2(const struct snd_seq_event *event,
				     struct snd_seq_client_port *dest_port,
				     union snd_ump_midi2_msg *data,
				     unsigned char status)
{
	return system_1p_ev_to_ump_midi1(event, dest_port,
					 (union snd_ump_midi1_msg *)data,
					 status);
}

struct seq_ev_to_ump {
	int seq_type;
	unsigned char status;
	int (*midi1_encode)(const struct snd_seq_event *event,
			    struct snd_seq_client_port *dest_port,
			    union snd_ump_midi1_msg *data,
			    unsigned char status);
	int (*midi2_encode)(const struct snd_seq_event *event,
			    struct snd_seq_client_port *dest_port,
			    union snd_ump_midi2_msg *data,
			    unsigned char status);
};

static const struct seq_ev_to_ump seq_ev_ump_encoders[] = {
	{ SNDRV_SEQ_EVENT_NOTEON, UMP_MSG_STATUS_NOTE_ON,
	  note_ev_to_ump_midi1, note_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_NOTEOFF, UMP_MSG_STATUS_NOTE_OFF,
	  note_ev_to_ump_midi1, note_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_KEYPRESS, UMP_MSG_STATUS_POLY_PRESSURE,
	  note_ev_to_ump_midi1, paf_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_CONTROLLER, UMP_MSG_STATUS_CC,
	  cc_ev_to_ump_midi1, cc_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_PGMCHANGE, UMP_MSG_STATUS_PROGRAM,
	  ctrl_ev_to_ump_midi1, pgm_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_CHANPRESS, UMP_MSG_STATUS_CHANNEL_PRESSURE,
	  ctrl_ev_to_ump_midi1, ctrl_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_PITCHBEND, UMP_MSG_STATUS_PITCH_BEND,
	  pitchbend_ev_to_ump_midi1, pitchbend_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_CONTROL14, 0,
	  ctrl14_ev_to_ump_midi1, ctrl14_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_NONREGPARAM, UMP_MSG_STATUS_NRPN,
	  rpn_ev_to_ump_midi1, rpn_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_REGPARAM, UMP_MSG_STATUS_RPN,
	  rpn_ev_to_ump_midi1, rpn_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_QFRAME, UMP_SYSTEM_STATUS_MIDI_TIME_CODE,
	  system_1p_ev_to_ump_midi1, system_1p_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_SONGPOS, UMP_SYSTEM_STATUS_SONG_POSITION,
	  system_2p_ev_to_ump_midi1, system_2p_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_SONGSEL, UMP_SYSTEM_STATUS_SONG_SELECT,
	  system_1p_ev_to_ump_midi1, system_1p_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_TUNE_REQUEST, UMP_SYSTEM_STATUS_TUNE_REQUEST,
	  system_ev_to_ump_midi1, system_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_CLOCK, UMP_SYSTEM_STATUS_TIMING_CLOCK,
	  system_ev_to_ump_midi1, system_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_START, UMP_SYSTEM_STATUS_START,
	  system_ev_to_ump_midi1, system_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_CONTINUE, UMP_SYSTEM_STATUS_CONTINUE,
	  system_ev_to_ump_midi1, system_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_STOP, UMP_SYSTEM_STATUS_STOP,
	  system_ev_to_ump_midi1, system_ev_to_ump_midi2 },
	{ SNDRV_SEQ_EVENT_SENSING, UMP_SYSTEM_STATUS_ACTIVE_SENSING,
	  system_ev_to_ump_midi1, system_ev_to_ump_midi2 },
};

static const struct seq_ev_to_ump *find_ump_encoder(int type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(seq_ev_ump_encoders); i++)
		if (seq_ev_ump_encoders[i].seq_type == type)
			return &seq_ev_ump_encoders[i];

	return NULL;
}

static void setup_ump_event(struct snd_seq_ump_event *dest,
			    const struct snd_seq_event *src)
{
	memcpy(dest, src, sizeof(*src));
	dest->type = 0;
	dest->flags |= SNDRV_SEQ_EVENT_UMP;
	dest->flags &= ~SNDRV_SEQ_EVENT_LENGTH_MASK;
	memset(dest->ump, 0, sizeof(dest->ump));
}

/* Convert ALSA seq event to UMP MIDI 1.0 and deliver it */
static int cvt_to_ump_midi1(struct snd_seq_client *dest,
			    struct snd_seq_client_port *dest_port,
			    struct snd_seq_event *event,
			    int atomic, int hop)
{
	const struct seq_ev_to_ump *encoder;
	struct snd_seq_ump_event ev_cvt;
	union snd_ump_midi1_msg data[4];
	int i, n, err;

	encoder = find_ump_encoder(event->type);
	if (!encoder)
		return __snd_seq_deliver_single_event(dest, dest_port,
						      event, atomic, hop);

	data->raw = make_raw_ump(dest_port, UMP_MSG_TYPE_MIDI1_CHANNEL_VOICE);
	n = encoder->midi1_encode(event, dest_port, data, encoder->status);
	if (!n)
		return 0;

	setup_ump_event(&ev_cvt, event);
	for (i = 0; i < n; i++) {
		ev_cvt.ump[0] = data[i].raw;
		err = __snd_seq_deliver_single_event(dest, dest_port,
						     (struct snd_seq_event *)&ev_cvt,
						     atomic, hop);
		if (err < 0)
			return err;
	}

	return 0;
}

/* Convert ALSA seq event to UMP MIDI 2.0 and deliver it */
static int cvt_to_ump_midi2(struct snd_seq_client *dest,
			    struct snd_seq_client_port *dest_port,
			    struct snd_seq_event *event,
			    int atomic, int hop)
{
	const struct seq_ev_to_ump *encoder;
	struct snd_seq_ump_event ev_cvt;
	union snd_ump_midi2_msg data[2];
	int i, n, err;

	encoder = find_ump_encoder(event->type);
	if (!encoder)
		return __snd_seq_deliver_single_event(dest, dest_port,
						      event, atomic, hop);

	data->raw[0] = make_raw_ump(dest_port, UMP_MSG_TYPE_MIDI2_CHANNEL_VOICE);
	data->raw[1] = 0;
	n = encoder->midi2_encode(event, dest_port, data, encoder->status);
	if (!n)
		return 0;

	setup_ump_event(&ev_cvt, event);
	for (i = 0; i < n; i++) {
		memcpy(ev_cvt.ump, &data[i], sizeof(data[i]));
		err = __snd_seq_deliver_single_event(dest, dest_port,
						     (struct snd_seq_event *)&ev_cvt,
						     atomic, hop);
		if (err < 0)
			return err;
	}

	return 0;
}

/* Fill up a sysex7 UMP from the byte stream */
static void fill_sysex7_ump(struct snd_seq_client_port *dest_port,
			    u32 *val, u8 status, u8 *buf, int len)
{
	memset(val, 0, 8);
	memcpy((u8 *)val + 2, buf, len);
#ifdef __LITTLE_ENDIAN
	swab32_array(val, 2);
#endif
	val[0] |= ump_compose(UMP_MSG_TYPE_DATA, get_ump_group(dest_port),
			      status, len);
}

/* Convert sysex var event to UMP sysex7 packets and deliver them */
static int cvt_sysex_to_ump(struct snd_seq_client *dest,
			    struct snd_seq_client_port *dest_port,
			    struct snd_seq_event *event,
			    int atomic, int hop)
{
	struct snd_seq_ump_event ev_cvt;
	unsigned char status;
	u8 buf[6], *xbuf;
	int offset = 0;
	int len, err;

	if (!snd_seq_ev_is_variable(event))
		return 0;

	setup_ump_event(&ev_cvt, event);
	for (;;) {
		len = snd_seq_expand_var_event_at(event, sizeof(buf), buf, offset);
		if (len <= 0)
			break;
		if (WARN_ON(len > 6))
			break;
		offset += len;
		xbuf = buf;
		if (*xbuf == UMP_MIDI1_MSG_SYSEX_START) {
			status = UMP_SYSEX_STATUS_START;
			xbuf++;
			len--;
			if (len > 0 && xbuf[len - 1] == UMP_MIDI1_MSG_SYSEX_END) {
				status = UMP_SYSEX_STATUS_SINGLE;
				len--;
			}
		} else {
			if (xbuf[len - 1] == UMP_MIDI1_MSG_SYSEX_END) {
				status = UMP_SYSEX_STATUS_END;
				len--;
			} else {
				status = UMP_SYSEX_STATUS_CONTINUE;
			}
		}
		fill_sysex7_ump(dest_port, ev_cvt.ump, status, xbuf, len);
		err = __snd_seq_deliver_single_event(dest, dest_port,
						     (struct snd_seq_event *)&ev_cvt,
						     atomic, hop);
		if (err < 0)
			return err;
	}
	return 0;
}

/* Convert to UMP packet and deliver */
int snd_seq_deliver_to_ump(struct snd_seq_client *source,
			   struct snd_seq_client *dest,
			   struct snd_seq_client_port *dest_port,
			   struct snd_seq_event *event,
			   int atomic, int hop)
{
	if (dest->group_filter & (1U << dest_port->ump_group))
		return 0; /* group filtered - skip the event */
	if (event->type == SNDRV_SEQ_EVENT_SYSEX)
		return cvt_sysex_to_ump(dest, dest_port, event, atomic, hop);
	else if (snd_seq_client_is_midi2(dest))
		return cvt_to_ump_midi2(dest, dest_port, event, atomic, hop);
	else
		return cvt_to_ump_midi1(dest, dest_port, event, atomic, hop);
}
