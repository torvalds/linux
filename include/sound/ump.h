/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Universal MIDI Packet (UMP) Support
 */
#ifndef __SOUND_UMP_H
#define __SOUND_UMP_H

#include <sound/rawmidi.h>

struct snd_ump_endpoint;
struct snd_ump_block;
struct snd_ump_ops;
struct ump_cvt_to_ump;
struct snd_seq_ump_ops;

struct snd_ump_endpoint {
	struct snd_rawmidi core;	/* raw UMP access */

	struct snd_ump_endpoint_info info;

	const struct snd_ump_ops *ops;	/* UMP ops set by the driver */
	struct snd_rawmidi_substream *substreams[2];	/* opened substreams */

	void *private_data;
	void (*private_free)(struct snd_ump_endpoint *ump);

	struct list_head block_list;	/* list of snd_ump_block objects */

	/* intermediate buffer for UMP input */
	u32 input_buf[4];
	int input_buf_head;
	int input_pending;

	struct mutex open_mutex;

#if IS_ENABLED(CONFIG_SND_UMP_LEGACY_RAWMIDI)
	spinlock_t legacy_locks[2];
	struct snd_rawmidi *legacy_rmidi;
	struct snd_rawmidi_substream *legacy_substreams[2][SNDRV_UMP_MAX_GROUPS];

	/* for legacy output; need to open the actual substream unlike input */
	int legacy_out_opens;
	struct snd_rawmidi_file legacy_out_rfile;
	struct ump_cvt_to_ump *out_cvts;
#endif

#if IS_ENABLED(CONFIG_SND_SEQUENCER)
	struct snd_seq_device *seq_dev;
	const struct snd_seq_ump_ops *seq_ops;
	void *seq_client;
#endif
};

/* ops filled by UMP drivers */
struct snd_ump_ops {
	int (*open)(struct snd_ump_endpoint *ump, int dir);
	void (*close)(struct snd_ump_endpoint *ump, int dir);
	void (*trigger)(struct snd_ump_endpoint *ump, int dir, int up);
	void (*drain)(struct snd_ump_endpoint *ump, int dir);
};

/* ops filled by sequencer binding */
struct snd_seq_ump_ops {
	void (*input_receive)(struct snd_ump_endpoint *ump,
			      const u32 *data, int words);
};

struct snd_ump_block {
	struct snd_ump_block_info info;
	struct snd_ump_endpoint *ump;

	void *private_data;
	void (*private_free)(struct snd_ump_block *blk);

	struct list_head list;
};

#define rawmidi_to_ump(rmidi)	container_of(rmidi, struct snd_ump_endpoint, core)

int snd_ump_endpoint_new(struct snd_card *card, char *id, int device,
			 int output, int input,
			 struct snd_ump_endpoint **ump_ret);
int snd_ump_block_new(struct snd_ump_endpoint *ump, unsigned int blk,
		      unsigned int direction, unsigned int first_group,
		      unsigned int num_groups, struct snd_ump_block **blk_ret);
int snd_ump_receive(struct snd_ump_endpoint *ump, const u32 *buffer, int count);
int snd_ump_transmit(struct snd_ump_endpoint *ump, u32 *buffer, int count);

#if IS_ENABLED(CONFIG_SND_UMP_LEGACY_RAWMIDI)
int snd_ump_attach_legacy_rawmidi(struct snd_ump_endpoint *ump,
				  char *id, int device);
#else
static inline int snd_ump_attach_legacy_rawmidi(struct snd_ump_endpoint *ump,
						char *id, int device)
{
	return 0;
}
#endif

/*
 * Some definitions for UMP
 */

/* MIDI 2.0 Message Type */
enum {
	UMP_MSG_TYPE_UTILITY			= 0x00,
	UMP_MSG_TYPE_SYSTEM			= 0x01,
	UMP_MSG_TYPE_MIDI1_CHANNEL_VOICE	= 0x02,
	UMP_MSG_TYPE_DATA			= 0x03,
	UMP_MSG_TYPE_MIDI2_CHANNEL_VOICE	= 0x04,
	UMP_MSG_TYPE_EXTENDED_DATA		= 0x05,
};

/* MIDI 2.0 SysEx / Data Status; same values for both 7-bit and 8-bit SysEx */
enum {
	UMP_SYSEX_STATUS_SINGLE			= 0,
	UMP_SYSEX_STATUS_START			= 1,
	UMP_SYSEX_STATUS_CONTINUE		= 2,
	UMP_SYSEX_STATUS_END			= 3,
};

/*
 * Helpers for retrieving / filling bits from UMP
 */
/* get the message type (4bit) from a UMP packet (header) */
static inline unsigned char ump_message_type(u32 data)
{
	return data >> 28;
}

/* get the group number (0-based, 4bit) from a UMP packet (header) */
static inline unsigned char ump_message_group(u32 data)
{
	return (data >> 24) & 0x0f;
}

/* get the MIDI status code (4bit) from a UMP packet (header) */
static inline unsigned char ump_message_status_code(u32 data)
{
	return (data >> 20) & 0x0f;
}

/* get the MIDI channel number (0-based, 4bit) from a UMP packet (header) */
static inline unsigned char ump_message_channel(u32 data)
{
	return (data >> 16) & 0x0f;
}

/* get the MIDI status + channel combo byte (8bit) from a UMP packet (header) */
static inline unsigned char ump_message_status_channel(u32 data)
{
	return (data >> 16) & 0xff;
}

/* compose a UMP packet (header) from type, group and status values */
static inline u32 ump_compose(unsigned char type, unsigned char group,
			      unsigned char status, unsigned char channel)
{
	return ((u32)type << 28) | ((u32)group << 24) | ((u32)status << 20) |
		((u32)channel << 16);
}

/* get SysEx message status (for both 7 and 8bits) from a UMP packet (header) */
static inline unsigned char ump_sysex_message_status(u32 data)
{
	return (data >> 20) & 0xf;
}

/* get SysEx message length (for both 7 and 8bits) from a UMP packet (header) */
static inline unsigned char ump_sysex_message_length(u32 data)
{
	return (data >> 16) & 0xf;
}

#endif /* __SOUND_UMP_H */
