/*
 * oxfw-scs1x.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2015 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

#define HSS1394_ADDRESS			0xc007dedadadaULL
#define HSS1394_MAX_PACKET_SIZE		64
#define HSS1394_TAG_USER_DATA		0x00
#define HSS1394_TAG_CHANGE_ADDRESS	0xf1

struct fw_scs1x {
	struct fw_address_handler hss_handler;
	u8 input_escape_count;
	struct snd_rawmidi_substream *input;

	/* For MIDI playback. */
	struct snd_rawmidi_substream *output;
	bool output_idle;
	u8 output_status;
	u8 output_bytes;
	bool output_escaped;
	bool output_escape_high_nibble;
	struct tasklet_struct tasklet;
	wait_queue_head_t idle_wait;
	u8 buffer[HSS1394_MAX_PACKET_SIZE];
	bool transaction_running;
	struct fw_transaction transaction;
	struct fw_device *fw_dev;
};

static const u8 sysex_escape_prefix[] = {
	0xf0,			/* SysEx begin */
	0x00, 0x01, 0x60,	/* Stanton DJ */
	0x48, 0x53, 0x53,	/* "HSS" */
};

static void midi_input_escaped_byte(struct snd_rawmidi_substream *stream,
				    u8 byte)
{
	u8 nibbles[2];

	nibbles[0] = byte >> 4;
	nibbles[1] = byte & 0x0f;
	snd_rawmidi_receive(stream, nibbles, 2);
}

static void midi_input_byte(struct fw_scs1x *scs,
			    struct snd_rawmidi_substream *stream, u8 byte)
{
	const u8 eox = 0xf7;

	if (scs->input_escape_count > 0) {
		midi_input_escaped_byte(stream, byte);
		scs->input_escape_count--;
		if (scs->input_escape_count == 0)
			snd_rawmidi_receive(stream, &eox, sizeof(eox));
	} else if (byte == 0xf9) {
		snd_rawmidi_receive(stream, sysex_escape_prefix,
				    ARRAY_SIZE(sysex_escape_prefix));
		midi_input_escaped_byte(stream, 0x00);
		midi_input_escaped_byte(stream, 0xf9);
		scs->input_escape_count = 3;
	} else {
		snd_rawmidi_receive(stream, &byte, 1);
	}
}

static void midi_input_packet(struct fw_scs1x *scs,
			      struct snd_rawmidi_substream *stream,
			      const u8 *data, unsigned int bytes)
{
	unsigned int i;
	const u8 eox = 0xf7;

	if (data[0] == HSS1394_TAG_USER_DATA) {
		for (i = 1; i < bytes; ++i)
			midi_input_byte(scs, stream, data[i]);
	} else {
		snd_rawmidi_receive(stream, sysex_escape_prefix,
				    ARRAY_SIZE(sysex_escape_prefix));
		for (i = 0; i < bytes; ++i)
			midi_input_escaped_byte(stream, data[i]);
		snd_rawmidi_receive(stream, &eox, sizeof(eox));
	}
}

static void handle_hss(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source, int generation,
		       unsigned long long offset, void *data, size_t length,
		       void *callback_data)
{
	struct fw_scs1x *scs = callback_data;
	struct snd_rawmidi_substream *stream;
	int rcode;

	if (offset != scs->hss_handler.offset) {
		rcode = RCODE_ADDRESS_ERROR;
		goto end;
	}
	if (tcode != TCODE_WRITE_QUADLET_REQUEST &&
	    tcode != TCODE_WRITE_BLOCK_REQUEST) {
		rcode = RCODE_TYPE_ERROR;
		goto end;
	}

	if (length >= 1) {
		stream = ACCESS_ONCE(scs->input);
		if (stream)
			midi_input_packet(scs, stream, data, length);
	}

	rcode = RCODE_COMPLETE;
end:
	fw_send_response(card, request, rcode);
}

static void scs_write_callback(struct fw_card *card, int rcode,
			       void *data, size_t length, void *callback_data)
{
	struct fw_scs1x *scs = callback_data;

	if (rcode == RCODE_GENERATION)
		;	/* TODO: retry this packet */

	scs->transaction_running = false;
	tasklet_schedule(&scs->tasklet);
}

static bool is_valid_running_status(u8 status)
{
	return status >= 0x80 && status <= 0xef;
}

static bool is_one_byte_cmd(u8 status)
{
	return status == 0xf6 ||
	       status >= 0xf8;
}

static bool is_two_bytes_cmd(u8 status)
{
	return (status >= 0xc0 && status <= 0xdf) ||
	       status == 0xf1 ||
	       status == 0xf3;
}

static bool is_three_bytes_cmd(u8 status)
{
	return (status >= 0x80 && status <= 0xbf) ||
	       (status >= 0xe0 && status <= 0xef) ||
	       status == 0xf2;
}

static bool is_invalid_cmd(u8 status)
{
	return status == 0xf4 ||
	       status == 0xf5 ||
	       status == 0xf9 ||
	       status == 0xfd;
}

static void scs_output_tasklet(unsigned long data)
{
	struct fw_scs1x *scs = (struct fw_scs1x *)data;
	struct snd_rawmidi_substream *stream;
	unsigned int i;
	u8 byte;
	int generation;

	if (scs->transaction_running)
		return;

	stream = ACCESS_ONCE(scs->output);
	if (!stream) {
		scs->output_idle = true;
		wake_up(&scs->idle_wait);
		return;
	}

	i = scs->output_bytes;
	for (;;) {
		if (snd_rawmidi_transmit(stream, &byte, 1) != 1) {
			scs->output_bytes = i;
			scs->output_idle = true;
			wake_up(&scs->idle_wait);
			return;
		}
		/*
		 * Convert from real MIDI to what I think the device expects (no
		 * running status, one command per packet, unescaped SysExs).
		 */
		if (scs->output_escaped && byte < 0x80) {
			if (scs->output_escape_high_nibble) {
				if (i < HSS1394_MAX_PACKET_SIZE) {
					scs->buffer[i] = byte << 4;
					scs->output_escape_high_nibble = false;
				}
			} else {
				scs->buffer[i++] |= byte & 0x0f;
				scs->output_escape_high_nibble = true;
			}
		} else if (byte < 0x80) {
			if (i == 1) {
				if (!is_valid_running_status(
							scs->output_status))
					continue;
				scs->buffer[0] = HSS1394_TAG_USER_DATA;
				scs->buffer[i++] = scs->output_status;
			}
			scs->buffer[i++] = byte;
			if ((i == 3 && is_two_bytes_cmd(scs->output_status)) ||
			    (i == 4 && is_three_bytes_cmd(scs->output_status)))
				break;
			if (i == 1 + ARRAY_SIZE(sysex_escape_prefix) &&
			    !memcmp(scs->buffer + 1, sysex_escape_prefix,
				    ARRAY_SIZE(sysex_escape_prefix))) {
				scs->output_escaped = true;
				scs->output_escape_high_nibble = true;
				i = 0;
			}
			if (i >= HSS1394_MAX_PACKET_SIZE)
				i = 1;
		} else if (byte == 0xf7) {
			if (scs->output_escaped) {
				if (i >= 1 && scs->output_escape_high_nibble &&
				    scs->buffer[0] !=
						HSS1394_TAG_CHANGE_ADDRESS)
					break;
			} else {
				if (i > 1 && scs->output_status == 0xf0) {
					scs->buffer[i++] = 0xf7;
					break;
				}
			}
			i = 1;
			scs->output_escaped = false;
		} else if (!is_invalid_cmd(byte) && byte < 0xf8) {
			i = 1;
			scs->buffer[0] = HSS1394_TAG_USER_DATA;
			scs->buffer[i++] = byte;
			scs->output_status = byte;
			scs->output_escaped = false;
			if (is_one_byte_cmd(byte))
				break;
		}
	}
	scs->output_bytes = 1;
	scs->output_escaped = false;

	scs->transaction_running = true;
	generation = scs->fw_dev->generation;
	smp_rmb(); /* node_id vs. generation */
	fw_send_request(scs->fw_dev->card, &scs->transaction,
			TCODE_WRITE_BLOCK_REQUEST, scs->fw_dev->node_id,
			generation, scs->fw_dev->max_speed, HSS1394_ADDRESS,
			scs->buffer, i, scs_write_callback, scs);
}

static int midi_capture_open(struct snd_rawmidi_substream *stream)
{
	return 0;
}

static int midi_capture_close(struct snd_rawmidi_substream *stream)
{
	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *stream, int up)
{
	struct fw_scs1x *scs = stream->rmidi->private_data;

	if (up) {
		scs->input_escape_count = 0;
		ACCESS_ONCE(scs->input) = stream;
	} else {
		ACCESS_ONCE(scs->input) = NULL;
	}
}

static struct snd_rawmidi_ops midi_capture_ops = {
	.open    = midi_capture_open,
	.close   = midi_capture_close,
	.trigger = midi_capture_trigger,
};

static int midi_playback_open(struct snd_rawmidi_substream *stream)
{
	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *stream)
{
	return 0;
}

static void midi_playback_trigger(struct snd_rawmidi_substream *stream, int up)
{
	struct fw_scs1x *scs = stream->rmidi->private_data;

	if (up) {
		scs->output_status = 0;
		scs->output_bytes = 1;
		scs->output_escaped = false;
		scs->output_idle = false;

		ACCESS_ONCE(scs->output) = stream;
		tasklet_schedule(&scs->tasklet);
	} else {
		ACCESS_ONCE(scs->output) = NULL;
	}
}
static void midi_playback_drain(struct snd_rawmidi_substream *stream)
{
	struct fw_scs1x *scs = stream->rmidi->private_data;

	wait_event(scs->idle_wait, scs->output_idle);
}

static struct snd_rawmidi_ops midi_playback_ops = {
	.open    = midi_playback_open,
	.close   = midi_playback_close,
	.trigger = midi_playback_trigger,
	.drain   = midi_playback_drain,
};
static int register_address(struct snd_oxfw *oxfw)
{
	struct fw_scs1x *scs = oxfw->spec;
	__be64 data;

	data = cpu_to_be64(((u64)HSS1394_TAG_CHANGE_ADDRESS << 56) |
			    scs->hss_handler.offset);
	return snd_fw_transaction(oxfw->unit, TCODE_WRITE_BLOCK_REQUEST,
				  HSS1394_ADDRESS, &data, sizeof(data), 0);
}

static void remove_scs1x(struct snd_rawmidi *rmidi)
{
	struct fw_scs1x *scs = rmidi->private_data;

	fw_core_remove_address_handler(&scs->hss_handler);
}

void snd_oxfw_scs1x_update(struct snd_oxfw *oxfw)
{
	register_address(oxfw);
}

int snd_oxfw_scs1x_add(struct snd_oxfw *oxfw)
{
	struct snd_rawmidi *rmidi;
	struct fw_scs1x *scs;
	int err;

	scs = kzalloc(sizeof(struct fw_scs1x), GFP_KERNEL);
	if (scs == NULL)
		return -ENOMEM;
	scs->fw_dev = fw_parent_device(oxfw->unit);
	oxfw->spec = scs;

	/* Allocate own handler for imcoming asynchronous transaction. */
	scs->hss_handler.length = HSS1394_MAX_PACKET_SIZE;
	scs->hss_handler.address_callback = handle_hss;
	scs->hss_handler.callback_data = scs;
	err = fw_core_add_address_handler(&scs->hss_handler,
					  &fw_high_memory_region);
	if (err < 0)
		return err;

	err = register_address(oxfw);
	if (err < 0)
		goto err_allocated;

	/* Use unique name for backward compatibility to scs1x module. */
	err = snd_rawmidi_new(oxfw->card, "SCS.1x", 0, 1, 1, &rmidi);
	if (err < 0)
		goto err_allocated;
	rmidi->private_data = scs;
	rmidi->private_free = remove_scs1x;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", oxfw->card->shortname);

	rmidi->info_flags = SNDRV_RAWMIDI_INFO_INPUT |
			    SNDRV_RAWMIDI_INFO_OUTPUT |
			    SNDRV_RAWMIDI_INFO_DUPLEX;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &midi_capture_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &midi_playback_ops);

	tasklet_init(&scs->tasklet, scs_output_tasklet, (unsigned long)scs);
	init_waitqueue_head(&scs->idle_wait);
	scs->output_idle = true;

	return 0;
err_allocated:
	fw_core_remove_address_handler(&scs->hss_handler);
	return err;
}
