/*
 * oxfw_stream.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

#define AVC_GENERIC_FRAME_MAXIMUM_BYTES	512

/*
 * According to datasheet of Oxford Semiconductor:
 *  OXFW970: 32.0/44.1/48.0/96.0 Khz, 8 audio channels I/O
 *  OXFW971: 32.0/44.1/48.0/88.2/96.0/192.0 kHz, 16 audio channels I/O, MIDI I/O
 */
static const unsigned int oxfw_rate_table[] = {
	[0] = 32000,
	[1] = 44100,
	[2] = 48000,
	[3] = 88200,
	[4] = 96000,
	[5] = 192000,
};

/*
 * See Table 5.7 – Sampling frequency for Multi-bit Audio
 * in AV/C Stream Format Information Specification 1.1 (Apr 2005, 1394TA)
 */
static const unsigned int avc_stream_rate_table[] = {
	[0] = 0x02,
	[1] = 0x03,
	[2] = 0x04,
	[3] = 0x0a,
	[4] = 0x05,
	[5] = 0x07,
};

int snd_oxfw_stream_init_simplex(struct snd_oxfw *oxfw)
{
	int err;

	err = cmp_connection_init(&oxfw->in_conn, oxfw->unit,
				  CMP_INPUT, 0);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(&oxfw->rx_stream, oxfw->unit,
				AMDTP_OUT_STREAM, CIP_NONBLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(&oxfw->rx_stream);
		cmp_connection_destroy(&oxfw->in_conn);
	}
end:
	return err;
}

static void stop_stream(struct snd_oxfw *oxfw)
{
	amdtp_stream_pcm_abort(&oxfw->rx_stream);
	amdtp_stream_stop(&oxfw->rx_stream);
	cmp_connection_break(&oxfw->in_conn);
}

int snd_oxfw_stream_start_simplex(struct snd_oxfw *oxfw)
{
	int err = 0;

	if (amdtp_streaming_error(&oxfw->rx_stream))
		stop_stream(oxfw);

	if (amdtp_stream_running(&oxfw->rx_stream))
		goto end;

	err = cmp_connection_establish(&oxfw->in_conn,
			amdtp_stream_get_max_payload(&oxfw->rx_stream));
	if (err < 0)
		goto end;

	err = amdtp_stream_start(&oxfw->rx_stream,
				 oxfw->in_conn.resources.channel,
				 oxfw->in_conn.speed);
	if (err < 0)
		stop_stream(oxfw);
end:
	return err;
}

void snd_oxfw_stream_stop_simplex(struct snd_oxfw *oxfw)
{
	stop_stream(oxfw);
}

void snd_oxfw_stream_destroy_simplex(struct snd_oxfw *oxfw)
{
	stop_stream(oxfw);

	amdtp_stream_destroy(&oxfw->rx_stream);
	cmp_connection_destroy(&oxfw->in_conn);
}

void snd_oxfw_stream_update_simplex(struct snd_oxfw *oxfw)
{
	if (cmp_connection_update(&oxfw->in_conn) < 0)
		stop_stream(oxfw);
	else
		amdtp_stream_update(&oxfw->rx_stream);
}

int snd_oxfw_stream_get_current_formation(struct snd_oxfw *oxfw,
				enum avc_general_plug_dir dir,
				struct snd_oxfw_stream_formation *formation)
{
	u8 *format;
	unsigned int len;
	int err;

	len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
	format = kmalloc(len, GFP_KERNEL);
	if (format == NULL)
		return -ENOMEM;

	err = avc_stream_get_format_single(oxfw->unit, dir, 0, format, &len);
	if (err < 0)
		goto end;
	if (len < 3) {
		err = -EIO;
		goto end;
	}

	err = snd_oxfw_stream_parse_format(format, formation);
end:
	kfree(format);
	return err;
}

/*
 * See Table 6.16 - AM824 Stream Format
 *     Figure 6.19 - format_information field for AM824 Compound
 * in AV/C Stream Format Information Specification 1.1 (Apr 2005, 1394TA)
 * Also 'Clause 12 AM824 sequence adaption layers' in IEC 61883-6:2005
 */
int snd_oxfw_stream_parse_format(u8 *format,
				 struct snd_oxfw_stream_formation *formation)
{
	unsigned int i, e, channels, type;

	memset(formation, 0, sizeof(struct snd_oxfw_stream_formation));

	/*
	 * this module can support a hierarchy combination that:
	 *  Root:	Audio and Music (0x90)
	 *  Level 1:	AM824 Compound  (0x40)
	 */
	if ((format[0] != 0x90) || (format[1] != 0x40))
		return -ENOSYS;

	/* check the sampling rate */
	for (i = 0; i < ARRAY_SIZE(avc_stream_rate_table); i++) {
		if (format[2] == avc_stream_rate_table[i])
			break;
	}
	if (i == ARRAY_SIZE(avc_stream_rate_table))
		return -ENOSYS;

	formation->rate = oxfw_rate_table[i];

	for (e = 0; e < format[4]; e++) {
		channels = format[5 + e * 2];
		type = format[6 + e * 2];

		switch (type) {
		/* IEC 60958 Conformant, currently handled as MBLA */
		case 0x00:
		/* Multi Bit Linear Audio (Raw) */
		case 0x06:
			formation->pcm += channels;
			break;
		/* MIDI Conformant */
		case 0x0d:
			formation->midi = channels;
			break;
		/* IEC 61937-3 to 7 */
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		/* Multi Bit Linear Audio */
		case 0x07:	/* DVD-Audio */
		case 0x0c:	/* High Precision */
		/* One Bit Audio */
		case 0x08:	/* (Plain) Raw */
		case 0x09:	/* (Plain) SACD */
		case 0x0a:	/* (Encoded) Raw */
		case 0x0b:	/* (Encoded) SACD */
		/* SMPTE Time-Code conformant */
		case 0x0e:
		/* Sample Count */
		case 0x0f:
		/* Anciliary Data */
		case 0x10:
		/* Synchronization Stream (Stereo Raw audio) */
		case 0x40:
		/* Don't care */
		case 0xff:
		default:
			return -ENOSYS;	/* not supported */
		}
	}

	if (formation->pcm  > AMDTP_MAX_CHANNELS_FOR_PCM ||
	    formation->midi > AMDTP_MAX_CHANNELS_FOR_MIDI)
		return -ENOSYS;

	return 0;
}

static int
assume_stream_formats(struct snd_oxfw *oxfw, enum avc_general_plug_dir dir,
		      unsigned int pid, u8 *buf, unsigned int *len,
		      u8 **formats)
{
	struct snd_oxfw_stream_formation formation;
	unsigned int i, eid;
	int err;

	/* get format at current sampling rate */
	err = avc_stream_get_format_single(oxfw->unit, dir, pid, buf, len);
	if (err < 0) {
		dev_err(&oxfw->unit->device,
		"fail to get current stream format for isoc %s plug %d:%d\n",
			(dir == AVC_GENERAL_PLUG_DIR_IN) ? "in" : "out",
			pid, err);
		goto end;
	}

	/* parse and set stream format */
	eid = 0;
	err = snd_oxfw_stream_parse_format(buf, &formation);
	if (err < 0)
		goto end;

	formats[eid] = kmalloc(*len, GFP_KERNEL);
	if (formats[eid] == NULL) {
		err = -ENOMEM;
		goto end;
	}
	memcpy(formats[eid], buf, *len);

	/* apply the format for each available sampling rate */
	for (i = 0; i < ARRAY_SIZE(oxfw_rate_table); i++) {
		if (formation.rate == oxfw_rate_table[i])
			continue;

		err = avc_general_inquiry_sig_fmt(oxfw->unit,
						  oxfw_rate_table[i],
						  dir, pid);
		if (err < 0)
			continue;

		eid++;
		formats[eid] = kmalloc(*len, GFP_KERNEL);
		if (formats[eid] == NULL) {
			err = -ENOMEM;
			goto end;
		}
		memcpy(formats[eid], buf, *len);
		formats[eid][2] = avc_stream_rate_table[i];
	}

	err = 0;
	oxfw->assumed = true;
end:
	return err;
}

static int fill_stream_formats(struct snd_oxfw *oxfw,
			       enum avc_general_plug_dir dir,
			       unsigned short pid)
{
	u8 *buf, **formats;
	unsigned int len, eid = 0;
	struct snd_oxfw_stream_formation dummy;
	int err;

	buf = kmalloc(AVC_GENERIC_FRAME_MAXIMUM_BYTES, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	formats = oxfw->rx_stream_formats;

	/* get first entry */
	len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
	err = avc_stream_get_format_list(oxfw->unit, dir, 0, buf, &len, 0);
	if (err == -ENOSYS) {
		/* LIST subfunction is not implemented */
		len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
		err = assume_stream_formats(oxfw, dir, pid, buf, &len,
					    formats);
		goto end;
	} else if (err < 0) {
		dev_err(&oxfw->unit->device,
			"fail to get stream format %d for isoc %s plug %d:%d\n",
			eid, (dir == AVC_GENERAL_PLUG_DIR_IN) ? "in" : "out",
			pid, err);
		goto end;
	}

	/* LIST subfunction is implemented */
	while (eid < SND_OXFW_STREAM_FORMAT_ENTRIES) {
		/* The format is too short. */
		if (len < 3) {
			err = -EIO;
			break;
		}

		/* parse and set stream format */
		err = snd_oxfw_stream_parse_format(buf, &dummy);
		if (err < 0)
			break;

		formats[eid] = kmalloc(len, GFP_KERNEL);
		if (formats[eid] == NULL) {
			err = -ENOMEM;
			break;
		}
		memcpy(formats[eid], buf, len);

		/* get next entry */
		len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
		err = avc_stream_get_format_list(oxfw->unit, dir, 0,
						 buf, &len, ++eid);
		/* No entries remained. */
		if (err == -EINVAL) {
			err = 0;
			break;
		} else if (err < 0) {
			dev_err(&oxfw->unit->device,
			"fail to get stream format %d for isoc %s plug %d:%d\n",
				eid, (dir == AVC_GENERAL_PLUG_DIR_IN) ? "in" :
									"out",
				pid, err);
			break;
		}
	}
end:
	kfree(buf);
	return err;
}

int snd_oxfw_stream_discover(struct snd_oxfw *oxfw)
{
	u8 plugs[AVC_PLUG_INFO_BUF_BYTES];
	int err;

	/* the number of plugs for isoc in/out, ext in/out  */
	err = avc_general_get_plug_info(oxfw->unit, 0x1f, 0x07, 0x00, plugs);
	if (err < 0) {
		dev_err(&oxfw->unit->device,
		"fail to get info for isoc/external in/out plugs: %d\n",
			err);
		goto end;
	} else if (plugs[0] == 0) {
		err = -ENOSYS;
		goto end;
	}

	/* use iPCR[0] if exists */
	if (plugs[0] > 0)
		err = fill_stream_formats(oxfw, AVC_GENERAL_PLUG_DIR_IN, 0);
end:
	return err;
}
