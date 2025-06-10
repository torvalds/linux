/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TCP-AO selftest library. Provides helpers to unshare network
 * namespaces, create veth, assign ip addresses, set routes,
 * manipulate socket options, read network counter and etc.
 * Author: Dmitry Safonov <dima@arista.com>
 */
#ifndef _AOLIB_H_
#define _AOLIB_H_

#include <arpa/inet.h>
#include <errno.h>
#include <linux/snmp.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../../../../../include/linux/stringify.h"
#include "../../../../../include/linux/bits.h"

#ifndef SOL_TCP
/* can't include <netinet/tcp.h> as including <linux/tcp.h> */
# define SOL_TCP		6	/* TCP level */
#endif

/* Working around ksft, see the comment in lib/setup.c */
extern void __test_msg(const char *buf);
extern void __test_ok(const char *buf);
extern void __test_fail(const char *buf);
extern void __test_xfail(const char *buf);
extern void __test_error(const char *buf);
extern void __test_skip(const char *buf);

static inline char *test_snprintf(const char *fmt, va_list vargs)
{
	char *ret = NULL;
	size_t size = 0;
	va_list tmp;
	int n = 0;

	va_copy(tmp, vargs);
	n = vsnprintf(ret, size, fmt, tmp);
	va_end(tmp);
	if (n < 0)
		return NULL;

	size = n + 1;
	ret = malloc(size);
	if (!ret)
		return NULL;

	n = vsnprintf(ret, size, fmt, vargs);
	if (n < 0 || n > size - 1) {
		free(ret);
		return NULL;
	}
	return ret;
}

static __printf(1, 2) inline char *test_sprintf(const char *fmt, ...)
{
	va_list vargs;
	char *ret;

	va_start(vargs, fmt);
	ret = test_snprintf(fmt, vargs);
	va_end(vargs);

	return ret;
}

static __printf(2, 3) inline void __test_print(void (*fn)(const char *),
					       const char *fmt, ...)
{
	va_list vargs;
	char *msg;

	va_start(vargs, fmt);
	msg = test_snprintf(fmt, vargs);
	va_end(vargs);

	if (!msg)
		return;

	fn(msg);
	free(msg);
}

#define test_print(fmt, ...)						\
	__test_print(__test_msg, "%ld[%s:%u] " fmt "\n",		\
		     syscall(SYS_gettid),				\
		     __FILE__, __LINE__, ##__VA_ARGS__)

#define test_ok(fmt, ...)						\
	__test_print(__test_ok, fmt "\n", ##__VA_ARGS__)
#define test_skip(fmt, ...)						\
	__test_print(__test_skip, fmt "\n", ##__VA_ARGS__)
#define test_xfail(fmt, ...)						\
	__test_print(__test_xfail, fmt "\n", ##__VA_ARGS__)

#define test_fail(fmt, ...)						\
do {									\
	if (errno)							\
		__test_print(__test_fail, fmt ": %m\n", ##__VA_ARGS__);	\
	else								\
		__test_print(__test_fail, fmt "\n", ##__VA_ARGS__);	\
	test_failed();							\
} while (0)

#define KSFT_FAIL  1
#define test_error(fmt, ...)						\
do {									\
	if (errno)							\
		__test_print(__test_error, "%ld[%s:%u] " fmt ": %m\n",	\
			     syscall(SYS_gettid), __FILE__, __LINE__,	\
			     ##__VA_ARGS__);				\
	else								\
		__test_print(__test_error, "%ld[%s:%u] " fmt "\n",	\
			     syscall(SYS_gettid), __FILE__, __LINE__,	\
			     ##__VA_ARGS__);				\
	exit(KSFT_FAIL);						\
} while (0)

enum test_fault {
	FAULT_TIMEOUT = 1,
	FAULT_KEYREJECT,
	FAULT_PREINSTALL_AO,
	FAULT_PREINSTALL_MD5,
	FAULT_POSTINSTALL,
	FAULT_BUSY,
	FAULT_CURRNEXT,
	FAULT_FIXME,
};
typedef enum test_fault fault_t;

enum test_needs_kconfig {
	KCONFIG_NET_NS = 0,		/* required */
	KCONFIG_VETH,			/* required */
	KCONFIG_TCP_AO,			/* required */
	KCONFIG_TCP_MD5,		/* optional, for TCP-MD5 features */
	KCONFIG_NET_VRF,		/* optional, for L3/VRF testing */
	KCONFIG_FTRACE,			/* optional, for tracepoints checks */
	__KCONFIG_LAST__
};
extern bool kernel_config_has(enum test_needs_kconfig k);
extern const char *tests_skip_reason[__KCONFIG_LAST__];
static inline bool should_skip_test(const char *tst_name,
				    enum test_needs_kconfig k)
{
	if (kernel_config_has(k))
		return false;
	test_skip("%s: %s", tst_name, tests_skip_reason[k]);
	return true;
}

union tcp_addr {
	struct in_addr a4;
	struct in6_addr a6;
};

typedef void *(*thread_fn)(void *);
extern void test_failed(void);
extern void __test_init(unsigned int ntests, int family, unsigned int prefix,
			union tcp_addr addr1, union tcp_addr addr2,
			thread_fn peer1, thread_fn peer2);

static inline void test_init2(unsigned int ntests,
			      thread_fn peer1, thread_fn peer2,
			      int family, unsigned int prefix,
			      const char *addr1, const char *addr2)
{
	union tcp_addr taddr1, taddr2;

	if (inet_pton(family, addr1, &taddr1) != 1)
		test_error("Can't convert ip address %s", addr1);
	if (inet_pton(family, addr2, &taddr2) != 1)
		test_error("Can't convert ip address %s", addr2);

	__test_init(ntests, family, prefix, taddr1, taddr2, peer1, peer2);
}
extern void test_add_destructor(void (*d)(void));
extern void test_init_ftrace(int nsfd1, int nsfd2);
extern int test_setup_tracing(void);

/* To adjust optmem socket limit, approximately estimate a number,
 * that is bigger than sizeof(struct tcp_ao_key).
 */
#define KERNEL_TCP_AO_KEY_SZ_ROUND_UP	300

extern void test_set_optmem(size_t value);
extern size_t test_get_optmem(void);

extern const struct sockaddr_in6 addr_any6;
extern const struct sockaddr_in addr_any4;

#ifdef IPV6_TEST
# define __TEST_CLIENT_IP(n)	("2001:db8:" __stringify(n) "::1")
# define TEST_CLIENT_IP	__TEST_CLIENT_IP(1)
# define TEST_WRONG_IP	"2001:db8:253::1"
# define TEST_SERVER_IP	"2001:db8:254::1"
# define TEST_NETWORK	"2001::"
# define TEST_PREFIX	128
# define TEST_FAMILY	AF_INET6
# define SOCKADDR_ANY	addr_any6
# define sockaddr_af	struct sockaddr_in6
#else
# define __TEST_CLIENT_IP(n)	("10.0." __stringify(n) ".1")
# define TEST_CLIENT_IP	__TEST_CLIENT_IP(1)
# define TEST_WRONG_IP	"10.0.253.1"
# define TEST_SERVER_IP	"10.0.254.1"
# define TEST_NETWORK	"10.0.0.0"
# define TEST_PREFIX	32
# define TEST_FAMILY	AF_INET
# define SOCKADDR_ANY	addr_any4
# define sockaddr_af	struct sockaddr_in
#endif

static inline union tcp_addr gen_tcp_addr(union tcp_addr net, size_t n)
{
	union tcp_addr ret = net;

#ifdef IPV6_TEST
	ret.a6.s6_addr32[3] = htonl(n & (BIT(32) - 1));
	ret.a6.s6_addr32[2] = htonl((n >> 32) & (BIT(32) - 1));
#else
	ret.a4.s_addr = htonl(ntohl(net.a4.s_addr) + n);
#endif

	return ret;
}

static inline void tcp_addr_to_sockaddr_in(void *dest,
					   const union tcp_addr *src,
					   unsigned int port)
{
	sockaddr_af *out = dest;

	memset(out, 0, sizeof(*out));
#ifdef IPV6_TEST
	out->sin6_family = AF_INET6;
	out->sin6_port   = port;
	out->sin6_addr   = src->a6;
#else
	out->sin_family  = AF_INET;
	out->sin_port    = port;
	out->sin_addr    = src->a4;
#endif
}

static inline void test_init(unsigned int ntests,
			     thread_fn peer1, thread_fn peer2)
{
	test_init2(ntests, peer1, peer2, TEST_FAMILY, TEST_PREFIX,
			TEST_SERVER_IP, TEST_CLIENT_IP);
}
extern void synchronize_threads(void);
extern void switch_ns(int fd);
extern int switch_save_ns(int fd);
extern void switch_close_ns(int fd);

extern __thread union tcp_addr this_ip_addr;
extern __thread union tcp_addr this_ip_dest;
extern int test_family;

extern void randomize_buffer(void *buf, size_t buflen);
extern __printf(3, 4) int test_echo(const char *fname, bool append,
				    const char *fmt, ...);

extern int open_netns(void);
extern int unshare_open_netns(void);
extern const char veth_name[];
extern int add_veth(const char *name, int nsfda, int nsfdb);
extern int add_vrf(const char *name, uint32_t tabid, int ifindex, int nsfd);
extern int ip_addr_add(const char *intf, int family,
		       union tcp_addr addr, uint8_t prefix);
extern int ip_route_add(const char *intf, int family,
			union tcp_addr src, union tcp_addr dst);
extern int ip_route_add_vrf(const char *intf, int family,
			    union tcp_addr src, union tcp_addr dst,
			    uint8_t vrf);
extern int link_set_up(const char *intf);

extern const unsigned int test_server_port;
extern int test_wait_fd(int sk, time_t sec, bool write);
extern int __test_connect_socket(int sk, const char *device,
				 void *addr, size_t addr_sz, bool async);
extern int __test_listen_socket(int backlog, void *addr, size_t addr_sz);

static inline int test_listen_socket(const union tcp_addr taddr,
				     unsigned int port, int backlog)
{
	sockaddr_af addr;

	tcp_addr_to_sockaddr_in(&addr, &taddr, htons(port));
	return __test_listen_socket(backlog, (void *)&addr, sizeof(addr));
}

/*
 * In order for selftests to work under CONFIG_CRYPTO_FIPS=y,
 * the password should be loger than 14 bytes, see hmac_setkey()
 */
#define TEST_TCP_AO_MINKEYLEN	14
#define DEFAULT_TEST_PASSWORD	"In this hour, I do not believe that any darkness will endure."

#ifndef DEFAULT_TEST_ALGO
#define DEFAULT_TEST_ALGO	"cmac(aes128)"
#endif

#ifdef IPV6_TEST
#define DEFAULT_TEST_PREFIX	128
#else
#define DEFAULT_TEST_PREFIX	32
#endif

/*
 * Timeout on syscalls where failure is not expected.
 * You may want to rise it if the test machine is very busy.
 */
#ifndef TEST_TIMEOUT_SEC
#define TEST_TIMEOUT_SEC	5
#endif

/*
 * Timeout on connect() where a failure is expected.
 * If set to 0 - kernel will try to retransmit SYN number of times, set in
 * /proc/sys/net/ipv4/tcp_syn_retries
 * By default set to 1 to make tests pass faster on non-busy machine.
 * [in process of removal, don't use in new tests]
 */
#ifndef TEST_RETRANSMIT_SEC
#define TEST_RETRANSMIT_SEC	1
#endif

static inline int _test_connect_socket(int sk, const union tcp_addr taddr,
				       unsigned int port, bool async)
{
	sockaddr_af addr;

	tcp_addr_to_sockaddr_in(&addr, &taddr, htons(port));
	return __test_connect_socket(sk, veth_name,
				     (void *)&addr, sizeof(addr), async);
}

static inline int test_connect_socket(int sk, const union tcp_addr taddr,
				      unsigned int port)
{
	return _test_connect_socket(sk, taddr, port, false);
}

extern int __test_set_md5(int sk, void *addr, size_t addr_sz,
			  uint8_t prefix, int vrf, const char *password);
static inline int test_set_md5(int sk, const union tcp_addr in_addr,
			       uint8_t prefix, int vrf, const char *password)
{
	sockaddr_af addr;

	if (prefix > DEFAULT_TEST_PREFIX)
		prefix = DEFAULT_TEST_PREFIX;

	tcp_addr_to_sockaddr_in(&addr, &in_addr, 0);
	return __test_set_md5(sk, (void *)&addr, sizeof(addr),
			prefix, vrf, password);
}

extern int test_prepare_key_sockaddr(struct tcp_ao_add *ao, const char *alg,
		void *addr, size_t addr_sz, bool set_current, bool set_rnext,
		uint8_t prefix, uint8_t vrf,
		uint8_t sndid, uint8_t rcvid, uint8_t maclen,
		uint8_t keyflags, uint8_t keylen, const char *key);

static inline int test_prepare_key(struct tcp_ao_add *ao,
		const char *alg, union tcp_addr taddr,
		bool set_current, bool set_rnext,
		uint8_t prefix, uint8_t vrf,
		uint8_t sndid, uint8_t rcvid, uint8_t maclen,
		uint8_t keyflags, uint8_t keylen, const char *key)
{
	sockaddr_af addr;

	tcp_addr_to_sockaddr_in(&addr, &taddr, 0);
	return test_prepare_key_sockaddr(ao, alg, (void *)&addr, sizeof(addr),
			set_current, set_rnext, prefix, vrf, sndid, rcvid,
			maclen, keyflags, keylen, key);
}

static inline int test_prepare_def_key(struct tcp_ao_add *ao,
		const char *key, uint8_t keyflags,
		union tcp_addr in_addr, uint8_t prefix, uint8_t vrf,
		uint8_t sndid, uint8_t rcvid)
{
	if (prefix > DEFAULT_TEST_PREFIX)
		prefix = DEFAULT_TEST_PREFIX;

	return test_prepare_key(ao, DEFAULT_TEST_ALGO, in_addr, false, false,
				prefix, vrf, sndid, rcvid, 0, keyflags,
				strlen(key), key);
}

extern int test_get_one_ao(int sk, struct tcp_ao_getsockopt *out,
			   void *addr, size_t addr_sz,
			   uint8_t prefix, uint8_t sndid, uint8_t rcvid);
extern int test_get_ao_info(int sk, struct tcp_ao_info_opt *out);
extern int test_set_ao_info(int sk, struct tcp_ao_info_opt *in);
extern int test_cmp_getsockopt_setsockopt(const struct tcp_ao_add *a,
					  const struct tcp_ao_getsockopt *b);
extern int test_cmp_getsockopt_setsockopt_ao(const struct tcp_ao_info_opt *a,
					     const struct tcp_ao_info_opt *b);

static inline int test_verify_socket_key(int sk, struct tcp_ao_add *key)
{
	struct tcp_ao_getsockopt key2 = {};
	int err;

	err = test_get_one_ao(sk, &key2, &key->addr, sizeof(key->addr),
			      key->prefix, key->sndid, key->rcvid);
	if (err)
		return err;

	return test_cmp_getsockopt_setsockopt(key, &key2);
}

static inline int test_add_key_vrf(int sk,
				   const char *key, uint8_t keyflags,
				   union tcp_addr in_addr, uint8_t prefix,
				   uint8_t vrf, uint8_t sndid, uint8_t rcvid)
{
	struct tcp_ao_add tmp = {};
	int err;

	err = test_prepare_def_key(&tmp, key, keyflags, in_addr, prefix,
				   vrf, sndid, rcvid);
	if (err)
		return err;

	err = setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &tmp, sizeof(tmp));
	if (err < 0)
		return -errno;

	return test_verify_socket_key(sk, &tmp);
}

static inline int test_add_key(int sk, const char *key,
			      union tcp_addr in_addr, uint8_t prefix,
			      uint8_t sndid, uint8_t rcvid)
{
	return test_add_key_vrf(sk, key, 0, in_addr, prefix, 0, sndid, rcvid);
}

static inline int test_verify_socket_ao(int sk, struct tcp_ao_info_opt *ao)
{
	struct tcp_ao_info_opt ao2 = {};
	int err;

	err = test_get_ao_info(sk, &ao2);
	if (err)
		return err;

	return test_cmp_getsockopt_setsockopt_ao(ao, &ao2);
}

static inline int test_set_ao_flags(int sk, bool ao_required, bool accept_icmps)
{
	struct tcp_ao_info_opt ao = {};
	int err;

	err = test_get_ao_info(sk, &ao);
	/* Maybe ao_info wasn't allocated yet */
	if (err && err != -ENOENT)
		return err;

	ao.ao_required = !!ao_required;
	ao.accept_icmps = !!accept_icmps;
	err = test_set_ao_info(sk, &ao);
	if (err)
		return err;

	return test_verify_socket_ao(sk, &ao);
}

extern ssize_t test_server_run(int sk, ssize_t quota, time_t timeout_sec);
extern int test_client_verify(int sk, const size_t msg_len, const size_t nr);

struct tcp_ao_key_counters {
	uint8_t sndid;
	uint8_t rcvid;
	uint64_t pkt_good;
	uint64_t pkt_bad;
};

struct tcp_ao_counters {
	/* per-netns */
	uint64_t netns_ao_good;
	uint64_t netns_ao_bad;
	uint64_t netns_ao_key_not_found;
	uint64_t netns_ao_required;
	uint64_t netns_ao_dropped_icmp;
	/* per-socket */
	uint64_t ao_info_pkt_good;
	uint64_t ao_info_pkt_bad;
	uint64_t ao_info_pkt_key_not_found;
	uint64_t ao_info_pkt_ao_required;
	uint64_t ao_info_pkt_dropped_icmp;
	/* per-key */
	size_t nr_keys;
	struct tcp_ao_key_counters *key_cnts;
};

struct tcp_counters {
	struct tcp_ao_counters ao;
	uint64_t netns_md5_notfound;
	uint64_t netns_md5_unexpected;
	uint64_t netns_md5_failure;
};

extern int test_get_tcp_counters(int sk, struct tcp_counters *out);

#define TEST_CNT_KEY_GOOD		BIT(0)
#define TEST_CNT_KEY_BAD		BIT(1)
#define TEST_CNT_SOCK_GOOD		BIT(2)
#define TEST_CNT_SOCK_BAD		BIT(3)
#define TEST_CNT_SOCK_KEY_NOT_FOUND	BIT(4)
#define TEST_CNT_SOCK_AO_REQUIRED	BIT(5)
#define TEST_CNT_SOCK_DROPPED_ICMP	BIT(6)
#define TEST_CNT_NS_GOOD		BIT(7)
#define TEST_CNT_NS_BAD			BIT(8)
#define TEST_CNT_NS_KEY_NOT_FOUND	BIT(9)
#define TEST_CNT_NS_AO_REQUIRED		BIT(10)
#define TEST_CNT_NS_DROPPED_ICMP	BIT(11)
#define TEST_CNT_NS_MD5_NOT_FOUND	BIT(12)
#define TEST_CNT_NS_MD5_UNEXPECTED	BIT(13)
#define TEST_CNT_NS_MD5_FAILURE		BIT(14)
typedef uint16_t test_cnt;

#define _for_each_counter(f)						\
do {									\
	/* per-netns */							\
	f(ao.netns_ao_good,		TEST_CNT_NS_GOOD);		\
	f(ao.netns_ao_bad,		TEST_CNT_NS_BAD);		\
	f(ao.netns_ao_key_not_found,	TEST_CNT_NS_KEY_NOT_FOUND);	\
	f(ao.netns_ao_required,		TEST_CNT_NS_AO_REQUIRED);	\
	f(ao.netns_ao_dropped_icmp,	TEST_CNT_NS_DROPPED_ICMP);	\
	/* per-socket */						\
	f(ao.ao_info_pkt_good,		TEST_CNT_SOCK_GOOD);		\
	f(ao.ao_info_pkt_bad,		TEST_CNT_SOCK_BAD);		\
	f(ao.ao_info_pkt_key_not_found,	TEST_CNT_SOCK_KEY_NOT_FOUND);	\
	f(ao.ao_info_pkt_ao_required,	TEST_CNT_SOCK_AO_REQUIRED);	\
	f(ao.ao_info_pkt_dropped_icmp,	TEST_CNT_SOCK_DROPPED_ICMP);	\
	/* non-AO */							\
	f(netns_md5_notfound,		TEST_CNT_NS_MD5_NOT_FOUND);	\
	f(netns_md5_unexpected,		TEST_CNT_NS_MD5_UNEXPECTED);	\
	f(netns_md5_failure,		TEST_CNT_NS_MD5_FAILURE);	\
} while (0)

#define TEST_CNT_AO_GOOD		(TEST_CNT_SOCK_GOOD | TEST_CNT_NS_GOOD)
#define TEST_CNT_AO_BAD			(TEST_CNT_SOCK_BAD | TEST_CNT_NS_BAD)
#define TEST_CNT_AO_KEY_NOT_FOUND	(TEST_CNT_SOCK_KEY_NOT_FOUND | \
					 TEST_CNT_NS_KEY_NOT_FOUND)
#define TEST_CNT_AO_REQUIRED		(TEST_CNT_SOCK_AO_REQUIRED | \
					 TEST_CNT_NS_AO_REQUIRED)
#define TEST_CNT_AO_DROPPED_ICMP	(TEST_CNT_SOCK_DROPPED_ICMP | \
					 TEST_CNT_NS_DROPPED_ICMP)
#define TEST_CNT_GOOD			(TEST_CNT_KEY_GOOD | TEST_CNT_AO_GOOD)
#define TEST_CNT_BAD			(TEST_CNT_KEY_BAD | TEST_CNT_AO_BAD)

extern test_cnt test_cmp_counters(struct tcp_counters *before,
				  struct tcp_counters *after);
extern int test_assert_counters_sk(const char *tst_name,
		struct tcp_counters *before, struct tcp_counters *after,
		test_cnt expected);
extern int test_assert_counters_key(const char *tst_name,
		struct tcp_ao_counters *before, struct tcp_ao_counters *after,
		test_cnt expected, int sndid, int rcvid);
extern void test_tcp_counters_free(struct tcp_counters *cnts);

/*
 * Polling for netns and socket counters during select()/connect() and also
 * client/server messaging. Instead of constant timeout on underlying select(),
 * check the counters and return early. This allows to pass the tests where
 * timeout is expected without waiting for that fixing timeout (tests speed-up).
 * Previously shorter timeouts were used for tests expecting to time out,
 * but that leaded to sporadic false positives on counter checks failures,
 * as one second timeouts aren't enough for TCP retransmit.
 *
 * Two sides of the socketpair (client/server) should synchronize failures
 * using a shared variable *err, so that they can detect the other side's
 * failure.
 */
extern int test_skpair_wait_poll(int sk, bool write, test_cnt cond,
				 volatile int *err);
extern int _test_skpair_connect_poll(int sk, const char *device,
				     void *addr, size_t addr_sz,
				     test_cnt cond, volatile int *err);
static inline int test_skpair_connect_poll(int sk, const union tcp_addr taddr,
					   unsigned int port,
					   test_cnt cond, volatile int *err)
{
	sockaddr_af addr;

	tcp_addr_to_sockaddr_in(&addr, &taddr, htons(port));
	return _test_skpair_connect_poll(sk, veth_name,
					 (void *)&addr, sizeof(addr), cond, err);
}

extern int test_skpair_client(int sk, const size_t msg_len, const size_t nr,
			      test_cnt cond, volatile int *err);
extern int test_skpair_server(int sk, ssize_t quota,
			      test_cnt cond, volatile int *err);

/*
 * Frees buffers allocated in test_get_tcp_counters().
 * The function doesn't expect new keys or keys removed between calls
 * to test_get_tcp_counters(). Check key counters manually if they
 * may change.
 */
static inline int test_assert_counters(const char *tst_name,
				       struct tcp_counters *before,
				       struct tcp_counters *after,
				       test_cnt expected)
{
	int ret;

	ret = test_assert_counters_sk(tst_name, before, after, expected);
	if (ret)
		goto out;
	ret = test_assert_counters_key(tst_name, &before->ao, &after->ao,
				       expected, -1, -1);
out:
	test_tcp_counters_free(before);
	test_tcp_counters_free(after);
	return ret;
}

struct netstat;
extern struct netstat *netstat_read(void);
extern void netstat_free(struct netstat *ns);
extern void netstat_print_diff(struct netstat *nsa, struct netstat *nsb);
extern uint64_t netstat_get(struct netstat *ns,
			    const char *name, bool *not_found);

static inline uint64_t netstat_get_one(const char *name, bool *not_found)
{
	struct netstat *ns = netstat_read();
	uint64_t ret;

	ret = netstat_get(ns, name, not_found);

	netstat_free(ns);
	return ret;
}

struct tcp_sock_queue {
	uint32_t seq;
	void *buf;
};

struct tcp_sock_state {
	struct tcp_info info;
	struct tcp_repair_window trw;
	struct tcp_sock_queue out;
	int outq_len;		/* output queue size (not sent + not acked) */
	int outq_nsd_len;	/* output queue size (not sent only) */
	struct tcp_sock_queue in;
	int inq_len;
	int mss;
	int timestamp;
};

extern void __test_sock_checkpoint(int sk, struct tcp_sock_state *state,
				   void *addr, size_t addr_size);
static inline void test_sock_checkpoint(int sk, struct tcp_sock_state *state,
					sockaddr_af *saddr)
{
	__test_sock_checkpoint(sk, state, saddr, sizeof(*saddr));
}
extern void test_ao_checkpoint(int sk, struct tcp_ao_repair *state);
extern void __test_sock_restore(int sk, const char *device,
				struct tcp_sock_state *state,
				void *saddr, void *daddr, size_t addr_size);
static inline void test_sock_restore(int sk, struct tcp_sock_state *state,
				     sockaddr_af *saddr,
				     const union tcp_addr daddr,
				     unsigned int dport)
{
	sockaddr_af addr;

	tcp_addr_to_sockaddr_in(&addr, &daddr, htons(dport));
	__test_sock_restore(sk, veth_name, state, saddr, &addr, sizeof(addr));
}
extern void test_ao_restore(int sk, struct tcp_ao_repair *state);
extern void test_sock_state_free(struct tcp_sock_state *state);
extern void test_enable_repair(int sk);
extern void test_disable_repair(int sk);
extern void test_kill_sk(int sk);
static inline int test_add_repaired_key(int sk,
					const char *key, uint8_t keyflags,
					union tcp_addr in_addr, uint8_t prefix,
					uint8_t sndid, uint8_t rcvid)
{
	struct tcp_ao_add tmp = {};
	int err;

	err = test_prepare_def_key(&tmp, key, keyflags, in_addr, prefix,
				   0, sndid, rcvid);
	if (err)
		return err;

	tmp.set_current = 1;
	tmp.set_rnext = 1;
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &tmp, sizeof(tmp)) < 0)
		return -errno;

	return test_verify_socket_key(sk, &tmp);
}

#define DEFAULT_FTRACE_BUFFER_KB	10000
#define DEFAULT_TRACER_LINES_ARR	200
struct test_ftracer;
extern uint64_t ns_cookie1, ns_cookie2;

enum ftracer_op {
	FTRACER_LINE_DISCARD = 0,
	FTRACER_LINE_PRESERVE,
	FTRACER_EXIT,
};

extern struct test_ftracer *create_ftracer(const char *name,
		enum ftracer_op (*process_line)(const char *line),
		void (*destructor)(struct test_ftracer *tracer),
		bool (*expecting_more)(void),
		size_t lines_buf_sz, size_t buffer_size_kb);
extern int setup_trace_event(struct test_ftracer *tracer,
			     const char *event, const char *filter);
extern void destroy_ftracer(struct test_ftracer *tracer);
extern const size_t tracer_get_savedlines_nr(struct test_ftracer *tracer);
extern const char **tracer_get_savedlines(struct test_ftracer *tracer);

enum trace_events {
	/* TCP_HASH_EVENT */
	TCP_HASH_BAD_HEADER = 0,
	TCP_HASH_MD5_REQUIRED,
	TCP_HASH_MD5_UNEXPECTED,
	TCP_HASH_MD5_MISMATCH,
	TCP_HASH_AO_REQUIRED,
	/* TCP_AO_EVENT */
	TCP_AO_HANDSHAKE_FAILURE,
	TCP_AO_WRONG_MACLEN,
	TCP_AO_MISMATCH,
	TCP_AO_KEY_NOT_FOUND,
	TCP_AO_RNEXT_REQUEST,
	/* TCP_AO_EVENT_SK */
	TCP_AO_SYNACK_NO_KEY,
	/* TCP_AO_EVENT_SNE */
	TCP_AO_SND_SNE_UPDATE,
	TCP_AO_RCV_SNE_UPDATE,
	__MAX_TRACE_EVENTS
};

extern int __trace_event_expect(enum trace_events type, int family,
				union tcp_addr src, union tcp_addr dst,
				int src_port, int dst_port, int L3index,
				int fin, int syn, int rst, int psh, int ack,
				int keyid, int rnext, int maclen, int sne);

static inline void trace_hash_event_expect(enum trace_events type,
				union tcp_addr src, union tcp_addr dst,
				int src_port, int dst_port, int L3index,
				int fin, int syn, int rst, int psh, int ack)
{
	int err;

	err = __trace_event_expect(type, TEST_FAMILY, src, dst,
				   src_port, dst_port, L3index,
				   fin, syn, rst, psh, ack,
				   -1, -1, -1, -1);
	if (err)
		test_error("Couldn't add a trace event: %d", err);
}

static inline void trace_ao_event_expect(enum trace_events type,
				union tcp_addr src, union tcp_addr dst,
				int src_port, int dst_port, int L3index,
				int fin, int syn, int rst, int psh, int ack,
				int keyid, int rnext, int maclen)
{
	int err;

	err = __trace_event_expect(type, TEST_FAMILY, src, dst,
				   src_port, dst_port, L3index,
				   fin, syn, rst, psh, ack,
				   keyid, rnext, maclen, -1);
	if (err)
		test_error("Couldn't add a trace event: %d", err);
}

static inline void trace_ao_event_sk_expect(enum trace_events type,
				union tcp_addr src, union tcp_addr dst,
				int src_port, int dst_port,
				int keyid, int rnext)
{
	int err;

	err = __trace_event_expect(type, TEST_FAMILY, src, dst,
				   src_port, dst_port, -1,
				   -1, -1, -1, -1, -1,
				   keyid, rnext, -1, -1);
	if (err)
		test_error("Couldn't add a trace event: %d", err);
}

static inline void trace_ao_event_sne_expect(enum trace_events type,
				union tcp_addr src, union tcp_addr dst,
				int src_port, int dst_port, int sne)
{
	int err;

	err = __trace_event_expect(type, TEST_FAMILY, src, dst,
				   src_port, dst_port, -1,
				   -1, -1, -1, -1, -1,
				   -1, -1, -1, sne);
	if (err)
		test_error("Couldn't add a trace event: %d", err);
}

extern int setup_aolib_ftracer(void);

#endif /* _AOLIB_H_ */
