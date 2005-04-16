/* Internal logging interface, which relies on the real 
   LOG target modules */
#ifndef __LINUX_NETFILTER_LOGGING_H
#define __LINUX_NETFILTER_LOGGING_H

#ifdef __KERNEL__
#include <asm/atomic.h>

struct nf_logging_t {
	void (*nf_log_packet)(struct sk_buff **pskb,
			      unsigned int hooknum,
			      const struct net_device *in,
			      const struct net_device *out,
			      const char *prefix);
	void (*nf_log)(char *pfh, size_t len,
		       const char *prefix);
};

extern void nf_log_register(int pf, const struct nf_logging_t *logging);
extern void nf_log_unregister(int pf, const struct nf_logging_t *logging);

extern void nf_log_packet(int pf,
			  struct sk_buff **pskb,
			  unsigned int hooknum,
			  const struct net_device *in,
			  const struct net_device *out,
			  const char *fmt, ...);
extern void nf_log(int pf,
		   char *pfh, size_t len,
		   const char *fmt, ...);
#endif /*__KERNEL__*/

#endif /*__LINUX_NETFILTER_LOGGING_H*/
