#ifndef __LINUX_OMAP_DMA_H
#define __LINUX_OMAP_DMA_H
#include <linux/omap-dmaengine.h>

/*
 *  Legacy OMAP DMA handling defines and functions
 *
 *  NOTE: Do not use these any longer.
 *
 *  Use the generic dmaengine functions as defined in
 *  include/linux/dmaengine.h.
 *
 *  Copyright (C) 2003 Nokia Corporation
 *  Author: Juha Yrjölä <juha.yrjola@nokia.com>
 *
 */

#include <linux/platform_device.h>

#define INT_DMA_LCD			(NR_IRQS_LEGACY + 25)

#define OMAP1_DMA_TOUT_IRQ		(1 << 0)
#define OMAP_DMA_DROP_IRQ		(1 << 1)
#define OMAP_DMA_HALF_IRQ		(1 << 2)
#define OMAP_DMA_FRAME_IRQ		(1 << 3)
#define OMAP_DMA_LAST_IRQ		(1 << 4)
#define OMAP_DMA_BLOCK_IRQ		(1 << 5)
#define OMAP1_DMA_SYNC_IRQ		(1 << 6)
#define OMAP2_DMA_PKT_IRQ		(1 << 7)
#define OMAP2_DMA_TRANS_ERR_IRQ		(1 << 8)
#define OMAP2_DMA_SECURE_ERR_IRQ	(1 << 9)
#define OMAP2_DMA_SUPERVISOR_ERR_IRQ	(1 << 10)
#define OMAP2_DMA_MISALIGNED_ERR_IRQ	(1 << 11)

#define OMAP_DMA_CCR_EN			(1 << 7)
#define OMAP_DMA_CCR_RD_ACTIVE		(1 << 9)
#define OMAP_DMA_CCR_WR_ACTIVE		(1 << 10)
#define OMAP_DMA_CCR_SEL_SRC_DST_SYNC	(1 << 24)
#define OMAP_DMA_CCR_BUFFERING_DISABLE	(1 << 25)

#define OMAP_DMA_DATA_TYPE_S8		0x00
#define OMAP_DMA_DATA_TYPE_S16		0x01
#define OMAP_DMA_DATA_TYPE_S32		0x02

#define OMAP_DMA_SYNC_ELEMENT		0x00
#define OMAP_DMA_SYNC_FRAME		0x01
#define OMAP_DMA_SYNC_BLOCK		0x02
#define OMAP_DMA_SYNC_PACKET		0x03

#define OMAP_DMA_DST_SYNC_PREFETCH	0x02
#define OMAP_DMA_SRC_SYNC		0x01
#define OMAP_DMA_DST_SYNC		0x00

#define OMAP_DMA_PORT_EMIFF		0x00
#define OMAP_DMA_PORT_EMIFS		0x01
#define OMAP_DMA_PORT_OCP_T1		0x02
#define OMAP_DMA_PORT_TIPB		0x03
#define OMAP_DMA_PORT_OCP_T2		0x04
#define OMAP_DMA_PORT_MPUI		0x05

#define OMAP_DMA_AMODE_CONSTANT		0x00
#define OMAP_DMA_AMODE_POST_INC		0x01
#define OMAP_DMA_AMODE_SINGLE_IDX	0x02
#define OMAP_DMA_AMODE_DOUBLE_IDX	0x03

#define DMA_DEFAULT_FIFO_DEPTH		0x10
#define DMA_DEFAULT_ARB_RATE		0x01
/* Pass THREAD_RESERVE ORed with THREAD_FIFO for tparams */
#define DMA_THREAD_RESERVE_NORM		(0x00 << 12) /* Def */
#define DMA_THREAD_RESERVE_ONET		(0x01 << 12)
#define DMA_THREAD_RESERVE_TWOT		(0x02 << 12)
#define DMA_THREAD_RESERVE_THREET	(0x03 << 12)
#define DMA_THREAD_FIFO_NONE		(0x00 << 14) /* Def */
#define DMA_THREAD_FIFO_75		(0x01 << 14)
#define DMA_THREAD_FIFO_25		(0x02 << 14)
#define DMA_THREAD_FIFO_50		(0x03 << 14)

/* DMA4_OCP_SYSCONFIG bits */
#define DMA_SYSCONFIG_MIDLEMODE_MASK		(3 << 12)
#define DMA_SYSCONFIG_CLOCKACTIVITY_MASK	(3 << 8)
#define DMA_SYSCONFIG_EMUFREE			(1 << 5)
#define DMA_SYSCONFIG_SIDLEMODE_MASK		(3 << 3)
#define DMA_SYSCONFIG_SOFTRESET			(1 << 2)
#define DMA_SYSCONFIG_AUTOIDLE			(1 << 0)

#define DMA_SYSCONFIG_MIDLEMODE(n)		((n) << 12)
#define DMA_SYSCONFIG_SIDLEMODE(n)		((n) << 3)

#define DMA_IDLEMODE_SMARTIDLE			0x2
#define DMA_IDLEMODE_NO_IDLE			0x1
#define DMA_IDLEMODE_FORCE_IDLE			0x0

/* Chaining modes*/
#ifndef CONFIG_ARCH_OMAP1
#define OMAP_DMA_STATIC_CHAIN		0x1
#define OMAP_DMA_DYNAMIC_CHAIN		0x2
#define OMAP_DMA_CHAIN_ACTIVE		0x1
#define OMAP_DMA_CHAIN_INACTIVE		0x0
#endif

#define DMA_CH_PRIO_HIGH		0x1
#define DMA_CH_PRIO_LOW			0x0 /* Def */

/* Errata handling */
#define IS_DMA_ERRATA(id)		(errata & (id))
#define SET_DMA_ERRATA(id)		(errata |= (id))

#define DMA_ERRATA_IFRAME_BUFFERING	BIT(0x0)
#define DMA_ERRATA_PARALLEL_CHANNELS	BIT(0x1)
#define DMA_ERRATA_i378			BIT(0x2)
#define DMA_ERRATA_i541			BIT(0x3)
#define DMA_ERRATA_i88			BIT(0x4)
#define DMA_ERRATA_3_3			BIT(0x5)
#define DMA_ROMCODE_BUG			BIT(0x6)

/* Attributes for OMAP DMA Contrller */
#define DMA_LINKED_LCH			BIT(0x0)
#define GLOBAL_PRIORITY			BIT(0x1)
#define RESERVE_CHANNEL			BIT(0x2)
#define IS_CSSA_32			BIT(0x3)
#define IS_CDSA_32			BIT(0x4)
#define IS_RW_PRIORITY			BIT(0x5)
#define ENABLE_1510_MODE		BIT(0x6)
#define SRC_PORT			BIT(0x7)
#define DST_PORT			BIT(0x8)
#define SRC_INDEX			BIT(0x9)
#define DST_INDEX			BIT(0xa)
#define IS_BURST_ONLY4			BIT(0xb)
#define CLEAR_CSR_ON_READ		BIT(0xc)
#define IS_WORD_16			BIT(0xd)
#define ENABLE_16XX_MODE		BIT(0xe)
#define HS_CHANNELS_RESERVED		BIT(0xf)
#define DMA_ENGINE_HANDLE_IRQ		BIT(0x10)

/* Defines for DMA Capabilities */
#define DMA_HAS_TRANSPARENT_CAPS	(0x1 << 18)
#define DMA_HAS_CONSTANT_FILL_CAPS	(0x1 << 19)
#define DMA_HAS_DESCRIPTOR_CAPS		(0x3 << 20)

enum omap_reg_offsets {

GCR,		GSCR,		GRST1,		HW_ID,
PCH2_ID,	PCH0_ID,	PCH1_ID,	PCHG_ID,
PCHD_ID,	CAPS_0,		CAPS_1,		CAPS_2,
CAPS_3,		CAPS_4,		PCH2_SR,	PCH0_SR,
PCH1_SR,	PCHD_SR,	REVISION,	IRQSTATUS_L0,
IRQSTATUS_L1,	IRQSTATUS_L2,	IRQSTATUS_L3,	IRQENABLE_L0,
IRQENABLE_L1,	IRQENABLE_L2,	IRQENABLE_L3,	SYSSTATUS,
OCP_SYSCONFIG,

/* omap1+ specific */
CPC, CCR2, LCH_CTRL,

/* Common registers for all omap's */
CSDP,		CCR,		CICR,		CSR,
CEN,		CFN,		CSFI,		CSEI,
CSAC,		CDAC,		CDEI,
CDFI,		CLNK_CTRL,

/* Channel specific registers */
CSSA,		CDSA,		COLOR,
CCEN,		CCFN,

/* omap3630 and omap4 specific */
CDP,		CNDP,		CCDN,

};

enum omap_dma_burst_mode {
	OMAP_DMA_DATA_BURST_DIS = 0,
	OMAP_DMA_DATA_BURST_4,
	OMAP_DMA_DATA_BURST_8,
	OMAP_DMA_DATA_BURST_16,
};

enum end_type {
	OMAP_DMA_LITTLE_ENDIAN = 0,
	OMAP_DMA_BIG_ENDIAN
};

enum omap_dma_color_mode {
	OMAP_DMA_COLOR_DIS = 0,
	OMAP_DMA_CONSTANT_FILL,
	OMAP_DMA_TRANSPARENT_COPY
};

enum omap_dma_write_mode {
	OMAP_DMA_WRITE_NON_POSTED = 0,
	OMAP_DMA_WRITE_POSTED,
	OMAP_DMA_WRITE_LAST_NON_POSTED
};

enum omap_dma_channel_mode {
	OMAP_DMA_LCH_2D = 0,
	OMAP_DMA_LCH_G,
	OMAP_DMA_LCH_P,
	OMAP_DMA_LCH_PD
};

struct omap_dma_channel_params {
	int data_type;		/* data type 8,16,32 */
	int elem_count;		/* number of elements in a frame */
	int frame_count;	/* number of frames in a element */

	int src_port;		/* Only on OMAP1 REVISIT: Is this needed? */
	int src_amode;		/* constant, post increment, indexed,
					double indexed */
	unsigned long src_start;	/* source address : physical */
	int src_ei;		/* source element index */
	int src_fi;		/* source frame index */

	int dst_port;		/* Only on OMAP1 REVISIT: Is this needed? */
	int dst_amode;		/* constant, post increment, indexed,
					double indexed */
	unsigned long dst_start;	/* source address : physical */
	int dst_ei;		/* source element index */
	int dst_fi;		/* source frame index */

	int trigger;		/* trigger attached if the channel is
					synchronized */
	int sync_mode;		/* sycn on element, frame , block or packet */
	int src_or_dst_synch;	/* source synch(1) or destination synch(0) */

	int ie;			/* interrupt enabled */

	unsigned char read_prio;/* read priority */
	unsigned char write_prio;/* write priority */

#ifndef CONFIG_ARCH_OMAP1
	enum omap_dma_burst_mode burst_mode; /* Burst mode 4/8/16 words */
#endif
};

struct omap_dma_lch {
	int next_lch;
	int dev_id;
	u16 saved_csr;
	u16 enabled_irqs;
	const char *dev_name;
	void (*callback)(int lch, u16 ch_status, void *data);
	void *data;
	long flags;
	/* required for Dynamic chaining */
	int prev_linked_ch;
	int next_linked_ch;
	int state;
	int chain_id;
	int status;
};

struct omap_dma_dev_attr {
	u32 dev_caps;
	u16 lch_count;
	u16 chan_count;
};

enum {
	OMAP_DMA_REG_NONE,
	OMAP_DMA_REG_16BIT,
	OMAP_DMA_REG_2X16BIT,
	OMAP_DMA_REG_32BIT,
};

struct omap_dma_reg {
	u16	offset;
	u8	stride;
	u8	type;
};

/* System DMA platform data structure */
struct omap_system_dma_plat_info {
	const struct omap_dma_reg *reg_map;
	unsigned channel_stride;
	struct omap_dma_dev_attr *dma_attr;
	u32 errata;
	void (*show_dma_caps)(void);
	void (*clear_lch_regs)(int lch);
	void (*clear_dma)(int lch);
	void (*dma_write)(u32 val, int reg, int lch);
	u32 (*dma_read)(int reg, int lch);
};

#ifdef CONFIG_ARCH_OMAP2PLUS
#define dma_omap2plus()	1
#else
#define dma_omap2plus()	0
#endif
#define dma_omap1()	(!dma_omap2plus())
#define __dma_omap15xx(d) (dma_omap1() && (d)->dev_caps & ENABLE_1510_MODE)
#define __dma_omap16xx(d) (dma_omap1() && (d)->dev_caps & ENABLE_16XX_MODE)
#define dma_omap15xx()	__dma_omap15xx(d)
#define dma_omap16xx()	__dma_omap16xx(d)

extern struct omap_system_dma_plat_info *omap_get_plat_info(void);

extern void omap_set_dma_priority(int lch, int dst_port, int priority);
extern int omap_request_dma(int dev_id, const char *dev_name,
			void (*callback)(int lch, u16 ch_status, void *data),
			void *data, int *dma_ch);
extern void omap_enable_dma_irq(int ch, u16 irq_bits);
extern void omap_disable_dma_irq(int ch, u16 irq_bits);
extern void omap_free_dma(int ch);
extern void omap_start_dma(int lch);
extern void omap_stop_dma(int lch);
extern void omap_set_dma_transfer_params(int lch, int data_type,
					 int elem_count, int frame_count,
					 int sync_mode,
					 int dma_trigger, int src_or_dst_synch);
extern void omap_set_dma_write_mode(int lch, enum omap_dma_write_mode mode);
extern void omap_set_dma_channel_mode(int lch, enum omap_dma_channel_mode mode);

extern void omap_set_dma_src_params(int lch, int src_port, int src_amode,
				    unsigned long src_start,
				    int src_ei, int src_fi);
extern void omap_set_dma_src_data_pack(int lch, int enable);
extern void omap_set_dma_src_burst_mode(int lch,
					enum omap_dma_burst_mode burst_mode);

extern void omap_set_dma_dest_params(int lch, int dest_port, int dest_amode,
				     unsigned long dest_start,
				     int dst_ei, int dst_fi);
extern void omap_set_dma_dest_data_pack(int lch, int enable);
extern void omap_set_dma_dest_burst_mode(int lch,
					 enum omap_dma_burst_mode burst_mode);

extern void omap_set_dma_params(int lch,
				struct omap_dma_channel_params *params);

extern void omap_dma_link_lch(int lch_head, int lch_queue);

extern int omap_set_dma_callback(int lch,
			void (*callback)(int lch, u16 ch_status, void *data),
			void *data);
extern dma_addr_t omap_get_dma_src_pos(int lch);
extern dma_addr_t omap_get_dma_dst_pos(int lch);
extern int omap_get_dma_active_status(int lch);
extern int omap_dma_running(void);
extern void omap_dma_set_global_params(int arb_rate, int max_fifo_depth,
				       int tparams);
void omap_dma_global_context_save(void);
void omap_dma_global_context_restore(void);

#if defined(CONFIG_ARCH_OMAP1) && IS_ENABLED(CONFIG_FB_OMAP)
#include <mach/lcd_dma.h>
#else
static inline int omap_lcd_dma_running(void)
{
	return 0;
}
#endif

#endif /* __LINUX_OMAP_DMA_H */
