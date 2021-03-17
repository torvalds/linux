// SPDX-License-Identifier: GPL-2.0-only
/*
 * This test checks the response of the system clock to frequency
 * steps made with adjtimex(). The frequency error and stability of
 * the CLOCK_MONOTONIC clock relative to the CLOCK_MONOTONIC_RAW clock
 * is measured in two intervals following the step. The test fails if
 * values from the second interval exceed specified limits.
 *
 * Copyright (C) Miroslav Lichvar <mlichvar@redhat.com>  2017
 */

#include <math.h>
#include <stdio.h>
#include <sys/timex.h>
#include <time.h>
#include <unistd.h>

#include "../kselftest.h"

#define SAMPLES 100
#define SAMPLE_READINGS 10
#define MEAN_SAMPLE_INTERVAL 0.1
#define STEP_INTERVAL 1.0
#define MAX_PRECISION 500e-9
#define MAX_FREQ_ERROR 0.02e-6
#define MAX_STDDEV 50e-9

#ifndef ADJ_SETOFFSET
  #define ADJ_SETOFFSET 0x0100
#endif

struct sample {
	double offset;
	double time;
};

static time_t mono_raw_base;
static time_t mono_base;
static long user_hz;
static double precision;
static double mono_freq_offset;

static double diff_timespec(struct timespec *ts1, struct timespec *ts2)
{
	return ts1->tv_sec - ts2->tv_sec + (ts1->tv_nsec - ts2->tv_nsec) / 1e9;
}

static double get_sample(struct sample *sample)
{
	double delay, mindelay = 0.0;
	struct timespec ts1, ts2, ts3;
	int i;

	for (i = 0; i < SAMPLE_READINGS; i++) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts1);
		clock_gettime(CLOCK_MONOTONIC, &ts2);
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts3);

		ts1.tv_sec -= mono_raw_base;
		ts2.tv_sec -= mono_base;
		ts3.tv_sec -= mono_raw_base;

		delay = diff_timespec(&ts3, &ts1);
		if (delay <= 1e-9) {
			i--;
			continue;
		}

		if (!i || delay < mindelay) {
			sample->offset = diff_timespec(&ts2, &ts1);
			sample->offset -= delay / 2.0;
			sample->time = ts1.tv_sec + ts1.tv_nsec / 1e9;
			mindelay = delay;
		}
	}

	return mindelay;
}

static void reset_ntp_error(void)
{
	struct timex txc;

	txc.modes = ADJ_SETOFFSET;
	txc.time.tv_sec = 0;
	txc.time.tv_usec = 0;

	if (adjtimex(&txc) < 0) {
		perror("[FAIL] adjtimex");
		ksft_exit_fail();
	}
}

static void set_frequency(double freq)
{
	struct timex txc;
	int tick_offset;

	tick_offset = 1e6 * freq / user_hz;

	txc.modes = ADJ_TICK | ADJ_FREQUENCY;
	txc.tick = 1000000 / user_hz + tick_offset;
	txc.freq = (1e6 * freq - user_hz * tick_offset) * (1 << 16);

	if (adjtimex(&txc) < 0) {
		perror("[FAIL] adjtimex");
		ksft_exit_fail();
	}
}

static void regress(struct sample *samples, int n, double *intercept,
		    double *slope, double *r_stddev, double *r_max)
{
	double x, y, r, x_sum, y_sum, xy_sum, x2_sum, r2_sum;
	int i;

	x_sum = 0.0, y_sum = 0.0, xy_sum = 0.0, x2_sum = 0.0;

	for (i = 0; i < n; i++) {
		x = samples[i].time;
		y = samples[i].offset;

		x_sum += x;
		y_sum += y;
		xy_sum += x * y;
		x2_sum += x * x;
	}

	*slope = (xy_sum - x_sum * y_sum / n) / (x2_sum - x_sum * x_sum / n);
	*intercept = (y_sum - *slope * x_sum) / n;

	*r_max = 0.0, r2_sum = 0.0;

	for (i = 0; i < n; i++) {
		x = samples[i].time;
		y = samples[i].offset;
		r = fabs(x * *slope + *intercept - y);
		if (*r_max < r)
			*r_max = r;
		r2_sum += r * r;
	}

	*r_stddev = sqrt(r2_sum / n);
}

static int run_test(int calibration, double freq_base, double freq_step)
{
	struct sample samples[SAMPLES];
	double intercept, slope, stddev1, max1, stddev2, max2;
	double freq_error1, freq_error2;
	int i;

	set_frequency(freq_base);

	for (i = 0; i < 10; i++)
		usleep(1e6 * MEAN_SAMPLE_INTERVAL / 10);

	reset_ntp_error();

	set_frequency(freq_base + freq_step);

	for (i = 0; i < 10; i++)
		usleep(rand() % 2000000 * STEP_INTERVAL / 10);

	set_frequency(freq_base);

	for (i = 0; i < SAMPLES; i++) {
		usleep(rand() % 2000000 * MEAN_SAMPLE_INTERVAL);
		get_sample(&samples[i]);
	}

	if (calibration) {
		regress(samples, SAMPLES, &intercept, &slope, &stddev1, &max1);
		mono_freq_offset = slope;
		printf("CLOCK_MONOTONIC_RAW frequency offset: %11.3f ppm\n",
		       1e6 * mono_freq_offset);
		return 0;
	}

	regress(samples, SAMPLES / 2, &intercept, &slope, &stddev1, &max1);
	freq_error1 = slope * (1.0 - mono_freq_offset) - mono_freq_offset -
			freq_base;

	regress(samples + SAMPLES / 2, SAMPLES / 2, &intercept, &slope,
		&stddev2, &max2);
	freq_error2 = slope * (1.0 - mono_freq_offset) - mono_freq_offset -
			freq_base;

	printf("%6.0f %+10.3f %6.0f %7.0f %+10.3f %6.0f %7.0f\t",
	       1e6 * freq_step,
	       1e6 * freq_error1, 1e9 * stddev1, 1e9 * max1,
	       1e6 * freq_error2, 1e9 * stddev2, 1e9 * max2);

	if (fabs(freq_error2) > MAX_FREQ_ERROR || stddev2 > MAX_STDDEV) {
		printf("[FAIL]\n");
		return 1;
	}

	printf("[OK]\n");
	return 0;
}

static void init_test(void)
{
	struct timespec ts;
	struct sample sample;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) {
		perror("[FAIL] clock_gettime(CLOCK_MONOTONIC_RAW)");
		ksft_exit_fail();
	}

	mono_raw_base = ts.tv_sec;

	if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
		perror("[FAIL] clock_gettime(CLOCK_MONOTONIC)");
		ksft_exit_fail();
	}

	mono_base = ts.tv_sec;

	user_hz = sysconf(_SC_CLK_TCK);

	precision = get_sample(&sample) / 2.0;
	printf("CLOCK_MONOTONIC_RAW+CLOCK_MONOTONIC precision: %.0f ns\t\t",
	       1e9 * precision);

	if (precision > MAX_PRECISION)
		ksft_exit_skip("precision: %.0f ns > MAX_PRECISION: %.0f ns\n",
				1e9 * precision, 1e9 * MAX_PRECISION);

	printf("[OK]\n");
	srand(ts.tv_sec ^ ts.tv_nsec);

	run_test(1, 0.0, 0.0);
}

int main(int argc, char **argv)
{
	double freq_base, freq_step;
	int i, j, fails = 0;

	init_test();

	printf("Checking response to frequency step:\n");
	printf("  Step           1st interval              2nd interval\n");
	printf("             Freq    Dev     Max       Freq    Dev     Max\n");

	for (i = 2; i >= 0; i--) {
		for (j = 0; j < 5; j++) {
			freq_base = (rand() % (1 << 24) - (1 << 23)) / 65536e6;
			freq_step = 10e-6 * (1 << (6 * i));
			fails += run_test(0, freq_base, freq_step);
		}
	}

	set_frequency(0.0);

	if (fails)
		return ksft_exit_fail();

	return ksft_exit_pass();
}
