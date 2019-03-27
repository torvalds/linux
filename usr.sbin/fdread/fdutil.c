/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Joerg Wunsch
 *
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE DEVELOPERS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <dev/ic/nec765.h>

#include <sys/fdcio.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "fdutil.h"

/*
 * Decode the FDC status pointed to by `fdcsp', and print a textual
 * translation to stderr.  If `terse' is false, the numerical FDC
 * register status is printed, too.
 */
void
printstatus(struct fdc_status *fdcsp, int terse)
{
	char msgbuf[100];

	if (!terse)
		fprintf(stderr,
		"\nFDC status ST0=%#x ST1=%#x ST2=%#x C=%u H=%u R=%u N=%u:\n",
			fdcsp->status[0] & 0xff,
			fdcsp->status[1] & 0xff,
			fdcsp->status[2] & 0xff,
			fdcsp->status[3] & 0xff,
			fdcsp->status[4] & 0xff,
			fdcsp->status[5] & 0xff,
			fdcsp->status[6] & 0xff);

	if ((fdcsp->status[0] & NE7_ST0_IC_RC) == 0) {
		sprintf(msgbuf, "timeout");
	} else if ((fdcsp->status[0] & NE7_ST0_IC_RC) != NE7_ST0_IC_AT) {
		sprintf(msgbuf, "unexcpted interrupt code %#x",
			fdcsp->status[0] & NE7_ST0_IC_RC);
	} else {
		strcpy(msgbuf, "unexpected error code in ST1/ST2");

		if (fdcsp->status[1] & NE7_ST1_EN)
			strcpy(msgbuf, "end of cylinder (wrong format)");
		else if (fdcsp->status[1] & NE7_ST1_DE) {
			if (fdcsp->status[2] & NE7_ST2_DD)
				strcpy(msgbuf, "CRC error in data field");
			else
				strcpy(msgbuf, "CRC error in ID field");
		} else if (fdcsp->status[1] & NE7_ST1_MA) {
			if (fdcsp->status[2] & NE7_ST2_MD)
				strcpy(msgbuf, "no address mark in data field");
			else
				strcpy(msgbuf, "no address mark in ID field");
		} else if (fdcsp->status[2] & NE7_ST2_WC)
			strcpy(msgbuf, "wrong cylinder (format mismatch)");
		else if (fdcsp->status[1] & NE7_ST1_ND)
			strcpy(msgbuf, "no data (sector not found)");
	}
	fputs(msgbuf, stderr);
}

static struct fd_type fd_types_auto[1] =
    { { 0,0,0,0,0,0,0,0,0,0,0,FL_AUTO } };


static struct fd_type fd_types_288m[] = {
#if 0
	{ FDF_3_2880 },
#endif
	{ FDF_3_1722 },
	{ FDF_3_1476 },
	{ FDF_3_1440 },
	{ FDF_3_1200 },
	{ FDF_3_820 },
	{ FDF_3_800 },
	{ FDF_3_720 },
	{ 0,0,0,0,0,0,0,0,0,0,0,0 }
};

static struct fd_type fd_types_144m[] = {
	{ FDF_3_1722 },
	{ FDF_3_1476 },
	{ FDF_3_1440 },
	{ FDF_3_1200 },
	{ FDF_3_820 },
	{ FDF_3_800 },
	{ FDF_3_720 },
	{ 0,0,0,0,0,0,0,0,0,0,0,0 }
};

static struct fd_type fd_types_12m[] = {
	{ FDF_5_1200 },
	{ FDF_5_1230 },
	{ FDF_5_1480 },
	{ FDF_5_1440 },
	{ FDF_5_820 },
	{ FDF_5_800 },
	{ FDF_5_720 },
	{ FDF_5_360 | FL_2STEP },
	{ FDF_5_640 },
	{ 0,0,0,0,0,0,0,0,0,0,0,0 }
};

static struct fd_type fd_types_720k[] =
{
	{ FDF_3_720 },
	{ 0,0,0,0,0,0,0,0,0,0,0,0 }
};

static struct fd_type fd_types_360k[] =
{
	{ FDF_5_360 },
	{ 0,0,0,0,0,0,0,0,0,0,0,0 }
};


/*
 * Parse a format string, and fill in the parameter pointed to by `out'.
 *
 * sectrac,secsize,datalen,gap,ncyls,speed,heads,f_gap,f_inter,offs2,flags[...]
 *
 * sectrac = sectors per track
 * secsize = sector size in bytes
 * datalen = length of sector if secsize == 128
 * gap     = gap length when reading
 * ncyls   = number of cylinders
 * speed   = transfer speed 250/300/500/1000 KB/s
 * heads   = number of heads
 * f_gap   = gap length when formatting
 * f_inter = sector interleave when formatting
 * offs2   = offset of sectors on side 2
 * flags   = +/-mfm | +/-2step | +/-perpend
 *             mfm - use MFM recording
 *             2step - use 2 steps between cylinders
 *             perpend - user perpendicular (vertical) recording
 *
 * Any omitted value will be passed on from parameter `in'.
 */
void
parse_fmt(const char *s, enum fd_drivetype type,
	  struct fd_type in, struct fd_type *out)
{
	int i, j;
	const char *cp;
	char *s1;

	*out = in;

	for (i = 0;; i++) {
		if (s == NULL)
			break;

		if ((cp = strchr(s, ',')) == NULL) {
			s1 = strdup(s);
			if (s1 == NULL)
				abort();
			s = 0;
		} else {
			s1 = malloc(cp - s + 1);
			if (s1 == NULL)
				abort();
			memcpy(s1, s, cp - s);
			s1[cp - s] = 0;

			s = cp + 1;
		}
		if (strlen(s1) == 0) {
			free(s1);
			continue;
		}

		switch (i) {
		case 0:		/* sectrac */
			if (getnum(s1, &out->sectrac))
				errx(EX_USAGE,
				     "bad numeric value for sectrac: %s", s1);
			break;

		case 1:		/* secsize */
			if (getnum(s1, &j))
				errx(EX_USAGE,
				     "bad numeric value for secsize: %s", s1);
			if (j == 128) out->secsize = 0;
			else if (j == 256) out->secsize = 1;
			else if (j == 512) out->secsize = 2;
			else if (j == 1024) out->secsize = 3;
			else
				errx(EX_USAGE, "bad sector size %d", j);
			break;

		case 2:		/* datalen */
			if (getnum(s1, &j))
				errx(EX_USAGE,
				     "bad numeric value for datalen: %s", s1);
			if (j >= 256)
				errx(EX_USAGE, "bad datalen %d", j);
			out->datalen = j;
			break;

		case 3:		/* gap */
			if (getnum(s1, &out->gap))
				errx(EX_USAGE,
				     "bad numeric value for gap: %s", s1);
			break;

		case 4:		/* ncyls */
			if (getnum(s1, &j))
				errx(EX_USAGE,
				     "bad numeric value for ncyls: %s", s1);
			if (j > 85)
				errx(EX_USAGE, "bad # of cylinders %d", j);
			out->tracks = j;
			break;

		case 5:		/* speed */
			if (getnum(s1, &j))
				errx(EX_USAGE,
				     "bad numeric value for speed: %s", s1);
			switch (type) {
			default:
				abort(); /* paranoia */

			case FDT_360K:
			case FDT_720K:
				if (j == 250)
					out->trans = FDC_250KBPS;
				else
					errx(EX_USAGE, "bad speed %d", j);
				break;

			case FDT_12M:
				if (j == 300)
					out->trans = FDC_300KBPS;
				else if (j == 250)
					out->trans = FDC_250KBPS;
				else if (j == 500)
					out->trans = FDC_500KBPS;
				else
					errx(EX_USAGE, "bad speed %d", j);
				break;

			case FDT_288M:
				if (j == 1000)
					out->trans = FDC_1MBPS;
				/* FALLTHROUGH */
			case FDT_144M:
				if (j == 250)
					out->trans = FDC_250KBPS;
				else if (j == 500)
					out->trans = FDC_500KBPS;
				else
					errx(EX_USAGE, "bad speed %d", j);
				break;
			}
			break;

		case 6:		/* heads */
			if (getnum(s1, &j))
				errx(EX_USAGE,
				     "bad numeric value for heads: %s", s1);
			if (j == 1 || j == 2)
				out->heads = j;
			else
				errx(EX_USAGE, "bad # of heads %d", j);
			break;

		case 7:		/* f_gap */
			if (getnum(s1, &out->f_gap))
				errx(EX_USAGE,
				     "bad numeric value for f_gap: %s", s1);
			break;

		case 8:		/* f_inter */
			if (getnum(s1, &out->f_inter))
				errx(EX_USAGE,
				     "bad numeric value for f_inter: %s", s1);
			break;

		case 9:		/* offs2 */
			if (getnum(s1, &out->offset_side2))
				errx(EX_USAGE,
				     "bad numeric value for offs2: %s", s1);
			break;

		default:
			if (strcmp(s1, "+mfm") == 0)
				out->flags |= FL_MFM;
			else if (strcmp(s1, "-mfm") == 0)
				out->flags &= ~FL_MFM;
			else if (strcmp(s1, "+auto") == 0)
				out->flags |= FL_AUTO;
			else if (strcmp(s1, "-auto") == 0)
				out->flags &= ~FL_AUTO;
			else if (strcmp(s1, "+2step") == 0)
				out->flags |= FL_2STEP;
			else if (strcmp(s1, "-2step") == 0)
				out->flags &= ~FL_2STEP;
			else if (strcmp(s1, "+perpnd") == 0)
				out->flags |= FL_PERPND;
			else if (strcmp(s1, "-perpnd") == 0)
				out->flags &= ~FL_PERPND;
			else
				errx(EX_USAGE, "bad flag: %s", s1);
			break;
		}
		free(s1);
	}

	out->size = out->tracks * out->heads * out->sectrac;
}

/*
 * Print a textual translation of the drive (density) type described
 * by `in' to stdout.  The string uses the same form that is parseable
 * by parse_fmt().
 */
void
print_fmt(struct fd_type in)
{
	int secsize, speed;

	secsize = 128 << in.secsize;
	switch (in.trans) {
	case FDC_250KBPS:	speed = 250; break;
	case FDC_300KBPS:	speed = 300; break;
	case FDC_500KBPS:	speed = 500; break;
	case FDC_1MBPS:		speed = 1000; break;
	default:		speed = 1; break;
	}

	printf("%d,%d,%#x,%#x,%d,%d,%d,%#x,%d,%d",
	       in.sectrac, secsize, in.datalen, in.gap, in.tracks,
	       speed, in.heads, in.f_gap, in.f_inter, in.offset_side2);
	if (in.flags & FL_MFM)
		printf(",+mfm");
	if (in.flags & FL_2STEP)
		printf(",+2step");
	if (in.flags & FL_PERPND)
		printf(",+perpnd");
	if (in.flags & FL_AUTO)
		printf(",+auto");
	putc('\n', stdout);
}

/*
 * Based on `size' (in kilobytes), walk through the table of known
 * densities for drive type `type' and see if we can find one.  If
 * found, return it (as a pointer to static storage), otherwise return
 * NULL.
 */
struct fd_type *
get_fmt(int size, enum fd_drivetype type)
{
	int i, n;
	struct fd_type *fdtp;

	switch (type) {
	default:
		return (0);

	case FDT_360K:
		fdtp = fd_types_360k;
		n = sizeof fd_types_360k / sizeof(struct fd_type);
		break;

	case FDT_720K:
		fdtp = fd_types_720k;
		n = sizeof fd_types_720k / sizeof(struct fd_type);
		break;

	case FDT_12M:
		fdtp = fd_types_12m;
		n = sizeof fd_types_12m / sizeof(struct fd_type);
		break;

	case FDT_144M:
		fdtp = fd_types_144m;
		n = sizeof fd_types_144m / sizeof(struct fd_type);
		break;

	case FDT_288M:
		fdtp = fd_types_288m;
		n = sizeof fd_types_288m / sizeof(struct fd_type);
		break;
	}

	if (size == -1)
		return fd_types_auto;

	for (i = 0; i < n; i++, fdtp++) {
		fdtp->size = fdtp->sectrac * fdtp->heads * fdtp->tracks;
		if (((128 << fdtp->secsize) * fdtp->size / 1024) == size)
			return (fdtp);
	}
	return (0);
}

/*
 * Parse a number from `s'.  If the string cannot be converted into a
 * number completely, return -1, otherwise 0.  The result is returned
 * in `*res'.
 */
int
getnum(const char *s, int *res)
{
	unsigned long ul;
	char *cp;

	ul = strtoul(s, &cp, 0);
	if (*cp != '\0')
	  return (-1);

	*res = (int)ul;
	return (0);
}

/*
 * Return a short name and a verbose description for the drive
 * described by `t'.
 */
void
getname(enum fd_drivetype t, const char **name, const char **descr)
{

	switch (t) {
	default:
		*name = "unknown";
		*descr = "unknown drive type";
		break;

	case FDT_360K:
		*name = "360K";
		*descr = "5.25\" double-density";
		break;

	case FDT_12M:
		*name = "1.2M";
		*descr = "5.25\" high-density";
		break;

	case FDT_720K:
		*name = "720K";
		*descr = "3.5\" double-density";
		break;

	case FDT_144M:
		*name = "1.44M";
		*descr = "3.5\" high-density";
		break;

	case FDT_288M:
		*name = "2.88M";
		*descr = "3.5\" extra-density";
		break;
	}
}
