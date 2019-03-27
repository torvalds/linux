/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2003
 * 	Hidetoshi Shimokawa. All rights reserved.
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

#include <dev/firewire/firewire.h>
#include <dev/firewire/iec68113.h>

#include "fwmethods.h"

#define DEBUG		0
#define FIX_FRAME	1

struct frac {
	int n,d;
};

struct frac frame_cycle[2]  = {
	{8000*100, 2997},	/* NTSC 8000 cycle / 29.97 Hz */
	{320, 1},		/* PAL  8000 cycle / 25 Hz */
};
int npackets[] = {
	250		/* NTSC */,
	300		/* PAL */
};
struct frac pad_rate[2]  = {
	{203, 2997},	/* = (8000 - 29.97 * 250)/(29.97 * 250) */
	{1, 15},	/* = (8000 - 25 * 300)/(25 * 300) */
};
char *system_name[] = {"NTSC", "PAL"};
int frame_rate[] = {30, 25};

#define PSIZE 512
#define DSIZE 480
#define NCHUNK 64

#define NPACKET_R 256
#define NPACKET_T 255
#define TNBUF 100	/* XXX too large value causes block noise */
#define NEMPTY 10	/* depends on TNBUF */
#define RBUFSIZE (PSIZE * NPACKET_R)
#define MAXBLOCKS (300)
#define CYCLE_FRAC 0xc00

void
dvrecv(int d, const char *filename, char ich, int count)
{
	struct fw_isochreq isoreq;
	struct fw_isobufreq bufreq;
	struct dvdbc *dv;
	struct ciphdr *ciph;
	struct fw_pkt *pkt;
	char *pad, *buf;
	u_int32_t *ptr;
	int len, tlen, npad, fd, k, m, vec, system = -1, nb;
	int nblocks[] = {250 /* NTSC */, 300 /* PAL */};
	struct iovec wbuf[NPACKET_R];

	if(strcmp(filename, "-") == 0) {
		fd = STDOUT_FILENO;
	} else {
		fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0660);
		if (fd == -1)
			err(EX_NOINPUT, "%s", filename);
	}
	buf = malloc(RBUFSIZE);
	pad = malloc(DSIZE*MAXBLOCKS);
	memset(pad, 0xff, DSIZE*MAXBLOCKS);
	bzero(wbuf, sizeof(wbuf));

	bufreq.rx.nchunk = NCHUNK;
	bufreq.rx.npacket = NPACKET_R;
	bufreq.rx.psize = PSIZE;
	bufreq.tx.nchunk = 0;
	bufreq.tx.npacket = 0;
	bufreq.tx.psize = 0;
	if (ioctl(d, FW_SSTBUF, &bufreq) < 0)
		err(1, "ioctl FW_SSTBUF");

	isoreq.ch = ich & 0x3f;
	isoreq.tag = (ich >> 6) & 3;

	if (ioctl(d, FW_SRSTREAM, &isoreq) < 0)
       		err(1, "ioctl");

	k = m = 0;
	while (count <= 0 || k <= count) {
#if 0
		tlen = 0;
		while ((len = read(d, buf + tlen, PSIZE
						/* RBUFSIZE - tlen */)) > 0) {
			if (len < 0) {
				if (errno == EAGAIN) {
					fprintf(stderr, "(EAGAIN)\n");
					fflush(stderr);
					if (len <= 0)
						continue;
				} else
					err(1, "read failed");
			}
			tlen += len;
			if ((RBUFSIZE - tlen) < PSIZE)
				break;
		};
#else
		tlen = len = read(d, buf, RBUFSIZE);
		if (len < 0) {
			if (errno == EAGAIN) {
				fprintf(stderr, "(EAGAIN) - push 'Play'?\n");
				fflush(stderr);
				if (len <= 0)
					continue;
			} else
				err(1, "read failed");
		}
#endif
		vec = 0;
		ptr = (u_int32_t *) buf;
again:
		pkt = (struct fw_pkt *) ptr;
#if DEBUG
		fprintf(stderr, "%08x %08x %08x %08x\n",
			htonl(ptr[0]), htonl(ptr[1]),
			htonl(ptr[2]), htonl(ptr[3]));
#endif
		ciph = (struct ciphdr *)(ptr + 1);	/* skip iso header */
		if (ciph->fmt != CIP_FMT_DVCR)
			errx(1, "unknown format 0x%x", ciph->fmt);
		ptr = (u_int32_t *) (ciph + 1);		/* skip cip header */
#if DEBUG
		if (ciph->fdf.dv.cyc != 0xffff && k == 0) {
			fprintf(stderr, "0x%04x\n", ntohs(ciph->fdf.dv.cyc));
		}
#endif
		if (pkt->mode.stream.len <= sizeof(struct ciphdr))
			/* no payload */
			goto next;
		for (dv = (struct dvdbc *)ptr;
				(char *)dv < (char *)(ptr + ciph->len);
				dv+=6) {

#if DEBUG
			fprintf(stderr, "(%d,%d) ", dv->sct, dv->dseq);
#endif
			if  (dv->sct == DV_SCT_HEADER && dv->dseq == 0) {
				if (system < 0) {
					system = ciph->fdf.dv.fs;
					fprintf(stderr, "%s\n", system_name[system]);
				}

				/* Fix DSF bit */
				if (system == 1 &&
					(dv->payload[0] & DV_DSF_12) == 0)
					dv->payload[0] |= DV_DSF_12;
				nb = nblocks[system];
 				fprintf(stderr, "%d:%02d:%02d %d\r",
					k / (3600 * frame_rate[system]),
					(k / (60 * frame_rate[system])) % 60,
					(k / frame_rate[system]) % 60,
					k % frame_rate[system]);

#if FIX_FRAME
				if (m > 0 && m != nb) {
					/* padding bad frame */
					npad = ((nb - m) % nb);
					if (npad < 0)
						npad += nb;
					fprintf(stderr, "\n%d blocks padded\n",
					    npad);
					npad *= DSIZE;
					wbuf[vec].iov_base = pad;
					wbuf[vec++].iov_len = npad;
					if (vec >= NPACKET_R) {
						writev(fd, wbuf, vec);
						vec = 0;
					}
				}
#endif
				k++;
				fflush(stderr);
				m = 0;
			}
			if (k == 0 || (count > 0 && k > count))
				continue;
			m++;
			wbuf[vec].iov_base = (char *) dv;
			wbuf[vec++].iov_len = DSIZE;
			if (vec >= NPACKET_R) {
				writev(fd, wbuf, vec);
				vec = 0;
			}
		}
		ptr = (u_int32_t *)dv;
next:
		if ((char *)ptr < buf + tlen)
			goto again;
		if (vec > 0)
			writev(fd, wbuf, vec);
	}
	if (fd != STDOUT_FILENO)
		close(fd);
	fprintf(stderr, "\n");
}


void
dvsend(int d, const char *filename, char ich, int count)
{
	struct fw_isochreq isoreq;
	struct fw_isobufreq bufreq;
	struct dvdbc *dv;
	struct fw_pkt *pkt;
	int len, tlen, header, fd, frames, packets, vec, offset, nhdr, i;
	int system=-1, pad_acc, cycle_acc, cycle, f_cycle, f_frac;
	struct iovec wbuf[TNBUF*2 + NEMPTY];
	char *pbuf;
	u_int32_t iso_data, iso_empty, hdr[TNBUF + NEMPTY][3];
	struct ciphdr *ciph;
	struct timeval start, end;
	double rtime;

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		err(EX_NOINPUT, "%s", filename);

	pbuf = malloc(DSIZE * TNBUF);
	bzero(wbuf, sizeof(wbuf));

	bufreq.rx.nchunk = 0;
	bufreq.rx.npacket = 0;
	bufreq.rx.psize = 0;
	bufreq.tx.nchunk = NCHUNK;
	bufreq.tx.npacket = NPACKET_T;
	bufreq.tx.psize = PSIZE;
	if (ioctl(d, FW_SSTBUF, &bufreq) < 0)
		err(1, "ioctl FW_SSTBUF");

	isoreq.ch = ich & 0x3f;
	isoreq.tag = (ich >> 6) & 3;

	if (ioctl(d, FW_STSTREAM, &isoreq) < 0)
       		err(1, "ioctl FW_STSTREAM");

	iso_data = 0;
	pkt = (struct fw_pkt *) &iso_data;
	pkt->mode.stream.len = DSIZE + sizeof(struct ciphdr);
	pkt->mode.stream.sy = 0;
	pkt->mode.stream.tcode = FWTCODE_STREAM;
	pkt->mode.stream.chtag = ich;
	iso_empty = iso_data;
	pkt = (struct fw_pkt *) &iso_empty;
	pkt->mode.stream.len = sizeof(struct ciphdr);

	bzero(hdr[0], sizeof(hdr[0]));
	hdr[0][0] = iso_data;
	ciph = (struct ciphdr *)&hdr[0][1];
	ciph->src = 0;	 /* XXX */
	ciph->len = 120;
	ciph->dbc = 0;
	ciph->eoh1 = 1;
	ciph->fdf.dv.cyc = 0xffff;

	for (i = 1; i < TNBUF; i++)
		bcopy(hdr[0], hdr[i], sizeof(hdr[0]));

	gettimeofday(&start, NULL);
#if DEBUG
	fprintf(stderr, "%08x %08x %08x\n",
			htonl(hdr[0]), htonl(hdr[1]), htonl(hdr[2]));
#endif
	frames = 0;
	packets = 0;
	pad_acc = 0;
	while (1) {
		tlen = 0;
		while (tlen < DSIZE * TNBUF) {
			len = read(fd, pbuf + tlen, DSIZE * TNBUF - tlen);
			if (len <= 0) {
				if (tlen > 0)
					break;
				if (len < 0)
					warn("read");
				else
					fprintf(stderr, "\nend of file\n");
				goto send_end;
			}
			tlen += len;
		}
		vec = 0;
		offset = 0;
		nhdr = 0;
next:
		dv = (struct dvdbc *)(pbuf + offset * DSIZE);
#if 0
		header = (dv->sct == 0 && dv->dseq == 0);
#else
		header = (packets == 0 || packets % npackets[system] == 0);
#endif

		ciph = (struct ciphdr *)&hdr[nhdr][1];
		if (header) {
			if (system < 0) {
				system = ((dv->payload[0] & DV_DSF_12) != 0);
				printf("%s\n", system_name[system]);
				cycle = 1;
				cycle_acc = frame_cycle[system].d * cycle;
			}
			fprintf(stderr, "%d", frames % 10);
			frames ++;
			if (count > 0 && frames > count)
				break;
			if (frames % frame_rate[system] == 0)
				fprintf(stderr, "\n");
			fflush(stderr);
			f_cycle = (cycle_acc / frame_cycle[system].d) & 0xf;
			f_frac = (cycle_acc % frame_cycle[system].d
					* CYCLE_FRAC) / frame_cycle[system].d;
#if 0
			ciph->fdf.dv.cyc = htons(f_cycle << 12 | f_frac);
#else
			ciph->fdf.dv.cyc = htons(cycle << 12 | f_frac);
#endif
			cycle_acc += frame_cycle[system].n;
			cycle_acc %= frame_cycle[system].d * 0x10;

		} else {
			ciph->fdf.dv.cyc = 0xffff;
		}
		ciph->dbc = packets++ % 256;
		pad_acc += pad_rate[system].n;
		if (pad_acc >= pad_rate[system].d) {
			pad_acc -= pad_rate[system].d;
			bcopy(hdr[nhdr], hdr[nhdr+1], sizeof(hdr[0]));
			hdr[nhdr][0] = iso_empty;
			wbuf[vec].iov_base = (char *)hdr[nhdr];
			wbuf[vec++].iov_len = sizeof(hdr[0]);
			nhdr ++;
			cycle ++;
		}
		hdr[nhdr][0] = iso_data;
		wbuf[vec].iov_base = (char *)hdr[nhdr];
		wbuf[vec++].iov_len = sizeof(hdr[0]);
		wbuf[vec].iov_base = (char *)dv;
		wbuf[vec++].iov_len = DSIZE;
		nhdr ++;
		cycle ++;
		offset ++;
		if (offset * DSIZE < tlen)
			goto next;

again:
		len = writev(d, wbuf, vec);
		if (len < 0) {
			if (errno == EAGAIN) {
				fprintf(stderr, "(EAGAIN) - push 'Play'?\n");
				goto again;
			}
			err(1, "write failed");
		}
	}
	fprintf(stderr, "\n");
send_end:
	gettimeofday(&end, NULL);
	rtime = end.tv_sec - start.tv_sec
			+ (end.tv_usec - start.tv_usec) * 1e-6;
	fprintf(stderr, "%d frames, %.2f secs, %.2f frames/sec\n",
			frames, rtime, frames/rtime);
	close(fd);
}
