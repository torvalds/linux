#ifndef _NF_QUEUE_H
#define _NF_QUEUE_H

/* Each queued (to userspace) skbuff has one of these. */
struct nf_info {
	struct nf_hook_ops	*elem;
	int			pf;
	unsigned int		hook;
	struct net_device	*indev;
	struct net_device	*outdev;
	int			(*okfn)(struct sk_buff *);
};

#define nf_info_reroute(x) ((void *)x + sizeof(struct nf_info))

/* Packet queuing */
struct nf_queue_handler {
	int			(*outfn)(struct sk_buff *skb,
					 struct nf_info *info,
					 unsigned int queuenum);
	char			*name;
};

extern int nf_register_queue_handler(int pf,
				     const struct nf_queue_handler *qh);
extern int nf_unregister_queue_handler(int pf,
				       const struct nf_queue_handler *qh);
extern void nf_unregister_queue_handlers(const struct nf_queue_handler *qh);
extern void nf_reinject(struct sk_buff *skb, struct nf_info *info,
			unsigned int verdict);

#endif /* _NF_QUEUE_H */
