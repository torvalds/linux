/*	$OpenBSD: session.h,v 1.192 2025/08/21 15:15:25 claudio Exp $ */

/*
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

#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

#define	MAX_BACKLOG			5
#define	INTERVAL_CONNECTRETRY		120
#define	INTERVAL_HOLD_INITIAL		240
#define	INTERVAL_HOLD			90
#define	INTERVAL_IDLE_HOLD_INITIAL	30
#define	INTERVAL_HOLD_DEMOTED		60
#define	INTERVAL_STALE			180
#define	INTERVAL_SESSION_DOWN		3600
#define	MAX_IDLE_HOLD			3600
#define	MSGSIZE_HEADER			19
#define	MSGSIZE_HEADER_MARKER		16
#define	MSGSIZE_NOTIFICATION_MIN	21	/* 19 hdr + 1 code + 1 sub */
#define	MSGSIZE_OPEN_MIN		29
#define	MSGSIZE_UPDATE_MIN		23
#define	MSGSIZE_KEEPALIVE		MSGSIZE_HEADER
#define	MSGSIZE_RREFRESH		(MSGSIZE_HEADER + 4)
#define	MSGSIZE_RREFRESH_MIN		MSGSIZE_RREFRESH
#define	MSG_PROCESS_LIMIT		25
#define	SESSION_CLEAR_DELAY		5

enum session_state {
	STATE_NONE,
	STATE_IDLE,
	STATE_CONNECT,
	STATE_ACTIVE,
	STATE_OPENSENT,
	STATE_OPENCONFIRM,
	STATE_ESTABLISHED
};

enum session_events {
	EVNT_NONE,
	EVNT_START,
	EVNT_STOP,
	EVNT_CON_OPEN,
	EVNT_CON_CLOSED,
	EVNT_CON_OPENFAIL,
	EVNT_CON_FATAL,
	EVNT_TIMER_CONNRETRY,
	EVNT_TIMER_HOLDTIME,
	EVNT_TIMER_KEEPALIVE,
	EVNT_TIMER_SENDHOLD,
	EVNT_RCVD_OPEN,
	EVNT_RCVD_KEEPALIVE,
	EVNT_RCVD_UPDATE,
	EVNT_RCVD_NOTIFICATION,
	EVNT_RCVD_GRACE_NOTIFICATION,
};

enum msg_type {
	BGP_OPEN = 1,
	BGP_UPDATE,
	BGP_NOTIFICATION,
	BGP_KEEPALIVE,
	BGP_RREFRESH
};

enum suberr_header {
	ERR_HDR_SYNC = 1,
	ERR_HDR_LEN,
	ERR_HDR_TYPE
};

enum suberr_open {
	ERR_OPEN_VERSION = 1,
	ERR_OPEN_AS,
	ERR_OPEN_BGPID,
	ERR_OPEN_OPT,
	ERR_OPEN_AUTH,			/* deprecated */
	ERR_OPEN_HOLDTIME,
	ERR_OPEN_CAPA,
	ERR_OPEN_ROLE = 11,
};

enum suberr_fsm {
	ERR_FSM_UNSPECIFIC = 0,
	ERR_FSM_UNEX_OPENSENT,
	ERR_FSM_UNEX_OPENCONFIRM,
	ERR_FSM_UNEX_ESTABLISHED
};

enum opt_params {
	OPT_PARAM_NONE,
	OPT_PARAM_AUTH,
	OPT_PARAM_CAPABILITIES,
	OPT_PARAM_EXT_LEN=255,
};

struct bgpd_sysdep {
	uint8_t			no_pfkey;
	uint8_t			no_md5sig;
};

struct ctl_conn {
	TAILQ_ENTRY(ctl_conn)	entry;
	struct imsgbuf		imsgbuf;
	int			restricted;
	int			throttled;
	int			terminate;
};

struct peer_stats {
	unsigned long long	 msg_rcvd_open;
	unsigned long long	 msg_rcvd_update;
	unsigned long long	 msg_rcvd_notification;
	unsigned long long	 msg_rcvd_keepalive;
	unsigned long long	 msg_rcvd_rrefresh;
	unsigned long long	 msg_sent_open;
	unsigned long long	 msg_sent_update;
	unsigned long long	 msg_sent_notification;
	unsigned long long	 msg_sent_keepalive;
	unsigned long long	 msg_sent_rrefresh;
	unsigned long long	 refresh_rcvd_req;
	unsigned long long	 refresh_rcvd_borr;
	unsigned long long	 refresh_rcvd_eorr;
	unsigned long long	 refresh_sent_req;
	unsigned long long	 refresh_sent_borr;
	unsigned long long	 refresh_sent_eorr;
	unsigned long long	 prefix_rcvd_update;
	unsigned long long	 prefix_rcvd_withdraw;
	unsigned long long	 prefix_rcvd_eor;
	unsigned long long	 prefix_sent_update;
	unsigned long long	 prefix_sent_withdraw;
	unsigned long long	 prefix_sent_eor;
	monotime_t		 last_updown;
	monotime_t		 last_read;
	monotime_t		 last_write;
	uint32_t		 msg_queue_len;
	uint32_t		 prefix_cnt;
	uint32_t		 prefix_out_cnt;
	uint32_t		 pending_update;
	uint32_t		 pending_withdraw;
	uint8_t			 last_sent_errcode;
	uint8_t			 last_sent_suberr;
	uint8_t			 last_rcvd_errcode;
	uint8_t			 last_rcvd_suberr;
	char			 last_reason[REASON_LEN];
};

struct auth_state {
	struct bgpd_addr	local_addr;
	struct bgpd_addr	remote_addr;
	uint32_t		spi_in;
	uint32_t		spi_out;
	enum auth_method	method;
	uint8_t			established;
};

enum Timer {
	Timer_None,
	Timer_ConnectRetry,
	Timer_Keepalive,
	Timer_Hold,
	Timer_SendHold,
	Timer_IdleHold,
	Timer_IdleHoldReset,
	Timer_CarpUndemote,
	Timer_RestartTimeout,
	Timer_SessionDown,
	Timer_Rtr_Refresh,
	Timer_Rtr_Retry,
	Timer_Rtr_Expire,
	Timer_Rtr_Active,
	Timer_Max
};

struct timer {
	TAILQ_ENTRY(timer)	entry;
	enum Timer		type;
	monotime_t		val;
};

TAILQ_HEAD(timer_head, timer);

struct peer {
	struct peer_config	 conf;
	struct peer_stats	 stats;
	RB_ENTRY(peer)		 entry;
	struct {
		struct capabilities	ann;
		struct capabilities	peer;
		struct capabilities	neg;
	}			 capa;
	struct auth_state	 auth_state;
	struct auth_config	 auth_conf;
	struct bgpd_addr	 local;
	struct bgpd_addr	 local_alt;
	struct bgpd_addr	 remote;
	struct timer_head	 timers;
	struct msgbuf		*wbuf;
	struct peer		*template;
	int			 fd;
	int			 lasterr;
	u_int			 errcnt;
	u_int			 IdleHoldTime;
	unsigned int		 if_scope;	/* interface scope for IPv6 */
	uint32_t		 local_bgpid;
	uint32_t		 remote_bgpid;
	enum session_state	 state;
	enum session_state	 prev_state;
	enum reconf_action	 reconf_action;
	enum role		 remote_role;
	uint16_t		 short_as;
	uint16_t		 holdtime;
	uint16_t		 local_port;
	uint16_t		 remote_port;
	uint8_t			 depend_ok;
	uint8_t			 demoted;
	uint8_t			 passive;
	uint8_t			 throttled;
	uint8_t			 rpending;
	uint8_t			 rdesession;
};

extern monotime_t		 pauseaccept;

struct ctl_timer {
	enum Timer	type;
	monotime_t	val;
};

/* carp.c */
int	 carp_demote_init(char *, int);
void	 carp_demote_shutdown(void);
int	 carp_demote_get(char *);
int	 carp_demote_set(char *, int);

/* config.c */
void	 merge_config(struct bgpd_config *, struct bgpd_config *);
void	 free_deleted_peers(struct bgpd_config *);
int	 prepare_listeners(struct bgpd_config *);

/* control.c */
int	control_check(char *);
int	control_init(int, char *);
int	control_listen(int);
size_t	control_fill_pfds(struct pollfd *, size_t);
void	control_shutdown(int);
int	control_dispatch_msg(struct pollfd *, struct peer_head *);
unsigned int	control_accept(int, int);

/* log.c */
char	*log_fmt_peer(const struct peer_config *);
void	 log_statechange(struct peer *,  enum session_state,
	    enum session_events);
void	 log_notification(const struct peer *, uint8_t, uint8_t,
	    const struct ibuf *, const char *);
void	 log_conn_attempt(const struct peer *, struct sockaddr *,
	    socklen_t);

/* mrt.c */
void	 mrt_dump_bgp_msg(struct mrt *, struct ibuf *, struct peer *,
	    enum msg_type);
void	 mrt_dump_state(struct mrt *, struct peer *);
void	 mrt_done(struct mrt *);

/* pfkey.c */
struct sadb_msg;
int	pfkey_read(int, struct sadb_msg *);
int	pfkey_establish(struct auth_state *, struct auth_config *,
	    const struct bgpd_addr *, const struct bgpd_addr *);
int	pfkey_remove(struct auth_state *);
int	pfkey_init(void);
int	pfkey_send_conf(struct imsgbuf *, uint32_t, struct auth_config *);
int	pfkey_recv_conf(struct peer *, struct imsg *);
int	tcp_md5_check(int, struct auth_config *);
int	tcp_md5_set(int, struct auth_config *, struct bgpd_addr *);
int	tcp_md5_prep_listener(struct listen_addr *, struct peer_head *);
void	tcp_md5_add_listener(struct bgpd_config *, struct peer *);
void	tcp_md5_del_listener(struct bgpd_config *, struct peer *);

/* printconf.c */
void	print_config(struct bgpd_config *, struct rib_names *);

/* rde.c */
void	rde_main(int, int);

/* rtr_proto.c */
struct rtr_session;
size_t			 rtr_count(void);
void			 rtr_check_events(struct pollfd *, size_t);
size_t			 rtr_poll_events(struct pollfd *, size_t, monotime_t *);
struct rtr_session	*rtr_new(uint32_t, struct rtr_config_msg *);
struct rtr_session	*rtr_get(uint32_t);
void			 rtr_free(struct rtr_session *);
void			 rtr_open(struct rtr_session *, int);
void			 rtr_config_prep(void);
void			 rtr_config_merge(void);
void			 rtr_config_keep(struct rtr_session *,
			     struct rtr_config_msg *);
void			 rtr_roa_merge(struct roa_tree *);
void			 rtr_aspa_merge(struct aspa_tree *);
void			 rtr_shutdown(void);
void			 rtr_show(struct rtr_session *, pid_t);

/* rtr.c */
void	rtr_sem_acquire(int);
void	rtr_sem_release(int);
void	rtr_roa_insert(struct roa_tree *, struct roa *);
void	rtr_aspa_insert(struct aspa_tree *, struct aspa_set *);
void	rtr_main(int, int);
void	rtr_imsg_compose(int, uint32_t, pid_t, void *, size_t);
void	rtr_recalc(void);

/* session.c */
RB_PROTOTYPE(peer_head, peer, entry, peer_compare);

void		 session_main(int, int);
int		 session_neighbor_rrefresh(struct peer *p);
void		 get_alternate_addr(struct bgpd_addr *, struct bgpd_addr *,
		    struct bgpd_addr *, unsigned int *);
struct peer	*getpeerbydesc(struct bgpd_config *, const char *);
struct peer	*getpeerbyip(struct bgpd_config *, struct sockaddr *);
struct peer	*getpeerbyid(struct bgpd_config *, uint32_t);
void		 session_handle_update(struct peer *, struct ibuf *);
void		 session_handle_rrefresh(struct peer *, struct route_refresh *);
void		 session_graceful_restart(struct peer *);
void		 session_graceful_flush(struct peer *, uint8_t, const char *);
void		 session_mrt_dump_state(struct peer *);
void		 session_mrt_dump_bgp_msg(struct peer *, struct ibuf *,
		    enum msg_type, enum directions);
int		 peer_matched(struct peer *, struct ctl_neighbor *);
int		 imsg_ctl_parent(struct imsg *);
int		 imsg_ctl_rde(struct imsg *);
int		 imsg_ctl_rde_msg(int, uint32_t, pid_t);
int		 session_connect(struct peer *);
void		 session_close(struct peer *);
void		 session_up(struct peer *);
void		 session_down(struct peer *);
void		 session_demote(struct peer *, int);
void		 session_md5_reload(struct peer *);
void		 session_stop(struct peer *, uint8_t, const char *);
struct bgpd_addr *session_localaddr(struct peer *);

/* session_bgp.c */
void	session_open(struct peer *);
void	session_keepalive(struct peer *);
void	session_update(struct peer *, struct ibuf *);
void	session_notification(struct peer *, uint8_t, uint8_t, struct ibuf *);
void	session_notification_data(struct peer *, uint8_t, uint8_t, void *,
	    size_t);
void	session_rrefresh(struct peer *, uint8_t, uint8_t);
int	session_dispatch_msg(struct pollfd *, struct peer *);
void	session_process_msg(struct peer *);

struct ibuf	*parse_header(struct ibuf *, void *, int *);

void	start_timer_sendholdtime(struct peer *);
void	bgp_fsm(struct peer *, enum session_events, struct ibuf *);
void    change_state(struct peer *, enum session_state, enum session_events);

/* timer.c */
struct timer	*timer_get(struct timer_head *, enum Timer);
struct timer	*timer_nextisdue(struct timer_head *, monotime_t);
monotime_t	 timer_nextduein(struct timer_head *);
int		 timer_running(struct timer_head *, enum Timer, monotime_t *);
void		 timer_set(struct timer_head *, enum Timer, u_int);
void		 timer_stop(struct timer_head *, enum Timer);
void		 timer_remove(struct timer_head *, enum Timer);
void		 timer_remove_all(struct timer_head *);
