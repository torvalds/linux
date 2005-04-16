/*
 * linux/include/asm-arm/arch-pxa/udc.h
 *
 * This supports machine-specific differences in how the PXA2xx
 * USB Device Controller (UDC) is wired.
 *
 * It is set in linux/arch/arm/mach-pxa/<machine>.c and used in
 * the probe routine of linux/drivers/usb/gadget/pxa2xx_udc.c
 */
struct pxa2xx_udc_mach_info {
        int  (*udc_is_connected)(void);		/* do we see host? */
        void (*udc_command)(int cmd);
#define	PXA2XX_UDC_CMD_CONNECT		0	/* let host see us */
#define	PXA2XX_UDC_CMD_DISCONNECT	1	/* so host won't see us */
};

extern void pxa_set_udc_info(struct pxa2xx_udc_mach_info *info);

