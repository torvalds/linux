/*	$OpenBSD: radiusctl.c,v 1.17 2024/11/21 13:43:10 claudio Exp $	*/
/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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
#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <inttypes.h>
#include <md5.h>
#include <netdb.h>
#include <radius.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "parser.h"
#include "radiusd.h"
#include "radiusd_ipcp.h"
#include "chap_ms.h"
#include "json.h"

#ifndef MAXIMUM
#define MAXIMUM(_a, _b)	(((_a) > (_b))? (_a) : (_b))
#endif

static int		 radius_test(struct parse_result *);
static void		 radius_dump(FILE *, RADIUS_PACKET *, bool,
			    const char *);

static int		 ipcp_handle_imsg(struct parse_result *, struct imsg *,
			    int);
static void		 ipcp_handle_show(struct radiusd_ipcp_db_dump *,
			    size_t, int);
static void		 ipcp_handle_dumps(struct radiusd_ipcp_db_dump *,
			    size_t, int);
static void		 ipcp_handle_dump(struct radiusd_ipcp_db_dump *,
			    size_t, int);
static void		 ipcp_handle_dump0(struct radiusd_ipcp_db_dump *,
			    size_t, struct timespec *, struct timespec *,
			    struct timespec *, int);
static void		 ipcp_handle_stat(struct radiusd_ipcp_statistics *);
static void		 ipcp_handle_jsons(struct radiusd_ipcp_db_dump *,
			    size_t, int);
static void		 ipcp_handle_json(struct radiusd_ipcp_db_dump *,
			    size_t, struct radiusd_ipcp_statistics *, int);
static void		 ipcp_handle_json0(struct radiusd_ipcp_db_dump *,
			    size_t, struct timespec *, struct timespec *,
			    struct timespec *, int);

static const char	*radius_code_str(int code);
static const char	*hexstr(const u_char *, int, char *, int);
static const char	*sockaddr_str(struct sockaddr *, char *, size_t);
static const char	*time_long_str(struct timespec *, char *, size_t);
static const char	*time_short_str(struct timespec *, struct timespec *,
			    char *, size_t);
static const char	*humanize_seconds(long, char *, size_t);

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s command [argument ...]\n", __progname);
}

int
main(int argc, char *argv[])
{
	int			 ch, sock, done = 0;
	ssize_t			 n;
	struct parse_result	*res;
	struct sockaddr_un	 sun;
	struct imsgbuf		 ibuf;
	struct imsg		 imsg;
	struct iovec		 iov[5];
	int			 niov = 0, cnt = 0;
	char			 module_name[RADIUSD_MODULE_NAME_LEN + 1];

	while ((ch = getopt(argc, argv, "")) != -1)
		switch (ch) {
		default:
			usage();
			return (EXIT_FAILURE);
		}
	argc -= optind;
	argv += optind;

	if (unveil(RADIUSD_SOCK, "rw") == -1)
		err(EX_OSERR, "unveil");
	if (pledge("stdio unix rpath dns inet", NULL) == -1)
		err(EX_OSERR, "pledge");

	res = parse(argc, argv);
	if (res == NULL)
		exit(EX_USAGE);

	switch (res->action) {
	default:
		break;
	case NONE:
		exit(EXIT_SUCCESS);
		break;
	case TEST:
		if (pledge("stdio dns inet", NULL) == -1)
			err(EXIT_FAILURE, "pledge");
		exit(radius_test(res));
		break;
	}

	if (pledge("stdio unix rpath", NULL) == -1)
		err(EX_OSERR, "pledge");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	sun.sun_len = sizeof(sun);
	strlcpy(sun.sun_path, RADIUSD_SOCK, sizeof(sun.sun_path));

	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		err(EX_OSERR, "socket");
	if (connect(sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(EX_OSERR, "connect");
	if (imsgbuf_init(&ibuf, sock) == -1)
		err(EX_OSERR, "imsgbuf_init");

	res = parse(argc, argv);
	if (res == NULL)
		exit(EX_USAGE);

	switch (res->action) {
	case TEST:
	case NONE:
		abort();
		break;
	case IPCP_SHOW:
	case IPCP_DUMP:
	case IPCP_MONITOR:
		memset(module_name, 0, sizeof(module_name));
		strlcpy(module_name, "ipcp",
		    sizeof(module_name));
		iov[niov].iov_base = module_name;
		iov[niov++].iov_len = RADIUSD_MODULE_NAME_LEN;
		imsg_composev(&ibuf, (res->action == IPCP_MONITOR)?
		    IMSG_RADIUSD_MODULE_IPCP_MONITOR :
		    IMSG_RADIUSD_MODULE_IPCP_DUMP, 0, 0, -1, iov, niov);
		break;
	case IPCP_DELETE:
	case IPCP_DISCONNECT:
		memset(module_name, 0, sizeof(module_name));
		strlcpy(module_name, "ipcp",
		    sizeof(module_name));
		iov[niov].iov_base = module_name;
		iov[niov++].iov_len = RADIUSD_MODULE_NAME_LEN;
		iov[niov].iov_base = &res->session_seq;
		iov[niov++].iov_len = sizeof(res->session_seq);
		imsg_composev(&ibuf,
		    (res->action == IPCP_DELETE)
		    ? IMSG_RADIUSD_MODULE_IPCP_DELETE
		    : IMSG_RADIUSD_MODULE_IPCP_DISCONNECT, 0, 0, -1, iov, niov);
		break;
	}
	if (imsgbuf_flush(&ibuf) == -1)
		err(1, "ibuf_ctl: imsgbuf_flush error");
	while (!done) {
		if (imsgbuf_read(&ibuf) != 1)
			break;
		for (;;) {
			if ((n = imsg_get(&ibuf, &imsg)) <= 0) {
				if (n != 0)
					done = 1;
				break;
			}
			switch (res->action) {
			case IPCP_SHOW:
			case IPCP_DUMP:
			case IPCP_MONITOR:
			case IPCP_DELETE:
			case IPCP_DISCONNECT:
				done = ipcp_handle_imsg(res, &imsg, cnt++);
				break;
			default:
				break;
			}
			imsg_free(&imsg);
			if (done)
				break;

		}
	}
	close(sock);

	exit(EXIT_SUCCESS);
}

/***********************************************************************
 * "test"
 ***********************************************************************/
struct radius_test {
	const struct parse_result	*res;
	int				 ecode;

	RADIUS_PACKET			*reqpkt;
	int				 sock;
	unsigned int			 tries;
	struct event			 ev_send;
	struct event			 ev_recv;
	struct event			 ev_timedout;
};

static void	radius_test_send(int, short, void *);
static void	radius_test_recv(int, short, void *);
static void	radius_test_timedout(int, short, void *);

static int
radius_test(struct parse_result *res)
{
	struct radius_test	 test = { .res = res };
	RADIUS_PACKET		*reqpkt;
	struct addrinfo		 hints, *ai;
	int			 sock, retval;
	struct sockaddr_storage	 sockaddr;
	socklen_t		 sockaddrlen;
	struct sockaddr_in	*sin4;
	struct sockaddr_in6	*sin6;
	uint32_t		 u32val;
	uint8_t			 id;

	reqpkt = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);
	if (reqpkt == NULL)
		err(1, "radius_new_request_packet");
	id = arc4random();
	radius_set_id(reqpkt, id);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	retval = getaddrinfo(res->hostname, "radius", &hints, &ai);
	if (retval)
		errx(1, "%s %s", res->hostname, gai_strerror(retval));

	if (res->port != 0)
		((struct sockaddr_in *)ai->ai_addr)->sin_port =
		    htons(res->port);

	sock = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK,
	    ai->ai_protocol);
	if (sock == -1)
		err(1, "socket");

	/* Prepare NAS-IP{,V6}-ADDRESS attribute */
	if (connect(sock, ai->ai_addr, ai->ai_addrlen) == -1)
		err(1, "connect");
	sockaddrlen = sizeof(sockaddr);
	if (getsockname(sock, (struct sockaddr *)&sockaddr, &sockaddrlen) == -1)
		err(1, "getsockname");
	sin4 = (struct sockaddr_in *)&sockaddr;
	sin6 = (struct sockaddr_in6 *)&sockaddr;
	switch (sockaddr.ss_family) {
	case AF_INET:
		radius_put_ipv4_attr(reqpkt, RADIUS_TYPE_NAS_IP_ADDRESS,
		    sin4->sin_addr);
		break;
	case AF_INET6:
		radius_put_raw_attr(reqpkt, RADIUS_TYPE_NAS_IPV6_ADDRESS,
		    sin6->sin6_addr.s6_addr, sizeof(sin6->sin6_addr.s6_addr));
		break;
	}

	/* User-Name and User-Password */
	radius_put_string_attr(reqpkt, RADIUS_TYPE_USER_NAME,
	    res->username);

	switch (res->auth_method) {
	case PAP:
		if (res->password != NULL)
			radius_put_user_password_attr(reqpkt, res->password,
			    res->secret);
		break;
	case CHAP:
	    {
		u_char	 chal[16];
		u_char	 resp[1 + MD5_DIGEST_LENGTH]; /* "1 + " for CHAP Id */
		MD5_CTX	 md5ctx;

		arc4random_buf(chal, sizeof(chal));
		arc4random_buf(resp, 1);	/* CHAP Id is random */
		MD5Init(&md5ctx);
		MD5Update(&md5ctx, resp, 1);
		if (res->password != NULL)
			MD5Update(&md5ctx, res->password,
			    strlen(res->password));
		MD5Update(&md5ctx, chal, sizeof(chal));
		MD5Final(resp + 1, &md5ctx);
		radius_put_raw_attr(reqpkt, RADIUS_TYPE_CHAP_CHALLENGE,
		    chal, sizeof(chal));
		radius_put_raw_attr(reqpkt, RADIUS_TYPE_CHAP_PASSWORD,
		    resp, sizeof(resp));
	    }
		break;
	case MSCHAPV2:
	    {
		u_char	pass[256], chal[16];
		u_int	i, lpass;
		struct _resp {
			u_int8_t ident;
			u_int8_t flags;
			char peer_challenge[16];
			char reserved[8];
			char response[24];
		} __packed resp;

		if (res->password == NULL) {
			lpass = 0;
		} else {
			lpass = strlen(res->password);
			if (lpass * 2 >= sizeof(pass))
				err(1, "password too long");
			for (i = 0; i < lpass; i++) {
				pass[i * 2] = res->password[i];
				pass[i * 2 + 1] = 0;
			}
		}

		memset(&resp, 0, sizeof(resp));
		resp.ident = arc4random();
		arc4random_buf(chal, sizeof(chal));
		arc4random_buf(resp.peer_challenge,
		    sizeof(resp.peer_challenge));

		mschap_nt_response(chal, resp.peer_challenge,
		    (char *)res->username, strlen(res->username), pass,
		    lpass * 2, resp.response);

		radius_put_vs_raw_attr(reqpkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP_CHALLENGE, chal, sizeof(chal));
		radius_put_vs_raw_attr(reqpkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP2_RESPONSE, &resp, sizeof(resp));
		explicit_bzero(pass, sizeof(pass));
	    }
		break;

	}
	u32val = htonl(res->nas_port);
	radius_put_raw_attr(reqpkt, RADIUS_TYPE_NAS_PORT, &u32val, 4);

	if (res->msgauth)
		radius_put_message_authenticator(reqpkt, res->secret);

	event_init();

	test.ecode = EXIT_FAILURE;
	test.res = res;
	test.sock = sock;
	test.reqpkt = reqpkt;

	event_set(&test.ev_recv, sock, EV_READ|EV_PERSIST,
	    radius_test_recv, &test);

	evtimer_set(&test.ev_send, radius_test_send, &test);
	evtimer_set(&test.ev_timedout, radius_test_timedout, &test);

	event_add(&test.ev_recv, NULL);
	evtimer_add(&test.ev_timedout, &res->maxwait);

	/* Send! */
	fprintf(stderr, "Sending:\n");
	radius_dump(stdout, reqpkt, false, res->secret);
	radius_test_send(0, EV_TIMEOUT, &test);

	event_dispatch();

	/* Release the resources */
	radius_delete_packet(reqpkt);
	close(sock);
	freeaddrinfo(ai);

	explicit_bzero((char *)res->secret, strlen(res->secret));
	if (res->password)
		explicit_bzero((char *)res->password, strlen(res->password));

	return (test.ecode);
}

static void
radius_test_send(int thing, short revents, void *arg)
{
	struct radius_test	*test = arg;
	RADIUS_PACKET		*reqpkt = test->reqpkt;
	ssize_t			 rv;

retry:
	rv = send(test->sock,
	    radius_get_data(reqpkt), radius_get_length(reqpkt), 0);
	if (rv == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			goto retry;
		default:
			break;
		}

		warn("send");
	}

	if (++test->tries >= test->res->tries)
		return;

	evtimer_add(&test->ev_send, &test->res->interval);
}

static void
radius_test_recv(int sock, short revents, void *arg)
{
	struct radius_test	*test = arg;
	RADIUS_PACKET		*respkt;
	RADIUS_PACKET		*reqpkt = test->reqpkt;

retry:
	respkt = radius_recv(sock, 0);
	if (respkt == NULL) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			goto retry;
		default:
			break;
		}

		warn("recv");
		return;
	}

	radius_set_request_packet(respkt, reqpkt);
	if (radius_get_id(respkt) == radius_get_id(reqpkt)) {
		fprintf(stderr, "\nReceived:\n");
		radius_dump(stdout, respkt, true, test->res->secret);

		event_del(&test->ev_recv);
		evtimer_del(&test->ev_send);
		evtimer_del(&test->ev_timedout);
		test->ecode = EXIT_SUCCESS;
	}

	radius_delete_packet(respkt);
}

static void
radius_test_timedout(int thing, short revents, void *arg)
{
	struct radius_test	*test = arg;

	event_del(&test->ev_recv);
}

static void
radius_dump(FILE *out, RADIUS_PACKET *pkt, bool resp, const char *secret)
{
	size_t		 len;
	char		 buf[256], buf1[256];
	uint32_t	 u32val;
	struct in_addr	 ipv4;

	fprintf(out,
	    "    Id                        = %d\n"
	    "    Code                      = %s(%d)\n",
	    (int)radius_get_id(pkt), radius_code_str((int)radius_get_code(pkt)),
	    (int)radius_get_code(pkt));
	if (resp && secret) {
		fprintf(out, "    Authenticator             = %s\n",
		    (radius_check_response_authenticator(pkt, secret) == 0)
		    ? "Verified" : "NG");
		fprintf(out, "    Message-Authenticator     = %s\n",
		    (!radius_has_attr(pkt, RADIUS_TYPE_MESSAGE_AUTHENTICATOR))
		    ? "(Not present)"
		    : (radius_check_message_authenticator(pkt, secret) == 0)
		    ? "Verified" : "NG");
	}
	if (!resp)
		fprintf(out, "    Message-Authenticator     = %s\n",
		    (radius_has_attr(pkt, RADIUS_TYPE_MESSAGE_AUTHENTICATOR))
		    ? "(Present)" : "(Not present)");

	if (radius_get_string_attr(pkt, RADIUS_TYPE_USER_NAME, buf,
	    sizeof(buf)) == 0)
		fprintf(out, "    User-Name                 = \"%s\"\n", buf);

	if (secret &&
	    radius_get_user_password_attr(pkt, buf, sizeof(buf), secret) == 0)
		fprintf(out, "    User-Password             = \"%s\"\n", buf);

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_raw_attr(pkt, RADIUS_TYPE_CHAP_PASSWORD, buf, &len)
	    == 0)
		fprintf(out, "    CHAP-Password             = %s\n",
		    (hexstr(buf, len, buf1, sizeof(buf1)))
			    ? buf1 : "(too long)");

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_raw_attr(pkt, RADIUS_TYPE_CHAP_CHALLENGE, buf, &len)
	    == 0)
		fprintf(out, "    CHAP-Challenge            = %s\n",
		    (hexstr(buf, len, buf1, sizeof(buf1)))
			? buf1 : "(too long)");

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MS_CHAP_CHALLENGE, buf, &len) == 0)
		fprintf(out, "    MS-CHAP-Challenge         = %s\n",
		    (hexstr(buf, len, buf1, sizeof(buf1)))
			? buf1 : "(too long)");

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MS_CHAP2_RESPONSE, buf, &len) == 0)
		fprintf(out, "    MS-CHAP2-Response         = %s\n",
		    (hexstr(buf, len, buf1, sizeof(buf1)))
		    ? buf1 : "(too long)");

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf) - 1;
	if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MS_CHAP2_SUCCESS, buf, &len) == 0) {
		fprintf(out, "    MS-CHAP-Success           = Id=%u \"%s\"\n",
		    (u_int)(u_char)buf[0], buf + 1);
	}

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf) - 1;
	if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MS_CHAP_ERROR, buf, &len) == 0) {
		fprintf(out, "    MS-CHAP-Error             = Id=%u \"%s\"\n",
		    (u_int)(u_char)buf[0], buf + 1);
	}

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MPPE_SEND_KEY, buf, &len) == 0)
		fprintf(out, "    MS-MPPE-Send-Key          = %s\n",
		    (hexstr(buf, len, buf1, sizeof(buf1)))
		    ? buf1 : "(too long)");

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MPPE_RECV_KEY, buf, &len) == 0)
		fprintf(out, "    MS-MPPE-Recv-Key          = %s\n",
		    (hexstr(buf, len, buf1, sizeof(buf1)))
		    ? buf1 : "(too long)");

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MPPE_ENCRYPTION_POLICY, buf, &len) == 0)
		fprintf(out, "    MS-MPPE-Encryption-Policy = 0x%08x\n",
		    ntohl(*(u_long *)buf));

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MPPE_ENCRYPTION_TYPES, buf, &len) == 0)
		fprintf(out, "    MS-MPPE-Encryption-Types  = 0x%08x\n",
		    ntohl(*(u_long *)buf));

	if (radius_get_string_attr(pkt, RADIUS_TYPE_REPLY_MESSAGE, buf,
	    sizeof(buf)) == 0)
		fprintf(out, "    Reply-Message             = \"%s\"\n", buf);

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_uint32_attr(pkt, RADIUS_TYPE_NAS_PORT, &u32val) == 0)
		fprintf(out, "    NAS-Port                  = %lu\n",
		    (u_long)u32val);

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_ipv4_attr(pkt, RADIUS_TYPE_NAS_IP_ADDRESS, &ipv4) == 0)
		fprintf(out, "    NAS-IP-Address            = %s\n",
		    inet_ntoa(ipv4));

	memset(buf, 0, sizeof(buf));
	len = sizeof(buf);
	if (radius_get_raw_attr(pkt, RADIUS_TYPE_NAS_IPV6_ADDRESS, buf, &len)
	    == 0)
		fprintf(out, "    NAS-IPv6-Address          = %s\n",
		    inet_ntop(AF_INET6, buf, buf1, len));

}

/***********************************************************************
 * ipcp
 ***********************************************************************/
int
ipcp_handle_imsg(struct parse_result *res, struct imsg *imsg, int cnt)
{
	ssize_t				 datalen;
	struct radiusd_ipcp_db_dump	*dump;
	struct radiusd_ipcp_statistics	*stat;
	int				 done = 0;

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	switch (imsg->hdr.type) {
	case IMSG_OK:
		if (datalen > 0 && *((char *)imsg->data + datalen - 1) == '\0')
			fprintf(stderr, "OK: %s\n", (char *)imsg->data);
		else
			fprintf(stderr, "OK\n");
		done = 1;
		break;
	case IMSG_NG:
		if (datalen > 0 && *((char *)imsg->data + datalen - 1) == '\0')
			fprintf(stderr, "error: %s\n", (char *)imsg->data);
		else
			fprintf(stderr, "error\n");
		exit(EXIT_FAILURE);
	case IMSG_RADIUSD_MODULE_IPCP_DUMP:
		if ((size_t)datalen < sizeof(struct
		    radiusd_ipcp_db_dump))
			errx(1, "received a message which size is invalid");
		dump = imsg->data;
		if (res->action == IPCP_SHOW)
			ipcp_handle_show(dump, datalen, (cnt++ == 0)? 1 : 0);
		else {
			if (res->flags & FLAGS_JSON)
				ipcp_handle_jsons(dump, datalen,
				    (cnt++ == 0)? 1 : 0);
			else
				ipcp_handle_dumps(dump, datalen,
				    (cnt++ == 0)? 1 : 0);
		}
		if (dump->islast &&
		    (res->action == IPCP_SHOW || res->action == IPCP_DUMP))
			done = 1;
		break;
	case IMSG_RADIUSD_MODULE_IPCP_START:
		if ((size_t)datalen < offsetof(struct
		    radiusd_ipcp_db_dump, records[1]))
			errx(1, "received a message which size is invalid");
		dump = imsg->data;
		if (res->flags & FLAGS_JSON)
			ipcp_handle_json(dump, datalen, NULL, 0);
		else {
			printf("Start\n");
			ipcp_handle_dump(dump, datalen, 0);
		}
		break;
	case IMSG_RADIUSD_MODULE_IPCP_STOP:
		if ((size_t)datalen < offsetof(
		    struct radiusd_ipcp_db_dump,
		    records[1]) +
		    sizeof(struct
		    radiusd_ipcp_statistics))
			errx(1, "received a message which size is invalid");
		dump = imsg->data;
		stat = (struct radiusd_ipcp_statistics *)
		    ((char *)imsg->data + offsetof(
			struct radiusd_ipcp_db_dump, records[1]));
		if (res->flags & FLAGS_JSON)
			ipcp_handle_json(dump, datalen, stat, 0);
		else {
			printf("Stop\n");
			ipcp_handle_dump(dump, datalen, 0);
			ipcp_handle_stat(stat);
		}
		break;
	}

	return (done);
}

static void
ipcp_handle_show(struct radiusd_ipcp_db_dump *dump, size_t dumpsiz, int first)
{
	int		 i, width;
	uint32_t	 maxseq = 999;
	char		 buf0[128], buf1[NI_MAXHOST + NI_MAXSERV + 4], buf2[80];
	struct timespec	 upt, now, dif, start;

	clock_gettime(CLOCK_BOOTTIME, &upt);
	clock_gettime(CLOCK_REALTIME, &now);
	timespecsub(&now, &upt, &upt);

	for (i = 0; ; i++) {
		if (offsetof(struct radiusd_ipcp_db_dump, records[i])
		    >= dumpsiz)
			break;
		maxseq = MAXIMUM(maxseq, dump->records[i].rec.seq);
	}
	for (width = 0; maxseq != 0; maxseq /= 10, width++)
		;

	for (i = 0; ; i++) {
		if (offsetof(struct radiusd_ipcp_db_dump, records[i])
		    >= dumpsiz)
			break;
		if (i == 0 && first)
			printf("%-*s Assigned        Username               "
			    "Start    Tunnel From\n"
			    "%.*s --------------- ---------------------- "
			    "-------- %.*s\n", width, "Seq", width,
			    "----------", 28 - width,
			    "-------------------------");
		timespecadd(&upt, &dump->records[i].rec.start, &start);
		timespecsub(&now, &start, &dif);
		printf("%*d %-15s %-22s %-8s %s\n",
		    width, dump->records[i].rec.seq,
		    inet_ntop(dump->records[i].af, &dump->records[i].addr,
		    buf0, sizeof(buf0)), dump->records[i].rec.username,
		    time_short_str(&start, &dif, buf2, sizeof(buf2)),
		    sockaddr_str(
		    (struct sockaddr *)&dump->records[i].rec.tun_client, buf1,
		    sizeof(buf1)));
	}
}
static void
ipcp_handle_dump(struct radiusd_ipcp_db_dump *dump, size_t dumpsiz, int idx)
{
	struct timespec	 upt, now, dif, start, timeout;

	clock_gettime(CLOCK_BOOTTIME, &upt);
	clock_gettime(CLOCK_REALTIME, &now);
	timespecsub(&now, &upt, &upt);

	timespecadd(&upt, &dump->records[idx].rec.start, &start);
	timespecsub(&now, &start, &dif);

	if (dump->records[idx].rec.start.tv_sec == 0)
		ipcp_handle_dump0(dump, dumpsiz, &dif, &start, NULL, idx);
	else {
		timespecadd(&upt, &dump->records[idx].rec.timeout, &timeout);
		ipcp_handle_dump0(dump, dumpsiz, &dif, &start, &timeout, idx);
	}
}

static void
ipcp_handle_dump0(struct radiusd_ipcp_db_dump *dump, size_t dumpsiz,
    struct timespec *dif, struct timespec *start, struct timespec *timeout,
    int idx)
{
	char		 buf0[128], buf1[NI_MAXHOST + NI_MAXSERV + 4], buf2[80];

	printf(
	    "    Sequence Number     : %u\n"
	    "    Session Id          : %s\n"
	    "    Username            : %s\n"
	    "    Auth Method         : %s\n"
	    "    Assigned IP Address : %s\n"
	    "    Start Time          : %s\n"
	    "    Elapsed Time        : %lld second%s%s\n",
	    dump->records[idx].rec.seq, dump->records[idx].rec.session_id,
	    dump->records[idx].rec.username, dump->records[idx].rec.auth_method,
	    inet_ntop(dump->records[idx].af, &dump->records[idx].addr, buf0,
	    sizeof(buf0)), time_long_str(start, buf1, sizeof(buf1)),
	    (long long)dif->tv_sec, (dif->tv_sec == 0)? "" : "s",
	    humanize_seconds(dif->tv_sec, buf2, sizeof(buf2)));
	if (timeout != NULL)
		printf("    Timeout             : %s\n",
		    time_long_str(timeout, buf0, sizeof(buf0)));
	printf(
	    "    NAS Identifier      : %s\n"
	    "    Tunnel Type         : %s\n"
	    "    Tunnel From         : %s\n",
	    dump->records[idx].rec.nas_id, dump->records[idx].rec.tun_type,
	    sockaddr_str((struct sockaddr *)
		&dump->records[idx].rec.tun_client, buf1, sizeof(buf1)));
}

void
ipcp_handle_stat(struct radiusd_ipcp_statistics *stat)
{
	printf(
	    "    Terminate Cause     : %s\n"
	    "    Input Packets       : %"PRIu32"\n"
	    "    Output Packets      : %"PRIu32"\n"
	    "    Input Bytes         : %"PRIu64"\n"
	    "    Output Bytes        : %"PRIu64"\n",
	    stat->cause, stat->ipackets, stat->opackets, stat->ibytes,
	    stat->obytes);
}

static void
ipcp_handle_jsons(struct radiusd_ipcp_db_dump *dump, size_t dumpsiz, int first)
{
	int		 i;
	struct timespec	 upt, now, dif, start, timeout;

	clock_gettime(CLOCK_BOOTTIME, &upt);
	clock_gettime(CLOCK_REALTIME, &now);
	timespecsub(&now, &upt, &upt);

	for (i = 0; ; i++) {
		if (offsetof(struct radiusd_ipcp_db_dump, records[i])
		    >= dumpsiz)
			break;
		timespecadd(&upt, &dump->records[i].rec.start, &start);
		timespecsub(&now, &start, &dif);
		json_do_start(stdout);
		json_do_string("action", "start");
		if (dump->records[i].rec.timeout.tv_sec == 0)
			ipcp_handle_json0(dump, dumpsiz, &dif, &start, NULL, i);
		else {
			timespecadd(&upt, &dump->records[i].rec.timeout,
			    &timeout);
			ipcp_handle_json0(dump, dumpsiz, &dif, &start, &timeout,
			    i);
		}
		json_do_finish();
	}
	fflush(stdout);
}

static void
ipcp_handle_json(struct radiusd_ipcp_db_dump *dump, size_t dumpsiz,
    struct radiusd_ipcp_statistics *stat, int idx)
{
	struct timespec	 upt, now, dif, start, timeout;

	json_do_start(stdout);
	clock_gettime(CLOCK_BOOTTIME, &upt);
	clock_gettime(CLOCK_REALTIME, &now);
	timespecsub(&now, &upt, &upt);
	timespecadd(&upt, &dump->records[idx].rec.start, &start);
	timespecsub(&now, &start, &dif);

	if (stat == NULL)
		json_do_string("action", "start");
	else
		json_do_string("action", "stop");
	if (dump->records[idx].rec.timeout.tv_sec == 0)
		ipcp_handle_json0(dump, dumpsiz, &dif, &start, NULL, idx);
	else {
		timespecadd(&upt, &dump->records[idx].rec.timeout, &timeout);
		ipcp_handle_json0(dump, dumpsiz, &dif, &start, &timeout, idx);
	}
	if (stat != NULL) {
		json_do_string("terminate-cause", stat->cause);
		json_do_uint("input-packets", stat->ipackets);
		json_do_uint("output-packets", stat->opackets);
		json_do_uint("input-bytes", stat->ibytes);
		json_do_uint("output-bytes", stat->obytes);
	}
	json_do_finish();
	fflush(stdout);
}

static void
ipcp_handle_json0(struct radiusd_ipcp_db_dump *dump, size_t dumpsiz,
    struct timespec *dif, struct timespec *start, struct timespec *timeout,
    int idx)
{
	char		 buf[128];

	json_do_uint("sequence-number", dump->records[idx].rec.seq);
	json_do_string("session-id", dump->records[idx].rec.session_id);
	json_do_string("username", dump->records[idx].rec.username);
	json_do_string("auth-method", dump->records[idx].rec.auth_method);
	json_do_string("assigned-ip-address", inet_ntop(dump->records[idx].af,
	    &dump->records[idx].addr, buf, sizeof(buf)));
	json_do_uint("start", start->tv_sec);
	json_do_uint("elapsed", dif->tv_sec);
	if (timeout != NULL)
		json_do_uint("timeout", timeout->tv_sec);
	json_do_string("nas-identifier", dump->records[idx].rec.nas_id);
	json_do_string("tunnel-type", dump->records[idx].rec.tun_type);
	json_do_string("tunnel-from",
	    sockaddr_str((struct sockaddr *)&dump->records[idx].rec.tun_client,
	    buf, sizeof(buf)));
}

static void
ipcp_handle_dumps(struct radiusd_ipcp_db_dump *dump, size_t dumpsiz, int first)
{
	static int	 cnt = 0;
	int		 i;
	struct timespec	 upt, now, dif, start, timeout;

	clock_gettime(CLOCK_BOOTTIME, &upt);
	clock_gettime(CLOCK_REALTIME, &now);
	timespecsub(&now, &upt, &upt);

	if (first)
		cnt = 0;
	for (i = 0; ; i++, cnt++) {
		if (offsetof(struct radiusd_ipcp_db_dump, records[i])
		    >= dumpsiz)
			break;
		timespecadd(&upt, &dump->records[i].rec.start, &start);
		timespecsub(&now, &start, &dif);
		printf("#%d\n", cnt + 1);
		if (dump->records[i].rec.timeout.tv_sec == 0)
			ipcp_handle_dump0(dump, dumpsiz, &dif, &start, NULL, i);
		else {
			timespecadd(&upt, &dump->records[i].rec.timeout,
			    &timeout);
			ipcp_handle_dump0(dump, dumpsiz, &dif, &start,
			    &timeout, i);
		}
	}
}


/***********************************************************************
 * Miscellaneous functions
 ***********************************************************************/
const char *
radius_code_str(int code)
{
	int i;
	static struct _codestr {
		int		 code;
		const char	*str;
	} codestr[] = {
	    { RADIUS_CODE_ACCESS_REQUEST,	"Access-Request" },
	    { RADIUS_CODE_ACCESS_ACCEPT,	"Access-Accept" },
	    { RADIUS_CODE_ACCESS_REJECT,	"Access-Reject" },
	    { RADIUS_CODE_ACCOUNTING_REQUEST,	"Accounting-Request" },
	    { RADIUS_CODE_ACCOUNTING_RESPONSE,	"Accounting-Response" },
	    { RADIUS_CODE_ACCESS_CHALLENGE,	"Access-Challenge" },
	    { RADIUS_CODE_STATUS_SERVER,	"Status-Server" },
	    { RADIUS_CODE_STATUS_CLIENT,	"Status-Client" },
	    { -1, NULL }
	};

	for (i = 0; codestr[i].code != -1; i++) {
		if (codestr[i].code == code)
			return (codestr[i].str);
	}

	return ("Unknown");
}

static const char *
hexstr(const u_char *data, int len, char *str, int strsiz)
{
	int			 i, off = 0;
	static const char	 hex[] = "0123456789abcdef";

	for (i = 0; i < len; i++) {
		if (strsiz - off < 3)
			return (NULL);
		str[off++] = hex[(data[i] & 0xf0) >> 4];
		str[off++] = hex[(data[i] & 0x0f)];
		str[off++] = ' ';
	}
	if (strsiz - off < 1)
		return (NULL);

	str[off++] = '\0';

	return (str);
}

const char *
sockaddr_str(struct sockaddr *sa, char *buf, size_t bufsiz)
{
	int	noport, ret;
	char	hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	if (ntohs(((struct sockaddr_in *)sa)->sin_port) == 0) {
		noport = 1;
		ret = getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf), NULL, 0,
		    NI_NUMERICHOST);
	} else {
		noport = 0;
		ret = getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf), sbuf,
		    sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
	}
	if (ret != 0)
		return "";
	if (noport)
		strlcpy(buf, hbuf, bufsiz);
	else if (sa->sa_family == AF_INET6)
		snprintf(buf, bufsiz, "[%s]:%s", hbuf, sbuf);
	else
		snprintf(buf, bufsiz, "%s:%s", hbuf, sbuf);

	return (buf);
}

const char *
time_long_str(struct timespec *tim, char *buf, size_t bufsiz)
{
	struct tm	 tm;

	localtime_r(&tim->tv_sec, &tm);
	strftime(buf, bufsiz, "%F %T", &tm);

	return (buf);
}

const char *
time_short_str(struct timespec *tim, struct timespec *dif, char *buf,
    size_t bufsiz)
{
	struct tm	 tm;

	localtime_r(&tim->tv_sec, &tm);
	if (dif->tv_sec < 12 * 60 * 60)
		strftime(buf, bufsiz, "%l:%M%p", &tm);
	else if (dif->tv_sec < 7 * 24 * 60 * 60)
		strftime(buf, bufsiz, "%e%b%y", &tm);
	else
		strftime(buf, bufsiz, "%m/%d", &tm);

	return (buf);
}

const char *
humanize_seconds(long seconds, char *buf, size_t bufsiz)
{
	char	 fbuf[80];
	int	 hour, min;

	hour = seconds / 3600;
	min = (seconds % 3600) / 60;

	if (bufsiz == 0)
		return NULL;
	buf[0] = '\0';
	if (hour != 0 || min != 0) {
		strlcat(buf, " (", bufsiz);
		if (hour != 0) {
			snprintf(fbuf, sizeof(fbuf), "%d hour%s", hour,
			    (hour == 1)? "" : "s");
			strlcat(buf, fbuf, bufsiz);
		}
		if (hour != 0 && min != 0)
			strlcat(buf, " and ", bufsiz);
		if (min != 0) {
			snprintf(fbuf, sizeof(fbuf), "%d minute%s", min,
			    (min == 1)? "" : "s");
			strlcat(buf, fbuf, bufsiz);
		}
		strlcat(buf, ")", bufsiz);
	}

	return (buf);
}
