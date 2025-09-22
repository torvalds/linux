/*	$OpenBSD: privsep.c,v 1.28 2023/03/08 04:43:13 guenther Exp $	*/

/*
 * Copyright (c) 2004, 2005 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 1995, 1999 Theo de Raadt
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
#include <sys/socket.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/if_llc.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "hostapd.h"
#include "iapp.h"

enum hostapd_cmd_types {
	PRIV_APME_BSSID,	/* Get the Host AP's BSSID */
	PRIV_APME_GETNODE,	/* Get a node from the Host AP */
	PRIV_APME_ADDNODE,	/* Delete a node from the Host AP */
	PRIV_APME_DELNODE,	/* Delete a node from the Host AP */
	PRIV_APME_ADDROAMING,	/* Add a route to the kernel */
	PRIV_APME_DELROAMING,	/* Delete a route from the kernel */
	PRIV_LLC_SEND_XID	/* Send IEEE 802.3 LLC XID frame */
};

void	 hostapd_priv(int, short, void *);
struct hostapd_apme *hostapd_priv_getapme(int, struct hostapd_config *);
void	 hostapd_sig_relay(int, short, void *);
void	 hostapd_sig_chld(int, short, void *);
int	 hostapd_may_read(int, void *, size_t);
void	 hostapd_must_read(int, void *, size_t);
void	 hostapd_must_write(int, void *, size_t);

static int priv_fd = -1;
static volatile pid_t child_pid = -1;

/*
 * Main privsep functions
 */

void
hostapd_priv_init(struct hostapd_config *cfg)
{
	struct event ev_sigalrm;
	struct event ev_sigterm;
	struct event ev_sigint;
	struct event ev_sighup;
	struct event ev_sigchld;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	struct hostapd_apme *apme;
	int i, socks[2];
	struct passwd *pw;
	struct servent *se;

	for (i = 1; i < _NSIG; i++)
		signal(i, SIG_DFL);

	if ((se = getservbyname("iapp", "udp")) == NULL) {
		iapp->i_udp_port = IAPP_PORT;
	} else
		iapp->i_udp_port = se->s_port;

	if ((pw = getpwnam(HOSTAPD_USER)) == NULL)
		hostapd_fatal("failed to get user \"%s\"\n", HOSTAPD_USER);

	endservent();

	/* Create sockets */
	if (socketpair(AF_LOCAL, SOCK_STREAM, PF_UNSPEC, socks) == -1)
		hostapd_fatal("failed to get socket pair\n");

	if ((child_pid = fork()) == -1)
		hostapd_fatal("failed to fork child process\n");

	/*
	 * Unprivileged child process
	 */
	if (child_pid == 0) {
		cfg->c_flags &= ~HOSTAPD_CFG_F_PRIV;

		/*
		 * Change the child's root directory to the unprivileged
		 * user's home directory
		 */
		if (chroot(pw->pw_dir) == -1)
			hostapd_fatal("failed to change root directory\n");
		if (chdir("/") == -1)
			hostapd_fatal("failed to change directory\n");

		/*
		 * Drop privileges and clear the group access list
		 */
		if (setgroups(1, &pw->pw_gid) == -1 ||
		    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1 ||
		    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1)
			hostapd_fatal("can't drop privileges\n");

		(void)close(socks[0]);
		priv_fd = socks[1];
		return;
	}

	/*
	 * Privileged mother process
	 */
	cfg->c_flags |= HOSTAPD_CFG_F_PRIV;

	(void)event_init();

	/* Pass ALRM/TERM/INT/HUP through to child, and accept CHLD */
	signal_set(&ev_sigalrm, SIGALRM, hostapd_sig_relay, NULL);
	signal_set(&ev_sigterm, SIGTERM, hostapd_sig_relay, NULL);
	signal_set(&ev_sigint, SIGINT, hostapd_sig_relay, NULL);
	signal_set(&ev_sighup, SIGHUP, hostapd_sig_relay, NULL);
	signal_set(&ev_sigchld, SIGCHLD, hostapd_sig_chld, NULL);
	signal_add(&ev_sigalrm, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sighup, NULL);
	signal_add(&ev_sigchld, NULL);

	(void)close(socks[1]);

	if (cfg->c_flags & HOSTAPD_CFG_F_APME) {
		if ((cfg->c_apme_ctl = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
			hostapd_fatal("unable to open ioctl socket\n");
		TAILQ_FOREACH(apme, &cfg->c_apmes, a_entries)
			if (apme->a_chanavail != NULL)
				hostapd_apme_sethopper(apme, 0);
	}

	hostapd_roaming_init(cfg);

	/* Start a new event listener */
	event_set(&cfg->c_priv_ev, socks[0], EV_READ, hostapd_priv, cfg);
	if (event_add(&cfg->c_priv_ev, NULL) == -1)
		hostapd_fatal("failed to add priv event");

	/* Run privileged event loop */
	if (event_dispatch() == -1)
		hostapd_fatal("failed to dispatch priv hostapd");

	/* Executed after the event loop has been terminated */
	hostapd_cleanup(cfg);
	_exit(EXIT_SUCCESS);
}

struct hostapd_apme *
hostapd_priv_getapme(int fd, struct hostapd_config *cfg)
{
	struct hostapd_apme *apme;
	char name[IFNAMSIZ];
	int n;

	hostapd_must_read(fd, name, IFNAMSIZ);
	if ((cfg->c_flags & HOSTAPD_CFG_F_APME) == 0 ||
	    (apme = hostapd_apme_lookup(cfg, name)) == NULL) {
		n = ENXIO;
		hostapd_must_write(fd, &n, sizeof(int));
		return (NULL);
	}
	return (apme);
}

void
hostapd_priv(int fd, short sig, void *arg)
{
	struct hostapd_config *cfg = (struct hostapd_config *)arg;
	struct hostapd_apme *apme;
	struct hostapd_node node;
	struct ieee80211_bssid bssid;
	struct ieee80211_nodereq nr;
	struct ifreq ifr;
	unsigned long request;
	int ret = 0, cmd;

	/* Terminate the event if we got an invalid signal */
	if (sig != EV_READ)
		return;

	bzero(&node, sizeof(struct hostapd_node));
	bzero(&nr, sizeof(struct ieee80211_nodereq));

	/* Get privsep command */
	if (hostapd_may_read(fd, &cmd, sizeof(int)))
		return;

	switch (cmd) {
	case PRIV_APME_BSSID:
		hostapd_log(HOSTAPD_LOG_DEBUG,
		    "[priv]: msg PRIV_APME_BSSID received");

		if ((apme = hostapd_priv_getapme(fd, cfg)) == NULL)
			break;
		(void)strlcpy(bssid.i_name, apme->a_iface, sizeof(bssid.i_name));

		/* Try to get the APME's BSSID */
		if ((ret = ioctl(cfg->c_apme_ctl,
		    SIOCG80211BSSID, &bssid)) != 0)
			ret = errno;

		hostapd_must_write(fd, &ret, sizeof(int));
		if (ret == 0)
			hostapd_must_write(fd, &bssid.i_bssid,
			    IEEE80211_ADDR_LEN);
		break;

	case PRIV_APME_GETNODE:
		hostapd_log(HOSTAPD_LOG_DEBUG,
		    "[priv]: msg PRIV_APME_GETNODE received");

		hostapd_must_read(fd, &node, sizeof(struct hostapd_node));
		bcopy(node.ni_macaddr, nr.nr_macaddr, IEEE80211_ADDR_LEN);

		if ((apme = hostapd_priv_getapme(fd, cfg)) == NULL)
			break;
		(void)strlcpy(nr.nr_ifname, apme->a_iface, sizeof(ifr.ifr_name));

		/* Try to get a station from the APME */
		if ((ret = ioctl(cfg->c_apme_ctl,
		    SIOCG80211NODE, &nr)) != 0)
			ret = errno;

		hostapd_must_write(fd, &ret, sizeof(int));
		if (ret == 0) {
			node.ni_associd = nr.nr_associd;
			node.ni_flags = IEEE80211_NODEREQ_STATE(nr.nr_state);
			node.ni_rssi = nr.nr_rssi;
			node.ni_capinfo = nr.nr_capinfo;

			hostapd_must_write(fd, &node,
			    sizeof(struct hostapd_node));
		}
		break;

	case PRIV_APME_ADDNODE:
	case PRIV_APME_DELNODE:
		hostapd_log(HOSTAPD_LOG_DEBUG,
		    "[priv]: msg PRIV_APME_[ADD|DEL]NODE received");

		hostapd_must_read(fd, &node, sizeof(struct hostapd_node));
		bcopy(node.ni_macaddr, nr.nr_macaddr, IEEE80211_ADDR_LEN);

		if ((apme = hostapd_priv_getapme(fd, cfg)) == NULL)
			break;
		(void)strlcpy(nr.nr_ifname, apme->a_iface, sizeof(ifr.ifr_name));

		request = cmd == PRIV_APME_ADDNODE ?
		    SIOCS80211NODE : SIOCS80211DELNODE;

		/* Try to add/delete a station from the APME */
		if ((ret = ioctl(cfg->c_apme_ctl, request, &nr)) == -1)
			ret = errno;

		hostapd_must_write(fd, &ret, sizeof(int));
		break;

	case PRIV_LLC_SEND_XID:
		hostapd_log(HOSTAPD_LOG_DEBUG,
		    "[priv]: msg PRIV_LLC_SEND_XID received");

		hostapd_must_read(fd, &node, sizeof(struct hostapd_node));

		/* Send a LLC XID frame to reset possible switch ports */
		ret = hostapd_llc_send_xid(cfg, &node);
		hostapd_must_write(fd, &ret, sizeof(int));
		break;

	case PRIV_APME_ADDROAMING:
	case PRIV_APME_DELROAMING:
		hostapd_log(HOSTAPD_LOG_DEBUG,
		    "[priv]: msg PRIV_APME_[ADD|DEL]ROAMING received");

		hostapd_must_read(fd, &node, sizeof(struct hostapd_node));

		if ((apme = hostapd_priv_getapme(fd, cfg)) == NULL)
			break;
		ret = hostapd_roaming(apme, &node, cmd == PRIV_APME_ADDROAMING);
		hostapd_must_write(fd, &ret, sizeof(int));
		break;

	default:
		hostapd_fatal("[priv]: unknown command %d\n", cmd);
	}
	if (event_add(&cfg->c_priv_ev, NULL) == -1)
		hostapd_fatal("failed to schedult priv event");

	return;
}

/*
 * Unprivileged callers
 */
int
hostapd_priv_apme_getnode(struct hostapd_apme *apme, struct hostapd_node *node)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	int ret, cmd;

	if (priv_fd < 0)
		hostapd_fatal("%s: called from privileged portion\n", __func__);

	if ((cfg->c_flags & HOSTAPD_CFG_F_APME) == 0)
		hostapd_fatal("%s: Host AP is not available\n", __func__);

	cmd = PRIV_APME_GETNODE;
	hostapd_must_write(priv_fd, &cmd, sizeof(int));
	hostapd_must_write(priv_fd, node, sizeof(struct hostapd_node));
	hostapd_must_write(priv_fd, &apme->a_iface, IFNAMSIZ);
	hostapd_must_read(priv_fd, &ret, sizeof(int));
	if (ret != 0)
		return (ret);

	hostapd_must_read(priv_fd, node, sizeof(struct hostapd_node));
	return (ret);
}

int
hostapd_priv_apme_setnode(struct hostapd_apme *apme, struct hostapd_node *node,
    int add)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	struct hostapd_iapp *iapp = &cfg->c_iapp;
	int ret, cmd;

	if (priv_fd < 0)
		hostapd_fatal("%s: called from privileged portion\n", __func__);

	if ((cfg->c_flags & HOSTAPD_CFG_F_APME) == 0)
		hostapd_fatal("%s: Host AP is not available\n", __func__);

	if (add)
		cmd = PRIV_APME_ADDNODE;
	else
		cmd = PRIV_APME_DELNODE;
	hostapd_must_write(priv_fd, &cmd, sizeof(int));
	hostapd_must_write(priv_fd, node, sizeof(struct hostapd_node));
	hostapd_must_write(priv_fd, &apme->a_iface, IFNAMSIZ);

	hostapd_must_read(priv_fd, &ret, sizeof(int));
	if (ret == 0)
		hostapd_log(HOSTAPD_LOG_VERBOSE, "%s/%s: %s node %s",
		    apme->a_iface, iapp->i_iface,
		    add ? "added" : "removed",
		    etheraddr_string(node->ni_macaddr));

	return (ret);
}

void
hostapd_priv_apme_bssid(struct hostapd_apme *apme)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	int ret, cmd;

	if (priv_fd < 0)
		hostapd_fatal("%s: called from privileged portion\n", __func__);

	if ((cfg->c_flags & HOSTAPD_CFG_F_APME) == 0)
		hostapd_fatal("%s: Host AP is not available\n", __func__);

	cmd = PRIV_APME_BSSID;
	hostapd_must_write(priv_fd, &cmd, sizeof(int));
	hostapd_must_write(priv_fd, &apme->a_iface, IFNAMSIZ);
	hostapd_must_read(priv_fd, &ret, sizeof(int));
	if (ret != 0)
		hostapd_fatal("failed to get Host AP's BSSID on"
		    " \"%s\": %s\n", apme->a_iface, strerror(errno));

	hostapd_must_read(priv_fd, &apme->a_bssid, IEEE80211_ADDR_LEN);
	cfg->c_stats.cn_tx_apme++;
}

int
hostapd_priv_llc_xid(struct hostapd_config *cfg, struct hostapd_node *node)
{
	int ret, cmd;

	if (priv_fd < 0)
		hostapd_fatal("%s: called from privileged portion\n", __func__);

	cmd = PRIV_LLC_SEND_XID;
	hostapd_must_write(priv_fd, &cmd, sizeof(int));
	hostapd_must_write(priv_fd, node, sizeof(struct hostapd_node));
	hostapd_must_read(priv_fd, &ret, sizeof(int));

	if (ret == 0)
		cfg->c_stats.cn_tx_llc++;
	return (ret);
}

int
hostapd_priv_roaming(struct hostapd_apme *apme, struct hostapd_node *node,
    int add)
{
	struct hostapd_config *cfg = (struct hostapd_config *)apme->a_cfg;
	int ret, cmd;

	if (priv_fd < 0)
		hostapd_fatal("%s: called from privileged portion\n", __func__);

	if ((cfg->c_flags & HOSTAPD_CFG_F_APME) == 0)
		hostapd_fatal("%s: Host AP is not available\n", __func__);

	if (add)
		cmd = PRIV_APME_ADDROAMING;
	else
		cmd = PRIV_APME_DELROAMING;
	hostapd_must_write(priv_fd, &cmd, sizeof(int));
	hostapd_must_write(priv_fd, node, sizeof(struct hostapd_node));
	hostapd_must_write(priv_fd, &apme->a_iface, IFNAMSIZ);

	hostapd_must_read(priv_fd, &ret, sizeof(int));

	return (ret);
}

/*
 * If priv parent gets a TERM or HUP, pass it through to child instead.
 */
void
hostapd_sig_relay(int sig, short event, void *arg)
{
	int oerrno = errno;

	if (child_pid != -1)
		if (kill(child_pid, sig) == -1)
			hostapd_fatal("hostapd_sig_relay: kill(%d, %d)",
			    child_pid, sig);
	errno = oerrno;
}

void
hostapd_sig_chld(int sig, short event, void *arg)
{
	/*
	 * If parent gets a SIGCHLD, it will exit.
	 */

	if (sig == SIGCHLD)
		(void)event_loopexit(NULL);
}

/*
 * privsep I/O functions
 */

/* Read all data or return 1 for error.  */
int
hostapd_may_read(int fd, void *buf, size_t n)
{
	char *s = buf;
	ssize_t res, pos = 0;

	while ((ssize_t)n > pos) {
		res = read(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
			/* FALLTHROUGH */
		case 0:
			return (1);
		default:
			pos += res;
		}
	}
	return (0);
}

/*
 * Read data with the assertion that it all must come through, or
 * else abort the process.  Based on atomicio() from openssh.
 */
void
hostapd_must_read(int fd, void *buf, size_t n)
{
	char *s = buf;
	ssize_t res, pos = 0;

	while ((ssize_t)n > pos) {
		res = read(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
			/* FALLTHROUGH */
		case 0:
			_exit(0);
			break;
		default:
			pos += res;
		}
	}
}

/*
 * Write data with the assertion that it all has to be written, or
 * else abort the process.  Based on atomicio() from openssh.
 */
void
hostapd_must_write(int fd, void *buf, size_t n)
{
	char *s = buf;
	ssize_t res, pos = 0;

	while ((ssize_t)n > pos) {
		res = write(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
			/* FALLTHROUGH */
		case 0:
			_exit(0);
			break;
		default:
			pos += res;
		}
	}
}

