/*
 * Arch specific extensions to struct device
 *
 * This file is released under the GPLv2
 */
#ifndef _ASM_SPARC_DEVICE_H
#define _ASM_SPARC_DEVICE_H

struct device_node;
struct of_device;

struct dev_archdata {
	struct device_node	*prom_node;
	struct of_device	*op;
};

#endif /* _ASM_SPARC_DEVICE_H */


