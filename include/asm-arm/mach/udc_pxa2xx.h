/*
 * linux/include/asm-arm/mach/udc_pxa2xx.h
 *
 * This supports machine-specific differences in how the PXA2xx
 * USB Device Controller (UDC) is wired.
 *
 * It is set in linux/arch/arm/mach-pxa/<machine>.c or in
 * linux/arch/mach-ixp4xx/<machine>.c and used in
 * the probe routine of linux/drivers/usb/gadget/pxa2xx_udc.c
 */

struct pxa2xx_udc_mach_info {
        int  (*udc_is_connected)(void);		/* do we see host? */
        void (*udc_command)(int cmd);
#define	PXA2XX_UDC_CMD_CONNECT		0	/* let host see us */
#define	PXA2XX_UDC_CMD_DISCONNECT	1	/* so host won't see us */

	/* Boards following the design guidelines in the developer's manual,
	 * with on-chip GPIOs not Lubbock's wierd hardware, can have a sane
	 * VBUS IRQ and omit the methods above.  Store the GPIO number
	 * here; for GPIO 0, also mask in one of the pxa_gpio_mode() bits.
	 */
	u16	gpio_vbus;			/* high == vbus present */
	u16	gpio_pullup;			/* high == pullup activated */
};

