# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	gpio
SHLIB_MAJOR= 0

SRCS=	gpio.c
INCS=	libgpio.h
MAN+=	gpio.3

CFLAGS+= -I${.CURDIR}

MLINKS=	gpio.3 gpio_open.3 \
	gpio.3 gpio_open_device.3 \
	gpio.3 gpio_close.3 \
	gpio.3 gpio_pin_list.3 \
	gpio.3 gpio_pin_config.3 \
	gpio.3 gpio_pin_set_flags.3 \
	gpio.3 gpio_pin_set_name.3 \
	gpio.3 gpio_pin_get.3 \
	gpio.3 gpio_pin_set.3 \
	gpio.3 gpio_pin_low.3 \
	gpio.3 gpio_pin_high.3 \
	gpio.3 gpio_pin_input.3 \
	gpio.3 gpio_pin_output.3 \
	gpio.3 gpio_pin_opendrain.3 \
	gpio.3 gpio_pin_pushpull.3 \
	gpio.3 gpio_pin_tristate.3 \
	gpio.3 gpio_pin_pullup.3 \
	gpio.3 gpio_pin_pulldown.3 \
	gpio.3 gpio_pin_invin.3 \
	gpio.3 gpio_pin_invout.3 \
	gpio.3 gpio_pin_pulsate.3

.include <bsd.lib.mk>
