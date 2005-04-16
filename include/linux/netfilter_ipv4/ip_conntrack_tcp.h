#ifndef _IP_CONNTRACK_TCP_H
#define _IP_CONNTRACK_TCP_H
/* TCP tracking. */

enum tcp_conntrack {
	TCP_CONNTRACK_NONE,
	TCP_CONNTRACK_SYN_SENT,
	TCP_CONNTRACK_SYN_RECV,
	TCP_CONNTRACK_ESTABLISHED,
	TCP_CONNTRACK_FIN_WAIT,
	TCP_CONNTRACK_CLOSE_WAIT,
	TCP_CONNTRACK_LAST_ACK,
	TCP_CONNTRACK_TIME_WAIT,
	TCP_CONNTRACK_CLOSE,
	TCP_CONNTRACK_LISTEN,
	TCP_CONNTRACK_MAX,
	TCP_CONNTRACK_IGNORE
};

/* Window scaling is advertised by the sender */
#define IP_CT_TCP_FLAG_WINDOW_SCALE		0x01

/* SACK is permitted by the sender */
#define IP_CT_TCP_FLAG_SACK_PERM		0x02

/* This sender sent FIN first */
#define IP_CT_TCP_FLAG_CLOSE_INIT		0x03

struct ip_ct_tcp_state {
	u_int32_t	td_end;		/* max of seq + len */
	u_int32_t	td_maxend;	/* max of ack + max(win, 1) */
	u_int32_t	td_maxwin;	/* max(win) */
	u_int8_t	td_scale;	/* window scale factor */
	u_int8_t	loose;		/* used when connection picked up from the middle */
	u_int8_t	flags;		/* per direction options */
};

struct ip_ct_tcp
{
	struct ip_ct_tcp_state seen[2];	/* connection parameters per direction */
	u_int8_t	state;		/* state of the connection (enum tcp_conntrack) */
	/* For detecting stale connections */
	u_int8_t	last_dir;	/* Direction of the last packet (enum ip_conntrack_dir) */
	u_int8_t	retrans;	/* Number of retransmitted packets */
	u_int8_t	last_index;	/* Index of the last packet */
	u_int32_t	last_seq;	/* Last sequence number seen in dir */
	u_int32_t	last_ack;	/* Last sequence number seen in opposite dir */
	u_int32_t	last_end;	/* Last seq + len */
};

#endif /* _IP_CONNTRACK_TCP_H */
