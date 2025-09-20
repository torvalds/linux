// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2024 Meta

#include <poll.h>
#include <test_progs.h>
#include "network_helpers.h"
#include "sock_iter_batch.skel.h"

#define TEST_NS "sock_iter_batch_netns"
#define TEST_CHILD_NS "sock_iter_batch_child_netns"

static const int init_batch_size = 16;
static const int nr_soreuse = 4;

struct iter_out {
	int idx;
	__u64 cookie;
} __packed;

struct sock_count {
	__u64 cookie;
	int count;
};

static int insert(__u64 cookie, struct sock_count counts[], int counts_len)
{
	int insert = -1;
	int i = 0;

	for (; i < counts_len; i++) {
		if (!counts[i].cookie) {
			insert = i;
		} else if (counts[i].cookie == cookie) {
			insert = i;
			break;
		}
	}
	if (insert < 0)
		return insert;

	counts[insert].cookie = cookie;
	counts[insert].count++;

	return counts[insert].count;
}

static int read_n(int iter_fd, int n, struct sock_count counts[],
		  int counts_len)
{
	struct iter_out out;
	int nread = 1;
	int i = 0;

	for (; nread > 0 && (n < 0 || i < n); i++) {
		nread = read(iter_fd, &out, sizeof(out));
		if (!nread || !ASSERT_EQ(nread, sizeof(out), "nread"))
			break;
		ASSERT_GE(insert(out.cookie, counts, counts_len), 0, "insert");
	}

	ASSERT_TRUE(n < 0 || i == n, "n < 0 || i == n");

	return i;
}

static __u64 socket_cookie(int fd)
{
	__u64 cookie;
	socklen_t cookie_len = sizeof(cookie);

	if (!ASSERT_OK(getsockopt(fd, SOL_SOCKET, SO_COOKIE, &cookie,
				  &cookie_len), "getsockopt(SO_COOKIE)"))
		return 0;
	return cookie;
}

static bool was_seen(int fd, struct sock_count counts[], int counts_len)
{
	__u64 cookie = socket_cookie(fd);
	int i = 0;

	for (; cookie && i < counts_len; i++)
		if (cookie == counts[i].cookie)
			return true;

	return false;
}

static int get_seen_socket(int *fds, struct sock_count counts[], int n)
{
	int i = 0;

	for (; i < n; i++)
		if (was_seen(fds[i], counts, n))
			return i;
	return -1;
}

static int get_nth_socket(int *fds, int fds_len, struct bpf_link *link, int n)
{
	int i, nread, iter_fd;
	int nth_sock_idx = -1;
	struct iter_out out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_OK_FD(iter_fd, "bpf_iter_create"))
		return -1;

	for (; n >= 0; n--) {
		nread = read(iter_fd, &out, sizeof(out));
		if (!nread || !ASSERT_GE(nread, 1, "nread"))
			goto done;
	}

	for (i = 0; i < fds_len && nth_sock_idx < 0; i++)
		if (fds[i] >= 0 && socket_cookie(fds[i]) == out.cookie)
			nth_sock_idx = i;
done:
	close(iter_fd);
	return nth_sock_idx;
}

static void destroy(int fd)
{
	struct sock_iter_batch *skel = NULL;
	__u64 cookie = socket_cookie(fd);
	struct bpf_link *link = NULL;
	int iter_fd = -1;
	int nread;
	__u64 out;

	skel = sock_iter_batch__open();
	if (!ASSERT_OK_PTR(skel, "sock_iter_batch__open"))
		goto done;

	skel->rodata->destroy_cookie = cookie;

	if (!ASSERT_OK(sock_iter_batch__load(skel), "sock_iter_batch__load"))
		goto done;

	link = bpf_program__attach_iter(skel->progs.iter_tcp_destroy, NULL);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_iter"))
		goto done;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_OK_FD(iter_fd, "bpf_iter_create"))
		goto done;

	/* Delete matching socket. */
	nread = read(iter_fd, &out, sizeof(out));
	ASSERT_GE(nread, 0, "nread");
	if (nread)
		ASSERT_EQ(out, cookie, "cookie matches");
done:
	if (iter_fd >= 0)
		close(iter_fd);
	bpf_link__destroy(link);
	sock_iter_batch__destroy(skel);
	close(fd);
}

static int get_seen_count(int fd, struct sock_count counts[], int n)
{
	__u64 cookie = socket_cookie(fd);
	int count = 0;
	int i = 0;

	for (; cookie && !count && i < n; i++)
		if (cookie == counts[i].cookie)
			count = counts[i].count;

	return count;
}

static void check_n_were_seen_once(int *fds, int fds_len, int n,
				   struct sock_count counts[], int counts_len)
{
	int seen_once = 0;
	int seen_cnt;
	int i = 0;

	for (; i < fds_len; i++) {
		/* Skip any sockets that were closed or that weren't seen
		 * exactly once.
		 */
		if (fds[i] < 0)
			continue;
		seen_cnt = get_seen_count(fds[i], counts, counts_len);
		if (seen_cnt && ASSERT_EQ(seen_cnt, 1, "seen_cnt"))
			seen_once++;
	}

	ASSERT_EQ(seen_once, n, "seen_once");
}

static int accept_from_one(struct pollfd *server_poll_fds,
			   int server_poll_fds_len)
{
	static const int poll_timeout_ms = 5000; /* 5s */
	int ret;
	int i;

	ret = poll(server_poll_fds, server_poll_fds_len, poll_timeout_ms);
	if (!ASSERT_EQ(ret, 1, "poll"))
		return -1;

	for (i = 0; i < server_poll_fds_len; i++)
		if (server_poll_fds[i].revents & POLLIN)
			return accept(server_poll_fds[i].fd, NULL, NULL);

	return -1;
}

static int *connect_to_server(int family, int sock_type, const char *addr,
			      __u16 port, int nr_connects, int *server_fds,
			      int server_fds_len)
{
	struct pollfd *server_poll_fds = NULL;
	int *established_socks = NULL;
	int i;

	server_poll_fds = calloc(server_fds_len, sizeof(*server_poll_fds));
	if (!ASSERT_OK_PTR(server_poll_fds, "server_poll_fds"))
		return NULL;

	for (i = 0; i < server_fds_len; i++) {
		server_poll_fds[i].fd = server_fds[i];
		server_poll_fds[i].events = POLLIN;
	}

	i = 0;

	established_socks = malloc(sizeof(*established_socks) * nr_connects*2);
	if (!ASSERT_OK_PTR(established_socks, "established_socks"))
		goto error;

	while (nr_connects--) {
		established_socks[i] = connect_to_addr_str(family, sock_type,
							   addr, port, NULL);
		if (!ASSERT_OK_FD(established_socks[i], "connect_to_addr_str"))
			goto error;
		i++;
		established_socks[i] = accept_from_one(server_poll_fds,
						       server_fds_len);
		if (!ASSERT_OK_FD(established_socks[i], "accept_from_one"))
			goto error;
		i++;
	}

	free(server_poll_fds);
	return established_socks;
error:
	free_fds(established_socks, i);
	free(server_poll_fds);
	return NULL;
}

static void remove_seen(int family, int sock_type, const char *addr, __u16 port,
			int *socks, int socks_len, int *established_socks,
			int established_socks_len, struct sock_count *counts,
			int counts_len, struct bpf_link *link, int iter_fd)
{
	int close_idx;

	/* Iterate through the first socks_len - 1 sockets. */
	read_n(iter_fd, socks_len - 1, counts, counts_len);

	/* Make sure we saw socks_len - 1 sockets exactly once. */
	check_n_were_seen_once(socks, socks_len, socks_len - 1, counts,
			       counts_len);

	/* Close a socket we've already seen to remove it from the bucket. */
	close_idx = get_seen_socket(socks, counts, counts_len);
	if (!ASSERT_GE(close_idx, 0, "close_idx"))
		return;
	close(socks[close_idx]);
	socks[close_idx] = -1;

	/* Iterate through the rest of the sockets. */
	read_n(iter_fd, -1, counts, counts_len);

	/* Make sure the last socket wasn't skipped and that there were no
	 * repeats.
	 */
	check_n_were_seen_once(socks, socks_len, socks_len - 1, counts,
			       counts_len);
}

static void remove_seen_established(int family, int sock_type, const char *addr,
				    __u16 port, int *listen_socks,
				    int listen_socks_len, int *established_socks,
				    int established_socks_len,
				    struct sock_count *counts, int counts_len,
				    struct bpf_link *link, int iter_fd)
{
	int close_idx;

	/* Iterate through all listening sockets. */
	read_n(iter_fd, listen_socks_len, counts, counts_len);

	/* Make sure we saw all listening sockets exactly once. */
	check_n_were_seen_once(listen_socks, listen_socks_len, listen_socks_len,
			       counts, counts_len);

	/* Leave one established socket. */
	read_n(iter_fd, established_socks_len - 1, counts, counts_len);

	/* Close a socket we've already seen to remove it from the bucket. */
	close_idx = get_nth_socket(established_socks, established_socks_len,
				   link, listen_socks_len + 1);
	if (!ASSERT_GE(close_idx, 0, "close_idx"))
		return;
	destroy(established_socks[close_idx]);
	established_socks[close_idx] = -1;

	/* Iterate through the rest of the sockets. */
	read_n(iter_fd, -1, counts, counts_len);

	/* Make sure the last socket wasn't skipped and that there were no
	 * repeats.
	 */
	check_n_were_seen_once(established_socks, established_socks_len,
			       established_socks_len - 1, counts, counts_len);
}

static void remove_unseen(int family, int sock_type, const char *addr,
			  __u16 port, int *socks, int socks_len,
			  int *established_socks, int established_socks_len,
			  struct sock_count *counts, int counts_len,
			  struct bpf_link *link, int iter_fd)
{
	int close_idx;

	/* Iterate through the first socket. */
	read_n(iter_fd, 1, counts, counts_len);

	/* Make sure we saw a socket from fds. */
	check_n_were_seen_once(socks, socks_len, 1, counts, counts_len);

	/* Close what would be the next socket in the bucket to exercise the
	 * condition where we need to skip past the first cookie we remembered.
	 */
	close_idx = get_nth_socket(socks, socks_len, link, 1);
	if (!ASSERT_GE(close_idx, 0, "close_idx"))
		return;
	close(socks[close_idx]);
	socks[close_idx] = -1;

	/* Iterate through the rest of the sockets. */
	read_n(iter_fd, -1, counts, counts_len);

	/* Make sure the remaining sockets were seen exactly once and that we
	 * didn't repeat the socket that was already seen.
	 */
	check_n_were_seen_once(socks, socks_len, socks_len - 1, counts,
			       counts_len);
}

static void remove_unseen_established(int family, int sock_type,
				      const char *addr, __u16 port,
				      int *listen_socks, int listen_socks_len,
				      int *established_socks,
				      int established_socks_len,
				      struct sock_count *counts, int counts_len,
				      struct bpf_link *link, int iter_fd)
{
	int close_idx;

	/* Iterate through all listening sockets. */
	read_n(iter_fd, listen_socks_len, counts, counts_len);

	/* Make sure we saw all listening sockets exactly once. */
	check_n_were_seen_once(listen_socks, listen_socks_len, listen_socks_len,
			       counts, counts_len);

	/* Iterate through the first established socket. */
	read_n(iter_fd, 1, counts, counts_len);

	/* Make sure we saw one established socks. */
	check_n_were_seen_once(established_socks, established_socks_len, 1,
			       counts, counts_len);

	/* Close what would be the next socket in the bucket to exercise the
	 * condition where we need to skip past the first cookie we remembered.
	 */
	close_idx = get_nth_socket(established_socks, established_socks_len,
				   link, listen_socks_len + 1);
	if (!ASSERT_GE(close_idx, 0, "close_idx"))
		return;

	destroy(established_socks[close_idx]);
	established_socks[close_idx] = -1;

	/* Iterate through the rest of the sockets. */
	read_n(iter_fd, -1, counts, counts_len);

	/* Make sure the remaining sockets were seen exactly once and that we
	 * didn't repeat the socket that was already seen.
	 */
	check_n_were_seen_once(established_socks, established_socks_len,
			       established_socks_len - 1, counts, counts_len);
}

static void remove_all(int family, int sock_type, const char *addr,
		       __u16 port, int *socks, int socks_len,
		       int *established_socks, int established_socks_len,
		       struct sock_count *counts, int counts_len,
		       struct bpf_link *link, int iter_fd)
{
	int close_idx, i;

	/* Iterate through the first socket. */
	read_n(iter_fd, 1, counts, counts_len);

	/* Make sure we saw a socket from fds. */
	check_n_were_seen_once(socks, socks_len, 1, counts, counts_len);

	/* Close all remaining sockets to exhaust the list of saved cookies and
	 * exit without putting any sockets into the batch on the next read.
	 */
	for (i = 0; i < socks_len - 1; i++) {
		close_idx = get_nth_socket(socks, socks_len, link, 1);
		if (!ASSERT_GE(close_idx, 0, "close_idx"))
			return;
		close(socks[close_idx]);
		socks[close_idx] = -1;
	}

	/* Make sure there are no more sockets returned */
	ASSERT_EQ(read_n(iter_fd, -1, counts, counts_len), 0, "read_n");
}

static void remove_all_established(int family, int sock_type, const char *addr,
				   __u16 port, int *listen_socks,
				   int listen_socks_len, int *established_socks,
				   int established_socks_len,
				   struct sock_count *counts, int counts_len,
				   struct bpf_link *link, int iter_fd)
{
	int *close_idx = NULL;
	int i;

	/* Iterate through all listening sockets. */
	read_n(iter_fd, listen_socks_len, counts, counts_len);

	/* Make sure we saw all listening sockets exactly once. */
	check_n_were_seen_once(listen_socks, listen_socks_len, listen_socks_len,
			       counts, counts_len);

	/* Iterate through the first established socket. */
	read_n(iter_fd, 1, counts, counts_len);

	/* Make sure we saw one established socks. */
	check_n_were_seen_once(established_socks, established_socks_len, 1,
			       counts, counts_len);

	/* Close all remaining sockets to exhaust the list of saved cookies and
	 * exit without putting any sockets into the batch on the next read.
	 */
	close_idx = malloc(sizeof(int) * (established_socks_len - 1));
	if (!ASSERT_OK_PTR(close_idx, "close_idx malloc"))
		return;
	for (i = 0; i < established_socks_len - 1; i++) {
		close_idx[i] = get_nth_socket(established_socks,
					      established_socks_len, link,
					      listen_socks_len + i);
		if (!ASSERT_GE(close_idx[i], 0, "close_idx"))
			return;
	}

	for (i = 0; i < established_socks_len - 1; i++) {
		destroy(established_socks[close_idx[i]]);
		established_socks[close_idx[i]] = -1;
	}

	/* Make sure there are no more sockets returned */
	ASSERT_EQ(read_n(iter_fd, -1, counts, counts_len), 0, "read_n");
	free(close_idx);
}

static void add_some(int family, int sock_type, const char *addr, __u16 port,
		     int *socks, int socks_len, int *established_socks,
		     int established_socks_len, struct sock_count *counts,
		     int counts_len, struct bpf_link *link, int iter_fd)
{
	int *new_socks = NULL;

	/* Iterate through the first socks_len - 1 sockets. */
	read_n(iter_fd, socks_len - 1, counts, counts_len);

	/* Make sure we saw socks_len - 1 sockets exactly once. */
	check_n_were_seen_once(socks, socks_len, socks_len - 1, counts,
			       counts_len);

	/* Double the number of sockets in the bucket. */
	new_socks = start_reuseport_server(family, sock_type, addr, port, 0,
					   socks_len);
	if (!ASSERT_OK_PTR(new_socks, "start_reuseport_server"))
		goto done;

	/* Iterate through the rest of the sockets. */
	read_n(iter_fd, -1, counts, counts_len);

	/* Make sure each of the original sockets was seen exactly once. */
	check_n_were_seen_once(socks, socks_len, socks_len, counts,
			       counts_len);
done:
	free_fds(new_socks, socks_len);
}

static void add_some_established(int family, int sock_type, const char *addr,
				 __u16 port, int *listen_socks,
				 int listen_socks_len, int *established_socks,
				 int established_socks_len,
				 struct sock_count *counts,
				 int counts_len, struct bpf_link *link,
				 int iter_fd)
{
	int *new_socks = NULL;

	/* Iterate through all listening sockets. */
	read_n(iter_fd, listen_socks_len, counts, counts_len);

	/* Make sure we saw all listening sockets exactly once. */
	check_n_were_seen_once(listen_socks, listen_socks_len, listen_socks_len,
			       counts, counts_len);

	/* Iterate through the first established_socks_len - 1 sockets. */
	read_n(iter_fd, established_socks_len - 1, counts, counts_len);

	/* Make sure we saw established_socks_len - 1 sockets exactly once. */
	check_n_were_seen_once(established_socks, established_socks_len,
			       established_socks_len - 1, counts, counts_len);

	/* Double the number of established sockets in the bucket. */
	new_socks = connect_to_server(family, sock_type, addr, port,
				      established_socks_len / 2, listen_socks,
				      listen_socks_len);
	if (!ASSERT_OK_PTR(new_socks, "connect_to_server"))
		goto done;

	/* Iterate through the rest of the sockets. */
	read_n(iter_fd, -1, counts, counts_len);

	/* Make sure each of the original sockets was seen exactly once. */
	check_n_were_seen_once(listen_socks, listen_socks_len, listen_socks_len,
			       counts, counts_len);
	check_n_were_seen_once(established_socks, established_socks_len,
			       established_socks_len, counts, counts_len);
done:
	free_fds(new_socks, established_socks_len);
}

static void force_realloc(int family, int sock_type, const char *addr,
			  __u16 port, int *socks, int socks_len,
			  int *established_socks, int established_socks_len,
			  struct sock_count *counts, int counts_len,
			  struct bpf_link *link, int iter_fd)
{
	int *new_socks = NULL;

	/* Iterate through the first socket just to initialize the batch. */
	read_n(iter_fd, 1, counts, counts_len);

	/* Double the number of sockets in the bucket to force a realloc on the
	 * next read.
	 */
	new_socks = start_reuseport_server(family, sock_type, addr, port, 0,
					   socks_len);
	if (!ASSERT_OK_PTR(new_socks, "start_reuseport_server"))
		goto done;

	/* Iterate through the rest of the sockets. */
	read_n(iter_fd, -1, counts, counts_len);

	/* Make sure each socket from the first set was seen exactly once. */
	check_n_were_seen_once(socks, socks_len, socks_len, counts,
			       counts_len);
done:
	free_fds(new_socks, socks_len);
}

static void force_realloc_established(int family, int sock_type,
				      const char *addr, __u16 port,
				      int *listen_socks, int listen_socks_len,
				      int *established_socks,
				      int established_socks_len,
				      struct sock_count *counts, int counts_len,
				      struct bpf_link *link, int iter_fd)
{
	/* Iterate through all sockets to trigger a realloc. */
	read_n(iter_fd, -1, counts, counts_len);

	/* Make sure each socket was seen exactly once. */
	check_n_were_seen_once(listen_socks, listen_socks_len, listen_socks_len,
			       counts, counts_len);
	check_n_were_seen_once(established_socks, established_socks_len,
			       established_socks_len, counts, counts_len);
}

struct test_case {
	void (*test)(int family, int sock_type, const char *addr, __u16 port,
		     int *socks, int socks_len, int *established_socks,
		     int established_socks_len, struct sock_count *counts,
		     int counts_len, struct bpf_link *link, int iter_fd);
	const char *description;
	int ehash_buckets;
	int connections;
	int init_socks;
	int max_socks;
	int sock_type;
	int family;
};

static struct test_case resume_tests[] = {
	{
		.description = "udp: resume after removing a seen socket",
		.init_socks = nr_soreuse,
		.max_socks = nr_soreuse,
		.sock_type = SOCK_DGRAM,
		.family = AF_INET6,
		.test = remove_seen,
	},
	{
		.description = "udp: resume after removing one unseen socket",
		.init_socks = nr_soreuse,
		.max_socks = nr_soreuse,
		.sock_type = SOCK_DGRAM,
		.family = AF_INET6,
		.test = remove_unseen,
	},
	{
		.description = "udp: resume after removing all unseen sockets",
		.init_socks = nr_soreuse,
		.max_socks = nr_soreuse,
		.sock_type = SOCK_DGRAM,
		.family = AF_INET6,
		.test = remove_all,
	},
	{
		.description = "udp: resume after adding a few sockets",
		.init_socks = nr_soreuse,
		.max_socks = nr_soreuse,
		.sock_type = SOCK_DGRAM,
		/* Use AF_INET so that new sockets are added to the head of the
		 * bucket's list.
		 */
		.family = AF_INET,
		.test = add_some,
	},
	{
		.description = "udp: force a realloc to occur",
		.init_socks = init_batch_size,
		.max_socks = init_batch_size * 2,
		.sock_type = SOCK_DGRAM,
		/* Use AF_INET6 so that new sockets are added to the tail of the
		 * bucket's list, needing to be added to the next batch to force
		 * a realloc.
		 */
		.family = AF_INET6,
		.test = force_realloc,
	},
	{
		.description = "tcp: resume after removing a seen socket (listening)",
		.init_socks = nr_soreuse,
		.max_socks = nr_soreuse,
		.sock_type = SOCK_STREAM,
		.family = AF_INET6,
		.test = remove_seen,
	},
	{
		.description = "tcp: resume after removing one unseen socket (listening)",
		.init_socks = nr_soreuse,
		.max_socks = nr_soreuse,
		.sock_type = SOCK_STREAM,
		.family = AF_INET6,
		.test = remove_unseen,
	},
	{
		.description = "tcp: resume after removing all unseen sockets (listening)",
		.init_socks = nr_soreuse,
		.max_socks = nr_soreuse,
		.sock_type = SOCK_STREAM,
		.family = AF_INET6,
		.test = remove_all,
	},
	{
		.description = "tcp: resume after adding a few sockets (listening)",
		.init_socks = nr_soreuse,
		.max_socks = nr_soreuse,
		.sock_type = SOCK_STREAM,
		/* Use AF_INET so that new sockets are added to the head of the
		 * bucket's list.
		 */
		.family = AF_INET,
		.test = add_some,
	},
	{
		.description = "tcp: force a realloc to occur (listening)",
		.init_socks = init_batch_size,
		.max_socks = init_batch_size * 2,
		.sock_type = SOCK_STREAM,
		/* Use AF_INET6 so that new sockets are added to the tail of the
		 * bucket's list, needing to be added to the next batch to force
		 * a realloc.
		 */
		.family = AF_INET6,
		.test = force_realloc,
	},
	{
		.description = "tcp: resume after removing a seen socket (established)",
		/* Force all established sockets into one bucket */
		.ehash_buckets = 1,
		.connections = nr_soreuse,
		.init_socks = nr_soreuse,
		/* Room for connect()ed and accept()ed sockets */
		.max_socks = nr_soreuse * 3,
		.sock_type = SOCK_STREAM,
		.family = AF_INET6,
		.test = remove_seen_established,
	},
	{
		.description = "tcp: resume after removing one unseen socket (established)",
		/* Force all established sockets into one bucket */
		.ehash_buckets = 1,
		.connections = nr_soreuse,
		.init_socks = nr_soreuse,
		/* Room for connect()ed and accept()ed sockets */
		.max_socks = nr_soreuse * 3,
		.sock_type = SOCK_STREAM,
		.family = AF_INET6,
		.test = remove_unseen_established,
	},
	{
		.description = "tcp: resume after removing all unseen sockets (established)",
		/* Force all established sockets into one bucket */
		.ehash_buckets = 1,
		.connections = nr_soreuse,
		.init_socks = nr_soreuse,
		/* Room for connect()ed and accept()ed sockets */
		.max_socks = nr_soreuse * 3,
		.sock_type = SOCK_STREAM,
		.family = AF_INET6,
		.test = remove_all_established,
	},
	{
		.description = "tcp: resume after adding a few sockets (established)",
		/* Force all established sockets into one bucket */
		.ehash_buckets = 1,
		.connections = nr_soreuse,
		.init_socks = nr_soreuse,
		/* Room for connect()ed and accept()ed sockets */
		.max_socks = nr_soreuse * 3,
		.sock_type = SOCK_STREAM,
		.family = AF_INET6,
		.test = add_some_established,
	},
	{
		.description = "tcp: force a realloc to occur (established)",
		/* Force all established sockets into one bucket */
		.ehash_buckets = 1,
		/* Bucket size will need to double when going from listening to
		 * established sockets.
		 */
		.connections = init_batch_size,
		.init_socks = nr_soreuse,
		/* Room for connect()ed and accept()ed sockets */
		.max_socks = nr_soreuse + (init_batch_size * 2),
		.sock_type = SOCK_STREAM,
		.family = AF_INET6,
		.test = force_realloc_established,
	},
};

static void do_resume_test(struct test_case *tc)
{
	struct sock_iter_batch *skel = NULL;
	struct sock_count *counts = NULL;
	static const __u16 port = 10001;
	struct nstoken *nstoken = NULL;
	struct bpf_link *link = NULL;
	int *established_fds = NULL;
	int err, iter_fd = -1;
	const char *addr;
	int *fds = NULL;

	if (tc->ehash_buckets) {
		SYS_NOFAIL("ip netns del " TEST_CHILD_NS);
		SYS(done, "sysctl -wq net.ipv4.tcp_child_ehash_entries=%d",
		    tc->ehash_buckets);
		SYS(done, "ip netns add %s", TEST_CHILD_NS);
		SYS(done, "ip -net %s link set dev lo up", TEST_CHILD_NS);
		nstoken = open_netns(TEST_CHILD_NS);
		if (!ASSERT_OK_PTR(nstoken, "open_child_netns"))
			goto done;
	}

	counts = calloc(tc->max_socks, sizeof(*counts));
	if (!ASSERT_OK_PTR(counts, "counts"))
		goto done;
	skel = sock_iter_batch__open();
	if (!ASSERT_OK_PTR(skel, "sock_iter_batch__open"))
		goto done;

	/* Prepare a bucket of sockets in the kernel hashtable */
	addr = tc->family == AF_INET6 ? "::1" : "127.0.0.1";
	fds = start_reuseport_server(tc->family, tc->sock_type, addr, port, 0,
				     tc->init_socks);
	if (!ASSERT_OK_PTR(fds, "start_reuseport_server"))
		goto done;
	if (tc->connections) {
		established_fds = connect_to_server(tc->family, tc->sock_type,
						    addr, port,
						    tc->connections, fds,
						    tc->init_socks);
		if (!ASSERT_OK_PTR(established_fds, "connect_to_server"))
			goto done;
	}
	skel->rodata->ports[0] = 0;
	skel->rodata->ports[1] = 0;
	skel->rodata->sf = tc->family;
	skel->rodata->ss = 0;

	err = sock_iter_batch__load(skel);
	if (!ASSERT_OK(err, "sock_iter_batch__load"))
		goto done;

	link = bpf_program__attach_iter(tc->sock_type == SOCK_STREAM ?
					skel->progs.iter_tcp_soreuse :
					skel->progs.iter_udp_soreuse,
					NULL);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_iter"))
		goto done;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_OK_FD(iter_fd, "bpf_iter_create"))
		goto done;

	tc->test(tc->family, tc->sock_type, addr, port, fds, tc->init_socks,
		 established_fds, tc->connections*2, counts, tc->max_socks,
		 link, iter_fd);
done:
	close_netns(nstoken);
	SYS_NOFAIL("ip netns del " TEST_CHILD_NS);
	SYS_NOFAIL("sysctl -w net.ipv4.tcp_child_ehash_entries=0");
	free(counts);
	free_fds(fds, tc->init_socks);
	free_fds(established_fds, tc->connections*2);
	if (iter_fd >= 0)
		close(iter_fd);
	bpf_link__destroy(link);
	sock_iter_batch__destroy(skel);
}

static void do_resume_tests(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(resume_tests); i++) {
		if (test__start_subtest(resume_tests[i].description)) {
			do_resume_test(&resume_tests[i]);
		}
	}
}

static void do_test(int sock_type, bool onebyone)
{
	int err, i, nread, to_read, total_read, iter_fd = -1;
	struct iter_out outputs[nr_soreuse];
	struct bpf_link *link = NULL;
	struct sock_iter_batch *skel;
	int first_idx, second_idx;
	int *fds[2] = {};

	skel = sock_iter_batch__open();
	if (!ASSERT_OK_PTR(skel, "sock_iter_batch__open"))
		return;

	/* Prepare 2 buckets of sockets in the kernel hashtable */
	for (i = 0; i < ARRAY_SIZE(fds); i++) {
		int local_port;

		fds[i] = start_reuseport_server(AF_INET6, sock_type, "::1", 0, 0,
						nr_soreuse);
		if (!ASSERT_OK_PTR(fds[i], "start_reuseport_server"))
			goto done;
		local_port = get_socket_local_port(*fds[i]);
		if (!ASSERT_GE(local_port, 0, "get_socket_local_port"))
			goto done;
		skel->rodata->ports[i] = ntohs(local_port);
	}
	skel->rodata->sf = AF_INET6;
	if (sock_type == SOCK_STREAM)
		skel->rodata->ss = TCP_LISTEN;

	err = sock_iter_batch__load(skel);
	if (!ASSERT_OK(err, "sock_iter_batch__load"))
		goto done;

	link = bpf_program__attach_iter(sock_type == SOCK_STREAM ?
					skel->progs.iter_tcp_soreuse :
					skel->progs.iter_udp_soreuse,
					NULL);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_iter"))
		goto done;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "bpf_iter_create"))
		goto done;

	/* Test reading a bucket (either from fds[0] or fds[1]).
	 * Only read "nr_soreuse - 1" number of sockets
	 * from a bucket and leave one socket out from
	 * that bucket on purpose.
	 */
	to_read = (nr_soreuse - 1) * sizeof(*outputs);
	total_read = 0;
	first_idx = -1;
	do {
		nread = read(iter_fd, outputs, onebyone ? sizeof(*outputs) : to_read);
		if (nread <= 0 || nread % sizeof(*outputs))
			break;
		total_read += nread;

		if (first_idx == -1)
			first_idx = outputs[0].idx;
		for (i = 0; i < nread / sizeof(*outputs); i++)
			ASSERT_EQ(outputs[i].idx, first_idx, "first_idx");
	} while (total_read < to_read);
	ASSERT_EQ(nread, onebyone ? sizeof(*outputs) : to_read, "nread");
	ASSERT_EQ(total_read, to_read, "total_read");

	free_fds(fds[first_idx], nr_soreuse);
	fds[first_idx] = NULL;

	/* Read the "whole" second bucket */
	to_read = nr_soreuse * sizeof(*outputs);
	total_read = 0;
	second_idx = !first_idx;
	do {
		nread = read(iter_fd, outputs, onebyone ? sizeof(*outputs) : to_read);
		if (nread <= 0 || nread % sizeof(*outputs))
			break;
		total_read += nread;

		for (i = 0; i < nread / sizeof(*outputs); i++)
			ASSERT_EQ(outputs[i].idx, second_idx, "second_idx");
	} while (total_read <= to_read);
	ASSERT_EQ(nread, 0, "nread");
	/* Both so_reuseport ports should be in different buckets, so
	 * total_read must equal to the expected to_read.
	 *
	 * For a very unlikely case, both ports collide at the same bucket,
	 * the bucket offset (i.e. 3) will be skipped and it cannot
	 * expect the to_read number of bytes.
	 */
	if (skel->bss->bucket[0] != skel->bss->bucket[1])
		ASSERT_EQ(total_read, to_read, "total_read");

done:
	for (i = 0; i < ARRAY_SIZE(fds); i++)
		free_fds(fds[i], nr_soreuse);
	if (iter_fd < 0)
		close(iter_fd);
	bpf_link__destroy(link);
	sock_iter_batch__destroy(skel);
}

void test_sock_iter_batch(void)
{
	struct nstoken *nstoken = NULL;

	SYS_NOFAIL("ip netns del " TEST_NS);
	SYS(done, "ip netns add %s", TEST_NS);
	SYS(done, "ip -net %s link set dev lo up", TEST_NS);

	nstoken = open_netns(TEST_NS);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto done;

	if (test__start_subtest("tcp")) {
		do_test(SOCK_STREAM, true);
		do_test(SOCK_STREAM, false);
	}
	if (test__start_subtest("udp")) {
		do_test(SOCK_DGRAM, true);
		do_test(SOCK_DGRAM, false);
	}
	do_resume_tests();
	close_netns(nstoken);

done:
	SYS_NOFAIL("ip netns del " TEST_NS);
}
