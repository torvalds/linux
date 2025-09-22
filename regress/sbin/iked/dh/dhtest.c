/*	$OpenBSD: dhtest.c,v 1.5 2021/05/28 21:09:01 tobhe Exp $	*/
/*	$EOM: dhtest.c,v 1.1 1998/07/18 21:14:20 provos Exp $	*/

/*
 * Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2010 Reyk Floeter <reyk@vantronix.net>
 * Copyright (c) 1998 Niels Provos.  All rights reserved.
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

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

/*
 * This module does a Diffie-Hellman Exchange
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <event.h>
#include <imsg.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dh.h"
#include "iked.h"

int
main(void)
{
	int id;
	struct ibuf *buf, *buf2;
	struct ibuf *sec, *sec2;
	uint8_t *raw, *raw2;
	struct dh_group *group, *group2;
	const char *name[] = { "MODP", "ECP", "CURVE25519" };

	group_init();

	for (id = 0; id < 0xffff; id++) {
		if (((group = group_get(id)) == NULL ||
		    (group2 = group_get(id)) == NULL) ||
		    group->spec->type == GROUP_SNTRUP761X25519)
			continue;

		dh_create_exchange(group, &buf, NULL);
		dh_create_exchange(group2, &buf2, NULL);

		printf ("Testing group %d (%s-%d, length %zu): ", id,
		    name[group->spec->type],
		    group->spec->bits, ibuf_length(buf) * 8);

		dh_create_shared(group, &sec, buf2);
		dh_create_shared(group2, &sec2, buf);

		raw = ibuf_data(sec);
		raw2 = ibuf_data(sec2);

		if (memcmp (raw, raw2, ibuf_length(sec))) {
			printf("FAILED\n");
			return (1);
		} else
			printf("OKAY\n");

		group_free(group);
		group_free(group2);
	}

	return (0);
}
