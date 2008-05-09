#ifndef _LINUX_IF_MACVLAN_H
#define _LINUX_IF_MACVLAN_H

extern struct sk_buff *(*macvlan_handle_frame_hook)(struct sk_buff *);

#endif /* _LINUX_IF_MACVLAN_H */
