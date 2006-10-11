/*
 * IBM PowerPC eBus Infrastructure Support.
 *
 * Copyright (c) 2005 IBM Corporation
 *  Heiko J Schick <schickhj@de.ibm.com>
 *    
 * All rights reserved.
 *
 * This source code is distributed under a dual license of GPL v2.0 and OpenIB 
 * BSD. 
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * Redistributions of source code must retain the above copyright notice, this 
 * list of conditions and the following disclaimer. 
 *
 * Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials
 * provided with the distribution. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_EBUS_H
#define _ASM_EBUS_H
#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <asm/of_device.h>

extern struct dma_mapping_ops ibmebus_dma_ops;
extern struct bus_type ibmebus_bus_type;

struct ibmebus_dev {	
	const char *name;
	struct of_device ofdev;
};

struct ibmebus_driver {	
	char *name;
	struct of_device_id *id_table;
	int (*probe) (struct ibmebus_dev *dev, const struct of_device_id *id);
	int (*remove) (struct ibmebus_dev *dev);
	struct device_driver driver;
};

int ibmebus_register_driver(struct ibmebus_driver *drv);
void ibmebus_unregister_driver(struct ibmebus_driver *drv);

int ibmebus_request_irq(struct ibmebus_dev *dev,
			u32 ist, 
			irq_handler_t handler,
			unsigned long irq_flags, const char * devname,
			void *dev_id);
void ibmebus_free_irq(struct ibmebus_dev *dev, u32 ist, void *dev_id);

static inline struct ibmebus_driver *to_ibmebus_driver(struct device_driver *drv)
{
	return container_of(drv, struct ibmebus_driver, driver);
}

static inline struct ibmebus_dev *to_ibmebus_dev(struct device *dev)
{
	return container_of(dev, struct ibmebus_dev, ofdev.dev);
}


#endif /* __KERNEL__ */
#endif /* _ASM_IBMEBUS_H */
