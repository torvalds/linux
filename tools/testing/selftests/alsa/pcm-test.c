// SPDX-License-Identifier: GPL-2.0
//
// kselftest for the ALSA PCM API
//
// Original author: Jaroslav Kysela <perex@perex.cz>
// Copyright (c) 2022 Red Hat Inc.

// This test will iterate over all cards detected in the system, exercising
// every PCM device it can find.  This may conflict with other system
// software if there is audio activity so is best run on a system with a
// minimal active userspace.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#include "../kselftest.h"
#include "alsa-local.h"

typedef struct timespec timestamp_t;

struct pcm_data {
	snd_pcm_t *handle;
	int card;
	int device;
	int subdevice;
	snd_pcm_stream_t stream;
	snd_config_t *pcm_config;
	struct pcm_data *next;
};

int num_pcms = 0;
struct pcm_data *pcm_list = NULL;

int num_missing = 0;
struct pcm_data *pcm_missing = NULL;

struct time_test_def {
	const char *cfg_prefix;
	const char *format;
	long rate;
	long channels;
	long period_size;
	long buffer_size;
};

void timestamp_now(timestamp_t *tstamp)
{
	if (clock_gettime(CLOCK_MONOTONIC_RAW, tstamp))
		ksft_exit_fail_msg("clock_get_time\n");
}

long long timestamp_diff_ms(timestamp_t *tstamp)
{
	timestamp_t now, diff;
	timestamp_now(&now);
	if (tstamp->tv_nsec > now.tv_nsec) {
		diff.tv_sec = now.tv_sec - tstamp->tv_sec - 1;
		diff.tv_nsec = (now.tv_nsec + 1000000000L) - tstamp->tv_nsec;
	} else {
		diff.tv_sec = now.tv_sec - tstamp->tv_sec;
		diff.tv_nsec = now.tv_nsec - tstamp->tv_nsec;
	}
	return (diff.tv_sec * 1000) + ((diff.tv_nsec + 500000L) / 1000000L);
}

static long device_from_id(snd_config_t *node)
{
	const char *id;
	char *end;
	long v;

	if (snd_config_get_id(node, &id))
		ksft_exit_fail_msg("snd_config_get_id\n");
	errno = 0;
	v = strtol(id, &end, 10);
	if (errno || *end)
		return -1;
	return v;
}

static void missing_device(int card, int device, int subdevice, snd_pcm_stream_t stream)
{
	struct pcm_data *pcm_data;

	for (pcm_data = pcm_list; pcm_data != NULL; pcm_data = pcm_data->next) {
		if (pcm_data->card != card)
			continue;
		if (pcm_data->device != device)
			continue;
		if (pcm_data->subdevice != subdevice)
			continue;
		if (pcm_data->stream != stream)
			continue;
		return;
	}
	pcm_data = calloc(1, sizeof(*pcm_data));
	if (!pcm_data)
		ksft_exit_fail_msg("Out of memory\n");
	pcm_data->card = card;
	pcm_data->device = device;
	pcm_data->subdevice = subdevice;
	pcm_data->stream = stream;
	pcm_data->next = pcm_missing;
	pcm_missing = pcm_data;
	num_missing++;
}

static void missing_devices(int card, snd_config_t *card_config)
{
	snd_config_t *pcm_config, *node1, *node2;
	snd_config_iterator_t i1, i2, next1, next2;
	int device, subdevice;

	pcm_config = conf_get_subtree(card_config, "pcm", NULL);
	if (!pcm_config)
		return;
	snd_config_for_each(i1, next1, pcm_config) {
		node1 = snd_config_iterator_entry(i1);
		device = device_from_id(node1);
		if (device < 0)
			continue;
		if (snd_config_get_type(node1) != SND_CONFIG_TYPE_COMPOUND)
			continue;
		snd_config_for_each(i2, next2, node1) {
			node2 = snd_config_iterator_entry(i2);
			subdevice = device_from_id(node2);
			if (subdevice < 0)
				continue;
			if (conf_get_subtree(node2, "PLAYBACK", NULL))
				missing_device(card, device, subdevice, SND_PCM_STREAM_PLAYBACK);
			if (conf_get_subtree(node2, "CAPTURE", NULL))
				missing_device(card, device, subdevice, SND_PCM_STREAM_CAPTURE);
		}
	}
}

static void find_pcms(void)
{
	char name[32], key[64];
	int card, dev, subdev, count, direction, err;
	snd_pcm_stream_t stream;
	struct pcm_data *pcm_data;
	snd_ctl_t *handle;
	snd_pcm_info_t *pcm_info;
	snd_config_t *config, *card_config, *pcm_config;

	snd_pcm_info_alloca(&pcm_info);

	card = -1;
	if (snd_card_next(&card) < 0 || card < 0)
		return;

	config = get_alsalib_config();

	while (card >= 0) {
		sprintf(name, "hw:%d", card);

		err = snd_ctl_open_lconf(&handle, name, 0, config);
		if (err < 0) {
			ksft_print_msg("Failed to get hctl for card %d: %s\n",
				       card, snd_strerror(err));
			goto next_card;
		}

		card_config = conf_by_card(card);

		dev = -1;
		while (1) {
			if (snd_ctl_pcm_next_device(handle, &dev) < 0)
				ksft_exit_fail_msg("snd_ctl_pcm_next_device\n");
			if (dev < 0)
				break;

			for (direction = 0; direction < 2; direction++) {
				stream = direction ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
				sprintf(key, "pcm.%d.%s", dev, snd_pcm_stream_name(stream));
				pcm_config = conf_get_subtree(card_config, key, NULL);
				if (conf_get_bool(card_config, key, "skip", false)) {
					ksft_print_msg("skipping pcm %d.%d.%s\n", card, dev, snd_pcm_stream_name(stream));
					continue;
				}
				snd_pcm_info_set_device(pcm_info, dev);
				snd_pcm_info_set_subdevice(pcm_info, 0);
				snd_pcm_info_set_stream(pcm_info, stream);
				err = snd_ctl_pcm_info(handle, pcm_info);
				if (err == -ENOENT)
					continue;
				if (err < 0)
					ksft_exit_fail_msg("snd_ctl_pcm_info: %d:%d:%d\n",
							   dev, 0, stream);
				count = snd_pcm_info_get_subdevices_count(pcm_info);
				for (subdev = 0; subdev < count; subdev++) {
					sprintf(key, "pcm.%d.%d.%s", dev, subdev, snd_pcm_stream_name(stream));
					if (conf_get_bool(card_config, key, "skip", false)) {
						ksft_print_msg("skipping pcm %d.%d.%d.%s\n", card, dev,
							       subdev, snd_pcm_stream_name(stream));
						continue;
					}
					pcm_data = calloc(1, sizeof(*pcm_data));
					if (!pcm_data)
						ksft_exit_fail_msg("Out of memory\n");
					pcm_data->card = card;
					pcm_data->device = dev;
					pcm_data->subdevice = subdev;
					pcm_data->stream = stream;
					pcm_data->pcm_config = conf_get_subtree(card_config, key, NULL);
					pcm_data->next = pcm_list;
					pcm_list = pcm_data;
					num_pcms++;
				}
			}
		}

		/* check for missing devices */
		missing_devices(card, card_config);

	next_card:
		snd_ctl_close(handle);
		if (snd_card_next(&card) < 0) {
			ksft_print_msg("snd_card_next");
			break;
		}
	}

	snd_config_delete(config);
}

static void test_pcm_time1(struct pcm_data *data,
			   const struct time_test_def *test)
{
	char name[64], key[128], msg[256];
	const char *cs;
	int i, err;
	snd_pcm_t *handle = NULL;
	snd_pcm_access_t access = SND_PCM_ACCESS_RW_INTERLEAVED;
	snd_pcm_format_t format;
	unsigned char *samples = NULL;
	snd_pcm_sframes_t frames;
	long long ms;
	long rate, channels, period_size, buffer_size;
	unsigned int rchannels;
	unsigned int rrate;
	snd_pcm_uframes_t rperiod_size, rbuffer_size, start_threshold;
	timestamp_t tstamp;
	bool pass = false, automatic = true;
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	bool skip = false;

	snd_pcm_hw_params_alloca(&hw_params);
	snd_pcm_sw_params_alloca(&sw_params);

	cs = conf_get_string(data->pcm_config, test->cfg_prefix, "format", test->format);
	format = snd_pcm_format_value(cs);
	if (format == SND_PCM_FORMAT_UNKNOWN)
		ksft_exit_fail_msg("Wrong format '%s'\n", cs);
	rate = conf_get_long(data->pcm_config, test->cfg_prefix, "rate", test->rate);
	channels = conf_get_long(data->pcm_config, test->cfg_prefix, "channels", test->channels);
	period_size = conf_get_long(data->pcm_config, test->cfg_prefix, "period_size", test->period_size);
	buffer_size = conf_get_long(data->pcm_config, test->cfg_prefix, "buffer_size", test->buffer_size);

	automatic = strcmp(test->format, snd_pcm_format_name(format)) == 0 &&
			test->rate == rate &&
			test->channels == channels &&
			test->period_size == period_size &&
			test->buffer_size == buffer_size;

	samples = malloc((rate * channels * snd_pcm_format_physical_width(format)) / 8);
	if (!samples)
		ksft_exit_fail_msg("Out of memory\n");
	snd_pcm_format_set_silence(format, samples, rate * channels);

	sprintf(name, "hw:%d,%d,%d", data->card, data->device, data->subdevice);
	err = snd_pcm_open(&handle, name, data->stream, 0);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "Failed to get pcm handle: %s", snd_strerror(err));
		goto __close;
	}

	err = snd_pcm_hw_params_any(handle, hw_params);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_any: %s", snd_strerror(err));
		goto __close;
	}
	err = snd_pcm_hw_params_set_rate_resample(handle, hw_params, 0);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_rate_resample: %s", snd_strerror(err));
		goto __close;
	}
	err = snd_pcm_hw_params_set_access(handle, hw_params, access);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_access %s: %s",
					   snd_pcm_access_name(access), snd_strerror(err));
		goto __close;
	}
__format:
	err = snd_pcm_hw_params_set_format(handle, hw_params, format);
	if (err < 0) {
		if (automatic && format == SND_PCM_FORMAT_S16_LE) {
			format = SND_PCM_FORMAT_S32_LE;
			ksft_print_msg("%s.%d.%d.%d.%s.%s format S16_LE -> S32_LE\n",
					 test->cfg_prefix,
					 data->card, data->device, data->subdevice,
					 snd_pcm_stream_name(data->stream),
					 snd_pcm_access_name(access));
		}
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_format %s: %s",
					   snd_pcm_format_name(format), snd_strerror(err));
		goto __close;
	}
	rchannels = channels;
	err = snd_pcm_hw_params_set_channels_near(handle, hw_params, &rchannels);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_channels %ld: %s", channels, snd_strerror(err));
		goto __close;
	}
	if (rchannels != channels) {
		snprintf(msg, sizeof(msg), "channels unsupported %ld != %ld", channels, rchannels);
		skip = true;
		goto __close;
	}
	rrate = rate;
	err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &rrate, 0);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_rate %ld: %s", rate, snd_strerror(err));
		goto __close;
	}
	if (rrate != rate) {
		snprintf(msg, sizeof(msg), "rate unsupported %ld != %ld", rate, rrate);
		skip = true;
		goto __close;
	}
	rperiod_size = period_size;
	err = snd_pcm_hw_params_set_period_size_near(handle, hw_params, &rperiod_size, 0);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_period_size %ld: %s", period_size, snd_strerror(err));
		goto __close;
	}
	rbuffer_size = buffer_size;
	err = snd_pcm_hw_params_set_buffer_size_near(handle, hw_params, &rbuffer_size);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_buffer_size %ld: %s", buffer_size, snd_strerror(err));
		goto __close;
	}
	err = snd_pcm_hw_params(handle, hw_params);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params: %s", snd_strerror(err));
		goto __close;
	}

	err = snd_pcm_sw_params_current(handle, sw_params);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_sw_params_current: %s", snd_strerror(err));
		goto __close;
	}
	if (data->stream == SND_PCM_STREAM_PLAYBACK) {
		start_threshold = (rbuffer_size / rperiod_size) * rperiod_size;
	} else {
		start_threshold = rperiod_size;
	}
	err = snd_pcm_sw_params_set_start_threshold(handle, sw_params, start_threshold);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_sw_params_set_start_threshold %ld: %s", (long)start_threshold, snd_strerror(err));
		goto __close;
	}
	err = snd_pcm_sw_params_set_avail_min(handle, sw_params, rperiod_size);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_sw_params_set_avail_min %ld: %s", (long)rperiod_size, snd_strerror(err));
		goto __close;
	}
	err = snd_pcm_sw_params(handle, sw_params);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_sw_params: %s", snd_strerror(err));
		goto __close;
	}

	ksft_print_msg("%s.%d.%d.%d.%s hw_params.%s.%s.%ld.%ld.%ld.%ld sw_params.%ld\n",
			 test->cfg_prefix,
			 data->card, data->device, data->subdevice,
			 snd_pcm_stream_name(data->stream),
			 snd_pcm_access_name(access),
			 snd_pcm_format_name(format),
			 (long)rate, (long)channels,
			 (long)rperiod_size, (long)rbuffer_size,
			 (long)start_threshold);

	timestamp_now(&tstamp);
	for (i = 0; i < 4; i++) {
		if (data->stream == SND_PCM_STREAM_PLAYBACK) {
			frames = snd_pcm_writei(handle, samples, rate);
			if (frames < 0) {
				snprintf(msg, sizeof(msg),
					 "Write failed: expected %d, wrote %li", rate, frames);
				goto __close;
			}
			if (frames < rate) {
				snprintf(msg, sizeof(msg),
					 "expected %d, wrote %li", rate, frames);
				goto __close;
			}
		} else {
			frames = snd_pcm_readi(handle, samples, rate);
			if (frames < 0) {
				snprintf(msg, sizeof(msg),
					 "expected %d, wrote %li", rate, frames);
				goto __close;
			}
			if (frames < rate) {
				snprintf(msg, sizeof(msg),
					 "expected %d, wrote %li", rate, frames);
				goto __close;
			}
		}
	}

	snd_pcm_drain(handle);
	ms = timestamp_diff_ms(&tstamp);
	if (ms < 3900 || ms > 4100) {
		snprintf(msg, sizeof(msg), "time mismatch: expected 4000ms got %lld", ms);
		goto __close;
	}

	msg[0] = '\0';
	pass = true;
__close:
	if (!skip) {
		ksft_test_result(pass, "%s.%d.%d.%d.%s%s%s\n",
				 test->cfg_prefix,
				 data->card, data->device, data->subdevice,
				 snd_pcm_stream_name(data->stream),
				 msg[0] ? " " : "", msg);
	} else {
		ksft_test_result_skip("%s.%d.%d.%d.%s%s%s\n",
				      test->cfg_prefix,
				      data->card, data->device,
				      data->subdevice,
				      snd_pcm_stream_name(data->stream),
				      msg[0] ? " " : "", msg);
	}
	free(samples);
	if (handle)
		snd_pcm_close(handle);
}

static const struct time_test_def time_tests[] = {
	/* name          format     rate   chan  period  buffer */
	{ "8k.1.big",    "S16_LE",   8000, 2,     8000,   32000 },
	{ "8k.2.big",    "S16_LE",   8000, 2,     8000,   32000 },
	{ "44k1.2.big",  "S16_LE",  44100, 2,    22050,  192000 },
	{ "48k.2.small", "S16_LE",  48000, 2,      512,    4096 },
	{ "48k.2.big",   "S16_LE",  48000, 2,    24000,  192000 },
	{ "48k.6.big",   "S16_LE",  48000, 6,    48000,  576000 },
	{ "96k.2.big",   "S16_LE",  96000, 2,    48000,  192000 },
};

int main(void)
{
	struct pcm_data *pcm;
	int i;

	ksft_print_header();

	conf_load();

	find_pcms();

	ksft_set_plan(num_missing + num_pcms * ARRAY_SIZE(time_tests));

	for (pcm = pcm_missing; pcm != NULL; pcm = pcm->next) {
		ksft_test_result(false, "test.missing.%d.%d.%d.%s\n",
				 pcm->card, pcm->device, pcm->subdevice,
				 snd_pcm_stream_name(pcm->stream));
	}

	for (pcm = pcm_list; pcm != NULL; pcm = pcm->next) {
		for (i = 0; i < ARRAY_SIZE(time_tests); i++) {
			test_pcm_time1(pcm, &time_tests[i]);
		}
	}

	conf_free();

	ksft_exit_pass();

	return 0;
}
