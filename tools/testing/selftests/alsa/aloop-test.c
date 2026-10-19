// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for the hw constraints between the two ends of an snd-aloop cable,
 * with and without the "PCM Notify" control, and for the "PCM Slave"
 * controls that report the playback side's parameters.
 *
 * Needs snd-aloop loaded with the default card id "Loopback". The tests use
 * the cable between hw:Loopback,0,0 (playback) and hw:Loopback,1,0 (capture).
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include "kselftest_harness.h"

#define FRAMES		1024
#define MAX_CHANNELS	4

struct stream_params {
	snd_pcm_access_t access;
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int rate;
};

/* The playback params differ from the capture params in every field. */
static const struct stream_params capture_params = {
	SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_FORMAT_S16_LE, 2, 44100
};

static const struct stream_params playback_params = {
	SND_PCM_ACCESS_RW_NONINTERLEAVED, SND_PCM_FORMAT_S32_LE, 4, 96000
};

struct probe_result {
	unsigned int rate_min, rate_max;
	unsigned int channels_min, channels_max;
	bool format_ok;
};

/* Value events seen on the cable's controls */
enum {
	EV_ACTIVE	= 1 << 0,
	EV_FORMAT	= 1 << 1,
	EV_RATE		= 1 << 2,
	EV_CHANNELS	= 1 << 3,
	EV_ACCESS	= 1 << 4,
};

FIXTURE(aloop) {
	char play_name[32];
	char capt_name[32];
	snd_ctl_t *ctl;
	bool saved_notify;
	bool notify_saved;
};

/* The cable's controls are on the capture side device. */
static void cable_ctl_id(snd_ctl_elem_value_t *value, const char *name)
{
	snd_ctl_elem_value_set_interface(value, SND_CTL_ELEM_IFACE_PCM);
	snd_ctl_elem_value_set_name(value, name);
	snd_ctl_elem_value_set_device(value, 1);
	snd_ctl_elem_value_set_subdevice(value, 0);
}

static long cable_ctl_get(snd_ctl_t *ctl, const char *name)
{
	snd_ctl_elem_value_t *value;

	snd_ctl_elem_value_alloca(&value);
	cable_ctl_id(value, name);
	if (snd_ctl_elem_read(ctl, value) < 0)
		return -1;
	if (!strcmp(name, "PCM Slave Access Mode"))
		return snd_ctl_elem_value_get_enumerated(value, 0);
	return snd_ctl_elem_value_get_integer(value, 0);
}

static int set_notify(snd_ctl_t *ctl, bool on)
{
	snd_ctl_elem_value_t *value;

	snd_ctl_elem_value_alloca(&value);
	cable_ctl_id(value, "PCM Notify");
	snd_ctl_elem_value_set_boolean(value, 0, on);
	return snd_ctl_elem_write(ctl, value);
}

/* Read all pending control events and return the EV_* bits for the cable's controls. */
static unsigned int read_events(snd_ctl_t *ctl)
{
	static const struct {
		const char *name;
		unsigned int bit;
	} names[] = {
		{ "PCM Slave Active", EV_ACTIVE },
		{ "PCM Slave Format", EV_FORMAT },
		{ "PCM Slave Rate", EV_RATE },
		{ "PCM Slave Channels", EV_CHANNELS },
		{ "PCM Slave Access Mode", EV_ACCESS },
	};
	snd_ctl_event_t *event;
	unsigned int seen = 0;
	int i;

	snd_ctl_event_alloca(&event);
	while (snd_ctl_read(ctl, event) > 0) {
		if (snd_ctl_event_get_type(event) != SND_CTL_EVENT_ELEM ||
		    !(snd_ctl_event_elem_get_mask(event) & SND_CTL_EVENT_MASK_VALUE) ||
		    snd_ctl_event_elem_get_device(event) != 1 ||
		    snd_ctl_event_elem_get_subdevice(event) != 0)
			continue;
		for (i = 0; i < ARRAY_SIZE(names); i++)
			if (!strcmp(snd_ctl_event_elem_get_name(event), names[i].name))
				seen |= names[i].bit;
	}
	return seen;
}

/* Open and configure a stream. snd_pcm_hw_params() also prepares it. */
static int open_pcm(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream,
		    const struct stream_params *p)
{
	unsigned int buffer_time = 100000;
	snd_pcm_hw_params_t *hw;
	int err;

	snd_pcm_hw_params_alloca(&hw);
	err = snd_pcm_open(pcm, name, stream, 0);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_any(*pcm, hw);
	if (err >= 0)
		err = snd_pcm_hw_params_set_access(*pcm, hw, p->access);
	if (err >= 0)
		err = snd_pcm_hw_params_set_format(*pcm, hw, p->format);
	if (err >= 0)
		err = snd_pcm_hw_params_set_channels(*pcm, hw, p->channels);
	if (err >= 0)
		err = snd_pcm_hw_params_set_rate(*pcm, hw, p->rate, 0);
	if (err >= 0)
		err = snd_pcm_hw_params_set_buffer_time_near(*pcm, hw, &buffer_time, NULL);
	if (err >= 0)
		err = snd_pcm_hw_params(*pcm, hw);
	if (err < 0) {
		snd_pcm_close(*pcm);
		*pcm = NULL;
	}
	return err;
}

/* What a client probing the device sees, and whether it may use the given format */
static int probe_pcm(const char *name, snd_pcm_stream_t stream, snd_pcm_format_t format,
		     struct probe_result *res)
{
	snd_pcm_hw_params_t *hw;
	snd_pcm_t *pcm;
	int err;

	snd_pcm_hw_params_alloca(&hw);
	err = snd_pcm_open(&pcm, name, stream, 0);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_any(pcm, hw);
	if (err >= 0)
		err = snd_pcm_hw_params_get_rate_min(hw, &res->rate_min, NULL);
	if (err >= 0)
		err = snd_pcm_hw_params_get_rate_max(hw, &res->rate_max, NULL);
	if (err >= 0)
		err = snd_pcm_hw_params_get_channels_min(hw, &res->channels_min);
	if (err >= 0)
		err = snd_pcm_hw_params_get_channels_max(hw, &res->channels_max);
	if (err >= 0)
		res->format_ok = !snd_pcm_hw_params_test_format(pcm, hw, format);
	snd_pcm_close(pcm);
	return err;
}

static int start_playback(snd_pcm_t *pcm, const struct stream_params *p)
{
	static char silence[FRAMES * MAX_CHANNELS * 4];
	void *bufs[MAX_CHANNELS];
	snd_pcm_sframes_t written;
	unsigned int i;

	if (p->access == SND_PCM_ACCESS_RW_NONINTERLEAVED) {
		for (i = 0; i < p->channels; i++)
			bufs[i] = silence + i * FRAMES * 4;
		written = snd_pcm_writen(pcm, bufs, FRAMES);
	} else {
		written = snd_pcm_writei(pcm, silence, FRAMES);
	}
	if (written < 0)
		return written;
	if (snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED)
		return snd_pcm_start(pcm);
	return 0;
}

FIXTURE_SETUP(aloop) {
	char ctl_name[32];
	snd_pcm_t *pcm;
	int card, err;

	card = snd_card_get_index("Loopback");
	if (card < 0)
		SKIP(return, "No Loopback card, snd-aloop is probably not loaded");

	sprintf(ctl_name, "hw:%d", card);
	sprintf(self->play_name, "hw:%d,0,0", card);
	sprintf(self->capt_name, "hw:%d,1,0", card);

	err = snd_pcm_open(&pcm, self->capt_name, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
	if (err == -EBUSY)
		SKIP(return, "%s is in use", self->capt_name);
	ASSERT_EQ(err, 0);
	snd_pcm_close(pcm);
	err = snd_pcm_open(&pcm, self->play_name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if (err == -EBUSY)
		SKIP(return, "%s is in use", self->play_name);
	ASSERT_EQ(err, 0);
	snd_pcm_close(pcm);

	ASSERT_EQ(snd_ctl_open(&self->ctl, ctl_name, SND_CTL_NONBLOCK), 0);
	ASSERT_EQ(snd_ctl_subscribe_events(self->ctl, 1), 0);
	self->saved_notify = cable_ctl_get(self->ctl, "PCM Notify");
	self->notify_saved = true;
}

FIXTURE_TEARDOWN(aloop) {
	if (self->notify_saved)
		set_notify(self->ctl, self->saved_notify);
	if (self->ctl)
		snd_ctl_close(self->ctl);
}

/* Without notify, a playback opened while a capture is set up is pinned to its parameters. */
TEST_F(aloop, playback_constrained_without_notify) {
	struct probe_result res;
	snd_pcm_t *capt;

	ASSERT_EQ(set_notify(self->ctl, false), 0);
	ASSERT_EQ(open_pcm(&capt, self->capt_name, SND_PCM_STREAM_CAPTURE, &capture_params), 0);

	ASSERT_EQ(probe_pcm(self->play_name, SND_PCM_STREAM_PLAYBACK, playback_params.format,
			    &res), 0);
	EXPECT_EQ(res.rate_min, capture_params.rate);
	EXPECT_EQ(res.rate_max, capture_params.rate);
	EXPECT_EQ(res.channels_min, capture_params.channels);
	EXPECT_EQ(res.channels_max, capture_params.channels);
	EXPECT_FALSE(res.format_ok);

	snd_pcm_close(capt);
}

/* With notify, the playback side is free to pick other parameters. */
TEST_F(aloop, playback_unconstrained_with_notify) {
	struct probe_result res;
	snd_pcm_t *capt;

	ASSERT_EQ(set_notify(self->ctl, true), 0);
	ASSERT_EQ(open_pcm(&capt, self->capt_name, SND_PCM_STREAM_CAPTURE, &capture_params), 0);

	ASSERT_EQ(probe_pcm(self->play_name, SND_PCM_STREAM_PLAYBACK, playback_params.format,
			    &res), 0);
	EXPECT_LE(res.rate_min, capture_params.rate);
	EXPECT_GE(res.rate_max, playback_params.rate);
	EXPECT_LE(res.channels_min, capture_params.channels);
	EXPECT_GE(res.channels_max, playback_params.channels);
	EXPECT_TRUE(res.format_ok);

	snd_pcm_close(capt);
}

/*
 * With notify, starting a playback with different parameters stops the
 * running capture, and the "PCM Slave" controls report the new parameters
 * with a value event for each one that changed.
 */
TEST_F(aloop, params_change_stops_capture_with_notify) {
	snd_pcm_t *capt, *play;
	unsigned int events;

	/*
	 * The controls keep the last playback's parameters, and only notify on
	 * a change. Start a playback with the capture's parameters while no
	 * capture is open, so that every control changes below.
	 */
	ASSERT_EQ(open_pcm(&play, self->play_name, SND_PCM_STREAM_PLAYBACK, &capture_params), 0);
	ASSERT_EQ(start_playback(play, &capture_params), 0);
	snd_pcm_close(play);

	ASSERT_EQ(set_notify(self->ctl, true), 0);
	ASSERT_EQ(open_pcm(&capt, self->capt_name, SND_PCM_STREAM_CAPTURE, &capture_params), 0);
	ASSERT_EQ(snd_pcm_start(capt), 0);
	ASSERT_EQ(snd_pcm_state(capt), SND_PCM_STATE_RUNNING);
	EXPECT_EQ(cable_ctl_get(self->ctl, "PCM Slave Active"), 0);
	read_events(self->ctl);

	ASSERT_EQ(open_pcm(&play, self->play_name, SND_PCM_STREAM_PLAYBACK, &playback_params), 0)
		TH_LOG("Playback refused other parameters while the capture is running");
	ASSERT_EQ(start_playback(play, &playback_params), 0);

	/* loopback_check_format() stops the capture from the playback's start trigger. */
	EXPECT_NE(snd_pcm_state(capt), SND_PCM_STATE_RUNNING);

	EXPECT_EQ(cable_ctl_get(self->ctl, "PCM Slave Active"), 1);
	EXPECT_EQ(cable_ctl_get(self->ctl, "PCM Slave Format"), playback_params.format);
	EXPECT_EQ(cable_ctl_get(self->ctl, "PCM Slave Rate"), playback_params.rate);
	EXPECT_EQ(cable_ctl_get(self->ctl, "PCM Slave Channels"), playback_params.channels);
	EXPECT_EQ(cable_ctl_get(self->ctl, "PCM Slave Access Mode"), 1);

	events = read_events(self->ctl);
	EXPECT_TRUE(events & EV_ACTIVE);
	EXPECT_TRUE(events & EV_FORMAT);
	EXPECT_TRUE(events & EV_RATE);
	EXPECT_TRUE(events & EV_CHANNELS);
	EXPECT_TRUE(events & EV_ACCESS);

	snd_pcm_close(play);
	snd_pcm_close(capt);
}

/* With notify, a capture opened second is still pinned to the playback's parameters. */
TEST_F(aloop, capture_constrained_with_notify) {
	struct probe_result res;
	snd_pcm_t *play;

	ASSERT_EQ(set_notify(self->ctl, true), 0);
	ASSERT_EQ(open_pcm(&play, self->play_name, SND_PCM_STREAM_PLAYBACK, &playback_params), 0);

	ASSERT_EQ(probe_pcm(self->capt_name, SND_PCM_STREAM_CAPTURE, capture_params.format,
			    &res), 0);
	EXPECT_EQ(res.rate_min, playback_params.rate);
	EXPECT_EQ(res.rate_max, playback_params.rate);
	EXPECT_EQ(res.channels_min, playback_params.channels);
	EXPECT_EQ(res.channels_max, playback_params.channels);
	EXPECT_FALSE(res.format_ok);

	snd_pcm_close(play);
}

TEST_HARNESS_MAIN
