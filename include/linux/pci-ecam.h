/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2016 Broadcom
 */
#ifndef DRIVERS_PCI_ECAM_H
#define DRIVERS_PCI_ECAM_H

#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

/*
 * struct to hold pci ops and bus shift of the config window
 * for a PCI controller.
 */
struct pci_config_window;
struct pci_ecam_ops {
	unsigned int			bus_shift;
	struct pci_ops			pci_ops;
	int				(*init)(struct pci_config_window *);
};

/*
 * struct to hold the mappings of a config space window. This
 * is expected to be used as sysdata for PCI controllers that
 * use ECAM.
 */
struct pci_config_window {
	struct resource			res;
	struct resource			busr;
	void				*priv;
	const struct pci_ecam_ops	*ops;
	union {
		void __iomem		*win;	/* 64-bit single mapping */
		void __iomem		**winp; /* 32-bit per-bus mapping */
	};
	struct device			*parent;/* ECAM res was from this dev */
};

/* create and free pci_config_window */
struct pci_config_window *pci_ecam_create(struct device *dev,
		struct resource *cfgres, struct resource *busr,
		const struct pci_ecam_ops *ops);
void pci_ecam_free(struct pci_config_window *cfg);

/* map_bus when ->sysdata is an instance of pci_config_window */
void __iomem *pci_ecam_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where);
/* default ECAM ops */
extern const struct pci_ecam_ops pci_generic_ecam_ops;

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)
extern const struct pci_ecam_ops pci_32b_ops;	/* 32-bit accesses only */
extern const struct pci_ecam_ops pci_32b_read_ops; /* 32-bit read only */
extern const struct pci_ecam_ops hisi_pcie_ops;	/* HiSilicon */
extern const struct pci_ecam_ops rk_pcie_ecam_ops;	/* Rockchip */
extern const struct pci_ecam_ops thunder_pem_ecam_ops; /* Cavium ThunderX 1.x & 2.x */
extern const struct pci_ecam_ops pci_thunder_ecam_ops; /* Cavium ThunderX 1.x */
extern const struct pci_ecam_ops xgene_v1_pcie_ecam_ops; /* APM X-Gene PCIe v1 */
extern const struct pci_ecam_ops xgene_v2_pcie_ecam_ops; /* APM X-Gene PCIe v2.x */
extern const struct pci_ecam_ops al_pcie_ops;	/* Amazon Annapurna Labs PCIe */
#endif

#if IS_ENABLED(CONFIG_PCI_HOST_COMMON)
/* for DT-based PCI controllers that support ECAM */
int pci_host_common_probe(struct platform_device *pdev);
int pci_host_common_remove(struct platform_device *pdev);
#endif
#endif
