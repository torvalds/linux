/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_QCOM_GENI_SE_COMMON
#define _LINUX_QCOM_GENI_SE_COMMON
#include <linux/clk.h>
#include <linux/dma-direction.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

#ifdef CONFIG_ARM64
#define GENI_SE_DMA_PTR_L(ptr) ((u32)ptr)
#define GENI_SE_DMA_PTR_H(ptr) ((u32)(ptr >> 32))
#else
#define GENI_SE_DMA_PTR_L(ptr) ((u32)ptr)
#define GENI_SE_DMA_PTR_H(ptr) 0
#endif

#define GENI_SE_ERR(log_ctx, print, dev, x...) do { \
ipc_log_string(log_ctx, x); \
if (print) { \
	if (dev) \
		dev_err((dev), x); \
	else \
		pr_err(x); \
} \
} while (0)

#define GENI_SE_DBG(log_ctx, print, dev, x...) do { \
ipc_log_string(log_ctx, x); \
if (print) { \
	if (dev) \
		dev_dbg((dev), x); \
	else \
		pr_debug(x); \
} \
} while (0)

#define DEFAULT_BUS_WIDTH	(4)

/* In KHz */
#define DEFAULT_SE_CLK	19200
#define SPI_CORE2X_VOTE	51000
#define I2C_CORE2X_VOTE	50000
#define I3C_CORE2X_VOTE	19200
#define APPS_PROC_TO_QUP_VOTE	140000
/* SE_DMA_GENERAL_CFG */
#define SE_DMA_DEBUG_REG0		(0xE40)

#define SE_DMA_TX_PTR_L			(0xC30)
#define SE_DMA_TX_PTR_H			(0xC34)
#define SE_DMA_TX_LEN                   (0xC3C)
#define SE_DMA_TX_IRQ_EN                (0xC48)
#define SE_DMA_TX_LEN_IN                (0xC54)

#define SE_DMA_RX_PTR_L			(0xD30)
#define SE_DMA_RX_PTR_H			(0xD34)
#define SE_DMA_RX_ATTR			(0xD38)
#define SE_DMA_RX_LEN			(0xD3C)
#define SE_DMA_RX_IRQ_EN                (0xD48)
#define SE_DMA_RX_LEN_IN                (0xD54)

#define SE_DMA_TX_IRQ_EN_SET	(0xC4C)
#define SE_DMA_TX_IRQ_EN_CLR	(0xC50)

#define SE_DMA_RX_IRQ_EN_SET	(0xD4C)
#define SE_DMA_RX_IRQ_EN_CLR	(0xD50)

#define TX_GENI_CANCEL_IRQ		(BIT(14))
#define SE_HW_PARAM_2                   (0xE2C)
/* DMA DEBUG Register fields */
#define DMA_TX_ACTIVE			(BIT(0))
#define DMA_RX_ACTIVE			(BIT(1))
#define DMA_TX_STATE			(GENMASK(7, 4))
#define DMA_RX_STATE			(GENMASK(11, 8))

/* SE_IRQ_EN fields */
#define DMA_RX_IRQ_EN			(BIT(0))
#define DMA_TX_IRQ_EN			(BIT(1))
#define GENI_M_IRQ_EN			(BIT(2))
#define GENI_S_IRQ_EN			(BIT(3))

#define GENI_FW_S_REVISION_RO	(0x6C)
#define FW_REV_VERSION_MSK		(GENMASK(7, 0))

/* SE_HW_PARAM_2 fields */
#define GEN_HW_FSM_I2C			(BIT(15))

/* GENI_OUTPUT_CTRL fields */
#define GENI_CFG_REG80		0x240
#define GENI_IO_MUX_0_EN	BIT(0)
#define GENI_IO_MUX_1_EN	BIT(2)

/* GENI_CFG_REG80 fields */
#define IO1_SEL_TX		BIT(2)
#define IO2_DATA_IN_SEL_PAD2	GENMASK(11, 10)
#define IO3_DATA_IN_SEL_PAD2	BIT(15)

#define GSI_TX_PACK_EN          (BIT(0))
#define GSI_RX_PACK_EN          (BIT(1))
#define GSI_PRESERVE_PACK       (BIT(2))

#define HW_VER_MAJOR_MASK GENMASK(31, 28)
#define HW_VER_MAJOR_SHFT 28
#define HW_VER_MINOR_MASK GENMASK(27, 16)
#define HW_VER_MINOR_SHFT 16
#define HW_VER_STEP_MASK GENMASK(15, 0)

#define OTHER_IO_OE		BIT(12)
#define IO2_DATA_IN_SEL		BIT(11)
#define RX_DATA_IN_SEL		BIT(8)
#define IO_MACRO_IO3_SEL	(GENMASK(7, 6))
#define IO_MACRO_IO2_SEL	BIT(5)
#define IO_MACRO_IO0_SEL_BIT	BIT(0)

static inline int geni_se_common_resources_init(struct geni_se *se, u32 geni_to_core,
			 u32 cpu_to_geni, u32 geni_to_ddr)
{
	int ret;

	ret = geni_icc_get(se, "qup-memory");
	if (ret)
		return ret;

	se->icc_paths[GENI_TO_CORE].avg_bw = geni_to_core;
	se->icc_paths[CPU_TO_GENI].avg_bw = cpu_to_geni;
	se->icc_paths[GENI_TO_DDR].avg_bw = geni_to_ddr;

	return ret;
}

static inline int geni_se_common_get_proto(void __iomem *base)
{
	int proto;

	proto = ((readl_relaxed(base + GENI_FW_REVISION_RO)
			& FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT);
	return proto;
}

/**
 * geni_se_common_get_m_fw - Read the Firmware ver for the Main sequencer engine
 * @base:   Base address of the serial engine's register block.
 *
 * Return:  Firmware version for the Main sequencer engine
 */
static inline int geni_se_common_get_m_fw(void __iomem *base)
{
	int fw_ver_m;

	fw_ver_m = ((readl_relaxed(base + GENI_FW_REVISION_RO)
			& FW_REV_VERSION_MSK));
	return fw_ver_m;
}

/**
 * geni_se_common_get_s_fw() - Read the Firmware ver for the Secondry sequencer engine
 * @base:   Base address of the serial engine's register block.
 *
 * Return:  Firmware version for the Secondry sequencer engine
 */
static inline int geni_se_common_get_s_fw(void __iomem *base)
{
	int fw_ver_s;

	fw_ver_s = ((readl_relaxed(base + GENI_FW_S_REVISION_RO)
			& FW_REV_VERSION_MSK));
	return fw_ver_s;
}

/**
 * geni_se_common_clks_off - Disabling SE clks and common clks
 * @se_clk:	Pointer to the SE-CLk.
 * @m_ahb_clk:	Pointer to the SE common m_ahb_clk.
 * @s_ahb_clk:	Pointer to the SE common s_ahb_clk.
 */
static inline void geni_se_common_clks_off(struct clk *se_clk, struct clk *m_ahb_clk,
					struct clk *s_ahb_clk)
{
	clk_disable_unprepare(se_clk);
	clk_disable_unprepare(m_ahb_clk);
	clk_disable_unprepare(s_ahb_clk);
}

/**
 * geni_se_common_clks_on - enabling SE clks and common clks
 * @se_clk:	Pointer to the SE-CLk.
 * @m_ahb_clk:	Pointer to the SE common m_ahb_clk.
 * @s_ahb_clk:	Pointer to the SE common s_ahb_clk.
 */
static inline int geni_se_common_clks_on(struct clk *se_clk, struct clk *m_ahb_clk,
					struct clk *s_ahb_clk)
{
	int ret;

	ret = clk_prepare_enable(m_ahb_clk);
	if (ret)
		goto clks_on_err1;

	ret = clk_prepare_enable(s_ahb_clk);
	if (ret)
		goto clks_on_err2;

	ret = clk_prepare_enable(se_clk);
	if (ret)
		goto clks_on_err3;

	return 0;

clks_on_err3:
	clk_disable_unprepare(s_ahb_clk);
clks_on_err2:
	clk_disable_unprepare(m_ahb_clk);
clks_on_err1:
	return ret;
}


/**
 * geni_write_reg() - Helper function to write into a GENI register
 * @value:	Value to be written into the register.
 * @base:	Base address of the serial engine's register block.
 * @offset:	Offset within the serial engine's register block.
 */
static inline  void geni_write_reg(unsigned int value, void __iomem *base, int offset)
{
	return writel_relaxed(value, (base + offset));
}

/**
 * geni_read_reg() - Helper function to read from a GENI register
 * @base:	Base address of the serial engine's register block.
 * @offset:	Offset within the serial engine's register block.
 *
 * Return:	Return the contents of the register.
 */
static inline unsigned int geni_read_reg(void __iomem *base, int offset)
{
	return readl_relaxed(base + offset);
}


/**
 * geni_se_common_iommu_map_buf() - Map a single buffer into QUPv3 context bank
 * @wrapper_dev:	Pointer to the corresponding QUPv3 wrapper core.
 * @iova:		Pointer in which the mapped virtual address is stored.
 * @buf:		Address of the buffer that needs to be mapped.
 * @size:		Size of the buffer.
 * @dir:		Direction of the DMA transfer.
 *
 * This function is used to map an already allocated buffer into the
 * QUPv3 context bank device space.
 *
 * Return:	0 on success, standard Linux error codes on failure/error.
 */
static inline int geni_se_common_iommu_map_buf(struct device *wrapper_dev, dma_addr_t *iova,
			  void *buf, size_t size, enum dma_data_direction dir)
{
	if (!wrapper_dev)
		return -EINVAL;

	*iova = dma_map_single(wrapper_dev, buf, size, dir);
	if (dma_mapping_error(wrapper_dev, *iova))
		return -EIO;

	return 0;
}

/**
 * geni_se_common_iommu_unmap_buf() - Unmap a single buffer from QUPv3 context bank
 * @wrapper_dev:	Pointer to the corresponding QUPv3 wrapper core.
 * @iova:		Pointer in which the mapped virtual address is stored.
 * @size:		Size of the buffer.
 * @dir:		Direction of the DMA transfer.
 *
 * This function is used to unmap an already mapped buffer from the
 * QUPv3 context bank device space.
 *
 * Return:	0 on success, standard Linux error codes on failure/error.
 */
static inline int geni_se_common_iommu_unmap_buf(struct device *wrapper_dev, dma_addr_t *iova,
			    size_t size, enum dma_data_direction dir)
{
	if (!dma_mapping_error(wrapper_dev, *iova))
		dma_unmap_single(wrapper_dev, *iova,  size, dir);
	return 0;
}

/**
 * geni_se_common_iommu_alloc_buf() - Allocate & map a single buffer into QUPv3
 *                 context bank
 * @wrapper_dev:    Pointer to the corresponding QUPv3 wrapper core.
 * @iova:       Pointer in which the mapped virtual address is stored.
 * @size:       Size of the buffer.
 *
 * This function is used to allocate a buffer and map it into the
 * QUPv3 context bank device space.
 *
 * Return:  address of the buffer on success, NULL or ERR_PTR on
 *      failure/error.
 */
static inline void *geni_se_common_iommu_alloc_buf(struct device *wrapper_dev, dma_addr_t *iova,
				size_t size)
{
	void *buf = NULL;

	if (!wrapper_dev || !iova || !size)
		return ERR_PTR(-EINVAL);

	*iova = DMA_MAPPING_ERROR;
	buf = dma_alloc_coherent(wrapper_dev, size, iova, GFP_KERNEL);
	return buf;
}

/**
 * geni_se_common_iommu_free_buf() - Unmap & free a single buffer from QUPv3
 *                context bank
 * @wrapper_dev:    Pointer to the corresponding QUPv3 wrapper core.
 * @iova:       Pointer in which the mapped virtual address is stored.
 * @buf:        Address of the buffer.
 * @size:       Size of the buffer.
 *
 * This function is used to unmap and free a buffer from the
 * QUPv3 context bank device space.
 *
 * Return:  0 on success, standard Linux error codes on failure/error.
 */
static inline int geni_se_common_iommu_free_buf(struct device *wrapper_dev, dma_addr_t *iova,
				void *buf, size_t size)
{
	if (!wrapper_dev || !iova || !buf || !size)
		return -EINVAL;

	dma_free_coherent(wrapper_dev, size, buf, *iova);
	return 0;
}

/**
 * geni_se_common_rx_dma_start() - Prepare Serial Engine registers for RX DMA
				transfers.
 * @base:       Base address of the SE register block.
 * @rx_len:     Length of the RX buffer.
 * @rx_dma:     Pointer to store the mapped DMA address.
 *
 * This function is used to prepare the Serial Engine registers for DMA RX.
 *
 * Return:  None.
 */
static inline void geni_se_common_rx_dma_start(void __iomem *base, int rx_len, dma_addr_t *rx_dma)
{
	if (!*rx_dma || !base || !rx_len)
		return;

	geni_write_reg(7, base, SE_DMA_RX_IRQ_EN_SET);
	geni_write_reg(GENI_SE_DMA_PTR_L(*rx_dma), base, SE_DMA_RX_PTR_L);
	geni_write_reg(GENI_SE_DMA_PTR_H(*rx_dma), base, SE_DMA_RX_PTR_H);
	/* RX does not have EOT bit */
	geni_write_reg(0, base, SE_DMA_RX_ATTR);

	/* Ensure that above register writes went through */
	 mb();
	geni_write_reg(rx_len, base, SE_DMA_RX_LEN);
}

/**
 * geni_se_common_get_major_minor_num() - Split qup hw_version into
				major, minor and step.
 * @hw_version:	HW version of the qup
 * @major:      Buffer for Major Version field.
 * @minor:      Buffer for Minor Version field.
 * @step:       Buffer for Step Version field.
 *
 * Return:  None
 */
static inline void geni_se_common_get_major_minor_num(u32 hw_version,
			unsigned int *major, unsigned int *minor, unsigned int *step)
{
	*major = (hw_version & HW_VER_MAJOR_MASK) >> HW_VER_MAJOR_SHFT;
	*minor = (hw_version & HW_VER_MINOR_MASK) >> HW_VER_MINOR_SHFT;
	*step = hw_version & HW_VER_STEP_MASK;
}
#endif

