/*	$OpenBSD: auth.c,v 1.43 2024/08/21 14:57:05 florian Exp $	*/

/*
 * auth.c - PPP authentication and phase control.
 *
 * Copyright (c) 1989-2002 Paul Mackerras. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name(s) of the authors of this software must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Paul Mackerras
 *     <paulus@samba.org>".
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Copyright (c) 1984-2000 Carnegie Mellon University. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any legal
 *    details, please contact
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <syslog.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <utmp.h>
#include <fcntl.h>
#if defined(_PATH_LASTLOG) && defined(_linux_)
#include <lastlog.h>
#endif

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>



#include "pppd.h"
#include "fsm.h"
#include "lcp.h"
#include "ipcp.h"
#include "upap.h"
#include "chap.h"
#ifdef CBCP_SUPPORT
#include "cbcp.h"
#endif
#include "pathnames.h"

/* Used for storing a sequence of words.  Usually malloced. */
struct wordlist {
    struct wordlist	*next;
    char		word[1];
};

/* Bits in scan_authfile return value */
#define NONWILD_SERVER	1
#define NONWILD_CLIENT	2

#define ISWILD(word)	(word[0] == '*' && word[1] == 0)

#define FALSE	0
#define TRUE	1

/* The name by which the peer authenticated itself to us. */
char peer_authname[MAXNAMELEN];

/* Records which authentication operations haven't completed yet. */
static int auth_pending[NUM_PPP];

/* Set if we have successfully called plogin() */
static int logged_in;

/* Set if we have run the /etc/ppp/auth-up script. */
static int did_authup;

/* List of addresses which the peer may use. */
static struct wordlist *addresses[NUM_PPP];

/* Number of network protocols which we have opened. */
static int num_np_open;

/* Number of network protocols which have come up. */
static int num_np_up;

/* Set if we got the contents of passwd[] from the pap-secrets file. */
static int passwd_from_file;

/* Bits in auth_pending[] */
#define PAP_WITHPEER	1
#define PAP_PEER	2
#define CHAP_WITHPEER	4
#define CHAP_PEER	8

extern char *crypt(const char *, const char *);

/* Prototypes for procedures local to this file. */

static void network_phase(int);
static void check_idle(void *);
static void connect_time_expired(void *);
static int  plogin(char *, char *, char **, int *);
static void plogout(void);
static int  null_login(int);
static int  get_pap_passwd(char *);
static int  have_pap_secret(void);
static int  have_chap_secret(char *, char *, u_int32_t);
static int  ip_addr_check(u_int32_t, struct wordlist *);
static int  scan_authfile(FILE *, char *, char *, u_int32_t, char *,
		struct wordlist **, char *);
static void free_wordlist(struct wordlist *);
static void auth_script(char *);
static void set_allowed_addrs(int, struct wordlist *);

/*
 * An Open on LCP has requested a change from Dead to Establish phase.
 * Do what's necessary to bring the physical layer up.
 */
void
link_required(int unit)
{
}

/*
 * LCP has terminated the link; go to the Dead phase and take the
 * physical layer down.
 */
void
link_terminated(int unit)
{
    if (phase == PHASE_DEAD)
	return;
    if (logged_in)
	plogout();
    phase = PHASE_DEAD;
    syslog(LOG_NOTICE, "Connection terminated.");
}

/*
 * LCP has gone down; it will either die or try to re-establish.
 */
void
link_down(int unit)
{
    int i;
    struct protent *protp;

    if (did_authup) {
	auth_script(_PATH_AUTHDOWN);
	did_authup = 0;
    }
    for (i = 0; (protp = protocols[i]) != NULL; ++i) {
	if (!protp->enabled_flag)
	    continue;
        if (protp->protocol != PPP_LCP && protp->lowerdown != NULL)
	    (*protp->lowerdown)(unit);
        if (protp->protocol < 0xC000 && protp->close != NULL)
	    (*protp->close)(unit, "LCP down");
    }
    num_np_open = 0;
    num_np_up = 0;
    if (phase != PHASE_DEAD)
	phase = PHASE_TERMINATE;
}

/*
 * The link is established.
 * Proceed to the Dead, Authenticate or Network phase as appropriate.
 */
void
link_established(int unit)
{
    int auth;
    lcp_options *wo = &lcp_wantoptions[unit];
    lcp_options *go = &lcp_gotoptions[unit];
    lcp_options *ho = &lcp_hisoptions[unit];
    int i;
    struct protent *protp;

    /*
     * Tell higher-level protocols that LCP is up.
     */
    for (i = 0; (protp = protocols[i]) != NULL; ++i)
        if (protp->protocol != PPP_LCP && protp->enabled_flag
	    && protp->lowerup != NULL)
	    (*protp->lowerup)(unit);

    if (auth_required && !(go->neg_chap || go->neg_upap)) {
	/*
	 * We wanted the peer to authenticate itself, and it refused:
	 * treat it as though it authenticated with PAP using a username
	 * of "" and a password of "".  If that's not OK, boot it out.
	 */
	if (!wo->neg_upap || !null_login(unit)) {
	    syslog(LOG_WARNING, "peer refused to authenticate");
	    lcp_close(unit, "peer refused to authenticate");
	    return;
	}
    }

    phase = PHASE_AUTHENTICATE;
    auth = 0;
    if (go->neg_chap) {
	ChapAuthPeer(unit, our_name, go->chap_mdtype);
	auth |= CHAP_PEER;
    } else if (go->neg_upap) {
	upap_authpeer(unit);
	auth |= PAP_PEER;
    }
    if (ho->neg_chap) {
	ChapAuthWithPeer(unit, user, ho->chap_mdtype);
	auth |= CHAP_WITHPEER;
    } else if (ho->neg_upap) {
	if (passwd[0] == 0) {
	    passwd_from_file = 1;
	    if (!get_pap_passwd(passwd))
		syslog(LOG_ERR, "No secret found for PAP login");
	}
	upap_authwithpeer(unit, user, passwd);
	auth |= PAP_WITHPEER;
    }
    auth_pending[unit] = auth;

    if (!auth)
	network_phase(unit);
}

/*
 * Proceed to the network phase.
 */
static void
network_phase(int unit)
{
    int i;
    struct protent *protp;
    lcp_options *go = &lcp_gotoptions[unit];

    /*
     * If the peer had to authenticate, run the auth-up script now.
     */
    if ((go->neg_chap || go->neg_upap) && !did_authup) {
	auth_script(_PATH_AUTHUP);
	did_authup = 1;
    }

#ifdef CBCP_SUPPORT
    /*
     * If we negotiated callback, do it now.
     */
    if (go->neg_cbcp) {
	phase = PHASE_CALLBACK;
	(*cbcp_protent.open)(unit);
	return;
    }
#endif

    phase = PHASE_NETWORK;
#if 0
    if (!demand)
	set_filters(&pass_filter, &active_filter);
#endif
    for (i = 0; (protp = protocols[i]) != NULL; ++i)
        if (protp->protocol < 0xC000 && protp->enabled_flag
	    && protp->open != NULL) {
	    (*protp->open)(unit);
	    if (protp->protocol != PPP_CCP)
		++num_np_open;
	}

    if (num_np_open == 0)
	/* nothing to do */
	lcp_close(0, "No network protocols running");
}

/*
 * The peer has failed to authenticate himself using `protocol'.
 */
void
auth_peer_fail(int unit, int protocol)
{
    /*
     * Authentication failure: take the link down
     */
    lcp_close(unit, "Authentication failed");
}

/*
 * The peer has been successfully authenticated using `protocol'.
 */
void
auth_peer_success(int unit, int protocol, char *name, int namelen)
{
    int bit;

    switch (protocol) {
    case PPP_CHAP:
	bit = CHAP_PEER;
	break;
    case PPP_PAP:
	bit = PAP_PEER;
	break;
    default:
	syslog(LOG_WARNING, "auth_peer_success: unknown protocol %x",
	       protocol);
	return;
    }

    /*
     * Save the authenticated name of the peer for later.
     */
    if (namelen > sizeof(peer_authname) - 1)
	namelen = sizeof(peer_authname) - 1;
    BCOPY(name, peer_authname, namelen);
    peer_authname[namelen] = 0;
    script_setenv("PEERNAME", peer_authname);

    /*
     * If there is no more authentication still to be done,
     * proceed to the network (or callback) phase.
     */
    if ((auth_pending[unit] &= ~bit) == 0)
        network_phase(unit);
}

/*
 * We have failed to authenticate ourselves to the peer using `protocol'.
 */
void
auth_withpeer_fail(int unit, int protocol)
{
    if (passwd_from_file)
	EXPLICIT_BZERO(passwd, MAXSECRETLEN);
    /*
     * We've failed to authenticate ourselves to our peer.
     * He'll probably take the link down, and there's not much
     * we can do except wait for that.
     */
}

/*
 * We have successfully authenticated ourselves with the peer using `protocol'.
 */
void
auth_withpeer_success(int unit, int protocol)
{
    int bit;

    switch (protocol) {
    case PPP_CHAP:
	bit = CHAP_WITHPEER;
	break;
    case PPP_PAP:
	if (passwd_from_file)
	    EXPLICIT_BZERO(passwd, MAXSECRETLEN);
	bit = PAP_WITHPEER;
	break;
    default:
	syslog(LOG_WARNING, "auth_peer_success: unknown protocol %x",
	       protocol);
	bit = 0;
    }

    /*
     * If there is no more authentication still being done,
     * proceed to the network (or callback) phase.
     */
    if ((auth_pending[unit] &= ~bit) == 0)
	network_phase(unit);
}


/*
 * np_up - a network protocol has come up.
 */
void
np_up(int unit, int proto)
{
    if (num_np_up == 0) {
	/*
	 * At this point we consider that the link has come up successfully.
	 */
	need_holdoff = 0;

	if (idle_time_limit > 0)
	    TIMEOUT(check_idle, NULL, idle_time_limit);

	/*
	 * Set a timeout to close the connection once the maximum
	 * connect time has expired.
	 */
	if (maxconnect > 0)
	    TIMEOUT(connect_time_expired, 0, maxconnect);

	/*
	 * Detach now, if the updetach option was given.
	 */
	if (nodetach == -1)
	    detach();
    }
    ++num_np_up;
}

/*
 * np_down - a network protocol has gone down.
 */
void
np_down(int unit, int proto)
{
    if (--num_np_up == 0 && idle_time_limit > 0) {
	UNTIMEOUT(check_idle, NULL);
    }
}

/*
 * np_finished - a network protocol has finished using the link.
 */
void
np_finished(int unit, int roto)
{
    if (--num_np_open <= 0) {
	/* no further use for the link: shut up shop. */
	lcp_close(0, "No network protocols running");
    }
}

/*
 * check_idle - check whether the link has been idle for long
 * enough that we can shut it down.
 */
static void
check_idle(void *arg)
{
    struct ppp_idle idle;
    time_t itime;

    if (!get_idle_time(0, &idle))
	return;
    itime = MIN(idle.xmit_idle, idle.recv_idle);
    if (itime >= idle_time_limit) {
	/* link is idle: shut it down. */
	syslog(LOG_INFO, "Terminating connection due to lack of activity.");
	lcp_close(0, "Link inactive");
    } else {
	TIMEOUT(check_idle, NULL, idle_time_limit - itime);
    }
}

/*
 * connect_time_expired - log a message and close the connection.
 */
static void
connect_time_expired(void *arg)
{
    syslog(LOG_INFO, "Connect time expired");
    lcp_close(0, "Connect time expired");	/* Close connection */
}

/*
 * auth_check_options - called to check authentication options.
 */
void
auth_check_options(void)
{
    lcp_options *wo = &lcp_wantoptions[0];
    int can_auth;
    ipcp_options *ipwo = &ipcp_wantoptions[0];
    u_int32_t remote;

    /* Default our_name to hostname, and user to our_name */
    if (our_name[0] == 0 || usehostname)
	strlcpy(our_name, hostname, HOST_NAME_MAX+1);
    if (user[0] == 0)
	strlcpy(user, our_name, MAXNAMELEN);

    /* If authentication is required, ask peer for CHAP or PAP. */
    if (auth_required && !wo->neg_chap && !wo->neg_upap) {
	wo->neg_chap = 1;
	wo->neg_upap = 1;
    }

    /*
     * Check whether we have appropriate secrets to use
     * to authenticate the peer.
     */
    can_auth = wo->neg_upap && (uselogin || have_pap_secret());
    if (!can_auth && wo->neg_chap) {
	remote = ipwo->accept_remote? 0: ipwo->hisaddr;
	can_auth = have_chap_secret(remote_name, our_name, remote);
    }

    if (auth_required && !can_auth) {
	option_error("peer authentication required but no suitable secret(s) found\n");
	if (remote_name[0] == 0)
	    option_error("for authenticating any peer to us (%s)\n", our_name);
	else
	    option_error("for authenticating peer %s to us (%s)\n",
			 remote_name, our_name);
	exit(1);
    }

    /*
     * Check whether the user tried to override certain values
     * set by root.
     */
    if (!auth_required && auth_req_info.priv > 0) {
	if (!default_device && devnam_info.priv == 0) {
	    option_error("can't override device name when noauth option used");
	    exit(1);
	}
	if ((connector != NULL && connector_info.priv == 0)
	    || (disconnector != NULL && disconnector_info.priv == 0)
	    || (welcomer != NULL && welcomer_info.priv == 0)) {
	    option_error("can't override connect, disconnect or welcome");
	    option_error("option values when noauth option used");
	    exit(1);
	}
    }
}

/*
 * auth_reset - called when LCP is starting negotiations to recheck
 * authentication options, i.e. whether we have appropriate secrets
 * to use for authenticating ourselves and/or the peer.
 */
void
auth_reset(int unit)

{
    lcp_options *go = &lcp_gotoptions[unit];
    lcp_options *ao = &lcp_allowoptions[0];
    ipcp_options *ipwo = &ipcp_wantoptions[0];
    u_int32_t remote;

    ao->neg_upap = !refuse_pap && (passwd[0] != 0 || get_pap_passwd(NULL));
    ao->neg_chap = !refuse_chap
	&& have_chap_secret(user, remote_name, (u_int32_t)0);

    if (go->neg_upap && !uselogin && !have_pap_secret())
	go->neg_upap = 0;
    if (go->neg_chap) {
	remote = ipwo->accept_remote? 0: ipwo->hisaddr;
	if (!have_chap_secret(remote_name, our_name, remote))
	    go->neg_chap = 0;
    }
}


/*
 * check_passwd - Check the user name and passwd against the PAP secrets
 * file.  If requested, also check against the system password database,
 * and login the user if OK.
 *
 * returns:
 *	UPAP_AUTHNAK: Authentication failed.
 *	UPAP_AUTHACK: Authentication succeeded.
 * In either case, msg points to an appropriate message.
 */
int
check_passwd(int unit, char *auser, int userlen,
    char *apasswd, int passwdlen, char **msg, int *msglen)
{
    int ret;
    char *filename;
    FILE *f;
    struct wordlist *addrs;
    u_int32_t remote;
    ipcp_options *ipwo = &ipcp_wantoptions[unit];
    char passwd[256], user[256];
    char secret[MAXWORDLEN];
    static int attempts = 0;

    /*
     * Make copies of apasswd and auser, then null-terminate them.
     */
    BCOPY(apasswd, passwd, passwdlen);
    passwd[passwdlen] = '\0';
    BCOPY(auser, user, userlen);
    user[userlen] = '\0';
    *msg = (char *) 0;

    /*
     * Open the file of pap secrets and scan for a suitable secret
     * for authenticating this user.
     */
    filename = _PATH_UPAPFILE;
    addrs = NULL;
    ret = UPAP_AUTHACK;
    f = fopen(filename, "r");
    if (f == NULL) {
	syslog(LOG_ERR, "Can't open PAP password file %s: %m", filename);
	ret = UPAP_AUTHNAK;
    } else {
	check_access(f, filename);
	remote = ipwo->accept_remote? 0: ipwo->hisaddr;
	if (scan_authfile(f, user, our_name, remote,
			  secret, &addrs, filename) < 0
	    || (secret[0] != 0 && (cryptpap || strcmp(passwd, secret) != 0)
		&& strcmp(crypt(passwd, secret), secret) != 0)) {
	    syslog(LOG_WARNING, "PAP authentication failure for %s", user);
	    ret = UPAP_AUTHNAK;
	}
	fclose(f);
    }

    if (uselogin && ret == UPAP_AUTHACK) {
	ret = plogin(user, passwd, msg, msglen);
	if (ret == UPAP_AUTHNAK) {
	    syslog(LOG_WARNING, "PAP login failure for %s", user);
	}
    }

    if (ret == UPAP_AUTHNAK) {
        if (*msg == (char *) 0)
	    *msg = "Login incorrect";
	*msglen = strlen(*msg);
	/*
	 * Frustrate passwd stealer programs.
	 * Allow 10 tries, but start backing off after 3 (stolen from login).
	 * On 10'th, drop the connection.
	 */
	if (attempts++ >= 10) {
	    syslog(LOG_WARNING, "%d LOGIN FAILURES ON %s, %s",
		   attempts, devnam, user);
	    quit();
	}
	if (attempts > 3)
	    sleep((u_int) (attempts - 3) * 5);
	if (addrs != NULL)
	    free_wordlist(addrs);

    } else {
	attempts = 0;			/* Reset count */
	if (*msg == (char *) 0)
	    *msg = "Login ok";
	*msglen = strlen(*msg);
	set_allowed_addrs(unit, addrs);
    }

    EXPLICIT_BZERO(passwd, sizeof(passwd));
    EXPLICIT_BZERO(secret, sizeof(secret));

    return ret;
}

/*
 * plogin - Check the user name and password against the system
 * password database, and login the user if OK.
 *
 * returns:
 *	UPAP_AUTHNAK: Login failed.
 *	UPAP_AUTHACK: Login succeeded.
 * In either case, msg points to an appropriate message.
 */

static int
plogin(char *user, char *passwd, char **msg, int *msglen)
{
    struct passwd *pw;
    char *tty;

    pw = getpwnam_shadow(user);
    if (crypt_checkpass(passwd, pw ? pw->pw_passwd : NULL))
	    return UPAP_AUTHNAK;

    /*
     * Write a wtmp entry for this user.
     */

    tty = devnam;
    if (strncmp(tty, "/dev/", 5) == 0)
	tty += 5;
    logwtmp(tty, user, remote_name);		/* Add wtmp login entry */

#if defined(_PATH_LASTLOG)
    {
	    struct lastlog ll;
	    int fd;

	    if ((fd = open(_PATH_LASTLOG, O_RDWR)) >= 0) {
		memset(&ll, 0, sizeof(ll));
		(void)time(&ll.ll_time);
		(void)strncpy(ll.ll_line, tty, sizeof(ll.ll_line));
		(void)pwrite(fd, &ll, sizeof(ll), (off_t)pw->pw_uid *
		    sizeof(ll));
		(void)close(fd);
	    }
    }
#endif


    syslog(LOG_INFO, "user %s logged in", user);
    logged_in = TRUE;

    return (UPAP_AUTHACK);
}

/*
 * plogout - Logout the user.
 */
static void
plogout(void)
{
    char *tty;

    tty = devnam;
    if (strncmp(tty, "/dev/", 5) == 0)
	tty += 5;
    logwtmp(tty, "", "");		/* Wipe out utmp logout entry */

    logged_in = FALSE;
}


/*
 * null_login - Check if a username of "" and a password of "" are
 * acceptable, and iff so, set the list of acceptable IP addresses
 * and return 1.
 */
static int
null_login(int unit)
{
    char *filename;
    FILE *f;
    int i, ret;
    struct wordlist *addrs;
    char secret[MAXWORDLEN];

    /*
     * Open the file of pap secrets and scan for a suitable secret.
     * We don't accept a wildcard client.
     */
    filename = _PATH_UPAPFILE;
    addrs = NULL;
    f = fopen(filename, "r");
    if (f == NULL)
	return 0;
    check_access(f, filename);

    i = scan_authfile(f, "", our_name, (u_int32_t)0, secret, &addrs, filename);
    ret = i >= 0 && (i & NONWILD_CLIENT) != 0 && secret[0] == 0;
    EXPLICIT_BZERO(secret, sizeof(secret));

    if (ret)
	set_allowed_addrs(unit, addrs);
    else
	free_wordlist(addrs);

    fclose(f);
    return ret;
}


/*
 * get_pap_passwd - get a password for authenticating ourselves with
 * our peer using PAP.  Returns 1 on success, 0 if no suitable password
 * could be found.
 */
static int
get_pap_passwd(char *passwd)
{
    char *filename;
    FILE *f;
    int ret;
    char secret[MAXWORDLEN];

    filename = _PATH_UPAPFILE;
    f = fopen(filename, "r");
    if (f == NULL)
	return 0;
    check_access(f, filename);
    ret = scan_authfile(f, user,
			remote_name[0]? remote_name: NULL,
			(u_int32_t)0, secret, NULL, filename);
    fclose(f);
    if (ret < 0)
	return 0;
    if (passwd != NULL)
	strlcpy(passwd, secret, MAXSECRETLEN);
    EXPLICIT_BZERO(secret, sizeof(secret));
    return 1;
}


/*
 * have_pap_secret - check whether we have a PAP file with any
 * secrets that we could possibly use for authenticating the peer.
 */
static int
have_pap_secret(void)
{
    FILE *f;
    int ret;
    char *filename;
    ipcp_options *ipwo = &ipcp_wantoptions[0];
    u_int32_t remote;

    filename = _PATH_UPAPFILE;
    f = fopen(filename, "r");
    if (f == NULL)
	return 0;

    remote = ipwo->accept_remote? 0: ipwo->hisaddr;
    ret = scan_authfile(f, NULL, our_name, remote, NULL, NULL, filename);
    fclose(f);
    if (ret < 0)
	return 0;

    return 1;
}


/*
 * have_chap_secret - check whether we have a CHAP file with a
 * secret that we could possibly use for authenticating `client'
 * on `server'.  Either can be the null string, meaning we don't
 * know the identity yet.
 */
static int
have_chap_secret(char *client, char *server, u_int32_t remote)
{
    FILE *f;
    int ret;
    char *filename;

    filename = _PATH_CHAPFILE;
    f = fopen(filename, "r");
    if (f == NULL)
	return 0;

    if (client[0] == 0)
	client = NULL;
    else if (server[0] == 0)
	server = NULL;

    ret = scan_authfile(f, client, server, remote, NULL, NULL, filename);
    fclose(f);
    if (ret < 0)
	return 0;

    return 1;
}


/*
 * get_secret - open the CHAP secret file and return the secret
 * for authenticating the given client on the given server.
 * (We could be either client or server).
 */
int
get_secret(int unit, char *client, char *server,
    char *secret, int *secret_len, int save_addrs)
{
    FILE *f;
    int ret, len;
    char *filename;
    struct wordlist *addrs;
    char secbuf[MAXWORDLEN];

    filename = _PATH_CHAPFILE;
    addrs = NULL;
    secbuf[0] = 0;

    f = fopen(filename, "r");
    if (f == NULL) {
	syslog(LOG_ERR, "Can't open chap secret file %s: %m", filename);
	return 0;
    }
    check_access(f, filename);

    ret = scan_authfile(f, client, server, (u_int32_t)0,
			secbuf, &addrs, filename);
    fclose(f);
    if (ret < 0)
	return 0;

    if (save_addrs)
	set_allowed_addrs(unit, addrs);

    len = strlen(secbuf);
    if (len > MAXSECRETLEN) {
	syslog(LOG_ERR, "Secret for %s on %s is too long", client, server);
	len = MAXSECRETLEN;
    }
    BCOPY(secbuf, secret, len);
    EXPLICIT_BZERO(secbuf, sizeof(secbuf));
    *secret_len = len;

    return 1;
}

/*
 * set_allowed_addrs() - set the list of allowed addresses.
 */
static void
set_allowed_addrs(int unit, struct wordlist *addrs)
{
    if (addresses[unit] != NULL)
	free_wordlist(addresses[unit]);
    addresses[unit] = addrs;

    /*
     * If there's only one authorized address we might as well
     * ask our peer for that one right away
     */
    if (addrs != NULL && addrs->next == NULL) {
	char *p = addrs->word;
	struct ipcp_options *wo = &ipcp_wantoptions[unit];
	struct in_addr ina;
	struct hostent *hp;

	if (*p != '!' && *p != '-' && !ISWILD(p) && strchr(p, '/') == NULL) {
	    hp = gethostbyname(p);
	    if (hp != NULL && hp->h_addrtype == AF_INET)
		wo->hisaddr = *(u_int32_t *)hp->h_addr;
	    else if (inet_pton(AF_INET, p, &ina) == 1)
		wo->hisaddr = ina.s_addr;
	}
    }
}

/*
 * auth_ip_addr - check whether the peer is authorized to use
 * a given IP address.  Returns 1 if authorized, 0 otherwise.
 */
int
auth_ip_addr(int unit, u_int32_t addr)
{
    return ip_addr_check(addr, addresses[unit]);
}

static int
ip_addr_check(u_int32_t addr, struct wordlist *addrs)
{
    u_int32_t mask, ah;
    struct in_addr ina;
    int accept, r = 1;
    char *ptr_word, *ptr_mask;
    struct hostent *hp;

    /* don't allow loopback or multicast address */
    if (bad_ip_adrs(addr))
	return 0;

    if (addrs == NULL)
	return !auth_required;		/* no addresses authorized */

    for (; addrs != NULL; addrs = addrs->next) {
	/* "-" means no addresses authorized, "*" means any address allowed */
	ptr_word = addrs->word;
	if (strcmp(ptr_word, "-") == 0)
	    break;
	if (strcmp(ptr_word, "*") == 0)
	    return 1;

	accept = 1;
	if (*ptr_word == '!') {
	    accept = 0;
	    ++ptr_word;
	}

	mask = ~ (u_int32_t) 0;
	ptr_mask = strchr (ptr_word, '/');
	if (ptr_mask != NULL) {
	    int bit_count;

	    bit_count = (int) strtol (ptr_mask+1, (char **) 0, 10);
	    if (bit_count <= 0 || bit_count > 32) {
		syslog (LOG_WARNING,
			"invalid address length %s in auth. address list",
			ptr_mask);
		continue;
	    }
	    *ptr_mask = '\0';
	    mask <<= 32 - bit_count;
	}

	hp = gethostbyname(ptr_word);
	if (hp != NULL && hp->h_addrtype == AF_INET) {
	    ina.s_addr = *(u_int32_t *)hp->h_addr;
	} else {
	    r = inet_pton(AF_INET, ptr_word, &ina);
	    if (ptr_mask == NULL) {
		/* calculate appropriate mask for net */
		ah = ntohl(ina.s_addr);
		if (IN_CLASSA(ah))
		    mask = IN_CLASSA_NET;
		else if (IN_CLASSB(ah))
		    mask = IN_CLASSB_NET;
		else if (IN_CLASSC(ah))
		    mask = IN_CLASSC_NET;
	    }
	}

	if (ptr_mask != NULL)
	    *ptr_mask = '/';

	if (r != 1)
	    syslog (LOG_WARNING,
		    "unknown host %s in auth. address list",
		    addrs->word);
	else
	    /* Here ina.s_addr and addr are in network byte order,
	       and mask is in host order. */
	    if (((addr ^ ina.s_addr) & htonl(mask)) == 0)
		return accept;
    }
    return 0;			/* not in list => can't have it */
}

/*
 * bad_ip_adrs - return 1 if the IP address is one we don't want
 * to use, such as an address in the loopback net or a multicast address.
 * addr is in network byte order.
 */
int
bad_ip_adrs(u_int32_t addr)
{
    addr = ntohl(addr);
    return (addr >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET
	|| IN_MULTICAST(addr);
}

/*
 * check_access - complain if a secret file has too-liberal permissions.
 */
void
check_access(FILE *f, char *filename)
{
    struct stat sbuf;

    if (fstat(fileno(f), &sbuf) < 0) {
	syslog(LOG_WARNING, "cannot stat secret file %s: %m", filename);
    } else if ((sbuf.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
	syslog(LOG_WARNING, "Warning - secret file %s has world and/or group access", filename);
    }
}


/*
 * scan_authfile - Scan an authorization file for a secret suitable
 * for authenticating `client' on `server'.  The return value is -1
 * if no secret is found, otherwise >= 0.  The return value has
 * NONWILD_CLIENT set if the secret didn't have "*" for the client, and
 * NONWILD_SERVER set if the secret didn't have "*" for the server.
 * Any following words on the line (i.e. address authorization
 * info) are placed in a wordlist and returned in *addrs.
 */
static int
scan_authfile(FILE *f, char *client, char *server, u_int32_t ipaddr,
    char *secret, struct wordlist **addrs, char *filename)
{
    int newline, xxx;
    int got_flag, best_flag;
    FILE *sf;
    struct wordlist *ap, *addr_list, *alist, *alast;
    char word[MAXWORDLEN];
    char atfile[MAXWORDLEN];
    char lsecret[MAXWORDLEN];

    if (addrs != NULL)
	*addrs = NULL;
    addr_list = NULL;
    if (!getword(f, word, &newline, filename))
	return -1;		/* file is empty??? */
    newline = 1;
    best_flag = -1;
    for (;;) {
	/*
	 * Skip until we find a word at the start of a line.
	 */
	while (!newline && getword(f, word, &newline, filename))
	    ;
	if (!newline)
	    break;		/* got to end of file */

	/*
	 * Got a client - check if it's a match or a wildcard.
	 */
	got_flag = 0;
	if (client != NULL && strcmp(word, client) != 0 && !ISWILD(word)) {
	    newline = 0;
	    continue;
	}
	if (!ISWILD(word))
	    got_flag = NONWILD_CLIENT;

	/*
	 * Now get a server and check if it matches.
	 */
	if (!getword(f, word, &newline, filename))
	    break;
	if (newline)
	    continue;
	if (server != NULL && strcmp(word, server) != 0 && !ISWILD(word))
	    continue;
	if (!ISWILD(word))
	    got_flag |= NONWILD_SERVER;

	/*
	 * Got some sort of a match - see if it's better than what
	 * we have already.
	 */
	if (got_flag <= best_flag)
	    continue;

	/*
	 * Get the secret.
	 */
	if (!getword(f, word, &newline, filename))
	    break;
	if (newline)
	    continue;

	/*
	 * Special syntax: @filename means read secret from file.
	 */
	if (word[0] == '@') {
	    strlcpy(atfile, word+1, sizeof atfile);
	    if ((sf = fopen(atfile, "r")) == NULL) {
		syslog(LOG_WARNING, "can't open indirect secret file %s",
		       atfile);
		continue;
	    }
	    check_access(sf, atfile);
	    if (!getword(sf, word, &xxx, atfile)) {
		syslog(LOG_WARNING, "no secret in indirect secret file %s",
		       atfile);
		fclose(sf);
		continue;
	    }
	    fclose(sf);
	}
	if (secret != NULL)
	    strlcpy(lsecret, word, sizeof lsecret);

	/*
	 * Now read address authorization info and make a wordlist.
	 */
	alist = alast = NULL;
	for (;;) {
	    size_t wordlen;

	    if (!getword(f, word, &newline, filename) || newline)
		break;
	    wordlen = strlen(word);	/* NUL in struct wordlist */
	    ap = (struct wordlist *) malloc(sizeof(struct wordlist) +
		wordlen);

	    if (ap == NULL)
		novm("authorized addresses");
	    ap->next = NULL;
	    strlcpy(ap->word, word, wordlen + 1);
	    if (alist == NULL)
		alist = ap;
	    else
		alast->next = ap;
	    alast = ap;
	}

	/*
	 * Check if the given IP address is allowed by the wordlist.
	 */
	if (ipaddr != 0 && !ip_addr_check(ipaddr, alist)) {
	    free_wordlist(alist);
	    continue;
	}

	/*
	 * This is the best so far; remember it.
	 */
	best_flag = got_flag;
	if (addr_list)
	    free_wordlist(addr_list);
	addr_list = alist;
	if (secret != NULL)
	    strlcpy(secret, lsecret, MAXWORDLEN);

	if (!newline)
	    break;
    }

    if (addrs != NULL)
	*addrs = addr_list;
    else if (addr_list != NULL)
	free_wordlist(addr_list);

    return best_flag;
}

/*
 * free_wordlist - release memory allocated for a wordlist.
 */
static void
free_wordlist(struct wordlist *wp)
{
    struct wordlist *next;

    while (wp != NULL) {
	next = wp->next;
	free(wp);
	wp = next;
    }
}

/*
 * auth_script - execute a script with arguments
 * interface-name peer-name real-user tty speed
 */
static void
auth_script(char *script)
{
    char strspeed[32];
    struct passwd *pw;
    char struid[32];
    char *user_name;
    char *argv[8];

    if ((pw = getpwuid(getuid())) != NULL && pw->pw_name != NULL)
	user_name = pw->pw_name;
    else {
	snprintf(struid, sizeof struid, "%u", getuid());
	user_name = struid;
    }
    snprintf(strspeed, sizeof strspeed, "%d", baud_rate);

    argv[0] = script;
    argv[1] = ifname;
    argv[2] = peer_authname;
    argv[3] = user_name;
    argv[4] = devnam;
    argv[5] = strspeed;
    argv[6] = NULL;

    run_program(script, argv, 0);
}
