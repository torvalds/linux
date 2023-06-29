/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
#ifndef _DT_BINDINGS_SOC_ROCKCHIP_AMP_H
#define _DT_BINDINGS_SOC_ROCKCHIP_AMP_H

#define CPU_GET_AFFINITY(cpu, cluster) ((cpu) << 0 | ((cluster) << 8))
#define GIC_AMP_IRQ_CFG_ROUTE(_irq, _prio, _aff) (_irq) (_prio) (_aff)
#endif
