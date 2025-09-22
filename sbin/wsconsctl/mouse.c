/*	$OpenBSD: mouse.c,v 1.22 2024/10/05 13:27:16 chrisz Exp $	*/
/*	$NetBSD: mouse.c,v 1.3 1999/11/15 13:47:30 ad Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juergen Hannken-Illjes.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/ioctl.h>
#include <sys/time.h>
#include <dev/wscons/wsconsio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include "wsconsctl.h"
#include "mousecfg.h"

static u_int mstype;
static u_int resolution;
static u_int samplerate;
static int rawmode;

struct wsmouse_calibcoords wmcoords, wmcoords_save;

struct field mouse_field_tab[] = {
    { "resolution",		&resolution,	FMT_UINT,	FLG_WRONLY },
    { "samplerate",		&samplerate,	FMT_UINT,	FLG_WRONLY },
    { "type",			&mstype,	FMT_MSTYPE,	FLG_RDONLY },
    { "rawmode",		&rawmode,	FMT_UINT,	FLG_MODIFY|FLG_INIT},
    { "scale",			&wmcoords,	FMT_SCALE,	FLG_MODIFY|FLG_INIT},
    /* mouse and touchpad configuration (mousecfg): */
    { "reverse_scrolling",	&cfg_revscroll, FMT_CFG,	FLG_NORDBACK },
    /* touchpad-specific options: */
    { "tp.tapping",		&cfg_tapping,	FMT_CFG,	FLG_NORDBACK },
    { "tp.mtbuttons",		&cfg_mtbuttons,	FMT_CFG,	FLG_NORDBACK },
    { "tp.scaling",		&cfg_scaling,	FMT_CFG,	FLG_NORDBACK | FLG_WRONLY },
    { "tp.swapsides",		&cfg_swapsides,	FMT_CFG,	FLG_NORDBACK },
    { "tp.disable",		&cfg_disable,	FMT_CFG,	FLG_NORDBACK },
    { "tp.edges",		&cfg_edges,	FMT_CFG,	FLG_NORDBACK },
    { "tp.param",		&cfg_param,	FMT_CFG,	FLG_WRONLY },
    /* Add aliases.  These fields are valid for all wsmouse devices. */
    { "param",			&cfg_param,	FMT_CFG,	FLG_WRONLY },
    { "scaling",		&cfg_scaling,	FMT_CFG,	FLG_NORDBACK },
    { NULL }
};

static int dev_index = -1;

static struct wsmouse_parameters mtbtn_maxdist = {
    (struct wsmouse_param[]) { { WSMOUSECFG_MTBTN_MAXDIST, 0 } }, 1
};


void
mouse_init(int devfd, int devidx) {
	struct field *f;
	const char *errstr;
	int err;

	if (dev_index == devidx)
		return;

	if ((err = mousecfg_init(devfd, &errstr))) {
		devidx = -1;
		for (f = mouse_field_tab; f->name != NULL; f++) {
			if (f->format == FMT_CFG)
				f->flags |= FLG_DEAD;
		}
		warnx("mousecfg error: %s (%d)", errstr, err);
	} else if (cfg_touchpad) {
		for (f = mouse_field_tab; f->name != NULL; f++)
			if (f->format == FMT_CFG) {
				f->flags &= ~FLG_DEAD;
			}
		/* Hide the 'mtbuttons' field if the feature is unavailable. */
		if (mousecfg_get_field(&mtbtn_maxdist) ||
		    mtbtn_maxdist.params[0].value < 0) {
			f = field_by_value(mouse_field_tab, &cfg_mtbuttons);
			f->flags |= FLG_DEAD;
		}
	} else {
		for (f = mouse_field_tab; f->name != NULL; f++)
			if (f->format == FMT_CFG) {
				if (f->valp != &cfg_param
				    && f->valp != &cfg_scaling
				    && f->valp != &cfg_revscroll)
					f->flags |= FLG_DEAD;
				else
					f->flags &= ~FLG_DEAD;
			}
	}

	dev_index = devidx;
}

void
mouse_get_values(int fd)
{
	struct field *f;

	if (field_by_value(mouse_field_tab, &mstype)->flags & FLG_GET)
		if (ioctl(fd, WSMOUSEIO_GTYPE, &mstype) == -1)
			warn("WSMOUSEIO_GTYPE");

	if (field_by_value(mouse_field_tab, &rawmode)->flags & FLG_GET) {
		if (ioctl(fd, WSMOUSEIO_GCALIBCOORDS, &wmcoords) == -1) {
			if (errno == ENOTTY)
				field_by_value(mouse_field_tab,
				    &rawmode)->flags |= FLG_DEAD;
			else
				warn("WSMOUSEIO_GCALIBCOORDS");
		}
		rawmode = wmcoords.samplelen;
	}

	if (field_by_value(mouse_field_tab, &wmcoords)->flags & FLG_GET)
		if (ioctl(fd, WSMOUSEIO_GCALIBCOORDS, &wmcoords) == -1) {
			if (errno == ENOTTY)
				field_by_value(mouse_field_tab,
				    &wmcoords)->flags |= FLG_DEAD;
			else
				warn("WSMOUSEIO_GCALIBCOORDS");
	}

	for (f = mouse_field_tab; f->name != NULL; f++) {
		if (f->format != FMT_CFG || !(f->flags & FLG_GET)
		    || f->valp == &cfg_param || (f->flags & FLG_DEAD))
			continue;
		if (mousecfg_get_field((struct wsmouse_parameters *) f->valp)) {
			f->flags |= FLG_DEAD;
			warnx("mousecfg: invalid key in '%s'", f->name);
		}
	}
}

int
mouse_put_values(int fd)
{
	struct field *f;

	if (field_by_value(mouse_field_tab, &resolution)->flags & FLG_SET) {
		if (ioctl(fd, WSMOUSEIO_SRES, &resolution) == -1) {
			warn("WSMOUSEIO_SRES");
			return 1;
		}
	}
	if (field_by_value(mouse_field_tab, &rawmode)->flags & FLG_SET) {
		wmcoords.samplelen = rawmode;
		if (ioctl(fd, WSMOUSEIO_SCALIBCOORDS, &wmcoords) == -1) {
			if (errno == ENOTTY) {
				field_by_value(mouse_field_tab,
				    &rawmode)->flags |= FLG_DEAD;
			} else {
				warn("WSMOUSEIO_SCALIBCOORDS");
				return 1;
			}
		}
	}
	if (field_by_value(mouse_field_tab, &wmcoords)->flags & FLG_SET) {
		if (ioctl(fd, WSMOUSEIO_GCALIBCOORDS, &wmcoords_save) == -1) {
			if (errno == ENOTTY)
				field_by_value(mouse_field_tab,
				    &wmcoords)->flags |= FLG_DEAD;
			else
				warn("WSMOUSEIO_GCALIBCOORDS");
		}
		wmcoords.samplelen = wmcoords_save.samplelen;
		if (ioctl(fd, WSMOUSEIO_SCALIBCOORDS, &wmcoords) == -1) {
			if (errno == ENOTTY) {
				field_by_value(mouse_field_tab,
				    &wmcoords)->flags |= FLG_DEAD;
			} else {
				warn("WSMOUSEIO_SCALIBCOORDS");
				return 1;
			}
		}
	}

	for (f = mouse_field_tab; f->name != NULL; f++) {
		if (f->format != FMT_CFG || !(f->flags & FLG_SET))
			continue;
		if (mousecfg_put_field(fd,
		    (struct wsmouse_parameters *) f->valp)) {
			warn("mousecfg error (%s)", f->name);
			return 1;
		}
	}

	return 0;
}

char *
mouse_next_device(int index)
{
	static char devname[20];

	snprintf(devname, sizeof(devname), "/dev/wsmouse%d", index);
	return (devname);
}
