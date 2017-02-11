#ifndef _LINUX_IF_TAP_H_
#define _LINUX_IF_TAP_H_

#if IS_ENABLED(CONFIG_MACVTAP)
struct socket *tap_get_socket(struct file *);
#else
#include <linux/err.h>
#include <linux/errno.h>
struct file;
struct socket;
static inline struct socket *tap_get_socket(struct file *f)
{
	return ERR_PTR(-EINVAL);
}
#endif /* CONFIG_MACVTAP */

rx_handler_result_t tap_handle_frame(struct sk_buff **pskb);
void tap_del_queues(struct net_device *dev);
int tap_get_minor(struct macvlan_dev *vlan);
void tap_free_minor(struct macvlan_dev *vlan);
int tap_queue_resize(struct macvlan_dev *vlan);

#endif /*_LINUX_IF_TAP_H_*/
