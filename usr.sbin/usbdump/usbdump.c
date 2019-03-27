/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Weongyo Jeong <weongyo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/queue.h>
#include <net/if.h>
#include <net/bpf.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_pf.h>
#include <dev/usb/usbdi.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sysexits.h>
#include <err.h>

#define	BPF_STORE_JUMP(x,_c,_k,_jt,_jf) do {	\
  (x).code = (_c);				\
  (x).k = (_k);					\
  (x).jt = (_jt);				\
  (x).jf = (_jf);				\
} while (0)

#define	BPF_STORE_STMT(x,_c,_k) do {		\
  (x).code = (_c);				\
  (x).k = (_k);					\
  (x).jt = 0;					\
  (x).jf = 0;					\
} while (0)

struct usb_filt {
	STAILQ_ENTRY(usb_filt) entry;
	int unit;
	int endpoint;
};

struct usbcap {
	int		fd;		/* fd for /dev/usbpf */
	uint32_t	bufsize;
	uint8_t		*buffer;

	/* for -w option */
	int		wfd;
	/* for -r option */
	int		rfd;
	/* for -b option */
	int		bfd;
};

struct usbcap_filehdr {
	uint32_t	magic;
#define	USBCAP_FILEHDR_MAGIC	0x9a90000e
	uint8_t   	major;
	uint8_t		minor;
	uint8_t		reserved[26];
} __packed;

struct header_32 {
	/* capture timestamp */
	uint32_t ts_sec;
	uint32_t ts_usec;
	/* data length and alignment information */
	uint32_t caplen;
	uint32_t datalen;
	uint8_t hdrlen;
	uint8_t align;
} __packed;

static int doexit = 0;
static int pkt_captured = 0;
static int verbose = 0;
static int uf_minor;
static const char *i_arg = "usbus0";
static const char *r_arg = NULL;
static const char *w_arg = NULL;
static const char *b_arg = NULL;
static struct usbcap uc;
static const char *errstr_table[USB_ERR_MAX] = {
	[USB_ERR_NORMAL_COMPLETION]	= "0",
	[USB_ERR_PENDING_REQUESTS]	= "PENDING_REQUESTS",
	[USB_ERR_NOT_STARTED]		= "NOT_STARTED",
	[USB_ERR_INVAL]			= "INVAL",
	[USB_ERR_NOMEM]			= "NOMEM",
	[USB_ERR_CANCELLED]		= "CANCELLED",
	[USB_ERR_BAD_ADDRESS]		= "BAD_ADDRESS",
	[USB_ERR_BAD_BUFSIZE]		= "BAD_BUFSIZE",
	[USB_ERR_BAD_FLAG]		= "BAD_FLAG",
	[USB_ERR_NO_CALLBACK]		= "NO_CALLBACK",
	[USB_ERR_IN_USE]		= "IN_USE",
	[USB_ERR_NO_ADDR]		= "NO_ADDR",
	[USB_ERR_NO_PIPE]		= "NO_PIPE",
	[USB_ERR_ZERO_NFRAMES]		= "ZERO_NFRAMES",
	[USB_ERR_ZERO_MAXP]		= "ZERO_MAXP",
	[USB_ERR_SET_ADDR_FAILED]	= "SET_ADDR_FAILED",
	[USB_ERR_NO_POWER]		= "NO_POWER",
	[USB_ERR_TOO_DEEP]		= "TOO_DEEP",
	[USB_ERR_IOERROR]		= "IOERROR",
	[USB_ERR_NOT_CONFIGURED]	= "NOT_CONFIGURED",
	[USB_ERR_TIMEOUT]		= "TIMEOUT",
	[USB_ERR_SHORT_XFER]		= "SHORT_XFER",
	[USB_ERR_STALLED]		= "STALLED",
	[USB_ERR_INTERRUPTED]		= "INTERRUPTED",
	[USB_ERR_DMA_LOAD_FAILED]	= "DMA_LOAD_FAILED",
	[USB_ERR_BAD_CONTEXT]		= "BAD_CONTEXT",
	[USB_ERR_NO_ROOT_HUB]		= "NO_ROOT_HUB",
	[USB_ERR_NO_INTR_THREAD]	= "NO_INTR_THREAD",
	[USB_ERR_NOT_LOCKED]		= "NOT_LOCKED",
};

static const char *xfertype_table[4] = {
	[UE_CONTROL]			= "CTRL",
	[UE_ISOCHRONOUS]		= "ISOC",
	[UE_BULK]			= "BULK",
	[UE_INTERRUPT]			= "INTR"
};

static const char *speed_table[USB_SPEED_MAX] = {
	[USB_SPEED_FULL] = "FULL",
	[USB_SPEED_HIGH] = "HIGH",
	[USB_SPEED_LOW] = "LOW",
	[USB_SPEED_VARIABLE] = "VARI",
	[USB_SPEED_SUPER] = "SUPER",
};

static STAILQ_HEAD(,usb_filt) usb_filt_head =
    STAILQ_HEAD_INITIALIZER(usb_filt_head);

static void
add_filter(int usb_filt_unit, int usb_filt_ep)
{
	struct usb_filt *puf;

	puf = malloc(sizeof(struct usb_filt));
	if (puf == NULL)
		errx(EX_SOFTWARE, "Out of memory.");

	puf->unit = usb_filt_unit;
	puf->endpoint = usb_filt_ep;

	STAILQ_INSERT_TAIL(&usb_filt_head, puf, entry);
}

static void
make_filter(struct bpf_program *pprog, int snapshot)
{
	struct usb_filt *puf;
	struct bpf_insn *dynamic_insn;
	int len;

	len = 0;

	STAILQ_FOREACH(puf, &usb_filt_head, entry)
		len++;

	dynamic_insn = malloc(((len * 5) + 1) * sizeof(struct bpf_insn));

	if (dynamic_insn == NULL)
		errx(EX_SOFTWARE, "Out of memory.");

	len++;

	if (len == 1) {
		/* accept all packets */

		BPF_STORE_STMT(dynamic_insn[0], BPF_RET | BPF_K, snapshot);

		goto done;
	}

	len = 0;

	STAILQ_FOREACH(puf, &usb_filt_head, entry) {
		const int addr_off = (uintptr_t)&((struct usbpf_pkthdr *)0)->up_address;
		const int addr_ep = (uintptr_t)&((struct usbpf_pkthdr *)0)->up_endpoint;
		
		if (puf->unit != -1) {
			if (puf->endpoint != -1) {
				BPF_STORE_STMT(dynamic_insn[len],
				    BPF_LD | BPF_B | BPF_ABS, addr_off);
				len++;
				BPF_STORE_JUMP(dynamic_insn[len],
				    BPF_JMP | BPF_JEQ | BPF_K, (uint8_t)puf->unit, 0, 3);
				len++;
				BPF_STORE_STMT(dynamic_insn[len],
				    BPF_LD | BPF_W | BPF_ABS, addr_ep);
				len++;
				BPF_STORE_JUMP(dynamic_insn[len],
				    BPF_JMP | BPF_JEQ | BPF_K, htobe32(puf->endpoint), 0, 1);
				len++;
			} else {
				BPF_STORE_STMT(dynamic_insn[len],
				    BPF_LD | BPF_B | BPF_ABS, addr_off);
				len++;
				BPF_STORE_JUMP(dynamic_insn[len],
				    BPF_JMP | BPF_JEQ | BPF_K, (uint8_t)puf->unit, 0, 1);
				len++;
			}
		} else {
			if (puf->endpoint != -1) {
				BPF_STORE_STMT(dynamic_insn[len],
				    BPF_LD | BPF_W | BPF_ABS, addr_ep);
				len++;
				BPF_STORE_JUMP(dynamic_insn[len],
				    BPF_JMP | BPF_JEQ | BPF_K, htobe32(puf->endpoint), 0, 1);
				len++;
			}
		}
		BPF_STORE_STMT(dynamic_insn[len],
		    BPF_RET | BPF_K, snapshot);
		len++;
	}

	BPF_STORE_STMT(dynamic_insn[len], BPF_RET | BPF_K, 0);
	len++;

done:
	pprog->bf_len = len;
	pprog->bf_insns = dynamic_insn;
}

static int
match_filter(int unit, int endpoint)
{
	struct usb_filt *puf;

	if (STAILQ_FIRST(&usb_filt_head) == NULL)
		return (1);

	STAILQ_FOREACH(puf, &usb_filt_head, entry) {
		if ((puf->unit == -1 || puf->unit == unit) &&
		    (puf->endpoint == -1 || puf->endpoint == endpoint))
			return (1);
	}
	return (0);
}

static void
free_filter(struct bpf_program *pprog)
{
	struct usb_filt *puf;

	while ((puf = STAILQ_FIRST(&usb_filt_head)) != NULL) {
		STAILQ_REMOVE_HEAD(&usb_filt_head, entry);
		free(puf);
	}
	free(pprog->bf_insns);
}

static void
handle_sigint(int sig)
{

	(void)sig;
	doexit = 1;
}

#define	FLAGS(x, name)	\
	(((x) & USBPF_FLAG_##name) ? #name "|" : "")

#define	STATUS(x, name) \
	(((x) & USBPF_STATUS_##name) ? #name "|" : "")

static const char *
usb_errstr(uint32_t error)
{
	if (error >= USB_ERR_MAX || errstr_table[error] == NULL)
		return ("UNKNOWN");
	else
		return (errstr_table[error]);
}

static const char *
usb_speedstr(uint8_t speed)
{
	if (speed >= USB_SPEED_MAX  || speed_table[speed] == NULL)
		return ("UNKNOWN");
	else
		return (speed_table[speed]);
}

static void
print_flags(uint32_t flags)
{
	printf(" flags %#x <%s%s%s%s%s%s%s%s%s0>\n",
	    flags,
	    FLAGS(flags, FORCE_SHORT_XFER),
	    FLAGS(flags, SHORT_XFER_OK),
	    FLAGS(flags, SHORT_FRAMES_OK),
	    FLAGS(flags, PIPE_BOF),
	    FLAGS(flags, PROXY_BUFFER),
	    FLAGS(flags, EXT_BUFFER),
	    FLAGS(flags, MANUAL_STATUS),
	    FLAGS(flags, NO_PIPE_OK),
	    FLAGS(flags, STALL_PIPE));
}

static void
print_status(uint32_t status)
{
	printf(" status %#x <%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s0>\n",
	    status, 
	    STATUS(status, OPEN),
	    STATUS(status, TRANSFERRING),
	    STATUS(status, DID_DMA_DELAY),
	    STATUS(status, DID_CLOSE),
	    STATUS(status, DRAINING),
	    STATUS(status, STARTED),
	    STATUS(status, BW_RECLAIMED),
	    STATUS(status, CONTROL_XFR),
	    STATUS(status, CONTROL_HDR),
	    STATUS(status, CONTROL_ACT),
	    STATUS(status, CONTROL_STALL),
	    STATUS(status, SHORT_FRAMES_OK),
	    STATUS(status, SHORT_XFER_OK),
	    STATUS(status, BDMA_ENABLE),
	    STATUS(status, BDMA_NO_POST_SYNC),
	    STATUS(status, BDMA_SETUP),
	    STATUS(status, ISOCHRONOUS_XFR),
	    STATUS(status, CURR_DMA_SET),
	    STATUS(status, CAN_CANCEL_IMMED),
	    STATUS(status, DOING_CALLBACK));
}

/*
 * Dump a byte into hex format.
 */
static void
hexbyte(char *buf, uint8_t temp)
{
	uint8_t lo;
	uint8_t hi;

	lo = temp & 0xF;
	hi = temp >> 4;

	if (hi < 10)
		buf[0] = '0' + hi;
	else
		buf[0] = 'A' + hi - 10;

	if (lo < 10)
		buf[1] = '0' + lo;
	else
		buf[1] = 'A' + lo - 10;
}

/*
 * Display a region in traditional hexdump format.
 */
static void
hexdump(const uint8_t *region, uint32_t len)
{
	const uint8_t *line;
	char linebuf[128];
	int i;
	int x;
	int c;

	for (line = region; line < (region + len); line += 16) {

		i = 0;

		linebuf[i] = ' ';
		hexbyte(linebuf + i + 1, ((line - region) >> 8) & 0xFF);
		hexbyte(linebuf + i + 3, (line - region) & 0xFF);
		linebuf[i + 5] = ' ';
		linebuf[i + 6] = ' ';
		i += 7;

		for (x = 0; x < 16; x++) {
		  if ((line + x) < (region + len)) {
			hexbyte(linebuf + i,
			    *(const u_int8_t *)(line + x));
		  } else {
			  linebuf[i] = '-';
			  linebuf[i + 1] = '-';
			}
			linebuf[i + 2] = ' ';
			if (x == 7) {
			  linebuf[i + 3] = ' ';
			  i += 4;
			} else {
			  i += 3;
			}
		}
		linebuf[i] = ' ';
		linebuf[i + 1] = '|';
		i += 2;
		for (x = 0; x < 16; x++) {
			if ((line + x) < (region + len)) {
				c = *(const u_int8_t *)(line + x);
				/* !isprint(c) */
				if ((c < ' ') || (c > '~'))
					c = '.';
				linebuf[i] = c;
			} else {
				linebuf[i] = ' ';
			}
			i++;
		}
		linebuf[i] = '|';
		linebuf[i + 1] = 0;
		i += 2;
		puts(linebuf);
	}
}

static void
print_apacket(const struct header_32 *hdr, const uint8_t *ptr, int ptr_len)
{
	struct tm *tm;
	struct usbpf_pkthdr up_temp;
	struct usbpf_pkthdr *up;
	struct timeval tv;
	size_t len;
	uint32_t x;
	char buf[64];

	ptr += USBPF_HDR_LEN;
	ptr_len -= USBPF_HDR_LEN;
	if (ptr_len < 0)
		return;

	/* make sure we don't change the source buffer */
	memcpy(&up_temp, ptr - USBPF_HDR_LEN, sizeof(up_temp));
	up = &up_temp;

	/*
	 * A packet from the kernel is based on little endian byte
	 * order.
	 */
	up->up_totlen = le32toh(up->up_totlen);
	up->up_busunit = le32toh(up->up_busunit);
	up->up_flags = le32toh(up->up_flags);
	up->up_status = le32toh(up->up_status);
	up->up_error = le32toh(up->up_error);
	up->up_interval = le32toh(up->up_interval);
	up->up_frames = le32toh(up->up_frames);
	up->up_packet_size = le32toh(up->up_packet_size);
	up->up_packet_count = le32toh(up->up_packet_count);
	up->up_endpoint = le32toh(up->up_endpoint);

	if (!match_filter(up->up_address, up->up_endpoint))
		return;

	tv.tv_sec = hdr->ts_sec;
	tv.tv_usec = hdr->ts_usec;
	tm = localtime(&tv.tv_sec);

	len = strftime(buf, sizeof(buf), "%H:%M:%S", tm);

	if (verbose >= 0) {
		printf("%.*s.%06ld usbus%d.%d %s-%s-EP=%08x,SPD=%s,NFR=%d,SLEN=%d,IVAL=%d%s%s\n",
		    (int)len, buf, tv.tv_usec,
		    (int)up->up_busunit, (int)up->up_address,
		    (up->up_type == USBPF_XFERTAP_SUBMIT) ? "SUBM" : "DONE",
		    xfertype_table[up->up_xfertype],
		    (unsigned int)up->up_endpoint,
		    usb_speedstr(up->up_speed),
		    (int)up->up_frames,
		    (int)(up->up_totlen - USBPF_HDR_LEN -
		    (USBPF_FRAME_HDR_LEN * up->up_frames)),
		    (int)up->up_interval,
		    (up->up_type == USBPF_XFERTAP_DONE) ? ",ERR=" : "",
		    (up->up_type == USBPF_XFERTAP_DONE) ?
		    usb_errstr(up->up_error) : "");
	}

	if (verbose >= 1 || b_arg != NULL) {
		for (x = 0; x != up->up_frames; x++) {
			const struct usbpf_framehdr *uf;
			uint32_t framelen;
			uint32_t flags;

			uf = (const struct usbpf_framehdr *)ptr;
			ptr += USBPF_FRAME_HDR_LEN;
			ptr_len -= USBPF_FRAME_HDR_LEN;
			if (ptr_len < 0)
				return;

			framelen = le32toh(uf->length);
			flags = le32toh(uf->flags);

			if (verbose >= 1) {
				printf(" frame[%u] %s %d bytes\n",
				    (unsigned int)x,
				    (flags & USBPF_FRAMEFLAG_READ) ? "READ" : "WRITE",
				    (int)framelen);
			}

			if (flags & USBPF_FRAMEFLAG_DATA_FOLLOWS) {

				int tot_frame_len;

				tot_frame_len = USBPF_FRAME_ALIGN(framelen);

				ptr_len -= tot_frame_len;

				if (tot_frame_len < 0 ||
				    (int)framelen < 0 || (int)ptr_len < 0)
					break;

				if (b_arg != NULL) {
					struct usbcap *p = &uc;
					int ret;
					ret = write(p->bfd, ptr, framelen);
					if (ret != (int)framelen)
						err(EXIT_FAILURE, "Could not write binary data");
				}
				if (verbose >= 1)
					hexdump(ptr, framelen);

				ptr += tot_frame_len;
			}
		}
	}
	if (verbose >= 2)
		print_flags(up->up_flags);
	if (verbose >= 3)
		print_status(up->up_status);
}

static void
fix_packets(uint8_t *data, const int datalen)
{
	struct header_32 temp;
	uint8_t *ptr;
	uint8_t *next;
	uint32_t hdrlen;
	uint32_t caplen;

	for (ptr = data; ptr < (data + datalen); ptr = next) {

		const struct bpf_hdr *hdr;

		hdr = (const struct bpf_hdr *)ptr;

		temp.ts_sec = htole32(hdr->bh_tstamp.tv_sec);
		temp.ts_usec = htole32(hdr->bh_tstamp.tv_usec);
		temp.caplen = htole32(hdr->bh_caplen);
		temp.datalen = htole32(hdr->bh_datalen);
		temp.hdrlen = hdr->bh_hdrlen;
		temp.align = BPF_WORDALIGN(1);

		hdrlen = hdr->bh_hdrlen;
		caplen = hdr->bh_caplen;

		if ((hdrlen >= sizeof(temp)) && (hdrlen <= 255) &&
		    ((ptr + hdrlen) <= (data + datalen))) {
			memcpy(ptr, &temp, sizeof(temp));
			memset(ptr + sizeof(temp), 0, hdrlen - sizeof(temp));
		} else {
			err(EXIT_FAILURE, "Invalid header length %d", hdrlen);
		}

		next = ptr + BPF_WORDALIGN(hdrlen + caplen);

		if (next <= ptr)
			err(EXIT_FAILURE, "Invalid length");
	}
}

static void
print_packets(uint8_t *data, const int datalen)
{
	struct header_32 temp;
	uint8_t *ptr;
	uint8_t *next;

	for (ptr = data; ptr < (data + datalen); ptr = next) {

		const struct header_32 *hdr32;

		hdr32 = (const struct header_32 *)ptr;

		temp.ts_sec = le32toh(hdr32->ts_sec);
		temp.ts_usec = le32toh(hdr32->ts_usec);
		temp.caplen = le32toh(hdr32->caplen);
		temp.datalen = le32toh(hdr32->datalen);
		temp.hdrlen = hdr32->hdrlen;
		temp.align = hdr32->align;

		next = ptr + roundup2(temp.hdrlen + temp.caplen, temp.align);

		if (next <= ptr)
			err(EXIT_FAILURE, "Invalid length");

		if (verbose >= 0 || r_arg != NULL || b_arg != NULL) {
			print_apacket(&temp, ptr +
			    temp.hdrlen, temp.caplen);
		}
		pkt_captured++;
	}
}

static void
write_packets(struct usbcap *p, const uint8_t *data, const int datalen)
{
	int len = htole32(datalen);
	int ret;

	ret = write(p->wfd, &len, sizeof(int));
	if (ret != sizeof(int)) {
		err(EXIT_FAILURE, "Could not write length "
		    "field of USB data payload");
	}
	ret = write(p->wfd, data, datalen);
	if (ret != datalen) {
		err(EXIT_FAILURE, "Could not write "
		    "complete USB data payload");
	}
}

static void
read_file(struct usbcap *p)
{
	int datalen;
	int ret;
	uint8_t *data;

	while ((ret = read(p->rfd, &datalen, sizeof(int))) == sizeof(int)) {
		datalen = le32toh(datalen);
		data = malloc(datalen);
		if (data == NULL)
			errx(EX_SOFTWARE, "Out of memory.");
		ret = read(p->rfd, data, datalen);
		if (ret != datalen) {
			err(EXIT_FAILURE, "Could not read complete "
			    "USB data payload");
		}
		if (uf_minor == 2)
			fix_packets(data, datalen);

		print_packets(data, datalen);
		free(data);
	}
}

static void
do_loop(struct usbcap *p)
{
	int cc;

	while (doexit == 0) {
		cc = read(p->fd, (uint8_t *)p->buffer, p->bufsize);
		if (cc < 0) {
			switch (errno) {
			case EINTR:
				break;
			default:
				fprintf(stderr, "read: %s\n", strerror(errno));
				return;
			}
			continue;
		}
		if (cc == 0)
			continue;

		fix_packets(p->buffer, cc);

		if (w_arg != NULL)
			write_packets(p, p->buffer, cc);
		print_packets(p->buffer, cc);
	}
}

static void
init_rfile(struct usbcap *p)
{
	struct usbcap_filehdr uf;
	int ret;

	p->rfd = open(r_arg, O_RDONLY);
	if (p->rfd < 0) {
		err(EXIT_FAILURE, "Could not open "
		    "'%s' for read", r_arg);
	}
	ret = read(p->rfd, &uf, sizeof(uf));
	if (ret != sizeof(uf)) {
		err(EXIT_FAILURE, "Could not read USB capture "
		    "file header");
	}
	if (le32toh(uf.magic) != USBCAP_FILEHDR_MAGIC) {
		errx(EX_SOFTWARE, "Invalid magic field(0x%08x) "
		    "in USB capture file header.",
		    (unsigned int)le32toh(uf.magic));
	}
	if (uf.major != 0) {
		errx(EX_SOFTWARE, "Invalid major version(%d) "
		    "field in USB capture file header.", (int)uf.major);
	}

	uf_minor = uf.minor;

	if (uf.minor != 3 && uf.minor != 2) {
		errx(EX_SOFTWARE, "Invalid minor version(%d) "
		    "field in USB capture file header.", (int)uf.minor);
	}
}

static void
init_wfile(struct usbcap *p)
{
	struct usbcap_filehdr uf;
	int ret;

	p->wfd = open(w_arg, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
	if (p->wfd < 0) {
		err(EXIT_FAILURE, "Could not open "
		    "'%s' for write", w_arg);
	}
	memset(&uf, 0, sizeof(uf));
	uf.magic = htole32(USBCAP_FILEHDR_MAGIC);
	uf.major = 0;
	uf.minor = 3;
	ret = write(p->wfd, (const void *)&uf, sizeof(uf));
	if (ret != sizeof(uf)) {
		err(EXIT_FAILURE, "Could not write "
		    "USB capture header");
	}
}

static void
usage(void)
{

#define FMT "    %-14s %s\n"
	fprintf(stderr, "usage: usbdump [options]\n");
	fprintf(stderr, FMT, "-i <usbusX>", "Listen on USB bus interface");
	fprintf(stderr, FMT, "-f <unit[.endpoint]>", "Specify a device and endpoint filter");
	fprintf(stderr, FMT, "-r <file>", "Read the raw packets from file");
	fprintf(stderr, FMT, "-s <snaplen>", "Snapshot bytes from each packet");
	fprintf(stderr, FMT, "-v", "Increase the verbose level");
	fprintf(stderr, FMT, "-b <file>", "Save raw version of all recorded data to file");
	fprintf(stderr, FMT, "-w <file>", "Write the raw packets to file");
	fprintf(stderr, FMT, "-h", "Display summary of command line options");
#undef FMT
	exit(EX_USAGE);
}

static void
check_usb_pf_sysctl(void)
{
	int error;
	int no_pf_val = 0;
	size_t no_pf_len = sizeof(int);

	/* check "hw.usb.no_pf" sysctl for 8- and 9- stable */

	error = sysctlbyname("hw.usb.no_pf", &no_pf_val,
	    &no_pf_len, NULL, 0);
	if (error == 0 && no_pf_val != 0) {
		warnx("The USB packet filter might be disabled.");
		warnx("See the \"hw.usb.no_pf\" sysctl for more information.");
	}
}

int
main(int argc, char *argv[])
{
	struct timeval tv;
	struct bpf_program total_prog;
	struct bpf_stat us;
	struct bpf_version bv;
	struct usbcap *p = &uc;
	struct ifreq ifr;
	long snapshot = 192;
	uint32_t v;
	int fd;
	int o;
	int filt_unit;
	int filt_ep;
	int s;
	int ifindex;
	const char *optstring;
	char *pp;

	optstring = "b:hi:r:s:vw:f:";
	while ((o = getopt(argc, argv, optstring)) != -1) {
		switch (o) {
		case 'i':
			i_arg = optarg;
			break;
		case 'r':
			r_arg = optarg;
			init_rfile(p);
			break;
		case 's':
			snapshot = strtol(optarg, &pp, 10);
			errno = 0;
			if (pp != NULL && *pp != 0)
				usage();
			if (snapshot == 0 && errno == EINVAL)
				usage();
			/* snapeshot == 0 is special */
			if (snapshot == 0)
				snapshot = -1;
			break;
		case 'b':
			b_arg = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'w':
			w_arg = optarg;
			init_wfile(p);
			break;
		case 'f':
			filt_unit = strtol(optarg, &pp, 10);
			filt_ep = -1;
			if (pp != NULL) {
				if (*pp == '.') {
					filt_ep = strtol(pp + 1, &pp, 10);
					if (pp != NULL && *pp != 0)
						usage();
				} else if (*pp != 0) {
					usage();
				}
			}
			add_filter(filt_unit, filt_ep);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (b_arg != NULL) {
		p->bfd = open(b_arg, O_CREAT | O_TRUNC |
		    O_WRONLY, S_IRUSR | S_IWUSR);
		if (p->bfd < 0) {
			err(EXIT_FAILURE, "Could not open "
			    "'%s' for write", b_arg);
		}
	}

	/*
	 * Require more verbosity to print anything when -w or -b is
	 * specified on the command line:
	 */
	if (w_arg != NULL || b_arg != NULL)
		verbose--;

	if (r_arg != NULL) {
		read_file(p);
		exit(EXIT_SUCCESS);
	}

	check_usb_pf_sysctl();

	p->fd = fd = open("/dev/bpf", O_RDONLY);
	if (p->fd < 0)
		err(EXIT_FAILURE, "Could not open BPF device");

	if (ioctl(fd, BIOCVERSION, (caddr_t)&bv) < 0)
		err(EXIT_FAILURE, "BIOCVERSION ioctl failed");

	if (bv.bv_major != BPF_MAJOR_VERSION ||
	    bv.bv_minor < BPF_MINOR_VERSION)
		errx(EXIT_FAILURE, "Kernel BPF filter out of date");

	/* USB transfers can be greater than 64KByte */
	v = 1U << 16;

	/* clear ifr structure */
	memset(&ifr, 0, sizeof(ifr));

	/* Try to create usbusN interface if it is not available. */
	s = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (s < 0)
		errx(EXIT_FAILURE, "Could not open a socket");
	ifindex = if_nametoindex(i_arg);
	if (ifindex == 0) {
		(void)strlcpy(ifr.ifr_name, i_arg, sizeof(ifr.ifr_name));
		if (ioctl(s, SIOCIFCREATE2, &ifr) < 0)
			errx(EXIT_FAILURE, "Invalid bus interface: %s", i_arg);
	}

	for ( ; v >= USBPF_HDR_LEN; v >>= 1) {
		(void)ioctl(fd, BIOCSBLEN, (caddr_t)&v);
		(void)strlcpy(ifr.ifr_name, i_arg, sizeof(ifr.ifr_name));
		if (ioctl(fd, BIOCSETIF, (caddr_t)&ifr) >= 0)
			break;
	}
	if (v == 0)
		errx(EXIT_FAILURE, "No buffer size worked.");

	if (ioctl(fd, BIOCGBLEN, (caddr_t)&v) < 0)
		err(EXIT_FAILURE, "BIOCGBLEN ioctl failed");

	p->bufsize = v;
	p->buffer = (uint8_t *)malloc(p->bufsize);
	if (p->buffer == NULL)
		errx(EX_SOFTWARE, "Out of memory.");

	make_filter(&total_prog, snapshot);

	if (ioctl(p->fd, BIOCSETF, (caddr_t)&total_prog) < 0)
		err(EXIT_FAILURE, "BIOCSETF ioctl failed");

	free_filter(&total_prog);

	/* 1 second read timeout */
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (ioctl(p->fd, BIOCSRTIMEOUT, (caddr_t)&tv) < 0)
		err(EXIT_FAILURE, "BIOCSRTIMEOUT ioctl failed");

	(void)signal(SIGINT, handle_sigint);

	do_loop(p);

	if (ioctl(fd, BIOCGSTATS, (caddr_t)&us) < 0)
		err(EXIT_FAILURE, "BIOCGSTATS ioctl failed");

	/* XXX what's difference between pkt_captured and us.us_recv? */
	printf("\n");
	printf("%d packets captured\n", pkt_captured);
	printf("%d packets received by filter\n", us.bs_recv);
	printf("%d packets dropped by kernel\n", us.bs_drop);

	/*
	 * Destroy the usbusN interface only if it was created by
	 * usbdump(8).  Ignore when it was already destroyed.
	 */
	if (ifindex == 0 && if_nametoindex(i_arg) > 0) {
		(void)strlcpy(ifr.ifr_name, i_arg, sizeof(ifr.ifr_name));
		if (ioctl(s, SIOCIFDESTROY, &ifr) < 0)
			warn("SIOCIFDESTROY ioctl failed");
	}
	close(s);

	if (p->fd > 0)
		close(p->fd);
	if (p->rfd > 0)
		close(p->rfd);
	if (p->wfd > 0)
		close(p->wfd);
	if (p->bfd > 0)
		close(p->bfd);

	return (EXIT_SUCCESS);
}
