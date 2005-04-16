/* IPv6 macros for the nternal logging interface. */
#ifndef __IP6_LOGGING_H
#define __IP6_LOGGING_H

#ifdef __KERNEL__
#include <linux/socket.h>
#include <linux/netfilter_logging.h>

#define nf_log_ip6_packet(pskb,hooknum,in,out,fmt,args...) \
	nf_log_packet(AF_INET6,pskb,hooknum,in,out,fmt,##args)

#define nf_log_ip6(pfh,len,fmt,args...) \
	nf_log(AF_INET6,pfh,len,fmt,##args)

#define nf_ip6_log_register(logging) nf_log_register(AF_INET6,logging)
#define nf_ip6_log_unregister(logging) nf_log_unregister(AF_INET6,logging)
	
#endif /*__KERNEL__*/

#endif /*__IP6_LOGGING_H*/
