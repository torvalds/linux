// SPDX-License-Identifier: GPL-2.0
/*
 * This is the test which covers PCM middle layer data transferring using
 * the virtual pcm test driver (snd-pcmtest).
 *
 * Copyright 2023 Ivan Orlov <ivan.orlov0322@gmail.com>
 */
#include <string.h>
#include <alsa/asoundlib.h>
#include "../kselftest_harness.h"

#define CH_NUM 4

struct pattern_buf {
	char buf[1024];
	int len;
};

struct pattern_buf patterns[CH_NUM];

struct pcmtest_test_params {
	unsigned long buffer_size;
	unsigned long period_size;
	unsigned long channels;
	unsigned int rate;
	snd_pcm_access_t access;
	size_t sec_buf_len;
	size_t sample_size;
	int time;
	snd_pcm_format_t format;
};

static int read_patterns(void)
{
	FILE *fp, *fpl;
	int i;
	char pf[64];
	char plf[64];

	for (i = 0; i < CH_NUM; i++) {
		sprintf(plf, "/sys/kernel/debug/pcmtest/fill_pattern%d_len", i);
		fpl = fopen(plf, "r");
		if (!fpl)
			return -1;
		fscanf(fpl, "%u", &patterns[i].len);
		fclose(fpl);

		sprintf(pf, "/sys/kernel/debug/pcmtest/fill_pattern%d", i);
		fp = fopen(pf, "r");
		if (!fp)
			return -1;
		fread(patterns[i].buf, 1, patterns[i].len, fp);
		fclose(fp);
	}

	return 0;
}

static int get_test_results(char *debug_name)
{
	int result;
	FILE *f;
	char fname[128];

	sprintf(fname, "/sys/kernel/debug/pcmtest/%s", debug_name);

	f = fopen(fname, "r");
	if (!f) {
		printf("Failed to open file\n");
		return -1;
	}
	fscanf(f, "%d", &result);
	fclose(f);

	return result;
}

static size_t get_sec_buf_len(unsigned int rate, unsigned long channels, snd_pcm_format_t format)
{
	return rate * channels * snd_pcm_format_physical_width(format) / 8;
}

static int setup_handle(snd_pcm_t **handle, snd_pcm_sw_params_t *swparams,
			snd_pcm_hw_params_t *hwparams, struct pcmtest_test_params *params,
			int card, snd_pcm_stream_t stream)
{
	char pcm_name[32];
	int err;

	sprintf(pcm_name, "hw:%d,0,0", card);
	err = snd_pcm_open(handle, pcm_name, stream, 0);
	if (err < 0)
		return err;
	snd_pcm_hw_params_any(*handle, hwparams);
	snd_pcm_hw_params_set_rate_resample(*handle, hwparams, 0);
	snd_pcm_hw_params_set_access(*handle, hwparams, params->access);
	snd_pcm_hw_params_set_format(*handle, hwparams, params->format);
	snd_pcm_hw_params_set_channels(*handle, hwparams, params->channels);
	snd_pcm_hw_params_set_rate_near(*handle, hwparams, &params->rate, 0);
	snd_pcm_hw_params_set_period_size_near(*handle, hwparams, &params->period_size, 0);
	snd_pcm_hw_params_set_buffer_size_near(*handle, hwparams, &params->buffer_size);
	snd_pcm_hw_params(*handle, hwparams);
	snd_pcm_sw_params_current(*handle, swparams);

	snd_pcm_hw_params_set_rate_resample(*handle, hwparams, 0);
	snd_pcm_sw_params_set_avail_min(*handle, swparams, params->period_size);
	snd_pcm_hw_params_set_buffer_size_near(*handle, hwparams, &params->buffer_size);
	snd_pcm_hw_params_set_period_size_near(*handle, hwparams, &params->period_size, 0);
	snd_pcm_sw_params(*handle, swparams);
	snd_pcm_hw_params(*handle, hwparams);

	return 0;
}

FIXTURE(pcmtest) {
	int card;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_hw_params_t *hwparams;
	struct pcmtest_test_params params;
};

FIXTURE_TEARDOWN(pcmtest) {
}

FIXTURE_SETUP(pcmtest) {
	char *card_name;
	int err;

	if (geteuid())
		SKIP(exit(-1), "This test needs root to run!");

	err = read_patterns();
	if (err)
		SKIP(exit(-1), "Can't read patterns. Probably, module isn't loaded");

	card_name = malloc(127);
	ASSERT_NE(card_name, NULL);
	self->params.buffer_size = 16384;
	self->params.period_size = 4096;
	self->params.channels = CH_NUM;
	self->params.rate = 8000;
	self->params.access = SND_PCM_ACCESS_RW_INTERLEAVED;
	self->params.format = SND_PCM_FORMAT_S16_LE;
	self->card = -1;
	self->params.sample_size = snd_pcm_format_physical_width(self->params.format) / 8;

	self->params.sec_buf_len = get_sec_buf_len(self->params.rate, self->params.channels,
						   self->params.format);
	self->params.time = 4;

	while (snd_card_next(&self->card) >= 0) {
		if (self->card == -1)
			break;
		snd_card_get_name(self->card, &card_name);
		if (!strcmp(card_name, "PCM-Test"))
			break;
	}
	free(card_name);
	ASSERT_NE(self->card, -1);
}

/*
 * Here we are trying to send the looped monotonically increasing sequence of bytes to the driver.
 * If our data isn't corrupted, the driver will set the content of 'pc_test' debugfs file to '1'
 */
TEST_F(pcmtest, playback) {
	snd_pcm_t *handle;
	unsigned char *it;
	size_t write_res;
	int test_results;
	int i, cur_ch, pos_in_ch;
	void *samples;
	struct pcmtest_test_params *params = &self->params;

	samples = calloc(self->params.sec_buf_len * self->params.time, 1);
	ASSERT_NE(samples, NULL);

	snd_pcm_sw_params_alloca(&self->swparams);
	snd_pcm_hw_params_alloca(&self->hwparams);

	ASSERT_EQ(setup_handle(&handle, self->swparams, self->hwparams, params,
			       self->card, SND_PCM_STREAM_PLAYBACK), 0);
	snd_pcm_format_set_silence(params->format, samples,
				   params->rate * params->channels * params->time);
	it = samples;
	for (i = 0; i < self->params.sec_buf_len * params->time; i++) {
		cur_ch = (i / params->sample_size) % CH_NUM;
		pos_in_ch = i / params->sample_size / CH_NUM * params->sample_size
			    + (i % params->sample_size);
		it[i] = patterns[cur_ch].buf[pos_in_ch % patterns[cur_ch].len];
	}
	write_res = snd_pcm_writei(handle, samples, params->rate * params->time);
	ASSERT_GE(write_res, 0);

	snd_pcm_close(handle);
	free(samples);
	test_results = get_test_results("pc_test");
	ASSERT_EQ(test_results, 1);
}

/*
 * Here we test that the virtual alsa driver returns looped and monotonically increasing sequence
 * of bytes. In the interleaved mode the buffer will contain samples in the following order:
 * C0, C1, C2, C3, C0, C1, ...
 */
TEST_F(pcmtest, capture) {
	snd_pcm_t *handle;
	unsigned char *it;
	size_t read_res;
	int i, cur_ch, pos_in_ch;
	void *samples;
	struct pcmtest_test_params *params = &self->params;

	samples = calloc(self->params.sec_buf_len * self->params.time, 1);
	ASSERT_NE(samples, NULL);

	snd_pcm_sw_params_alloca(&self->swparams);
	snd_pcm_hw_params_alloca(&self->hwparams);

	ASSERT_EQ(setup_handle(&handle, self->swparams, self->hwparams,
			       params, self->card, SND_PCM_STREAM_CAPTURE), 0);
	snd_pcm_format_set_silence(params->format, samples,
				   params->rate * params->channels * params->time);
	read_res = snd_pcm_readi(handle, samples, params->rate * params->time);
	ASSERT_GE(read_res, 0);
	snd_pcm_close(handle);
	it = (unsigned char *)samples;
	for (i = 0; i < self->params.sec_buf_len * self->params.time; i++) {
		cur_ch = (i / params->sample_size) % CH_NUM;
		pos_in_ch = i / params->sample_size / CH_NUM * params->sample_size
			    + (i % params->sample_size);
		ASSERT_EQ(it[i], patterns[cur_ch].buf[pos_in_ch % patterns[cur_ch].len]);
	}
	free(samples);
}

// Test capture in the non-interleaved access mode. The are buffers for each recorded channel
TEST_F(pcmtest, ni_capture) {
	snd_pcm_t *handle;
	struct pcmtest_test_params params = self->params;
	char **chan_samples;
	size_t i, j, read_res;

	chan_samples = calloc(CH_NUM, sizeof(*chan_samples));
	ASSERT_NE(chan_samples, NULL);

	snd_pcm_sw_params_alloca(&self->swparams);
	snd_pcm_hw_params_alloca(&self->hwparams);

	params.access = SND_PCM_ACCESS_RW_NONINTERLEAVED;

	ASSERT_EQ(setup_handle(&handle, self->swparams, self->hwparams,
			       &params, self->card, SND_PCM_STREAM_CAPTURE), 0);

	for (i = 0; i < CH_NUM; i++)
		chan_samples[i] = calloc(params.sec_buf_len * params.time, 1);

	for (i = 0; i < 1; i++) {
		read_res = snd_pcm_readn(handle, (void **)chan_samples, params.rate * params.time);
		ASSERT_GE(read_res, 0);
	}
	snd_pcm_close(handle);

	for (i = 0; i < CH_NUM; i++) {
		for (j = 0; j < params.rate * params.time; j++)
			ASSERT_EQ(chan_samples[i][j], patterns[i].buf[j % patterns[i].len]);
		free(chan_samples[i]);
	}
	free(chan_samples);
}

TEST_F(pcmtest, ni_playback) {
	snd_pcm_t *handle;
	struct pcmtest_test_params params = self->params;
	char **chan_samples;
	size_t i, j, read_res;
	int test_res;

	chan_samples = calloc(CH_NUM, sizeof(*chan_samples));
	ASSERT_NE(chan_samples, NULL);

	snd_pcm_sw_params_alloca(&self->swparams);
	snd_pcm_hw_params_alloca(&self->hwparams);

	params.access = SND_PCM_ACCESS_RW_NONINTERLEAVED;

	ASSERT_EQ(setup_handle(&handle, self->swparams, self->hwparams,
			       &params, self->card, SND_PCM_STREAM_PLAYBACK), 0);

	for (i = 0; i < CH_NUM; i++) {
		chan_samples[i] = calloc(params.sec_buf_len * params.time, 1);
		for (j = 0; j < params.sec_buf_len * params.time; j++)
			chan_samples[i][j] = patterns[i].buf[j % patterns[i].len];
	}

	for (i = 0; i < 1; i++) {
		read_res = snd_pcm_writen(handle, (void **)chan_samples, params.rate * params.time);
		ASSERT_GE(read_res, 0);
	}

	snd_pcm_close(handle);
	test_res = get_test_results("pc_test");
	ASSERT_EQ(test_res, 1);

	for (i = 0; i < CH_NUM; i++)
		free(chan_samples[i]);
	free(chan_samples);
}

/*
 * Here we are testing the custom ioctl definition inside the virtual driver. If it triggers
 * successfully, the driver sets the content of 'ioctl_test' debugfs file to '1'.
 */
TEST_F(pcmtest, reset_ioctl) {
	snd_pcm_t *handle;
	unsigned char *it;
	int test_res;
	struct pcmtest_test_params *params = &self->params;

	snd_pcm_sw_params_alloca(&self->swparams);
	snd_pcm_hw_params_alloca(&self->hwparams);

	ASSERT_EQ(setup_handle(&handle, self->swparams, self->hwparams, params,
			       self->card, SND_PCM_STREAM_CAPTURE), 0);
	snd_pcm_reset(handle);
	test_res = get_test_results("ioctl_test");
	ASSERT_EQ(test_res, 1);
	snd_pcm_close(handle);
}

TEST_HARNESS_MAIN
