/*-
 * Copyright (c) 2016 Marcel Moolenaar
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdint.h>
#include <string.h>

#include "endian.h"
#include "image.h"
#include "mkimg.h"

static void osdep_uuidgen(mkimg_uuid_t *);

#ifdef __APPLE__
#include <uuid/uuid.h>

static void
osdep_uuidgen(mkimg_uuid_t *uuid)
{

	uuid_generate_time((void *)uuid);
}
#endif	/* __APPLE__ */

#ifdef __FreeBSD__
#include <sys/uuid.h>

static void
osdep_uuidgen(mkimg_uuid_t *uuid)
{

	uuidgen((void *)uuid, 1);
}
#endif	/* __FreeBSD__ */

#ifdef __linux__
#include <stdlib.h>
#include <time.h>

static void
osdep_uuidgen(mkimg_uuid_t *uuid)
{
	struct timeval tv;
	uint64_t time = 0x01B21DD213814000LL;
	u_int i;
	uint16_t seq;

	if (gettimeofday(&tv, NULL) == -1)
		abort();

	time += (uint64_t)tv.tv_sec * 10000000LL;
	time += tv.tv_usec * 10;

	uuid->time_low = (uint32_t)time;
	uuid->time_mid = (uint16_t)(time >> 32);
	uuid->time_hi_and_version = (uint16_t)(time >> 48) & 0xfff;
	uuid->time_hi_and_version |= 1 << 12;

	seq = random();

	uuid->clock_seq_hi_and_reserved = (uint8_t)(seq >> 8) & 0x3f;
	uuid->clock_seq_low = (uint8_t)seq;

	for (i = 0; i < 6; i++)
		uuid->node[i] = (uint8_t)random();
	uuid->node[0] |= 0x01;
}
#endif	/* __linux__ */

void
mkimg_uuid(mkimg_uuid_t *uuid)
{
	static uint8_t gen[sizeof(mkimg_uuid_t)];
	u_int i;

	if (!unit_testing) {
		osdep_uuidgen(uuid);
		return;
	}

	for (i = 0; i < sizeof(gen); i++)
		gen[i]++;
	memcpy(uuid, gen, sizeof(*uuid));
}

void
mkimg_uuid_enc(void *buf, const mkimg_uuid_t *uuid)
{
	uint8_t *p = buf;
	u_int i;

	le32enc(p, uuid->time_low);
	le16enc(p + 4, uuid->time_mid);
	le16enc(p + 6, uuid->time_hi_and_version);
	p[8] = uuid->clock_seq_hi_and_reserved;
	p[9] = uuid->clock_seq_low;
	for (i = 0; i < 6; i++)
		p[10 + i] = uuid->node[i];
}
