/* SPDX-License-Identifier: GPL-2.0 */

#if !defined(_TRACE_NET_PROBE_COMMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NET_PROBE_COMMON_H

#define TP_STORE_ADDR_PORTS_V4(__entry, inet, sk)			\
	do {								\
		struct sockaddr_in *v4 = (void *)__entry->saddr;	\
									\
		v4->sin_family = AF_INET;				\
		v4->sin_port = inet->inet_sport;			\
		v4->sin_addr.s_addr = inet->inet_saddr;			\
		v4 = (void *)__entry->daddr;				\
		v4->sin_family = AF_INET;				\
		v4->sin_port = inet->inet_dport;			\
		v4->sin_addr.s_addr = inet->inet_daddr;			\
	} while (0)

#if IS_ENABLED(CONFIG_IPV6)

#define TP_STORE_ADDR_PORTS(__entry, inet, sk)				\
	do {								\
		if (sk->sk_family == AF_INET6) {			\
			struct sockaddr_in6 *v6 = (void *)__entry->saddr; \
									\
			v6->sin6_family = AF_INET6;			\
			v6->sin6_port = inet->inet_sport;		\
			v6->sin6_addr = inet6_sk(sk)->saddr;		\
			v6 = (void *)__entry->daddr;			\
			v6->sin6_family = AF_INET6;			\
			v6->sin6_port = inet->inet_dport;		\
			v6->sin6_addr = sk->sk_v6_daddr;		\
		} else							\
			TP_STORE_ADDR_PORTS_V4(__entry, inet, sk);	\
	} while (0)

#else

#define TP_STORE_ADDR_PORTS(__entry, inet, sk)		\
	TP_STORE_ADDR_PORTS_V4(__entry, inet, sk);

#endif

#define TP_STORE_V4MAPPED(__entry, saddr, daddr)		\
	do {							\
		struct in6_addr *pin6;				\
								\
		pin6 = (struct in6_addr *)__entry->saddr_v6;	\
		ipv6_addr_set_v4mapped(saddr, pin6);		\
		pin6 = (struct in6_addr *)__entry->daddr_v6;	\
		ipv6_addr_set_v4mapped(daddr, pin6);		\
	} while (0)

#if IS_ENABLED(CONFIG_IPV6)
#define TP_STORE_ADDRS(__entry, saddr, daddr, saddr6, daddr6)		\
	do {								\
		if (sk->sk_family == AF_INET6) {			\
			struct in6_addr *pin6;				\
									\
			pin6 = (struct in6_addr *)__entry->saddr_v6;	\
			*pin6 = saddr6;					\
			pin6 = (struct in6_addr *)__entry->daddr_v6;	\
			*pin6 = daddr6;					\
		} else {						\
			TP_STORE_V4MAPPED(__entry, saddr, daddr);	\
		}							\
	} while (0)
#else
#define TP_STORE_ADDRS(__entry, saddr, daddr, saddr6, daddr6)	\
	TP_STORE_V4MAPPED(__entry, saddr, daddr)
#endif

#endif
