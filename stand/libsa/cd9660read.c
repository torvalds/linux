/*
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Originally derived from libsa/cd9660.c: */
/*	$NetBSD: cd9660.c,v 1.5 1997/06/26 19:11:33 drochner Exp $	*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <fs/cd9660/iso.h>
#include <fs/cd9660/cd9660_rrip.h>

static uint64_t cd9660_lookup(const char *);
static ssize_t cd9660_fsread(uint64_t, void *, size_t);

#define	SUSP_CONTINUATION	"CE"
#define	SUSP_PRESENT		"SP"
#define	SUSP_STOP		"ST"
#define	SUSP_EXTREF		"ER"
#define	RRIP_NAME		"NM"

typedef struct {
	ISO_SUSP_HEADER		h;
	u_char signature	[ISODCL (  5,    6)];
	u_char len_skp		[ISODCL (  7,    7)]; /* 711 */
} ISO_SUSP_PRESENT;

static int
read_iso_block(void *buffer, daddr_t blkno)
{

	return (drvread(&dsk, buffer, blkno * 4, 4));
}

static ISO_SUSP_HEADER *
susp_lookup_record(const char *identifier, struct iso_directory_record *dp,
    int lenskip)
{
	static char susp_buffer[ISO_DEFAULT_BLOCK_SIZE];
	ISO_SUSP_HEADER *sh;
	ISO_RRIP_CONT *shc;
	char *p, *end;
	int error;

	p = dp->name + isonum_711(dp->name_len) + lenskip;
	/* Names of even length have a padding byte after the name. */
	if ((isonum_711(dp->name_len) & 1) == 0)
		p++;
	end = (char *)dp + isonum_711(dp->length);
	while (p + 3 < end) {
		sh = (ISO_SUSP_HEADER *)p;
		if (bcmp(sh->type, identifier, 2) == 0)
			return (sh);
		if (bcmp(sh->type, SUSP_STOP, 2) == 0)
			return (NULL);
		if (bcmp(sh->type, SUSP_CONTINUATION, 2) == 0) {
			shc = (ISO_RRIP_CONT *)sh;
			error = read_iso_block(susp_buffer,
			    isonum_733(shc->location));

			/* Bail if it fails. */
			if (error != 0)
				return (NULL);
			p = susp_buffer + isonum_733(shc->offset);
			end = p + isonum_733(shc->length);
		} else {
			/* Ignore this record and skip to the next. */
			p += isonum_711(sh->length);

			/* Avoid infinite loops with corrupted file systems */
			if (isonum_711(sh->length) == 0)
				return (NULL);
		}
	}
	return (NULL);
}

static const char *
rrip_lookup_name(struct iso_directory_record *dp, int lenskip, size_t *len)
{
	ISO_RRIP_ALTNAME *p;

	if (len == NULL)
		return (NULL);

	p = (ISO_RRIP_ALTNAME *)susp_lookup_record(RRIP_NAME, dp, lenskip);
	if (p == NULL)
		return (NULL);
	switch (*p->flags) {
	case ISO_SUSP_CFLAG_CURRENT:
		*len = 1;
		return (".");
	case ISO_SUSP_CFLAG_PARENT:
		*len = 2;
		return ("..");
	case 0:
		*len = isonum_711(p->h.length) - 5;
		return ((char *)p + 5);
	default:
		/*
		 * We don't handle hostnames or continued names as they are
		 * too hard, so just bail and use the default name.
		 */
		return (NULL);
	}
}

static int
rrip_check(struct iso_directory_record *dp, int *lenskip)
{
	ISO_SUSP_PRESENT *sp;
	ISO_RRIP_EXTREF *er;
	char *p;

	/* First, see if we can find a SP field. */
	p = dp->name + isonum_711(dp->name_len);
	if (p > (char *)dp + isonum_711(dp->length)) {
		return (0);
	}
	sp = (ISO_SUSP_PRESENT *)p;
	if (bcmp(sp->h.type, SUSP_PRESENT, 2) != 0) {
		return (0);
	}
	if (isonum_711(sp->h.length) != sizeof(ISO_SUSP_PRESENT)) {
		return (0);
	}
	if (sp->signature[0] != 0xbe || sp->signature[1] != 0xef) {
		return (0);
	}
	*lenskip = isonum_711(sp->len_skp);

	/*
	 * Now look for an ER field.  If RRIP is present, then there must
	 * be at least one of these.  It would be more pedantic to walk
	 * through the list of fields looking for a Rock Ridge ER field.
	 */
	er = (ISO_RRIP_EXTREF *)susp_lookup_record(SUSP_EXTREF, dp, 0);
	if (er == NULL) {
		return (0);
	}
	return (1);
}

static int
dirmatch(const char *path, struct iso_directory_record *dp, int use_rrip,
    int lenskip)
{
	size_t len;
	const char *cp = NULL;
	int i, icase;

	if (use_rrip)
		cp = rrip_lookup_name(dp, lenskip, &len);
	else
		cp = NULL;
	if (cp == NULL) {
		len = isonum_711(dp->name_len);
		cp = dp->name;
		icase = 1;
	} else
		icase = 0;
	for (i = len; --i >= 0; path++, cp++) {
		if (!*path || *path == '/')
			break;
		if (*path == *cp)
			continue;
		if (!icase && toupper(*path) == *cp)
			continue;
		return 0;
	}
	if (*path && *path != '/') {
		return 0;
	}
	/*
	 * Allow stripping of trailing dots and the version number.
	 * Note that this will find the first instead of the last version
	 * of a file.
	 */
	if (i >= 0 && (*cp == ';' || *cp == '.')) {
		/* This is to prevent matching of numeric extensions */
		if (*cp == '.' && cp[1] != ';') {
			return 0;
		}
		while (--i >= 0)
			if (*++cp != ';' && (*cp < '0' || *cp > '9')) {
				return 0;
			}
	}
	return 1;
}

static uint64_t
cd9660_lookup(const char *path)
{
	static char blkbuf[ISO_DEFAULT_BLOCK_SIZE];
	struct iso_primary_descriptor *vd;
	struct iso_directory_record rec;
	struct iso_directory_record *dp = NULL;
	size_t dsize, off;
	daddr_t bno, boff;
	int rc, first, use_rrip, lenskip;
	uint64_t cookie;

	for (bno = 16;; bno++) {
		rc = read_iso_block(blkbuf, bno);
		vd = (struct iso_primary_descriptor *)blkbuf;

		if (bcmp(vd->id, ISO_STANDARD_ID, sizeof vd->id) != 0)
			return (0);
		if (isonum_711(vd->type) == ISO_VD_END)
			return (0);
		if (isonum_711(vd->type) == ISO_VD_PRIMARY)
			break;
	}

	bcopy(vd->root_directory_record, &rec, sizeof(rec));
	if (*path == '/') path++; /* eat leading '/' */

	first = 1;
	use_rrip = 0;
	lenskip = 0;
	while (*path) {
		bno = isonum_733(rec.extent) + isonum_711(rec.ext_attr_length);
		dsize = isonum_733(rec.size);
		off = 0;
		boff = 0;

		while (off < dsize) {
			if ((off % ISO_DEFAULT_BLOCK_SIZE) == 0) {
				rc = read_iso_block(blkbuf, bno + boff);
				if (rc) {
					return (0);
				}
				boff++;
				dp = (struct iso_directory_record *) blkbuf;
			}
			if (isonum_711(dp->length) == 0) {
				/* skip to next block, if any */
				off = boff * ISO_DEFAULT_BLOCK_SIZE;
				continue;
			}

			/* See if RRIP is in use. */
			if (first)
				use_rrip = rrip_check(dp, &lenskip);

			if (dirmatch(path, dp, use_rrip,
			    first ? 0 : lenskip)) {
				first = 0;
				break;
			} else
				first = 0;

			dp = (struct iso_directory_record *)
			    ((char *) dp + isonum_711(dp->length));
			/* If the new block has zero length, it is padding. */
			if (isonum_711(dp->length) == 0) {
				/* Skip to next block, if any. */
				off = boff * ISO_DEFAULT_BLOCK_SIZE;
				continue;
			}
			off += isonum_711(dp->length);
		}
		if (off >= dsize) {
			return (0);
		}

		rec = *dp;
		while (*path && *path != '/') /* look for next component */
			path++;
		if (*path) path++; /* skip '/' */
	}

	if ((isonum_711(rec.flags) & 2) != 0) {
		return (0);
	}

	cookie = isonum_733(rec.extent) + isonum_711(rec.ext_attr_length);
	cookie = (cookie << 32) | isonum_733(rec.size);

	return (cookie);
}

static ssize_t
cd9660_fsread(uint64_t cookie, void *buf, size_t nbytes)
{
	static char blkbuf[ISO_DEFAULT_BLOCK_SIZE];
	static daddr_t curstart = 0, curblk = 0;
	daddr_t blk, blk_off;
	off_t byte_off;
	size_t size, remaining, n;
	char *s;

	size = cookie & 0xffffffff;
	blk = (cookie >> 32) & 0xffffffff;

	/* Make sure we're looking at the right file. */
	if (((blk << 32) | size) != cookie) {
		return (-1);
	}

	if (blk != curstart) {
		curstart = blk;
		fs_off = 0;
	}

	size -= fs_off;
	if (size < nbytes) {
		nbytes = size;
	}
	remaining = nbytes;
	s = buf;

	while (remaining > 0) {
		blk_off = fs_off >> ISO_DEFAULT_BLOCK_SHIFT;
		byte_off = fs_off & (ISO_DEFAULT_BLOCK_SIZE - 1);

		if (curblk != curstart + blk_off) {
			curblk = curstart + blk_off;
			read_iso_block(blkbuf, curblk);
		}

		if (remaining < ISO_DEFAULT_BLOCK_SIZE - byte_off) {
			n = remaining;
		} else {
			n = ISO_DEFAULT_BLOCK_SIZE - byte_off;
		}
		memcpy(s, blkbuf + byte_off, n);
		remaining -= n;
		s += n;

		fs_off += n;
	}

	return (nbytes);
}
