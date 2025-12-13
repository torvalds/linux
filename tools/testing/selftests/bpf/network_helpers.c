// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>

#include <arpa/inet.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/eventfd.h>

#include <linux/err.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/limits.h>

#include <linux/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <net/if.h>

#include "bpf_util.h"
#include "network_helpers.h"
#include "test_progs.h"

#ifdef TRAFFIC_MONITOR
/* Prevent pcap.h from including pcap/bpf.h and causing conflicts */
#define PCAP_DONT_INCLUDE_PCAP_BPF_H 1
#include <pcap/pcap.h>
#include <pcap/dlt.h>
#endif

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_err(MSG, ...) ({						\
			int __save = errno;				\
			fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
				__FILE__, __LINE__, clean_errno(),	\
				##__VA_ARGS__);				\
			errno = __save;					\
})

struct ipv4_packet pkt_v4 = {
	.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
	.iph.ihl = 5,
	.iph.protocol = IPPROTO_TCP,
	.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
	.tcp.urg_ptr = 123,
	.tcp.doff = 5,
};

struct ipv6_packet pkt_v6 = {
	.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
	.iph.nexthdr = IPPROTO_TCP,
	.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
	.tcp.urg_ptr = 123,
	.tcp.doff = 5,
};

static const struct network_helper_opts default_opts;

int settimeo(int fd, int timeout_ms)
{
	struct timeval timeout = { .tv_sec = 3 };

	if (timeout_ms > 0) {
		timeout.tv_sec = timeout_ms / 1000;
		timeout.tv_usec = (timeout_ms % 1000) * 1000;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
		       sizeof(timeout))) {
		log_err("Failed to set SO_RCVTIMEO");
		return -1;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
		       sizeof(timeout))) {
		log_err("Failed to set SO_SNDTIMEO");
		return -1;
	}

	return 0;
}

#define save_errno_close(fd) ({ int __save = errno; close(fd); errno = __save; })

int start_server_addr(int type, const struct sockaddr_storage *addr, socklen_t addrlen,
		      const struct network_helper_opts *opts)
{
	int fd;

	if (!opts)
		opts = &default_opts;

	fd = socket(addr->ss_family, type, opts->proto);
	if (fd < 0) {
		log_err("Failed to create server socket");
		return -1;
	}

	if (settimeo(fd, opts->timeout_ms))
		goto error_close;

	if (opts->post_socket_cb &&
	    opts->post_socket_cb(fd, opts->cb_opts)) {
		log_err("Failed to call post_socket_cb");
		goto error_close;
	}

	if (bind(fd, (struct sockaddr *)addr, addrlen) < 0) {
		log_err("Failed to bind socket");
		goto error_close;
	}

	if (type == SOCK_STREAM) {
		if (listen(fd, opts->backlog ? MAX(opts->backlog, 0) : 1) < 0) {
			log_err("Failed to listed on socket");
			goto error_close;
		}
	}

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

int start_server_str(int family, int type, const char *addr_str, __u16 port,
		     const struct network_helper_opts *opts)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;

	if (!opts)
		opts = &default_opts;

	if (make_sockaddr(family, addr_str, port, &addr, &addrlen))
		return -1;

	return start_server_addr(type, &addr, addrlen, opts);
}

int start_server(int family, int type, const char *addr_str, __u16 port,
		 int timeout_ms)
{
	struct network_helper_opts opts = {
		.timeout_ms	= timeout_ms,
	};

	return start_server_str(family, type, addr_str, port, &opts);
}

static int reuseport_cb(int fd, void *opts)
{
	int on = 1;

	return setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
}

int *start_reuseport_server(int family, int type, const char *addr_str,
			    __u16 port, int timeout_ms, unsigned int nr_listens)
{
	struct network_helper_opts opts = {
		.timeout_ms = timeout_ms,
		.post_socket_cb = reuseport_cb,
	};
	struct sockaddr_storage addr;
	unsigned int nr_fds = 0;
	socklen_t addrlen;
	int *fds;

	if (!nr_listens)
		return NULL;

	if (make_sockaddr(family, addr_str, port, &addr, &addrlen))
		return NULL;

	fds = malloc(sizeof(*fds) * nr_listens);
	if (!fds)
		return NULL;

	fds[0] = start_server_addr(type, &addr, addrlen, &opts);
	if (fds[0] == -1)
		goto close_fds;
	nr_fds = 1;

	if (getsockname(fds[0], (struct sockaddr *)&addr, &addrlen))
		goto close_fds;

	for (; nr_fds < nr_listens; nr_fds++) {
		fds[nr_fds] = start_server_addr(type, &addr, addrlen, &opts);
		if (fds[nr_fds] == -1)
			goto close_fds;
	}

	return fds;

close_fds:
	free_fds(fds, nr_fds);
	return NULL;
}

void free_fds(int *fds, unsigned int nr_close_fds)
{
	if (fds) {
		while (nr_close_fds)
			close(fds[--nr_close_fds]);
		free(fds);
	}
}

int fastopen_connect(int server_fd, const char *data, unsigned int data_len,
		     int timeout_ms)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	struct sockaddr_in *addr_in;
	int fd, ret;

	if (getsockname(server_fd, (struct sockaddr *)&addr, &addrlen)) {
		log_err("Failed to get server addr");
		return -1;
	}

	addr_in = (struct sockaddr_in *)&addr;
	fd = socket(addr_in->sin_family, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (settimeo(fd, timeout_ms))
		goto error_close;

	ret = sendto(fd, data, data_len, MSG_FASTOPEN, (struct sockaddr *)&addr,
		     addrlen);
	if (ret != data_len) {
		log_err("sendto(data, %u) != %d\n", data_len, ret);
		goto error_close;
	}

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

int client_socket(int family, int type,
		  const struct network_helper_opts *opts)
{
	int fd;

	if (!opts)
		opts = &default_opts;

	fd = socket(family, type, opts->proto);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (settimeo(fd, opts->timeout_ms))
		goto error_close;

	if (opts->post_socket_cb &&
	    opts->post_socket_cb(fd, opts->cb_opts))
		goto error_close;

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

int connect_to_addr(int type, const struct sockaddr_storage *addr, socklen_t addrlen,
		    const struct network_helper_opts *opts)
{
	int fd;

	if (!opts)
		opts = &default_opts;

	fd = client_socket(addr->ss_family, type, opts);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (connect(fd, (const struct sockaddr *)addr, addrlen)) {
		log_err("Failed to connect to server");
		save_errno_close(fd);
		return -1;
	}

	return fd;
}

int connect_to_addr_str(int family, int type, const char *addr_str, __u16 port,
			const struct network_helper_opts *opts)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;

	if (!opts)
		opts = &default_opts;

	if (make_sockaddr(family, addr_str, port, &addr, &addrlen))
		return -1;

	return connect_to_addr(type, &addr, addrlen, opts);
}

int connect_to_fd_opts(int server_fd, const struct network_helper_opts *opts)
{
	struct sockaddr_storage addr;
	socklen_t addrlen, optlen;
	int type;

	if (!opts)
		opts = &default_opts;

	optlen = sizeof(type);
	if (getsockopt(server_fd, SOL_SOCKET, SO_TYPE, &type, &optlen)) {
		log_err("getsockopt(SOL_TYPE)");
		return -1;
	}

	addrlen = sizeof(addr);
	if (getsockname(server_fd, (struct sockaddr *)&addr, &addrlen)) {
		log_err("Failed to get server addr");
		return -1;
	}

	return connect_to_addr(type, &addr, addrlen, opts);
}

int connect_to_fd(int server_fd, int timeout_ms)
{
	struct network_helper_opts opts = {
		.timeout_ms = timeout_ms,
	};
	socklen_t optlen;
	int protocol;

	optlen = sizeof(protocol);
	if (getsockopt(server_fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &optlen)) {
		log_err("getsockopt(SOL_PROTOCOL)");
		return -1;
	}
	opts.proto = protocol;

	return connect_to_fd_opts(server_fd, &opts);
}

int connect_fd_to_fd(int client_fd, int server_fd, int timeout_ms)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);

	if (settimeo(client_fd, timeout_ms))
		return -1;

	if (getsockname(server_fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		return -1;
	}

	if (connect(client_fd, (const struct sockaddr *)&addr, len)) {
		log_err("Failed to connect to server");
		return -1;
	}

	return 0;
}

int make_sockaddr(int family, const char *addr_str, __u16 port,
		  struct sockaddr_storage *addr, socklen_t *len)
{
	if (family == AF_INET) {
		struct sockaddr_in *sin = (void *)addr;

		memset(addr, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		if (addr_str &&
		    inet_pton(AF_INET, addr_str, &sin->sin_addr) != 1) {
			log_err("inet_pton(AF_INET, %s)", addr_str);
			return -1;
		}
		if (len)
			*len = sizeof(*sin);
		return 0;
	} else if (family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (void *)addr;

		memset(addr, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		if (addr_str &&
		    inet_pton(AF_INET6, addr_str, &sin6->sin6_addr) != 1) {
			log_err("inet_pton(AF_INET6, %s)", addr_str);
			return -1;
		}
		if (len)
			*len = sizeof(*sin6);
		return 0;
	} else if (family == AF_UNIX) {
		/* Note that we always use abstract unix sockets to avoid having
		 * to clean up leftover files.
		 */
		struct sockaddr_un *sun = (void *)addr;

		memset(addr, 0, sizeof(*sun));
		sun->sun_family = family;
		sun->sun_path[0] = 0;
		strcpy(sun->sun_path + 1, addr_str);
		if (len)
			*len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(addr_str);
		return 0;
	}
	return -1;
}

char *ping_command(int family)
{
	if (family == AF_INET6) {
		/* On some systems 'ping' doesn't support IPv6, so use ping6 if it is present. */
		if (!system("which ping6 >/dev/null 2>&1"))
			return "ping6";
		else
			return "ping -6";
	}
	return "ping";
}

int append_tid(char *str, size_t sz)
{
	size_t end;

	if (!str)
		return -1;

	end = strlen(str);
	if (end + 8 > sz)
		return -1;

	sprintf(&str[end], "%07ld", sys_gettid());
	str[end + 7] = '\0';

	return 0;
}

int remove_netns(const char *name)
{
	char *cmd;
	int r;

	r = asprintf(&cmd, "ip netns del %s >/dev/null 2>&1", name);
	if (r < 0) {
		log_err("Failed to malloc cmd");
		return -1;
	}

	r = system(cmd);
	free(cmd);
	return r;
}

int make_netns(const char *name)
{
	char *cmd;
	int r;

	r = asprintf(&cmd, "ip netns add %s", name);
	if (r < 0) {
		log_err("Failed to malloc cmd");
		return -1;
	}

	r = system(cmd);
	free(cmd);

	if (r)
		return r;

	r = asprintf(&cmd, "ip -n %s link set lo up", name);
	if (r < 0) {
		log_err("Failed to malloc cmd for setting up lo");
		remove_netns(name);
		return -1;
	}

	r = system(cmd);
	free(cmd);

	return r;
}

struct nstoken {
	int orig_netns_fd;
};

struct nstoken *open_netns(const char *name)
{
	int nsfd;
	char nspath[PATH_MAX];
	int err;
	struct nstoken *token;

	token = calloc(1, sizeof(struct nstoken));
	if (!token) {
		log_err("Failed to malloc token");
		return NULL;
	}

	token->orig_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	if (token->orig_netns_fd == -1) {
		log_err("Failed to open(/proc/self/ns/net)");
		goto fail;
	}

	snprintf(nspath, sizeof(nspath), "%s/%s", "/var/run/netns", name);
	nsfd = open(nspath, O_RDONLY | O_CLOEXEC);
	if (nsfd == -1) {
		log_err("Failed to open(%s)", nspath);
		goto fail;
	}

	err = setns(nsfd, CLONE_NEWNET);
	close(nsfd);
	if (err) {
		log_err("Failed to setns(nsfd)");
		goto fail;
	}

	return token;
fail:
	if (token->orig_netns_fd != -1)
		close(token->orig_netns_fd);
	free(token);
	return NULL;
}

void close_netns(struct nstoken *token)
{
	if (!token)
		return;

	if (setns(token->orig_netns_fd, CLONE_NEWNET))
		log_err("Failed to setns(orig_netns_fd)");
	close(token->orig_netns_fd);
	free(token);
}

int open_tuntap(const char *dev_name, bool need_mac)
{
	int err = 0;
	struct ifreq ifr;
	int fd = open("/dev/net/tun", O_RDWR);

	if (!ASSERT_GE(fd, 0, "open(/dev/net/tun)"))
		return -1;

	ifr.ifr_flags = IFF_NO_PI | (need_mac ? IFF_TAP : IFF_TUN);
	strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';

	err = ioctl(fd, TUNSETIFF, &ifr);
	if (!ASSERT_OK(err, "ioctl(TUNSETIFF)")) {
		close(fd);
		return -1;
	}

	err = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (!ASSERT_OK(err, "fcntl(O_NONBLOCK)")) {
		close(fd);
		return -1;
	}

	return fd;
}

int get_socket_local_port(int sock_fd)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	int err;

	err = getsockname(sock_fd, (struct sockaddr *)&addr, &addrlen);
	if (err < 0)
		return err;

	if (addr.ss_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&addr;

		return sin->sin_port;
	} else if (addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)&addr;

		return sin->sin6_port;
	}

	return -1;
}

int get_hw_ring_size(char *ifname, struct ethtool_ringparam *ring_param)
{
	struct ifreq ifr = {0};
	int sockfd, err;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		return -errno;

	memcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	ring_param->cmd = ETHTOOL_GRINGPARAM;
	ifr.ifr_data = (char *)ring_param;

	if (ioctl(sockfd, SIOCETHTOOL, &ifr) < 0) {
		err = errno;
		close(sockfd);
		return -err;
	}

	close(sockfd);
	return 0;
}

int set_hw_ring_size(char *ifname, struct ethtool_ringparam *ring_param)
{
	struct ifreq ifr = {0};
	int sockfd, err;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		return -errno;

	memcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	ring_param->cmd = ETHTOOL_SRINGPARAM;
	ifr.ifr_data = (char *)ring_param;

	if (ioctl(sockfd, SIOCETHTOOL, &ifr) < 0) {
		err = errno;
		close(sockfd);
		return -err;
	}

	close(sockfd);
	return 0;
}

struct send_recv_arg {
	int		fd;
	uint32_t	bytes;
	int		stop;
};

static void *send_recv_server(void *arg)
{
	struct send_recv_arg *a = (struct send_recv_arg *)arg;
	ssize_t nr_sent = 0, bytes = 0;
	char batch[1500];
	int err = 0, fd;

	fd = accept(a->fd, NULL, NULL);
	while (fd == -1) {
		if (errno == EINTR)
			continue;
		err = -errno;
		goto done;
	}

	if (settimeo(fd, 0)) {
		err = -errno;
		goto done;
	}

	while (bytes < a->bytes && !READ_ONCE(a->stop)) {
		nr_sent = send(fd, &batch,
			       MIN(a->bytes - bytes, sizeof(batch)), 0);
		if (nr_sent == -1 && errno == EINTR)
			continue;
		if (nr_sent == -1) {
			err = -errno;
			break;
		}
		bytes += nr_sent;
	}

	if (bytes != a->bytes) {
		log_err("send %zd expected %u", bytes, a->bytes);
		if (!err)
			err = bytes > a->bytes ? -E2BIG : -EINTR;
	}

done:
	if (fd >= 0)
		close(fd);
	if (err) {
		WRITE_ONCE(a->stop, 1);
		return ERR_PTR(err);
	}
	return NULL;
}

int send_recv_data(int lfd, int fd, uint32_t total_bytes)
{
	ssize_t nr_recv = 0, bytes = 0;
	struct send_recv_arg arg = {
		.fd	= lfd,
		.bytes	= total_bytes,
		.stop	= 0,
	};
	pthread_t srv_thread;
	void *thread_ret;
	char batch[1500];
	int err = 0;

	err = pthread_create(&srv_thread, NULL, send_recv_server, (void *)&arg);
	if (err) {
		log_err("Failed to pthread_create");
		return err;
	}

	/* recv total_bytes */
	while (bytes < total_bytes && !READ_ONCE(arg.stop)) {
		nr_recv = recv(fd, &batch,
			       MIN(total_bytes - bytes, sizeof(batch)), 0);
		if (nr_recv == -1 && errno == EINTR)
			continue;
		if (nr_recv == -1) {
			err = -errno;
			break;
		}
		bytes += nr_recv;
	}

	if (bytes != total_bytes) {
		log_err("recv %zd expected %u", bytes, total_bytes);
		if (!err)
			err = bytes > total_bytes ? -E2BIG : -EINTR;
	}

	WRITE_ONCE(arg.stop, 1);
	pthread_join(srv_thread, &thread_ret);
	if (IS_ERR(thread_ret)) {
		log_err("Failed in thread_ret %ld", PTR_ERR(thread_ret));
		err = err ? : PTR_ERR(thread_ret);
	}

	return err;
}

#ifdef TRAFFIC_MONITOR
struct tmonitor_ctx {
	pcap_t *pcap;
	pcap_dumper_t *dumper;
	pthread_t thread;
	int wake_fd;

	volatile bool done;
	char pkt_fname[PATH_MAX];
	int pcap_fd;
};

static int __base_pr(const char *format, va_list args)
{
	return vfprintf(stdout, format, args);
}

static tm_print_fn_t __tm_pr = __base_pr;

tm_print_fn_t traffic_monitor_set_print(tm_print_fn_t fn)
{
	tm_print_fn_t old_print_fn;

	old_print_fn = __atomic_exchange_n(&__tm_pr, fn, __ATOMIC_RELAXED);

	return old_print_fn;
}

void tm_print(const char *format, ...)
{
	tm_print_fn_t print_fn;
	va_list args;

	print_fn = __atomic_load_n(&__tm_pr, __ATOMIC_RELAXED);
	if (!print_fn)
		return;

	va_start(args, format);
	print_fn(format, args);
	va_end(args);
}

/* Is this packet captured with a Ethernet protocol type? */
static bool is_ethernet(const u_char *packet)
{
	u16 arphdr_type;

	memcpy(&arphdr_type, packet + 8, 2);
	arphdr_type = ntohs(arphdr_type);

	/* Except the following cases, the protocol type contains the
	 * Ethernet protocol type for the packet.
	 *
	 * https://www.tcpdump.org/linktypes/LINKTYPE_LINUX_SLL2.html
	 */
	switch (arphdr_type) {
	case 770: /* ARPHRD_FRAD */
	case 778: /* ARPHDR_IPGRE */
	case 803: /* ARPHRD_IEEE80211_RADIOTAP */
		tm_print("Packet captured: arphdr_type=%d\n", arphdr_type);
		return false;
	}
	return true;
}

static const char * const pkt_types[] = {
	"In",
	"B",			/* Broadcast */
	"M",			/* Multicast */
	"C",			/* Captured with the promiscuous mode */
	"Out",
};

static const char *pkt_type_str(u16 pkt_type)
{
	if (pkt_type < ARRAY_SIZE(pkt_types))
		return pkt_types[pkt_type];
	return "Unknown";
}

#define MAX_FLAGS_STRLEN 21
/* Show the information of the transport layer in the packet */
static void show_transport(const u_char *packet, u16 len, u32 ifindex,
			   const char *src_addr, const char *dst_addr,
			   u16 proto, bool ipv6, u8 pkt_type)
{
	char *ifname, _ifname[IF_NAMESIZE], flags[MAX_FLAGS_STRLEN] = "";
	const char *transport_str;
	u16 src_port, dst_port;
	struct udphdr *udp;
	struct tcphdr *tcp;

	ifname = if_indextoname(ifindex, _ifname);
	if (!ifname) {
		snprintf(_ifname, sizeof(_ifname), "unknown(%d)", ifindex);
		ifname = _ifname;
	}

	if (proto == IPPROTO_UDP) {
		udp = (struct udphdr *)packet;
		src_port = ntohs(udp->source);
		dst_port = ntohs(udp->dest);
		transport_str = "UDP";
	} else if (proto == IPPROTO_TCP) {
		tcp = (struct tcphdr *)packet;
		src_port = ntohs(tcp->source);
		dst_port = ntohs(tcp->dest);
		transport_str = "TCP";
	} else if (proto == IPPROTO_ICMP) {
		tm_print("%-7s %-3s IPv4 %s > %s: ICMP, length %d, type %d, code %d\n",
			 ifname, pkt_type_str(pkt_type), src_addr, dst_addr, len,
			 packet[0], packet[1]);
		return;
	} else if (proto == IPPROTO_ICMPV6) {
		tm_print("%-7s %-3s IPv6 %s > %s: ICMPv6, length %d, type %d, code %d\n",
			 ifname, pkt_type_str(pkt_type), src_addr, dst_addr, len,
			 packet[0], packet[1]);
		return;
	} else {
		tm_print("%-7s %-3s %s %s > %s: protocol %d\n",
			 ifname, pkt_type_str(pkt_type), ipv6 ? "IPv6" : "IPv4",
			 src_addr, dst_addr, proto);
		return;
	}

	/* TCP or UDP*/

	if (proto == IPPROTO_TCP)
		snprintf(flags, MAX_FLAGS_STRLEN, "%s%s%s%s",
			 tcp->fin ? ", FIN" : "",
			 tcp->syn ? ", SYN" : "",
			 tcp->rst ? ", RST" : "",
			 tcp->ack ? ", ACK" : "");

	if (ipv6)
		tm_print("%-7s %-3s IPv6 %s.%d > %s.%d: %s, length %d%s\n",
			 ifname, pkt_type_str(pkt_type), src_addr, src_port,
			 dst_addr, dst_port, transport_str, len, flags);
	else
		tm_print("%-7s %-3s IPv4 %s:%d > %s:%d: %s, length %d%s\n",
			 ifname, pkt_type_str(pkt_type), src_addr, src_port,
			 dst_addr, dst_port, transport_str, len, flags);
}

static void show_ipv6_packet(const u_char *packet, u32 ifindex, u8 pkt_type)
{
	char src_buf[INET6_ADDRSTRLEN], dst_buf[INET6_ADDRSTRLEN];
	struct ipv6hdr *pkt = (struct ipv6hdr *)packet;
	const char *src, *dst;
	u_char proto;

	src = inet_ntop(AF_INET6, &pkt->saddr, src_buf, sizeof(src_buf));
	if (!src)
		src = "<invalid>";
	dst = inet_ntop(AF_INET6, &pkt->daddr, dst_buf, sizeof(dst_buf));
	if (!dst)
		dst = "<invalid>";
	proto = pkt->nexthdr;
	show_transport(packet + sizeof(struct ipv6hdr),
		       ntohs(pkt->payload_len),
		       ifindex, src, dst, proto, true, pkt_type);
}

static void show_ipv4_packet(const u_char *packet, u32 ifindex, u8 pkt_type)
{
	char src_buf[INET_ADDRSTRLEN], dst_buf[INET_ADDRSTRLEN];
	struct iphdr *pkt = (struct iphdr *)packet;
	const char *src, *dst;
	u_char proto;

	src = inet_ntop(AF_INET, &pkt->saddr, src_buf, sizeof(src_buf));
	if (!src)
		src = "<invalid>";
	dst = inet_ntop(AF_INET, &pkt->daddr, dst_buf, sizeof(dst_buf));
	if (!dst)
		dst = "<invalid>";
	proto = pkt->protocol;
	show_transport(packet + sizeof(struct iphdr),
		       ntohs(pkt->tot_len),
		       ifindex, src, dst, proto, false, pkt_type);
}

static void *traffic_monitor_thread(void *arg)
{
	char *ifname, _ifname[IF_NAMESIZE];
	const u_char *packet, *payload;
	struct tmonitor_ctx *ctx = arg;
	pcap_dumper_t *dumper = ctx->dumper;
	int fd = ctx->pcap_fd, nfds, r;
	int wake_fd = ctx->wake_fd;
	struct pcap_pkthdr header;
	pcap_t *pcap = ctx->pcap;
	u32 ifindex;
	fd_set fds;
	u16 proto;
	u8 ptype;

	nfds = (fd > wake_fd ? fd : wake_fd) + 1;
	FD_ZERO(&fds);

	while (!ctx->done) {
		FD_SET(fd, &fds);
		FD_SET(wake_fd, &fds);
		r = select(nfds, &fds, NULL, NULL, NULL);
		if (!r)
			continue;
		if (r < 0) {
			if (errno == EINTR)
				continue;
			log_err("Fail to select on pcap fd and wake fd");
			break;
		}

		/* This instance of pcap is non-blocking */
		packet = pcap_next(pcap, &header);
		if (!packet)
			continue;

		/* According to the man page of pcap_dump(), first argument
		 * is the pcap_dumper_t pointer even it's argument type is
		 * u_char *.
		 */
		pcap_dump((u_char *)dumper, &header, packet);

		/* Not sure what other types of packets look like. Here, we
		 * parse only Ethernet and compatible packets.
		 */
		if (!is_ethernet(packet))
			continue;

		/* Skip SLL2 header
		 * https://www.tcpdump.org/linktypes/LINKTYPE_LINUX_SLL2.html
		 *
		 * Although the document doesn't mention that, the payload
		 * doesn't include the Ethernet header. The payload starts
		 * from the first byte of the network layer header.
		 */
		payload = packet + 20;

		memcpy(&proto, packet, 2);
		proto = ntohs(proto);
		memcpy(&ifindex, packet + 4, 4);
		ifindex = ntohl(ifindex);
		ptype = packet[10];

		if (proto == ETH_P_IPV6) {
			show_ipv6_packet(payload, ifindex, ptype);
		} else if (proto == ETH_P_IP) {
			show_ipv4_packet(payload, ifindex, ptype);
		} else {
			ifname = if_indextoname(ifindex, _ifname);
			if (!ifname) {
				snprintf(_ifname, sizeof(_ifname), "unknown(%d)", ifindex);
				ifname = _ifname;
			}

			tm_print("%-7s %-3s Unknown network protocol type 0x%x\n",
				 ifname, pkt_type_str(ptype), proto);
		}
	}

	return NULL;
}

/* Prepare the pcap handle to capture packets.
 *
 * This pcap is non-blocking and immediate mode is enabled to receive
 * captured packets as soon as possible.  The snaplen is set to 1024 bytes
 * to limit the size of captured content. The format of the link-layer
 * header is set to DLT_LINUX_SLL2 to enable handling various link-layer
 * technologies.
 */
static pcap_t *traffic_monitor_prepare_pcap(void)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *pcap;
	int r;

	/* Listen on all NICs in the namespace */
	pcap = pcap_create("any", errbuf);
	if (!pcap) {
		log_err("Failed to open pcap: %s", errbuf);
		return NULL;
	}
	/* Limit the size of the packet (first N bytes) */
	r = pcap_set_snaplen(pcap, 1024);
	if (r) {
		log_err("Failed to set snaplen: %s", pcap_geterr(pcap));
		goto error;
	}
	/* To receive packets as fast as possible */
	r = pcap_set_immediate_mode(pcap, 1);
	if (r) {
		log_err("Failed to set immediate mode: %s", pcap_geterr(pcap));
		goto error;
	}
	r = pcap_setnonblock(pcap, 1, errbuf);
	if (r) {
		log_err("Failed to set nonblock: %s", errbuf);
		goto error;
	}
	r = pcap_activate(pcap);
	if (r) {
		log_err("Failed to activate pcap: %s", pcap_geterr(pcap));
		goto error;
	}
	/* Determine the format of the link-layer header */
	r = pcap_set_datalink(pcap, DLT_LINUX_SLL2);
	if (r) {
		log_err("Failed to set datalink: %s", pcap_geterr(pcap));
		goto error;
	}

	return pcap;
error:
	pcap_close(pcap);
	return NULL;
}

static void encode_test_name(char *buf, size_t len, const char *test_name, const char *subtest_name)
{
	char *p;

	if (subtest_name)
		snprintf(buf, len, "%s__%s", test_name, subtest_name);
	else
		snprintf(buf, len, "%s", test_name);
	while ((p = strchr(buf, '/')))
		*p = '_';
	while ((p = strchr(buf, ' ')))
		*p = '_';
}

#define PCAP_DIR "/tmp/tmon_pcap"

/* Start to monitor the network traffic in the given network namespace.
 *
 * netns: the name of the network namespace to monitor. If NULL, the
 *        current network namespace is monitored.
 * test_name: the name of the running test.
 * subtest_name: the name of the running subtest if there is. It should be
 *               NULL if it is not a subtest.
 *
 * This function will start a thread to capture packets going through NICs
 * in the give network namespace.
 */
struct tmonitor_ctx *traffic_monitor_start(const char *netns, const char *test_name,
					   const char *subtest_name)
{
	struct nstoken *nstoken = NULL;
	struct tmonitor_ctx *ctx;
	char test_name_buf[64];
	static int tmon_seq;
	int r;

	if (netns) {
		nstoken = open_netns(netns);
		if (!nstoken)
			return NULL;
	}
	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		log_err("Failed to malloc ctx");
		goto fail_ctx;
	}
	memset(ctx, 0, sizeof(*ctx));

	encode_test_name(test_name_buf, sizeof(test_name_buf), test_name, subtest_name);
	snprintf(ctx->pkt_fname, sizeof(ctx->pkt_fname),
		 PCAP_DIR "/packets-%d-%d-%s-%s.log", getpid(), tmon_seq++,
		 test_name_buf, netns ? netns : "unknown");

	r = mkdir(PCAP_DIR, 0755);
	if (r && errno != EEXIST) {
		log_err("Failed to create " PCAP_DIR);
		goto fail_pcap;
	}

	ctx->pcap = traffic_monitor_prepare_pcap();
	if (!ctx->pcap)
		goto fail_pcap;
	ctx->pcap_fd = pcap_get_selectable_fd(ctx->pcap);
	if (ctx->pcap_fd < 0) {
		log_err("Failed to get pcap fd");
		goto fail_dumper;
	}

	/* Create a packet file */
	ctx->dumper = pcap_dump_open(ctx->pcap, ctx->pkt_fname);
	if (!ctx->dumper) {
		log_err("Failed to open pcap dump: %s", ctx->pkt_fname);
		goto fail_dumper;
	}

	/* Create an eventfd to wake up the monitor thread */
	ctx->wake_fd = eventfd(0, 0);
	if (ctx->wake_fd < 0) {
		log_err("Failed to create eventfd");
		goto fail_eventfd;
	}

	r = pthread_create(&ctx->thread, NULL, traffic_monitor_thread, ctx);
	if (r) {
		log_err("Failed to create thread");
		goto fail;
	}

	close_netns(nstoken);

	return ctx;

fail:
	close(ctx->wake_fd);

fail_eventfd:
	pcap_dump_close(ctx->dumper);
	unlink(ctx->pkt_fname);

fail_dumper:
	pcap_close(ctx->pcap);

fail_pcap:
	free(ctx);

fail_ctx:
	close_netns(nstoken);

	return NULL;
}

static void traffic_monitor_release(struct tmonitor_ctx *ctx)
{
	pcap_close(ctx->pcap);
	pcap_dump_close(ctx->dumper);

	close(ctx->wake_fd);

	free(ctx);
}

/* Stop the network traffic monitor.
 *
 * ctx: the context returned by traffic_monitor_start()
 */
void traffic_monitor_stop(struct tmonitor_ctx *ctx)
{
	__u64 w = 1;

	if (!ctx)
		return;

	/* Stop the monitor thread */
	ctx->done = true;
	/* Wake up the background thread. */
	write(ctx->wake_fd, &w, sizeof(w));
	pthread_join(ctx->thread, NULL);

	tm_print("Packet file: %s\n", strrchr(ctx->pkt_fname, '/') + 1);

	traffic_monitor_release(ctx);
}

#endif /* TRAFFIC_MONITOR */
