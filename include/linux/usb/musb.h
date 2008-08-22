/*
 * This is used to for host and peripheral modes of the driver for
 * Inventra (Multidrop) Highspeed Dual-Role Controllers:  (M)HDRC.
 *
 * Board initialization should put one of these into dev->platform_data,
 * probably on some platform_device named "musb_hdrc".  It encapsulates
 * key configuration differences between boards.
 */

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

struct musb_hdrc_eps_bits {
	const char	name[16];
	u8		bits;
};

struct musb_hdrc_config {
	/* MUSB configuration-specific details */
	unsigned	multipoint:1;	/* multipoint device */
	unsigned	dyn_fifo:1;	/* supports dynamic fifo sizing */
	unsigned	soft_con:1;	/* soft connect required */
	unsigned	utm_16:1;	/* utm data witdh is 16 bits */
	unsigned	big_endian:1;	/* true if CPU uses big-endian */
	unsigned	mult_bulk_tx:1;	/* Tx ep required for multbulk pkts */
	unsigned	mult_bulk_rx:1;	/* Rx ep required for multbulk pkts */
	unsigned	high_iso_tx:1;	/* Tx ep required for HB iso */
	unsigned	high_iso_rx:1;	/* Rx ep required for HD iso */
	unsigned	dma:1;		/* supports DMA */
	unsigned	vendor_req:1;	/* vendor registers required */

	u8		num_eps;	/* number of endpoints _with_ ep0 */
	u8		dma_channels;	/* number of dma channels */
	u8		dyn_fifo_size;	/* dynamic size in bytes */
	u8		vendor_ctrl;	/* vendor control reg width */
	u8		vendor_stat;	/* vendor status reg witdh */
	u8		dma_req_chan;	/* bitmask for required dma channels */
	u8		ram_bits;	/* ram address size */

	struct musb_hdrc_eps_bits *eps_bits;
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

	/* Power the device on or off */
	int		(*set_power)(int state);

	/* Turn device clock on or off */
	int		(*set_clock)(struct clk *clock, int is_on);

	/* MUSB configuration-specific details */
	struct musb_hdrc_config	*config;
};


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
