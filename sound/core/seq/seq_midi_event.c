/*
 *  MIDI byte <-> sequencer event coder
 *
 *  Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>,
 *                        Jaroslav Kysela <perex@suse.cz>
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

#include <sound/driver.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/seq_kernel.h>
#include <sound/seq_midi_event.h>
#include <sound/asoundef.h>

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>, Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("MIDI byte <-> sequencer event coder");
MODULE_LICENSE("GPL");

/* queue type */
/* from 0 to 7 are normal commands (note off, on, etc.) */
#define ST_NOTEOFF	0
#define ST_NOTEON	1
#define ST_SPECIAL	8
#define ST_SYSEX	ST_SPECIAL
/* from 8 to 15 are events for 0xf0-0xf7 */


/*
 * prototypes
 */
static void note_event(struct snd_midi_event *dev, struct snd_seq_event *ev);
static void one_param_ctrl_event(struct snd_midi_event *dev, struct snd_seq_event *ev);
static void pitchbend_ctrl_event(struct snd_midi_event *dev, struct snd_seq_event *ev);
static void two_param_ctrl_event(struct snd_midi_event *dev, struct snd_seq_event *ev);
static void one_param_event(struct snd_midi_event *dev, struct snd_seq_event *ev);
static void songpos_event(struct snd_midi_event *dev, struct snd_seq_event *ev);
static void note_decode(struct snd_seq_event *ev, unsigned char *buf);
static void one_param_decode(struct snd_seq_event *ev, unsigned char *buf);
static void pitchbend_decode(struct snd_seq_event *ev, unsigned char *buf);
static void two_param_decode(struct snd_seq_event *ev, unsigned char *buf);
static void songpos_decode(struct snd_seq_event *ev, unsigned char *buf);

/*
 * event list
 */
static struct status_event_list {
	int event;
	int qlen;
	void (*encode)(struct snd_midi_event *dev, struct snd_seq_event *ev);
	void (*decode)(struct snd_seq_event *ev, unsigned char *buf);
} status_event[] = {
	/* 0x80 - 0xf0 */
	{SNDRV_SEQ_EVENT_NOTEOFF,	2, note_event, note_decode},
	{SNDRV_SEQ_EVENT_NOTEON,	2, note_event, note_decode},
	{SNDRV_SEQ_EVENT_KEYPRESS,	2, note_event, note_decode},
	{SNDRV_SEQ_EVENT_CONTROLLER,	2, two_param_ctrl_event, two_param_decode},
	{SNDRV_SEQ_EVENT_PGMCHANGE,	1, one_param_ctrl_event, one_param_decode},
	{SNDRV_SEQ_EVENT_CHANPRESS,	1, one_param_ctrl_event, one_param_decode},
	{SNDRV_SEQ_EVENT_PITCHBEND,	2, pitchbend_ctrl_event, pitchbend_decode},
	{SNDRV_SEQ_EVENT_NONE,		0, NULL, NULL}, /* 0xf0 */
	/* 0xf0 - 0xff */
	{SNDRV_SEQ_EVENT_SYSEX,		1, NULL, NULL}, /* sysex: 0xf0 */
	{SNDRV_SEQ_EVENT_QFRAME,	1, one_param_event, one_param_decode}, /* 0xf1 */
	{SNDRV_SEQ_EVENT_SONGPOS,	2, songpos_event, songpos_decode}, /* 0xf2 */
	{SNDRV_SEQ_EVENT_SONGSEL,	1, one_param_event, one_param_decode}, /* 0xf3 */
	{SNDRV_SEQ_EVENT_NONE,		0, NULL, NULL}, /* 0xf4 */
	{SNDRV_SEQ_EVENT_NONE,		0, NULL, NULL}, /* 0xf5 */
	{SNDRV_SEQ_EVENT_TUNE_REQUEST,	0, NULL, NULL},	/* 0xf6 */
	{SNDRV_SEQ_EVENT_NONE,		0, NULL, NULL}, /* 0xf7 */
	{SNDRV_SEQ_EVENT_CLOCK,		0, NULL, NULL}, /* 0xf8 */
	{SNDRV_SEQ_EVENT_NONE,		0, NULL, NULL}, /* 0xf9 */
	{SNDRV_SEQ_EVENT_START,		0, NULL, NULL}, /* 0xfa */
	{SNDRV_SEQ_EVENT_CONTINUE,	0, NULL, NULL}, /* 0xfb */
	{SNDRV_SEQ_EVENT_STOP, 		0, NULL, NULL}, /* 0xfc */
	{SNDRV_SEQ_EVENT_NONE, 		0, NULL, NULL}, /* 0xfd */
	{SNDRV_SEQ_EVENT_SENSING, 	0, NULL, NULL}, /* 0xfe */
	{SNDRV_SEQ_EVENT_RESET, 	0, NULL, NULL}, /* 0xff */
};

static int extra_decode_ctrl14(struct snd_midi_event *dev, unsigned char *buf, int len,
			       struct snd_seq_event *ev);
static int extra_decode_xrpn(struct snd_midi_event *dev, unsigned char *buf, int count,
			     struct snd_seq_event *ev);

static struct extra_event_list {
	int event;
	int (*decode)(struct snd_midi_event *dev, unsigned char *buf, int len,
		      struct snd_seq_event *ev);
} extra_event[] = {
	{SNDRV_SEQ_EVENT_CONTROL14, extra_decode_ctrl14},
	{SNDRV_SEQ_EVENT_NONREGPARAM, extra_decode_xrpn},
	{SNDRV_SEQ_EVENT_REGPARAM, extra_decode_xrpn},
};

/*
 *  new/delete record
 */

int snd_midi_event_new(int bufsize, struct snd_midi_event **rdev)
{
	struct snd_midi_event *dev;

	*rdev = NULL;
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;
	if (bufsize > 0) {
		dev->buf = kmalloc(bufsize, GFP_KERNEL);
		if (dev->buf == NULL) {
			kfree(dev);
			return -ENOMEM;
		}
	}
	dev->bufsize = bufsize;
	dev->lastcmd = 0xff;
	spin_lock_init(&dev->lock);
	*rdev = dev;
	return 0;
}

void snd_midi_event_free(struct snd_midi_event *dev)
{
	if (dev != NULL) {
		kfree(dev->buf);
		kfree(dev);
	}
}

/*
 * initialize record
 */
static inline void reset_encode(struct snd_midi_event *dev)
{
	dev->read = 0;
	dev->qlen = 0;
	dev->type = 0;
}

void snd_midi_event_reset_encode(struct snd_midi_event *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	reset_encode(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
}

void snd_midi_event_reset_decode(struct snd_midi_event *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->lastcmd = 0xff;
	spin_unlock_irqrestore(&dev->lock, flags);
}

#if 0
void snd_midi_event_init(struct snd_midi_event *dev)
{
	snd_midi_event_reset_encode(dev);
	snd_midi_event_reset_decode(dev);
}
#endif  /*  0  */

void snd_midi_event_no_status(struct snd_midi_event *dev, int on)
{
	dev->nostat = on ? 1 : 0;
}

/*
 * resize buffer
 */
#if 0
int snd_midi_event_resize_buffer(struct snd_midi_event *dev, int bufsize)
{
	unsigned char *new_buf, *old_buf;
	unsigned long flags;

	if (bufsize == dev->bufsize)
		return 0;
	new_buf = kmalloc(bufsize, GFP_KERNEL);
	if (new_buf == NULL)
		return -ENOMEM;
	spin_lock_irqsave(&dev->lock, flags);
	old_buf = dev->buf;
	dev->buf = new_buf;
	dev->bufsize = bufsize;
	reset_encode(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	kfree(old_buf);
	return 0;
}
#endif  /*  0  */

/*
 *  read bytes and encode to sequencer event if finished
 *  return the size of encoded bytes
 */
long snd_midi_event_encode(struct snd_midi_event *dev, unsigned char *buf, long count,
			   struct snd_seq_event *ev)
{
	long result = 0;
	int rc;

	ev->type = SNDRV_SEQ_EVENT_NONE;

	while (count-- > 0) {
		rc = snd_midi_event_encode_byte(dev, *buf++, ev);
		result++;
		if (rc < 0)
			return rc;
		else if (rc > 0)
			return result;
	}

	return result;
}

/*
 *  read one byte and encode to sequencer event:
 *  return 1 if MIDI bytes are encoded to an event
 *         0 data is not finished
 *         negative for error
 */
int snd_midi_event_encode_byte(struct snd_midi_event *dev, int c,
			       struct snd_seq_event *ev)
{
	int rc = 0;
	unsigned long flags;

	c &= 0xff;

	if (c >= MIDI_CMD_COMMON_CLOCK) {
		/* real-time event */
		ev->type = status_event[ST_SPECIAL + c - 0xf0].event;
		ev->flags &= ~SNDRV_SEQ_EVENT_LENGTH_MASK;
		ev->flags |= SNDRV_SEQ_EVENT_LENGTH_FIXED;
		return 1;
	}

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->qlen > 0) {
		/* rest of command */
		dev->buf[dev->read++] = c;
		if (dev->type != ST_SYSEX)
			dev->qlen--;
	} else {
		/* new command */
		dev->read = 1;
		if (c & 0x80) {
			dev->buf[0] = c;
			if ((c & 0xf0) == 0xf0) /* special events */
				dev->type = (c & 0x0f) + ST_SPECIAL;
			else
				dev->type = (c >> 4) & 0x07;
			dev->qlen = status_event[dev->type].qlen;
		} else {
			/* process this byte as argument */
			dev->buf[dev->read++] = c;
			dev->qlen = status_event[dev->type].qlen - 1;
		}
	}
	if (dev->qlen == 0) {
		ev->type = status_event[dev->type].event;
		ev->flags &= ~SNDRV_SEQ_EVENT_LENGTH_MASK;
		ev->flags |= SNDRV_SEQ_EVENT_LENGTH_FIXED;
		if (status_event[dev->type].encode) /* set data values */
			status_event[dev->type].encode(dev, ev);
		rc = 1;
	} else 	if (dev->type == ST_SYSEX) {
		if (c == MIDI_CMD_COMMON_SYSEX_END ||
		    dev->read >= dev->bufsize) {
			ev->flags &= ~SNDRV_SEQ_EVENT_LENGTH_MASK;
			ev->flags |= SNDRV_SEQ_EVENT_LENGTH_VARIABLE;
			ev->type = SNDRV_SEQ_EVENT_SYSEX;
			ev->data.ext.len = dev->read;
			ev->data.ext.ptr = dev->buf;
			if (c != MIDI_CMD_COMMON_SYSEX_END)
				dev->read = 0; /* continue to parse */
			else
				reset_encode(dev); /* all parsed */
			rc = 1;
		}
	}

	spin_unlock_irqrestore(&dev->lock, flags);
	return rc;
}

/* encode note event */
static void note_event(struct snd_midi_event *dev, struct snd_seq_event *ev)
{
	ev->data.note.channel = dev->buf[0] & 0x0f;
	ev->data.note.note = dev->buf[1];
	ev->data.note.velocity = dev->buf[2];
}

/* encode one parameter controls */
static void one_param_ctrl_event(struct snd_midi_event *dev, struct snd_seq_event *ev)
{
	ev->data.control.channel = dev->buf[0] & 0x0f;
	ev->data.control.value = dev->buf[1];
}

/* encode pitch wheel change */
static void pitchbend_ctrl_event(struct snd_midi_event *dev, struct snd_seq_event *ev)
{
	ev->data.control.channel = dev->buf[0] & 0x0f;
	ev->data.control.value = (int)dev->buf[2] * 128 + (int)dev->buf[1] - 8192;
}

/* encode midi control change */
static void two_param_ctrl_event(struct snd_midi_event *dev, struct snd_seq_event *ev)
{
	ev->data.control.channel = dev->buf[0] & 0x0f;
	ev->data.control.param = dev->buf[1];
	ev->data.control.value = dev->buf[2];
}

/* encode one parameter value*/
static void one_param_event(struct snd_midi_event *dev, struct snd_seq_event *ev)
{
	ev->data.control.value = dev->buf[1];
}

/* encode song position */
static void songpos_event(struct snd_midi_event *dev, struct snd_seq_event *ev)
{
	ev->data.control.value = (int)dev->buf[2] * 128 + (int)dev->buf[1];
}

/*
 * decode from a sequencer event to midi bytes
 * return the size of decoded midi events
 */
long snd_midi_event_decode(struct snd_midi_event *dev, unsigned char *buf, long count,
			   struct snd_seq_event *ev)
{
	unsigned int cmd, type;

	if (ev->type == SNDRV_SEQ_EVENT_NONE)
		return -ENOENT;

	for (type = 0; type < ARRAY_SIZE(status_event); type++) {
		if (ev->type == status_event[type].event)
			goto __found;
	}
	for (type = 0; type < ARRAY_SIZE(extra_event); type++) {
		if (ev->type == extra_event[type].event)
			return extra_event[type].decode(dev, buf, count, ev);
	}
	return -ENOENT;

      __found:
	if (type >= ST_SPECIAL)
		cmd = 0xf0 + (type - ST_SPECIAL);
	else
		/* data.note.channel and data.control.channel is identical */
		cmd = 0x80 | (type << 4) | (ev->data.note.channel & 0x0f);


	if (cmd == MIDI_CMD_COMMON_SYSEX) {
		snd_midi_event_reset_decode(dev);
		return snd_seq_expand_var_event(ev, count, buf, 1, 0);
	} else {
		int qlen;
		unsigned char xbuf[4];
		unsigned long flags;

		spin_lock_irqsave(&dev->lock, flags);
		if ((cmd & 0xf0) == 0xf0 || dev->lastcmd != cmd || dev->nostat) {
			dev->lastcmd = cmd;
			spin_unlock_irqrestore(&dev->lock, flags);
			xbuf[0] = cmd;
			if (status_event[type].decode)
				status_event[type].decode(ev, xbuf + 1);
			qlen = status_event[type].qlen + 1;
		} else {
			spin_unlock_irqrestore(&dev->lock, flags);
			if (status_event[type].decode)
				status_event[type].decode(ev, xbuf + 0);
			qlen = status_event[type].qlen;
		}
		if (count < qlen)
			return -ENOMEM;
		memcpy(buf, xbuf, qlen);
		return qlen;
	}
}


/* decode note event */
static void note_decode(struct snd_seq_event *ev, unsigned char *buf)
{
	buf[0] = ev->data.note.note & 0x7f;
	buf[1] = ev->data.note.velocity & 0x7f;
}

/* decode one parameter controls */
static void one_param_decode(struct snd_seq_event *ev, unsigned char *buf)
{
	buf[0] = ev->data.control.value & 0x7f;
}

/* decode pitch wheel change */
static void pitchbend_decode(struct snd_seq_event *ev, unsigned char *buf)
{
	int value = ev->data.control.value + 8192;
	buf[0] = value & 0x7f;
	buf[1] = (value >> 7) & 0x7f;
}

/* decode midi control change */
static void two_param_decode(struct snd_seq_event *ev, unsigned char *buf)
{
	buf[0] = ev->data.control.param & 0x7f;
	buf[1] = ev->data.control.value & 0x7f;
}

/* decode song position */
static void songpos_decode(struct snd_seq_event *ev, unsigned char *buf)
{
	buf[0] = ev->data.control.value & 0x7f;
	buf[1] = (ev->data.control.value >> 7) & 0x7f;
}

/* decode 14bit control */
static int extra_decode_ctrl14(struct snd_midi_event *dev, unsigned char *buf,
			       int count, struct snd_seq_event *ev)
{
	unsigned char cmd;
	int idx = 0;

	cmd = MIDI_CMD_CONTROL|(ev->data.control.channel & 0x0f);
	if (ev->data.control.param < 0x20) {
		if (count < 4)
			return -ENOMEM;
		if (dev->nostat && count < 6)
			return -ENOMEM;
		if (cmd != dev->lastcmd || dev->nostat) {
			if (count < 5)
				return -ENOMEM;
			buf[idx++] = dev->lastcmd = cmd;
		}
		buf[idx++] = ev->data.control.param;
		buf[idx++] = (ev->data.control.value >> 7) & 0x7f;
		if (dev->nostat)
			buf[idx++] = cmd;
		buf[idx++] = ev->data.control.param + 0x20;
		buf[idx++] = ev->data.control.value & 0x7f;
	} else {
		if (count < 2)
			return -ENOMEM;
		if (cmd != dev->lastcmd || dev->nostat) {
			if (count < 3)
				return -ENOMEM;
			buf[idx++] = dev->lastcmd = cmd;
		}
		buf[idx++] = ev->data.control.param & 0x7f;
		buf[idx++] = ev->data.control.value & 0x7f;
	}
	return idx;
}

/* decode reg/nonreg param */
static int extra_decode_xrpn(struct snd_midi_event *dev, unsigned char *buf,
			     int count, struct snd_seq_event *ev)
{
	unsigned char cmd;
	char *cbytes;
	static char cbytes_nrpn[4] = { MIDI_CTL_NONREG_PARM_NUM_MSB,
				       MIDI_CTL_NONREG_PARM_NUM_LSB,
				       MIDI_CTL_MSB_DATA_ENTRY,
				       MIDI_CTL_LSB_DATA_ENTRY };
	static char cbytes_rpn[4] =  { MIDI_CTL_REGIST_PARM_NUM_MSB,
				       MIDI_CTL_REGIST_PARM_NUM_LSB,
				       MIDI_CTL_MSB_DATA_ENTRY,
				       MIDI_CTL_LSB_DATA_ENTRY };
	unsigned char bytes[4];
	int idx = 0, i;

	if (count < 8)
		return -ENOMEM;
	if (dev->nostat && count < 12)
		return -ENOMEM;
	cmd = MIDI_CMD_CONTROL|(ev->data.control.channel & 0x0f);
	bytes[0] = ev->data.control.param & 0x007f;
	bytes[1] = (ev->data.control.param & 0x3f80) >> 7;
	bytes[2] = ev->data.control.value & 0x007f;
	bytes[3] = (ev->data.control.value & 0x3f80) >> 7;
	if (cmd != dev->lastcmd && !dev->nostat) {
		if (count < 9)
			return -ENOMEM;
		buf[idx++] = dev->lastcmd = cmd;
	}
	cbytes = ev->type == SNDRV_SEQ_EVENT_NONREGPARAM ? cbytes_nrpn : cbytes_rpn;
	for (i = 0; i < 4; i++) {
		if (dev->nostat)
			buf[idx++] = dev->lastcmd = cmd;
		buf[idx++] = cbytes[i];
		buf[idx++] = bytes[i];
	}
	return idx;
}

/*
 *  exports
 */
 
EXPORT_SYMBOL(snd_midi_event_new);
EXPORT_SYMBOL(snd_midi_event_free);
EXPORT_SYMBOL(snd_midi_event_reset_encode);
EXPORT_SYMBOL(snd_midi_event_reset_decode);
EXPORT_SYMBOL(snd_midi_event_no_status);
EXPORT_SYMBOL(snd_midi_event_encode);
EXPORT_SYMBOL(snd_midi_event_encode_byte);
EXPORT_SYMBOL(snd_midi_event_decode);

static int __init alsa_seq_midi_event_init(void)
{
	return 0;
}

static void __exit alsa_seq_midi_event_exit(void)
{
}

module_init(alsa_seq_midi_event_init)
module_exit(alsa_seq_midi_event_exit)
