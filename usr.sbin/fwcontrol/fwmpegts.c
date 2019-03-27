/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2005
 * 	Petr Holub, Hidetoshi Shimokawa. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

#if __FreeBSD_version >= 500000
#include <arpa/inet.h>
#endif

#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#if defined(__FreeBSD__)
#include <dev/firewire/firewire.h>
#include <dev/firewire/iec68113.h>
#elif defined(__NetBSD__)
#include <dev/ieee1394/firewire.h>
#include <dev/ieee1394/iec68113.h>
#else
#warning "You need to add support for your OS"
#endif


#include "fwmethods.h"

#define	DEBUG 0

/*****************************************************************************

MPEG-2 Transport Stream (MPEG TS) packet format according to IEC 61883:

31                              15                             0
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  --------
|           len                 |tag|  channel  | tcode |  sy   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    1394
|                           header_CRC                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  --------
|0|0|    sid    |      dbs      |fn | qpc |S|RSV|       dbc     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    CIP
|1|0|    fmt    |      fdf      |          fdf/syt              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  --------
|   reserved  |        cycle_count      |      cycle_offset     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |    N x
.                                                               .    MPEG
.                   MPEG TS payload 188 bytes                   .
.                                                               .
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  --------
|                            data_CRC                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  

N.b. that CRCs are removed by firewire layer!

The following fields are fixed for IEEE-1394:
tag = 01b
tcode = 1010b
The length is payload length, i.e. includes CIP header and data size.

The following fields are constant for MPEG TS:
sph = 1 (denoted as S in CIP header above)
dbs = 6
fmt = (1<<5)
fdf = reserved
In the supported streams we also require
qpc = 0
fn = 3 
and thus the payload is divided in 8 blocks as follows:

  +-----+-----+-----+-----+-----+-----+-----+-----+
  | db0 | db1 | db2 | db3 | db4 | db5 | db6 | db7 |
  +-----+-----+-----+-----+-----+-----+-----+-----+

We have several cases of payload distribution based on stream
bandwidth (R):
1) R < 1.5 Mbps: any of db0..db7 may be payload,
2) 1.5 < R < 3 Mbps: db0/db1 or db2/db3 or db4/db5 or db6/db7 is payload,
3) 3 < R < 6 Mbps: db0/db1/db2/db3 or db4/db5/db6/db7 is payload,
4) R > 6 Mbps: all db0..db7 contain the payload.
Currently, only case (4) is supported in fwmpegts.c

Each packet may contain N  MPEG TS data blocks with timestamp header,
which are (4+188)B long. Experimentally, the N ranges from 0 through 3.

*****************************************************************************/


typedef uint8_t mpeg_ts_pld[188];

struct mpeg_pldt {
#if BYTE_ORDER == BIG_ENDIAN
	uint32_t	:7,
				c_count:13,
				c_offset:12;
#else /* BYTE_ORDER != BIG_ENDIAN */
	uint32_t	c_offset:12,
				c_count:13,
				:7;
#endif /* BYTE_ORDER == BIG_ENDIAN */
	mpeg_ts_pld payload;
};


#define	NCHUNK 8
#define	PSIZE 596
#define	NPACKET_R 4096
#define	RBUFSIZE (PSIZE * NPACKET_R)

void
mpegtsrecv(int d, const char *filename, char ich, int count)
{
	struct ciphdr *ciph;
	struct fw_isochreq isoreq;
	struct fw_isobufreq bufreq;
	struct fw_pkt *pkt;
	struct mpeg_pldt *pld;
	uint32_t *ptr;
	int fd, k, len, m, pkt_size, startwr, tlen;
	char *buf;

	startwr = 0;

	if (strcmp(filename, "-") == 0)
		fd = STDOUT_FILENO;
	else {
		fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0660);
		if (fd == -1)
			err(EX_NOINPUT, "%s", filename);
	}
	buf = malloc(RBUFSIZE);

	bufreq.rx.nchunk = NCHUNK;
	bufreq.rx.npacket = NPACKET_R;
	bufreq.rx.psize = PSIZE;
	bufreq.tx.nchunk = 0;
	bufreq.tx.npacket = 0;
	bufreq.tx.psize = 0;
	if (ioctl(d, FW_SSTBUF, &bufreq) < 0)
		err(1, "ioctl");

	isoreq.ch = ich & 0x3f;
	isoreq.tag = (ich >> 6) & 3;

	if (ioctl(d, FW_SRSTREAM, &isoreq) < 0)
		err(1, "ioctl");

	k = m = 0;
	while (count <= 0 || k <= count) {
		len = tlen = read(d, buf, RBUFSIZE);
#if DEBUG
		fprintf(stderr, "Read %d bytes.\n", len);
#endif /* DEBUG */
		if (len < 0) {
			if (errno == EAGAIN) {
				fprintf(stderr, "(EAGAIN) - push 'Play'?\n");
				continue;
			}
			err(1, "read failed");
		}
		ptr = (uint32_t *) buf;

		do {
			pkt = (struct fw_pkt *) ptr;	
#if DEBUG
			fprintf(stderr, "\nReading new packet.\n");
			fprintf(stderr, "%08x %08x %08x %08x\n",
				htonl(ptr[0]), htonl(ptr[1]),
				htonl(ptr[2]), htonl(ptr[3]));
#endif /* DEBUG */
			/* there is no CRC in the 1394 header */
			ciph = (struct ciphdr *)(ptr + 1);	/* skip iso header */
			if (ciph->fmt != CIP_FMT_MPEG)
				errx(1, "unknown format 0x%x", ciph->fmt);
			if (ciph->fn != 3) {
				errx(1,
						"unsupported MPEG TS stream, fn=%d (only fn=3 is supported)",
						ciph->fn);
			}
			ptr = (uint32_t *) (ciph + 1);		/* skip cip header */

			if (pkt->mode.stream.len <= sizeof(struct ciphdr)) {
				/* no payload */
				/* tlen needs to be decremented before end of the loop */
				goto next;
			}
#if DEBUG
			else {
				fprintf(stderr,
						"Packet net payload length (IEEE1394 header): %d\n",
						pkt->mode.stream.len - sizeof(struct ciphdr));
				fprintf(stderr, "Data block size (CIP header): %d [q], %d [B]\n",
						ciph->len, ciph->len * 4);
				fprintf(stderr,
						"Data fraction number (CIP header): %d => DBC increments with %d\n",
						ciph->fn, (1<<ciph->fn) );
				fprintf(stderr, "QCP (CIP header): %d\n", ciph->qpc );
				fprintf(stderr, "DBC counter (CIP header): %d\n", ciph->dbc );
				fprintf(stderr, "MPEG payload type size: %d\n",
						sizeof(struct mpeg_pldt));
			}
#endif /* DEBUG */

			/* This is a condition that needs to be satisfied to start
			   writing the data */
			if (ciph->dbc % (1<<ciph->fn) == 0)
				startwr = 1;
			/* Read out all the MPEG TS data blocks from current packet */
			for (pld = (struct mpeg_pldt *)ptr;
			    (intptr_t)pld < (intptr_t)((char *)ptr +
			    pkt->mode.stream.len - sizeof(struct ciphdr));
			    pld++) {
				if (startwr == 1)
					write(fd, pld->payload,
					    sizeof(pld->payload));
			}

next:
			/* CRCs are removed from both header and trailer
			so that only 4 bytes of 1394 header remains */
			pkt_size = pkt->mode.stream.len + 4; 
			ptr = (uint32_t *)((intptr_t)pkt + pkt_size);
			tlen -= pkt_size;
		} while (tlen > 0);
#if DEBUG
		fprintf(stderr, "\nReading a data from firewire.\n");
#endif /* DEBUG */

	}
	if (fd != STDOUT_FILENO)
		close(fd);
	fprintf(stderr, "\n");
}
