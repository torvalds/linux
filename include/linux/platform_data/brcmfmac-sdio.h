/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LINUX_BRCMFMAC_PLATFORM_H
#define _LINUX_BRCMFMAC_PLATFORM_H

/*
 * Platform specific driver functions and data. Through the platform specific
 * device data functions can be provided to help the brcmfmac driver to
 * operate with the device in combination with the used platform.
 *
 * Use the platform data in the following (similar) way:
 *
 *
#include <brcmfmac_platform.h>


static void brcmfmac_power_on(void)
{
}

static void brcmfmac_power_off(void)
{
}

static void brcmfmac_reset(void)
{
}

static struct brcmfmac_sdio_platform_data brcmfmac_sdio_pdata = {
	.power_on		= brcmfmac_power_on,
	.power_off		= brcmfmac_power_off,
	.reset			= brcmfmac_reset
};

static struct platform_device brcmfmac_device = {
	.name			= BRCMFMAC_SDIO_PDATA_NAME,
	.id			= PLATFORM_DEVID_NONE,
	.dev.platform_data	= &brcmfmac_sdio_pdata
};

void __init brcmfmac_init_pdata(void)
{
	brcmfmac_sdio_pdata.oob_irq_supported = true;
	brcmfmac_sdio_pdata.oob_irq_nr = gpio_to_irq(GPIO_BRCMF_SDIO_OOB);
	brcmfmac_sdio_pdata.oob_irq_flags = IORESOURCE_IRQ |
					    IORESOURCE_IRQ_HIGHLEVEL;
	platform_device_register(&brcmfmac_device);
}
 *
 *
 * Note: the brcmfmac can be loaded as module or be statically built-in into
 * the kernel. If built-in then do note that it uses module_init (and
 * module_exit) routines which equal device_initcall. So if you intend to
 * create a module with the platform specific data for the brcmfmac and have
 * it built-in to the kernel then use a higher initcall then device_initcall
 * (see init.h). If this is not done then brcmfmac will load without problems
 * but will not pickup the platform data.
 *
 * When the driver does not "detect" platform driver data then it will continue
 * without reporting anything and just assume there is no data needed. Which is
 * probably true for most platforms.
 *
 * Explanation of the platform_data fields:
 *
 * drive_strength: is the preferred drive_strength to be used for the SDIO
 * pins. If 0 then a default value will be used. This is the target drive
 * strength, the exact drive strength which will be used depends on the
 * capabilities of the device.
 *
 * oob_irq_supported: does the board have support for OOB interrupts. SDIO
 * in-band interrupts are relatively slow and for having less overhead on
 * interrupt processing an out of band interrupt can be used. If the HW
 * supports this then enable this by setting this field to true and configure
 * the oob related fields.
 *
 * oob_irq_nr, oob_irq_flags: the OOB interrupt information. The values are
 * used for registering the irq using request_irq function.
 *
 * broken_sg_support: flag for broken sg list support of SDIO host controller.
 * Set this to true if the SDIO host controller has higher align requirement
 * than 32 bytes for each scatterlist item.
 *
 * power_on: This function is called by the brcmfmac when the module gets
 * loaded. This can be particularly useful for low power devices. The platform
 * spcific routine may for example decide to power up the complete device.
 * If there is no use-case for this function then provide NULL.
 *
 * power_off: This function is called by the brcmfmac when the module gets
 * unloaded. At this point the device can be powered down or otherwise be reset.
 * So if an actual power_off is not supported but reset is then reset the device
 * when this function gets called. This can be particularly useful for low power
 * devices. If there is no use-case for this function (either power-down or
 * reset) then provide NULL.
 *
 * reset: This function can get called if the device communication broke down.
 * This functionality is particularly useful in case of SDIO type devices. It is
 * possible to reset a dongle via sdio data interface, but it requires that
 * this is fully functional. This function is chip/module specific and this
 * function should return only after the complete reset has completed.
 */

#define BRCMFMAC_SDIO_PDATA_NAME	"brcmfmac_sdio"

struct brcmfmac_sdio_platform_data {
	unsigned int drive_strength;
	bool oob_irq_supported;
	unsigned int oob_irq_nr;
	unsigned long oob_irq_flags;
	bool broken_sg_support;
	void (*power_on)(void);
	void (*power_off)(void);
	void (*reset)(void);
};

#endif /* _LINUX_BRCMFMAC_PLATFORM_H */
