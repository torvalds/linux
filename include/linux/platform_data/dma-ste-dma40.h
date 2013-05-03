/*
 * Copyright (C) ST-Ericsson SA 2007-2010
 * Author: Per Forlin <per.forlin@stericsson.com> for ST-Ericsson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */


#ifndef STE_DMA40_H
#define STE_DMA40_H

#include <linux/dmaengine.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>

/*
 * Maxium size for a single dma descriptor
 * Size is limited to 16 bits.
 * Size is in the units of addr-widths (1,2,4,8 bytes)
 * Larger transfers will be split up to multiple linked desc
 */
#define STEDMA40_MAX_SEG_SIZE 0xFFFF

/* dev types for memcpy */
#define STEDMA40_DEV_DST_MEMORY (-1)
#define	STEDMA40_DEV_SRC_MEMORY (-1)

enum stedma40_mode {
	STEDMA40_MODE_LOGICAL = 0,
	STEDMA40_MODE_PHYSICAL,
	STEDMA40_MODE_OPERATION,
};

enum stedma40_mode_opt {
	STEDMA40_PCHAN_BASIC_MODE = 0,
	STEDMA40_LCHAN_SRC_LOG_DST_LOG = 0,
	STEDMA40_PCHAN_MODULO_MODE,
	STEDMA40_PCHAN_DOUBLE_DST_MODE,
	STEDMA40_LCHAN_SRC_PHY_DST_LOG,
	STEDMA40_LCHAN_SRC_LOG_DST_PHY,
};

#define STEDMA40_ESIZE_8_BIT  0x0
#define STEDMA40_ESIZE_16_BIT 0x1
#define STEDMA40_ESIZE_32_BIT 0x2
#define STEDMA40_ESIZE_64_BIT 0x3

/* The value 4 indicates that PEN-reg shall be set to 0 */
#define STEDMA40_PSIZE_PHY_1  0x4
#define STEDMA40_PSIZE_PHY_2  0x0
#define STEDMA40_PSIZE_PHY_4  0x1
#define STEDMA40_PSIZE_PHY_8  0x2
#define STEDMA40_PSIZE_PHY_16 0x3

/*
 * The number of elements differ in logical and
 * physical mode
 */
#define STEDMA40_PSIZE_LOG_1  STEDMA40_PSIZE_PHY_2
#define STEDMA40_PSIZE_LOG_4  STEDMA40_PSIZE_PHY_4
#define STEDMA40_PSIZE_LOG_8  STEDMA40_PSIZE_PHY_8
#define STEDMA40_PSIZE_LOG_16 STEDMA40_PSIZE_PHY_16

/* Maximum number of possible physical channels */
#define STEDMA40_MAX_PHYS 32

enum stedma40_flow_ctrl {
	STEDMA40_NO_FLOW_CTRL,
	STEDMA40_FLOW_CTRL,
};

enum stedma40_periph_data_width {
	STEDMA40_BYTE_WIDTH = STEDMA40_ESIZE_8_BIT,
	STEDMA40_HALFWORD_WIDTH = STEDMA40_ESIZE_16_BIT,
	STEDMA40_WORD_WIDTH = STEDMA40_ESIZE_32_BIT,
	STEDMA40_DOUBLEWORD_WIDTH = STEDMA40_ESIZE_64_BIT
};

enum stedma40_xfer_dir {
	STEDMA40_MEM_TO_MEM = 1,
	STEDMA40_MEM_TO_PERIPH,
	STEDMA40_PERIPH_TO_MEM,
	STEDMA40_PERIPH_TO_PERIPH
};


/**
 * struct stedma40_chan_cfg - dst/src channel configuration
 *
 * @big_endian: true if the src/dst should be read as big endian
 * @data_width: Data width of the src/dst hardware
 * @p_size: Burst size
 * @flow_ctrl: Flow control on/off.
 */
struct stedma40_half_channel_info {
	bool big_endian;
	enum stedma40_periph_data_width data_width;
	int psize;
	enum stedma40_flow_ctrl flow_ctrl;
};

/**
 * struct stedma40_chan_cfg - Structure to be filled by client drivers.
 *
 * @dir: MEM 2 MEM, PERIPH 2 MEM , MEM 2 PERIPH, PERIPH 2 PERIPH
 * @high_priority: true if high-priority
 * @realtime: true if realtime mode is to be enabled.  Only available on DMA40
 * version 3+, i.e DB8500v2+
 * @mode: channel mode: physical, logical, or operation
 * @mode_opt: options for the chosen channel mode
 * @dev_type: src/dst device type (driver uses dir to figure out which)
 * @src_info: Parameters for dst half channel
 * @dst_info: Parameters for dst half channel
 * @use_fixed_channel: if true, use physical channel specified by phy_channel
 * @phy_channel: physical channel to use, only if use_fixed_channel is true
 *
 * This structure has to be filled by the client drivers.
 * It is recommended to do all dma configurations for clients in the machine.
 *
 */
struct stedma40_chan_cfg {
	enum stedma40_xfer_dir			 dir;
	bool					 high_priority;
	bool					 realtime;
	enum stedma40_mode			 mode;
	enum stedma40_mode_opt			 mode_opt;
	int					 dev_type;
	struct stedma40_half_channel_info	 src_info;
	struct stedma40_half_channel_info	 dst_info;

	bool					 use_fixed_channel;
	int					 phy_channel;
};

/**
 * struct stedma40_platform_data - Configuration struct for the dma device.
 *
 * @dev_len: length of dev_tx and dev_rx
 * @dev_tx: mapping between destination event line and io address
 * @dev_rx: mapping between source event line and io address
 * @disabled_channels: A vector, ending with -1, that marks physical channels
 * that are for different reasons not available for the driver.
 * @soft_lli_chans: A vector, that marks physical channels will use LLI by SW
 * which avoids HW bug that exists in some versions of the controller.
 * SoftLLI introduces relink overhead that could impact performace for
 * certain use cases.
 * @num_of_soft_lli_chans: The number of channels that needs to be configured
 * to use SoftLLI.
 * @use_esram_lcla: flag for mapping the lcla into esram region
 * @num_of_phy_chans: The number of physical channels implemented in HW.
 * 0 means reading the number of channels from DMA HW but this is only valid
 * for 'multiple of 4' channels, like 8.
 */
struct stedma40_platform_data {
	u32				 dev_len;
	const dma_addr_t		*dev_tx;
	const dma_addr_t		*dev_rx;
	int				 disabled_channels[STEDMA40_MAX_PHYS];
	int				*soft_lli_chans;
	int				 num_of_soft_lli_chans;
	bool				 use_esram_lcla;
	int				 num_of_phy_chans;
};

#ifdef CONFIG_STE_DMA40

/**
 * stedma40_filter() - Provides stedma40_chan_cfg to the
 * ste_dma40 dma driver via the dmaengine framework.
 * does some checking of what's provided.
 *
 * Never directly called by client. It used by dmaengine.
 * @chan: dmaengine handle.
 * @data: Must be of type: struct stedma40_chan_cfg and is
 * the configuration of the framework.
 *
 *
 */

bool stedma40_filter(struct dma_chan *chan, void *data);

/**
 * stedma40_slave_mem() - Transfers a raw data buffer to or from a slave
 * (=device)
 *
 * @chan: dmaengine handle
 * @addr: source or destination physicall address.
 * @size: bytes to transfer
 * @direction: direction of transfer
 * @flags: is actually enum dma_ctrl_flags. See dmaengine.h
 */

static inline struct
dma_async_tx_descriptor *stedma40_slave_mem(struct dma_chan *chan,
					    dma_addr_t addr,
					    unsigned int size,
					    enum dma_transfer_direction direction,
					    unsigned long flags)
{
	struct scatterlist sg;
	sg_init_table(&sg, 1);
	sg.dma_address = addr;
	sg.length = size;

	return dmaengine_prep_slave_sg(chan, &sg, 1, direction, flags);
}

#else
static inline bool stedma40_filter(struct dma_chan *chan, void *data)
{
	return false;
}

static inline struct
dma_async_tx_descriptor *stedma40_slave_mem(struct dma_chan *chan,
					    dma_addr_t addr,
					    unsigned int size,
					    enum dma_transfer_direction direction,
					    unsigned long flags)
{
	return NULL;
}
#endif

#endif
