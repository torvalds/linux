/*	$OpenBSD: iscsid.h,v 1.24 2025/01/28 20:41:44 claudio Exp $ */

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
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

#define ISCSID_DEVICE	"/dev/vscsi0"
#define ISCSID_CONTROL	"/var/run/iscsid.sock"
#define ISCSID_CONFIG	"/etc/iscsi.conf"
#define ISCSID_USER	"_iscsid"

#define ISCSID_BASE_NAME	"iqn.1995-11.org.openbsd.iscsid"
#define ISCSID_DEF_CONNS	8
#define ISCSID_HOLD_TIME_MAX	128

#define PDU_READ_SIZE		(256 * 1024)
#define CONTROL_READ_SIZE	8192
#define PDU_MAXIOV		5
#define PDU_WRIOV		(PDU_MAXIOV * 8)

#define PDU_HEADER	0
#define PDU_AHS		1
#define PDU_HDIGEST	2
#define PDU_DATA	3
#define PDU_DDIGEST	4

#define PDU_LEN(x)	((((x) + 3) / 4) * 4)

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

/*
 * Common control message header.
 * A message can consist of up to 3 parts with specified length.
 */
struct ctrlmsghdr {
	u_int16_t	type;
	u_int16_t	len[3];
};

struct ctrldata {
	void		*buf;
	size_t		 len;
};

#define CTRLARGV(x...)	((struct ctrldata []){ x })

/* Control message types */
#define CTRL_SUCCESS		1
#define CTRL_FAILURE		2
#define CTRL_INPROGRESS		3
#define CTRL_INITIATOR_CONFIG	4
#define CTRL_SESSION_CONFIG	5
#define CTRL_LOG_VERBOSE	6
#define CTRL_VSCSI_STATS	7
#define CTRL_SHOW_SUM		8
#define CTRL_SESS_POLL		9

TAILQ_HEAD(session_head, session);
TAILQ_HEAD(connection_head, connection);
TAILQ_HEAD(pduq, pdu);
TAILQ_HEAD(taskq, task);

/* as in tcp_seq.h */
#define SEQ_LT(a,b)	((int)((a)-(b)) < 0)
#define SEQ_LEQ(a,b)	((int)((a)-(b)) <= 0)
#define SEQ_GT(a,b)	((int)((a)-(b)) > 0)
#define SEQ_GEQ(a,b)	((int)((a)-(b)) >= 0)

#define SESS_INIT		0x0001
#define SESS_FREE		0x0002
#define SESS_LOGGED_IN		0x0004
#define SESS_FAILED		0x0008
#define SESS_ANYSTATE		0xffff
#define SESS_RUNNING		(SESS_FREE | SESS_LOGGED_IN | SESS_FAILED)

#define CONN_FREE		0x0001	/* S1 = R3 */
#define CONN_XPT_WAIT		0x0002	/* S2 */
#define CONN_XPT_UP		0x0004	/* S3 */
#define CONN_IN_LOGIN		0x0008	/* S4 */
#define CONN_LOGGED_IN		0x0010	/* S5 */
#define CONN_IN_LOGOUT		0x0020	/* S6 */
#define CONN_LOGOUT_REQ		0x0040	/* S7 */
#define CONN_CLEANUP_WAIT	0x0080	/* S8 = R1 */
#define CONN_IN_CLEANUP		0x0100	/* R2 */
#define CONN_ANYSTATE		0xffff
#define CONN_RUNNING		(CONN_LOGGED_IN | CONN_LOGOUT_REQ)
#define CONN_FAILED		(CONN_CLEANUP_WAIT | CONN_IN_CLEANUP)
#define CONN_NEVER_LOGGED_IN	(CONN_FREE | CONN_XPT_WAIT | CONN_XPT_UP | \
				    CONN_IN_LOGIN)

enum c_event {
	CONN_EV_FAIL,
	CONN_EV_FREE,
	CONN_EV_CONNECT,
	CONN_EV_CONNECTED,
	CONN_EV_LOGGED_IN,
	CONN_EV_REQ_LOGOUT,
	CONN_EV_LOGOUT,
	CONN_EV_LOGGED_OUT,
	CONN_EV_CLOSED,
	CONN_EV_CLEANING_UP
};

enum s_event {
	SESS_EV_START,
	SESS_EV_STOP,
	SESS_EV_CONN_LOGGED_IN,
	SESS_EV_CONN_FAIL,
	SESS_EV_CONN_CLOSED,
	SESS_EV_REINSTATEMENT,
	SESS_EV_TIMEOUT,
	SESS_EV_CLOSED,
	SESS_EV_FAIL,
	SESS_EV_FREE
};

#define SESS_ACT_UP		0
#define SESS_ACT_DOWN		1

struct pdu {
	TAILQ_ENTRY(pdu)	 entry;
	struct iovec		 iov[PDU_MAXIOV];
	size_t			 resid;
};

struct pdu_readbuf {
	char		*buf;
	size_t		 size;
	size_t		 rpos;
	size_t		 wpos;
	struct pdu	*wip;
};

struct connection_config {
	/* values inherited from session_config */
	struct sockaddr_storage	 TargetAddr;	/* IP:port of target */
	struct sockaddr_storage	 LocalAddr;	/* IP:port of target */
};

struct initiator_config {
	u_int32_t		isid_base;	/* only 24 bits */
	u_int16_t		isid_qual;
	u_int16_t		pad;
};

struct session_config {
	/* unique session ID */
	char			SessionName[32];
	/*
	 * I = initialize only, L = leading only
	 * S = session wide, C = connection only
	 */
	struct connection_config connection;

	char			*TargetName;	/* String: IS */

	char			*InitiatorName;	/* String: IS */

	u_int16_t		 MaxConnections;
				 /* 1, 1-65535 (min()): LS */
	u_int8_t		 HeaderDigest;
				 /* None , (None|CRC32): IC */
	u_int8_t		 DataDigest;
				 /* None , (None|CRC32): IC */
	u_int8_t		 SessionType;
				 /* Normal, (Discovery|Normal): LS */
	u_int8_t		 disabled;
};

#define DIGEST_NONE		0x1
#define DIGEST_CRC32C		0x2

#define SESSION_TYPE_NORMAL	0
#define SESSION_TYPE_DISCOVERY	1

struct session_params {
	u_int32_t		 MaxBurstLength;
				 /* 262144, 512-to-(2**24-1) (min()): LS */
	u_int32_t		 FirstBurstLength;
				 /* 65536, 512-to-(2**24-1) (min()): LS */
	u_int16_t		 DefaultTime2Wait;
				 /* 2, 0-to-3600 (max()): LS */
	u_int16_t		 DefaultTime2Retain;
				 /* 20, 0-to-3600 (min()): LS */
	u_int16_t		 MaxOutstandingR2T;
				 /* 1, 1-to-65535 (min()): LS */
	u_int16_t		 TargetPortalGroupTag;
				 /* 1- 65535: IS */
	u_int16_t		 MaxConnections;
				 /* 1, 1-65535 (min()): LS */
	u_int8_t		 InitialR2T;
				 /* yes, bool (||): LS  */
	u_int8_t		 ImmediateData;
				 /* yes, bool (&&): LS */
	u_int8_t		 DataPDUInOrder;
				 /* yes, bool (||): LS */
	u_int8_t		 DataSequenceInOrder;
				 /* yes, bool (||): LS */
	u_int8_t		 ErrorRecoveryLevel;
				 /* 0, 0 - 2 (min()): LS */
};

struct connection_params {
	u_int32_t		 MaxRecvDataSegmentLength;
				 /* 8192, 512-to-(2**24-1): C */
				 /* inherited from session_config */
	u_int8_t		 HeaderDigest;
	u_int8_t		 DataDigest;
};

struct initiator {
	struct session_head	sessions;
	struct initiator_config	config;
	u_int			target;
};

struct sessev {
	struct event		 ev;
	struct session		*sess;
	struct connection	*conn;
	enum s_event		 event;
};

struct session {
	TAILQ_ENTRY(session)	 entry;
	struct sessev		 sev;
	struct connection_head	 connections;
	struct taskq		 tasks;
	struct session_config	 config;
	struct session_params	 mine;
	struct session_params	 his;
	struct session_params	 active;
	u_int32_t		 cmdseqnum;
	u_int32_t		 itt;
	u_int32_t		 isid_base;	/* only 24 bits */
	u_int16_t		 isid_qual;	/* inherited from initiator */
	u_int16_t		 tsih;		/* target session id handle */
	u_int			 target;
	int			 holdTimer;	/* internal hold timer */
	int			 state;
	int			 action;
};

struct session_poll {
	int session_count;
	int session_init_count;
	int session_running_count;
	int conn_logged_in_count;
	int conn_failed_count;
	int conn_waiting_count;
	int sess_conn_status;
};

struct connection {
	struct event		 ev;
	struct event		 wev;
	struct sessev		 sev;
	TAILQ_ENTRY(connection)	 entry;
	struct connection_params mine;
	struct connection_params his;
	struct connection_params active;
	struct connection_config config;
	struct pdu_readbuf	 prbuf;
	struct pduq		 pdu_w;
	struct taskq		 tasks;
	struct session		*session;
	u_int32_t		 expstatsn;
	int			 state;
	int			 fd;
	u_int16_t		 cid;	/* connection id */
};

struct task {
	TAILQ_ENTRY(task)	 entry;
	struct pduq		 sendq;
	struct pduq		 recvq;
	void			*callarg;
	void	(*callback)(struct connection *, void *, struct pdu *);
	void	(*failback)(void *);
	u_int32_t		 cmdseqnum;
	u_int32_t		 itt;
	u_int8_t		 pending;
};

struct kvp {
	char	*key;
	char	*value;
	long	 flags;
};
#define KVP_KEY_ALLOCED		0x01
#define KVP_VALUE_ALLOCED	0x02

struct vscsi_stats {
	u_int64_t	bytes_rd;
	u_int64_t	bytes_wr;
	u_int64_t	cnt_read;
	u_int64_t	cnt_write;
	u_int64_t	cnt_i2t;
	u_int64_t	cnt_i2t_dir[3];
	u_int64_t	cnt_t2i;
	u_int64_t	cnt_t2i_status[3];
	u_int32_t	cnt_probe;
	u_int32_t	cnt_detach;
};

extern const struct session_params iscsi_sess_defaults;
extern const struct connection_params iscsi_conn_defaults;
extern struct session_params initiator_sess_defaults;
extern struct connection_params initiator_conn_defaults;

void	iscsid_ctrl_dispatch(void *, struct pdu *);
void	iscsi_merge_sess_params(struct session_params *,
	    struct session_params *, struct session_params *);
void	iscsi_merge_conn_params(struct connection_params *,
	    struct connection_params *, struct connection_params *);

void	initiator_init(void);
void	initiator_cleanup(void);
void	initiator_set_config(struct initiator_config *);
struct initiator_config *initiator_get_config(void);
void	initiator_shutdown(void);
int	initiator_isdown(void);
struct session	*initiator_new_session(u_int8_t);
struct session	*initiator_find_session(char *);
struct session	*initiator_t2s(u_int);
struct session_head	*initiator_get_sessions(void);
void	initiator_login(struct connection *);
void	initiator_discovery(struct session *);
void	initiator_logout(struct session *, struct connection *, u_int8_t);
void	initiator_nop_in_imm(struct connection *, struct pdu *);
char	*default_initiator_name(void);

int	control_init(char *);
void	control_cleanup(char *);
void	control_event_init(void);
void	control_queue(void *, struct pdu *);
int	control_compose(void *, u_int16_t, void *, size_t);
int	control_build(void *, u_int16_t, int, struct ctrldata *);

void	session_cleanup(struct session *);
int	session_shutdown(struct session *);
void	session_config(struct session *, struct session_config *);
void	session_task_issue(struct session *, struct task *);
void	session_logout_issue(struct session *, struct task *);
void	session_schedule(struct session *);
void	session_fsm(struct sessev *, enum s_event, unsigned int);
void	session_fsm_callback(int, short, void *);

void	conn_new(struct session *, struct connection_config *);
void	conn_free(struct connection *);
int	conn_task_ready(struct connection *);
void	conn_task_issue(struct connection *, struct task *);
void	conn_task_schedule(struct connection *);
void	conn_task_cleanup(struct connection *, struct task *);
int	conn_parse_kvp(struct connection *, struct kvp *);
int	kvp_set_from_mine(struct kvp *, const char *, struct connection *);
void	conn_pdu_write(struct connection *, struct pdu *);
void	conn_fail(struct connection *);
void	conn_fsm(struct connection *, enum c_event);

void	*pdu_gethdr(struct pdu *);
int	text_to_pdu(struct kvp *, struct pdu *);
struct kvp *pdu_to_text(char *, size_t);
u_int64_t	text_to_num(const char *, u_int64_t, u_int64_t, const char **);
int		text_to_bool(const char *, const char **);
int		text_to_digest(const char *, const char **);
void	pdu_free_queue(struct pduq *);
ssize_t	pdu_read(struct connection *);
ssize_t	pdu_write(struct connection *);
int	pdu_pending(struct connection *);
void	pdu_parse(struct connection *);
int	pdu_readbuf_set(struct pdu_readbuf *, size_t);
void	pdu_readbuf_free(struct pdu_readbuf *);

struct pdu *pdu_new(void);
void	*pdu_alloc(size_t);
void	*pdu_dup(void *, size_t);
int	pdu_addbuf(struct pdu *, void *, size_t, unsigned int);
void	*pdu_getbuf(struct pdu *, size_t *, unsigned int);
void	pdu_free(struct pdu *);
int	socket_setblockmode(int, int);
const char *log_sockaddr(void *);
void	kvp_free(struct kvp *);

void	task_init(struct task *, struct session *, int, void *,
	    void (*)(struct connection *, void *, struct pdu *),
	    void (*)(void *));
void	taskq_cleanup(struct taskq *);
void	task_pdu_add(struct task *, struct pdu *);
void	task_pdu_cb(struct connection *, struct pdu *);

void	vscsi_open(char *);
void	vscsi_dispatch(int, short, void *);
void	vscsi_data(unsigned long, int, void *, size_t);
void	vscsi_status(int, int, void *, size_t);
void	vscsi_event(unsigned long, u_int, u_int);
struct vscsi_stats *vscsi_stats(void);

/* Session polling */
void    poll_session(struct session_poll *, struct session *);
void    poll_finalize(struct session_poll *);

/* logmsg.c */
void	log_hexdump(void *, size_t);
void	log_pdu(struct pdu *, int);
