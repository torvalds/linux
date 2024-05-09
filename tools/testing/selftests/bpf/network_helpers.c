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
#include <sys/un.h>

#include <linux/err.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/limits.h>

#include "bpf_util.h"
#include "network_helpers.h"
#include "test_progs.h"

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

static int __start_server(int type, const struct sockaddr *addr, socklen_t addrlen,
			  const struct network_helper_opts *opts)
{
	int fd;

	fd = socket(addr->sa_family, type, opts->proto);
	if (fd < 0) {
		log_err("Failed to create server socket");
		return -1;
	}

	if (settimeo(fd, opts->timeout_ms))
		goto error_close;

	if (opts->post_socket_cb && opts->post_socket_cb(fd, NULL)) {
		log_err("Failed to call post_socket_cb");
		goto error_close;
	}

	if (bind(fd, addr, addrlen) < 0) {
		log_err("Failed to bind socket");
		goto error_close;
	}

	if (type == SOCK_STREAM) {
		if (listen(fd, 1) < 0) {
			log_err("Failed to listed on socket");
			goto error_close;
		}
	}

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

int start_server(int family, int type, const char *addr_str, __u16 port,
		 int timeout_ms)
{
	struct network_helper_opts opts = {
		.timeout_ms	= timeout_ms,
	};
	struct sockaddr_storage addr;
	socklen_t addrlen;

	if (make_sockaddr(family, addr_str, port, &addr, &addrlen))
		return -1;

	return __start_server(type, (struct sockaddr *)&addr, addrlen, &opts);
}

static int reuseport_cb(int fd, const struct post_socket_opts *opts)
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

	fds[0] = __start_server(type, (struct sockaddr *)&addr, addrlen, &opts);
	if (fds[0] == -1)
		goto close_fds;
	nr_fds = 1;

	if (getsockname(fds[0], (struct sockaddr *)&addr, &addrlen))
		goto close_fds;

	for (; nr_fds < nr_listens; nr_fds++) {
		fds[nr_fds] = __start_server(type, (struct sockaddr *)&addr, addrlen, &opts);
		if (fds[nr_fds] == -1)
			goto close_fds;
	}

	return fds;

close_fds:
	free_fds(fds, nr_fds);
	return NULL;
}

int start_server_addr(int type, const struct sockaddr_storage *addr, socklen_t len,
		      const struct network_helper_opts *opts)
{
	if (!opts)
		opts = &default_opts;

	return __start_server(type, (struct sockaddr *)addr, len, opts);
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

static int connect_fd_to_addr(int fd,
			      const struct sockaddr_storage *addr,
			      socklen_t addrlen, const bool must_fail)
{
	int ret;

	errno = 0;
	ret = connect(fd, (const struct sockaddr *)addr, addrlen);
	if (must_fail) {
		if (!ret) {
			log_err("Unexpected success to connect to server");
			return -1;
		}
		if (errno != EPERM) {
			log_err("Unexpected error from connect to server");
			return -1;
		}
	} else {
		if (ret) {
			log_err("Failed to connect to server");
			return -1;
		}
	}

	return 0;
}

int connect_to_addr(int type, const struct sockaddr_storage *addr, socklen_t addrlen,
		    const struct network_helper_opts *opts)
{
	int fd;

	if (!opts)
		opts = &default_opts;

	fd = socket(addr->ss_family, type, opts->proto);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (settimeo(fd, opts->timeout_ms))
		goto error_close;

	if (connect_fd_to_addr(fd, addr, addrlen, opts->must_fail))
		goto error_close;

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

int connect_to_fd_opts(int server_fd, const struct network_helper_opts *opts)
{
	struct sockaddr_storage addr;
	struct sockaddr_in *addr_in;
	socklen_t addrlen, optlen;
	int fd, type, protocol;

	if (!opts)
		opts = &default_opts;

	optlen = sizeof(type);

	if (opts->type) {
		type = opts->type;
	} else {
		if (getsockopt(server_fd, SOL_SOCKET, SO_TYPE, &type, &optlen)) {
			log_err("getsockopt(SOL_TYPE)");
			return -1;
		}
	}

	if (opts->proto) {
		protocol = opts->proto;
	} else {
		if (getsockopt(server_fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &optlen)) {
			log_err("getsockopt(SOL_PROTOCOL)");
			return -1;
		}
	}

	addrlen = sizeof(addr);
	if (getsockname(server_fd, (struct sockaddr *)&addr, &addrlen)) {
		log_err("Failed to get server addr");
		return -1;
	}

	addr_in = (struct sockaddr_in *)&addr;
	fd = socket(addr_in->sin_family, type, protocol);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (settimeo(fd, opts->timeout_ms))
		goto error_close;

	if (opts->cc && opts->cc[0] &&
	    setsockopt(fd, SOL_TCP, TCP_CONGESTION, opts->cc,
		       strlen(opts->cc) + 1))
		goto error_close;

	if (!opts->noconnect)
		if (connect_fd_to_addr(fd, &addr, addrlen, opts->must_fail))
			goto error_close;

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

int connect_to_fd(int server_fd, int timeout_ms)
{
	struct network_helper_opts opts = {
		.timeout_ms = timeout_ms,
	};

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

	if (connect_fd_to_addr(client_fd, &addr, len, false))
		return -1;

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
