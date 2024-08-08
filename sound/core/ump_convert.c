// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Helpers for UMP <-> MIDI 1.0 byte stream conversion
 */

#include <linux/module.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/asound.h>
#include <sound/ump.h>
#include <sound/ump_convert.h>

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

/*
 * UMP -> MIDI 1 byte stream conversion
 */
/* convert a UMP System message to MIDI 1.0 byte stream */
static int cvt_ump_system_to_legacy(u32 data, unsigned char *buf)
{
	buf[0] = ump_message_status_channel(data);
	switch (ump_message_status_code(data)) {
	case UMP_SYSTEM_STATUS_MIDI_TIME_CODE:
	case UMP_SYSTEM_STATUS_SONG_SELECT:
		buf[1] = (data >> 8) & 0x7f;
		return 2;
	case UMP_SYSTEM_STATUS_SONG_POSITION:
		buf[1] = (data >> 8) & 0x7f;
		buf[2] = data & 0x7f;
		return 3;
	default:
		return 1;
	}
}

/* convert a UMP MIDI 1.0 Channel Voice message to MIDI 1.0 byte stream */
static int cvt_ump_midi1_to_legacy(u32 data, unsigned char *buf)
{
	buf[0] = ump_message_status_channel(data);
	buf[1] = (data >> 8) & 0xff;
	switch (ump_message_status_code(data)) {
	case UMP_MSG_STATUS_PROGRAM:
	case UMP_MSG_STATUS_CHANNEL_PRESSURE:
		return 2;
	default:
		buf[2] = data & 0xff;
		return 3;
	}
}

/* convert a UMP MIDI 2.0 Channel Voice message to MIDI 1.0 byte stream */
static int cvt_ump_midi2_to_legacy(const union snd_ump_midi2_msg *midi2,
				   unsigned char *buf)
{
	unsigned char status = midi2->note.status;
	unsigned char channel = midi2->note.channel;
	u16 v;

	buf[0] = (status << 4) | channel;
	switch (status) {
	case UMP_MSG_STATUS_NOTE_OFF:
	case UMP_MSG_STATUS_NOTE_ON:
		buf[1] = midi2->note.note;
		buf[2] = downscale_16_to_7bit(midi2->note.velocity);
		if (status == UMP_MSG_STATUS_NOTE_ON && !buf[2])
			buf[2] = 1;
		return 3;
	case UMP_MSG_STATUS_POLY_PRESSURE:
		buf[1] = midi2->paf.note;
		buf[2] = downscale_32_to_7bit(midi2->paf.data);
		return 3;
	case UMP_MSG_STATUS_CC:
		buf[1] = midi2->cc.index;
		buf[2] = downscale_32_to_7bit(midi2->cc.data);
		return 3;
	case UMP_MSG_STATUS_CHANNEL_PRESSURE:
		buf[1] = downscale_32_to_7bit(midi2->caf.data);
		return 2;
	case UMP_MSG_STATUS_PROGRAM:
		if (midi2->pg.bank_valid) {
			buf[0] = channel | (UMP_MSG_STATUS_CC << 4);
			buf[1] = UMP_CC_BANK_SELECT;
			buf[2] = midi2->pg.bank_msb;
			buf[3] = channel | (UMP_MSG_STATUS_CC << 4);
			buf[4] = UMP_CC_BANK_SELECT_LSB;
			buf[5] = midi2->pg.bank_lsb;
			buf[6] = channel | (UMP_MSG_STATUS_PROGRAM << 4);
			buf[7] = midi2->pg.program;
			return 8;
		}
		buf[1] = midi2->pg.program;
		return 2;
	case UMP_MSG_STATUS_PITCH_BEND:
		v = downscale_32_to_14bit(midi2->pb.data);
		buf[1] = v & 0x7f;
		buf[2] = v >> 7;
		return 3;
	case UMP_MSG_STATUS_RPN:
	case UMP_MSG_STATUS_NRPN:
		buf[0] = channel | (UMP_MSG_STATUS_CC << 4);
		buf[1] = status == UMP_MSG_STATUS_RPN ? UMP_CC_RPN_MSB : UMP_CC_NRPN_MSB;
		buf[2] = midi2->rpn.bank;
		buf[3] = buf[0];
		buf[4] = status == UMP_MSG_STATUS_RPN ? UMP_CC_RPN_LSB : UMP_CC_NRPN_LSB;
		buf[5] = midi2->rpn.index;
		buf[6] = buf[0];
		buf[7] = UMP_CC_DATA;
		v = downscale_32_to_14bit(midi2->rpn.data);
		buf[8] = v >> 7;
		buf[9] = buf[0];
		buf[10] = UMP_CC_DATA_LSB;
		buf[11] = v & 0x7f;
		return 12;
	default:
		return 0;
	}
}

/* convert a UMP 7-bit SysEx message to MIDI 1.0 byte stream */
static int cvt_ump_sysex7_to_legacy(const u32 *data, unsigned char *buf)
{
	unsigned char status;
	unsigned char bytes;
	int size, offset;

	status = ump_sysex_message_status(*data);
	if (status > UMP_SYSEX_STATUS_END)
		return 0; // unsupported, skip
	bytes = ump_sysex_message_length(*data);
	if (bytes > 6)
		return 0; // skip

	size = 0;
	if (status == UMP_SYSEX_STATUS_SINGLE ||
	    status == UMP_SYSEX_STATUS_START) {
		buf[0] = UMP_MIDI1_MSG_SYSEX_START;
		size = 1;
	}

	offset = 8;
	for (; bytes; bytes--, size++) {
		buf[size] = (*data >> offset) & 0x7f;
		if (!offset) {
			offset = 24;
			data++;
		} else {
			offset -= 8;
		}
	}

	if (status == UMP_SYSEX_STATUS_SINGLE ||
	    status == UMP_SYSEX_STATUS_END)
		buf[size++] = UMP_MIDI1_MSG_SYSEX_END;

	return size;
}

/**
 * snd_ump_convert_from_ump - convert from UMP to legacy MIDI
 * @data: UMP packet
 * @buf: buffer to store legacy MIDI data
 * @group_ret: pointer to store the target group
 *
 * Convert from a UMP packet @data to MIDI 1.0 bytes at @buf.
 * The target group is stored at @group_ret.
 *
 * The function returns the number of bytes of MIDI 1.0 stream.
 */
int snd_ump_convert_from_ump(const u32 *data,
			     unsigned char *buf,
			     unsigned char *group_ret)
{
	*group_ret = ump_message_group(*data);

	switch (ump_message_type(*data)) {
	case UMP_MSG_TYPE_SYSTEM:
		return cvt_ump_system_to_legacy(*data, buf);
	case UMP_MSG_TYPE_MIDI1_CHANNEL_VOICE:
		return cvt_ump_midi1_to_legacy(*data, buf);
	case UMP_MSG_TYPE_MIDI2_CHANNEL_VOICE:
		return cvt_ump_midi2_to_legacy((const union snd_ump_midi2_msg *)data,
					       buf);
	case UMP_MSG_TYPE_DATA:
		return cvt_ump_sysex7_to_legacy(data, buf);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_ump_convert_from_ump);

/*
 * MIDI 1 byte stream -> UMP conversion
 */
/* convert MIDI 1.0 SysEx to a UMP packet */
static int cvt_legacy_sysex_to_ump(struct ump_cvt_to_ump *cvt,
				   unsigned char group, u32 *data, bool finish)
{
	unsigned char status;
	bool start = cvt->in_sysex == 1;
	int i, offset;

	if (start && finish)
		status = UMP_SYSEX_STATUS_SINGLE;
	else if (start)
		status = UMP_SYSEX_STATUS_START;
	else if (finish)
		status = UMP_SYSEX_STATUS_END;
	else
		status = UMP_SYSEX_STATUS_CONTINUE;
	*data = ump_compose(UMP_MSG_TYPE_DATA, group, status, cvt->len);
	offset = 8;
	for (i = 0; i < cvt->len; i++) {
		*data |= cvt->buf[i] << offset;
		if (!offset) {
			offset = 24;
			data++;
		} else
			offset -= 8;
	}
	cvt->len = 0;
	if (finish)
		cvt->in_sysex = 0;
	else
		cvt->in_sysex++;
	return 8;
}

/* convert to a UMP System message */
static int cvt_legacy_system_to_ump(struct ump_cvt_to_ump *cvt,
				    unsigned char group, u32 *data)
{
	data[0] = ump_compose(UMP_MSG_TYPE_SYSTEM, group, 0, cvt->buf[0]);
	if (cvt->cmd_bytes > 1)
		data[0] |= cvt->buf[1] << 8;
	if (cvt->cmd_bytes > 2)
		data[0] |= cvt->buf[2];
	return 4;
}

static void reset_rpn(struct ump_cvt_to_ump_bank *cc)
{
	cc->rpn_set = 0;
	cc->nrpn_set = 0;
	cc->cc_rpn_msb = cc->cc_rpn_lsb = 0;
	cc->cc_data_msb = cc->cc_data_lsb = 0;
	cc->cc_data_msb_set = cc->cc_data_lsb_set = 0;
}

static int fill_rpn(struct ump_cvt_to_ump_bank *cc,
		    union snd_ump_midi2_msg *midi2,
		    bool flush)
{
	if (!(cc->cc_data_lsb_set || cc->cc_data_msb_set))
		return 0; // skip
	/* when not flushing, wait for complete data set */
	if (!flush && (!cc->cc_data_lsb_set || !cc->cc_data_msb_set))
		return 0; // skip

	if (cc->rpn_set) {
		midi2->rpn.status = UMP_MSG_STATUS_RPN;
		midi2->rpn.bank = cc->cc_rpn_msb;
		midi2->rpn.index = cc->cc_rpn_lsb;
	} else if (cc->nrpn_set) {
		midi2->rpn.status = UMP_MSG_STATUS_NRPN;
		midi2->rpn.bank = cc->cc_nrpn_msb;
		midi2->rpn.index = cc->cc_nrpn_lsb;
	} else {
		return 0; // skip
	}

	midi2->rpn.data = upscale_14_to_32bit((cc->cc_data_msb << 7) |
					      cc->cc_data_lsb);

	reset_rpn(cc);
	return 1;
}

/* convert to a MIDI 1.0 Channel Voice message */
static int cvt_legacy_cmd_to_ump(struct ump_cvt_to_ump *cvt,
				 unsigned char group,
				 unsigned int protocol,
				 u32 *data, unsigned char bytes)
{
	const unsigned char *buf = cvt->buf;
	struct ump_cvt_to_ump_bank *cc;
	union snd_ump_midi2_msg *midi2 = (union snd_ump_midi2_msg *)data;
	unsigned char status, channel;
	int ret;

	BUILD_BUG_ON(sizeof(union snd_ump_midi1_msg) != 4);
	BUILD_BUG_ON(sizeof(union snd_ump_midi2_msg) != 8);

	/* for MIDI 1.0 UMP, it's easy, just pack it into UMP */
	if (protocol & SNDRV_UMP_EP_INFO_PROTO_MIDI1) {
		data[0] = ump_compose(UMP_MSG_TYPE_MIDI1_CHANNEL_VOICE,
				      group, 0, buf[0]);
		data[0] |= buf[1] << 8;
		if (bytes > 2)
			data[0] |= buf[2];
		return 4;
	}

	status = *buf >> 4;
	channel = *buf & 0x0f;
	cc = &cvt->bank[channel];

	/* special handling: treat note-on with 0 velocity as note-off */
	if (status == UMP_MSG_STATUS_NOTE_ON && !buf[2])
		status = UMP_MSG_STATUS_NOTE_OFF;

	/* initialize the packet */
	data[0] = ump_compose(UMP_MSG_TYPE_MIDI2_CHANNEL_VOICE,
			      group, status, channel);
	data[1] = 0;

	switch (status) {
	case UMP_MSG_STATUS_NOTE_ON:
	case UMP_MSG_STATUS_NOTE_OFF:
		midi2->note.note = buf[1];
		midi2->note.velocity = upscale_7_to_16bit(buf[2]);
		break;
	case UMP_MSG_STATUS_POLY_PRESSURE:
		midi2->paf.note = buf[1];
		midi2->paf.data = upscale_7_to_32bit(buf[2]);
		break;
	case UMP_MSG_STATUS_CC:
		switch (buf[1]) {
		case UMP_CC_RPN_MSB:
			ret = fill_rpn(cc, midi2, true);
			cc->rpn_set = 1;
			cc->cc_rpn_msb = buf[2];
			if (cc->cc_rpn_msb == 0x7f && cc->cc_rpn_lsb == 0x7f)
				reset_rpn(cc);
			return ret;
		case UMP_CC_RPN_LSB:
			ret = fill_rpn(cc, midi2, true);
			cc->rpn_set = 1;
			cc->cc_rpn_lsb = buf[2];
			if (cc->cc_rpn_msb == 0x7f && cc->cc_rpn_lsb == 0x7f)
				reset_rpn(cc);
			return ret;
		case UMP_CC_NRPN_MSB:
			ret = fill_rpn(cc, midi2, true);
			cc->nrpn_set = 1;
			cc->cc_nrpn_msb = buf[2];
			return ret;
		case UMP_CC_NRPN_LSB:
			ret = fill_rpn(cc, midi2, true);
			cc->nrpn_set = 1;
			cc->cc_nrpn_lsb = buf[2];
			return ret;
		case UMP_CC_DATA:
			cc->cc_data_msb_set = 1;
			cc->cc_data_msb = buf[2];
			return fill_rpn(cc, midi2, false);
		case UMP_CC_BANK_SELECT:
			cc->bank_set = 1;
			cc->cc_bank_msb = buf[2];
			return 0; // skip
		case UMP_CC_BANK_SELECT_LSB:
			cc->bank_set = 1;
			cc->cc_bank_lsb = buf[2];
			return 0; // skip
		case UMP_CC_DATA_LSB:
			cc->cc_data_lsb_set = 1;
			cc->cc_data_lsb = buf[2];
			return fill_rpn(cc, midi2, false);
		default:
			midi2->cc.index = buf[1];
			midi2->cc.data = upscale_7_to_32bit(buf[2]);
			break;
		}
		break;
	case UMP_MSG_STATUS_PROGRAM:
		midi2->pg.program = buf[1];
		if (cc->bank_set) {
			midi2->pg.bank_valid = 1;
			midi2->pg.bank_msb = cc->cc_bank_msb;
			midi2->pg.bank_lsb = cc->cc_bank_lsb;
			cc->bank_set = 0;
		}
		break;
	case UMP_MSG_STATUS_CHANNEL_PRESSURE:
		midi2->caf.data = upscale_7_to_32bit(buf[1]);
		break;
	case UMP_MSG_STATUS_PITCH_BEND:
		midi2->pb.data = upscale_14_to_32bit(buf[1] | (buf[2] << 7));
		break;
	default:
		return 0;
	}

	return 8;
}

static int do_convert_to_ump(struct ump_cvt_to_ump *cvt, unsigned char group,
			     unsigned int protocol, unsigned char c, u32 *data)
{
	/* bytes for 0x80-0xf0 */
	static unsigned char cmd_bytes[8] = {
		3, 3, 3, 3, 2, 2, 3, 0
	};
	/* bytes for 0xf0-0xff */
	static unsigned char system_bytes[16] = {
		0, 2, 3, 2, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 1
	};
	unsigned char bytes;

	if (c == UMP_MIDI1_MSG_SYSEX_START) {
		cvt->in_sysex = 1;
		cvt->len = 0;
		return 0;
	}
	if (c == UMP_MIDI1_MSG_SYSEX_END) {
		if (!cvt->in_sysex)
			return 0; /* skip */
		return cvt_legacy_sysex_to_ump(cvt, group, data, true);
	}

	if ((c & 0xf0) == UMP_MIDI1_MSG_REALTIME) {
		bytes = system_bytes[c & 0x0f];
		if (!bytes)
			return 0; /* skip */
		if (bytes == 1) {
			data[0] = ump_compose(UMP_MSG_TYPE_SYSTEM, group, 0, c);
			return 4;
		}
		cvt->buf[0] = c;
		cvt->len = 1;
		cvt->cmd_bytes = bytes;
		cvt->in_sysex = 0; /* abort SysEx */
		return 0;
	}

	if (c & 0x80) {
		bytes = cmd_bytes[(c >> 4) & 7];
		cvt->buf[0] = c;
		cvt->len = 1;
		cvt->cmd_bytes = bytes;
		cvt->in_sysex = 0; /* abort SysEx */
		return 0;
	}

	if (cvt->in_sysex) {
		cvt->buf[cvt->len++] = c;
		if (cvt->len == 6)
			return cvt_legacy_sysex_to_ump(cvt, group, data, false);
		return 0;
	}

	if (!cvt->len)
		return 0;

	cvt->buf[cvt->len++] = c;
	if (cvt->len < cvt->cmd_bytes)
		return 0;
	cvt->len = 1;
	if ((cvt->buf[0] & 0xf0) == UMP_MIDI1_MSG_REALTIME)
		return cvt_legacy_system_to_ump(cvt, group, data);
	return cvt_legacy_cmd_to_ump(cvt, group, protocol, data, cvt->cmd_bytes);
}

/**
 * snd_ump_convert_to_ump - convert legacy MIDI byte to UMP packet
 * @cvt: converter context
 * @group: target UMP group
 * @protocol: target UMP protocol
 * @c: MIDI 1.0 byte data
 *
 * Feed a MIDI 1.0 byte @c and convert to a UMP packet if completed.
 * The result is stored in the buffer in @cvt.
 */
void snd_ump_convert_to_ump(struct ump_cvt_to_ump *cvt, unsigned char group,
			    unsigned int protocol, unsigned char c)
{
	cvt->ump_bytes = do_convert_to_ump(cvt, group, protocol, c, cvt->ump);
}
EXPORT_SYMBOL_GPL(snd_ump_convert_to_ump);
