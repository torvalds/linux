/*	$NetBSD: tftp.c,v 1.4 1997/09/17 16:57:07 drochner Exp $	 */

/*
 * Copyright (c) 1996
 *	Matthias Drochner.  All rights reserved.
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
 *	This product includes software developed for the NetBSD Project
 *	by Matthias Drochner.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Simple TFTP implementation for libsa.
 * Assumes:
 *  - socket descriptor (int) at open_file->f_devdata
 *  - server host IP in global servip
 * Restrictions:
 *  - read only
 *  - lseek only with SEEK_SET or SEEK_CUR
 *  - no big time differences between transfers (<tftp timeout)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/in_systm.h>
#include <arpa/tftp.h>

#include <string.h>

#include "stand.h"
#include "net.h"
#include "netif.h"

#include "tftp.h"

struct tftp_handle;
struct tftprecv_extra;

static ssize_t recvtftp(struct iodesc *, void **, void **, time_t, void *);
static int tftp_open(const char *, struct open_file *);
static int tftp_close(struct open_file *);
static int tftp_parse_oack(struct tftp_handle *, char *, size_t);
static int tftp_read(struct open_file *, void *, size_t, size_t *);
static off_t tftp_seek(struct open_file *, off_t, int);
static int tftp_set_blksize(struct tftp_handle *, const char *);
static int tftp_stat(struct open_file *, struct stat *);

struct fs_ops tftp_fsops = {
	.fs_name = "tftp",
	.fo_open = tftp_open,
	.fo_close = tftp_close,
	.fo_read = tftp_read,
	.fo_write = null_write,
	.fo_seek = tftp_seek,
	.fo_stat = tftp_stat,
	.fo_readdir = null_readdir
};

extern struct in_addr servip;

static int	tftpport = 2000;
static int	is_open = 0;

/*
 * The legacy TFTP_BLKSIZE value was SEGSIZE(512).
 * TFTP_REQUESTED_BLKSIZE of 1428 is (Ethernet MTU, less the TFTP, UDP and
 * IP header lengths).
 */
#define	TFTP_REQUESTED_BLKSIZE 1428

/*
 * Choose a blksize big enough so we can test with Ethernet
 * Jumbo frames in the future.
 */
#define	TFTP_MAX_BLKSIZE 9008

struct tftp_handle {
	struct iodesc  *iodesc;
	int		currblock;	/* contents of lastdata */
	int		islastblock;	/* flag */
	int		validsize;
	int		off;
	char		*path;	/* saved for re-requests */
	unsigned int	tftp_blksize;
	unsigned long	tftp_tsize;
	void		*pkt;
	struct tftphdr	*tftp_hdr;
};

struct tftprecv_extra {
	struct tftp_handle	*tftp_handle;
	unsigned short		rtype;		/* Received type */
};

#define	TFTP_MAX_ERRCODE EOPTNEG
static const int tftperrors[TFTP_MAX_ERRCODE + 1] = {
	0,			/* ??? */
	ENOENT,
	EPERM,
	ENOSPC,
	EINVAL,			/* ??? */
	EINVAL,			/* ??? */
	EEXIST,
	EINVAL,			/* ??? */
	EINVAL,			/* Option negotiation failed. */
};

static int  tftp_getnextblock(struct tftp_handle *h);

/* send error message back. */
static void
tftp_senderr(struct tftp_handle *h, u_short errcode, const char *msg)
{
	struct {
		u_char header[HEADER_SIZE];
		struct tftphdr t;
		u_char space[63]; /* +1 from t */
	} __packed __aligned(4) wbuf;
	char *wtail;
	int len;

	len = strlen(msg);
	if (len > sizeof(wbuf.space))
		len = sizeof(wbuf.space);

	wbuf.t.th_opcode = htons((u_short)ERROR);
	wbuf.t.th_code = htons(errcode);

	wtail = wbuf.t.th_msg;
	bcopy(msg, wtail, len);
	wtail[len] = '\0';
	wtail += len + 1;

	sendudp(h->iodesc, &wbuf.t, wtail - (char *)&wbuf.t);
}

static void
tftp_sendack(struct tftp_handle *h, u_short block)
{
	struct {
		u_char header[HEADER_SIZE];
		struct tftphdr  t;
	} __packed __aligned(4) wbuf;
	char *wtail;

	wbuf.t.th_opcode = htons((u_short)ACK);
	wtail = (char *)&wbuf.t.th_block;
	wbuf.t.th_block = htons(block);
	wtail += 2;

	sendudp(h->iodesc, &wbuf.t, wtail - (char *)&wbuf.t);
}

static ssize_t
recvtftp(struct iodesc *d, void **pkt, void **payload, time_t tleft,
    void *recv_extra)
{
	struct tftprecv_extra *extra;
	struct tftp_handle *h;
	struct tftphdr *t;
	void *ptr = NULL;
	ssize_t len;

	errno = 0;
	extra = recv_extra;
	h = extra->tftp_handle;

	len = readudp(d, &ptr, (void **)&t, tleft);

	if (len < 4) {
		free(ptr);
		return (-1);
	}

	extra->rtype = ntohs(t->th_opcode);
	switch (ntohs(t->th_opcode)) {
	case DATA: {
		int got;

		if (htons(t->th_block) < (u_short)d->xid) {
			/*
			 * Apparently our ACK was missed, re-send.
			 */
			tftp_sendack(h, htons(t->th_block));
			free(ptr);
			return (-1);
		}
		if (htons(t->th_block) != (u_short)d->xid) {
			/*
			 * Packet from the future, drop this.
			 */
			free(ptr);
			return (-1);
		}
		if (d->xid == 1) {
			/*
			 * First data packet from new port.
			 */
			struct udphdr *uh;
			uh = (struct udphdr *)t - 1;
			d->destport = uh->uh_sport;
		}
		got = len - (t->th_data - (char *)t);
		*pkt = ptr;
		*payload = t;
		return (got);
	}
	case ERROR:
		if ((unsigned)ntohs(t->th_code) > TFTP_MAX_ERRCODE) {
			printf("illegal tftp error %d\n", ntohs(t->th_code));
			errno = EIO;
		} else {
#ifdef TFTP_DEBUG
			printf("tftp-error %d\n", ntohs(t->th_code));
#endif
			errno = tftperrors[ntohs(t->th_code)];
		}
		free(ptr);
		return (-1);
	case OACK: {
		struct udphdr *uh;
		int tftp_oack_len;

		/*
		 * Unexpected OACK. TFTP transfer already in progress.
		 * Drop the pkt.
		 */
		if (d->xid != 1) {
			free(ptr);
			return (-1);
		}

		/*
		 * Remember which port this OACK came from, because we need
		 * to send the ACK or errors back to it.
		 */
		uh = (struct udphdr *)t - 1;
		d->destport = uh->uh_sport;

		/* Parse options ACK-ed by the server. */
		tftp_oack_len = len - sizeof(t->th_opcode);
		if (tftp_parse_oack(h, t->th_u.tu_stuff, tftp_oack_len) != 0) {
			tftp_senderr(h, EOPTNEG, "Malformed OACK");
			errno = EIO;
			free(ptr);
			return (-1);
		}
		*pkt = ptr;
		*payload = t;
		return (0);
	}
	default:
#ifdef TFTP_DEBUG
		printf("tftp type %d not handled\n", ntohs(t->th_opcode));
#endif
		free(ptr);
		return (-1);
	}
}

/* send request, expect first block (or error) */
static int
tftp_makereq(struct tftp_handle *h)
{
	struct {
		u_char header[HEADER_SIZE];
		struct tftphdr  t;
		u_char space[FNAME_SIZE + 6];
	} __packed __aligned(4) wbuf;
	struct tftprecv_extra recv_extra;
	char *wtail;
	int l;
	ssize_t res;
	void *pkt;
	struct tftphdr *t;
	char *tftp_blksize = NULL;
	int blksize_l;

	/*
	 * Allow overriding default TFTP block size by setting
	 * a tftp.blksize environment variable.
	 */
	if ((tftp_blksize = getenv("tftp.blksize")) != NULL) {
		tftp_set_blksize(h, tftp_blksize);
	}

	wbuf.t.th_opcode = htons((u_short)RRQ);
	wtail = wbuf.t.th_stuff;
	l = strlen(h->path);
#ifdef TFTP_PREPEND_PATH
	if (l > FNAME_SIZE - (sizeof(TFTP_PREPEND_PATH) - 1))
		return (ENAMETOOLONG);
	bcopy(TFTP_PREPEND_PATH, wtail, sizeof(TFTP_PREPEND_PATH) - 1);
	wtail += sizeof(TFTP_PREPEND_PATH) - 1;
#else
	if (l > FNAME_SIZE)
		return (ENAMETOOLONG);
#endif
	bcopy(h->path, wtail, l + 1);
	wtail += l + 1;
	bcopy("octet", wtail, 6);
	wtail += 6;
	bcopy("blksize", wtail, 8);
	wtail += 8;
	blksize_l = sprintf(wtail, "%d", h->tftp_blksize);
	wtail += blksize_l + 1;
	bcopy("tsize", wtail, 6);
	wtail += 6;
	bcopy("0", wtail, 2);
	wtail += 2;

	h->iodesc->myport = htons(tftpport + (getsecs() & 0x3ff));
	h->iodesc->destport = htons(IPPORT_TFTP);
	h->iodesc->xid = 1;	/* expected block */

	h->currblock = 0;
	h->islastblock = 0;
	h->validsize = 0;

	pkt = NULL;
	recv_extra.tftp_handle = h;
	res = sendrecv(h->iodesc, &sendudp, &wbuf.t, wtail - (char *)&wbuf.t,
	    &recvtftp, &pkt, (void **)&t, &recv_extra);
	if (res == -1) {
		free(pkt);
		return (errno);
	}

	free(h->pkt);
	h->pkt = pkt;
	h->tftp_hdr = t;

	if (recv_extra.rtype == OACK)
		return (tftp_getnextblock(h));

	/* Server ignored our blksize request, revert to TFTP default. */
	h->tftp_blksize = SEGSIZE;

	switch (recv_extra.rtype) {
		case DATA: {
			h->currblock = 1;
			h->validsize = res;
			h->islastblock = 0;
			if (res < h->tftp_blksize) {
				h->islastblock = 1;	/* very short file */
				tftp_sendack(h, h->currblock);
			}
			return (0);
		}
		case ERROR:
		default:
			return (errno);
	}

}

/* ack block, expect next */
static int
tftp_getnextblock(struct tftp_handle *h)
{
	struct {
		u_char header[HEADER_SIZE];
		struct tftphdr t;
	} __packed __aligned(4) wbuf;
	struct tftprecv_extra recv_extra;
	char *wtail;
	int res;
	void *pkt;
	struct tftphdr *t;

	wbuf.t.th_opcode = htons((u_short)ACK);
	wtail = (char *)&wbuf.t.th_block;
	wbuf.t.th_block = htons((u_short)h->currblock);
	wtail += 2;

	h->iodesc->xid = h->currblock + 1;	/* expected block */

	pkt = NULL;
	recv_extra.tftp_handle = h;
	res = sendrecv(h->iodesc, &sendudp, &wbuf.t, wtail - (char *)&wbuf.t,
	    &recvtftp, &pkt, (void **)&t, &recv_extra);

	if (res == -1) {		/* 0 is OK! */
		free(pkt);
		return (errno);
	}

	free(h->pkt);
	h->pkt = pkt;
	h->tftp_hdr = t;
	h->currblock++;
	h->validsize = res;
	if (res < h->tftp_blksize)
		h->islastblock = 1;	/* EOF */

	if (h->islastblock == 1) {
		/* Send an ACK for the last block */
		wbuf.t.th_block = htons((u_short)h->currblock);
		sendudp(h->iodesc, &wbuf.t, wtail - (char *)&wbuf.t);
	}

	return (0);
}

static int
tftp_open(const char *path, struct open_file *f)
{
	struct tftp_handle *tftpfile;
	struct iodesc	*io;
	int		res;
	size_t		pathsize;
	const char	*extraslash;

	if (netproto != NET_TFTP)
		return (EINVAL);

	if (f->f_dev->dv_type != DEVT_NET)
		return (EINVAL);

	if (is_open)
		return (EBUSY);

	tftpfile = calloc(1, sizeof(*tftpfile));
	if (!tftpfile)
		return (ENOMEM);

	tftpfile->tftp_blksize = TFTP_REQUESTED_BLKSIZE;
	tftpfile->iodesc = io = socktodesc(*(int *)(f->f_devdata));
	if (io == NULL) {
		free(tftpfile);
		return (EINVAL);
	}

	io->destip = servip;
	tftpfile->off = 0;
	pathsize = (strlen(rootpath) + 1 + strlen(path) + 1) * sizeof(char);
	tftpfile->path = malloc(pathsize);
	if (tftpfile->path == NULL) {
		free(tftpfile);
		return (ENOMEM);
	}
	if (rootpath[strlen(rootpath) - 1] == '/' || path[0] == '/')
		extraslash = "";
	else
		extraslash = "/";
	res = snprintf(tftpfile->path, pathsize, "%s%s%s",
	    rootpath, extraslash, path);
	if (res < 0 || res > pathsize) {
		free(tftpfile->path);
		free(tftpfile);
		return (ENOMEM);
	}

	res = tftp_makereq(tftpfile);

	if (res) {
		free(tftpfile->path);
		free(tftpfile->pkt);
		free(tftpfile);
		return (res);
	}
	f->f_fsdata = tftpfile;
	is_open = 1;
	return (0);
}

static int
tftp_read(struct open_file *f, void *addr, size_t size,
    size_t *resid /* out */)
{
	struct tftp_handle *tftpfile;
	size_t res;
	int rc;

	rc = 0;
	res = size;
	tftpfile = f->f_fsdata;

	/* Make sure we will not read past file end */
	if (tftpfile->tftp_tsize > 0 &&
	    tftpfile->off + size > tftpfile->tftp_tsize) {
		size = tftpfile->tftp_tsize - tftpfile->off;
	}

	while (size > 0) {
		int needblock, count;

		twiddle(32);

		needblock = tftpfile->off / tftpfile->tftp_blksize + 1;

		if (tftpfile->currblock > needblock) {	/* seek backwards */
			tftp_senderr(tftpfile, 0, "No error: read aborted");
			rc = tftp_makereq(tftpfile);
			if (rc != 0)
				break;
		}

		while (tftpfile->currblock < needblock) {

			rc = tftp_getnextblock(tftpfile);
			if (rc) {	/* no answer */
#ifdef TFTP_DEBUG
				printf("tftp: read error\n");
#endif
				return (rc);
			}
			if (tftpfile->islastblock)
				break;
		}

		if (tftpfile->currblock == needblock) {
			int offinblock, inbuffer;

			offinblock = tftpfile->off % tftpfile->tftp_blksize;

			inbuffer = tftpfile->validsize - offinblock;
			if (inbuffer < 0) {
#ifdef TFTP_DEBUG
				printf("tftp: invalid offset %d\n",
				    tftpfile->off);
#endif
				return (EINVAL);
			}
			count = (size < inbuffer ? size : inbuffer);
			bcopy(tftpfile->tftp_hdr->th_data + offinblock,
			    addr, count);

			addr = (char *)addr + count;
			tftpfile->off += count;
			size -= count;
			res -= count;

			if ((tftpfile->islastblock) && (count == inbuffer))
				break;	/* EOF */
		} else {
#ifdef TFTP_DEBUG
			printf("tftp: block %d not found\n", needblock);
#endif
			return (EINVAL);
		}

	}

	if (resid != NULL)
		*resid = res;
	return (rc);
}

static int
tftp_close(struct open_file *f)
{
	struct tftp_handle *tftpfile;
	tftpfile = f->f_fsdata;

	/* let it time out ... */

	if (tftpfile) {
		free(tftpfile->path);
		free(tftpfile->pkt);
		free(tftpfile);
	}
	is_open = 0;
	return (0);
}

static int
tftp_stat(struct open_file *f, struct stat *sb)
{
	struct tftp_handle *tftpfile;
	tftpfile = f->f_fsdata;

	sb->st_mode = 0444 | S_IFREG;
	sb->st_nlink = 1;
	sb->st_uid = 0;
	sb->st_gid = 0;
	sb->st_size = tftpfile->tftp_tsize;
	return (0);
}

static off_t
tftp_seek(struct open_file *f, off_t offset, int where)
{
	struct tftp_handle *tftpfile;
	tftpfile = f->f_fsdata;

	switch (where) {
	case SEEK_SET:
		tftpfile->off = offset;
		break;
	case SEEK_CUR:
		tftpfile->off += offset;
		break;
	default:
		errno = EOFFSET;
		return (-1);
	}
	return (tftpfile->off);
}

static int
tftp_set_blksize(struct tftp_handle *h, const char *str)
{
	char *endptr;
	int new_blksize;
	int ret = 0;

	if (h == NULL || str == NULL)
		return (ret);

	new_blksize =
	    (unsigned int)strtol(str, &endptr, 0);

	/*
	 * Only accept blksize value if it is numeric.
	 * RFC2348 specifies that acceptable values are 8-65464.
	 * Let's choose a limit less than MAXRSPACE.
	 */
	if (*endptr == '\0' && new_blksize >= 8 &&
	    new_blksize <= TFTP_MAX_BLKSIZE) {
		h->tftp_blksize = new_blksize;
		ret = 1;
	}

	return (ret);
}

/*
 * In RFC2347, the TFTP Option Acknowledgement package (OACK)
 * is used to acknowledge a client's option negotiation request.
 * The format of an OACK packet is:
 *    +-------+---~~---+---+---~~---+---+---~~---+---+---~~---+---+
 *    |  opc  |  opt1  | 0 | value1 | 0 |  optN  | 0 | valueN | 0 |
 *    +-------+---~~---+---+---~~---+---+---~~---+---+---~~---+---+
 *
 *    opc
 *       The opcode field contains a 6, for Option Acknowledgment.
 *
 *    opt1
 *       The first option acknowledgment, copied from the original
 *       request.
 *
 *    value1
 *       The acknowledged value associated with the first option.  If
 *       and how this value may differ from the original request is
 *       detailed in the specification for the option.
 *
 *    optN, valueN
 *       The final option/value acknowledgment pair.
 */
static int
tftp_parse_oack(struct tftp_handle *h, char *buf, size_t len)
{
	/*
	 *  We parse the OACK strings into an array
	 *  of name-value pairs.
	 */
	char *tftp_options[128] = { 0 };
	char *val = buf;
	int i = 0;
	int option_idx = 0;
	int blksize_is_set = 0;
	int tsize = 0;

	unsigned int orig_blksize;

	while (option_idx < 128 && i < len) {
		if (buf[i] == '\0') {
			if (&buf[i] > val) {
				tftp_options[option_idx] = val;
				val = &buf[i] + 1;
				++option_idx;
			}
		}
		++i;
	}

	/* Save the block size we requested for sanity check later. */
	orig_blksize = h->tftp_blksize;

	/*
	 * Parse individual TFTP options.
	 *    * "blksize" is specified in RFC2348.
	 *    * "tsize" is specified in RFC2349.
	 */
	for (i = 0; i < option_idx; i += 2) {
		if (strcasecmp(tftp_options[i], "blksize") == 0) {
			if (i + 1 < option_idx)
				blksize_is_set =
				    tftp_set_blksize(h, tftp_options[i + 1]);
		} else if (strcasecmp(tftp_options[i], "tsize") == 0) {
			if (i + 1 < option_idx)
				tsize = strtol(tftp_options[i + 1], NULL, 10);
			if (tsize != 0)
				h->tftp_tsize = tsize;
		} else {
			/*
			 * Do not allow any options we did not expect to be
			 * ACKed.
			 */
			printf("unexpected tftp option '%s'\n",
			    tftp_options[i]);
			return (-1);
		}
	}

	if (!blksize_is_set) {
		/*
		 * If TFTP blksize was not set, try defaulting
		 * to the legacy TFTP blksize of SEGSIZE(512)
		 */
		h->tftp_blksize = SEGSIZE;
	} else if (h->tftp_blksize > orig_blksize) {
		/*
		 * Server should not be proposing block sizes that
		 * exceed what we said we can handle.
		 */
		printf("unexpected blksize %u\n", h->tftp_blksize);
		return (-1);
	}

#ifdef TFTP_DEBUG
	printf("tftp_blksize: %u\n", h->tftp_blksize);
	printf("tftp_tsize: %lu\n", h->tftp_tsize);
#endif
	return (0);
}
