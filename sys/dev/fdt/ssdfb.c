/* $OpenBSD: ssdfb.c,v 1.14 2024/05/13 01:15:50 jsg Exp $ */
/*
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/stdint.h>

#include <uvm/uvm_extern.h>

#include <dev/i2c/i2cvar.h>
#include <dev/spi/spivar.h>
#include <dev/usb/udlio.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#define SSDFB_SET_LOWER_COLUMN_START_ADDRESS	0x00
#define SSDFB_SET_HIGHER_COLUMN_START_ADDRESS	0x10
#define SSDFB_SET_MEMORY_ADDRESSING_MODE	0x20
#define SSDFB_SET_COLUMN_RANGE			0x21
#define SSDFB_SET_PAGE_RANGE			0x22
#define SSDFB_SET_START_LINE			0x40
#define SSDFB_SET_CONTRAST_CONTROL		0x81
#define SSDFB_CHARGE_PUMP			0x8d
#define SSDFB_SET_COLUMN_DIRECTION_NORMAL	0xa0
#define SSDFB_SET_COLUMN_DIRECTION_REVERSE	0xa1
#define SSDFB_SET_MULTIPLEX_RATIO		0xa8
#define SSDFB_SET_COM_OUTPUT_DIRECTION_NORMAL	0xc0
#define SSDFB_SET_COM_OUTPUT_DIRECTION_REMAP	0xc8
#define SSDFB_ENTIRE_DISPLAY_ON			0xa4
#define SSDFB_SET_DISPLAY_MODE_NORMAL		0xa6
#define SSDFB_SET_DISPLAY_MODE_INVERS		0xa7
#define SSDFB_SET_DISPLAY_OFF			0xae
#define SSDFB_SET_DISPLAY_ON			0xaf
#define SSDFB_SET_DISPLAY_OFFSET		0xd3
#define SSDFB_SET_DISPLAY_CLOCK_DIVIDE_RATIO	0xd5
#define SSDFB_SET_PRE_CHARGE_PERIOD		0xd9
#define SSDFB_SET_COM_PINS_HARD_CONF		0xda
#define SSDFB_SET_VCOM_DESELECT_LEVEL		0xdb
#define SSDFB_SET_PAGE_START_ADDRESS		0xb0

#define SSDFB_I2C_COMMAND			0x00
#define SSDFB_I2C_DATA				0x40

struct ssdfb_softc {
	struct device		 sc_dev;
	int			 sc_node;
	int			 sc_width;
	int			 sc_height;
	int			 sc_pgoff;

	uint8_t			*sc_fb;
	size_t			 sc_fbsize;
	struct rasops_info	 sc_rinfo;
	struct wsdisplay_emulops sc_riops;
	int			 (*sc_ri_do_cursor)(struct rasops_info *);

	uint8_t			 sc_brightness;
	int			 sc_mode;

	uint8_t			 sc_column_range[2];
	uint8_t			 sc_page_range[2];

	/* I2C */
	i2c_tag_t		 sc_i2c_tag;
	i2c_addr_t		 sc_i2c_addr;

	/* SPI */
	spi_tag_t		 sc_spi_tag;
	struct spi_config	 sc_spi_conf;
	uint32_t		*sc_gpio;
	size_t			 sc_gpiolen;
	int			 sc_cd;

	void			 (*sc_write_command)(struct ssdfb_softc *,
				   char *, size_t);
	void			 (*sc_write_data)(struct ssdfb_softc *,
				   char *, size_t);

};

int	 ssdfb_i2c_match(struct device *, void *, void *);
void	 ssdfb_i2c_attach(struct device *, struct device *, void *);
int	 ssdfb_i2c_detach(struct device *, int);
void	 ssdfb_i2c_write_command(struct ssdfb_softc *, char *, size_t);
void	 ssdfb_i2c_write_data(struct ssdfb_softc *, char *, size_t);

int	 ssdfb_spi_match(struct device *, void *, void *);
void	 ssdfb_spi_attach(struct device *, struct device *, void *);
int	 ssdfb_spi_detach(struct device *, int);
void	 ssdfb_spi_write_command(struct ssdfb_softc *, char *, size_t);
void	 ssdfb_spi_write_data(struct ssdfb_softc *, char *, size_t);

void	 ssdfb_attach(struct ssdfb_softc *);
int	 ssdfb_detach(struct ssdfb_softc *, int);
void	 ssdfb_write_command(struct ssdfb_softc *, char *, size_t);
void	 ssdfb_write_data(struct ssdfb_softc *, char *, size_t);

void	 ssdfb_init(struct ssdfb_softc *);

void	 ssdfb_partial(struct ssdfb_softc *, uint32_t, uint32_t,
	    uint32_t, uint32_t);
void	 ssdfb_set_range(struct ssdfb_softc *, uint8_t, uint8_t,
	    uint8_t, uint8_t);

int	 ssdfb_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	 ssdfb_mmap(void *, off_t, int);
int	 ssdfb_alloc_screen(void *, const struct wsscreen_descr *, void **,
	    int *, int *, uint32_t *);
void	 ssdfb_free_screen(void *, void *);
int	 ssdfb_show_screen(void *, void *, int, void (*cb) (void *, int, int),
	    void *);
int	 ssdfb_list_font(void *, struct wsdisplay_font *);
int	 ssdfb_load_font(void *, void *, struct wsdisplay_font *);

int	 ssdfb_putchar(void *, int, int, u_int, uint32_t);
int	 ssdfb_copycols(void *, int, int, int, int);
int	 ssdfb_erasecols(void *, int, int, int, uint32_t);
int	 ssdfb_copyrows(void *, int, int, int);
int	 ssdfb_eraserows(void *, int, int, uint32_t);
int	 ssdfb_do_cursor(struct rasops_info *);

const struct cfattach ssdfb_i2c_ca = {
	sizeof(struct ssdfb_softc),
	ssdfb_i2c_match,
	ssdfb_i2c_attach,
	ssdfb_i2c_detach,
};

const struct cfattach ssdfb_spi_ca = {
	sizeof(struct ssdfb_softc),
	ssdfb_spi_match,
	ssdfb_spi_attach,
	ssdfb_spi_detach,
};

struct cfdriver ssdfb_cd = {
	NULL, "ssdfb", DV_DULL
};

struct wsscreen_descr ssdfb_std_descr = { "std" };

const struct wsscreen_descr *ssdfb_descrs[] = {
	&ssdfb_std_descr
};

const struct wsscreen_list ssdfb_screen_list = {
	nitems(ssdfb_descrs), ssdfb_descrs
};

struct wsdisplay_accessops ssdfb_accessops = {
	.ioctl = ssdfb_ioctl,
	.mmap = ssdfb_mmap,
	.alloc_screen = ssdfb_alloc_screen,
	.free_screen = ssdfb_free_screen,
	.show_screen = ssdfb_show_screen,
	.load_font = ssdfb_load_font,
	.list_font = ssdfb_list_font
};

int
ssdfb_i2c_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "solomon,ssd1306fb-i2c") == 0 ||
	    strcmp(ia->ia_name, "solomon,ssd1309fb-i2c") == 0)
		return 1;

	return 0;
}

void
ssdfb_i2c_attach(struct device *parent, struct device *self, void *aux)
{
	struct ssdfb_softc *sc = (struct ssdfb_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_i2c_tag = ia->ia_tag;
	sc->sc_i2c_addr = ia->ia_addr;
	sc->sc_node = *(int *)ia->ia_cookie;

	sc->sc_write_command = ssdfb_i2c_write_command;
	sc->sc_write_data = ssdfb_i2c_write_data;

	ssdfb_attach(sc);
}

int
ssdfb_i2c_detach(struct device *self, int flags)
{
	struct ssdfb_softc *sc = (struct ssdfb_softc *)self;
	ssdfb_detach(sc, flags);
	return 0;
}

int
ssdfb_spi_match(struct device *parent, void *match, void *aux)
{
	struct spi_attach_args *sa = aux;

	if (strcmp(sa->sa_name, "solomon,ssd1309fb-spi") == 0)
		return 1;

	return 0;
}

void
ssdfb_spi_attach(struct device *parent, struct device *self, void *aux)
{
	struct ssdfb_softc *sc = (struct ssdfb_softc *)self;
	struct spi_attach_args *sa = aux;
	ssize_t len;

	sc->sc_spi_tag = sa->sa_tag;
	sc->sc_node = *(int *)sa->sa_cookie;

	sc->sc_spi_conf.sc_bpw = 8;
	sc->sc_spi_conf.sc_freq = 1000 * 1000;
	sc->sc_spi_conf.sc_cs = OF_getpropint(sc->sc_node, "reg", 0);
	if (OF_getproplen(sc->sc_node, "spi-cpol") == 0)
		sc->sc_spi_conf.sc_flags |= SPI_CONFIG_CPOL;
	if (OF_getproplen(sc->sc_node, "spi-cpha") == 0)
		sc->sc_spi_conf.sc_flags |= SPI_CONFIG_CPHA;
	if (OF_getproplen(sc->sc_node, "spi-cs-high") == 0)
		sc->sc_spi_conf.sc_flags |= SPI_CONFIG_CS_HIGH;

	len = OF_getproplen(sc->sc_node, "cd-gpio");
	if (len <= 0)
		return;

	sc->sc_gpio = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "cd-gpio", sc->sc_gpio, len);
	sc->sc_gpiolen = len;
	gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(sc->sc_gpio, 0);

	sc->sc_write_command = ssdfb_spi_write_command;
	sc->sc_write_data = ssdfb_spi_write_data;

	ssdfb_attach(sc);
}

int
ssdfb_spi_detach(struct device *self, int flags)
{
	struct ssdfb_softc *sc = (struct ssdfb_softc *)self;
	ssdfb_detach(sc, flags);
	free(sc->sc_gpio, M_DEVBUF, sc->sc_gpiolen);
	return 0;
}

void
ssdfb_attach(struct ssdfb_softc *sc)
{
	struct wsemuldisplaydev_attach_args aa;
	struct rasops_info *ri;
	uint32_t *gpio;
	ssize_t len;

	pinctrl_byname(sc->sc_node, "default");

	len = OF_getproplen(sc->sc_node, "reset-gpios");
	if (len > 0) {
		gpio = malloc(len, M_DEVBUF, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "reset-gpios",
		    gpio, len);
		gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(gpio, 1);
		delay(100 * 1000);
		gpio_controller_set_pin(gpio, 0);
		delay(1000 * 1000);
		free(gpio, M_DEVBUF, len);
	}

	sc->sc_width = OF_getpropint(sc->sc_node, "solomon,width", 96);
	sc->sc_height = OF_getpropint(sc->sc_node, "solomon,height", 16);
	sc->sc_pgoff = OF_getpropint(sc->sc_node, "solomon,page-offset", 1);

	sc->sc_fbsize = round_page((sc->sc_width * sc->sc_height) / 8);
	sc->sc_fb = malloc(sc->sc_fbsize, M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_brightness = 223;

	ri = &sc->sc_rinfo;
	ri->ri_bits = malloc(sc->sc_fbsize, M_DEVBUF, M_WAITOK | M_ZERO);
	ri->ri_bs = mallocarray(sc->sc_width * sc->sc_height,
	    sizeof(struct wsdisplay_charcell), M_DEVBUF, M_WAITOK | M_ZERO);
	ri->ri_flg = RI_CLEAR | RI_VCONS;
	ri->ri_depth = 1;
	ri->ri_width = sc->sc_width;
	ri->ri_height = sc->sc_height;
	ri->ri_stride = ri->ri_width * ri->ri_depth / 8;
	ri->ri_hw = sc;

	rasops_init(ri, sc->sc_height, sc->sc_width);
	ssdfb_std_descr.ncols = ri->ri_cols;
	ssdfb_std_descr.nrows = ri->ri_rows;
	ssdfb_std_descr.textops = &ri->ri_ops;
	ssdfb_std_descr.fontwidth = ri->ri_font->fontwidth;
	ssdfb_std_descr.fontheight = ri->ri_font->fontheight;
	ssdfb_std_descr.capabilities = ri->ri_caps;

	sc->sc_riops.putchar = ri->ri_putchar;
	sc->sc_riops.copycols = ri->ri_copycols;
	sc->sc_riops.erasecols = ri->ri_erasecols;
	sc->sc_riops.copyrows = ri->ri_copyrows;
	sc->sc_riops.eraserows = ri->ri_eraserows;
	sc->sc_ri_do_cursor = ri->ri_do_cursor;

	ri->ri_putchar = ssdfb_putchar;
	ri->ri_copycols = ssdfb_copycols;
	ri->ri_erasecols = ssdfb_erasecols;
	ri->ri_copyrows = ssdfb_copyrows;
	ri->ri_eraserows = ssdfb_eraserows;
	ri->ri_do_cursor = ssdfb_do_cursor;

	printf(": %dx%d, %dbpp\n", ri->ri_width, ri->ri_height, ri->ri_depth);

	memset(&aa, 0, sizeof(aa));
	aa.console = 0;
	aa.scrdata = &ssdfb_screen_list;
	aa.accessops = &ssdfb_accessops;
	aa.accesscookie = sc;
	aa.defaultscreens = 0;

	config_found_sm(&sc->sc_dev, &aa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);
	ssdfb_init(sc);
}

int
ssdfb_detach(struct ssdfb_softc *sc, int flags)
{
	struct rasops_info *ri = &sc->sc_rinfo;
	free(ri->ri_bs, M_DEVBUF, sc->sc_width * sc->sc_height *
	    sizeof(struct wsdisplay_charcell));
	free(ri->ri_bits, M_DEVBUF, sc->sc_fbsize);
	free(sc->sc_fb, M_DEVBUF, sc->sc_fbsize);
	return 0;
}

void
ssdfb_init(struct ssdfb_softc *sc)
{
	uint8_t reg[2];

	reg[0] = SSDFB_SET_DISPLAY_OFF;
	ssdfb_write_command(sc, reg, 1);

	reg[0] = SSDFB_SET_MEMORY_ADDRESSING_MODE;
	reg[1] = 0x00; /* Horizontal Addressing Mode */
	ssdfb_write_command(sc, reg, 2);
	reg[0] = SSDFB_SET_PAGE_START_ADDRESS;
	ssdfb_write_command(sc, reg, 1);
	ssdfb_set_range(sc, 0, sc->sc_width - 1,
	    0, (sc->sc_height / 8) - 1);
	if (OF_is_compatible(sc->sc_node, "solomon,ssd1309fb-i2c") ||
	    OF_is_compatible(sc->sc_node, "solomon,ssd1309fb-spi")) {
		reg[0] = SSDFB_SET_DISPLAY_CLOCK_DIVIDE_RATIO;
		reg[1] = 0xa0;
		ssdfb_write_command(sc, reg, 2);
	}
	if (OF_is_compatible(sc->sc_node, "solomon,ssd1306fb-i2c")) {
		reg[0] = SSDFB_SET_DISPLAY_CLOCK_DIVIDE_RATIO;
		reg[1] = 0x80;
		ssdfb_write_command(sc, reg, 2);
	}
	reg[0] = SSDFB_SET_MULTIPLEX_RATIO;
	reg[1] = 0x3f;
	ssdfb_write_command(sc, reg, 2);
	reg[0] = SSDFB_SET_DISPLAY_OFFSET;
	reg[1] = OF_getpropint(sc->sc_node, "solomon,com-offset", 0);
	ssdfb_write_command(sc, reg, 2);
	reg[0] = SSDFB_SET_START_LINE | 0x00;
	ssdfb_write_command(sc, reg, 1);
	reg[0] = SSDFB_SET_COLUMN_DIRECTION_NORMAL;
	if (OF_getproplen(sc->sc_node, "solomon,com-invdir") == 0)
		reg[0] = SSDFB_SET_COLUMN_DIRECTION_REVERSE;
	ssdfb_write_command(sc, reg, 1);
	reg[0] = SSDFB_SET_COM_OUTPUT_DIRECTION_REMAP;
	if (OF_getproplen(sc->sc_node, "solomon,segment-no-remap") == 0)
		reg[0] = SSDFB_SET_COM_OUTPUT_DIRECTION_NORMAL;
	ssdfb_write_command(sc, reg, 1);
	reg[0] = SSDFB_SET_COM_PINS_HARD_CONF;
	reg[1] = 0x12;
	if (OF_getproplen(sc->sc_node, "solomon,com-seq") == 0)
		reg[1] &= ~(1 << 4);
	if (OF_getproplen(sc->sc_node, "solomon,com-lrremap") == 0)
		reg[1] |= 1 << 5;
	ssdfb_write_command(sc, reg, 2);
	reg[0] = SSDFB_SET_CONTRAST_CONTROL;
	reg[1] = sc->sc_brightness;
	ssdfb_write_command(sc, reg, 2);
	reg[0] = SSDFB_SET_PRE_CHARGE_PERIOD;
	reg[1] = (OF_getpropint(sc->sc_node, "solomon,prechargep1", 2) & 0xf) << 0;
	reg[1] |= (OF_getpropint(sc->sc_node, "solomon,prechargep2", 2) & 0xf) << 4;
	ssdfb_write_command(sc, reg, 2);
	if (OF_is_compatible(sc->sc_node, "solomon,ssd1309fb-i2c") ||
	    OF_is_compatible(sc->sc_node, "solomon,ssd1309fb-spi")) {
		reg[0] = SSDFB_SET_VCOM_DESELECT_LEVEL;
		reg[1] = 0x34;
		ssdfb_write_command(sc, reg, 2);
	}
	if (OF_is_compatible(sc->sc_node, "solomon,ssd1306fb-i2c")) {
		reg[0] = SSDFB_SET_VCOM_DESELECT_LEVEL;
		reg[1] = 0x20;
		ssdfb_write_command(sc, reg, 2);
	}
	reg[0] = SSDFB_CHARGE_PUMP;
	reg[1] = 0x10;
	if (OF_is_compatible(sc->sc_node, "solomon,ssd1306fb-i2c"))
		reg[1] |= 1 << 2;
	ssdfb_write_command(sc, reg, 2);
	reg[0] = SSDFB_ENTIRE_DISPLAY_ON;
	ssdfb_write_command(sc, reg, 1);
	reg[0] = SSDFB_SET_DISPLAY_MODE_NORMAL;
	ssdfb_write_command(sc, reg, 1);

	ssdfb_partial(sc, 0, sc->sc_width, 0, sc->sc_height);

	reg[0] = SSDFB_SET_DISPLAY_ON;
	ssdfb_write_command(sc, reg, 1);
}

void
ssdfb_set_range(struct ssdfb_softc *sc, uint8_t x1, uint8_t x2,
    uint8_t y1, uint8_t y2)
{
	uint8_t reg[3];

	y1 += sc->sc_pgoff;
	y2 += sc->sc_pgoff;

	if (sc->sc_column_range[0] != x1 || sc->sc_column_range[1] != x2) {
		sc->sc_column_range[0] = x1;
		sc->sc_column_range[1] = x2;
		reg[0] = SSDFB_SET_COLUMN_RANGE;
		reg[1] = sc->sc_column_range[0];
		reg[2] = sc->sc_column_range[1];
		ssdfb_write_command(sc, reg, 3);
	}
	if (sc->sc_page_range[0] != y1 || sc->sc_page_range[1] != y2) {
		sc->sc_page_range[0] = y1;
		sc->sc_page_range[1] = y2;
		reg[0] = SSDFB_SET_PAGE_RANGE;
		reg[1] = sc->sc_page_range[0];
		reg[2] = sc->sc_page_range[1];
		ssdfb_write_command(sc, reg, 3);
	}
}

void
ssdfb_partial(struct ssdfb_softc *sc, uint32_t x1, uint32_t x2,
    uint32_t y1, uint32_t y2)
{
	struct rasops_info *ri = &sc->sc_rinfo;
	uint32_t off, width, height;
	uint8_t *bit, val;
	int i, j, k;

	if (x2 < x1 || y2 < y1)
		return;

	if (x2 > sc->sc_width || y2 > sc->sc_height)
		return;

	y1 = y1 & ~0x7;
	y2 = roundup(y2, 8);

	width = x2 - x1;
	height = y2 - y1;

	memset(sc->sc_fb, 0, (width * height) / 8);

	for (i = 0; i < height; i += 8) {
		for (j = 0; j < width; j++) {
			bit = &sc->sc_fb[(i / 8) * width + j];
			for (k = 0; k < 8; k++) {
				off = ri->ri_stride * (y1 + i + k);
				off += (x1 + j) / 8;
				val = *(ri->ri_bits + off);
				val &= (1 << ((x1 + j) % 8));
				*bit |= !!val << k;
			}
		}
	}

	ssdfb_set_range(sc, x1, x2 - 1, y1 / 8, (y2 / 8) - 1);
	ssdfb_write_data(sc, sc->sc_fb, (width * height) / 8);
}

void
ssdfb_write_command(struct ssdfb_softc *sc, char *buf, size_t len)
{
	return sc->sc_write_command(sc, buf, len);
}

void
ssdfb_write_data(struct ssdfb_softc *sc, char *buf, size_t len)
{
	return sc->sc_write_data(sc, buf, len);
}

void
ssdfb_i2c_write_command(struct ssdfb_softc *sc, char *buf, size_t len)
{
	uint8_t type;

	type = SSDFB_I2C_COMMAND;
	iic_acquire_bus(sc->sc_i2c_tag, 0);
	if (iic_exec(sc->sc_i2c_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_i2c_addr, &type, sizeof(type), buf, len, 0)) {
		printf("%s: cannot write\n", sc->sc_dev.dv_xname);
	}
	iic_release_bus(sc->sc_i2c_tag, 0);
}

void
ssdfb_i2c_write_data(struct ssdfb_softc *sc, char *buf, size_t len)
{
	uint8_t type;

	type = SSDFB_I2C_DATA;
	iic_acquire_bus(sc->sc_i2c_tag, 0);
	if (iic_exec(sc->sc_i2c_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_i2c_addr, &type, sizeof(type), buf, len, 0)) {
		printf("%s: cannot write\n", sc->sc_dev.dv_xname);
	}
	iic_release_bus(sc->sc_i2c_tag, 0);
}

void
ssdfb_spi_write_command(struct ssdfb_softc *sc, char *buf, size_t len)
{
	if (sc->sc_cd != 0) {
		gpio_controller_set_pin(sc->sc_gpio, 0);
		sc->sc_cd = 0;
		delay(1);
	}

	spi_acquire_bus(sc->sc_spi_tag, 0);
	spi_config(sc->sc_spi_tag, &sc->sc_spi_conf);
	if (spi_write(sc->sc_spi_tag, buf, len))
		printf("%s: cannot write\n", sc->sc_dev.dv_xname);
	spi_release_bus(sc->sc_spi_tag, 0);
}

void
ssdfb_spi_write_data(struct ssdfb_softc *sc, char *buf, size_t len)
{
	if (sc->sc_cd != 1) {
		gpio_controller_set_pin(sc->sc_gpio, 1);
		sc->sc_cd = 1;
		delay(1);
	}

	spi_acquire_bus(sc->sc_spi_tag, 0);
	spi_config(sc->sc_spi_tag, &sc->sc_spi_conf);
	if (spi_write(sc->sc_spi_tag, buf, len))
		printf("%s: cannot write\n", sc->sc_dev.dv_xname);
	spi_release_bus(sc->sc_spi_tag, 0);
}

int
ssdfb_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct ssdfb_softc	*sc = v;
	struct rasops_info 	*ri = &sc->sc_rinfo;
	struct wsdisplay_param	*dp = (struct wsdisplay_param *)data;
	struct wsdisplay_fbinfo	*wdf;
	struct udl_ioctl_damage *d;
	int			 mode;
	uint8_t			 reg[2];

	switch (cmd) {
	case WSDISPLAYIO_GETPARAM:
		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			dp->min = 0;
			dp->max = 255;
			dp->curval = sc->sc_brightness;
			break;
		default:
			return (-1);
		}
		break;
	case WSDISPLAYIO_SETPARAM:
		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			if (dp->curval == 0) {
				reg[0] = SSDFB_SET_DISPLAY_OFF;
				ssdfb_write_command(sc, reg, 1);
			} else if (sc->sc_brightness == 0) {
				reg[0] = SSDFB_SET_DISPLAY_ON;
				ssdfb_write_command(sc, reg, 1);
			}
			reg[0] = SSDFB_SET_CONTRAST_CONTROL;
			reg[1] = sc->sc_brightness = dp->curval;
			ssdfb_write_command(sc, reg, 2);
			break;
		default:
			return (-1);
		}
		break;
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_DL;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = 0;
		wdf->cmsize = 0;	/* color map is unavailable */
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = ri->ri_stride;
		break;
	case WSDISPLAYIO_SMODE:
		mode = *(u_int *)data;
		switch (mode) {
		case WSDISPLAYIO_MODE_EMUL:
			if (sc->sc_mode != WSDISPLAYIO_MODE_EMUL) {
				memset(ri->ri_bits, 0, sc->sc_fbsize);
				ssdfb_partial(sc, 0, sc->sc_width,
				    0, sc->sc_height);
				sc->sc_mode = mode;
			}
			break;
		case WSDISPLAYIO_MODE_DUMBFB:
			if (sc->sc_mode != WSDISPLAYIO_MODE_DUMBFB) {
				memset(ri->ri_bits, 0, sc->sc_fbsize);
				ssdfb_partial(sc, 0, sc->sc_width,
				    0, sc->sc_height);
				sc->sc_mode = mode;
			}
			break;
		case WSDISPLAYIO_MODE_MAPPED:
		default:
			return (-1);
		}
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		*(u_int *)data = WSDISPLAYIO_DEPTH_1;
		break;
	case UDLIO_DAMAGE:
		d = (struct udl_ioctl_damage *)data;
		d->status = UDLIO_STATUS_OK;
		ssdfb_partial(sc, d->x1, d->x2, d->y1, d->y2);
		break;
	default:
		return (-1);
	}

	return (0);
}

paddr_t
ssdfb_mmap(void *v, off_t off, int prot)
{
	struct ssdfb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_rinfo;
	paddr_t			 pa;

	if (off >= sc->sc_fbsize || off < 0)
		return (-1);

	if (!pmap_extract(pmap_kernel(), (vaddr_t)ri->ri_bits, &pa))
		return (-1);

	return (pa + off);
}

int
ssdfb_alloc_screen(void *v, const struct wsscreen_descr *descr,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	struct ssdfb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_rinfo;

	return rasops_alloc_screen(ri, cookiep, curxp, curyp, attrp);
}

void
ssdfb_free_screen(void *v, void *cookie)
{
	struct ssdfb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_rinfo;

	rasops_free_screen(ri, cookie);
}

int
ssdfb_show_screen(void *v, void *cookie, int waitok,
    void (*cb) (void *, int, int), void *cb_arg)
{
	struct ssdfb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_rinfo;

	return rasops_show_screen(ri, cookie, waitok, cb, cb_arg);
}

int
ssdfb_load_font(void *v, void *cookie, struct wsdisplay_font *font)
{
	struct ssdfb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_rinfo;

	return (rasops_load_font(ri, cookie, font));
}

int
ssdfb_list_font(void *v, struct wsdisplay_font *font)
{
	struct ssdfb_softc	*sc = v;
	struct rasops_info	*ri = &sc->sc_rinfo;

	return (rasops_list_font(ri, font));
}

int
ssdfb_putchar(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri = (struct rasops_info *)cookie;
	struct ssdfb_softc *sc = ri->ri_hw;

	sc->sc_riops.putchar(cookie, row, col, uc, attr);
	ssdfb_partial(sc,
	    col * ri->ri_font->fontwidth,
	    (col + 1) * ri->ri_font->fontwidth,
	    row * ri->ri_font->fontheight,
	    (row + 1) * ri->ri_font->fontheight);
	return 0;
}

int
ssdfb_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = (struct rasops_info *)cookie;
	struct ssdfb_softc *sc = ri->ri_hw;

	sc->sc_riops.copycols(cookie, row, src, dst, num);
	ssdfb_partial(sc,
	    dst * ri->ri_font->fontwidth,
	    (dst + num) * ri->ri_font->fontwidth,
	    row * ri->ri_font->fontheight,
	    (row + 1) * ri->ri_font->fontheight);
	return 0;
}

int
ssdfb_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = (struct rasops_info *)cookie;
	struct ssdfb_softc *sc = ri->ri_hw;

	sc->sc_riops.erasecols(cookie, row, col, num, attr);
	ssdfb_partial(sc,
	    col * ri->ri_font->fontwidth,
	    (col + num) * ri->ri_font->fontwidth,
	    row * ri->ri_font->fontheight,
	    (row + 1) * ri->ri_font->fontheight);
	return 0;
}

int
ssdfb_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = (struct rasops_info *)cookie;
	struct ssdfb_softc *sc = ri->ri_hw;

	sc->sc_riops.copyrows(cookie, src, dst, num);
	ssdfb_partial(sc, 0, sc->sc_width,
	    dst * ri->ri_font->fontheight,
	    (dst + num) * ri->ri_font->fontheight);
	return 0;
}

int
ssdfb_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = (struct rasops_info *)cookie;
	struct ssdfb_softc *sc = ri->ri_hw;

	sc->sc_riops.eraserows(cookie, row, num, attr);
	if (num == ri->ri_rows && (ri->ri_flg & RI_FULLCLEAR) != 0)
		ssdfb_partial(sc, 0, sc->sc_width, 0, sc->sc_height);
	else
		ssdfb_partial(sc, 0, sc->sc_width,
		    row * ri->ri_font->fontheight,
		    (row + num) * ri->ri_font->fontheight);
	return 0;
}

int
ssdfb_do_cursor(struct rasops_info *ri)
{
	struct ssdfb_softc *sc = ri->ri_hw;
	int orow, ocol, nrow, ncol;

	orow = ri->ri_crow;
	ocol = ri->ri_ccol;
	sc->sc_ri_do_cursor(ri);
	nrow = ri->ri_crow;
	ncol = ri->ri_ccol;

	ssdfb_partial(sc,
	    ocol * ri->ri_font->fontwidth,
	    (ocol + 1) * ri->ri_font->fontwidth,
	    orow * ri->ri_font->fontheight,
	    (orow + 1) * ri->ri_font->fontheight);
	ssdfb_partial(sc,
	    ncol * ri->ri_font->fontwidth,
	    (ncol + 1) * ri->ri_font->fontwidth,
	    nrow * ri->ri_font->fontheight,
	    (nrow + 1) * ri->ri_font->fontheight);

	return 0;
}
