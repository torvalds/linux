/* QLogic qed NIC Driver
 *
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_IF_H
#define _QED_IF_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/qed/common_hsi.h>
#include <linux/qed/qed_chain.h>

enum qed_led_mode {
	QED_LED_MODE_OFF,
	QED_LED_MODE_ON,
	QED_LED_MODE_RESTORE
};

#define DIRECT_REG_WR(reg_addr, val) writel((u32)val, \
					    (void __iomem *)(reg_addr))

#define DIRECT_REG_RD(reg_addr) readl((void __iomem *)(reg_addr))

#define QED_COALESCE_MAX 0xFF

/* forward */
struct qed_dev;

struct qed_eth_pf_params {
	/* The following parameters are used during HW-init
	 * and these parameters need to be passed as arguments
	 * to update_pf_params routine invoked before slowpath start
	 */
	u16 num_cons;
};

struct qed_pf_params {
	struct qed_eth_pf_params eth_pf_params;
};

enum qed_int_mode {
	QED_INT_MODE_INTA,
	QED_INT_MODE_MSIX,
	QED_INT_MODE_MSI,
	QED_INT_MODE_POLL,
};

struct qed_sb_info {
	struct status_block	*sb_virt;
	dma_addr_t		sb_phys;
	u32			sb_ack; /* Last given ack */
	u16			igu_sb_id;
	void __iomem		*igu_addr;
	u8			flags;
#define QED_SB_INFO_INIT        0x1
#define QED_SB_INFO_SETUP       0x2

	struct qed_dev		*cdev;
};

struct qed_dev_info {
	unsigned long	pci_mem_start;
	unsigned long	pci_mem_end;
	unsigned int	pci_irq;
	u8		num_hwfns;

	u8		hw_mac[ETH_ALEN];
	bool		is_mf_default;

	/* FW version */
	u16		fw_major;
	u16		fw_minor;
	u16		fw_rev;
	u16		fw_eng;

	/* MFW version */
	u32		mfw_rev;

	u32		flash_size;
	u8		mf_mode;
};

enum qed_sb_type {
	QED_SB_TYPE_L2_QUEUE,
};

enum qed_protocol {
	QED_PROTOCOL_ETH,
};

struct qed_link_params {
	bool	link_up;

#define QED_LINK_OVERRIDE_SPEED_AUTONEG         BIT(0)
#define QED_LINK_OVERRIDE_SPEED_ADV_SPEEDS      BIT(1)
#define QED_LINK_OVERRIDE_SPEED_FORCED_SPEED    BIT(2)
#define QED_LINK_OVERRIDE_PAUSE_CONFIG          BIT(3)
	u32	override_flags;
	bool	autoneg;
	u32	adv_speeds;
	u32	forced_speed;
#define QED_LINK_PAUSE_AUTONEG_ENABLE           BIT(0)
#define QED_LINK_PAUSE_RX_ENABLE                BIT(1)
#define QED_LINK_PAUSE_TX_ENABLE                BIT(2)
	u32	pause_config;
};

struct qed_link_output {
	bool	link_up;

	u32	supported_caps;         /* In SUPPORTED defs */
	u32	advertised_caps;        /* In ADVERTISED defs */
	u32	lp_caps;                /* In ADVERTISED defs */
	u32	speed;                  /* In Mb/s */
	u8	duplex;                 /* In DUPLEX defs */
	u8	port;                   /* In PORT defs */
	bool	autoneg;
	u32	pause_config;
};

#define QED_DRV_VER_STR_SIZE 12
struct qed_slowpath_params {
	u32	int_mode;
	u8	drv_major;
	u8	drv_minor;
	u8	drv_rev;
	u8	drv_eng;
	u8	name[QED_DRV_VER_STR_SIZE];
};

#define ILT_PAGE_SIZE_TCFC 0x8000 /* 32KB */

struct qed_int_info {
	struct msix_entry	*msix;
	u8			msix_cnt;

	/* This should be updated by the protocol driver */
	u8			used_cnt;
};

struct qed_common_cb_ops {
	void	(*link_update)(void			*dev,
			       struct qed_link_output	*link);
};

struct qed_common_ops {
	struct qed_dev*	(*probe)(struct pci_dev *dev,
				 enum qed_protocol protocol,
				 u32 dp_module, u8 dp_level);

	void		(*remove)(struct qed_dev *cdev);

	int		(*set_power_state)(struct qed_dev *cdev,
					   pci_power_t state);

	void		(*set_id)(struct qed_dev *cdev,
				  char name[],
				  char ver_str[]);

	/* Client drivers need to make this call before slowpath_start.
	 * PF params required for the call before slowpath_start is
	 * documented within the qed_pf_params structure definition.
	 */
	void		(*update_pf_params)(struct qed_dev *cdev,
					    struct qed_pf_params *params);
	int		(*slowpath_start)(struct qed_dev *cdev,
					  struct qed_slowpath_params *params);

	int		(*slowpath_stop)(struct qed_dev *cdev);

	/* Requests to use `cnt' interrupts for fastpath.
	 * upon success, returns number of interrupts allocated for fastpath.
	 */
	int		(*set_fp_int)(struct qed_dev *cdev,
				      u16 cnt);

	/* Fills `info' with pointers required for utilizing interrupts */
	int		(*get_fp_int)(struct qed_dev *cdev,
				      struct qed_int_info *info);

	u32		(*sb_init)(struct qed_dev *cdev,
				   struct qed_sb_info *sb_info,
				   void *sb_virt_addr,
				   dma_addr_t sb_phy_addr,
				   u16 sb_id,
				   enum qed_sb_type type);

	u32		(*sb_release)(struct qed_dev *cdev,
				      struct qed_sb_info *sb_info,
				      u16 sb_id);

	void		(*simd_handler_config)(struct qed_dev *cdev,
					       void *token,
					       int index,
					       void (*handler)(void *));

	void		(*simd_handler_clean)(struct qed_dev *cdev,
					      int index);
/**
 * @brief set_link - set links according to params
 *
 * @param cdev
 * @param params - values used to override the default link configuration
 *
 * @return 0 on success, error otherwise.
 */
	int		(*set_link)(struct qed_dev *cdev,
				    struct qed_link_params *params);

/**
 * @brief get_link - returns the current link state.
 *
 * @param cdev
 * @param if_link - structure to be filled with current link configuration.
 */
	void		(*get_link)(struct qed_dev *cdev,
				    struct qed_link_output *if_link);

/**
 * @brief - drains chip in case Tx completions fail to arrive due to pause.
 *
 * @param cdev
 */
	int		(*drain)(struct qed_dev *cdev);

/**
 * @brief update_msglvl - update module debug level
 *
 * @param cdev
 * @param dp_module
 * @param dp_level
 */
	void		(*update_msglvl)(struct qed_dev *cdev,
					 u32 dp_module,
					 u8 dp_level);

	int		(*chain_alloc)(struct qed_dev *cdev,
				       enum qed_chain_use_mode intended_use,
				       enum qed_chain_mode mode,
				       u16 num_elems,
				       size_t elem_size,
				       struct qed_chain *p_chain);

	void		(*chain_free)(struct qed_dev *cdev,
				      struct qed_chain *p_chain);

/**
 * @brief set_led - Configure LED mode
 *
 * @param cdev
 * @param mode - LED mode
 *
 * @return 0 on success, error otherwise.
 */
	int (*set_led)(struct qed_dev *cdev,
		       enum qed_led_mode mode);
};

/**
 * @brief qed_get_protocol_version
 *
 * @param protocol
 *
 * @return version supported by qed for given protocol driver
 */
u32 qed_get_protocol_version(enum qed_protocol protocol);

#define MASK_FIELD(_name, _value) \
	((_value) &= (_name ## _MASK))

#define FIELD_VALUE(_name, _value) \
	((_value & _name ## _MASK) << _name ## _SHIFT)

#define SET_FIELD(value, name, flag)			       \
	do {						       \
		(value) &= ~(name ## _MASK << name ## _SHIFT); \
		(value) |= (((u64)flag) << (name ## _SHIFT));  \
	} while (0)

#define GET_FIELD(value, name) \
	(((value) >> (name ## _SHIFT)) & name ## _MASK)

/* Debug print definitions */
#define DP_ERR(cdev, fmt, ...)						     \
		pr_err("[%s:%d(%s)]" fmt,				     \
		       __func__, __LINE__,				     \
		       DP_NAME(cdev) ? DP_NAME(cdev) : "",		     \
		       ## __VA_ARGS__)					     \

#define DP_NOTICE(cdev, fmt, ...)				      \
	do {							      \
		if (unlikely((cdev)->dp_level <= QED_LEVEL_NOTICE)) { \
			pr_notice("[%s:%d(%s)]" fmt,		      \
				  __func__, __LINE__,		      \
				  DP_NAME(cdev) ? DP_NAME(cdev) : "", \
				  ## __VA_ARGS__);		      \
								      \
		}						      \
	} while (0)

#define DP_INFO(cdev, fmt, ...)					      \
	do {							      \
		if (unlikely((cdev)->dp_level <= QED_LEVEL_INFO)) {   \
			pr_notice("[%s:%d(%s)]" fmt,		      \
				  __func__, __LINE__,		      \
				  DP_NAME(cdev) ? DP_NAME(cdev) : "", \
				  ## __VA_ARGS__);		      \
		}						      \
	} while (0)

#define DP_VERBOSE(cdev, module, fmt, ...)				\
	do {								\
		if (unlikely(((cdev)->dp_level <= QED_LEVEL_VERBOSE) &&	\
			     ((cdev)->dp_module & module))) {		\
			pr_notice("[%s:%d(%s)]" fmt,			\
				  __func__, __LINE__,			\
				  DP_NAME(cdev) ? DP_NAME(cdev) : "",	\
				  ## __VA_ARGS__);			\
		}							\
	} while (0)

enum DP_LEVEL {
	QED_LEVEL_VERBOSE	= 0x0,
	QED_LEVEL_INFO		= 0x1,
	QED_LEVEL_NOTICE	= 0x2,
	QED_LEVEL_ERR		= 0x3,
};

#define QED_LOG_LEVEL_SHIFT     (30)
#define QED_LOG_VERBOSE_MASK    (0x3fffffff)
#define QED_LOG_INFO_MASK       (0x40000000)
#define QED_LOG_NOTICE_MASK     (0x80000000)

enum DP_MODULE {
	QED_MSG_SPQ	= 0x10000,
	QED_MSG_STATS	= 0x20000,
	QED_MSG_DCB	= 0x40000,
	QED_MSG_IOV	= 0x80000,
	QED_MSG_SP	= 0x100000,
	QED_MSG_STORAGE = 0x200000,
	QED_MSG_CXT	= 0x800000,
	QED_MSG_ILT	= 0x2000000,
	QED_MSG_ROCE	= 0x4000000,
	QED_MSG_DEBUG	= 0x8000000,
	/* to be added...up to 0x8000000 */
};

enum qed_mf_mode {
	QED_MF_DEFAULT,
	QED_MF_OVLAN,
	QED_MF_NPAR,
};

struct qed_eth_stats {
	u64	no_buff_discards;
	u64	packet_too_big_discard;
	u64	ttl0_discard;
	u64	rx_ucast_bytes;
	u64	rx_mcast_bytes;
	u64	rx_bcast_bytes;
	u64	rx_ucast_pkts;
	u64	rx_mcast_pkts;
	u64	rx_bcast_pkts;
	u64	mftag_filter_discards;
	u64	mac_filter_discards;
	u64	tx_ucast_bytes;
	u64	tx_mcast_bytes;
	u64	tx_bcast_bytes;
	u64	tx_ucast_pkts;
	u64	tx_mcast_pkts;
	u64	tx_bcast_pkts;
	u64	tx_err_drop_pkts;
	u64	tpa_coalesced_pkts;
	u64	tpa_coalesced_events;
	u64	tpa_aborts_num;
	u64	tpa_not_coalesced_pkts;
	u64	tpa_coalesced_bytes;

	/* port */
	u64	rx_64_byte_packets;
	u64	rx_127_byte_packets;
	u64	rx_255_byte_packets;
	u64	rx_511_byte_packets;
	u64	rx_1023_byte_packets;
	u64	rx_1518_byte_packets;
	u64	rx_1522_byte_packets;
	u64	rx_2047_byte_packets;
	u64	rx_4095_byte_packets;
	u64	rx_9216_byte_packets;
	u64	rx_16383_byte_packets;
	u64	rx_crc_errors;
	u64	rx_mac_crtl_frames;
	u64	rx_pause_frames;
	u64	rx_pfc_frames;
	u64	rx_align_errors;
	u64	rx_carrier_errors;
	u64	rx_oversize_packets;
	u64	rx_jabbers;
	u64	rx_undersize_packets;
	u64	rx_fragments;
	u64	tx_64_byte_packets;
	u64	tx_65_to_127_byte_packets;
	u64	tx_128_to_255_byte_packets;
	u64	tx_256_to_511_byte_packets;
	u64	tx_512_to_1023_byte_packets;
	u64	tx_1024_to_1518_byte_packets;
	u64	tx_1519_to_2047_byte_packets;
	u64	tx_2048_to_4095_byte_packets;
	u64	tx_4096_to_9216_byte_packets;
	u64	tx_9217_to_16383_byte_packets;
	u64	tx_pause_frames;
	u64	tx_pfc_frames;
	u64	tx_lpi_entry_count;
	u64	tx_total_collisions;
	u64	brb_truncates;
	u64	brb_discards;
	u64	rx_mac_bytes;
	u64	rx_mac_uc_packets;
	u64	rx_mac_mc_packets;
	u64	rx_mac_bc_packets;
	u64	rx_mac_frames_ok;
	u64	tx_mac_bytes;
	u64	tx_mac_uc_packets;
	u64	tx_mac_mc_packets;
	u64	tx_mac_bc_packets;
	u64	tx_mac_ctrl_frames;
};

#define QED_SB_IDX              0x0002

#define RX_PI           0
#define TX_PI(tc)       (RX_PI + 1 + tc)

struct qed_sb_cnt_info {
	int	sb_cnt;
	int	sb_iov_cnt;
	int	sb_free_blk;
};

static inline u16 qed_sb_update_sb_idx(struct qed_sb_info *sb_info)
{
	u32 prod = 0;
	u16 rc = 0;

	prod = le32_to_cpu(sb_info->sb_virt->prod_index) &
	       STATUS_BLOCK_PROD_INDEX_MASK;
	if (sb_info->sb_ack != prod) {
		sb_info->sb_ack = prod;
		rc |= QED_SB_IDX;
	}

	/* Let SB update */
	mmiowb();
	return rc;
}

/**
 *
 * @brief This function creates an update command for interrupts that is
 *        written to the IGU.
 *
 * @param sb_info       - This is the structure allocated and
 *                 initialized per status block. Assumption is
 *                 that it was initialized using qed_sb_init
 * @param int_cmd       - Enable/Disable/Nop
 * @param upd_flg       - whether igu consumer should be
 *                 updated.
 *
 * @return inline void
 */
static inline void qed_sb_ack(struct qed_sb_info *sb_info,
			      enum igu_int_cmd int_cmd,
			      u8 upd_flg)
{
	struct igu_prod_cons_update igu_ack = { 0 };

	igu_ack.sb_id_and_flags =
		((sb_info->sb_ack << IGU_PROD_CONS_UPDATE_SB_INDEX_SHIFT) |
		 (upd_flg << IGU_PROD_CONS_UPDATE_UPDATE_FLAG_SHIFT) |
		 (int_cmd << IGU_PROD_CONS_UPDATE_ENABLE_INT_SHIFT) |
		 (IGU_SEG_ACCESS_REG <<
		  IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_SHIFT));

	DIRECT_REG_WR(sb_info->igu_addr, igu_ack.sb_id_and_flags);

	/* Both segments (interrupts & acks) are written to same place address;
	 * Need to guarantee all commands will be received (in-order) by HW.
	 */
	mmiowb();
	barrier();
}

static inline void __internal_ram_wr(void *p_hwfn,
				     void __iomem *addr,
				     int size,
				     u32 *data)

{
	unsigned int i;

	for (i = 0; i < size / sizeof(*data); i++)
		DIRECT_REG_WR(&((u32 __iomem *)addr)[i], data[i]);
}

static inline void internal_ram_wr(void __iomem *addr,
				   int size,
				   u32 *data)
{
	__internal_ram_wr(NULL, addr, size, data);
}

#endif
