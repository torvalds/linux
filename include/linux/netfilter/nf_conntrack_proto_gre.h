#ifndef _CONNTRACK_PROTO_GRE_H
#define _CONNTRACK_PROTO_GRE_H
#include <asm/byteorder.h>
#include <net/gre.h>
#include <net/pptp.h>

struct nf_ct_gre {
	unsigned int stream_timeout;
	unsigned int timeout;
};

#ifdef __KERNEL__
#include <net/netfilter/nf_conntrack_tuple.h>

struct nf_conn;

/* structure for original <-> reply keymap */
struct nf_ct_gre_keymap {
	struct list_head list;
	struct nf_conntrack_tuple tuple;
};

/* add new tuple->key_reply pair to keymap */
int nf_ct_gre_keymap_add(struct nf_conn *ct, enum ip_conntrack_dir dir,
			 struct nf_conntrack_tuple *t);

/* delete keymap entries */
void nf_ct_gre_keymap_destroy(struct nf_conn *ct);

void nf_nat_need_gre(void);

#endif /* __KERNEL__ */
#endif /* _CONNTRACK_PROTO_GRE_H */
