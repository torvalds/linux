/*	$OpenBSD: hostapd.c,v 1.42 2023/03/08 04:43:13 guenther Exp $	*/

/*
 * Copyright (c) 2004, 2005 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/if_llc.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <err.h>

#include "hostapd.h"
#include "iapp.h"

void	 hostapd_usage(void);
void	 hostapd_udp_init(struct hostapd_config *);
void	 hostapd_sig_handler(int, short, void *);
static __inline int
	 hostapd_entry_cmp(struct hostapd_entry *, struct hostapd_entry *);

struct hostapd_config hostapd_cfg;

extern char *__progname;
char printbuf[BUFSIZ];

void
hostapd_usage(void)
{
	fprintf(stderr, "usage: %s [-dv] [-D macro=value] [-f file]\n",
	    __progname);
	exit(EXIT_FAILURE);
}

void
hostapd_log(u_int level, const char *fmt, ...)
{
	char *nfmt = NULL;
	va_list ap;

	if (level > hostapd_cfg.c_verbose)
		return;

	va_start(ap, fmt);
	if (hostapd_cfg.c_debug) {
		if (asprintf(&nfmt, "%s\n", fmt) != -1)
			vfprintf(stderr, nfmt, ap);
		else {
			vfprintf(stderr, fmt, ap);
			fprintf(stderr, "\n");
		}
		fflush(stderr);
	} else
		vsyslog(LOG_INFO, fmt, ap);
	va_end(ap);

	free(nfmt);
}

void
hostapd_printf(const char *fmt, ...)
{
	char newfmt[BUFSIZ];
	va_list ap;
	size_t n;

	if (fmt == NULL)
		goto flush;

	va_start(ap, fmt);
	bzero(newfmt, sizeof(newfmt));
	if ((n = strlcpy(newfmt, printbuf, sizeof(newfmt))) >= sizeof(newfmt))
		goto va_flush;
	if (strlcpy(newfmt + n, fmt, sizeof(newfmt) - n) >= sizeof(newfmt) - n)
		goto va_flush;
	if (vsnprintf(printbuf, sizeof(printbuf), newfmt, ap) < 0)
		goto va_flush;
	va_end(ap);

	return;

 va_flush:
	va_end(ap);
 flush:
	if (strlen(printbuf))
		hostapd_log(HOSTAPD_LOG, "%s", printbuf);
	bzero(printbuf, sizeof(printbuf));
}

void
hostapd_fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (hostapd_cfg.c_debug) {
		vfprintf(stderr, fmt, ap);
		fflush(stderr);
	} else
		vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);

	hostapd_cleanup(&hostapd_cfg);
	exit(EXIT_FAILURE);
}

int
hostapd_check_file_secrecy(int fd, const char *fname)
{
	struct stat st;

	if (fstat(fd, &st)) {
		hostapd_log(HOSTAPD_LOG,
		    "cannot stat %s", fname);
		return (-1);
	}

	if (st.st_uid != 0 && st.st_uid != getuid()) {
		hostapd_log(HOSTAPD_LOG,
		    "%s: owner not root or current user", fname);
		return (-1);
	}

	if (st.st_mode & (S_IRWXG | S_IRWXO)) {
		hostapd_log(HOSTAPD_LOG,
		    "%s: group/world readable/writeable", fname);
		return (-1);
	}

	return (0);
}

int
hostapd_bpf_open(u_int flags)
{
	int fd = -1;
	struct bpf_version bpv;

	if ((fd = open("/dev/bpf", flags)) == -1) {
		hostapd_fatal("unable to open BPF device: %s\n",
		    strerror(errno));
	}

	/*
	 * Get and validate the BPF version
	 */

	if (ioctl(fd, BIOCVERSION, &bpv) == -1)
		hostapd_fatal("failed to get BPF version: %s\n",
		    strerror(errno));

	if (bpv.bv_major != BPF_MAJOR_VERSION ||
	    bpv.bv_minor < BPF_MINOR_VERSION)
		hostapd_fatal("invalid BPF version\n");

	return (fd);
}

void
hostapd_udp_init(struct hostapd_config *cfg)
{
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct ifreq ifr;
	struct sockaddr_in *addr, baddr;
	struct ip_mreq mreq;
	int brd = 1;

	bzero(&ifr, sizeof(ifr));

	/*
	 * Open a listening UDP socket
	 */

	if ((iapp->i_udp = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		hostapd_fatal("unable to open udp socket\n");

	cfg->c_flags |= HOSTAPD_CFG_F_UDP;

	(void)strlcpy(ifr.ifr_name, iapp->i_iface, sizeof(ifr.ifr_name));

	if (ioctl(iapp->i_udp, SIOCGIFADDR, &ifr) == -1)
		hostapd_fatal("UDP ioctl %s on \"%s\" failed: %s\n",
		    "SIOCGIFADDR", ifr.ifr_name, strerror(errno));

	addr = (struct sockaddr_in *)&ifr.ifr_addr;
	iapp->i_addr.sin_family = AF_INET;
	iapp->i_addr.sin_addr.s_addr = addr->sin_addr.s_addr;
	if (iapp->i_addr.sin_port == 0)
		iapp->i_addr.sin_port = htons(IAPP_PORT);

	if (ioctl(iapp->i_udp, SIOCGIFBRDADDR, &ifr) == -1)
		hostapd_fatal("UDP ioctl %s on \"%s\" failed: %s\n",
		    "SIOCGIFBRDADDR", ifr.ifr_name, strerror(errno));

	addr = (struct sockaddr_in *)&ifr.ifr_addr;
	iapp->i_broadcast.sin_family = AF_INET;
	iapp->i_broadcast.sin_addr.s_addr = addr->sin_addr.s_addr;
	iapp->i_broadcast.sin_port = iapp->i_addr.sin_port;

	baddr.sin_family = AF_INET;
	baddr.sin_addr.s_addr = htonl(INADDR_ANY);
	baddr.sin_port = iapp->i_addr.sin_port;

	if (bind(iapp->i_udp, (struct sockaddr *)&baddr,
	    sizeof(baddr)) == -1)
		hostapd_fatal("failed to bind UDP socket: %s\n",
		    strerror(errno));

	/*
	 * The revised 802.11F standard requires IAPP messages to be
	 * sent via multicast to the default group 224.0.1.178.
	 * Nevertheless, some implementations still use broadcasts
	 * for IAPP messages.
	 */
	if (cfg->c_flags & HOSTAPD_CFG_F_BRDCAST) {
		/*
		 * Enable broadcast
		 */
		if (setsockopt(iapp->i_udp, SOL_SOCKET, SO_BROADCAST,
		    &brd, sizeof(brd)) == -1)
			hostapd_fatal("failed to enable broadcast on socket\n");

		hostapd_log(HOSTAPD_LOG_DEBUG, "%s: using broadcast mode "
		    "(address %s)", iapp->i_iface, inet_ntoa(addr->sin_addr));
	} else {
		/*
		 * Enable multicast
		 */
		bzero(&mreq, sizeof(mreq));

		iapp->i_multicast.sin_family = AF_INET;
		if (iapp->i_multicast.sin_addr.s_addr == INADDR_ANY)
			iapp->i_multicast.sin_addr.s_addr =
			    inet_addr(IAPP_MCASTADDR);
		iapp->i_multicast.sin_port = iapp->i_addr.sin_port;

		mreq.imr_multiaddr.s_addr =
		    iapp->i_multicast.sin_addr.s_addr;
		mreq.imr_interface.s_addr =
		    iapp->i_addr.sin_addr.s_addr;

		if (setsockopt(iapp->i_udp, IPPROTO_IP,
		    IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1)
			hostapd_fatal("failed to add multicast membership to "
			    "%s: %s\n", IAPP_MCASTADDR, strerror(errno));

		if (setsockopt(iapp->i_udp, IPPROTO_IP, IP_MULTICAST_TTL,
		    &iapp->i_ttl, sizeof(iapp->i_ttl)) == -1)
			hostapd_fatal("failed to set multicast ttl to "
			    "%u: %s\n", iapp->i_ttl, strerror(errno));

		hostapd_log(HOSTAPD_LOG_DEBUG, "%s: using multicast mode "
		    "(ttl %u, group %s)", iapp->i_iface, iapp->i_ttl,
		    inet_ntoa(iapp->i_multicast.sin_addr));
	}
}

void
hostapd_sig_handler(int sig, short event, void *arg)
{
	switch (sig) {
	case SIGALRM:
	case SIGTERM:
	case SIGQUIT:
	case SIGINT:
		(void)event_loopexit(NULL);
	}
}

void
hostapd_cleanup(struct hostapd_config *cfg)
{
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct ip_mreq mreq;
	struct hostapd_apme *apme;
	struct hostapd_table *table;
	struct hostapd_entry *entry;

	/* Release all Host APs */
	if (cfg->c_flags & HOSTAPD_CFG_F_APME) {
		while ((apme = TAILQ_FIRST(&cfg->c_apmes)) != NULL)
			hostapd_apme_term(apme);
	}

	if (cfg->c_flags & HOSTAPD_CFG_F_PRIV &&
	    (cfg->c_flags & HOSTAPD_CFG_F_BRDCAST) == 0) {
		/*
		 * Disable multicast and let the kernel unsubscribe
		 * from the multicast group.
		 */

		bzero(&mreq, sizeof(mreq));

		mreq.imr_multiaddr.s_addr =
		    inet_addr(IAPP_MCASTADDR);
		mreq.imr_interface.s_addr =
		    iapp->i_addr.sin_addr.s_addr;

		if (setsockopt(iapp->i_udp, IPPROTO_IP,
		    IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) == -1)
			hostapd_log(HOSTAPD_LOG, "failed to remove multicast"
			    " membership to %s: %s",
			    IAPP_MCASTADDR, strerror(errno));
	}

	if ((cfg->c_flags & HOSTAPD_CFG_F_PRIV) == 0 &&
	    cfg->c_flags & HOSTAPD_CFG_F_APME) {
		/* Shutdown the Host AP protocol handler */
		hostapd_iapp_term(&hostapd_cfg);
	}

	/* Cleanup tables */
	while ((table = TAILQ_FIRST(&cfg->c_tables)) != NULL) {
		while ((entry = RB_MIN(hostapd_tree, &table->t_tree)) != NULL) {
			RB_REMOVE(hostapd_tree, &table->t_tree, entry);
			free(entry);
		}
		while ((entry = TAILQ_FIRST(&table->t_mask_head)) != NULL) {
			TAILQ_REMOVE(&table->t_mask_head, entry, e_entries);
			free(entry);
		}
		TAILQ_REMOVE(&cfg->c_tables, table, t_entries);
		free(table);
	}

	hostapd_log(HOSTAPD_LOG_VERBOSE, "bye!");
}

int
main(int argc, char *argv[])
{
	struct event ev_sigalrm;
	struct event ev_sigterm;
	struct event ev_sigquit;
	struct event ev_sigint;
	struct hostapd_config *cfg = &hostapd_cfg;
	struct hostapd_iapp *iapp;
	struct hostapd_apme *apme;
	char *config = NULL;
	u_int debug = 0, ret;
	int ch;

	/* Set startup logging */
	cfg->c_debug = 1;

	/*
	 * Get and parse command line options
	 */
	while ((ch = getopt(argc, argv, "f:D:dv")) != -1) {
		switch (ch) {
		case 'f':
			config = optarg;
			break;
		case 'D':
			if (hostapd_parse_symset(optarg) < 0)
				hostapd_fatal("could not parse macro "
				    "definition %s\n", optarg);
			break;
		case 'd':
			debug++;
			break;
		case 'v':
			cfg->c_verbose++;
			break;
		default:
			hostapd_usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		hostapd_usage();

	if (config == NULL)
		ret = strlcpy(cfg->c_config, HOSTAPD_CONFIG, sizeof(cfg->c_config));
	else
		ret = strlcpy(cfg->c_config, config, sizeof(cfg->c_config));
	if (ret >= sizeof(cfg->c_config))
		hostapd_fatal("invalid configuration file\n");

	if (geteuid())
		hostapd_fatal("need root privileges\n");

	/* Parse the configuration file */
	if (hostapd_parse_file(cfg) != 0)
		hostapd_fatal("invalid configuration in %s\n", cfg->c_config);

	iapp = &cfg->c_iapp;

	if ((cfg->c_flags & HOSTAPD_CFG_F_IAPP) == 0)
		hostapd_fatal("IAPP interface not specified\n");

	if (cfg->c_apme_dlt == 0)
		cfg->c_apme_dlt = HOSTAPD_DLT;

	/*
	 * Setup the hostapd handlers
	 */
	hostapd_udp_init(cfg);
	hostapd_llc_init(cfg);

	/*
	 * Set runtime logging and detach as daemon
	 */
	if ((cfg->c_debug = debug) == 0) {
		openlog(__progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);
		tzset();
		if (daemon(0, 0) == -1)
			hostapd_fatal("failed to daemonize\n");
	}

	if (cfg->c_flags & HOSTAPD_CFG_F_APME) {
		TAILQ_FOREACH(apme, &cfg->c_apmes, a_entries)
			hostapd_apme_init(apme);
	} else
		hostapd_log(HOSTAPD_LOG, "%s: running without a Host AP",
		    iapp->i_iface);

	/* Drop all privileges in an unprivileged child process */
	hostapd_priv_init(cfg);

	if (cfg->c_flags & HOSTAPD_CFG_F_APME)
		setproctitle("IAPP: %s, Host AP", iapp->i_iface);
	else
		setproctitle("IAPP: %s", iapp->i_iface);

	/*
	 * Unprivileged child process
	 */

	(void)event_init();

	/*
	 * Set signal handlers
	 */
	signal_set(&ev_sigalrm, SIGALRM, hostapd_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, hostapd_sig_handler, NULL);
	signal_set(&ev_sigquit, SIGQUIT, hostapd_sig_handler, NULL);
	signal_set(&ev_sigint, SIGINT, hostapd_sig_handler, NULL);
	signal_add(&ev_sigalrm, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sigquit, NULL);
	signal_add(&ev_sigint, NULL);
	signal(SIGHUP, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	/* Initialize the IAPP protocol handler */
	hostapd_iapp_init(cfg);

	/*
	 * Schedule the Host AP listener
	 */
	if (cfg->c_flags & HOSTAPD_CFG_F_APME) {
		TAILQ_FOREACH(apme, &cfg->c_apmes, a_entries) {
			event_set(&apme->a_ev, apme->a_raw,
			    EV_READ | EV_PERSIST, hostapd_apme_input, apme);
			if (event_add(&apme->a_ev, NULL) == -1)
				hostapd_fatal("failed to add APME event");
		}
	}

	/*
	 * Schedule the IAPP listener
	 */
	event_set(&iapp->i_udp_ev, iapp->i_udp, EV_READ | EV_PERSIST,
	    hostapd_iapp_input, cfg);
	if (event_add(&iapp->i_udp_ev, NULL) == -1)
		hostapd_fatal("failed to add IAPP event");

	hostapd_log(HOSTAPD_LOG, "starting hostapd with pid %u",
	    getpid());

	/* Run event loop */
	if (event_dispatch() == -1)
		hostapd_fatal("failed to dispatch hostapd");

	/* Executed after the event loop has been terminated */
	hostapd_cleanup(cfg);
	return (EXIT_SUCCESS);
}

void
hostapd_randval(u_int8_t *buf, const u_int len)
{
	u_int32_t data = 0;
	u_int i;

	for (i = 0; i < len; i++) {
		if ((i % sizeof(data)) == 0)
			data = arc4random();
		buf[i] = data & 0xff;
		data >>= 8;
	}
}

struct hostapd_table *
hostapd_table_add(struct hostapd_config *cfg, const char *name)
{
	struct hostapd_table *table;

	if (hostapd_table_lookup(cfg, name) != NULL)
		return (NULL);
	if ((table = (struct hostapd_table *)
	    calloc(1, sizeof(struct hostapd_table))) == NULL)
		return (NULL);
	if (strlcpy(table->t_name, name, sizeof(table->t_name)) >=
	    sizeof(table->t_name)) {
		free(table);
		return (NULL);
	}
	RB_INIT(&table->t_tree);
	TAILQ_INIT(&table->t_mask_head);
	TAILQ_INSERT_TAIL(&cfg->c_tables, table, t_entries);

	return (table);
}

struct hostapd_table *
hostapd_table_lookup(struct hostapd_config *cfg, const char *name)
{
	struct hostapd_table *table;

	TAILQ_FOREACH(table, &cfg->c_tables, t_entries) {
		if (strcmp(name, table->t_name) == 0)
			return (table);
	}

	return (NULL);
}

struct hostapd_entry *
hostapd_entry_add(struct hostapd_table *table, u_int8_t *lladdr)
{
	struct hostapd_entry *entry;

	if (hostapd_entry_lookup(table, lladdr) != NULL)
		return (NULL);

	if ((entry = (struct hostapd_entry *)
	    calloc(1, sizeof(struct hostapd_entry))) == NULL)
		return (NULL);

	bcopy(lladdr, entry->e_lladdr, IEEE80211_ADDR_LEN);
	RB_INSERT(hostapd_tree, &table->t_tree, entry);

	return (entry);
}

struct hostapd_entry *
hostapd_entry_lookup(struct hostapd_table *table, u_int8_t *lladdr)
{
	struct hostapd_entry *entry, key;

	bcopy(lladdr, key.e_lladdr, IEEE80211_ADDR_LEN);
	if ((entry = RB_FIND(hostapd_tree, &table->t_tree, &key)) != NULL)
		return (entry);

	/* Masked entries can't be handled by the red-black tree */
	TAILQ_FOREACH(entry, &table->t_mask_head, e_entries) {
		if (HOSTAPD_ENTRY_MASK_MATCH(entry, lladdr))
			return (entry);
	}

	return (NULL);
}

void
hostapd_entry_update(struct hostapd_table *table, struct hostapd_entry *entry)
{
	RB_REMOVE(hostapd_tree, &table->t_tree, entry);

	/* Apply mask to entry */
	if (entry->e_flags & HOSTAPD_ENTRY_F_MASK) {
		HOSTAPD_ENTRY_MASK_ADD(entry->e_lladdr, entry->e_mask);
		TAILQ_INSERT_TAIL(&table->t_mask_head, entry, e_entries);
	} else {
		RB_INSERT(hostapd_tree, &table->t_tree, entry);
	}
}

static __inline int
hostapd_entry_cmp(struct hostapd_entry *a, struct hostapd_entry *b)
{
	return (memcmp(a->e_lladdr, b->e_lladdr, IEEE80211_ADDR_LEN));
}

RB_GENERATE(hostapd_tree, hostapd_entry, e_nodes, hostapd_entry_cmp);
