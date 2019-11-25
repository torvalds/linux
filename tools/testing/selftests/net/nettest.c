// SPDX-License-Identifier: GPL-2.0
/* nettest - used for functional tests of networking APIs
 *
 * Copyright (c) 2013-2019 David Ahern <dsahern@gmail.com>. All rights reserved.
 */

#define _GNU_SOURCE
#include <features.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/tcp.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#ifndef IPV6_UNICAST_IF
#define IPV6_UNICAST_IF         76
#endif
#ifndef IPV6_MULTICAST_IF
#define IPV6_MULTICAST_IF       17
#endif

#define DEFAULT_PORT 12345

#ifndef MAX
#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b)  ((a) < (b) ? (a) : (b))
#endif

struct sock_args {
	/* local address */
	union {
		struct in_addr  in;
		struct in6_addr in6;
	} local_addr;

	/* remote address */
	union {
		struct in_addr  in;
		struct in6_addr in6;
	} remote_addr;
	int scope_id;  /* remote scope; v6 send only */

	struct in_addr grp;     /* multicast group */

	unsigned int has_local_ip:1,
		     has_remote_ip:1,
		     has_grp:1,
		     has_expected_laddr:1,
		     has_expected_raddr:1,
		     bind_test_only:1;

	unsigned short port;

	int type;      /* DGRAM, STREAM, RAW */
	int protocol;
	int version;   /* AF_INET/AF_INET6 */

	int use_setsockopt;
	int use_cmsg;
	const char *dev;
	int ifindex;
	const char *password;

	/* expected addresses and device index for connection */
	int expected_ifindex;

	/* local address */
	union {
		struct in_addr  in;
		struct in6_addr in6;
	} expected_laddr;

	/* remote address */
	union {
		struct in_addr  in;
		struct in6_addr in6;
	} expected_raddr;
};

static int server_mode;
static unsigned int prog_timeout = 5;
static unsigned int interactive;
static int iter = 1;
static char *msg = "Hello world!";
static int msglen;
static int quiet;
static int try_broadcast = 1;

static char *timestamp(char *timebuf, int buflen)
{
	time_t now;

	now = time(NULL);
	if (strftime(timebuf, buflen, "%T", localtime(&now)) == 0) {
		memset(timebuf, 0, buflen);
		strncpy(timebuf, "00:00:00", buflen-1);
	}

	return timebuf;
}

static void log_msg(const char *format, ...)
{
	char timebuf[64];
	va_list args;

	if (quiet)
		return;

	fprintf(stdout, "%s %s:",
		timestamp(timebuf, sizeof(timebuf)),
		server_mode ? "server" : "client");
	va_start(args, format);
	vfprintf(stdout, format, args);
	va_end(args);

	fflush(stdout);
}

static void log_error(const char *format, ...)
{
	char timebuf[64];
	va_list args;

	if (quiet)
		return;

	fprintf(stderr, "%s %s:",
		timestamp(timebuf, sizeof(timebuf)),
		server_mode ? "server" : "client");
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fflush(stderr);
}

static void log_err_errno(const char *fmt, ...)
{
	char timebuf[64];
	va_list args;

	if (quiet)
		return;

	fprintf(stderr, "%s %s: ",
		timestamp(timebuf, sizeof(timebuf)),
		server_mode ? "server" : "client");
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, ": %d: %s\n", errno, strerror(errno));
	fflush(stderr);
}

static void log_address(const char *desc, struct sockaddr *sa)
{
	char addrstr[64];

	if (quiet)
		return;

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *) sa;

		log_msg("%s %s:%d",
			desc,
			inet_ntop(AF_INET, &s->sin_addr, addrstr,
				  sizeof(addrstr)),
			ntohs(s->sin_port));

	} else if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) sa;

		log_msg("%s [%s]:%d",
			desc,
			inet_ntop(AF_INET6, &s6->sin6_addr, addrstr,
				  sizeof(addrstr)),
			ntohs(s6->sin6_port));
	}

	printf("\n");

	fflush(stdout);
}

static int tcp_md5sig(int sd, void *addr, socklen_t alen, const char *password)
{
	struct tcp_md5sig md5sig;
	int keylen = password ? strlen(password) : 0;
	int rc;

	memset(&md5sig, 0, sizeof(md5sig));
	memcpy(&md5sig.tcpm_addr, addr, alen);
	md5sig.tcpm_keylen = keylen;

	if (keylen)
		memcpy(md5sig.tcpm_key, password, keylen);

	rc = setsockopt(sd, IPPROTO_TCP, TCP_MD5SIG, &md5sig, sizeof(md5sig));
	if (rc < 0) {
		/* ENOENT is harmless. Returned when a password is cleared */
		if (errno == ENOENT)
			rc = 0;
		else
			log_err_errno("setsockopt(TCP_MD5SIG)");
	}

	return rc;
}

static int tcp_md5_remote(int sd, struct sock_args *args)
{
	struct sockaddr_in sin = {
		.sin_family = AF_INET,
	};
	struct sockaddr_in6 sin6 = {
		.sin6_family = AF_INET6,
	};
	void *addr;
	int alen;

	switch (args->version) {
	case AF_INET:
		sin.sin_port = htons(args->port);
		sin.sin_addr = args->remote_addr.in;
		addr = &sin;
		alen = sizeof(sin);
		break;
	case AF_INET6:
		sin6.sin6_port = htons(args->port);
		sin6.sin6_addr = args->remote_addr.in6;
		addr = &sin6;
		alen = sizeof(sin6);
		break;
	default:
		log_error("unknown address family\n");
		exit(1);
	}

	if (tcp_md5sig(sd, addr, alen, args->password))
		return -1;

	return 0;
}

static int get_ifidx(const char *ifname)
{
	struct ifreq ifdata;
	int sd, rc;

	if (!ifname || *ifname == '\0')
		return -1;

	memset(&ifdata, 0, sizeof(ifdata));

	strcpy(ifdata.ifr_name, ifname);

	sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sd < 0) {
		log_err_errno("socket failed");
		return -1;
	}

	rc = ioctl(sd, SIOCGIFINDEX, (char *)&ifdata);
	close(sd);
	if (rc != 0) {
		log_err_errno("ioctl(SIOCGIFINDEX) failed");
		return -1;
	}

	return ifdata.ifr_ifindex;
}

static int bind_to_device(int sd, const char *name)
{
	int rc;

	rc = setsockopt(sd, SOL_SOCKET, SO_BINDTODEVICE, name, strlen(name)+1);
	if (rc < 0)
		log_err_errno("setsockopt(SO_BINDTODEVICE)");

	return rc;
}

static int get_bind_to_device(int sd, char *name, size_t len)
{
	int rc;
	socklen_t optlen = len;

	name[0] = '\0';
	rc = getsockopt(sd, SOL_SOCKET, SO_BINDTODEVICE, name, &optlen);
	if (rc < 0)
		log_err_errno("setsockopt(SO_BINDTODEVICE)");

	return rc;
}

static int check_device(int sd, struct sock_args *args)
{
	int ifindex = 0;
	char name[32];

	if (get_bind_to_device(sd, name, sizeof(name)))
		*name = '\0';
	else
		ifindex = get_ifidx(name);

	log_msg("    bound to device %s/%d\n",
		*name ? name : "<none>", ifindex);

	if (!args->expected_ifindex)
		return 0;

	if (args->expected_ifindex != ifindex) {
		log_error("Device index mismatch: expected %d have %d\n",
			  args->expected_ifindex, ifindex);
		return 1;
	}

	log_msg("Device index matches: expected %d have %d\n",
		args->expected_ifindex, ifindex);

	return 0;
}

static int set_pktinfo_v4(int sd)
{
	int one = 1;
	int rc;

	rc = setsockopt(sd, SOL_IP, IP_PKTINFO, &one, sizeof(one));
	if (rc < 0 && rc != -ENOTSUP)
		log_err_errno("setsockopt(IP_PKTINFO)");

	return rc;
}

static int set_recvpktinfo_v6(int sd)
{
	int one = 1;
	int rc;

	rc = setsockopt(sd, SOL_IPV6, IPV6_RECVPKTINFO, &one, sizeof(one));
	if (rc < 0 && rc != -ENOTSUP)
		log_err_errno("setsockopt(IPV6_RECVPKTINFO)");

	return rc;
}

static int set_recverr_v4(int sd)
{
	int one = 1;
	int rc;

	rc = setsockopt(sd, SOL_IP, IP_RECVERR, &one, sizeof(one));
	if (rc < 0 && rc != -ENOTSUP)
		log_err_errno("setsockopt(IP_RECVERR)");

	return rc;
}

static int set_recverr_v6(int sd)
{
	int one = 1;
	int rc;

	rc = setsockopt(sd, SOL_IPV6, IPV6_RECVERR, &one, sizeof(one));
	if (rc < 0 && rc != -ENOTSUP)
		log_err_errno("setsockopt(IPV6_RECVERR)");

	return rc;
}

static int set_unicast_if(int sd, int ifindex, int version)
{
	int opt = IP_UNICAST_IF;
	int level = SOL_IP;
	int rc;

	ifindex = htonl(ifindex);

	if (version == AF_INET6) {
		opt = IPV6_UNICAST_IF;
		level = SOL_IPV6;
	}
	rc = setsockopt(sd, level, opt, &ifindex, sizeof(ifindex));
	if (rc < 0)
		log_err_errno("setsockopt(IP_UNICAST_IF)");

	return rc;
}

static int set_multicast_if(int sd, int ifindex)
{
	struct ip_mreqn mreq = { .imr_ifindex = ifindex };
	int rc;

	rc = setsockopt(sd, SOL_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq));
	if (rc < 0)
		log_err_errno("setsockopt(IP_MULTICAST_IF)");

	return rc;
}

static int set_membership(int sd, uint32_t grp, uint32_t addr, int ifindex)
{
	uint32_t if_addr = addr;
	struct ip_mreqn mreq;
	int rc;

	if (addr == htonl(INADDR_ANY) && !ifindex) {
		log_error("Either local address or device needs to be given for multicast membership\n");
		return -1;
	}

	mreq.imr_multiaddr.s_addr = grp;
	mreq.imr_address.s_addr = if_addr;
	mreq.imr_ifindex = ifindex;

	rc = setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
	if (rc < 0) {
		log_err_errno("setsockopt(IP_ADD_MEMBERSHIP)");
		return -1;
	}

	return 0;
}

static int set_broadcast(int sd)
{
	unsigned int one = 1;
	int rc = 0;

	if (setsockopt(sd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) != 0) {
		log_err_errno("setsockopt(SO_BROADCAST)");
		rc = -1;
	}

	return rc;
}

static int set_reuseport(int sd)
{
	unsigned int one = 1;
	int rc = 0;

	if (setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) != 0) {
		log_err_errno("setsockopt(SO_REUSEPORT)");
		rc = -1;
	}

	return rc;
}

static int set_reuseaddr(int sd)
{
	unsigned int one = 1;
	int rc = 0;

	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
		log_err_errno("setsockopt(SO_REUSEADDR)");
		rc = -1;
	}

	return rc;
}

static int str_to_uint(const char *str, int min, int max, unsigned int *value)
{
	int number;
	char *end;

	errno = 0;
	number = (unsigned int) strtoul(str, &end, 0);

	/* entire string should be consumed by conversion
	 * and value should be between min and max
	 */
	if (((*end == '\0') || (*end == '\n')) && (end != str) &&
	    (errno != ERANGE) && (min <= number) && (number <= max)) {
		*value = number;
		return 0;
	}

	return -1;
}

static int expected_addr_match(struct sockaddr *sa, void *expected,
			       const char *desc)
{
	char addrstr[64];
	int rc = 0;

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *) sa;
		struct in_addr *exp_in = (struct in_addr *) expected;

		if (s->sin_addr.s_addr != exp_in->s_addr) {
			log_error("%s address does not match expected %s",
				  desc,
				  inet_ntop(AF_INET, exp_in,
					    addrstr, sizeof(addrstr)));
			rc = 1;
		}
	} else if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) sa;
		struct in6_addr *exp_in = (struct in6_addr *) expected;

		if (memcmp(&s6->sin6_addr, exp_in, sizeof(*exp_in))) {
			log_error("%s address does not match expected %s",
				  desc,
				  inet_ntop(AF_INET6, exp_in,
					    addrstr, sizeof(addrstr)));
			rc = 1;
		}
	} else {
		log_error("%s address does not match expected - unknown family",
			  desc);
		rc = 1;
	}

	if (!rc)
		log_msg("%s address matches expected\n", desc);

	return rc;
}

static int show_sockstat(int sd, struct sock_args *args)
{
	struct sockaddr_in6 local_addr, remote_addr;
	socklen_t alen = sizeof(local_addr);
	struct sockaddr *sa;
	const char *desc;
	int rc = 0;

	desc = server_mode ? "server local:" : "client local:";
	sa = (struct sockaddr *) &local_addr;
	if (getsockname(sd, sa, &alen) == 0) {
		log_address(desc, sa);

		if (args->has_expected_laddr) {
			rc = expected_addr_match(sa, &args->expected_laddr,
						 "local");
		}
	} else {
		log_err_errno("getsockname failed");
	}

	sa = (struct sockaddr *) &remote_addr;
	desc = server_mode ? "server peer:" : "client peer:";
	if (getpeername(sd, sa, &alen) == 0) {
		log_address(desc, sa);

		if (args->has_expected_raddr) {
			rc |= expected_addr_match(sa, &args->expected_raddr,
						 "remote");
		}
	} else {
		log_err_errno("getpeername failed");
	}

	return rc;
}

static int get_index_from_cmsg(struct msghdr *m)
{
	struct cmsghdr *cm;
	int ifindex = 0;
	char buf[64];

	for (cm = (struct cmsghdr *)CMSG_FIRSTHDR(m);
	     m->msg_controllen != 0 && cm;
	     cm = (struct cmsghdr *)CMSG_NXTHDR(m, cm)) {

		if (cm->cmsg_level == SOL_IP &&
		    cm->cmsg_type == IP_PKTINFO) {
			struct in_pktinfo *pi;

			pi = (struct in_pktinfo *)(CMSG_DATA(cm));
			inet_ntop(AF_INET, &pi->ipi_addr, buf, sizeof(buf));
			ifindex = pi->ipi_ifindex;
		} else if (cm->cmsg_level == SOL_IPV6 &&
			   cm->cmsg_type == IPV6_PKTINFO) {
			struct in6_pktinfo *pi6;

			pi6 = (struct in6_pktinfo *)(CMSG_DATA(cm));
			inet_ntop(AF_INET6, &pi6->ipi6_addr, buf, sizeof(buf));
			ifindex = pi6->ipi6_ifindex;
		}
	}

	if (ifindex) {
		log_msg("    pktinfo: ifindex %d dest addr %s\n",
			ifindex, buf);
	}
	return ifindex;
}

static int send_msg_no_cmsg(int sd, void *addr, socklen_t alen)
{
	int err;

again:
	err = sendto(sd, msg, msglen, 0, addr, alen);
	if (err < 0) {
		if (errno == EACCES && try_broadcast) {
			try_broadcast = 0;
			if (!set_broadcast(sd))
				goto again;
			errno = EACCES;
		}

		log_err_errno("sendto failed");
		return 1;
	}

	return 0;
}

static int send_msg_cmsg(int sd, void *addr, socklen_t alen,
			 int ifindex, int version)
{
	unsigned char cmsgbuf[64];
	struct iovec iov[2];
	struct cmsghdr *cm;
	struct msghdr m;
	int err;

	iov[0].iov_base = msg;
	iov[0].iov_len = msglen;
	m.msg_iov = iov;
	m.msg_iovlen = 1;
	m.msg_name = (caddr_t)addr;
	m.msg_namelen = alen;

	memset(cmsgbuf, 0, sizeof(cmsgbuf));
	cm = (struct cmsghdr *)cmsgbuf;
	m.msg_control = (caddr_t)cm;

	if (version == AF_INET) {
		struct in_pktinfo *pi;

		cm->cmsg_level = SOL_IP;
		cm->cmsg_type = IP_PKTINFO;
		cm->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
		pi = (struct in_pktinfo *)(CMSG_DATA(cm));
		pi->ipi_ifindex = ifindex;

		m.msg_controllen = cm->cmsg_len;

	} else if (version == AF_INET6) {
		struct in6_pktinfo *pi6;

		cm->cmsg_level = SOL_IPV6;
		cm->cmsg_type = IPV6_PKTINFO;
		cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));

		pi6 = (struct in6_pktinfo *)(CMSG_DATA(cm));
		pi6->ipi6_ifindex = ifindex;

		m.msg_controllen = cm->cmsg_len;
	}

again:
	err = sendmsg(sd, &m, 0);
	if (err < 0) {
		if (errno == EACCES && try_broadcast) {
			try_broadcast = 0;
			if (!set_broadcast(sd))
				goto again;
			errno = EACCES;
		}

		log_err_errno("sendmsg failed");
		return 1;
	}

	return 0;
}


static int send_msg(int sd, void *addr, socklen_t alen, struct sock_args *args)
{
	if (args->type == SOCK_STREAM) {
		if (write(sd, msg, msglen) < 0) {
			log_err_errno("write failed sending msg to peer");
			return 1;
		}
	} else if (args->ifindex && args->use_cmsg) {
		if (send_msg_cmsg(sd, addr, alen, args->ifindex, args->version))
			return 1;
	} else {
		if (send_msg_no_cmsg(sd, addr, alen))
			return 1;
	}

	log_msg("Sent message:\n");
	log_msg("    %.24s%s\n", msg, msglen > 24 ? " ..." : "");

	return 0;
}

static int socket_read_dgram(int sd, struct sock_args *args)
{
	unsigned char addr[sizeof(struct sockaddr_in6)];
	struct sockaddr *sa = (struct sockaddr *) addr;
	socklen_t alen = sizeof(addr);
	struct iovec iov[2];
	struct msghdr m = {
		.msg_name = (caddr_t)addr,
		.msg_namelen = alen,
		.msg_iov = iov,
		.msg_iovlen = 1,
	};
	unsigned char cmsgbuf[256];
	struct cmsghdr *cm = (struct cmsghdr *)cmsgbuf;
	char buf[16*1024];
	int ifindex;
	int len;

	iov[0].iov_base = (caddr_t)buf;
	iov[0].iov_len = sizeof(buf);

	memset(cmsgbuf, 0, sizeof(cmsgbuf));
	m.msg_control = (caddr_t)cm;
	m.msg_controllen = sizeof(cmsgbuf);

	len = recvmsg(sd, &m, 0);
	if (len == 0) {
		log_msg("peer closed connection.\n");
		return 0;
	} else if (len < 0) {
		log_msg("failed to read message: %d: %s\n",
			errno, strerror(errno));
		return -1;
	}

	buf[len] = '\0';

	log_address("Message from:", sa);
	log_msg("    %.24s%s\n", buf, len > 24 ? " ..." : "");

	ifindex = get_index_from_cmsg(&m);
	if (args->expected_ifindex) {
		if (args->expected_ifindex != ifindex) {
			log_error("Device index mismatch: expected %d have %d\n",
				  args->expected_ifindex, ifindex);
			return -1;
		}
		log_msg("Device index matches: expected %d have %d\n",
			args->expected_ifindex, ifindex);
	}

	if (!interactive && server_mode) {
		if (sa->sa_family == AF_INET6) {
			struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) sa;
			struct in6_addr *in6 = &s6->sin6_addr;

			if (IN6_IS_ADDR_V4MAPPED(in6)) {
				const uint32_t *pa = (uint32_t *) &in6->s6_addr;
				struct in_addr in4;
				struct sockaddr_in *sin;

				sin = (struct sockaddr_in *) addr;
				pa += 3;
				in4.s_addr = *pa;
				sin->sin_addr = in4;
				sin->sin_family = AF_INET;
				if (send_msg_cmsg(sd, addr, alen,
						  ifindex, AF_INET) < 0)
					goto out_err;
			}
		}
again:
		iov[0].iov_len = len;

		if (args->version == AF_INET6) {
			struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) sa;

			if (args->dev) {
				/* avoid PKTINFO conflicts with bindtodev */
				if (sendto(sd, buf, len, 0,
					   (void *) addr, alen) < 0)
					goto out_err;
			} else {
				/* kernel is allowing scope_id to be set to VRF
				 * index for LLA. for sends to global address
				 * reset scope id
				 */
				s6->sin6_scope_id = ifindex;
				if (sendmsg(sd, &m, 0) < 0)
					goto out_err;
			}
		} else {
			int err;

			err = sendmsg(sd, &m, 0);
			if (err < 0) {
				if (errno == EACCES && try_broadcast) {
					try_broadcast = 0;
					if (!set_broadcast(sd))
						goto again;
					errno = EACCES;
				}
				goto out_err;
			}
		}
		log_msg("Sent message:\n");
		log_msg("    %.24s%s\n", buf, len > 24 ? " ..." : "");
	}

	return 1;
out_err:
	log_err_errno("failed to send msg to peer");
	return -1;
}

static int socket_read_stream(int sd)
{
	char buf[1024];
	int len;

	len = read(sd, buf, sizeof(buf)-1);
	if (len == 0) {
		log_msg("client closed connection.\n");
		return 0;
	} else if (len < 0) {
		log_msg("failed to read message\n");
		return -1;
	}

	buf[len] = '\0';
	log_msg("Incoming message:\n");
	log_msg("    %.24s%s\n", buf, len > 24 ? " ..." : "");

	if (!interactive && server_mode) {
		if (write(sd, buf, len) < 0) {
			log_err_errno("failed to send buf");
			return -1;
		}
		log_msg("Sent message:\n");
		log_msg("     %.24s%s\n", buf, len > 24 ? " ..." : "");
	}

	return 1;
}

static int socket_read(int sd, struct sock_args *args)
{
	if (args->type == SOCK_STREAM)
		return socket_read_stream(sd);

	return socket_read_dgram(sd, args);
}

static int stdin_to_socket(int sd, int type, void *addr, socklen_t alen)
{
	char buf[1024];
	int len;

	if (fgets(buf, sizeof(buf), stdin) == NULL)
		return 0;

	len = strlen(buf);
	if (type == SOCK_STREAM) {
		if (write(sd, buf, len) < 0) {
			log_err_errno("failed to send buf");
			return -1;
		}
	} else {
		int err;

again:
		err = sendto(sd, buf, len, 0, addr, alen);
		if (err < 0) {
			if (errno == EACCES && try_broadcast) {
				try_broadcast = 0;
				if (!set_broadcast(sd))
					goto again;
				errno = EACCES;
			}
			log_err_errno("failed to send msg to peer");
			return -1;
		}
	}
	log_msg("Sent message:\n");
	log_msg("    %.24s%s\n", buf, len > 24 ? " ..." : "");

	return 1;
}

static void set_recv_attr(int sd, int version)
{
	if (version == AF_INET6) {
		set_recvpktinfo_v6(sd);
		set_recverr_v6(sd);
	} else {
		set_pktinfo_v4(sd);
		set_recverr_v4(sd);
	}
}

static int msg_loop(int client, int sd, void *addr, socklen_t alen,
		    struct sock_args *args)
{
	struct timeval timeout = { .tv_sec = prog_timeout }, *ptval = NULL;
	fd_set rfds;
	int nfds;
	int rc;

	if (args->type != SOCK_STREAM)
		set_recv_attr(sd, args->version);

	if (msg) {
		msglen = strlen(msg);

		/* client sends first message */
		if (client) {
			if (send_msg(sd, addr, alen, args))
				return 1;
		}
		if (!interactive) {
			ptval = &timeout;
			if (!prog_timeout)
				timeout.tv_sec = 5;
		}
	}

	nfds = interactive ? MAX(fileno(stdin), sd)  + 1 : sd + 1;
	while (1) {
		FD_ZERO(&rfds);
		FD_SET(sd, &rfds);
		if (interactive)
			FD_SET(fileno(stdin), &rfds);

		rc = select(nfds, &rfds, NULL, NULL, ptval);
		if (rc < 0) {
			if (errno == EINTR)
				continue;

			rc = 1;
			log_err_errno("select failed");
			break;
		} else if (rc == 0) {
			log_error("Timed out waiting for response\n");
			rc = 2;
			break;
		}

		if (FD_ISSET(sd, &rfds)) {
			rc = socket_read(sd, args);
			if (rc < 0) {
				rc = 1;
				break;
			}
			if (rc == 0)
				break;
		}

		rc = 0;

		if (FD_ISSET(fileno(stdin), &rfds)) {
			if (stdin_to_socket(sd, args->type, addr, alen) <= 0)
				break;
		}

		if (interactive)
			continue;

		if (iter != -1) {
			--iter;
			if (iter == 0)
				break;
		}

		log_msg("Going into quiet mode\n");
		quiet = 1;

		if (client) {
			if (send_msg(sd, addr, alen, args)) {
				rc = 1;
				break;
			}
		}
	}

	return rc;
}

static int msock_init(struct sock_args *args, int server)
{
	uint32_t if_addr = htonl(INADDR_ANY);
	struct sockaddr_in laddr = {
		.sin_family = AF_INET,
		.sin_port = htons(args->port),
	};
	int one = 1;
	int sd;

	if (!server && args->has_local_ip)
		if_addr = args->local_addr.in.s_addr;

	sd = socket(PF_INET, SOCK_DGRAM, 0);
	if (sd < 0) {
		log_err_errno("socket");
		return -1;
	}

	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR,
		       (char *)&one, sizeof(one)) < 0) {
		log_err_errno("Setting SO_REUSEADDR error");
		goto out_err;
	}

	if (setsockopt(sd, SOL_SOCKET, SO_BROADCAST,
		       (char *)&one, sizeof(one)) < 0)
		log_err_errno("Setting SO_BROADCAST error");

	if (args->dev && bind_to_device(sd, args->dev) != 0)
		goto out_err;
	else if (args->use_setsockopt &&
		 set_multicast_if(sd, args->ifindex))
		goto out_err;

	laddr.sin_addr.s_addr = if_addr;

	if (bind(sd, (struct sockaddr *) &laddr, sizeof(laddr)) < 0) {
		log_err_errno("bind failed");
		goto out_err;
	}

	if (server &&
	    set_membership(sd, args->grp.s_addr,
			   args->local_addr.in.s_addr, args->ifindex))
		goto out_err;

	return sd;
out_err:
	close(sd);
	return -1;
}

static int msock_server(struct sock_args *args)
{
	return msock_init(args, 1);
}

static int msock_client(struct sock_args *args)
{
	return msock_init(args, 0);
}

static int bind_socket(int sd, struct sock_args *args)
{
	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
	};
	struct sockaddr_in6 serv6_addr = {
		.sin6_family = AF_INET6,
	};
	void *addr;
	socklen_t alen;

	if (!args->has_local_ip && args->type == SOCK_RAW)
		return 0;

	switch (args->version) {
	case AF_INET:
		serv_addr.sin_port = htons(args->port);
		serv_addr.sin_addr = args->local_addr.in;
		addr = &serv_addr;
		alen = sizeof(serv_addr);
		break;

	case AF_INET6:
		serv6_addr.sin6_port = htons(args->port);
		serv6_addr.sin6_addr = args->local_addr.in6;
		addr = &serv6_addr;
		alen = sizeof(serv6_addr);
		break;

	default:
		log_error("Invalid address family\n");
		return -1;
	}

	if (bind(sd, addr, alen) < 0) {
		log_err_errno("error binding socket");
		return -1;
	}

	return 0;
}

static int lsock_init(struct sock_args *args)
{
	long flags;
	int sd;

	sd = socket(args->version, args->type, args->protocol);
	if (sd < 0) {
		log_err_errno("Error opening socket");
		return  -1;
	}

	if (set_reuseaddr(sd) != 0)
		goto err;

	if (set_reuseport(sd) != 0)
		goto err;

	if (args->dev && bind_to_device(sd, args->dev) != 0)
		goto err;
	else if (args->use_setsockopt &&
		 set_unicast_if(sd, args->ifindex, args->version))
		goto err;

	if (bind_socket(sd, args))
		goto err;

	if (args->bind_test_only)
		goto out;

	if (args->type == SOCK_STREAM && listen(sd, 1) < 0) {
		log_err_errno("listen failed");
		goto err;
	}

	flags = fcntl(sd, F_GETFL);
	if ((flags < 0) || (fcntl(sd, F_SETFL, flags|O_NONBLOCK) < 0)) {
		log_err_errno("Failed to set non-blocking option");
		goto err;
	}

	if (fcntl(sd, F_SETFD, FD_CLOEXEC) < 0)
		log_err_errno("Failed to set close-on-exec flag");

out:
	return sd;

err:
	close(sd);
	return -1;
}

static int do_server(struct sock_args *args)
{
	struct timeval timeout = { .tv_sec = prog_timeout }, *ptval = NULL;
	unsigned char addr[sizeof(struct sockaddr_in6)] = {};
	socklen_t alen = sizeof(addr);
	int lsd, csd = -1;

	fd_set rfds;
	int rc;

	if (prog_timeout)
		ptval = &timeout;

	if (args->has_grp)
		lsd = msock_server(args);
	else
		lsd = lsock_init(args);

	if (lsd < 0)
		return 1;

	if (args->bind_test_only) {
		close(lsd);
		return 0;
	}

	if (args->type != SOCK_STREAM) {
		rc = msg_loop(0, lsd, (void *) addr, alen, args);
		close(lsd);
		return rc;
	}

	if (args->password && tcp_md5_remote(lsd, args)) {
		close(lsd);
		return -1;
	}

	while (1) {
		log_msg("\n");
		log_msg("waiting for client connection.\n");
		FD_ZERO(&rfds);
		FD_SET(lsd, &rfds);

		rc = select(lsd+1, &rfds, NULL, NULL, ptval);
		if (rc == 0) {
			rc = 2;
			break;
		}

		if (rc < 0) {
			if (errno == EINTR)
				continue;

			log_err_errno("select failed");
			break;
		}

		if (FD_ISSET(lsd, &rfds)) {

			csd = accept(lsd, (void *) addr, &alen);
			if (csd < 0) {
				log_err_errno("accept failed");
				break;
			}

			rc = show_sockstat(csd, args);
			if (rc)
				break;

			rc = check_device(csd, args);
			if (rc)
				break;
		}

		rc = msg_loop(0, csd, (void *) addr, alen, args);
		close(csd);

		if (!interactive)
			break;
	}

	close(lsd);

	return rc;
}

static int wait_for_connect(int sd)
{
	struct timeval _tv = { .tv_sec = prog_timeout }, *tv = NULL;
	fd_set wfd;
	int val = 0, sz = sizeof(val);
	int rc;

	FD_ZERO(&wfd);
	FD_SET(sd, &wfd);

	if (prog_timeout)
		tv = &_tv;

	rc = select(FD_SETSIZE, NULL, &wfd, NULL, tv);
	if (rc == 0) {
		log_error("connect timed out\n");
		return -2;
	} else if (rc < 0) {
		log_err_errno("select failed");
		return -3;
	}

	if (getsockopt(sd, SOL_SOCKET, SO_ERROR, &val, (socklen_t *)&sz) < 0) {
		log_err_errno("getsockopt(SO_ERROR) failed");
		return -4;
	}

	if (val != 0) {
		log_error("connect failed: %d: %s\n", val, strerror(val));
		return -1;
	}

	return 0;
}

static int connectsock(void *addr, socklen_t alen, struct sock_args *args)
{
	int sd, rc = -1;
	long flags;

	sd = socket(args->version, args->type, args->protocol);
	if (sd < 0) {
		log_err_errno("Failed to create socket");
		return -1;
	}

	flags = fcntl(sd, F_GETFL);
	if ((flags < 0) || (fcntl(sd, F_SETFL, flags|O_NONBLOCK) < 0)) {
		log_err_errno("Failed to set non-blocking option");
		goto err;
	}

	if (set_reuseport(sd) != 0)
		goto err;

	if (args->dev && bind_to_device(sd, args->dev) != 0)
		goto err;
	else if (args->use_setsockopt &&
		 set_unicast_if(sd, args->ifindex, args->version))
		goto err;

	if (args->has_local_ip && bind_socket(sd, args))
		goto err;

	if (args->type != SOCK_STREAM)
		goto out;

	if (args->password && tcp_md5sig(sd, addr, alen, args->password))
		goto err;

	if (args->bind_test_only)
		goto out;

	if (connect(sd, addr, alen) < 0) {
		if (errno != EINPROGRESS) {
			log_err_errno("Failed to connect to remote host");
			rc = -1;
			goto err;
		}
		rc = wait_for_connect(sd);
		if (rc < 0)
			goto err;
	}
out:
	return sd;

err:
	close(sd);
	return rc;
}

static int do_client(struct sock_args *args)
{
	struct sockaddr_in sin = {
		.sin_family = AF_INET,
	};
	struct sockaddr_in6 sin6 = {
		.sin6_family = AF_INET6,
	};
	void *addr;
	int alen;
	int rc = 0;
	int sd;

	if (!args->has_remote_ip && !args->has_grp) {
		fprintf(stderr, "remote IP or multicast group not given\n");
		return 1;
	}

	switch (args->version) {
	case AF_INET:
		sin.sin_port = htons(args->port);
		if (args->has_grp)
			sin.sin_addr = args->grp;
		else
			sin.sin_addr = args->remote_addr.in;
		addr = &sin;
		alen = sizeof(sin);
		break;
	case AF_INET6:
		sin6.sin6_port = htons(args->port);
		sin6.sin6_addr = args->remote_addr.in6;
		sin6.sin6_scope_id = args->scope_id;
		addr = &sin6;
		alen = sizeof(sin6);
		break;
	}

	if (args->has_grp)
		sd = msock_client(args);
	else
		sd = connectsock(addr, alen, args);

	if (sd < 0)
		return -sd;

	if (args->bind_test_only)
		goto out;

	if (args->type == SOCK_STREAM) {
		rc = show_sockstat(sd, args);
		if (rc != 0)
			goto out;
	}

	rc = msg_loop(1, sd, addr, alen, args);

out:
	close(sd);

	return rc;
}

enum addr_type {
	ADDR_TYPE_LOCAL,
	ADDR_TYPE_REMOTE,
	ADDR_TYPE_MCAST,
	ADDR_TYPE_EXPECTED_LOCAL,
	ADDR_TYPE_EXPECTED_REMOTE,
};

static int convert_addr(struct sock_args *args, const char *_str,
			enum addr_type atype)
{
	int family = args->version;
	struct in6_addr *in6;
	struct in_addr  *in;
	const char *desc;
	char *str, *dev;
	void *addr;
	int rc = 0;

	str = strdup(_str);
	if (!str)
		return -ENOMEM;

	switch (atype) {
	case ADDR_TYPE_LOCAL:
		desc = "local";
		addr = &args->local_addr;
		break;
	case ADDR_TYPE_REMOTE:
		desc = "remote";
		addr = &args->remote_addr;
		break;
	case ADDR_TYPE_MCAST:
		desc = "mcast grp";
		addr = &args->grp;
		break;
	case ADDR_TYPE_EXPECTED_LOCAL:
		desc = "expected local";
		addr = &args->expected_laddr;
		break;
	case ADDR_TYPE_EXPECTED_REMOTE:
		desc = "expected remote";
		addr = &args->expected_raddr;
		break;
	default:
		log_error("unknown address type");
		exit(1);
	}

	switch (family) {
	case AF_INET:
		in  = (struct in_addr *) addr;
		if (str) {
			if (inet_pton(AF_INET, str, in) == 0) {
				log_error("Invalid %s IP address\n", desc);
				rc = -1;
				goto out;
			}
		} else {
			in->s_addr = htonl(INADDR_ANY);
		}
		break;

	case AF_INET6:
		dev = strchr(str, '%');
		if (dev) {
			*dev = '\0';
			dev++;
		}

		in6 = (struct in6_addr *) addr;
		if (str) {
			if (inet_pton(AF_INET6, str, in6) == 0) {
				log_error("Invalid %s IPv6 address\n", desc);
				rc = -1;
				goto out;
			}
		} else {
			*in6 = in6addr_any;
		}
		if (dev) {
			args->scope_id = get_ifidx(dev);
			if (args->scope_id < 0) {
				log_error("Invalid scope on %s IPv6 address\n",
					  desc);
				rc = -1;
				goto out;
			}
		}
		break;

	default:
		log_error("Invalid address family\n");
	}

out:
	free(str);
	return rc;
}

static char *random_msg(int len)
{
	int i, n = 0, olen = len + 1;
	char *m;

	if (len <= 0)
		return NULL;

	m = malloc(olen);
	if (!m)
		return NULL;

	while (len > 26) {
		i = snprintf(m + n, olen - n, "%.26s",
			     "abcdefghijklmnopqrstuvwxyz");
		n += i;
		len -= i;
	}
	i = snprintf(m + n, olen - n, "%.*s", len,
		     "abcdefghijklmnopqrstuvwxyz");
	return m;
}

#define GETOPT_STR  "sr:l:p:t:g:P:DRn:M:d:SCi6L:0:1:2:Fbq"

static void print_usage(char *prog)
{
	printf(
	"usage: %s OPTS\n"
	"Required:\n"
	"    -r addr       remote address to connect to (client mode only)\n"
	"    -p port       port to connect to (client mode)/listen on (server mode)\n"
	"                  (default: %d)\n"
	"    -s            server mode (default: client mode)\n"
	"    -t            timeout seconds (default: none)\n"
	"\n"
	"Optional:\n"
	"    -F            Restart server loop\n"
	"    -6            IPv6 (default is IPv4)\n"
	"    -P proto      protocol for socket: icmp, ospf (default: none)\n"
	"    -D|R          datagram (D) / raw (R) socket (default stream)\n"
	"    -l addr       local address to bind to\n"
	"\n"
	"    -d dev        bind socket to given device name\n"
	"    -S            use setsockopt (IP_UNICAST_IF or IP_MULTICAST_IF)\n"
	"                  to set device binding\n"
	"    -C            use cmsg and IP_PKTINFO to specify device binding\n"
	"\n"
	"    -L len        send random message of given length\n"
	"    -n num        number of times to send message\n"
	"\n"
	"    -M password   use MD5 sum protection\n"
	"    -g grp        multicast group (e.g., 239.1.1.1)\n"
	"    -i            interactive mode (default is echo and terminate)\n"
	"\n"
	"    -0 addr       Expected local address\n"
	"    -1 addr       Expected remote address\n"
	"    -2 dev        Expected device name (or index) to receive packet\n"
	"\n"
	"    -b            Bind test only.\n"
	"    -q            Be quiet. Run test without printing anything.\n"
	, prog, DEFAULT_PORT);
}

int main(int argc, char *argv[])
{
	struct sock_args args = {
		.version = AF_INET,
		.type    = SOCK_STREAM,
		.port    = DEFAULT_PORT,
	};
	struct protoent *pe;
	unsigned int tmp;
	int forever = 0;

	/* process inputs */
	extern char *optarg;
	int rc = 0;

	/*
	 * process input args
	 */

	while ((rc = getopt(argc, argv, GETOPT_STR)) != -1) {
		switch (rc) {
		case 's':
			server_mode = 1;
			break;
		case 'F':
			forever = 1;
			break;
		case 'l':
			args.has_local_ip = 1;
			if (convert_addr(&args, optarg, ADDR_TYPE_LOCAL) < 0)
				return 1;
			break;
		case 'r':
			args.has_remote_ip = 1;
			if (convert_addr(&args, optarg, ADDR_TYPE_REMOTE) < 0)
				return 1;
			break;
		case 'p':
			if (str_to_uint(optarg, 1, 65535, &tmp) != 0) {
				fprintf(stderr, "Invalid port\n");
				return 1;
			}
			args.port = (unsigned short) tmp;
			break;
		case 't':
			if (str_to_uint(optarg, 0, INT_MAX,
					&prog_timeout) != 0) {
				fprintf(stderr, "Invalid timeout\n");
				return 1;
			}
			break;
		case 'D':
			args.type = SOCK_DGRAM;
			break;
		case 'R':
			args.type = SOCK_RAW;
			args.port = 0;
			break;
		case 'P':
			pe = getprotobyname(optarg);
			if (pe) {
				args.protocol = pe->p_proto;
			} else {
				if (str_to_uint(optarg, 0, 0xffff, &tmp) != 0) {
					fprintf(stderr, "Invalid protocol\n");
					return 1;
				}
				args.protocol = tmp;
			}
			break;
		case 'n':
			iter = atoi(optarg);
			break;
		case 'L':
			msg = random_msg(atoi(optarg));
			break;
		case 'M':
			args.password = optarg;
			break;
		case 'S':
			args.use_setsockopt = 1;
			break;
		case 'C':
			args.use_cmsg = 1;
			break;
		case 'd':
			args.dev = optarg;
			args.ifindex = get_ifidx(optarg);
			if (args.ifindex < 0) {
				fprintf(stderr, "Invalid device name\n");
				return 1;
			}
			break;
		case 'i':
			interactive = 1;
			break;
		case 'g':
			args.has_grp = 1;
			if (convert_addr(&args, optarg, ADDR_TYPE_MCAST) < 0)
				return 1;
			args.type = SOCK_DGRAM;
			break;
		case '6':
			args.version = AF_INET6;
			break;
		case 'b':
			args.bind_test_only = 1;
			break;
		case '0':
			args.has_expected_laddr = 1;
			if (convert_addr(&args, optarg,
					 ADDR_TYPE_EXPECTED_LOCAL))
				return 1;
			break;
		case '1':
			args.has_expected_raddr = 1;
			if (convert_addr(&args, optarg,
					 ADDR_TYPE_EXPECTED_REMOTE))
				return 1;

			break;
		case '2':
			if (str_to_uint(optarg, 0, INT_MAX, &tmp) == 0) {
				args.expected_ifindex = (int)tmp;
			} else {
				args.expected_ifindex = get_ifidx(optarg);
				if (args.expected_ifindex < 0) {
					fprintf(stderr,
						"Invalid expected device\n");
					return 1;
				}
			}
			break;
		case 'q':
			quiet = 1;
			break;
		default:
			print_usage(argv[0]);
			return 1;
		}
	}

	if (args.password &&
	    (!args.has_remote_ip || args.type != SOCK_STREAM)) {
		log_error("MD5 passwords apply to TCP only and require a remote ip for the password\n");
		return 1;
	}

	if ((args.use_setsockopt || args.use_cmsg) && !args.ifindex) {
		fprintf(stderr, "Device binding not specified\n");
		return 1;
	}
	if (args.use_setsockopt || args.use_cmsg)
		args.dev = NULL;

	if (iter == 0) {
		fprintf(stderr, "Invalid number of messages to send\n");
		return 1;
	}

	if (args.type == SOCK_STREAM && !args.protocol)
		args.protocol = IPPROTO_TCP;
	if (args.type == SOCK_DGRAM && !args.protocol)
		args.protocol = IPPROTO_UDP;

	if ((args.type == SOCK_STREAM || args.type == SOCK_DGRAM) &&
	     args.port == 0) {
		fprintf(stderr, "Invalid port number\n");
		return 1;
	}

	if (!server_mode && !args.has_grp &&
	    !args.has_remote_ip && !args.has_local_ip) {
		fprintf(stderr,
			"Local (server mode) or remote IP (client IP) required\n");
		return 1;
	}

	if (interactive) {
		prog_timeout = 0;
		msg = NULL;
	}

	if (server_mode) {
		do {
			rc = do_server(&args);
		} while (forever);

		return rc;
	}
	return do_client(&args);
}
