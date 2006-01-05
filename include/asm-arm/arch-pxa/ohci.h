#ifndef ASMARM_ARCH_OHCI_H
#define ASMARM_ARCH_OHCI_H

struct device;

struct pxaohci_platform_data {
	int (*init)(struct device *);
	void (*exit)(struct device *);

	int port_mode;
#define PMM_NPS_MODE           1
#define PMM_GLOBAL_MODE        2
#define PMM_PERPORT_MODE       3
};

extern void pxa_set_ohci_info(struct pxaohci_platform_data *info);

#endif
