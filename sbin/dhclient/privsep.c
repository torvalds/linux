/*	$OpenBSD: privsep.c,v 1.7 2004/05/10 18:34:42 deraadt Exp $ */

/*
 * Copyright (c) 2004 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE, ABUSE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "dhcpd.h"
#include "privsep.h"

struct buf *
buf_open(size_t len)
{
	struct buf	*buf;

	if ((buf = calloc(1, sizeof(struct buf))) == NULL)
		return (NULL);
	if ((buf->buf = malloc(len)) == NULL) {
		free(buf);
		return (NULL);
	}
	buf->size = len;

	return (buf);
}

int
buf_add(struct buf *buf, const void *data, size_t len)
{
	if (buf->wpos + len > buf->size)
		return (-1);

	memcpy(buf->buf + buf->wpos, data, len);
	buf->wpos += len;
	return (0);
}

int
buf_close(int sock, struct buf *buf)
{
	ssize_t	n;

	do {
		n = write(sock, buf->buf + buf->rpos, buf->size - buf->rpos);
		if (n != -1)
			buf->rpos += n;
		if (n == 0) {			/* connection closed */
			errno = 0;
			return (-1);
		}
	} while (n == -1 && (errno == EAGAIN || errno == EINTR));

	if (buf->rpos < buf->size)
		error("short write: wanted %lu got %ld bytes",
		    (unsigned long)buf->size, (long)buf->rpos);

	free(buf->buf);
	free(buf);
	return (n);
}

ssize_t
buf_read(int sock, void *buf, size_t nbytes)
{
	ssize_t	n;
	size_t r = 0;
	char *p = buf;

	do {
		n = read(sock, p, nbytes);
		if (n == 0)
			error("connection closed");
		if (n != -1) {
			r += (size_t)n;
			p += n;
			nbytes -= n;
		}
	} while (n == -1 && (errno == EINTR || errno == EAGAIN));

	if (n == -1)
		error("buf_read: %m");

	if (r < nbytes)
		error("short read: wanted %lu got %ld bytes",
		    (unsigned long)nbytes, (long)r);

	return (r);
}

void
dispatch_imsg(struct interface_info *ifix, int fd)
{
	struct imsg_hdr		 hdr;
	char			*medium, *reason, *filename,
				*servername, *prefix;
	size_t			 medium_len, reason_len, filename_len,
				 servername_len, optlen, prefix_len, totlen;
	struct client_lease	 lease;
	int			 ret, i;
	struct buf		*buf;
	u_int16_t		mtu;

	buf_read(fd, &hdr, sizeof(hdr));

	switch (hdr.code) {
	case IMSG_SCRIPT_INIT:
		if (hdr.len < sizeof(hdr) + sizeof(size_t))
			error("corrupted message received");
		buf_read(fd, &medium_len, sizeof(medium_len));
		if (hdr.len < medium_len + sizeof(size_t) + sizeof(hdr)
		    + sizeof(size_t) || medium_len == SIZE_T_MAX)
			error("corrupted message received");
		if (medium_len > 0) {
			if ((medium = calloc(1, medium_len + 1)) == NULL)
				error("%m");
			buf_read(fd, medium, medium_len);
		} else
			medium = NULL;

		buf_read(fd, &reason_len, sizeof(reason_len));
		if (hdr.len < medium_len + reason_len + sizeof(hdr) ||
		    reason_len == SIZE_T_MAX)
			error("corrupted message received");
		if (reason_len > 0) {
			if ((reason = calloc(1, reason_len + 1)) == NULL)
				error("%m");
			buf_read(fd, reason, reason_len);
		} else
			reason = NULL;

		priv_script_init(reason, medium);
		free(reason);
		free(medium);
		break;
	case IMSG_SCRIPT_WRITE_PARAMS:
		bzero(&lease, sizeof lease);
		totlen = sizeof(hdr) + sizeof(lease) + sizeof(size_t);
		if (hdr.len < totlen)
			error("corrupted message received");
		buf_read(fd, &lease, sizeof(lease));

		buf_read(fd, &filename_len, sizeof(filename_len));
		totlen += filename_len + sizeof(size_t);
		if (hdr.len < totlen || filename_len == SIZE_T_MAX)
			error("corrupted message received");
		if (filename_len > 0) {
			if ((filename = calloc(1, filename_len + 1)) == NULL)
				error("%m");
			buf_read(fd, filename, filename_len);
		} else
			filename = NULL;

		buf_read(fd, &servername_len, sizeof(servername_len));
		totlen += servername_len + sizeof(size_t);
		if (hdr.len < totlen || servername_len == SIZE_T_MAX)
			error("corrupted message received");
		if (servername_len > 0) {
			if ((servername =
			    calloc(1, servername_len + 1)) == NULL)
				error("%m");
			buf_read(fd, servername, servername_len);
		} else
			servername = NULL;

		buf_read(fd, &prefix_len, sizeof(prefix_len));
		totlen += prefix_len;
		if (hdr.len < totlen || prefix_len == SIZE_T_MAX)
			error("corrupted message received");
		if (prefix_len > 0) {
			if ((prefix = calloc(1, prefix_len + 1)) == NULL)
				error("%m");
			buf_read(fd, prefix, prefix_len);
		} else
			prefix = NULL;

		for (i = 0; i < 256; i++) {
			totlen += sizeof(optlen);
			if (hdr.len < totlen)
				error("corrupted message received");
			buf_read(fd, &optlen, sizeof(optlen));
			lease.options[i].data = NULL;
			lease.options[i].len = optlen;
			if (optlen > 0) {
				totlen += optlen;
				if (hdr.len < totlen || optlen == SIZE_T_MAX)
					error("corrupted message received");
				lease.options[i].data =
				    calloc(1, optlen + 1);
				if (lease.options[i].data == NULL)
				    error("%m");
				buf_read(fd, lease.options[i].data, optlen);
			}
		}
		lease.server_name = servername;
		lease.filename = filename;

		priv_script_write_params(prefix, &lease);

		free(servername);
		free(filename);
		free(prefix);
		for (i = 0; i < 256; i++)
			if (lease.options[i].len > 0)
				free(lease.options[i].data);
		break;
	case IMSG_SCRIPT_GO:
		if (hdr.len != sizeof(hdr))
			error("corrupted message received");

		ret = priv_script_go();

		hdr.code = IMSG_SCRIPT_GO_RET;
		hdr.len = sizeof(struct imsg_hdr) + sizeof(int);
		if ((buf = buf_open(hdr.len)) == NULL)
			error("buf_open: %m");
		if (buf_add(buf, &hdr, sizeof(hdr)))
			error("buf_add: %m");
		if (buf_add(buf, &ret, sizeof(ret)))
			error("buf_add: %m");
		if (buf_close(fd, buf) == -1)
			error("buf_close: %m");
		break;
	case IMSG_SEND_PACKET:
		send_packet_priv(ifix, &hdr, fd);
		break;
	case IMSG_SET_INTERFACE_MTU:
		if (hdr.len < sizeof(hdr) + sizeof(u_int16_t))
			error("corrupted message received");	
	
		buf_read(fd, &mtu, sizeof(u_int16_t));
		interface_set_mtu_priv(ifix->name, mtu);
		break;
	default:
		error("received unknown message, code %d", hdr.code);
	}
}
