/*
 * Copyright (c) 2013 Adrian Chadd <adrian@FreeBSD.org>
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/alq.h>
#include <sys/endian.h>

#include <dev/ath/if_ath_alq.h>

#include "tdma.h"

void
ath_tdma_beacon_state(struct if_ath_alq_payload *a)
{
	struct if_ath_alq_tdma_beacon_state t;
	static uint64_t last_beacon_tx = 0;

	memcpy(&t, &a->payload, sizeof(t));

	printf("[%u.%06u] [%llu] BEACON: RX TSF=%llu Beacon TSF=%llu (%d)\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    (unsigned long long) be64toh(t.rx_tsf),
	    (unsigned long long) be64toh(t.beacon_tsf),
	    be64toh(t.beacon_tsf) - last_beacon_tx);

	last_beacon_tx = be64toh(t.beacon_tsf);
}

void
ath_tdma_timer_config(struct if_ath_alq_payload *a)
{
	struct if_ath_alq_tdma_timer_config t;

	memcpy(&t, &a->payload, sizeof(t));
}

void
ath_tdma_slot_calc(struct if_ath_alq_payload *a)
{
	struct if_ath_alq_tdma_slot_calc t;

	memcpy(&t, &a->payload, sizeof(t));
	printf("[%u.%06u] [%llu] SLOTCALC: NEXTTBTT=%llu nextslot=%llu "
	    "tsfdelta=%d avg (%d/%d)\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    (unsigned long long) be64toh(t.nexttbtt),
	    (unsigned long long) be64toh(t.next_slot),
	    (int) be32toh(t.tsfdelta),
	    (int) be32toh(t.avg_plus),
	    (int) be32toh(t.avg_minus));
}

void
ath_tdma_tsf_adjust(struct if_ath_alq_payload *a)
{
	struct if_ath_alq_tdma_tsf_adjust t;

	memcpy(&t, &a->payload, sizeof(t));
	printf("[%u.%06u] [%llu] TSFADJUST: TSF64 was %llu, adj=%d, "
	    "now %llu\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    (unsigned long long) be64toh(t.tsf64_old),
	    (int) be32toh(t.tsfdelta),
	    (unsigned long long) be64toh(t.tsf64_new));
}

void
ath_tdma_timer_set(struct if_ath_alq_payload *a)
{
	struct if_ath_alq_tdma_timer_set t;

	memcpy(&t, &a->payload, sizeof(t));
	printf("[%u.%06u] [%llu] TIMERSET: bt_intval=%d nexttbtt=%d "
	    "nextdba=%d nextswba=%d nextatim=%d flags=0x%x tdmadbaprep=%d "
	    "tdmaswbaprep=%d\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    be32toh(t.bt_intval),
	    be32toh(t.bt_nexttbtt),
	    be32toh(t.bt_nextdba),
	    be32toh(t.bt_nextswba),
	    be32toh(t.bt_nextatim),
	    be32toh(t.bt_flags),
	    be32toh(t.sc_tdmadbaprep),
	    be32toh(t.sc_tdmaswbaprep));
}
