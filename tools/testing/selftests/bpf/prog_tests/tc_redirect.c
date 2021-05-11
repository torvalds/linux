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
#include <fcntl.h>
#include <linux/limits.h>
#include <linux/sysctl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

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
#define IP4_PORT 9004

#define IP6_SRC "::1:dead:beef:cafe"
#define IP6_DST "::2:dead:beef:cafe"
#define IP6_PORT 9006

#define IP4_SLL "169.254.0.1"
#define IP4_DLL "169.254.0.2"
#define IP4_NET "169.254.0.0"

#define IFADDR_STR_LEN 18
#define PING_ARGS "-c 3 -w 10 -q"

#define SRC_PROG_PIN_FILE "/sys/fs/bpf/test_tc_src"
#define DST_PROG_PIN_FILE "/sys/fs/bpf/test_tc_dst"
#define CHK_PROG_PIN_FILE "/sys/fs/bpf/test_tc_chk"

#define TIMEOUT_MILLIS 10000

#define MAX_PROC_MODS 128
#define MAX_PROC_VALUE_LEN 16

#define log_err(MSG, ...) \
	fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
		__FILE__, __LINE__, strerror(errno), ##__VA_ARGS__)

struct proc_mod {
	char path[PATH_MAX];
	char oldval[MAX_PROC_VALUE_LEN];
	int oldlen;
};

static const char * const namespaces[] = {NS_SRC, NS_FWD, NS_DST, NULL};
static int root_netns_fd = -1;
static int num_proc_mods;
static struct proc_mod proc_mods[MAX_PROC_MODS];

/**
 * modify_proc() - Modify entry in /proc
 *
 * Modifies an entry in /proc and saves the original value for later
 * restoration with restore_proc().
 */
static int modify_proc(const char *path, const char *newval)
{
	struct proc_mod *mod;
	FILE *f;

	if (num_proc_mods + 1 > MAX_PROC_MODS)
		return -1;

	f = fopen(path, "r+");
	if (!f)
		return -1;

	mod = &proc_mods[num_proc_mods];
	num_proc_mods++;

	strncpy(mod->path, path, PATH_MAX);

	if (!fread(mod->oldval, 1, MAX_PROC_VALUE_LEN, f)) {
		log_err("reading from %s failed", path);
		goto fail;
	}
	rewind(f);
	if (fwrite(newval, strlen(newval), 1, f) != 1) {
		log_err("writing to %s failed", path);
		goto fail;
	}

	fclose(f);
	return 0;

fail:
	fclose(f);
	num_proc_mods--;
	return -1;
}

/**
 * restore_proc() - Restore all /proc modifications
 */
static void restore_proc(void)
{
	int i;

	for (i = 0; i < num_proc_mods; i++) {
		struct proc_mod *mod = &proc_mods[i];
		FILE *f;

		f = fopen(mod->path, "w");
		if (!f) {
			log_err("fopen of %s failed", mod->path);
			continue;
		}

		if (fwrite(mod->oldval, mod->oldlen, 1, f) != 1)
			log_err("fwrite to %s failed", mod->path);

		fclose(f);
	}
	num_proc_mods = 0;
}

/**
 * setns_by_name() - Set networks namespace by name
 */
static int setns_by_name(const char *name)
{
	int nsfd;
	char nspath[PATH_MAX];
	int err;

	snprintf(nspath, sizeof(nspath), "%s/%s", "/var/run/netns", name);
	nsfd = open(nspath, O_RDONLY | O_CLOEXEC);
	if (nsfd < 0)
		return nsfd;

	err = setns(nsfd, CLONE_NEWNET);
	close(nsfd);

	return err;
}

/**
 * setns_root() - Set network namespace to original (root) namespace
 *
 * Not expected to ever fail, so error not returned, but failure logged
 * and test marked as failed.
 */
static void setns_root(void)
{
	ASSERT_OK(setns(root_netns_fd, CLONE_NEWNET), "setns root");
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
	char veth_src_fwd_addr[IFADDR_STR_LEN+1] = {};
	char veth_dst_fwd_addr[IFADDR_STR_LEN+1] = {};

	SYS("ip link add veth_src type veth peer name veth_src_fwd");
	SYS("ip link add veth_dst type veth peer name veth_dst_fwd");
	if (get_ifaddr("veth_src_fwd", veth_src_fwd_addr))
		goto fail;
	if (get_ifaddr("veth_dst_fwd", veth_dst_fwd_addr))
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
	if (!ASSERT_OK(setns_by_name(NS_SRC), "setns src"))
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

	/** setup in 'fwd' namespace */
	if (!ASSERT_OK(setns_by_name(NS_FWD), "setns fwd"))
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

	/** setup in 'dst' namespace */
	if (!ASSERT_OK(setns_by_name(NS_DST), "setns dst"))
		goto fail;

	SYS("ip addr add " IP4_DST "/32 dev veth_dst");
	SYS("ip addr add " IP6_DST "/128 dev veth_dst nodad");
	SYS("ip link set dev veth_dst up");

	SYS("ip route add " IP4_SRC "/32 dev veth_dst scope global");
	SYS("ip route add " IP4_NET "/16 dev veth_dst scope global");
	SYS("ip route add " IP6_SRC "/128 dev veth_dst scope global");

	SYS("ip neigh add " IP4_SRC " dev veth_dst lladdr %s",
	    veth_dst_fwd_addr);
	SYS("ip neigh add " IP6_SRC " dev veth_dst lladdr %s",
	    veth_dst_fwd_addr);

	setns_root();
	return 0;
fail:
	setns_root();
	return -1;
}

static int netns_load_bpf(void)
{
	if (!ASSERT_OK(setns_by_name(NS_FWD), "setns fwd"))
		return -1;

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

	setns_root();
	return -1;
fail:
	setns_root();
	return -1;
}

static int netns_unload_bpf(void)
{
	if (!ASSERT_OK(setns_by_name(NS_FWD), "setns fwd"))
		goto fail;
	SYS("tc qdisc delete dev veth_src_fwd clsact");
	SYS("tc qdisc delete dev veth_dst_fwd clsact");

	setns_root();
	return 0;
fail:
	setns_root();
	return -1;
}


static void test_tcp(int family, const char *addr, __u16 port)
{
	int listen_fd = -1, accept_fd = -1, client_fd = -1;
	char buf[] = "testing testing";
	int n;

	if (!ASSERT_OK(setns_by_name(NS_DST), "setns dst"))
		return;

	listen_fd = start_server(family, SOCK_STREAM, addr, port, 0);
	if (!ASSERT_GE(listen_fd, 0, "listen"))
		goto done;

	if (!ASSERT_OK(setns_by_name(NS_SRC), "setns src"))
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
	setns_root();
	if (listen_fd >= 0)
		close(listen_fd);
	if (accept_fd >= 0)
		close(accept_fd);
	if (client_fd >= 0)
		close(client_fd);
}

static int test_ping(int family, const char *addr)
{
	const char *ping = family == AF_INET6 ? "ping6" : "ping";

	SYS("ip netns exec " NS_SRC " %s " PING_ARGS " %s", ping, addr);
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

static void test_tc_redirect_neigh_fib(struct netns_setup_result *setup_result)
{
	struct test_tc_neigh_fib *skel;
	int err;

	skel = test_tc_neigh_fib__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_neigh_fib__open"))
		return;

	if (!ASSERT_OK(test_tc_neigh_fib__load(skel), "test_tc_neigh_fib__load")) {
		test_tc_neigh_fib__destroy(skel);
		return;
	}

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
	if (!ASSERT_OK(setns_by_name(NS_FWD), "setns fwd"))
		goto done;

	err = modify_proc("/proc/sys/net/ipv4/ip_forward", "1");
	if (!ASSERT_OK(err, "set ipv4.ip_forward"))
		goto done;

	err = modify_proc("/proc/sys/net/ipv6/conf/all/forwarding", "1");
	if (!ASSERT_OK(err, "set ipv6.forwarding"))
		goto done;
	setns_root();

	test_connectivity();
done:
	bpf_program__unpin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	test_tc_neigh_fib__destroy(skel);
	netns_unload_bpf();
	setns_root();
	restore_proc();
}

static void test_tc_redirect_neigh(struct netns_setup_result *setup_result)
{
	struct test_tc_neigh *skel;
	int err;

	skel = test_tc_neigh__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_neigh__open"))
		return;

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_veth_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_veth_dst_fwd;

	err = test_tc_neigh__load(skel);
	if (!ASSERT_OK(err, "test_tc_neigh__load")) {
		test_tc_neigh__destroy(skel);
		return;
	}

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

	test_connectivity();

done:
	bpf_program__unpin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	test_tc_neigh__destroy(skel);
	netns_unload_bpf();
	setns_root();
}

static void test_tc_redirect_peer(struct netns_setup_result *setup_result)
{
	struct test_tc_peer *skel;
	int err;

	skel = test_tc_peer__open();
	if (!ASSERT_OK_PTR(skel, "test_tc_peer__open"))
		return;

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_veth_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_veth_dst_fwd;

	err = test_tc_peer__load(skel);
	if (!ASSERT_OK(err, "test_tc_peer__load")) {
		test_tc_peer__destroy(skel);
		return;
	}

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

	test_connectivity();

done:
	bpf_program__unpin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	test_tc_peer__destroy(skel);
	netns_unload_bpf();
	setns_root();
}

void test_tc_redirect(void)
{
	struct netns_setup_result setup_result;

	root_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	if (!ASSERT_GE(root_netns_fd, 0, "open /proc/self/ns/net"))
		return;

	if (netns_setup_namespaces("add"))
		goto done;

	if (netns_setup_links_and_routes(&setup_result))
		goto done;

	if (test__start_subtest("tc_redirect_peer"))
		test_tc_redirect_peer(&setup_result);

	if (test__start_subtest("tc_redirect_neigh"))
		test_tc_redirect_neigh(&setup_result);

	if (test__start_subtest("tc_redirect_neigh_fib"))
		test_tc_redirect_neigh_fib(&setup_result);

done:
	close(root_netns_fd);
	netns_setup_namespaces("delete");
}
