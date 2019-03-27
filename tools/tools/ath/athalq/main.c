/*
 * Copyright (c) 2012 Adrian Chadd <adrian@FreeBSD.org>
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

#if 1
#include "ar9300_ds.h"
#endif
#include "ar5210_ds.h"
#include "ar5211_ds.h"
#include "ar5212_ds.h"
#include "ar5416_ds.h"

#include "tdma.h"

#define AR5210_MAGIC    0x19980124
#define AR5211_MAGIC    0x19570405
#define AR5212_MAGIC    0x19541014
#define AR5416_MAGIC    0x20065416
#define AR9300_MAGIC    0x19741014

#define	READBUF_SIZE	1024

struct if_ath_alq_init_state hdr;

static void
ath_alq_print_hdr(struct if_ath_alq_init_state *hdr)
{
	printf("macVersion=%d.%d, PHY=%d, Magic=%08x\n",
	    be32toh(hdr->sc_mac_version),
	    be32toh(hdr->sc_mac_revision),
	    be32toh(hdr->sc_phy_rev),
	    be32toh(hdr->sc_hal_magic));
}

static void
ath_alq_print_intr_status(struct if_ath_alq_payload *a)
{
	struct if_ath_alq_interrupt is;

	/* XXX len check! */
	memcpy(&is, &a->payload, sizeof(is));

	printf("[%u.%06u] [%llu] INTR: status=0x%08x\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    be32toh(is.intr_status));
}

static void
ath_alq_print_beacon_miss(struct if_ath_alq_payload *a)
{

	printf("[%u.%06u] [%llu] BMISS\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid));
}

static void
ath_alq_print_beacon_stuck(struct if_ath_alq_payload *a)
{

	printf("[%u.%06u] [%llu] BSTUCK\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid));
}

static void
ath_alq_print_beacon_resume(struct if_ath_alq_payload *a)
{

	printf("[%u.%06u] [%llu] BRESUME\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid));
}

int
main(int argc, const char *argv[])
{
	const char *file = argv[1];
	int fd;
	struct if_ath_alq_payload *a;
	int r;
	char buf[READBUF_SIZE];
	int buflen = 0;

	if (argc < 2) {
		printf("usage: %s <ahq log>\n", argv[0]);
		exit(127);
	}

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror("open"); 
		exit(127);
	}

	/*
	 * The payload structure is now no longer a fixed
	 * size. So, hoops are jumped through.  Really
	 * terrible, infficient hoops.
	 */
	while (1) {
		if (buflen < 512) { /* XXX Eww */
			r = read(fd, buf + buflen, READBUF_SIZE - buflen);
			if (r <= 0)
				break;
			buflen += r;
			//printf("read %d bytes, buflen now %d\n", r, buflen);
		}

		a = (struct if_ath_alq_payload *) &buf[0];

		/*
		 * XXX sanity check that len is within the left over
		 * size of buf.
		 */
		if (be16toh(a->hdr.len) > buflen) {
			fprintf(stderr, "%s: len=%d, buf=%d, tsk!\n",
			    argv[0], be16toh(a->hdr.len),
			    buflen);
			break;
		}

		switch (be16toh(a->hdr.op)) {
			case ATH_ALQ_INIT_STATE:
				/* XXX should double check length! */
				memcpy(&hdr, a->payload, sizeof(hdr));
				ath_alq_print_hdr(&hdr);
				break;
			case ATH_ALQ_TDMA_BEACON_STATE:
				ath_tdma_beacon_state(a);
				break;
			case ATH_ALQ_TDMA_TIMER_CONFIG:
				ath_tdma_timer_config(a);
				break;
			case ATH_ALQ_TDMA_SLOT_CALC:
				ath_tdma_slot_calc(a);
				break;
			case ATH_ALQ_TDMA_TSF_ADJUST:
				ath_tdma_tsf_adjust(a);
				break;
			case ATH_ALQ_TDMA_TIMER_SET:
				ath_tdma_timer_set(a);
				break;
			case ATH_ALQ_INTR_STATUS:
				ath_alq_print_intr_status(a);
				break;
			case ATH_ALQ_MISSED_BEACON:
				ath_alq_print_beacon_miss(a);
				break;
			case ATH_ALQ_STUCK_BEACON:
				ath_alq_print_beacon_stuck(a);
				break;
			case ATH_ALQ_RESUME_BEACON:
				ath_alq_print_beacon_resume(a);
				break;
			case ATH_ALQ_TX_FIFO_PUSH:
				ath_alq_print_edma_tx_fifo_push(a);
				break;
			default:
				if (be32toh(hdr.sc_hal_magic) == AR5210_MAGIC)
					ar5210_alq_payload(a);
				else if (be32toh(hdr.sc_hal_magic) == AR5211_MAGIC)
					ar5211_alq_payload(a);
				else if (be32toh(hdr.sc_hal_magic) == AR5212_MAGIC)
					ar5212_alq_payload(a);
				else if (be32toh(hdr.sc_hal_magic) == AR5416_MAGIC)
					ar5416_alq_payload(a);
				else if (be32toh(hdr.sc_hal_magic) == AR9300_MAGIC)
					ar9300_alq_payload(a);
				else
					printf("[%d.%06d] [%lld] op: %d; len %d\n",
					    be32toh(a->hdr.tstamp_sec),
					    be32toh(a->hdr.tstamp_usec),
					    be64toh(a->hdr.threadid),
					    be16toh(a->hdr.op),
					    be16toh(a->hdr.len));
		}

		/*
		 * a.len is minus the header size, so..
		 */
		buflen -= (be16toh(a->hdr.len)
		    + sizeof(struct if_ath_alq_hdr));
		memmove(&buf[0],
		   &buf[be16toh(a->hdr.len) + sizeof(struct if_ath_alq_hdr)],
		   READBUF_SIZE - (be16toh(a->hdr.len)
		   + sizeof(struct if_ath_alq_hdr)));
		//printf("  buflen is now %d\n", buflen);
	}
	close(fd);
}
