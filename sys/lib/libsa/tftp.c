/*	$OpenBSD: tftp.c,v 1.7 2021/10/25 15:59:46 patrick Exp $	*/
/*	$NetBSD: tftp.c,v 1.15 2003/08/18 15:45:29 dsl Exp $	 */

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
 * Simple TFTP implementation for libsa.
 * Assumes:
 *  - socket descriptor (int) at open_file->f_devdata
 *  - server host IP in global servip
 * Restrictions:
 *  - read only
 *  - lseek only with SEEK_SET or SEEK_CUR
 *  - no big time differences between transfers (<tftp timeout)
 */

/*
 * XXX Does not currently implement:
 * XXX
 * XXX LIBSA_NO_FS_CLOSE
 * XXX LIBSA_NO_FS_SEEK
 * XXX LIBSA_NO_FS_WRITE
 * XXX LIBSA_NO_FS_SYMLINK (does this even make sense?)
 * XXX LIBSA_FS_SINGLECOMPONENT (does this even make sense?)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <lib/libkern/libkern.h>

#include "stand.h"
#include "net.h"
#include "netif.h"

#include "tftp.h"

extern struct in_addr servip;

static int      tftpport = 2000;

#define RSPACE 520		/* max data packet, rounded up */

struct tftp_handle {
	struct iodesc  *iodesc;
	int             currblock;	/* contents of lastdata */
	int             islastblock;	/* flag */
	int             validsize;
	int             off;
	const char     *path;	/* saved for re-requests */
	struct {
		struct packet_header header;
		struct tftphdr t;
		u_char space[RSPACE];
	} lastdata;
};

static const int tftperrors[8] = {
	0,			/* ??? */
	ENOENT,
	EPERM,
	ENOSPC,
	EINVAL,			/* ??? */
	EINVAL,			/* ??? */
	EEXIST,
	EINVAL			/* ??? */
};

ssize_t recvtftp(struct iodesc *, void *, size_t, time_t);
int tftp_makereq(struct tftp_handle *);
int tftp_getnextblock(struct tftp_handle *);
#ifndef TFTP_NOTERMINATE
void tftp_terminate(struct tftp_handle *);
#endif

ssize_t
recvtftp(struct iodesc *d, void *pkt, size_t len, time_t tleft)
{
	ssize_t n;
	struct tftphdr *t;

	errno = 0;

	n = readudp(d, pkt, len, tleft);

	if (n < 4)
		return -1;

	t = (struct tftphdr *) pkt;
	switch (ntohs(t->th_opcode)) {
	case DATA:
		if (htons(t->th_block) != d->xid) {
			/*
			 * Expected block?
			 */
			return -1;
		}
		if (d->xid == 1) {
			/*
			 * First data packet from new port.
			 */
			struct udphdr *uh;
			uh = (struct udphdr *) pkt - 1;
			d->destport = uh->uh_sport;
		} /* else check uh_sport has not changed??? */
		return (n - (t->th_data - (char *)t));
	case ERROR:
		if ((unsigned) ntohs(t->th_code) >= 8) {
			printf("illegal tftp error %d\n", ntohs(t->th_code));
			errno = EIO;
		} else {
#ifdef DEBUG
			printf("tftp-error %d\n", ntohs(t->th_code));
#endif
			errno = tftperrors[ntohs(t->th_code)];
		}
		return -1;
	default:
#ifdef DEBUG
		printf("tftp type %d not handled\n", ntohs(t->th_opcode));
#endif
		return -1;
	}
}

/* send request, expect first block (or error) */
int
tftp_makereq(struct tftp_handle *h)
{
	struct {
		struct packet_header header;
		struct tftphdr  t;
		u_char space[FNAME_SIZE + 6];
	} wbuf;
	char           *wtail;
	int             l;
	ssize_t         res;
	struct tftphdr *t;

	bzero(&wbuf, sizeof(wbuf));

	wbuf.t.th_opcode = htons((u_short) RRQ);
	wtail = wbuf.t.th_stuff;
	l = strlen(h->path);
	bcopy(h->path, wtail, l + 1);
	wtail += l + 1;
	bcopy("octet", wtail, 6);
	wtail += 6;

	t = &h->lastdata.t;

	/* h->iodesc->myport = htons(--tftpport); */
	h->iodesc->myport = htons(tftpport + (getsecs() & 0x3ff));
	h->iodesc->destport = htons(IPPORT_TFTP);
	h->iodesc->xid = 1;	/* expected block */

	res = sendrecv(h->iodesc, sendudp, &wbuf.t, wtail - (char *) &wbuf.t,
	    recvtftp, t, sizeof(*t) + RSPACE);

	if (res == -1)
		return errno;

	h->currblock = 1;
	h->validsize = res;
	h->islastblock = 0;
	if (res < SEGSIZE)
		h->islastblock = 1;	/* very short file */
	return 0;
}

/* ack block, expect next */
int
tftp_getnextblock(struct tftp_handle *h)
{
	struct {
		struct packet_header header;
		struct tftphdr t;
	} wbuf;
	char           *wtail;
	int             res;
	struct tftphdr *t;

	bzero(&wbuf, sizeof(wbuf));

	wbuf.t.th_opcode = htons((u_short) ACK);
	wbuf.t.th_block = htons((u_short) h->currblock);
	wtail = (char *) &wbuf.t.th_data;

	t = &h->lastdata.t;

	h->iodesc->xid = h->currblock + 1;	/* expected block */

	res = sendrecv(h->iodesc, sendudp, &wbuf.t, wtail - (char *) &wbuf.t,
	    recvtftp, t, sizeof(*t) + RSPACE);

	if (res == -1)		/* 0 is OK! */
		return errno;

	h->currblock++;
	h->validsize = res;
	if (res < SEGSIZE)
		h->islastblock = 1;	/* EOF */
	return 0;
}

#ifndef TFTP_NOTERMINATE
void
tftp_terminate(struct tftp_handle *h)
{
	struct {
		struct packet_header header;
		struct tftphdr t;
	} wbuf;
	char           *wtail;

	bzero(&wbuf, sizeof(wbuf));
	wtail = (char *) &wbuf.t.th_data;

	if (h->islastblock) {
		wbuf.t.th_opcode = htons((u_short) ACK);
		wbuf.t.th_block = htons((u_short) h->currblock);
	} else {
		wbuf.t.th_opcode = htons((u_short) ERROR);
		wbuf.t.th_code = htons((u_short) ENOSPACE); /* ??? */
		wtail++; 	/* ERROR data is a string, thus needs NUL. */
	}

	(void) sendudp(h->iodesc, &wbuf.t, wtail - (char *) &wbuf.t);
}
#endif

int
tftp_open(char *path, struct open_file *f)
{
	struct tftp_handle *tftpfile;
	struct iodesc  *io;
	int             res;

	tftpfile = (struct tftp_handle *) alloc(sizeof(*tftpfile));
	if (tftpfile == NULL)
		return ENOMEM;

	tftpfile->iodesc = io = socktodesc(*(int *) (f->f_devdata));
	io->destip = servip;
	tftpfile->off = 0;
	tftpfile->path = path;	/* XXXXXXX we hope it's static */

	res = tftp_makereq(tftpfile);

	if (res) {
		free(tftpfile, sizeof(*tftpfile));
		return res;
	}
	f->f_fsdata = (void *) tftpfile;
	return 0;
}

int
tftp_read(struct open_file *f, void *addr, size_t size, size_t *resid)
{
	struct tftp_handle *tftpfile;
#if !defined(LIBSA_NO_TWIDDLE)
	static int tc = 0;
#endif
	tftpfile = (struct tftp_handle *) f->f_fsdata;

	while (size > 0) {
		int needblock;
		size_t count;

		needblock = tftpfile->off / SEGSIZE + 1;

		if (tftpfile->currblock > needblock) {	/* seek backwards */
#ifndef TFTP_NOTERMINATE
			tftp_terminate(tftpfile);
#endif
			/* Don't bother to check retval: it worked for open() */
			tftp_makereq(tftpfile);
		}

		while (tftpfile->currblock < needblock) {
			int res;

#if !defined(LIBSA_NO_TWIDDLE)
			if ((tc++ % 16) == 0)
				twiddle();
#endif
			res = tftp_getnextblock(tftpfile);
			if (res) {	/* no answer */
#ifdef DEBUG
				printf("tftp: read error (block %d->%d)\n",
				    tftpfile->currblock, needblock);
#endif
				return res;
			}
			if (tftpfile->islastblock)
				break;
		}

		if (tftpfile->currblock == needblock) {
			size_t offinblock, inbuffer;

			offinblock = tftpfile->off % SEGSIZE;

			if (offinblock > tftpfile->validsize) {
#ifdef DEBUG
				printf("tftp: invalid offset %d\n",
				    tftpfile->off);
#endif
				return EINVAL;
			}
			inbuffer = tftpfile->validsize - offinblock;
			count = (size < inbuffer ? size : inbuffer);
			bcopy(tftpfile->lastdata.t.th_data + offinblock,
			    addr, count);

			addr = (caddr_t)addr + count;
			tftpfile->off += count;
			size -= count;

			if ((tftpfile->islastblock) && (count == inbuffer))
				break;	/* EOF */
		} else {
#ifdef DEBUG
			printf("tftp: block %d not found\n", needblock);
#endif
			return EINVAL;
		}

	}

	if (resid != NULL)
		*resid = size;
	return 0;
}

int
tftp_close(struct open_file *f)
{
	struct tftp_handle *tftpfile;
	tftpfile = (struct tftp_handle *) f->f_fsdata;

#ifdef TFTP_NOTERMINATE
	/* let it time out ... */
#else
	tftp_terminate(tftpfile);
#endif

	free(tftpfile, sizeof(*tftpfile));
	return 0;
}

int
tftp_write(struct open_file *f, void *start, size_t size, size_t *resid)
{
	return EROFS;
}

int
tftp_stat(struct open_file *f, struct stat *sb)
{
	sb->st_mode = 0444;
	sb->st_nlink = 1;
	sb->st_uid = 0;
	sb->st_gid = 0;
	sb->st_size = -1;

	return 0;
}

off_t
tftp_seek(struct open_file *f, off_t offset, int where)
{
	struct tftp_handle *tftpfile;
	tftpfile = (struct tftp_handle *) f->f_fsdata;

	switch (where) {
	case SEEK_SET:
		tftpfile->off = offset;
		break;
	case SEEK_CUR:
		tftpfile->off += offset;
		break;
	default:
		errno = EOFFSET;
		return -1;
	}

	return (tftpfile->off);
}

/*
 * Not implemented.
 */
#ifndef NO_READDIR
int
tftp_readdir(struct open_file *f, char *name)
{
	return EROFS;
}
#endif
