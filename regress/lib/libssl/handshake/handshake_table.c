/*	$OpenBSD: handshake_table.c,v 1.18 2022/12/01 13:49:12 tb Exp $	*/
/*
 * Copyright (c) 2019 Theo Buehler <tb@openbsd.org>
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "tls13_handshake.h"

#define MAX_FLAGS (UINT8_MAX + 1)

/*
 * From RFC 8446:
 *
 * Appendix A.  State Machine
 *
 *    This appendix provides a summary of the legal state transitions for
 *    the client and server handshakes.  State names (in all capitals,
 *    e.g., START) have no formal meaning but are provided for ease of
 *    comprehension.  Actions which are taken only in certain circumstances
 *    are indicated in [].  The notation "K_{send,recv} = foo" means "set
 *    the send/recv key to the given key".
 *
 * A.1.  Client
 *
 *                               START <----+
 *                Send ClientHello |        | Recv HelloRetryRequest
 *           [K_send = early data] |        |
 *                                 v        |
 *            /                 WAIT_SH ----+
 *            |                    | Recv ServerHello
 *            |                    | K_recv = handshake
 *        Can |                    V
 *       send |                 WAIT_EE
 *      early |                    | Recv EncryptedExtensions
 *       data |           +--------+--------+
 *            |     Using |                 | Using certificate
 *            |       PSK |                 v
 *            |           |            WAIT_CERT_CR
 *            |           |        Recv |       | Recv CertificateRequest
 *            |           | Certificate |       v
 *            |           |             |    WAIT_CERT
 *            |           |             |       | Recv Certificate
 *            |           |             v       v
 *            |           |              WAIT_CV
 *            |           |                 | Recv CertificateVerify
 *            |           +> WAIT_FINISHED <+
 *            |                  | Recv Finished
 *            \                  | [Send EndOfEarlyData]
 *                               | K_send = handshake
 *                               | [Send Certificate [+ CertificateVerify]]
 *     Can send                  | Send Finished
 *     app data   -->            | K_send = K_recv = application
 *     after here                v
 *                           CONNECTED
 *
 *    Note that with the transitions as shown above, clients may send
 *    alerts that derive from post-ServerHello messages in the clear or
 *    with the early data keys.  If clients need to send such alerts, they
 *    SHOULD first rekey to the handshake keys if possible.
 *
 */

struct child {
	enum tls13_message_type	mt;
	uint8_t			flag;
	uint8_t			forced;
	uint8_t			illegal;
};

static struct child stateinfo[][TLS13_NUM_MESSAGE_TYPES] = {
	[CLIENT_HELLO] = {
		{
			.mt = SERVER_HELLO_RETRY_REQUEST,
		},
		{
			.mt = SERVER_HELLO,
			.flag = WITHOUT_HRR,
		},
	},
	[SERVER_HELLO_RETRY_REQUEST] = {
		{
			.mt = CLIENT_HELLO_RETRY,
		},
	},
	[CLIENT_HELLO_RETRY] = {
		{
			.mt = SERVER_HELLO,
		},
	},
	[SERVER_HELLO] = {
		{
			.mt = SERVER_ENCRYPTED_EXTENSIONS,
		},
	},
	[SERVER_ENCRYPTED_EXTENSIONS] = {
		{
			.mt = SERVER_CERTIFICATE_REQUEST,
		},
		{	.mt = SERVER_CERTIFICATE,
			.flag = WITHOUT_CR,
		},
		{
			.mt = SERVER_FINISHED,
			.flag = WITH_PSK,
		},
	},
	[SERVER_CERTIFICATE_REQUEST] = {
		{
			.mt = SERVER_CERTIFICATE,
		},
	},
	[SERVER_CERTIFICATE] = {
		{
			.mt = SERVER_CERTIFICATE_VERIFY,
		},
	},
	[SERVER_CERTIFICATE_VERIFY] = {
		{
			.mt = SERVER_FINISHED,
		},
	},
	[SERVER_FINISHED] = {
		{
			.mt = CLIENT_FINISHED,
			.forced = WITHOUT_CR | WITH_PSK,
		},
		{
			.mt = CLIENT_CERTIFICATE,
			.illegal = WITHOUT_CR | WITH_PSK,
		},
	},
	[CLIENT_CERTIFICATE] = {
		{
			.mt = CLIENT_FINISHED,
		},
		{
			.mt = CLIENT_CERTIFICATE_VERIFY,
			.flag = WITH_CCV,
		},
	},
	[CLIENT_CERTIFICATE_VERIFY] = {
		{
			.mt = CLIENT_FINISHED,
		},
	},
	[CLIENT_FINISHED] = {
		{
			.mt = APPLICATION_DATA,
		},
	},
	[APPLICATION_DATA] = {
		{
			.mt = 0,
		},
	},
};

const size_t	 stateinfo_count = sizeof(stateinfo) / sizeof(stateinfo[0]);

void		 build_table(enum tls13_message_type
		     table[MAX_FLAGS][TLS13_NUM_MESSAGE_TYPES],
		     struct child current, struct child end,
		     struct child path[], uint8_t flags, unsigned int depth);
size_t		 count_handshakes(void);
void		 edge(enum tls13_message_type start,
		     enum tls13_message_type end, uint8_t flag);
const char	*flag2str(uint8_t flag);
void		 flag_label(uint8_t flag);
void		 forced_edges(enum tls13_message_type start,
		     enum tls13_message_type end, uint8_t forced);
int		 generate_graphics(void);
void		 fprint_entry(FILE *stream,
		     enum tls13_message_type path[TLS13_NUM_MESSAGE_TYPES],
		     uint8_t flags);
void		 fprint_flags(FILE *stream, uint8_t flags);
const char	*mt2str(enum tls13_message_type mt);
void		 usage(void);
int		 verify_table(enum tls13_message_type
		     table[MAX_FLAGS][TLS13_NUM_MESSAGE_TYPES], int print);

const char *
flag2str(uint8_t flag)
{
	const char *ret;

	if (flag & (flag - 1))
		errx(1, "more than one bit is set");

	switch (flag) {
	case INITIAL:
		ret = "INITIAL";
		break;
	case NEGOTIATED:
		ret = "NEGOTIATED";
		break;
	case WITHOUT_CR:
		ret = "WITHOUT_CR";
		break;
	case WITHOUT_HRR:
		ret = "WITHOUT_HRR";
		break;
	case WITH_PSK:
		ret = "WITH_PSK";
		break;
	case WITH_CCV:
		ret = "WITH_CCV";
		break;
	case WITH_0RTT:
		ret = "WITH_0RTT";
		break;
	default:
		ret = "UNKNOWN";
	}

	return ret;
}

const char *
mt2str(enum tls13_message_type mt)
{
	const char *ret;

	switch (mt) {
	case INVALID:
		ret = "INVALID";
		break;
	case CLIENT_HELLO:
		ret = "CLIENT_HELLO";
		break;
	case CLIENT_HELLO_RETRY:
		ret = "CLIENT_HELLO_RETRY";
		break;
	case CLIENT_END_OF_EARLY_DATA:
		ret = "CLIENT_END_OF_EARLY_DATA";
		break;
	case CLIENT_CERTIFICATE:
		ret = "CLIENT_CERTIFICATE";
		break;
	case CLIENT_CERTIFICATE_VERIFY:
		ret = "CLIENT_CERTIFICATE_VERIFY";
		break;
	case CLIENT_FINISHED:
		ret = "CLIENT_FINISHED";
		break;
	case SERVER_HELLO:
		ret = "SERVER_HELLO";
		break;
	case SERVER_HELLO_RETRY_REQUEST:
		ret = "SERVER_HELLO_RETRY_REQUEST";
		break;
	case SERVER_ENCRYPTED_EXTENSIONS:
		ret = "SERVER_ENCRYPTED_EXTENSIONS";
		break;
	case SERVER_CERTIFICATE:
		ret = "SERVER_CERTIFICATE";
		break;
	case SERVER_CERTIFICATE_VERIFY:
		ret = "SERVER_CERTIFICATE_VERIFY";
		break;
	case SERVER_CERTIFICATE_REQUEST:
		ret = "SERVER_CERTIFICATE_REQUEST";
		break;
	case SERVER_FINISHED:
		ret = "SERVER_FINISHED";
		break;
	case APPLICATION_DATA:
		ret = "APPLICATION_DATA";
		break;
	case TLS13_NUM_MESSAGE_TYPES:
		ret = "TLS13_NUM_MESSAGE_TYPES";
		break;
	default:
		ret = "UNKNOWN";
		break;
	}

	return ret;
}

void
fprint_flags(FILE *stream, uint8_t flags)
{
	int first = 1, i;

	if (flags == 0) {
		fprintf(stream, "%s", flag2str(flags));
		return;
	}

	for (i = 0; i < 8; i++) {
		uint8_t set = flags & (1U << i);

		if (set) {
			fprintf(stream, "%s%s", first ? "" : " | ",
			    flag2str(set));
			first = 0;
		}
	}
}

void
fprint_entry(FILE *stream,
    enum tls13_message_type path[TLS13_NUM_MESSAGE_TYPES], uint8_t flags)
{
	int i;

	fprintf(stream, "\t[");
	fprint_flags(stream, flags);
	fprintf(stream, "] = {\n");

	for (i = 0; i < TLS13_NUM_MESSAGE_TYPES; i++) {
		if (path[i] == 0)
			break;
		fprintf(stream, "\t\t%s,\n", mt2str(path[i]));
	}
	fprintf(stream, "\t},\n");
}

void
edge(enum tls13_message_type start, enum tls13_message_type end,
    uint8_t flag)
{
	printf("\t%s -> %s", mt2str(start), mt2str(end));
	flag_label(flag);
	printf(";\n");
}

void
flag_label(uint8_t flag)
{
	if (flag)
		printf(" [label=\"%s\"]", flag2str(flag));
}

void
forced_edges(enum tls13_message_type start, enum tls13_message_type end,
    uint8_t forced)
{
	uint8_t	forced_flag, i;

	if (forced == 0)
		return;

	for (i = 0; i < 8; i++) {
		forced_flag = forced & (1U << i);
		if (forced_flag)
			edge(start, end, forced_flag);
	}
}

int
generate_graphics(void)
{
	enum tls13_message_type	start, end;
	unsigned int		child;
	uint8_t			flag;
	uint8_t			forced;

	printf("digraph G {\n");
	printf("\t%s [shape=box];\n", mt2str(CLIENT_HELLO));
	printf("\t%s [shape=box];\n", mt2str(APPLICATION_DATA));

	for (start = CLIENT_HELLO; start < APPLICATION_DATA; start++) {
		for (child = 0; stateinfo[start][child].mt != 0; child++) {
			end = stateinfo[start][child].mt;
			flag = stateinfo[start][child].flag;
			forced = stateinfo[start][child].forced;

			if (forced == 0)
				edge(start, end, flag);
			else
				forced_edges(start, end, forced);
		}
	}

	printf("}\n");
	return 0;
}

extern enum tls13_message_type	handshakes[][TLS13_NUM_MESSAGE_TYPES];
extern size_t			handshake_count;

size_t
count_handshakes(void)
{
	size_t	ret = 0, i;

	for (i = 0; i < handshake_count; i++) {
		if (handshakes[i][0] != INVALID)
			ret++;
	}

	return ret;
}

void
build_table(enum tls13_message_type table[MAX_FLAGS][TLS13_NUM_MESSAGE_TYPES],
    struct child current, struct child end, struct child path[], uint8_t flags,
    unsigned int depth)
{
	unsigned int i;

	if (depth >= TLS13_NUM_MESSAGE_TYPES - 1)
		errx(1, "recursed too deeply");

	/* Record current node. */
	path[depth++] = current;
	flags |= current.flag;

	/* If we haven't reached the end, recurse over the children. */
	if (current.mt != end.mt) {
		for (i = 0; stateinfo[current.mt][i].mt != 0; i++) {
			struct child child = stateinfo[current.mt][i];
			int forced = stateinfo[current.mt][i].forced;
			int illegal = stateinfo[current.mt][i].illegal;

			if ((forced == 0 || (forced & flags)) &&
			    (illegal == 0 || !(illegal & flags)))
				build_table(table, child, end, path, flags,
				    depth);
		}
		return;
	}

	if (flags == 0)
		errx(1, "path does not set flags");

	if (table[flags][0] != 0)
		errx(1, "path traversed twice");

	for (i = 0; i < depth; i++)
		table[flags][i] = path[i].mt;
}

int
verify_table(enum tls13_message_type table[MAX_FLAGS][TLS13_NUM_MESSAGE_TYPES],
    int print)
{
	int	success = 1, i;
	size_t	num_valid, num_found = 0;
	uint8_t	flags = 0;

	do {
		if (table[flags][0] == 0)
			continue;

		num_found++;

		for (i = 0; i < TLS13_NUM_MESSAGE_TYPES; i++) {
			if (table[flags][i] != handshakes[flags][i]) {
				fprintf(stderr,
				    "incorrect entry %d of handshake ", i);
				fprint_flags(stderr, flags);
				fprintf(stderr, "\n");
				success = 0;
			}
		}

		if (print)
			fprint_entry(stdout, table[flags], flags);
	} while(++flags != 0);

	num_valid = count_handshakes();
	if (num_valid != num_found) {
		fprintf(stderr,
		    "incorrect number of handshakes: want %zu, got %zu.\n",
		    num_valid, num_found);
		success = 0;
	}

	return success;
}

void
usage(void)
{
	fprintf(stderr, "usage: handshake_table [-C | -g]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	static enum tls13_message_type
	    hs_table[MAX_FLAGS][TLS13_NUM_MESSAGE_TYPES] = {
		[INITIAL] = {
			CLIENT_HELLO,
			SERVER_HELLO_RETRY_REQUEST,
			CLIENT_HELLO_RETRY,
			SERVER_HELLO,
		},
	};
	struct child	start = {
		.mt = CLIENT_HELLO,
	};
	struct child	end = {
		.mt = APPLICATION_DATA,
	};
	struct child	path[TLS13_NUM_MESSAGE_TYPES] = {{0}};
	uint8_t		flags = NEGOTIATED;
	unsigned int	depth = 0;
	int		ch, graphviz = 0, print = 0;

	while ((ch = getopt(argc, argv, "Cg")) != -1) {
		switch (ch) {
		case 'C':
			print = 1;
			break;
		case 'g':
			graphviz = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (graphviz && print)
		usage();

	if (graphviz)
		return generate_graphics();

	build_table(hs_table, start, end, path, flags, depth);
	if (!verify_table(hs_table, print))
		return 1;

	return 0;
}
