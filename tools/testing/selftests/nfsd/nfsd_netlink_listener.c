// SPDX-License-Identifier: GPL-2.0
/*
 * Regression tests for the NFSD generic-netlink listener interface
 * (NFSD_CMD_LISTENER_SET / NFSD_CMD_LISTENER_GET).
 *
 * Three groups:
 *   validation  - malformed/abusive LISTENER_SET requests are rejected by
 *                 nfsd_nl_validate_listeners(), before nfsd_mutex is taken.
 *   functional  - create/add/remove listeners and verify LISTENER_GET
 *                 reflects the set (round-trip of transport + addr:port).
 *   semantics   - once threads are running (THREADS_SET) a listener change
 *                 is refused with -EBUSY.
 *
 * Each test runs in its own private net + mount namespace (unshare in
 * FIXTURE_SETUP). /run is masked there: a pathname AF_LOCAL connect is not
 * scoped by the network namespace, since unix_find_bsd() resolves by inode
 * and takes no struct net, so the kernel's rpcbind client would otherwise be
 * able to reach the rpcbind running on the host. Anything that creates a
 * serv is served by the per-netns rpcbind stub below instead.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/nfsd_netlink.h>

#include "../kselftest_harness.h"

#define NFS_PROGRAM			100003
#define NFS_ACL_PROGRAM			100227

#define NLA_ALIGN4(len)			(((len) + 3) & ~3)
#define TEST_PORT			20049
#define MAX_LISTENERS			8
#define RECV_TIMEO_SEC			30

static int nfsd_family = -1;		/* set per-test in FIXTURE_SETUP */

/* Extack message from the last genl_request(); empty if there was none. */
static char last_extack[128];

static void die(const char *msg)
{
	perror(msg);
	exit(1);
}

/* ------------------- minimal generic-netlink plumbing ------------------- */

static int genl_open(void)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	struct timeval tv = { .tv_sec = RECV_TIMEO_SEC };
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	int on = 1;

	if (fd < 0)
		die("socket(NETLINK_GENERIC)");
	if (bind(fd, (void *)&sa, sizeof(sa)) < 0)
		die("bind(netlink)");
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	/*
	 * Ask for extack, and cap the ack so the request is not echoed back:
	 * the TLVs then always follow the fixed part of the error message.
	 */
	setsockopt(fd, SOL_NETLINK, NETLINK_EXT_ACK, &on, sizeof(on));
	setsockopt(fd, SOL_NETLINK, NETLINK_CAP_ACK, &on, sizeof(on));
	return fd;
}

/* Stash the extack message of an ack, if it carries one. */
static void parse_extack(const char *rbuf)
{
	const struct nlmsghdr *nlh = (const void *)rbuf;
	const struct nlattr *na;
	int off, left;

	last_extack[0] = '\0';
	if (nlh->nlmsg_type != NLMSG_ERROR ||
	    !(nlh->nlmsg_flags & NLM_F_ACK_TLVS))
		return;

	off = NLMSG_HDRLEN + NLMSG_ALIGN(sizeof(struct nlmsgerr));
	left = nlh->nlmsg_len - off;
	na = (const void *)(rbuf + off);

	while (left >= (int)NLA_HDRLEN) {
		if ((na->nla_type & NLA_TYPE_MASK) == NLMSGERR_ATTR_MSG) {
			strncpy(last_extack, (const char *)na + NLA_HDRLEN,
				sizeof(last_extack) - 1);
			last_extack[sizeof(last_extack) - 1] = '\0';
			return;
		}
		left -= NLA_ALIGN4(na->nla_len);
		na = (const void *)((const char *)na + NLA_ALIGN4(na->nla_len));
	}
}

/* Append an attribute at @off; return the new (aligned) offset. */
static int put_attr(char *buf, int off, uint16_t type,
		    const void *data, int len)
{
	struct nlattr *na = (void *)(buf + off);

	na->nla_type = type;
	na->nla_len = NLA_HDRLEN + len;
	if (len)
		memcpy(buf + off + NLA_HDRLEN, data, len);
	return off + NLA_ALIGN4(NLA_HDRLEN + len);
}

/* Build a genl message header into @buf; return the offset past it. */
static int genl_hdr(char *buf, uint16_t type, uint16_t flags, uint8_t cmd)
{
	struct nlmsghdr *nlh = (void *)buf;
	struct genlmsghdr *gnl = (void *)(buf + NLMSG_HDRLEN);

	memset(buf, 0, NLMSG_HDRLEN + GENL_HDRLEN);
	nlh->nlmsg_type = type;
	nlh->nlmsg_flags = flags;
	nlh->nlmsg_seq = 1;
	gnl->cmd = cmd;
	gnl->version = 1;
	return NLMSG_HDRLEN + GENL_HDRLEN;
}

/* Send an nfsd command with an ACK; return the ACK errno (<= 0). */
static int genl_request(uint8_t cmd, const char *attrs, int attrs_len)
{
	char buf[1 << 20], rbuf[4096];
	struct nlmsghdr *nlh = (void *)buf;
	int fd = genl_open();
	int off, n, ret;

	off = genl_hdr(buf, nfsd_family, NLM_F_REQUEST | NLM_F_ACK, cmd);
	if (attrs_len) {
		memcpy(buf + off, attrs, attrs_len);
		off += attrs_len;
	}
	nlh->nlmsg_len = off;

	if (send(fd, buf, off, 0) < 0)
		die("send(genl)");

	last_extack[0] = '\0';
	n = recv(fd, rbuf, sizeof(rbuf), 0);
	if (n < 0) {
		ret = (errno == EAGAIN || errno == EWOULDBLOCK) ? -ETIMEDOUT : -errno;
	} else if (((struct nlmsghdr *)rbuf)->nlmsg_type == NLMSG_ERROR) {
		ret = ((struct nlmsgerr *)NLMSG_DATA(rbuf))->error;
		parse_extack(rbuf);
	} else {
		ret = 0;
	}
	close(fd);
	return ret;
}

/*
 * Send a command with attributes and return the full reply message; -errno
 * on failure. NLM_F_ACK is left off: the kernel reports an error either way,
 * so the first message back is the reply whenever there is one.
 */
static int genl_request_reply_attrs(uint8_t cmd, const char *attrs,
				    int attrs_len, char *rbuf, size_t rlen)
{
	char buf[1 << 20];
	struct nlmsghdr *nlh = (void *)buf;
	int fd = genl_open();
	int off, n, ret;

	off = genl_hdr(buf, nfsd_family, NLM_F_REQUEST, cmd);
	if (attrs_len) {
		memcpy(buf + off, attrs, attrs_len);
		off += attrs_len;
	}
	nlh->nlmsg_len = off;

	if (send(fd, buf, off, 0) < 0)
		die("send(genl reply)");

	n = recv(fd, rbuf, rlen, 0);
	if (n < 0)
		ret = (errno == EAGAIN || errno == EWOULDBLOCK) ? -ETIMEDOUT : -errno;
	else if (((struct nlmsghdr *)rbuf)->nlmsg_type == NLMSG_ERROR)
		ret = ((struct nlmsgerr *)NLMSG_DATA(rbuf))->error;
	else
		ret = n;
	close(fd);
	return ret;
}

static int genl_request_reply(uint8_t cmd, char *rbuf, size_t rlen)
{
	return genl_request_reply_attrs(cmd, NULL, 0, rbuf, rlen);
}

/* Resolve the "nfsd" genl family id; -1 if not registered. */
static int genl_resolve_nfsd(void)
{
	char buf[1024], rbuf[4096];
	struct nlmsghdr *nlh = (void *)buf;
	struct nlmsghdr *rh = (void *)rbuf;
	struct nlattr *na;
	int fd, off, left, id = -1;

	fd = genl_open();
	off = genl_hdr(buf, GENL_ID_CTRL, NLM_F_REQUEST, CTRL_CMD_GETFAMILY);
	off = put_attr(buf, off, CTRL_ATTR_FAMILY_NAME,
		       NFSD_FAMILY_NAME, sizeof(NFSD_FAMILY_NAME));
	nlh->nlmsg_len = off;

	if (send(fd, buf, off, 0) < 0)
		die("send(GETFAMILY)");
	if (recv(fd, rbuf, sizeof(rbuf), 0) < 0)
		die("recv(GETFAMILY)");
	close(fd);

	if (rh->nlmsg_type == NLMSG_ERROR)
		return -1;

	na = (void *)((char *)NLMSG_DATA(rh) + GENL_HDRLEN);
	left = rh->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;
	while (left >= (int)NLA_HDRLEN) {
		if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
			id = *(uint16_t *)((char *)na + NLA_HDRLEN);
			break;
		}
		left -= NLA_ALIGN4(na->nla_len);
		na = (void *)((char *)na + NLA_ALIGN4(na->nla_len));
	}
	return id;
}

/* ------------------- listener request builders ------------------- */

/* Fine-grained control for negative tests: any field can be omitted/malformed. */
struct raw_listener {
	const char *xprt;	/* NULL -> omit NFSD_A_SOCK_TRANSPORT_NAME */
	int emit_addr;		/* 0 -> omit NFSD_A_SOCK_ADDR */
	const void *addr;
	int addr_len;		/* bytes to emit for NFSD_A_SOCK_ADDR */
};

static int put_raw_listener(char *buf, int off, const struct raw_listener *r)
{
	struct nlattr *nest = (void *)(buf + off);
	int inner = off + NLA_HDRLEN;

	if (r->emit_addr)
		inner = put_attr(buf, inner, NFSD_A_SOCK_ADDR, r->addr, r->addr_len);
	if (r->xprt)
		inner = put_attr(buf, inner, NFSD_A_SOCK_TRANSPORT_NAME,
				 r->xprt, strlen(r->xprt) + 1);
	nest->nla_type = NFSD_A_SERVER_SOCK_ADDR | NLA_F_NESTED;
	nest->nla_len = inner - off;
	return off + NLA_ALIGN4(nest->nla_len);
}

/* Well-formed loopback listener for @family (AF_INET or AF_INET6). */
static int put_listener_af(char *buf, int off, const char *xprt, int family,
			   uint16_t port)
{
	struct sockaddr_storage ss = {0};
	struct raw_listener r = { .xprt = xprt, .emit_addr = 1, .addr = &ss };

	if (family == AF_INET6) {
		struct sockaddr_in6 *s6 = (void *)&ss;

		s6->sin6_family = AF_INET6;
		s6->sin6_port = htons(port);
		s6->sin6_addr = in6addr_loopback;
		r.addr_len = sizeof(*s6);
	} else {
		struct sockaddr_in *s4 = (void *)&ss;

		s4->sin_family = AF_INET;
		s4->sin_port = htons(port);
		s4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		r.addr_len = sizeof(*s4);
	}
	return put_raw_listener(buf, off, &r);
}

static int put_listener(char *buf, int off, const char *xprt, uint16_t port)
{
	return put_listener_af(buf, off, xprt, AF_INET, port);
}

/* ------------------- LISTENER_GET parsing ------------------- */

struct listener_ent {
	char xprt[16];
	int family;
	uint16_t port;
	struct in_addr a4;
	struct in6_addr a6;
};

static int parse_listener_get(const char *rbuf, int len,
			      struct listener_ent *out, int max)
{
	const struct nlmsghdr *nlh = (const void *)rbuf;
	const struct nlattr *na;
	int left, count = 0;

	(void)len;
	na = (const void *)(rbuf + NLMSG_HDRLEN + GENL_HDRLEN);
	left = nlh->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;

	while (left >= (int)NLA_HDRLEN) {
		int alen = na->nla_len;

		if ((na->nla_type & NLA_TYPE_MASK) == NFSD_A_SERVER_SOCK_ADDR &&
		    count < max) {
			const struct nlattr *in = (const void *)((char *)na + NLA_HDRLEN);
			int ileft = alen - NLA_HDRLEN;
			struct listener_ent *e = &out[count];

			memset(e, 0, sizeof(*e));
			while (ileft >= (int)NLA_HDRLEN) {
				const void *d = (const char *)in + NLA_HDRLEN;
				int t = in->nla_type & NLA_TYPE_MASK;

				if (t == NFSD_A_SOCK_TRANSPORT_NAME) {
					strncpy(e->xprt, d, sizeof(e->xprt) - 1);
				} else if (t == NFSD_A_SOCK_ADDR) {
					const struct sockaddr_storage *ss = d;

					e->family = ss->ss_family;
					if (ss->ss_family == AF_INET) {
						const struct sockaddr_in *s = d;

						e->a4 = s->sin_addr;
						e->port = ntohs(s->sin_port);
					} else if (ss->ss_family == AF_INET6) {
						const struct sockaddr_in6 *s = d;

						e->a6 = s->sin6_addr;
						e->port = ntohs(s->sin6_port);
					}
				}
				ileft -= NLA_ALIGN4(in->nla_len);
				in = (const void *)((char *)in + NLA_ALIGN4(in->nla_len));
			}
			count++;
		}
		left -= NLA_ALIGN4(alen);
		na = (const void *)((char *)na + NLA_ALIGN4(alen));
	}
	return count;
}

/* ------------------- convenience wrappers ------------------- */

static int listener_set(const char *attrs, int len)
{
	return genl_request(NFSD_CMD_LISTENER_SET, attrs, len);
}

/*
 * Enable exactly one NFS version in this netns. NFSD_CMD_VERSION_SET clears
 * every version first, so one nest is enough to leave the server v4-only.
 * It refuses once a serv exists, so call it before any listener.
 */
static int version_set_only(uint32_t major, uint32_t minor)
{
	char attrs[64];
	struct nlattr *nest = (void *)attrs;
	int inner = NLA_HDRLEN;

	inner = put_attr(attrs, inner, NFSD_A_VERSION_MAJOR,
			 &major, sizeof(major));
	inner = put_attr(attrs, inner, NFSD_A_VERSION_MINOR,
			 &minor, sizeof(minor));
	inner = put_attr(attrs, inner, NFSD_A_VERSION_ENABLED, NULL, 0);
	nest->nla_type = NFSD_A_SERVER_PROTO_VERSION | NLA_F_NESTED;
	nest->nla_len = inner;

	return genl_request(NFSD_CMD_VERSION_SET, attrs, NLA_ALIGN4(inner));
}

/* ------------------- userspace-rpcbind ------------------- */

struct rpcb_ent {
	uint32_t program;
	uint32_t version;
	uint32_t flags;
};

/* More listeners than GENLMSG_DEFAULT_SIZE would have held. */
#define RPCB_MANY_LISTENERS		40

struct rpcb_reply {
	int acked;			/* saw NFSD_A_SERVER_SOCK_USERSPACE_RPCBIND */
	int nprog;
	struct rpcb_ent prog[16];
	int naddr;			/* every addr nest, not just the stored ones */
	int nlistener;
	struct listener_ent listener[MAX_LISTENERS];
};

/* Append the userspace-rpcbind request flag. */
static int put_userspace_rpcbind(char *buf, int off)
{
	return put_attr(buf, off, NFSD_A_SERVER_SOCK_USERSPACE_RPCBIND, NULL, 0);
}

static void parse_rpcb_nest(const struct nlattr *na, struct rpcb_ent *e)
{
	const struct nlattr *in = (const void *)((const char *)na + NLA_HDRLEN);
	int ileft = na->nla_len - NLA_HDRLEN;

	memset(e, 0, sizeof(*e));
	while (ileft >= (int)NLA_HDRLEN) {
		const void *d = (const char *)in + NLA_HDRLEN;

		switch (in->nla_type & NLA_TYPE_MASK) {
		case NFSD_A_RPCBIND_PROGRAM:
			e->program = *(const uint32_t *)d;
			break;
		case NFSD_A_RPCBIND_VERSION:
			e->version = *(const uint32_t *)d;
			break;
		case NFSD_A_RPCBIND_FLAGS:
			e->flags = *(const uint32_t *)d;
			break;
		}
		ileft -= NLA_ALIGN4(in->nla_len);
		in = (const void *)((const char *)in + NLA_ALIGN4(in->nla_len));
	}
}

/*
 * Send a listener_set that asks to own rpcbind, and parse the reply.
 * Returns 0 on success or -errno.
 */
static int listener_set_rpcb(char *attrs, int off, struct rpcb_reply *out)
{
	char rbuf[64 * 1024];
	const struct nlmsghdr *nlh = (const void *)rbuf;
	const struct nlattr *na;
	int left, n;

	off = put_userspace_rpcbind(attrs, off);
	memset(out, 0, sizeof(*out));

	n = genl_request_reply_attrs(NFSD_CMD_LISTENER_SET, attrs, off,
				     rbuf, sizeof(rbuf));
	if (n < 0)
		return n;

	/*
	 * A reply at NFSD_NL_LISTENER_MAX outgrows rbuf, and the parse loops
	 * below trust nlmsg_len. Refuse a short read rather than walk off it.
	 */
	if (n < (int)(NLMSG_HDRLEN + GENL_HDRLEN) ||
	    nlh->nlmsg_len > (unsigned int)n)
		return -EMSGSIZE;

	out->nlistener = parse_listener_get(rbuf, n, out->listener,
					    MAX_LISTENERS);

	na = (const void *)(rbuf + NLMSG_HDRLEN + GENL_HDRLEN);
	left = nlh->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;
	while (left >= (int)NLA_HDRLEN) {
		switch (na->nla_type & NLA_TYPE_MASK) {
		case NFSD_A_SERVER_SOCK_USERSPACE_RPCBIND:
			out->acked = 1;
			break;
		case NFSD_A_SERVER_SOCK_ADDR:
			out->naddr++;
			break;
		case NFSD_A_SERVER_SOCK_RPCBIND:
			if (out->nprog < (int)ARRAY_SIZE(out->prog))
				parse_rpcb_nest(na, &out->prog[out->nprog++]);
			break;
		}
		left -= NLA_ALIGN4(na->nla_len);
		na = (const void *)((const char *)na + NLA_ALIGN4(na->nla_len));
	}
	return 0;
}

static struct rpcb_ent *find_rpcb(struct rpcb_reply *r, uint32_t prog,
				  uint32_t vers)
{
	int i;

	for (i = 0; i < r->nprog; i++)
		if (r->prog[i].program == prog && r->prog[i].version == vers)
			return &r->prog[i];
	return NULL;
}

/* Fetch the current listeners; returns count (>=0) or -errno. */
static int listener_get(struct listener_ent *out, int max)
{
	char rbuf[8192];
	int n = genl_request_reply(NFSD_CMD_LISTENER_GET, rbuf, sizeof(rbuf));

	if (n < 0)
		return n;
	return parse_listener_get(rbuf, n, out, max);
}

/*
 * Every listener these tests create comes from put_listener_af(), so the
 * address is always loopback. Match on it too: without that, a reply that
 * gave the right transport and port on the wrong address (0.0.0.0, say)
 * would pass.
 */
static struct listener_ent *find_listener(struct listener_ent *e, int n,
					  const char *xprt, int family,
					  uint16_t port)
{
	int i;

	for (i = 0; i < n; i++) {
		if (e[i].family != family || e[i].port != port ||
		    strcmp(e[i].xprt, xprt))
			continue;
		if (family == AF_INET6) {
			if (memcmp(&e[i].a6, &in6addr_loopback, sizeof(e[i].a6)))
				continue;
		} else if (e[i].a4.s_addr != htonl(INADDR_LOOPBACK)) {
			continue;
		}
		return &e[i];
	}
	return NULL;
}

/* Start (@n > 0) or stop (@n == 0) nfsd threads in this netns. */
static int threads_set(int n)
{
	char attrs[64];
	uint32_t v = n;
	int off = put_attr(attrs, 0, NFSD_A_SERVER_THREADS, &v, sizeof(v));

	return genl_request(NFSD_CMD_THREADS_SET, attrs, off);
}

/* ------------------- per-netns local rpcbind stub ------------------- */

/*
 * Creating a listener registers with rpcbind: nfsd_nl_listener_set_doit()
 * passes no SVC_SOCK_ANONYMOUS for the first entry of a request, so
 * pmap_register is true in svc_setup_socket(). The fixture's server has v3
 * enabled, and nfsd_version3 does not set vs_rpcb_optnl, so a failure there
 * comes back out of svc_register() and takes the listener down with it.
 * With nothing listening, every attempt first waits out the local rpcbind
 * timeout. The abstract AF_LOCAL name the kernel tries first is per-netns
 * (unix_find_abstract() takes a struct net), so answer it here and stay out
 * of the host's rpcbind.
 *
 * Arguments are never decoded. The NULL procedure gets an empty success and
 * SET/UNSET get TRUE, for both RPCBVERS_2 and RPCBVERS_4. v4 has to be
 * answered because __svc_rpcb_register6() turns a v4 refusal into
 * -EAFNOSUPPORT, which would leave every IPv6 listener unregistered.
 *
 * In RPCB_STUB_REFUSE mode SET is answered FALSE instead, which
 * rpcb_register_call() reports as -EACCES. UNSET is left alone: only
 * svc_unregister() issues it, and it discards the result.
 *
 * In RPCB_STUB_SILENT mode a SET or an UNSET is read and nothing is written
 * back, so the kernel waits out its own timeout. That is the only mode that
 * makes rpcb_register_call() report a call that got no answer, which is what
 * the per-net failure count records. The NULL procedure is still answered:
 * rpcb_create_af_local() builds its client without RPC_CLNT_CREATE_NOPING, so
 * rpc_create() pings, and a ping that goes unanswered drops the kernel onto
 * the loopback rpcb_create_local_net() client, which never reaches this stub.
 *
 * The stub also keeps counters and the mode in a page shared with the test, so
 * a test can assert that the kernel never talked to rpcbind at all, or that it
 * dropped the local rpcbind client and had to reconnect.
 *
 * The mode lives there rather than in the child so that a test can change it
 * with a serv already up. Killing and restarting the stub would close the
 * connection the kernel holds, and rpcb_register_call() issues UNSET over
 * AF_LOCAL with RPC_TASK_NOCONNECT, so the next call would fail at once with
 * -ENOTCONN instead of waiting out a timeout.
 */
#define RPCB_PROGRAM		100000
#define RPCB_PROC_NULL		0
#define RPCB_PROC_SET		1
#define RPCB_PROC_UNSET		2
#define RPCB_ABSTRACT_NAME	"/run/rpcbind.sock"
#define RPCB_STUB_MAXCONN	4

enum { RPCB_STUB_ACCEPT, RPCB_STUB_REFUSE, RPCB_STUB_SILENT };

struct rpcb_stub_stats {
	unsigned int conns;		/* connections accepted */
	unsigned int calls;		/* calls received */
	unsigned int mode;		/* RPCB_STUB_*, read on every call */
};

static volatile struct rpcb_stub_stats *rpcb_stats;	/* MAP_SHARED */

static int rpcb_stats_alloc(void)
{
	void *p = mmap(NULL, sizeof(*rpcb_stats), PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	if (p == MAP_FAILED)
		return -1;
	rpcb_stats = p;
	return 0;
}

/*
 * The stub bumps these before it replies and the kernel waits for that reply,
 * so whatever a netlink request provoked is visible once it returns.
 */
static int rpcb_calls(void)
{
	return rpcb_stats ? (int)rpcb_stats->calls : 0;
}

static int rpcb_conns(void)
{
	return rpcb_stats ? (int)rpcb_stats->conns : 0;
}

/* Takes effect on the stub's next call; the caller has not sent one yet. */
static void rpcb_stub_set_mode(int mode)
{
	rpcb_stats->mode = mode;
}

static int rpcb_stub_listen(void)
{
	struct sockaddr_un sun = { .sun_family = AF_UNIX };
	size_t nlen = strlen(RPCB_ABSTRACT_NAME);
	socklen_t alen;
	int fd;

	/* Abstract names are length-delimited, so the length must match. */
	memcpy(sun.sun_path + 1, RPCB_ABSTRACT_NAME, nlen);
	alen = offsetof(struct sockaddr_un, sun_path) + 1 + nlen;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (bind(fd, (struct sockaddr *)&sun, alen) < 0 ||
	    listen(fd, RPCB_STUB_MAXCONN) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int rpcb_stub_read(int fd, void *buf, size_t len)
{
	size_t done = 0;

	while (done < len) {
		ssize_t n = read(fd, (char *)buf + done, len - done);

		if (n <= 0)
			return -1;
		done += n;
	}
	return 0;
}

/* Handle one record-marked RPC call. Returns -1 when the peer is done. */
static int rpcb_stub_call(int fd)
{
	unsigned int len, nrep = 6, mode = rpcb_stats->mode;
	uint32_t mark, call[6], rep[7];
	size_t replen;

	if (rpcb_stub_read(fd, &mark, sizeof(mark)))
		return -1;
	len = ntohl(mark) & 0x7fffffff;
	if (len < sizeof(call) || len > 4096)
		return -1;
	if (rpcb_stub_read(fd, call, sizeof(call)))
		return -1;

	/* xid, msg_type, rpcvers, prog, vers, proc; the rest is discarded */
	for (len -= sizeof(call); len; ) {
		char sink[256];
		unsigned int n = len > sizeof(sink) ? sizeof(sink) : len;

		if (rpcb_stub_read(fd, sink, n))
			return -1;
		len -= n;
	}

	if (rpcb_stats)
		rpcb_stats->calls++;

	rep[0] = call[0];		/* xid */
	rep[1] = htonl(1);		/* REPLY */
	rep[2] = htonl(0);		/* MSG_ACCEPTED */
	rep[3] = htonl(0);		/* verifier flavor AUTH_NULL */
	rep[4] = htonl(0);		/* verifier length */
	rep[5] = htonl(0);		/* SUCCESS */

	if (ntohl(call[3]) != RPCB_PROGRAM) {
		rep[5] = htonl(1);	/* PROG_UNAVAIL */
	} else {
		unsigned int proc = ntohl(call[5]);

		switch (proc) {
		case RPCB_PROC_NULL:
			break;
		case RPCB_PROC_SET:
			rep[6] = htonl(mode == RPCB_STUB_REFUSE ? 0 : 1);
			nrep = 7;
			break;
		case RPCB_PROC_UNSET:
			rep[6] = htonl(1);	/* TRUE */
			nrep = 7;
			break;
		default:
			rep[5] = htonl(3);	/* PROC_UNAVAIL */
		}

		/*
		 * Answer nothing, so the caller waits out its timeout. The
		 * NULL procedure is answered even here: the kernel pings at
		 * client creation, and a ping with no answer takes it off
		 * this socket entirely.
		 */
		if (mode == RPCB_STUB_SILENT && proc != RPCB_PROC_NULL)
			return 0;
	}

	replen = nrep * sizeof(rep[0]);
	mark = htonl(0x80000000 | replen);
	if (write(fd, &mark, sizeof(mark)) != (ssize_t)sizeof(mark) ||
	    write(fd, rep, replen) != (ssize_t)replen)
		return -1;
	return 0;
}

static void rpcb_stub_serve(int lfd)
{
	struct pollfd pfd[1 + RPCB_STUB_MAXCONN];
	nfds_t n = 1, i;

	pfd[0].fd = lfd;

	for (;;) {
		/* stop polling the listener when full, or poll() spins */
		pfd[0].events = n < 1 + RPCB_STUB_MAXCONN ? POLLIN : 0;

		if (poll(pfd, n, -1) < 0)
			return;

		if (pfd[0].revents & POLLIN) {
			int c = accept(lfd, NULL, NULL);

			if (c >= 0) {
				pfd[n].fd = c;
				pfd[n].events = POLLIN;
				/*
				 * poll() ran with the old n, so it did not
				 * write this revents. The loop below reads it.
				 */
				pfd[n].revents = 0;
				n++;
				if (rpcb_stats)
					rpcb_stats->conns++;
			}
		}

		for (i = 1; i < n; i++) {
			if (!(pfd[i].revents & (POLLIN | POLLHUP | POLLERR)))
				continue;
			if (rpcb_stub_call(pfd[i].fd)) {
				close(pfd[i].fd);
				pfd[i] = pfd[--n];
			}
		}
	}
}

/* Returns the stub's pid, or -1. The socket is listening before we fork. */
static pid_t rpcb_stub_start(int mode)
{
	int lfd = rpcb_stub_listen();
	pid_t pid;

	if (lfd < 0)
		return -1;

	rpcb_stats->mode = mode;

	pid = fork();
	if (pid < 0) {
		close(lfd);
		return -1;
	}
	if (pid == 0) {
		signal(SIGPIPE, SIG_IGN);
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		if (getppid() == 1)		/* raced with parent exit */
			_exit(0);
		rpcb_stub_serve(lfd);
		_exit(0);
	}

	close(lfd);
	return pid;
}

/* --------------------------- fixture --------------------------- */

FIXTURE(nfsd_listener) {
	pid_t rpcbd;
};

FIXTURE_SETUP(nfsd_listener)
{
	struct ifreq ifr = {0};
	struct stat st;
	int s;

	if (geteuid() != 0)
		SKIP(return, "must be run as root");
	if (unshare(CLONE_NEWNET | CLONE_NEWNS) < 0)
		SKIP(return, "unshare(NEWNET|NEWNS): %s", strerror(errno));
	if (mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
		SKIP(return, "mount(/ private): %s", strerror(errno));

	/*
	 * Keep the kernel's rpcbind client inside this namespace. The
	 * abstract socket it tries first is per-netns, but the
	 * "/var/run/rpcbind.sock" fallback is not, so hide the path.
	 */
	if (mount("tmpfs", "/run", "tmpfs", 0, NULL) < 0)
		SKIP(return, "mount(tmpfs on /run): %s", strerror(errno));
	if (lstat("/var/run", &st) == 0 && S_ISDIR(st.st_mode) &&
	    mount("tmpfs", "/var/run", "tmpfs", 0, NULL) < 0)
		SKIP(return, "mount(tmpfs on /var/run): %s", strerror(errno));

	/*
	 * Bring loopback up so listener binds (127.0.0.1 / ::1) work. Root
	 * without CAP_NET_ADMIN in this netns gets -EPERM here, so skip.
	 */
	s = socket(AF_INET, SOCK_DGRAM, 0);
	ASSERT_GE(s, 0);
	strcpy(ifr.ifr_name, "lo");
	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
		close(s);
		SKIP(return, "SIOCGIFFLAGS(lo): %s", strerror(errno));
	}
	ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
	if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) {
		close(s);
		SKIP(return, "SIOCSIFFLAGS(lo): %s", strerror(errno));
	}
	close(s);

	nfsd_family = genl_resolve_nfsd();
	if (nfsd_family < 0)
		SKIP(return, "nfsd genl family not found (modprobe nfsd?)");

	if (rpcb_stats_alloc() < 0)
		SKIP(return, "mmap(rpcbind stub counters): %s", strerror(errno));

	self->rpcbd = rpcb_stub_start(RPCB_STUB_ACCEPT);
	if (self->rpcbd < 0)
		SKIP(return, "cannot start the rpcbind stub: %s",
		     strerror(errno));
}

FIXTURE_TEARDOWN(nfsd_listener)
{
	/*
	 * A listener holds a reference to this netns, which outlives the test
	 * process, so anything still up leaks it. Threads pin the listeners in
	 * turn; dropping them destroys the serv and everything under it.
	 */
	if (nfsd_family >= 0 && listener_set(NULL, 0) == -EBUSY)
		threads_set(0);

	if (self->rpcbd > 0) {
		kill(self->rpcbd, SIGKILL);
		waitpid(self->rpcbd, NULL, 0);
	}
	if (rpcb_stats) {
		munmap((void *)rpcb_stats, sizeof(*rpcb_stats));
		rpcb_stats = NULL;
	}
}

/* ===================== validation / negative ===================== */

TEST_F(nfsd_listener, val_empty_list_ok)
{
	EXPECT_EQ(0, listener_set(NULL, 0));
}

TEST_F(nfsd_listener, val_too_many)
{
	static char attrs[1 << 20];
	int i, off = 0;

	for (i = 0; i < 1025; i++)		/* > NFSD_NL_LISTENER_MAX (1024) */
		off = put_listener(attrs, off, "udp", TEST_PORT);
	EXPECT_EQ(-E2BIG, listener_set(attrs, off));
}

TEST_F(nfsd_listener, val_missing_addr)
{
	char attrs[64];
	struct raw_listener r = { .xprt = "tcp", .emit_addr = 0 };
	int off = put_raw_listener(attrs, 0, &r);

	EXPECT_EQ(-EINVAL, listener_set(attrs, off));
}

TEST_F(nfsd_listener, val_missing_transport)
{
	struct sockaddr_in s4 = { .sin_family = AF_INET, .sin_port = htons(TEST_PORT) };
	struct raw_listener r = { .xprt = NULL, .emit_addr = 1,
				  .addr = &s4, .addr_len = sizeof(s4) };
	char attrs[64];
	int off = put_raw_listener(attrs, 0, &r);

	EXPECT_EQ(-EINVAL, listener_set(attrs, off));
}

/*
 * A name matching no transport class must be refused before nfsd_mutex is
 * taken, so it never reaches svc_xprt_create_from_sa() and its
 * request_module("svc%s", name) upcall.
 *
 * The errno cannot show that -- svc_xprt_create_from_sa() returns
 * -EPROTONOSUPPORT for an unknown name too. The rpcbind traffic can:
 * getting that far means nfsd_create_serv() ran, and svc_bind() pings
 * rpcbind at client creation and then sweeps stale entries with
 * svc_unregister(). A silent stub is the proof nothing was created.
 */
TEST_F(nfsd_listener, val_bad_transport)
{
	char attrs[64];
	int off = put_listener(attrs, 0, "bogus_xprt", TEST_PORT);

	ASSERT_EQ(0, rpcb_calls());
	EXPECT_EQ(-EPROTONOSUPPORT, listener_set(attrs, off));
	EXPECT_EQ(0, rpcb_calls());
}

TEST_F(nfsd_listener, val_addr_too_short)
{
	unsigned char tiny = 0;
	struct raw_listener r = { .xprt = "tcp", .emit_addr = 1,
				  .addr = &tiny, .addr_len = 1 };
	char attrs[64];
	int off = put_raw_listener(attrs, 0, &r);

	EXPECT_EQ(-EINVAL, listener_set(attrs, off));
}

TEST_F(nfsd_listener, val_inet_short)
{
	struct sockaddr_in s4 = { .sin_family = AF_INET, .sin_port = htons(TEST_PORT) };
	struct raw_listener r = { .xprt = "tcp", .emit_addr = 1, .addr = &s4,
				  .addr_len = sizeof(sa_family_t) + 2 };
	char attrs[64];
	int off = put_raw_listener(attrs, 0, &r);

	EXPECT_EQ(-EINVAL, listener_set(attrs, off));
}

TEST_F(nfsd_listener, val_inet6_short)
{
	struct sockaddr_in6 s6 = { .sin6_family = AF_INET6, .sin6_port = htons(TEST_PORT) };
	struct raw_listener r = { .xprt = "tcp", .emit_addr = 1, .addr = &s6,
				  .addr_len = sizeof(struct sockaddr_in) };
	char attrs[64];
	int off = put_raw_listener(attrs, 0, &r);

	EXPECT_EQ(-EINVAL, listener_set(attrs, off));
}

TEST_F(nfsd_listener, val_bad_family)
{
	struct sockaddr_storage ss = { .ss_family = AF_UNIX };
	struct raw_listener r = { .xprt = "tcp", .emit_addr = 1, .addr = &ss,
				  .addr_len = sizeof(struct sockaddr_in) };
	char attrs[64];
	int off = put_raw_listener(attrs, 0, &r);

	EXPECT_EQ(-EAFNOSUPPORT, listener_set(attrs, off));
}

TEST_F(nfsd_listener, val_second_entry_bad)
{
	struct sockaddr_storage ss = { .ss_family = AF_UNIX };
	struct raw_listener bad = { .xprt = "tcp", .emit_addr = 1, .addr = &ss,
				    .addr_len = sizeof(struct sockaddr_in) };
	struct listener_ent got[MAX_LISTENERS];
	char attrs[128];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	off = put_raw_listener(attrs, off, &bad);
	/* The whole request is rejected during validation; nothing applied. */
	EXPECT_EQ(-EAFNOSUPPORT, listener_set(attrs, off));
	/*
	 * Again the errno alone does not say so: svc_xprt_create_from_sa()
	 * also returns -EAFNOSUPPORT, and the doit keeps the listeners it did
	 * manage to create, so the well-formed tcp entry ahead of the bad one
	 * would still be up.
	 */
	EXPECT_EQ(0, listener_get(got, MAX_LISTENERS));
}

/*
 * A rejected request must leave the listeners that are already up alone.
 * The errno alone does not show that: svc_xprt_create_from_sa() returns
 * -EPROTONOSUPPORT for an unknown name too. What differs is how far the
 * request gets -- without the check in nfsd_nl_validate_listeners(),
 * nfsd_nl_listener_set_doit() has already moved the unmatched tcp listener
 * off sv_permsocks and run svc_xprt_destroy_all() on it by the time the
 * name fails.
 */
TEST_F(nfsd_listener, val_reject_keeps_listeners)
{
	struct listener_ent got[MAX_LISTENERS];
	char good[64], bad[64];
	int og = put_listener(good, 0, "tcp", TEST_PORT);
	int ob = put_listener(bad, 0, "bogus_xprt", TEST_PORT);

	ASSERT_EQ(0, listener_set(good, og));
	ASSERT_EQ(1, listener_get(got, MAX_LISTENERS));

	EXPECT_EQ(-EPROTONOSUPPORT, listener_set(bad, ob));

	ASSERT_EQ(1, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 1, "tcp", AF_INET, TEST_PORT));
}

/* ===================== functional / round-trip ===================== */

/* LISTENER_GET with no serv in this netns returns an empty list. */
TEST_F(nfsd_listener, func_get_empty)
{
	struct listener_ent got[MAX_LISTENERS];

	EXPECT_EQ(0, listener_get(got, MAX_LISTENERS));
}

TEST_F(nfsd_listener, func_create_tcp)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set(attrs, off));
	EXPECT_STREQ("", last_extack);		/* nothing to warn about */
	ASSERT_EQ(1, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 1, "tcp", AF_INET, TEST_PORT));
}

TEST_F(nfsd_listener, func_create_udp)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[64];
	int off = put_listener(attrs, 0, "udp", TEST_PORT);

	ASSERT_EQ(0, listener_set(attrs, off));
	ASSERT_EQ(1, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 1, "udp", AF_INET, TEST_PORT));
}

TEST_F(nfsd_listener, func_create_multi)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[128];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	off = put_listener(attrs, off, "udp", TEST_PORT);
	ASSERT_EQ(0, listener_set(attrs, off));
	ASSERT_EQ(2, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 2, "tcp", AF_INET, TEST_PORT));
	EXPECT_NE(NULL, find_listener(got, 2, "udp", AF_INET, TEST_PORT));
}

TEST_F(nfsd_listener, func_idempotent)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set(attrs, off));
	EXPECT_EQ(0, listener_set(attrs, off));		/* re-set same list */
	ASSERT_EQ(1, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 1, "tcp", AF_INET, TEST_PORT));
}

TEST_F(nfsd_listener, func_add)
{
	struct listener_ent got[MAX_LISTENERS];
	char one[64], two[128];
	int o1 = put_listener(one, 0, "tcp", TEST_PORT);
	int o2 = put_listener(two, 0, "tcp", TEST_PORT);

	o2 = put_listener(two, o2, "udp", TEST_PORT);
	ASSERT_EQ(0, listener_set(one, o1));
	ASSERT_EQ(0, listener_set(two, o2));		/* add udp, keep tcp */
	ASSERT_EQ(2, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 2, "tcp", AF_INET, TEST_PORT));
	EXPECT_NE(NULL, find_listener(got, 2, "udp", AF_INET, TEST_PORT));
}

TEST_F(nfsd_listener, func_remove_subset)
{
	struct listener_ent got[MAX_LISTENERS];
	char both[128], one[64];
	int ob = put_listener(both, 0, "tcp", TEST_PORT);
	int oo = put_listener(one, 0, "tcp", TEST_PORT);

	ob = put_listener(both, ob, "udp", TEST_PORT);
	ASSERT_EQ(0, listener_set(both, ob));
	ASSERT_EQ(0, listener_set(one, oo));		/* drop udp */
	ASSERT_EQ(1, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 1, "tcp", AF_INET, TEST_PORT));
}

/*
 * LISTENER_GET cannot tell a destroyed serv from a live one with no
 * permsocks: nfsd_nl_listener_get_doit() replies empty either way. The
 * rpcbind client can. nfsd_destroy_serv() is the only path that reaches
 * svc_xprt_destroy_all(..., unregister=true) -> svc_rpcb_cleanup() ->
 * rpcb_put_local(), which drops the last user and shuts the local client
 * down; the next serv then has to connect again. Leaving the serv in place
 * would keep the first connection and the stub would see just the one.
 */
TEST_F(nfsd_listener, func_empty_destroys)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);
	int conns;

	ASSERT_EQ(0, listener_set(attrs, off));
	conns = rpcb_conns();
	ASSERT_GT(conns, 0);

	EXPECT_EQ(0, listener_set(NULL, 0));		/* empty -> destroy serv */
	EXPECT_EQ(0, listener_get(got, MAX_LISTENERS));

	ASSERT_EQ(0, listener_set(attrs, off));
	EXPECT_GT(rpcb_conns(), conns);
}

TEST_F(nfsd_listener, func_ipv6)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[64];
	int off, s;

	s = socket(AF_INET6, SOCK_STREAM, 0);
	if (s < 0)
		SKIP(return, "IPv6 unavailable: %s", strerror(errno));
	close(s);

	off = put_listener_af(attrs, 0, "tcp", AF_INET6, TEST_PORT);
	ASSERT_EQ(0, listener_set(attrs, off));
	ASSERT_EQ(1, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 1, "tcp", AF_INET6, TEST_PORT));
}

/* ===================== rpcbind registration ===================== */

/*
 * A rpcbind that refuses the registration takes the listener down with it.
 * svc_register() fails, so svc_setup_socket() fails, so no listener is
 * created. -EACCES alone does not show that, since a bind can return it
 * too, so read the listener set back as well.
 */
TEST_F(nfsd_listener, sem_register_refused)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	rpcb_stub_set_mode(RPCB_STUB_REFUSE);

	EXPECT_EQ(-EACCES, listener_set(attrs, off));
	EXPECT_STRNE("", last_extack);
	EXPECT_EQ(0, listener_get(got, MAX_LISTENERS));
}

/*
 * A listener that cannot be created reports which one it was: the errno
 * alone does not name the entry in a multi-listener request.
 */
TEST_F(nfsd_listener, sem_create_failure_extack)
{
	struct sockaddr_in s4 = { .sin_family = AF_INET,
				  .sin_port = htons(TEST_PORT),
				  .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	struct listener_ent got[MAX_LISTENERS];
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);
	int s;

	/* squat on the port so the listener cannot bind */
	s = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GE(s, 0);
	ASSERT_EQ(0, bind(s, (struct sockaddr *)&s4, sizeof(s4)));

	EXPECT_EQ(-EADDRINUSE, listener_set(attrs, off));
	EXPECT_STRNE("", last_extack);
	EXPECT_EQ(0, listener_get(got, MAX_LISTENERS));
	close(s);
}

/* ============ one rpcbind attempt for each request ============ */

/*
 * Every listener used to register on its own, so a rpcbind that never
 * answers cost one timeout for each entry. Ask for one listener, then for
 * three, and compare what the stub saw. Three entries must not cost three
 * times as much.
 *
 * The stub has to stay silent rather than refuse. A refusal is an answer,
 * and rpcbind refuses one entry at a time, so the count ignores it.
 */
TEST_F(nfsd_listener, rpcb_stop_after_failure)
{
	int before, one, three, off;
	char attrs[192];

	rpcb_stub_set_mode(RPCB_STUB_SILENT);

	before = rpcb_calls();
	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	listener_set(attrs, off);
	one = rpcb_calls() - before;
	ASSERT_GT(one, 0);

	ASSERT_EQ(0, listener_set(attrs, 0));

	before = rpcb_calls();
	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 1);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 2);
	listener_set(attrs, off);
	three = rpcb_calls() - before;

	/* the second and third entries must not reach rpcbind at all */
	EXPECT_LE(three, one);
}

/*
 * The entry that finds rpcbind silent is the one that pays for the
 * discovery, and v3 has no vs_rpcb_optnl to discard the error, so it is the
 * only entry whose listener would be lost. Nothing distinguishes it from the
 * rest of the request, and a retry of the same request would fail the same
 * entry again, so the set would stay short for as long as rpcbind was quiet.
 *
 * Ask for three listeners against a silent stub and require the whole set,
 * a success, and a warning that says why.
 */
TEST_F(nfsd_listener, rpcb_silent_set_complete)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[192];
	int off;

	rpcb_stub_set_mode(RPCB_STUB_SILENT);

	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 1);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 2);
	EXPECT_EQ(0, listener_set(attrs, off));

	/* the first entry is not the odd one out */
	EXPECT_EQ(3, listener_get(got, MAX_LISTENERS));
	/* no errno reports this, so the ack has to */
	EXPECT_STRNE("", last_extack);
}

/*
 * The case that needs the count rather than a failed listener. NFSv4 sets
 * vs_rpcb_optnl, so svc_generic_rpcbind_set() discards the error, every
 * listener comes up, and nothing reports a failure. Without the fix each
 * entry still waits for rpcbind on its own.
 *
 * Make the server v4-only, answer no SET, and require three things: the
 * listeners come up, the ack warns that they are not registered, and the
 * stub does not see one round trip for each entry.
 */
TEST_F(nfsd_listener, rpcb_v4_only_bounded)
{
	struct listener_ent got[MAX_LISTENERS];
	int before, one, three, off;
	char attrs[192];

	/* refuses once a serv exists, so this has to come first */
	ASSERT_EQ(0, version_set_only(4, 1));
	rpcb_stub_set_mode(RPCB_STUB_SILENT);

	before = rpcb_calls();
	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	ASSERT_EQ(0, listener_set(attrs, off));
	one = rpcb_calls() - before;
	ASSERT_GT(one, 0);

	/* start over, so the second measurement also builds a serv */
	ASSERT_EQ(0, listener_set(attrs, 0));

	before = rpcb_calls();
	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 1);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 2);
	ASSERT_EQ(0, listener_set(attrs, off));
	three = rpcb_calls() - before;

	/* the listeners are up even though rpcbind never answered */
	EXPECT_EQ(3, listener_get(got, MAX_LISTENERS));
	/* and the ack says they are unregistered, since no errno can */
	EXPECT_STRNE("", last_extack);
	EXPECT_LE(three, one);
}

/*
 * The stop applies to one request only. After rpcbind starts answering,
 * the next request must register without any other step.
 */
TEST_F(nfsd_listener, rpcb_retry_next_request)
{
	int before, after, off;
	char attrs[192];

	rpcb_stub_set_mode(RPCB_STUB_SILENT);

	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 1);
	listener_set(attrs, off);
	ASSERT_EQ(0, listener_set(attrs, 0));

	/* rpcbind recovers */
	rpcb_stub_set_mode(RPCB_STUB_ACCEPT);

	before = rpcb_calls();
	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	EXPECT_EQ(0, listener_set(attrs, off));
	after = rpcb_calls();

	/* a fresh request starts from a fresh reading and tries again */
	EXPECT_GT(after, before);
	EXPECT_STREQ("", last_extack);
}

/*
 * The same rule on the way out. Removing a listener unregisters it, so a
 * rpcbind that stops answering used to cost one timeout for each listener
 * removed. Register one listener while the stub answers, silence the stub,
 * remove it and count; then do the same with three.
 *
 * Both measurements also pay the svc_unregister() sweep that
 * nfsd_destroy_serv() runs once the last listener is gone, so that cancels
 * out of the comparison.
 */
TEST_F(nfsd_listener, rpcb_unreg_stop_after_failure)
{
	int before, one, three, off;
	char attrs[192];

	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	ASSERT_EQ(0, listener_set(attrs, off));

	rpcb_stub_set_mode(RPCB_STUB_SILENT);
	before = rpcb_calls();
	ASSERT_EQ(0, listener_set(NULL, 0));
	one = rpcb_calls() - before;
	ASSERT_GT(one, 0);

	rpcb_stub_set_mode(RPCB_STUB_ACCEPT);
	off = put_listener(attrs, 0, "tcp", TEST_PORT);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 1);
	off = put_listener(attrs, off, "tcp", TEST_PORT + 2);
	ASSERT_EQ(0, listener_set(attrs, off));

	rpcb_stub_set_mode(RPCB_STUB_SILENT);
	before = rpcb_calls();
	ASSERT_EQ(0, listener_set(NULL, 0));
	three = rpcb_calls() - before;

	/* the second and third removals must not reach rpcbind at all */
	EXPECT_LE(three, one);
}

/* ===================== userspace rpcbind ===================== */

/*
 * The point of the flag: nfsd must make no rpcbind call at all. svc_bind()
 * pings rpcbind at client creation and svc_register() calls it once per
 * program and version, so a silent stub is what proves both were skipped.
 */
TEST_F(nfsd_listener, rpcb_userspace_no_traffic)
{
	struct rpcb_reply r;
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, rpcb_conns());
	ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));
	EXPECT_EQ(0, rpcb_conns());
	EXPECT_EQ(0, rpcb_calls());
}

/*
 * The reply has to tell the caller what to register. Without the program
 * list it cannot know whether nfsacl is built in.
 */
TEST_F(nfsd_listener, rpcb_userspace_reply)
{
	struct rpcb_reply r;
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));
	EXPECT_EQ(1, r.acked);
	EXPECT_GT(r.nprog, 0);
	/* the listener came up and is named, so the caller knows the port */
	ASSERT_EQ(1, r.nlistener);
	EXPECT_NE(NULL, find_listener(r.listener, r.nlistener, "tcp",
				      AF_INET, TEST_PORT));
	/* nfslocalio is hidden and must never be offered for registration */
	EXPECT_EQ(NULL, find_rpcb(&r, 400122, 1));
}

/*
 * nfsd_nl_validate_listeners() allows far more listeners than the default
 * genl buffer holds, so the reply has to be sized from its contents. If it
 * is not, the listeners all come up and the caller still sees -EMSGSIZE.
 */
TEST_F(nfsd_listener, rpcb_userspace_many_listeners)
{
	char attrs[RPCB_MANY_LISTENERS * 64];
	struct rpcb_reply r;
	int off = 0, i;

	for (i = 0; i < RPCB_MANY_LISTENERS; i++)
		off = put_listener(attrs, off, "tcp", TEST_PORT + i);

	ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));
	EXPECT_EQ(1, r.acked);
	EXPECT_GT(r.nprog, 0);
	EXPECT_EQ(RPCB_MANY_LISTENERS, r.naddr);
}

/*
 * NFSv4 sets vs_need_cong_ctrl, so the kernel never registered it on UDP.
 * The reply cannot filter it out, because the rule depends on the listener,
 * so it must carry the flag instead.
 */
TEST_F(nfsd_listener, rpcb_userspace_no_v4_udp)
{
	struct rpcb_ent *v4, *v3;
	struct rpcb_reply r;
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));

	/* v3 is always built in and on by default, so it must be offered */
	v3 = find_rpcb(&r, NFS_PROGRAM, 3);
	ASSERT_NE(NULL, v3);
	EXPECT_EQ(0, v3->flags & NFSD_RPCBIND_FLAGS_NO_UDP);

	v4 = find_rpcb(&r, NFS_PROGRAM, 4);
	if (!v4)
		SKIP(return, "NFSv4 is not enabled");
	EXPECT_EQ(NFSD_RPCBIND_FLAGS_NO_UDP,
		  v4->flags & NFSD_RPCBIND_FLAGS_NO_UDP);
}

/*
 * nfsacl is the value userland cannot derive: CONFIG_NFSD_V3_ACL is not
 * visible over netlink. Only assert self-consistency -- if the kernel
 * offers nfsacl v3 then it must also offer nfs v3, since both gate on the
 * same enabled version.
 */
TEST_F(nfsd_listener, rpcb_userspace_nfsacl_follows_nfs)
{
	struct rpcb_reply r;
	char attrs[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));
	if (find_rpcb(&r, NFS_ACL_PROGRAM, 3))
		EXPECT_NE(NULL, find_rpcb(&r, NFS_PROGRAM, 3));
}

/*
 * svc_bind() decided whether to take the rpcb_users reference that teardown
 * drops, so ownership cannot flip under a live serv.
 */
TEST_F(nfsd_listener, rpcb_userspace_busy)
{
	struct rpcb_reply r;
	char attrs[64], plain[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);
	int poff = put_listener(plain, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));

	/* same listeners, but now asking the kernel to own rpcbind */
	EXPECT_EQ(-EBUSY, listener_set(plain, poff));
	EXPECT_STRNE("", last_extack);
}

/* And the same the other way round. */
TEST_F(nfsd_listener, rpcb_userspace_busy_reverse)
{
	struct rpcb_reply r;
	char attrs[64], plain[64];
	int poff = put_listener(plain, 0, "tcp", TEST_PORT);
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set(plain, poff));
	EXPECT_EQ(-EBUSY, listener_set_rpcb(attrs, off, &r));
}

#define NFSDFS_DIR	"/run/nfsdfs"

/* nfsdfs binds to the netns it is mounted in, so mount a private one */
static int mount_nfsdfs(void)
{
	if (mkdir(NFSDFS_DIR, 0700) < 0 && errno != EEXIST)
		return -1;
	return mount("nfsd", NFSDFS_DIR, "nfsd", 0, NULL);
}

/* simple_transaction allows one write per open, so open every time */
static int write_portlist(int sock)
{
	char buf[32];
	int fd, len, ret = 0;

	fd = open(NFSDFS_DIR "/portlist", O_WRONLY);
	if (fd < 0)
		return -errno;
	len = snprintf(buf, sizeof(buf), "%d\n", sock);
	if (write(fd, buf, len) < 0)
		ret = -errno;
	close(fd);
	return ret;
}

static int tcp_listener(int port)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons(port),
	};
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		return -1;
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 ||
	    listen(fd, 1) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

/*
 * The legacy portlist interface cannot hand a listener to the rpcbind owner
 * and svc_register() will not register it, so it has to refuse.
 */
TEST_F(nfsd_listener, rpcb_userspace_portlist_busy)
{
	struct rpcb_reply r;
	char attrs[64], plain[64];
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);
	int poff = put_listener(plain, 0, "tcp", TEST_PORT);
	int sock;

	if (mount_nfsdfs() < 0)
		SKIP(return, "cannot mount nfsdfs: %s", strerror(errno));

	sock = tcp_listener(TEST_PORT + 1);
	ASSERT_GE(sock, 0);

	ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));
	EXPECT_EQ(-EBUSY, write_portlist(sock));

	/* the same write against a kernel-owned serv still works */
	ASSERT_EQ(0, listener_set_rpcb(attrs, 0, &r));	/* destroys the serv */
	ASSERT_EQ(0, listener_set(plain, poff));
	EXPECT_EQ(0, write_portlist(sock));

	close(sock);
}

/*
 * rpcb_create_local() increments sn->rpcb_users and rpcb_put_local()
 * decrements it. A serv that never took the reference must not drop it, or
 * the next serv finds the count wrong. Cycle a few times, then confirm a
 * kernel-owned serv can still reach rpcbind.
 */
TEST_F(nfsd_listener, rpcb_userspace_teardown)
{
	struct rpcb_reply r;
	char attrs[64], plain[64];
	int off, poff, i;

	for (i = 0; i < 3; i++) {
		off = put_listener(attrs, 0, "tcp", TEST_PORT);
		ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));
		/* empty list with no threads destroys the serv */
		ASSERT_EQ(0, listener_set_rpcb(attrs, 0, &r));
		ASSERT_EQ(0, rpcb_conns());
	}

	poff = put_listener(plain, 0, "tcp", TEST_PORT);
	ASSERT_EQ(0, listener_set(plain, poff));
	EXPECT_GT(rpcb_conns(), 0);
}

/*
 * Teardown destroys the serv, so it is not an ownership change. A caller
 * that never learned about the flag has to be able to shut nfsd down.
 */
TEST_F(nfsd_listener, rpcb_userspace_unflagged_teardown)
{
	struct listener_ent got[MAX_LISTENERS];
	char attrs[64], plain[64];
	struct rpcb_reply r;
	int off = put_listener(attrs, 0, "tcp", TEST_PORT);
	int poff = put_listener(plain, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set_rpcb(attrs, off, &r));
	ASSERT_EQ(1, r.nlistener);

	/* no flag, no listeners */
	EXPECT_EQ(0, listener_set(NULL, 0));
	EXPECT_EQ(0, listener_get(got, MAX_LISTENERS));

	/* the serv is gone, so kernel ownership can be taken */
	EXPECT_EQ(0, listener_set(plain, poff));
}

/* And the same the other way round. */
TEST_F(nfsd_listener, rpcb_userspace_flagged_teardown_of_kernel_serv)
{
	char attrs[64], plain[64];
	struct rpcb_reply r;
	int poff = put_listener(plain, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set(plain, poff));
	EXPECT_EQ(0, listener_set_rpcb(attrs, 0, &r));
	EXPECT_EQ(0, listener_set(plain, poff));
}

/* ===================== threads / -EBUSY semantics ===================== */

TEST_F(nfsd_listener, sem_busy_on_change)
{
	struct listener_ent got[MAX_LISTENERS];
	char one[64], two[128];
	int o1 = put_listener(one, 0, "tcp", TEST_PORT);
	int o2 = put_listener(two, 0, "tcp", TEST_PORT);

	o2 = put_listener(two, o2, "udp", TEST_PORT);
	ASSERT_EQ(0, listener_set(one, o1));
	ASSERT_EQ(0, threads_set(1));			/* threads now running */
	EXPECT_EQ(-EBUSY, listener_set(two, o2));	/* add refused */

	/* refused means refused: the udp listener must not have been added */
	EXPECT_EQ(1, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 1, "tcp", AF_INET, TEST_PORT));

	threads_set(0);					/* stop before netns exit */
}

TEST_F(nfsd_listener, sem_busy_on_remove)
{
	struct listener_ent got[MAX_LISTENERS];
	char one[64];
	int o1 = put_listener(one, 0, "tcp", TEST_PORT);

	ASSERT_EQ(0, listener_set(one, o1));
	ASSERT_EQ(0, threads_set(1));
	EXPECT_EQ(-EBUSY, listener_set(NULL, 0));	/* remove refused */

	/* the doit moves the permsocks to a temp list before it can fail */
	EXPECT_EQ(1, listener_get(got, MAX_LISTENERS));
	EXPECT_NE(NULL, find_listener(got, 1, "tcp", AF_INET, TEST_PORT));

	threads_set(0);
}

TEST_HARNESS_MAIN
