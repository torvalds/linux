/* IPv4 macros for the internal logging interface. */
#ifndef __IP_LOGGING_H
#define __IP_LOGGING_H

#ifdef __KERNEL__
#include <linux/socket.h>
#include <linux/netfilter_logging.h>

#define nf_log_ip_packet(pskb,hooknum,in,out,fmt,args...) \
	nf_log_packet(AF_INET,pskb,hooknum,in,out,fmt,##args)

#define nf_log_ip(pfh,len,fmt,args...) \
	nf_log(AF_INET,pfh,len,fmt,##args)

#define nf_ip_log_register(logging) nf_log_register(AF_INET,logging)
#define nf_ip_log_unregister(logging) nf_log_unregister(AF_INET,logging)
	
#endif /*__KERNEL__*/

#endif /*__IP_LOGGING_H*/
