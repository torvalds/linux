#ifndef __NET_WEXT_H
#define __NET_WEXT_H

#include <net/iw_handler.h>

struct net;

#ifdef CONFIG_WEXT_CORE
int wext_handle_ioctl(struct net *net, struct iwreq *iwr, unsigned int cmd,
		      void __user *arg);
int compat_wext_handle_ioctl(struct net *net, unsigned int cmd,
			     unsigned long arg);

struct iw_statistics *get_wireless_stats(struct net_device *dev);
int call_commit_handler(struct net_device *dev);
#else
static inline int wext_handle_ioctl(struct net *net, struct iwreq *iwr, unsigned int cmd,
				    void __user *arg)
{
	return -EINVAL;
}
static inline int compat_wext_handle_ioctl(struct net *net, unsigned int cmd,
					   unsigned long arg)
{
	return -EINVAL;
}
#endif

#ifdef CONFIG_WEXT_PROC
int wext_proc_init(struct net *net);
void wext_proc_exit(struct net *net);
#else
static inline int wext_proc_init(struct net *net)
{
	return 0;
}
static inline void wext_proc_exit(struct net *net)
{
	return;
}
#endif

#ifdef CONFIG_WEXT_PRIV
int ioctl_private_call(struct net_device *dev, struct iwreq *iwr,
		       unsigned int cmd, struct iw_request_info *info,
		       iw_handler handler);
int compat_private_call(struct net_device *dev, struct iwreq *iwr,
			unsigned int cmd, struct iw_request_info *info,
			iw_handler handler);
int iw_handler_get_private(struct net_device *		dev,
			   struct iw_request_info *	info,
			   union iwreq_data *		wrqu,
			   char *			extra);
#else
#define ioctl_private_call NULL
#define compat_private_call NULL
#endif


#endif /* __NET_WEXT_H */
