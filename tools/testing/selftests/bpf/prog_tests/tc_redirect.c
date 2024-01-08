// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/*
 * This test sets up 3 netns (src <-> fwd <-> dst). There is no direct veth link
 * between src and dst. The netns fwd has veth links to each src and dst. The
 * client is in src and server in dst. The test installs a TC BPF program to each
 * host facing veth in fwd which calls into i) bpf_redirect_neigh() to perform the
 * neigh addr population and redirect or ii) bpf_redirect_peer() for namespace
 * switch from ingress side; it also installs a checker prog on the egress side
 * to drop unexpected traffic.
 */

#include <arpa/inet.h>
#include <linux/if_tun.h>
#include <linux/limits.h>
#include <linux/sysctl.h>
#include <linux/time_types.h>
#include <linux/net_tstamp.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "netlink_helpers.h"
#include "test_tc_neigh_fib.skel.h"
#include "test_tc_neigh.skel.h"
#include "test_tc_peer.skel.h"
#include "test_tc_dtime.skel.h"

#ifndef TCP_TX_DELAY
#define TCP_TX_DELAY 37
#endif

#define NS_SRC "ns_src"
#define NS_FWD "ns_fwd"
#define NS_DST "ns_dst"

#define IP4_SRC "172.16.1.100"
#define IP4_DST "172.16.2.100"
#define IP4_TUN_SRC "172.17.1.100"
#define IP4_TUN_FWD "172.17.1.200"
#define IP4_PORT 9004

#define IP6_SRC "0::1:dead:beef:cafe"
#define IP6_DST "0::2:dead:beef:cafe"
#define IP6_TUN_SRC "1::1:dead:beef:cafe"
#define IP6_TUN_FWD "1::2:dead:beef:cafe"
#define IP6_PORT 9006

#define IP4_SLL "169.254.0.1"
#define IP4_DLL "169.254.0.2"
#define IP4_NET "169.254.0.0"

#define MAC_DST_FWD "00:11:22:33:44:55"
#define MAC_DST "00:22:33:44:55:66"

#define IFADDR_STR_LEN 18
#define PING_ARGS "-i 0.2 -c 3 -w 10 -q"

#define TIMEOUT_MILLIS 10000
#define NSEC_PER_SEC 1000000000ULL

#define log_err(MSG, ...) \
	fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
		__FILE__, __LINE__, strerror(errno), ##__VA_ARGS__)

static const char * const namespaces[] = {NS_SRC, NS_FWD, NS_DST, NULL};

static int write_file(const char *path, const char *newval)
{
	FILE *f;

	f = fopen(path, "r+");
	if (!f)
		return -1;
	if (fwrite(newval, strlen(newval), 1, f) != 1) {
		log_err("writing to %s failed", path);
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int netns_setup_namespaces(const char *verb)
{
	const char * const *ns = namespaces;
	char cmd[128];

	while (*ns) {
		snprintf(cmd, sizeof(cmd), "ip netns %s %s", verb, *ns);
		if (!ASSERT_OK(system(cmd), cmd))
			return -1;
		ns++;
	}
	return 0;
}

static void netns_setup_namespaces_nofail(const char *verb)
{
	const char * const *ns = namespaces;
	char cmd[128];

	while (*ns) {
		snprintf(cmd, sizeof(cmd), "ip netns %s %s > /dev/null 2>&1", verb, *ns);
		system(cmd);
		ns++;
	}
}

enum dev_mode {
	MODE_VETH,
	MODE_NETKIT,
};

struct netns_setup_result {
	enum dev_mode dev_mode;
	int ifindex_src;
	int ifindex_src_fwd;
	int ifindex_dst;
	int ifindex_dst_fwd;
};

static int get_ifaddr(const char *name, char *ifaddr)
{
	char path[PATH_MAX];
	FILE *f;
	int ret;

	snprintf(path, PATH_MAX, "/sys/class/net/%s/address", name);
	f = fopen(path, "r");
	if (!ASSERT_OK_PTR(f, path))
		return -1;

	ret = fread(ifaddr, 1, IFADDR_STR_LEN, f);
	if (!ASSERT_EQ(ret, IFADDR_STR_LEN, "fread ifaddr")) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int create_netkit(int mode, char *prim, char *peer)
{
	struct rtattr *linkinfo, *data, *peer_info;
	struct rtnl_handle rth = { .fd = -1 };
	const char *type = "netkit";
	struct {
		struct nlmsghdr n;
		struct ifinfomsg i;
		char buf[1024];
	} req = {};
	int err;

	err = rtnl_open(&rth, 0);
	if (!ASSERT_OK(err, "open_rtnetlink"))
		return err;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = AF_UNSPEC;

	addattr_l(&req.n, sizeof(req), IFLA_IFNAME, prim, strlen(prim));
	linkinfo = addattr_nest(&req.n, sizeof(req), IFLA_LINKINFO);
	addattr_l(&req.n, sizeof(req), IFLA_INFO_KIND, type, strlen(type));
	data = addattr_nest(&req.n, sizeof(req), IFLA_INFO_DATA);
	addattr32(&req.n, sizeof(req), IFLA_NETKIT_MODE, mode);
	peer_info = addattr_nest(&req.n, sizeof(req), IFLA_NETKIT_PEER_INFO);
	req.n.nlmsg_len += sizeof(struct ifinfomsg);
	addattr_l(&req.n, sizeof(req), IFLA_IFNAME, peer, strlen(peer));
	addattr_nest_end(&req.n, peer_info);
	addattr_nest_end(&req.n, data);
	addattr_nest_end(&req.n, linkinfo);

	err = rtnl_talk(&rth, &req.n, NULL);
	ASSERT_OK(err, "talk_rtnetlink");
	rtnl_close(&rth);
	return err;
}

static int netns_setup_links_and_routes(struct netns_setup_result *result)
{
	struct nstoken *nstoken = NULL;
	char src_fwd_addr[IFADDR_STR_LEN+1] = {};
	int err;

	if (result->dev_mode == MODE_VETH) {
		SYS(fail, "ip link add src type veth peer name src_fwd");
		SYS(fail, "ip link add dst type veth peer name dst_fwd");

		SYS(fail, "ip link set dst_fwd address " MAC_DST_FWD);
		SYS(fail, "ip link set dst address " MAC_DST);
	} else if (result->dev_mode == MODE_NETKIT) {
		err = create_netkit(NETKIT_L3, "src", "src_fwd");
		if (!ASSERT_OK(err, "create_ifindex_src"))
			goto fail;
		err = create_netkit(NETKIT_L3, "dst", "dst_fwd");
		if (!ASSERT_OK(err, "create_ifindex_dst"))
			goto fail;
	}

	if (get_ifaddr("src_fwd", src_fwd_addr))
		goto fail;

	result->ifindex_src = if_nametoindex("src");
	if (!ASSERT_GT(result->ifindex_src, 0, "ifindex_src"))
		goto fail;

	result->ifindex_src_fwd = if_nametoindex("src_fwd");
	if (!ASSERT_GT(result->ifindex_src_fwd, 0, "ifindex_src_fwd"))
		goto fail;

	result->ifindex_dst = if_nametoindex("dst");
	if (!ASSERT_GT(result->ifindex_dst, 0, "ifindex_dst"))
		goto fail;

	result->ifindex_dst_fwd = if_nametoindex("dst_fwd");
	if (!ASSERT_GT(result->ifindex_dst_fwd, 0, "ifindex_dst_fwd"))
		goto fail;

	SYS(fail, "ip link set src netns " NS_SRC);
	SYS(fail, "ip link set src_fwd netns " NS_FWD);
	SYS(fail, "ip link set dst_fwd netns " NS_FWD);
	SYS(fail, "ip link set dst netns " NS_DST);

	/** setup in 'src' namespace */
	nstoken = open_netns(NS_SRC);
	if (!ASSERT_OK_PTR(nstoken, "setns src"))
		goto fail;

	SYS(fail, "ip addr add " IP4_SRC "/32 dev src");
	SYS(fail, "ip addr add " IP6_SRC "/128 dev src nodad");
	SYS(fail, "ip link set dev src up");

	SYS(fail, "ip route add " IP4_DST "/32 dev src scope global");
	SYS(fail, "ip route add " IP4_NET "/16 dev src scope global");
	SYS(fail, "ip route add " IP6_DST "/128 dev src scope global");

	if (result->dev_mode == MODE_VETH) {
		SYS(fail, "ip neigh add " IP4_DST " dev src lladdr %s",
		    src_fwd_addr);
		SYS(fail, "ip neigh add " IP6_DST " dev src lladdr %s",
		    src_fwd_addr);
	}

	close_netns(nstoken);

	/** setup in 'fwd' namespace */
	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns fwd"))
		goto fail;

	/* The fwd netns automatically gets a v6 LL address / routes, but also
	 * needs v4 one in order to start ARP probing. IP4_NET route is added
	 * to the endpoints so that the ARP processing will reply.
	 */
	SYS(fail, "ip addr add " IP4_SLL "/32 dev src_fwd");
	SYS(fail, "ip addr add " IP4_DLL "/32 dev dst_fwd");
	SYS(fail, "ip link set dev src_fwd up");
	SYS(fail, "ip link set dev dst_fwd up");

	SYS(fail, "ip route add " IP4_SRC "/32 dev src_fwd scope global");
	SYS(fail, "ip route add " IP6_SRC "/128 dev src_fwd scope global");
	SYS(fail, "ip route add " IP4_DST "/32 dev dst_fwd scope global");
	SYS(fail, "ip route add " IP6_DST "/128 dev dst_fwd scope global");

	close_netns(nstoken);

	/** setup in 'dst' namespace */
	nstoken = open_netns(NS_DST);
	if (!ASSERT_OK_PTR(nstoken, "setns dst"))
		goto fail;

	SYS(fail, "ip addr add " IP4_DST "/32 dev dst");
	SYS(fail, "ip addr add " IP6_DST "/128 dev dst nodad");
	SYS(fail, "ip link set dev dst up");

	SYS(fail, "ip route add " IP4_SRC "/32 dev dst scope global");
	SYS(fail, "ip route add " IP4_NET "/16 dev dst scope global");
	SYS(fail, "ip route add " IP6_SRC "/128 dev dst scope global");

	if (result->dev_mode == MODE_VETH) {
		SYS(fail, "ip neigh add " IP4_SRC " dev dst lladdr " MAC_DST_FWD);
		SYS(fail, "ip neigh add " IP6_SRC " dev dst lladdr " MAC_DST_FWD);
	}

	close_netns(nstoken);

	return 0;
fail:
	if (nstoken)
		close_netns(nstoken);
	return -1;
}

static int qdisc_clsact_create(struct bpf_tc_hook *qdisc_hook, int ifindex)
{
	char err_str[128], ifname[16];
	int err;

	qdisc_hook->ifindex = ifindex;
	qdisc_hook->attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS;
	err = bpf_tc_hook_create(qdisc_hook);
	snprintf(err_str, sizeof(err_str),
		 "qdisc add dev %s clsact",
		 if_indextoname(qdisc_hook->ifindex, ifname) ? : "<unknown_iface>");
	err_str[sizeof(err_str) - 1] = 0;
	ASSERT_OK(err, err_str);

	return err;
}

static int xgress_filter_add(struct bpf_tc_hook *qdisc_hook,
			     enum bpf_tc_attach_point xgress,
			     const struct bpf_program *prog, int priority)
{
	LIBBPF_OPTS(bpf_tc_opts, tc_attach);
	char err_str[128], ifname[16];
	int err;

	qdisc_hook->attach_point = xgress;
	tc_attach.prog_fd = bpf_program__fd(prog);
	tc_attach.priority = priority;
	err = bpf_tc_attach(qdisc_hook, &tc_attach);
	snprintf(err_str, sizeof(err_str),
		 "filter add dev %s %s prio %d bpf da %s",
		 if_indextoname(qdisc_hook->ifindex, ifname) ? : "<unknown_iface>",
		 xgress == BPF_TC_INGRESS ? "ingress" : "egress",
		 priority, bpf_program__name(prog));
	err_str[sizeof(err_str) - 1] = 0;
	ASSERT_OK(err, err_str);

	return err;
}

#define QDISC_CLSACT_CREATE(qdisc_hook, ifindex) ({		\
	if ((err = qdisc_clsact_create(qdisc_hook, ifindex)))	\
		goto fail;					\
})

#define XGRESS_FILTER_ADD(qdisc_hook, xgress, prog, priority) ({		\
	if ((err = xgress_filter_add(qdisc_hook, xgress, prog, priority)))	\
		goto fail;							\
})

static int netns_load_bpf(const struct bpf_program *src_prog,
			  const struct bpf_program *dst_prog,
			  const struct bpf_program *chk_prog,
			  const struct netns_setup_result *setup_result)
{
	LIBBPF_OPTS(bpf_tc_hook, qdisc_src_fwd);
	LIBBPF_OPTS(bpf_tc_hook, qdisc_dst_fwd);
	int err;

	/* tc qdisc add dev src_fwd clsact */
	QDISC_CLSACT_CREATE(&qdisc_src_fwd, setup_result->ifindex_src_fwd);
	/* tc filter add dev src_fwd ingress bpf da src_prog */
	XGRESS_FILTER_ADD(&qdisc_src_fwd, BPF_TC_INGRESS, src_prog, 0);
	/* tc filter add dev src_fwd egress bpf da chk_prog */
	XGRESS_FILTER_ADD(&qdisc_src_fwd, BPF_TC_EGRESS, chk_prog, 0);

	/* tc qdisc add dev dst_fwd clsact */
	QDISC_CLSACT_CREATE(&qdisc_dst_fwd, setup_result->ifindex_dst_fwd);
	/* tc filter add dev dst_fwd ingress bpf da dst_prog */
	XGRESS_FILTER_ADD(&qdisc_dst_fwd, BPF_TC_INGRESS, dst_prog, 0);
	/* tc filter add dev dst_fwd egress bpf da chk_prog */
	XGRESS_FILTER_ADD(&qdisc_dst_fwd, BPF_TC_EGRESS, chk_prog, 0);

	return 0;
fail:
	return -1;
}

static void test_tcp(int family, const char *addr, __u16 port)
{
	int listen_fd = -1, accept_fd = -1, client_fd = -1;
	char buf[] = "testing testing";
	int n;
	struct nstoken *nstoken;

	nstoken = open_netns(NS_DST);
	if (!ASSERT_OK_PTR(nstoken, "setns dst"))
		return;

	listen_fd = start_server(family, SOCK_STREAM, addr, port, 0);
	if (!ASSERT_GE(listen_fd, 0, "listen"))
		goto done;

	close_netns(nstoken);
	nstoken = open_netns(NS_SRC);
	if (!ASSERT_OK_PTR(nstoken, "setns src"))
		goto done;

	client_fd = connect_to_fd(listen_fd, TIMEOUT_MILLIS);
	if (!ASSERT_GE(client_fd, 0, "connect_to_fd"))
		goto done;

	accept_fd = accept(listen_fd, NULL, NULL);
	if (!ASSERT_GE(accept_fd, 0, "accept"))
		goto done;

	if (!ASSERT_OK(settimeo(accept_fd, TIMEOUT_MILLIS), "settimeo"))
		goto done;

	n = write(client_fd, buf, sizeof(buf));
	if (!ASSERT_EQ(n, sizeof(buf), "send to server"))
		goto done;

	n = read(accept_fd, buf, sizeof(buf));
	ASSERT_EQ(n, sizeof(buf), "recv from server");

done:
	if (nstoken)
		close_netns(nstoken);
	if (listen_fd >= 0)
		close(listen_fd);
	if (accept_fd >= 0)
		close(accept_fd);
	if (client_fd >= 0)
		close(client_fd);
}

static int test_ping(int family, const char *addr)
{
	SYS(fail, "ip netns exec " NS_SRC " %s " PING_ARGS " %s > /dev/null", ping_command(family), addr);
	return 0;
fail:
	return -1;
}

static void test_connectivity(void)
{
	test_tcp(AF_INET, IP4_DST, IP4_PORT);
	test_ping(AF_INET, IP4_DST);
	test_tcp(AF_INET6, IP6_DST, IP6_PORT);
	test_ping(AF_INET6, IP6_DST);
}

static int set_forwarding(bool enable)
{
	int err;

	err = write_file("/proc/sys/net/ipv4/ip_forward", enable ? "1" : "0");
	if (!ASSERT_OK(err, "set ipv4.ip_forward=0"))
		return err;

	err = write_file("/proc/sys/net/ipv6/conf/all/forwarding", enable ? "1" : "0");
	if (!ASSERT_OK(err, "set ipv6.forwarding=0"))
		return err;

	return 0;
}

static void rcv_tstamp(int fd, const char *expected, size_t s)
{
	struct __kernel_timespec pkt_ts = {};
	char ctl[CMSG_SPACE(sizeof(pkt_ts))];
	struct timespec now_ts;
	struct msghdr msg = {};
	__u64 now_ns, pkt_ns;
	struct cmsghdr *cmsg;
	struct iovec iov;
	char data[32];
	int ret;

	iov.iov_base = data;
	iov.iov_len = sizeof(data);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctl;
	msg.msg_controllen = sizeof(ctl);

	ret = recvmsg(fd, &msg, 0);
	if (!ASSERT_EQ(ret, s, "recvmsg"))
		return;
	ASSERT_STRNEQ(data, expected, s, "expected rcv data");

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg && cmsg->cmsg_level == SOL_SOCKET &&
	    cmsg->cmsg_type == SO_TIMESTAMPNS_NEW)
		memcpy(&pkt_ts, CMSG_DATA(cmsg), sizeof(pkt_ts));

	pkt_ns = pkt_ts.tv_sec * NSEC_PER_SEC + pkt_ts.tv_nsec;
	ASSERT_NEQ(pkt_ns, 0, "pkt rcv tstamp");

	ret = clock_gettime(CLOCK_REALTIME, &now_ts);
	ASSERT_OK(ret, "clock_gettime");
	now_ns = now_ts.tv_sec * NSEC_PER_SEC + now_ts.tv_nsec;

	if (ASSERT_GE(now_ns, pkt_ns, "check rcv tstamp"))
		ASSERT_LT(now_ns - pkt_ns, 5 * NSEC_PER_SEC,
			  "check rcv tstamp");
}

static void snd_tstamp(int fd, char *b, size_t s)
{
	struct sock_txtime opt = { .clockid = CLOCK_TAI };
	char ctl[CMSG_SPACE(sizeof(__u64))];
	struct timespec now_ts;
	struct msghdr msg = {};
	struct cmsghdr *cmsg;
	struct iovec iov;
	__u64 now_ns;
	int ret;

	ret = clock_gettime(CLOCK_TAI, &now_ts);
	ASSERT_OK(ret, "clock_get_time(CLOCK_TAI)");
	now_ns = now_ts.tv_sec * NSEC_PER_SEC + now_ts.tv_nsec;

	iov.iov_base = b;
	iov.iov_len = s;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctl;
	msg.msg_controllen = sizeof(ctl);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_TXTIME;
	cmsg->cmsg_len = CMSG_LEN(sizeof(now_ns));
	*(__u64 *)CMSG_DATA(cmsg) = now_ns;

	ret = setsockopt(fd, SOL_SOCKET, SO_TXTIME, &opt, sizeof(opt));
	ASSERT_OK(ret, "setsockopt(SO_TXTIME)");

	ret = sendmsg(fd, &msg, 0);
	ASSERT_EQ(ret, s, "sendmsg");
}

static void test_inet_dtime(int family, int type, const char *addr, __u16 port)
{
	int opt = 1, accept_fd = -1, client_fd = -1, listen_fd, err;
	char buf[] = "testing testing";
	struct nstoken *nstoken;

	nstoken = open_netns(NS_DST);
	if (!ASSERT_OK_PTR(nstoken, "setns dst"))
		return;
	listen_fd = start_server(family, type, addr, port, 0);
	close_netns(nstoken);

	if (!ASSERT_GE(listen_fd, 0, "listen"))
		return;

	/* Ensure the kernel puts the (rcv) timestamp for all skb */
	err = setsockopt(listen_fd, SOL_SOCKET, SO_TIMESTAMPNS_NEW,
			 &opt, sizeof(opt));
	if (!ASSERT_OK(err, "setsockopt(SO_TIMESTAMPNS_NEW)"))
		goto done;

	if (type == SOCK_STREAM) {
		/* Ensure the kernel set EDT when sending out rst/ack
		 * from the kernel's ctl_sk.
		 */
		err = setsockopt(listen_fd, SOL_TCP, TCP_TX_DELAY, &opt,
				 sizeof(opt));
		if (!ASSERT_OK(err, "setsockopt(TCP_TX_DELAY)"))
			goto done;
	}

	nstoken = open_netns(NS_SRC);
	if (!ASSERT_OK_PTR(nstoken, "setns src"))
		goto done;
	client_fd = connect_to_fd(listen_fd, TIMEOUT_MILLIS);
	close_netns(nstoken);

	if (!ASSERT_GE(client_fd, 0, "connect_to_fd"))
		goto done;

	if (type == SOCK_STREAM) {
		int n;

		accept_fd = accept(listen_fd, NULL, NULL);
		if (!ASSERT_GE(accept_fd, 0, "accept"))
			goto done;

		n = write(client_fd, buf, sizeof(buf));
		if (!ASSERT_EQ(n, sizeof(buf), "send to server"))
			goto done;
		rcv_tstamp(accept_fd, buf, sizeof(buf));
	} else {
		snd_tstamp(client_fd, buf, sizeof(buf));
		rcv_tstamp(listen_fd, buf, sizeof(buf));
	}

done:
	close(listen_fd);
	if (accept_fd != -1)
		close(accept_fd);
	if (client_fd != -1)
		close(client_fd);
}

static int netns_load_dtime_bpf(struct test_tc_dtime *skel,
				const struct netns_setup_result *setup_result)
{
	LIBBPF_OPTS(bpf_tc_hook, qdisc_src_fwd);
	LIBBPF_OPTS(bpf_tc_hook, qdisc_dst_fwd);
	LIBBPF_OPTS(bpf_tc_hook, qdisc_src);
	LIBBPF_OPTS(bpf_tc_hook, qdisc_dst);
	struct nstoken *nstoken;
	int err;

	/* setup ns_src tc progs */
	nstoken = open_netns(NS_SRC);
	if (!ASSERT_OK_PTR(nstoken, "setns " NS_SRC))
		return -1;
	/* tc qdisc add dev src clsact */
	QDISC_CLSACT_CREATE(&qdisc_src, setup_result->ifindex_src);
	/* tc filter add dev src ingress bpf da ingress_host */
	XGRESS_FILTER_ADD(&qdisc_src, BPF_TC_INGRESS, skel->progs.ingress_host, 0);
	/* tc filter add dev src egress bpf da egress_host */
	XGRESS_FILTER_ADD(&qdisc_src, BPF_TC_EGRESS, skel->progs.egress_host, 0);
	close_netns(nstoken);

	/* setup ns_dst tc progs */
	nstoken = open_netns(NS_DST);
	if (!ASSERT_OK_PTR(nstoken, "setns " NS_DST))
		return -1;
	/* tc qdisc add dev dst clsact */
	QDISC_CLSACT_CREATE(&qdisc_dst, setup_result->ifindex_dst);
	/* tc filter add dev dst ingress bpf da ingress_host */
	XGRESS_FILTER_ADD(&qdisc_dst, BPF_TC_INGRESS, skel->progs.ingress_host, 0);
	/* tc filter add dev dst egress bpf da egress_host */
	XGRESS_FILTER_ADD(&qdisc_dst, BPF_TC_EGRESS, skel->progs.egress_host, 0);
	close_netns(nstoken);

	/* setup ns_fwd tc progs */
	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns " NS_FWD))
		return -1;
	/* tc qdisc add dev dst_fwd clsact */
	QDISC_CLSACT_CREATE(&qdisc_dst_fwd, setup_result->ifindex_dst_fwd);
	/* tc filter add dev dst_fwd ingress prio 100 bpf da ingress_fwdns_prio100 */
	XGRESS_FILTER_ADD(&qdisc_dst_fwd, BPF_TC_INGRESS,
			  skel->progs.ingress_fwdns_prio100, 100);
	/* tc filter add dev dst_fwd ingress prio 101 bpf da ingress_fwdns_prio101 */
	XGRESS_FILTER_ADD(&qdisc_dst_fwd, BPF_TC_INGRESS,
			  skel->progs.ingress_fwdns_prio101, 101);
	/* tc filter add dev dst_fwd egress prio 100 bpf da egress_fwdns_prio100 */
	XGRESS_FILTER_ADD(&qdisc_dst_fwd, BPF_TC_EGRESS,
			  skel->progs.egress_fwdns_prio100, 100);
	/* tc filter add dev dst_fwd egress prio 101 bpf da egress_fwdns_prio101 */
	XGRESS_FILTER_ADD(&qdisc_dst_fwd, BPF_TC_EGRESS,
			  skel->progs.egress_fwdns_prio101, 101);

	/* tc qdisc add dev src_fwd clsact */
	QDISC_CLSACT_CREATE(&qdisc_src_fwd, setup_result->ifindex_src_fwd);
	/* tc filter add dev src_fwd ingress prio 100 bpf da ingress_fwdns_prio100 */
	XGRESS_FILTER_ADD(&qdisc_src_fwd, BPF_TC_INGRESS,
			  skel->progs.ingress_fwdns_prio100, 100);
	/* tc filter add dev src_fwd ingress prio 101 bpf da ingress_fwdns_prio101 */
	XGRESS_FILTER_ADD(&qdisc_src_fwd, BPF_TC_INGRESS,
			  skel->progs.ingress_fwdns_prio101, 101);
	/* tc filter add dev src_fwd egress prio 100 bpf da egress_fwdns_prio100 */
	XGRESS_FILTER_ADD(&qdisc_src_fwd, BPF_TC_EGRESS,
			  skel->progs.egress_fwdns_prio100, 100);
	/* tc filter add dev src_fwd egress prio 101 bpf da egress_fwdns_prio101 */
	XGRESS_FILTER_ADD(&qdisc_src_fwd, BPF_TC_EGRESS,
			  skel->progs.egress_fwdns_prio101, 101);
	close_netns(nstoken);
	return 0;

fail:
	close_netns(nstoken);
	return err;
}

enum {
	INGRESS_FWDNS_P100,
	INGRESS_FWDNS_P101,
	EGRESS_FWDNS_P100,
	EGRESS_FWDNS_P101,
	INGRESS_ENDHOST,
	EGRESS_ENDHOST,
	SET_DTIME,
	__MAX_CNT,
};

const char *cnt_names[] = {
	"ingress_fwdns_p100",
	"ingress_fwdns_p101",
	"egress_fwdns_p100",
	"egress_fwdns_p101",
	"ingress_endhost",
	"egress_endhost",
	"set_dtime",
};

enum {
	TCP_IP6_CLEAR_DTIME,
	TCP_IP4,
	TCP_IP6,
	UDP_IP4,
	UDP_IP6,
	TCP_IP4_RT_FWD,
	TCP_IP6_RT_FWD,
	UDP_IP4_RT_FWD,
	UDP_IP6_RT_FWD,
	UKN_TEST,
	__NR_TESTS,
};

const char *test_names[] = {
	"tcp ip6 clear dtime",
	"tcp ip4",
	"tcp ip6",
	"udp ip4",
	"udp ip6",
	"tcp ip4 rt fwd",
	"tcp ip6 rt fwd",
	"udp ip4 rt fwd",
	"udp ip6 rt fwd",
};

static const char *dtime_cnt_str(int test, int cnt)
{
	static char name[64];

	snprintf(name, sizeof(name), "%s %s", test_names[test], cnt_names[cnt]);

	return name;
}

static const char *dtime_err_str(int test, int cnt)
{
	static char name[64];

	snprintf(name, sizeof(name), "%s %s errs", test_names[test],
		 cnt_names[cnt]);

	return name;
}

static void test_tcp_clear_dtime(struct test_tc_dtime *skel)
{
	int i, t = TCP_IP6_CLEAR_DTIME;
	__u32 *dtimes = skel->bss->dtimes[t];
	__u32 *errs = skel->bss->errs[t];

	skel->bss->test = t;
	test_inet_dtime(AF_INET6, SOCK_STREAM, IP6_DST, 50000 + t);

	ASSERT_EQ(dtimes[INGRESS_FWDNS_P100], 0,
		  dtime_cnt_str(t, INGRESS_FWDNS_P100));
	ASSERT_EQ(dtimes[INGRESS_FWDNS_P101], 0,
		  dtime_cnt_str(t, INGRESS_FWDNS_P101));
	ASSERT_GT(dtimes[EGRESS_FWDNS_P100], 0,
		  dtime_cnt_str(t, EGRESS_FWDNS_P100));
	ASSERT_EQ(dtimes[EGRESS_FWDNS_P101], 0,
		  dtime_cnt_str(t, EGRESS_FWDNS_P101));
	ASSERT_GT(dtimes[EGRESS_ENDHOST], 0,
		  dtime_cnt_str(t, EGRESS_ENDHOST));
	ASSERT_GT(dtimes[INGRESS_ENDHOST], 0,
		  dtime_cnt_str(t, INGRESS_ENDHOST));

	for (i = INGRESS_FWDNS_P100; i < __MAX_CNT; i++)
		ASSERT_EQ(errs[i], 0, dtime_err_str(t, i));
}

static void test_tcp_dtime(struct test_tc_dtime *skel, int family, bool bpf_fwd)
{
	__u32 *dtimes, *errs;
	const char *addr;
	int i, t;

	if (family == AF_INET) {
		t = bpf_fwd ? TCP_IP4 : TCP_IP4_RT_FWD;
		addr = IP4_DST;
	} else {
		t = bpf_fwd ? TCP_IP6 : TCP_IP6_RT_FWD;
		addr = IP6_DST;
	}

	dtimes = skel->bss->dtimes[t];
	errs = skel->bss->errs[t];

	skel->bss->test = t;
	test_inet_dtime(family, SOCK_STREAM, addr, 50000 + t);

	/* fwdns_prio100 prog does not read delivery_time_type, so
	 * kernel puts the (rcv) timetamp in __sk_buff->tstamp
	 */
	ASSERT_EQ(dtimes[INGRESS_FWDNS_P100], 0,
		  dtime_cnt_str(t, INGRESS_FWDNS_P100));
	for (i = INGRESS_FWDNS_P101; i < SET_DTIME; i++)
		ASSERT_GT(dtimes[i], 0, dtime_cnt_str(t, i));

	for (i = INGRESS_FWDNS_P100; i < __MAX_CNT; i++)
		ASSERT_EQ(errs[i], 0, dtime_err_str(t, i));
}

static void test_udp_dtime(struct test_tc_dtime *skel, int family, bool bpf_fwd)
{
	__u32 *dtimes, *errs;
	const char *addr;
	int i, t;

	if (family == AF_INET) {
		t = bpf_fwd ? UDP_IP4 : UDP_IP4_RT_FWD;
		addr = IP4_DST;
	} else {
		t = bpf_fwd ? UDP_IP6 : UDP_IP6_RT_FWD;
		addr = IP6_DST;
	}

	dtimes = skel->bss->dtimes[t];
	errs = skel->bss->errs[t];

	skel->bss->test = t;
	test_inet_dtime(family, SOCK_DGRAM, addr, 50000 + t);

	ASSERT_EQ(dtimes[INGRESS_FWDNS_P100], 0,
		  dtime_cnt_str(t, INGRESS_FWDNS_P100));
	/* non mono delivery time is not forwarded */
	ASSERT_EQ(dtimes[INGRESS_FWDNS_P101], 0,
		  dtime_cnt_str(t, INGRESS_FWDNS_P101));
	for (i = EGRESS_FWDNS_P100; i < SET_DTIME; i++)
		ASSERT_GT(dtimes[i], 0, dtime_cnt_str(t, i));

	for (i = INGRESS_FWDNS_P100; i < __MAX_CNT; i++)
		ASSERT_EQ(errs[i], 0, dtime_err_str(t, i));
}

static void test_tc_redirect_dtime(struct netns_setup_result *setup_result)
{
	struct test_tc_dtime *skel;
	struct nstoken *nstoken;
	int err;

	skel = test_tc_dtime__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_dtime__open"))
		return;

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_dst_fwd;

	err = test_tc_dtime__load(skel);
	if (!ASSERT_OK(err, "test_tc_dtime__load"))
		goto done;

	if (netns_load_dtime_bpf(skel, setup_result))
		goto done;

	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns fwd"))
		goto done;
	err = set_forwarding(false);
	close_netns(nstoken);
	if (!ASSERT_OK(err, "disable forwarding"))
		goto done;

	test_tcp_clear_dtime(skel);

	test_tcp_dtime(skel, AF_INET, true);
	test_tcp_dtime(skel, AF_INET6, true);
	test_udp_dtime(skel, AF_INET, true);
	test_udp_dtime(skel, AF_INET6, true);

	/* Test the kernel ip[6]_forward path instead
	 * of bpf_redirect_neigh().
	 */
	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns fwd"))
		goto done;
	err = set_forwarding(true);
	close_netns(nstoken);
	if (!ASSERT_OK(err, "enable forwarding"))
		goto done;

	test_tcp_dtime(skel, AF_INET, false);
	test_tcp_dtime(skel, AF_INET6, false);
	test_udp_dtime(skel, AF_INET, false);
	test_udp_dtime(skel, AF_INET6, false);

done:
	test_tc_dtime__destroy(skel);
}

static void test_tc_redirect_neigh_fib(struct netns_setup_result *setup_result)
{
	struct nstoken *nstoken = NULL;
	struct test_tc_neigh_fib *skel = NULL;

	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns fwd"))
		return;

	skel = test_tc_neigh_fib__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_neigh_fib__open"))
		goto done;

	if (!ASSERT_OK(test_tc_neigh_fib__load(skel), "test_tc_neigh_fib__load"))
		goto done;

	if (netns_load_bpf(skel->progs.tc_src, skel->progs.tc_dst,
			   skel->progs.tc_chk, setup_result))
		goto done;

	/* bpf_fib_lookup() checks if forwarding is enabled */
	if (!ASSERT_OK(set_forwarding(true), "enable forwarding"))
		goto done;

	test_connectivity();

done:
	if (skel)
		test_tc_neigh_fib__destroy(skel);
	close_netns(nstoken);
}

static void test_tc_redirect_neigh(struct netns_setup_result *setup_result)
{
	struct nstoken *nstoken = NULL;
	struct test_tc_neigh *skel = NULL;
	int err;

	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns fwd"))
		return;

	skel = test_tc_neigh__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_neigh__open"))
		goto done;

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_dst_fwd;

	err = test_tc_neigh__load(skel);
	if (!ASSERT_OK(err, "test_tc_neigh__load"))
		goto done;

	if (netns_load_bpf(skel->progs.tc_src, skel->progs.tc_dst,
			   skel->progs.tc_chk, setup_result))
		goto done;

	if (!ASSERT_OK(set_forwarding(false), "disable forwarding"))
		goto done;

	test_connectivity();

done:
	if (skel)
		test_tc_neigh__destroy(skel);
	close_netns(nstoken);
}

static void test_tc_redirect_peer(struct netns_setup_result *setup_result)
{
	struct nstoken *nstoken;
	struct test_tc_peer *skel;
	int err;

	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns fwd"))
		return;

	skel = test_tc_peer__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_peer__open"))
		goto done;

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_dst_fwd;

	err = test_tc_peer__load(skel);
	if (!ASSERT_OK(err, "test_tc_peer__load"))
		goto done;

	if (netns_load_bpf(skel->progs.tc_src, skel->progs.tc_dst,
			   skel->progs.tc_chk, setup_result))
		goto done;

	if (!ASSERT_OK(set_forwarding(false), "disable forwarding"))
		goto done;

	test_connectivity();

done:
	if (skel)
		test_tc_peer__destroy(skel);
	close_netns(nstoken);
}

static int tun_open(char *name)
{
	struct ifreq ifr;
	int fd, err;

	fd = open("/dev/net/tun", O_RDWR);
	if (!ASSERT_GE(fd, 0, "open /dev/net/tun"))
		return -1;

	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	if (*name)
		strncpy(ifr.ifr_name, name, IFNAMSIZ);

	err = ioctl(fd, TUNSETIFF, &ifr);
	if (!ASSERT_OK(err, "ioctl TUNSETIFF"))
		goto fail;

	SYS(fail, "ip link set dev %s up", name);

	return fd;
fail:
	close(fd);
	return -1;
}

enum {
	SRC_TO_TARGET = 0,
	TARGET_TO_SRC = 1,
};

static int tun_relay_loop(int src_fd, int target_fd)
{
	fd_set rfds, wfds;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	for (;;) {
		char buf[1500];
		int direction, nread, nwrite;

		FD_SET(src_fd, &rfds);
		FD_SET(target_fd, &rfds);

		if (select(1 + MAX(src_fd, target_fd), &rfds, NULL, NULL, NULL) < 0) {
			log_err("select failed");
			return 1;
		}

		direction = FD_ISSET(src_fd, &rfds) ? SRC_TO_TARGET : TARGET_TO_SRC;

		nread = read(direction == SRC_TO_TARGET ? src_fd : target_fd, buf, sizeof(buf));
		if (nread < 0) {
			log_err("read failed");
			return 1;
		}

		nwrite = write(direction == SRC_TO_TARGET ? target_fd : src_fd, buf, nread);
		if (nwrite != nread) {
			log_err("write failed");
			return 1;
		}
	}
}

static void test_tc_redirect_peer_l3(struct netns_setup_result *setup_result)
{
	LIBBPF_OPTS(bpf_tc_hook, qdisc_tun_fwd);
	LIBBPF_OPTS(bpf_tc_hook, qdisc_dst_fwd);
	struct test_tc_peer *skel = NULL;
	struct nstoken *nstoken = NULL;
	int err;
	int tunnel_pid = -1;
	int src_fd, target_fd = -1;
	int ifindex;

	/* Start a L3 TUN/TAP tunnel between the src and dst namespaces.
	 * This test is using TUN/TAP instead of e.g. IPIP or GRE tunnel as those
	 * expose the L2 headers encapsulating the IP packet to BPF and hence
	 * don't have skb in suitable state for this test. Alternative to TUN/TAP
	 * would be e.g. Wireguard which would appear as a pure L3 device to BPF,
	 * but that requires much more complicated setup.
	 */
	nstoken = open_netns(NS_SRC);
	if (!ASSERT_OK_PTR(nstoken, "setns " NS_SRC))
		return;

	src_fd = tun_open("tun_src");
	if (!ASSERT_GE(src_fd, 0, "tun_open tun_src"))
		goto fail;

	close_netns(nstoken);

	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns " NS_FWD))
		goto fail;

	target_fd = tun_open("tun_fwd");
	if (!ASSERT_GE(target_fd, 0, "tun_open tun_fwd"))
		goto fail;

	tunnel_pid = fork();
	if (!ASSERT_GE(tunnel_pid, 0, "fork tun_relay_loop"))
		goto fail;

	if (tunnel_pid == 0)
		exit(tun_relay_loop(src_fd, target_fd));

	skel = test_tc_peer__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_peer__open"))
		goto fail;

	ifindex = if_nametoindex("tun_fwd");
	if (!ASSERT_GT(ifindex, 0, "if_indextoname tun_fwd"))
		goto fail;

	skel->rodata->IFINDEX_SRC = ifindex;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_dst_fwd;

	err = test_tc_peer__load(skel);
	if (!ASSERT_OK(err, "test_tc_peer__load"))
		goto fail;

	/* Load "tc_src_l3" to the tun_fwd interface to redirect packets
	 * towards dst, and "tc_dst" to redirect packets
	 * and "tc_chk" on dst_fwd to drop non-redirected packets.
	 */
	/* tc qdisc add dev tun_fwd clsact */
	QDISC_CLSACT_CREATE(&qdisc_tun_fwd, ifindex);
	/* tc filter add dev tun_fwd ingress bpf da tc_src_l3 */
	XGRESS_FILTER_ADD(&qdisc_tun_fwd, BPF_TC_INGRESS, skel->progs.tc_src_l3, 0);

	/* tc qdisc add dev dst_fwd clsact */
	QDISC_CLSACT_CREATE(&qdisc_dst_fwd, setup_result->ifindex_dst_fwd);
	/* tc filter add dev dst_fwd ingress bpf da tc_dst_l3 */
	XGRESS_FILTER_ADD(&qdisc_dst_fwd, BPF_TC_INGRESS, skel->progs.tc_dst_l3, 0);
	/* tc filter add dev dst_fwd egress bpf da tc_chk */
	XGRESS_FILTER_ADD(&qdisc_dst_fwd, BPF_TC_EGRESS, skel->progs.tc_chk, 0);

	/* Setup route and neigh tables */
	SYS(fail, "ip -netns " NS_SRC " addr add dev tun_src " IP4_TUN_SRC "/24");
	SYS(fail, "ip -netns " NS_FWD " addr add dev tun_fwd " IP4_TUN_FWD "/24");

	SYS(fail, "ip -netns " NS_SRC " addr add dev tun_src " IP6_TUN_SRC "/64 nodad");
	SYS(fail, "ip -netns " NS_FWD " addr add dev tun_fwd " IP6_TUN_FWD "/64 nodad");

	SYS(fail, "ip -netns " NS_SRC " route del " IP4_DST "/32 dev src scope global");
	SYS(fail, "ip -netns " NS_SRC " route add " IP4_DST "/32 via " IP4_TUN_FWD
	    " dev tun_src scope global");
	SYS(fail, "ip -netns " NS_DST " route add " IP4_TUN_SRC "/32 dev dst scope global");
	SYS(fail, "ip -netns " NS_SRC " route del " IP6_DST "/128 dev src scope global");
	SYS(fail, "ip -netns " NS_SRC " route add " IP6_DST "/128 via " IP6_TUN_FWD
	    " dev tun_src scope global");
	SYS(fail, "ip -netns " NS_DST " route add " IP6_TUN_SRC "/128 dev dst scope global");

	SYS(fail, "ip -netns " NS_DST " neigh add " IP4_TUN_SRC " dev dst lladdr " MAC_DST_FWD);
	SYS(fail, "ip -netns " NS_DST " neigh add " IP6_TUN_SRC " dev dst lladdr " MAC_DST_FWD);

	if (!ASSERT_OK(set_forwarding(false), "disable forwarding"))
		goto fail;

	test_connectivity();

fail:
	if (tunnel_pid > 0) {
		kill(tunnel_pid, SIGTERM);
		waitpid(tunnel_pid, NULL, 0);
	}
	if (src_fd >= 0)
		close(src_fd);
	if (target_fd >= 0)
		close(target_fd);
	if (skel)
		test_tc_peer__destroy(skel);
	if (nstoken)
		close_netns(nstoken);
}

#define RUN_TEST(name, mode)                                                                \
	({                                                                                  \
		struct netns_setup_result setup_result = { .dev_mode = mode, };             \
		if (test__start_subtest(#name))                                             \
			if (ASSERT_OK(netns_setup_namespaces("add"), "setup namespaces")) { \
				if (ASSERT_OK(netns_setup_links_and_routes(&setup_result),  \
					      "setup links and routes"))                    \
					test_ ## name(&setup_result);                       \
				netns_setup_namespaces("delete");                           \
			}                                                                   \
	})

static void *test_tc_redirect_run_tests(void *arg)
{
	netns_setup_namespaces_nofail("delete");

	RUN_TEST(tc_redirect_peer, MODE_VETH);
	RUN_TEST(tc_redirect_peer, MODE_NETKIT);
	RUN_TEST(tc_redirect_peer_l3, MODE_VETH);
	RUN_TEST(tc_redirect_peer_l3, MODE_NETKIT);
	RUN_TEST(tc_redirect_neigh, MODE_VETH);
	RUN_TEST(tc_redirect_neigh_fib, MODE_VETH);
	RUN_TEST(tc_redirect_dtime, MODE_VETH);
	return NULL;
}

void test_tc_redirect(void)
{
	pthread_t test_thread;
	int err;

	/* Run the tests in their own thread to isolate the namespace changes
	 * so they do not affect the environment of other tests.
	 * (specifically needed because of unshare(CLONE_NEWNS) in open_netns())
	 */
	err = pthread_create(&test_thread, NULL, &test_tc_redirect_run_tests, NULL);
	if (ASSERT_OK(err, "pthread_create"))
		ASSERT_OK(pthread_join(test_thread, NULL), "pthread_join");
}
