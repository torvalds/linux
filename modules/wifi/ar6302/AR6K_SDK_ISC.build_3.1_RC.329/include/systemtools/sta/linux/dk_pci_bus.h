// Copyright (c) 2010 Atheros Communications Inc.
// All rights reserved.
// $ATH_LICENSE_TARGET_C$

/* pci_bus.h - contains declarations of the pci bus functions */

#ifndef __DK_PCI_BUS_H_
#define __DK_PCI_BUS_H_

#include "dk.h"

INT32 bus_module_init
(
	VOID
);

VOID bus_module_exit
(
	VOID
);

INT32 bus_dev_init
(
     void *bus_dev
);


INT32 bus_dev_exit
(
     void  *bus_dev
);

INT32 bus_cfg_read
(
     void *bus_dev,
     INT32 offset,
     INT32 size,
     INT32 *ret_val
);

INT32 bus_cfg_write
(
    void *bus_dev,
    INT32 offset,
	INT32 size,
	INT32 ret_val
);
		
#endif //__PCI_BUS_H_
