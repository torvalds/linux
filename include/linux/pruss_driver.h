/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PRU-ICSS sub-system specific definitions
 *
 * Copyright (C) 2014-2020 Texas Instruments Incorporated - http://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 */

#ifndef _PRUSS_DRIVER_H_
#define _PRUSS_DRIVER_H_

#include <linux/mutex.h>
#include <linux/remoteproc/pruss.h>
#include <linux/types.h>
#include <linux/err.h>

/*
 * enum pruss_gp_mux_sel - PRUSS GPI/O Mux modes for the
 * PRUSS_GPCFG0/1 registers
 *
 * NOTE: The below defines are the most common values, but there
 * are some exceptions like on 66AK2G, where the RESERVED and MII2
 * values are interchanged. Also, this bit-field does not exist on
 * AM335x SoCs
 */
enum pruss_gp_mux_sel {
	PRUSS_GP_MUX_SEL_GP,
	PRUSS_GP_MUX_SEL_ENDAT,
	PRUSS_GP_MUX_SEL_RESERVED,
	PRUSS_GP_MUX_SEL_SD,
	PRUSS_GP_MUX_SEL_MII2,
	PRUSS_GP_MUX_SEL_MAX,
};

/*
 * enum pruss_gpi_mode - PRUSS GPI configuration modes, used
 *			 to program the PRUSS_GPCFG0/1 registers
 */
enum pruss_gpi_mode {
	PRUSS_GPI_MODE_DIRECT,
	PRUSS_GPI_MODE_PARALLEL,
	PRUSS_GPI_MODE_28BIT_SHIFT,
	PRUSS_GPI_MODE_MII,
	PRUSS_GPI_MODE_MAX,
};

/**
 * enum pru_type - PRU core type identifier
 *
 * @PRU_TYPE_PRU: Programmable Real-time Unit
 * @PRU_TYPE_RTU: Auxiliary Programmable Real-Time Unit
 * @PRU_TYPE_TX_PRU: Transmit Programmable Real-Time Unit
 * @PRU_TYPE_MAX: just keep this one at the end
 */
enum pru_type {
	PRU_TYPE_PRU,
	PRU_TYPE_RTU,
	PRU_TYPE_TX_PRU,
	PRU_TYPE_MAX,
};

/*
 * enum pruss_mem - PRUSS memory range identifiers
 */
enum pruss_mem {
	PRUSS_MEM_DRAM0 = 0,
	PRUSS_MEM_DRAM1,
	PRUSS_MEM_SHRD_RAM2,
	PRUSS_MEM_MAX,
};

/**
 * struct pruss_mem_region - PRUSS memory region structure
 * @va: kernel virtual address of the PRUSS memory region
 * @pa: physical (bus) address of the PRUSS memory region
 * @size: size of the PRUSS memory region
 */
struct pruss_mem_region {
	void __iomem *va;
	phys_addr_t pa;
	size_t size;
};

/**
 * struct pruss - PRUSS parent structure
 * @dev: pruss device pointer
 * @cfg_base: base iomap for CFG region
 * @cfg_regmap: regmap for config region
 * @mem_regions: data for each of the PRUSS memory regions
 * @mem_in_use: to indicate if memory resource is in use
 * @lock: mutex to serialize access to resources
 * @core_clk_mux: clk handle for PRUSS CORE_CLK_MUX
 * @iep_clk_mux: clk handle for PRUSS IEP_CLK_MUX
 */
struct pruss {
	struct device *dev;
	void __iomem *cfg_base;
	struct regmap *cfg_regmap;
	struct pruss_mem_region mem_regions[PRUSS_MEM_MAX];
	struct pruss_mem_region *mem_in_use[PRUSS_MEM_MAX];
	struct mutex lock; /* PRU resource lock */
	struct clk *core_clk_mux;
	struct clk *iep_clk_mux;
};

#if IS_ENABLED(CONFIG_TI_PRUSS)

struct pruss *pruss_get(struct rproc *rproc);
void pruss_put(struct pruss *pruss);
int pruss_request_mem_region(struct pruss *pruss, enum pruss_mem mem_id,
			     struct pruss_mem_region *region);
int pruss_release_mem_region(struct pruss *pruss,
			     struct pruss_mem_region *region);
int pruss_cfg_get_gpmux(struct pruss *pruss, enum pruss_pru_id pru_id, u8 *mux);
int pruss_cfg_set_gpmux(struct pruss *pruss, enum pruss_pru_id pru_id, u8 mux);
int pruss_cfg_gpimode(struct pruss *pruss, enum pruss_pru_id pru_id,
		      enum pruss_gpi_mode mode);
int pruss_cfg_miirt_enable(struct pruss *pruss, bool enable);
int pruss_cfg_xfr_enable(struct pruss *pruss, enum pru_type pru_type,
			 bool enable);

#else

static inline struct pruss *pruss_get(struct rproc *rproc)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void pruss_put(struct pruss *pruss) { }

static inline int pruss_request_mem_region(struct pruss *pruss,
					   enum pruss_mem mem_id,
					   struct pruss_mem_region *region)
{
	return -EOPNOTSUPP;
}

static inline int pruss_release_mem_region(struct pruss *pruss,
					   struct pruss_mem_region *region)
{
	return -EOPNOTSUPP;
}

static inline int pruss_cfg_get_gpmux(struct pruss *pruss,
				      enum pruss_pru_id pru_id, u8 *mux)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int pruss_cfg_set_gpmux(struct pruss *pruss,
				      enum pruss_pru_id pru_id, u8 mux)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int pruss_cfg_gpimode(struct pruss *pruss,
				    enum pruss_pru_id pru_id,
				    enum pruss_gpi_mode mode)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int pruss_cfg_miirt_enable(struct pruss *pruss, bool enable)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int pruss_cfg_xfr_enable(struct pruss *pruss,
				       enum pru_type pru_type,
				       bool enable);
{
	return ERR_PTR(-EOPNOTSUPP);
}

#endif /* CONFIG_TI_PRUSS */

#endif	/* _PRUSS_DRIVER_H_ */
