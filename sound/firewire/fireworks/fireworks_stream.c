// SPDX-License-Identifier: GPL-2.0-only
/*
 * fireworks_stream.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 */
#include "./fireworks.h"

#define READY_TIMEOUT_MS	100

static int init_stream(struct snd_efw *efw, struct amdtp_stream *stream)
{
	struct cmp_connection *conn;
	enum cmp_direction c_dir;
	enum amdtp_stream_direction s_dir;
	int err;

	if (stream == &efw->tx_stream) {
		conn = &efw->out_conn;
		c_dir = CMP_OUTPUT;
		s_dir = AMDTP_IN_STREAM;
	} else {
		conn = &efw->in_conn;
		c_dir = CMP_INPUT;
		s_dir = AMDTP_OUT_STREAM;
	}

	err = cmp_connection_init(conn, efw->unit, c_dir, 0);
	if (err < 0)
		return err;

	err = amdtp_am824_init(stream, efw->unit, s_dir, CIP_BLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(stream);
		cmp_connection_destroy(conn);
		return err;
	}

	if (stream == &efw->tx_stream) {
		// Fireworks transmits NODATA packets with TAG0.
		efw->tx_stream.flags |= CIP_EMPTY_WITH_TAG0;
		// Fireworks has its own meaning for dbc.
		efw->tx_stream.flags |= CIP_DBC_IS_END_EVENT;
		// Fireworks reset dbc at bus reset.
		efw->tx_stream.flags |= CIP_SKIP_DBC_ZERO_CHECK;
		// But Recent firmwares starts packets with non-zero dbc.
		// Driver version 5.7.6 installs firmware version 5.7.3.
		if (efw->is_fireworks3 &&
		    (efw->firmware_version == 0x5070000 ||
		     efw->firmware_version == 0x5070300 ||
		     efw->firmware_version == 0x5080000))
			efw->tx_stream.flags |= CIP_UNALIGHED_DBC;
		// AudioFire9 always reports wrong dbs.
		if (efw->is_af9)
			efw->tx_stream.flags |= CIP_WRONG_DBS;
		// Firmware version 5.5 reports fixed interval for dbc.
		if (efw->firmware_version == 0x5050000)
			efw->tx_stream.ctx_data.tx.dbc_interval = 8;
	}

	return err;
}

static int start_stream(struct snd_efw *efw, struct amdtp_stream *stream,
			unsigned int rate)
{
	struct cmp_connection *conn;
	int err;

	if (stream == &efw->tx_stream)
		conn = &efw->out_conn;
	else
		conn = &efw->in_conn;

	// Establish connection via CMP.
	err = cmp_connection_establish(conn);
	if (err < 0)
		return err;

	// Start amdtp stream.
	err = amdtp_domain_add_stream(&efw->domain, stream,
				      conn->resources.channel, conn->speed);
	if (err < 0) {
		cmp_connection_break(conn);
		return err;
	}

	return 0;
}

// This function should be called before starting the stream or after stopping
// the streams.
static void destroy_stream(struct snd_efw *efw, struct amdtp_stream *stream)
{
	amdtp_stream_destroy(stream);

	if (stream == &efw->tx_stream)
		cmp_connection_destroy(&efw->out_conn);
	else
		cmp_connection_destroy(&efw->in_conn);
}

static int
check_connection_used_by_others(struct snd_efw *efw, struct amdtp_stream *s)
{
	struct cmp_connection *conn;
	bool used;
	int err;

	if (s == &efw->tx_stream)
		conn = &efw->out_conn;
	else
		conn = &efw->in_conn;

	err = cmp_connection_check_used(conn, &used);
	if ((err >= 0) && used && !amdtp_stream_running(s)) {
		dev_err(&efw->unit->device,
			"Connection established by others: %cPCR[%d]\n",
			(conn->direction == CMP_OUTPUT) ? 'o' : 'i',
			conn->pcr_index);
		err = -EBUSY;
	}

	return err;
}

int snd_efw_stream_init_duplex(struct snd_efw *efw)
{
	int err;

	err = init_stream(efw, &efw->tx_stream);
	if (err < 0)
		return err;

	err = init_stream(efw, &efw->rx_stream);
	if (err < 0) {
		destroy_stream(efw, &efw->tx_stream);
		return err;
	}

	err = amdtp_domain_init(&efw->domain);
	if (err < 0) {
		destroy_stream(efw, &efw->tx_stream);
		destroy_stream(efw, &efw->rx_stream);
		return err;
	}

	// set IEC61883 compliant mode (actually not fully compliant...).
	err = snd_efw_command_set_tx_mode(efw, SND_EFW_TRANSPORT_MODE_IEC61883);
	if (err < 0) {
		destroy_stream(efw, &efw->tx_stream);
		destroy_stream(efw, &efw->rx_stream);
	}

	return err;
}

static int keep_resources(struct snd_efw *efw, struct amdtp_stream *stream,
			  unsigned int rate, unsigned int mode)
{
	unsigned int pcm_channels;
	unsigned int midi_ports;
	struct cmp_connection *conn;
	int err;

	if (stream == &efw->tx_stream) {
		pcm_channels = efw->pcm_capture_channels[mode];
		midi_ports = efw->midi_out_ports;
		conn = &efw->out_conn;
	} else {
		pcm_channels = efw->pcm_playback_channels[mode];
		midi_ports = efw->midi_in_ports;
		conn = &efw->in_conn;
	}

	err = amdtp_am824_set_parameters(stream, rate, pcm_channels,
					 midi_ports, false);
	if (err < 0)
		return err;

	return cmp_connection_reserve(conn, amdtp_stream_get_max_payload(stream));
}

int snd_efw_stream_reserve_duplex(struct snd_efw *efw, unsigned int rate,
				  unsigned int frames_per_period,
				  unsigned int frames_per_buffer)
{
	unsigned int curr_rate;
	int err;

	// Considering JACK/FFADO streaming:
	// TODO: This can be removed hwdep functionality becomes popular.
	err = check_connection_used_by_others(efw, &efw->rx_stream);
	if (err < 0)
		return err;

	// stop streams if rate is different.
	err = snd_efw_command_get_sampling_rate(efw, &curr_rate);
	if (err < 0)
		return err;
	if (rate == 0)
		rate = curr_rate;
	if (rate != curr_rate) {
		amdtp_domain_stop(&efw->domain);

		cmp_connection_break(&efw->out_conn);
		cmp_connection_break(&efw->in_conn);

		cmp_connection_release(&efw->out_conn);
		cmp_connection_release(&efw->in_conn);
	}

	if (efw->substreams_counter == 0 || rate != curr_rate) {
		unsigned int mode;

		err = snd_efw_command_set_sampling_rate(efw, rate);
		if (err < 0)
			return err;

		err = snd_efw_get_multiplier_mode(rate, &mode);
		if (err < 0)
			return err;

		err = keep_resources(efw, &efw->tx_stream, rate, mode);
		if (err < 0)
			return err;

		err = keep_resources(efw, &efw->rx_stream, rate, mode);
		if (err < 0) {
			cmp_connection_release(&efw->in_conn);
			return err;
		}

		err = amdtp_domain_set_events_per_period(&efw->domain,
					frames_per_period, frames_per_buffer);
		if (err < 0) {
			cmp_connection_release(&efw->in_conn);
			cmp_connection_release(&efw->out_conn);
			return err;
		}
	}

	return 0;
}

int snd_efw_stream_start_duplex(struct snd_efw *efw)
{
	unsigned int rate;
	int err = 0;

	// Need no substreams.
	if (efw->substreams_counter == 0)
		return -EIO;

	if (amdtp_streaming_error(&efw->rx_stream) ||
	    amdtp_streaming_error(&efw->tx_stream)) {
		amdtp_domain_stop(&efw->domain);
		cmp_connection_break(&efw->out_conn);
		cmp_connection_break(&efw->in_conn);
	}

	err = snd_efw_command_get_sampling_rate(efw, &rate);
	if (err < 0)
		return err;

	if (!amdtp_stream_running(&efw->rx_stream)) {
		err = start_stream(efw, &efw->rx_stream, rate);
		if (err < 0)
			goto error;

		err = start_stream(efw, &efw->tx_stream, rate);
		if (err < 0)
			goto error;

		err = amdtp_domain_start(&efw->domain, 0);
		if (err < 0)
			goto error;

		if (!amdtp_domain_wait_ready(&efw->domain, READY_TIMEOUT_MS)) {
			err = -ETIMEDOUT;
			goto error;
		}
	}

	return 0;
error:
	amdtp_domain_stop(&efw->domain);

	cmp_connection_break(&efw->out_conn);
	cmp_connection_break(&efw->in_conn);

	return err;
}

void snd_efw_stream_stop_duplex(struct snd_efw *efw)
{
	if (efw->substreams_counter == 0) {
		amdtp_domain_stop(&efw->domain);

		cmp_connection_break(&efw->out_conn);
		cmp_connection_break(&efw->in_conn);

		cmp_connection_release(&efw->out_conn);
		cmp_connection_release(&efw->in_conn);
	}
}

void snd_efw_stream_update_duplex(struct snd_efw *efw)
{
	amdtp_domain_stop(&efw->domain);

	cmp_connection_break(&efw->out_conn);
	cmp_connection_break(&efw->in_conn);

	amdtp_stream_pcm_abort(&efw->rx_stream);
	amdtp_stream_pcm_abort(&efw->tx_stream);
}

void snd_efw_stream_destroy_duplex(struct snd_efw *efw)
{
	amdtp_domain_destroy(&efw->domain);

	destroy_stream(efw, &efw->rx_stream);
	destroy_stream(efw, &efw->tx_stream);
}

void snd_efw_stream_lock_changed(struct snd_efw *efw)
{
	efw->dev_lock_changed = true;
	wake_up(&efw->hwdep_wait);
}

int snd_efw_stream_lock_try(struct snd_efw *efw)
{
	int err;

	spin_lock_irq(&efw->lock);

	/* user land lock this */
	if (efw->dev_lock_count < 0) {
		err = -EBUSY;
		goto end;
	}

	/* this is the first time */
	if (efw->dev_lock_count++ == 0)
		snd_efw_stream_lock_changed(efw);
	err = 0;
end:
	spin_unlock_irq(&efw->lock);
	return err;
}

void snd_efw_stream_lock_release(struct snd_efw *efw)
{
	spin_lock_irq(&efw->lock);

	if (WARN_ON(efw->dev_lock_count <= 0))
		goto end;
	if (--efw->dev_lock_count == 0)
		snd_efw_stream_lock_changed(efw);
end:
	spin_unlock_irq(&efw->lock);
}
