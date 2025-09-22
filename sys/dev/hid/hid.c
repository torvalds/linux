/*	$OpenBSD: hid.c,v 1.8 2025/07/21 21:46:40 bru Exp $ */
/*	$NetBSD: hid.c,v 1.23 2002/07/11 21:14:25 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/hid.c,v 1.11 1999/11/17 22:33:39 n_hibma Exp $ */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <dev/hid/hid.h>

#ifdef USBHID_DEBUG
#define DPRINTF(x...)	 do { printf(x); } while (0)
#else
#define DPRINTF(x...)
#endif

#define	MAXUSAGE 64
#define	MAXPUSH 4
#define	MAXID 16

struct hid_pos_data {
	int32_t rid;
	uint32_t pos;
};

struct hid_data {
	const uint8_t *start;
	const uint8_t *end;
	const uint8_t *p;
	struct hid_item cur[MAXPUSH];
	struct hid_pos_data last_pos[MAXID];
	int32_t	usages_min[MAXUSAGE];
	int32_t	usages_max[MAXUSAGE];
	int32_t usage_last;	/* last seen usage */
	uint32_t loc_size;	/* last seen size */
	uint32_t loc_count;	/* last seen count */
	enum hid_kind kind;
	uint8_t	pushlevel;	/* current pushlevel */
	uint8_t	ncount;		/* end usage item count */
	uint8_t icount;		/* current usage item count */
	uint8_t	nusage;		/* end "usages_min/max" index */
	uint8_t	iusage;		/* current "usages_min/max" index */
	uint8_t ousage;		/* current "usages_min/max" offset */
	uint8_t	susage;		/* usage set flags */
};

static void
hid_clear_local(struct hid_item *c)
{
	c->loc.count = 0;
	c->loc.size = 0;
	c->usage = 0;
	c->usage_minimum = 0;
	c->usage_maximum = 0;
	c->designator_index = 0;
	c->designator_minimum = 0;
	c->designator_maximum = 0;
	c->string_index = 0;
	c->string_minimum = 0;
	c->string_maximum = 0;
	c->set_delimiter = 0;
}

static void
hid_switch_rid(struct hid_data *s, struct hid_item *c, int32_t nextid)
{
	uint8_t i;

	if (c->report_ID == nextid)
		return;

	/* save current position for current rID */
	if (c->report_ID == 0) {
		i = 0;
	} else {
		for (i = 1; i != MAXID; i++) {
			if (s->last_pos[i].rid == c->report_ID)
				break;
			if (s->last_pos[i].rid == 0)
				break;
		}
	}
	if (i != MAXID) {
		s->last_pos[i].rid = c->report_ID;
		s->last_pos[i].pos = c->loc.pos;
	}

	/* store next report ID */
	c->report_ID = nextid;

	/* lookup last position for next rID */
	if (nextid == 0) {
		i = 0;
	} else {
		for (i = 1; i != MAXID; i++) {
			if (s->last_pos[i].rid == nextid)
				break;
			if (s->last_pos[i].rid == 0)
				break;
		}
	}
	if (i != MAXID) {
		s->last_pos[i].rid = nextid;
		c->loc.pos = s->last_pos[i].pos;
	} else {
		DPRINTF("Out of RID entries, position is set to zero!\n");
		c->loc.pos = 0;
	}
}

struct hid_data *
hid_start_parse(const void *d, int len, enum hid_kind kind)
{
	struct hid_data *s;

	s = malloc(sizeof(*s), M_TEMP, M_WAITOK | M_ZERO);

	s->start = s->p = d;
	s->end = ((const uint8_t *)d) + len;
	s->kind = kind;
	return (s);
}

void
hid_end_parse(struct hid_data *s)
{
	if (s == NULL)
		return;

	free(s, M_TEMP, 0);
}

static uint8_t
hid_get_byte(struct hid_data *s, const uint16_t wSize)
{
	const uint8_t *ptr;
	uint8_t retval;

	ptr = s->p;

	/* check if end is reached */
	if (ptr == s->end)
		return (0);

	/* read out a byte */
	retval = *ptr;

	/* check if data pointer can be advanced by "wSize" bytes */
	if ((s->end - ptr) < wSize)
		ptr = s->end;
	else
		ptr += wSize;

	/* update pointer */
	s->p = ptr;

	return (retval);
}

int
hid_get_item(struct hid_data *s, struct hid_item *h)
{
	struct hid_item *c;
	unsigned int bTag, bType, bSize;
	uint32_t oldpos;
	int32_t mask;
	int32_t dval;

	if (s == NULL)
		return (0);

	if (s->pushlevel >= MAXPUSH)
		return (0);

	c = &s->cur[s->pushlevel];

 top:
	/* check if there is an array of items */
	DPRINTF("%s: icount=%d ncount=%d\n", __func__,
	    s->icount, s->ncount);
	if (s->icount < s->ncount) {
		/* get current usage */
		if (s->iusage < s->nusage) {
			dval = s->usages_min[s->iusage] + s->ousage;
			c->usage = dval;
			s->usage_last = dval;
			if (dval == s->usages_max[s->iusage]) {
				s->iusage ++;
				s->ousage = 0;
			} else {
				s->ousage ++;
			}
		} else {
			DPRINTF("Using last usage\n");
			dval = s->usage_last;
		}
		s->icount ++;
		/*
		 * Only copy HID item, increment position and return
		 * if correct kind!
		 */
		if (s->kind == hid_all || s->kind == c->kind) {
			*h = *c;
			DPRINTF("%u,%u,%u\n", h->loc.pos,
			    h->loc.size, h->loc.count);
			c->loc.pos += c->loc.size * c->loc.count;
			return (1);
		}
	}

	/* reset state variables */
	s->icount = 0;
	s->ncount = 0;
	s->iusage = 0;
	s->nusage = 0;
	s->susage = 0;
	s->ousage = 0;
	hid_clear_local(c);

	/* get next item */
	while (s->p != s->end) {

		bSize = hid_get_byte(s, 1);
		if (bSize == 0xfe) {
			/* long item */
			bSize = hid_get_byte(s, 1);
			bSize |= hid_get_byte(s, 1) << 8;
			bTag = hid_get_byte(s, 1);
			bType = 0xff;	/* XXX what should it be */
		} else {
			/* short item */
			bTag = bSize >> 4;
			bType = (bSize >> 2) & 3;
			bSize &= 3;
			if (bSize == 3)
				bSize = 4;
		}
		switch (bSize) {
		case 0:
			dval = 0;
			mask = 0;
			break;
		case 1:
			dval = hid_get_byte(s, 1);
			mask = 0xFF;
			break;
		case 2:
			dval = hid_get_byte(s, 1);
			dval |= hid_get_byte(s, 1) << 8;
			mask = 0xFFFF;
			break;
		case 4:
			dval = hid_get_byte(s, 1);
			dval |= hid_get_byte(s, 1) << 8;
			dval |= hid_get_byte(s, 1) << 16;
			dval |= hid_get_byte(s, 1) << 24;
			mask = 0xFFFFFFFF;
			break;
		default:
			dval = hid_get_byte(s, bSize);
			DPRINTF("bad length %u (data=0x%02x)\n",
			    bSize, dval);
			continue;
		}

		DPRINTF("%s: bType=%d bTag=%d dval=%d\n", __func__,
		    bType, bTag, dval);
		switch (bType) {
		case 0:		/* Main */
			switch (bTag) {
			case 8:	/* Input */
				c->kind = hid_input;
				c->flags = dval;
		ret:
				c->loc.count = s->loc_count;
				c->loc.size = s->loc_size;

				if (c->flags & HIO_VARIABLE) {
					/* range check usage count */
					if (c->loc.count > 255) {
						DPRINTF("Number of "
						    "items truncated to 255\n");
						s->ncount = 255;
					} else
						s->ncount = c->loc.count;

					/*
					 * The "top" loop will return
					 * one and one item:
					 */
					c->loc.count = 1;
				} else {
					s->ncount = 1;
				}
				goto top;

			case 9:	/* Output */
				c->kind = hid_output;
				c->flags = dval;
				goto ret;
			case 10:	/* Collection */
				c->kind = hid_collection;
				c->collection = dval;
				c->collevel++;
				c->usage = s->usage_last;
				*h = *c;
				return (1);
			case 11:	/* Feature */
				c->kind = hid_feature;
				c->flags = dval;
				goto ret;
			case 12:	/* End collection */
				c->kind = hid_endcollection;
				if (c->collevel == 0) {
					DPRINTF("invalid end collection\n");
					return (0);
				}
				c->collevel--;
				*h = *c;
				return (1);
			default:
				DPRINTF("Main bTag=%d\n", bTag);
				break;
			}
			break;
		case 1:		/* Global */
			switch (bTag) {
			case 0:
				c->_usage_page = dval << 16;
				break;
			case 1:
				c->logical_minimum = dval;
				break;
			case 2:
				c->logical_maximum = dval;
				break;
			case 3:
				c->physical_minimum = dval;
				break;
			case 4:
				c->physical_maximum = dval;
				break;
			case 5:
				c->unit_exponent = dval;
				break;
			case 6:
				c->unit = dval;
				break;
			case 7:
				/* mask because value is unsigned */
				s->loc_size = dval & mask;
				break;
			case 8:
				hid_switch_rid(s, c, dval & mask);
				break;
			case 9:
				/* mask because value is unsigned */
				s->loc_count = dval & mask;
				break;
			case 10:	/* Push */
				if (s->pushlevel < MAXPUSH - 1) {
					s->pushlevel++;
					s->cur[s->pushlevel] = *c;
					/* store size and count */
					c->loc.size = s->loc_size;
					c->loc.count = s->loc_count;
					/* update current item pointer */
					c = &s->cur[s->pushlevel];
				} else {
					DPRINTF("Cannot push "
					    "item @ %d\n", s->pushlevel);
				}
				break;
			case 11:	/* Pop */
				if (s->pushlevel > 0) {
					s->pushlevel--;
					/* preserve position */
					oldpos = c->loc.pos;
					c = &s->cur[s->pushlevel];
					/* restore size and count */
					s->loc_size = c->loc.size;
					s->loc_count = c->loc.count;
					/* set default item location */
					c->loc.pos = oldpos;
					c->loc.size = 0;
					c->loc.count = 0;
				} else {
					DPRINTF("Cannot pop "
					    "item @ %d\n", s->pushlevel);
				}
				break;
			default:
				DPRINTF("Global bTag=%d\n", bTag);
				break;
			}
			break;
		case 2:		/* Local */
			switch (bTag) {
			case 0:
				if (bSize != 4)
					dval = (dval & mask) | c->_usage_page;

				/* set last usage, in case of a collection */
				s->usage_last = dval;

				if (s->nusage < MAXUSAGE) {
					s->usages_min[s->nusage] = dval;
					s->usages_max[s->nusage] = dval;
					s->nusage ++;
				} else {
					DPRINTF("max usage reached\n");
				}

				/* clear any pending usage sets */
				s->susage = 0;
				break;
			case 1:
				s->susage |= 1;

				if (bSize != 4)
					dval = (dval & mask) | c->_usage_page;
				c->usage_minimum = dval;

				goto check_set;
			case 2:
				s->susage |= 2;

				if (bSize != 4)
					dval = (dval & mask) | c->_usage_page;
				c->usage_maximum = dval;

			check_set:
				if (s->susage != 3)
					break;

				/* sanity check */
				if ((s->nusage < MAXUSAGE) &&
				    (c->usage_minimum <= c->usage_maximum)) {
					/* add usage range */
					s->usages_min[s->nusage] = 
					    c->usage_minimum;
					s->usages_max[s->nusage] = 
					    c->usage_maximum;
					s->nusage ++;
				} else {
					DPRINTF("Usage set dropped\n");
				}
				s->susage = 0;
				break;
			case 3:
				c->designator_index = dval;
				break;
			case 4:
				c->designator_minimum = dval;
				break;
			case 5:
				c->designator_maximum = dval;
				break;
			case 7:
				c->string_index = dval;
				break;
			case 8:
				c->string_minimum = dval;
				break;
			case 9:
				c->string_maximum = dval;
				break;
			case 10:
				c->set_delimiter = dval;
				break;
			default:
				DPRINTF("Local bTag=%d\n", bTag);
				break;
			}
			break;
		default:
			DPRINTF("default bType=%d\n", bType);
			break;
		}
	}
	return (0);
}

int
hid_report_size(const void *buf, int len, enum hid_kind k, u_int8_t id)
{
	struct hid_data *d;
	struct hid_item h;
	int lo, hi;

	h.report_ID = 0;
	lo = hi = -1;
	DPRINTF("hid_report_size: kind=%d id=%d\n", k, id);
	for (d = hid_start_parse(buf, len, k); hid_get_item(d, &h); ) {
		DPRINTF("hid_report_size: item kind=%d id=%d pos=%d "
			  "size=%d count=%d\n",
			  h.kind, h.report_ID, h.loc.pos, h.loc.size,
			  h.loc.count);
		if (h.report_ID == id && h.kind == k) {
			if (lo < 0) {
				lo = h.loc.pos;
#ifdef DIAGNOSTIC
				if (lo != 0) {
					printf("hid_report_size: lo != 0\n");
				}
#endif
			}
			hi = h.loc.pos + h.loc.size * h.loc.count;
			DPRINTF("hid_report_size: lo=%d hi=%d\n", lo, hi);

		}
	}
	hid_end_parse(d);
	return ((hi - lo + 7) / 8);
}

int
hid_locate(const void *desc, int size, int32_t u, uint8_t id, enum hid_kind k,
    struct hid_location *loc, uint32_t *flags)
{
	struct hid_data *d;
	struct hid_item h;

	h.report_ID = 0;
	DPRINTF("hid_locate: enter usage=0x%x kind=%d id=%d\n", u, k, id);
	for (d = hid_start_parse(desc, size, k); hid_get_item(d, &h); ) {
		DPRINTF("hid_locate: usage=0x%x kind=%d id=%d flags=0x%x\n",
			    h.usage, h.kind, h.report_ID, h.flags);
		if (h.kind == k && !(h.flags & HIO_CONST) &&
		    h.usage == u && h.report_ID == id) {
			if (loc != NULL)
				*loc = h.loc;
			if (flags != NULL)
				*flags = h.flags;
			hid_end_parse(d);
			return (1);
		}
	}
	hid_end_parse(d);
	if (loc != NULL)
		loc->size = 0;
	if (flags != NULL)
		*flags = 0;
	return (0);
}

uint32_t
hid_get_data_sub(const uint8_t *buf, int len, struct hid_location *loc,
    int is_signed)
{
	uint32_t hpos = loc->pos;
	uint32_t hsize = loc->size;
	uint32_t data;
	uint32_t rpos;
	uint8_t n;

	DPRINTF("hid_get_data_sub: loc %d/%d\n", hpos, hsize);

	/* Range check and limit */
	if (hsize == 0)
		return (0);
	if (hsize > 32)
		hsize = 32;

	/* Get data in a safe way */
	data = 0;
	rpos = (hpos / 8);
	n = (hsize + 7) / 8;
	rpos += n;
	while (n--) {
		rpos--;
		if (rpos < len)
			data |= buf[rpos] << (8 * n);
	}

	/* Correctly shift down data */
	data = (data >> (hpos % 8));
	n = 32 - hsize;

	/* Mask and sign extend in one */
	if (is_signed != 0)
		data = (int32_t)((int32_t)data << n) >> n;
	else
		data = (uint32_t)((uint32_t)data << n) >> n;

	DPRINTF("hid_get_data_sub: loc %d/%d = %lu\n",
	    loc->pos, loc->size, (long)data);
	return (data);
}

int32_t
hid_get_data(const uint8_t *buf, int len, struct hid_location *loc)
{
	return (hid_get_data_sub(buf, len, loc, 1));
}

uint32_t
hid_get_udata(const uint8_t *buf, int len, struct hid_location *loc)
{
        return (hid_get_data_sub(buf, len, loc, 0));
}

int
hid_is_collection(const void *desc, int size, uint8_t id, int32_t usage)
{
	struct hid_data *hd;
	struct hid_item hi;
	uint32_t coll_usage = ~0;

	hd = hid_start_parse(desc, size, hid_all);

	DPRINTF("%s: id=%d usage=0x%x\n", __func__, id, usage);
	while (hid_get_item(hd, &hi)) {
		DPRINTF("%s: kind=%d id=%d usage=0x%x(0x%x)\n", __func__,
			    hi.kind, hi.report_ID, hi.usage, coll_usage);
		if (hi.kind == hid_collection &&
		    hi.collection == HCOLL_APPLICATION)
			coll_usage = hi.usage;
		if (hi.kind == hid_endcollection &&
		    coll_usage == usage && hi.report_ID == id) {
			DPRINTF("%s: found\n", __func__);
			hid_end_parse(hd);
			return (1);
		}
	}
	DPRINTF("%s: not found\n", __func__);
	hid_end_parse(hd);
	return (0);
}

struct hid_data *
hid_get_collection_data(const void *desc, int size, int32_t usage,
    uint32_t collection)
{
	struct hid_data *hd;
	struct hid_item hi;

	hd = hid_start_parse(desc, size, hid_all);

	DPRINTF("%s: usage=0x%x\n", __func__, usage);
	while (hid_get_item(hd, &hi)) {
		DPRINTF("%s: kind=%d id=%d usage=0x%x(0x%x)\n", __func__,
		    hi.kind, hi.report_ID, hi.usage, usage);
		if (hi.kind == hid_collection &&
		    hi.collection == collection && hi.usage == usage) {
			DPRINTF("%s: found\n", __func__);
			return hd;
		}
	}
	DPRINTF("%s: not found\n", __func__);
	hid_end_parse(hd);
	return NULL;
}

int
hid_get_id_of_collection(const void *desc, int size, int32_t usage,
    uint32_t collection)
{
	struct hid_data *hd;
	struct hid_item hi;

	hd = hid_start_parse(desc, size, hid_all);

	DPRINTF("%s: usage=0x%x\n", __func__, usage);
	while (hid_get_item(hd, &hi)) {
		DPRINTF("%s: kind=%d id=%d usage=0x%x(0x%x)\n", __func__,
		    hi.kind, hi.report_ID, hi.usage, usage);
		if (hi.kind == hid_collection &&
		    hi.collection == collection && hi.usage == usage) {
			DPRINTF("%s: found\n", __func__);
			hid_end_parse(hd);
			return hi.report_ID;
		}
	}
	DPRINTF("%s: not found\n", __func__);
	hid_end_parse(hd);
	return -1;
}

/*
 * Find the first report that contains each of the given "usages" and
 * belongs to an application collection with the 'app_usage' type.
 * The size of the 'usages' array must be in the range [1..32].
 *
 * If 'coll_usages' is NULL, the search will skip collections with
 * usages from vendor pages (0xFF00 - 0xFFFF).
 *
 * If 'coll_usages' is non-NULL, it must point to a 0-terminated
 * sequence of collection usages, and the search will skip collections
 * with usages not present in this set. (It isn't necessary to include
 * the usage of the application collection here.)
 *
 * Return Values:
 *     -1:		No match
 *     0:		Success (single report without an ID)
 *     [1..255]:	Report ID
 */
int
hid_find_report(const void *desc, int len, enum hid_kind kind,
    int32_t app_usage, int n_usages, int32_t *usages, int32_t *coll_usages)
{
	struct hid_data *hd;
	struct hid_item h;
	uint32_t matches;
	int i, cur_id, skip;

	hd = hid_start_parse(desc, len, hid_all);
	for (cur_id = -1, skip = 0; hid_get_item(hd, &h); ) {
		if (cur_id != h.report_ID) {
			matches = 0;
			cur_id = h.report_ID;
		}
		if (h.kind == hid_collection) {
			if (skip)
				continue;
			if (h.collevel == 1) {
				if (h.usage != app_usage)
					skip = 1;
			} else if (coll_usages != NULL) {
				for (i = 0; coll_usages[i] != h.usage; i++)
					if (coll_usages[i] == 0) {
						skip = h.collevel;
						break;
					}
			} else if (((h.usage >> 16) & 0xffff) >= 0xff00) {
				skip = h.collevel;
			}
		} else if (h.kind == hid_endcollection) {
			if (h.collevel < skip)
				skip = 0;
		}
		if (h.kind != kind || skip)
			continue;
		for (i = 0; i < n_usages; i++)
			if (h.usage == usages[i] && !(matches & (1 << i))) {
				matches |= (1 << i);
				if (matches != (1 << n_usages) - 1)
					break;
				hid_end_parse(hd);
				return (h.report_ID);
			}
	}
	hid_end_parse(hd);
	return (-1);
}
