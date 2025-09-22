/*	$OpenBSD: rde_community_test.c,v 1.10 2024/01/24 14:51:56 claudio Exp $ */

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
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rde.h"
#include "log.h"

#include "rde_community_test.h"

struct rde_memstats rdemem;
struct rde_community comm;

static void
dump(uint8_t *b, size_t len)
{
	size_t l;

	printf("\n\t{\n\t\t.data = \"");
	for (l = 0; l < len; l++) {
		printf("\\x%02x", b[l]);
		if (l % 12 == 0 && l != 0)
			printf("\"\n\t\t    \"");
	}
	printf("\",\n\t\t.size = %zu\n\t},\n", len);
}

static int
test_parsing(size_t num, struct ibuf *in, struct ibuf *out)
{
	struct ibuf *buf, abuf;
	uint8_t flags, type, attr[256];
	size_t skip;
	uint16_t attr_len;
	int r;

	communities_clean(&comm);

	do {
		if (ibuf_get_n8(in, &flags) == -1 ||
		    ibuf_get_n8(in, &type) == -1)
			goto bad;
		if (flags & ATTR_EXTLEN) {
			if (ibuf_get_n16(in, &attr_len) == -1)
				goto bad;
		} else {
			uint8_t tmp;
			if (ibuf_get_n8(in, &tmp) == -1)
				goto bad;
			attr_len = tmp;
		}
		if (ibuf_get_ibuf(in, attr_len, &abuf) == -1) {
 bad:
			printf("Test %zu: attribute parse failure\n", num);
			return -1;
		}

		switch (type) {
		case ATTR_COMMUNITIES:
			r = community_add(&comm, flags, &abuf);
			break;
		case ATTR_EXT_COMMUNITIES:
			r = community_ext_add(&comm, flags, 0, &abuf);
			break;
		case ATTR_LARGE_COMMUNITIES:
			r = community_large_add(&comm, flags, &abuf);
			break;
		}
		if (r == -1) {
			printf("Test %zu: community_add failed\n", num);
			return -1;
		}
	} while (ibuf_size(in) > 0);

	if ((buf = ibuf_dynamic(0, 4096)) == NULL) {
		printf("Test %zu: ibuf_dynamic failed\n", num);
		return -1;
	}

	if (community_writebuf(&comm, ATTR_COMMUNITIES, 1, buf) == -1) {
		printf("Test %zu: community_writebuf failed\n", num);
		return -1;
	}
	if (community_writebuf(&comm, ATTR_EXT_COMMUNITIES, 1, buf) == -1) {
		printf("Test %zu: community_writebuf failed\n", num);
		return -1;
	}
	if (community_writebuf(&comm, ATTR_LARGE_COMMUNITIES, 1, buf) == -1) {
		printf("Test %zu: community_writebuf failed\n", num);
		return -1;
	}

	if (ibuf_size(buf) != ibuf_size(out)) {
		printf("Test %zu: ibuf size value %zd != %zd:",
		    num, ibuf_size(buf), ibuf_size(out));
		dump(ibuf_data(buf), ibuf_size(buf));
		printf("expected: ");
		dump(ibuf_data(out), ibuf_size(out));
		return -1;
	}
	if (memcmp(ibuf_data(buf), ibuf_data(out), ibuf_size(out)) != 0) {
		printf("Test %zu: unexpected encoding: ", num);
		dump(ibuf_data(buf), ibuf_size(buf));
		printf("expected: ");
		dump(ibuf_data(out), ibuf_size(out));
		return -1;
	}

	return 0;
}

static int
test_filter(size_t num, struct testfilter *f)
{
	size_t l;
	int r;
	struct rde_peer *p = &peer;

	communities_clean(&comm);

	if (f->peer != NULL)
		p = f->peer;

	for (l = 0; f->in[l] != -1; l++) {
		r = community_set(&comm, &filters[f->in[l]], p);
		if (r != 1) {
			printf("Test %zu: community_set %zu "
			    "unexpected return %d != 1\n",
			    num, l, r);
			return -1;
		}
	}

	if (f->match != -1) {
		r = community_match(&comm, &filters[f->match], p);
		if (r != f->mout) {
			printf("Test %zu: community_match "
			    "unexpected return %d != %d\n", num, r, f->mout);
			return -1;
		}
	}

	if (f->delete != -1) {
		community_delete(&comm, &filters[f->delete], p);

		if (community_match(&comm, &filters[f->delete], p) != 0) {
			printf("Test %zu: community_delete still around\n",
			    num);
			return -1;
		}
	}

	if (f->ncomm != 0) {
		if (community_count(&comm, COMMUNITY_TYPE_BASIC) !=
		    f->ncomm - 1) {
			printf("Test %zu: community_count unexpected "
			    "return %d != %d\n", num, r, f->ncomm - 1);
			return -1;
		}
	}

	if (f->next != 0) {
		if (community_count(&comm, COMMUNITY_TYPE_EXT) !=
		    f->next - 1) {
			printf("Test %zu: ext community_count unexpected "
			    "return %d != %d\n", num, r, f->next - 1);
			return -1;
		}
	}

	if (f->nlarge != 0) {
		if (community_count(&comm, COMMUNITY_TYPE_LARGE) !=
		    f->nlarge - 1) {
			printf("Test %zu: large community_count unexpected "
			    "return %d != %d\n", num, r, f->nlarge - 1);
			return -1;
		}
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	size_t t;
	int error = 0;

	for (t = 0; t < sizeof(vectors) / sizeof(*vectors); t++) {
		struct ibuf in, out;

		ibuf_from_buffer(&in, vectors[t].data, vectors[t].size);
		if (vectors[t].expected == NULL)
			ibuf_from_buffer(&out,
			    vectors[t].data, vectors[t].size);
		else 
			ibuf_from_buffer(&out,
			    vectors[t].expected, vectors[t].expsize);

		if (test_parsing(t, &in, &out) == -1)
			error = 1;
	}

	for (t = 0; t < sizeof(testfilters) / sizeof(*testfilters); t++) {
		if (test_filter(t, &testfilters[t]) == -1)
			error = 1;
	}

	if (!error)
		printf("OK\n");
	return error;
}

__dead void
fatalx(const char *emsg, ...)
{
	va_list ap;
	va_start(ap, emsg);
	verrx(2, emsg, ap);
}

__dead void
fatal(const char *emsg, ...)
{
	va_list ap;
	va_start(ap, emsg);
	verr(2, emsg, ap);
}

void
log_warnx(const char *emsg, ...)
{
	va_list  ap;
	va_start(ap, emsg);
	vwarnx(emsg, ap);
	va_end(ap);
}

int
attr_writebuf(struct ibuf *buf, uint8_t flags, uint8_t type, void *data,
    uint16_t data_len)
{
	u_char  hdr[4];

	flags &= ~ATTR_DEFMASK;
	if (data_len > 255) {
		flags |= ATTR_EXTLEN;
		hdr[2] = (data_len >> 8) & 0xff;
		hdr[3] = data_len & 0xff;
	} else {
		hdr[2] = data_len & 0xff;
	}

	hdr[0] = flags;
	hdr[1] = type;

	if (ibuf_add(buf, hdr, flags & ATTR_EXTLEN ? 4 : 3) == -1)
		return (-1);
	if (data != NULL && ibuf_add(buf, data, data_len) == -1)
		return (-1);
	return (0);

}
