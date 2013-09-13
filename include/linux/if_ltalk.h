#ifndef __LINUX_LTALK_H
#define __LINUX_LTALK_H

#include <uapi/linux/if_ltalk.h>

extern struct net_device *alloc_ltalkdev(int sizeof_priv);
#endif
