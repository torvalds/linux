/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_NET_TRACKERS_H
#define __NET_NET_TRACKERS_H
#include <linux/ref_tracker.h>

#ifdef CONFIG_NET_DEV_REFCNT_TRACKER
typedef struct ref_tracker *netdevice_tracker;
#else
typedef struct {} netdevice_tracker;
#endif

#ifdef CONFIG_NET_NS_REFCNT_TRACKER
typedef struct ref_tracker *netns_tracker;
#else
typedef struct {} netns_tracker;
#endif

#endif /* __NET_NET_TRACKERS_H */
