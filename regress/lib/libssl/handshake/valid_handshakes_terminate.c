/*	$OpenBSD: valid_handshakes_terminate.c,v 1.4 2022/12/01 13:49:12 tb Exp $	*/
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
#include <stdio.h>

#include "tls13_handshake.c"

int
main(int argc, char *argv[])
{
	size_t	i, j;
	int	terminates;
	int	fail = 0;

	for (i = 1; i < handshake_count; i++) {
		enum tls13_message_type mt = handshakes[i][0];

		if (mt == INVALID)
			continue;

		terminates = 0;

		for (j = 0; j < TLS13_NUM_MESSAGE_TYPES; j++) {
			mt = handshakes[i][j];
			if (state_machine[mt].handshake_complete) {
				terminates = 1;
				break;
			}
		}

		if (!terminates) {
			fail = 1;
			printf("FAIL: handshake_complete never true in "
			    "handshake %zu\n", i);
		}
	}

	return fail;
}
