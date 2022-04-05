// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <subcmd/pager.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "color.h"
#include <math.h>
#include <unistd.h>

int perf_use_color_default = -1;

static int __color_vsnprintf(char *bf, size_t size, const char *color,
			     const char *fmt, va_list args, const char *trail)
{
	int r = 0;

	/*
	 * Auto-detect:
	 */
	if (perf_use_color_default < 0) {
		if (isatty(1) || pager_in_use())
			perf_use_color_default = 1;
		else
			perf_use_color_default = 0;
	}

	if (perf_use_color_default && *color)
		r += scnprintf(bf, size, "%s", color);
	r += vscnprintf(bf + r, size - r, fmt, args);
	if (perf_use_color_default && *color)
		r += scnprintf(bf + r, size - r, "%s", PERF_COLOR_RESET);
	if (trail)
		r += scnprintf(bf + r, size - r, "%s", trail);
	return r;
}

/* Colors are not included in return value */
static int __color_vfprintf(FILE *fp, const char *color, const char *fmt,
		va_list args)
{
	int r = 0;

	/*
	 * Auto-detect:
	 */
	if (perf_use_color_default < 0) {
		if (isatty(fileno(fp)) || pager_in_use())
			perf_use_color_default = 1;
		else
			perf_use_color_default = 0;
	}

	if (perf_use_color_default && *color)
		fprintf(fp, "%s", color);
	r += vfprintf(fp, fmt, args);
	if (perf_use_color_default && *color)
		fprintf(fp, "%s", PERF_COLOR_RESET);
	return r;
}

int color_vsnprintf(char *bf, size_t size, const char *color,
		    const char *fmt, va_list args)
{
	return __color_vsnprintf(bf, size, color, fmt, args, NULL);
}

int color_vfprintf(FILE *fp, const char *color, const char *fmt, va_list args)
{
	return __color_vfprintf(fp, color, fmt, args);
}

int color_snprintf(char *bf, size_t size, const char *color,
		   const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = color_vsnprintf(bf, size, color, fmt, args);
	va_end(args);
	return r;
}

int color_fprintf(FILE *fp, const char *color, const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = color_vfprintf(fp, color, fmt, args);
	va_end(args);
	return r;
}

/*
 * This function splits the buffer by newlines and colors the lines individually.
 *
 * Returns 0 on success.
 */
int color_fwrite_lines(FILE *fp, const char *color,
		size_t count, const char *buf)
{
	if (!*color)
		return fwrite(buf, count, 1, fp) != 1;

	while (count) {
		char *p = memchr(buf, '\n', count);

		if (p != buf && (fputs(color, fp) < 0 ||
				fwrite(buf, p ? (size_t)(p - buf) : count, 1, fp) != 1 ||
				fputs(PERF_COLOR_RESET, fp) < 0))
			return -1;
		if (!p)
			return 0;
		if (fputc('\n', fp) < 0)
			return -1;
		count -= p + 1 - buf;
		buf = p + 1;
	}
	return 0;
}

const char *get_percent_color(double percent)
{
	const char *color = PERF_COLOR_NORMAL;

	/*
	 * We color high-overhead entries in red, mid-overhead
	 * entries in green - and keep the low overhead places
	 * normal:
	 */
	if (fabs(percent) >= MIN_RED)
		color = PERF_COLOR_RED;
	else {
		if (fabs(percent) > MIN_GREEN)
			color = PERF_COLOR_GREEN;
	}
	return color;
}

int percent_color_fprintf(FILE *fp, const char *fmt, double percent)
{
	int r;
	const char *color;

	color = get_percent_color(percent);
	r = color_fprintf(fp, color, fmt, percent);

	return r;
}

int value_color_snprintf(char *bf, size_t size, const char *fmt, double value)
{
	const char *color = get_percent_color(value);
	return color_snprintf(bf, size, color, fmt, value);
}

int percent_color_snprintf(char *bf, size_t size, const char *fmt, ...)
{
	va_list args;
	double percent;

	va_start(args, fmt);
	percent = va_arg(args, double);
	va_end(args);
	return value_color_snprintf(bf, size, fmt, percent);
}

int percent_color_len_snprintf(char *bf, size_t size, const char *fmt, ...)
{
	va_list args;
	int len;
	double percent;
	const char *color;

	va_start(args, fmt);
	len = va_arg(args, int);
	percent = va_arg(args, double);
	va_end(args);

	color = get_percent_color(percent);
	return color_snprintf(bf, size, color, fmt, len, percent);
}
