#ifndef _NF_CONNTRACK_COUNT_H
#define _NF_CONNTRACK_COUNT_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_zones.h>

struct nf_conncount_data;

struct nf_conncount_list {
	spinlock_t list_lock;
	struct list_head head;	/* connections with the same filtering key */
	unsigned int count;	/* length of list */
};

struct nf_conncount_data *nf_conncount_init(struct net *net, unsigned int family,
					    unsigned int keylen);
void nf_conncount_destroy(struct net *net, unsigned int family,
			  struct nf_conncount_data *data);

unsigned int nf_conncount_count(struct net *net,
				struct nf_conncount_data *data,
				const u32 *key,
				const struct nf_conntrack_tuple *tuple,
				const struct nf_conntrack_zone *zone);

int nf_conncount_add(struct net *net, struct nf_conncount_list *list,
		     const struct nf_conntrack_tuple *tuple,
		     const struct nf_conntrack_zone *zone);

void nf_conncount_list_init(struct nf_conncount_list *list);

bool nf_conncount_gc_list(struct net *net,
			  struct nf_conncount_list *list);

void nf_conncount_cache_free(struct nf_conncount_list *list);

#endif
