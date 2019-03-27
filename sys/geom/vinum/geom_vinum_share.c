/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
 * Copyright (c) 1997, 1998, 1999
 *      Nan Yang Computer Services Limited.  All rights reserved.
 *
 *  Parts written by Greg Lehey
 *
 *  This software is distributed under the so-called ``Berkeley
 *  License'':
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Nan Yang Computer
 *      Services Limited.
 * 4. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even if
 * advised of the possibility of such damage.
 *
 */

/* This file is shared between kernel and userland. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#ifdef _KERNEL
#include <sys/malloc.h>
#include <sys/systm.h>

#include <geom/geom.h>
#define	iswhite(c) (((c) == ' ') || ((c) == '\t'))
#else
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define	iswhite	isspace
#define	g_free	free
#endif /* _KERNEL */

#include <sys/mutex.h>
#include <sys/queue.h>

#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum_share.h>

/*
 * Take a blank separated list of tokens and turn it into a list of
 * individual nul-delimited strings.  Build a list of pointers at
 * token, which must have enough space for the tokens.  Return the
 * number of tokens, or -1 on error (typically a missing string
 * delimiter).
 */
int
gv_tokenize(char *cptr, char *token[], int maxtoken)
{
	int tokennr;	/* Index of this token. */
	char delim;	/* Delimiter for searching for the partner. */
	
	for (tokennr = 0; tokennr < maxtoken;) {

		/* Skip leading white space. */
		while (iswhite(*cptr))
			cptr++;

		/* End of line. */
		if ((*cptr == '\0') || (*cptr == '\n') || (*cptr == '#'))
			return tokennr;

		delim = *cptr;
		token[tokennr] = cptr;		/* Point to it. */
		tokennr++;			/* One more. */

		/* Run off the end? */
		if (tokennr == maxtoken)
			return tokennr;

		/* Quoted? */
		if ((delim == '\'') || (delim == '"')) {
			for (;;) {
				cptr++;

				/* Found the partner. */
				if ((*cptr == delim) && (cptr[-1] != '\\')) {
					cptr++;

					/* Space after closing quote needed. */
					if (!iswhite(*cptr))
						return -1;

					/* Delimit. */
					*cptr++ = '\0';

				/* End-of-line? */
				} else if ((*cptr == '\0') || (*cptr == '\n'))
					return -1;
			}

		/* Not quoted. */
		} else {
			while ((*cptr != '\0') &&
			    (!iswhite(*cptr)) &&
			    (*cptr != '\n'))
				cptr++;

			/* Not end-of-line; delimit and move to the next. */
			if (*cptr != '\0')
				*cptr++ = '\0';
		}
	}

	/* Can't get here. */
	return maxtoken;
}


/*
 * Take a number with an optional scale factor and convert it to a number of
 * bytes.
 *
 * The scale factors are:
 *
 * s    sectors (of 512 bytes)
 * b    blocks (of 512 bytes).  This unit is deprecated, because it's
 *      confusing, but maintained to avoid confusing Veritas users.
 * k    kilobytes (1024 bytes)
 * m    megabytes (of 1024 * 1024 bytes)
 * g    gigabytes (of 1024 * 1024 * 1024 bytes)
 *
 * XXX: need a way to signal error
 */
off_t
gv_sizespec(char *spec)
{
	uint64_t size;
	char *s;
	int sign;
	
	size = 0;
	sign = 1;
	if (spec != NULL) {		/* we have a parameter */
		s = spec;
		if (*s == '-') {	/* negative, */
			sign = -1;
			s++;		/* skip */
		}

		/* It's numeric. */
		if ((*s >= '0') && (*s <= '9')) {

			/* It's numeric. */
			while ((*s >= '0') && (*s <= '9'))
				/* Convert it. */
				size = size * 10 + *s++ - '0';

			switch (*s) {
			case '\0':
				return size * sign;
			
			case 'B':
			case 'b':
			case 'S':
			case 's':
				return size * sign * 512;
			
			case 'K':
			case 'k':
				return size * sign * 1024;
			
			case 'M':
			case 'm':
				return size * sign * 1024 * 1024;
			
			case 'G':
			case 'g':
				return size * sign * 1024 * 1024 * 1024;
			}
		}
	}

	return (0);
}

const char *
gv_drivestate(int state)
{
	switch (state) {
	case GV_DRIVE_DOWN:
		return "down";
	case GV_DRIVE_UP:
		return "up";
	default:
		return "??";
	}
}

int
gv_drivestatei(char *buf)
{
	if (!strcmp(buf, "up"))
		return (GV_DRIVE_UP);
	else
		return (GV_DRIVE_DOWN);
}

/* Translate from a string to a subdisk state. */
int
gv_sdstatei(char *buf)
{
	if (!strcmp(buf, "up"))
		return (GV_SD_UP);
	else if (!strcmp(buf, "reviving"))
		return (GV_SD_REVIVING);
	else if (!strcmp(buf, "initializing"))
		return (GV_SD_INITIALIZING);
	else if (!strcmp(buf, "stale"))
		return (GV_SD_STALE);
	else
		return (GV_SD_DOWN);
}

/* Translate from a subdisk state to a string. */
const char *
gv_sdstate(int state)
{
	switch (state) {
	case GV_SD_INITIALIZING:
		return "initializing";
	case GV_SD_STALE:
		return "stale";
	case GV_SD_DOWN:
		return "down";
	case GV_SD_REVIVING:
		return "reviving";
	case GV_SD_UP:
		return "up";
	default:
		return "??";
	}
}

/* Translate from a string to a plex state. */
int
gv_plexstatei(char *buf)
{
	if (!strcmp(buf, "up"))
		return (GV_PLEX_UP);
	else if (!strcmp(buf, "initializing"))
		return (GV_PLEX_INITIALIZING);
	else if (!strcmp(buf, "degraded"))
		return (GV_PLEX_DEGRADED);
	else if (!strcmp(buf, "growable"))
		return (GV_PLEX_GROWABLE);
	else
		return (GV_PLEX_DOWN);
}

/* Translate from a plex state to a string. */
const char *
gv_plexstate(int state)
{
	switch (state) {
	case GV_PLEX_DOWN:
		return "down";
	case GV_PLEX_INITIALIZING:
		return "initializing";
	case GV_PLEX_DEGRADED:
		return "degraded";
	case GV_PLEX_GROWABLE:
		return "growable";
	case GV_PLEX_UP:
		return "up";
	default:
		return "??";
	}
}

/* Translate from a string to a plex organization. */
int
gv_plexorgi(char *buf)
{
	if (!strcmp(buf, "concat"))
		return (GV_PLEX_CONCAT);
	else if (!strcmp(buf, "striped"))
		return (GV_PLEX_STRIPED);
	else if (!strcmp(buf, "raid5"))
		return (GV_PLEX_RAID5);
	else
		return (GV_PLEX_DISORG);
}

int
gv_volstatei(char *buf)
{
	if (!strcmp(buf, "up"))
		return (GV_VOL_UP);
	else
		return (GV_VOL_DOWN);
}

const char *
gv_volstate(int state)
{
	switch (state) {
	case GV_VOL_UP:
		return "up";
	case GV_VOL_DOWN:
		return "down";
	default:
		return "??";
	}
}

/* Translate from a plex organization to a string. */
const char *
gv_plexorg(int org)
{
	switch (org) {
	case GV_PLEX_DISORG:
		return "??";
	case GV_PLEX_CONCAT:
		return "concat";
	case GV_PLEX_STRIPED:
		return "striped";
	case GV_PLEX_RAID5:
		return "raid5";
	default:
		return "??";
	}
}

const char *
gv_plexorg_short(int org)
{
	switch (org) {
	case GV_PLEX_DISORG:
		return "??";
	case GV_PLEX_CONCAT:
		return "C";
	case GV_PLEX_STRIPED:
		return "S";
	case GV_PLEX_RAID5:
		return "R5";
	default:
		return "??";
	}
}

struct gv_sd *
gv_alloc_sd(void)
{
	struct gv_sd *s;

#ifdef _KERNEL
	s = g_malloc(sizeof(struct gv_sd), M_NOWAIT);
#else
	s = malloc(sizeof(struct gv_sd));
#endif
	if (s == NULL)
		return (NULL);
	bzero(s, sizeof(struct gv_sd));
	s->plex_offset = -1;
	s->size = -1;
	s->drive_offset = -1;
	return (s);
}

struct gv_drive *
gv_alloc_drive(void)
{
	struct gv_drive *d;

#ifdef _KERNEL
	d = g_malloc(sizeof(struct gv_drive), M_NOWAIT);
#else
	d = malloc(sizeof(struct gv_drive));
#endif
	if (d == NULL)
		return (NULL);
	bzero(d, sizeof(struct gv_drive));
	return (d);
}

struct gv_volume *
gv_alloc_volume(void)
{
	struct gv_volume *v;

#ifdef _KERNEL
	v = g_malloc(sizeof(struct gv_volume), M_NOWAIT);
#else
	v = malloc(sizeof(struct gv_volume));
#endif
	if (v == NULL)
		return (NULL);
	bzero(v, sizeof(struct gv_volume));
	return (v);
}

struct gv_plex *
gv_alloc_plex(void)
{
	struct gv_plex *p;

#ifdef _KERNEL
	p = g_malloc(sizeof(struct gv_plex), M_NOWAIT);
#else
	p = malloc(sizeof(struct gv_plex));
#endif
	if (p == NULL)
		return (NULL);
	bzero(p, sizeof(struct gv_plex));
	return (p);
}

/* Get a new drive object. */
struct gv_drive *
gv_new_drive(int max, char *token[])
{
	struct gv_drive *d;
	int j, errors;
	char *ptr;

	if (token[1] == NULL || *token[1] == '\0')
		return (NULL);
	d = gv_alloc_drive();
	if (d == NULL)
		return (NULL);
	errors = 0;
	for (j = 1; j < max; j++) {
		if (!strcmp(token[j], "state")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			d->state = gv_drivestatei(token[j]);
		} else if (!strcmp(token[j], "device")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			ptr = token[j];

			if (strncmp(ptr, "/dev/", 5) == 0)
				ptr += 5;
			strlcpy(d->device, ptr, sizeof(d->device));
		} else {
			/* We assume this is the drive name. */
			strlcpy(d->name, token[j], sizeof(d->name));
		}
	}

	if (strlen(d->name) == 0 || strlen(d->device) == 0)
		errors++;

	if (errors) {
		g_free(d);
		return (NULL);
	}

	return (d);
}

/* Get a new volume object. */
struct gv_volume *
gv_new_volume(int max, char *token[])
{
	struct gv_volume *v;
	int j, errors;

	if (token[1] == NULL || *token[1] == '\0')
		return (NULL);

	v = gv_alloc_volume();
	if (v == NULL)
		return (NULL);

	errors = 0;
	for (j = 1; j < max; j++) {
		if (!strcmp(token[j], "state")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			v->state = gv_volstatei(token[j]);
		} else {
			/* We assume this is the volume name. */
			strlcpy(v->name, token[j], sizeof(v->name));
		}
	}

	if (strlen(v->name) == 0)
		errors++;

	if (errors) {
		g_free(v);
		return (NULL);
	}

	return (v);
}

/* Get a new plex object. */
struct gv_plex *
gv_new_plex(int max, char *token[])
{
	struct gv_plex *p;
	int j, errors;

	if (token[1] == NULL || *token[1] == '\0')
		return (NULL);

	p = gv_alloc_plex();
	if (p == NULL)
		return (NULL);

	errors = 0;
	for (j = 1; j < max; j++) {
		if (!strcmp(token[j], "name")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			strlcpy(p->name, token[j], sizeof(p->name));
		} else if (!strcmp(token[j], "org")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			p->org = gv_plexorgi(token[j]);
			if ((p->org == GV_PLEX_RAID5) ||
			    (p->org == GV_PLEX_STRIPED)) {
				j++;
				if (j >= max) {
					errors++;
					break;
				}
				p->stripesize = gv_sizespec(token[j]);
				if (p->stripesize == 0) {
					errors++;
					break;
				}
			}
		} else if (!strcmp(token[j], "state")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			p->state = gv_plexstatei(token[j]);
		} else if (!strcmp(token[j], "vol") ||
			    !strcmp(token[j], "volume")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			strlcpy(p->volume, token[j], sizeof(p->volume));
		} else {
			errors++;
			break;
		}
	}

	if (errors) {
		g_free(p);
		return (NULL);
	}

	return (p);
}



/* Get a new subdisk object. */
struct gv_sd *
gv_new_sd(int max, char *token[])
{
	struct gv_sd *s;
	int j, errors;

	if (token[1] == NULL || *token[1] == '\0')
		return (NULL);

	s = gv_alloc_sd();
	if (s == NULL)
		return (NULL);

	errors = 0;
	for (j = 1; j < max; j++) {
		if (!strcmp(token[j], "name")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			strlcpy(s->name, token[j], sizeof(s->name));
		} else if (!strcmp(token[j], "drive")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			strlcpy(s->drive, token[j], sizeof(s->drive));
		} else if (!strcmp(token[j], "plex")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			strlcpy(s->plex, token[j], sizeof(s->plex));
		} else if (!strcmp(token[j], "state")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			s->state = gv_sdstatei(token[j]);
		} else if (!strcmp(token[j], "len") ||
		    !strcmp(token[j], "length")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			s->size = gv_sizespec(token[j]);
			if (s->size <= 0)
				s->size = -1;
		} else if (!strcmp(token[j], "driveoffset")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			s->drive_offset = gv_sizespec(token[j]);
			if (s->drive_offset != 0 &&
			    s->drive_offset < GV_DATA_START) {
				errors++;
				break;
			}
		} else if (!strcmp(token[j], "plexoffset")) {
			j++;
			if (j >= max) {
				errors++;
				break;
			}
			s->plex_offset = gv_sizespec(token[j]);
			if (s->plex_offset < 0) {
				errors++;
				break;
			}
		} else {
			errors++;
			break;
		}
	}

	if (strlen(s->drive) == 0)
		errors++;

	if (errors) {
		g_free(s);
		return (NULL);
	}

	return (s);
}

/*
 * Take a size in bytes and return a pointer to a string which represents the
 * size best.  If lj is != 0, return left justified, otherwise in a fixed 10
 * character field suitable for columnar printing.
 *
 * Note this uses a static string: it's only intended to be used immediately
 * for printing.
 */
const char *
gv_roughlength(off_t bytes, int lj)
{
	static char desc[16];
	
	/* Gigabytes. */
	if (bytes > (off_t)MEGABYTE * 10000)
		snprintf(desc, sizeof(desc), lj ? "%jd GB" : "%10jd GB",
		    bytes / GIGABYTE);

	/* Megabytes. */
	else if (bytes > KILOBYTE * 10000)
		snprintf(desc, sizeof(desc), lj ? "%jd MB" : "%10jd MB",
		    bytes / MEGABYTE);

	/* Kilobytes. */
	else if (bytes > 10000)
		snprintf(desc, sizeof(desc), lj ? "%jd kB" : "%10jd kB",
		    bytes / KILOBYTE);

	/* Bytes. */
	else
		snprintf(desc, sizeof(desc), lj ? "%jd  B" : "%10jd  B", bytes);

	return (desc);
}
