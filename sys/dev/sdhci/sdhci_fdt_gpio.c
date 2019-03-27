/*-
 * Copyright (c) 2017 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Support routines usable by any SoC sdhci bridge driver that uses gpio pins
 * for card detect and write protect, and uses FDT data to describe those pins.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/mmc/bridge.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/sdhci/sdhci.h>
#include <dev/sdhci/sdhci_fdt_gpio.h>

struct sdhci_fdt_gpio {
	device_t		dev;
	struct sdhci_slot *	slot;
	gpio_pin_t		wp_pin;
	gpio_pin_t		cd_pin;
	void *			cd_ihandler;
	struct resource *	cd_ires;
	int			cd_irid;
	bool			wp_disabled;
	bool			wp_inverted;
	bool			cd_disabled;
	bool			cd_inverted;
};

/*
 * Card detect interrupt handler.
 */
static void
cd_intr(void *arg)
{
	struct sdhci_fdt_gpio *gpio = arg;

	sdhci_handle_card_present(gpio->slot, sdhci_fdt_gpio_get_present(gpio));
}

/*
 * Card detect setup.
 */
static void
cd_setup(struct sdhci_fdt_gpio *gpio, phandle_t node)
{
	int pincaps;
	device_t dev;
	const char *cd_mode_str;

	dev = gpio->dev;

	/*
	 * If the device is flagged as non-removable, set that slot option, and
	 * set a flag to make sdhci_fdt_gpio_get_present() always return true.
	 */
	if (OF_hasprop(node, "non-removable")) {
		gpio->slot->opt |= SDHCI_NON_REMOVABLE;
		gpio->cd_disabled = true;
		if (bootverbose)
			device_printf(dev, "Non-removable media\n");
		return;
	}

	/*
	 * If there is no cd-gpios property, then presumably the hardware
	 * PRESENT_STATE register and interrupts will reflect card state
	 * properly, and there's nothing more for us to do.  Our get_present()
	 * will return sdhci_generic_get_card_present() because cd_pin is NULL.
	 *
	 * If there is a property, make sure we can read the pin.
	 */
	if (gpio_pin_get_by_ofw_property(dev, node, "cd-gpios", &gpio->cd_pin))
		return;

	if (gpio_pin_getcaps(gpio->cd_pin, &pincaps) != 0 ||
	    !(pincaps & GPIO_PIN_INPUT)) {
		device_printf(dev, "Cannot read card-detect gpio pin; "
		    "setting card-always-present flag.\n");
		gpio->cd_disabled = true;
		return;
	}

	if (OF_hasprop(node, "cd-inverted"))
		gpio->cd_inverted = true;

	/*
	 * If the pin can trigger an interrupt on both rising and falling edges,
	 * we can use it to detect card presence changes.  If not, we'll request
	 * card presence polling instead of using interrupts.
	 */
	if (!(pincaps & GPIO_INTR_EDGE_BOTH)) {
		if (bootverbose)
			device_printf(dev, "Cannot configure "
			    "GPIO_INTR_EDGE_BOTH for card detect\n");
		goto without_interrupts;
	}

	/*
	 * Create an interrupt resource from the pin and set up the interrupt.
	 */
	if ((gpio->cd_ires = gpio_alloc_intr_resource(dev, &gpio->cd_irid,
	    RF_ACTIVE, gpio->cd_pin, GPIO_INTR_EDGE_BOTH)) == NULL) {
		if (bootverbose)
			device_printf(dev, "Cannot allocate an IRQ for card "
			    "detect GPIO\n");
		goto without_interrupts;
	}

	if (bus_setup_intr(dev, gpio->cd_ires, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, cd_intr, gpio, &gpio->cd_ihandler) != 0) {
		device_printf(dev, "Unable to setup card-detect irq handler\n");
		gpio->cd_ihandler = NULL;
		goto without_interrupts;
	}

without_interrupts:

	/*
	 * If we have a readable gpio pin, but didn't successfully configure
	 * gpio interrupts, ask the sdhci driver to poll from a callout.
	 */
	if (gpio->cd_ihandler == NULL) {
		cd_mode_str = "polling";
		gpio->slot->quirks |= SDHCI_QUIRK_POLL_CARD_PRESENT;
	} else {
		cd_mode_str = "interrupts";
	}

	if (bootverbose) {
		device_printf(dev, "Card presence detect on %s pin %u, "
		    "configured for %s.\n",
		    device_get_nameunit(gpio->cd_pin->dev), gpio->cd_pin->pin,
		    cd_mode_str);
	}
}

/*
 * Write protect setup.
 */
static void
wp_setup(struct sdhci_fdt_gpio *gpio, phandle_t node)
{
	device_t dev;

	dev = gpio->dev;

	if (OF_hasprop(node, "wp-disable")) {
		gpio->wp_disabled = true;
		if (bootverbose)
			device_printf(dev, "Write protect disabled\n");
		return;
	}

	if (gpio_pin_get_by_ofw_property(dev, node, "wp-gpios", &gpio->wp_pin))
		return;

	if (OF_hasprop(node, "wp-inverted"))
		gpio->wp_inverted = true;

	if (bootverbose)
		device_printf(dev, "Write protect switch on %s pin %u\n",
		    device_get_nameunit(gpio->wp_pin->dev), gpio->wp_pin->pin);
}

struct sdhci_fdt_gpio *
sdhci_fdt_gpio_setup(device_t dev, struct sdhci_slot *slot)
{
	phandle_t node;
	struct sdhci_fdt_gpio *gpio;

	gpio = malloc(sizeof(*gpio), M_DEVBUF, M_ZERO | M_WAITOK);
	gpio->dev  = dev;
	gpio->slot = slot;

	node = ofw_bus_get_node(dev);

	wp_setup(gpio, node);
	cd_setup(gpio, node);

	return (gpio);
}

void
sdhci_fdt_gpio_teardown(struct sdhci_fdt_gpio *gpio)
{

	if (gpio == NULL)
		return;

	if (gpio->cd_ihandler != NULL)
		bus_teardown_intr(gpio->dev, gpio->cd_ires, gpio->cd_ihandler);
	if (gpio->wp_pin != NULL)
		gpio_pin_release(gpio->wp_pin);
	if (gpio->cd_pin != NULL)
		gpio_pin_release(gpio->cd_pin);
	if (gpio->cd_ires != NULL)
		bus_release_resource(gpio->dev, SYS_RES_IRQ, 0, gpio->cd_ires);

	free(gpio, M_DEVBUF);
}

bool
sdhci_fdt_gpio_get_present(struct sdhci_fdt_gpio *gpio)
{
	bool pinstate;

	if (gpio->cd_disabled)
		return (true);

	if (gpio->cd_pin == NULL)
		return (sdhci_generic_get_card_present(gpio->slot->bus,
		    gpio->slot));

	gpio_pin_is_active(gpio->cd_pin, &pinstate);

	return (pinstate ^ gpio->cd_inverted);
}

int
sdhci_fdt_gpio_get_readonly(struct sdhci_fdt_gpio *gpio)
{
	bool pinstate;

	if (gpio->wp_disabled)
		return (false);

	if (gpio->wp_pin == NULL)
		return (sdhci_generic_get_ro(gpio->slot->bus, gpio->slot->dev));

	gpio_pin_is_active(gpio->wp_pin, &pinstate);

	return (pinstate ^ gpio->wp_inverted);
}
