/*
 * nsd.c -- nsd(8)
 *
 * Copyright (c) 2001-2024, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_GRP_H
#include <grp.h>
#endif /* HAVE_GRP_H */
#ifdef HAVE_SETUSERCONTEXT
#ifdef HAVE_LOGIN_CAP_H
#include <login_cap.h>
#endif /* HAVE_LOGIN_CAP_H */
#endif /* HAVE_SETUSERCONTEXT */
#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif

#include "nsd.h"
#include "options.h"
#include "tsig.h"
#include "remote.h"
#include "xfrd-disk.h"
#include "ipc.h"
#include "util.h"
#ifdef USE_METRICS
#include "metrics.h"
#endif /* USE_METRICS */
#ifdef USE_DNSTAP
#include "dnstap/dnstap_collector.h"
#endif
#ifdef USE_XDP
#include <sys/prctl.h>
#include "xdp-server.h"
#include "xdp-util.h"
#endif
#include "util/proxy_protocol.h"

/* The server handler... */
struct nsd nsd;
static char hostname[MAXHOSTNAMELEN];
extern config_parser_state_type* cfg_parser;
static void version(void) ATTR_NORETURN;

/*
 * Print the help text.
 *
 */
static void
usage (void)
{
	fprintf(stderr, "Usage: nsd [OPTION]...\n");
	fprintf(stderr, "Name Server Daemon.\n\n");
	fprintf(stderr,
		"Supported options:\n"
		"  -4                   Only listen to IPv4 connections.\n"
		"  -6                   Only listen to IPv6 connections.\n"
		"  -a ip-address[@port] Listen to the specified incoming IP address (and port)\n"
		"                       (May be specified multiple times).\n"
		"  -c configfile        Read specified configfile instead of %s.\n"
		"  -d                   do not fork as a daemon process.\n"
#ifndef NDEBUG
		"  -F facilities        Specify the debug facilities.\n"
#endif /* NDEBUG */
		"  -h                   Print this help information.\n"
		, CONFIGFILE);
	fprintf(stderr,
		"  -i identity          Specify the identity when queried for id.server CHAOS TXT.\n"
		"  -I nsid              Specify the NSID. This must be a hex string.\n"
#ifndef NDEBUG
		"  -L level             Specify the debug level.\n"
#endif /* NDEBUG */
		"  -l filename          Specify the log file.\n"
		"  -N server-count      The number of servers to start.\n"
		"  -n tcp-count         The maximum number of TCP connections per server.\n"
		"  -P pidfile           Specify the PID file to write.\n"
		"  -p port              Specify the port to listen to.\n"
		"  -s seconds           Dump statistics every SECONDS seconds.\n"
		"  -t chrootdir         Change root to specified directory on startup.\n"
		);
	fprintf(stderr,
		"  -u user              Change effective uid to the specified user.\n"
		"  -V level             Specify verbosity level.\n"
		"  -v                   Print version information.\n"
		);
	fprintf(stderr, "Version %s. Report bugs to <%s>.\n",
		PACKAGE_VERSION, PACKAGE_BUGREPORT);
}

/*
 * Print the version exit.
 *
 */
static void
version(void)
{
	fprintf(stderr, "%s version %s\n", PACKAGE_NAME, PACKAGE_VERSION);
	fprintf(stderr, "Written by NLnet Labs.\n\n");
	fprintf(stderr, "Configure line: %s\n", CONFCMDLINE);
#ifdef USE_MINI_EVENT
	fprintf(stderr, "Event loop: internal (uses select)\n");
#else
#  if defined(HAVE_EV_LOOP) || defined(HAVE_EV_DEFAULT_LOOP)
	fprintf(stderr, "Event loop: %s %s (uses %s)\n",
		"libev",
		nsd_event_vs(),
		nsd_event_method());
#  else
	fprintf(stderr, "Event loop: %s %s (uses %s)\n",
		"libevent",
		nsd_event_vs(),
		nsd_event_method());
#  endif
#endif
#ifdef HAVE_SSL
	fprintf(stderr, "Linked with %s\n\n",
#  ifdef SSLEAY_VERSION
		SSLeay_version(SSLEAY_VERSION)
#  else
		OpenSSL_version(OPENSSL_VERSION)
#  endif
		);
#endif
	fprintf(stderr,
		"Copyright (C) 2001-2024 NLnet Labs.  This is free software.\n"
		"There is NO warranty; not even for MERCHANTABILITY or FITNESS\n"
		"FOR A PARTICULAR PURPOSE.\n");
	exit(0);
}

static void
setup_verifier_environment(void)
{
	size_t i;
	int ret, ip4, ip6;
	char *buf, host[NI_MAXHOST], serv[NI_MAXSERV];
	size_t size, cnt = 0;

	/* allocate large enough buffer to hold a list of all ip addresses.
	   ((" " + INET6_ADDRSTRLEN + "@" + "65535") * n) + "\0" */
	size = ((INET6_ADDRSTRLEN + 1 + 5 + 1) * nsd.verify_ifs) + 1;
	buf = xalloc(size);

	ip4 = ip6 = 0;
	for(i = 0; i < nsd.verify_ifs; i++) {
		ret = getnameinfo(
			(struct sockaddr *)&nsd.verify_udp[i].addr.ai_addr,
			nsd.verify_udp[i].addr.ai_addrlen,
			host, sizeof(host), serv, sizeof(serv),
			NI_NUMERICHOST | NI_NUMERICSERV);
		if(ret != 0) {
			log_msg(LOG_ERR, "error in getnameinfo: %s",
				gai_strerror(ret));
			continue;
		}
		buf[cnt++] = ' ';
		cnt += strlcpy(&buf[cnt], host, size - cnt);
		assert(cnt < size);
		buf[cnt++] = '@';
		cnt += strlcpy(&buf[cnt], serv, size - cnt);
		assert(cnt < size);
#ifdef INET6
		if (nsd.verify_udp[i].addr.ai_family == AF_INET6 && !ip6) {
			setenv("VERIFY_IPV6_ADDRESS", host, 1);
			setenv("VERIFY_IPV6_PORT", serv, 1);
			setenv("VERIFY_IP_ADDRESS", host, 1);
			setenv("VERIFY_PORT", serv, 1);
			ip6 = 1;
		} else
#endif
		if (!ip4) {
			assert(nsd.verify_udp[i].addr.ai_family == AF_INET);
			setenv("VERIFY_IPV4_ADDRESS", host, 1);
			setenv("VERIFY_IPV4_PORT", serv, 1);
			if (!ip6) {
				setenv("VERIFY_IP_ADDRESS", host, 1);
				setenv("VERIFY_PORT", serv, 1);
			}
			ip4 = 1;
		}
	}

	setenv("VERIFY_IP_ADDRESSES", &buf[1], 1);
	free(buf);
}

static void
copyaddrinfo(struct nsd_addrinfo *dest, struct addrinfo *src)
{
	dest->ai_flags = src->ai_flags;
	dest->ai_family = src->ai_family;
	dest->ai_socktype = src->ai_socktype;
	dest->ai_addrlen = src->ai_addrlen;
	memcpy(&dest->ai_addr, src->ai_addr, src->ai_addrlen);
}

static void
setup_socket(
	struct nsd_socket *sock, const char *node, const char *port,
	struct addrinfo *hints)
{
	int ret;
	char *host;
	char host_buf[sizeof("65535") + INET6_ADDRSTRLEN + 1 /* '\0' */];
	const char *service;
	struct addrinfo *addr = NULL;

	sock->fib = -1;
	if(node) {
		char *sep;

		if (strlcpy(host_buf, node, sizeof(host_buf)) >= sizeof(host_buf)) {
			error("cannot parse address '%s': %s", node,
			    strerror(ENAMETOOLONG));
		}

		host = host_buf;
		sep = strchr(host_buf, '@');
		if(sep != NULL) {
			*sep = '\0';
			service = sep + 1;
		} else {
			service = port;
		}
	} else {
		host = NULL;
		service = port;
	}

	if((ret = getaddrinfo(host, service, hints, &addr)) == 0) {
		copyaddrinfo(&sock->addr, addr);
		freeaddrinfo(addr);
	} else {
		error("cannot parse address '%s': getaddrinfo: %s %s",
		      host ? host : "(null)",
		      gai_strerror(ret),
		      ret==EAI_SYSTEM ? strerror(errno) : "");
	}
}

static void
figure_socket_servers(
	struct nsd_socket *sock, struct ip_address_option *ip)
{
	int i;
	struct range_option *server;

	sock->servers = xalloc_zero(nsd_bitset_size(nsd.child_count));
	region_add_cleanup(nsd.region, free, sock->servers);
	nsd_bitset_init(sock->servers, nsd.child_count);

	if(!ip || !ip->servers) {
		/* every server must listen on this socket */
		for(i = 0; i < (int)nsd.child_count; i++) {
			nsd_bitset_set(sock->servers, i);
		}
		return;
	}

	/* only specific servers must listen on this socket */
	for(server = ip->servers; server; server = server->next) {
		if(server->first == server->last) {
			if(server->first <= 0) {
				error("server %d specified for ip-address %s "
				      "is invalid; server ranges are 1-based",
				      server->first, ip->address);
			} else if(server->last > (int)nsd.child_count) {
				error("server %d specified for ip-address %s "
				      "exceeds number of servers configured "
				      "in server-count",
				      server->first, ip->address);
			}
		} else {
			/* parse_range must ensure range itself is valid */
			assert(server->first < server->last);
			if(server->first <= 0) {
				error("server range %d-%d specified for "
				      "ip-address %s is invalid; server "
				      "ranges are 1-based",
				      server->first, server->last, ip->address);
			} else if(server->last > (int)nsd.child_count) {
				error("server range %d-%d specified for "
				      "ip-address %s exceeds number of servers "
				      "configured in server-count",
				      server->first, server->last, ip->address);
			}
		}
		for(i = server->first - 1; i < server->last; i++) {
			nsd_bitset_set(sock->servers, i);
		}
	}
}

static void
figure_default_sockets(
	struct nsd_socket **udp, struct nsd_socket **tcp, size_t *ifs,
	const char *node, const char *udp_port, const char *tcp_port,
	const struct addrinfo *hints)
{
	size_t i = 0, n = 1;
	struct addrinfo ai[2] = { *hints, *hints };

	assert(udp != NULL);
	assert(tcp != NULL);
	assert(ifs != NULL);

	ai[0].ai_socktype = SOCK_DGRAM;
	ai[1].ai_socktype = SOCK_STREAM;

#ifdef INET6
#ifdef IPV6_V6ONLY
	if (hints->ai_family == AF_UNSPEC) {
		ai[0].ai_family = AF_INET6;
		ai[1].ai_family = AF_INET6;
		n++;
	}
#endif /* IPV6_V6ONLY */
#endif /* INET6 */

	*udp = xalloc_zero((n + 1) * sizeof(struct nsd_socket));
	*tcp = xalloc_zero((n + 1) * sizeof(struct nsd_socket));
	region_add_cleanup(nsd.region, free, *udp);
	region_add_cleanup(nsd.region, free, *tcp);

#ifdef INET6
	if(hints->ai_family == AF_UNSPEC) {
		/*
		 * With IPv6 we'd like to open two separate sockets, one for
		 * IPv4 and one for IPv6, both listening to the wildcard
		 * address (unless the -4 or -6 flags are specified).
		 *
		 * However, this is only supported on platforms where we can
		 * turn the socket option IPV6_V6ONLY _on_. Otherwise we just
		 * listen to a single IPv6 socket and any incoming IPv4
		 * connections will be automatically mapped to our IPv6
		 * socket.
		 */
#ifdef IPV6_V6ONLY
		int r;
		struct addrinfo *addrs[2] = { NULL, NULL };

		if((r = getaddrinfo(node, udp_port, &ai[0], &addrs[0])) == 0 &&
		   (r = getaddrinfo(node, tcp_port, &ai[1], &addrs[1])) == 0)
		{
			(*udp)[i].flags |= NSD_SOCKET_IS_OPTIONAL;
			(*udp)[i].fib = -1;
			copyaddrinfo(&(*udp)[i].addr, addrs[0]);
			figure_socket_servers(&(*udp)[i], NULL);
			(*tcp)[i].flags |= NSD_SOCKET_IS_OPTIONAL;
			(*tcp)[i].fib = -1;
			copyaddrinfo(&(*tcp)[i].addr, addrs[1]);
			figure_socket_servers(&(*tcp)[i], NULL);
			i++;
		} else {
			log_msg(LOG_WARNING, "No IPv6, fallback to IPv4. getaddrinfo: %s",
			  r == EAI_SYSTEM ? strerror(errno) : gai_strerror(r));
		}

		if(addrs[0])
			freeaddrinfo(addrs[0]);
		if(addrs[1])
			freeaddrinfo(addrs[1]);

		ai[0].ai_family = AF_INET;
		ai[1].ai_family = AF_INET;
#endif /* IPV6_V6ONLY */
	}
#endif /* INET6 */

	*ifs = i + 1;
	setup_socket(&(*udp)[i], node, udp_port, &ai[0]);
	figure_socket_servers(&(*udp)[i], NULL);
	setup_socket(&(*tcp)[i], node, tcp_port, &ai[1]);
	figure_socket_servers(&(*tcp)[i], NULL);
}

#ifdef HAVE_GETIFADDRS
static int
find_device(
	struct nsd_socket *sock,
	const struct ifaddrs *ifa)
{
	for(; ifa != NULL; ifa = ifa->ifa_next) {
		if((ifa->ifa_addr == NULL) ||
		   (ifa->ifa_addr->sa_family != sock->addr.ai_family) ||
		   ((ifa->ifa_flags & IFF_UP) == 0 ||
		    (ifa->ifa_flags & IFF_LOOPBACK) != 0 ||
		    (ifa->ifa_flags & IFF_RUNNING) == 0))
		{
			continue;
		}

#ifdef INET6
		if(ifa->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6 *sa1, *sa2;
			size_t sz = sizeof(struct in6_addr);
			sa1 = (struct sockaddr_in6 *)ifa->ifa_addr;
			sa2 = (struct sockaddr_in6 *)&sock->addr.ai_addr;
			if(memcmp(&sa1->sin6_addr, &sa2->sin6_addr, sz) == 0) {
				break;
			}
		} else
#endif
		if(ifa->ifa_addr->sa_family == AF_INET) {
			struct sockaddr_in *sa1, *sa2;
			sa1 = (struct sockaddr_in *)ifa->ifa_addr;
			sa2 = (struct sockaddr_in *)&sock->addr.ai_addr;
			if(sa1->sin_addr.s_addr == sa2->sin_addr.s_addr) {
				break;
			}
		}
	}

	if(ifa != NULL) {
		size_t len = strlcpy(sock->device, ifa->ifa_name, sizeof(sock->device));
		if(len < sizeof(sock->device)) {
			char *colon = strchr(sock->device, ':');
			if(colon != NULL)
				*colon = '\0';
			return 1;
		}
	}

	return 0;
}
#endif /* HAVE_GETIFADDRS */

static void
figure_sockets(
	struct nsd_socket **udp, struct nsd_socket **tcp, size_t *ifs,
	struct ip_address_option *ips,
	const char *node, const char *udp_port, const char *tcp_port,
	const struct addrinfo *hints)
{
	size_t i = 0;
	struct addrinfo ai = *hints;
	struct ip_address_option *ip;
#ifdef HAVE_GETIFADDRS
	struct ifaddrs *ifa = NULL;
#endif
	int bind_device = 0;

	if(!ips) {
		figure_default_sockets(
			udp, tcp, ifs, node, udp_port, tcp_port, hints);
		return;
	}

	*ifs = 0;
	for(ip = ips; ip; ip = ip->next) {
		(*ifs)++;
		bind_device |= (ip->dev != 0);
	}

#ifdef HAVE_GETIFADDRS
	if(bind_device && getifaddrs(&ifa) == -1) {
		error("getifaddrs failed: %s", strerror(errno));
	}
#endif

	*udp = xalloc_zero((*ifs + 1) * sizeof(struct nsd_socket));
	*tcp = xalloc_zero((*ifs + 1) * sizeof(struct nsd_socket));
	region_add_cleanup(nsd.region, free, *udp);
	region_add_cleanup(nsd.region, free, *tcp);

	ai.ai_flags |= AI_NUMERICHOST;
	for(ip = ips, i = 0; ip; ip = ip->next, i++) {
		ai.ai_socktype = SOCK_DGRAM;
		setup_socket(&(*udp)[i], ip->address, udp_port, &ai);
		figure_socket_servers(&(*udp)[i], ip);
		ai.ai_socktype = SOCK_STREAM;
		setup_socket(&(*tcp)[i], ip->address, tcp_port, &ai);
		figure_socket_servers(&(*tcp)[i], ip);
		if(ip->fib != -1) {
			(*udp)[i].fib = ip->fib;
			(*tcp)[i].fib = ip->fib;
		}
#ifdef HAVE_GETIFADDRS
		if(ip->dev != 0) {
			(*udp)[i].flags |= NSD_BIND_DEVICE;
			(*tcp)[i].flags |= NSD_BIND_DEVICE;
			if(ifa != NULL && (find_device(&(*udp)[i], ifa) == 0 ||
			                   find_device(&(*tcp)[i], ifa) == 0))
			{
				error("cannot find device for ip-address %s",
				      ip->address);
			}
		}
#endif
	}

	assert(i == *ifs);

#ifdef HAVE_GETIFADDRS
	if(ifa != NULL) {
		freeifaddrs(ifa);
	}
#endif
}

static void
print_sockets(
	struct nsd_socket *udp, struct nsd_socket *tcp, size_t ifs)
{
#define SERVERBUF_SIZE_MAX (999*4+1) /* assume something big */
	char sockbuf[INET6_ADDRSTRLEN + 6 + 1];
	char serverbuf[SERVERBUF_SIZE_MAX];
	size_t i, serverbufsz, servercnt;
	const char *fmt = "listen on ip-address %s (%s) with server(s): %s";
	struct nsd_bitset *servers;

	if(ifs == 0) {
		return;
	}

	assert(udp != NULL);
	assert(tcp != NULL);

	servercnt = udp[0].servers->size;
	serverbufsz = SERVERBUF_SIZE_MAX;

	/* warn user of unused servers */
	servers = xalloc(nsd_bitset_size(servercnt));
	nsd_bitset_init(servers, (size_t)servercnt);

	for(i = 0; i < ifs; i++) {
		assert(udp[i].servers->size == servercnt);
		addrport2str((void*)&udp[i].addr.ai_addr, sockbuf, sizeof(sockbuf));
		(void)print_socket_servers(udp[i].servers, serverbuf, serverbufsz);
		nsd_bitset_or(servers, servers, udp[i].servers);
		VERBOSITY(3, (LOG_NOTICE, fmt, sockbuf, "udp", serverbuf));
		assert(tcp[i].servers->size == servercnt);
		addrport2str((void*)&tcp[i].addr.ai_addr, sockbuf, sizeof(sockbuf));
		(void)print_socket_servers(tcp[i].servers, serverbuf, serverbufsz);
		nsd_bitset_or(servers, servers, tcp[i].servers);
		VERBOSITY(3, (LOG_NOTICE, fmt, sockbuf, "tcp", serverbuf));
	}


	/* warn user of unused servers */
	for(i = 0; i < servercnt; i++) {
		if(!nsd_bitset_isset(servers, i)) {
			log_msg(LOG_WARNING, "server %zu will not listen on "
			                     "any specified ip-address", i+1);
		}
	}
	free(servers);
}

#ifdef HAVE_CPUSET_T
static void free_cpuset(void *ptr)
{
	cpuset_t *set = (cpuset_t *)ptr;
	cpuset_destroy(set);
}
#endif

/*
 * Fetch the nsd parent process id from the nsd pidfile
 *
 */
pid_t
readpid(const char *file)
{
	int fd;
	pid_t pid;
	char pidbuf[16];
	char *t;
	int l;

	if ((fd = open(file, O_RDONLY)) == -1) {
		return -1;
	}

	if (((l = read(fd, pidbuf, sizeof(pidbuf)))) == -1) {
		int errno_bak = errno;
		close(fd);
		errno = errno_bak;
		return -1;
	}

	close(fd);

	/* Empty pidfile means no pidfile... */
	if (l == 0) {
		errno = ENOENT;
		return -1;
	}

	pid = (pid_t) strtol(pidbuf, &t, 10);

	if (*t && *t != '\n') {
		return -1;
	}
	return pid;
}

/*
 * Store the nsd parent process id in the nsd pidfile
 *
 */
int
writepid(struct nsd *nsd)
{
	int fd;
	char pidbuf[32];
	size_t count = 0;
	if(!nsd->pidfile || !nsd->pidfile[0])
		return 0;

	snprintf(pidbuf, sizeof(pidbuf), "%lu\n", (unsigned long) nsd->pid);

	if((fd = open(nsd->pidfile, O_WRONLY | O_CREAT | O_TRUNC
#ifdef O_NOFOLLOW
		| O_NOFOLLOW
#endif
		, 0644)) == -1) {
		log_msg(LOG_ERR, "cannot open pidfile %s: %s",
			nsd->pidfile, strerror(errno));
		return -1;
	}

	while(count < strlen(pidbuf)) {
		ssize_t r = write(fd, pidbuf+count, strlen(pidbuf)-count);
		if(r == -1) {
			if(errno == EAGAIN || errno == EINTR)
				continue;
			log_msg(LOG_ERR, "cannot write pidfile %s: %s",
				nsd->pidfile, strerror(errno));
			close(fd);
			return -1;
		} else if(r == 0) {
			log_msg(LOG_ERR, "cannot write any bytes to "
				"pidfile %s: write returns 0 bytes written",
				nsd->pidfile);
			close(fd);
			return -1;
		}
		count += r;
	}
	close(fd);

	return 0;
}

void
unlinkpid(const char* file, const char* username)
{
	int fd = -1;

	if (file && file[0]) {
		/* truncate pidfile */
		fd = open(file, O_WRONLY | O_TRUNC
#ifdef O_NOFOLLOW
			| O_NOFOLLOW
#endif
			, 0644);
		if (fd == -1) {
			/* Truncate the pid file.  */
			/* If there is a username configured, we assume that
			 * due to privilege drops, NSD cannot truncate or
			 * unlink the pid file. NSD does not chown the file
			 * because that creates a privilege escape. */
			if(username && username[0]) {
				VERBOSITY(5, (LOG_INFO, "can not truncate the pid file %s: %s", file, strerror(errno)));
			} else {
				log_msg(LOG_ERR, "can not truncate the pid file %s: %s", file, strerror(errno));
			}
		} else {
			close(fd);
		}

		/* unlink pidfile */
		if (unlink(file) == -1) {
			/* this unlink may not work if the pidfile is located
			 * outside of the chroot/workdir or we no longer
			 * have permissions */
			if(username && username[0]) {
				VERBOSITY(5, (LOG_INFO,
					"failed to unlink pidfile %s: %s",
					file, strerror(errno)));
			} else {
				VERBOSITY(3, (LOG_WARNING,
					"failed to unlink pidfile %s: %s",
					file, strerror(errno)));
			}
		}
	}
}

/*
 * Incoming signals, set appropriate actions.
 *
 */
void
sig_handler(int sig)
{
	/* To avoid race cond. We really don't want to use log_msg() in this handler */

	/* Are we a child server? */
	if (nsd.server_kind != NSD_SERVER_MAIN) {
		switch (sig) {
		case SIGCHLD:
			nsd.signal_hint_child = 1;
			break;
		case SIGALRM:
			break;
		case SIGINT:
		case SIGTERM:
			nsd.signal_hint_quit = 1;
			break;
		case SIGILL:
		case SIGUSR1:	/* Dump stats on SIGUSR1.  */
			nsd.signal_hint_statsusr = 1;
			break;
		default:
			break;
		}
		return;
	}

	/* We are the main process */
	switch (sig) {
	case SIGCHLD:
		nsd.signal_hint_child = 1;
		return;
	case SIGHUP:
		nsd.signal_hint_reload_hup = 1;
		return;
	case SIGALRM:
		nsd.signal_hint_stats = 1;
		break;
	case SIGILL:
		/*
		 * For backwards compatibility with BIND 8 and older
		 * versions of NSD.
		 */
		nsd.signal_hint_statsusr = 1;
		break;
	case SIGUSR1:
		/* Dump statistics.  */
		nsd.signal_hint_statsusr = 1;
		break;
	case SIGINT:
	case SIGTERM:
	default:
		nsd.signal_hint_shutdown = 1;
		break;
	}
}

/*
 * Statistic output...
 *
 */
#ifdef BIND8_STATS
void
bind8_stats (struct nsd *nsd)
{
	char buf[MAXSYSLOGMSGLEN];
	char *msg, *t;
	int i, len;
	struct nsdst st;

	/* Current time... */
	time_t now;
	if(!nsd->st_period)
		return;
	time(&now);

	memcpy(&st, nsd->st, sizeof(st));
	stats_subtract(&st, &nsd->stat_proc);

	/* NSTATS */
	t = msg = buf + snprintf(buf, MAXSYSLOGMSGLEN, "NSTATS %lld %lu",
				 (long long) now, (unsigned long) st.boot);
	for (i = 0; i <= 255; i++) {
		/* How much space left? */
		if ((len = buf + MAXSYSLOGMSGLEN - t) < 32) {
			log_msg(LOG_INFO, "%s", buf);
			t = msg;
			len = buf + MAXSYSLOGMSGLEN - t;
		}

		if (st.qtype[i] != 0) {
			t += snprintf(t, len, " %s=%lu", rrtype_to_string(i), st.qtype[i]);
		}
	}
	if (t > msg)
		log_msg(LOG_INFO, "%s", buf);

	/* XSTATS */
	/* Only print it if we're in the main daemon or have anything to report... */
	if (nsd->server_kind == NSD_SERVER_MAIN
	    || st.dropped || st.raxfr || st.rixfr || (st.qudp + st.qudp6 - st.dropped)
	    || st.txerr || st.opcode[OPCODE_QUERY] || st.opcode[OPCODE_IQUERY]
	    || st.wrongzone || st.ctcp + st.ctcp6 || st.rcode[RCODE_SERVFAIL]
	    || st.rcode[RCODE_FORMAT] || st.nona || st.rcode[RCODE_NXDOMAIN]
	    || st.opcode[OPCODE_UPDATE]) {

		log_msg(LOG_INFO, "XSTATS %lld %lu"
			" RR=%lu RNXD=%lu RFwdR=%lu RDupR=%lu RFail=%lu RFErr=%lu RErr=%lu RAXFR=%lu RIXFR=%lu"
			" RLame=%lu ROpts=%lu SSysQ=%lu SAns=%lu SFwdQ=%lu SDupQ=%lu SErr=%lu RQ=%lu"
			" RIQ=%lu RFwdQ=%lu RDupQ=%lu RTCP=%lu SFwdR=%lu SFail=%lu SFErr=%lu SNaAns=%lu"
			" SNXD=%lu RUQ=%lu RURQ=%lu RUXFR=%lu RUUpd=%lu",
			(long long) now, (unsigned long) st.boot,
			st.dropped, (unsigned long)0, (unsigned long)0, (unsigned long)0, (unsigned long)0,
			(unsigned long)0, (unsigned long)0, st.raxfr, st.rixfr, (unsigned long)0, (unsigned long)0,
			(unsigned long)0, st.qudp + st.qudp6 - st.dropped, (unsigned long)0,
			(unsigned long)0, st.txerr,
			st.opcode[OPCODE_QUERY], st.opcode[OPCODE_IQUERY], st.wrongzone,
			(unsigned long)0, st.ctcp + st.ctcp6,
			(unsigned long)0, st.rcode[RCODE_SERVFAIL], st.rcode[RCODE_FORMAT],
			st.nona, st.rcode[RCODE_NXDOMAIN],
			(unsigned long)0, (unsigned long)0, (unsigned long)0, st.opcode[OPCODE_UPDATE]);
	}

}
#endif /* BIND8_STATS */

extern char *optarg;
extern int optind;

int
main(int argc, char *argv[])
{
	/* Scratch variables... */
	int c;
	pid_t	oldpid;
	size_t i;
	struct sigaction action;
#ifdef HAVE_GETPWNAM
	struct passwd *pwd = NULL;
#endif /* HAVE_GETPWNAM */

	struct ip_address_option *ip;
	struct addrinfo hints;
	const char *udp_port = 0;
	const char *tcp_port = 0;
	const char *verify_port = 0;

	const char *configfile = CONFIGFILE;

	char* argv0 = (argv0 = strrchr(argv[0], '/')) ? argv0 + 1 : argv[0];

	log_init(argv0);

	/* Initialize the server handler... */
	memset(&nsd, 0, sizeof(struct nsd));
	nsd.region      = region_create(xalloc, free);
	nsd.pidfile	= 0;
	nsd.server_kind = NSD_SERVER_MAIN;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = DEFAULT_AI_FAMILY;
	hints.ai_flags = AI_PASSIVE;
	nsd.identity	= 0;
	nsd.version	= VERSION;
	nsd.username	= 0;
	nsd.chrootdir	= 0;
	nsd.nsid 	= NULL;
	nsd.nsid_len 	= 0;
	nsd.do_answer_cookie = 0;
	nsd.cookie_count = 0;
	nsd.cookie_secrets_source = COOKIE_SECRETS_NONE;
	nsd.cookie_secrets_filename = NULL;

	nsd.child_count = 0;
	nsd.maximum_tcp_count = 0;
	nsd.current_tcp_count = 0;
	nsd.file_rotation_ok = 0;


	/* Set up our default identity to gethostname(2) */
	if (gethostname(hostname, MAXHOSTNAMELEN) == 0) {
		nsd.identity = hostname;
	} else {
		log_msg(LOG_ERR,
			"failed to get the host name: %s - using default identity",
			strerror(errno));
		nsd.identity = IDENTITY;
	}

	/* Create region where options will be stored and set defaults */
	nsd.options = nsd_options_create(region_create_custom(xalloc, free,
		DEFAULT_CHUNK_SIZE, DEFAULT_LARGE_OBJECT_SIZE,
		DEFAULT_INITIAL_CLEANUP_SIZE, 1));

	/* Parse the command line... */
	while ((c = getopt(argc, argv, "46a:c:df:hi:I:l:N:n:P:p:s:u:t:X:V:v"
#ifndef NDEBUG /* <mattthijs> only when configured with --enable-checking */
		"F:L:"
#endif /* NDEBUG */
		)) != -1) {
		switch (c) {
		case '4':
			hints.ai_family = AF_INET;
			break;
		case '6':
#ifdef INET6
			hints.ai_family = AF_INET6;
#else /* !INET6 */
			error("IPv6 support not enabled.");
#endif /* INET6 */
			break;
		case 'a':
			ip = region_alloc_zero(
				nsd.options->region, sizeof(*ip));
			ip->address = region_strdup(
				nsd.options->region, optarg);
			ip->next = nsd.options->ip_addresses;
			nsd.options->ip_addresses = ip;
			break;
		case 'c':
			configfile = optarg;
			break;
		case 'd':
			nsd.debug = 1;
			break;
		case 'f':
			break;
		case 'h':
			usage();
			exit(0);
		case 'i':
			nsd.identity = optarg;
			break;
		case 'I':
			if (nsd.nsid_len != 0) {
				/* can only be given once */
				break;
			}
			if (strncasecmp(optarg, "ascii_", 6) == 0) {
				nsd.nsid = xalloc(strlen(optarg+6));
				nsd.nsid_len = strlen(optarg+6);
				memmove(nsd.nsid, optarg+6, nsd.nsid_len);
			} else {
				if (strlen(optarg) % 2 != 0) {
					error("the NSID must be a hex string of an even length.");
				}
				nsd.nsid = xalloc(strlen(optarg) / 2);
				nsd.nsid_len = strlen(optarg) / 2;
				if (hex_pton(optarg, nsd.nsid, nsd.nsid_len) == -1) {
					error("hex string cannot be parsed '%s' in NSID.", optarg);
				}
			}
			break;
		case 'l':
			nsd.log_filename = optarg;
			break;
		case 'N':
			i = atoi(optarg);
			if (i <= 0) {
				error("number of child servers must be greater than zero.");
			} else {
				nsd.child_count = i;
			}
			break;
		case 'n':
			i = atoi(optarg);
			if (i <= 0) {
				error("number of concurrent TCP connections must greater than zero.");
			} else {
				nsd.maximum_tcp_count = i;
			}
			break;
		case 'P':
			nsd.pidfile = optarg;
			break;
		case 'p':
			if (atoi(optarg) == 0) {
				error("port argument must be numeric.");
			}
			tcp_port = optarg;
			udp_port = optarg;
			break;
		case 's':
#ifdef BIND8_STATS
			nsd.st_period = atoi(optarg);
#else /* !BIND8_STATS */
			error("BIND 8 statistics not enabled.");
#endif /* BIND8_STATS */
			break;
		case 't':
#ifdef HAVE_CHROOT
			nsd.chrootdir = optarg;
#else /* !HAVE_CHROOT */
			error("chroot not supported on this platform.");
#endif /* HAVE_CHROOT */
			break;
		case 'u':
			nsd.username = optarg;
			break;
		case 'V':
			verbosity = atoi(optarg);
			break;
		case 'v':
			version();
			/* version exits */
			break;
#ifndef NDEBUG
		case 'F':
			sscanf(optarg, "%x", &nsd_debug_facilities);
			break;
		case 'L':
			sscanf(optarg, "%d", &nsd_debug_level);
			break;
#endif /* NDEBUG */
		case '?':
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	/* argv += optind; */

	/* Commandline parse error */
	if (argc != 0) {
		usage();
		exit(1);
	}

	if (strlen(nsd.identity) > UCHAR_MAX) {
		error("server identity too long (%u characters)",
		      (unsigned) strlen(nsd.identity));
	}
	if(!tsig_init(nsd.region))
		error("init tsig failed");
	pp_init(&write_uint16, &write_uint32);

	/* Read options */
	if(!parse_options_file(nsd.options, configfile, NULL, NULL, NULL)) {
		error("could not read config: %s\n", configfile);
	}
	if(!parse_zone_list_file(nsd.options)) {
		error("could not read zonelist file %s\n",
			nsd.options->zonelistfile);
	}
	if(nsd.options->do_ip4 && !nsd.options->do_ip6) {
		hints.ai_family = AF_INET;
	}
#ifdef INET6
	if(nsd.options->do_ip6 && !nsd.options->do_ip4) {
		hints.ai_family = AF_INET6;
	}
#endif /* INET6 */
	if (verbosity == 0)
		verbosity = nsd.options->verbosity;
#ifndef NDEBUG
	if (nsd_debug_level > 0 && verbosity == 0)
		verbosity = nsd_debug_level;
#endif /* NDEBUG */
	if(nsd.options->debug_mode) nsd.debug=1;
	if(!nsd.pidfile)
	{
		if(nsd.options->pidfile)
			nsd.pidfile = nsd.options->pidfile;
		else
			nsd.pidfile = PIDFILE;
	}
	if(strcmp(nsd.identity, hostname)==0 || strcmp(nsd.identity,IDENTITY)==0)
	{
		if(nsd.options->identity)
			nsd.identity = nsd.options->identity;
	}
	if(nsd.options->version) {
		nsd.version = nsd.options->version;
	}
	if (nsd.options->logfile && !nsd.log_filename) {
		nsd.log_filename = nsd.options->logfile;
	}
	if(nsd.child_count == 0) {
		nsd.child_count = nsd.options->server_count;
	}

#ifdef USE_XDP
	/* make sure we will spawn enough servers to serve all queues of the
	 * selected network device, otherwise traffic to these queues will take
	 * the non-af_xdp path */
	if (nsd.options->xdp_interface) {
		int res = ethtool_channels_get(nsd.options->xdp_interface);
		if (res <= 0) {
			log_msg(LOG_ERR,
			        "xdp: could not determine netdev queue count: %s. "
			        "(attempting to continue with 1 queue)",
			        strerror(errno));
			nsd.xdp.xdp_server.queue_count = 1;
		} else {
			nsd.xdp.xdp_server.queue_count = res;
		}

		if (nsd.child_count < nsd.xdp.xdp_server.queue_count) {
			log_msg(LOG_NOTICE,
			        "xdp configured but server-count not high enough to serve "
			        "all netdev queues, raising server-count to %u",
			        nsd.xdp.xdp_server.queue_count);
			nsd.child_count = nsd.xdp.xdp_server.queue_count;
		}
	}
#endif

#ifdef SO_REUSEPORT
	if(nsd.options->reuseport && nsd.child_count > 1) {
		nsd.reuseport = nsd.child_count;
	}
#endif /* SO_REUSEPORT */
	if(nsd.maximum_tcp_count == 0) {
		nsd.maximum_tcp_count = nsd.options->tcp_count;
	}
	nsd.tcp_timeout = nsd.options->tcp_timeout;
	nsd.tcp_query_count = nsd.options->tcp_query_count;
	nsd.tcp_mss = nsd.options->tcp_mss;
	nsd.outgoing_tcp_mss = nsd.options->outgoing_tcp_mss;
	nsd.ipv4_edns_size = nsd.options->ipv4_edns_size;
	nsd.ipv6_edns_size = nsd.options->ipv6_edns_size;
#ifdef HAVE_SSL
	nsd.tls_ctx = NULL;
	nsd.tls_auth_ctx = NULL;
#endif

	if(udp_port == 0)
	{
		if(nsd.options->port != 0) {
			udp_port = nsd.options->port;
			tcp_port = nsd.options->port;
		} else {
			udp_port = UDP_PORT;
			tcp_port = TCP_PORT;
		}
	}
	if(nsd.options->verify_port != 0) {
		verify_port = nsd.options->verify_port;
	} else {
		verify_port = VERIFY_PORT;
	}
#ifdef BIND8_STATS
	if(nsd.st_period == 0) {
		nsd.st_period = nsd.options->statistics;
	}
#endif /* BIND8_STATS */
#ifdef HAVE_CHROOT
	if(nsd.chrootdir == 0) nsd.chrootdir = nsd.options->chroot;
#ifdef CHROOTDIR
	/* if still no chrootdir, fallback to default */
	if(nsd.chrootdir == 0) nsd.chrootdir = CHROOTDIR;
#endif /* CHROOTDIR */
#endif /* HAVE_CHROOT */
	if(nsd.username == 0) {
		if(nsd.options->username) nsd.username = nsd.options->username;
		else nsd.username = USER;
	}
	if(nsd.options->zonesdir && nsd.options->zonesdir[0]) {
		if(chdir(nsd.options->zonesdir)) {
			error("cannot chdir to '%s': %s",
				nsd.options->zonesdir, strerror(errno));
		}
		DEBUG(DEBUG_IPC,1, (LOG_INFO, "changed directory to %s",
			nsd.options->zonesdir));
	}

	/* EDNS0 */
	edns_init_data(&nsd.edns_ipv4, nsd.options->ipv4_edns_size);
#if defined(INET6)
#if defined(IPV6_USE_MIN_MTU) || defined(IPV6_MTU)
	edns_init_data(&nsd.edns_ipv6, nsd.options->ipv6_edns_size);
#else /* no way to set IPV6 MTU, send no bigger than that. */
	if (nsd.options->ipv6_edns_size < IPV6_MIN_MTU)
		edns_init_data(&nsd.edns_ipv6, nsd.options->ipv6_edns_size);
	else
		edns_init_data(&nsd.edns_ipv6, IPV6_MIN_MTU);
#endif /* IPV6 MTU) */
#endif /* defined(INET6) */

	reconfig_cookies(&nsd, nsd.options);

	if (nsd.nsid_len == 0 && nsd.options->nsid) {
		if (strlen(nsd.options->nsid) % 2 != 0) {
			error("the NSID must be a hex string of an even length.");
		}
		nsd.nsid = xalloc(strlen(nsd.options->nsid) / 2);
		nsd.nsid_len = strlen(nsd.options->nsid) / 2;
		if (hex_pton(nsd.options->nsid, nsd.nsid, nsd.nsid_len) == -1) {
			error("hex string cannot be parsed '%s' in NSID.", nsd.options->nsid);
		}
	}
	edns_init_nsid(&nsd.edns_ipv4, nsd.nsid_len);
#if defined(INET6)
	edns_init_nsid(&nsd.edns_ipv6, nsd.nsid_len);
#endif /* defined(INET6) */

#ifdef HAVE_CPUSET_T
	nsd.use_cpu_affinity = (nsd.options->cpu_affinity != NULL);
	if(nsd.use_cpu_affinity) {
		int ncpus;
		struct cpu_option* opt = nsd.options->cpu_affinity;

		if((ncpus = number_of_cpus()) == -1) {
			error("cannot retrieve number of cpus: %s",
			      strerror(errno));
		}
		nsd.cpuset = cpuset_create();
		region_add_cleanup(nsd.region, free_cpuset, nsd.cpuset);
		for(; opt; opt = opt->next) {
			assert(opt->cpu >= 0);
			if(opt->cpu >= ncpus) {
				error("invalid cpu %d specified in "
				      "cpu-affinity", opt->cpu);
			}
			cpuset_set((cpuid_t)opt->cpu, nsd.cpuset);
		}
	}
	if(nsd.use_cpu_affinity) {
		int cpu;
		struct cpu_map_option *opt
			= nsd.options->service_cpu_affinity;

		cpu = -1;
		for(; opt && cpu == -1; opt = opt->next) {
			if(opt->service == -1) {
				cpu = opt->cpu;
				assert(cpu >= 0);
			}
		}
		nsd.xfrd_cpuset = cpuset_create();
		region_add_cleanup(nsd.region, free_cpuset, nsd.xfrd_cpuset);
		if(cpu == -1) {
			cpuset_or(nsd.xfrd_cpuset,
			          nsd.cpuset);
		} else {
			if(!cpuset_isset(cpu, nsd.cpuset)) {
				error("cpu %d specified in xfrd-cpu-affinity "
				      "is not specified in cpu-affinity", cpu);
			}
			cpuset_set((cpuid_t)cpu, nsd.xfrd_cpuset);
		}
	}
#endif /* HAVE_CPUSET_T */

	/* Number of child servers to fork.  */
	nsd.children = (struct nsd_child *) region_alloc_array(
		nsd.region, nsd.child_count, sizeof(struct nsd_child));
	for (i = 0; i < nsd.child_count; ++i) {
		nsd.children[i].kind = NSD_SERVER_BOTH;
		nsd.children[i].pid = -1;
		nsd.children[i].child_fd = -1;
		nsd.children[i].parent_fd = -1;
		nsd.children[i].handler = NULL;
		nsd.children[i].need_to_send_STATS = 0;
		nsd.children[i].need_to_send_QUIT = 0;
		nsd.children[i].need_to_exit = 0;
		nsd.children[i].has_exited = 0;
#ifdef BIND8_STATS
		nsd.children[i].query_count = 0;
#endif

#ifdef HAVE_CPUSET_T
		if(nsd.use_cpu_affinity) {
			int cpu, server;
			struct cpu_map_option *opt
				= nsd.options->service_cpu_affinity;

			cpu = -1;
			server = i+1;
			for(; opt && cpu == -1; opt = opt->next) {
				if(opt->service == server) {
					cpu = opt->cpu;
					assert(cpu >= 0);
				}
			}
			nsd.children[i].cpuset = cpuset_create();
			region_add_cleanup(nsd.region,
			                   free_cpuset,
			                   nsd.children[i].cpuset);
			if(cpu == -1) {
				cpuset_or(nsd.children[i].cpuset,
				          nsd.cpuset);
			} else {
				if(!cpuset_isset((cpuid_t)cpu, nsd.cpuset)) {
					error("cpu %d specified in "
					      "server-%d-cpu-affinity is not "
					      "specified in cpu-affinity",
					      cpu, server);
				}
				cpuset_set(
					(cpuid_t)cpu, nsd.children[i].cpuset);
			}
		}
#endif /* HAVE_CPUSET_T */
	}

	nsd.this_child = NULL;

	resolve_interface_names(nsd.options);
	figure_sockets(&nsd.udp, &nsd.tcp, &nsd.ifs,
		nsd.options->ip_addresses, NULL, udp_port, tcp_port, &hints);

	if(nsd.options->verify_enable) {
		figure_sockets(&nsd.verify_udp, &nsd.verify_tcp, &nsd.verify_ifs,
			nsd.options->verify_ip_addresses, "localhost", verify_port, verify_port, &hints);
		setup_verifier_environment();
	}

	/* Parse the username into uid and gid */
	nsd.gid = getgid();
	nsd.uid = getuid();
#ifdef HAVE_GETPWNAM
	/* Parse the username into uid and gid */
	if (*nsd.username) {
		if (isdigit((unsigned char)*nsd.username)) {
			char *t;
			nsd.uid = strtol(nsd.username, &t, 10);
			if (*t != 0) {
				if (*t != '.' || !isdigit((unsigned char)*++t)) {
					error("-u user or -u uid or -u uid.gid");
				}
				nsd.gid = strtol(t, &t, 10);
			} else {
				/* Lookup the group id in /etc/passwd */
				if ((pwd = getpwuid(nsd.uid)) == NULL) {
					error("user id %u does not exist.", (unsigned) nsd.uid);
				} else {
					nsd.gid = pwd->pw_gid;
				}
			}
		} else {
			/* Lookup the user id in /etc/passwd */
			if ((pwd = getpwnam(nsd.username)) == NULL) {
				error("user '%s' does not exist.", nsd.username);
			} else {
				nsd.uid = pwd->pw_uid;
				nsd.gid = pwd->pw_gid;
			}
		}
	}
	/* endpwent(); */
#endif /* HAVE_GETPWNAM */

#if defined(HAVE_SSL)
	key_options_tsig_add(nsd.options);
#endif

	append_trailing_slash(&nsd.options->xfrdir, nsd.options->region);
	/* Check relativity of pathnames to chroot */
	if (nsd.chrootdir && nsd.chrootdir[0]) {
		/* existing chrootdir: append trailing slash for strncmp checking */
		append_trailing_slash(&nsd.chrootdir, nsd.region);
		append_trailing_slash(&nsd.options->zonesdir, nsd.options->region);

		/* zonesdir must be absolute and within chroot,
		 * all other pathnames may be relative to zonesdir */
		if (strncmp(nsd.options->zonesdir, nsd.chrootdir, strlen(nsd.chrootdir)) != 0) {
			error("zonesdir %s has to be an absolute path that starts with the chroot path %s",
				nsd.options->zonesdir, nsd.chrootdir);
		} else if (!file_inside_chroot(nsd.pidfile, nsd.chrootdir)) {
			error("pidfile %s is not relative to %s: chroot not possible",
				nsd.pidfile, nsd.chrootdir);
		} else if (!file_inside_chroot(nsd.options->xfrdfile, nsd.chrootdir)) {
			error("xfrdfile %s is not relative to %s: chroot not possible",
				nsd.options->xfrdfile, nsd.chrootdir);
		} else if (!file_inside_chroot(nsd.options->zonelistfile, nsd.chrootdir)) {
			error("zonelistfile %s is not relative to %s: chroot not possible",
				nsd.options->zonelistfile, nsd.chrootdir);
		} else if (!file_inside_chroot(nsd.options->xfrdir, nsd.chrootdir)) {
			error("xfrdir %s is not relative to %s: chroot not possible",
				nsd.options->xfrdir, nsd.chrootdir);
		}
	}

	/* Set up the logging */
	log_open(LOG_PID, FACILITY, nsd.log_filename);
	if(nsd.options->log_only_syslog)
		log_set_log_function(log_only_syslog);
	else if (!nsd.log_filename)
		log_set_log_function(log_syslog);
	else if (nsd.uid && nsd.gid) {
		if(chown(nsd.log_filename, nsd.uid, nsd.gid) != 0)
			VERBOSITY(2, (LOG_WARNING, "chown %s failed: %s",
				nsd.log_filename, strerror(errno)));
	}
	log_msg(LOG_NOTICE, "%s starting (%s)", argv0, PACKAGE_STRING);

	/* Do we have a running nsd? */
	/* When there is a username configured, we assume that due to
	 * privilege drops, the pidfile could not be removed by NSD and
	 * as such could be lingering around. We could not remove it,
	 * and also not chown it as that creates privilege escape problems.
	 * The init system could remove the pidfile after use for us, but
	 * it is not sure if it is configured to do so. */
	if(nsd.pidfile && nsd.pidfile[0] &&
		!(nsd.username && nsd.username[0])) {
		if ((oldpid = readpid(nsd.pidfile)) == -1) {
			if (errno != ENOENT) {
				log_msg(LOG_ERR, "can't read pidfile %s: %s",
					nsd.pidfile, strerror(errno));
			}
		} else {
			if (kill(oldpid, 0) == 0 || errno == EPERM) {
				log_msg(LOG_WARNING,
					"%s is already running as %u, continuing",
					argv0, (unsigned) oldpid);
			} else {
				log_msg(LOG_ERR,
					"...stale pid file from process %u",
					(unsigned) oldpid);
			}
		}
	}

#ifdef HAVE_SETPROCTITLE
	setproctitle("main");
#endif
#ifdef HAVE_CPUSET_T
	if(nsd.use_cpu_affinity) {
		set_cpu_affinity(nsd.cpuset);
	}
#endif

#ifdef USE_XDP
	/* Set XDP config */
	nsd.xdp.xdp_server.region = nsd.region;
	nsd.xdp.xdp_server.interface_name = nsd.options->xdp_interface;
	nsd.xdp.xdp_server.bpf_prog_filename = nsd.options->xdp_program_path;
	nsd.xdp.xdp_server.bpf_prog_should_load = nsd.options->xdp_program_load;
	nsd.xdp.xdp_server.bpf_bpffs_path = nsd.options->xdp_bpffs_path;
	nsd.xdp.xdp_server.force_copy = nsd.options->xdp_force_copy;
	nsd.xdp.xdp_server.nsd = &nsd;

	if (!nsd.options->xdp_interface)
		log_msg(LOG_NOTICE, "XDP support is enabled, but not configured. Not using XDP.");

	/* moved xdp_server_init to after privilege drop to prevent
	 * problems with file ownership of bpf object pins. */
#endif

	print_sockets(nsd.udp, nsd.tcp, nsd.ifs);

	/* Setup the signal handling... */
	action.sa_handler = sig_handler;
	sigfillset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGHUP, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGILL, &action, NULL);
	sigaction(SIGUSR1, &action, NULL);
	sigaction(SIGALRM, &action, NULL);
	sigaction(SIGCHLD, &action, NULL);
	action.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &action, NULL);

	/* Initialize... */
	nsd.mode = NSD_RUN;
	nsd.signal_hint_child = 0;
	nsd.signal_hint_reload = 0;
	nsd.signal_hint_reload_hup = 0;
	nsd.signal_hint_quit = 0;
	nsd.signal_hint_shutdown = 0;
	nsd.signal_hint_stats = 0;
	nsd.signal_hint_statsusr = 0;
	nsd.quit_sync_done = 0;

	/* Initialize the server... */
	if (server_init(&nsd) != 0) {
		error("server initialization failed, %s could "
			"not be started", argv0);
	}
#if defined(HAVE_SSL)
	if(nsd.options->control_enable || (nsd.options->tls_service_key && nsd.options->tls_service_key[0])) {
		perform_openssl_init();
	}
#endif /* HAVE_SSL */
	if(nsd.options->control_enable) {
		/* read ssl keys while superuser and outside chroot */
		if(!(nsd.rc = daemon_remote_create(nsd.options)))
			error("could not perform remote control setup");
	}
#ifdef USE_METRICS
	if(nsd.options->metrics_enable) {
		if(!(nsd.metrics = daemon_metrics_create(nsd.options)))
			error("could not perform metrics server setup");
	}
#endif /* USE_METRICS */
#if defined(HAVE_SSL)
	if(nsd.options->tls_service_key && nsd.options->tls_service_key[0]
	   && nsd.options->tls_service_pem && nsd.options->tls_service_pem[0]) {
		/* normal tls port with no client authentication */
		if(!(nsd.tls_ctx = server_tls_ctx_create(&nsd, NULL,
		     nsd.options->tls_service_ocsp)))
			error("could not set up tls SSL_CTX");
		/* tls-auth port with required client authentication */
		if(nsd.options->tls_auth_port) {
			if(!(nsd.tls_auth_ctx = server_tls_ctx_create(&nsd,
			     (char*)nsd.options->tls_cert_bundle,
			     nsd.options->tls_service_ocsp)))
				error("could not set up tls SSL_CTX");
		}
	}
#endif /* HAVE_SSL */

	/* Unless we're debugging, fork... */
	if (!nsd.debug) {
		int fd;

		/* Take off... */
		switch (fork()) {
		case 0:
			/* Child */
			break;
		case -1:
			error("fork() failed: %s", strerror(errno));
			break;
		default:
			/* Parent is done */
			server_close_all_sockets(nsd.udp, nsd.ifs);
			server_close_all_sockets(nsd.tcp, nsd.ifs);
			exit(0);
		}

		/* Detach ourselves... */
		if (setsid() == -1) {
			error("setsid() failed: %s", strerror(errno));
		}

		if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
			(void)dup2(fd, STDIN_FILENO);
			(void)dup2(fd, STDOUT_FILENO);
			(void)dup2(fd, STDERR_FILENO);
			if (fd > 2)
				(void)close(fd);
		}
	}

	/* Get our process id */
	nsd.pid = getpid();

	/* Set user context */
#ifdef HAVE_GETPWNAM
	if (*nsd.username) {
#ifdef HAVE_SETUSERCONTEXT
		/* setusercontext does initgroups, setuid, setgid, and
		 * also resource limits from login config, but we
		 * still call setresuid, setresgid to be sure to set all uid */
		if (setusercontext(NULL, pwd, nsd.uid,
			LOGIN_SETALL & ~LOGIN_SETUSER & ~LOGIN_SETGROUP) != 0)
			log_msg(LOG_WARNING, "unable to setusercontext %s: %s",
				nsd.username, strerror(errno));
#endif /* HAVE_SETUSERCONTEXT */
	}
#endif /* HAVE_GETPWNAM */

	/* Chroot */
#ifdef HAVE_CHROOT
	if (nsd.chrootdir && nsd.chrootdir[0]) {
		int l = strlen(nsd.chrootdir)-1; /* ends in trailing slash */

		if (file_inside_chroot(nsd.log_filename, nsd.chrootdir))
			nsd.file_rotation_ok = 1;

		/* strip chroot from pathnames if they're absolute */
		nsd.options->zonesdir += l;
		if (nsd.log_filename){
			if (nsd.log_filename[0] == '/')
				nsd.log_filename += l;
		}
		if (nsd.pidfile && nsd.pidfile[0] == '/')
			nsd.pidfile += l;
		if (nsd.options->xfrdfile[0] == '/')
			nsd.options->xfrdfile += l;
		if (nsd.options->zonelistfile[0] == '/')
			nsd.options->zonelistfile += l;
		if (nsd.options->xfrdir[0] == '/')
			nsd.options->xfrdir += l;

		/* strip chroot from pathnames of "include:" statements
		 * on subsequent repattern commands */
		cfg_parser->chroot = nsd.chrootdir;

#ifdef HAVE_TZSET
		/* set timezone whilst not yet in chroot */
		tzset();
#endif
		if (chroot(nsd.chrootdir)) {
			error("unable to chroot: %s", strerror(errno));
		}
		if (chdir("/")) {
			error("unable to chdir to chroot: %s", strerror(errno));
		}
		DEBUG(DEBUG_IPC,1, (LOG_INFO, "changed root directory to %s",
			nsd.chrootdir));
		/* chdir to zonesdir again after chroot */
		if(nsd.options->zonesdir && nsd.options->zonesdir[0]) {
			if(chdir(nsd.options->zonesdir)) {
				error("unable to chdir to '%s': %s",
					nsd.options->zonesdir, strerror(errno));
			}
			DEBUG(DEBUG_IPC,1, (LOG_INFO, "changed directory to %s",
				nsd.options->zonesdir));
		}
	}
	else
#endif /* HAVE_CHROOT */
		nsd.file_rotation_ok = 1;

	DEBUG(DEBUG_IPC,1, (LOG_INFO, "file rotation on %s %sabled",
		nsd.log_filename, nsd.file_rotation_ok?"en":"dis"));

	/* Write pidfile */
	if (writepid(&nsd) == -1) {
		log_msg(LOG_ERR, "cannot overwrite the pidfile %s: %s",
			nsd.pidfile, strerror(errno));
	}

#ifdef USE_XDP
	if (nsd.options->xdp_interface) {
		/* initializing xdp needs the CAP_SYS_ADMIN, therefore doing it
		 * before privilege drop (and not keeping CAP_SYS_ADMIN) */
		if (xdp_server_init(&nsd.xdp.xdp_server)) {
			log_msg(LOG_ERR, "failed to initialize XDP... disabling XDP.");
			nsd.options->xdp_interface = NULL;
		}
	}
#endif

	/* Drop the permissions */
#ifdef HAVE_GETPWNAM
	if (*nsd.username) {
#ifdef USE_XDP
		if (nsd.options->xdp_interface) {
			/* tell kernel to keep (permitted) privileges across uid change */
			if (!prctl(PR_SET_KEEPCAPS, 1)) {
				/* only keep needed capabilities across privilege drop */
				/* this needs to be close to the privilege drop to prevent issues
				 * with other setup functions like tls setup */
				set_caps(0);
			} else {
				log_msg(LOG_ERR, "couldn't set keep capabilities... disabling XDP.");
				nsd.options->xdp_interface = NULL;
			}
		}
#endif /* USE_XDP */

#ifdef HAVE_INITGROUPS
		if(initgroups(nsd.username, nsd.gid) != 0)
			log_msg(LOG_WARNING, "unable to initgroups %s: %s",
				nsd.username, strerror(errno));
#endif /* HAVE_INITGROUPS */
		endpwent();

#ifdef HAVE_SETRESGID
		if(setresgid(nsd.gid,nsd.gid,nsd.gid) != 0)
#elif defined(HAVE_SETREGID) && !defined(DARWIN_BROKEN_SETREUID)
			if(setregid(nsd.gid,nsd.gid) != 0)
#else /* use setgid */
				if(setgid(nsd.gid) != 0)
#endif /* HAVE_SETRESGID */
					error("unable to set group id of %s: %s",
						nsd.username, strerror(errno));

#ifdef HAVE_SETRESUID
		if(setresuid(nsd.uid,nsd.uid,nsd.uid) != 0)
#elif defined(HAVE_SETREUID) && !defined(DARWIN_BROKEN_SETREUID)
			if(setreuid(nsd.uid,nsd.uid) != 0)
#else /* use setuid */
				if(setuid(nsd.uid) != 0)
#endif /* HAVE_SETRESUID */
					error("unable to set user id of %s: %s",
						nsd.username, strerror(errno));

		DEBUG(DEBUG_IPC,1, (LOG_INFO, "dropped user privileges, run as %s",
			nsd.username));

#ifdef USE_XDP
		/* enable capabilities after privilege drop */
		if (nsd.options->xdp_interface) {
			/* re-enable needed capabilities and drop setuid/gid capabilities */
			set_caps(1);
		}
#endif /* USE_XDP */
	}
#endif /* HAVE_GETPWNAM */

	if (pledge("stdio rpath wpath cpath dns inet proc", NULL) == -1)
		error("pledge");

	xfrd_make_tempdir(&nsd);
#ifdef USE_ZONE_STATS
	options_zonestatnames_create(nsd.options);
	server_zonestat_alloc(&nsd);
#endif /* USE_ZONE_STATS */
#ifdef BIND8_STATS
	server_stat_alloc(&nsd);
#endif /* BIND8_STATS */
	if(nsd.server_kind == NSD_SERVER_MAIN) {
		server_prepare_xfrd(&nsd);
		/* xfrd forks this before reading database, so it does not get
		 * the memory size of the database */
		server_start_xfrd(&nsd, 0, 0);
		/* close zonelistfile in non-xfrd processes */
		zone_list_close(nsd.options);
#ifdef USE_DNSTAP
		if(nsd.options->dnstap_enable) {
			nsd.dt_collector = dt_collector_create(&nsd);
			dt_collector_start(nsd.dt_collector, &nsd);
		}
#endif /* USE_DNSTAP */
	}
	if (server_prepare(&nsd) != 0) {
		unlinkpid(nsd.pidfile, nsd.username);
		error("server preparation failed, %s could "
			"not be started", argv0);
	}
	if(nsd.server_kind == NSD_SERVER_MAIN) {
		server_send_soa_xfrd(&nsd, 0);
	}

	/* Really take off */
	log_msg(LOG_NOTICE, "%s started (%s), pid %d",
		argv0, PACKAGE_STRING, (int) nsd.pid);

	if (nsd.server_kind == NSD_SERVER_MAIN) {
		server_main(&nsd);
	} else {
		server_child(&nsd);
	}

	/* NOTREACH */
	exit(0);
}
