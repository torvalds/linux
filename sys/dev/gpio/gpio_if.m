#-
# Copyright (c) 2009 Oleksandr Tymoshenko <gonzo@freebsd.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#include <sys/bus.h>
#include <sys/gpio.h>

INTERFACE gpio;

CODE {
	static device_t
	gpio_default_get_bus(void)
	{

		return (NULL);
	}

	static int
	gpio_default_nosupport(void)
	{

		return (EOPNOTSUPP);
	}

	static int
	gpio_default_map_gpios(device_t bus, phandle_t dev,
	    phandle_t gparent, int gcells, pcell_t *gpios, uint32_t *pin,
	    uint32_t *flags)
	{
		/* Propagate up the bus hierarchy until someone handles it. */  
		if (device_get_parent(bus) != NULL)
			return (GPIO_MAP_GPIOS(device_get_parent(bus), dev,
			    gparent, gcells, gpios, pin, flags));

		/* If that fails, then assume the FreeBSD defaults. */
		*pin = gpios[0];
		if (gcells == 2 || gcells == 3)
			*flags = gpios[gcells - 1];

		return (0);
	}
};

HEADER {
	#include <dev/ofw/openfirm.h>
};

#
# Return the gpiobus device reference
#
METHOD device_t get_bus {
	device_t dev;
} DEFAULT gpio_default_get_bus;

#
# Get maximum pin number
#
METHOD int pin_max {
	device_t dev;
	int *maxpin;
};

#
# Set value of pin specifed by pin_num 
#
METHOD int pin_set {
	device_t dev;
	uint32_t pin_num;
	uint32_t pin_value;
};

#
# Get value of pin specifed by pin_num 
#
METHOD int pin_get {
	device_t dev;
	uint32_t pin_num;
	uint32_t *pin_value;
};

#
# Toggle value of pin specifed by pin_num 
#
METHOD int pin_toggle {
	device_t dev;
	uint32_t pin_num;
};

#
# Get pin capabilities
#
METHOD int pin_getcaps {
	device_t dev;
	uint32_t pin_num;
	uint32_t *caps;
};

#
# Get pin flags
#
METHOD int pin_getflags {
	device_t dev;
	uint32_t pin_num;
	uint32_t *flags;
};

#
# Get pin name
#
METHOD int pin_getname {
	device_t dev;
	uint32_t pin_num;
	char *name;
};

#
# Set current configuration and capabilities
#
METHOD int pin_setflags {
	device_t dev;
	uint32_t pin_num;
	uint32_t flags;
};

#
# Allow the GPIO controller to map the gpio-specifier on its own.
#
METHOD int map_gpios {
        device_t bus;
        phandle_t dev;
        phandle_t gparent;
        int gcells;
        pcell_t *gpios;
        uint32_t *pin;
        uint32_t *flags;
} DEFAULT gpio_default_map_gpios;

#
# Simultaneously read and/or change up to 32 adjacent pins.
# If the device cannot change the pins simultaneously, returns EOPNOTSUPP.
#
# More details about using this interface can be found in sys/gpio.h
#
METHOD int pin_access_32 {
	device_t dev;
	uint32_t first_pin;
	uint32_t clear_pins;
	uint32_t change_pins;
	uint32_t *orig_pins;
} DEFAULT gpio_default_nosupport;

#
# Simultaneously configure up to 32 adjacent pins.
# This is intended to change the configuration of all the pins simultaneously,
# but unlike pin_access_32, this will not fail if the hardware can't do so.
#
# More details about using this interface can be found in sys/gpio.h
#
METHOD int pin_config_32 {
	device_t dev;
	uint32_t first_pin;
	uint32_t num_pins;
	uint32_t *pin_flags;
} DEFAULT gpio_default_nosupport;
