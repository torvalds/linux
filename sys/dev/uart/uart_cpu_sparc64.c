/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, 2004 Marcel Moolenaar
 * Copyright (c) 2004 - 2006 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/bus_private.h>

#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>

bus_space_tag_t uart_bus_space_io;
bus_space_tag_t uart_bus_space_mem;

static struct bus_space_tag bst_store[3];

/*
 * Determine which channel of a SCC a device referenced by a full device
 * path or as an alias is (in the latter case we try to look up the device
 * path via the /aliases node).
 * Only the device paths of devices which are used for TTYs really allow
 * to do this as they look like these (taken from /aliases nodes):
 * ttya:  '/central/fhc/zs@0,902000:a'
 * ttyc:  '/pci@1f,0/pci@1,1/ebus@1/se@14,400000:a'
 * Additionally, for device paths of SCCs which are connected to a RSC
 * (Remote System Control) device we can hardcode the appropriate channel.
 * Such device paths look like these:
 * rsc:   '/pci@1f,4000/ebus@1/se@14,200000:ssp'
 * ttyc:  '/pci@1f,4000/ebus@1/se@14,200000:ssp'
 */
static int
uart_cpu_channel(char *dev)
{
	char alias[64];
	phandle_t aliases;
	int len;
	const char *p;

	strcpy(alias, dev);
	if ((aliases = OF_finddevice("/aliases")) != -1)
		(void)OF_getprop(aliases, dev, alias, sizeof(alias));
	len = strlen(alias);
	if ((p = strrchr(alias, ':')) == NULL)
		return (0);
	p++;
	if (p - alias == len - 1 && (*p == 'a' || *p == 'b'))
		return (*p - 'a' + 1);
	if (strcmp(p, "ssp") == 0)
		return (1);
	return (0);
}

int
uart_cpu_eqres(struct uart_bas *b1, struct uart_bas *b2)
{

	return ((b1->bsh == b2->bsh) ? 1 : 0);
}

/*
 * Get the package handle of the UART that is selected as the console, if
 * the console is an UART of course. Note that we enforce that both input
 * and output are selected.
 * Note that the currently active console (i.e. /chosen/stdout and
 * /chosen/stdin) may not be the same as the device selected in the
 * environment (ie /options/output-device and /options/input-device) because
 * keyboard and screen were selected but the keyboard was unplugged or the
 * user has changed the environment. In the latter case I would assume that
 * the user expects that FreeBSD uses the new console setting.
 * For weirder configurations, use ofw_console(4).
 */
static phandle_t
uart_cpu_getdev_console(phandle_t options, char *dev, size_t devsz)
{
	char buf[sizeof("serial")];
	ihandle_t inst;
	phandle_t chosen, input, output;

	if (OF_getprop(options, "input-device", dev, devsz) == -1)
		return (-1);
	input = OF_finddevice(dev);
	if (OF_getprop(options, "output-device", dev, devsz) == -1)
		return (-1);
	output = OF_finddevice(dev);
	if (input == -1 || output == -1 ||
	    OF_getproplen(input, "keyboard") >= 0) {
		if ((chosen = OF_finddevice("/chosen")) == -1)
			return (-1);
		if (OF_getprop(chosen, "stdin", &inst, sizeof(inst)) == -1)
			return (-1);
		if ((input = OF_instance_to_package(inst)) == -1)
			return (-1);
		if (OF_getprop(chosen, "stdout", &inst, sizeof(inst)) == -1)
			return (-1);
		if ((output = OF_instance_to_package(inst)) == -1)
			return (-1);
		snprintf(dev, devsz, "ttya");
	}
	if (input != output)
		return (-1);
	if (OF_getprop(input, "device_type", buf, sizeof(buf)) == -1)
		return (-1);
	if (strcmp(buf, "serial") != 0)
		return (-1);
	/* For a Serengeti console device point to the bootbus controller. */
	if (OF_getprop(input, "name", buf, sizeof(buf)) > 0 &&
	    !strcmp(buf, "sgcn")) {
		if ((chosen = OF_finddevice("/chosen")) == -1)
			return (-1);
		if (OF_getprop(chosen, "iosram", &input, sizeof(input)) == -1)
			return (-1);
	}
	return (input);
}

/*
 * Get the package handle of the UART that's selected as the debug port.
 * Since there's no place for this in the OF, we use the kernel environment
 * variable "hw.uart.dbgport". Note however that the variable is not a
 * list of attributes. It's single device name or alias, as known by
 * the OF.
 */
static phandle_t
uart_cpu_getdev_dbgport(char *dev, size_t devsz)
{
	char buf[sizeof("serial")];
	phandle_t input;

	if (!getenv_string("hw.uart.dbgport", dev, devsz))
		return (-1);
	if ((input = OF_finddevice(dev)) == -1)
		return (-1);
	if (OF_getprop(input, "device_type", buf, sizeof(buf)) == -1)
		return (-1);
	if (strcmp(buf, "serial") != 0)
		return (-1);
	return (input);
}

/*
 * Get the package handle of the UART that is selected as the keyboard port,
 * if it's actually used to connect the keyboard according to the OF. I.e.
 * this will return the UART used to connect the keyboard regardless whether
 * it's stdin or not, however not in case the user or the OF gave preference
 * to e.g. a PS/2 keyboard by setting /aliases/keyboard accordingly.
 */
static phandle_t
uart_cpu_getdev_keyboard(char *dev, size_t devsz)
{
	char buf[sizeof("serial")];
	phandle_t input;

	if ((input = OF_finddevice("keyboard")) == -1)
		return (-1);
	if (OF_getprop(input, "device_type", buf, sizeof(buf)) == -1)
		return (-1);
	if (strcmp(buf, "serial") != 0)
		return (-1);
	if (OF_getprop(input, "name", dev, devsz) == -1)
		return (-1);
	/*
	 * So far this also matched PS/2 keyboard nodes so make sure it's
	 * one of the SCCs/UARTs known to be used to connect keyboards.
	 */
	if (strcmp(dev, "su") && strcmp(dev, "su_pnp") && strcmp(dev, "zs"))
		return (-1);
	return (input);
}

int
uart_cpu_getdev(int devtype, struct uart_devinfo *di)
{
	char buf[32], compat[32], dev[64];
	struct uart_class *class;
	phandle_t input, options;
	bus_addr_t addr;
	int baud, bits, error, range, space, stop;
	char flag, par;

	if ((options = OF_finddevice("/options")) == -1)
		return (ENXIO);
	switch (devtype) {
	case UART_DEV_CONSOLE:
		input = uart_cpu_getdev_console(options, dev, sizeof(dev));
		break;
	case UART_DEV_DBGPORT:
		input = uart_cpu_getdev_dbgport(dev, sizeof(dev));
		break;
	case UART_DEV_KEYBOARD:
		input = uart_cpu_getdev_keyboard(dev, sizeof(dev));
		break;
	default:
		input = -1;
		break;
	}
	if (input == -1)
		return (ENXIO);
	error = OF_decode_addr(input, 0, &space, &addr);
	if (error)
		return (error);

	/* Get the device class. */
	if (OF_getprop(input, "name", buf, sizeof(buf)) == -1)
		return (ENXIO);
	if (OF_getprop(input, "compatible", compat, sizeof(compat)) == -1)
		compat[0] = '\0';
	di->bas.regshft = 0;
	di->bas.rclk = 0;
	class = NULL;
	if (!strcmp(buf, "se") || !strcmp(buf, "FJSV,se") ||
	    !strcmp(compat, "sab82532")) {
		class = &uart_sab82532_class;
		/* SAB82532 are only known to be used for TTYs. */
		if ((di->bas.chan = uart_cpu_channel(dev)) == 0)
			return (ENXIO);
		addr += uart_getrange(class) * (di->bas.chan - 1);
	} else if (!strcmp(buf, "zs")) {
		class = &uart_z8530_class;
		if ((di->bas.chan = uart_cpu_channel(dev)) == 0) {
			/*
			 * There's no way to determine from OF which
			 * channel has the keyboard. Should always be
			 * on channel 1 however.
			 */
			if (devtype == UART_DEV_KEYBOARD)
				di->bas.chan = 1;
			else
				return (ENXIO);
		}
		di->bas.regshft = 1;
		range = uart_getrange(class) << di->bas.regshft;
		addr += range - range * (di->bas.chan - 1);
	} else if (!strcmp(buf, "lom-console") || !strcmp(buf, "su") ||
	    !strcmp(buf, "su_pnp") || !strcmp(compat, "rsc-console") ||
	    !strcmp(compat, "su") || !strcmp(compat, "su16550") ||
	    !strcmp(compat, "su16552")) {
		class = &uart_ns8250_class;
		di->bas.chan = 0;
	} else if (!strcmp(compat, "sgsbbc")) {
		class = &uart_sbbc_class;
		di->bas.chan = 0;
	}
	if (class == NULL)
		return (ENXIO);

	/* Fill in the device info. */
	di->ops = uart_getops(class);
	di->bas.bst = &bst_store[devtype];
	di->bas.bsh = sparc64_fake_bustag(space, addr, di->bas.bst);

	/* Get the line settings. */
	if (devtype == UART_DEV_KEYBOARD)
		di->baudrate = 1200;
	else if (!strcmp(compat, "rsc-console"))
		di->baudrate = 115200;
	else
		di->baudrate = 9600;
	di->databits = 8;
	di->stopbits = 1;
	di->parity = UART_PARITY_NONE;
	snprintf(buf, sizeof(buf), "%s-mode", dev);
	if (OF_getprop(options, buf, buf, sizeof(buf)) == -1 &&
	    OF_getprop(input, "ssp-console-modes", buf, sizeof(buf)) == -1)
		return (0);
	if (sscanf(buf, "%d,%d,%c,%d,%c", &baud, &bits, &par, &stop, &flag)
	    != 5)
		return (0);
	di->baudrate = baud;
	di->databits = bits;
	di->stopbits = stop;
	di->parity = (par == 'n') ? UART_PARITY_NONE :
	    (par == 'o') ? UART_PARITY_ODD : UART_PARITY_EVEN;
	return (0);
}
