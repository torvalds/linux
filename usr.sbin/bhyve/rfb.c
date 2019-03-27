/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * Copyright (c) 2015 Leon Dang
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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

#include <sys/param.h>
#ifndef WITHOUT_CAPSICUM
#include <sys/capsicum.h>
#endif
#include <sys/endian.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <netinet/in.h>
#include <netdb.h>

#include <assert.h>
#ifndef WITHOUT_CAPSICUM
#include <capsicum_helpers.h>
#endif
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <zlib.h>

#include "bhyvegc.h"
#include "console.h"
#include "rfb.h"
#include "sockstream.h"

#ifndef NO_OPENSSL
#include <openssl/des.h>
#endif

static int rfb_debug = 0;
#define	DPRINTF(params) if (rfb_debug) printf params
#define	WPRINTF(params) printf params

#define AUTH_LENGTH	16
#define PASSWD_LENGTH	8

#define SECURITY_TYPE_NONE	1
#define SECURITY_TYPE_VNC_AUTH	2

#define AUTH_FAILED_UNAUTH	1
#define AUTH_FAILED_ERROR	2

struct rfb_softc {
	int		sfd;
	pthread_t	tid;

	int		cfd;

	int		width, height;

	char		*password;

	bool	enc_raw_ok;
	bool	enc_zlib_ok;
	bool	enc_resize_ok;

	z_stream	zstream;
	uint8_t		*zbuf;
	int		zbuflen;

	int		conn_wait;
	int		sending;
	pthread_mutex_t mtx;
	pthread_cond_t  cond;

	int		hw_crc;
	uint32_t	*crc;		/* WxH crc cells */
	uint32_t	*crc_tmp;	/* buffer to store single crc row */
	int		crc_width, crc_height;
};

struct rfb_pixfmt {
	uint8_t		bpp;
	uint8_t		depth;
	uint8_t		bigendian;
	uint8_t		truecolor;
	uint16_t	red_max;
	uint16_t	green_max;
	uint16_t	blue_max;
	uint8_t		red_shift;
	uint8_t		green_shift;
	uint8_t		blue_shift;
	uint8_t		pad[3];
};

struct rfb_srvr_info {
	uint16_t		width;
	uint16_t		height;
	struct rfb_pixfmt	pixfmt;
	uint32_t		namelen;
};

struct rfb_pixfmt_msg {
	uint8_t			type;
	uint8_t			pad[3];
	struct rfb_pixfmt	pixfmt;
};

#define	RFB_ENCODING_RAW		0
#define	RFB_ENCODING_ZLIB		6
#define	RFB_ENCODING_RESIZE		-223

#define	RFB_MAX_WIDTH			2000
#define	RFB_MAX_HEIGHT			1200
#define	RFB_ZLIB_BUFSZ			RFB_MAX_WIDTH*RFB_MAX_HEIGHT*4

/* percentage changes to screen before sending the entire screen */
#define	RFB_SEND_ALL_THRESH		25

struct rfb_enc_msg {
	uint8_t		type;
	uint8_t		pad;
	uint16_t	numencs;
};

struct rfb_updt_msg {
	uint8_t		type;
	uint8_t		incremental;
	uint16_t	x;
	uint16_t	y;
	uint16_t	width;
	uint16_t	height;
};

struct rfb_key_msg {
	uint8_t		type;
	uint8_t		down;
	uint16_t	pad;
	uint32_t	code;
};

struct rfb_ptr_msg {
	uint8_t		type;
	uint8_t		button;
	uint16_t	x;
	uint16_t	y;
};

struct rfb_srvr_updt_msg {
	uint8_t		type;
	uint8_t		pad;
	uint16_t	numrects;
};

struct rfb_srvr_rect_hdr {
	uint16_t	x;
	uint16_t	y;
	uint16_t	width;
	uint16_t	height;
	uint32_t	encoding;
};

struct rfb_cuttext_msg {
	uint8_t		type;
	uint8_t		padding[3];
	uint32_t	length;
};


static void
rfb_send_server_init_msg(int cfd)
{
	struct bhyvegc_image *gc_image;
	struct rfb_srvr_info sinfo;

	gc_image = console_get_image();

	sinfo.width = htons(gc_image->width);
	sinfo.height = htons(gc_image->height);
	sinfo.pixfmt.bpp = 32;
	sinfo.pixfmt.depth = 32;
	sinfo.pixfmt.bigendian = 0;
	sinfo.pixfmt.truecolor = 1;
	sinfo.pixfmt.red_max = htons(255);
	sinfo.pixfmt.green_max = htons(255);
	sinfo.pixfmt.blue_max = htons(255);
	sinfo.pixfmt.red_shift = 16;
	sinfo.pixfmt.green_shift = 8;
	sinfo.pixfmt.blue_shift = 0;
	sinfo.namelen = htonl(strlen("bhyve"));
	(void)stream_write(cfd, &sinfo, sizeof(sinfo));
	(void)stream_write(cfd, "bhyve", strlen("bhyve"));
}

static void
rfb_send_resize_update_msg(struct rfb_softc *rc, int cfd)
{
	struct rfb_srvr_updt_msg supdt_msg;
	struct rfb_srvr_rect_hdr srect_hdr;

	/* Number of rectangles: 1 */
	supdt_msg.type = 0;
	supdt_msg.pad = 0;
	supdt_msg.numrects = htons(1);
	stream_write(cfd, &supdt_msg, sizeof(struct rfb_srvr_updt_msg));

	/* Rectangle header */
	srect_hdr.x = htons(0);
	srect_hdr.y = htons(0);
	srect_hdr.width = htons(rc->width);
	srect_hdr.height = htons(rc->height);
	srect_hdr.encoding = htonl(RFB_ENCODING_RESIZE);
	stream_write(cfd, &srect_hdr, sizeof(struct rfb_srvr_rect_hdr));
}

static void
rfb_recv_set_pixfmt_msg(struct rfb_softc *rc, int cfd)
{
	struct rfb_pixfmt_msg pixfmt_msg;

	(void)stream_read(cfd, ((void *)&pixfmt_msg)+1, sizeof(pixfmt_msg)-1);
}


static void
rfb_recv_set_encodings_msg(struct rfb_softc *rc, int cfd)
{
	struct rfb_enc_msg enc_msg;
	int i;
	uint32_t encoding;

	assert((sizeof(enc_msg) - 1) == 3);
	(void)stream_read(cfd, ((void *)&enc_msg)+1, sizeof(enc_msg)-1);

	for (i = 0; i < htons(enc_msg.numencs); i++) {
		(void)stream_read(cfd, &encoding, sizeof(encoding));
		switch (htonl(encoding)) {
		case RFB_ENCODING_RAW:
			rc->enc_raw_ok = true;
			break;
		case RFB_ENCODING_ZLIB:
			rc->enc_zlib_ok = true;
			deflateInit(&rc->zstream, Z_BEST_SPEED);
			break;
		case RFB_ENCODING_RESIZE:
			rc->enc_resize_ok = true;
			break;
		}
	}
}

/*
 * Calculate CRC32 using SSE4.2; Intel or AMD Bulldozer+ CPUs only
 */
static __inline uint32_t
fast_crc32(void *buf, int len, uint32_t crcval)
{
	uint32_t q = len / sizeof(uint32_t);
	uint32_t *p = (uint32_t *)buf;

	while (q--) {
		asm volatile (
			".byte 0xf2, 0xf, 0x38, 0xf1, 0xf1;"
			:"=S" (crcval)
			:"0" (crcval), "c" (*p)
		);
		p++;
	}

	return (crcval);
}


static int
rfb_send_rect(struct rfb_softc *rc, int cfd, struct bhyvegc_image *gc,
              int x, int y, int w, int h)
{
	struct rfb_srvr_updt_msg supdt_msg;
	struct rfb_srvr_rect_hdr srect_hdr;
	unsigned long zlen;
	ssize_t nwrite, total;
	int err;
	uint32_t *p;
	uint8_t *zbufp;

	/*
	 * Send a single rectangle of the given x, y, w h dimensions.
	 */

	/* Number of rectangles: 1 */
	supdt_msg.type = 0;
	supdt_msg.pad = 0;
	supdt_msg.numrects = htons(1);
	nwrite = stream_write(cfd, &supdt_msg,
	                      sizeof(struct rfb_srvr_updt_msg));
	if (nwrite <= 0)
		return (nwrite);


	/* Rectangle header */
	srect_hdr.x = htons(x);
	srect_hdr.y = htons(y);
	srect_hdr.width = htons(w);
	srect_hdr.height = htons(h);

	h = y + h;
	w *= sizeof(uint32_t);
	if (rc->enc_zlib_ok) {
		zbufp = rc->zbuf;
		rc->zstream.total_in = 0;
		rc->zstream.total_out = 0;
		for (p = &gc->data[y * gc->width + x]; y < h; y++) {
			rc->zstream.next_in = (Bytef *)p;
			rc->zstream.avail_in = w;
			rc->zstream.next_out = (Bytef *)zbufp;
			rc->zstream.avail_out = RFB_ZLIB_BUFSZ + 16 -
			                        rc->zstream.total_out;
			rc->zstream.data_type = Z_BINARY;

			/* Compress with zlib */
			err = deflate(&rc->zstream, Z_SYNC_FLUSH);
			if (err != Z_OK) {
				WPRINTF(("zlib[rect] deflate err: %d\n", err));
				rc->enc_zlib_ok = false;
				deflateEnd(&rc->zstream);
				goto doraw;
			}
			zbufp = rc->zbuf + rc->zstream.total_out;
			p += gc->width;
		}
		srect_hdr.encoding = htonl(RFB_ENCODING_ZLIB);
		nwrite = stream_write(cfd, &srect_hdr,
		                      sizeof(struct rfb_srvr_rect_hdr));
		if (nwrite <= 0)
			return (nwrite);

		zlen = htonl(rc->zstream.total_out);
		nwrite = stream_write(cfd, &zlen, sizeof(uint32_t));
		if (nwrite <= 0)
			return (nwrite);
		return (stream_write(cfd, rc->zbuf, rc->zstream.total_out));
	}

doraw:

	total = 0;
	zbufp = rc->zbuf;
	for (p = &gc->data[y * gc->width + x]; y < h; y++) {
		memcpy(zbufp, p, w);
		zbufp += w;
		total += w;
		p += gc->width;
	}

	srect_hdr.encoding = htonl(RFB_ENCODING_RAW);
	nwrite = stream_write(cfd, &srect_hdr,
	                      sizeof(struct rfb_srvr_rect_hdr));
	if (nwrite <= 0)
		return (nwrite);

	total = stream_write(cfd, rc->zbuf, total);

	return (total);
}

static int
rfb_send_all(struct rfb_softc *rc, int cfd, struct bhyvegc_image *gc)
{
	struct rfb_srvr_updt_msg supdt_msg;
        struct rfb_srvr_rect_hdr srect_hdr;
	ssize_t nwrite;
	unsigned long zlen;
	int err;

	/*
	 * Send the whole thing
	 */

	/* Number of rectangles: 1 */
	supdt_msg.type = 0;
	supdt_msg.pad = 0;
	supdt_msg.numrects = htons(1);
	nwrite = stream_write(cfd, &supdt_msg,
	                      sizeof(struct rfb_srvr_updt_msg));
	if (nwrite <= 0)
		return (nwrite);

	/* Rectangle header */
	srect_hdr.x = 0;
	srect_hdr.y = 0;
	srect_hdr.width = htons(gc->width);
	srect_hdr.height = htons(gc->height);
	if (rc->enc_zlib_ok) {
		rc->zstream.next_in = (Bytef *)gc->data;
		rc->zstream.avail_in = gc->width * gc->height *
		                   sizeof(uint32_t);
		rc->zstream.next_out = (Bytef *)rc->zbuf;
		rc->zstream.avail_out = RFB_ZLIB_BUFSZ + 16;
		rc->zstream.data_type = Z_BINARY;

		rc->zstream.total_in = 0;
		rc->zstream.total_out = 0;

		/* Compress with zlib */
		err = deflate(&rc->zstream, Z_SYNC_FLUSH);
		if (err != Z_OK) {
			WPRINTF(("zlib deflate err: %d\n", err));
			rc->enc_zlib_ok = false;
			deflateEnd(&rc->zstream);
			goto doraw;
		}

		srect_hdr.encoding = htonl(RFB_ENCODING_ZLIB);
		nwrite = stream_write(cfd, &srect_hdr,
		                      sizeof(struct rfb_srvr_rect_hdr));
		if (nwrite <= 0)
			return (nwrite);

		zlen = htonl(rc->zstream.total_out);
		nwrite = stream_write(cfd, &zlen, sizeof(uint32_t));
		if (nwrite <= 0)
			return (nwrite);
		return (stream_write(cfd, rc->zbuf, rc->zstream.total_out));
	}

doraw:
	srect_hdr.encoding = htonl(RFB_ENCODING_RAW);
	nwrite = stream_write(cfd, &srect_hdr,
	                      sizeof(struct rfb_srvr_rect_hdr));
	if (nwrite <= 0)
		return (nwrite);

	nwrite = stream_write(cfd, gc->data,
	               gc->width * gc->height * sizeof(uint32_t));

	return (nwrite);
}

#define	PIX_PER_CELL	32
#define	PIXCELL_SHIFT	5
#define	PIXCELL_MASK	0x1F

static int
rfb_send_screen(struct rfb_softc *rc, int cfd, int all)
{
	struct bhyvegc_image *gc_image;
	ssize_t nwrite;
	int x, y;
	int celly, cellwidth;
	int xcells, ycells;
	int w, h;
	uint32_t *p;
	int rem_x, rem_y;   /* remainder for resolutions not x32 pixels ratio */
	int retval;
	uint32_t *crc_p, *orig_crc;
	int changes;

	console_refresh();
	gc_image = console_get_image();

	pthread_mutex_lock(&rc->mtx);
	if (rc->sending) {
		pthread_mutex_unlock(&rc->mtx);
		return (1);
	}
	rc->sending = 1;
	pthread_mutex_unlock(&rc->mtx);

	retval = 0;

	if (all) {
		retval = rfb_send_all(rc, cfd, gc_image);
		goto done;
	}

	/*
	 * Calculate the checksum for each 32x32 cell. Send each that
	 * has changed since the last scan.
	 */

	/* Resolution changed */

	rc->crc_width = gc_image->width;
	rc->crc_height = gc_image->height;

	w = rc->crc_width;
	h = rc->crc_height;
	xcells = howmany(rc->crc_width, PIX_PER_CELL);
	ycells = howmany(rc->crc_height, PIX_PER_CELL);

	rem_x = w & PIXCELL_MASK;

	rem_y = h & PIXCELL_MASK;
	if (!rem_y)
		rem_y = PIX_PER_CELL;

	p = gc_image->data;

	/*
	 * Go through all cells and calculate crc. If significant number
	 * of changes, then send entire screen.
	 * crc_tmp is dual purpose: to store the new crc and to flag as
	 * a cell that has changed.
	 */
	crc_p = rc->crc_tmp - xcells;
	orig_crc = rc->crc - xcells;
	changes = 0;
	memset(rc->crc_tmp, 0, sizeof(uint32_t) * xcells * ycells);
	for (y = 0; y < h; y++) {
		if ((y & PIXCELL_MASK) == 0) {
			crc_p += xcells;
			orig_crc += xcells;
		}

		for (x = 0; x < xcells; x++) {
			if (x == (xcells - 1) && rem_x > 0)
				cellwidth = rem_x;
			else
				cellwidth = PIX_PER_CELL;

			if (rc->hw_crc)
				crc_p[x] = fast_crc32(p,
				             cellwidth * sizeof(uint32_t),
				             crc_p[x]);
			else
				crc_p[x] = (uint32_t)crc32(crc_p[x],
				             (Bytef *)p,
				             cellwidth * sizeof(uint32_t));

			p += cellwidth;

			/* check for crc delta if last row in cell */
			if ((y & PIXCELL_MASK) == PIXCELL_MASK || y == (h-1)) {
				if (orig_crc[x] != crc_p[x]) {
					orig_crc[x] = crc_p[x];
					crc_p[x] = 1;
					changes++;
				} else {
					crc_p[x] = 0;
				}
			}
		}
	}

	/* If number of changes is > THRESH percent, send the whole screen */
	if (((changes * 100) / (xcells * ycells)) >= RFB_SEND_ALL_THRESH) {
		retval = rfb_send_all(rc, cfd, gc_image);
		goto done;
	}
	
	/* Go through all cells, and send only changed ones */
	crc_p = rc->crc_tmp;
	for (y = 0; y < h; y += PIX_PER_CELL) {
		/* previous cell's row */
		celly = (y >> PIXCELL_SHIFT);

		/* Delta check crc to previous set */
		for (x = 0; x < xcells; x++) {
			if (*crc_p++ == 0)
				continue;

			if (x == (xcells - 1) && rem_x > 0)
				cellwidth = rem_x;
			else
				cellwidth = PIX_PER_CELL;
			nwrite = rfb_send_rect(rc, cfd,
				gc_image,
				x * PIX_PER_CELL,
				celly * PIX_PER_CELL,
			        cellwidth,
				y + PIX_PER_CELL >= h ? rem_y : PIX_PER_CELL);
			if (nwrite <= 0) {
				retval = nwrite;
				goto done;
			}
		}
	}
	retval = 1;

done:
	pthread_mutex_lock(&rc->mtx);
	rc->sending = 0;
	pthread_mutex_unlock(&rc->mtx);
	
	return (retval);
}


static void
rfb_recv_update_msg(struct rfb_softc *rc, int cfd, int discardonly)
{
	struct rfb_updt_msg updt_msg;
	struct bhyvegc_image *gc_image;

	(void)stream_read(cfd, ((void *)&updt_msg) + 1 , sizeof(updt_msg) - 1);

	console_refresh();
	gc_image = console_get_image();

	updt_msg.x = htons(updt_msg.x);
	updt_msg.y = htons(updt_msg.y);
	updt_msg.width = htons(updt_msg.width);
	updt_msg.height = htons(updt_msg.height);

	if (updt_msg.width != gc_image->width ||
	    updt_msg.height != gc_image->height) {
		rc->width = gc_image->width;
		rc->height = gc_image->height;
		if (rc->enc_resize_ok)
			rfb_send_resize_update_msg(rc, cfd);
	}

	if (discardonly)
		return;

	rfb_send_screen(rc, cfd, 1);
}

static void
rfb_recv_key_msg(struct rfb_softc *rc, int cfd)
{
	struct rfb_key_msg key_msg;

	(void)stream_read(cfd, ((void *)&key_msg) + 1, sizeof(key_msg) - 1);

	console_key_event(key_msg.down, htonl(key_msg.code));
}

static void
rfb_recv_ptr_msg(struct rfb_softc *rc, int cfd)
{
	struct rfb_ptr_msg ptr_msg;

	(void)stream_read(cfd, ((void *)&ptr_msg) + 1, sizeof(ptr_msg) - 1);

	console_ptr_event(ptr_msg.button, htons(ptr_msg.x), htons(ptr_msg.y));
}

static void
rfb_recv_cuttext_msg(struct rfb_softc *rc, int cfd)
{
	struct rfb_cuttext_msg ct_msg;
	unsigned char buf[32];
	int len;

	len = stream_read(cfd, ((void *)&ct_msg) + 1, sizeof(ct_msg) - 1);
	ct_msg.length = htonl(ct_msg.length);
	while (ct_msg.length > 0) {
		len = stream_read(cfd, buf, ct_msg.length > sizeof(buf) ?
			sizeof(buf) : ct_msg.length);
		ct_msg.length -= len;
	}
}

static int64_t
timeval_delta(struct timeval *prev, struct timeval *now)
{
	int64_t n1, n2;
	n1 = now->tv_sec * 1000000 + now->tv_usec;
	n2 = prev->tv_sec * 1000000 + prev->tv_usec;
	return (n1 - n2);
}

static void *
rfb_wr_thr(void *arg)
{
	struct rfb_softc *rc;
	fd_set rfds;
	struct timeval tv;
	struct timeval prev_tv;
	int64_t tdiff;
	int cfd;
	int err;

	rc = arg;
	cfd = rc->cfd;

	prev_tv.tv_sec = 0;
	prev_tv.tv_usec = 0;
	while (rc->cfd >= 0) {
		FD_ZERO(&rfds);
		FD_SET(cfd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 10000;

		err = select(cfd+1, &rfds, NULL, NULL, &tv);
		if (err < 0)
			return (NULL);

		/* Determine if its time to push screen; ~24hz */
		gettimeofday(&tv, NULL);
		tdiff = timeval_delta(&prev_tv, &tv);
		if (tdiff > 40000) {
			prev_tv.tv_sec = tv.tv_sec;
			prev_tv.tv_usec = tv.tv_usec;
			if (rfb_send_screen(rc, cfd, 0) <= 0) {
				return (NULL);
			}
		} else {
			/* sleep */
			usleep(40000 - tdiff);
		}
	}

	return (NULL);
}

void
rfb_handle(struct rfb_softc *rc, int cfd)
{
	const char *vbuf = "RFB 003.008\n";
	unsigned char buf[80];
	unsigned char *message = NULL;

#ifndef NO_OPENSSL
	unsigned char challenge[AUTH_LENGTH];
	unsigned char keystr[PASSWD_LENGTH];
	unsigned char crypt_expected[AUTH_LENGTH];

	DES_key_schedule ks;
	int i;
#endif

	pthread_t tid;
	uint32_t sres = 0;
	int len;
	int perror = 1;

	rc->cfd = cfd;

	/* 1a. Send server version */
	stream_write(cfd, vbuf, strlen(vbuf));

	/* 1b. Read client version */
	len = read(cfd, buf, sizeof(buf));

	/* 2a. Send security type */
	buf[0] = 1;
#ifndef NO_OPENSSL
	if (rc->password) 
		buf[1] = SECURITY_TYPE_VNC_AUTH;
	else
		buf[1] = SECURITY_TYPE_NONE;
#else
	buf[1] = SECURITY_TYPE_NONE;
#endif

	stream_write(cfd, buf, 2);

	/* 2b. Read agreed security type */
	len = stream_read(cfd, buf, 1);

	/* 2c. Do VNC authentication */
	switch (buf[0]) {
	case SECURITY_TYPE_NONE:
		sres = 0;
		break;
	case SECURITY_TYPE_VNC_AUTH:
		/*
		 * The client encrypts the challenge with DES, using a password
		 * supplied by the user as the key.
		 * To form the key, the password is truncated to
		 * eight characters, or padded with null bytes on the right.
		 * The client then sends the resulting 16-bytes response.
		 */
#ifndef NO_OPENSSL
		strncpy(keystr, rc->password, PASSWD_LENGTH);

		/* VNC clients encrypts the challenge with all the bit fields
		 * in each byte of the password mirrored.
		 * Here we flip each byte of the keystr.
		 */
		for (i = 0; i < PASSWD_LENGTH; i++) {
			keystr[i] = (keystr[i] & 0xF0) >> 4
				  | (keystr[i] & 0x0F) << 4;
			keystr[i] = (keystr[i] & 0xCC) >> 2
				  | (keystr[i] & 0x33) << 2;
			keystr[i] = (keystr[i] & 0xAA) >> 1
				  | (keystr[i] & 0x55) << 1;
		}

		/* Initialize a 16-byte random challenge */
		arc4random_buf(challenge, sizeof(challenge));
		stream_write(cfd, challenge, AUTH_LENGTH);

		/* Receive the 16-byte challenge response */
		stream_read(cfd, buf, AUTH_LENGTH);

		memcpy(crypt_expected, challenge, AUTH_LENGTH);

		/* Encrypt the Challenge with DES */
		DES_set_key((const_DES_cblock *)keystr, &ks);
		DES_ecb_encrypt((const_DES_cblock *)challenge,
				(const_DES_cblock *)crypt_expected,
				&ks, DES_ENCRYPT);
		DES_ecb_encrypt((const_DES_cblock *)(challenge + PASSWD_LENGTH),
				(const_DES_cblock *)(crypt_expected +
				PASSWD_LENGTH),
				&ks, DES_ENCRYPT);

		if (memcmp(crypt_expected, buf, AUTH_LENGTH) != 0) {
			message = "Auth Failed: Invalid Password.";
			sres = htonl(1);
		} else
			sres = 0;
#else
		sres = 0;
		WPRINTF(("Auth not supported, no OpenSSL in your system"));
#endif

		break;
	}

	/* 2d. Write back a status */
	stream_write(cfd, &sres, 4);

	if (sres) {
		be32enc(buf, strlen(message));
		stream_write(cfd, buf, 4);
		stream_write(cfd, message, strlen(message));
		goto done;
	}

	/* 3a. Read client shared-flag byte */
	len = stream_read(cfd, buf, 1);

	/* 4a. Write server-init info */
	rfb_send_server_init_msg(cfd);

	if (!rc->zbuf) {
		rc->zbuf = malloc(RFB_ZLIB_BUFSZ + 16);
		assert(rc->zbuf != NULL);
	}

	rfb_send_screen(rc, cfd, 1);

	perror = pthread_create(&tid, NULL, rfb_wr_thr, rc);
	if (perror == 0)
		pthread_set_name_np(tid, "rfbout");

        /* Now read in client requests. 1st byte identifies type */
	for (;;) {
		len = read(cfd, buf, 1);
		if (len <= 0) {
			DPRINTF(("rfb client exiting\r\n"));
			break;
		}

		switch (buf[0]) {
		case 0:
			rfb_recv_set_pixfmt_msg(rc, cfd);
			break;
		case 2:
			rfb_recv_set_encodings_msg(rc, cfd);
			break;
		case 3:
			rfb_recv_update_msg(rc, cfd, 1);
			break;
		case 4:
			rfb_recv_key_msg(rc, cfd);
			break;
		case 5:
			rfb_recv_ptr_msg(rc, cfd);
			break;
		case 6:
			rfb_recv_cuttext_msg(rc, cfd);
			break;
		default:
			WPRINTF(("rfb unknown cli-code %d!\n", buf[0] & 0xff));
			goto done;
		}
	}
done:
	rc->cfd = -1;
	if (perror == 0)
		pthread_join(tid, NULL);
	if (rc->enc_zlib_ok)
		deflateEnd(&rc->zstream);
}

static void *
rfb_thr(void *arg)
{
	struct rfb_softc *rc;
	sigset_t set;

	int cfd;

	rc = arg;

	sigemptyset(&set);
	sigaddset(&set, SIGPIPE);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
		perror("pthread_sigmask");
		return (NULL);
	}

	for (;;) {
		rc->enc_raw_ok = false;
		rc->enc_zlib_ok = false;
		rc->enc_resize_ok = false;

		cfd = accept(rc->sfd, NULL, NULL);
		if (rc->conn_wait) {
			pthread_mutex_lock(&rc->mtx);
			pthread_cond_signal(&rc->cond);
			pthread_mutex_unlock(&rc->mtx);
			rc->conn_wait = 0;
		}
		rfb_handle(rc, cfd);
		close(cfd);
	}

	/* NOTREACHED */
	return (NULL);
}

static int
sse42_supported(void)
{
	u_int cpu_registers[4], ecx;

	do_cpuid(1, cpu_registers);

	ecx = cpu_registers[2];

	return ((ecx & CPUID2_SSE42) != 0);
}

int
rfb_init(char *hostname, int port, int wait, char *password)
{
	int e;
	char servname[6];
	struct rfb_softc *rc;
	struct addrinfo *ai;
	struct addrinfo hints;
	int on = 1;
#ifndef WITHOUT_CAPSICUM
	cap_rights_t rights;
#endif

	rc = calloc(1, sizeof(struct rfb_softc));

	rc->crc = calloc(howmany(RFB_MAX_WIDTH * RFB_MAX_HEIGHT, 32),
	                 sizeof(uint32_t));
	rc->crc_tmp = calloc(howmany(RFB_MAX_WIDTH * RFB_MAX_HEIGHT, 32),
	                     sizeof(uint32_t));
	rc->crc_width = RFB_MAX_WIDTH;
	rc->crc_height = RFB_MAX_HEIGHT;

	rc->password = password;

	snprintf(servname, sizeof(servname), "%d", port ? port : 5900);

	if (!hostname || strlen(hostname) == 0)
#if defined(INET)
		hostname = "127.0.0.1";
#elif defined(INET6)
		hostname = "[::1]";
#endif

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;

	if ((e = getaddrinfo(hostname, servname, &hints, &ai)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(e));
		return(-1);
	}

	rc->sfd = socket(ai->ai_family, ai->ai_socktype, 0);
	if (rc->sfd < 0) {
		perror("socket");
		freeaddrinfo(ai);
		return (-1);
	}

	setsockopt(rc->sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	if (bind(rc->sfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		perror("bind");
		freeaddrinfo(ai);
		return (-1);
	}

	if (listen(rc->sfd, 1) < 0) {
		perror("listen");
		freeaddrinfo(ai);
		return (-1);
	}

#ifndef WITHOUT_CAPSICUM
	cap_rights_init(&rights, CAP_ACCEPT, CAP_EVENT, CAP_READ, CAP_WRITE);
	if (caph_rights_limit(rc->sfd, &rights) == -1)
		errx(EX_OSERR, "Unable to apply rights for sandbox");
#endif

	rc->hw_crc = sse42_supported();

	rc->conn_wait = wait;
	if (wait) {
		pthread_mutex_init(&rc->mtx, NULL);
		pthread_cond_init(&rc->cond, NULL);
	}

	pthread_create(&rc->tid, NULL, rfb_thr, rc);
	pthread_set_name_np(rc->tid, "rfb");

	if (wait) {
		DPRINTF(("Waiting for rfb client...\n"));
		pthread_mutex_lock(&rc->mtx);
		pthread_cond_wait(&rc->cond, &rc->mtx);
		pthread_mutex_unlock(&rc->mtx);
	}

	freeaddrinfo(ai);
	return (0);
}
