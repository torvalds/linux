/* vdev.h: SUN4V virtual device interfaces and defines.
 *
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#ifndef _SPARC64_VDEV_H
#define _SPARC64_VDEV_H

#include <linux/types.h>
#include <asm/oplib.h>

extern u32 sun4v_vdev_devhandle;
extern int sun4v_vdev_root;
extern struct linux_prom_pci_intmap *sun4v_vdev_intmap;
extern int sun4v_vdev_num_intmap;
extern struct linux_prom_pci_intmap sun4v_vdev_intmask;

#endif /* !(_SPARC64_VDEV_H) */
