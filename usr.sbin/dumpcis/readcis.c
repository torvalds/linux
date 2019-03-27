/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995 Andrew McRae.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Code cleanup, bug-fix and extension
 * by Tatsumi Hosokawa <hosokawa@mt.cs.keio.ac.jp>
 */

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cardinfo.h"
#include "cis.h"
#include "readcis.h"

static int ck_linktarget(int, off_t, int);
static struct tuple_list *read_one_tuplelist(int, int, off_t);
static struct tuple_list *read_tuples(int);
static struct tuple *find_tuple_in_list(struct tuple_list *, unsigned char);
static struct tuple_info *get_tuple_info(unsigned char);

#define LENGTH_ANY 255

static struct tuple_info tuple_info[] = {
	{"Null tuple", CIS_NULL, 0},
	{"Common memory descriptor", CIS_MEM_COMMON, LENGTH_ANY},
	{"Long link to next chain for CardBus", CIS_LONGLINK_CB, LENGTH_ANY},
	{"Indirect access", CIS_INDIRECT, LENGTH_ANY},
	{"Configuration map for CardBus", CIS_CONF_MAP_CB, LENGTH_ANY},
	{"Configuration entry for CardBus", CIS_CONFIG_CB, LENGTH_ANY},
	{"Long link to next chain for MFC", CIS_LONGLINK_MFC, LENGTH_ANY},
	{"Base address register for CardBus", CIS_BAR, 6},
	{"Checksum", CIS_CHECKSUM, 5},
	{"Long link to attribute memory", CIS_LONGLINK_A, 4},
	{"Long link to common memory", CIS_LONGLINK_C, 4},
	{"Link target", CIS_LINKTARGET, 3},
	{"No link", CIS_NOLINK, 0},
	{"Version 1 info", CIS_INFO_V1, LENGTH_ANY},
	{"Alternate language string", CIS_ALTSTR, LENGTH_ANY},
	{"Attribute memory descriptor", CIS_MEM_ATTR, LENGTH_ANY},
	{"JEDEC descr for common memory", CIS_JEDEC_C, LENGTH_ANY},
	{"JEDEC descr for attribute memory", CIS_JEDEC_A, LENGTH_ANY},
	{"Configuration map", CIS_CONF_MAP, LENGTH_ANY},
	{"Configuration entry", CIS_CONFIG, LENGTH_ANY},
	{"Other conditions for common memory", CIS_DEVICE_OC, LENGTH_ANY},
	{"Other conditions for attribute memory", CIS_DEVICE_OA, LENGTH_ANY},
	{"Geometry info for common memory", CIS_DEVICEGEO, LENGTH_ANY},
	{"Geometry info for attribute memory", CIS_DEVICEGEO_A, LENGTH_ANY},
	{"Manufacturer ID", CIS_MANUF_ID, 4},
	{"Functional ID", CIS_FUNC_ID, 2},
	{"Functional EXT", CIS_FUNC_EXT, LENGTH_ANY},
	{"Software interleave", CIS_SW_INTERLV, 2},
	{"Version 2 Info", CIS_VERS_2, LENGTH_ANY},
	{"Data format", CIS_FORMAT, LENGTH_ANY},
	{"Geometry", CIS_GEOMETRY, 4},
	{"Byte order", CIS_BYTEORDER, 2},
	{"Card init date", CIS_DATE, 4},
	{"Battery replacement", CIS_BATTERY, 4},
	{"Organization", CIS_ORG, LENGTH_ANY},
	{"Terminator", CIS_END, 0},
	{0, 0, 0}
};

static void *
xmalloc(int sz)
{
	void   *p;

	sz = (sz + 7) & ~7;
	p = malloc(sz);
	if (p == NULL)
		errx(1, "malloc");
	bzero(p, sz);
	return (p);
}

/*
 *	After reading the tuples, decode the relevant ones.
 */
struct tuple_list *
readcis(int fd)
{

	return (read_tuples(fd));
}

/*
 *	free_cis - delete cis entry.
 */
void
freecis(struct tuple_list *tlist)
{
	struct tuple_list *tl;
	struct tuple *tp;

	while ((tl = tlist) != 0) {
		tlist = tl->next;
		while ((tp = tl->tuples) != 0) {
			tl->tuples = tp->next;
			free(tp->data);
			free(tp);
		}
		free(tl);
	}
}

/*
 *	Parse variable length value.
 */
u_int
parse_num(int sz, u_char *p, u_char **q, int ofs)
{
	u_int num = 0;

	switch (sz) {	
	case 0:
	case 0x10:
		break;
	case 1:
	case 0x11:
		num = (*p++) + ofs;
		break;
	case 2:
	case 0x12:
		num = tpl16(p) + ofs;
		p += 2;
		break;
	case 0x13:
		num = tpl24(p) + ofs;
		p += 3;
		break;
	case 3:
	case 0x14:
		num = tpl32(p) + ofs;
		p += 4;
		break;
	}
	if (q)
		*q = p;
	return num;
}

/*
 *	Read the tuples from the card.
 *	The processing of tuples is as follows:
 *		- Read tuples at attribute memory, offset 0.
 *		- If a CIS_END is the first tuple, look for
 *		  a tuple list at common memory offset 0; this list
 *		  must start with a LINKTARGET.
 *		- If a long link tuple was encountered, execute the long
 *		  link.
 *		- If a no-link tuple was seen, terminate processing.
 *		- If no no-link tuple exists, and no long link tuple
 *		  exists while processing the primary tuple list,
 *		  then look for a LINKTARGET tuple in common memory.
 *		- If a long link tuple is found in any list, then process
 *		  it. Only one link is allowed per list.
 */
static struct tuple_list *tlist;

static struct tuple_list *
read_tuples(int fd)
{
	struct tuple_list *tl = 0, *last_tl;
	struct tuple *tp;
	int     flag;
	off_t   offs;

	tlist = 0;
	last_tl = tlist = read_one_tuplelist(fd, MDF_ATTR, (off_t) 0);

	/* Now start processing the links (if any). */
	do {
		flag = MDF_ATTR;
		tp = find_tuple_in_list(last_tl, CIS_LONGLINK_A);
		if (tp == NULL) {
			flag = 0;
			tp = find_tuple_in_list(last_tl, CIS_LONGLINK_C);
		}

		if (tp == NULL || tp->length != 4)
			break;

		offs = (uint32_t)tpl32(tp->data);
#ifdef	DEBUG
		printf("Checking long link at %zd (%s memory)\n",
		    offs, flag ? "Attribute" : "Common");
#endif
		/*
		 * If a link was found, it looks sane read the tuple list from it.
		 */
		if (offs > 0 && offs < 32 * 1024 && ck_linktarget(fd, offs, flag)) {
			tl = read_one_tuplelist(fd, flag, offs);
			last_tl->next = tl;
			last_tl = tl;
		}
	} while (tl);

	/*
	 * If the primary list had no NOLINK tuple, and no LINKTARGET, then try
	 * to read a tuple list at common memory (offset 0).
	 */
	if (find_tuple_in_list(tlist, CIS_NOLINK) == 0 &&
	    find_tuple_in_list(tlist, CIS_LINKTARGET) == 0 &&
	    ck_linktarget(fd, (off_t) 0, 0)) {
#ifdef	DEBUG
		printf("Reading long link at 0 (Common memory)\n");
#endif
		tlist->next = read_one_tuplelist(fd, 0, 0);
	}
	return (tlist);
}

/*
 *	Read one tuple list from the card.
 */
static struct tuple_list *
read_one_tuplelist(int fd, int flags, off_t offs)
{
	struct tuple *tp, *last_tp = 0;
	struct tuple_list *tl;
	struct tuple_info *tinfo;
	int     total = 0;
	unsigned char code, length;

	/* Check to see if this memory has already been scanned. */
	for (tl = tlist; tl; tl = tl->next)
		if (tl->offs == offs && tl->flags == (flags & MDF_ATTR))
			return (0);
	tl = xmalloc(sizeof(*tl));
	tl->offs = offs;
	tl->flags = flags & MDF_ATTR;
	if (ioctl(fd, PIOCRWFLAG, &flags) < 0)
		err(1, "Setting flag to rad %s memory failed",
		    flags ? "attribute" : "common");
	if (lseek(fd, offs, SEEK_SET) < 0)
		err(1, "Unable to seek to memory offset %ju",
		    (uintmax_t)offs);
	do {
		if (read(fd, &code, 1) != 1)
			errx(1, "CIS code read");
		total++;
		if (code == CIS_NULL)
			continue;
		tp = xmalloc(sizeof(*tp));
		tp->code = code;
		if (code == CIS_END)
			length = 0;
		else {
			if (read(fd, &length, 1) != 1)
				errx(1, "CIS len read");
			total++;
		}
#ifdef	DEBUG
		printf("Tuple code = 0x%x, len = %d\n", code, length);
#endif

		/*
		 * A length of 255 is invalid, all others are valid. Treat a
		 * length of 255 as the end of the list. Some cards don't have a
		 * CIS_END at the end. These work on other systems because the
		 * end of the CIS eventually sees an area that's not decoded and
		 * read back as 0xff.
		 */
		if (length == 0xFF) {
			length = 0;
			code = CIS_END;
		}
		assert(length < 0xff);

		/*
		 * Check the tuple, and ignore it if it isn't in the table
		 * or the length is illegal.
		 */
		tinfo = get_tuple_info(code);
		if (tinfo == NULL || (tinfo->length != LENGTH_ANY && tinfo->length > length)) {
			printf("code %s (%d) ignored\n", tuple_name(code), code);
			continue;
		}
		tp->length = length;
		if (length != 0) {
			total += length;
			tp->data = xmalloc(length);
			if (read(fd, tp->data, length) != length)
				errx(1, "Can't read CIS data");
		}

		if (last_tp != NULL)
			last_tp->next = tp;
		if (tl->tuples == NULL) {
			tl->tuples = tp;
			tp->next = NULL;
		}
		last_tp = tp;
	} while (code != CIS_END && total < 1024);
	return (tl);
}

/*
 *	return true if the offset points to a LINKTARGET tuple.
 */
static int
ck_linktarget(int fd, off_t offs, int flag)
{
	char    blk[5];

	if (ioctl(fd, PIOCRWFLAG, &flag) < 0)
		err(1, "Setting flag to rad %s memory failed",
		    flag ? "attribute" : "common");
	if (lseek(fd, offs, SEEK_SET) < 0)
		err(1, "Unable to seek to memory offset %ju",
		    (uintmax_t)offs);
	if (read(fd, blk, 5) != 5)
		return (0);
	if (blk[0] == CIS_LINKTARGET &&
	    blk[1] == 0x3 &&
	    blk[2] == 'C' &&
	    blk[3] == 'I' &&
	    blk[4] == 'S')
		return (1);
	return (0);
}

/*
 *	find_tuple_in_list - find a tuple within a
 *	single tuple list.
 */
static struct tuple *
find_tuple_in_list(struct tuple_list *tl, unsigned char code)
{
	struct tuple *tp;

	for (tp = tl->tuples; tp; tp = tp->next)
		if (tp->code == code)
			break;
	return (tp);
}

/*
 *	return table entry for code.
 */
static struct tuple_info *
get_tuple_info(unsigned char code)
{
	struct tuple_info *tp;

	for (tp = tuple_info; tp->name; tp++)
		if (tp->code == code)
			return (tp);
	return (0);
}

const char *
tuple_name(unsigned char code)
{
	struct tuple_info *tp;

	tp = get_tuple_info(code);
	if (tp)
		return (tp->name);
	return ("Unknown");
}
