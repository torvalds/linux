#ifndef _NET_GARP_H
#define _NET_GARP_H

#include <net/stp.h>

#define GARP_PROTOCOL_ID	0x1
#define GARP_END_MARK		0x0

struct garp_pdu_hdr {
	__be16	protocol;
};

struct garp_msg_hdr {
	u8	attrtype;
};

enum garp_attr_event {
	GARP_LEAVE_ALL,
	GARP_JOIN_EMPTY,
	GARP_JOIN_IN,
	GARP_LEAVE_EMPTY,
	GARP_LEAVE_IN,
	GARP_EMPTY,
};

struct garp_attr_hdr {
	u8	len;
	u8	event;
	u8	data[];
};

struct garp_skb_cb {
	u8	cur_type;
};

static inline struct garp_skb_cb *garp_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct garp_skb_cb) >
		     FIELD_SIZEOF(struct sk_buff, cb));
	return (struct garp_skb_cb *)skb->cb;
}

enum garp_applicant_state {
	GARP_APPLICANT_INVALID,
	GARP_APPLICANT_VA,
	GARP_APPLICANT_AA,
	GARP_APPLICANT_QA,
	GARP_APPLICANT_LA,
	GARP_APPLICANT_VP,
	GARP_APPLICANT_AP,
	GARP_APPLICANT_QP,
	GARP_APPLICANT_VO,
	GARP_APPLICANT_AO,
	GARP_APPLICANT_QO,
	__GARP_APPLICANT_MAX
};
#define GARP_APPLICANT_MAX	(__GARP_APPLICANT_MAX - 1)

enum garp_event {
	GARP_EVENT_REQ_JOIN,
	GARP_EVENT_REQ_LEAVE,
	GARP_EVENT_R_JOIN_IN,
	GARP_EVENT_R_JOIN_EMPTY,
	GARP_EVENT_R_EMPTY,
	GARP_EVENT_R_LEAVE_IN,
	GARP_EVENT_R_LEAVE_EMPTY,
	GARP_EVENT_TRANSMIT_PDU,
	__GARP_EVENT_MAX
};
#define GARP_EVENT_MAX		(__GARP_EVENT_MAX - 1)

enum garp_action {
	GARP_ACTION_NONE,
	GARP_ACTION_S_JOIN_IN,
	GARP_ACTION_S_LEAVE_EMPTY,
};

struct garp_attr {
	struct rb_node			node;
	enum garp_applicant_state	state;
	u8				type;
	u8				dlen;
	unsigned char			data[];
};

enum garp_applications {
	GARP_APPLICATION_GVRP,
	__GARP_APPLICATION_MAX
};
#define GARP_APPLICATION_MAX	(__GARP_APPLICATION_MAX - 1)

struct garp_application {
	enum garp_applications	type;
	unsigned int		maxattr;
	struct stp_proto	proto;
};

struct garp_applicant {
	struct garp_application	*app;
	struct net_device	*dev;
	struct timer_list	join_timer;

	spinlock_t		lock;
	struct sk_buff_head	queue;
	struct sk_buff		*pdu;
	struct rb_root		gid;
	struct rcu_head		rcu;
};

struct garp_port {
	struct garp_applicant __rcu	*applicants[GARP_APPLICATION_MAX + 1];
	struct rcu_head			rcu;
};

int garp_register_application(struct garp_application *app);
void garp_unregister_application(struct garp_application *app);

int garp_init_applicant(struct net_device *dev, struct garp_application *app);
void garp_uninit_applicant(struct net_device *dev,
			   struct garp_application *app);

int garp_request_join(const struct net_device *dev,
		      const struct garp_application *app, const void *data,
		      u8 len, u8 type);
void garp_request_leave(const struct net_device *dev,
			const struct garp_application *app,
			const void *data, u8 len, u8 type);

#endif /* _NET_GARP_H */
