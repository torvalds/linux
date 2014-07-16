/*
 * fireworks_stream.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */
#include "./fireworks.h"

#define CALLBACK_TIMEOUT	100

static int
init_stream(struct snd_efw *efw, struct amdtp_stream *stream)
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
		goto end;

	err = amdtp_stream_init(stream, efw->unit, s_dir, CIP_BLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(stream);
		cmp_connection_destroy(conn);
	}
end:
	return err;
}

static void
stop_stream(struct snd_efw *efw, struct amdtp_stream *stream)
{
	amdtp_stream_pcm_abort(stream);
	amdtp_stream_stop(stream);

	if (stream == &efw->tx_stream)
		cmp_connection_break(&efw->out_conn);
	else
		cmp_connection_break(&efw->in_conn);
}

static int
start_stream(struct snd_efw *efw, struct amdtp_stream *stream,
	     unsigned int sampling_rate)
{
	struct cmp_connection *conn;
	unsigned int mode, pcm_channels, midi_ports;
	int err;

	err = snd_efw_get_multiplier_mode(sampling_rate, &mode);
	if (err < 0)
		goto end;
	if (stream == &efw->tx_stream) {
		conn = &efw->out_conn;
		pcm_channels = efw->pcm_capture_channels[mode];
		midi_ports = efw->midi_out_ports;
	} else {
		conn = &efw->in_conn;
		pcm_channels = efw->pcm_playback_channels[mode];
		midi_ports = efw->midi_in_ports;
	}

	amdtp_stream_set_parameters(stream, sampling_rate,
				    pcm_channels, midi_ports);

	/*  establish connection via CMP */
	err = cmp_connection_establish(conn,
				amdtp_stream_get_max_payload(stream));
	if (err < 0)
		goto end;

	/* start amdtp stream */
	err = amdtp_stream_start(stream,
				 conn->resources.channel,
				 conn->speed);
	if (err < 0) {
		stop_stream(efw, stream);
		goto end;
	}

	/* wait first callback */
	if (!amdtp_stream_wait_callback(stream, CALLBACK_TIMEOUT)) {
		stop_stream(efw, stream);
		err = -ETIMEDOUT;
	}
end:
	return err;
}

static void
destroy_stream(struct snd_efw *efw, struct amdtp_stream *stream)
{
	stop_stream(efw, stream);

	amdtp_stream_destroy(stream);

	if (stream == &efw->tx_stream)
		cmp_connection_destroy(&efw->out_conn);
	else
		cmp_connection_destroy(&efw->in_conn);
}

static int
get_sync_mode(struct snd_efw *efw, enum cip_flags *sync_mode)
{
	enum snd_efw_clock_source clock_source;
	int err;

	err = snd_efw_command_get_clock_source(efw, &clock_source);
	if (err < 0)
		return err;

	if (clock_source == SND_EFW_CLOCK_SOURCE_SYTMATCH)
		return -ENOSYS;

	*sync_mode = CIP_SYNC_TO_DEVICE;
	return 0;
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
		goto end;
	/* Fireworks transmits NODATA packets with TAG0. */
	efw->tx_stream.flags |= CIP_EMPTY_WITH_TAG0;
	/* Fireworks has its own meaning for dbc. */
	efw->tx_stream.flags |= CIP_DBC_IS_END_EVENT;
	/* Fireworks reset dbc at bus reset. */
	efw->tx_stream.flags |= CIP_SKIP_DBC_ZERO_CHECK;
	/* AudioFire9 always reports wrong dbs. */
	if (efw->is_af9)
		efw->tx_stream.flags |= CIP_WRONG_DBS;
	/* Firmware version 5.5 reports fixed interval for dbc. */
	if (efw->firmware_version == 0x5050000)
		efw->tx_stream.tx_dbc_interval = 8;

	err = init_stream(efw, &efw->rx_stream);
	if (err < 0) {
		destroy_stream(efw, &efw->tx_stream);
		goto end;
	}
	/*
	 * Fireworks ignores MIDI messages in more than first 8 data
	 * blocks of an received AMDTP packet.
	 */
	efw->rx_stream.rx_blocks_for_midi = 8;

	/* set IEC61883 compliant mode (actually not fully compliant...) */
	err = snd_efw_command_set_tx_mode(efw, SND_EFW_TRANSPORT_MODE_IEC61883);
	if (err < 0) {
		destroy_stream(efw, &efw->tx_stream);
		destroy_stream(efw, &efw->rx_stream);
	}
end:
	return err;
}

int snd_efw_stream_start_duplex(struct snd_efw *efw, unsigned int rate)
{
	struct amdtp_stream *master, *slave;
	atomic_t *slave_substreams;
	enum cip_flags sync_mode;
	unsigned int curr_rate;
	int err = 0;

	mutex_lock(&efw->mutex);

	/* Need no substreams */
	if ((atomic_read(&efw->playback_substreams) == 0) &&
	    (atomic_read(&efw->capture_substreams)  == 0))
		goto end;

	err = get_sync_mode(efw, &sync_mode);
	if (err < 0)
		goto end;
	if (sync_mode == CIP_SYNC_TO_DEVICE) {
		master = &efw->tx_stream;
		slave  = &efw->rx_stream;
		slave_substreams  = &efw->playback_substreams;
	} else {
		master = &efw->rx_stream;
		slave  = &efw->tx_stream;
		slave_substreams = &efw->capture_substreams;
	}

	/*
	 * Considering JACK/FFADO streaming:
	 * TODO: This can be removed hwdep functionality becomes popular.
	 */
	err = check_connection_used_by_others(efw, master);
	if (err < 0)
		goto end;

	/* packet queueing error */
	if (amdtp_streaming_error(slave))
		stop_stream(efw, slave);
	if (amdtp_streaming_error(master))
		stop_stream(efw, master);

	/* stop streams if rate is different */
	err = snd_efw_command_get_sampling_rate(efw, &curr_rate);
	if (err < 0)
		goto end;
	if (rate == 0)
		rate = curr_rate;
	if (rate != curr_rate) {
		stop_stream(efw, slave);
		stop_stream(efw, master);
	}

	/* master should be always running */
	if (!amdtp_stream_running(master)) {
		amdtp_stream_set_sync(sync_mode, master, slave);
		efw->master = master;

		err = snd_efw_command_set_sampling_rate(efw, rate);
		if (err < 0)
			goto end;

		err = start_stream(efw, master, rate);
		if (err < 0) {
			dev_err(&efw->unit->device,
				"fail to start AMDTP master stream:%d\n", err);
			goto end;
		}
	}

	/* start slave if needed */
	if (atomic_read(slave_substreams) > 0 && !amdtp_stream_running(slave)) {
		err = start_stream(efw, slave, rate);
		if (err < 0) {
			dev_err(&efw->unit->device,
				"fail to start AMDTP slave stream:%d\n", err);
			stop_stream(efw, master);
		}
	}
end:
	mutex_unlock(&efw->mutex);
	return err;
}

void snd_efw_stream_stop_duplex(struct snd_efw *efw)
{
	struct amdtp_stream *master, *slave;
	atomic_t *master_substreams, *slave_substreams;

	if (efw->master == &efw->rx_stream) {
		slave  = &efw->tx_stream;
		master = &efw->rx_stream;
		slave_substreams  = &efw->capture_substreams;
		master_substreams = &efw->playback_substreams;
	} else {
		slave  = &efw->rx_stream;
		master = &efw->tx_stream;
		slave_substreams  = &efw->playback_substreams;
		master_substreams = &efw->capture_substreams;
	}

	mutex_lock(&efw->mutex);

	if (atomic_read(slave_substreams) == 0) {
		stop_stream(efw, slave);

		if (atomic_read(master_substreams) == 0)
			stop_stream(efw, master);
	}

	mutex_unlock(&efw->mutex);
}

void snd_efw_stream_update_duplex(struct snd_efw *efw)
{
	if ((cmp_connection_update(&efw->out_conn) < 0) ||
	    (cmp_connection_update(&efw->in_conn) < 0)) {
		mutex_lock(&efw->mutex);
		stop_stream(efw, &efw->rx_stream);
		stop_stream(efw, &efw->tx_stream);
		mutex_unlock(&efw->mutex);
	} else {
		amdtp_stream_update(&efw->rx_stream);
		amdtp_stream_update(&efw->tx_stream);
	}
}

void snd_efw_stream_destroy_duplex(struct snd_efw *efw)
{
	mutex_lock(&efw->mutex);

	destroy_stream(efw, &efw->rx_stream);
	destroy_stream(efw, &efw->tx_stream);

	mutex_unlock(&efw->mutex);
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
