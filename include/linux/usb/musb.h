/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is used to for host and peripheral modes of the driver for
 * Inventra (Multidrop) Highspeed Dual-Role Controllers:  (M)HDRC.
 *
 * Board initialization should put one of these into dev->platform_data,
 * probably on some platform_device named "musb-hdrc".  It encapsulates
 * key configuration differences between boards.
 */

#ifndef __LINUX_USB_MUSB_H
#define __LINUX_USB_MUSB_H

/* The USB role is defined by the connector used on the board, so long as
 * standards are being followed.  (Developer boards sometimes won't.)
 */
enum musb_mode {
	MUSB_UNDEFINED = 0,
	MUSB_HOST,		/* A or Mini-A connector */
	MUSB_PERIPHERAL,	/* B or Mini-B connector */
	MUSB_OTG		/* Mini-AB connector */
};

struct clk;

enum musb_fifo_style {
	FIFO_RXTX,
	FIFO_TX,
	FIFO_RX
} __attribute__ ((packed));

enum musb_buf_mode {
	BUF_SINGLE,
	BUF_DOUBLE
} __attribute__ ((packed));

struct musb_fifo_cfg {
	u8			hw_ep_num;
	enum musb_fifo_style	style;
	enum musb_buf_mode	mode;
	u16			maxpacket;
};

#define MUSB_EP_FIFO(ep, st, m, pkt)		\
{						\
	.hw_ep_num	= ep,			\
	.style		= st,			\
	.mode		= m,			\
	.maxpacket	= pkt,			\
}

#define MUSB_EP_FIFO_SINGLE(ep, st, pkt)	\
	MUSB_EP_FIFO(ep, st, BUF_SINGLE, pkt)

#define MUSB_EP_FIFO_DOUBLE(ep, st, pkt)	\
	MUSB_EP_FIFO(ep, st, BUF_DOUBLE, pkt)

struct musb_hdrc_eps_bits {
	const char	name[16];
	u8		bits;
};

struct musb_hdrc_config {
	struct musb_fifo_cfg	*fifo_cfg;	/* board fifo configuration */
	unsigned		fifo_cfg_size;	/* size of the fifo configuration */

	/* MUSB configuration-specific details */
	unsigned	multipoint:1;	/* multipoint device */
	unsigned	dyn_fifo:1 __deprecated; /* supports dynamic fifo sizing */
	unsigned	soft_con:1 __deprecated; /* soft connect required */
	unsigned	utm_16:1 __deprecated; /* utm data witdh is 16 bits */
	unsigned	big_endian:1;	/* true if CPU uses big-endian */
	unsigned	mult_bulk_tx:1;	/* Tx ep required for multbulk pkts */
	unsigned	mult_bulk_rx:1;	/* Rx ep required for multbulk pkts */
	unsigned	high_iso_tx:1;	/* Tx ep required for HB iso */
	unsigned	high_iso_rx:1;	/* Rx ep required for HD iso */
	unsigned	dma:1 __deprecated; /* supports DMA */
	unsigned	vendor_req:1 __deprecated; /* vendor registers required */

	/* need to explicitly de-assert the port reset after resume? */
	unsigned	host_port_deassert_reset_at_resume:1;

	u8		num_eps;	/* number of endpoints _with_ ep0 */
	u8		dma_channels __deprecated; /* number of dma channels */
	u8		dyn_fifo_size;	/* dynamic size in bytes */
	u8		vendor_ctrl __deprecated; /* vendor control reg width */
	u8		vendor_stat __deprecated; /* vendor status reg witdh */
	u8		dma_req_chan __deprecated; /* bitmask for required dma channels */
	u8		ram_bits;	/* ram address size */

	struct musb_hdrc_eps_bits *eps_bits __deprecated;
#ifdef CONFIG_BLACKFIN
	/* A GPIO controlling VRSEL in Blackfin */
	unsigned int	gpio_vrsel;
	unsigned int	gpio_vrsel_active;
	/* musb CLKIN in Blackfin in MHZ */
	unsigned char   clkin;
#endif
	u32		maximum_speed;
};

struct musb_hdrc_platform_data {
	/* MUSB_HOST, MUSB_PERIPHERAL, or MUSB_OTG */
	u8		mode;

	/* for clk_get() */
	const char	*clock;

	/* (HOST or OTG) switch VBUS on/off */
	int		(*set_vbus)(struct device *dev, int is_on);

	/* (HOST or OTG) mA/2 power supplied on (default = 8mA) */
	u8		power;

	/* (PERIPHERAL) mA/2 max power consumed (default = 100mA) */
	u8		min_power;

	/* (HOST or OTG) msec/2 after VBUS on till power good */
	u8		potpgt;

	/* (HOST or OTG) program PHY for external Vbus */
	unsigned	extvbus:1;

	/* Power the device on or off */
	int		(*set_power)(int state);

	/* MUSB configuration-specific details */
	const struct musb_hdrc_config *config;

	/* Architecture specific board data	*/
	void		*board_data;

	/* Platform specific struct musb_ops pointer */
	const void	*platform_ops;
};

enum musb_vbus_id_status {
	MUSB_UNKNOWN = 0,
	MUSB_ID_GROUND,
	MUSB_ID_FLOAT,
	MUSB_VBUS_VALID,
	MUSB_VBUS_OFF,
};

#if IS_ENABLED(CONFIG_USB_MUSB_HDRC)
int musb_mailbox(enum musb_vbus_id_status status);
#else
static inline int musb_mailbox(enum musb_vbus_id_status status)
{
	return 0;
}
#endif

/* TUSB 6010 support */

#define	TUSB6010_OSCCLK_60	16667	/* psec/clk @ 60.0 MHz */
#define	TUSB6010_REFCLK_24	41667	/* psec/clk @ 24.0 MHz XI */
#define	TUSB6010_REFCLK_19	52083	/* psec/clk @ 19.2 MHz CLKIN */

#ifdef	CONFIG_ARCH_OMAP2

extern int __init tusb6010_setup_interface(
		struct musb_hdrc_platform_data *data,
		unsigned ps_refclk, unsigned waitpin,
		unsigned async_cs, unsigned sync_cs,
		unsigned irq, unsigned dmachan);

extern int tusb6010_platform_retime(unsigned is_refclk);

#endif	/* OMAP2 */

#endif /* __LINUX_USB_MUSB_H */
