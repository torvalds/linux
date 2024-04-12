/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Andes Technology Corporation
 */
#ifndef __ANDES_IRQ_H
#define __ANDES_IRQ_H

/* Andes PMU irq number */
#define ANDES_RV_IRQ_PMOVI		18
#define ANDES_RV_IRQ_LAST		ANDES_RV_IRQ_PMOVI
#define ANDES_SLI_CAUSE_BASE		256

/* Andes PMU related registers */
#define ANDES_CSR_SLIE			0x9c4
#define ANDES_CSR_SLIP			0x9c5
#define ANDES_CSR_SCOUNTEROF		0x9d4

#endif /* __ANDES_IRQ_H */
