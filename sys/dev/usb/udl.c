/*	$OpenBSD: udl.c,v 1.103 2024/11/09 08:26:29 miod Exp $ */

/*
 * Copyright (c) 2009 Marcus Glocker <mglocker@openbsd.org>
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

/*
 * Driver for the ``DisplayLink DL-120 / DL-160'' graphic chips based
 * on the reversed engineered specifications of Florian Echtler
 * <floe@butterbrot.org>:
 *
 * 	http://floe.butterbrot.org/displaylink/doku.php
 *
 * This driver has been inspired by the cfxga(4) driver because we have
 * to deal with similar challenges, like no direct access to the video
 * memory.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/usb/udl.h>
#include <dev/usb/udlio.h>

/*
 * Defines.
 */
#if 0
#define UDL_DEBUG
#endif
#ifdef UDL_DEBUG
int udl_debug = 1;
#define DPRINTF(l, x...) do { if ((l) <= udl_debug) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

#define DN(sc)		((sc)->sc_dev.dv_xname)
#define FUNC		__func__

/*
 * Prototypes.
 */
int		udl_match(struct device *, void *, void *);
void		udl_attach(struct device *, struct device *, void *);
void		udl_attach_hook(struct device *);
int		udl_detach(struct device *, int);
int		udl_activate(struct device *, int);

int		udl_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t		udl_mmap(void *, off_t, int);
int		udl_alloc_screen(void *, const struct wsscreen_descr *,
		    void **, int *, int *, uint32_t *);
void		udl_free_screen(void *, void *);
int		udl_show_screen(void *, void *, int,
		    void (*)(void *, int, int), void *);
int		udl_load_font(void *, void *, struct wsdisplay_font *);
int		udl_list_font(void *, struct wsdisplay_font *);
void		udl_burner(void *, u_int, u_int);

int		udl_copycols(void *, int, int, int, int);
int		udl_copyrows(void *, int, int, int);
int		udl_erasecols(void *, int, int, int, uint32_t);
int		udl_eraserows(void *, int, int, uint32_t);
int		udl_putchar(void *, int, int, u_int, uint32_t);
int		udl_do_cursor(struct rasops_info *);
int		udl_draw_char(struct udl_softc *, uint16_t, uint16_t, u_int,
		    uint32_t, uint32_t);
int		udl_damage(struct udl_softc *, uint8_t *,
		    uint32_t, uint32_t, uint32_t, uint32_t);
int		udl_draw_image(struct udl_softc *, uint8_t *,
		    uint32_t, uint32_t, uint32_t, uint32_t);

usbd_status	udl_ctrl_msg(struct udl_softc *, uint8_t, uint8_t,
		    uint16_t, uint16_t, uint8_t *, size_t);
usbd_status	udl_poll(struct udl_softc *, uint32_t *);
usbd_status	udl_read_1(struct udl_softc *, uint16_t, uint8_t *);
usbd_status	udl_write_1(struct udl_softc *, uint16_t, uint8_t);
usbd_status	udl_read_edid(struct udl_softc *, uint8_t *);
uint8_t		udl_lookup_mode(uint16_t, uint16_t, uint8_t, uint16_t,
		    uint32_t);
int		udl_select_chip(struct udl_softc *);
usbd_status	udl_set_enc_key(struct udl_softc *, uint8_t *, uint8_t);
usbd_status	udl_set_decomp_table(struct udl_softc *, uint8_t *, uint16_t);

int		udl_load_huffman(struct udl_softc *);
void		udl_free_huffman(struct udl_softc *);
int		udl_fbmem_alloc(struct udl_softc *);
void		udl_fbmem_free(struct udl_softc *);
usbd_status	udl_cmd_alloc_xfer(struct udl_softc *);
void		udl_cmd_free_xfer(struct udl_softc *);
int		udl_cmd_alloc_buf(struct udl_softc *);
void		udl_cmd_free_buf(struct udl_softc *);
void		udl_cmd_insert_int_1(struct udl_softc *, uint8_t);
void		udl_cmd_insert_int_2(struct udl_softc *, uint16_t);
void		udl_cmd_insert_int_3(struct udl_softc *, uint32_t);
void		udl_cmd_insert_int_4(struct udl_softc *, uint32_t);
void		udl_cmd_insert_buf(struct udl_softc *, uint8_t *, uint32_t);
int		udl_cmd_insert_buf_comp(struct udl_softc *, uint8_t *,
		    uint32_t);
int		udl_cmd_insert_head_comp(struct udl_softc *, uint32_t);
int		udl_cmd_insert_check(struct udl_softc *, int);
void		udl_cmd_set_xfer_type(struct udl_softc *, int);
void		udl_cmd_save_offset(struct udl_softc *);
void		udl_cmd_restore_offset(struct udl_softc *);
void		udl_cmd_write_reg_1(struct udl_softc *, uint8_t, uint8_t);
void		udl_cmd_write_reg_3(struct udl_softc *, uint8_t, uint32_t);
usbd_status	udl_cmd_send(struct udl_softc *);
usbd_status	udl_cmd_send_async(struct udl_softc *);
void		udl_cmd_send_async_cb(struct usbd_xfer *, void *, usbd_status);

usbd_status	udl_init_chip(struct udl_softc *);
void		udl_init_fb_offsets(struct udl_softc *, uint32_t, uint32_t,
		    uint32_t, uint32_t);
usbd_status	udl_init_resolution(struct udl_softc *);
usbd_status	udl_clear_screen(struct udl_softc *);
void		udl_select_mode(struct udl_softc *);
int		udl_fb_buf_write(struct udl_softc *, uint8_t *, uint32_t,
		    uint32_t, uint16_t);
int		udl_fb_block_write(struct udl_softc *, uint16_t, uint32_t,
		    uint32_t, uint32_t, uint32_t);
int		udl_fb_line_write(struct udl_softc *, uint16_t, uint32_t,
		    uint32_t, uint32_t);
int		udl_fb_off_write(struct udl_softc *, uint16_t, uint32_t,
		    uint16_t);
int		udl_fb_block_copy(struct udl_softc *, uint32_t, uint32_t,
		    uint32_t, uint32_t, uint32_t, uint32_t);
int		udl_fb_line_copy(struct udl_softc *, uint32_t, uint32_t,
		    uint32_t, uint32_t, uint32_t);
int		udl_fb_off_copy(struct udl_softc *, uint32_t, uint32_t,
		    uint16_t);
int		udl_fb_buf_write_comp(struct udl_softc *, uint8_t *, uint32_t,
		    uint32_t, uint16_t);
int		udl_fb_block_write_comp(struct udl_softc *, uint16_t, uint32_t,
		    uint32_t, uint32_t, uint32_t);
int		udl_fb_line_write_comp(struct udl_softc *, uint16_t, uint32_t,
		    uint32_t, uint32_t);
int		udl_fb_off_write_comp(struct udl_softc *, uint16_t, uint32_t,
		    uint16_t);
int		udl_fb_block_copy_comp(struct udl_softc *, uint32_t, uint32_t,
		    uint32_t, uint32_t, uint32_t, uint32_t);
int		udl_fb_line_copy_comp(struct udl_softc *, uint32_t, uint32_t,
		    uint32_t, uint32_t, uint32_t);
int		udl_fb_off_copy_comp(struct udl_softc *, uint32_t, uint32_t,
		    uint16_t);
#ifdef UDL_DEBUG
void		udl_hexdump(void *, int, int);
usbd_status	udl_init_test(struct udl_softc *);
#endif

/*
 * Driver glue.
 */
struct cfdriver udl_cd = {
	NULL, "udl", DV_DULL
};

const struct cfattach udl_ca = {
	sizeof(struct udl_softc),
	udl_match,
	udl_attach,
	udl_detach,
	udl_activate
};

/*
 * wsdisplay glue.
 */
struct wsscreen_descr udl_stdscreen = {
	"std",			/* name */
	0, 0,			/* ncols, nrows */
	NULL,			/* textops */
	0, 0,			/* fontwidth, fontheight */
	WSSCREEN_WSCOLORS	/* capabilities */
};

const struct wsscreen_descr *udl_scrlist[] = {
	&udl_stdscreen
};

struct wsscreen_list udl_screenlist = {
	sizeof(udl_scrlist) / sizeof(struct wsscreen_descr *), udl_scrlist
};

struct wsdisplay_accessops udl_accessops = {
	.ioctl = udl_ioctl,
	.mmap = udl_mmap,
	.alloc_screen = udl_alloc_screen,
	.free_screen = udl_free_screen,
	.show_screen = udl_show_screen,
	.load_font = udl_load_font,
	.list_font = udl_list_font,
	.burn_screen = udl_burner
};

/*
 * Matching devices.
 */
struct udl_type {
	struct usb_devno	udl_dev;
	uint16_t		udl_chip;
};

static const struct udl_type udl_devs[] = {
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_GUC2020 },	DL160 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LD220 },	DL165 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LD190 },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_U70 },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_TOSHIBA },  DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_POLARIS2 },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_VCUD60 },	DL160 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_CONV },	DL160 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_DLDVI },	DL160 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_USBRGB },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LCDUSB7X },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LCDUSB10X },
	    DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_VGA10 },	DL120 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_WSDVI },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_EC008 },	DL160 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_FYDVI2 },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_GXDVIU2 },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LCD4300U },	DL120 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LCD8000U },	DL120 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_HPDOCK },	DL160 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_NL571 },	DL160 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_M01061 },	DL195 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_NBDOCK },	DL165 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_GXDVIU2B },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_SWDVI },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LUM70 },	DL125 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LCD8000UD_DVI },
	    DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LDEWX015U },
	    DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_KC002N },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_MIMO },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_PLUGABLE },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LT1421 },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_SD_U2VDH },	DLUNK },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_UM7X0 },	DL120 },
	{ { USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_FYDVI },	DLUNK }
};
#define udl_lookup(v, p) ((struct udl_type *)usb_lookup(udl_devs, v, p))

int
udl_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	if (udl_lookup(uaa->vendor, uaa->product) != NULL)
		return (UMATCH_VENDOR_PRODUCT);

	return (UMATCH_NONE);
}

void
udl_attach(struct device *parent, struct device *self, void *aux)
{
	struct udl_softc *sc = (struct udl_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct wsemuldisplaydev_attach_args aa;
	usbd_status error;
	int err, i;

	sc->sc_udev = uaa->device;
	sc->sc_chip = udl_lookup(uaa->vendor, uaa->product)->udl_chip;
	sc->sc_width = 0;
	sc->sc_height = 0;
	sc->sc_depth = 16;
	sc->sc_cur_mode = MAX_DL_MODES;

	/*
	 * Override chip if requested.
	 */
	if ((sc->sc_dev.dv_cfdata->cf_flags & 0xff00) > 0) {
		i = ((sc->sc_dev.dv_cfdata->cf_flags & 0xff00) >> 8) - 1;
		if (i <= DLMAX) {
			sc->sc_chip = i;
			printf("%s: %s: cf_flags (0x%04x) forced chip to %d\n",
			    DN(sc), FUNC,
			    sc->sc_dev.dv_cfdata->cf_flags, i);
		}
	}

	/*
	 * The product might have more than one chip
	 */
	if (sc->sc_chip == DLUNK)
		if (udl_select_chip(sc))
			return;


	/*
	 * Create device handle to interface descriptor.
	 */
	error = usbd_device2interface_handle(sc->sc_udev, 0, &sc->sc_iface);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/*
	 * Allocate bulk command xfer.
	 */
	error = udl_cmd_alloc_xfer(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/*
	 * Allocate command buffer.
	 */
	err = udl_cmd_alloc_buf(sc);
	if (err != 0)
		return;

	/*
	 * Open bulk TX pipe.
	 */
	error = usbd_open_pipe(sc->sc_iface, 0x01, USBD_EXCLUSIVE_USE,
	    &sc->sc_tx_pipeh);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/*
	 * Device initialization is done per synchronous xfers.
	 */
	udl_cmd_set_xfer_type(sc, UDL_CMD_XFER_SYNC);

	/*
	 * Initialize chip.
	 */
	error = udl_init_chip(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/*
	 * Select edid mode.
	 */
	udl_select_mode(sc);

	/*
	 * Override mode if requested.
	 */
	if ((sc->sc_dev.dv_cfdata->cf_flags & 0xff) > 0) {
		i = (sc->sc_dev.dv_cfdata->cf_flags & 0xff) - 1;

		if (i < MAX_DL_MODES) {
			if (udl_modes[i].chip <= sc->sc_chip) {
				sc->sc_width = udl_modes[i].hdisplay;
				sc->sc_height = udl_modes[i].vdisplay;
				printf("%s: %s: cf_flags (0x%04x) ",
				    DN(sc), FUNC,
				    sc->sc_dev.dv_cfdata->cf_flags);
				printf("forced mode to %d\n", i);
				sc->sc_cur_mode = i;
			}
		}
	}

	error = udl_init_resolution(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/*
	 * Attach wsdisplay.
	 */
	aa.console = 0;
	aa.scrdata = &udl_screenlist;
	aa.accessops = &udl_accessops;
	aa.accesscookie = sc;
	aa.defaultscreens = 0;

	sc->sc_wsdisplay = config_found(self, &aa, wsemuldisplaydevprint);

	/*
	 * Load Huffman table.
	 */
	config_mountroot(self, udl_attach_hook);
}

void
udl_attach_hook(struct device *self)
{
	struct udl_softc *sc = (struct udl_softc *)self;

	if (udl_load_huffman(sc) != 0) {
		/* compression not possible */
		printf("%s: run in uncompressed mode\n", DN(sc));
		sc->udl_fb_buf_write = udl_fb_buf_write;
		sc->udl_fb_block_write = udl_fb_block_write;
		sc->udl_fb_line_write = udl_fb_line_write;
		sc->udl_fb_off_write = udl_fb_off_write;
		sc->udl_fb_block_copy = udl_fb_block_copy;
		sc->udl_fb_line_copy = udl_fb_line_copy;
		sc->udl_fb_off_copy = udl_fb_off_copy;
	} else {
		/* compression possible */
		sc->udl_fb_buf_write = udl_fb_buf_write_comp;
		sc->udl_fb_block_write = udl_fb_block_write_comp;
		sc->udl_fb_line_write = udl_fb_line_write_comp;
		sc->udl_fb_off_write = udl_fb_off_write_comp;
		sc->udl_fb_block_copy = udl_fb_block_copy_comp;
		sc->udl_fb_line_copy = udl_fb_line_copy_comp;
		sc->udl_fb_off_copy = udl_fb_off_copy_comp;
	}
#ifdef UDL_DEBUG
	if (udl_debug >= 4)
		udl_init_test(sc);
#endif
	/*
	 * From this point on we do asynchronous xfers.
	 */
	udl_cmd_set_xfer_type(sc, UDL_CMD_XFER_ASYNC);

	/*
	 * Set initial wsdisplay emulation mode.
	 */
	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;
}

int
udl_detach(struct device *self, int flags)
{
	struct udl_softc *sc = (struct udl_softc *)self;

	/*
	 * Close bulk TX pipe.
	 */
	if (sc->sc_tx_pipeh != NULL)
		usbd_close_pipe(sc->sc_tx_pipeh);

	/*
	 * Free command buffer.
	 */
	udl_cmd_free_buf(sc);

	/*
	 * Free command xfer.
	 */
	udl_cmd_free_xfer(sc);

	/*
	 * Free Huffman table.
	 */
	udl_free_huffman(sc);

	/*
	 * Free framebuffer memory.
	 */
	udl_fbmem_free(sc);

	/*
	 * Detach wsdisplay.
	 */
	if (sc->sc_wsdisplay != NULL)
		config_detach(sc->sc_wsdisplay, DETACH_FORCE);

	return (0);
}

int
udl_activate(struct device *self, int act)
{
	struct udl_softc *sc = (struct udl_softc *)self;
	int rv;

	switch (act) {
	case DVACT_DEACTIVATE:
		usbd_deactivate(sc->sc_udev);
		break;
	}
	rv = config_activate_children(self, act);
	return (rv);
}

/* ---------- */

int
udl_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct udl_softc *sc;
	struct wsdisplay_fbinfo *wdf;
	struct udl_ioctl_damage *d;
	int r, error, mode;

	sc = v;

	DPRINTF(1, "%s: %s: ('%c', %zu, %zu)\n",
	    DN(sc), FUNC, (int) IOCGROUP(cmd), cmd & 0xff, IOCPARM_LEN(cmd));

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_DL;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->height = sc->sc_height;
		wdf->width = sc->sc_width;
		wdf->depth = sc->sc_depth;
		wdf->stride = sc->sc_width * (sc->sc_depth / 8);
		wdf->offset = 0;
		wdf->cmsize = 0;	/* XXX fill up colormap size */
		break;
	case WSDISPLAYIO_SMODE:
		mode = *(u_int *)data;
		if (mode == sc->sc_mode)
			break;
		switch (mode) {
		case WSDISPLAYIO_MODE_EMUL:
			/* clear screen */
			(void)udl_clear_screen(sc);
			break;
		case WSDISPLAYIO_MODE_DUMBFB:
			/* TODO */
			break;
		default:
			return (EINVAL);
		}
		sc->sc_mode = mode;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_width * (sc->sc_depth / 8);
		break;
	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		/* handled for us by wscons */
		break;
	case UDLIO_DAMAGE:
		d = (struct udl_ioctl_damage *)data;
		d->status = UDLIO_STATUS_OK;
		r = udl_damage(sc, sc->sc_fbmem, d->x1, d->x2, d->y1, d->y2);
		if (r != 0) {
			error = tsleep_nsec(sc, 0, "udlio", MSEC_TO_NSEC(10));
			if (error) {
				d->status = UDLIO_STATUS_FAILED;
			} else {
				r = udl_damage(sc, sc->sc_fbmem, d->x1, d->x2,
				    d->y1, d->y2);
				if (r != 0)
					d->status = UDLIO_STATUS_FAILED;
			}
		}
		break;
	default:
		return (-1);
	}

	return (0);
}

paddr_t
udl_mmap(void *v, off_t off, int prot)
{
	struct udl_softc *sc;
	caddr_t p;
	paddr_t pa;

	sc = v;

	DPRINTF(1, "%s: %s\n", DN(sc), FUNC);

	/* allocate framebuffer memory */
	if (udl_fbmem_alloc(sc) == -1)
		return (-1);

	/* return memory address to userland process */
	p = sc->sc_fbmem + off;
	if (pmap_extract(pmap_kernel(), (vaddr_t)p, &pa) == FALSE) {
		printf("udl_mmap: invalid page\n");
		udl_fbmem_free(sc);
		return (-1);
	}
	return (pa);
}

int
udl_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	struct udl_softc *sc = v;
	struct wsdisplay_font *font;

	DPRINTF(1, "%s: %s\n", DN(sc), FUNC);

	if (sc->sc_nscreens > 0)
		return (ENOMEM);

	/*
	 * Initialize rasops.
	 */
	sc->sc_ri.ri_depth = sc->sc_depth;
	sc->sc_ri.ri_bits = NULL;
	sc->sc_ri.ri_width = sc->sc_width;
	sc->sc_ri.ri_height = sc->sc_height;
	sc->sc_ri.ri_stride = sc->sc_width * sc->sc_height / 8;
	sc->sc_ri.ri_hw = (void *)sc;
	sc->sc_ri.ri_flg = 0;

	/* swap B and R at 16 bpp */
	if (sc->sc_depth == 16) {
		sc->sc_ri.ri_rnum = 5;
		sc->sc_ri.ri_rpos = 11;
		sc->sc_ri.ri_gnum = 6;
		sc->sc_ri.ri_gpos = 5;
		sc->sc_ri.ri_bnum = 5;
		sc->sc_ri.ri_bpos = 0;
	}

	rasops_init(&sc->sc_ri, 100, 200);

	sc->sc_ri.ri_ops.copycols = udl_copycols;
	sc->sc_ri.ri_ops.copyrows = udl_copyrows;
	sc->sc_ri.ri_ops.erasecols = udl_erasecols;
	sc->sc_ri.ri_ops.eraserows = udl_eraserows;
	sc->sc_ri.ri_ops.putchar = udl_putchar;
	sc->sc_ri.ri_do_cursor = udl_do_cursor;

	sc->sc_ri.ri_ops.pack_attr(&sc->sc_ri, 0, 0, 0, attrp);

	udl_stdscreen.nrows = sc->sc_ri.ri_rows;
	udl_stdscreen.ncols = sc->sc_ri.ri_cols;
	udl_stdscreen.textops = &sc->sc_ri.ri_ops;
	udl_stdscreen.fontwidth = sc->sc_ri.ri_font->fontwidth;
	udl_stdscreen.fontheight = sc->sc_ri.ri_font->fontheight;
	udl_stdscreen.capabilities = sc->sc_ri.ri_caps;

	*cookiep = &sc->sc_ri;
	*curxp = 0;
	*curyp = 0;

	/* allocate character backing store */
	sc->sc_cbs = mallocarray(sc->sc_ri.ri_rows, sc->sc_ri.ri_cols *
	    sizeof(*sc->sc_cbs), M_USBDEV, M_NOWAIT|M_ZERO);
	if (sc->sc_cbs == NULL) {
		printf("%s: can't allocate mem for character backing store!\n",
		    DN(sc));
		return (ENOMEM);
	}
	sc->sc_cbslen = sc->sc_ri.ri_rows * sc->sc_ri.ri_cols *
	    sizeof(*sc->sc_cbs);

	sc->sc_nscreens++;

	font = sc->sc_ri.ri_font;
	DPRINTF(1, "%s: %s: using font %s (%dx%d)\n",
	    DN(sc), FUNC, font->name, sc->sc_ri.ri_cols, sc->sc_ri.ri_rows);

	return (0);
}

void
udl_free_screen(void *v, void *cookie)
{
	struct udl_softc *sc;

	sc = v;

	DPRINTF(1, "%s: %s\n", DN(sc), FUNC);

	/* free character backing store */
	if (sc->sc_cbs != NULL)
		free(sc->sc_cbs, M_USBDEV, sc->sc_cbslen);

	sc->sc_nscreens--;
}

int
udl_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	struct udl_softc *sc;

	sc = v;

	DPRINTF(1, "%s: %s\n", DN(sc), FUNC);

	return (0);
}

int
udl_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct udl_softc *sc = v;
	struct rasops_info *ri = &sc->sc_ri;

	return rasops_load_font(ri, emulcookie, font);
}

int
udl_list_font(void *v, struct wsdisplay_font *font)
{
	struct udl_softc *sc = v;
	struct rasops_info *ri = &sc->sc_ri;

	return rasops_list_font(ri, font);
}

void
udl_burner(void *v, u_int on, u_int flags)
{
	struct udl_softc *sc;

	sc = v;

	DPRINTF(1, "%s: %s: screen %s\n", DN(sc), FUNC, on ? "ON" : "OFF");

	if (on)
		udl_cmd_write_reg_1(sc, UDL_REG_SCREEN, UDL_REG_SCREEN_ON);
	else
		udl_cmd_write_reg_1(sc, UDL_REG_SCREEN, UDL_REG_SCREEN_OFF);

	udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);

	(void)udl_cmd_send_async(sc);
}

/* ---------- */

int
udl_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct udl_softc *sc;
	int sx, sy, dx, dy, cx, cy, r;
	usbd_status error;

	sc = ri->ri_hw;

	DPRINTF(2, "%s: %s: row=%d, src=%d, dst=%d, num=%d\n",
	    DN(sc), FUNC, row, src, dst, num);

	udl_cmd_save_offset(sc);

	sx = src * ri->ri_font->fontwidth;
	sy = row * ri->ri_font->fontheight;
	dx = dst * ri->ri_font->fontwidth;
	dy = row * ri->ri_font->fontheight;
	cx = num * ri->ri_font->fontwidth;
	cy = ri->ri_font->fontheight;

	/* copy row block to off-screen first to fix overlay-copy problem */
	r = (sc->udl_fb_block_copy)
	    (sc, sx, sy, 0, sc->sc_ri.ri_emuheight, cx, cy);
	if (r != 0)
		goto fail;

	/* copy row block back from off-screen now */
	r = (sc->udl_fb_block_copy)
	    (sc, 0, sc->sc_ri.ri_emuheight, dx, dy, cx, cy);
	if (r != 0)
		goto fail;

	error = udl_cmd_send_async(sc);
	if (error != USBD_NORMAL_COMPLETION) {
fail:
		udl_cmd_restore_offset(sc);
		return (EAGAIN);
	}

	/* update character backing store */
	bcopy(sc->sc_cbs + ((row * sc->sc_ri.ri_cols) + src),
	    sc->sc_cbs + ((row * sc->sc_ri.ri_cols) + dst),
	    num * sizeof(*sc->sc_cbs));

	return (0);
}

int
udl_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct udl_softc *sc;
	int sy, dy, cx, cy, r;
	usbd_status error;

	sc = ri->ri_hw;

	DPRINTF(2, "%s: %s: src=%d, dst=%d, num=%d\n",
	    DN(sc), FUNC, src, dst, num);

	udl_cmd_save_offset(sc);

	sy = src * sc->sc_ri.ri_font->fontheight;
	dy = dst * sc->sc_ri.ri_font->fontheight;
	cx = sc->sc_ri.ri_emuwidth;
	cy = num * sc->sc_ri.ri_font->fontheight;

	/* copy row block to off-screen first to fix overlay-copy problem */
	r = (sc->udl_fb_block_copy)
	    (sc, 0, sy, 0, sc->sc_ri.ri_emuheight, cx, cy);
	if (r != 0)
		goto fail;

	/* copy row block back from off-screen now */
	r = (sc->udl_fb_block_copy)
	    (sc, 0, sc->sc_ri.ri_emuheight, 0, dy, cx, cy);
	if (r != 0)
		goto fail;

	error = udl_cmd_send_async(sc);
	if (error != USBD_NORMAL_COMPLETION) {
fail:
		udl_cmd_restore_offset(sc);
		return (EAGAIN);
	}

	/* update character backing store */
	bcopy(sc->sc_cbs + (src * sc->sc_ri.ri_cols),
	    sc->sc_cbs + (dst * sc->sc_ri.ri_cols),
	    (num * sc->sc_ri.ri_cols) * sizeof(*sc->sc_cbs));

	return (0);
}

int
udl_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct udl_softc *sc = ri->ri_hw;
	uint16_t bgc;
	int fg, bg;
	int x, y, cx, cy, r;
	usbd_status error;

	sc = ri->ri_hw;

	DPRINTF(2, "%s: %s: row=%d, col=%d, num=%d\n",
	    DN(sc), FUNC, row, col, num);

	udl_cmd_save_offset(sc);

	sc->sc_ri.ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	bgc = (uint16_t)sc->sc_ri.ri_devcmap[bg];

	x = col * sc->sc_ri.ri_font->fontwidth;
	y = row * sc->sc_ri.ri_font->fontheight;
	cx = num * sc->sc_ri.ri_font->fontwidth;
	cy = sc->sc_ri.ri_font->fontheight;

	r = (sc->udl_fb_block_write)(sc, bgc, x, y, cx, cy);
	if (r != 0)
		goto fail;

	error = udl_cmd_send_async(sc);
	if (error != USBD_NORMAL_COMPLETION) {
fail:
		udl_cmd_restore_offset(sc);
		return (EAGAIN);
	}

	/* update character backing store */
	bzero(sc->sc_cbs + ((row * sc->sc_ri.ri_cols) + col),
	    num * sizeof(*sc->sc_cbs));

	return (0);
}

int
udl_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct udl_softc *sc;
	uint16_t bgc;
	int fg, bg;
	int x, y, cx, cy, r;
	usbd_status error;

	sc = ri->ri_hw;

	DPRINTF(2, "%s: %s: row=%d, num=%d\n", DN(sc), FUNC, row, num);

	udl_cmd_save_offset(sc);

	sc->sc_ri.ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	bgc = (uint16_t)sc->sc_ri.ri_devcmap[bg];

	x = 0;
	y = row * sc->sc_ri.ri_font->fontheight;
	cx = sc->sc_ri.ri_emuwidth;
	cy = num * sc->sc_ri.ri_font->fontheight;

	r = (sc->udl_fb_block_write)(sc, bgc, x, y, cx, cy);
	if (r != 0)
		goto fail;

	error = udl_cmd_send_async(sc);
	if (error != USBD_NORMAL_COMPLETION) {
fail:
		udl_cmd_restore_offset(sc);
		return (EAGAIN);
	}

	/* update character backing store */
	bzero(sc->sc_cbs + (row * sc->sc_ri.ri_cols),
	    (num * sc->sc_ri.ri_cols) * sizeof(*sc->sc_cbs));

	return (0);
}

int
udl_putchar(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct udl_softc *sc = ri->ri_hw;
	int r;
	uint16_t fgc, bgc;
	uint32_t x, y, fg, bg;

	DPRINTF(4, "%s: %s\n", DN(sc), FUNC);

	udl_cmd_save_offset(sc);

	sc->sc_ri.ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	fgc = (uint16_t)sc->sc_ri.ri_devcmap[fg];
	bgc = (uint16_t)sc->sc_ri.ri_devcmap[bg];

	x = col * ri->ri_font->fontwidth;
	y = row * ri->ri_font->fontheight;

	if (uc == ' ') {
		/*
		 * Writing a block for the space character instead rendering
		 * it from font bits is more slim.
		 */
		r = (sc->udl_fb_block_write)(sc, bgc, x, y,
		    ri->ri_font->fontwidth, ri->ri_font->fontheight);
		if (r != 0)
			goto fail;
	} else {
		/* render a character from font bits */
		r = udl_draw_char(sc, fgc, bgc, uc, x, y);
		if (r != 0)
			goto fail;
	}

	/*
	 * We don't call udl_cmd_send_async() here, since sending each
	 * character by itself gets the performance down bad.  Instead the
	 * character will be buffered until another rasops function flush
	 * the buffer.
	 */

	/* update character backing store */
	sc->sc_cbs[(row * sc->sc_ri.ri_cols) + col] = uc;

	return (0);

fail:
	udl_cmd_restore_offset(sc);
	return (EAGAIN);
}

int
udl_do_cursor(struct rasops_info *ri)
{
	struct udl_softc *sc = ri->ri_hw;
	int r, pos;
	uint32_t x, y;
	uint8_t save_cursor;
	usbd_status error;

	DPRINTF(2, "%s: %s: ccol=%d, crow=%d\n",
	    DN(sc), FUNC, ri->ri_ccol, ri->ri_crow);

	udl_cmd_save_offset(sc);
	save_cursor = sc->sc_cursor_on;

	x = ri->ri_ccol * ri->ri_font->fontwidth;
	y = ri->ri_crow * ri->ri_font->fontheight;

	if (sc->sc_cursor_on == 0) {
		/* save the last character block to off-screen */
		r = (sc->udl_fb_block_copy)(sc, x, y, 0, sc->sc_ri.ri_emuheight,
		    ri->ri_font->fontwidth, ri->ri_font->fontheight);
		if (r != 0)
			goto fail;

		/* draw cursor */
		pos = (ri->ri_crow * sc->sc_ri.ri_cols) + ri->ri_ccol;
		if (sc->sc_cbs[pos] == 0 || sc->sc_cbs[pos] == ' ') {
			r = (sc->udl_fb_block_write)(sc, 0xffff, x, y,
			    ri->ri_font->fontwidth, ri->ri_font->fontheight);
		} else {
			r = udl_draw_char(sc, 0x0000, 0xffff, sc->sc_cbs[pos],
			    x, y);
		}
		if (r != 0)
			goto fail;

		sc->sc_cursor_on = 1;
	} else {
		/* restore the last saved character from off-screen */
		r = (sc->udl_fb_block_copy)(sc, 0, sc->sc_ri.ri_emuheight, x, y,
		    ri->ri_font->fontwidth, ri->ri_font->fontheight);
		if (r != 0)
			goto fail;

		sc->sc_cursor_on = 0;
	}

	error = udl_cmd_send_async(sc);
	if (error != USBD_NORMAL_COMPLETION) {
fail:
		udl_cmd_restore_offset(sc);
		sc->sc_cursor_on = save_cursor;
		return (EAGAIN);
	}

	return (0);
}

int
udl_draw_char(struct udl_softc *sc, uint16_t fg, uint16_t bg, u_int uc,
    uint32_t x, uint32_t y)
{
	int i, j, ly, r;
	uint8_t *fontchar;
	uint8_t buf[UDL_CMD_MAX_DATA_SIZE];
	uint16_t *line, lrgb16, fontbits, luc;
	struct wsdisplay_font *font = sc->sc_ri.ri_font;

	fontchar = (uint8_t *)(font->data + (uc - font->firstchar) *
	    sc->sc_ri.ri_fontscale);

	ly = y;
	for (i = 0; i < font->fontheight; i++) {
		if (font->fontwidth > 8) {
			fontbits = betoh16(*(uint16_t *)fontchar);
		} else {
			fontbits = *fontchar;
			fontbits = fontbits << 8;
		}
		line = (uint16_t *)buf;

		for (j = 15; j > (15 - font->fontwidth); j--) {
			luc = 1 << j;
			if (fontbits & luc)
				lrgb16 = htobe16(fg);
			else
				lrgb16 = htobe16(bg);
			bcopy(&lrgb16, line, 2);
			line++;
		}
		r = (sc->udl_fb_buf_write)(sc, buf, x, ly, font->fontwidth);
		if (r != 0)
			return (r);
		ly++;

		fontchar += font->stride;
	}

	return (0);
}

int
udl_damage(struct udl_softc *sc, uint8_t *image,
    uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2)
{
	int r;
	int x, y, width, height;
	usbd_status error;

	udl_cmd_save_offset(sc);

	x = x1;
	y = y1;
	width = x2 - x1;
	height = y2 - y1;

	r = udl_draw_image(sc, image, x, y, width, height);
	if (r != 0)
		goto fail;

	error = udl_cmd_send_async(sc);
	if (error != USBD_NORMAL_COMPLETION) {
fail:
		udl_cmd_restore_offset(sc);
		return (EAGAIN);
	}

	return (0);
}

int
udl_draw_image(struct udl_softc *sc, uint8_t *image,
    uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	int i, j, r;
	int width_cur, x_cur;
	uint8_t buf[UDL_CMD_MAX_DATA_SIZE];
	uint16_t *image16, lrgb16;
	uint32_t off, block;

	for (i = 0; i < height; i++) {
		off = ((y * sc->sc_width) + x) * 2;
		x_cur = x;
		width_cur = width;

		while (width_cur) {
			if (width_cur > UDL_CMD_MAX_PIXEL_COUNT)
				block = UDL_CMD_MAX_PIXEL_COUNT;
			else
				block = width_cur;

			/* fix RGB ordering */
			image16 = (uint16_t *)(image + off);
			for (j = 0; j < (block * 2); j += 2) {
				lrgb16 = htobe16(*image16);
				bcopy(&lrgb16, buf + j, 2);
				image16++;
			}

			r = (sc->udl_fb_buf_write)(sc, buf, x_cur, y, block);
			if (r != 0)
				return (r);

			off += block * 2;
			x_cur += block;
			width_cur -= block;
		}
		y++;
	}

	return (0);
}

/* ---------- */

usbd_status
udl_ctrl_msg(struct udl_softc *sc, uint8_t rt, uint8_t r,
    uint16_t index, uint16_t value, uint8_t *buf, size_t len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = rt;
	req.bRequest = r;
	USETW(req.wIndex, index);
	USETW(req.wValue, value);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(error));
		return (error);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
udl_poll(struct udl_softc *sc, uint32_t *buf)
{
	uint8_t lbuf[4];
	usbd_status error;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_POLL, 0x0000, 0x0000, lbuf, 4);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(error));
		return (error);
	}
	*buf = *(uint32_t *)lbuf;

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
udl_read_1(struct udl_softc *sc, uint16_t addr, uint8_t *buf)
{
	uint8_t lbuf[1];
	usbd_status error;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_READ_1, addr, 0x0000, lbuf, 1);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(error));
		return (error);
	}
	*buf = *(uint8_t *)lbuf;

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
udl_write_1(struct udl_softc *sc, uint16_t addr, uint8_t buf)
{
	usbd_status error;

	error = udl_ctrl_msg(sc, UT_WRITE_VENDOR_DEVICE,
	    UDL_CTRL_CMD_WRITE_1, addr, 0x0000, &buf, 1);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(error));
		return (error);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
udl_read_edid(struct udl_softc *sc, uint8_t *buf)
{
	uint8_t lbuf[64];
	uint16_t offset;
	usbd_status error;

	offset = 0;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_READ_EDID, 0x00a1, (offset << 8), lbuf, 64);
	if (error != USBD_NORMAL_COMPLETION)
		goto fail;
	bcopy(lbuf + 1, buf + offset, 63);
	offset += 63;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_READ_EDID, 0x00a1, (offset << 8), lbuf, 64);
	if (error != USBD_NORMAL_COMPLETION)
		goto fail;
	bcopy(lbuf + 1, buf + offset, 63);
	offset += 63;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_READ_EDID, 0x00a1, (offset << 8), lbuf, 3);
	if (error != USBD_NORMAL_COMPLETION)
		goto fail;
	bcopy(lbuf + 1, buf + offset, 2);

	return (USBD_NORMAL_COMPLETION);
fail:
	printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(error));
	return (error);
}

uint8_t
udl_lookup_mode(uint16_t hdisplay, uint16_t vdisplay, uint8_t freq,
    uint16_t chip, uint32_t clock)
{
	uint8_t	idx = 0;

	/*
	 * Check first if we have a matching mode with pixelclock
	 */
	while (idx < MAX_DL_MODES) {
		if ((udl_modes[idx].hdisplay == hdisplay) &&
		    (udl_modes[idx].vdisplay == vdisplay) &&
		    (udl_modes[idx].clock == clock) &&
		    (udl_modes[idx].chip <= chip)) {
			return(idx);
		}
		idx++;
	}

	/*
	 * If not, check for matching mode with update frequency
	 */
	idx = 0;
	while (idx < MAX_DL_MODES) {
		if ((udl_modes[idx].hdisplay == hdisplay) &&
		    (udl_modes[idx].vdisplay == vdisplay) &&
		    (udl_modes[idx].freq == freq) &&
		    (udl_modes[idx].chip <= chip)) {
			return(idx);
		}
		idx++;
	}

	return(idx);
}

int
udl_select_chip(struct udl_softc *sc)
{
	char serialnum[USB_MAX_STRING_LEN];
	usb_device_descriptor_t *dd;
	usb_string_descriptor_t us;
	usbd_status error;
	int len, i, n;
	char *s;
	uint16_t c;

	sc->sc_chip = DL120;

	dd = usbd_get_device_descriptor(sc->sc_udev);

	if ((UGETW(dd->idVendor) == USB_VENDOR_DISPLAYLINK) &&
	    (UGETW(dd->idProduct) == USB_PRODUCT_DISPLAYLINK_WSDVI)) {

		/*
		 * WS Tech DVI is DL120 or DL160. All deviced uses the
		 * same revision (0.04) so iSerialNumber must be used
		 * to determine which chip it is.
		 */

		bzero(serialnum, sizeof serialnum);
		error = usbd_get_string_desc(sc->sc_udev, dd->iSerialNumber,
		    0, &us, &len);
		if (error != USBD_NORMAL_COMPLETION)
			return (1);

		s = &serialnum[0];
		n = len / 2 - 1;
		for (i = 0; i < n && i < nitems(us.bString); i++) {
			c = UGETW(us.bString[i]);
			/* Convert from Unicode, handle buggy strings. */
			if ((c & 0xff00) == 0)
				*s++ = c;
			else if ((c & 0x00ff) == 0)
				*s++ = c >> 8;
			else
				*s++ = '?';
		}
		*s++ = 0;

		if (strlen(serialnum) > 7)
			if (strncmp(serialnum, "0198-13", 7) == 0)
				sc->sc_chip = DL160;

		DPRINTF(1, "%s: %s: iSerialNumber (%s) used to select chip (%d)\n",
		     DN(sc), FUNC, serialnum, sc->sc_chip);

	}

	if ((UGETW(dd->idVendor) == USB_VENDOR_DISPLAYLINK) &&
	    (UGETW(dd->idProduct) == USB_PRODUCT_DISPLAYLINK_SWDVI)) {

		/*
		 * SUNWEIT DVI is DL160, DL125, DL165 or DL195. Major revision
		 * can be used to differ between DL1x0 and DL1x5. Minor to
		 * differ between DL1x5. iSerialNumber seems not to be unique.
		 */

		sc->sc_chip = DL160;

		if (UGETW(dd->bcdDevice) >= 0x100) {
			sc->sc_chip = DL165;
			if (UGETW(dd->bcdDevice) == 0x104)
				sc->sc_chip = DL195;
			if (UGETW(dd->bcdDevice) == 0x108)
				sc->sc_chip = DL125;
		}

		DPRINTF(1, "%s: %s: bcdDevice (%02x) used to select chip (%d)\n",
		     DN(sc), FUNC, UGETW(dd->bcdDevice), sc->sc_chip);

	}

	return (0);
}

usbd_status
udl_set_enc_key(struct udl_softc *sc, uint8_t *buf, uint8_t len)
{
	usbd_status error;

	error = udl_ctrl_msg(sc, UT_WRITE_VENDOR_DEVICE,
	    UDL_CTRL_CMD_SET_KEY, 0x0000, 0x0000, buf, len);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(error));
		return (error);
	}
	
	return (USBD_NORMAL_COMPLETION);
}

usbd_status
udl_set_decomp_table(struct udl_softc *sc, uint8_t *buf, uint16_t len)
{
	int err;

	udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
	udl_cmd_insert_int_1(sc, UDL_BULK_CMD_DECOMP);
	udl_cmd_insert_int_4(sc, 0x263871cd);	/* magic number */
	udl_cmd_insert_int_4(sc, 0x00000200);	/* 512 byte chunks */
	udl_cmd_insert_buf(sc, buf, len);

	err = udl_cmd_send(sc);
	if (err != 0)
		return (USBD_INVAL);

	return (USBD_NORMAL_COMPLETION);
}

/* ---------- */

int
udl_load_huffman(struct udl_softc *sc)
{
	const char *name = "udl_huffman";
	int error;

	if (sc->sc_huffman == NULL) {
		error = loadfirmware(name, &sc->sc_huffman,
		    &sc->sc_huffman_size);
		if (error != 0) {
			printf("%s: error %d, could not read huffman table "
			    "%s!\n", DN(sc), error, name);
			return (EIO);
		}
	}

	DPRINTF(1, "%s: huffman table %s allocated\n", DN(sc), name);

	return (0);
}

void
udl_free_huffman(struct udl_softc *sc)
{
	if (sc->sc_huffman != NULL) {
		free(sc->sc_huffman, M_USBDEV, sc->sc_huffman_size);
		sc->sc_huffman = NULL;
		sc->sc_huffman_size = 0;
		DPRINTF(1, "%s: huffman table freed\n", DN(sc));
	}
}

int
udl_fbmem_alloc(struct udl_softc *sc)
{
	int size;

	size = (sc->sc_width * sc->sc_height) * (sc->sc_depth / 8);
	size = round_page(size);

	if (sc->sc_fbmem == NULL) {
		sc->sc_fbmem = malloc(size, M_USBDEV, M_NOWAIT|M_ZERO);
		if (sc->sc_fbmem == NULL)
			return (-1);
	}
	sc->sc_fbmemsize = size;
	return (0);
}

void
udl_fbmem_free(struct udl_softc *sc)
{
	if (sc->sc_fbmem != NULL) {
		free(sc->sc_fbmem, M_USBDEV, sc->sc_fbmemsize);
		sc->sc_fbmem = NULL;
		sc->sc_fbmemsize = 0;
	}
}

usbd_status
udl_cmd_alloc_xfer(struct udl_softc *sc)
{
	int i;

	for (i = 0; i < UDL_CMD_XFER_COUNT; i++) {
		struct udl_cmd_xfer *cx = &sc->sc_cmd_xfer[i];

		cx->sc = sc;

		cx->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (cx->xfer == NULL) {
			printf("%s: %s: can't allocate xfer handle!\n",
			    DN(sc), FUNC);
			return (USBD_NOMEM);
		}

		cx->buf = usbd_alloc_buffer(cx->xfer, UDL_CMD_MAX_XFER_SIZE);
		if (cx->buf == NULL) {
			printf("%s: %s: can't allocate xfer buffer!\n",
			    DN(sc), FUNC);
			return (USBD_NOMEM);
		}
	}

	return (USBD_NORMAL_COMPLETION);
}

void
udl_cmd_free_xfer(struct udl_softc *sc)
{
	int i;

	for (i = 0; i < UDL_CMD_XFER_COUNT; i++) {
		struct udl_cmd_xfer *cx = &sc->sc_cmd_xfer[i];

		if (cx->xfer != NULL) {
			usbd_free_xfer(cx->xfer);
			cx->xfer = NULL;
		}
	}
}

int
udl_cmd_alloc_buf(struct udl_softc *sc)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	cb->buf = malloc(UDL_CMD_MAX_XFER_SIZE, M_USBDEV, M_NOWAIT|M_ZERO);
	if (cb->buf == NULL) {
		printf("%s: %s: can't allocate buffer!\n",
		    DN(sc), FUNC);
		return (ENOMEM);
	}
	cb->off = 0;
	cb->compblock = 0;

	return (0);
}

void
udl_cmd_free_buf(struct udl_softc *sc)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	if (cb->buf != NULL) {
		free(cb->buf, M_USBDEV, UDL_CMD_MAX_XFER_SIZE);
		cb->buf = NULL;
	}
	cb->off = 0;
}

void
udl_cmd_insert_int_1(struct udl_softc *sc, uint8_t value)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	cb->buf[cb->off] = value;

	cb->off += 1;
}

void
udl_cmd_insert_int_2(struct udl_softc *sc, uint16_t value)
{
	uint16_t lvalue;
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	lvalue = htobe16(value);
	bcopy(&lvalue, cb->buf + cb->off, 2);

	cb->off += 2;
}

void
udl_cmd_insert_int_3(struct udl_softc *sc, uint32_t value)
{
	uint32_t lvalue;
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
#if BYTE_ORDER == BIG_ENDIAN
	lvalue = htobe32(value) << 8;
#else
	lvalue = htobe32(value) >> 8;
#endif
	bcopy(&lvalue, cb->buf + cb->off, 3);

	cb->off += 3;
}

void
udl_cmd_insert_int_4(struct udl_softc *sc, uint32_t value)
{
	uint32_t lvalue;
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	lvalue = htobe32(value);
	bcopy(&lvalue, cb->buf + cb->off, 4);

	cb->off += 4;
}

void
udl_cmd_insert_buf(struct udl_softc *sc, uint8_t *buf, uint32_t len)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	bcopy(buf, cb->buf + cb->off, len);

	cb->off += len;
}

int
udl_cmd_insert_buf_comp(struct udl_softc *sc, uint8_t *buf, uint32_t len)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	struct udl_huffman *h;
	uint8_t bit_pos;
	uint16_t *pixels, prev;
	int16_t diff;
	uint32_t bit_count, bit_pattern, bit_cur;
	int i, j, bytes, eob, padding, next;

	pixels = (uint16_t *)buf;
	bit_pos = bytes = eob = padding = 0;

	/*
	 * If the header doesn't fit into the 512 byte main-block anymore,
	 * skip the header and finish up the main-block.  We return zero
	 * to signal our caller that the header has been skipped.
	 */
	if (cb->compblock >= UDL_CB_RESTART_SIZE) {
		cb->off -= UDL_CMD_WRITE_HEAD_SIZE;
		cb->compblock -= UDL_CMD_WRITE_HEAD_SIZE;
		eob = 1;
	}

	/*
	 * Generate a sub-block with maximal 256 pixels compressed data.
	 */
	for (i = 0; i < len / 2 && eob == 0; i++) {
		/* get difference between current and previous pixel */
		if (i > 0)
			prev = betoh16(pixels[i - 1]);
		else
			prev = 0;

		/* get the huffman difference bit sequence */
		diff = betoh16(pixels[i]) - prev;
		h = (struct udl_huffman *)(sc->sc_huffman + UDL_HUFFMAN_BASE);
		h += diff;
		bit_count = h->bit_count;
		bit_pattern = betoh32(h->bit_pattern);


		/* we are near the end of the main-block, so quit loop */
		if (bit_count % 8 == 0)
			next = bit_count / 8;
		else
			next = (bit_count / 8) + 1;

		if (cb->compblock + next >= UDL_CB_BODY_SIZE) {
			eob = 1;
			break;
		}

		/* generate one pixel compressed data */
		for (j = 0; j < bit_count; j++) {
			if (bit_pos == 0)
				cb->buf[cb->off] = 0;
			bit_cur = (bit_pattern >> j) & 1;
			cb->buf[cb->off] |= (bit_cur << bit_pos);
			bit_pos++;

			if (bit_pos == 8) {
				bit_pos = 0;
				cb->off++;
				cb->compblock++;
			}
		}
		bytes += 2;
	}

	/*
	 * If we have bits left in our last byte, round up to the next
	 * byte, so we don't overwrite them.
	 */
	if (bit_pos != 0) {
		cb->off++;
		cb->compblock++;
	}

	/*
	 * Finish up a 512 byte main-block.  The leftover space gets
	 * padded to zero.  Finally terminate the block by writing the
	 * 0xff-into-UDL_REG_SYNC-register sequence.
	 */
	if (eob == 1) {
		padding = (UDL_CB_BODY_SIZE - cb->compblock);
		for (i = 0; i < padding; i++) {
			cb->buf[cb->off] = 0;
			cb->off++;
			cb->compblock++;
		}
		udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);
		cb->compblock = 0;
	}

	/* return how many bytes we have compressed */
	return (bytes);
}

int
udl_cmd_insert_head_comp(struct udl_softc *sc, uint32_t len)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	int i, padding;

	if (cb->compblock > UDL_CB_BODY_SIZE) {
		cb->off -= UDL_CMD_COPY_HEAD_SIZE;
		cb->compblock -= UDL_CMD_COPY_HEAD_SIZE;

		padding = (UDL_CB_BODY_SIZE - cb->compblock);
		for (i = 0; i < padding; i++) {
			cb->buf[cb->off] = 0;
			cb->off++;
			cb->compblock++;
		}
		udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);
		cb->compblock = 0;
		return (0);
	}

	return (len);
}

int
udl_cmd_insert_check(struct udl_softc *sc, int len)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	int total;
	usbd_status error;

	total = cb->off + len;

	if (total > UDL_CMD_MAX_XFER_SIZE) {
		/* command buffer is almost full, try to flush it */
		if (cb->xfer_type == UDL_CMD_XFER_ASYNC)
			error = udl_cmd_send_async(sc);
		else
			error = udl_cmd_send(sc);
		if (error != USBD_NORMAL_COMPLETION) {
			DPRINTF(1, "%s: %s: can't flush full command buffer\n",
			    DN(sc), FUNC);
			return (EAGAIN);
		}
	}

	return (0);
}

void
udl_cmd_set_xfer_type(struct udl_softc *sc, int xfer_type)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	cb->xfer_type = xfer_type;
}

void
udl_cmd_save_offset(struct udl_softc *sc)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	cb->off_save = cb->off;
	cb->compblock_save = cb->compblock;
}

void
udl_cmd_restore_offset(struct udl_softc *sc)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;

	cb->off = cb->off_save;
	cb->compblock = cb->compblock_save;
}

void
udl_cmd_write_reg_1(struct udl_softc *sc, uint8_t reg, uint8_t val)
{
	udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
	udl_cmd_insert_int_1(sc, UDL_BULK_CMD_REG_WRITE_1);
	udl_cmd_insert_int_1(sc, reg);
	udl_cmd_insert_int_1(sc, val);
}

void
udl_cmd_write_reg_3(struct udl_softc *sc, uint8_t reg, uint32_t val)
{
	udl_cmd_write_reg_1(sc, reg + 0, (val >> 16) & 0xff);
	udl_cmd_write_reg_1(sc, reg + 1, (val >> 8) & 0xff);
	udl_cmd_write_reg_1(sc, reg + 2, (val >> 0) & 0xff);
}

usbd_status
udl_cmd_send(struct udl_softc *sc)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	struct udl_cmd_xfer *cx = &sc->sc_cmd_xfer[0];
	int len;
	usbd_status error;

	/* mark end of command stack */
	udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
	udl_cmd_insert_int_1(sc, UDL_BULK_CMD_EOC);

	bcopy(cb->buf, cx->buf, cb->off);

	len = cb->off;
	usbd_setup_xfer(cx->xfer, sc->sc_tx_pipeh, 0, cx->buf, len,
	    USBD_NO_COPY | USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS, 1000, NULL);
	error = usbd_transfer(cx->xfer);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(error));
		/* we clear our buffer now to avoid growing out of bounds */
		goto fail;
	}
	DPRINTF(1, "%s: %s: sent %d of %d bytes\n",
	    DN(sc), FUNC, len, cb->off);
fail:
	cb->off = 0;
	cb->compblock = 0;

	return (error);
}

usbd_status
udl_cmd_send_async(struct udl_softc *sc)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	struct udl_cmd_xfer *cx;
	usbd_status error;
	int i, s;

	/* check if command xfer queue is full */
	if (sc->sc_cmd_xfer_cnt == UDL_CMD_XFER_COUNT)
		return (USBD_IN_USE);

	s = splusb();	/* no callbacks please until accounting is done */

	/* find a free command xfer buffer */
	for (i = 0; i < UDL_CMD_XFER_COUNT; i++) {
		if (sc->sc_cmd_xfer[i].busy == 0)
			break;
	}
	if (i == UDL_CMD_XFER_COUNT) {
		/* this shouldn't happen */
		splx(s);
		return (USBD_IN_USE);
	}
	cx = &sc->sc_cmd_xfer[i];

	/* mark end of command stack */
	udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
	udl_cmd_insert_int_1(sc, UDL_BULK_CMD_EOC);

	/* copy command buffer to xfer buffer */
	bcopy(cb->buf, cx->buf, cb->off);

	/* do xfer */
	usbd_setup_xfer(cx->xfer, sc->sc_tx_pipeh, cx, cx->buf, cb->off,
	     USBD_NO_COPY, 1000, udl_cmd_send_async_cb);
	error = usbd_transfer(cx->xfer);
	if (error != 0 && error != USBD_IN_PROGRESS) {
		printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(error));
		splx(s);
		return (error);
	}
	DPRINTF(2, "%s: %s: sending %d bytes from buffer no. %d\n",
	    DN(sc), FUNC, cb->off, i);

	/* free command buffer, lock xfer buffer */
	cb->off = 0;
	cb->compblock = 0;
	cx->busy = 1;
	sc->sc_cmd_xfer_cnt++;

	splx(s);

	return (USBD_NORMAL_COMPLETION);
}

void
udl_cmd_send_async_cb(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct udl_cmd_xfer *cx = priv;
	struct udl_softc *sc = cx->sc;
	int len;

	if (status != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: %s!\n", DN(sc), FUNC, usbd_errstr(status));

		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_tx_pipeh);
		goto skip;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	DPRINTF(3, "%s: %s: sent %d bytes\n", DN(sc), FUNC, len);
skip:
	/* free xfer buffer */
	cx->busy = 0;
	sc->sc_cmd_xfer_cnt--;

	/* wakeup UDLIO_DAMAGE if it sleeps for a free xfer buffer */
	wakeup(sc);
}

/* ---------- */

usbd_status
udl_init_chip(struct udl_softc *sc)
{
	uint8_t ui8;
	uint32_t ui32;
	usbd_status error;

	error = udl_poll(sc, &ui32);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);
	DPRINTF(1, "%s: %s: poll=0x%08x\n", DN(sc), FUNC, ui32);

	/* Some products may use later chip too */
	switch (ui32 & 0xff) {
	case 0xf1:				/* DL1x5 */
		switch (sc->sc_chip) {
		case DL120:
			sc->sc_chip = DL125;
			break;
		case DL160:
			sc->sc_chip = DL165;
			break;
		}
		break;
	}
	DPRINTF(1, "%s: %s: chip %d\n", DN(sc), FUNC, sc->sc_chip);

	error = udl_read_1(sc, 0xc484, &ui8);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);
	DPRINTF(1, "%s: %s: read 0x%02x from 0xc484\n", DN(sc), FUNC, ui8);

	error = udl_write_1(sc, 0xc41f, 0x01);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);
	DPRINTF(1, "%s: %s: write 0x01 to 0xc41f\n", DN(sc), FUNC);

	error = udl_read_edid(sc, sc->sc_edid);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);
	DPRINTF(1, "%s: %s: read EDID\n", DN(sc), FUNC);

	error = udl_set_enc_key(sc, udl_null_key_1, sizeof(udl_null_key_1));
	if (error != USBD_NORMAL_COMPLETION)
		return (error);
	DPRINTF(1, "%s: %s: set encryption key\n", DN(sc), FUNC);

	error = udl_write_1(sc, 0xc40b, 0x00);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);
	DPRINTF(1, "%s: %s: write 0x00 to 0xc40b\n", DN(sc), FUNC);

	error = udl_set_decomp_table(sc, udl_decomp_table,
	    sizeof(udl_decomp_table));
	if (error != USBD_NORMAL_COMPLETION)
		return (error);
	DPRINTF(1, "%s: %s: set decompression table\n", DN(sc), FUNC);

	return (USBD_NORMAL_COMPLETION);
}

void
udl_init_fb_offsets(struct udl_softc *sc, uint32_t start16, uint32_t stride16,
    uint32_t start8, uint32_t stride8)
{
	udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0x00);
	udl_cmd_write_reg_3(sc, UDL_REG_ADDR_START16, start16);
	udl_cmd_write_reg_3(sc, UDL_REG_ADDR_STRIDE16, stride16);
	udl_cmd_write_reg_3(sc, UDL_REG_ADDR_START8, start8);
	udl_cmd_write_reg_3(sc, UDL_REG_ADDR_STRIDE8, stride8);
	udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);
}

usbd_status
udl_init_resolution(struct udl_softc *sc)
{
	int i;
	usbd_status error;
	uint8_t *buf = udl_modes[sc->sc_cur_mode].mode;

	/* write resolution values and set video memory offsets */
	udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0x00);
	for (i = 0; i < UDL_MODE_SIZE; i++)
		udl_cmd_write_reg_1(sc, i, buf[i]);
	udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);

	udl_init_fb_offsets(sc, 0x000000, 0x000a00, 0x555555, 0x000500);
	error = udl_cmd_send(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* clear screen */
	error = udl_clear_screen(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* show framebuffer content */
	udl_cmd_write_reg_1(sc, UDL_REG_SCREEN, UDL_REG_SCREEN_ON);
	udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);
	error = udl_cmd_send(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
udl_clear_screen(struct udl_softc *sc)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	usbd_status error;

	/* clear screen */
	udl_fb_block_write(sc, 0x0000, 0, 0, sc->sc_width, sc->sc_height);
	if (cb->xfer_type == UDL_CMD_XFER_ASYNC)
		error = udl_cmd_send_async(sc);
	else
		error = udl_cmd_send(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	return (USBD_NORMAL_COMPLETION);
}

void
udl_select_mode(struct udl_softc *sc)
{
	struct udl_mode mode;
	int index = MAX_DL_MODES, i;

	/* try to get the preferred mode from EDID */
	edid_parse(DN(sc), sc->sc_edid, &sc->sc_edid_info);
#if defined(UDL_DEBUG) && defined(EDID_DEBUG)
	edid_print(&sc->sc_edid_info);
#endif
	if (sc->sc_edid_info.edid_preferred_mode != NULL) {
		mode.freq =
		    (sc->sc_edid_info.edid_preferred_mode->dot_clock * 1000) /
		    (sc->sc_edid_info.edid_preferred_mode->htotal *
		     sc->sc_edid_info.edid_preferred_mode->vtotal);
		mode.clock = 
		    sc->sc_edid_info.edid_preferred_mode->dot_clock / 10;
		mode.hdisplay =
		    sc->sc_edid_info.edid_preferred_mode->hdisplay;
		mode.vdisplay =
		    sc->sc_edid_info.edid_preferred_mode->vdisplay;
		index = udl_lookup_mode(mode.hdisplay, mode.vdisplay, mode.freq,
		    sc->sc_chip, mode.clock);
		sc->sc_cur_mode = index;
	} else {
		DPRINTF(1, "%s: %s: no preferred mode found!\n", DN(sc), FUNC);
	}

	if (index == MAX_DL_MODES) {
		DPRINTF(1, "%s: %s: no mode line found for %dx%d @ %dHz!\n",
		    DN(sc), FUNC, mode.hdisplay, mode.vdisplay, mode.freq);

		i = 0;
		while (i < sc->sc_edid_info.edid_nmodes) {
			mode.freq =
			    (sc->sc_edid_info.edid_modes[i].dot_clock * 1000) /
			    (sc->sc_edid_info.edid_modes[i].htotal *
			     sc->sc_edid_info.edid_modes[i].vtotal);
			mode.clock = 
			    sc->sc_edid_info.edid_modes[i].dot_clock / 10;
			mode.hdisplay =
			    sc->sc_edid_info.edid_modes[i].hdisplay;
			mode.vdisplay =
			    sc->sc_edid_info.edid_modes[i].vdisplay;
			index = udl_lookup_mode(mode.hdisplay, mode.vdisplay,
			    mode.freq, sc->sc_chip, mode.clock);
			if (index < MAX_DL_MODES)
				if ((sc->sc_cur_mode == MAX_DL_MODES) ||
				    (index > sc->sc_cur_mode))
					sc->sc_cur_mode = index;
			i++;
		}
	}

	/*
	 * If no mode found use default.
	 */
	if (sc->sc_cur_mode == MAX_DL_MODES)
		sc->sc_cur_mode = udl_lookup_mode(800, 600, 60, sc->sc_chip, 0);

	mode = udl_modes[sc->sc_cur_mode];
	sc->sc_width = mode.hdisplay;
	sc->sc_height = mode.vdisplay;

	/*
	 * We always use 16bit color depth for now.
	 */
	sc->sc_depth = 16;

	DPRINTF(1, "%s: %s: %dx%d @ %dHz\n",
	    DN(sc), FUNC, mode.hdisplay, mode.vdisplay, mode.freq);
}

int
udl_fb_buf_write(struct udl_softc *sc, uint8_t *buf, uint32_t x,
    uint32_t y, uint16_t width)
{
	uint16_t lwidth;
	uint32_t off;
	int r;

	r = udl_cmd_insert_check(sc, UDL_CMD_WRITE_MAX_SIZE);
	if (r != 0)
		return (r);

	off = ((y * sc->sc_width) + x) * 2;
	lwidth = width * 2;

	udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
	udl_cmd_insert_int_1(sc, UDL_BULK_CMD_FB_WRITE | UDL_BULK_CMD_FB_WORD);
	udl_cmd_insert_int_3(sc, off);
	udl_cmd_insert_int_1(sc, width >= UDL_CMD_MAX_PIXEL_COUNT ? 0 : width);

	udl_cmd_insert_buf(sc, buf, lwidth);

	return (0);
}

int
udl_fb_block_write(struct udl_softc *sc, uint16_t rgb16, uint32_t x,
    uint32_t y, uint32_t width, uint32_t height)
{
	uint32_t i;
	int r;

	for (i = 0; i < height; i++) {
		r = udl_fb_line_write(sc, rgb16, x, y + i, width);
		if (r != 0)
			return (r);
	}

	return (0);
}

int
udl_fb_line_write(struct udl_softc *sc, uint16_t rgb16, uint32_t x,
    uint32_t y, uint32_t width)
{
	uint32_t off, block;
	int r;

	off = (y * sc->sc_width) + x;

	while (width) {
		if (width > UDL_CMD_MAX_PIXEL_COUNT)	
			block = UDL_CMD_MAX_PIXEL_COUNT;
		else
			block = width;

		r = udl_fb_off_write(sc, rgb16, off, block);
		if (r != 0)
			return (r);

		off += block;
		width -= block;
	}

	return (0);
}

int
udl_fb_off_write(struct udl_softc *sc, uint16_t rgb16, uint32_t off,
    uint16_t width)
{
	uint8_t buf[UDL_CMD_MAX_DATA_SIZE];
	uint16_t lwidth, lrgb16;
	uint32_t loff;
	int i, r;

	r = udl_cmd_insert_check(sc, UDL_CMD_WRITE_MAX_SIZE);
	if (r != 0)
		return (r);

	loff = off * 2;
	lwidth = width * 2;

	udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
	udl_cmd_insert_int_1(sc, UDL_BULK_CMD_FB_WRITE | UDL_BULK_CMD_FB_WORD);
	udl_cmd_insert_int_3(sc, loff);
	udl_cmd_insert_int_1(sc, width >= UDL_CMD_MAX_PIXEL_COUNT ? 0 : width);

	for (i = 0; i < lwidth; i += 2) {
		lrgb16 = htobe16(rgb16);
		bcopy(&lrgb16, buf + i, 2);
	}

	udl_cmd_insert_buf(sc, buf, lwidth);

	return (0);
}

int
udl_fb_block_copy(struct udl_softc *sc, uint32_t src_x, uint32_t src_y,
    uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height)
{
	int i, r;

	for (i = 0; i < height; i++) {
		r = udl_fb_line_copy(sc, src_x, src_y + i, dst_x, dst_y + i,
		    width);
		if (r != 0)
			return (r);
	}

	return (0);
}


int
udl_fb_line_copy(struct udl_softc *sc, uint32_t src_x, uint32_t src_y,
    uint32_t dst_x, uint32_t dst_y, uint32_t width)
{
	uint32_t src_off, dst_off, block;
	int r;

	src_off = (src_y * sc->sc_width) + src_x;
	dst_off = (dst_y * sc->sc_width) + dst_x;

	while (width) {
		if (width > UDL_CMD_MAX_PIXEL_COUNT)
			block = UDL_CMD_MAX_PIXEL_COUNT;
		else
			block = width;

		r = udl_fb_off_copy(sc, src_off, dst_off, block);
		if (r != 0)
			return (r);

		src_off += block;
		dst_off += block;
		width -= block;
	}

	return (0);
}

int
udl_fb_off_copy(struct udl_softc *sc, uint32_t src_off, uint32_t dst_off,
    uint16_t width)
{
	uint32_t ldst_off, lsrc_off;
	int r;

	r = udl_cmd_insert_check(sc, UDL_CMD_COPY_MAX_SIZE);
	if (r != 0)
		return (r);

	ldst_off = dst_off * 2;
	lsrc_off = src_off * 2;

	udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
	udl_cmd_insert_int_1(sc, UDL_BULK_CMD_FB_COPY | UDL_BULK_CMD_FB_WORD);
	udl_cmd_insert_int_3(sc, ldst_off);
	udl_cmd_insert_int_1(sc, width >= UDL_CMD_MAX_PIXEL_COUNT ? 0 : width);
	udl_cmd_insert_int_3(sc, lsrc_off);

	return (0);
}

int
udl_fb_buf_write_comp(struct udl_softc *sc, uint8_t *buf, uint32_t x,
    uint32_t y, uint16_t width)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	uint8_t *count;
	uint16_t lwidth;
	uint32_t off;
	int r, sent;

	r = udl_cmd_insert_check(sc, UDL_CMD_WRITE_MAX_SIZE);
	if (r != 0)
		return (r);

	off = ((y * sc->sc_width) + x) * 2;
	lwidth = width * 2;

	/*
	 * A new compressed stream needs the 0xff-into-UDL_REG_SYNC-register
	 * sequence always as first command.
	 */
	if (cb->off == 0)
		udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);

	r = sent = 0;
	while (sent < lwidth) {
		udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
		udl_cmd_insert_int_1(sc,
		    UDL_BULK_CMD_FB_WRITE |
		    UDL_BULK_CMD_FB_WORD |
		    UDL_BULK_CMD_FB_COMP);
		udl_cmd_insert_int_3(sc, off + sent);
		udl_cmd_insert_int_1(sc,
		    width >= UDL_CMD_MAX_PIXEL_COUNT ? 0 : width);
		cb->compblock += UDL_CMD_WRITE_HEAD_SIZE;

		count = &cb->buf[cb->off - 1];
		r = udl_cmd_insert_buf_comp(sc, buf + sent, lwidth - sent);
		if (r > 0 && r != (lwidth - sent)) {
			*count = r / 2;
			width -= r / 2;
		}
		sent += r;
	}

	return (0);
}

int
udl_fb_block_write_comp(struct udl_softc *sc, uint16_t rgb16, uint32_t x,
    uint32_t y, uint32_t width, uint32_t height)
{
	uint32_t i;
	int r;

	for (i = 0; i < height; i++) {
		r = udl_fb_line_write_comp(sc, rgb16, x, y + i, width);
		if (r != 0)
			return (r);
	}

	return (0);
}

int
udl_fb_line_write_comp(struct udl_softc *sc, uint16_t rgb16, uint32_t x,
    uint32_t y, uint32_t width)
{
	uint32_t off, block;
	int r;

	off = (y * sc->sc_width) + x;

	while (width) {
		if (width > UDL_CMD_MAX_PIXEL_COUNT)	
			block = UDL_CMD_MAX_PIXEL_COUNT;
		else
			block = width;

		r = udl_fb_off_write_comp(sc, rgb16, off, block);
		if (r != 0)
			return (r);

		off += block;
		width -= block;
	}

	return (0);
}

int
udl_fb_off_write_comp(struct udl_softc *sc, uint16_t rgb16, uint32_t off,
    uint16_t width)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	uint8_t buf[UDL_CMD_MAX_DATA_SIZE];
	uint8_t *count;
	uint16_t lwidth, lrgb16;
	uint32_t loff;
	int i, r, sent;

	r = udl_cmd_insert_check(sc, UDL_CMD_WRITE_MAX_SIZE);
	if (r != 0)
		return (r);

	loff = off * 2;
	lwidth = width * 2;

	for (i = 0; i < lwidth; i += 2) {
		lrgb16 = htobe16(rgb16);
		bcopy(&lrgb16, buf + i, 2);
	}

	/*
	 * A new compressed stream needs the 0xff-into-UDL_REG_SYNC-register
	 * sequence always as first command.
	 */
	if (cb->off == 0)
		udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);

	r = sent = 0;
	while (sent < lwidth) {
		udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
		udl_cmd_insert_int_1(sc,
		    UDL_BULK_CMD_FB_WRITE |
		    UDL_BULK_CMD_FB_WORD |
		    UDL_BULK_CMD_FB_COMP);
		udl_cmd_insert_int_3(sc, loff + sent);
		udl_cmd_insert_int_1(sc,
		    width >= UDL_CMD_MAX_PIXEL_COUNT ? 0 : width);
		cb->compblock += UDL_CMD_WRITE_HEAD_SIZE;

		count = &cb->buf[cb->off - 1];
		r = udl_cmd_insert_buf_comp(sc, buf + sent, lwidth - sent);
		if (r > 0 && r != (lwidth - sent)) {
			*count = r / 2;
			width -= r / 2;
		}
		sent += r;
	}

	return (0);
}

int
udl_fb_block_copy_comp(struct udl_softc *sc, uint32_t src_x, uint32_t src_y,
    uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height)
{
	int i, r;

	for (i = 0; i < height; i++) {
		r = udl_fb_line_copy_comp(sc, src_x, src_y + i,
		    dst_x, dst_y + i, width);
		if (r != 0)
			return (r);
	}

	return (0);
}

int
udl_fb_line_copy_comp(struct udl_softc *sc, uint32_t src_x, uint32_t src_y,
    uint32_t dst_x, uint32_t dst_y, uint32_t width)
{
	uint32_t src_off, dst_off, block;
	int r;

	src_off = (src_y * sc->sc_width) + src_x;
	dst_off = (dst_y * sc->sc_width) + dst_x;

	while (width) {
		if (width > UDL_CMD_MAX_PIXEL_COUNT)
			block = UDL_CMD_MAX_PIXEL_COUNT;
		else
			block = width;

		r = udl_fb_off_copy_comp(sc, src_off, dst_off, block);
		if (r != 0)
			return (r);

		src_off += block;
		dst_off += block;
		width -= block;
	}

	return (0);
}

int
udl_fb_off_copy_comp(struct udl_softc *sc, uint32_t src_off, uint32_t dst_off,
    uint16_t width)
{
	struct udl_cmd_buf *cb = &sc->sc_cmd_buf;
	uint32_t ldst_off, lsrc_off;
	int r;

	r = udl_cmd_insert_check(sc, UDL_CMD_COPY_MAX_SIZE);
	if (r != 0)
		return (r);

	ldst_off = dst_off * 2;
	lsrc_off = src_off * 2;

	/*
	 * A new compressed stream needs the 0xff-into-UDL_REG_SYNC-register
	 * sequence always as first command.
	 */
	if (cb->off == 0)
		udl_cmd_write_reg_1(sc, UDL_REG_SYNC, 0xff);

	r = 0;
	while (r < 1) {
		udl_cmd_insert_int_1(sc, UDL_BULK_SOC);
		udl_cmd_insert_int_1(sc,
		    UDL_BULK_CMD_FB_COPY | UDL_BULK_CMD_FB_WORD);
		udl_cmd_insert_int_3(sc, ldst_off);
		udl_cmd_insert_int_1(sc,
		    width >= UDL_CMD_MAX_PIXEL_COUNT ? 0 : width);
		udl_cmd_insert_int_3(sc, lsrc_off);
		cb->compblock += UDL_CMD_COPY_HEAD_SIZE;

		r = udl_cmd_insert_head_comp(sc, UDL_CMD_COPY_HEAD_SIZE);
	}

	return (0);
}

/* ---------- */
#ifdef UDL_DEBUG
void
udl_hexdump(void *buf, int len, int quiet)
{
	int i;

	for (i = 0; i < len; i++) {
		if (quiet == 0) {
			if (i % 16 == 0)
				printf("%s%5i:", i ? "\n" : "", i);
			if (i % 4 == 0)
				printf(" ");
		}
		printf("%02x", (int)*((u_char *)buf + i));
	}
	printf("\n");
}

usbd_status
udl_init_test(struct udl_softc *sc)
{
	int i, j, parts, loops;
	uint16_t color;
	uint16_t rgb24[3] = { 0xf800, 0x07e0, 0x001f };

	loops = (sc->sc_width * sc->sc_height) / UDL_CMD_MAX_PIXEL_COUNT;
	parts = loops / 3;
	color = rgb24[0];

	j = 1;
	for (i = 0; i < loops; i++) {
		if (i == parts) {
			color = rgb24[j];
			parts += parts;
			j++;
		}
		(sc->udl_fb_off_write)(sc, color, i * UDL_CMD_MAX_PIXEL_COUNT,
		    UDL_CMD_MAX_PIXEL_COUNT);
	}
	(void)udl_cmd_send(sc);

	return (USBD_NORMAL_COMPLETION);
}
#endif
