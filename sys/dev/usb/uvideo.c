/*	$OpenBSD: uvideo.c,v 1.265 2025/09/06 13:45:41 kirill Exp $ */

/*
 * Copyright (c) 2008 Robert Nagy <robert@openbsd.org>
 * Copyright (c) 2008 Marcus Glocker <mglocker@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/stat.h>
#include <sys/kthread.h>
#include <sys/stdint.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uvideo.h>

#include <dev/video_if.h>

#ifdef UVIDEO_DEBUG
int uvideo_debug = 1;
#define DPRINTF(l, x...) do { if ((l) <= uvideo_debug) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

#define DEVNAME(_s) ((_s)->sc_dev.dv_xname)

#define byteof(x) ((x) >> 3)
#define bitof(x)  (1L << ((x) & 0x7))

struct uvideo_softc {
	struct device				 sc_dev;
	struct usbd_device			*sc_udev;
	int					 sc_iface;
	int					 sc_nifaces;

	struct device				*sc_videodev;

	int					 sc_max_ctrl_size;
	int					 sc_max_fbuf_size;
	int					 sc_negotiated_flag;
	int					 sc_frame_rate;

	struct uvideo_frame_buffer		 sc_frame_buffer;

	struct uvideo_mmap			 sc_mmap[UVIDEO_MAX_BUFFERS];
	struct uvideo_mmap			*sc_mmap_cur;
	uint8_t					*sc_mmap_buffer;
	size_t					 sc_mmap_buffer_size;
	int					 sc_mmap_buffer_idx;
	q_mmap					 sc_mmap_q;
	int					 sc_mmap_count;
	int					 sc_mmap_flag;

	int					 sc_nframes;
	struct usb_video_probe_commit		 sc_desc_probe;
	struct usb_video_header_desc_all	 sc_desc_vc_header;
	struct usb_video_input_header_desc_all	 sc_desc_vs_input_header;

#define UVIDEO_MAX_PU				 8
	int					 sc_desc_vc_pu_num;
	struct usb_video_vc_processing_desc	*sc_desc_vc_pu_cur;
	struct usb_video_vc_processing_desc	*sc_desc_vc_pu[UVIDEO_MAX_PU];

#define UVIDEO_MAX_FORMAT			 8
	int					 sc_fmtgrp_idx;
	int					 sc_fmtgrp_num;
	struct uvideo_format_group		*sc_fmtgrp_cur;
	struct uvideo_format_group		 sc_fmtgrp[UVIDEO_MAX_FORMAT];

#define	UVIDEO_MAX_VS_NUM			 8
	struct uvideo_vs_iface			*sc_vs_cur;
	struct uvideo_vs_iface			 sc_vs_coll[UVIDEO_MAX_VS_NUM];

	void					*sc_uplayer_arg;
	int					*sc_uplayer_fsize;
	uint8_t					*sc_uplayer_fbuffer;
	void					 (*sc_uplayer_intr)(void *);

	const struct uvideo_devs		*sc_quirk;
	void					(*sc_decode_stream_header)
						    (struct uvideo_softc *,
						    uint8_t *, int);
};

int		uvideo_open(void *, int, int *, uint8_t *, void (*)(void *),
		    void *);
int		uvideo_close(void *);
int		uvideo_match(struct device *, void *, void *);
void		uvideo_attach(struct device *, struct device *, void *);
void		uvideo_attach_hook(struct device *);
int		uvideo_detach(struct device *, int);

usbd_status	uvideo_vc_parse_desc(struct uvideo_softc *);
usbd_status	uvideo_vc_parse_desc_header(struct uvideo_softc *,
		    const usb_descriptor_t *);
usbd_status	uvideo_vc_parse_desc_pu(struct uvideo_softc *,
		    const usb_descriptor_t *);
usbd_status	uvideo_vc_get_ctrl(struct uvideo_softc *, uint8_t *, uint8_t,
		    uint8_t, uint16_t, uint16_t);
usbd_status	uvideo_vc_set_ctrl(struct uvideo_softc *, uint8_t *, uint8_t,
		    uint8_t, uint16_t, uint16_t);
int		uvideo_find_ctrl(struct uvideo_softc *, int);
int		uvideo_has_ctrl(struct usb_video_vc_processing_desc *, int);

usbd_status	uvideo_vs_parse_desc(struct uvideo_softc *,
		    usb_config_descriptor_t *);
usbd_status	uvideo_vs_parse_desc_input_header(struct uvideo_softc *,
		    const usb_descriptor_t *);
usbd_status	uvideo_vs_parse_desc_format(struct uvideo_softc *);
void		uvideo_vs_parse_desc_colorformat(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_vs_parse_desc_format_frame_based(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_vs_parse_desc_format_h264(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_vs_parse_desc_format_mjpeg(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_vs_parse_desc_format_uncompressed(struct uvideo_softc *,
		    const usb_descriptor_t *);
usbd_status	uvideo_vs_parse_desc_frame(struct uvideo_softc *);
usbd_status	uvideo_vs_parse_desc_frame_buffer_size(struct uvideo_softc *,
		    const usb_descriptor_t *);
usbd_status	uvideo_vs_parse_desc_frame_max_rate(struct uvideo_softc *,
		    const usb_descriptor_t *);
uint32_t	uvideo_vc_parse_max_packet_size(struct uvideo_softc *,
		    usb_endpoint_descriptor_t *);
usbd_status	uvideo_vs_parse_desc_alt(struct uvideo_softc *, int, int, int);
usbd_status	uvideo_vs_set_alt(struct uvideo_softc *,
		    struct usbd_interface *, int);
int		uvideo_desc_len(const usb_descriptor_t *, int, int, int, int);
void		uvideo_find_res(struct uvideo_softc *, int, int, int,
		    struct uvideo_res *);
usbd_status	uvideo_vs_negotiation(struct uvideo_softc *, int);
usbd_status	uvideo_vs_set_probe(struct uvideo_softc *, uint8_t *);
usbd_status	uvideo_vs_get_probe(struct uvideo_softc *, uint8_t *, uint8_t);
usbd_status	uvideo_vs_set_commit(struct uvideo_softc *, uint8_t *);
usbd_status	uvideo_vs_alloc_frame(struct uvideo_softc *);
void		uvideo_vs_free_frame(struct uvideo_softc *);
usbd_status	uvideo_vs_alloc_isoc(struct uvideo_softc *);
usbd_status	uvideo_vs_alloc_bulk(struct uvideo_softc *);
void		uvideo_vs_free_isoc(struct uvideo_softc *);
void		uvideo_vs_free_bulk(struct uvideo_softc *);
usbd_status	uvideo_vs_open(struct uvideo_softc *);
void		uvideo_vs_close(struct uvideo_softc *);
usbd_status	uvideo_vs_init(struct uvideo_softc *);
int		uvideo_vs_start_bulk(struct uvideo_softc *);
void		uvideo_vs_start_bulk_thread(void *);
void		uvideo_vs_start_isoc(struct uvideo_softc *);
void		uvideo_vs_start_isoc_ixfer(struct uvideo_softc *,
		    struct uvideo_isoc_xfer *);
void		uvideo_vs_cb(struct usbd_xfer *, void *,
		    usbd_status);
void		uvideo_vs_decode_stream_header(struct uvideo_softc *,
		    uint8_t *, int);
void		uvideo_vs_decode_stream_header_isight(struct uvideo_softc *,
		    uint8_t *, int);
uint8_t *	uvideo_mmap_getbuf(struct uvideo_softc *);
void		uvideo_mmap_queue(struct uvideo_softc *, int, int);
void		uvideo_read(struct uvideo_softc *, uint8_t *, int);
usbd_status	uvideo_usb_control(struct uvideo_softc *, uint8_t, uint8_t,
		    uint16_t, uint8_t *, size_t);

#ifdef UVIDEO_DEBUG
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>

void		uvideo_dump_desc_all(struct uvideo_softc *);
void		uvideo_dump_desc_vc_header(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_input_header(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_input(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_output(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_endpoint(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_endpoint_ss_comp(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_iface_assoc(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_interface(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_config(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_cs_endpoint(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_colorformat(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_format_mjpeg(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_format_uncompressed(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_format_h264(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_format_frame_based(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_frame(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_h264_frame(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_frame_based_frame(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_processing(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_dump_desc_extension(struct uvideo_softc *,
		    const usb_descriptor_t *);
void		uvideo_hexdump(void *, int, int);
#endif

/*
 * IOCTL's
 */
int		uvideo_querycap(void *, struct v4l2_capability *);
int		uvideo_enum_fmt(void *, struct v4l2_fmtdesc *);
int		uvideo_enum_fsizes(void *, struct v4l2_frmsizeenum *);
int		uvideo_enum_fivals(void *, struct v4l2_frmivalenum *);
int		uvideo_s_fmt(void *, struct v4l2_format *);
int		uvideo_g_fmt(void *, struct v4l2_format *);
int		uvideo_s_parm(void *, struct v4l2_streamparm *);
int		uvideo_g_parm(void *, struct v4l2_streamparm *);
int		uvideo_enum_input(void *, struct v4l2_input *);
int		uvideo_s_input(void *, int);
int		uvideo_g_input(void *, int *);
int		uvideo_reqbufs(void *, struct v4l2_requestbuffers *);
int		uvideo_querybuf(void *, struct v4l2_buffer *);
int		uvideo_qbuf(void *, struct v4l2_buffer *);
int		uvideo_dqbuf(void *, struct v4l2_buffer *);
int		uvideo_streamon(void *, int);
int		uvideo_streamoff(void *, int);
int		uvideo_try_fmt(void *, struct v4l2_format *);
int		uvideo_queryctrl(void *, struct v4l2_queryctrl *);
int		uvideo_g_ctrl(void *, struct v4l2_control *);
int		uvideo_s_ctrl(void *, struct v4l2_control *);

/*
 * Other hardware interface related functions
 */
caddr_t		uvideo_mappage(void *, off_t, int);
int		uvideo_get_bufsize(void *);
int		uvideo_start_read(void *);

/*
 * Firmware
 */
usbd_status	uvideo_ucode_loader_ricoh(struct uvideo_softc *);
usbd_status	uvideo_ucode_loader_apple_isight(struct uvideo_softc *);

struct cfdriver uvideo_cd = {
	NULL, "uvideo", DV_DULL
};

const struct cfattach uvideo_ca = {
	sizeof(struct uvideo_softc), uvideo_match, uvideo_attach, uvideo_detach
};

const struct video_hw_if uvideo_hw_if = {
	uvideo_open,		/* open */
	uvideo_close,		/* close */
	uvideo_querycap,	/* VIDIOC_QUERYCAP */
	uvideo_enum_fmt,	/* VIDIOC_ENUM_FMT */
	uvideo_enum_fsizes,	/* VIDIOC_ENUM_FRAMESIZES */
	uvideo_enum_fivals,	/* VIDIOC_ENUM_FRAMEINTERVALS */
	uvideo_s_fmt,		/* VIDIOC_S_FMT */
	uvideo_g_fmt,		/* VIDIOC_G_FMT */
	uvideo_s_parm,		/* VIDIOC_S_PARM */
	uvideo_g_parm,		/* VIDIOC_G_PARM */
	uvideo_enum_input,	/* VIDIOC_ENUMINPUT */
	uvideo_s_input,		/* VIDIOC_S_INPUT */
	uvideo_g_input,		/* VIDIOC_G_INPUT */
	uvideo_reqbufs,		/* VIDIOC_REQBUFS */
	uvideo_querybuf,	/* VIDIOC_QUERYBUF */
	uvideo_qbuf,		/* VIDIOC_QBUF */
	uvideo_dqbuf,		/* VIDIOC_DQBUF */
	uvideo_streamon,	/* VIDIOC_STREAMON */
	uvideo_streamoff,	/* VIDIOC_STREAMOFF */
	uvideo_try_fmt,		/* VIDIOC_TRY_FMT */
	uvideo_queryctrl,	/* VIDIOC_QUERYCTRL */
	uvideo_g_ctrl,		/* VIDIOC_G_CTRL */
	uvideo_s_ctrl,		/* VIDIOC_S_CTRL */
	uvideo_mappage,		/* mmap */
	uvideo_get_bufsize,	/* read */
	uvideo_start_read	/* start stream for read */
};

/*
 * Devices which either fail to declare themselves as UICLASS_VIDEO,
 * or which need firmware uploads or other quirk handling later on.
 */
#define UVIDEO_FLAG_ISIGHT_STREAM_HEADER	0x01
#define UVIDEO_FLAG_REATTACH			0x02
#define UVIDEO_FLAG_VENDOR_CLASS		0x04
#define UVIDEO_FLAG_NOATTACH			0x08
#define UVIDEO_FLAG_FORMAT_INDEX_IN_BMHINT	0x10
const struct uvideo_devs {
	struct usb_devno	 uv_dev;
	char			*ucode_name;
	usbd_status		 (*ucode_loader)(struct uvideo_softc *);
	int			 flags;
} uvideo_devs[] = {
	{
	    /* Needs firmware */
	    { USB_VENDOR_RICOH, USB_PRODUCT_RICOH_VGPVCC5 },
	    "uvideo_r5u87x_05ca-1835",
	    uvideo_ucode_loader_ricoh,
	    0
	},
	{
	    /* Needs firmware */
	    { USB_VENDOR_RICOH, USB_PRODUCT_RICOH_VGPVCC4 },
	    "uvideo_r5u87x_05ca-1836",
	    uvideo_ucode_loader_ricoh,
	    0
	},
	{
	    /* Needs firmware */
	    { USB_VENDOR_RICOH, USB_PRODUCT_RICOH_VGPVCC4_2 },
	    "uvideo_r5u87x_05ca-1837",
	    uvideo_ucode_loader_ricoh,
	    0
	},
	{
	    /* Needs firmware */
	    { USB_VENDOR_RICOH, USB_PRODUCT_RICOH_VGPVCC6 },
	    "uvideo_r5u87x_05ca-1839",
	    uvideo_ucode_loader_ricoh,
	    0
	},
	{
	    /* Needs firmware */
	    { USB_VENDOR_RICOH, USB_PRODUCT_RICOH_VGPVCC7 },
	    "uvideo_r5u87x_05ca-183a",
	    uvideo_ucode_loader_ricoh,
	    0
	},
	{
	    /* Needs firmware */
	    { USB_VENDOR_RICOH, USB_PRODUCT_RICOH_VGPVCC8 },
	    "uvideo_r5u87x_05ca-183b",
	    uvideo_ucode_loader_ricoh,
	    0
	},
	{
	    /* Needs firmware */
	    { USB_VENDOR_RICOH, USB_PRODUCT_RICOH_VGPVCC9 },
	    "uvideo_r5u87x_05ca-183e",
	    uvideo_ucode_loader_ricoh,
	    0
	},
	{
	    /* Needs firmware */
	    { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_BLUETOOTH },
	    "uvideo_isight_05ac-8300",
	    uvideo_ucode_loader_apple_isight,
	    UVIDEO_FLAG_REATTACH
	},
	{
	    /* Has a non-standard streaming header protocol */
	    { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_ISIGHT_1 },
	    NULL,
	    NULL,
	    UVIDEO_FLAG_ISIGHT_STREAM_HEADER
	},
	{   /* Incorrectly reports as bInterfaceClass=UICLASS_VENDOR */
	    { USB_VENDOR_LOGITECH, USB_PRODUCT_LOGITECH_QUICKCAMOEM_1 },
	    NULL,
	    NULL,
	    UVIDEO_FLAG_VENDOR_CLASS
	},
	{   /* Infrared camera not supported */
	    { USB_VENDOR_CHICONY, USB_PRODUCT_CHICONY_IRCAMERA },
	    NULL,
	    NULL,
	    UVIDEO_FLAG_NOATTACH
	},
	{   /* Has incorrect control response */
	    { USB_VENDOR_ELGATO, USB_PRODUCT_ELGATO_HD60 },
	    NULL,
	    NULL,
	    UVIDEO_FLAG_FORMAT_INDEX_IN_BMHINT
	},
};
#define uvideo_lookup(v, p) \
	((const struct uvideo_devs *)usb_lookup(uvideo_devs, v, p))

const struct uvideo_map_fmts {
	uint8_t		guidFormat[16];
	uint32_t	pixelformat;
} uvideo_map_fmts[] = {
	{ UVIDEO_FORMAT_GUID_YUY2, V4L2_PIX_FMT_YUYV },
	{ UVIDEO_FORMAT_GUID_YV12, V4L2_PIX_FMT_YVU420 },
	{ UVIDEO_FORMAT_GUID_I420, V4L2_PIX_FMT_YUV420 },
	{ UVIDEO_FORMAT_GUID_Y800, V4L2_PIX_FMT_GREY },
	{ UVIDEO_FORMAT_GUID_Y8, V4L2_PIX_FMT_GREY },
	{ UVIDEO_FORMAT_GUID_D3DFMT_L8, V4L2_PIX_FMT_GREY },
	{ UVIDEO_FORMAT_GUID_KSMEDIA_L8_IR, V4L2_PIX_FMT_GREY },
	{ UVIDEO_FORMAT_GUID_BY8, V4L2_PIX_FMT_SBGGR8 },
	{ UVIDEO_FORMAT_GUID_BA81, V4L2_PIX_FMT_SBGGR8 },
	{ UVIDEO_FORMAT_GUID_GBRG, V4L2_PIX_FMT_SGBRG8 },
	{ UVIDEO_FORMAT_GUID_GRBG, V4L2_PIX_FMT_SGRBG8 },
	{ UVIDEO_FORMAT_GUID_RGGB, V4L2_PIX_FMT_SRGGB8 },
	{ UVIDEO_FORMAT_GUID_RGBP, V4L2_PIX_FMT_RGB565 },
	{ UVIDEO_FORMAT_GUID_D3DFMT_R5G6B5, V4L2_PIX_FMT_RGB565 },
	{ UVIDEO_FORMAT_GUID_BGR3, V4L2_PIX_FMT_BGR24 },
	{ UVIDEO_FORMAT_GUID_BGR4, V4L2_PIX_FMT_XBGR32 },
	{ UVIDEO_FORMAT_GUID_H265, V4L2_PIX_FMT_HEVC },
	{ UVIDEO_FORMAT_GUID_RW10, V4L2_PIX_FMT_SRGGB10P },
	{ UVIDEO_FORMAT_GUID_BG16, V4L2_PIX_FMT_SBGGR16 },
	{ UVIDEO_FORMAT_GUID_GB16, V4L2_PIX_FMT_SGBRG16 },
	{ UVIDEO_FORMAT_GUID_RG16, V4L2_PIX_FMT_SRGGB16 },
	{ UVIDEO_FORMAT_GUID_GR16, V4L2_PIX_FMT_SGRBG16 },
	{ UVIDEO_FORMAT_GUID_INVZ, V4L2_PIX_FMT_Z16 },
	{ UVIDEO_FORMAT_GUID_INVI, V4L2_PIX_FMT_Y10 },
};

const enum v4l2_colorspace uvideo_color_primaries[] = {
	V4L2_COLORSPACE_SRGB,		/* Unspecified */
	V4L2_COLORSPACE_SRGB,
	V4L2_COLORSPACE_470_SYSTEM_M,
	V4L2_COLORSPACE_470_SYSTEM_BG,
	V4L2_COLORSPACE_SMPTE170M,
	V4L2_COLORSPACE_SMPTE240M,
};

const enum v4l2_xfer_func uvideo_xfer_characteristics[] = {
	V4L2_XFER_FUNC_DEFAULT,		/* Unspecified */
	V4L2_XFER_FUNC_709,
	V4L2_XFER_FUNC_709,		/* Substitution for BT.470-2 M */
	V4L2_XFER_FUNC_709,		/* Substitution for BT.470-2 B, G */
	V4L2_XFER_FUNC_709,		/* Substitution for SMPTE 170M */
	V4L2_XFER_FUNC_SMPTE240M,
	V4L2_XFER_FUNC_NONE,
	V4L2_XFER_FUNC_SRGB,
};

const enum v4l2_ycbcr_encoding uvideo_matrix_coefficients[] = {
	V4L2_YCBCR_ENC_DEFAULT,		/* Unspecified */
	V4L2_YCBCR_ENC_709,
	V4L2_YCBCR_ENC_601,		/* Substitution for FCC */
	V4L2_YCBCR_ENC_601,		/* Substitution for BT.470-2 B, G */
	V4L2_YCBCR_ENC_601,
	V4L2_YCBCR_ENC_SMPTE240M,
};

int
uvideo_open(void *addr, int flags, int *size, uint8_t *buffer,
    void (*intr)(void *), void *arg)
{
	struct uvideo_softc *sc = addr;

	DPRINTF(1, "%s: uvideo_open: sc=%p\n", DEVNAME(sc), sc);

	if (usbd_is_dying(sc->sc_udev) || sc->sc_vs_cur == NULL)
		return (EIO);

	/* pointers to upper video layer */
	sc->sc_uplayer_arg = arg;
	sc->sc_uplayer_fsize = size;
	sc->sc_uplayer_fbuffer = buffer;
	sc->sc_uplayer_intr = intr;

	sc->sc_mmap_flag = 0;
	sc->sc_negotiated_flag = 0;

	return (0);
}

int
uvideo_close(void *addr)
{
	struct uvideo_softc *sc = addr;

	DPRINTF(1, "%s: uvideo_close: sc=%p\n", DEVNAME(sc), sc);

	if (sc->sc_vs_cur == NULL)
		return (EIO);

	/* close video stream pipe */
	uvideo_vs_close(sc);

	/* free video stream xfer buffer */
	if (sc->sc_vs_cur->bulk_endpoint)
		uvideo_vs_free_bulk(sc);
	else
		uvideo_vs_free_isoc(sc);

	/* free video stream frame buffer */
	uvideo_vs_free_frame(sc);
	return (0);
}

int
uvideo_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	const struct uvideo_devs *quirk;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	id = usbd_get_interface_descriptor(uaa->iface);
	if (id == NULL)
		return (UMATCH_NONE);

	/* quirk devices */
	quirk = uvideo_lookup(uaa->vendor, uaa->product);
	if (quirk != NULL) {
		if (quirk->flags & UVIDEO_FLAG_REATTACH)
			return (UMATCH_VENDOR_PRODUCT_CONF_IFACE);

		if (quirk->flags & UVIDEO_FLAG_VENDOR_CLASS &&
		    id->bInterfaceClass == UICLASS_VENDOR &&
		    id->bInterfaceSubClass == UISUBCLASS_VIDEOCONTROL)
			return (UMATCH_VENDOR_PRODUCT_CONF_IFACE);
	}

	if (id->bInterfaceClass == UICLASS_VIDEO &&
	    id->bInterfaceSubClass == UISUBCLASS_VIDEOCONTROL)
		return (UMATCH_VENDOR_PRODUCT_CONF_IFACE);

	return (UMATCH_NONE);
}

void
uvideo_attach(struct device *parent, struct device *self, void *aux)
{
	struct uvideo_softc *sc = (struct uvideo_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_assoc_descriptor_t *iad;
	usb_interface_descriptor_t *id;
	const usb_descriptor_t *desc;
	struct usbd_desc_iter iter;
	int i;

	sc->sc_udev = uaa->device;

	/* Find the first unclaimed video interface. */
	for (i = 0; i < uaa->nifaces; i++) {
		if (usbd_iface_claimed(sc->sc_udev, i))
			continue;
		id = usbd_get_interface_descriptor(&sc->sc_udev->ifaces[i]);
		if (id == NULL)
			continue;
		if (id->bInterfaceClass == UICLASS_VIDEO)
			break;
	}
	if (i == uaa->nifaces) {
		printf("%s: can't find video interface\n", DEVNAME(sc));
		return;
	}

	/* Find out which interface association we belong to. */
	usbd_desc_iter_init(sc->sc_udev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		if (desc->bDescriptorType != UDESC_IFACE_ASSOC) {
			desc = usbd_desc_iter_next(&iter);
			continue;
		}
		iad = (usb_interface_assoc_descriptor_t *)desc;
		if (i >= iad->bFirstInterface &&
		    i < iad->bFirstInterface + iad->bInterfaceCount)
			break;
		desc = usbd_desc_iter_next(&iter);
	}
	if (desc == NULL) {
		printf("%s: can't find interface assoc descriptor\n",
		    DEVNAME(sc));
		return;
	}

	/*
	 * Claim all interfaces of our association.  Interfaces must be
	 * claimed during attach, during attach hooks is too late.
	 */
	for (i = iad->bFirstInterface;
	    i < iad->bFirstInterface + iad->bInterfaceCount; i++) {
		if (usbd_iface_claimed(sc->sc_udev, i)) {
			printf("%s: interface already claimed\n",
			    DEVNAME(sc));
			return;
		}
		usbd_claim_iface(sc->sc_udev, i);
	}

	/* Remember our association by saving the first interface. */
	sc->sc_iface = iad->bFirstInterface;
	sc->sc_nifaces = iad->bInterfaceCount;

	/* maybe the device has quirks */
	sc->sc_quirk = uvideo_lookup(uaa->vendor, uaa->product);

	if (sc->sc_quirk && sc->sc_quirk->flags & UVIDEO_FLAG_NOATTACH) {
		printf("%s: device not supported\n", DEVNAME(sc));
		return;
	}

	if (sc->sc_quirk && sc->sc_quirk->ucode_name)
		config_mountroot(self, uvideo_attach_hook);
	else
		uvideo_attach_hook(self);
}

void
uvideo_attach_hook(struct device *self)
{
	struct uvideo_softc *sc = (struct uvideo_softc *)self;
	usb_config_descriptor_t *cdesc;
	usbd_status error;

	/* maybe the device needs a firmware */
	if (sc->sc_quirk && sc->sc_quirk->ucode_name) {
		error = (sc->sc_quirk->ucode_loader)(sc);
		if (error != USBD_NORMAL_COMPLETION)
			return;
	}

	/* map stream header decode function */
	if (sc->sc_quirk &&
	    sc->sc_quirk->flags & UVIDEO_FLAG_ISIGHT_STREAM_HEADER) {
		sc->sc_decode_stream_header =
		    uvideo_vs_decode_stream_header_isight;
	} else {
		sc->sc_decode_stream_header =
		    uvideo_vs_decode_stream_header;
	}

	/* get the config descriptor */
	cdesc = usbd_get_config_descriptor(sc->sc_udev);
	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
		    DEVNAME(sc));
		return;
	}
#ifdef UVIDEO_DEBUG
	uvideo_dump_desc_all(sc);
#endif
	/* parse video control descriptors */
	error = uvideo_vc_parse_desc(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/* parse video stream descriptors */
	error = uvideo_vs_parse_desc(sc, cdesc);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/* set default video stream interface */
	error = usbd_set_interface(sc->sc_vs_cur->ifaceh, 0);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/* do device negotiation without commit */
	error = uvideo_vs_negotiation(sc, 0);
	if (error != USBD_NORMAL_COMPLETION)
		return;

	/* init mmap queue */
	SIMPLEQ_INIT(&sc->sc_mmap_q);
	sc->sc_mmap_count = 0;

	DPRINTF(1, "uvideo_attach: doing video_attach_mi\n");
	sc->sc_videodev = video_attach_mi(&uvideo_hw_if, sc, &sc->sc_dev);
}

int
uvideo_detach(struct device *self, int flags)
{
	struct uvideo_softc *sc = (struct uvideo_softc *)self;
	int rv = 0;

	if (sc->sc_videodev != NULL)
		rv = config_detach(sc->sc_videodev, flags);

	uvideo_close(sc);

	return (rv);
}

usbd_status
uvideo_vc_parse_desc(struct uvideo_softc *sc)
{
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;
	int vc_header_found;
	usbd_status error;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	vc_header_found = 0;

	usbd_desc_iter_init(sc->sc_udev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		/* Skip all interfaces until we found our first. */
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			id = (usb_interface_descriptor_t *)desc;
			if (id->bInterfaceNumber == sc->sc_iface)
				break;
		}
		desc = usbd_desc_iter_next(&iter);
	}
	while (desc) {
		/* Crossed device function boundary. */
		if (desc->bDescriptorType == UDESC_IFACE_ASSOC)
			break;
		if (desc->bDescriptorType != UDESC_CS_INTERFACE) {
			desc = usbd_desc_iter_next(&iter);
			continue;
		}

		switch (desc->bDescriptorSubtype) {
		case UDESCSUB_VC_HEADER:
			if (!uvideo_desc_len(desc, 12, 11, 1, 0))
				break;
			if (vc_header_found) {
				printf("%s: too many VC_HEADERs!\n",
				    DEVNAME(sc));
				return (USBD_INVAL);
			}
			error = uvideo_vc_parse_desc_header(sc, desc);
			if (error != USBD_NORMAL_COMPLETION)
				return (error);
			vc_header_found = 1;
			break;
		case UDESCSUB_VC_PROCESSING_UNIT:
			/* XXX do correct length calculation */
			if (desc->bLength < UVIDEO_FRAME_MIN_LEN(desc))
				(void)uvideo_vc_parse_desc_pu(sc, desc);
			break;

		/* TODO: which VC descriptors do we need else? */
		}

		desc = usbd_desc_iter_next(&iter);
	}

	if (vc_header_found == 0) {
		printf("%s: no VC_HEADER found!\n", DEVNAME(sc));
		return (USBD_INVAL);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vc_parse_desc_header(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_header_desc *d;

	d = (struct usb_video_header_desc *)(uint8_t *)desc;

	if (d->bInCollection == 0) {
		printf("%s: no VS interface found!\n",
		    DEVNAME(sc));
		return (USBD_INVAL);
	}
	
	sc->sc_desc_vc_header.fix = d;
	sc->sc_desc_vc_header.baInterfaceNr = (uByte *)(d + 1);
	if (UGETW(d->bcdUVC) < 0x0110)
		sc->sc_max_ctrl_size = 26;
	else if (UGETW(d->bcdUVC) < 0x0150)
		sc->sc_max_ctrl_size = 34;
	else
		sc->sc_max_ctrl_size = 48;

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vc_parse_desc_pu(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_vc_processing_desc *d;

	/* PU descriptor is variable sized */
	d = (void *)desc;

	if (sc->sc_desc_vc_pu_num == UVIDEO_MAX_PU) {
		printf("%s: too many PU descriptors found!\n", DEVNAME(sc));
		return (USBD_INVAL);
	}

	sc->sc_desc_vc_pu[sc->sc_desc_vc_pu_num] = d;
	sc->sc_desc_vc_pu_num++;

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vc_get_ctrl(struct uvideo_softc *sc, uint8_t *ctrl_data,
    uint8_t request, uint8_t unitid, uint16_t ctrl_selector, uint16_t ctrl_len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UVIDEO_GET_IF;
	req.bRequest = request;
	USETW(req.wValue, (ctrl_selector << 8));
	USETW(req.wIndex, (unitid << 8));
	USETW(req.wLength, ctrl_len);

	error = usbd_do_request(sc->sc_udev, &req, ctrl_data);
	if (error) {
		DPRINTF(1, "%s: %s: could not GET ctrl request: %s\n",
		    DEVNAME(sc), __func__, usbd_errstr(error));
		return (USBD_INVAL);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vc_set_ctrl(struct uvideo_softc *sc, uint8_t *ctrl_data,
    uint8_t request, uint8_t unitid, uint16_t ctrl_selector, uint16_t ctrl_len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UVIDEO_SET_IF;
	req.bRequest = request;
	USETW(req.wValue, (ctrl_selector << 8));
	USETW(req.wIndex, (unitid << 8));
	USETW(req.wLength, ctrl_len);

	error = usbd_do_request(sc->sc_udev, &req, ctrl_data);
	if (error) {
		DPRINTF(1, "%s: %s: could not SET ctrl request: %s\n",
		    DEVNAME(sc), __func__, usbd_errstr(error));
		return (USBD_INVAL);
	}

	return (USBD_NORMAL_COMPLETION);
}

int
uvideo_find_ctrl(struct uvideo_softc *sc, int id)
{
	int i, j, found;

	if (sc->sc_desc_vc_pu_num == 0) {
		/* no processing unit descriptors found */
		DPRINTF(1, "%s: %s: no processing unit descriptors found!\n",
		    DEVNAME(sc), __func__);
		return (EINVAL);
	}

	/* do we support this control? */
	for (found = 0, i = 0; uvideo_ctrls[i].cid != 0; i++) {
		if (id == uvideo_ctrls[i].cid) {
			found = 1;
			break;
		}
	}
	if (found == 0) {
		DPRINTF(1, "%s: %s: control not supported by driver!\n",
		    DEVNAME(sc), __func__);
		return (EINVAL);
	}

	/* does the device support this control? */
	for (found = 0, j = 0; j < sc->sc_desc_vc_pu_num; j++) {
		if (uvideo_has_ctrl(sc->sc_desc_vc_pu[j],
		    uvideo_ctrls[i].ctrl_bit) != 0) {
			found = 1;
			break; 
		}
	}
	if (found == 0) {
		DPRINTF(1, "%s: %s: control not supported by device!\n",
		    DEVNAME(sc), __func__);
		return (EINVAL);
	}
	sc->sc_desc_vc_pu_cur = sc->sc_desc_vc_pu[j];

	return (i);
}

int
uvideo_has_ctrl(struct usb_video_vc_processing_desc *desc, int ctrl_bit)
{
	if (desc->bControlSize * 8 <= ctrl_bit)
		return (0);

	return (desc->bmControls[byteof(ctrl_bit)] & bitof(ctrl_bit));
}

usbd_status
uvideo_vs_parse_desc(struct uvideo_softc *sc, usb_config_descriptor_t *cdesc)
{
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;
	int i, iface, numalts;
	usbd_status error;

	DPRINTF(1, "%s: number of total interfaces=%d\n",
	    DEVNAME(sc), sc->sc_nifaces);
	DPRINTF(1, "%s: number of VS interfaces=%d\n",
	    DEVNAME(sc), sc->sc_desc_vc_header.fix->bInCollection);

	usbd_desc_iter_init(sc->sc_udev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		/* Skip all interfaces until we found our first. */
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			id = (usb_interface_descriptor_t *)desc;
			if (id->bInterfaceNumber == sc->sc_iface)
				break;
		}
		desc = usbd_desc_iter_next(&iter);
	}
	while (desc) {
		/* Crossed device function boundary. */
		if (desc->bDescriptorType == UDESC_IFACE_ASSOC)
			break;
		if (desc->bDescriptorType != UDESC_CS_INTERFACE) {
			desc = usbd_desc_iter_next(&iter);
			continue;
		}

		switch (desc->bDescriptorSubtype) {
		case UDESCSUB_VS_INPUT_HEADER:
			if (!uvideo_desc_len(desc, 13, 3, 0, 12))
				break;
			error = uvideo_vs_parse_desc_input_header(sc, desc);
			if (error != USBD_NORMAL_COMPLETION)
				return (error);
			break;

		/* TODO: which VS descriptors do we need else? */
		}

		desc = usbd_desc_iter_next(&iter);
	}

	/* parse video stream format descriptors */
	error = uvideo_vs_parse_desc_format(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* parse video stream frame descriptors */
	error = uvideo_vs_parse_desc_frame(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* parse interface collection */
	for (i = 0; i < sc->sc_desc_vc_header.fix->bInCollection; i++) {
		iface = sc->sc_desc_vc_header.baInterfaceNr[i];

		id = usbd_get_interface_descriptor(&sc->sc_udev->ifaces[iface]);
		if (id == NULL) {
			printf("%s: can't get VS interface %d!\n",
			    DEVNAME(sc), iface);
			return (USBD_INVAL);
		}
		usbd_claim_iface(sc->sc_udev, iface);

		numalts = usbd_get_no_alts(cdesc, id->bInterfaceNumber);

		DPRINTF(1, "%s: VS interface %d, ", DEVNAME(sc), i);
		DPRINTF(1, "bInterfaceNumber=0x%02x, numalts=%d\n",
		    id->bInterfaceNumber, numalts);

		error = uvideo_vs_parse_desc_alt(sc, i, iface, numalts);
		if (error != USBD_NORMAL_COMPLETION)
			return (error);
	}

	/* XXX for now always use the first video stream */
	sc->sc_vs_cur = &sc->sc_vs_coll[0];

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_parse_desc_input_header(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_input_header_desc *d;

	d = (struct usb_video_input_header_desc *)(uint8_t *)desc;

	/* on some devices bNumFormats is larger than the truth */
	if (d->bNumFormats == 0) {
		printf("%s: no INPUT FORMAT descriptors found!\n", DEVNAME(sc));
		return (USBD_INVAL);
	}

	sc->sc_desc_vs_input_header.fix = d;
	sc->sc_desc_vs_input_header.bmaControls = (uByte *)(d + 1);

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_parse_desc_format(struct uvideo_softc *sc)
{
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	usbd_desc_iter_init(sc->sc_udev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		/* Skip all interfaces until we found our first. */
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			id = (usb_interface_descriptor_t *)desc;
			if (id->bInterfaceNumber == sc->sc_iface)
				break;
		}
		desc = usbd_desc_iter_next(&iter);
	}
	while (desc) {
		/* Crossed device function boundary. */
		if (desc->bDescriptorType == UDESC_IFACE_ASSOC)
			break;

		if (desc->bDescriptorType != UDESC_CS_INTERFACE)
			goto next;

		if (desc->bLength != UVIDEO_FORMAT_LEN(desc))
			goto next;

		switch (desc->bDescriptorSubtype) {
		case UDESCSUB_VS_COLORFORMAT:
			uvideo_vs_parse_desc_colorformat(sc, desc);
			break;
		case UDESCSUB_VS_FORMAT_MJPEG:
			uvideo_vs_parse_desc_format_mjpeg(sc, desc);
			break;
		case UDESCSUB_VS_FORMAT_UNCOMPRESSED:
			uvideo_vs_parse_desc_format_uncompressed(sc, desc);
			break;
		case UDESCSUB_VS_FORMAT_FRAME_BASED:
			uvideo_vs_parse_desc_format_frame_based(sc, desc);
			break;
		case UDESCSUB_VS_FORMAT_H264:
		case UDESCSUB_VS_FORMAT_H264_SIMULCAST:
			uvideo_vs_parse_desc_format_h264(sc, desc);
			break;
		}

next:
		desc = usbd_desc_iter_next(&iter);
	}

	sc->sc_fmtgrp_idx = 0;

	if (sc->sc_fmtgrp_num == 0) {
		printf("%s: no format descriptors found!\n", DEVNAME(sc));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: number of total format descriptors=%d\n",
	    DEVNAME(sc), sc->sc_fmtgrp_num);

	return (USBD_NORMAL_COMPLETION);
}

void
uvideo_vs_parse_desc_colorformat(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	int fmtidx;
	struct usb_video_colorformat_desc *d;

	d = (struct usb_video_colorformat_desc *)(uint8_t *)desc;

	fmtidx = sc->sc_fmtgrp_idx - 1;
	if (fmtidx < 0 || sc->sc_fmtgrp[fmtidx].has_colorformat)
		return;

	if (d->bColorPrimaries < nitems(uvideo_color_primaries))
		sc->sc_fmtgrp[fmtidx].colorspace =
		    uvideo_color_primaries[d->bColorPrimaries];
	else
		sc->sc_fmtgrp[fmtidx].colorspace = V4L2_COLORSPACE_SRGB;

	if (d->bTransferCharacteristics < nitems(uvideo_xfer_characteristics))
		sc->sc_fmtgrp[fmtidx].xfer_func =
		    uvideo_xfer_characteristics[d->bTransferCharacteristics];
	else
		sc->sc_fmtgrp[fmtidx].xfer_func = V4L2_XFER_FUNC_DEFAULT;

	if (d->bMatrixCoefficients < nitems(uvideo_matrix_coefficients))
		sc->sc_fmtgrp[fmtidx].ycbcr_enc =
		    uvideo_matrix_coefficients[d->bMatrixCoefficients];
	else
		sc->sc_fmtgrp[fmtidx].ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;

	sc->sc_fmtgrp[fmtidx].has_colorformat = 1;
}

void
uvideo_vs_parse_desc_format_mjpeg(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_desc *d;

	d = (struct usb_video_format_desc *)(uint8_t *)desc;

	if (d->bNumFrameDescriptors == 0) {
		printf("%s: no MJPEG frame descriptors available!\n",
		    DEVNAME(sc));
		return;
	}

	if (sc->sc_fmtgrp_idx >= UVIDEO_MAX_FORMAT) {
		printf("%s: too many format descriptors found!\n", DEVNAME(sc));
		return;
	}

	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format = d;
	if (d->u.mjpeg.bDefaultFrameIndex > d->bNumFrameDescriptors ||
	    d->u.mjpeg.bDefaultFrameIndex < 1) {
		/* sanitize wrong bDefaultFrameIndex value */
		sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx = 1;
	} else {
		sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx =
		    d->u.mjpeg.bDefaultFrameIndex;
	}
	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].pixelformat = V4L2_PIX_FMT_MJPEG;

	if (sc->sc_fmtgrp_cur == NULL)
		/* set MJPEG format */
		sc->sc_fmtgrp_cur = &sc->sc_fmtgrp[sc->sc_fmtgrp_idx];

	sc->sc_fmtgrp_idx++;
	sc->sc_fmtgrp_num++;
}

void
uvideo_vs_parse_desc_format_h264(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_desc *d;

	d = (struct usb_video_format_desc *)(uint8_t *)desc;

	if (d->bNumFrameDescriptors == 0) {
		printf("%s: no H264 frame descriptors available!\n",
		    DEVNAME(sc));
		return;
	}

	if (sc->sc_fmtgrp_idx >= UVIDEO_MAX_FORMAT) {
		printf("%s: too many format descriptors found!\n", DEVNAME(sc));
		return;
	}

	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format = d;
	if (d->u.h264.bDefaultFrameIndex > d->bNumFrameDescriptors ||
	    d->u.h264.bDefaultFrameIndex < 1) {
		/* sanitize wrong bDefaultFrameIndex value */
		sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx = 1;
	} else {
		sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx =
		    d->u.h264.bDefaultFrameIndex;
	}
	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].pixelformat = V4L2_PIX_FMT_H264;

	if (sc->sc_fmtgrp_cur == NULL)
		/* set H264 format */
		sc->sc_fmtgrp_cur = &sc->sc_fmtgrp[sc->sc_fmtgrp_idx];

	sc->sc_fmtgrp_idx++;
	sc->sc_fmtgrp_num++;
}

void
uvideo_vs_parse_desc_format_frame_based(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_desc *d;
	int i, j, nent;

	d = (struct usb_video_format_desc *)(uint8_t *)desc;

	if (d->bNumFrameDescriptors == 0) {
		printf("%s: no Frame Based frame descriptors available!\n",
		    DEVNAME(sc));
		return;
	}

	if (sc->sc_fmtgrp_idx >= UVIDEO_MAX_FORMAT) {
		printf("%s: too many format descriptors found!\n", DEVNAME(sc));
		return;
	}

	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format = d;
	if (d->u.fb.bDefaultFrameIndex > d->bNumFrameDescriptors ||
	    d->u.fb.bDefaultFrameIndex < 1) {
		/* sanitize wrong bDefaultFrameIndex value */
		sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx = 1;
	} else {
		sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx =
		    d->u.fb.bDefaultFrameIndex;
	}

	i = sc->sc_fmtgrp_idx;

	/* map GUID to pixel format if a matching entry is found */
	for (j = 0, nent = nitems(uvideo_map_fmts); j < nent; j++) {
		if (!memcmp(sc->sc_fmtgrp[i].format->u.uc.guidFormat,
		    uvideo_map_fmts[j].guidFormat, 16)) {
			sc->sc_fmtgrp[i].pixelformat =
			    uvideo_map_fmts[j].pixelformat;
			break;
		}
	}
	/* default to using GUID start as the pixel format */
	if (j == nent)
		memcpy(&sc->sc_fmtgrp[i].pixelformat,
		    sc->sc_fmtgrp[i].format->u.uc.guidFormat,
		    sizeof(uint32_t));

	if (sc->sc_fmtgrp_cur == NULL)
		/* set Frame Based format */
		sc->sc_fmtgrp_cur = &sc->sc_fmtgrp[sc->sc_fmtgrp_idx];

	sc->sc_fmtgrp_idx++;
	sc->sc_fmtgrp_num++;
}

void
uvideo_vs_parse_desc_format_uncompressed(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_desc *d;
	int i, j, nent;

	d = (struct usb_video_format_desc *)(uint8_t *)desc;

	if (d->bNumFrameDescriptors == 0) {
		printf("%s: no UNCOMPRESSED frame descriptors available!\n",
		    DEVNAME(sc));
		return;
	}

	if (sc->sc_fmtgrp_idx >= UVIDEO_MAX_FORMAT) {
		printf("%s: too many format descriptors found!\n", DEVNAME(sc));
		return;
	}

	sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format = d;
	if (d->u.uc.bDefaultFrameIndex > d->bNumFrameDescriptors ||
	    d->u.uc.bDefaultFrameIndex < 1) {
		/* sanitize wrong bDefaultFrameIndex value */
		sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx = 1;
	} else {
		sc->sc_fmtgrp[sc->sc_fmtgrp_idx].format_dfidx =
		    d->u.uc.bDefaultFrameIndex;
	}

	i = sc->sc_fmtgrp_idx;

	/* map GUID to pixel format if a matching entry is found */
	for (j = 0, nent = nitems(uvideo_map_fmts); j < nent; j++) {
		if (!memcmp(sc->sc_fmtgrp[i].format->u.uc.guidFormat,
		    uvideo_map_fmts[j].guidFormat, 16)) {
			sc->sc_fmtgrp[i].pixelformat =
			    uvideo_map_fmts[j].pixelformat;
			break;
		}
	}
	/* default to using GUID start as the pixel format */
	if (j == nent)
		memcpy(&sc->sc_fmtgrp[i].pixelformat,
		    sc->sc_fmtgrp[i].format->u.uc.guidFormat,
		    sizeof(uint32_t));

	if (sc->sc_fmtgrp_cur == NULL)
		/* set UNCOMPRESSED format */
		sc->sc_fmtgrp_cur = &sc->sc_fmtgrp[sc->sc_fmtgrp_idx];

	sc->sc_fmtgrp_idx++;
	sc->sc_fmtgrp_num++;

	return;
}

usbd_status
uvideo_vs_parse_desc_frame(struct uvideo_softc *sc)
{
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;
	usbd_status error;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	usbd_desc_iter_init(sc->sc_udev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		/* Skip all interfaces until we found our first. */
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			id = (usb_interface_descriptor_t *)desc;
			if (id->bInterfaceNumber == sc->sc_iface)
				break;
		}
		desc = usbd_desc_iter_next(&iter);
	}
	while (desc) {
		/* Crossed device function boundary. */
		if (desc->bDescriptorType == UDESC_IFACE_ASSOC)
			break;
		if (desc->bDescriptorType == UDESC_CS_INTERFACE &&
		    desc->bLength > UVIDEO_FRAME_MIN_LEN(desc) &&
		    (desc->bDescriptorSubtype == UDESCSUB_VS_FRAME_MJPEG ||
		    desc->bDescriptorSubtype == UDESCSUB_VS_FRAME_UNCOMPRESSED)) {
			error = uvideo_vs_parse_desc_frame_buffer_size(sc, desc);
			if (error != USBD_NORMAL_COMPLETION)
				return (error);
		}
		if (desc->bDescriptorType == UDESC_CS_INTERFACE &&
		    desc->bLength > UVIDEO_FRAME_MIN_LEN(desc) &&
		    (desc->bDescriptorSubtype == UDESCSUB_VS_FRAME_H264 ||
		    desc->bDescriptorSubtype == UDESCSUB_VS_FRAME_FRAME_BASED)) {
			error = uvideo_vs_parse_desc_frame_max_rate(sc, desc);
			if (error != USBD_NORMAL_COMPLETION)
				return (error);
		}
		desc = usbd_desc_iter_next(&iter);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_parse_desc_frame_buffer_size(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_frame_desc *fd = 
	    (struct usb_video_frame_desc *)(uint8_t *)desc;
	int fmtidx, frame_num;
	uint32_t fbuf_size;

	fmtidx = sc->sc_fmtgrp_idx;
	frame_num = sc->sc_fmtgrp[fmtidx].frame_num;
	if (frame_num >= UVIDEO_MAX_FRAME) {
		printf("%s: too many %s frame descriptors found!\n",
		    DEVNAME(sc),
		    desc->bDescriptorSubtype == UDESCSUB_VS_FRAME_MJPEG ?
		    "MJPEG" : "UNCOMPRESSED");
		return (USBD_INVAL);
	}
	sc->sc_fmtgrp[fmtidx].frame[frame_num] = fd;

	if (sc->sc_fmtgrp[fmtidx].frame_cur == NULL ||
	    sc->sc_fmtgrp[fmtidx].format_dfidx == fd->bFrameIndex)
		sc->sc_fmtgrp[fmtidx].frame_cur = fd;

	/*
	 * On some devices, dwMaxVideoFrameBufferSize is not correct.
	 * Version 1.1 of the UVC spec says this field is deprecated.
	 * For uncompressed pixel formats, the frame buffer size can
	 * be determined by multiplying width, height, and bytes per pixel.
	 * Uncompressed formats have a fixed number of bytes per pixel.
	 * Bytes per pixel can vary with compressed formats.
	 */
	if (desc->bDescriptorSubtype == UDESCSUB_VS_FRAME_UNCOMPRESSED) {
		fbuf_size = UGETW(fd->u.uc.wWidth) * UGETW(fd->u.uc.wHeight) *
		    sc->sc_fmtgrp[fmtidx].format->u.uc.bBitsPerPixel / NBBY;
		DPRINTF(10, "%s: %s: frame buffer size=%d "
		    "width=%d height=%d bpp=%d\n", DEVNAME(sc), __func__,
		    fbuf_size, UGETW(fd->u.uc.wWidth), UGETW(fd->u.uc.wHeight),
		    sc->sc_fmtgrp[fmtidx].format->u.uc.bBitsPerPixel);
	} else
		fbuf_size = UGETDW(fd->u.uc.dwMaxVideoFrameBufferSize);

	/* store max value */
	if (fbuf_size > sc->sc_max_fbuf_size)
		sc->sc_max_fbuf_size = fbuf_size;

	/*
	 * Increment frame count.  If this is the last frame in the
	 * format group, go on to next group.
	 */
	if (++sc->sc_fmtgrp[fmtidx].frame_num ==
	    sc->sc_fmtgrp[fmtidx].format->bNumFrameDescriptors) {
		sc->sc_fmtgrp_idx++;
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_parse_desc_frame_max_rate(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_frame_desc *fd =
	    (struct usb_video_frame_desc *)(uint8_t *)desc;
	uint8_t *p;
	int i, fmtidx, frame_num, length, nivals;
	uint32_t fbuf_size, frame_ival, next_frame_ival;

	fmtidx = sc->sc_fmtgrp_idx;
	frame_num = sc->sc_fmtgrp[fmtidx].frame_num;
	if (frame_num >= UVIDEO_MAX_FRAME) {
		printf("%s: too many %s frame descriptors found!\n",
		    DEVNAME(sc),
		    desc->bDescriptorSubtype == UDESCSUB_VS_FRAME_H264 ?
		    "H264" : "FRAME BASED");
		return (USBD_INVAL);
	}
	sc->sc_fmtgrp[fmtidx].frame[frame_num] = fd;

	if (sc->sc_fmtgrp[fmtidx].frame_cur == NULL ||
	    sc->sc_fmtgrp[fmtidx].format_dfidx == fd->bFrameIndex)
		sc->sc_fmtgrp[fmtidx].frame_cur = fd;

	/*
	 * Frame Based and H264 frame hasn't got
	 * dwMaxVideoFrameBufferSize, instead compute required buffer
	 * via dwMaxBitRate and dwFrameInterval.
	 */

	frame_ival = UGETDW(fd->u.h264.dwDefaultFrameInterval);

	p = (uint8_t *)desc + UVIDEO_FRAME_MIN_LEN(fd);
	length = fd->bLength - UVIDEO_FRAME_MIN_LEN(fd);

	nivals = UVIDEO_FRAME_NUM_INTERVALS(fd);

	for (i = 0; i < nivals; i++) {
		if (length <= 0) {
			printf("frame descriptor ended early\n");
			break;
		}
		next_frame_ival = UGETDW(p);
		if (next_frame_ival > frame_ival)
			frame_ival = next_frame_ival;
		p += sizeof(uDWord);
		length -= sizeof(uDWord);
	}

	fbuf_size = UGETDW(UVIDEO_FRAME_FIELD(fd, dwMaxBitRate)) * frame_ival;
	fbuf_size /= 8 * 10000000;

	/* store max value */
	if (fbuf_size > sc->sc_max_fbuf_size)
		sc->sc_max_fbuf_size = fbuf_size;

	/*
	 * Increment frame count.  If this is the last frame in the
	 * format group, go on to next group.
	 */
	if (++sc->sc_fmtgrp[fmtidx].frame_num ==
	    sc->sc_fmtgrp[fmtidx].format->bNumFrameDescriptors) {
		sc->sc_fmtgrp_idx++;
	}

	return (USBD_NORMAL_COMPLETION);
}

uint32_t
uvideo_vc_parse_max_packet_size(struct uvideo_softc *sc,
    usb_endpoint_descriptor_t *ed)
{
	uint32_t psize;
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;
	usb_endpoint_ss_comp_descriptor_t *esscd;

	/*
	 * USB 3.0 Section 9.6.7 states that wBytesPerInterval is only
	 * valid for periodic endpoints (isochronous and interrupt).
	 */
	if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
		goto skip_ss_comp;

	usbd_desc_iter_init(sc->sc_udev, &iter);
	while ((desc = usbd_desc_iter_next(&iter))) {
		if (desc == (const usb_descriptor_t *)ed) {
			desc = usbd_desc_iter_next(&iter);
			break;
		}
	}

	if (desc && sc->sc_udev->speed >= USB_SPEED_SUPER &&
	    desc->bDescriptorType == UDESC_ENDPOINT_SS_COMP) {
		esscd = (usb_endpoint_ss_comp_descriptor_t *)desc;
		psize = UGETW(esscd->wBytesPerInterval);
		DPRINTF(1, "%s: wBytesPerInterval=%d\n",
		    DEVNAME(sc), psize);
		return psize;
	}

skip_ss_comp:
	psize = UGETW(ed->wMaxPacketSize);
	psize = UE_GET_SIZE(psize) * (1 + UE_GET_TRANS(psize));

	return psize;
}

usbd_status
uvideo_vs_parse_desc_alt(struct uvideo_softc *sc, int vs_nr, int iface, int numalts)
{
	struct uvideo_vs_iface *vs;
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	uint8_t ep_dir, ep_type;
	int bulk_endpoint;
	uint32_t psize;

	vs = &sc->sc_vs_coll[vs_nr];

	usbd_desc_iter_init(sc->sc_udev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		/* Skip all interfaces until we found our first. */
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			id = (usb_interface_descriptor_t *)desc;
			if (id->bInterfaceNumber == sc->sc_iface)
				break;
		}
		desc = usbd_desc_iter_next(&iter);
	}

	vs->bulk_endpoint = 1;

	while (desc) {
		/* Crossed device function boundary. */
		if (desc->bDescriptorType == UDESC_IFACE_ASSOC)
			break;
		/* find video stream interface */
		if (desc->bDescriptorType != UDESC_INTERFACE)
			goto next;
		id = (usb_interface_descriptor_t *)(uint8_t *)desc;
		if (id->bInterfaceNumber != iface)
			goto next;
		DPRINTF(1, "%s: bAlternateSetting=0x%02x, ",
		    DEVNAME(sc), id->bAlternateSetting);
		if (id->bNumEndpoints == 0) {
			DPRINTF(1, "no endpoint descriptor\n");
			goto next;
		}

		/* jump to corresponding endpoint descriptor */
		while ((desc = usbd_desc_iter_next(&iter))) {
			if (desc->bDescriptorType == UDESC_ENDPOINT)
				break;
		}
		ed = (usb_endpoint_descriptor_t *)(uint8_t *)desc;
		DPRINTF(1, "bEndpointAddress=0x%02x, ", ed->bEndpointAddress);
		DPRINTF(1, "wMaxPacketSize=%d\n", UGETW(ed->wMaxPacketSize));

		/* locate endpoint type */
		ep_dir = UE_GET_DIR(ed->bEndpointAddress);
		ep_type = UE_GET_XFERTYPE(ed->bmAttributes);
		if (ep_dir == UE_DIR_IN && ep_type == UE_ISOCHRONOUS)
			bulk_endpoint = 0;
		else if (ep_dir == UE_DIR_IN && ep_type == UE_BULK)
			bulk_endpoint = 1;
		else
			goto next;

		/*
		 * Section 2.4.3 does not prohibit the mix of bulk and
		 * isochronous endpoints when the bulk endpoints are
		 * used solely for still image transfer.
		 */
		if (bulk_endpoint && !vs->bulk_endpoint)
			goto next;

		psize = uvideo_vc_parse_max_packet_size(sc, ed);
		/* save endpoint with largest bandwidth */
		if (psize > vs->psize) {
			vs->ifaceh = &sc->sc_udev->ifaces[iface];
			vs->endpoint = ed->bEndpointAddress;
			vs->numalts = numalts;
			vs->curalt = id->bAlternateSetting;
			vs->psize = psize;
			vs->iface = iface;
			vs->bulk_endpoint = bulk_endpoint;
		}
next:
		desc = usbd_desc_iter_next(&iter);
	}

	/* check if we have found a valid alternate interface */
	if (vs->ifaceh == NULL) {
		printf("%s: no valid alternate interface found!\n",
		    DEVNAME(sc));
		return (USBD_INVAL);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_set_alt(struct uvideo_softc *sc, struct usbd_interface *ifaceh,
    int max_packet_size)
{
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int diff, best_diff = INT_MAX, bulk_endpoint;
	usbd_status error;
	uint32_t psize;
	uint8_t ep_type;

	usbd_desc_iter_init(sc->sc_udev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		/* Skip all interfaces until we found our first. */
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			id = (usb_interface_descriptor_t *)desc;
			if (id->bInterfaceNumber == sc->sc_iface)
				break;
		}
		desc = usbd_desc_iter_next(&iter);
	}
	while (desc) {
		/* Crossed device function boundary. */
		if (desc->bDescriptorType == UDESC_IFACE_ASSOC)
			break;
		/* find video stream interface */
		if (desc->bDescriptorType != UDESC_INTERFACE)
			goto next;
		id = (usb_interface_descriptor_t *)(uint8_t *)desc;
		if (id->bInterfaceNumber != sc->sc_vs_cur->iface)
			goto next;
		if (id->bNumEndpoints == 0)
			goto next;

		/* jump to corresponding endpoint descriptor */
		desc = usbd_desc_iter_next(&iter);
		if (desc->bDescriptorType != UDESC_ENDPOINT)
			goto next;
		ed = (usb_endpoint_descriptor_t *)(uint8_t *)desc;

		ep_type = UE_GET_XFERTYPE(ed->bmAttributes);
		if (ep_type == UE_ISOCHRONOUS)
			bulk_endpoint = 0;
		else if (ep_type == UE_BULK)
			bulk_endpoint = 1;

		if (bulk_endpoint && !sc->sc_vs_cur->bulk_endpoint)
			goto next;

		/* save endpoint with requested bandwidth */
		psize = uvideo_vc_parse_max_packet_size(sc, ed);
		if (psize >= max_packet_size)
			diff = psize - max_packet_size;
		else
			goto next;
		if (diff < best_diff) {
			best_diff = diff;
			sc->sc_vs_cur->endpoint = ed->bEndpointAddress;
			sc->sc_vs_cur->curalt = id->bAlternateSetting;
			sc->sc_vs_cur->psize = psize;
			if (diff == 0)
				break;
		}
next:
		desc = usbd_desc_iter_next(&iter);
	}

	DPRINTF(1, "%s: set alternate iface to ", DEVNAME(sc));
	DPRINTF(1, "bAlternateSetting=0x%02x psize=%d max_packet_size=%d\n",
	    sc->sc_vs_cur->curalt, sc->sc_vs_cur->psize, max_packet_size);

	/* set alternate video stream interface */
	error = usbd_set_interface(ifaceh, sc->sc_vs_cur->curalt);
	if (error) {
		printf("%s: could not set alternate interface %d!\n",
		    DEVNAME(sc), sc->sc_vs_cur->curalt);
		return (USBD_INVAL);
	}

	return (USBD_NORMAL_COMPLETION);
}

/*
 * Thanks to the retarded USB Video Class specs there are different
 * descriptors types with the same bDescriptorSubtype which makes
 * it necessary to differ between those types by doing descriptor
 * size dances :-(
 *
 * size_fix:		total size of the fixed structure part
 * off_num_elements:	offset which tells the number of following elements
 * size_element:	size of a single element
 * off_size_element:	if size_element is 0 the element size is taken from
 *			this offset in the descriptor 
 */
int
uvideo_desc_len(const usb_descriptor_t *desc,
    int size_fix, int off_num_elements, int size_element, int off_size_element)
{
	uint8_t *buf;
	int size_elements, size_total;

	if (desc->bLength < size_fix)
		return (0);

	buf = (uint8_t *)desc;

	if (size_element == 0)
		size_element = buf[off_size_element];

	size_elements = buf[off_num_elements] * size_element;
	size_total = size_fix + size_elements;

	if (desc->bLength == size_total && size_elements != 0)
		return (1);

	return (0);
}

/*
 * Find the next best matching resolution which we can offer and
 * return it.
 */
void
uvideo_find_res(struct uvideo_softc *sc, int idx, int width, int height,
    struct uvideo_res *r)
{
	int i, w, h, diff, diff_best, size_want, size_is;
	struct usb_video_frame_desc *frame;

	size_want = width * height;

	for (i = 0; i < sc->sc_fmtgrp[idx].frame_num; i++) {
		frame = sc->sc_fmtgrp[idx].frame[i];
		w = UGETW(UVIDEO_FRAME_FIELD(frame, wWidth));
		h = UGETW(UVIDEO_FRAME_FIELD(frame, wHeight));
		size_is = w * h;
		if (size_is > size_want)
			diff = size_is - size_want;
		else
			diff = size_want - size_is;
		if (i == 0)
			diff_best = diff;
		if (diff <= diff_best) {
			diff_best = diff;
			r->width = w;
			r->height = h;
			r->fidx = i;
		}
		DPRINTF(1, "%s: %s: frame index %d: width=%d, height=%d\n",
		    DEVNAME(sc), __func__, i, w, h);
	}
}

usbd_status
uvideo_vs_negotiation(struct uvideo_softc *sc, int commit)
{
	struct usb_video_probe_commit *pc;
	struct uvideo_format_group *fmtgrp;
	struct usb_video_header_desc *hd;
	struct usb_video_frame_desc *frame;
	uint8_t *p, *cur;
	uint8_t probe_data[48];
	uint32_t frame_ival, nivals, min, max, step, diff;
	usbd_status error;
	int i, ival_bytes, changed = 0;
	size_t len;

	pc = (struct usb_video_probe_commit *)probe_data;

	fmtgrp = sc->sc_fmtgrp_cur;

	/* check if the format descriptor contains frame descriptors */
	if (fmtgrp->frame_num == 0) {
		printf("%s: %s: no frame descriptors found!\n",
		    __func__, DEVNAME(sc));
		return (USBD_INVAL);
	}

	/* set probe */
	bzero(probe_data, sizeof(probe_data));
	/* hint that dwFrameInterval should be favored over other parameters */
	USETW(pc->bmHint, 0x1);
	pc->bFormatIndex = fmtgrp->format->bFormatIndex;
	pc->bFrameIndex = fmtgrp->frame_cur->bFrameIndex;
	/* dwFrameInterval: 30fps=333333, 15fps=666666, 10fps=1000000 */
	frame = fmtgrp->frame_cur;
	frame_ival = UGETDW(UVIDEO_FRAME_FIELD(frame, dwDefaultFrameInterval));
	if (sc->sc_frame_rate != 0) {
		frame_ival = 10000000 / sc->sc_frame_rate;
		/* find closest matching interval the device supports */
		len = UVIDEO_FRAME_MIN_LEN(frame);
		nivals = UVIDEO_FRAME_NUM_INTERVALS(frame);
		p = (uint8_t *)fmtgrp->frame_cur;
		p += len;
		ival_bytes = frame->bLength - len;
		if (!nivals && (ival_bytes >= sizeof(uDWord) * 3)) {
			/* continuous */
			min = UGETDW(p);
			p += sizeof(uDWord);
			max = UGETDW(p);
			p += sizeof(uDWord);
			step = UGETDW(p);
			p += sizeof(uDWord);
			if (frame_ival <= min)
				frame_ival = min;
			else if (frame_ival >= max)
				frame_ival = max;
			else {
				for (i = min; i + step/2 < frame_ival; i+= step)
					;	/* nothing */
				frame_ival = i;
			}
		} else if (nivals > 0 && ival_bytes >= sizeof(uDWord)) {
			/* discrete */
			cur = p;
			min = UINT_MAX;
			for (i = 0; i < nivals; i++) {
				if (ival_bytes < sizeof(uDWord)) {
					/* short descriptor ? */
					break;
				}
				diff = abs(UGETDW(p) - frame_ival);
				if (diff < min) {
					min = diff;
					cur = p;
					if (diff == 0)
						break;
				}
				p += sizeof(uDWord);
				ival_bytes -= sizeof(uDWord);
			}
			frame_ival = UGETDW(cur);
		} else {
			DPRINTF(1, "%s: %s: bad frame ival descriptor\n",
			    DEVNAME(sc), __func__);
		}
	}
	USETDW(pc->dwFrameInterval, frame_ival);
	error = uvideo_vs_set_probe(sc, probe_data);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* get probe */
	bzero(probe_data, sizeof(probe_data));
	error = uvideo_vs_get_probe(sc, probe_data, GET_CUR);
	if (error != USBD_NORMAL_COMPLETION)
		return (error);

	/* check that the format and frame indexes are what we wanted */
	if (pc->bFormatIndex != fmtgrp->format->bFormatIndex) {
		changed++;
		DPRINTF(1, "%s: %s: wanted format 0x%x, got format 0x%x\n",
		    DEVNAME(sc), __func__, fmtgrp->format->bFormatIndex,
		    pc->bFormatIndex);
		for (i = 0; i < sc->sc_fmtgrp_num; i++) {
			if (sc->sc_fmtgrp[i].format->bFormatIndex ==
			    pc->bFormatIndex) {
				fmtgrp = &sc->sc_fmtgrp[i];
				break;
			}
		}
		if (i == sc->sc_fmtgrp_num) {
			DPRINTF(1, "%s: %s: invalid format index 0x%x\n",
			    DEVNAME(sc), __func__, pc->bFormatIndex);
			return (USBD_INVAL);
		}
	}
	if (pc->bFrameIndex != fmtgrp->frame_cur->bFrameIndex) {
		changed++;
		DPRINTF(1, "%s: %s: wanted frame 0x%x, got frame 0x%x\n",
		    DEVNAME(sc), __func__, fmtgrp->frame_cur->bFrameIndex,
		    pc->bFrameIndex);
		for (i = 0; i < fmtgrp->frame_num; i++) {
			if (fmtgrp->frame[i]->bFrameIndex == pc->bFrameIndex) {
				frame = fmtgrp->frame[i];
				break;
			}
		}
		if (i == fmtgrp->frame_num) {
			DPRINTF(1, "%s: %s: invalid frame index 0x%x\n",
			    DEVNAME(sc), __func__, pc->bFrameIndex);
			return (USBD_INVAL);
		}
	} else
		frame = fmtgrp->frame_cur;

	/*
	 * Uncompressed formats have fixed bits per pixel, which means
	 * the frame buffer size is fixed and can be calculated.  Because
	 * some devices return incorrect values, always override the
	 * frame size with a calculated value.
	 */
	if (frame->bDescriptorSubtype == UDESCSUB_VS_FRAME_UNCOMPRESSED) {
		USETDW(pc->dwMaxVideoFrameSize,
		    UGETW(frame->u.uc.wWidth) * UGETW(frame->u.uc.wHeight) *
		    fmtgrp->format->u.uc.bBitsPerPixel / NBBY);
		DPRINTF(1, "fixed dwMaxVideoFrameSize=%d, "
		    "width=%d height=%d bpp=%d\n",
		    UGETDW(pc->dwMaxVideoFrameSize),
		    UGETW(frame->u.uc.wWidth), UGETW(frame->u.uc.wHeight),
		    fmtgrp->format->u.uc.bBitsPerPixel);
	} else {
		/*
		 * Some UVC 1.00 devices return dwMaxVideoFrameSize = 0.
		 * If so, fix it by format/frame descriptors.
		 */
		hd = sc->sc_desc_vc_header.fix;
		if (UGETDW(pc->dwMaxVideoFrameSize) == 0 &&
		    UGETW(hd->bcdUVC) < 0x0110 ) {
			DPRINTF(1, "%s: dwMaxVideoFrameSize == 0, fixed\n",
			    DEVNAME(sc));
			USETDW(pc->dwMaxVideoFrameSize,
			    UGETDW(frame->u.uc.dwMaxVideoFrameBufferSize));
		}
	}

	/* commit */
	if (commit) {
		if (changed > 0) {
			/* didn't get the frame format or size we wanted */
			return (USBD_INVAL);
		}
		error = uvideo_vs_set_commit(sc, probe_data);
		if (error != USBD_NORMAL_COMPLETION)
			return (error);
	}

	/* save a copy of probe commit */
	bcopy(pc, &sc->sc_desc_probe, sizeof(sc->sc_desc_probe));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_set_probe(struct uvideo_softc *sc, uint8_t *probe_data)
{
	usb_device_request_t req;
	usbd_status error;
	uint16_t tmp;
	struct usb_video_probe_commit *pc;

	req.bmRequestType = UVIDEO_SET_IF;
	req.bRequest = SET_CUR;
	tmp = VS_PROBE_CONTROL;
	tmp = tmp << 8;
	USETW(req.wValue, tmp);
	USETW(req.wIndex, sc->sc_vs_cur->iface);
	USETW(req.wLength, sc->sc_max_ctrl_size);

	pc = (struct usb_video_probe_commit *)probe_data;

	error = usbd_do_request(sc->sc_udev, &req, probe_data);
	if (error) {
		printf("%s: could not SET probe request: %s\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: SET probe request successfully\n", DEVNAME(sc));

	DPRINTF(1, "bmHint=0x%02x\n", UGETW(pc->bmHint));
	DPRINTF(1, "bFormatIndex=0x%02x\n", pc->bFormatIndex);
	DPRINTF(1, "bFrameIndex=0x%02x\n", pc->bFrameIndex);
	DPRINTF(1, "dwFrameInterval=%d (100ns units)\n",
	    UGETDW(pc->dwFrameInterval));
	DPRINTF(1, "wKeyFrameRate=%d\n", UGETW(pc->wKeyFrameRate));
	DPRINTF(1, "wPFrameRate=%d\n", UGETW(pc->wPFrameRate));
	DPRINTF(1, "wCompQuality=%d\n", UGETW(pc->wCompQuality));
	DPRINTF(1, "wCompWindowSize=%d\n", UGETW(pc->wCompWindowSize));
	DPRINTF(1, "wDelay=%d (ms)\n", UGETW(pc->wDelay));
	DPRINTF(1, "dwMaxVideoFrameSize=%d (bytes)\n",
	    UGETDW(pc->dwMaxVideoFrameSize));
	DPRINTF(1, "dwMaxPayloadTransferSize=%d (bytes)\n",
	    UGETDW(pc->dwMaxPayloadTransferSize));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_get_probe(struct uvideo_softc *sc, uint8_t *probe_data,
    uint8_t request)
{
	usb_device_request_t req;
	usbd_status error;
	uint16_t tmp;
	int actlen;
	struct usb_video_probe_commit *pc;

	req.bmRequestType = UVIDEO_GET_IF;
	req.bRequest = request;
	tmp = VS_PROBE_CONTROL;
	tmp = tmp << 8;
	USETW(req.wValue, tmp);
	USETW(req.wIndex, sc->sc_vs_cur->iface);
	USETW(req.wLength, sc->sc_max_ctrl_size);

	pc = (struct usb_video_probe_commit *)probe_data;

	error = usbd_do_request_flags(sc->sc_udev, &req, probe_data,
	    USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: could not GET probe request: %s\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (USBD_INVAL);
	}

	/*
	 * Different UVC version defines different length of a probe;
	 * Make sure that the unused portion of probe_data remains zero.
	 */
	bzero(probe_data + actlen,
	    sizeof(struct usb_video_probe_commit) - actlen);

	if (sc->sc_quirk &&
	    sc->sc_quirk->flags & UVIDEO_FLAG_FORMAT_INDEX_IN_BMHINT &&
	    UGETW(pc->bmHint) > 255) {
		/*
		 * Some devices such as the Elgato Cam Link 4K or Elgato
		 * Game Capture HD60 returns an invalid bmHint response,
		 * which contains the bFormatIndex in the second byte.
		 * But it should be always zero since it's a reserved
		 * bit field.
		 */
		pc->bFormatIndex = UGETW(pc->bmHint) >> 8;
		USETW(pc->bmHint, 1);
	}

	DPRINTF(1, "%s: GET probe request successfully, length: %d\n",
	    DEVNAME(sc), actlen);

	DPRINTF(1, "bmHint=0x%02x\n", UGETW(pc->bmHint));
	DPRINTF(1, "bFormatIndex=0x%02x\n", pc->bFormatIndex);
	DPRINTF(1, "bFrameIndex=0x%02x\n", pc->bFrameIndex);
	DPRINTF(1, "dwFrameInterval=%d (100ns units)\n",
	    UGETDW(pc->dwFrameInterval));
	DPRINTF(1, "wKeyFrameRate=%d\n", UGETW(pc->wKeyFrameRate));
	DPRINTF(1, "wPFrameRate=%d\n", UGETW(pc->wPFrameRate));
	DPRINTF(1, "wCompQuality=%d\n", UGETW(pc->wCompQuality));
	DPRINTF(1, "wCompWindowSize=%d\n", UGETW(pc->wCompWindowSize));
	DPRINTF(1, "wDelay=%d (ms)\n", UGETW(pc->wDelay));
	DPRINTF(1, "dwMaxVideoFrameSize=%d (bytes)\n",
	    UGETDW(pc->dwMaxVideoFrameSize));
	DPRINTF(1, "dwMaxPayloadTransferSize=%d (bytes)\n",
	    UGETDW(pc->dwMaxPayloadTransferSize));
	DPRINTF(1, "dwClockFrequency=%d (Hz)\n",
	    UGETDW(pc->dwClockFrequency));
	DPRINTF(1, "bmFramingInfo=0x%02x\n", pc->bmFramingInfo);
	DPRINTF(1, "bPreferedVersion=%d\n", pc->bPreferedVersion);
	DPRINTF(1, "bMinVersion=%d\n", pc->bMinVersion);
	DPRINTF(1, "bMaxVersion=%d\n", pc->bMaxVersion);
	DPRINTF(1, "bUsage=%d\n", pc->bUsage);
	DPRINTF(1, "bBitDepthLuma=%d\n", pc->bBitDepthLuma);
	DPRINTF(1, "bmSettings=0x%02x\n", pc->bmSettings);
	DPRINTF(1, "bMaxNumberOfRefFramesPlus1=%d\n",
	    pc->bMaxNumberOfRefFramesPlus1);
	DPRINTF(1, "bmRateControlModes=%d\n",
	    UGETW(pc->bmRateControlModes));
#ifdef UVIDEO_DEBUG
	if (1 <= uvideo_debug) {
		printf("bmLayoutPerStream=0x");
		uvideo_hexdump(pc->bmLayoutPerStream,
		    sizeof(pc->bmLayoutPerStream), 1);
	}
#endif

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_set_commit(struct uvideo_softc *sc, uint8_t *probe_data)
{
	usb_device_request_t req;
	usbd_status error;
	uint16_t tmp;

	req.bmRequestType = UVIDEO_SET_IF;
	req.bRequest = SET_CUR;
	tmp = VS_COMMIT_CONTROL;
	tmp = tmp << 8;
	USETW(req.wValue, tmp);
	USETW(req.wIndex, sc->sc_vs_cur->iface);
	USETW(req.wLength, sc->sc_max_ctrl_size);

	error = usbd_do_request(sc->sc_udev, &req, probe_data);
	if (error) {
		printf("%s: could not SET commit request: %s\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: SET commit request successfully\n", DEVNAME(sc));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_alloc_frame(struct uvideo_softc *sc)
{
	struct uvideo_frame_buffer *fb = &sc->sc_frame_buffer;

	fb->buf_size = UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize);

	/* don't overflow the upper layer frame buffer */
	if (sc->sc_max_fbuf_size < fb->buf_size &&
	    sc->sc_mmap_flag == 0) {
		printf("%s: software video buffer is too small!\n",
		    DEVNAME(sc));
		return (USBD_NOMEM);
	}

	fb->buf = malloc(fb->buf_size, M_USBDEV, M_NOWAIT);
	if (fb->buf == NULL) {
		printf("%s: can't allocate frame buffer!\n", DEVNAME(sc));
		return (USBD_NOMEM);
	}

	DPRINTF(1, "%s: %s: allocated %d bytes frame buffer\n",
	    DEVNAME(sc), __func__, fb->buf_size);

	fb->sample = 0;
	fb->fid = 0;
	fb->offset = 0;
	fb->error = 0;
	fb->mmap_q_full = 0;
	fb->fmt_flags = sc->sc_fmtgrp_cur->frame_cur->bDescriptorSubtype ==
	    UDESCSUB_VS_FRAME_UNCOMPRESSED ? 0 : V4L2_FMT_FLAG_COMPRESSED;

	return (USBD_NORMAL_COMPLETION);
}

void
uvideo_vs_free_frame(struct uvideo_softc *sc)
{
	struct uvideo_frame_buffer *fb = &sc->sc_frame_buffer;

	if (fb->buf != NULL) {
		free(fb->buf, M_USBDEV, fb->buf_size);
		fb->buf = NULL;
	}

	if (sc->sc_mmap_buffer != NULL) {
		free(sc->sc_mmap_buffer, M_USBDEV, sc->sc_mmap_buffer_size);
		sc->sc_mmap_buffer = NULL;
		sc->sc_mmap_buffer_size = 0;
	}

	while (!SIMPLEQ_EMPTY(&sc->sc_mmap_q))
		SIMPLEQ_REMOVE_HEAD(&sc->sc_mmap_q, q_frames);

	sc->sc_mmap_count = 0;
}

usbd_status
uvideo_vs_alloc_isoc(struct uvideo_softc *sc)
{
	int size, i;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	for (i = 0; i < UVIDEO_IXFERS; i++) {
		sc->sc_vs_cur->ixfer[i].sc = sc;

		sc->sc_vs_cur->ixfer[i].xfer = usbd_alloc_xfer(sc->sc_udev);	
		if (sc->sc_vs_cur->ixfer[i].xfer == NULL) {
			printf("%s: could not allocate isoc VS xfer!\n",
			    DEVNAME(sc));
			return (USBD_NOMEM);	
		}

		size = sc->sc_vs_cur->psize * sc->sc_nframes;

		sc->sc_vs_cur->ixfer[i].buf =
		    usbd_alloc_buffer(sc->sc_vs_cur->ixfer[i].xfer, size);
		if (sc->sc_vs_cur->ixfer[i].buf == NULL) {
			printf("%s: could not allocate isoc VS buffer!\n",
			    DEVNAME(sc));
			return (USBD_NOMEM);
		}
		DPRINTF(1, "%s: allocated %d bytes isoc VS xfer buffer\n",
		    DEVNAME(sc), size);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_vs_alloc_bulk(struct uvideo_softc *sc)
{
	int size;

	sc->sc_vs_cur->bxfer.sc = sc;

	sc->sc_vs_cur->bxfer.xfer = usbd_alloc_xfer(sc->sc_udev);
	if (sc->sc_vs_cur->bxfer.xfer == NULL) {
		printf("%s: could not allocate bulk VS xfer!\n",
		    DEVNAME(sc));
		return (USBD_NOMEM);
	}

	size = UGETDW(sc->sc_desc_probe.dwMaxPayloadTransferSize);

	sc->sc_vs_cur->bxfer.buf =
	    usbd_alloc_buffer(sc->sc_vs_cur->bxfer.xfer, size);
	if (sc->sc_vs_cur->bxfer.buf == NULL) {
		printf("%s: could not allocate bulk VS buffer!\n",
		    DEVNAME(sc));
		return (USBD_NOMEM);
	}
	DPRINTF(1, "%s: allocated %d bytes bulk VS xfer buffer\n",
	    DEVNAME(sc), size);

	return (USBD_NORMAL_COMPLETION);
}

void
uvideo_vs_free_isoc(struct uvideo_softc *sc)
{
	int i;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	for (i = 0; i < UVIDEO_IXFERS; i++) {
		if (sc->sc_vs_cur->ixfer[i].buf != NULL) {
			usbd_free_buffer(sc->sc_vs_cur->ixfer[i].xfer);
			sc->sc_vs_cur->ixfer[i].buf = NULL;
		}

		if (sc->sc_vs_cur->ixfer[i].xfer != NULL) {
			usbd_free_xfer(sc->sc_vs_cur->ixfer[i].xfer);
			sc->sc_vs_cur->ixfer[i].xfer = NULL;
		}
	}
}

void
uvideo_vs_free_bulk(struct uvideo_softc *sc)
{
	if (sc->sc_vs_cur->bxfer.buf != NULL) {
		usbd_free_buffer(sc->sc_vs_cur->bxfer.xfer);
		sc->sc_vs_cur->bxfer.buf = NULL;
	}

	if (sc->sc_vs_cur->bxfer.xfer != NULL) {
		usbd_free_xfer(sc->sc_vs_cur->bxfer.xfer);
		sc->sc_vs_cur->bxfer.xfer = NULL;
	}
}

usbd_status
uvideo_vs_open(struct uvideo_softc *sc)
{
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	uint32_t dwMaxVideoFrameSize;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (sc->sc_negotiated_flag == 0) {
		/* do device negotiation with commit */
		error = uvideo_vs_negotiation(sc, 1);
		if (error != USBD_NORMAL_COMPLETION)
			return (error);
	}

	/* 2.4.3 the bulk endpoint only supports the alternative setting of 0 */
	if (sc->sc_vs_cur->bulk_endpoint)
		goto skip_set_alt;

	error = uvideo_vs_set_alt(sc, sc->sc_vs_cur->ifaceh,
	    UGETDW(sc->sc_desc_probe.dwMaxPayloadTransferSize));
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: could not set alternate interface!\n",
		    DEVNAME(sc));
		return (error);
	}

	/* double check if we can access the selected endpoint descriptor */
	ed = usbd_get_endpoint_descriptor(sc->sc_vs_cur->ifaceh,
	    sc->sc_vs_cur->endpoint);
	if (ed == NULL) {
		printf("%s: no endpoint descriptor for VS iface\n",
		    DEVNAME(sc));
		return (USBD_INVAL);
	}

skip_set_alt:
	DPRINTF(1, "%s: open pipe for bEndpointAddress=0x%02x\n",
	    DEVNAME(sc), sc->sc_vs_cur->endpoint);
	error = usbd_open_pipe(
	    sc->sc_vs_cur->ifaceh,
	    sc->sc_vs_cur->endpoint,
	    USBD_EXCLUSIVE_USE,
	    &sc->sc_vs_cur->pipeh);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: could not open VS pipe: %s\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (error);
	}

	/* calculate optimal isoc xfer size */
	if (strcmp(sc->sc_udev->bus->bdev.dv_cfdata->cf_driver->cd_name,
	    "ohci") == 0) {
		/* ohci workaround */
		sc->sc_nframes = 8;
	} else {
		dwMaxVideoFrameSize =
		    UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize);
		sc->sc_nframes = (dwMaxVideoFrameSize + sc->sc_vs_cur->psize -
		    1) / sc->sc_vs_cur->psize;
	}
	if (sc->sc_nframes > UVIDEO_NFRAMES_MAX)
		sc->sc_nframes = UVIDEO_NFRAMES_MAX;
	DPRINTF(1, "%s: nframes=%d\n", DEVNAME(sc), sc->sc_nframes);

	return (USBD_NORMAL_COMPLETION);
}

void
uvideo_vs_close(struct uvideo_softc *sc)
{
	if (sc->sc_vs_cur->bulk_running == 1) {
		sc->sc_vs_cur->bulk_running = 0;

		/* Bulk thread may sleep in usbd_transfer, abort it */
		if (sc->sc_vs_cur->pipeh)
			usbd_abort_pipe(sc->sc_vs_cur->pipeh);

		usbd_ref_wait(sc->sc_udev);
	}

	if (sc->sc_vs_cur->pipeh) {
		usbd_close_pipe(sc->sc_vs_cur->pipeh);
		sc->sc_vs_cur->pipeh = NULL;
	}

	/* No need to mess with HW if the device is gone. */
	if (usbd_is_dying(sc->sc_udev))
		return;

	if (sc->sc_vs_cur->bulk_endpoint) {
		/*
		 * UVC doesn't specify how to notify a bulk-based device
		 * when the video stream is stopped. Both, Linux and
		 * Windows send a CLEAR_FEATURE(HALT) request to the
		 * video streaming bulk endpoint.
		 */
		if (usbd_clear_endpoint_feature(sc->sc_udev,
		    sc->sc_vs_cur->endpoint, UF_ENDPOINT_HALT))
			printf("%s: clear endpoints failed!\n", __func__);
	} else {
		/*
		 * Some devices need time to shutdown before we switch back to
		 * the default interface (0).  Not doing so can leave the device
		 * back in a undefined condition.
		 */
		usbd_delay_ms(sc->sc_udev, 100);

		/* switch back to default interface (turns off cam LED) */
		(void)usbd_set_interface(sc->sc_vs_cur->ifaceh, 0);
	}
}

usbd_status
uvideo_vs_init(struct uvideo_softc *sc)
{
	usbd_status error;

	/* open video stream pipe */
	error = uvideo_vs_open(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (USBD_INVAL);

	/* allocate video stream xfer buffer */
	if (sc->sc_vs_cur->bulk_endpoint)
		error = uvideo_vs_alloc_bulk(sc);
	else
		error = uvideo_vs_alloc_isoc(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (USBD_INVAL);

	/* allocate video stream frame buffer */
	error = uvideo_vs_alloc_frame(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (USBD_INVAL);

	return (USBD_NORMAL_COMPLETION);
}

int
uvideo_vs_start_bulk(struct uvideo_softc *sc)
{
	int error;

	sc->sc_vs_cur->bulk_running = 1;

	error = kthread_create(uvideo_vs_start_bulk_thread, sc, NULL,
	    DEVNAME(sc));
	if (error) {
		printf("%s: can't create kernel thread!", DEVNAME(sc));
		return (error);
	}

	return (0);
}

void
uvideo_vs_start_bulk_thread(void *arg)
{
	struct uvideo_softc *sc = arg;
	usbd_status error;
	int size;

	usbd_ref_incr(sc->sc_udev);
	while (sc->sc_vs_cur->bulk_running) {
		size = UGETDW(sc->sc_desc_probe.dwMaxPayloadTransferSize);

		usbd_setup_xfer(
		    sc->sc_vs_cur->bxfer.xfer,
		    sc->sc_vs_cur->pipeh,
		    0,
		    sc->sc_vs_cur->bxfer.buf,
		    size,
		    USBD_NO_COPY | USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS,
		    0,
		    NULL);
		error = usbd_transfer(sc->sc_vs_cur->bxfer.xfer);
		if (error != USBD_NORMAL_COMPLETION) {
			DPRINTF(1, "%s: error in bulk xfer: %s!\n",
			    DEVNAME(sc), usbd_errstr(error));
			break;
		}

		usbd_get_xfer_status(sc->sc_vs_cur->bxfer.xfer,
		    NULL, NULL, &size, NULL);

		DPRINTF(2, "%s: *** buffer len = %d\n", DEVNAME(sc), size);

		sc->sc_decode_stream_header(sc, sc->sc_vs_cur->bxfer.buf, size);
	}
	usbd_ref_decr(sc->sc_udev);

	kthread_exit(0);
}

void
uvideo_vs_start_isoc(struct uvideo_softc *sc)
{
	int i;

	for (i = 0; i < UVIDEO_IXFERS; i++)
		uvideo_vs_start_isoc_ixfer(sc, &sc->sc_vs_cur->ixfer[i]);
}

void
uvideo_vs_start_isoc_ixfer(struct uvideo_softc *sc,
    struct uvideo_isoc_xfer *ixfer)
{
	int i;
	usbd_status error;

	DPRINTF(2, "%s: %s\n", DEVNAME(sc), __func__);

	for (i = 0; i < sc->sc_nframes; i++)
		ixfer->size[i] = sc->sc_vs_cur->psize;

	usbd_setup_isoc_xfer(
	    ixfer->xfer,
	    sc->sc_vs_cur->pipeh,
	    ixfer,
	    ixfer->size,
	    sc->sc_nframes,
	    USBD_NO_COPY | USBD_SHORT_XFER_OK,
	    uvideo_vs_cb);

	error = usbd_transfer(ixfer->xfer);
	if (error && error != USBD_IN_PROGRESS) {
		DPRINTF(1, "%s: usbd_transfer error=%s!\n",
		    DEVNAME(sc), usbd_errstr(error));
	}
}

void
uvideo_vs_cb(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct uvideo_isoc_xfer *ixfer = priv;
	struct uvideo_softc *sc = ixfer->sc;
	int len, i, frame_size;
	uint8_t *frame;

	DPRINTF(2, "%s: %s\n", DEVNAME(sc), __func__);

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF(1, "%s: %s: %s\n", DEVNAME(sc), __func__,
		    usbd_errstr(status));
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	DPRINTF(2, "%s: *** buffer len = %d\n", DEVNAME(sc), len);
	if (len == 0)
		goto skip;

	for (i = 0; i < sc->sc_nframes; i++) {
		frame = ixfer->buf + (i * sc->sc_vs_cur->psize);
		frame_size = ixfer->size[i];

		if (frame_size == 0)
			/* frame is empty */
			continue;

		sc->sc_decode_stream_header(sc, frame, frame_size);
	}

skip:	/* setup new transfer */
	uvideo_vs_start_isoc_ixfer(sc, ixfer);
}

void
uvideo_vs_decode_stream_header(struct uvideo_softc *sc, uint8_t *frame,
    int frame_size)
{
	struct uvideo_frame_buffer *fb = &sc->sc_frame_buffer;
	struct usb_video_stream_header *sh;
	int sample_len;
	uint8_t *buf;

	if (frame_size < UVIDEO_SH_MIN_LEN)
		/* frame too small to contain a valid stream header */
		return;

	sh = (struct usb_video_stream_header *)frame;

	DPRINTF(2, "%s: stream header len = %d\n", DEVNAME(sc), sh->bLength);

	if (sh->bLength > frame_size || sh->bLength < UVIDEO_SH_MIN_LEN)
		/* invalid header size */
		return;

	DPRINTF(2, "%s: frame_size = %d\n", DEVNAME(sc), frame_size);

	if (sh->bFlags & UVIDEO_SH_FLAG_FID) {
		DPRINTF(2, "%s: %s: FID ON (0x%02x)\n",
		    DEVNAME(sc), __func__,
		    sh->bFlags & UVIDEO_SH_FLAG_FID);
	} else {
		DPRINTF(2, "%s: %s: FID OFF (0x%02x)\n",
		    DEVNAME(sc), __func__,
		    sh->bFlags & UVIDEO_SH_FLAG_FID);
	}

	if (fb->sample == 0) {
		/* first sample for a frame */
		fb->sample = 1;
		fb->fid = sh->bFlags & UVIDEO_SH_FLAG_FID;
		fb->offset = 0;
		fb->error = 0;
		fb->mmap_q_full = 0;
	} else {
		/* continues sample for a frame, check consistency */
		if (fb->fid != (sh->bFlags & UVIDEO_SH_FLAG_FID)) {
			DPRINTF(1, "%s: %s: wrong FID, ignore last frame!\n",
			    DEVNAME(sc), __func__);
			fb->sample = 1;
			fb->fid = sh->bFlags & UVIDEO_SH_FLAG_FID;
			fb->offset = 0;
			fb->error = 0;
			fb->mmap_q_full = 0;
		}
	}

	if (sh->bFlags & UVIDEO_SH_FLAG_ERR) {
		/* stream error, skip xfer */
		DPRINTF(1, "%s: %s: stream error!\n", DEVNAME(sc), __func__);
		fb->error = 1;
	}

	if (sc->sc_mmap_flag) {
		if (!fb->mmap_q_full) {
			buf = uvideo_mmap_getbuf(sc);
			if (buf == NULL)
				fb->mmap_q_full = 1;
		}
	} else
		buf = sc->sc_frame_buffer.buf;

	/* save sample */
	sample_len = frame_size - sh->bLength;
	if (sample_len > fb->buf_size - fb->offset) {
		DPRINTF(1, "%s: %s: frame too large, marked as error\n",
		    DEVNAME(sc), __func__);
		sample_len = fb->buf_size - fb->offset;
		fb->error = 1;
	}
	if (!fb->mmap_q_full && sample_len > 0) {
		bcopy(frame + sh->bLength, buf + fb->offset, sample_len);
		fb->offset += sample_len;
	}

	if (sh->bFlags & UVIDEO_SH_FLAG_EOF) {
		/* got a full frame */
		DPRINTF(2, "%s: %s: EOF (frame size = %d bytes)\n",
		    DEVNAME(sc), __func__, fb->offset);

		if (fb->offset < fb->buf_size &&
		    !(fb->fmt_flags & V4L2_FMT_FLAG_COMPRESSED)) {
			DPRINTF(1, "%s: %s: frame too small, marked as error\n",
			    DEVNAME(sc), __func__);
			fb->error = 1;
		}

		if (sc->sc_mmap_flag) {
			/* mmap */
			if (!fb->mmap_q_full)
				uvideo_mmap_queue(sc, fb->offset, fb->error);
		} else if (fb->error) {
			DPRINTF(1, "%s: %s: error frame, skipped!\n",
			    DEVNAME(sc), __func__);
		} else {
			/* read */
			uvideo_read(sc, fb->buf, fb->offset);
		}

		fb->sample = 0;
		fb->fid = 0;
		fb->error = 0;
		fb->mmap_q_full = 0;
	}
}

/*
 * XXX Doesn't work yet.  Fix it!
 *
 * The iSight first generation device uses a own, non-standard streaming
 * protocol.  The stream header is just sent once per image and looks
 * like following:
 *
 *	uByte 	header length
 *	uByte	flags
 *	uByte	magic1[4]	always "11223344"
 *	uByte	magic2[8]	always "deadbeefdeadface"
 *	uByte	unknown[16]
 *
 * Sometimes the stream header is prefixed by a unknown byte.  Therefore
 * we check for the magic value on two offsets.
 */
void
uvideo_vs_decode_stream_header_isight(struct uvideo_softc *sc, uint8_t *frame,
    int frame_size)
{
	struct uvideo_frame_buffer *fb = &sc->sc_frame_buffer;
	int sample_len, header = 0;
	uint8_t *buf;
	uint8_t magic[] = {
	    0x11, 0x22, 0x33, 0x44,
	    0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xfa, 0xce };

	if (frame_size > 13 && !memcmp(&frame[2], magic, 12))
		header = 1;
	if (frame_size > 14 && !memcmp(&frame[3], magic, 12))
		header = 1;

	if (header && fb->fid == 0) {
		fb->fid = 1;
		return;
	}

	if (header) {
		if (sc->sc_mmap_flag) {
			/* mmap */
			if (!fb->mmap_q_full)
				uvideo_mmap_queue(sc, fb->offset, 0);
		} else {
			/* read */
			uvideo_read(sc, fb->buf, fb->offset);
		}
		fb->offset = 0;
		fb->mmap_q_full = 0;
	} else {
		if (sc->sc_mmap_flag) {
			if (!fb->mmap_q_full) {
				buf = uvideo_mmap_getbuf(sc);
				if (buf == NULL)
					fb->mmap_q_full = 1;
			}
		} else
			buf = sc->sc_frame_buffer.buf;

		/* save sample */
		sample_len = frame_size;
		if (!fb->mmap_q_full &&
		    (fb->offset + sample_len) <= fb->buf_size) {
			bcopy(frame, buf + fb->offset, sample_len);
			fb->offset += sample_len;
		}
	}
}

uint8_t *
uvideo_mmap_getbuf(struct uvideo_softc *sc)
{
	int i, idx = sc->sc_mmap_buffer_idx;

	/*
	 * Section 2.4.3.2 explicitly allows multiple frames per one
	 * transfer and multiple transfers per one frame.
	 */
	if (sc->sc_mmap_cur != NULL)
		return sc->sc_mmap_cur->buf;

	if (sc->sc_mmap_count == 0 || sc->sc_mmap_buffer == NULL)
		panic("%s: mmap buffers not allocated", __func__);

	/* find a buffer which is ready for queueing */
	for (i = 0; i < sc->sc_mmap_count; i++) {
		if (sc->sc_mmap[sc->sc_mmap_buffer_idx].v4l2_buf.flags &
		    V4L2_BUF_FLAG_QUEUED) {
			idx = sc->sc_mmap_buffer_idx;
			if (++sc->sc_mmap_buffer_idx == sc->sc_mmap_count)
				sc->sc_mmap_buffer_idx = 0;
			break;
		}
		if (++sc->sc_mmap_buffer_idx == sc->sc_mmap_count)
			sc->sc_mmap_buffer_idx = 0;
	}

	if (i == sc->sc_mmap_count) {
		DPRINTF(1, "%s: %s: mmap queue is full!\n",
		    DEVNAME(sc), __func__);
		return NULL;
	}

	sc->sc_mmap_cur = &sc->sc_mmap[idx];

	return sc->sc_mmap_cur->buf;
}

void
uvideo_mmap_queue(struct uvideo_softc *sc, int len, int err)
{
	if (sc->sc_mmap_cur == NULL)
		panic("uvideo_mmap_queue: NULL pointer!");

	/* report frame length */
	sc->sc_mmap_cur->v4l2_buf.bytesused = len;

	/* timestamp it */
	getmicrouptime(&sc->sc_mmap_cur->v4l2_buf.timestamp);
	sc->sc_mmap_cur->v4l2_buf.flags &= ~V4L2_BUF_FLAG_TIMESTAMP_MASK;
	sc->sc_mmap_cur->v4l2_buf.flags |= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	sc->sc_mmap_cur->v4l2_buf.flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	sc->sc_mmap_cur->v4l2_buf.flags |= V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
	sc->sc_mmap_cur->v4l2_buf.flags &= ~V4L2_BUF_FLAG_TIMECODE;

	/* forward error bit */
	sc->sc_mmap_cur->v4l2_buf.flags &= ~V4L2_BUF_FLAG_ERROR;
	if (err)
		sc->sc_mmap_cur->v4l2_buf.flags |= V4L2_BUF_FLAG_ERROR;

	/* queue it */
	sc->sc_mmap_cur->v4l2_buf.flags |= V4L2_BUF_FLAG_DONE;
	sc->sc_mmap_cur->v4l2_buf.flags &= ~V4L2_BUF_FLAG_QUEUED;
	SIMPLEQ_INSERT_TAIL(&sc->sc_mmap_q, sc->sc_mmap_cur, q_frames);
	sc->sc_mmap_cur = NULL;
	DPRINTF(2, "%s: %s: frame queued\n", DEVNAME(sc), __func__);

	wakeup(sc);

	/*
	 * In case userland uses poll(2), signal that we have a frame
	 * ready to dequeue.
	 */
	sc->sc_uplayer_intr(sc->sc_uplayer_arg);
}

void
uvideo_read(struct uvideo_softc *sc, uint8_t *buf, int len)
{
	/*
	 * Copy video frame to upper layer buffer and call
	 * upper layer interrupt.
	 */
	*sc->sc_uplayer_fsize = len;
	bcopy(buf, sc->sc_uplayer_fbuffer, len);
	sc->sc_uplayer_intr(sc->sc_uplayer_arg);
}

#ifdef UVIDEO_DEBUG
void
uvideo_dump_desc_all(struct uvideo_softc *sc)
{
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;

	usbd_desc_iter_init(sc->sc_udev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		printf("bLength=%d\n", desc->bLength);
		printf("bDescriptorType=0x%02x", desc->bDescriptorType);

		switch (desc->bDescriptorType) {
		case UDESC_CS_INTERFACE:
			printf(" (CS_INTERFACE)\n");

			switch (desc->bDescriptorSubtype) {
			case UDESCSUB_VC_HEADER:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				if (uvideo_desc_len(desc, 12, 11, 1, 0)) {
					printf(" (UDESCSUB_VC_HEADER)\n");
					printf("|\n");
					uvideo_dump_desc_vc_header(sc, desc);
					break;
				}
				if (uvideo_desc_len(desc, 13, 3, 0, 12)) {
					printf(" (UDESCSUB_VS_INPUT_HEADER)\n");
					printf("|\n");
					uvideo_dump_desc_input_header(sc, desc);
					break;
				}
				printf(" (unknown)\n");
				break;
			case UDESCSUB_VC_INPUT_TERMINAL:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VC_INPUT_TERMINAL)\n");
				printf("|\n");
				uvideo_dump_desc_input(sc, desc);
				break;
			case UDESCSUB_VC_OUTPUT_TERMINAL:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VC_OUTPUT)\n");
				printf("|\n");
				uvideo_dump_desc_output(sc, desc);
				break;
			case UDESCSUB_VC_SELECTOR_UNIT:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				if (desc->bLength == UVIDEO_FORMAT_LEN(desc)) {
					printf(" (UDESCSUB_VS_FORMAT_"
					    "UNCOMPRESSED)\n");
					uvideo_dump_desc_format_uncompressed(
					    sc, desc);
				} else {
					printf(" (UDESCSUB_VC_SELECTOR_"
					    "UNIT)\n");
					/* TODO */
				}
				break;
			case UDESCSUB_VC_PROCESSING_UNIT:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				if (desc->bLength > UVIDEO_FRAME_MIN_LEN(desc)) {
					printf(" (UDESCSUB_VS_FRAME_"
					    "UNCOMPRESSED)\n");
					uvideo_dump_desc_frame(sc, desc);
				} else {
					printf(" (UDESCSUB_VC_PROCESSING_"
					    "UNIT)\n");
					printf("|\n");
					uvideo_dump_desc_processing(sc, desc);
				}
				break;
			case UDESCSUB_VC_EXTENSION_UNIT:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				if (desc->bLength == UVIDEO_FORMAT_LEN(desc)) {
					printf(" (UDESCSUB_VS_FORMAT_MJPEG)\n");
					printf("|\n");
					uvideo_dump_desc_format_mjpeg(sc, desc);
				} else {
					printf(" (UDESCSUB_VC_EXTENSION_"
					    "UNIT)\n");
					printf("|\n");
					uvideo_dump_desc_extension(sc, desc);
				}
				break;
			case UDESCSUB_VS_FRAME_MJPEG:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_FRAME_MJPEG)\n");
				if (desc->bLength > UVIDEO_FRAME_MIN_LEN(desc)) {
					printf("|\n");
					uvideo_dump_desc_frame(sc, desc);
				}
				break;
			case UDESCSUB_VS_FORMAT_FRAME_BASED:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_FORMAT_FRAME_BASED)\n");
				if (desc->bLength == UVIDEO_FORMAT_LEN(desc)) {
					printf("|\n");
					uvideo_dump_desc_format_frame_based(sc, desc);
				}
				break;
			case UDESCSUB_VS_FRAME_FRAME_BASED:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_FRAME_FRAME_BASED)\n");
				if (desc->bLength > UVIDEO_FRAME_MIN_LEN(desc)) {
					printf("|\n");
					uvideo_dump_desc_frame_based_frame(sc, desc);
				}
				break;
			case UDESCSUB_VS_FORMAT_H264:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_FORMAT_H264)\n");
				if (desc->bLength == UVIDEO_FORMAT_LEN(desc)) {
					printf("|\n");
					uvideo_dump_desc_format_h264(sc, desc);
				}
				break;
			case UDESCSUB_VS_FRAME_H264:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_FRAME_H264)\n");
				if (desc->bLength > UVIDEO_FRAME_MIN_LEN(desc)) {
					printf("|\n");
					uvideo_dump_desc_h264_frame(sc, desc);
				}
				break;
			case UDESCSUB_VS_FORMAT_H264_SIMULCAST:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_FORMAT_H264_SIMULCAST)\n");
				if (desc->bLength == UVIDEO_FORMAT_LEN(desc)) {
					printf("|\n");
					uvideo_dump_desc_format_h264(sc, desc);
				}
				break;
			case UDESCSUB_VS_COLORFORMAT:
				printf("bDescriptorSubtype=0x%02x",
				   desc->bDescriptorSubtype);
				printf(" (UDESCSUB_VS_COLORFORMAT)\n");
				printf("|\n");
				uvideo_dump_desc_colorformat(sc, desc);
				break;
			default:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (unknown)\n");
				break;

			}

			break;
		case UDESC_CS_ENDPOINT:
			printf(" (UDESC_CS_ENDPOINT)\n");

			switch (desc->bDescriptorSubtype) {
			case EP_INTERRUPT:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (EP_INTERRUPT)\n");
				printf("|\n");
				uvideo_dump_desc_cs_endpoint(sc, desc);
				break;
			case EP_GENERAL:
				printf("bDescriptorSubtype=0x%02x",
				    desc->bDescriptorSubtype);
				printf(" (EP_GENERAL)\n");
				printf("|\n");
				uvideo_dump_desc_cs_endpoint(sc, desc);
				break;
			}

			break;
		case UDESC_CONFIG:
			printf(" (UDESC_CONFIG)\n");
			printf("|\n");
			uvideo_dump_desc_config(sc, desc);
			break;
		case UDESC_ENDPOINT:
			printf(" (UDESC_ENDPOINT)\n");
			printf("|\n");
			uvideo_dump_desc_endpoint(sc, desc);
			break;
		case UDESC_ENDPOINT_SS_COMP:
			printf(" (UDESC_ENDPOINT_SS_COMP)\n");
			printf("|\n");
			uvideo_dump_desc_endpoint_ss_comp(sc, desc);
			break;
		case UDESC_INTERFACE:
			printf(" (UDESC_INTERFACE)\n");
			printf("|\n");
			uvideo_dump_desc_interface(sc, desc);
			break;
		case UDESC_IFACE_ASSOC:
			printf(" (UDESC_IFACE_ASSOC)\n");
			printf("|\n");
			uvideo_dump_desc_iface_assoc(sc, desc);
			break;
		default:
			printf(" (unknown)\n");
			break;
		}

		printf("\n");

		desc = usbd_desc_iter_next(&iter);
	}	

}

void
uvideo_dump_desc_vc_header(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	int i;
	uByte *baInterfaceNr;
	struct usb_video_header_desc *d;

	d = (struct usb_video_header_desc *)(uint8_t *)desc;

	baInterfaceNr = (uByte *)(d + 1);

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bcdUVC=0x%04x\n", UGETW(d->bcdUVC));
	printf("wTotalLength=%d\n", UGETW(d->wTotalLength));
	printf("dwClockFrequency=%d\n", UGETDW(d->dwClockFrequency));
	printf("bInCollection=0x%02x\n", d->bInCollection);
	for (i = 0; i < d->bInCollection; i++)
		printf("baInterfaceNr[%d]=0x%02x\n", i, baInterfaceNr[i]);
}

void
uvideo_dump_desc_input_header(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_input_header_desc *d;

	d = (struct usb_video_input_header_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bNumFormats=%d\n", d->bNumFormats);
	printf("wTotalLength=%d\n", UGETW(d->wTotalLength));
	printf("bEndpointAddress=0x%02x\n", d->bEndpointAddress);
	printf("bmInfo=0x%02x\n", d->bmInfo);
	printf("bTerminalLink=0x%02x\n", d->bTerminalLink);
	printf("bStillCaptureMethod=0x%02x\n", d->bStillCaptureMethod);
	printf("bTriggerSupport=0x%02x\n", d->bTriggerSupport);
	printf("bTriggerUsage=0x%02x\n", d->bTriggerUsage);
	printf("bControlSize=%d\n", d->bControlSize);
}

void
uvideo_dump_desc_input(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_input_terminal_desc *d;

	d = (struct usb_video_input_terminal_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bTerminalID=0x%02x\n", d->bTerminalID);
	printf("wTerminalType=0x%04x\n", UGETW(d->wTerminalType));
	printf("bAssocTerminal=0x%02x\n", d->bAssocTerminal);
	printf("iTerminal=0x%02x\n", d->iTerminal);
}

void
uvideo_dump_desc_output(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_output_terminal_desc *d;

	d = (struct usb_video_output_terminal_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bTerminalID=0x%02x\n", d->bTerminalID);
	printf("bAssocTerminal=0x%02x\n", d->bAssocTerminal);
	printf("bSourceID=0x%02x\n", d->bSourceID);
	printf("iTerminal=0x%02x\n", d->iTerminal);

}

void
uvideo_dump_desc_endpoint(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	usb_endpoint_descriptor_t *d;

	d = (usb_endpoint_descriptor_t *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bEndpointAddress=0x%02x", d->bEndpointAddress);
	if (UE_GET_DIR(d->bEndpointAddress) == UE_DIR_IN)
		printf(" (IN)\n");
	if (UE_GET_DIR(d->bEndpointAddress) == UE_DIR_OUT)
		printf(" (OUT)\n");
	printf("bmAttributes=0x%02x", d->bmAttributes);
	if (UE_GET_XFERTYPE(d->bmAttributes) == UE_ISOCHRONOUS) {
		printf(" (UE_ISOCHRONOUS,");
		if (UE_GET_ISO_TYPE(d->bmAttributes) == UE_ISO_ASYNC)
			printf(" UE_ISO_ASYNC)\n");
		if (UE_GET_ISO_TYPE(d->bmAttributes) == UE_ISO_ADAPT)
			printf(" UE_ISO_ADAPT)\n");
		if (UE_GET_ISO_TYPE(d->bmAttributes) == UE_ISO_SYNC)
			printf(" UE_ISO_SYNC)\n");
	}
	if (UE_GET_XFERTYPE(d->bmAttributes) == UE_CONTROL)
		printf(" (UE_CONTROL)\n");
	if (UE_GET_XFERTYPE(d->bmAttributes) == UE_BULK)
		printf(" (UE_BULK)\n");
	if (UE_GET_XFERTYPE(d->bmAttributes) == UE_INTERRUPT)
		printf(" (UE_INTERRUPT)\n");
	printf("wMaxPacketSize=%d\n", UGETW(d->wMaxPacketSize));
	printf("bInterval=0x%02x\n", d->bInterval);
}

void
uvideo_dump_desc_endpoint_ss_comp(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	usb_endpoint_ss_comp_descriptor_t *d;

	d = (usb_endpoint_ss_comp_descriptor_t *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bMaxBurst=0x%02x\n", d->bMaxBurst);
	printf("bmAttributes=0x%02x\n", d->bmAttributes);
	printf("wBytesPerInterval=%d\n", UGETW(d->wBytesPerInterval));
}

void
uvideo_dump_desc_iface_assoc(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	usb_interface_assoc_descriptor_t *d;

	d = (usb_interface_assoc_descriptor_t *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bFirstInterface=0x%02x\n", d->bFirstInterface);
	printf("bInterfaceCount=%d\n", d->bInterfaceCount);
	printf("bFunctionClass=0x%02x\n", d->bFunctionClass);
	printf("bFunctionSubClass=0x%02x\n", d->bFunctionSubClass);
	printf("bFunctionProtocol=0x%02x\n", d->bFunctionProtocol);
	printf("iFunction=0x%02x\n", d->iFunction);
}

void
uvideo_dump_desc_interface(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	usb_interface_descriptor_t *d;

	d = (usb_interface_descriptor_t *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bInterfaceNumber=0x%02x\n", d->bInterfaceNumber);
	printf("bAlternateSetting=0x%02x\n", d->bAlternateSetting);
	printf("bNumEndpoints=%d\n", d->bNumEndpoints);
	printf("bInterfaceClass=0x%02x\n", d->bInterfaceClass);
	printf("bInterfaceSubClass=0x%02x\n", d->bInterfaceSubClass);
	printf("bInterfaceProtocol=0x%02x\n", d->bInterfaceProtocol);
	printf("iInterface=0x%02x\n", d->iInterface);
}

void
uvideo_dump_desc_config(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	usb_config_descriptor_t *d;

	d = (usb_config_descriptor_t *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("wTotalLength=%d\n", UGETW(d->wTotalLength));
	printf("bNumInterfaces=0x%02x\n", d->bNumInterfaces);
	printf("bConfigurationValue=0x%02x\n", d->bConfigurationValue);
	printf("iConfiguration=0x%02x\n", d->iConfiguration);
	printf("bmAttributes=0x%02x\n", d->bmAttributes);
	printf("bMaxPower=0x%02x\n", d->bMaxPower);
}

void
uvideo_dump_desc_cs_endpoint(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_vc_endpoint_desc *d;

	d = (struct usb_video_vc_endpoint_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("wMaxTransferSize=%d\n", UGETW(d->wMaxTransferSize));
}

void
uvideo_dump_desc_colorformat(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_color_matching_descr *d;

	d = (struct usb_video_color_matching_descr *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bColorPrimaries=0x%02x\n", d->bColorPrimaries);
	printf("bTransferCharacteristics=0x%02x\n",
	    d->bTransferCharacteristics);
	printf("bMatrixCoefficients=0x%02x\n", d->bMatrixCoefficients);
}

void
uvideo_dump_desc_format_mjpeg(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_desc *d;

	d = (struct usb_video_format_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFormatIndex=0x%02x\n", d->bFormatIndex);
	printf("bNumFrameDescriptors=0x%02x\n", d->bNumFrameDescriptors);
	printf("bmFlags=0x%02x\n", d->u.mjpeg.bmFlags);
	printf("bDefaultFrameIndex=0x%02x\n", d->u.mjpeg.bDefaultFrameIndex);
	printf("bAspectRatioX=0x%02x\n", d->u.mjpeg.bAspectRatioX);
	printf("bAspectRatioY=0x%02x\n", d->u.mjpeg.bAspectRatioY);
	printf("bmInterlaceFlags=0x%02x\n", d->u.mjpeg.bmInterlaceFlags);
	printf("bCopyProtect=0x%02x\n", d->u.mjpeg.bCopyProtect);
}

void
uvideo_dump_desc_frame(struct uvideo_softc *sc, const usb_descriptor_t *desc)
{
	struct usb_video_frame_desc *d;
	uint8_t *p;
	int length, i;

	d = (struct usb_video_frame_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFrameIndex=0x%02x\n", d->bFrameIndex);
	printf("bmCapabilities=0x%02x\n", d->u.uc.bmCapabilities);
	printf("wWidth=%d\n", UGETW(d->u.uc.wWidth));
	printf("wHeight=%d\n", UGETW(d->u.uc.wHeight));
	printf("dwMinBitRate=%d\n", UGETDW(d->u.uc.dwMinBitRate));
	printf("dwMaxBitRate=%d\n", UGETDW(d->u.uc.dwMaxBitRate));
	printf("dwMaxVideoFrameBufferSize=%d\n",
	    UGETDW(d->u.uc.dwMaxVideoFrameBufferSize));
	printf("dwDefaultFrameInterval=%d\n",
	    UGETDW(d->u.uc.dwDefaultFrameInterval));
	printf("bFrameIntervalType=0x%02x\n", d->u.uc.bFrameIntervalType);

	p = (uint8_t *)d + UVIDEO_FRAME_MIN_LEN(d);

	if (!d->u.uc.bFrameIntervalType) {
		/* continuous */
		if (d->bLength < UVIDEO_FRAME_MIN_LEN(d) +
		    (sizeof(uDWord) * 3)) {
			printf("invalid frame descriptor length\n");
		} else {
			printf("dwMinFrameInterval = %d\n", UGETDW(p));
			p += sizeof(uDWord);
			printf("dwMaxFrameInterval = %d\n", UGETDW(p));
			p += sizeof(uDWord);
			printf("dwFrameIntervalStep = %d\n", UGETDW(p));
			p += sizeof(uDWord);
		}
	} else {
		/* discrete */
		length = d->bLength - UVIDEO_FRAME_MIN_LEN(d);
		for (i = 0; i < d->u.uc.bFrameIntervalType; i++) {
			if (length <= 0) {
				printf("frame descriptor ended early\n");
				break;
			}
			printf("dwFrameInterval = %d\n", UGETDW(p));
			p += sizeof(uDWord);
			length -= sizeof(uDWord);
		}
	}
}

void
uvideo_dump_desc_format_uncompressed(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_desc *d;

	d = (struct usb_video_format_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFormatIndex=0x%02x\n", d->bFormatIndex);
	printf("bNumFrameDescriptors=0x%02x\n", d->bNumFrameDescriptors);
	printf("guidFormat=%s\n", d->u.uc.guidFormat);
	printf("bBitsPerPixel=0x%02x\n", d->u.uc.bBitsPerPixel);
	printf("bDefaultFrameIndex=0x%02x\n", d->u.uc.bDefaultFrameIndex);
	printf("bAspectRatioX=0x%02x\n", d->u.uc.bAspectRatioX);
	printf("bAspectRatioY=0x%02x\n", d->u.uc.bAspectRatioY);
	printf("bmInterlaceFlags=0x%02x\n", d->u.uc.bmInterlaceFlags);
	printf("bCopyProtect=0x%02x\n", d->u.uc.bCopyProtect);
}

void
uvideo_dump_desc_format_frame_based(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_desc *d;

	d = (struct usb_video_format_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFormatIndex=0x%02x\n", d->bFormatIndex);
	printf("bNumFrameDescriptors=0x%02x\n", d->bNumFrameDescriptors);
	printf("guidFormat=%s\n", d->u.fb.guidFormat);
	printf("bBitsPerPixel=0x%02x\n", d->u.fb.bBitsPerPixel);
	printf("bDefaultFrameIndex=0x%02x\n", d->u.fb.bDefaultFrameIndex);
	printf("bAspectRatioX=0x%02x\n", d->u.fb.bAspectRatioX);
	printf("bAspectRatioY=0x%02x\n", d->u.fb.bAspectRatioY);
	printf("bmInterlaceFlags=0x%02x\n", d->u.fb.bmInterlaceFlags);
	printf("bCopyProtect=0x%02x\n", d->u.fb.bCopyProtect);
	printf("bVariableSize=0x%02x\n", d->u.fb.bVariableSize);
}

void
uvideo_dump_desc_frame_based_frame(struct uvideo_softc *sc, const usb_descriptor_t *desc)
{
	struct usb_video_frame_desc *d;
	uint8_t *p;
	int length, i;

	d = (struct usb_video_frame_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFrameIndex=0x%02x\n", d->bFrameIndex);
	printf("bmCapabilities=0x%02x\n", d->u.fb.bmCapabilities);
	printf("wWidth=%d\n", UGETW(d->u.fb.wWidth));
	printf("wHeight=%d\n", UGETW(d->u.fb.wHeight));
	printf("dwMinBitRate=%d\n", UGETDW(d->u.fb.dwMinBitRate));
	printf("dwMaxBitRate=%d\n", UGETDW(d->u.fb.dwMaxBitRate));
	printf("dwDefaultFrameInterval=%d\n",
	    UGETDW(d->u.fb.dwDefaultFrameInterval));
	printf("bFrameIntervalType=0x%02x\n", d->u.fb.bFrameIntervalType);
	printf("dwBytesPerLine=%d\n",
	    UGETDW(d->u.fb.dwBytesPerLine));

	p = (uint8_t *)d + UVIDEO_FRAME_MIN_LEN(d);

	if (!d->u.uc.bFrameIntervalType) {
		/* continuous */
		if (d->bLength < UVIDEO_FRAME_MIN_LEN(d) +
		    (sizeof(uDWord) * 3)) {
			printf("invalid frame descriptor length\n");
		} else {
			printf("dwMinFrameInterval = %d\n", UGETDW(p));
			p += sizeof(uDWord);
			printf("dwMaxFrameInterval = %d\n", UGETDW(p));
			p += sizeof(uDWord);
			printf("dwFrameIntervalStep = %d\n", UGETDW(p));
			p += sizeof(uDWord);
		}
	} else {
		/* discrete */
		length = d->bLength - UVIDEO_FRAME_MIN_LEN(d);
		for (i = 0; i < d->u.uc.bFrameIntervalType; i++) {
			if (length <= 0) {
				printf("frame descriptor ended early\n");
				break;
			}
			printf("dwFrameInterval = %d\n", UGETDW(p));
			p += sizeof(uDWord);
			length -= sizeof(uDWord);
		}
	}
}

void
uvideo_dump_desc_format_h264(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_format_desc *d;

	d = (struct usb_video_format_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFormatIndex=0x%02x\n", d->bFormatIndex);
	printf("bNumFrameDescriptors=0x%02x\n", d->bNumFrameDescriptors);
	printf("bDefaultFrameIndex=0x%02x\n", d->u.h264.bDefaultFrameIndex);
	printf("bMaxCodecConfigDelay=0x%02x\n", d->u.h264.bMaxCodecConfigDelay);
	printf("bmSupportedSliceModes=0x%02x\n", d->u.h264.bmSupportedSliceModes);
	printf("bmSupportedSyncFrameTypes=0x%02x\n",
	    d->u.h264.bmSupportedSyncFrameTypes);
	printf("bmSupportedRateControlModes=0x%02x\n",
	    d->u.h264.bmSupportedRateControlModes);
	printf("wMaxMBperSecOneResolutionNoScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecOneResolutionNoScalability));
	printf("wMaxMBperSecTwoResolutionsNoScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecTwoResolutionsNoScalability));
	printf("wMaxMBperSecThreeResolutionsNoScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecThreeResolutionsNoScalability));
	printf("wMaxMBperSecFourResolutionsNoScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecFourResolutionsNoScalability));
	printf("wMaxMBperSecOneResolutionTemporalScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecOneResolutionTemporalScalability));
	printf("wMaxMBperSecTwoResolutionsTemporalScalablility=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecTwoResolutionsTemporalScalablility));
	printf("wMaxMBperSecThreeResolutionsTemporalScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecThreeResolutionsTemporalScalability));
	printf("wMaxMBperSecFourResolutionsTemporalScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecFourResolutionsTemporalScalability));
	printf("wMaxMBperSecOneResolutionTemporalQualityScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecOneResolutionTemporalQualityScalability));
	printf("wMaxMBperSecTwoResolutionsTemporalQualityScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecTwoResolutionsTemporalQualityScalability));
	printf("wMaxMBperSecThreeResolutionsTemporalQualityScalablity=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecThreeResolutionsTemporalQualityScalablity));
	printf("wMaxMBperSecFourResolutionsTemporalQualityScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecFourResolutionsTemporalQualityScalability));
	printf("wMaxMBperSecOneResolutionTemporalSpatialScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecOneResolutionTemporalSpatialScalability));
	printf("wMaxMBperSecTwoResolutionsTemporalSpatialScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecTwoResolutionsTemporalSpatialScalability));
	printf("wMaxMBperSecThreeResolutionsTemporalSpatialScalablity=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecThreeResolutionsTemporalSpatialScalablity));
	printf("wMaxMBperSecFourResolutionsTemporalSpatialScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecFourResolutionsTemporalSpatialScalability));
	printf("wMaxMBperSecOneResolutionFullScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecOneResolutionFullScalability));
	printf("wMaxMBperSecTwoResolutionsFullScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecTwoResolutionsFullScalability));
	printf("wMaxMBperSecThreeResolutionsFullScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecThreeResolutionsFullScalability));
	printf("wMaxMBperSecFourResolutionsFullScalability=%d\n",
	    UGETW(d->u.h264.wMaxMBperSecFourResolutionsFullScalability));
}

void
uvideo_dump_desc_h264_frame(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_frame_desc *d;
	uint8_t *p;
	int length, i;

	d = (struct usb_video_frame_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bFrameIndex=0x%02x\n", d->bFrameIndex);
	printf("wWidth=%d\n", UGETW(d->u.h264.wWidth));
	printf("wHeight=%d\n", UGETW(d->u.h264.wHeight));
	printf("wSARwidth=%d\n", UGETW(d->u.h264.wSARwidth));
	printf("wSARheight=%d\n", UGETW(d->u.h264.wSARheight));
	printf("wProfile=0x%04x\n", UGETW(d->u.h264.wProfile));
	printf("bLevelIDC=0x%02x\n", d->u.h264.bLevelIDC);
	printf("wConstrainedToolset=0x%04d\n",
	    UGETW(d->u.h264.wConstrainedToolset));
	printf("bmSupportedUsages=0x%08x\n",
	    UGETDW(d->u.h264.bmSupportedUsages));
	printf("bmCapabilities=0x%04d\n",
	    UGETW(d->u.h264.bmCapabilities));
	printf("bmSVCCapabilities=0x%08x\n",
	    UGETDW(d->u.h264.bmSVCCapabilities));
	printf("bmMVCCapabilities=0x%08x\n",
	    UGETDW(d->u.h264.bmMVCCapabilities));
	printf("dwMinBitRate=%d\n", UGETDW(d->u.h264.dwMinBitRate));
	printf("dwMaxBitRate=%d\n", UGETDW(d->u.h264.dwMaxBitRate));
	printf("dwDefaultFrameInterval=%d\n",
	    UGETDW(d->u.h264.dwDefaultFrameInterval));
	printf("bNumFrameIntervals=0x%02x\n",
	    d->u.h264.bNumFrameIntervals);

	p = (uint8_t *)d + UVIDEO_FRAME_MIN_LEN(d);

	length = d->bLength - UVIDEO_FRAME_MIN_LEN(d);
	for (i = 0; i < d->u.h264.bNumFrameIntervals; i++) {
		if (length <= 0) {
			printf("frame descriptor ended early\n");
			break;
		}
		printf("dwFrameInterval = %d\n", UGETDW(p));
		p += sizeof(uDWord);
		length -= sizeof(uDWord);
	}
}

void
uvideo_dump_desc_processing(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_vc_processing_desc *d;

	/* PU descriptor is variable sized */
	d = (void *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bUnitID=0x%02x\n", d->bUnitID);
	printf("bSourceID=0x%02x\n", d->bSourceID);
	printf("wMaxMultiplier=%d\n", UGETW(d->wMaxMultiplier));
	printf("bControlSize=%d\n", d->bControlSize);
	printf("bmControls=0x");
	uvideo_hexdump(d->bmControls, d->bControlSize, 1);
	printf("iProcessing=0x%02x\n", d->bmControls[d->bControlSize]);
	printf("bmVideoStandards=0x%02x\n", d->bmControls[d->bControlSize + 1]);
}

void
uvideo_dump_desc_extension(struct uvideo_softc *sc,
    const usb_descriptor_t *desc)
{
	struct usb_video_vc_extension_desc *d;

	d = (struct usb_video_vc_extension_desc *)(uint8_t *)desc;

	printf("bLength=%d\n", d->bLength);
	printf("bDescriptorType=0x%02x\n", d->bDescriptorType);
	printf("bDescriptorSubtype=0x%02x\n", d->bDescriptorSubtype);
	printf("bUnitID=0x%02x\n", d->bUnitID);
	printf("guidExtensionCode=0x");
	uvideo_hexdump(d->guidExtensionCode, sizeof(d->guidExtensionCode), 1);
	printf("bNumControls=0x%02x\n", d->bNumControls);
	printf("bNrInPins=0x%02x\n", d->bNrInPins);
}

void
uvideo_hexdump(void *buf, int len, int quiet)
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
#endif

/*
 * IOCTL's
 */
int
uvideo_querycap(void *v, struct v4l2_capability *caps)
{
	struct uvideo_softc *sc = v;

	bzero(caps, sizeof(*caps));
	strlcpy(caps->driver, "uvideo", sizeof(caps->driver));
	strlcpy(caps->card, sc->sc_udev->product, sizeof(caps->card));
	strlcpy(caps->bus_info, DEVNAME(sc), sizeof(caps->bus_info));

	caps->version = 1;
	caps->device_caps = V4L2_CAP_VIDEO_CAPTURE
	    | V4L2_CAP_STREAMING
	    | V4L2_CAP_READWRITE;
	caps->capabilities = caps->device_caps | V4L2_CAP_DEVICE_CAPS;

	return (0);
}

int
uvideo_enum_fmt(void *v, struct v4l2_fmtdesc *fmtdesc)
{
	struct uvideo_softc *sc = v;
	int idx;

	if (fmtdesc->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		/* type not supported */
		return (EINVAL);

	if (fmtdesc->index >= sc->sc_fmtgrp_num)
		/* no more formats left */
		return (EINVAL);
	idx = fmtdesc->index;

	switch (sc->sc_fmtgrp[idx].format->bDescriptorSubtype) {
	case UDESCSUB_VS_FORMAT_MJPEG:
		fmtdesc->flags = V4L2_FMT_FLAG_COMPRESSED;
		(void)strlcpy(fmtdesc->description, "MJPEG",
		    sizeof(fmtdesc->description));
		fmtdesc->pixelformat = V4L2_PIX_FMT_MJPEG;
		bzero(fmtdesc->reserved, sizeof(fmtdesc->reserved));
		break;
	case UDESCSUB_VS_FORMAT_UNCOMPRESSED:
		fmtdesc->flags = 0;
		fmtdesc->pixelformat = sc->sc_fmtgrp[idx].pixelformat;
		(void)strlcpy(fmtdesc->description,
		    (char *) &fmtdesc->pixelformat,
		    sizeof(fmtdesc->description));
		bzero(fmtdesc->reserved, sizeof(fmtdesc->reserved));
		break;
	case UDESCSUB_VS_FORMAT_FRAME_BASED:
		if (sc->sc_fmtgrp[idx].format->u.fb.bVariableSize)
			fmtdesc->flags = V4L2_FMT_FLAG_COMPRESSED;
		else
			fmtdesc->flags = 0;
		fmtdesc->pixelformat = sc->sc_fmtgrp[idx].pixelformat;
		(void)strlcpy(fmtdesc->description,
		    (char *) &fmtdesc->pixelformat,
		    sizeof(fmtdesc->description));
		bzero(fmtdesc->reserved, sizeof(fmtdesc->reserved));
		break;
	case UDESCSUB_VS_FORMAT_H264:
	case UDESCSUB_VS_FORMAT_H264_SIMULCAST:
		fmtdesc->flags = V4L2_FMT_FLAG_COMPRESSED;
		(void)strlcpy(fmtdesc->description, "H264",
		    sizeof(fmtdesc->description));
		fmtdesc->pixelformat = V4L2_PIX_FMT_H264;
		bzero(fmtdesc->reserved, sizeof(fmtdesc->reserved));
		break;
	default:
		fmtdesc->flags = 0;
		(void)strlcpy(fmtdesc->description, "Unknown Format",
		    sizeof(fmtdesc->description));
		fmtdesc->pixelformat = 0;
		bzero(fmtdesc->reserved, sizeof(fmtdesc->reserved));
		break;
	}

	return (0);
}

int
uvideo_enum_fsizes(void *v, struct v4l2_frmsizeenum *fsizes)
{
	struct uvideo_softc *sc = v;
	int idx, found = 0;
	struct usb_video_frame_desc *frame = NULL;

	for (idx = 0; idx < sc->sc_fmtgrp_num; idx++) {
		if (sc->sc_fmtgrp[idx].pixelformat == fsizes->pixel_format) {
			found = 1;
			break;
		}
	}
	if (found == 0)
		return (EINVAL);

	if (fsizes->index >= sc->sc_fmtgrp[idx].frame_num)
		return (EINVAL);

	fsizes->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	frame = sc->sc_fmtgrp[idx].frame[fsizes->index];
	fsizes->discrete.width = UGETW(UVIDEO_FRAME_FIELD(frame, wWidth));
	fsizes->discrete.height = UGETW(UVIDEO_FRAME_FIELD(frame, wHeight));

	return (0);
}

int
uvideo_enum_fivals(void *v, struct v4l2_frmivalenum *fivals)
{
	struct uvideo_softc *sc = v;
	int idx;
	struct uvideo_format_group *fmtgrp = NULL;
	struct usb_video_frame_desc *frame = NULL;
	uint8_t *p;
	uint32_t width, height;

	for (idx = 0; idx < sc->sc_fmtgrp_num; idx++) {
		if (sc->sc_fmtgrp[idx].pixelformat == fivals->pixel_format) {
			fmtgrp = &sc->sc_fmtgrp[idx];
			break;
		}
	}
	if (fmtgrp == NULL)
		return (EINVAL);

	for (idx = 0; idx < fmtgrp->frame_num; idx++) {
		width = UGETW(UVIDEO_FRAME_FIELD(fmtgrp->frame[idx], wWidth));
		height = UGETW(UVIDEO_FRAME_FIELD(fmtgrp->frame[idx], wHeight));

		if (width == fivals->width && height == fivals->height) {
			frame = fmtgrp->frame[idx];
			break;
		}
	}
	if (frame == NULL)
		return (EINVAL);

	/* byte-wise pointer to start of frame intervals */
	p = (uint8_t *)frame + UVIDEO_FRAME_MIN_LEN(frame);

	if (UVIDEO_FRAME_NUM_INTERVALS(frame) == 0) {
		if (fivals->index != 0)
			return (EINVAL);
		fivals->type = V4L2_FRMIVAL_TYPE_STEPWISE;
		fivals->stepwise.min.numerator = UGETDW(p);
		fivals->stepwise.min.denominator = 10000000;
		p += sizeof(uDWord);
		fivals->stepwise.max.numerator = UGETDW(p);
		fivals->stepwise.max.denominator = 10000000;
		p += sizeof(uDWord);
		fivals->stepwise.step.numerator = UGETDW(p);
		fivals->stepwise.step.denominator = 10000000;
		p += sizeof(uDWord);
	} else {
		if (fivals->index >= UVIDEO_FRAME_NUM_INTERVALS(frame))
			return (EINVAL);
		p += sizeof(uDWord) * fivals->index;
		if (p > frame->bLength + (uint8_t *)frame) {
			printf("%s: frame desc too short?\n", __func__);
			return (EINVAL);
		}
		fivals->type = V4L2_FRMIVAL_TYPE_DISCRETE;
		fivals->discrete.numerator = UGETDW(p);
		fivals->discrete.denominator = 10000000;
	}

	return (0);
}

int
uvideo_s_fmt(void *v, struct v4l2_format *fmt)
{
	struct uvideo_softc *sc = v;
	struct uvideo_format_group *fmtgrp_save;
	struct usb_video_frame_desc *frame_save;
	struct uvideo_res r;
	int found, i;
	usbd_status error;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);

	DPRINTF(1, "%s: %s: requested width=%d, height=%d\n",
	    DEVNAME(sc), __func__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	/* search requested pixel format */
	for (found = 0, i = 0; i < sc->sc_fmtgrp_num; i++) {
		if (fmt->fmt.pix.pixelformat == sc->sc_fmtgrp[i].pixelformat) {
			found = 1;
			break;
		}
	}
	if (found == 0)
		return (EINVAL);

	/* check if the format descriptor contains frame descriptors */
	if (sc->sc_fmtgrp[i].frame_num == 0) {
		printf("%s: %s: no frame descriptors found!\n",
		    __func__, DEVNAME(sc));
		return (EINVAL);
	}

	/* search requested frame resolution */
	uvideo_find_res(sc, i, fmt->fmt.pix.width, fmt->fmt.pix.height, &r);

	/*
	 * Do negotiation.
	 */
	/* save a copy of current format group in case of negotiation fails */
	fmtgrp_save = sc->sc_fmtgrp_cur;
	frame_save = sc->sc_fmtgrp_cur->frame_cur;
	/* set new format group */
	sc->sc_fmtgrp_cur = &sc->sc_fmtgrp[i];
	sc->sc_fmtgrp[i].frame_cur = sc->sc_fmtgrp[i].frame[r.fidx];

	/* do device negotiation with commit */
	error = uvideo_vs_negotiation(sc, 1);
	if (error != USBD_NORMAL_COMPLETION) {
		sc->sc_fmtgrp_cur = fmtgrp_save;
		sc->sc_fmtgrp_cur->frame_cur = frame_save;
		return (EINVAL);
	}
	sc->sc_negotiated_flag = 1;

	/* offer closest resolution which we have found */
	fmt->fmt.pix.width = r.width;
	fmt->fmt.pix.height = r.height;

	DPRINTF(1, "%s: %s: offered width=%d, height=%d\n",
	    DEVNAME(sc), __func__, r.width, r.height);

	/* tell our frame buffer size */
	fmt->fmt.pix.sizeimage = UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize);

	return (0);
}

int
uvideo_g_fmt(void *v, struct v4l2_format *fmt)
{
	struct uvideo_softc *sc = v;
	struct usb_video_frame_desc *frame;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);

	fmt->fmt.pix.pixelformat = sc->sc_fmtgrp_cur->pixelformat;

	frame = sc->sc_fmtgrp_cur->frame_cur;
	fmt->fmt.pix.width = UGETW(UVIDEO_FRAME_FIELD(frame, wWidth));
	fmt->fmt.pix.height = UGETW(UVIDEO_FRAME_FIELD(frame, wHeight));
	fmt->fmt.pix.sizeimage = UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize);

	if (sc->sc_fmtgrp_cur->has_colorformat) {
		fmt->fmt.pix.colorspace = sc->sc_fmtgrp_cur->colorspace;
		fmt->fmt.pix.xfer_func = sc->sc_fmtgrp_cur->xfer_func;
		fmt->fmt.pix.ycbcr_enc = sc->sc_fmtgrp_cur->ycbcr_enc;

		DPRINTF(1, "%s: %s: use color format"
		    " colorspace=%d, xfer_func=%d, ycbcr_enc=%d\n",
		    DEVNAME(sc), __func__, fmt->fmt.pix.colorspace,
		    fmt->fmt.pix.xfer_func, fmt->fmt.pix.ycbcr_enc);
	}

	DPRINTF(1, "%s: %s: current width=%d, height=%d\n",
	    DEVNAME(sc), __func__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	return (0);
}

int
uvideo_s_parm(void *v, struct v4l2_streamparm *parm)
{
	struct uvideo_softc *sc = v;
	usbd_status error;

	if (parm->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		/*
		 * XXX Only whole number frame rates for now.  Frame
		 * rate is the inverse of time per frame.
		 */
		if (parm->parm.capture.timeperframe.numerator == 0 ||
		    parm->parm.capture.timeperframe.denominator == 0) {
			sc->sc_frame_rate = 0;
		} else {
			sc->sc_frame_rate =
			    parm->parm.capture.timeperframe.denominator /
			    parm->parm.capture.timeperframe.numerator;
		}
	} else
		return (EINVAL);

	/* renegotiate if necessary */
	if (sc->sc_negotiated_flag) {
		error = uvideo_vs_negotiation(sc, 1);
		if (error != USBD_NORMAL_COMPLETION)
			return (error);
	}

	return (0);
}

int
uvideo_g_parm(void *v, struct v4l2_streamparm *parm)
{
	struct uvideo_softc *sc = v;

	if (parm->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
		parm->parm.capture.capturemode = 0;
		parm->parm.capture.timeperframe.numerator =
		    UGETDW(sc->sc_desc_probe.dwFrameInterval);
		parm->parm.capture.timeperframe.denominator = 10000000;
	} else
		return (EINVAL);

	return (0);
}

int
uvideo_enum_input(void *v, struct v4l2_input *input)
{
	if (input->index != 0)
		/* XXX we just support one input for now */
		return (EINVAL);

	strlcpy(input->name, "Camera Terminal", sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return (0);
}

int
uvideo_s_input(void *v, int input)
{
	if (input != 0)
		/* XXX we just support one input for now */
		return (EINVAL);

	return (0);
}

int
uvideo_g_input(void *v, int *input)
{
	/* XXX we just support one input for now */
	*input = 0;

	return (0);
}

int
uvideo_reqbufs(void *v, struct v4l2_requestbuffers *rb)
{
	struct uvideo_softc *sc = v;
	int i, buf_size, buf_size_total;

	DPRINTF(1, "%s: %s: count=%d\n", DEVNAME(sc), __func__, rb->count);

	/* We do not support freeing buffers via reqbufs(0) */
	if (rb->count == 0)
		return (EINVAL);

	if (sc->sc_mmap_count > 0 || sc->sc_mmap_buffer != NULL) {
		DPRINTF(1, "%s: %s: mmap buffers already allocated\n",
		    DEVNAME(sc), __func__);
		return (EINVAL);
	}

	/* limit the buffers */
	if (rb->count > UVIDEO_MAX_BUFFERS)
		sc->sc_mmap_count = UVIDEO_MAX_BUFFERS;
	else
		sc->sc_mmap_count = rb->count;

	/* allocate the total mmap buffer */	
	buf_size = UGETDW(sc->sc_desc_probe.dwMaxVideoFrameSize);
	buf_size_total = sc->sc_mmap_count * buf_size;
	buf_size_total = round_page(buf_size_total); /* page align buffer */
	if (buf_size_total > MALLOC_MAX) {
		printf("%s: video frame size too large!\n", DEVNAME(sc));
		sc->sc_mmap_count = 0;
		return (EINVAL);
	}
	sc->sc_mmap_buffer = malloc(buf_size_total, M_USBDEV, M_NOWAIT);
	if (sc->sc_mmap_buffer == NULL) {
		printf("%s: can't allocate mmap buffer!\n", DEVNAME(sc));
		sc->sc_mmap_count = 0;
		return (EINVAL);
	}
	sc->sc_mmap_buffer_size = buf_size_total;
	DPRINTF(1, "%s: allocated %d bytes mmap buffer\n",
	    DEVNAME(sc), buf_size_total);

	/* fill the v4l2_buffer structure */
	for (i = 0; i < sc->sc_mmap_count; i++) {
		sc->sc_mmap[i].buf = sc->sc_mmap_buffer + (i * buf_size);

		sc->sc_mmap[i].v4l2_buf.index = i;
		sc->sc_mmap[i].v4l2_buf.m.offset = i * buf_size;
		sc->sc_mmap[i].v4l2_buf.length = buf_size;
		sc->sc_mmap[i].v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		sc->sc_mmap[i].v4l2_buf.sequence = 0;
		sc->sc_mmap[i].v4l2_buf.field = V4L2_FIELD_NONE;
		sc->sc_mmap[i].v4l2_buf.memory = V4L2_MEMORY_MMAP;
		sc->sc_mmap[i].v4l2_buf.flags = V4L2_BUF_FLAG_MAPPED;

		DPRINTF(1, "%s: %s: index=%d, offset=%d, length=%d\n",
		    DEVNAME(sc), __func__,
		    sc->sc_mmap[i].v4l2_buf.index,
		    sc->sc_mmap[i].v4l2_buf.m.offset,
		    sc->sc_mmap[i].v4l2_buf.length);
	}

	sc->sc_mmap_buffer_idx = 0;
	sc->sc_mmap_cur = NULL;

	/* tell how many buffers we have really allocated */
	rb->count = sc->sc_mmap_count;

	rb->capabilities = V4L2_BUF_CAP_SUPPORTS_MMAP;

	return (0);
}

int
uvideo_querybuf(void *v, struct v4l2_buffer *qb)
{
	struct uvideo_softc *sc = v;

	if (qb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    qb->memory != V4L2_MEMORY_MMAP ||
	    qb->index >= sc->sc_mmap_count)
		return (EINVAL);

	bcopy(&sc->sc_mmap[qb->index].v4l2_buf, qb,
	    sizeof(struct v4l2_buffer));

	DPRINTF(1, "%s: %s: index=%d, offset=%d, length=%d\n",
	    DEVNAME(sc), __func__,
	    qb->index,
	    qb->m.offset,
	    qb->length);

	return (0);
}

int
uvideo_qbuf(void *v, struct v4l2_buffer *qb)
{
	struct uvideo_softc *sc = v;

	if (qb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    qb->memory != V4L2_MEMORY_MMAP ||
	    qb->index >= sc->sc_mmap_count)
		return (EINVAL);

	sc->sc_mmap[qb->index].v4l2_buf.flags &= ~V4L2_BUF_FLAG_DONE;
	sc->sc_mmap[qb->index].v4l2_buf.flags |= V4L2_BUF_FLAG_QUEUED;

	DPRINTF(2, "%s: %s: buffer on index %d ready for queueing\n",
	    DEVNAME(sc), __func__, qb->index);

	return (0);
}

int
uvideo_dqbuf(void *v, struct v4l2_buffer *dqb)
{
	struct uvideo_softc *sc = v;
	struct uvideo_mmap *mmap;
	int error;

	if (dqb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    dqb->memory != V4L2_MEMORY_MMAP)
		return (EINVAL);

	if (SIMPLEQ_EMPTY(&sc->sc_mmap_q)) {
		/* mmap queue is empty, block until first frame is queued */
		error = tsleep_nsec(sc, 0, "vid_mmap", SEC_TO_NSEC(10));
		if (error)
			return (EINVAL);
	}

	mmap = SIMPLEQ_FIRST(&sc->sc_mmap_q);
	if (mmap == NULL)
		panic("uvideo_dqbuf: NULL pointer!");

	bcopy(&mmap->v4l2_buf, dqb, sizeof(struct v4l2_buffer));

	mmap->v4l2_buf.flags &= ~V4L2_BUF_FLAG_DONE;
	mmap->v4l2_buf.flags &= ~V4L2_BUF_FLAG_QUEUED;

	DPRINTF(2, "%s: %s: frame dequeued from index %d\n",
	    DEVNAME(sc), __func__, mmap->v4l2_buf.index);
	SIMPLEQ_REMOVE_HEAD(&sc->sc_mmap_q, q_frames);

	return (0);
}

int
uvideo_streamon(void *v, int type)
{
	struct uvideo_softc *sc = v;
	usbd_status error;

	error = uvideo_vs_init(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EINVAL);

	if (sc->sc_vs_cur->bulk_endpoint)
		uvideo_vs_start_bulk(sc);
	else
		uvideo_vs_start_isoc(sc);

	return (0);
}

int
uvideo_streamoff(void *v, int type)
{
	struct uvideo_softc *sc = v;

	uvideo_vs_close(sc);

	return (0);
}

int
uvideo_queryctrl(void *v, struct v4l2_queryctrl *qctrl)
{
	struct uvideo_softc *sc = v;
	int i, ret = 0;
	usbd_status error;
	uint8_t *ctrl_data;
	uint16_t ctrl_len;

	i = uvideo_find_ctrl(sc, qctrl->id);
	if (i == EINVAL)
		return (i);

	ctrl_len = uvideo_ctrls[i].ctrl_len;
	if (ctrl_len < 1 || ctrl_len > 2) {
		printf("%s: invalid control length: %d\n", __func__, ctrl_len);
		return (EINVAL);
	}

	ctrl_data = malloc(ctrl_len, M_USBDEV, M_WAITOK | M_CANFAIL);
	if (ctrl_data == NULL) {
		printf("%s: could not allocate control data\n", __func__);
		return (ENOMEM);
	}

	/* set type */
	qctrl->type = uvideo_ctrls[i].type;

	/* set description name */
	strlcpy(qctrl->name, uvideo_ctrls[i].name, sizeof(qctrl->name));

	/* set minimum */
	error = uvideo_vc_get_ctrl(sc, ctrl_data, GET_MIN,
	    sc->sc_desc_vc_pu_cur->bUnitID,
	    uvideo_ctrls[i].ctrl_selector, uvideo_ctrls[i].ctrl_len);
	if (error != USBD_NORMAL_COMPLETION) {
		ret = EINVAL;
		goto out;
	}
	switch (ctrl_len) {
	case 1:
		qctrl->minimum = uvideo_ctrls[i].sig ?
		    *(int8_t *)ctrl_data :
		    *ctrl_data;
		break;
	case 2:
		qctrl->minimum = uvideo_ctrls[i].sig ?
		    (int16_t)UGETW(ctrl_data) :
		    UGETW(ctrl_data);
		break;
	}

	/* set maximum */
	error = uvideo_vc_get_ctrl(sc, ctrl_data, GET_MAX,
	    sc->sc_desc_vc_pu_cur->bUnitID,
	    uvideo_ctrls[i].ctrl_selector, uvideo_ctrls[i].ctrl_len);
	if (error != USBD_NORMAL_COMPLETION) {
		ret = EINVAL;
		goto out;
	}
	switch(ctrl_len) {
	case 1:
		qctrl->maximum = uvideo_ctrls[i].sig ?
		    *(int8_t *)ctrl_data :
		    *ctrl_data;
		break;
	case 2:
		qctrl->maximum = uvideo_ctrls[i].sig ?
		    (int16_t)UGETW(ctrl_data) :
		    UGETW(ctrl_data);
		break;
	}

	/* set resolution */
	error = uvideo_vc_get_ctrl(sc, ctrl_data, GET_RES,
	    sc->sc_desc_vc_pu_cur->bUnitID,
	    uvideo_ctrls[i].ctrl_selector, uvideo_ctrls[i].ctrl_len);
	if (error != USBD_NORMAL_COMPLETION) {
		ret = EINVAL;
		goto out;
	}
	switch(ctrl_len) {
	case 1:
		qctrl->step = uvideo_ctrls[i].sig ?
		    *(int8_t *)ctrl_data:
		    *ctrl_data;
		break;
	case 2:
		qctrl->step = uvideo_ctrls[i].sig ?
		    (int16_t)UGETW(ctrl_data) :
		    UGETW(ctrl_data);
		break;
	}

	/* set default */
	error = uvideo_vc_get_ctrl(sc, ctrl_data, GET_DEF,
	    sc->sc_desc_vc_pu_cur->bUnitID,
	    uvideo_ctrls[i].ctrl_selector, uvideo_ctrls[i].ctrl_len);
	if (error != USBD_NORMAL_COMPLETION) {
		ret = EINVAL;
		goto out;
	}
	switch(ctrl_len) {
	case 1:
		qctrl->default_value = uvideo_ctrls[i].sig ?
		    *(int8_t *)ctrl_data :
		    *ctrl_data;
		break;
	case 2:
		qctrl->default_value = uvideo_ctrls[i].sig ?
		    (int16_t)UGETW(ctrl_data) :
		    UGETW(ctrl_data);
		break;
	}

	/* set flags */
	qctrl->flags = 0;

out:
	free(ctrl_data, M_USBDEV, ctrl_len);

	return (ret);
}

int
uvideo_g_ctrl(void *v, struct v4l2_control *gctrl)
{
	struct uvideo_softc *sc = v;
	int i, ret = 0;
	usbd_status error;
	uint8_t *ctrl_data;
	uint16_t ctrl_len;

	i = uvideo_find_ctrl(sc, gctrl->id);
	if (i == EINVAL)
		return (i);

	ctrl_len = uvideo_ctrls[i].ctrl_len;
	if (ctrl_len < 1 || ctrl_len > 2) {
		printf("%s: invalid control length: %d\n", __func__, ctrl_len);
		return (EINVAL);
	}

	ctrl_data = malloc(ctrl_len, M_USBDEV, M_WAITOK | M_CANFAIL);
	if (ctrl_data == NULL) {
		printf("%s: could not allocate control data\n", __func__);
		return (ENOMEM);
	}

	error = uvideo_vc_get_ctrl(sc, ctrl_data, GET_CUR,
	    sc->sc_desc_vc_pu_cur->bUnitID,
	    uvideo_ctrls[i].ctrl_selector, uvideo_ctrls[i].ctrl_len);
	if (error != USBD_NORMAL_COMPLETION) {
		ret = EINVAL;
		goto out;
	}
	switch(ctrl_len) {
	case 1:
		gctrl->value = uvideo_ctrls[i].sig ?
		    *(int8_t *)ctrl_data :
		    *ctrl_data;
		break;
	case 2:
		gctrl->value = uvideo_ctrls[i].sig ?
		    (int16_t)UGETW(ctrl_data) :
		    UGETW(ctrl_data);
		break;
	}

out:
	free(ctrl_data, M_USBDEV, ctrl_len);

	return (0);
}

int
uvideo_s_ctrl(void *v, struct v4l2_control *sctrl)
{
	struct uvideo_softc *sc = v;
	int i, ret = 0;
	usbd_status error;
	uint8_t *ctrl_data;
	uint16_t ctrl_len;

	i = uvideo_find_ctrl(sc, sctrl->id);
	if (i == EINVAL)
		return (i);

	ctrl_len = uvideo_ctrls[i].ctrl_len;
	if (ctrl_len < 1 || ctrl_len > 2) {
		printf("%s: invalid control length: %d\n", __func__, ctrl_len);
		return (EINVAL);
	}

	ctrl_data = malloc(ctrl_len, M_USBDEV, M_WAITOK | M_CANFAIL);
	if (ctrl_data == NULL) {
		printf("%s: could not allocate control data\n", __func__);
		return (ENOMEM);
	}

	switch(ctrl_len) {
	case 1:
		if (uvideo_ctrls[i].sig)
			*(int8_t *)ctrl_data = sctrl->value;
		else
			*ctrl_data = sctrl->value;
		break;
	case 2:
		USETW(ctrl_data, sctrl->value);
		break;
	}
	error = uvideo_vc_set_ctrl(sc, ctrl_data, SET_CUR,
	    sc->sc_desc_vc_pu_cur->bUnitID,
	    uvideo_ctrls[i].ctrl_selector, uvideo_ctrls[i].ctrl_len);
	if (error != USBD_NORMAL_COMPLETION)
		ret = EINVAL;

	free(ctrl_data, M_USBDEV, ctrl_len);

	return (ret);
}

int
uvideo_try_fmt(void *v, struct v4l2_format *fmt)
{
	struct uvideo_softc *sc = v;
	struct uvideo_res r;
	int found, i;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);

	DPRINTF(1, "%s: %s: requested width=%d, height=%d\n",
	    DEVNAME(sc), __func__, fmt->fmt.pix.width, fmt->fmt.pix.height);

	/* search requested pixel format */
	for (found = 0, i = 0; i < sc->sc_fmtgrp_num; i++) {
		if (fmt->fmt.pix.pixelformat == sc->sc_fmtgrp[i].pixelformat) {
			found = 1;
			break;
		}
	}
	if (found == 0)
		return (EINVAL);

	/* search requested frame resolution */
	uvideo_find_res(sc, i, fmt->fmt.pix.width, fmt->fmt.pix.height, &r);

	/* offer closest resolution which we have found */
	fmt->fmt.pix.width = r.width;
	fmt->fmt.pix.height = r.height;

	DPRINTF(1, "%s: %s: offered width=%d, height=%d\n",
	    DEVNAME(sc), __func__, r.width, r.height);

	/* tell our frame buffer size */
	fmt->fmt.pix.sizeimage = sc->sc_frame_buffer.buf_size;

	return (0);
}

caddr_t
uvideo_mappage(void *v, off_t off, int prot)
{
	struct uvideo_softc *sc = v;
	caddr_t p;

	if (off >= sc->sc_mmap_buffer_size)
		return NULL;

	if (!sc->sc_mmap_flag)
		sc->sc_mmap_flag = 1;

	p = sc->sc_mmap_buffer + off;

	return (p);
}

int
uvideo_get_bufsize(void *v)
{
	struct uvideo_softc *sc = v;

	return (sc->sc_max_fbuf_size);
}

int
uvideo_start_read(void *v)
{
	struct uvideo_softc *sc = v;
	usbd_status error;

	if (sc->sc_mmap_flag)
		sc->sc_mmap_flag = 0;

	error = uvideo_vs_init(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EINVAL);

	if (sc->sc_vs_cur->bulk_endpoint)
		uvideo_vs_start_bulk(sc);
	else
		uvideo_vs_start_isoc(sc);

	return (0);
}

usbd_status
uvideo_usb_control(struct uvideo_softc *sc, uint8_t rt, uint8_t r,
    uint16_t value, uint8_t *data, size_t length)
{
	usb_device_request_t	req;
	usbd_status		err;

	req.bmRequestType = rt;
	req.bRequest = r;
	USETW(req.wIndex, 0);
	USETW(req.wValue, value);
	USETW(req.wLength, length);

	err = usbd_do_request(sc->sc_udev, &req, data);
	if (err != USBD_NORMAL_COMPLETION)
		return (err);

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvideo_ucode_loader_ricoh(struct uvideo_softc *sc)
{
	usbd_status error;
	uint8_t *ucode, len, cbuf;
	size_t ucode_size;
	uint16_t addr;
	int offset = 0, remain;

	/* get device microcode status */
	cbuf = 0;
	error = uvideo_usb_control(sc, UT_READ_VENDOR_DEVICE,
	    0xa4, 0, &cbuf, sizeof cbuf);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: ucode status error=%s!\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (USBD_INVAL);
	}
	if (cbuf) {
		DPRINTF(1, "%s: microcode already loaded\n", DEVNAME(sc));
		return (USBD_NORMAL_COMPLETION);
	} else {
		DPRINTF(1, "%s: microcode not loaded\n", DEVNAME(sc));
	}

	/* open microcode file */
	error = loadfirmware(sc->sc_quirk->ucode_name, &ucode, &ucode_size);
	if (error != 0) {
		printf("%s: loadfirmware error=%d!\n", DEVNAME(sc), error);
		return (USBD_INVAL);
	}

	/* upload microcode */
	remain = ucode_size;
	while (remain > 0) {
		if (remain < 3) {
			printf("%s: ucode file incomplete!\n", DEVNAME(sc));
			free(ucode, M_DEVBUF, ucode_size);
			return (USBD_INVAL);
		}

		len = ucode[offset];
		addr = ucode[offset + 1] | (ucode[offset + 2] << 8);
		offset += 3;
		remain -= 3;

		error = uvideo_usb_control(sc, UT_WRITE_VENDOR_DEVICE,
		    0xa0, addr, &ucode[offset], len);
		if (error != USBD_NORMAL_COMPLETION) {
			printf("%s: ucode upload error=%s!\n",
			    DEVNAME(sc), usbd_errstr(error));
			free(ucode, M_DEVBUF, ucode_size);
			return (USBD_INVAL);
		}
		DPRINTF(1, "%s: uploaded %d bytes ucode to addr 0x%x\n",
		    DEVNAME(sc), len, addr);

		offset += len;
		remain -= len;
	}
	free(ucode, M_DEVBUF, ucode_size);

	/* activate microcode */
	cbuf = 0;
	error = uvideo_usb_control(sc, UT_WRITE_VENDOR_DEVICE,
	    0xa1, 0, &cbuf, sizeof cbuf);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: ucode activate error=%s!\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: ucode activated\n", DEVNAME(sc));

	return (USBD_NORMAL_COMPLETION);
}

/*
 * The iSight first generation device will first attach as
 * 0x8300 non-UVC.  After the firmware is uploaded, the device
 * will reset and come back as 0x8501 UVC compatible.
 */
usbd_status
uvideo_ucode_loader_apple_isight(struct uvideo_softc *sc)
{
	usbd_status error;
	uint8_t *ucode, *code, cbuf;
	size_t ucode_size;
	uint16_t len, req, off, llen;

	/* open microcode file */
	error = loadfirmware(sc->sc_quirk->ucode_name, &ucode, &ucode_size);
	if (error != 0) {
		printf("%s: loadfirmware error=%d!\n", DEVNAME(sc), error);
		return (USBD_INVAL);
	}

	/* send init request */
	cbuf = 1;
	error = uvideo_usb_control(sc, UT_WRITE_VENDOR_DEVICE, 0xa0, 0xe600,
	    &cbuf, sizeof(cbuf));
	if (error) {
		printf("%s: failed to init firmware loading state: %s\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (error);
	}

	code = ucode;
	while (code < ucode + ucode_size) {
		/* get header information */
		len = (code[0] << 8) | code[1];
		req = (code[2] << 8) | code[3];
		DPRINTF(1, "%s: ucode data len=%d, request=0x%x\n",
		    DEVNAME(sc), len, req);
		if (len < 1 || len > 1023) {
			printf("%s: ucode header contains wrong value!\n",
			    DEVNAME(sc));
			free(ucode, M_DEVBUF, ucode_size);
			return (USBD_INVAL);
		}
		code += 4;

		/* send data to device */
		for (off = 0; len > 0; req += 50, off += 50) {
			llen = len > 50 ? 50 : len;
			len -= llen;

			DPRINTF(1, "%s: send %d bytes data to offset 0x%x\n",
			    DEVNAME(sc), llen, req);
			error = uvideo_usb_control(sc, UT_WRITE_VENDOR_DEVICE,
			    0xa0, req, code, llen);
			if (error) {
				printf("%s: ucode load failed: %s\n",
				    DEVNAME(sc), usbd_errstr(error));
				free(ucode, M_DEVBUF, ucode_size);
				return (USBD_INVAL);
			}

			code += llen;
		}
	}
	free(ucode, M_DEVBUF, ucode_size);

	/* send finished request */
	cbuf = 0;
	error = uvideo_usb_control(sc, UT_WRITE_VENDOR_DEVICE, 0xa0, 0xe600,
	    &cbuf, sizeof(cbuf));
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: ucode activate error=%s!\n",
		    DEVNAME(sc), usbd_errstr(error));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: ucode activated\n", DEVNAME(sc));

	/*
	 * We will always return from the attach routine since the device
	 * will reset and re-attach at this point.
	 */
	return (USBD_INVAL);
}
