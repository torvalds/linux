/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_QCOM_GENI_SE_COMMON
#define _LINUX_QCOM_GENI_SE_COMMON
#include <linux/clk.h>
#include <linux/dma-direction.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/sched/clock.h>
#include <linux/ipc_logging.h>
#include <linux/soc/qcom/geni-se.h>

#ifdef CONFIG_ARM64
#define GENI_SE_DMA_PTR_L(ptr) ((u32)ptr)
#define GENI_SE_DMA_PTR_H(ptr) ((u32)(ptr >> 32))
#else
#define GENI_SE_DMA_PTR_L(ptr) ((u32)ptr)
#define GENI_SE_DMA_PTR_H(ptr) 0
#endif

#define QUPV3_TEST_BUS_EN      0x204 //write 0x11
#define QUPV3_TEST_BUS_SEL     0x200 //write 0x5  [for SE index 4)
#define QUPV3_TEST_BUS_REG     0x208 //Read only reg, to be read as part of dump
#define IPC_LOG_KPI_PAGES	(4)  // KPI IPC Log size

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
#define Q2SPI_CORE2X_VOTE	100000
#define I2C_CORE2X_VOTE	50000
#define I3C_CORE2X_VOTE	50000
#define APPS_PROC_TO_QUP_VOTE	140000

/* COMMON SE REGISTERS */
#define GENI_GENERAL_CFG		(0x10)
#define GENI_CLK_CTRL_RO		(0x60)
#define GENI_FW_MULTILOCK_MSA_RO	(0x74)

/* SE_DMA_GENERAL_CFG */
#define DMA_IF_EN_RO			(0xe20)
#define SE_GSI_EVENT_EN			(0xe18)
#define SE_IRQ_EN			(0xe1c)
#define DMA_GENERAL_CFG			(0xe30)
#define SE_DMA_DEBUG_REG0		(0xE40)
#define SE_DMA_TX_PTR_L			(0xC30)
#define SE_DMA_TX_PTR_H			(0xC34)
#define SE_DMA_TX_ATTR			(0xC38)
#define SE_DMA_TX_LEN                   (0xC3C)
#define SE_DMA_TX_IRQ_EN                (0xC48)
#define SE_DMA_TX_LEN_IN                (0xC54)
#define GENI_SE_DMA_EOT_BUF		(BIT(0))

#define SE_DMA_RX_PTR_L			(0xD30)
#define SE_DMA_RX_PTR_H			(0xD34)
#define SE_DMA_RX_ATTR			(0xD38)
#define SE_DMA_RX_LEN			(0xD3C)
#define SE_DMA_RX_IRQ_EN                (0xD48)

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

/* Notifier block Structure */
struct ssc_qup_nb {
	struct notifier_block nb;
	/*Notifier block pointer to next notifier block structure*/
	void *next;
};

/*
 * struct ssc_qup_ssr - GENI Serial Engine SSC qup SSR Structure.
 * @is_ssr_down:	To check SE status.
 * @subsys_name:	Subsystem name for ssr registration.
 * @active_list_head:	List Head of all client in SSC QUPv3.
 */
struct ssc_qup_ssr {
	struct ssc_qup_nb ssc_qup_nb;
	struct list_head active_list_head;
	const char *subsys_name;
	bool is_ssr_down;
};

/*
 * struct se_rsc_ssr - GENI Resource SSR Structure.
 * @active_list:	List of SSC qup SE clients.
 * @force_suspend:	Function pointer for Subsystem shutdown case.
 * @force_resume:	Function pointer for Subsystem restart case.
 */
struct se_rsc_ssr {
	struct list_head active_list;
	void (*force_suspend)(struct device *ctrl_dev);
	void (*force_resume)(struct device *ctrl_dev);
	bool ssr_enable;
};

/*
 * geni_se_ssc_device - GENI SSC QUP driver Structure.
 * @dev:		Pointer to SSC driver device structure.
 * @wrapper_dev:	Pointer to SSC QUP wrapper dev struct.
 * @ssr:		SSC QUP SSR Structure.
 * @ssc_clks:		Pointer to clock data structure.
 * @is_ssc_clk_enabled:	To maintain clock is enable/disable.
 * @log_ctx:		Pointer to IPC log structure.
 */
struct geni_se_ssc_device {
	struct device *dev;
	struct device *wrapper_dev; /* QUP driver handler */
	struct ssc_qup_ssr ssr;
	struct clk_bulk_data *ssc_clks;
	atomic_t is_ssc_clk_enabled;
	void *log_ctx;
};

/*
 * geni_se_rsc - GENI SE Resource Structure.
 * @ctrl_dev:	 Pointer to protocol driver struct.
 * @ssc_dev:	 Pointer to SSC QUP driver struct
 * @se_rsc:	 Pointer to SE resource struct.
 * @rsc_ssr:	 Pointer to SE SSR struct.
 */
struct geni_se_rsc {
	struct device *ctrl_dev;
	struct geni_se_ssc_device *ssc_dev;
	struct geni_se *se_rsc;
	struct se_rsc_ssr rsc_ssr;
};

/*
 * struct kpi_time - Help to capture KPI information
 * @len: length of the request
 * @time_stamp: Time stamp of the request
 *
 * This struct used to hold length and time stamp of Tx/Rx request
 *
 */
struct kpi_time {
	unsigned int len;
	unsigned long long time_stamp;
};

#if IS_ENABLED(CONFIG_QCOM_GENI_SE_SSC)

struct geni_se_ssc_device *get_se_ssc_dev(void);
void geni_se_ssc_clk_enable(struct geni_se_rsc *rsc, bool enable);

#else

static inline void geni_se_ssc_clk_enable(struct geni_se_rsc *rsc, bool enable)
{ }

#endif

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

static inline int geni_se_common_rsc_init(struct geni_se_rsc *rsc, u32 geni_to_core,
					  u32 cpu_to_geni, u32 geni_to_ddr)
{
	if (!rsc || !rsc->se_rsc)
		return -EINVAL;

	if (rsc->rsc_ssr.ssr_enable) {
#if IS_ENABLED(CONFIG_QCOM_GENI_SE_SSC)
		struct geni_se_ssc_device *dev = get_se_ssc_dev();

		if (!dev) {
			dev_err(rsc->ctrl_dev, "%s: SSC QUP is not enabled\n",
				__func__);
		} else {
			rsc->ssc_dev = dev;
			if (dev->ssr.subsys_name) {
				INIT_LIST_HEAD(&rsc->rsc_ssr.active_list);
				list_add(&rsc->rsc_ssr.active_list,
					 &dev->ssr.active_list_head);
			} else {
				dev_err(rsc->ctrl_dev, "%s: SSR subsystem is not enabled\n",
					__func__);
			}
		}
#endif
	}
	return geni_se_common_resources_init(rsc->se_rsc, geni_to_core,
					     cpu_to_geni, geni_to_ddr);
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

/*
 * test_bus_enable_per_qupv3: enables particular test bus number.
 * @wrapper_dev: QUPV3 common driver handle from SE driver
 *
 * Note: Need to call only once.
 *
 * Return: none
 */
static inline void test_bus_enable_per_qupv3(struct device *wrapper_dev, void *ipc)
{
	struct geni_se *geni_se_dev;

	geni_se_dev = dev_get_drvdata(wrapper_dev);
	//Enablement of test bus is required only once.
	//TEST_BUS_EN:4, TEST_BUS_REG_EN:0
	geni_write_reg(0x11, geni_se_dev->base, QUPV3_TEST_BUS_EN);
	GENI_SE_ERR(ipc, false, geni_se_dev->dev,
		    "%s: TEST_BUS_EN: 0x%x @address:0x%x\n",
		    __func__, geni_read_reg(geni_se_dev->base, QUPV3_TEST_BUS_EN),
		    (geni_se_dev->base + QUPV3_TEST_BUS_EN));
}

/*
 * test_bus_select_per_qupv3: Selects the test bus as required
 * @wrapper_dev: QUPV3 common driver handle from SE driver
 * @test_bus_num: GENI SE number from QUPV3 core. E.g. SE0 should pass value 1.
 *
 * @Return: None
 */
static inline void test_bus_select_per_qupv3(struct device *wrapper_dev, u8 test_bus_num, void *ipc)
{
	struct geni_se *geni_se_dev;

	geni_se_dev = dev_get_drvdata(wrapper_dev);

	geni_write_reg(test_bus_num, geni_se_dev->base, QUPV3_TEST_BUS_SEL);
	GENI_SE_ERR(ipc, false, geni_se_dev->dev,
		    "%s: readback TEST_BUS_SEL: 0x%x @address:0x%x\n",
		    __func__, geni_read_reg(geni_se_dev->base, QUPV3_TEST_BUS_SEL),
		    (geni_se_dev->base + QUPV3_TEST_BUS_SEL));
}

/*
 * test_bus_read_per_qupv3: Selects the test bus as required
 * @wrapper_dev: QUPV3 common driver handle from SE driver
 *
 * Return: None
 */
static inline void test_bus_read_per_qupv3(struct device *wrapper_dev, void *ipc)
{
	struct geni_se *geni_se_dev;

	geni_se_dev = dev_get_drvdata(wrapper_dev);
	GENI_SE_ERR(ipc, false, geni_se_dev->dev,
		    "%s: dump QUPV3_TEST_BUS_REG:0x%x\n",
		    __func__, geni_read_reg(geni_se_dev->base, QUPV3_TEST_BUS_REG));
}

/**
 * geni_capture_start_time() - Used to capture start time of a function.
 * @se: serial engine device
 * @ipc: which IPC module to be used to log.
 * @func: for which function start time is captured.
 * @geni_kpi_capture_enabled: kpi capture enable flag to start capture the logs or not.
 *
 * Return:  start time if kpi geni_kpi_capture_enabled flag enabled or error value.
 */
static inline unsigned long long geni_capture_start_time(struct geni_se *se, void *ipc,
							 const char *func,
							 int geni_kpi_capture_enabled)
{
	struct device *dev = se->dev;
	unsigned long long start_time = 0;

	if (!ipc)
		return -EINVAL;

	if (geni_kpi_capture_enabled) {
		start_time = sched_clock();
		GENI_SE_ERR(ipc, false, dev,
			    "%s:start at %llu nsec(%llu usec)\n", func,
			    start_time, (start_time / 1000));
	}
	return start_time;
}

/**
 * geni_capture_stop_time() - Logs the function execution time
 * @se:	serial engine device
 * @ipc: which IPC module to be used to log.
 * @func: for which function kpi capture is used.
 * @geni_kpi_capture_enabled: kpi capture enable flag to start capture the logs or not.
 * @start_time: start time of the function
 * @len: Number of bytes of transfer
 * @freq: frequency of operation
 * Return: None
 */
static inline void geni_capture_stop_time(struct geni_se *se, void *ipc,
					  const char *func, int geni_kpi_capture_enabled,
					  unsigned long long start_time, unsigned int len,
					  unsigned int freq)
{
	struct device *dev = se->dev;
	unsigned long long exec_time = 0;

	if (!ipc)
		return;

	if (geni_kpi_capture_enabled && start_time) {
		exec_time = sched_clock() - start_time;
		if (!len)
			GENI_SE_ERR(ipc, false, dev,
				    "%s:took %llu nsec(%llu usec)\n",
				    func, exec_time, (exec_time / 1000));
		else if (len != 0 && freq != 0)
			GENI_SE_ERR(ipc, false, dev,
				    "%s:took %llu nsec(%llu usec) for %u bytes with freq %u\n",
				    func, exec_time, (exec_time / 1000), len, freq);
		else
			GENI_SE_ERR(ipc, false, dev,
				    "%s:took %llu nsec(%llu usec) for %u bytes\n", func,
				    exec_time, (exec_time / 1000), len);
	}
}
#endif

