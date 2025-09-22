/*	$OpenBSD: kbd_wscons.c,v 1.36 2022/05/05 16:12:42 bluhm Exp $ */

/*
 * Copyright (c) 2001 Mats O Jansson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/ioctl.h>
#include <sys/time.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsksymdef.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	NUM_KBD	10

char *kbtype_tab[] = {
	"pc-xt/pc-at",
	"usb",
	"adb",
	"lk201",
	"sun",
	"sun5",
	"hil",
	"gsc",
	"sgi"
};
enum {	SA_PCKBD,
	SA_UKBD,
	SA_AKBD,
	SA_LKKBD,
	SA_SUNKBD,
	SA_SUN5KBD,
	SA_HILKBD,
	SA_GSCKBD,
	SA_SGIKBD,

	SA_MAX
};

struct nameint {
	int value;
	char *name;
};

struct nameint kbdenc_tab[] = {
	KB_ENCTAB
	,
	{ 0, NULL }
};

struct nameint kbdvar_tab[] = {
	KB_VARTAB
	,
	{ 0, NULL }
};

extern char *__progname;

void	kbd_show_enc(struct wskbd_encoding_data *encs, int idx);
void	kbd_get_encs(int fd, struct wskbd_encoding_data *encs);
void	kbd_list(void);
void	kbd_set(char *name, int verbose);

void
kbd_show_enc(struct wskbd_encoding_data *encs, int idx)
{
	int found;
	kbd_t encoding, variant;
	struct nameint *n;
	int i;

	printf("tables available for %s keyboard:\nencoding\n\n",
	    kbtype_tab[idx]);

	for (i = 0; i < encs->nencodings; i++) {
		found = 0;
		encoding = encs->encodings[i];
		for (n = &kbdenc_tab[0]; n->value; n++) {
			if (n->value == KB_ENCODING(encoding)) {
				printf("%s", n->name);
				found++;
			}
		}
		if (found == 0)
			printf("<encoding 0x%04x>", KB_ENCODING(encoding));
		found = 0;
		variant = KB_VARIANT(encoding);
		for (n = &kbdvar_tab[0]; n->value; n++) {
			if ((n->value & KB_VARIANT(encoding)) == n->value) {
				printf(".%s", n->name);
				variant &= ~n->value;
			}
		}
		if (variant != 0)
			printf(".<variant 0x%08x>", variant);
		printf("\n");
	}
	printf("\n");
}

void
kbd_get_encs(int fd, struct wskbd_encoding_data *encs)
{
	int nencodings = 64;

	encs->nencodings = nencodings;
	while (encs->nencodings == nencodings) {
		encs->encodings = reallocarray(encs->encodings,
		    encs->nencodings, sizeof(kbd_t));
		if (encs->encodings == NULL)
			err(1, NULL);
		if (ioctl(fd, WSKBDIO_GETENCODINGS, encs) == -1)
			err(1, "WSKBDIO_GETENCODINGS");
		if (encs->nencodings == nencodings) {
			nencodings *= 2;
			encs->nencodings = nencodings;
		}
	}
}

void
kbd_list(void)
{
	int	kbds[SA_MAX];
	struct wskbd_encoding_data encs[SA_MAX];
	int	fd, i, kbtype, t, error = 0;
	char	device[PATH_MAX];

	memset(kbds, 0, sizeof(kbds));
	memset(encs, 0, sizeof(encs));

	/* Go through all keyboards. */
	for (i = 0; i < NUM_KBD; i++) {
		(void) snprintf(device, sizeof device, "/dev/wskbd%d", i);
		fd = open(device, O_WRONLY);
		if (fd == -1)
			fd = open(device, O_RDONLY);
		if (fd == -1) {
			/* remember the first error number */
			if (error == 0)
				error = errno;
		} else {
			/* at least one success, do not print error */
			error = -1;

			if (ioctl(fd, WSKBDIO_GTYPE, &kbtype) == -1)
				err(1, "WSKBDIO_GTYPE %s", device);
			switch (kbtype) {
			case WSKBD_TYPE_PC_XT:
			case WSKBD_TYPE_PC_AT:
				t = SA_PCKBD;
				break;
			case WSKBD_TYPE_USB:
				t = SA_UKBD;
				break;
			case WSKBD_TYPE_ADB:
				t = SA_AKBD;
				break;
			case WSKBD_TYPE_LK201:
			case WSKBD_TYPE_LK401:
				t = SA_LKKBD;
				break;
			case WSKBD_TYPE_SUN:
				t = SA_SUNKBD;
				break;
			case WSKBD_TYPE_SUN5:
				t = SA_SUN5KBD;
				break;
			case WSKBD_TYPE_HIL:
				t = SA_HILKBD;
				break;
			case WSKBD_TYPE_GSC:
				t = SA_GSCKBD;
				break;
			case WSKBD_TYPE_SGI:
				t = SA_SGIKBD;
				break;
			default:
				t = SA_MAX;
				break;
			}

			if (t != SA_MAX) {
				kbds[t]++;
				if (encs[t].encodings == NULL)
					kbd_get_encs(fd, &encs[t]);
			}
			close(fd);
		}
	}
	if (error > 0) {
		errno = error;
		err(1, "/dev/wskbd0");
	}

	for (i = 0; i < SA_MAX; i++)
		if (kbds[i] != 0)
			kbd_show_enc(&encs[i], i);

	for (i = 0; i < SA_MAX; i++)
		free(encs[i].encodings);
}

void
kbd_set(char *name, int verbose)
{
	char	buf[LINE_MAX], *c, *b, device[sizeof "/dev/wskbd00"];
	int	map = 0, v, i, fd, error = 0;
	struct nameint *n;

	c = name;
	b = buf;
	while (*c != '.' && *c != '\0' && b < buf + sizeof(buf) - 1)
		*b++ = *c++;
	*b = '\0';
	n = &kbdenc_tab[0];
	while (n->value) {
		if (strcmp(n->name, buf) == 0)
			map = n->value;
		n++;
	}
	if (map == 0)
		errx(1, "unknown encoding %s", buf);
	while (*c == '.') {
		b = buf;
		c++;
		while (*c != '.' && *c != '\0' && b < buf + sizeof(buf) - 1)
			*b++ = *c++;
		*b = '\0';
		v = 0;
		for (n = &kbdvar_tab[0]; n->value; n++) {
			if (strcmp(n->name, buf) == 0)
				v = n->value;
		}
		if (v == 0)
			errx(1, "unknown variant %s", buf);
		map |= v;
	}

	/* Go through all keyboards. */
	v = 0;
	for (i = 0; i < NUM_KBD; i++) {
		(void) snprintf(device, sizeof device, "/dev/wskbd%d", i);
		fd = open(device, O_WRONLY);
		if (fd == -1)
			fd = open(device, O_RDONLY);
		if (fd == -1) {
			/* remember the first error number */
			if (error == 0)
				error = errno;
		} else {
			/* at least one success, do not print error */
			error = -1;

			if (ioctl(fd, WSKBDIO_SETENCODING, &map) == -1) {
				if (errno != EINVAL)
					err(1, "WSKBDIO_SETENCODING %s",
					    device);
				fprintf(stderr,
				    "%s: unsupported encoding %s on %s\n",
				    __progname, name, device);
			} else
				v++;
			close(fd);
		}
	}
	if (error > 0) {
		errno = error;
		err(1, "/dev/wskbd0");
	}

	if (verbose && v > 0)
		fprintf(stderr, "kbd: keyboard mapping set to %s\n", name);
}
