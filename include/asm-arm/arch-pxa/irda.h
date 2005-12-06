#ifndef ASMARM_ARCH_IRDA_H
#define ASMARM_ARCH_IRDA_H

/* board specific transceiver capabilities */

#define IR_OFF		1
#define IR_SIRMODE	2
#define IR_FIRMODE	4

struct pxaficp_platform_data {
	int transceiver_cap;
	void (*transceiver_mode)(struct device *dev, int mode);
};

extern void pxa_set_ficp_info(struct pxaficp_platform_data *info);

#endif
