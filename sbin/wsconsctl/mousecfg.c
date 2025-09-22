/* $OpenBSD: mousecfg.c,v 1.12 2024/10/05 13:27:16 chrisz Exp $ */

/*
 * Copyright (c) 2017 Ulf Brosziewski
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Read/write wsmouse parameters for touchpad configuration.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dev/wscons/wsconsio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include "mousecfg.h"

#ifndef nitems
#define nitems(_a)       (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define BASESIZE ((WSMOUSECFG__FILTERS - WSMOUSECFG_DX_SCALE) \
    + (WSMOUSECFG__DEBUG - WSMOUSECFG_LOG_INPUT))

static const int range[][2] = {
	{ WSMOUSECFG_DX_SCALE, WSMOUSECFG__FILTERS - 1 },
	{ WSMOUSECFG_LOG_INPUT, WSMOUSECFG__DEBUG - 1 },
	{ WSMOUSECFG_DX_MAX, WSMOUSECFG__TPFILTERS - 1 },
	{ WSMOUSECFG_SOFTBUTTONS, WSMOUSECFG__TPFEATURES - 1 },
	{ WSMOUSECFG_LEFT_EDGE, WSMOUSECFG__TPSETUP - 1 },
};

static const int touchpad_types[] = {
	WSMOUSE_TYPE_SYNAPTICS,		/* Synaptics touchpad */
	WSMOUSE_TYPE_ALPS,		/* ALPS touchpad */
	WSMOUSE_TYPE_ELANTECH,		/* Elantech touchpad */
	WSMOUSE_TYPE_SYNAP_SBTN,	/* Synaptics soft buttons */
	WSMOUSE_TYPE_TOUCHPAD,		/* Generic touchpad */
};

struct wsmouse_parameters cfg_tapping = {
	(struct wsmouse_param[]) {
	    { WSMOUSECFG_TAP_ONE_BTNMAP, 0 },
	    { WSMOUSECFG_TAP_TWO_BTNMAP, 0 },
	    { WSMOUSECFG_TAP_THREE_BTNMAP, 0 }, },
	3
};

struct wsmouse_parameters cfg_mtbuttons = {
	(struct wsmouse_param[]) {
	    { WSMOUSECFG_MTBUTTONS, 0 }, },
	1
};

struct wsmouse_parameters cfg_scaling = {
	(struct wsmouse_param[]) {
	    { WSMOUSECFG_DX_SCALE, 0 },
	    { WSMOUSECFG_DY_SCALE, 0 } },
	2
};

struct wsmouse_parameters cfg_edges = {
	(struct wsmouse_param[]) {
	    { WSMOUSECFG_TOP_EDGE, 0 },
	    { WSMOUSECFG_RIGHT_EDGE, 0 },
	    { WSMOUSECFG_BOTTOM_EDGE, 0 },
	    { WSMOUSECFG_LEFT_EDGE, 0 } },
	4
};

struct wsmouse_parameters cfg_swapsides = {
	(struct wsmouse_param[]) {
	    { WSMOUSECFG_SWAPSIDES, 0 }, },
	1
};

struct wsmouse_parameters cfg_disable = {
	(struct wsmouse_param[]) {
	    { WSMOUSECFG_DISABLE, 0 }, },
	1
};

struct wsmouse_parameters cfg_revscroll = {
	(struct wsmouse_param[]) {
	    { WSMOUSECFG_REVERSE_SCROLLING, 0 }, },
	1
};

struct wsmouse_parameters cfg_param = {
	(struct wsmouse_param[]) {
	    { -1, 0 },
	    { -1, 0 },
	    { -1, 0 },
	    { -1, 0 } },
	4
};

int cfg_touchpad;

static int cfg_horiz_res;
static int cfg_vert_res;
static struct wsmouse_param cfg_buffer[WSMOUSECFG_MAX];


int
mousecfg_init(int dev_fd, const char **errstr)
{
	struct wsmouse_calibcoords coords;
	struct wsmouse_parameters parameters;
	struct wsmouse_param *param;
	enum wsmousecfg k;
	int i, err, type;

	*errstr = NULL;

	if ((err = ioctl(dev_fd, WSMOUSEIO_GTYPE, &type))) {
		*errstr = "WSMOUSEIO_GTYPE";
		return err;
	}
	cfg_touchpad = 0;
	for (i = 0; !cfg_touchpad && i < nitems(touchpad_types); i++)
		cfg_touchpad = (type == touchpad_types[i]);

	cfg_horiz_res = cfg_vert_res = 0;
	if (cfg_touchpad) {
		if ((err = ioctl(dev_fd, WSMOUSEIO_GCALIBCOORDS, &coords))) {
			*errstr = "WSMOUSEIO_GCALIBCOORDS";
			return err;
		}
		cfg_horiz_res = coords.resx;
		cfg_vert_res = coords.resy;
	}

	param = cfg_buffer;
	for (i = 0; i < nitems(range); i++)
		for (k = range[i][0]; k <= range[i][1]; k++, param++) {
			param->key = k;
			param->value = 0;
		}

	parameters.params = cfg_buffer;
	parameters.nparams = BASESIZE;
	if ((err = ioctl(dev_fd, WSMOUSEIO_GETPARAMS, &parameters))) {
		*errstr = "WSMOUSEIO_GETPARAMS";
		return (err);
	}
	if (cfg_touchpad) {
		parameters.params = cfg_buffer + BASESIZE;
		parameters.nparams = WSMOUSECFG_MAX - BASESIZE;
		if (ioctl(dev_fd, WSMOUSEIO_GETPARAMS, &parameters))
			cfg_touchpad = 0;
	}

	return (0);
}

/* Map a key to its buffer index. */
static int
index_of(enum wsmousecfg key)
{
	int i, n;

	for (i = 0, n = 0; i < nitems(range); i++) {
		if (key <= range[i][1] && key >= range[i][0]) {
			return (key - range[i][0] + n);
		}
		n += range[i][1] - range[i][0] + 1;
		if (!cfg_touchpad && n >= BASESIZE)
			break;
	}

	return (-1);
}

int
mousecfg_get_field(struct wsmouse_parameters *field)
{
	int i, n;

	for (i = 0; i < field->nparams; i++) {
		if ((n = index_of(field->params[i].key)) >= 0)
			field->params[i].value = cfg_buffer[n].value;
		else
			return (-1);
	}
	return (0);
}

int
mousecfg_put_field(int fd, struct wsmouse_parameters *field)
{
	int i, n, d, err;

	d = 0;
	for (i = 0; i < field->nparams; i++)
		if ((n = index_of(field->params[i].key)) < 0)
			return (-1);
		else
			d |= (cfg_buffer[n].value != field->params[i].value);

	if (!d)
		return (0);

	/* Write and read back immediately, wsmouse may normalize values. */
	if ((err = ioctl(fd, WSMOUSEIO_SETPARAMS, field))
	    || (err = ioctl(fd, WSMOUSEIO_GETPARAMS, field)))
		return err;

	for (i = 0; i < field->nparams; i++) {
		n = index_of(field->params[i].key);
		cfg_buffer[n].value = field->params[i].value;
	}

	return (0);
}

static int
get_value(struct wsmouse_parameters *field, enum wsmousecfg key)
{
	int i;

	for (i = 0; i < field->nparams && key != field->params[i].key; i++) {}

	return (i < field->nparams ? field->params[i].value : 0);
}

static void
set_value(struct wsmouse_parameters *field, enum wsmousecfg key, int value)
{
	int i;

	for (i = 0; i < field->nparams && key != field->params[i].key; i++) {}

	field->params[i].value = (i < field->nparams ? value : 0);
}

static float
get_percent(struct wsmouse_parameters *field, enum wsmousecfg key)
{
	return ((float) get_value(field, key) * 100 / 4096);
}

static void
set_percent(struct wsmouse_parameters *field, enum wsmousecfg key, float f)
{
	set_value(field, key, (int) ((f * 4096 + 50) / 100));
}

static int
set_tapping(struct wsmouse_parameters *field, char *tapping)
{
	int i1, i2, i3;

	switch (sscanf(tapping, "%d,%d,%d", &i1, &i2, &i3)) {
	case 1:
		if (i1 == 0) /* Disable */
			i2 = i3 = i1;
		else { /* Enable with defaults */
			i1 = 1; /* Left click */
			i2 = 3; /* Right click */
			i3 = 2; /* Middle click */
		}
		/* FALLTHROUGH */
	case 3:
		set_value(field, WSMOUSECFG_TAP_ONE_BTNMAP, i1);
		set_value(field, WSMOUSECFG_TAP_TWO_BTNMAP, i2);
		set_value(field, WSMOUSECFG_TAP_THREE_BTNMAP, i3);
		return (0);
	}
	return (-1);
}

static int
set_edges(struct wsmouse_parameters *field, char *edges)
{
	float f1, f2, f3, f4;

	if (sscanf(edges, "%f,%f,%f,%f", &f1, &f2, &f3, &f4) == 4) {
		set_percent(field, WSMOUSECFG_TOP_EDGE, f1);
		set_percent(field, WSMOUSECFG_RIGHT_EDGE,f2);
		set_percent(field, WSMOUSECFG_BOTTOM_EDGE, f3);
		set_percent(field, WSMOUSECFG_LEFT_EDGE, f4);
		return (0);
	}
	return (-1);
}

/*
 * Read or write up to four raw parameter values.  In this case
 * reading is a 'put' operation that writes back a value from the
 * buffer.
 */
static int
read_param(struct wsmouse_parameters *field, char *val)
{
	int i, j, n;

	n = sscanf(val, "%d:%d,%d:%d,%d:%d,%d:%d",
		&field->params[0].key, &field->params[0].value,
		&field->params[1].key, &field->params[1].value,
		&field->params[2].key, &field->params[2].value,
		&field->params[3].key, &field->params[3].value);
	if (n > 0 && (n & 1) == 0) {
		n /= 2;
		for (i = 0; i < n; i++) {
			if (index_of(field->params[i].key) < 0)
				return (-1);
		}
		field->nparams = n;
		return (0);
	}
	n = sscanf(val, "%d,%d,%d,%d",
		&field->params[0].key, &field->params[1].key,
		&field->params[2].key, &field->params[3].key);
	if (n > 0) {
		for (i = 0; i < n; i++) {
			if ((j = index_of(field->params[i].key)) < 0)
				return (-1);
			field->params[i].value = cfg_buffer[j].value;
		}
		field->nparams = n;
		return (0);
	}
	return (-1);
}

void
mousecfg_pr_field(struct wsmouse_parameters *field)
{
	int i, value;
	float f;

	if (field == &cfg_param) {
		for (i = 0; i < field->nparams; i++)
			printf(i > 0 ? ",%d:%d" : "%d:%d",
			    field->params[i].key,
			    field->params[i].value);
		return;
	}

	if (field == &cfg_scaling) {
		value = get_value(field, WSMOUSECFG_DX_SCALE);
		value = value == 0 ? 4096 : value;
		f = (float) value / 4096;
		printf("%.3f", f);
		return;
	}

	if (field == &cfg_edges) {
		printf("%.1f,%.1f,%.1f,%.1f",
		    get_percent(field, WSMOUSECFG_TOP_EDGE),
		    get_percent(field, WSMOUSECFG_RIGHT_EDGE),
		    get_percent(field, WSMOUSECFG_BOTTOM_EDGE),
		    get_percent(field, WSMOUSECFG_LEFT_EDGE));
		return;
	}

	for (i = 0; i < field->nparams; i++)
		printf(i > 0 ? ",%d" : "%d", field->params[i].value);
}

void
mousecfg_rd_field(struct wsmouse_parameters *field, char *val)
{
	int i, n;
	const char *s;
	float f;

	if (field == &cfg_param) {
		if (read_param(field, val))
			errx(1, "invalid input (param)");
		return;
	}

	if (field == &cfg_tapping) {
		if (set_tapping(field, val))
			errx(1, "invalid input (tapping)");
		return;
	}

	if (field == &cfg_scaling) {
		if (sscanf(val, "%f", &f) == 1) {
			n = (int) (f * 4096);
			set_value(field, WSMOUSECFG_DX_SCALE, n);
			if (cfg_horiz_res && cfg_vert_res)
				n = n * cfg_horiz_res / cfg_vert_res;
			set_value(field, WSMOUSECFG_DY_SCALE, n);
		} else {
			errx(1, "invalid input (scaling)");
		}
		return;
	}

	if (field == &cfg_edges) {
		if (set_edges(field, val))
			errx(1, "invalid input (edges)");
		return;
	}

	s = val;
	for (i = 0; i < field->nparams; i++) {
		if (sscanf(s, (i > 0 ? ",%d" : "%d"), &n) != 1)
			break;
		field->params[i].value = abs(n);
		for (s++; *s != '\0' && *s != ','; s++) {}
	}
	if (i < field->nparams || *s != '\0')
		errx(1, "invalid input '%s'", val);
}
