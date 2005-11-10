/*
 * connection tracking helpers.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalize L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_helper.h
 */

#ifndef _NF_CONNTRACK_HELPER_H
#define _NF_CONNTRACK_HELPER_H
#include <net/netfilter/nf_conntrack.h>

struct module;

struct nf_conntrack_helper
{	
	struct list_head list; 		/* Internal use. */

	const char *name;		/* name of the module */
	struct module *me;		/* pointer to self */
	unsigned int max_expected;	/* Maximum number of concurrent 
					 * expected connections */
	unsigned int timeout;		/* timeout for expecteds */

	/* Mask of things we will help (compared against server response) */
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_tuple mask;
	
	/* Function to call when data passes; return verdict, or -1 to
           invalidate. */
	int (*help)(struct sk_buff **pskb,
		    unsigned int protoff,
		    struct nf_conn *ct,
		    enum ip_conntrack_info conntrackinfo);
};

extern int nf_conntrack_helper_register(struct nf_conntrack_helper *);
extern void nf_conntrack_helper_unregister(struct nf_conntrack_helper *);

/* Allocate space for an expectation: this is mandatory before calling
   nf_conntrack_expect_related.  You will have to call put afterwards. */
extern struct nf_conntrack_expect *
nf_conntrack_expect_alloc(struct nf_conn *master);
extern void nf_conntrack_expect_put(struct nf_conntrack_expect *exp);

/* Add an expected connection: can have more than one per connection */
extern int nf_conntrack_expect_related(struct nf_conntrack_expect *exp);
extern void nf_conntrack_unexpect_related(struct nf_conntrack_expect *exp);

#endif /*_NF_CONNTRACK_HELPER_H*/
