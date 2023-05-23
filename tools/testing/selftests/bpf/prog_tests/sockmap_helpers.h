#ifndef __SOCKMAP_HELPERS__
#define __SOCKMAP_HELPERS__

#include <linux/vm_sockets.h>

#define IO_TIMEOUT_SEC 30
#define MAX_STRERR_LEN 256
#define MAX_TEST_NAME 80

/* workaround for older vm_sockets.h */
#ifndef VMADDR_CID_LOCAL
#define VMADDR_CID_LOCAL 1
#endif

#define __always_unused	__attribute__((__unused__))

#define _FAIL(errnum, fmt...)                                                  \
	({                                                                     \
		error_at_line(0, (errnum), __func__, __LINE__, fmt);           \
		CHECK_FAIL(true);                                              \
	})
#define FAIL(fmt...) _FAIL(0, fmt)
#define FAIL_ERRNO(fmt...) _FAIL(errno, fmt)
#define FAIL_LIBBPF(err, msg)                                                  \
	({                                                                     \
		char __buf[MAX_STRERR_LEN];                                    \
		libbpf_strerror((err), __buf, sizeof(__buf));                  \
		FAIL("%s: %s", (msg), __buf);                                  \
	})

/* Wrappers that fail the test on error and report it. */

#define xaccept_nonblock(fd, addr, len)                                        \
	({                                                                     \
		int __ret =                                                    \
			accept_timeout((fd), (addr), (len), IO_TIMEOUT_SEC);   \
		if (__ret == -1)                                               \
			FAIL_ERRNO("accept");                                  \
		__ret;                                                         \
	})

#define xbind(fd, addr, len)                                                   \
	({                                                                     \
		int __ret = bind((fd), (addr), (len));                         \
		if (__ret == -1)                                               \
			FAIL_ERRNO("bind");                                    \
		__ret;                                                         \
	})

#define xclose(fd)                                                             \
	({                                                                     \
		int __ret = close((fd));                                       \
		if (__ret == -1)                                               \
			FAIL_ERRNO("close");                                   \
		__ret;                                                         \
	})

#define xconnect(fd, addr, len)                                                \
	({                                                                     \
		int __ret = connect((fd), (addr), (len));                      \
		if (__ret == -1)                                               \
			FAIL_ERRNO("connect");                                 \
		__ret;                                                         \
	})

#define xgetsockname(fd, addr, len)                                            \
	({                                                                     \
		int __ret = getsockname((fd), (addr), (len));                  \
		if (__ret == -1)                                               \
			FAIL_ERRNO("getsockname");                             \
		__ret;                                                         \
	})

#define xgetsockopt(fd, level, name, val, len)                                 \
	({                                                                     \
		int __ret = getsockopt((fd), (level), (name), (val), (len));   \
		if (__ret == -1)                                               \
			FAIL_ERRNO("getsockopt(" #name ")");                   \
		__ret;                                                         \
	})

#define xlisten(fd, backlog)                                                   \
	({                                                                     \
		int __ret = listen((fd), (backlog));                           \
		if (__ret == -1)                                               \
			FAIL_ERRNO("listen");                                  \
		__ret;                                                         \
	})

#define xsetsockopt(fd, level, name, val, len)                                 \
	({                                                                     \
		int __ret = setsockopt((fd), (level), (name), (val), (len));   \
		if (__ret == -1)                                               \
			FAIL_ERRNO("setsockopt(" #name ")");                   \
		__ret;                                                         \
	})

#define xsend(fd, buf, len, flags)                                             \
	({                                                                     \
		ssize_t __ret = send((fd), (buf), (len), (flags));             \
		if (__ret == -1)                                               \
			FAIL_ERRNO("send");                                    \
		__ret;                                                         \
	})

#define xrecv_nonblock(fd, buf, len, flags)                                    \
	({                                                                     \
		ssize_t __ret = recv_timeout((fd), (buf), (len), (flags),      \
					     IO_TIMEOUT_SEC);                  \
		if (__ret == -1)                                               \
			FAIL_ERRNO("recv");                                    \
		__ret;                                                         \
	})

#define xsocket(family, sotype, flags)                                         \
	({                                                                     \
		int __ret = socket(family, sotype, flags);                     \
		if (__ret == -1)                                               \
			FAIL_ERRNO("socket");                                  \
		__ret;                                                         \
	})

#define xbpf_map_delete_elem(fd, key)                                          \
	({                                                                     \
		int __ret = bpf_map_delete_elem((fd), (key));                  \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_delete");                              \
		__ret;                                                         \
	})

#define xbpf_map_lookup_elem(fd, key, val)                                     \
	({                                                                     \
		int __ret = bpf_map_lookup_elem((fd), (key), (val));           \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_lookup");                              \
		__ret;                                                         \
	})

#define xbpf_map_update_elem(fd, key, val, flags)                              \
	({                                                                     \
		int __ret = bpf_map_update_elem((fd), (key), (val), (flags));  \
		if (__ret < 0)                                               \
			FAIL_ERRNO("map_update");                              \
		__ret;                                                         \
	})

#define xbpf_prog_attach(prog, target, type, flags)                            \
	({                                                                     \
		int __ret =                                                    \
			bpf_prog_attach((prog), (target), (type), (flags));    \
		if (__ret < 0)                                               \
			FAIL_ERRNO("prog_attach(" #type ")");                  \
		__ret;                                                         \
	})

#define xbpf_prog_detach2(prog, target, type)                                  \
	({                                                                     \
		int __ret = bpf_prog_detach2((prog), (target), (type));        \
		if (__ret < 0)                                               \
			FAIL_ERRNO("prog_detach2(" #type ")");                 \
		__ret;                                                         \
	})

#define xpthread_create(thread, attr, func, arg)                               \
	({                                                                     \
		int __ret = pthread_create((thread), (attr), (func), (arg));   \
		errno = __ret;                                                 \
		if (__ret)                                                     \
			FAIL_ERRNO("pthread_create");                          \
		__ret;                                                         \
	})

#define xpthread_join(thread, retval)                                          \
	({                                                                     \
		int __ret = pthread_join((thread), (retval));                  \
		errno = __ret;                                                 \
		if (__ret)                                                     \
			FAIL_ERRNO("pthread_join");                            \
		__ret;                                                         \
	})

static inline int poll_read(int fd, unsigned int timeout_sec)
{
	struct timeval timeout = { .tv_sec = timeout_sec };
	fd_set rfds;
	int r;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	r = select(fd + 1, &rfds, NULL, NULL, &timeout);
	if (r == 0)
		errno = ETIME;

	return r == 1 ? 0 : -1;
}

static inline int accept_timeout(int fd, struct sockaddr *addr, socklen_t *len,
				 unsigned int timeout_sec)
{
	if (poll_read(fd, timeout_sec))
		return -1;

	return accept(fd, addr, len);
}

static inline int recv_timeout(int fd, void *buf, size_t len, int flags,
			       unsigned int timeout_sec)
{
	if (poll_read(fd, timeout_sec))
		return -1;

	return recv(fd, buf, len, flags);
}

static inline void init_addr_loopback4(struct sockaddr_storage *ss,
				       socklen_t *len)
{
	struct sockaddr_in *addr4 = memset(ss, 0, sizeof(*ss));

	addr4->sin_family = AF_INET;
	addr4->sin_port = 0;
	addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	*len = sizeof(*addr4);
}

static inline void init_addr_loopback6(struct sockaddr_storage *ss,
				       socklen_t *len)
{
	struct sockaddr_in6 *addr6 = memset(ss, 0, sizeof(*ss));

	addr6->sin6_family = AF_INET6;
	addr6->sin6_port = 0;
	addr6->sin6_addr = in6addr_loopback;
	*len = sizeof(*addr6);
}

static inline void init_addr_loopback_vsock(struct sockaddr_storage *ss,
					    socklen_t *len)
{
	struct sockaddr_vm *addr = memset(ss, 0, sizeof(*ss));

	addr->svm_family = AF_VSOCK;
	addr->svm_port = VMADDR_PORT_ANY;
	addr->svm_cid = VMADDR_CID_LOCAL;
	*len = sizeof(*addr);
}

static inline void init_addr_loopback(int family, struct sockaddr_storage *ss,
				      socklen_t *len)
{
	switch (family) {
	case AF_INET:
		init_addr_loopback4(ss, len);
		return;
	case AF_INET6:
		init_addr_loopback6(ss, len);
		return;
	case AF_VSOCK:
		init_addr_loopback_vsock(ss, len);
		return;
	default:
		FAIL("unsupported address family %d", family);
	}
}

static inline struct sockaddr *sockaddr(struct sockaddr_storage *ss)
{
	return (struct sockaddr *)ss;
}

#endif // __SOCKMAP_HELPERS__
