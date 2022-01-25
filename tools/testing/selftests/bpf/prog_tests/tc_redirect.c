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

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/limits.h>
#include <linux/sysctl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "test_tc_neigh_fib.skel.h"
#include "test_tc_neigh.skel.h"
#include "test_tc_peer.skel.h"

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

#define SRC_PROG_PIN_FILE "/sys/fs/bpf/test_tc_src"
#define DST_PROG_PIN_FILE "/sys/fs/bpf/test_tc_dst"
#define CHK_PROG_PIN_FILE "/sys/fs/bpf/test_tc_chk"

#define TIMEOUT_MILLIS 10000

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

struct nstoken {
	int orig_netns_fd;
};

static int setns_by_fd(int nsfd)
{
	int err;

	err = setns(nsfd, CLONE_NEWNET);
	close(nsfd);

	if (!ASSERT_OK(err, "setns"))
		return err;

	/* Switch /sys to the new namespace so that e.g. /sys/class/net
	 * reflects the devices in the new namespace.
	 */
	err = unshare(CLONE_NEWNS);
	if (!ASSERT_OK(err, "unshare"))
		return err;

	/* Make our /sys mount private, so the following umount won't
	 * trigger the global umount in case it's shared.
	 */
	err = mount("none", "/sys", NULL, MS_PRIVATE, NULL);
	if (!ASSERT_OK(err, "remount private /sys"))
		return err;

	err = umount2("/sys", MNT_DETACH);
	if (!ASSERT_OK(err, "umount2 /sys"))
		return err;

	err = mount("sysfs", "/sys", "sysfs", 0, NULL);
	if (!ASSERT_OK(err, "mount /sys"))
		return err;

	err = mount("bpffs", "/sys/fs/bpf", "bpf", 0, NULL);
	if (!ASSERT_OK(err, "mount /sys/fs/bpf"))
		return err;

	return 0;
}

/**
 * open_netns() - Switch to specified network namespace by name.
 *
 * Returns token with which to restore the original namespace
 * using close_netns().
 */
static struct nstoken *open_netns(const char *name)
{
	int nsfd;
	char nspath[PATH_MAX];
	int err;
	struct nstoken *token;

	token = malloc(sizeof(struct nstoken));
	if (!ASSERT_OK_PTR(token, "malloc token"))
		return NULL;

	token->orig_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	if (!ASSERT_GE(token->orig_netns_fd, 0, "open /proc/self/ns/net"))
		goto fail;

	snprintf(nspath, sizeof(nspath), "%s/%s", "/var/run/netns", name);
	nsfd = open(nspath, O_RDONLY | O_CLOEXEC);
	if (!ASSERT_GE(nsfd, 0, "open netns fd"))
		goto fail;

	err = setns_by_fd(nsfd);
	if (!ASSERT_OK(err, "setns_by_fd"))
		goto fail;

	return token;
fail:
	free(token);
	return NULL;
}

static void close_netns(struct nstoken *token)
{
	ASSERT_OK(setns_by_fd(token->orig_netns_fd), "setns_by_fd");
	free(token);
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

struct netns_setup_result {
	int ifindex_veth_src_fwd;
	int ifindex_veth_dst_fwd;
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

static int get_ifindex(const char *name)
{
	char path[PATH_MAX];
	char buf[32];
	FILE *f;
	int ret;

	snprintf(path, PATH_MAX, "/sys/class/net/%s/ifindex", name);
	f = fopen(path, "r");
	if (!ASSERT_OK_PTR(f, path))
		return -1;

	ret = fread(buf, 1, sizeof(buf), f);
	if (!ASSERT_GT(ret, 0, "fread ifindex")) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return atoi(buf);
}

#define SYS(fmt, ...)						\
	({							\
		char cmd[1024];					\
		snprintf(cmd, sizeof(cmd), fmt, ##__VA_ARGS__);	\
		if (!ASSERT_OK(system(cmd), cmd))		\
			goto fail;				\
	})

static int netns_setup_links_and_routes(struct netns_setup_result *result)
{
	struct nstoken *nstoken = NULL;
	char veth_src_fwd_addr[IFADDR_STR_LEN+1] = {};

	SYS("ip link add veth_src type veth peer name veth_src_fwd");
	SYS("ip link add veth_dst type veth peer name veth_dst_fwd");

	SYS("ip link set veth_dst_fwd address " MAC_DST_FWD);
	SYS("ip link set veth_dst address " MAC_DST);

	if (get_ifaddr("veth_src_fwd", veth_src_fwd_addr))
		goto fail;

	result->ifindex_veth_src_fwd = get_ifindex("veth_src_fwd");
	if (result->ifindex_veth_src_fwd < 0)
		goto fail;
	result->ifindex_veth_dst_fwd = get_ifindex("veth_dst_fwd");
	if (result->ifindex_veth_dst_fwd < 0)
		goto fail;

	SYS("ip link set veth_src netns " NS_SRC);
	SYS("ip link set veth_src_fwd netns " NS_FWD);
	SYS("ip link set veth_dst_fwd netns " NS_FWD);
	SYS("ip link set veth_dst netns " NS_DST);

	/** setup in 'src' namespace */
	nstoken = open_netns(NS_SRC);
	if (!ASSERT_OK_PTR(nstoken, "setns src"))
		goto fail;

	SYS("ip addr add " IP4_SRC "/32 dev veth_src");
	SYS("ip addr add " IP6_SRC "/128 dev veth_src nodad");
	SYS("ip link set dev veth_src up");

	SYS("ip route add " IP4_DST "/32 dev veth_src scope global");
	SYS("ip route add " IP4_NET "/16 dev veth_src scope global");
	SYS("ip route add " IP6_DST "/128 dev veth_src scope global");

	SYS("ip neigh add " IP4_DST " dev veth_src lladdr %s",
	    veth_src_fwd_addr);
	SYS("ip neigh add " IP6_DST " dev veth_src lladdr %s",
	    veth_src_fwd_addr);

	close_netns(nstoken);

	/** setup in 'fwd' namespace */
	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns fwd"))
		goto fail;

	/* The fwd netns automatically gets a v6 LL address / routes, but also
	 * needs v4 one in order to start ARP probing. IP4_NET route is added
	 * to the endpoints so that the ARP processing will reply.
	 */
	SYS("ip addr add " IP4_SLL "/32 dev veth_src_fwd");
	SYS("ip addr add " IP4_DLL "/32 dev veth_dst_fwd");
	SYS("ip link set dev veth_src_fwd up");
	SYS("ip link set dev veth_dst_fwd up");

	SYS("ip route add " IP4_SRC "/32 dev veth_src_fwd scope global");
	SYS("ip route add " IP6_SRC "/128 dev veth_src_fwd scope global");
	SYS("ip route add " IP4_DST "/32 dev veth_dst_fwd scope global");
	SYS("ip route add " IP6_DST "/128 dev veth_dst_fwd scope global");

	close_netns(nstoken);

	/** setup in 'dst' namespace */
	nstoken = open_netns(NS_DST);
	if (!ASSERT_OK_PTR(nstoken, "setns dst"))
		goto fail;

	SYS("ip addr add " IP4_DST "/32 dev veth_dst");
	SYS("ip addr add " IP6_DST "/128 dev veth_dst nodad");
	SYS("ip link set dev veth_dst up");

	SYS("ip route add " IP4_SRC "/32 dev veth_dst scope global");
	SYS("ip route add " IP4_NET "/16 dev veth_dst scope global");
	SYS("ip route add " IP6_SRC "/128 dev veth_dst scope global");

	SYS("ip neigh add " IP4_SRC " dev veth_dst lladdr " MAC_DST_FWD);
	SYS("ip neigh add " IP6_SRC " dev veth_dst lladdr " MAC_DST_FWD);

	close_netns(nstoken);

	return 0;
fail:
	if (nstoken)
		close_netns(nstoken);
	return -1;
}

static int netns_load_bpf(void)
{
	SYS("tc qdisc add dev veth_src_fwd clsact");
	SYS("tc filter add dev veth_src_fwd ingress bpf da object-pinned "
	    SRC_PROG_PIN_FILE);
	SYS("tc filter add dev veth_src_fwd egress bpf da object-pinned "
	    CHK_PROG_PIN_FILE);

	SYS("tc qdisc add dev veth_dst_fwd clsact");
	SYS("tc filter add dev veth_dst_fwd ingress bpf da object-pinned "
	    DST_PROG_PIN_FILE);
	SYS("tc filter add dev veth_dst_fwd egress bpf da object-pinned "
	    CHK_PROG_PIN_FILE);

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
	SYS("ip netns exec " NS_SRC " %s " PING_ARGS " %s > /dev/null", ping_command(family), addr);
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

static void test_tc_redirect_neigh_fib(struct netns_setup_result *setup_result)
{
	struct nstoken *nstoken = NULL;
	struct test_tc_neigh_fib *skel = NULL;
	int err;

	nstoken = open_netns(NS_FWD);
	if (!ASSERT_OK_PTR(nstoken, "setns fwd"))
		return;

	skel = test_tc_neigh_fib__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_neigh_fib__open"))
		goto done;

	if (!ASSERT_OK(test_tc_neigh_fib__load(skel), "test_tc_neigh_fib__load"))
		goto done;

	err = bpf_program__pin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " SRC_PROG_PIN_FILE))
		goto done;

	err = bpf_program__pin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " CHK_PROG_PIN_FILE))
		goto done;

	err = bpf_program__pin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " DST_PROG_PIN_FILE))
		goto done;

	if (netns_load_bpf())
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

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_veth_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_veth_dst_fwd;

	err = test_tc_neigh__load(skel);
	if (!ASSERT_OK(err, "test_tc_neigh__load"))
		goto done;

	err = bpf_program__pin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " SRC_PROG_PIN_FILE))
		goto done;

	err = bpf_program__pin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " CHK_PROG_PIN_FILE))
		goto done;

	err = bpf_program__pin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " DST_PROG_PIN_FILE))
		goto done;

	if (netns_load_bpf())
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

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_veth_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_veth_dst_fwd;

	err = test_tc_peer__load(skel);
	if (!ASSERT_OK(err, "test_tc_peer__load"))
		goto done;

	err = bpf_program__pin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " SRC_PROG_PIN_FILE))
		goto done;

	err = bpf_program__pin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " CHK_PROG_PIN_FILE))
		goto done;

	err = bpf_program__pin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " DST_PROG_PIN_FILE))
		goto done;

	if (netns_load_bpf())
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

	SYS("ip link set dev %s up", name);

	return fd;
fail:
	close(fd);
	return -1;
}

#define MAX(a, b) ((a) > (b) ? (a) : (b))
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

	ifindex = get_ifindex("tun_fwd");
	if (!ASSERT_GE(ifindex, 0, "get_ifindex tun_fwd"))
		goto fail;

	skel->rodata->IFINDEX_SRC = ifindex;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_veth_dst_fwd;

	err = test_tc_peer__load(skel);
	if (!ASSERT_OK(err, "test_tc_peer__load"))
		goto fail;

	err = bpf_program__pin(skel->progs.tc_src_l3, SRC_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " SRC_PROG_PIN_FILE))
		goto fail;

	err = bpf_program__pin(skel->progs.tc_dst_l3, DST_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " DST_PROG_PIN_FILE))
		goto fail;

	err = bpf_program__pin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	if (!ASSERT_OK(err, "pin " CHK_PROG_PIN_FILE))
		goto fail;

	/* Load "tc_src_l3" to the tun_fwd interface to redirect packets
	 * towards dst, and "tc_dst" to redirect packets
	 * and "tc_chk" on veth_dst_fwd to drop non-redirected packets.
	 */
	SYS("tc qdisc add dev tun_fwd clsact");
	SYS("tc filter add dev tun_fwd ingress bpf da object-pinned "
	    SRC_PROG_PIN_FILE);

	SYS("tc qdisc add dev veth_dst_fwd clsact");
	SYS("tc filter add dev veth_dst_fwd ingress bpf da object-pinned "
	    DST_PROG_PIN_FILE);
	SYS("tc filter add dev veth_dst_fwd egress bpf da object-pinned "
	    CHK_PROG_PIN_FILE);

	/* Setup route and neigh tables */
	SYS("ip -netns " NS_SRC " addr add dev tun_src " IP4_TUN_SRC "/24");
	SYS("ip -netns " NS_FWD " addr add dev tun_fwd " IP4_TUN_FWD "/24");

	SYS("ip -netns " NS_SRC " addr add dev tun_src " IP6_TUN_SRC "/64 nodad");
	SYS("ip -netns " NS_FWD " addr add dev tun_fwd " IP6_TUN_FWD "/64 nodad");

	SYS("ip -netns " NS_SRC " route del " IP4_DST "/32 dev veth_src scope global");
	SYS("ip -netns " NS_SRC " route add " IP4_DST "/32 via " IP4_TUN_FWD
	    " dev tun_src scope global");
	SYS("ip -netns " NS_DST " route add " IP4_TUN_SRC "/32 dev veth_dst scope global");
	SYS("ip -netns " NS_SRC " route del " IP6_DST "/128 dev veth_src scope global");
	SYS("ip -netns " NS_SRC " route add " IP6_DST "/128 via " IP6_TUN_FWD
	    " dev tun_src scope global");
	SYS("ip -netns " NS_DST " route add " IP6_TUN_SRC "/128 dev veth_dst scope global");

	SYS("ip -netns " NS_DST " neigh add " IP4_TUN_SRC " dev veth_dst lladdr " MAC_DST_FWD);
	SYS("ip -netns " NS_DST " neigh add " IP6_TUN_SRC " dev veth_dst lladdr " MAC_DST_FWD);

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

#define RUN_TEST(name)                                                                      \
	({                                                                                  \
		struct netns_setup_result setup_result;                                     \
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

	RUN_TEST(tc_redirect_peer);
	RUN_TEST(tc_redirect_peer_l3);
	RUN_TEST(tc_redirect_neigh);
	RUN_TEST(tc_redirect_neigh_fib);
	return NULL;
}

void serial_test_tc_redirect(void)
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
