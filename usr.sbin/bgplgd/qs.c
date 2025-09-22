/*	$OpenBSD: qs.c,v 1.7 2024/12/03 10:38:06 claudio Exp $ */
/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#include "bgplgd.h"
#include "slowcgi.h"

enum qs_type {
	ONE,
	STRING,
	PREFIX,
	NUMBER,
	FAMILY,
	OVS,
	AVS,
};

const struct qs {
	unsigned int	qs;
	const char	*key;
	enum qs_type	type;
} qsargs[] = {
	{ QS_NEIGHBOR, "neighbor", STRING, },
	{ QS_GROUP, "group", STRING },
	{ QS_AS, "as", NUMBER },
	{ QS_PREFIX, "prefix", PREFIX },
	{ QS_COMMUNITY, "community", STRING },
	{ QS_EXTCOMMUNITY, "ext-community", STRING },
	{ QS_LARGECOMMUNITY, "large-community", STRING },
	{ QS_AF, "af", FAMILY },
	{ QS_RIB, "rib", STRING },
	{ QS_OVS, "ovs", OVS },
	{ QS_BEST, "best", ONE },
	{ QS_ALL, "all", ONE },
	{ QS_SHORTER, "or-shorter", ONE },
	{ QS_ERROR, "error", ONE },
	{ QS_AVS, "avs", AVS },
	{ QS_INVALID, "invalid", ONE },
	{ QS_LEAKED, "leaked", ONE },
	{ QS_FILTERED, "filtered", ONE },
	{ 0, NULL }
};

const char *qs2str(unsigned int qs);

static int
hex(char x)
{
	if ('0' <= x && x <= '9')
		return x - '0';
	if ('a' <= x && x <= 'f')
		return x - 'a' + 10;
	else
		return x - 'A' + 10;
}

static char *
urldecode(const char *s, size_t len)
{
	static char buf[256];
	size_t i, blen = 0;

	for (i = 0; i < len; i++) {
		if (blen >= sizeof(buf))
			return NULL;
		if (s[i] == '+') {
			buf[blen++] = ' ';
		} else if (s[i] == '%' && i + 2 < len) {
			if (isxdigit((unsigned char)s[i + 1]) &&
			    isxdigit((unsigned char)s[i + 2])) {
				char c;
				c = hex(s[i + 1]) << 4 | hex(s[i + 2]);
				/* replace NUL chars with space */
				if (c == 0)
					c = ' ';
				buf[blen++] = c;
				i += 2;
			} else
				buf[blen++] = s[i];
		} else {
			buf[blen++] = s[i];
		}
	}
	buf[blen] = '\0';

	return buf;
}

static int
valid_string(const char *str)
{
	unsigned char c;

	while ((c = *str++) != '\0')
		if (!isalnum(c) && !ispunct(c) && c != ' ')
			return 0;
	return 1;
}

/* validate that the input is pure decimal number */
static int
valid_number(const char *str)
{
	unsigned char c;
	int first = 1;

	while ((c = *str++) != '\0') {
		/* special handling of 0 */
		if (first && c == '0') {
			if (*str != '\0')
				return 0;
		}
		first = 0;
		if (!isdigit(c))
			return 0;
	}
	return 1;
}

/* validate a prefix, does not support old 10/8 notation but that is ok */
static int
valid_prefix(char *str)
{
	struct addrinfo hints, *res;
	char *p;
	int mask;

	if ((p = strrchr(str, '/')) != NULL) {
		const char *errstr;
		mask = strtonum(p+1, 0, 128, &errstr);
		if (errstr)
			return 0;
		p[0] = '\0';
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(str, NULL, &hints, &res) != 0)
		return 0;
	if (p) {
		if (res->ai_family == AF_INET && mask > 32)
			return 0;
		p[0] = '/';
	}
	freeaddrinfo(res);
	return 1;
}

static int
parse_value(struct lg_ctx *ctx, unsigned int qs, enum qs_type type, char *val)
{
	/* val can only be NULL if urldecode failed. */
	if (val == NULL) {
		lwarnx("urldecode of querystring failed");
		return 400;
	}

	switch (type) {
	case ONE:
		if (strcmp("1", val) == 0) {
			ctx->qs_args[qs].one = 1;
		} else if (strcmp("0", val) == 0) {
			/* silently ignored */
		} else {
			lwarnx("%s: bad value %s expected 1", qs2str(qs), val);
			return 400;
		}
		break;
	case STRING:
		/* limit string to limited ascii chars */
		if (!valid_string(val)) {
			lwarnx("%s: bad string", qs2str(qs));
			return 400;
		}
		ctx->qs_args[qs].string = strdup(val);
		if (ctx->qs_args[qs].string == NULL) {
			lwarn("parse_value");
			return 500;
		}
		break;
	case NUMBER:
		if (!valid_number(val)) {
			lwarnx("%s: bad number", qs2str(qs));
			return 400;
		}
		ctx->qs_args[qs].string = strdup(val);
		if (ctx->qs_args[qs].string == NULL) {
			lwarn("parse_value");
			return 500;
		}
		break;
	case PREFIX:
		if (!valid_prefix(val)) {
			lwarnx("%s: bad prefix", qs2str(qs));
			return 400;
		}
		ctx->qs_args[qs].string = strdup(val);
		if (ctx->qs_args[qs].string == NULL) {
			lwarn("parse_value");
			return 500;
		}
		break;
	case FAMILY:
		if (strcasecmp("ipv4", val) == 0 ||
		    strcasecmp("ipv6", val) == 0 ||
		    strcasecmp("vpnv4", val) == 0 ||
		    strcasecmp("vpnv6", val) == 0) {
			ctx->qs_args[qs].string = strdup(val);
			if (ctx->qs_args[qs].string == NULL) {
				lwarn("parse_value");
				return 500;
			}
		} else {
			lwarnx("%s: bad value %s", qs2str(qs), val);
			return 400;
		}
		break;
	case OVS:
		if (strcmp("not-found", val) == 0 ||
		    strcmp("valid", val) == 0 ||
		    strcmp("invalid", val) == 0) {
			ctx->qs_args[qs].string = strdup(val);
			if (ctx->qs_args[qs].string == NULL) {
				lwarn("parse_value");
				return 500;
			}
		} else {
			lwarnx("%s: bad OVS value %s", qs2str(qs), val);
			return 400;
		}
		break;
	case AVS:
		if (strcmp("unknown", val) == 0 ||
		    strcmp("valid", val) == 0 ||
		    strcmp("invalid", val) == 0) {
			ctx->qs_args[qs].string = strdup(val);
			if (ctx->qs_args[qs].string == NULL) {
				lwarn("parse_value");
				return 500;
			}
		} else {
			lwarnx("%s: bad AVS value %s", qs2str(qs), val);
			return 400;
		}
		break;
	}

	return 0;
}

int
parse_querystring(const char *param, struct lg_ctx *ctx)
{
	size_t len, i;
	int rv;

	while (param && *param) {
		len = strcspn(param, "=");
		for (i = 0; qsargs[i].key != NULL; i++)
			if (strncmp(qsargs[i].key, param, len) == 0)
				break;
		if (qsargs[i].key == NULL) {
			lwarnx("unknown querystring key %.*s", (int)len, param);
			return 400;
		}
		if (((1 << qsargs[i].qs) & ctx->qs_mask) == 0) {
			lwarnx("querystring param %s not allowed for command",
			    qsargs[i].key);
			return 400;
		}
		if (((1 << qsargs[i].qs) & ctx->qs_set) != 0) {
			lwarnx("querystring param %s already set",
			    qsargs[i].key);
			return 400;
		}
		ctx->qs_set |= (1 << qsargs[i].qs);

		if (param[len] != '=') {
			lwarnx("querystring %s without value", qsargs[i].key);
			return 400;
		}

		param += len + 1;
		len = strcspn(param, "&");

		if ((rv = parse_value(ctx, qsargs[i].qs, qsargs[i].type,
		    urldecode(param, len))) != 0)
			return rv;

		param += len;
		if (*param == '&')
			param++;
	}

	return 0;
}

size_t
qs_argv(char **argv, size_t argc, size_t len, struct lg_ctx *ctx, int barenbr)
{
	/* keep space for the final NULL in argv */
	len -= 1;

	/* NEIGHBOR and GROUP are exclusive */
	if (ctx->qs_set & (1 << QS_NEIGHBOR)) {
		if (!barenbr)
			if (argc < len)
				argv[argc++] = "neighbor";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_NEIGHBOR].string;
	} else if (ctx->qs_set & (1 << QS_GROUP)) {
		if (argc < len)
			argv[argc++] = "group";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_GROUP].string;
	}

	if (ctx->qs_set & (1 << QS_AS)) {
		if (argc < len)
			argv[argc++] = "source-as";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_AS].string;
	}
	if (ctx->qs_set & (1 << QS_COMMUNITY)) {
		if (argc < len)
			argv[argc++] = "community";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_COMMUNITY].string;
	}
	if (ctx->qs_set & (1 << QS_EXTCOMMUNITY)) {
		if (argc < len)
			argv[argc++] = "ext-community";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_EXTCOMMUNITY].string;
	}
	if (ctx->qs_set & (1 << QS_LARGECOMMUNITY)) {
		if (argc < len)
			argv[argc++] = "large-community";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_LARGECOMMUNITY].string;
	}
	if (ctx->qs_set & (1 << QS_AF)) {
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_AF].string;
	}
	if (ctx->qs_set & (1 << QS_RIB)) {
		if (argc < len)
			argv[argc++] = "table";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_RIB].string;
	}
	if (ctx->qs_set & (1 << QS_OVS)) {
		if (argc < len)
			argv[argc++] = "ovs";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_OVS].string;
	}
	if (ctx->qs_set & (1 << QS_AVS)) {
		if (argc < len)
			argv[argc++] = "avs";
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_AVS].string;
	}
	/* BEST, ERROR, FILTERED, INVALID and LEAKED are exclusive */
	if (ctx->qs_args[QS_BEST].one) {
		if (argc < len)
			argv[argc++] = "best";
	} else if (ctx->qs_args[QS_ERROR].one) {
		if (argc < len)
			argv[argc++] = "error";
	} else if (ctx->qs_args[QS_FILTERED].one) {
		if (argc < len)
			argv[argc++] = "filtered";
	} else if (ctx->qs_args[QS_INVALID].one) {
		if (argc < len)
			argv[argc++] = "disqualified";
	} else if (ctx->qs_args[QS_LEAKED].one) {
		if (argc < len)
			argv[argc++] = "leaked";
	}

	/* prefix must be last for show rib */
	if (ctx->qs_set & (1 << QS_PREFIX)) {
		if (argc < len)
			argv[argc++] = ctx->qs_args[QS_PREFIX].string;

		/* ALL and SHORTER are exclusive */
		if (ctx->qs_args[QS_ALL].one) {
			if (argc < len)
				argv[argc++] = "all";
		} else if (ctx->qs_args[QS_SHORTER].one) {
			if (argc < len)
				argv[argc++] = "or-shorter";
		}
	}

	if (argc >= len)
		lwarnx("hit limit of argv in qs_argv");

	return argc;
}

const char *
qs2str(unsigned int qs)
{
	size_t i;

	for (i = 0; qsargs[i].key != NULL; i++)
		if (qsargs[i].qs == qs)
			return qsargs[i].key;

	lerrx(1, "unknown querystring param %d", qs);
}
