/* IP connection tracking helpers. */
#ifndef _IP_CONNTRACK_HELPER_H
#define _IP_CONNTRACK_HELPER_H
#include <linux/netfilter_ipv4/ip_conntrack.h>

struct module;

struct ip_conntrack_helper
{	
	struct list_head list; 		/* Internal use. */

	const char *name;		/* name of the module */
	struct module *me;		/* pointer to self */
	unsigned int max_expected;	/* Maximum number of concurrent 
					 * expected connections */
	unsigned int timeout;		/* timeout for expecteds */

	/* Mask of things we will help (compared against server response) */
	struct ip_conntrack_tuple tuple;
	struct ip_conntrack_tuple mask;
	
	/* Function to call when data passes; return verdict, or -1 to
           invalidate. */
	int (*help)(struct sk_buff **pskb,
		    struct ip_conntrack *ct,
		    enum ip_conntrack_info conntrackinfo);
};

extern int ip_conntrack_helper_register(struct ip_conntrack_helper *);
extern void ip_conntrack_helper_unregister(struct ip_conntrack_helper *);

/* Allocate space for an expectation: this is mandatory before calling 
   ip_conntrack_expect_related. */
extern struct ip_conntrack_expect *ip_conntrack_expect_alloc(void);
extern void ip_conntrack_expect_free(struct ip_conntrack_expect *exp);

/* Add an expected connection: can have more than one per connection */
extern int ip_conntrack_expect_related(struct ip_conntrack_expect *exp);
extern void ip_conntrack_unexpect_related(struct ip_conntrack_expect *exp);

#endif /*_IP_CONNTRACK_HELPER_H*/
