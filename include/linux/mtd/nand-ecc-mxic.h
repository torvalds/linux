/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright © 2019 Macronix
 * Author: Miquèl Raynal <miquel.raynal@bootlin.com>
 *
 * Header for the Macronix external ECC engine.
 */

#ifndef __MTD_NAND_ECC_MXIC_H__
#define __MTD_NAND_ECC_MXIC_H__

#include <linux/platform_device.h>
#include <linux/device.h>

struct mxic_ecc_engine;

#if IS_ENABLED(CONFIG_MTD_NAND_ECC_MXIC) && IS_REACHABLE(CONFIG_MTD_NAND_CORE)

struct nand_ecc_engine_ops *mxic_ecc_get_pipelined_ops(void);
struct nand_ecc_engine *mxic_ecc_get_pipelined_engine(struct platform_device *spi_pdev);
void mxic_ecc_put_pipelined_engine(struct nand_ecc_engine *eng);
int mxic_ecc_process_data_pipelined(struct nand_ecc_engine *eng,
				    unsigned int direction, dma_addr_t dirmap);

#else /* !CONFIG_MTD_NAND_ECC_MXIC */

static inline struct nand_ecc_engine_ops *mxic_ecc_get_pipelined_ops(void)
{
	return NULL;
}

static inline struct nand_ecc_engine *
mxic_ecc_get_pipelined_engine(struct platform_device *spi_pdev)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void mxic_ecc_put_pipelined_engine(struct nand_ecc_engine *eng) {}

static inline int mxic_ecc_process_data_pipelined(struct nand_ecc_engine *eng,
						  unsigned int direction,
						  dma_addr_t dirmap)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_MTD_NAND_ECC_MXIC */

#endif /* __MTD_NAND_ECC_MXIC_H__ */
