/* vdev.h: SUN4V virtual device interfaces and defines.
 *
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#ifndef _SPARC64_VDEV_H
#define _SPARC64_VDEV_H

#include <linux/types.h>

extern u32 sun4v_vdev_devhandle;
extern int sun4v_vdev_root;

extern unsigned int sun4v_vdev_device_interrupt(unsigned int);

#endif /* !(_SPARC64_VDEV_H) */
