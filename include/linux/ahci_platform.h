/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AHCI SATA platform driver
 *
 * Copyright 2004-2005  Red Hat, Inc.
 *   Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2010  MontaVista Software, LLC.
 *   Anton Vorontsov <avorontsov@ru.mvista.com>
 */

#ifndef _AHCI_PLATFORM_H
#define _AHCI_PLATFORM_H

#include <linux/compiler.h>

struct clk;
struct device;
struct ata_port_info;
struct ahci_host_priv;
struct platform_device;
struct scsi_host_template;

int ahci_platform_enable_phys(struct ahci_host_priv *hpriv);
void ahci_platform_disable_phys(struct ahci_host_priv *hpriv);
struct clk *ahci_platform_find_clk(struct ahci_host_priv *hpriv,
				   const char *con_id);
int ahci_platform_enable_clks(struct ahci_host_priv *hpriv);
void ahci_platform_disable_clks(struct ahci_host_priv *hpriv);
int ahci_platform_deassert_rsts(struct ahci_host_priv *hpriv);
int ahci_platform_assert_rsts(struct ahci_host_priv *hpriv);
int ahci_platform_enable_regulators(struct ahci_host_priv *hpriv);
void ahci_platform_disable_regulators(struct ahci_host_priv *hpriv);
int ahci_platform_enable_resources(struct ahci_host_priv *hpriv);
void ahci_platform_disable_resources(struct ahci_host_priv *hpriv);
struct ahci_host_priv *ahci_platform_get_resources(
	struct platform_device *pdev, unsigned int flags);
int ahci_platform_init_host(struct platform_device *pdev,
			    struct ahci_host_priv *hpriv,
			    const struct ata_port_info *pi_template,
			    struct scsi_host_template *sht);

void ahci_platform_shutdown(struct platform_device *pdev);

int ahci_platform_suspend_host(struct device *dev);
int ahci_platform_resume_host(struct device *dev);
int ahci_platform_suspend(struct device *dev);
int ahci_platform_resume(struct device *dev);

#define AHCI_PLATFORM_GET_RESETS	BIT(0)
#define AHCI_PLATFORM_RST_TRIGGER	BIT(1)

#endif /* _AHCI_PLATFORM_H */
