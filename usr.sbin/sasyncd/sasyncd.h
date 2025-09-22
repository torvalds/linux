/*	$OpenBSD: sasyncd.h,v 1.19 2018/04/10 15:58:21 cheloha Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Multicom Security AB.
 */


#include <netinet/in.h>		/* in_port_t and sa_family_t */
#include <sys/queue.h>

enum RUNSTATE		{ INIT = 0, SLAVE, MASTER, FAIL };
#define CARPSTATES	{ "INIT", "SLAVE", "MASTER", "FAIL" }

struct syncpeer;
struct timespec;

struct cfgstate {
	enum RUNSTATE	 runstate;
	enum RUNSTATE	 lockedstate;
	int		 debug;
	int		 verboselevel;
	u_int32_t	 flags;

	char		*carp_ifname;
	char		*carp_ifgroup;
	int		 carp_ifindex;

	char		*sharedkey;
	int		 sharedkey_len;

	int		 pfkey_socket;

	int		 route_socket;

	char		*listen_on;
	in_port_t	 listen_port;
	sa_family_t	 listen_family;

	int		 peercnt;
	LIST_HEAD(, syncpeer) peerlist;
};

/* flags */
#define	FM_STARTUP	0x0000
#define FM_NEVER	0x0001
#define FM_SYNC		0x0002
#define FM_MASK		0x0003

/* Do not sync SAs to/from our peers. */
#define SKIP_LOCAL_SAS	0x0004

/* Control isakmpd or iked */
#define CTL_NONE	0x0000
#define CTL_ISAKMPD	0x0008
#define CTL_IKED	0x0010
#define CTL_DEFAULT	CTL_ISAKMPD
#define CTL_MASK	0x0018

extern struct cfgstate	cfgstate;
extern int		carp_demoted;

#define SASYNCD_USER	"_isakmpd"
#define SASYNCD_CFGFILE	"/etc/sasyncd.conf"

#define CARP_DEFAULT_INTERVAL	10
#define SASYNCD_DEFAULT_PORT	500

/*
 * sasyncd "protocol" definition
 *
 * Message format:
 *   u_int32_t	type
 *   u_int32_t	len
 *   raw        data
 */

/* sasyncd protocol message types */
#define MSG_SYNCCTL	0
#define MSG_PFKEYDATA	1
#define MSG_MAXTYPE	1	/* Increase when new types are added. */


#define CARP_DEC	-1
#define CARP_INC	1

#define CARP_DEMOTE_MAXTIME	60

/* conf.c */
int		conf_parse_file(char *);

/* carp.c */
int		carp_init(void);
void		carp_check_state(void);
void		carp_demote(int, int);
void		carp_update_state(enum RUNSTATE);
void		carp_set_rfd(fd_set *);
void		carp_read_message(fd_set *);
const char*	carp_state_name(enum RUNSTATE);
void		control_setrun(void);


/* log.c */
/*
 * Log levels for log_msg(level, ...) roughly means:
 *  0 = errors and other important messages
 *  1 = state changes, ctl message errors and dis-/connecting peers
 *  2 = configuration and initialization messages
 *  3 = PF_KEY logging
 *  4 = misc network
 *  5 = crypto
 *  6 = timers
 */
void	log_init(char *);
void	log_msg(int, const char *, ...)
		__attribute__((__format__ (printf, 2, 3)));
void	log_err(const char *, ...)
		__attribute__((__format__ (printf, 1, 2)));

/* monitor.c */
pid_t	monitor_init(void);
void	monitor_loop(void);
void	monitor_carpdemote(void *);
void	monitor_carpundemote(void *);

/* net.c */
void	dump_buf(int, u_int8_t *, u_int32_t, char *);
void	net_ctl_update_state(void);
int	net_init(void);
void	net_handle_messages(fd_set *);
int	net_queue(struct syncpeer *, u_int32_t, u_int8_t *, u_int32_t);
void	net_send_messages(fd_set *);
int	net_set_rfds(fd_set *);
int	net_set_pending_wfds(fd_set *);
void	net_shutdown(void);

/* pfkey.c */
int	pfkey_init(int);
int	pfkey_queue_message(u_int8_t *, u_int32_t);
void	pfkey_read_message(fd_set *);
void	pfkey_send_message(fd_set *);
void	pfkey_set_rfd(fd_set *);
void	pfkey_set_pending_wfd(fd_set *);
int	pfkey_set_promisc(void);
void	pfkey_shutdown(void);
void	pfkey_snapshot(void *);

/* timer.c */
void	timer_init(void);
void	timer_next_event(struct timespec *);
void	timer_run(void);
int	timer_add(char *, u_int32_t, void (*)(void *), void *);
