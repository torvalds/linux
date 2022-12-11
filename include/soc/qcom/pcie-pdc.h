/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SOC_QCOM_PCIE_PDC_H__
#define __SOC_QCOM_PCIE_PDC_H__

#if IS_ENABLED(CONFIG_QCOM_PCIE_PDC)

int pcie_pdc_cfg_irq(u32 gpio, unsigned int type, bool enable);

#else

static inline int pcie_pdc_cfg_irq(u32 gpio, unsigned int type, bool enable)
{ return -ENODEV; }

#endif /* CONFIG_QCOM_PCIE_PDC */

#endif /* __SOC_QCOM_PCIE_PDC_H__ */
