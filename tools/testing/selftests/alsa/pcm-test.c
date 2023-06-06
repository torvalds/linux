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
#include <pthread.h>

#include "../kselftest.h"
#include "alsa-local.h"

typedef struct timespec timestamp_t;

struct card_data {
	int card;
	pthread_t thread;
	struct card_data *next;
};

struct card_data *card_list = NULL;

struct pcm_data {
	snd_pcm_t *handle;
	int card;
	int device;
	int subdevice;
	snd_pcm_stream_t stream;
	snd_config_t *pcm_config;
	struct pcm_data *next;
};

struct pcm_data *pcm_list = NULL;

int num_missing = 0;
struct pcm_data *pcm_missing = NULL;

snd_config_t *default_pcm_config;

/* Lock while reporting results since kselftest doesn't */
pthread_mutex_t results_lock = PTHREAD_MUTEX_INITIALIZER;

enum test_class {
	TEST_CLASS_DEFAULT,
	TEST_CLASS_SYSTEM,
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
	char *card_name, *card_longname;
	int card, dev, subdev, count, direction, err;
	snd_pcm_stream_t stream;
	struct pcm_data *pcm_data;
	snd_ctl_t *handle;
	snd_pcm_info_t *pcm_info;
	snd_config_t *config, *card_config, *pcm_config;
	struct card_data *card_data;

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

		err = snd_card_get_name(card, &card_name);
		if (err != 0)
			card_name = "Unknown";
		err = snd_card_get_longname(card, &card_longname);
		if (err != 0)
			card_longname = "Unknown";
		ksft_print_msg("Card %d - %s (%s)\n", card,
			       card_name, card_longname);

		card_config = conf_by_card(card);

		card_data = calloc(1, sizeof(*card_data));
		if (!card_data)
			ksft_exit_fail_msg("Out of memory\n");
		card_data->card = card;
		card_data->next = card_list;
		card_list = card_data;

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

static void test_pcm_time(struct pcm_data *data, enum test_class class,
			  const char *test_name, snd_config_t *pcm_cfg)
{
	char name[64], key[128], msg[256];
	const char *cs;
	int i, err;
	snd_pcm_t *handle = NULL;
	snd_pcm_access_t access = SND_PCM_ACCESS_RW_INTERLEAVED;
	snd_pcm_format_t format, old_format;
	const char *alt_formats[8];
	unsigned char *samples = NULL;
	snd_pcm_sframes_t frames;
	long long ms;
	long rate, channels, period_size, buffer_size;
	unsigned int rrate;
	snd_pcm_uframes_t rperiod_size, rbuffer_size, start_threshold;
	timestamp_t tstamp;
	bool pass = false;
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	const char *test_class_name;
	bool skip = true;
	const char *desc;

	switch (class) {
	case TEST_CLASS_DEFAULT:
		test_class_name = "default";
		break;
	case TEST_CLASS_SYSTEM:
		test_class_name = "system";
		break;
	default:
		ksft_exit_fail_msg("Unknown test class %d\n", class);
		break;
	}

	desc = conf_get_string(pcm_cfg, "description", NULL, NULL);
	if (desc)
		ksft_print_msg("%s.%s.%d.%d.%d.%s - %s\n",
			       test_class_name, test_name,
			       data->card, data->device, data->subdevice,
			       snd_pcm_stream_name(data->stream),
			       desc);


	snd_pcm_hw_params_alloca(&hw_params);
	snd_pcm_sw_params_alloca(&sw_params);

	cs = conf_get_string(pcm_cfg, "format", NULL, "S16_LE");
	format = snd_pcm_format_value(cs);
	if (format == SND_PCM_FORMAT_UNKNOWN)
		ksft_exit_fail_msg("Wrong format '%s'\n", cs);
	conf_get_string_array(pcm_cfg, "alt_formats", NULL,
				alt_formats, ARRAY_SIZE(alt_formats), NULL);
	rate = conf_get_long(pcm_cfg, "rate", NULL, 48000);
	channels = conf_get_long(pcm_cfg, "channels", NULL, 2);
	period_size = conf_get_long(pcm_cfg, "period_size", NULL, 4096);
	buffer_size = conf_get_long(pcm_cfg, "buffer_size", NULL, 16384);

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
	i = -1;
__format:
	err = snd_pcm_hw_params_set_format(handle, hw_params, format);
	if (err < 0) {
		i++;
		if (i < ARRAY_SIZE(alt_formats) && alt_formats[i]) {
			old_format = format;
			format = snd_pcm_format_value(alt_formats[i]);
			if (format != SND_PCM_FORMAT_UNKNOWN) {
				ksft_print_msg("%s.%d.%d.%d.%s.%s format %s -> %s\n",
						 test_name,
						 data->card, data->device, data->subdevice,
						 snd_pcm_stream_name(data->stream),
						 snd_pcm_access_name(access),
						 snd_pcm_format_name(old_format),
						 snd_pcm_format_name(format));
				samples = realloc(samples, (rate * channels *
							    snd_pcm_format_physical_width(format)) / 8);
				if (!samples)
					ksft_exit_fail_msg("Out of memory\n");
				snd_pcm_format_set_silence(format, samples, rate * channels);
				goto __format;
			}
		}
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_format %s: %s",
					   snd_pcm_format_name(format), snd_strerror(err));
		goto __close;
	}
	err = snd_pcm_hw_params_set_channels(handle, hw_params, channels);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_channels %ld: %s", channels, snd_strerror(err));
		goto __close;
	}
	rrate = rate;
	err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &rrate, 0);
	if (err < 0) {
		snprintf(msg, sizeof(msg), "snd_pcm_hw_params_set_rate %ld: %s", rate, snd_strerror(err));
		goto __close;
	}
	if (rrate != rate) {
		snprintf(msg, sizeof(msg), "rate mismatch %ld != %ld", rate, rrate);
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

	ksft_print_msg("%s.%s.%d.%d.%d.%s hw_params.%s.%s.%ld.%ld.%ld.%ld sw_params.%ld\n",
		         test_class_name, test_name,
			 data->card, data->device, data->subdevice,
			 snd_pcm_stream_name(data->stream),
			 snd_pcm_access_name(access),
			 snd_pcm_format_name(format),
			 (long)rate, (long)channels,
			 (long)rperiod_size, (long)rbuffer_size,
			 (long)start_threshold);

	/* Set all the params, actually run the test */
	skip = false;

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
	pthread_mutex_lock(&results_lock);

	switch (class) {
	case TEST_CLASS_SYSTEM:
		test_class_name = "system";
		/*
		 * Anything specified as specific to this system
		 * should always be supported.
		 */
		ksft_test_result(!skip, "%s.%s.%d.%d.%d.%s.params\n",
				 test_class_name, test_name,
				 data->card, data->device, data->subdevice,
				 snd_pcm_stream_name(data->stream));
		break;
	default:
		break;
	}

	if (!skip)
		ksft_test_result(pass, "%s.%s.%d.%d.%d.%s\n",
				 test_class_name, test_name,
				 data->card, data->device, data->subdevice,
				 snd_pcm_stream_name(data->stream));
	else
		ksft_test_result_skip("%s.%s.%d.%d.%d.%s\n",
				 test_class_name, test_name,
				 data->card, data->device, data->subdevice,
				 snd_pcm_stream_name(data->stream));

	if (msg[0])
		ksft_print_msg("%s\n", msg);

	pthread_mutex_unlock(&results_lock);

	free(samples);
	if (handle)
		snd_pcm_close(handle);
}

void run_time_tests(struct pcm_data *pcm, enum test_class class,
		    snd_config_t *cfg)
{
	const char *test_name, *test_type;
	snd_config_t *pcm_cfg;
	snd_config_iterator_t i, next;

	if (!cfg)
		return;

	cfg = conf_get_subtree(cfg, "test", NULL);
	if (cfg == NULL)
		return;

	snd_config_for_each(i, next, cfg) {
		pcm_cfg = snd_config_iterator_entry(i);
		if (snd_config_get_id(pcm_cfg, &test_name) < 0)
			ksft_exit_fail_msg("snd_config_get_id\n");
		test_type = conf_get_string(pcm_cfg, "type", NULL, "time");
		if (strcmp(test_type, "time") == 0)
			test_pcm_time(pcm, class, test_name, pcm_cfg);
		else
			ksft_exit_fail_msg("unknown test type '%s'\n", test_type);
	}
}

void *card_thread(void *data)
{
	struct card_data *card = data;
	struct pcm_data *pcm;

	for (pcm = pcm_list; pcm != NULL; pcm = pcm->next) {
		if (pcm->card != card->card)
			continue;

		run_time_tests(pcm, TEST_CLASS_DEFAULT, default_pcm_config);
		run_time_tests(pcm, TEST_CLASS_SYSTEM, pcm->pcm_config);
	}

	return 0;
}

int main(void)
{
	struct card_data *card;
	struct pcm_data *pcm;
	snd_config_t *global_config, *cfg, *pcm_cfg;
	int num_pcm_tests = 0, num_tests, num_std_pcm_tests;
	int ret;
	void *thread_ret;

	ksft_print_header();

	global_config = conf_load_from_file("pcm-test.conf");
	default_pcm_config = conf_get_subtree(global_config, "pcm", NULL);
	if (default_pcm_config == NULL)
		ksft_exit_fail_msg("default pcm test configuration (pcm compound) is missing\n");

	conf_load();

	find_pcms();

	num_std_pcm_tests = conf_get_count(default_pcm_config, "test", NULL);

	for (pcm = pcm_list; pcm != NULL; pcm = pcm->next) {
		num_pcm_tests += num_std_pcm_tests;
		cfg = pcm->pcm_config;
		if (cfg == NULL)
			continue;
		/* Setting params is reported as a separate test */
		num_tests = conf_get_count(cfg, "test", NULL) * 2;
		if (num_tests > 0)
			num_pcm_tests += num_tests;
	}

	ksft_set_plan(num_missing + num_pcm_tests);

	for (pcm = pcm_missing; pcm != NULL; pcm = pcm->next) {
		ksft_test_result(false, "test.missing.%d.%d.%d.%s\n",
				 pcm->card, pcm->device, pcm->subdevice,
				 snd_pcm_stream_name(pcm->stream));
	}

	for (card = card_list; card != NULL; card = card->next) {
		ret = pthread_create(&card->thread, NULL, card_thread, card);
		if (ret != 0) {
			ksft_exit_fail_msg("Failed to create card %d thread: %d (%s)\n",
					   card->card, ret,
					   strerror(errno));
		}
	}

	for (card = card_list; card != NULL; card = card->next) {
		ret = pthread_join(card->thread, &thread_ret);
		if (ret != 0) {
			ksft_exit_fail_msg("Failed to join card %d thread: %d (%s)\n",
					   card->card, ret,
					   strerror(errno));
		}
	}

	snd_config_delete(global_config);
	conf_free();

	ksft_exit_pass();

	return 0;
}
