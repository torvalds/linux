#ifndef _LINUX_IF_MACVTAP_H_
#define _LINUX_IF_MACVTAP_H_

rx_handler_result_t macvtap_handle_frame(struct sk_buff **pskb);
void macvtap_del_queues(struct net_device *dev);
int macvtap_get_minor(struct macvlan_dev *vlan);
void macvtap_free_minor(struct macvlan_dev *vlan);
int macvtap_queue_resize(struct macvlan_dev *vlan);

#endif /*_LINUX_IF_MACVTAP_H_*/
