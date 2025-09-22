/*	$OpenBSD: privsep_pcap.c,v 1.25 2019/06/28 13:32:51 deraadt Exp $ */

/*
 * Copyright (c) 2004 Can Erkin Acar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pcap-int.h>
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "privsep.h"

/*
 * privileged part of priv_pcap_setfilter, compile the filter
 * expression, and return it to the parent. Note that we fake an hpcap
 * and use it to capture the error messages, and pass the error back
 * to client.
 */
int
setfilter(int bpfd, int sock, char *filter)
{
	struct bpf_program fcode;
	int oflag, snap, link;
	u_int32_t netmask;
	pcap_t hpcap;

	must_read(sock, &oflag, sizeof(oflag));
	must_read(sock, &netmask, sizeof(netmask));
	must_read(sock, &snap, sizeof(snap));
	must_read(sock, &link, sizeof(link));

	if (snap < 0) {
		snprintf(hpcap.errbuf, PCAP_ERRBUF_SIZE, "invalid snaplen");
		goto err;
	}

	/* fake hpcap, it only needs errbuf, snaplen, and linktype to
	 * compile a filter expression */
	/* XXX messing with pcap internals */
	hpcap.snapshot = snap;
	hpcap.linktype = link;
	if (pcap_compile(&hpcap, &fcode, filter, oflag, netmask))
		goto err;

	/* if bpf descriptor is open, set the filter XXX check oflag? */
	if (bpfd >= 0 && ioctl(bpfd, BIOCSETF, &fcode) == -1) {
		snprintf(hpcap.errbuf, PCAP_ERRBUF_SIZE,
		    "ioctl: BIOCSETF: %s", strerror(errno));
		pcap_freecode(&fcode);
		goto err;
	}
	if (fcode.bf_len > 0) {
		/* write the filter */
		must_write(sock, &fcode.bf_len, sizeof(fcode.bf_len));
		must_write(sock, fcode.bf_insns,
		    fcode.bf_len * sizeof(struct bpf_insn));
	} else {
		snprintf(hpcap.errbuf, PCAP_ERRBUF_SIZE, "Invalid filter size");
		pcap_freecode(&fcode);
		goto err;
	}


	pcap_freecode(&fcode);
	return (0);

 err:
	fcode.bf_len = 0;
	must_write(sock, &fcode.bf_len, sizeof(fcode.bf_len));

	/* write back the error string */
	write_string(sock, hpcap.errbuf);
	return (1);
}

/*
 * filter is compiled and set in the privileged process.
 * get the compiled output and set it locally for filtering dumps etc.
 */
struct bpf_program *
priv_pcap_setfilter(pcap_t *hpcap, int oflag, u_int32_t netmask)
{
	struct bpf_program *fcode = NULL;
	int snap, link;
	char *ebuf;

	if (priv_fd < 0)
		errx(1, "%s: called from privileged portion", __func__);

	ebuf = pcap_geterr(hpcap);
	snap = pcap_snapshot(hpcap);
	link = pcap_datalink(hpcap);

	fcode = calloc(1, sizeof(*fcode));
	if (fcode == NULL) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "out of memory");
		return (NULL);
	}

	write_command(priv_fd, PRIV_SETFILTER);

	/* send oflag, netmask, snaplen and linktype */
	must_write(priv_fd, &oflag, sizeof(oflag));
	must_write(priv_fd, &netmask, sizeof(netmask));
	must_write(priv_fd, &snap, sizeof(snap));
	must_write(priv_fd, &link, sizeof(link));

	/* receive compiled filter */
	must_read(priv_fd, &fcode->bf_len, sizeof(fcode->bf_len));
	if (fcode->bf_len <= 0) {
		int len;

		len = read_string(priv_fd, ebuf, PCAP_ERRBUF_SIZE, __func__);
		if (len == 0)
			snprintf(ebuf, PCAP_ERRBUF_SIZE, "pcap compile error");
		goto err;
	}

	fcode->bf_insns = calloc(fcode->bf_len, sizeof(struct bpf_insn));
	if (fcode->bf_insns == NULL) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "out of memory");
		goto err;
	}

	must_read(priv_fd, fcode->bf_insns,
	    fcode->bf_len * sizeof(struct bpf_insn));

	pcap_setfilter(hpcap, fcode);
	return (fcode);

 err:
	free(fcode);
	return (NULL);
}


/* privileged part of priv_pcap_live */
int
pcap_live(const char *device, int snaplen, int promisc, u_int dlt,
    u_int dirfilt, u_int fildrop)
{
	int		fd;
	struct ifreq	ifr;
	unsigned	v;

	if (device == NULL || snaplen <= 0)
		return (-1);

	if ((fd = open("/dev/bpf", O_RDONLY)) == -1)
		return (-1);

	v = 32768;	/* XXX this should be a user-accessible hook */
	ioctl(fd, BIOCSBLEN, &v);

	strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
	if (ioctl(fd, BIOCSETIF, &ifr) == -1)
		goto error;

	if (dlt != (u_int) -1 && ioctl(fd, BIOCSDLT, &dlt) == -1)
		goto error;

	if (promisc)
		/* this is allowed to fail */
		ioctl(fd, BIOCPROMISC, NULL);
	if (ioctl(fd, BIOCSDIRFILT, &dirfilt) == -1)
		goto error;

	if (ioctl(fd, BIOCSFILDROP, &fildrop) == -1)
		goto error;

	/* lock the descriptor */
	if (ioctl(fd, BIOCLOCK, NULL) == -1)
		goto error;
	return (fd);

 error:
	close(fd);
	return (-1);
}


/*
 * XXX reimplement pcap_open_live with privsep, this is the
 * unprivileged part.
 */
pcap_t *
priv_pcap_live(const char *dev, int slen, int prom, int to_ms,
    char *ebuf, u_int dlt, u_int dirfilt, u_int fildrop)
{
	int fd, err;
	struct bpf_version bv;
	u_int v;
	pcap_t *p;

	if (priv_fd < 0)
		errx(1, "%s: called from privileged portion", __func__);

	if (dev == NULL) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "No interface specified");
		return (NULL);
	}

	p = malloc(sizeof(*p));
	if (p == NULL) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "malloc: %s",
		    pcap_strerror(errno));
		return (NULL);
	}

	bzero(p, sizeof(*p));

	write_command(priv_fd, PRIV_OPEN_BPF);
	must_write(priv_fd, &slen, sizeof(int));
	must_write(priv_fd, &prom, sizeof(int));
	must_write(priv_fd, &dlt, sizeof(u_int));
	must_write(priv_fd, &dirfilt, sizeof(u_int));
	must_write(priv_fd, &fildrop, sizeof(fildrop));
	write_string(priv_fd, dev);

	fd = receive_fd(priv_fd);
	must_read(priv_fd, &err, sizeof(int));
	if (fd < 0) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "Failed to open bpf device for %s: %s",
		    dev, strerror(err));
		goto bad;
	}

	/* fd is locked, can only use 'safe' ioctls */
	if (ioctl(fd, BIOCVERSION, &bv) == -1) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCVERSION: %s",
		    pcap_strerror(errno));
		goto bad;
	}

	if (bv.bv_major != BPF_MAJOR_VERSION ||
	    bv.bv_minor < BPF_MINOR_VERSION) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "kernel bpf filter out of date");
		goto bad;
	}

	p->fd = fd;
	p->snapshot = slen;

	/* Get the data link layer type. */
	if (ioctl(fd, BIOCGDLT, &v) == -1) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCGDLT: %s",
		    pcap_strerror(errno));
		goto bad;
	}
	p->linktype = v;

	/* XXX hack */
	if (p->linktype == DLT_PFLOG && p->snapshot < 160)
		p->snapshot = 160;

	/* set timeout */
	if (to_ms != 0) {
		struct timeval to;
		to.tv_sec = to_ms / 1000;
		to.tv_usec = (to_ms * 1000) % 1000000;
		if (ioctl(p->fd, BIOCSRTIMEOUT, &to) == -1) {
			snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCSRTIMEOUT: %s",
			    pcap_strerror(errno));
			goto bad;
		}
	}

	if (ioctl(fd, BIOCGBLEN, &v) == -1) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCGBLEN: %s",
		    pcap_strerror(errno));
		goto bad;
	}
	p->bufsize = v;
	p->buffer = malloc(p->bufsize);
	if (p->buffer == NULL) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "malloc: %s",
		    pcap_strerror(errno));
		goto bad;
	}
	return (p);

 bad:
	if (fd >= 0)
		close(fd);
	free(p);
	return (NULL);
}



/*
 * reimplement pcap_open_offline with privsep, this is the
 * unprivileged part.
 * XXX merge with above?
 */
static void
swap_hdr(struct pcap_file_header *hp)
{
	hp->version_major = swap16(hp->version_major);
	hp->version_minor = swap16(hp->version_minor);
	hp->thiszone = swap32(hp->thiszone);
	hp->sigfigs = swap32(hp->sigfigs);
	hp->snaplen = swap32(hp->snaplen);
	hp->linktype = swap32(hp->linktype);
}

pcap_t *
priv_pcap_offline(const char *fname, char *errbuf)
{
	pcap_t *p;
	FILE *fp = NULL;
	struct pcap_file_header hdr;
	int linklen, err;

	if (priv_fd < 0)
		errx(1, "%s: called from privileged portion", __func__);

	p = malloc(sizeof(*p));
	if (p == NULL) {
		strlcpy(errbuf, "out of swap", PCAP_ERRBUF_SIZE);
		return (NULL);
	}

	memset((char *)p, 0, sizeof(*p));

	if (fname[0] == '-' && fname[1] == '\0') {
		p->fd = -1;
		fp = stdin;
	} else {
		write_command(priv_fd, PRIV_OPEN_DUMP);
		p->fd = receive_fd(priv_fd);
		must_read(priv_fd, &err, sizeof(int));
		if (p->fd < 0) {
			snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "Failed to open input file %s: %s",
			    fname, strerror(err));
			goto bad;
		}

		fp = fdopen(p->fd, "r");
		if (fp == NULL) {
			snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s: %s", fname,
			    pcap_strerror(errno));
			close(p->fd);
			p->fd = -1;
			goto bad;
		}
	}
	if (fread((char *)&hdr, sizeof(hdr), 1, fp) != 1) {
		snprintf(errbuf, PCAP_ERRBUF_SIZE, "fread: %s",
		    pcap_strerror(errno));
		goto bad;
	}

	if (hdr.magic != TCPDUMP_MAGIC) {
		if (swap32(hdr.magic) != TCPDUMP_MAGIC) {
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
	p->linktype = hdr.linktype;
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
	/* XXX what to do with this? */
	/* XXX padding only needed for kernel fcode */
	pcap_fddipad = 0;
#endif
	return (p);

 bad:
	if (fp != NULL && p->fd != -1)
		fclose(fp);
	free(p);
	return (NULL);
}


static int
sf_write_header(FILE *fp, int linktype, int thiszone, int snaplen)
{
	struct pcap_file_header hdr;

	bzero(&hdr, sizeof hdr);
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

pcap_dumper_t *
priv_pcap_dump_open(pcap_t *p, char *fname)
{
	int fd, err;
	FILE *f;

	if (priv_fd < 0)
		errx(1, "%s: called from privileged portion", __func__);

	if (fname[0] == '-' && fname[1] == '\0')
		f = stdout;
	else {
		write_command(priv_fd, PRIV_OPEN_OUTPUT);
		fd = receive_fd(priv_fd);
		must_read(priv_fd, &err, sizeof(err));
		if (fd < 0)  {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "Failed to open output file %s: %s",
			    fname, strerror(err));
			return (NULL);
		}
		f = fdopen(fd, "w");
		if (f == NULL) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "%s: %s",
			    fname, pcap_strerror(errno));
			close(fd);
			return (NULL);
		}
	}
	priv_init_done();

	(void)sf_write_header(f, p->linktype, p->tzoff, p->snapshot);
	return ((pcap_dumper_t *)f);
}
