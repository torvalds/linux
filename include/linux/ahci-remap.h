#ifndef _LINUX_AHCI_REMAP_H
#define _LINUX_AHCI_REMAP_H

#include <linux/sizes.h>

#define AHCI_VSCAP		0xa4
#define AHCI_REMAP_CAP		0x800

/* device class code */
#define AHCI_REMAP_N_DCC	0x880

/* remap-device base relative to ahci-bar */
#define AHCI_REMAP_N_OFFSET	SZ_16K
#define AHCI_REMAP_N_SIZE	SZ_16K

#define AHCI_MAX_REMAP		3

static inline unsigned int ahci_remap_dcc(int i)
{
	return AHCI_REMAP_N_DCC + i * 0x80;
}

static inline unsigned int ahci_remap_base(int i)
{
	return AHCI_REMAP_N_OFFSET + i * AHCI_REMAP_N_SIZE;
}

#endif /* _LINUX_AHCI_REMAP_H */
