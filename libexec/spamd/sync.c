/*	$OpenBSD: sync.c,v 1.14 2021/12/15 17:06:01 tb Exp $	*/

/*
 * Copyright (c) 2006, 2007 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/queue.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sha1.h>
#include <syslog.h>
#include <stdint.h>

#include <netdb.h>

#include <openssl/hmac.h>

#include "sdl.h"
#include "grey.h"
#include "sync.h"

extern struct syslog_data sdata;
extern int debug;
extern FILE *grey;
extern int greylist;

u_int32_t sync_counter;
int syncfd;
int sendmcast;
struct sockaddr_in sync_in;
struct sockaddr_in sync_out;
static char *sync_key;

struct sync_host {
	LIST_ENTRY(sync_host)	h_entry;

	char			*h_name;
	struct sockaddr_in	sh_addr;
};
LIST_HEAD(synchosts, sync_host) sync_hosts = LIST_HEAD_INITIALIZER(sync_hosts);

void	 sync_send(struct iovec *, int);
void	 sync_addr(time_t, time_t, char *, u_int16_t);

int
sync_addhost(const char *name, u_short port)
{
	struct addrinfo hints, *res, *res0;
	struct sync_host *shost;
	struct sockaddr_in *addr = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(name, NULL, &hints, &res0) != 0)
		return (EINVAL);
	for (res = res0; res != NULL; res = res->ai_next) {
		if (addr == NULL && res->ai_family == AF_INET) {
			addr = (struct sockaddr_in *)res->ai_addr;
			break;
		}
	}
	if (addr == NULL) {
		freeaddrinfo(res0);
		return (EINVAL);
	}
	if ((shost = (struct sync_host *)
	    calloc(1, sizeof(struct sync_host))) == NULL) {
		freeaddrinfo(res0);
		return (ENOMEM);
	}
	if ((shost->h_name = strdup(name)) == NULL) {
		free(shost);
		freeaddrinfo(res0);
		return (ENOMEM);
	}

	shost->sh_addr.sin_family = AF_INET;
	shost->sh_addr.sin_port = htons(port);
	shost->sh_addr.sin_addr.s_addr = addr->sin_addr.s_addr;
	freeaddrinfo(res0);

	LIST_INSERT_HEAD(&sync_hosts, shost, h_entry);

	if (debug)
		fprintf(stderr, "added spam sync host %s "
		    "(address %s, port %d)\n", shost->h_name,
		    inet_ntoa(shost->sh_addr.sin_addr), port);

	return (0);
}

int
sync_init(const char *iface, const char *baddr, u_short port)
{
	int one = 1;
	u_int8_t ttl;
	struct ifreq ifr;
	struct ip_mreq mreq;
	struct sockaddr_in *addr;
	char ifnam[IFNAMSIZ], *ttlstr;
	const char *errstr;
	struct in_addr ina;

	if (iface != NULL)
		sendmcast++;

	memset(&ina, 0, sizeof(ina));
	if (baddr != NULL) {
		if (inet_pton(AF_INET, baddr, &ina) != 1) {
			ina.s_addr = htonl(INADDR_ANY);
			if (iface == NULL)
				iface = baddr;
			else if (iface != NULL && strcmp(baddr, iface) != 0) {
				fprintf(stderr, "multicast interface does "
				    "not match");
				return (-1);
			}
		}
	}

	sync_key = SHA1File(SPAM_SYNC_KEY, NULL);
	if (sync_key == NULL) {
		if (errno != ENOENT) {
			fprintf(stderr, "failed to open sync key: %s\n",
			    strerror(errno));
			return (-1);
		}
		/* Use empty key by default */
		sync_key = "";
	}

	syncfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (syncfd == -1)
		return (-1);

	if (setsockopt(syncfd, SOL_SOCKET, SO_REUSEADDR, &one,
	    sizeof(one)) == -1)
		goto fail;

	memset(&sync_out, 0, sizeof(sync_out));
	sync_out.sin_family = AF_INET;
	sync_out.sin_len = sizeof(sync_out);
	sync_out.sin_addr.s_addr = ina.s_addr;
	if (baddr == NULL && iface == NULL)
		sync_out.sin_port = 0;
	else
		sync_out.sin_port = htons(port);

	if (bind(syncfd, (struct sockaddr *)&sync_out, sizeof(sync_out)) == -1)
		goto fail;

	/* Don't use multicast messages */
	if (iface == NULL)
		return (syncfd);

	strlcpy(ifnam, iface, sizeof(ifnam));
	ttl = SPAM_SYNC_MCASTTTL;
	if ((ttlstr = strchr(ifnam, ':')) != NULL) {
		*ttlstr++ = '\0';
		ttl = (u_int8_t)strtonum(ttlstr, 1, UINT8_MAX, &errstr);
		if (errstr) {
			fprintf(stderr, "invalid multicast ttl %s: %s",
			    ttlstr, errstr);
			goto fail;
		}
	}

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));
	if (ioctl(syncfd, SIOCGIFADDR, &ifr) == -1)
		goto fail;

	memset(&sync_in, 0, sizeof(sync_in));
	addr = (struct sockaddr_in *)&ifr.ifr_addr;
	sync_in.sin_family = AF_INET;
	sync_in.sin_len = sizeof(sync_in);
	sync_in.sin_addr.s_addr = addr->sin_addr.s_addr;
	sync_in.sin_port = htons(port);

	memset(&mreq, 0, sizeof(mreq));
	sync_out.sin_addr.s_addr = inet_addr(SPAM_SYNC_MCASTADDR);
	mreq.imr_multiaddr.s_addr = inet_addr(SPAM_SYNC_MCASTADDR);
	mreq.imr_interface.s_addr = sync_in.sin_addr.s_addr;

	if (setsockopt(syncfd, IPPROTO_IP,
	    IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
		fprintf(stderr, "failed to add multicast membership to %s: %s",
		    SPAM_SYNC_MCASTADDR, strerror(errno));
		goto fail;
	}
	if (setsockopt(syncfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
	    sizeof(ttl)) == -1) {
		fprintf(stderr, "failed to set multicast ttl to "
		    "%u: %s\n", ttl, strerror(errno));
		setsockopt(syncfd, IPPROTO_IP,
		    IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
		goto fail;
	}

	if (debug)
		printf("using multicast spam sync %smode "
		    "(ttl %u, group %s, port %d)\n",
		    sendmcast ? "" : "receive ",
		    ttl, inet_ntoa(sync_out.sin_addr), port);

	return (syncfd);

 fail:
	close(syncfd);
	return (-1);
}

void
sync_recv(void)
{
	struct spam_synchdr *hdr;
	struct sockaddr_in addr;
	struct spam_synctlv_hdr *tlv;
	struct spam_synctlv_grey *sg;
	struct spam_synctlv_addr *sd;
	u_int8_t buf[SPAM_SYNC_MAXSIZE];
	u_int8_t hmac[2][SPAM_SYNC_HMAC_LEN];
	struct in_addr ip;
	char *from, *to, *helo;
	u_int8_t *p;
	socklen_t addr_len;
	ssize_t len;
	u_int hmac_len;
	u_int32_t expire;

	memset(&addr, 0, sizeof(addr));
	memset(buf, 0, sizeof(buf));

	addr_len = sizeof(addr);
	if ((len = recvfrom(syncfd, buf, sizeof(buf), 0,
	    (struct sockaddr *)&addr, &addr_len)) < 1)
		return;
	if (addr.sin_addr.s_addr != htonl(INADDR_ANY) &&
	    bcmp(&sync_in.sin_addr, &addr.sin_addr,
	    sizeof(addr.sin_addr)) == 0)
		return;

	/* Ignore invalid or truncated packets */
	hdr = (struct spam_synchdr *)buf;
	if (len < sizeof(struct spam_synchdr) ||
	    hdr->sh_version != SPAM_SYNC_VERSION ||
	    hdr->sh_af != AF_INET ||
	    len < ntohs(hdr->sh_length))
		goto trunc;
	len = ntohs(hdr->sh_length);

	/* Compute and validate HMAC */
	memcpy(hmac[0], hdr->sh_hmac, SPAM_SYNC_HMAC_LEN);
	explicit_bzero(hdr->sh_hmac, SPAM_SYNC_HMAC_LEN);
	HMAC(EVP_sha1(), sync_key, strlen(sync_key), buf, len,
	    hmac[1], &hmac_len);
	if (bcmp(hmac[0], hmac[1], SPAM_SYNC_HMAC_LEN) != 0)
		goto trunc;

	if (debug)
		fprintf(stderr,
		    "%s(sync): received packet of %d bytes\n",
		    inet_ntoa(addr.sin_addr), (int)len);

	p = (u_int8_t *)(hdr + 1);
	while (len) {
		tlv = (struct spam_synctlv_hdr *)p;

		if (len < sizeof(struct spam_synctlv_hdr) ||
		    len < ntohs(tlv->st_length))
			goto trunc;

		switch (ntohs(tlv->st_type)) {
		case SPAM_SYNC_GREY:
			sg = (struct spam_synctlv_grey *)tlv;
			if ((sizeof(*sg) +
			    ntohs(sg->sg_from_length) +
			    ntohs(sg->sg_to_length) +
			    ntohs(sg->sg_helo_length)) >
			    ntohs(tlv->st_length))
				goto trunc;

			ip.s_addr = sg->sg_ip;
			from = (char *)(sg + 1);
			to = from + ntohs(sg->sg_from_length);
			helo = to + ntohs(sg->sg_to_length);
			if (debug) {
				fprintf(stderr, "%s(sync): "
				    "received grey entry ",
				    inet_ntoa(addr.sin_addr));
				fprintf(stderr, "helo %s ip %s "
				    "from %s to %s\n",
				    helo, inet_ntoa(ip), from, to);
			}
			if (greylist) {
				/* send this info to the greylister */
				fprintf(grey,
				    "SYNC\nHE:%s\nIP:%s\nFR:%s\nTO:%s\n",
				    helo, inet_ntoa(ip), from, to);
				fflush(grey);
			}
			break;
		case SPAM_SYNC_WHITE:
			sd = (struct spam_synctlv_addr *)tlv;
			if (sizeof(*sd) != ntohs(tlv->st_length))
				goto trunc;

			ip.s_addr = sd->sd_ip;
			expire = ntohl(sd->sd_expire);
			if (debug) {
				fprintf(stderr, "%s(sync): "
				    "received white entry ",
				    inet_ntoa(addr.sin_addr));
				fprintf(stderr, "ip %s ", inet_ntoa(ip));
			}
			if (greylist) {
				/* send this info to the greylister */
				fprintf(grey, "WHITE:%s:", inet_ntoa(ip));
				fprintf(grey, "%s:%u\n",
				    inet_ntoa(addr.sin_addr), expire);
				fflush(grey);
			}
			break;
		case SPAM_SYNC_TRAPPED:
			sd = (struct spam_synctlv_addr *)tlv;
			if (sizeof(*sd) != ntohs(tlv->st_length))
				goto trunc;

			ip.s_addr = sd->sd_ip;
			expire = ntohl(sd->sd_expire);
			if (debug) {
				fprintf(stderr, "%s(sync): "
				    "received trapped entry ",
				    inet_ntoa(addr.sin_addr));
				fprintf(stderr, "ip %s ", inet_ntoa(ip));
			}
			if (greylist) {
				/* send this info to the greylister */
				fprintf(grey, "TRAP:%s:", inet_ntoa(ip));
				fprintf(grey, "%s:%u\n",
				    inet_ntoa(addr.sin_addr), expire);
				fflush(grey);
			}
			break;
		case SPAM_SYNC_END:
			goto done;
		default:
			printf("invalid type: %d\n", ntohs(tlv->st_type));
			goto trunc;
		}
		len -= ntohs(tlv->st_length);
		p = ((u_int8_t *)tlv) + ntohs(tlv->st_length);
	}

 done:
	return;

 trunc:
	if (debug)
		fprintf(stderr, "%s(sync): truncated or invalid packet\n",
		    inet_ntoa(addr.sin_addr));
}

void
sync_send(struct iovec *iov, int iovlen)
{
	struct sync_host *shost;
	struct msghdr msg;

	/* setup buffer */
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	if (sendmcast) {
		if (debug)
			fprintf(stderr, "sending multicast sync message\n");
		msg.msg_name = &sync_out;
		msg.msg_namelen = sizeof(sync_out);
		sendmsg(syncfd, &msg, 0);
	}

	LIST_FOREACH(shost, &sync_hosts, h_entry) {
		if (debug)
			fprintf(stderr, "sending sync message to %s (%s)\n",
			    shost->h_name, inet_ntoa(shost->sh_addr.sin_addr));
		msg.msg_name = &shost->sh_addr;
		msg.msg_namelen = sizeof(shost->sh_addr);
		sendmsg(syncfd, &msg, 0);
	}
}

void
sync_update(time_t now, char *helo, char *ip, char *from, char *to)
{
	struct iovec iov[7];
	struct spam_synchdr hdr;
	struct spam_synctlv_grey sg;
	struct spam_synctlv_hdr end;
	u_int16_t sglen, fromlen, tolen, helolen, padlen;
	char pad[SPAM_ALIGNBYTES];
	int i = 0;
	HMAC_CTX *ctx;
	u_int hmac_len;

	if (debug)
		fprintf(stderr,
		    "sync grey update helo %s ip %s from %s to %s\n",
		    helo, ip, from, to);

	memset(&hdr, 0, sizeof(hdr));
	memset(&sg, 0, sizeof(sg));
	memset(&pad, 0, sizeof(pad));

	fromlen = strlen(from) + 1;
	tolen = strlen(to) + 1;
	helolen = strlen(helo) + 1;

	if ((ctx = HMAC_CTX_new()) == NULL)
		goto bad;
	if (!HMAC_Init_ex(ctx, sync_key, strlen(sync_key), EVP_sha1(), NULL))
		goto bad;

	sglen = sizeof(sg) + fromlen + tolen + helolen;
	padlen = SPAM_ALIGN(sglen) - sglen;

	/* Add SPAM sync packet header */
	hdr.sh_version = SPAM_SYNC_VERSION;
	hdr.sh_af = AF_INET;
	hdr.sh_counter = htonl(sync_counter++);
	hdr.sh_length = htons(sizeof(hdr) + sglen + padlen + sizeof(end));
	iov[i].iov_base = &hdr;
	iov[i].iov_len = sizeof(hdr);
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	/* Add single SPAM sync greylisting entry */
	sg.sg_type = htons(SPAM_SYNC_GREY);
	sg.sg_length = htons(sglen + padlen);
	sg.sg_timestamp = htonl(now);
	sg.sg_ip = inet_addr(ip);
	sg.sg_from_length = htons(fromlen);
	sg.sg_to_length = htons(tolen);
	sg.sg_helo_length = htons(helolen);
	iov[i].iov_base = &sg;
	iov[i].iov_len = sizeof(sg);
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	iov[i].iov_base = from;
	iov[i].iov_len = fromlen;
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	iov[i].iov_base = to;
	iov[i].iov_len = tolen;
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	iov[i].iov_base = helo;
	iov[i].iov_len = helolen;
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	iov[i].iov_base = pad;
	iov[i].iov_len = padlen;
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	/* Add end marker */
	end.st_type = htons(SPAM_SYNC_END);
	end.st_length = htons(sizeof(end));
	iov[i].iov_base = &end;
	iov[i].iov_len = sizeof(end);
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	if (!HMAC_Final(ctx, hdr.sh_hmac, &hmac_len))
		goto bad;

	/* Send message to the target hosts */
	sync_send(iov, i);

 bad:
	HMAC_CTX_free(ctx);
}

void
sync_addr(time_t now, time_t expire, char *ip, u_int16_t type)
{
	struct iovec iov[3];
	struct spam_synchdr hdr;
	struct spam_synctlv_addr sd;
	struct spam_synctlv_hdr end;
	int i = 0;
	HMAC_CTX *ctx;
	u_int hmac_len;

	if (debug)
		fprintf(stderr, "sync %s %s\n",
			type == SPAM_SYNC_WHITE ? "white" : "trapped", ip);

	memset(&hdr, 0, sizeof(hdr));
	memset(&sd, 0, sizeof(sd));

	if ((ctx = HMAC_CTX_new()) == NULL)
		goto bad;
	if (!HMAC_Init_ex(ctx, sync_key, strlen(sync_key), EVP_sha1(), NULL))
		goto bad;

	/* Add SPAM sync packet header */
	hdr.sh_version = SPAM_SYNC_VERSION;
	hdr.sh_af = AF_INET;
	hdr.sh_counter = htonl(sync_counter++);
	hdr.sh_length = htons(sizeof(hdr) + sizeof(sd) + sizeof(end));
	iov[i].iov_base = &hdr;
	iov[i].iov_len = sizeof(hdr);
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	/* Add single SPAM sync address entry */
	sd.sd_type = htons(type);
	sd.sd_length = htons(sizeof(sd));
	sd.sd_timestamp = htonl(now);
	sd.sd_expire = htonl(expire);
	sd.sd_ip = inet_addr(ip);
	iov[i].iov_base = &sd;
	iov[i].iov_len = sizeof(sd);
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	/* Add end marker */
	end.st_type = htons(SPAM_SYNC_END);
	end.st_length = htons(sizeof(end));
	iov[i].iov_base = &end;
	iov[i].iov_len = sizeof(end);
	if (!HMAC_Update(ctx, iov[i].iov_base, iov[i].iov_len))
		goto bad;
	i++;

	if (!HMAC_Final(ctx, hdr.sh_hmac, &hmac_len))
		goto bad;

	/* Send message to the target hosts */
	sync_send(iov, i);

 bad:
	HMAC_CTX_free(ctx);
}

void
sync_white(time_t now, time_t expire, char *ip)
{
	sync_addr(now, expire, ip, SPAM_SYNC_WHITE);
}

void
sync_trapped(time_t now, time_t expire, char *ip)
{
	sync_addr(now, expire, ip, SPAM_SYNC_TRAPPED);
}
