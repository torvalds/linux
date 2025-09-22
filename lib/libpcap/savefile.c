/*	$OpenBSD: savefile.c,v 1.18 2023/08/10 15:47:05 sashan Exp $	*/

/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * savefile.c - supports offline use of tcpdump
 *	Extraction/creation by Jeffrey Mogul, DECWRL
 *	Modified by Steve McCanne, LBL.
 *
 * Used to save the received packet headers, after filtering, to
 * a file, and then read them later.
 * The first record in the file contains saved values for the machine
 * dependent values so we can print the dump file on any architecture.
 */

#include <sys/types.h>
#include <sys/time.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "pcap-int.h"

#define TCPDUMP_MAGIC 0xa1b2c3d4

/*
 * We use the "receiver-makes-right" approach to byte order,
 * because time is at a premium when we are writing the file.
 * In other words, the pcap_file_header and pcap_pkthdr,
 * records are written in host byte order.
 * Note that the packets are always written in network byte order.
 *
 * ntoh[ls] aren't sufficient because we might need to swap on a big-endian
 * machine (if the file was written in little-end order).
 */
#define	SWAPLONG(y) \
((((y)&0xff)<<24) | (((y)&0xff00)<<8) | (((y)&0xff0000)>>8) | (((y)>>24)&0xff))
#define	SWAPSHORT(y) \
	( (((y)&0xff)<<8) | ((u_short)((y)&0xff00)>>8) )

#define SFERR_TRUNC		1
#define SFERR_BADVERSION	2
#define SFERR_BADF		3
#define SFERR_EOF		4 /* not really an error, just a status */

static int
sf_write_header(FILE *fp, int linktype, int thiszone, int snaplen)
{
	struct pcap_file_header hdr;

	hdr.magic = TCPDUMP_MAGIC;
	hdr.version_major = PCAP_VERSION_MAJOR;
	hdr.version_minor = PCAP_VERSION_MINOR;

	hdr.thiszone = thiszone;
	hdr.snaplen = snaplen;
	hdr.sigfigs = 0;
	hdr.linktype = linktype;

	if (fwrite((char *)&hdr, sizeof(hdr), 1, fp) != 1)
		return (-1);

	return (0);
}

static void
swap_hdr(struct pcap_file_header *hp)
{
	hp->version_major = SWAPSHORT(hp->version_major);
	hp->version_minor = SWAPSHORT(hp->version_minor);
	hp->thiszone = SWAPLONG(hp->thiszone);
	hp->sigfigs = SWAPLONG(hp->sigfigs);
	hp->snaplen = SWAPLONG(hp->snaplen);
	hp->linktype = SWAPLONG(hp->linktype);
}

pcap_t *
pcap_open_offline(const char *fname, char *errbuf)
{
	pcap_t *p;
	FILE *fp;

	if (fname[0] == '-' && fname[1] == '\0')
		fp = stdin;
	else {
		fp = fopen(fname, "r");
		if (fp == NULL) {
			snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s: %s", fname,
			    pcap_strerror(errno));
			return (NULL);
		}
	}
	p = pcap_fopen_offline(fp, errbuf);
	if (p == NULL) {
		if (fp != stdin)
			fclose(fp);
	}
	return (p);
}

pcap_t *
pcap_fopen_offline(FILE *fp, char *errbuf)
{
	pcap_t *p;
	struct pcap_file_header hdr;
	int linklen;

	p = calloc(1, sizeof(*p));
	if (p == NULL) {
		strlcpy(errbuf, "out of swap", PCAP_ERRBUF_SIZE);
		return (NULL);
	}

	/*
	 * Set this field so we don't double-close in pcap_close!
	 */
	p->fd = -1;

	if (fread((char *)&hdr, sizeof(hdr), 1, fp) != 1) {
		snprintf(errbuf, PCAP_ERRBUF_SIZE, "fread: %s",
		    pcap_strerror(errno));
		goto bad;
	}
	if (hdr.magic != TCPDUMP_MAGIC) {
		if (SWAPLONG(hdr.magic) != TCPDUMP_MAGIC) {
			snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "bad dump file format");
			goto bad;
		}
		p->sf.swapped = 1;
		swap_hdr(&hdr);
	}
	if (hdr.version_major < PCAP_VERSION_MAJOR) {
		snprintf(errbuf, PCAP_ERRBUF_SIZE, "archaic file format");
		goto bad;
	}
	p->tzoff = hdr.thiszone;
	p->snapshot = hdr.snaplen;
	/*
	 * Handle some LINKTYPE_ values in pcap headers that aren't
	 * the same as the corresponding OpenBSD DLT_ values.
	 *
	 * Those LINKTYPE_ values were assigned for DLT_s whose
	 * numerical values differ between platforms, so that
	 * the link-layer type value in pcap file headers can
	 * be platform-independent.  This means that code reading
	 * a pcap file doesn't have to know on which platform a
	 * file was written in order to read it correctly.
	 *
	 * See
	 *
	 *     https://www.tcpdump.org/linktypes.html
	 *
	 * for the current list of LINKTYPE_ values and the corresponding
	 * DLT_ values.
	 */
	switch (hdr.linktype) {

	case 100:
		/* LINKTYPE_ATM_RFC1483 */
		p->linktype = DLT_ATM_RFC1483;
		break;

	case 101:
		/* LINKTYPE_RAW */
		p->linktype = DLT_RAW;
		break;

	case 102:
		/* LINKTYPE_SLIP_BSDOS */
		p->linktype = DLT_SLIP_BSDOS;
		break;

	case 103:
		/* LINKTYPE_PPP_BSDOS */
		p->linktype = DLT_PPP_BSDOS;
		break;

	case 108:
		/* LINKTYPE_LOOP */
		p->linktype = DLT_LOOP;
		break;

	case 109:
		/* LINKTYPE_ENC */
		p->linktype = DLT_ENC;
		break;

	case 256:
		/* LINKTYPE_PFSYNC */
		p->linktype = DLT_PFSYNC;
		break;

	default:
		p->linktype = hdr.linktype;
		break;
	}
	p->sf.rfile = fp;
	p->bufsize = hdr.snaplen;

	/* Align link header as required for proper data alignment */
	/* XXX should handle all types */
	switch (p->linktype) {

	case DLT_EN10MB:
		linklen = 14;
		break;

	case DLT_FDDI:
		linklen = 13 + 8;	/* fddi_header + llc */
		break;

	case DLT_NULL:
	default:
		linklen = 0;
		break;
	}

	if (p->bufsize < 0)
		p->bufsize = BPF_MAXBUFSIZE;
	p->sf.base = malloc(p->bufsize + BPF_ALIGNMENT);
	if (p->sf.base == NULL) {
		strlcpy(errbuf, "out of swap", PCAP_ERRBUF_SIZE);
		goto bad;
	}
	p->buffer = p->sf.base + BPF_ALIGNMENT - (linklen % BPF_ALIGNMENT);
	p->sf.version_major = hdr.version_major;
	p->sf.version_minor = hdr.version_minor;
#ifdef PCAP_FDDIPAD
	/* XXX padding only needed for kernel fcode */
	pcap_fddipad = 0;
#endif

	return (p);
 bad:
	free(p);
	return (NULL);
}

/*
 * Read sf_readfile and return the next packet.  Return the header in hdr
 * and the contents in buf.  Return 0 on success, SFERR_EOF if there were
 * no more packets, and SFERR_TRUNC if a partial packet was encountered.
 */
static int
sf_next_packet(pcap_t *p, struct pcap_pkthdr *hdr, u_char *buf, int buflen)
{
	FILE *fp = p->sf.rfile;

	/* read the stamp */
	if (fread((char *)hdr, sizeof(struct pcap_pkthdr), 1, fp) != 1) {
		/* probably an EOF, though could be a truncated packet */
		return (1);
	}

	if (p->sf.swapped) {
		/* these were written in opposite byte order */
		hdr->caplen = SWAPLONG(hdr->caplen);
		hdr->len = SWAPLONG(hdr->len);
		hdr->ts.tv_sec = SWAPLONG(hdr->ts.tv_sec);
		hdr->ts.tv_usec = SWAPLONG(hdr->ts.tv_usec);
	}
	/*
	 * We interchanged the caplen and len fields at version 2.3,
	 * in order to match the bpf header layout.  But unfortunately
	 * some files were written with version 2.3 in their headers
	 * but without the interchanged fields.
	 */
	if (p->sf.version_minor < 3 ||
	    (p->sf.version_minor == 3 && hdr->caplen > hdr->len)) {
		int t = hdr->caplen;
		hdr->caplen = hdr->len;
		hdr->len = t;
	}

	if (hdr->caplen > buflen) {
		/*
		 * This can happen due to Solaris 2.3 systems tripping
		 * over the BUFMOD problem and not setting the snapshot
		 * correctly in the savefile header.  If the caplen isn't
		 * grossly wrong, try to salvage.
		 */
		static u_char *tp = NULL;
		static int tsize = 0;

		if (hdr->caplen > 65535) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "bogus savefile header");
			return (-1);
		}

		if (tsize < hdr->caplen) {
			tsize = ((hdr->caplen + 1023) / 1024) * 1024;
			free(tp);
			tp = malloc(tsize);
			if (tp == NULL) {
				tsize = 0;
				snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				    "BUFMOD hack malloc");
				return (-1);
			}
		}
		if (fread((char *)tp, hdr->caplen, 1, fp) != 1) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "truncated dump file");
			return (-1);
		}
		/*
		 * We can only keep up to buflen bytes.  Since caplen > buflen
		 * is exactly how we got here, we know we can only keep the
		 * first buflen bytes and must drop the remainder.  Adjust
		 * caplen accordingly, so we don't get confused later as
		 * to how many bytes we have to play with.
		 */
		hdr->caplen = buflen;
		memcpy((char *)buf, (char *)tp, buflen);

	} else {
		/* read the packet itself */

		if (fread((char *)buf, hdr->caplen, 1, fp) != 1) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "truncated dump file");
			return (-1);
		}
	}
	return (0);
}

/*
 * Print out packets stored in the file initialized by sf_read_init().
 * If cnt > 0, return after 'cnt' packets, otherwise continue until eof.
 */
int
pcap_offline_read(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	struct bpf_insn *fcode = p->fcode.bf_insns;
	int status = 0;
	int n = 0;

	while (status == 0) {
		struct pcap_pkthdr h;

		/*
		 * Has "pcap_breakloop()" been called?
		 * If so, return immediately - if we haven't read any
		 * packets, clear the flag and return -2 to indicate
		 * that we were told to break out of the loop, otherwise
		 * leave the flag set, so that the *next* call will break
		 * out of the loop without having read any packets, and
		 * return the number of packets we've processed so far.
		 */
		if (p->break_loop) {
			if (n == 0) {
				p->break_loop = 0;
				return (PCAP_ERROR_BREAK);
			} else
				return (n);
		}

		status = sf_next_packet(p, &h, p->buffer, p->bufsize);
		if (status) {
			if (status == 1)
				return (0);
			return (status);
		}

		if (fcode == NULL ||
		    bpf_filter(fcode, p->buffer, h.len, h.caplen)) {
			(*callback)(user, &h, p->buffer);
			if (++n >= cnt && cnt > 0)
				break;
		}
	}
	/*XXX this breaks semantics tcpslice expects */
	return (n);
}

/*
 * Output a packet to the initialized dump file.
 */
void
pcap_dump(u_char *user, const struct pcap_pkthdr *h, const u_char *sp)
{
	FILE *f;

	f = (FILE *)user;
	/* XXX we should check the return status */
	(void)fwrite((char *)h, sizeof(*h), 1, f);
	(void)fwrite((char *)sp, h->caplen, 1, f);
}

static pcap_dumper_t *
pcap_setup_dump(pcap_t *p, FILE *f, const char *fname)
{
	if (sf_write_header(f, p->linktype, p->tzoff, p->snapshot) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "Can't write to %s: %s",
		    fname, pcap_strerror(errno));
		if (f != stdout)
			(void)fclose(f);
		return (NULL);
	}
	return ((pcap_dumper_t *)f);
}

/*
 * Initialize so that sf_write() will output to the file named 'fname'.
 */
pcap_dumper_t *
pcap_dump_open(pcap_t *p, const char *fname)
{
	FILE *f;
	if (fname[0] == '-' && fname[1] == '\0')
		f = stdout;
	else {
		f = fopen(fname, "w");
		if (f == NULL) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "%s: %s",
			    fname, pcap_strerror(errno));
			return (NULL);
		}
	}
	return (pcap_setup_dump(p, f, fname));
}

/*
 * Initialize so that sf_write() will output to the given stream.
 */
pcap_dumper_t *
pcap_dump_fopen(pcap_t *p, FILE *f)
{	
	return (pcap_setup_dump(p, f, "stream"));
}

FILE *
pcap_dump_file(pcap_dumper_t *p)
{
	return ((FILE *)p);
}

long
pcap_dump_ftell(pcap_dumper_t *p)
{
	return (ftell((FILE *)p));
}

int
pcap_dump_flush(pcap_dumper_t *p)
{

	if (fflush((FILE *)p) == EOF)
		return (-1);
	else
		return (0);
}

void
pcap_dump_close(pcap_dumper_t *p)
{

#ifdef notyet
	if (ferror((FILE *)p))
		return-an-error;
	/* XXX should check return from fclose() too */
#endif
	(void)fclose((FILE *)p);
}
