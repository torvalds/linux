#ifndef ASMARM_ARCH_MMC_H
#define ASMARM_ARCH_MMC_H

#include <linux/mmc/protocol.h>
#include <linux/interrupt.h>

struct device;
struct mmc_host;

struct pxamci_platform_data {
	unsigned int ocr_mask;			/* available voltages */
	int (*init)(struct device *, irqreturn_t (*)(int, void *, struct pt_regs *), void *);
	void (*setpower)(struct device *, unsigned int);
	void (*exit)(struct device *, void *);
};

extern void pxa_set_mci_info(struct pxamci_platform_data *info);

#endif
