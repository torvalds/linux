#ifndef _NET_MRP_H
#define _NET_MRP_H

#define MRP_END_MARK		0x0

struct mrp_pdu_hdr {
	u8	version;
};

struct mrp_msg_hdr {
	u8	attrtype;
	u8	attrlen;
};

struct mrp_vecattr_hdr {
	__be16	lenflags;
	unsigned char	firstattrvalue[];
#define MRP_VECATTR_HDR_LEN_MASK cpu_to_be16(0x1FFF)
#define MRP_VECATTR_HDR_FLAG_LA cpu_to_be16(0x2000)
};

enum mrp_vecattr_event {
	MRP_VECATTR_EVENT_NEW,
	MRP_VECATTR_EVENT_JOIN_IN,
	MRP_VECATTR_EVENT_IN,
	MRP_VECATTR_EVENT_JOIN_MT,
	MRP_VECATTR_EVENT_MT,
	MRP_VECATTR_EVENT_LV,
	__MRP_VECATTR_EVENT_MAX
};

struct mrp_skb_cb {
	struct mrp_msg_hdr	*mh;
	struct mrp_vecattr_hdr	*vah;
	unsigned char		attrvalue[];
};

static inline struct mrp_skb_cb *mrp_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct mrp_skb_cb) >
		     FIELD_SIZEOF(struct sk_buff, cb));
	return (struct mrp_skb_cb *)skb->cb;
}

enum mrp_applicant_state {
	MRP_APPLICANT_INVALID,
	MRP_APPLICANT_VO,
	MRP_APPLICANT_VP,
	MRP_APPLICANT_VN,
	MRP_APPLICANT_AN,
	MRP_APPLICANT_AA,
	MRP_APPLICANT_QA,
	MRP_APPLICANT_LA,
	MRP_APPLICANT_AO,
	MRP_APPLICANT_QO,
	MRP_APPLICANT_AP,
	MRP_APPLICANT_QP,
	__MRP_APPLICANT_MAX
};
#define MRP_APPLICANT_MAX	(__MRP_APPLICANT_MAX - 1)

enum mrp_event {
	MRP_EVENT_NEW,
	MRP_EVENT_JOIN,
	MRP_EVENT_LV,
	MRP_EVENT_TX,
	MRP_EVENT_R_NEW,
	MRP_EVENT_R_JOIN_IN,
	MRP_EVENT_R_IN,
	MRP_EVENT_R_JOIN_MT,
	MRP_EVENT_R_MT,
	MRP_EVENT_R_LV,
	MRP_EVENT_R_LA,
	MRP_EVENT_REDECLARE,
	MRP_EVENT_PERIODIC,
	__MRP_EVENT_MAX
};
#define MRP_EVENT_MAX		(__MRP_EVENT_MAX - 1)

enum mrp_tx_action {
	MRP_TX_ACTION_NONE,
	MRP_TX_ACTION_S_NEW,
	MRP_TX_ACTION_S_JOIN_IN,
	MRP_TX_ACTION_S_JOIN_IN_OPTIONAL,
	MRP_TX_ACTION_S_IN_OPTIONAL,
	MRP_TX_ACTION_S_LV,
};

struct mrp_attr {
	struct rb_node			node;
	enum mrp_applicant_state	state;
	u8				type;
	u8				len;
	unsigned char			value[];
};

enum mrp_applications {
	MRP_APPLICATION_MVRP,
	__MRP_APPLICATION_MAX
};
#define MRP_APPLICATION_MAX	(__MRP_APPLICATION_MAX - 1)

struct mrp_application {
	enum mrp_applications	type;
	unsigned int		maxattr;
	struct packet_type	pkttype;
	unsigned char		group_address[ETH_ALEN];
	u8			version;
};

struct mrp_applicant {
	struct mrp_application	*app;
	struct net_device	*dev;
	struct timer_list	join_timer;

	spinlock_t		lock;
	struct sk_buff_head	queue;
	struct sk_buff		*pdu;
	struct rb_root		mad;
	struct rcu_head		rcu;
};

struct mrp_port {
	struct mrp_applicant __rcu	*applicants[MRP_APPLICATION_MAX + 1];
	struct rcu_head			rcu;
};

extern int	mrp_register_application(struct mrp_application *app);
extern void	mrp_unregister_application(struct mrp_application *app);

extern int	mrp_init_applicant(struct net_device *dev,
				    struct mrp_application *app);
extern void	mrp_uninit_applicant(struct net_device *dev,
				      struct mrp_application *app);

extern int	mrp_request_join(const struct net_device *dev,
				  const struct mrp_application *app,
				  const void *value, u8 len, u8 type);
extern void	mrp_request_leave(const struct net_device *dev,
				   const struct mrp_application *app,
				   const void *value, u8 len, u8 type);

#endif /* _NET_MRP_H */
