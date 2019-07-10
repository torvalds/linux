// SPDX-License-Identifier: GPL-2.0-only
/*
 * bebob_stream.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 */

#include "./bebob.h"

#define CALLBACK_TIMEOUT	2000
#define FW_ISO_RESOURCE_DELAY	1000

/*
 * NOTE;
 * For BeBoB streams, Both of input and output CMP connection are important.
 *
 * For most devices, each CMP connection starts to transmit/receive a
 * corresponding stream. But for a few devices, both of CMP connection needs
 * to start transmitting stream. An example is 'M-Audio Firewire 410'.
 */

/* 128 is an arbitrary length but it seems to be enough */
#define FORMAT_MAXIMUM_LENGTH 128

const unsigned int snd_bebob_rate_table[SND_BEBOB_STRM_FMT_ENTRIES] = {
	[0] = 32000,
	[1] = 44100,
	[2] = 48000,
	[3] = 88200,
	[4] = 96000,
	[5] = 176400,
	[6] = 192000,
};

/*
 * See: Table 51: Extended Stream Format Info ‘Sampling Frequency’
 * in Additional AVC commands (Nov 2003, BridgeCo)
 */
static const unsigned int bridgeco_freq_table[] = {
	[0] = 0x02,
	[1] = 0x03,
	[2] = 0x04,
	[3] = 0x0a,
	[4] = 0x05,
	[5] = 0x06,
	[6] = 0x07,
};

static int
get_formation_index(unsigned int rate, unsigned int *index)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(snd_bebob_rate_table); i++) {
		if (snd_bebob_rate_table[i] == rate) {
			*index = i;
			return 0;
		}
	}
	return -EINVAL;
}

int
snd_bebob_stream_get_rate(struct snd_bebob *bebob, unsigned int *curr_rate)
{
	unsigned int tx_rate, rx_rate, trials;
	int err;

	trials = 0;
	do {
		err = avc_general_get_sig_fmt(bebob->unit, &tx_rate,
					      AVC_GENERAL_PLUG_DIR_OUT, 0);
	} while (err == -EAGAIN && ++trials < 3);
	if (err < 0)
		goto end;

	trials = 0;
	do {
		err = avc_general_get_sig_fmt(bebob->unit, &rx_rate,
					      AVC_GENERAL_PLUG_DIR_IN, 0);
	} while (err == -EAGAIN && ++trials < 3);
	if (err < 0)
		goto end;

	*curr_rate = rx_rate;
	if (rx_rate == tx_rate)
		goto end;

	/* synchronize receive stream rate to transmit stream rate */
	err = avc_general_set_sig_fmt(bebob->unit, rx_rate,
				      AVC_GENERAL_PLUG_DIR_IN, 0);
end:
	return err;
}

int
snd_bebob_stream_set_rate(struct snd_bebob *bebob, unsigned int rate)
{
	int err;

	err = avc_general_set_sig_fmt(bebob->unit, rate,
				      AVC_GENERAL_PLUG_DIR_OUT, 0);
	if (err < 0)
		goto end;

	err = avc_general_set_sig_fmt(bebob->unit, rate,
				      AVC_GENERAL_PLUG_DIR_IN, 0);
	if (err < 0)
		goto end;

	/*
	 * Some devices need a bit time for transition.
	 * 300msec is got by some experiments.
	 */
	msleep(300);
end:
	return err;
}

int snd_bebob_stream_get_clock_src(struct snd_bebob *bebob,
				   enum snd_bebob_clock_type *src)
{
	const struct snd_bebob_clock_spec *clk_spec = bebob->spec->clock;
	u8 addr[AVC_BRIDGECO_ADDR_BYTES], input[7];
	unsigned int id;
	enum avc_bridgeco_plug_type type;
	int err = 0;

	/* 1.The device has its own operation to switch source of clock */
	if (clk_spec) {
		err = clk_spec->get(bebob, &id);
		if (err < 0) {
			dev_err(&bebob->unit->device,
				"fail to get clock source: %d\n", err);
			goto end;
		}

		if (id >= clk_spec->num) {
			dev_err(&bebob->unit->device,
				"clock source %d out of range 0..%d\n",
				id, clk_spec->num - 1);
			err = -EIO;
			goto end;
		}

		*src = clk_spec->types[id];
		goto end;
	}

	/*
	 * 2.The device don't support to switch source of clock then assumed
	 *   to use internal clock always
	 */
	if (bebob->sync_input_plug < 0) {
		*src = SND_BEBOB_CLOCK_TYPE_INTERNAL;
		goto end;
	}

	/*
	 * 3.The device supports to switch source of clock by an usual way.
	 *   Let's check input for 'Music Sub Unit Sync Input' plug.
	 */
	avc_bridgeco_fill_msu_addr(addr, AVC_BRIDGECO_PLUG_DIR_IN,
				   bebob->sync_input_plug);
	err = avc_bridgeco_get_plug_input(bebob->unit, addr, input);
	if (err < 0) {
		dev_err(&bebob->unit->device,
			"fail to get an input for MSU in plug %d: %d\n",
			bebob->sync_input_plug, err);
		goto end;
	}

	/*
	 * If there are no input plugs, all of fields are 0xff.
	 * Here check the first field. This field is used for direction.
	 */
	if (input[0] == 0xff) {
		*src = SND_BEBOB_CLOCK_TYPE_INTERNAL;
		goto end;
	}

	/* The source from any output plugs is for one purpose only. */
	if (input[0] == AVC_BRIDGECO_PLUG_DIR_OUT) {
		/*
		 * In BeBoB architecture, the source from music subunit may
		 * bypass from oPCR[0]. This means that this source gives
		 * synchronization to IEEE 1394 cycle start packet.
		 */
		if (input[1] == AVC_BRIDGECO_PLUG_MODE_SUBUNIT &&
		    input[2] == 0x0c) {
			*src = SND_BEBOB_CLOCK_TYPE_INTERNAL;
			goto end;
		}
	/* The source from any input units is for several purposes. */
	} else if (input[1] == AVC_BRIDGECO_PLUG_MODE_UNIT) {
		if (input[2] == AVC_BRIDGECO_PLUG_UNIT_ISOC) {
			if (input[3] == 0x00) {
				/*
				 * This source comes from iPCR[0]. This means
				 * that presentation timestamp calculated by
				 * SYT series of the received packets. In
				 * short, this driver is the master of
				 * synchronization.
				 */
				*src = SND_BEBOB_CLOCK_TYPE_SYT;
				goto end;
			} else {
				/*
				 * This source comes from iPCR[1-29]. This
				 * means that the synchronization stream is not
				 * the Audio/MIDI compound stream.
				 */
				*src = SND_BEBOB_CLOCK_TYPE_EXTERNAL;
				goto end;
			}
		} else if (input[2] == AVC_BRIDGECO_PLUG_UNIT_EXT) {
			/* Check type of this plug.  */
			avc_bridgeco_fill_unit_addr(addr,
						    AVC_BRIDGECO_PLUG_DIR_IN,
						    AVC_BRIDGECO_PLUG_UNIT_EXT,
						    input[3]);
			err = avc_bridgeco_get_plug_type(bebob->unit, addr,
							 &type);
			if (err < 0)
				goto end;

			if (type == AVC_BRIDGECO_PLUG_TYPE_DIG) {
				/*
				 * SPDIF/ADAT or sometimes (not always) word
				 * clock.
				 */
				*src = SND_BEBOB_CLOCK_TYPE_EXTERNAL;
				goto end;
			} else if (type == AVC_BRIDGECO_PLUG_TYPE_SYNC) {
				/* Often word clock. */
				*src = SND_BEBOB_CLOCK_TYPE_EXTERNAL;
				goto end;
			} else if (type == AVC_BRIDGECO_PLUG_TYPE_ADDITION) {
				/*
				 * Not standard.
				 * Mostly, additional internal clock.
				 */
				*src = SND_BEBOB_CLOCK_TYPE_INTERNAL;
				goto end;
			}
		}
	}

	/* Not supported. */
	err = -EIO;
end:
	return err;
}

static unsigned int
map_data_channels(struct snd_bebob *bebob, struct amdtp_stream *s)
{
	unsigned int sec, sections, ch, channels;
	unsigned int pcm, midi, location;
	unsigned int stm_pos, sec_loc, pos;
	u8 *buf, addr[AVC_BRIDGECO_ADDR_BYTES], type;
	enum avc_bridgeco_plug_dir dir;
	int err;

	/*
	 * The length of return value of this command cannot be expected. Here
	 * use the maximum length of FCP.
	 */
	buf = kzalloc(256, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (s == &bebob->tx_stream)
		dir = AVC_BRIDGECO_PLUG_DIR_OUT;
	else
		dir = AVC_BRIDGECO_PLUG_DIR_IN;

	avc_bridgeco_fill_unit_addr(addr, dir, AVC_BRIDGECO_PLUG_UNIT_ISOC, 0);
	err = avc_bridgeco_get_plug_ch_pos(bebob->unit, addr, buf, 256);
	if (err < 0) {
		dev_err(&bebob->unit->device,
			"fail to get channel position for isoc %s plug 0: %d\n",
			(dir == AVC_BRIDGECO_PLUG_DIR_IN) ? "in" : "out",
			err);
		goto end;
	}
	pos = 0;

	/* positions in I/O buffer */
	pcm = 0;
	midi = 0;

	/* the number of sections in AMDTP packet */
	sections = buf[pos++];

	for (sec = 0; sec < sections; sec++) {
		/* type of this section */
		avc_bridgeco_fill_unit_addr(addr, dir,
					    AVC_BRIDGECO_PLUG_UNIT_ISOC, 0);
		err = avc_bridgeco_get_plug_section_type(bebob->unit, addr,
							 sec, &type);
		if (err < 0) {
			dev_err(&bebob->unit->device,
			"fail to get section type for isoc %s plug 0: %d\n",
				(dir == AVC_BRIDGECO_PLUG_DIR_IN) ? "in" :
								    "out",
				err);
			goto end;
		}
		/* NoType */
		if (type == 0xff) {
			err = -ENOSYS;
			goto end;
		}

		/* the number of channels in this section */
		channels = buf[pos++];

		for (ch = 0; ch < channels; ch++) {
			/* position of this channel in AMDTP packet */
			stm_pos = buf[pos++] - 1;
			/* location of this channel in this section */
			sec_loc = buf[pos++] - 1;

			/*
			 * Basically the number of location is within the
			 * number of channels in this section. But some models
			 * of M-Audio don't follow this. Its location for MIDI
			 * is the position of MIDI channels in AMDTP packet.
			 */
			if (sec_loc >= channels)
				sec_loc = ch;

			switch (type) {
			/* for MIDI conformant data channel */
			case 0x0a:
				/* AMDTP_MAX_CHANNELS_FOR_MIDI is 1. */
				if ((midi > 0) && (stm_pos != midi)) {
					err = -ENOSYS;
					goto end;
				}
				amdtp_am824_set_midi_position(s, stm_pos);
				midi = stm_pos;
				break;
			/* for PCM data channel */
			case 0x01:	/* Headphone */
			case 0x02:	/* Microphone */
			case 0x03:	/* Line */
			case 0x04:	/* SPDIF */
			case 0x05:	/* ADAT */
			case 0x06:	/* TDIF */
			case 0x07:	/* MADI */
			/* for undefined/changeable signal  */
			case 0x08:	/* Analog */
			case 0x09:	/* Digital */
			default:
				location = pcm + sec_loc;
				if (location >= AM824_MAX_CHANNELS_FOR_PCM) {
					err = -ENOSYS;
					goto end;
				}
				amdtp_am824_set_pcm_position(s, location,
							     stm_pos);
				break;
			}
		}

		if (type != 0x0a)
			pcm += channels;
		else
			midi += channels;
	}
end:
	kfree(buf);
	return err;
}

static int
init_both_connections(struct snd_bebob *bebob)
{
	int err;

	err = cmp_connection_init(&bebob->in_conn,
				  bebob->unit, CMP_INPUT, 0);
	if (err < 0)
		goto end;

	err = cmp_connection_init(&bebob->out_conn,
				  bebob->unit, CMP_OUTPUT, 0);
	if (err < 0)
		cmp_connection_destroy(&bebob->in_conn);
end:
	return err;
}

static int
check_connection_used_by_others(struct snd_bebob *bebob, struct amdtp_stream *s)
{
	struct cmp_connection *conn;
	bool used;
	int err;

	if (s == &bebob->tx_stream)
		conn = &bebob->out_conn;
	else
		conn = &bebob->in_conn;

	err = cmp_connection_check_used(conn, &used);
	if ((err >= 0) && used && !amdtp_stream_running(s)) {
		dev_err(&bebob->unit->device,
			"Connection established by others: %cPCR[%d]\n",
			(conn->direction == CMP_OUTPUT) ? 'o' : 'i',
			conn->pcr_index);
		err = -EBUSY;
	}

	return err;
}

static int
make_both_connections(struct snd_bebob *bebob, unsigned int rate)
{
	int index, pcm_channels, midi_channels, err = 0;

	if (bebob->connected)
		goto end;

	/* confirm params for both streams */
	err = get_formation_index(rate, &index);
	if (err < 0)
		goto end;
	pcm_channels = bebob->tx_stream_formations[index].pcm;
	midi_channels = bebob->tx_stream_formations[index].midi;
	err = amdtp_am824_set_parameters(&bebob->tx_stream, rate,
					 pcm_channels, midi_channels * 8,
					 false);
	if (err < 0)
		goto end;

	pcm_channels = bebob->rx_stream_formations[index].pcm;
	midi_channels = bebob->rx_stream_formations[index].midi;
	err = amdtp_am824_set_parameters(&bebob->rx_stream, rate,
					 pcm_channels, midi_channels * 8,
					 false);
	if (err < 0)
		goto end;

	/* establish connections for both streams */
	err = cmp_connection_establish(&bebob->out_conn,
			amdtp_stream_get_max_payload(&bebob->tx_stream));
	if (err < 0)
		goto end;
	err = cmp_connection_establish(&bebob->in_conn,
			amdtp_stream_get_max_payload(&bebob->rx_stream));
	if (err < 0) {
		cmp_connection_break(&bebob->out_conn);
		goto end;
	}

	bebob->connected = true;
end:
	return err;
}

static void
break_both_connections(struct snd_bebob *bebob)
{
	cmp_connection_break(&bebob->in_conn);
	cmp_connection_break(&bebob->out_conn);

	bebob->connected = false;

	/* These models seems to be in transition state for a longer time. */
	if (bebob->maudio_special_quirk != NULL)
		msleep(200);
}

static void
destroy_both_connections(struct snd_bebob *bebob)
{
	cmp_connection_destroy(&bebob->in_conn);
	cmp_connection_destroy(&bebob->out_conn);
}

static int
start_stream(struct snd_bebob *bebob, struct amdtp_stream *stream,
	     unsigned int rate)
{
	struct cmp_connection *conn;
	int err = 0;

	if (stream == &bebob->rx_stream)
		conn = &bebob->in_conn;
	else
		conn = &bebob->out_conn;

	/* channel mapping */
	if (bebob->maudio_special_quirk == NULL) {
		err = map_data_channels(bebob, stream);
		if (err < 0)
			goto end;
	}

	/* start amdtp stream */
	err = amdtp_stream_start(stream,
				 conn->resources.channel,
				 conn->speed);
end:
	return err;
}

int snd_bebob_stream_init_duplex(struct snd_bebob *bebob)
{
	int err;

	err = init_both_connections(bebob);
	if (err < 0)
		goto end;

	err = amdtp_am824_init(&bebob->tx_stream, bebob->unit,
			       AMDTP_IN_STREAM, CIP_BLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(&bebob->tx_stream);
		destroy_both_connections(bebob);
		goto end;
	}

	/*
	 * BeBoB v3 transfers packets with these qurks:
	 *  - In the beginning of streaming, the value of dbc is incremented
	 *    even if no data blocks are transferred.
	 *  - The value of dbc is reset suddenly.
	 */
	if (bebob->version > 2)
		bebob->tx_stream.flags |= CIP_EMPTY_HAS_WRONG_DBC |
					  CIP_SKIP_DBC_ZERO_CHECK;

	/*
	 * At high sampling rate, M-Audio special firmware transmits empty
	 * packet with the value of dbc incremented by 8 but the others are
	 * valid to IEC 61883-1.
	 */
	if (bebob->maudio_special_quirk)
		bebob->tx_stream.flags |= CIP_EMPTY_HAS_WRONG_DBC;

	err = amdtp_am824_init(&bebob->rx_stream, bebob->unit,
			       AMDTP_OUT_STREAM, CIP_BLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(&bebob->tx_stream);
		amdtp_stream_destroy(&bebob->rx_stream);
		destroy_both_connections(bebob);
	}
end:
	return err;
}

int snd_bebob_stream_start_duplex(struct snd_bebob *bebob, unsigned int rate)
{
	const struct snd_bebob_rate_spec *rate_spec = bebob->spec->rate;
	unsigned int curr_rate;
	int err = 0;

	/* Need no substreams */
	if (bebob->substreams_counter == 0)
		goto end;

	/*
	 * Considering JACK/FFADO streaming:
	 * TODO: This can be removed hwdep functionality becomes popular.
	 */
	err = check_connection_used_by_others(bebob, &bebob->rx_stream);
	if (err < 0)
		goto end;

	/*
	 * packet queueing error or detecting discontinuity
	 *
	 * At bus reset, connections should not be broken here. So streams need
	 * to be re-started. This is a reason to use SKIP_INIT_DBC_CHECK flag.
	 */
	if (amdtp_streaming_error(&bebob->rx_stream))
		amdtp_stream_stop(&bebob->rx_stream);
	if (amdtp_streaming_error(&bebob->tx_stream))
		amdtp_stream_stop(&bebob->tx_stream);
	if (!amdtp_stream_running(&bebob->rx_stream) &&
	    !amdtp_stream_running(&bebob->tx_stream))
		break_both_connections(bebob);

	/* stop streams if rate is different */
	err = rate_spec->get(bebob, &curr_rate);
	if (err < 0) {
		dev_err(&bebob->unit->device,
			"fail to get sampling rate: %d\n", err);
		goto end;
	}
	if (rate == 0)
		rate = curr_rate;
	if (rate != curr_rate) {
		amdtp_stream_stop(&bebob->rx_stream);
		amdtp_stream_stop(&bebob->tx_stream);
		break_both_connections(bebob);
	}

	/* master should be always running */
	if (!amdtp_stream_running(&bebob->rx_stream)) {
		/*
		 * NOTE:
		 * If establishing connections at first, Yamaha GO46
		 * (and maybe Terratec X24) don't generate sound.
		 *
		 * For firmware customized by M-Audio, refer to next NOTE.
		 */
		if (bebob->maudio_special_quirk == NULL) {
			err = rate_spec->set(bebob, rate);
			if (err < 0) {
				dev_err(&bebob->unit->device,
					"fail to set sampling rate: %d\n",
					err);
				goto end;
			}
		}

		err = make_both_connections(bebob, rate);
		if (err < 0)
			goto end;

		err = start_stream(bebob, &bebob->rx_stream, rate);
		if (err < 0) {
			dev_err(&bebob->unit->device,
				"fail to run AMDTP master stream:%d\n", err);
			break_both_connections(bebob);
			goto end;
		}

		/*
		 * NOTE:
		 * The firmware customized by M-Audio uses these commands to
		 * start transmitting stream. This is not usual way.
		 */
		if (bebob->maudio_special_quirk != NULL) {
			err = rate_spec->set(bebob, rate);
			if (err < 0) {
				dev_err(&bebob->unit->device,
					"fail to ensure sampling rate: %d\n",
					err);
				amdtp_stream_stop(&bebob->rx_stream);
				break_both_connections(bebob);
				goto end;
			}
		}

		/* wait first callback */
		if (!amdtp_stream_wait_callback(&bebob->rx_stream,
						CALLBACK_TIMEOUT)) {
			amdtp_stream_stop(&bebob->rx_stream);
			break_both_connections(bebob);
			err = -ETIMEDOUT;
			goto end;
		}
	}

	/* start slave if needed */
	if (!amdtp_stream_running(&bebob->tx_stream)) {
		err = start_stream(bebob, &bebob->tx_stream, rate);
		if (err < 0) {
			dev_err(&bebob->unit->device,
				"fail to run AMDTP slave stream:%d\n", err);
			amdtp_stream_stop(&bebob->rx_stream);
			break_both_connections(bebob);
			goto end;
		}

		/* wait first callback */
		if (!amdtp_stream_wait_callback(&bebob->tx_stream,
						CALLBACK_TIMEOUT)) {
			amdtp_stream_stop(&bebob->tx_stream);
			amdtp_stream_stop(&bebob->rx_stream);
			break_both_connections(bebob);
			err = -ETIMEDOUT;
		}
	}
end:
	return err;
}

void snd_bebob_stream_stop_duplex(struct snd_bebob *bebob)
{
	if (bebob->substreams_counter == 0) {
		amdtp_stream_pcm_abort(&bebob->rx_stream);
		amdtp_stream_stop(&bebob->rx_stream);

		amdtp_stream_pcm_abort(&bebob->tx_stream);
		amdtp_stream_stop(&bebob->tx_stream);

		break_both_connections(bebob);
	}
}

/*
 * This function should be called before starting streams or after stopping
 * streams.
 */
void snd_bebob_stream_destroy_duplex(struct snd_bebob *bebob)
{
	amdtp_stream_destroy(&bebob->rx_stream);
	amdtp_stream_destroy(&bebob->tx_stream);

	destroy_both_connections(bebob);
}

/*
 * See: Table 50: Extended Stream Format Info Format Hierarchy Level 2’
 * in Additional AVC commands (Nov 2003, BridgeCo)
 * Also 'Clause 12 AM824 sequence adaption layers' in IEC 61883-6:2005
 */
static int
parse_stream_formation(u8 *buf, unsigned int len,
		       struct snd_bebob_stream_formation *formation)
{
	unsigned int i, e, channels, format;

	/*
	 * this module can support a hierarchy combination that:
	 *  Root:	Audio and Music (0x90)
	 *  Level 1:	AM824 Compound  (0x40)
	 */
	if ((buf[0] != 0x90) || (buf[1] != 0x40))
		return -ENOSYS;

	/* check sampling rate */
	for (i = 0; i < ARRAY_SIZE(bridgeco_freq_table); i++) {
		if (buf[2] == bridgeco_freq_table[i])
			break;
	}
	if (i == ARRAY_SIZE(bridgeco_freq_table))
		return -ENOSYS;

	/* Avoid double count by different entries for the same rate. */
	memset(&formation[i], 0, sizeof(struct snd_bebob_stream_formation));

	for (e = 0; e < buf[4]; e++) {
		channels = buf[5 + e * 2];
		format = buf[6 + e * 2];

		switch (format) {
		/* IEC 60958 Conformant, currently handled as MBLA */
		case 0x00:
		/* Multi bit linear audio */
		case 0x06:	/* Raw */
			formation[i].pcm += channels;
			break;
		/* MIDI Conformant */
		case 0x0d:
			formation[i].midi += channels;
			break;
		/* IEC 61937-3 to 7 */
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		/* Multi bit linear audio */
		case 0x07:	/* DVD-Audio */
		case 0x0c:	/* High Precision */
		/* One Bit Audio */
		case 0x08:	/* (Plain) Raw */
		case 0x09:	/* (Plain) SACD */
		case 0x0a:	/* (Encoded) Raw */
		case 0x0b:	/* (Encoded) SACD */
		/* Synchronization Stream (Stereo Raw audio) */
		case 0x40:
		/* Don't care */
		case 0xff:
		default:
			return -ENOSYS;	/* not supported */
		}
	}

	if (formation[i].pcm  > AM824_MAX_CHANNELS_FOR_PCM ||
	    formation[i].midi > AM824_MAX_CHANNELS_FOR_MIDI)
		return -ENOSYS;

	return 0;
}

static int
fill_stream_formations(struct snd_bebob *bebob, enum avc_bridgeco_plug_dir dir,
		       unsigned short pid)
{
	u8 *buf;
	struct snd_bebob_stream_formation *formations;
	unsigned int len, eid;
	u8 addr[AVC_BRIDGECO_ADDR_BYTES];
	int err;

	buf = kmalloc(FORMAT_MAXIMUM_LENGTH, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (dir == AVC_BRIDGECO_PLUG_DIR_IN)
		formations = bebob->rx_stream_formations;
	else
		formations = bebob->tx_stream_formations;

	for (eid = 0; eid < SND_BEBOB_STRM_FMT_ENTRIES; eid++) {
		len = FORMAT_MAXIMUM_LENGTH;
		avc_bridgeco_fill_unit_addr(addr, dir,
					    AVC_BRIDGECO_PLUG_UNIT_ISOC, pid);
		err = avc_bridgeco_get_plug_strm_fmt(bebob->unit, addr, buf,
						     &len, eid);
		/* No entries remained. */
		if (err == -EINVAL && eid > 0) {
			err = 0;
			break;
		} else if (err < 0) {
			dev_err(&bebob->unit->device,
			"fail to get stream format %d for isoc %s plug %d:%d\n",
				eid,
				(dir == AVC_BRIDGECO_PLUG_DIR_IN) ? "in" :
								    "out",
				pid, err);
			break;
		}

		err = parse_stream_formation(buf, len, formations);
		if (err < 0)
			break;
	}

	kfree(buf);
	return err;
}

static int
seek_msu_sync_input_plug(struct snd_bebob *bebob)
{
	u8 plugs[AVC_PLUG_INFO_BUF_BYTES], addr[AVC_BRIDGECO_ADDR_BYTES];
	unsigned int i;
	enum avc_bridgeco_plug_type type;
	int err;

	/* Get the number of Music Sub Unit for both direction. */
	err = avc_general_get_plug_info(bebob->unit, 0x0c, 0x00, 0x00, plugs);
	if (err < 0) {
		dev_err(&bebob->unit->device,
			"fail to get info for MSU in/out plugs: %d\n",
			err);
		goto end;
	}

	/* seek destination plugs for 'MSU sync input' */
	bebob->sync_input_plug = -1;
	for (i = 0; i < plugs[0]; i++) {
		avc_bridgeco_fill_msu_addr(addr, AVC_BRIDGECO_PLUG_DIR_IN, i);
		err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
		if (err < 0) {
			dev_err(&bebob->unit->device,
				"fail to get type for MSU in plug %d: %d\n",
				i, err);
			goto end;
		}

		if (type == AVC_BRIDGECO_PLUG_TYPE_SYNC) {
			bebob->sync_input_plug = i;
			break;
		}
	}
end:
	return err;
}

int snd_bebob_stream_discover(struct snd_bebob *bebob)
{
	const struct snd_bebob_clock_spec *clk_spec = bebob->spec->clock;
	u8 plugs[AVC_PLUG_INFO_BUF_BYTES], addr[AVC_BRIDGECO_ADDR_BYTES];
	enum avc_bridgeco_plug_type type;
	unsigned int i;
	int err;

	/* the number of plugs for isoc in/out, ext in/out  */
	err = avc_general_get_plug_info(bebob->unit, 0x1f, 0x07, 0x00, plugs);
	if (err < 0) {
		dev_err(&bebob->unit->device,
		"fail to get info for isoc/external in/out plugs: %d\n",
			err);
		goto end;
	}

	/*
	 * This module supports at least one isoc input plug and one isoc
	 * output plug.
	 */
	if ((plugs[0] == 0) || (plugs[1] == 0)) {
		err = -ENOSYS;
		goto end;
	}

	avc_bridgeco_fill_unit_addr(addr, AVC_BRIDGECO_PLUG_DIR_IN,
				    AVC_BRIDGECO_PLUG_UNIT_ISOC, 0);
	err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
	if (err < 0) {
		dev_err(&bebob->unit->device,
			"fail to get type for isoc in plug 0: %d\n", err);
		goto end;
	} else if (type != AVC_BRIDGECO_PLUG_TYPE_ISOC) {
		err = -ENOSYS;
		goto end;
	}
	err = fill_stream_formations(bebob, AVC_BRIDGECO_PLUG_DIR_IN, 0);
	if (err < 0)
		goto end;

	avc_bridgeco_fill_unit_addr(addr, AVC_BRIDGECO_PLUG_DIR_OUT,
				    AVC_BRIDGECO_PLUG_UNIT_ISOC, 0);
	err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
	if (err < 0) {
		dev_err(&bebob->unit->device,
			"fail to get type for isoc out plug 0: %d\n", err);
		goto end;
	} else if (type != AVC_BRIDGECO_PLUG_TYPE_ISOC) {
		err = -ENOSYS;
		goto end;
	}
	err = fill_stream_formations(bebob, AVC_BRIDGECO_PLUG_DIR_OUT, 0);
	if (err < 0)
		goto end;

	/* count external input plugs for MIDI */
	bebob->midi_input_ports = 0;
	for (i = 0; i < plugs[2]; i++) {
		avc_bridgeco_fill_unit_addr(addr, AVC_BRIDGECO_PLUG_DIR_IN,
					    AVC_BRIDGECO_PLUG_UNIT_EXT, i);
		err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
		if (err < 0) {
			dev_err(&bebob->unit->device,
			"fail to get type for external in plug %d: %d\n",
				i, err);
			goto end;
		} else if (type == AVC_BRIDGECO_PLUG_TYPE_MIDI) {
			bebob->midi_input_ports++;
		}
	}

	/* count external output plugs for MIDI */
	bebob->midi_output_ports = 0;
	for (i = 0; i < plugs[3]; i++) {
		avc_bridgeco_fill_unit_addr(addr, AVC_BRIDGECO_PLUG_DIR_OUT,
					    AVC_BRIDGECO_PLUG_UNIT_EXT, i);
		err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
		if (err < 0) {
			dev_err(&bebob->unit->device,
			"fail to get type for external out plug %d: %d\n",
				i, err);
			goto end;
		} else if (type == AVC_BRIDGECO_PLUG_TYPE_MIDI) {
			bebob->midi_output_ports++;
		}
	}

	/* for check source of clock later */
	if (!clk_spec)
		err = seek_msu_sync_input_plug(bebob);
end:
	return err;
}

void snd_bebob_stream_lock_changed(struct snd_bebob *bebob)
{
	bebob->dev_lock_changed = true;
	wake_up(&bebob->hwdep_wait);
}

int snd_bebob_stream_lock_try(struct snd_bebob *bebob)
{
	int err;

	spin_lock_irq(&bebob->lock);

	/* user land lock this */
	if (bebob->dev_lock_count < 0) {
		err = -EBUSY;
		goto end;
	}

	/* this is the first time */
	if (bebob->dev_lock_count++ == 0)
		snd_bebob_stream_lock_changed(bebob);
	err = 0;
end:
	spin_unlock_irq(&bebob->lock);
	return err;
}

void snd_bebob_stream_lock_release(struct snd_bebob *bebob)
{
	spin_lock_irq(&bebob->lock);

	if (WARN_ON(bebob->dev_lock_count <= 0))
		goto end;
	if (--bebob->dev_lock_count == 0)
		snd_bebob_stream_lock_changed(bebob);
end:
	spin_unlock_irq(&bebob->lock);
}
