/* $FreeBSD$ */

#ifndef res_private_h
#define res_private_h

struct __res_state_ext {
	union res_sockaddr_union nsaddrs[MAXNS];
	struct sort_list {
		int     af;
		union {
			struct in_addr  ina;
			struct in6_addr in6a;
		} addr, mask;
	} sort_list[MAXRESOLVSORT];
	char nsuffix[64];
	char nsuffix2[64];
	struct timespec	conf_mtim;	/* mod time of loaded resolv.conf */
	time_t		conf_stat;	/* time of last stat(resolv.conf) */
	u_short	reload_period;		/* seconds between stat(resolv.conf) */
};

extern int
res_ourserver_p(const res_state statp, const struct sockaddr *sa);

#endif

/*! \file */
