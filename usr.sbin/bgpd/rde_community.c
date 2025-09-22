/*	$OpenBSD: rde_community.c,v 1.16 2024/09/10 08:53:20 claudio Exp $ */

/*
 * Copyright (c) 2019 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/queue.h>

#include <endian.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "bgpd.h"
#include "rde.h"
#include "log.h"

static int
apply_flag(uint32_t in, uint8_t flag, struct rde_peer *peer, uint32_t *out,
    uint32_t *mask)
{
	switch (flag) {
	case COMMUNITY_ANY:
		if (mask == NULL)
			return -1;
		*out = 0;
		*mask = 0;
		return 0;
	case COMMUNITY_NEIGHBOR_AS:
		if (peer == NULL)
			return -1;
		*out = peer->conf.remote_as;
		break;
	case COMMUNITY_LOCAL_AS:
		if (peer == NULL)
			return -1;
		*out = peer->conf.local_as;
		break;
	default:
		*out = in;
		break;
	}
	if (mask)
		*mask = UINT32_MAX;
	return 0;
}

static int
fc2c(struct community *fc, struct rde_peer *peer, struct community *c,
    struct community *m)
{
	int type;
	uint8_t subtype;

	memset(c, 0, sizeof(*c));
	if (m)
		memset(m, 0xff, sizeof(*m));

	c->flags = (uint8_t)fc->flags;

	switch ((uint8_t)c->flags) {
	case COMMUNITY_TYPE_BASIC:
		if (apply_flag(fc->data1, fc->flags >> 8, peer,
		    &c->data1, m ? &m->data1 : NULL))
			return -1;
		if (apply_flag(fc->data2, fc->flags >> 16, peer,
		    &c->data2, m ? &m->data2 : NULL))
			return -1;

		/* check that values fit */
		if (c->data1 > USHRT_MAX || c->data2 > USHRT_MAX)
			return -1;
		return 0;
	case COMMUNITY_TYPE_LARGE:
		if (apply_flag(fc->data1, fc->flags >> 8, peer,
		    &c->data1, m ? &m->data1 : NULL))
			return -1;
		if (apply_flag(fc->data2, fc->flags >> 16, peer,
		    &c->data2, m ? &m->data2 : NULL))
			return -1;
		if (apply_flag(fc->data3, fc->flags >> 24, peer,
		    &c->data3, m ? &m->data3 : NULL))
			return -1;
		return 0;
	case COMMUNITY_TYPE_EXT:
		type = (int32_t)fc->data3 >> 8;
		subtype = fc->data3 & 0xff;

		if ((fc->flags >> 24 & 0xff) == COMMUNITY_ANY) {
			/* special case for 'ext-community * *' */
			if (m == NULL)
				return -1;
			m->data1 = 0;
			m->data2 = 0;
			m->data3 = 0;
			return 0;
		}

		if (type == -1) {
			/* special case for 'ext-community rt *' */
			if ((fc->flags >> 8 & 0xff) != COMMUNITY_ANY ||
			    m == NULL)
				return -1;
			c->data3 = subtype;
			m->data1 = 0;
			m->data2 = 0;
			m->data3 = 0xff;
			return 0;
		}

		c->data3 = type << 8 | subtype;
		switch (type & EXT_COMMUNITY_VALUE) {
		case EXT_COMMUNITY_TRANS_TWO_AS:
		case EXT_COMMUNITY_TRANS_FOUR_AS:
			if ((fc->flags >> 8 & 0xff) == COMMUNITY_ANY)
				break;

			if (apply_flag(fc->data1, fc->flags >> 8, peer,
			    &c->data1, m ? &m->data1 : NULL))
				return -1;
			if (apply_flag(fc->data2, fc->flags >> 16, peer,
			    &c->data2, m ? &m->data2 : NULL))
				return -1;
			if (m)
				m->data3 &= ~(EXT_COMMUNITY_TRANS_FOUR_AS << 8);
			return 0;
		case EXT_COMMUNITY_TRANS_IPV4:
			if ((fc->flags >> 8 & 0xff) == COMMUNITY_ANY)
				break;

			if (apply_flag(fc->data1, fc->flags >> 8, peer,
			    &c->data1, m ? &m->data1 : NULL))
				return -1;
			if (apply_flag(fc->data2, fc->flags >> 16, peer,
			    &c->data2, m ? &m->data2 : NULL))
				return -1;
			/* check that values fit */
			if (c->data2 > USHRT_MAX)
				return -1;
			return 0;
		case EXT_COMMUNITY_TRANS_OPAQUE:
		case EXT_COMMUNITY_TRANS_EVPN:
			if ((fc->flags >> 8 & 0xff) == COMMUNITY_ANY)
				break;

			c->data1 = fc->data1;
			c->data2 = fc->data2;
			return 0;
		}

		/* this is for 'ext-community subtype *' */
		if (m == NULL)
			return -1;
		m->data1 = 0;
		m->data2 = 0;
		return 0;
	default:
		fatalx("%s: unknown type %d", __func__, (uint8_t)c->flags);
	}
}

static int
fast_match(const void *va, const void *vb)
{
	const struct community *a = va;
	const struct community *b = vb;

	if ((uint8_t)a->flags != (uint8_t)b->flags)
		return (uint8_t)a->flags > (uint8_t)b->flags ? 1 : -1;

	if (a->data1 != b->data1)
		return a->data1 > b->data1 ? 1 : -1;
	if (a->data2 != b->data2)
		return a->data2 > b->data2 ? 1 : -1;
	if (a->data3 != b->data3)
		return a->data3 > b->data3 ? 1 : -1;
	return 0;
}

static int
mask_match(struct community *a, struct community *b, struct community *m)
{
	if ((uint8_t)a->flags != (uint8_t)b->flags)
		return (uint8_t)a->flags > (uint8_t)b->flags ? 1 : -1;

	if ((a->data1 & m->data1) != (b->data1 & m->data1)) {
		if ((a->data1 & m->data1) > (b->data1 & m->data1))
			return 1;
		return -1;
	}
	if ((a->data2 & m->data2) != (b->data2 & m->data2)) {
		if ((a->data2 & m->data2) > (b->data2 & m->data2))
			return 1;
		return -1;
	}
	if ((a->data3 & m->data3) != (b->data3 & m->data3)) {
		if ((a->data3 & m->data3) > (b->data3 & m->data3))
			return 1;
		return -1;
	}
	return 0;
}

/*
 * Insert a community keeping the list sorted. Don't add if already present.
 */
static void
insert_community(struct rde_community *comm, struct community *c)
{
	int l;
	int r;

	if (comm->nentries + 1 > comm->size) {
		struct community *new;
		int newsize = comm->size + 8;

		if ((new = recallocarray(comm->communities, comm->size,
		    newsize, sizeof(struct community))) == NULL)
			fatal(__func__);
		comm->communities = new;
		comm->size = newsize;
	}

	/* XXX can be made faster by binary search */
	for (l = 0; l < comm->nentries; l++) {
		r = fast_match(comm->communities + l, c);
		if (r == 0) {
			/* already present, nothing to do */
			return;
		} else if (r > 0) {
			/* shift reminder by one slot */
			memmove(comm->communities + l + 1,
			    comm->communities + l,
			    (comm->nentries - l) * sizeof(*c));
			break;
		}
	}

	/* insert community at slot l */
	comm->communities[l] = *c;
	comm->nentries++;
}

static int
non_transitive_ext_community(struct community *c)
{
	if ((uint8_t)c->flags != COMMUNITY_TYPE_EXT)
		return 0;
	if ((c->data3 >> 8) & EXT_COMMUNITY_NON_TRANSITIVE)
		return 1;
	return 0;
}

/*
 * Check if a community is present. This function will expand local-as and
 * neighbor-as and also mask of bits to support partial matches.
 */
int
community_match(struct rde_community *comm, struct community *fc,
struct rde_peer *peer)
{
	struct community test, mask;
	int l;

	if (fc->flags >> 8 == 0) {
		/* fast path */
		return (bsearch(fc, comm->communities, comm->nentries,
		    sizeof(*fc), fast_match) != NULL);
	} else {
		/* slow path */
		if (fc2c(fc, peer, &test, &mask) == -1)
			return 0;

		for (l = 0; l < comm->nentries; l++) {
			if (mask_match(&comm->communities[l], &test,
			    &mask) == 0)
				return 1;
		}
		return 0;
	}
}

/*
 * Count the number of communities of type type.
 */
int
community_count(struct rde_community *comm, uint8_t type)
{
	int l;
	int count = 0;

	/* use the fact that the array is ordered by type */
	switch (type) {
	case COMMUNITY_TYPE_BASIC:
		for (l = 0; l < comm->nentries; l++) {
			if ((uint8_t)comm->communities[l].flags == type)
				count++;
			else
				break;
		}
		break;
	case COMMUNITY_TYPE_EXT:
		for (l = 0; l < comm->nentries; l++) {
			if ((uint8_t)comm->communities[l].flags == type)
				count++;
			else if ((uint8_t)comm->communities[l].flags > type)
				break;
		}
		break;
	case COMMUNITY_TYPE_LARGE:
		for (l = comm->nentries; l > 0; l--) {
			if ((uint8_t)comm->communities[l - 1].flags == type)
				count++;
			else
				break;
		}
		break;
	}
	return count;
}

/*
 * Insert a community, expanding local-as and neighbor-as if needed.
 */
int
community_set(struct rde_community *comm, struct community *fc,
struct rde_peer *peer)
{
	struct community set;

	if (fc->flags >> 8 == 0) {
		/* fast path */
		insert_community(comm, fc);
	} else {
		if (fc2c(fc, peer, &set, NULL) == -1)
			return 0;
		if ((uint8_t)set.flags == COMMUNITY_TYPE_EXT) {
			int type = (int)set.data3 >> 8;
			switch (type & EXT_COMMUNITY_VALUE) {
			case EXT_COMMUNITY_TRANS_TWO_AS:
			case EXT_COMMUNITY_TRANS_FOUR_AS:
				/* check that values fit */
				if (set.data1 > USHRT_MAX &&
				    set.data2 > USHRT_MAX)
					return 0;
				if (set.data1 > USHRT_MAX)
					set.data3 = (set.data3 & 0xff) |
					    EXT_COMMUNITY_TRANS_FOUR_AS << 8;
				else
					set.data3 = (set.data3 & 0xff) |
					    EXT_COMMUNITY_TRANS_TWO_AS << 8;
				break;
			}
		}
		insert_community(comm, &set);
	}
	return 1;
}

/*
 * Remove a community if present, This function will expand local-as and
 * neighbor-as and also mask of bits to support partial matches.
 */
void
community_delete(struct rde_community *comm, struct community *fc,
struct rde_peer *peer)
{
	struct community test, mask;
	struct community *match;
	int l = 0;

	if (fc->flags >> 8 == 0) {
		/* fast path */
		match = bsearch(fc, comm->communities, comm->nentries,
		    sizeof(*fc), fast_match);
		if (match == NULL)
			return;
		/* move everything after match down by 1 */
		memmove(match, match + 1,
		    (char *)(comm->communities + comm->nentries) -
		    (char *)(match + 1));
		comm->nentries--;
		return;
	} else {
		if (fc2c(fc, peer, &test, &mask) == -1)
			return;

		while (l < comm->nentries) {
			if (mask_match(&comm->communities[l], &test,
			    &mask) == 0) {
				memmove(comm->communities + l,
				    comm->communities + l + 1,
				    (comm->nentries - l - 1) * sizeof(test));
				comm->nentries--;
				continue;
			}
			l++;
		}
	}
}

/*
 * Internalize communities from the wireformat.
 * Store the partial flag in struct rde_community so it is not lost.
 * - community_add for ATTR_COMMUNITUES
 * - community_large_add for ATTR_LARGE_COMMUNITIES
 * - community_ext_add for ATTR_EXT_COMMUNITIES
 */
int
community_add(struct rde_community *comm, int flags, struct ibuf *buf)
{
	struct community set = { .flags = COMMUNITY_TYPE_BASIC };
	uint16_t data1, data2;

	if (ibuf_size(buf) == 0 || ibuf_size(buf) % 4 != 0)
		return -1;

	if (flags & ATTR_PARTIAL)
		comm->flags |= PARTIAL_COMMUNITIES;

	while (ibuf_size(buf) > 0) {
		if (ibuf_get_n16(buf, &data1) == -1 ||
		    ibuf_get_n16(buf, &data2) == -1)
			return -1;
		set.data1 = data1;
		set.data2 = data2;
		insert_community(comm, &set);
	}

	return 0;
}

int
community_large_add(struct rde_community *comm, int flags, struct ibuf *buf)
{
	struct community set = { .flags = COMMUNITY_TYPE_LARGE };

	if (ibuf_size(buf) == 0 || ibuf_size(buf) % 12 != 0)
		return -1;

	if (flags & ATTR_PARTIAL)
		comm->flags |= PARTIAL_LARGE_COMMUNITIES;

	while (ibuf_size(buf) > 0) {
		if (ibuf_get_n32(buf, &set.data1) == -1 ||
		    ibuf_get_n32(buf, &set.data2) == -1 ||
		    ibuf_get_n32(buf, &set.data3) == -1)
			return -1;
		insert_community(comm, &set);
	}

	return 0;
}

int
community_ext_add(struct rde_community *comm, int flags, int ebgp,
    struct ibuf *buf)
{
	struct community set = { .flags = COMMUNITY_TYPE_EXT };
	uint64_t c;
	uint8_t type;

	if (ibuf_size(buf) == 0 || ibuf_size(buf) % 8 != 0)
		return -1;

	if (flags & ATTR_PARTIAL)
		comm->flags |= PARTIAL_EXT_COMMUNITIES;

	while (ibuf_size(buf) > 0) {
		if (ibuf_get_n64(buf, &c) == -1)
			return (-1);

		type = c >> 56;
		/* filter out non-transitive ext communuties from ebgp peers */
		if (ebgp && (type & EXT_COMMUNITY_NON_TRANSITIVE))
			continue;
		switch (type & EXT_COMMUNITY_VALUE) {
		case EXT_COMMUNITY_TRANS_TWO_AS:
		case EXT_COMMUNITY_TRANS_OPAQUE:
		case EXT_COMMUNITY_TRANS_EVPN:
			set.data1 = c >> 32 & 0xffff;
			set.data2 = c;
			break;
		case EXT_COMMUNITY_TRANS_FOUR_AS:
		case EXT_COMMUNITY_TRANS_IPV4:
			set.data1 = c >> 16;
			set.data2 = c & 0xffff;
			break;
		}
		set.data3 = c >> 48;

		insert_community(comm, &set);
	}

	return 0;
}

/*
 * Convert communities back to the wireformat.
 * When writing ATTR_EXT_COMMUNITIES non-transitive communities need to
 * be skipped if they are sent to an ebgp peer.
 */
int
community_writebuf(struct rde_community *comm, uint8_t type, int ebgp,
    struct ibuf *buf)
{
	struct community *cp;
	uint64_t ext;
	int l, size, start, end, num;
	int flags = ATTR_OPTIONAL | ATTR_TRANSITIVE;
	uint8_t t;

	switch (type) {
	case ATTR_COMMUNITIES:
		if (comm->flags & PARTIAL_COMMUNITIES)
			flags |= ATTR_PARTIAL;
		size = 4;
		t = COMMUNITY_TYPE_BASIC;
		break;
	case ATTR_EXT_COMMUNITIES:
		if (comm->flags & PARTIAL_EXT_COMMUNITIES)
			flags |= ATTR_PARTIAL;
		size = 8;
		t = COMMUNITY_TYPE_EXT;
		break;
	case ATTR_LARGE_COMMUNITIES:
		if (comm->flags & PARTIAL_LARGE_COMMUNITIES)
			flags |= ATTR_PARTIAL;
		size = 12;
		t = COMMUNITY_TYPE_LARGE;
		break;
	default:
		return -1;
	}

	/* first count how many communities will be written */
	num = 0;
	start = -1;
	for (l = 0; l < comm->nentries; l++) {
		cp = &comm->communities[l];
		if ((uint8_t)cp->flags == t) {
			if (ebgp && non_transitive_ext_community(cp))
				continue;
			num++;
			if (start == -1)
				start = l;
		}
		if ((uint8_t)cp->flags > t)
			break;
	}
	end = l;

	/* no communities for this type present */
	if (num == 0)
		return 0;

	if (num > INT16_MAX / size)
		return -1;

	/* write attribute header */
	if (attr_writebuf(buf, flags, type, NULL, num * size) == -1)
		return -1;

	/* write out the communities */
	for (l = start; l < end; l++) {
		cp = &comm->communities[l];

		switch (type) {
		case ATTR_COMMUNITIES:
			if (ibuf_add_n16(buf, cp->data1) == -1)
				return -1;
			if (ibuf_add_n16(buf, cp->data2) == -1)
				return -1;
			break;
		case ATTR_EXT_COMMUNITIES:
			if (ebgp && non_transitive_ext_community(cp))
				continue;

			ext = (uint64_t)cp->data3 << 48;
			switch ((cp->data3 >> 8) & EXT_COMMUNITY_VALUE) {
			case EXT_COMMUNITY_TRANS_TWO_AS:
			case EXT_COMMUNITY_TRANS_OPAQUE:
			case EXT_COMMUNITY_TRANS_EVPN:
				ext |= ((uint64_t)cp->data1 & 0xffff) << 32;
				ext |= (uint64_t)cp->data2;
				break;
			case EXT_COMMUNITY_TRANS_FOUR_AS:
			case EXT_COMMUNITY_TRANS_IPV4:
				ext |= (uint64_t)cp->data1 << 16;
				ext |= (uint64_t)cp->data2 & 0xffff;
				break;
			}
			if (ibuf_add_n64(buf, ext) == -1)
				return -1;
			break;
		case ATTR_LARGE_COMMUNITIES:
			if (ibuf_add_n32(buf, cp->data1) == -1)
				return -1;
			if (ibuf_add_n32(buf, cp->data2) == -1)
				return -1;
			if (ibuf_add_n32(buf, cp->data3) == -1)
				return -1;
			break;
		}
	}
	return 0;
}

/*
 * Global RIB cache for communities
 */
static inline int
communities_compare(struct rde_community *a, struct rde_community *b)
{
	if (a->nentries != b->nentries)
		return a->nentries > b->nentries ? 1 : -1;
	if (a->flags != b->flags)
		return a->flags > b->flags ? 1 : -1;

	return memcmp(a->communities, b->communities,
	    a->nentries * sizeof(struct community));
}

RB_HEAD(comm_tree, rde_community)	commtable = RB_INITIALIZER(&commtable);
RB_GENERATE_STATIC(comm_tree, rde_community, entry, communities_compare);

void
communities_shutdown(void)
{
	if (!RB_EMPTY(&commtable))
		log_warnx("%s: free non-free table", __func__);
}

struct rde_community *
communities_lookup(struct rde_community *comm)
{
	return RB_FIND(comm_tree, &commtable, comm);
}

struct rde_community *
communities_link(struct rde_community *comm)
{
	struct rde_community *n, *f;

	if ((n = malloc(sizeof(*n))) == NULL)
		fatal(__func__);
	communities_copy(n, comm);

	if ((f = RB_INSERT(comm_tree, &commtable, n)) != NULL) {
		log_warnx("duplicate communities collection inserted");
		free(n->communities);
		free(n);
		return f;
	}
	n->refcnt = 1;	/* initial reference by the cache */

	rdemem.comm_size += n->size;
	rdemem.comm_nmemb += n->nentries;
	rdemem.comm_cnt++;

	return n;
}

void
communities_unlink(struct rde_community *comm)
{
	if (comm->refcnt != 1)
		fatalx("%s: unlinking still referenced communities", __func__);

	RB_REMOVE(comm_tree, &commtable, comm);

	rdemem.comm_size -= comm->size;
	rdemem.comm_nmemb -= comm->nentries;
	rdemem.comm_cnt--;

	free(comm->communities);
	free(comm);
}

/*
 * Return true/1 if the two communities collections are identical,
 * otherwise returns zero.
 */
int
communities_equal(struct rde_community *a, struct rde_community *b)
{
	if (a->nentries != b->nentries)
		return 0;
	if (a->flags != b->flags)
		return 0;

	return (memcmp(a->communities, b->communities,
	    a->nentries * sizeof(struct community)) == 0);
}

/*
 * Copy communities to a new unreferenced struct. Needs to call
 * communities_clean() when done. to can be statically allocated,
 * it will be cleaned first.
 */
void
communities_copy(struct rde_community *to, struct rde_community *from)
{
	memset(to, 0, sizeof(*to));

	/* ignore from->size and allocate the perfect amount */
	to->size = from->nentries;
	to->nentries = from->nentries;
	to->flags = from->flags;

	if (to->nentries == 0)
		return;

	if ((to->communities = reallocarray(NULL, to->size,
	    sizeof(struct community))) == NULL)
		fatal(__func__);

	memcpy(to->communities, from->communities,
	    to->nentries * sizeof(struct community));
}

/*
 * Clean up the communities by freeing any dynamically allocated memory.
 */
void
communities_clean(struct rde_community *comm)
{
	if (comm->refcnt != 0)
		fatalx("%s: cleaning still referenced communities", __func__);

	free(comm->communities);
	memset(comm, 0, sizeof(*comm));
}

int
community_to_rd(struct community *fc, uint64_t *community)
{
	struct community c;
	uint64_t rd;

	if (fc2c(fc, NULL, &c, NULL) == -1)
		return -1;

	switch ((c.data3 >> 8) & EXT_COMMUNITY_VALUE) {
	case EXT_COMMUNITY_TRANS_TWO_AS:
		rd = (0ULL << 48);
		rd |= ((uint64_t)c.data1 & 0xffff) << 32;
		rd |= (uint64_t)c.data2;
		break;
	case EXT_COMMUNITY_TRANS_IPV4:
		rd = (1ULL << 48);
		rd |= (uint64_t)c.data1 << 16;
		rd |= (uint64_t)c.data2 & 0xffff;
		break;
	case EXT_COMMUNITY_TRANS_FOUR_AS:
		rd = (2ULL << 48);
		rd |= (uint64_t)c.data1 << 16;
		rd |= (uint64_t)c.data2 & 0xffff;
		break;
	default:
		return -1;
	}

	*community = htobe64(rd);
	return 0;
}
