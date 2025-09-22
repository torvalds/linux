/*	$OpenBSD: uaudio.c,v 1.179 2025/07/14 23:49:08 jsg Exp $	*/
/*
 * Copyright (c) 2018 Alexandre Ratchov <alex@caoua.org>
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
 * The USB Audio Class (UAC) defines what is an audio device and how
 * to use it. There are two versions of the UAC: v1.0 and v2.0. They
 * are not compatible with each other but they are close enough to
 * attempt to have the same driver for both.
 *
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/audioio.h>
#include <machine/bus.h>
#include <dev/audio_if.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#ifdef UAUDIO_DEBUG
#define DPRINTF(...)				\
	do {					\
		if (uaudio_debug)		\
			printf(__VA_ARGS__);	\
	} while (0)
#else
#define DPRINTF(...) do {} while(0)
#endif

#define DEVNAME(sc) ((sc)->dev.dv_xname)

/*
 * Isochronous endpoint usage (XXX: these belong to dev/usb/usb.h).
 */
#define UE_ISO_USAGE			0x30
#define  UE_ISO_USAGE_DATA		0x00
#define  UE_ISO_USAGE_FEEDBACK		0x10
#define  UE_ISO_USAGE_IMPL		0x20
#define UE_GET_ISO_USAGE(a)		((a) & UE_ISO_USAGE)

/*
 * Max length of unit names
 */
#define UAUDIO_NAMEMAX			MAX_AUDIO_DEV_LEN

/*
 * USB audio class versions
 */
#define UAUDIO_V1			0x100
#define UAUDIO_V2			0x200

/*
 * AC class-specific descriptor interface sub-type
 */
#define UAUDIO_AC_HEADER		0x1
#define UAUDIO_AC_INPUT			0x2
#define UAUDIO_AC_OUTPUT		0x3
#define UAUDIO_AC_MIXER			0x4
#define UAUDIO_AC_SELECTOR		0x5
#define UAUDIO_AC_FEATURE		0x6
#define UAUDIO_AC_EFFECT		0x7
#define UAUDIO_AC_PROCESSING		0x8
#define UAUDIO_AC_EXTENSION		0x9
#define UAUDIO_AC_CLKSRC		0xa
#define UAUDIO_AC_CLKSEL		0xb
#define UAUDIO_AC_CLKMULT		0xc
#define UAUDIO_AC_RATECONV		0xd

/*
 * AC class-specific CLKSRC controls
 */
#define UAUDIO_CLKSRC_FREQCTL		0x2

/*
 * AS class-specific interface sub-types
 */
#define UAUDIO_AS_GENERAL		0x1
#define UAUDIO_AS_FORMAT		0x2

/*
 * AS class-specific endpoint sub-types
 */
#define UAUDIO_AS_EP_GENERAL		0x1

/*
 * AS class-specific endpoint sub-type
 */
#define UAUDIO_EP_GENERAL		0x1

/*
 * UAC v1 formats, wFormatTag is an enum
 */
#define UAUDIO_V1_FMT_PCM		0x1
#define UAUDIO_V1_FMT_PCM8		0x2
#define UAUDIO_V1_FMT_FLOAT		0x3
#define UAUDIO_V1_FMT_ALAW		0x4
#define UAUDIO_V1_FMT_MULAW		0x5

/*
 * UAC v2 formats, bmFormats is a bitmap
 */
#define UAUDIO_V2_FMT_PCM		0x01
#define UAUDIO_V2_FMT_PCM8		0x02
#define UAUDIO_V2_FMT_FLOAT		0x04
#define UAUDIO_V2_FMT_ALAW		0x08
#define UAUDIO_V2_FMT_MULAW		0x10

/*
 * AC requests
 */
#define UAUDIO_V1_REQ_SET_CUR		0x01
#define UAUDIO_V1_REQ_SET_MIN		0x02
#define UAUDIO_V1_REQ_SET_MAX		0x03
#define UAUDIO_V1_REQ_SET_RES		0x04
#define UAUDIO_V1_REQ_GET_CUR		0x81
#define UAUDIO_V1_REQ_GET_MIN		0x82
#define UAUDIO_V1_REQ_GET_MAX		0x83
#define UAUDIO_V1_REQ_GET_RES		0x84
#define UAUDIO_V2_REQ_CUR		1
#define UAUDIO_V2_REQ_RANGES		2

/*
 * AC request "selector control"
 */
#define UAUDIO_V2_REQSEL_CLKFREQ	1
#define UAUDIO_V2_REQSEL_CLKSEL		1

/*
 * AS class-specific endpoint attributes
 */
#define UAUDIO_EP_FREQCTL		0x01

/*
 * AC feature control selectors (aka wValue in the request)
 */
#define UAUDIO_REQSEL_MUTE		0x01
#define UAUDIO_REQSEL_VOLUME		0x02
#define UAUDIO_REQSEL_BASS		0x03
#define UAUDIO_REQSEL_MID		0x04
#define UAUDIO_REQSEL_TREBLE		0x05
#define UAUDIO_REQSEL_EQ		0x06
#define UAUDIO_REQSEL_AGC		0x07
#define UAUDIO_REQSEL_DELAY		0x08
#define UAUDIO_REQSEL_BASSBOOST		0x09
#define UAUDIO_REQSEL_LOUDNESS		0x0a
#define UAUDIO_REQSEL_GAIN		0x0b
#define UAUDIO_REQSEL_GAINPAD		0x0c
#define UAUDIO_REQSEL_PHASEINV		0x0d

/*
 * Endpoint (UAC v1) or clock-source unit (UAC v2) sample rate control
 */
#define UAUDIO_REQSEL_RATE		0x01

/*
 * Samples-per-frame are fractions. UAC v2.0 requires the denominator to
 * be multiple of 2^16, as used in the sync pipe. On the other hand, to
 * represent sample-per-frame of all rates we support, we need the
 * denominator to be such that (rate / 1000) can be represented exactly,
 * 80 works. So we use the least common multiplier of both.
 */
#define UAUDIO_SPF_DIV			327680

/*
 * names of DAC and ADC unit names
 */
#define UAUDIO_NAME_PLAY	"dac"
#define UAUDIO_NAME_REC		"record"

/*
 * read/write pointers for secure sequential access of binary data,
 * ex. usb descriptors, tables and alike. Bytes are read using the
 * read pointer up to the write pointer.
 */
struct uaudio_blob {
	unsigned char *rptr, *wptr;
};

/*
 * Ranges of integer values used to represent controls values and
 * sample frequencies.
 */
struct uaudio_ranges {
	unsigned int nval;
	struct uaudio_ranges_el {
		struct uaudio_ranges_el *next;
		int min, max, res;
	} *el;
};

struct uaudio_softc {
	struct device dev;
	struct usbd_device *udev;
	int version;

	/*
	 * UAC exposes the device as a circuit of units. Input and
	 * output jacks are known as terminal units, others are
	 * processing units. The purpose of this driver is to give
	 * them reasonable names and expose them as mixer(1)
	 * controls. Control names are derived from the type of the
	 * unit and its role in the circuit.
	 *
	 * UAC v2.0 exposes also the clock circuitry using units, so
	 * selecting the sample rate also involves units usage.
	 */
	struct uaudio_unit {
		struct uaudio_unit *unit_next, *src_next, *dst_next;
		struct uaudio_unit *src_list, *dst_list;
		char name[UAUDIO_NAMEMAX];
		unsigned int nch;
		int type, id;

		/* terminal or clock type */
		unsigned int term;

		/* clock source, if a terminal or selector */
		struct uaudio_unit *clock;

		/* sample rates, if this is a clock source */
		struct uaudio_ranges rates;
		int cap_freqctl;

		/* mixer(4) bits */
#define UAUDIO_CLASS_OUT	0
#define UAUDIO_CLASS_IN		1
#define UAUDIO_CLASS_COUNT	2
		int mixer_class;
		struct uaudio_mixent {
			struct uaudio_mixent *next;
			char *fname;
#define UAUDIO_MIX_SW	0
#define UAUDIO_MIX_NUM	1
#define UAUDIO_MIX_ENUM	2
			int type;
			int chan;
			int req_sel;
			struct uaudio_ranges ranges;
		} *mixent_list;
	} *unit_list;

	/*
	 * Current clock, UAC v2.0 only
	 */
	struct uaudio_unit *clock;

	/*
	 * Number of input and output terminals
	 */
	unsigned int nin, nout;

	/*
	 * When unique names are needed, they are generated using a
	 * base string suffixed with a number. Ex. "spkr5". The
	 * following structure is used to keep track of strings we
	 * allocated.
	 */
	struct uaudio_name {
		struct uaudio_name *next;
		char *templ;
		unsigned int unit;
	} *names;

	/*
	 * Audio streaming (AS) alternate settings, i.e. stream format
	 * and USB-related parameters to use it.
	 */
	struct uaudio_alt {
		struct uaudio_alt *next;
		int ifnum, altnum;
		int mode;		/* one of AUMODE_{RECORD,PLAY} */
		int data_addr;		/* data endpoint address */
		int sync_addr;		/* feedback endpoint address */
		int maxpkt;		/* max supported bytes per frame */
		int fps;		/* USB (micro-)frames per second */
		int bps, bits, nch;	/* audio encoding */
		int v1_rates;		/* if UAC 1.0, bitmap of rates */
		int v1_cap_freqctl;		/* can set the sample rate */
	} *alts;

	/*
	 * Audio parameters: play and record stream formats usable
	 * together.
	 */
	struct uaudio_params {
		struct uaudio_params *next;
		struct uaudio_alt *palt, *ralt;
		int v1_rates;
	} *params_list, *params;

	/*
	 * One direction audio stream, aka "DMA" in progress
	 */
	struct uaudio_stream {
#define UAUDIO_NXFERS_MIN	2
#define UAUDIO_NXFERS_MAX	8
		struct uaudio_xfer {
			struct usbd_xfer *usb_xfer;
			unsigned char *buf;
			uint16_t *sizes;
			unsigned int size;	/* bytes requested */
			unsigned int nframes;	/* frames requested */
		} data_xfers[UAUDIO_NXFERS_MAX], sync_xfers[UAUDIO_NXFERS_MAX];

		/*
		 * We don't use all the data_xfers[] entries because
		 * we can't schedule too many frames in the usb
		 * controller.
		 */
		unsigned int nxfers;

		unsigned int spf_remain;	/* frac sample left */
		unsigned int spf;		/* avg samples per frame */
		unsigned int spf_min, spf_max;	/* allowed boundaries */

		/*
		 * The max frame size we'll need (which may be lower
		 * than the maxpkt the usb pipe supports).
		 */
		unsigned int maxpkt;

		/*
		 * max number of frames per xfer we'll need
		 */
		unsigned int nframes_max;

		/*
		 * At usb2.0 speed, the number of (micro-)frames per
		 * transfer must correspond to 1ms, which is the usb1.1
		 * frame duration. This is required by lower level usb
		 * drivers.
		 *
		 * The nframes_mask variable is used to test if the
		 * number of frames per transfer is usable (by checking
		 * that least significant bits are zero). For instance,
		 * nframes_mask will be set to 0x0 on usb1.1 device and
		 * 0x7 on usb2.0 devices running at 8000 fps.
		 */
		unsigned int nframes_mask;

		unsigned int data_nextxfer, sync_nextxfer;
		struct usbd_pipe *data_pipe;
		struct usbd_pipe *sync_pipe;
		void (*intr)(void *);
		void *arg;

		/* audio ring extents, passed to trigger() methods */
		unsigned char *ring_start, *ring_end;

		/* pointer to first byte available */
		unsigned char *ring_pos;
		
		/* audio(9) block size in bytes */
		int ring_blksz;

		/* xfer position relative to block boundary */
		int ring_offs;

		/*
		 * As USB sample-per-frame is not constant, we must
		 * schedule transfers slightly larger that one audio
		 * block. This is the "safe" block size, that ensures
		 * the transfer will cross the audio block boundary.
		 */
		int safe_blksz;

		/*
		 * Number of bytes completed, when it reaches a
		 * block size, we fire an audio(9) interrupt.
		 */
		int ring_icnt;

		/*
		 * USB transfers are used as a FIFO which is the
		 * concatenation of all transfers. This is the write
		 * (read) position of the play (rec) stream
		 */
		unsigned int ubuf_xfer;		/* xfer index */
		unsigned int ubuf_pos;		/* offset in bytes */
	} pstream, rstream;

	int ctl_ifnum;			/* aka AC interface */

	int mode;			/* open() mode */
	int trigger_mode;		/* trigger() mode */

	unsigned int rate;		/* current sample rate */
	unsigned int ufps;		/* USB frames per second */
	unsigned int sync_pktsz;	/* size of sync packet */
	unsigned int host_nframes;	/* max frames we can schedule */

	int diff_nsamp;			/* samples play is ahead of rec */
	int diff_nframes;		/* frames play is ahead of rec */
	unsigned int adjspf_age;	/* frames since last uaudio_adjspf */

	/*
	 * bytes pending to be copied to transfer buffer. This is play
	 * only, as recorded frames are copied as soon they are
	 * received.
	 */
	size_t copy_todo;
};

int uaudio_match(struct device *, void *, void *);
void uaudio_attach(struct device *, struct device *, void *);
int uaudio_detach(struct device *, int);

int uaudio_open(void *, int);
void uaudio_close(void *);
int uaudio_set_params(void *, int, int, struct audio_params *,
    struct audio_params *);
unsigned int uaudio_set_blksz(void *, int,
    struct audio_params *, struct audio_params *, unsigned int);
int uaudio_trigger_output(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int uaudio_trigger_input(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
void uaudio_copy_output(void *, size_t);
void uaudio_underrun(void *);
int uaudio_halt_output(void *);
int uaudio_halt_input(void *);
int uaudio_query_devinfo(void *, struct mixer_devinfo *);
int uaudio_get_port(void *, struct mixer_ctrl *);
int uaudio_set_port(void *, struct mixer_ctrl *);

int uaudio_process_unit(struct uaudio_softc *,
    struct uaudio_unit *, int,
    struct uaudio_blob,
    struct uaudio_unit **);

void uaudio_pdata_intr(struct usbd_xfer *, void *, usbd_status);
void uaudio_rdata_intr(struct usbd_xfer *, void *, usbd_status);
void uaudio_psync_intr(struct usbd_xfer *, void *, usbd_status);

#ifdef UAUDIO_DEBUG
char *uaudio_isoname(int isotype);
char *uaudio_modename(int mode);
char *uaudio_usagename(int usage);
void uaudio_rates_print(int rates);
void uaudio_ranges_print(struct uaudio_ranges *r);
void uaudio_print_unit(struct uaudio_softc *sc, struct uaudio_unit *u);
void uaudio_mixer_print(struct uaudio_softc *sc);
void uaudio_conf_print(struct uaudio_softc *sc);

/*
 * 0 - nothing, same as if UAUDIO_DEBUG isn't defined
 * 1 - initialisations & setup
 * 2 - audio(4) calls
 * 3 - transfers
 */
int uaudio_debug = 1;
#endif

struct cfdriver uaudio_cd = {
	NULL, "uaudio", DV_DULL
};

const struct cfattach uaudio_ca = {
	sizeof(struct uaudio_softc), uaudio_match, uaudio_attach, uaudio_detach
};

const struct audio_hw_if uaudio_hw_if = {
	.open = uaudio_open,
	.close = uaudio_close,
	.set_params = uaudio_set_params,
	.halt_output = uaudio_halt_output,
	.halt_input = uaudio_halt_input,
	.set_port = uaudio_set_port,
	.get_port = uaudio_get_port,
	.query_devinfo = uaudio_query_devinfo,
	.trigger_output = uaudio_trigger_output,
	.trigger_input = uaudio_trigger_input,
	.copy_output = uaudio_copy_output,
	.underrun = uaudio_underrun,
	.set_blksz = uaudio_set_blksz,
};

/*
 * To keep things simple, we support only the following rates, we
 * don't care about continuous sample rates or other "advanced"
 * features which complicate implementation.
 */
const int uaudio_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	64000, 88200, 96000, 128000, 176400, 192000
};

/*
 * Convert 8, 16, or 24-bit signed value to an int by expanding the
 * sign bit.
 */
int
uaudio_sign_expand(unsigned int val, int opsize)
{
	unsigned int s;

	s = 1 << (8 * opsize - 1);
	return (val ^ s) - s;
}

int
uaudio_req(struct uaudio_softc *sc,
    unsigned int type,
    unsigned int req,
    unsigned int sel,
    unsigned int chan,
    unsigned int ifnum,
    unsigned int id,
    unsigned char *buf,
    size_t size)
{
	struct usb_device_request r;
	int err;

	r.bmRequestType = type;
	r.bRequest = req;
	USETW(r.wValue, sel << 8 | chan);
	USETW(r.wIndex, id << 8 | ifnum);
	USETW(r.wLength, size);

	DPRINTF("%s: type = 0x%x, req = 0x%x, val = 0x%x, "
	    "index = 0x%x, size = %d\n", __func__,
	    type, req, UGETW(r.wValue), UGETW(r.wIndex), UGETW(r.wLength));

	err = usbd_do_request(sc->udev, &r, buf);
	if (err) {
		DPRINTF("%s: failed: %s\n", __func__, usbd_errstr(err));
		return 0;
	}
	return 1;
}

/*
 * Read a number of the given size (in bytes) from the given
 * blob. Return 0 on error.
 */
int
uaudio_getnum(struct uaudio_blob *p, unsigned int size, unsigned int *ret)
{
	unsigned int i, num = 0;

	if (p->wptr - p->rptr < size) {
		DPRINTF("%s: %d: too small\n", __func__, size);
		return 0;
	}

	for (i = 0; i < size; i++)
		num |= *p->rptr++ << (8 * i);

	if (ret)
		*ret = num;
	return 1;
}

/*
 * Read a USB descriptor from the given blob. Return 0 on error.
 */
int
uaudio_getdesc(struct uaudio_blob *p, struct uaudio_blob *ret)
{
	unsigned int size;

	if (!uaudio_getnum(p, 1, &size))
		return 0;
	if (size-- == 0) {
		DPRINTF("%s: zero sized desc\n", __func__);
		return 0;
	}
	if (p->wptr - p->rptr < size) {
		DPRINTF("%s: too small\n", __func__);
		return 0;
	}
	ret->rptr = p->rptr;
	ret->wptr = p->rptr + size;
	p->rptr += size;
	return 1;
}

/*
 * Find the unit with the given id, return NULL if not found.
 */
struct uaudio_unit *
uaudio_unit_byid(struct uaudio_softc *sc, unsigned int id)
{
	struct uaudio_unit *u;

	for (u = sc->unit_list; u != NULL; u = u->unit_next) {
		if (u->id == id)
			break;
	}
	return u;
}

/*
 * Return a terminal name for the given terminal type.
 */
char *
uaudio_tname(struct uaudio_softc *sc, unsigned int type, int isout)
{
	unsigned int hi, lo;
	char *name;

	hi = type >> 8;
	lo = type & 0xff;

	/* usb data stream */
	if (hi == 1)
		return isout ? UAUDIO_NAME_REC : UAUDIO_NAME_PLAY;

	/* if there is only one input (output) use "input" ("output") */
	if (isout) {
		if (sc->nout == 1)
			return "output";
	} else {
		if (sc->nin == 1)
			return "input";
	}

	/* determine name from USB terminal type */
	switch (hi) {
	case 2:
		/* embedded inputs */
		name = isout ? "mic-out" : "mic";
		break;
	case 3:
		/* embedded outputs, mostly speakers, except 0x302 */
		switch (lo) {
		case 0x02:
			name = isout ? "hp" : "hp-in";
			break;
		default:
			name = isout ? "spkr" : "spkr-in";
			break;
		}
		break;
	case 4:
		/* handsets and headset */
		name = isout ? "spkr" : "mic";
		break;
	case 5:
		/* phone line */
		name = isout ? "phone-in" : "phone-out";
		break;
	case 6:
		/* external sources/sinks */
		switch (lo) {
		case 0x02:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x09:
		case 0x0a:
			name = isout ? "dig-out" : "dig-in";
			break;
		default:
			name = isout ? "line-out" : "line-in";
			break;
		}
		break;
	case 7:
		/* internal devices */
		name = isout ? "int-out" : "int-in";
		break;
	default:
		name = isout ? "unk-out" : "unk-in";
	}
	return name;
}

/*
 * Return a clock name for the given clock type.
 */
char *
uaudio_clkname(unsigned int attr)
{
	static char *names[] = {"ext", "fixed", "var", "prog"};

	return names[attr & 3];
}

/*
 * Return an unique name for the given template.
 */
void
uaudio_mkname(struct uaudio_softc *sc, char *templ, char *res)
{
	struct uaudio_name *n;
	char *sep;

	/*
	 * if this is not a terminal name (i.e. there's a underscore
	 * in the name, like in "spkr2_mic3"), then use underscore as
	 * separator to avoid concatenating two numbers
	 */
	sep = strchr(templ, '_') != NULL ? "_" : "";

	n = sc->names;
	while (1) {
		if (n == NULL) {
			n = malloc(sizeof(struct uaudio_name),
			    M_USBDEV, M_WAITOK);
			n->templ = templ;
			n->unit = 0;
			n->next = sc->names;
			sc->names = n;
		}
		if (strcmp(n->templ, templ) == 0)
			break;
		n = n->next;
	}
	if (n->unit == 0)
		snprintf(res, UAUDIO_NAMEMAX, "%s", templ);
	else
		snprintf(res, UAUDIO_NAMEMAX, "%s%s%u", templ, sep, n->unit);
	n->unit++;
}

/*
 * Convert UAC v1.0 feature bitmap to UAC v2.0 feature bitmap.
 */
unsigned int
uaudio_feature_fixup(struct uaudio_softc *sc, unsigned int ctl)
{
	int i;
	unsigned int bits, n;

	switch (sc->version) {
	case UAUDIO_V1:
		n = 0;
		for (i = 0; i < 16; i++) {
			bits = (ctl >> i) & 1;
			if (bits)
				bits |= 2;
			n |= bits << (2 * i);
		}
		return n;
	case UAUDIO_V2:
		break;
	}
	return ctl;
}

/*
 * Initialize a uaudio_ranges to the empty set
 */
void
uaudio_ranges_init(struct uaudio_ranges *r)
{
	r->el = NULL;
	r->nval = 0;
}

/*
 * Add the given range to the uaudio_ranges structures. Ranges are
 * not supposed to overlap (required by USB spec). If they do we just
 * return.
 */
void
uaudio_ranges_add(struct uaudio_ranges *r, int min, int max, int res)
{
	struct uaudio_ranges_el *e, **pe;

	if (min > max) {
		DPRINTF("%s: [%d:%d]/%d: bad range\n", __func__,
		    min, max, res);
		return;
	}

	for (pe = &r->el; (e = *pe) != NULL; pe = &e->next) {
		if (min <= e->max && max >= e->min) {
			DPRINTF("%s: overlapping ranges\n", __func__);
			return;
		}
		if (min < e->max)
			break;
	}

	/* XXX: use 'res' here */
	r->nval += max - min + 1;

	e = malloc(sizeof(struct uaudio_ranges_el), M_USBDEV, M_WAITOK);
	e->min = min;
	e->max = max;
	e->res = res;
	e->next = *pe;
	*pe = e;
}

/*
 * Free all ranges making the uaudio_ranges the empty set
 */
void
uaudio_ranges_clear(struct uaudio_ranges *r)
{
	struct uaudio_ranges_el *e;

	while ((e = r->el) != NULL) {
		r->el = e->next;
		free(e, M_USBDEV, sizeof(struct uaudio_ranges_el));
	}
	r->nval = 0;
}

/*
 * Convert a value in the given uaudio_ranges, into a 0..255 integer
 * suitable for mixer usage
 */
int
uaudio_ranges_decode(struct uaudio_ranges *r, int val)
{
	struct uaudio_ranges_el *e;
	int diff, pos;

	pos = 0;

	for (e = r->el; e != NULL; e = e->next) {
		if (val >= e->min && val <= e->max) {
			pos += val - e->min;
			return (r->nval == 1) ? 0 :
			    (pos * 255 + (r->nval - 1) / 2) / (r->nval - 1);
		}
		diff = e->max - e->min + 1;
		pos += diff;
	}
	return 0;
}

/*
 * Convert a 0..255 to a value in the uaudio_ranges suitable for a USB
 * request.
 */
unsigned int
uaudio_ranges_encode(struct uaudio_ranges *r, int val)
{
	struct uaudio_ranges_el *e;
	int diff, pos;

	pos = (val * (r->nval - 1) + 127) / 255;

	for (e = r->el; e != NULL; e = e->next) {
		diff = e->max - e->min + 1;
		if (pos < diff)
			return e->min + pos;
		pos -= diff;
	}
	return 0;
}

/*
 * Return the bitmap of supported rates included in the given ranges.
 * This is not a mixer thing, UAC v2.0 uses ranges to report sample
 * rates.
 */
int
uaudio_ranges_getrates(struct uaudio_ranges *r,
    unsigned int mult, unsigned int div)
{
	struct uaudio_ranges_el *e;
	int rates, i, v;

	rates = 0;

	for (e = r->el; e != NULL; e = e->next) {
		for (i = 0; i < nitems(uaudio_rates); i++) {
			v = (unsigned long long)uaudio_rates[i] * mult / div;
			if (v < e->min || v > e->max)
				continue;
			if (e->res == 0 || v - e->min % e->res == 0)
				rates |= 1 << i;
		}
	}

	return rates;
}

/*
 * Return the index in the uaudio_rates[] array of rate closest to the
 * given rate in Hz.
 */
int
uaudio_rates_indexof(int mask, int rate)
{
	int i, diff, best_index, best_diff;

	best_index = -1;
	best_diff = INT_MAX;
	for (i = 0; i < nitems(uaudio_rates); i++) {
		if ((mask & (1 << i)) == 0)
			continue;
		diff = uaudio_rates[i] - rate;
		if (diff < 0)
			diff = -diff;
		if (diff < best_diff) {
			best_index = i;
			best_diff = diff;
		}
	}
	return best_index;
}

/*
 * Do a request that results in a uaudio_ranges. On UAC v1.0, this is
 * simply a min/max/res triplet. On UAC v2.0, this is an array of
 * min/max/res triplets.
 */
int
uaudio_req_ranges(struct uaudio_softc *sc,
    unsigned int opsize,
    unsigned int sel,
    unsigned int chan,
    unsigned int ifnum,
    unsigned int id,
    struct uaudio_ranges *r)
{
	unsigned char req_buf[16], *req = NULL;
	size_t req_size;
	struct uaudio_blob p;
	unsigned int count, min, max, res;
	int i;

	switch (sc->version) {
	case UAUDIO_V1:
		count = 1;
		req = req_buf;
		p.rptr = p.wptr = req;
		if (!uaudio_req(sc, UT_READ_CLASS_INTERFACE,
			UAUDIO_V1_REQ_GET_MIN, sel, chan,
			ifnum, id, p.wptr, opsize))
			return 0;
		p.wptr += opsize;
		if (!uaudio_req(sc, UT_READ_CLASS_INTERFACE,
			UAUDIO_V1_REQ_GET_MAX, sel, chan,
			ifnum, id, p.wptr, opsize))
			return 0;
		p.wptr += opsize;
		if (!uaudio_req(sc, UT_READ_CLASS_INTERFACE,
			UAUDIO_V1_REQ_GET_RES, sel, chan,
			ifnum, id, p.wptr, opsize))
			return 0;
		p.wptr += opsize;
		break;
	case UAUDIO_V2:
		/* fetch the ranges count only (first 2 bytes) */
		if (!uaudio_req(sc, UT_READ_CLASS_INTERFACE,
			UAUDIO_V2_REQ_RANGES, sel, chan,
			ifnum, id, req_buf, 2))
			return 0;

		/* count is at most 65535 */
		count = req_buf[0] | req_buf[1] << 8;

		/* restart the request on a large enough buffer */
		req_size = 2 + 3 * opsize * count;
		if (sizeof(req_buf) >= req_size)
			req = req_buf;
		else
			req = malloc(req_size, M_USBDEV, M_WAITOK);

		p.rptr = p.wptr = req;
		if (!uaudio_req(sc, UT_READ_CLASS_INTERFACE,
			UAUDIO_V2_REQ_RANGES, sel, chan,
			ifnum, id, p.wptr, req_size))
			return 0;
		p.wptr += req_size;

		/* skip initial 2 bytes of count */
		p.rptr += 2;
		break;
	}

	for (i = 0; i < count; i++) {
		if (!uaudio_getnum(&p, opsize, &min))
			return 0;
		if (!uaudio_getnum(&p, opsize, &max))
			return 0;
		if (!uaudio_getnum(&p, opsize, &res))
			return 0;
		uaudio_ranges_add(r,
		    uaudio_sign_expand(min, opsize),
		    uaudio_sign_expand(max, opsize),
		    uaudio_sign_expand(res, opsize));
	}

	if (req != req_buf)
		free(req, M_USBDEV, req_size);

	return 1;
}

/*
 * Return the rates bitmap of the given interface alt setting
 */
int
uaudio_alt_getrates(struct uaudio_softc *sc, struct uaudio_alt *p)
{
	struct uaudio_unit *u;
	unsigned int mult = 1, div = 1;

	switch (sc->version) {
	case UAUDIO_V1:
		return p->v1_rates;
	case UAUDIO_V2:
		u = sc->clock;
		while (1) {
			switch (u->type) {
			case UAUDIO_AC_CLKSRC:
				return uaudio_ranges_getrates(&u->rates,
				    mult, div);
			case UAUDIO_AC_CLKSEL:
				u = u->clock;
				break;
			case UAUDIO_AC_CLKMULT:
			case UAUDIO_AC_RATECONV:
				/* XXX: adjust rate with multiplier */
				u = u->src_list;
				break;
			default:
				DPRINTF("%s: no clock\n", __func__);
				return 0;
			}
		}
	}
	return 0;
}

/*
 * return the clock source unit
 */
struct uaudio_unit *
uaudio_clock(struct uaudio_softc *sc)
{
	struct uaudio_unit *u;

	u = sc->clock;
	while (1) {
		if (u == NULL) {
			DPRINTF("%s: NULL clock pointer\n", __func__);
			return NULL;
		}
		switch (u->type) {
		case UAUDIO_AC_CLKSRC:
			return u;
		case UAUDIO_AC_CLKSEL:
			u = u->clock;
			break;
		case UAUDIO_AC_CLKMULT:
		case UAUDIO_AC_RATECONV:
			u = u->src_list;
			break;
		default:
			DPRINTF("%s: no clock\n", __func__);
			return NULL;
		}
	}
}

/*
 * Return the rates bitmap of the given parameters setting
 */
int
uaudio_getrates(struct uaudio_softc *sc, struct uaudio_params *p)
{
	switch (sc->version) {
	case UAUDIO_V1:
		return p->v1_rates;
	case UAUDIO_V2:
		return uaudio_alt_getrates(sc, p->palt ? p->palt : p->ralt);
	}
	return 0;
}

/*
 * Add the given feature (aka mixer control) to the given unit.
 */
void
uaudio_feature_addent(struct uaudio_softc *sc,
    struct uaudio_unit *u, int uac_type, int chan)
{
	static struct {
		char *name;
		int mix_type;
		int req_sel;
	} features[] = {
		{"mute", UAUDIO_MIX_SW, UAUDIO_REQSEL_MUTE},
		{"level", UAUDIO_MIX_NUM, UAUDIO_REQSEL_VOLUME},
		{"bass", UAUDIO_MIX_NUM, UAUDIO_REQSEL_BASS},
		{"mid", UAUDIO_MIX_NUM, UAUDIO_REQSEL_MID},
		{"treble", UAUDIO_MIX_NUM, UAUDIO_REQSEL_TREBLE},
		{"eq", UAUDIO_MIX_NUM, UAUDIO_REQSEL_EQ},
		{"agc", UAUDIO_MIX_SW, UAUDIO_REQSEL_AGC},
		{NULL, -1, -1},			/* delay */
		{"bassboost", UAUDIO_MIX_SW, UAUDIO_REQSEL_BASSBOOST},
		{"loud", UAUDIO_MIX_SW, UAUDIO_REQSEL_LOUDNESS},
		{"gain", UAUDIO_MIX_NUM, UAUDIO_REQSEL_GAIN},
		{"gainpad", UAUDIO_MIX_SW, UAUDIO_REQSEL_GAINPAD},
		{"phase", UAUDIO_MIX_SW, UAUDIO_REQSEL_PHASEINV},
		{NULL, -1, -1},			/* underflow */
		{NULL, -1, -1}			/* overflow */
	};
	struct uaudio_mixent *m, *i, **pi;
	int cmp;

	if (uac_type >= sizeof(features) / sizeof(features[0])) {
		printf("%s: skipped unknown feature\n", DEVNAME(sc));
		return;
	}

	m = malloc(sizeof(struct uaudio_mixent), M_USBDEV, M_WAITOK);
	m->chan = chan;
	m->fname = features[uac_type].name;
	m->type = features[uac_type].mix_type;
	m->req_sel = features[uac_type].req_sel;
	uaudio_ranges_init(&m->ranges);

	if (m->type == UAUDIO_MIX_NUM) {
		if (!uaudio_req_ranges(sc, 2,
			m->req_sel, chan < 0 ? 0 : chan + 1,
			sc->ctl_ifnum, u->id,
			&m->ranges)) {
			printf("%s: failed to get ranges for %s control\n",
			    DEVNAME(sc), m->fname);
			free(m, M_USBDEV, sizeof(struct uaudio_mixent));
			return;
		}
		if (m->ranges.el == NULL) {
			printf("%s: skipped %s control with empty range\n",
			    DEVNAME(sc), m->fname);
			free(m, M_USBDEV, sizeof(struct uaudio_mixent));
			return;
		}
#ifdef UAUDIO_DEBUG
		if (uaudio_debug)
			uaudio_ranges_print(&m->ranges);
#endif
	}

	/*
	 * Add to unit's mixer controls list, sorting entries by name
	 * and increasing channel number.
	 */
	for (pi = &u->mixent_list; (i = *pi) != NULL; pi = &i->next) {
		cmp = strcmp(i->fname, m->fname);
		if (cmp == 0)
			cmp = i->chan - m->chan;
		if (cmp == 0) {
			DPRINTF("%02u: %s.%s: duplicate feature for chan %d\n",
			    u->id, u->name, m->fname, m->chan);
			free(m, M_USBDEV, sizeof(struct uaudio_mixent));
			return;
		}
		if (cmp > 0)
			break;
	}
	m->next = *pi;
	*pi = m;

	DPRINTF("\t%s[%d]\n", m->fname, m->chan);
}

/*
 * For the given unit, parse the list of its sources and recursively
 * call uaudio_process_unit() for each.
 */
int
uaudio_process_srcs(struct uaudio_softc *sc,
	struct uaudio_unit *u, struct uaudio_blob units,
	struct uaudio_blob *p)
{
	struct uaudio_unit *s, **ps;
	unsigned int i, npin, sid;

	if (!uaudio_getnum(p, 1, &npin))
		return 0;
	ps = &u->src_list;
	for (i = 0; i < npin; i++) {
		if (!uaudio_getnum(p, 1, &sid))
			return 0;
		if (!uaudio_process_unit(sc, u, sid, units, &s))
			return 0;
		s->src_next = NULL;
		*ps = s;
		ps = &s->src_next;
	}
	return 1;
}

/*
 * Parse the number of channels.
 */
int
uaudio_process_nch(struct uaudio_softc *sc,
	struct uaudio_unit *u, struct uaudio_blob *p)
{
	if (!uaudio_getnum(p, 1, &u->nch))
		return 0;
	/* skip junk */
	switch (sc->version) {
	case UAUDIO_V1:
		if (!uaudio_getnum(p, 2, NULL)) /* bmChannelConfig */
			return 0;
		break;
	case UAUDIO_V2:
		if (!uaudio_getnum(p, 4, NULL)) /* wChannelConfig */
			return 0;
		break;
	}
	if (!uaudio_getnum(p, 1, NULL)) /* iChannelNames */
		return 0;
	return 1;
}

/*
 * Find the AC class-specific descriptor for this unit id.
 */
int
uaudio_unit_getdesc(struct uaudio_softc *sc, int id,
	struct uaudio_blob units,
	struct uaudio_blob *p,
	unsigned int *rtype)
{
	unsigned int i, type, subtype;

	/*
	 * Find the usb descriptor for this id.
	 */
	while (1) {
		if (units.rptr == units.wptr) {
			DPRINTF("%s: %02u: not found\n", __func__, id);
			return 0;
		}
		if (!uaudio_getdesc(&units, p))
			return 0;
		if (!uaudio_getnum(p, 1, &type))
			return 0;
		if (!uaudio_getnum(p, 1, &subtype))
			return 0;
		if (!uaudio_getnum(p, 1, &i))
			return 0;
		if (i == id)
			break;
	}
	*rtype = subtype;
	return 1;
}

/*
 * Parse a unit, possibly calling uaudio_process_unit() for each of
 * its sources.
 */
int
uaudio_process_unit(struct uaudio_softc *sc,
	struct uaudio_unit *dest, int id,
	struct uaudio_blob units,
	struct uaudio_unit **rchild)
{
	struct uaudio_blob p;
	struct uaudio_unit *u, *s;
	unsigned int i, j, size, ctl, type, subtype, assoc, clk;
#ifdef UAUDIO_DEBUG
	unsigned int bit;
#endif

	if (!uaudio_unit_getdesc(sc, id, units, &p, &subtype))
		return 0;

	/*
	 * find this unit on the list as it may be already processed as
	 * the source of another destination
	 */
	u = uaudio_unit_byid(sc, id);
	if (u == NULL) {
		u = malloc(sizeof(struct uaudio_unit), M_USBDEV, M_WAITOK);
		u->id = id;
		u->type = subtype;
		u->term = 0;
		u->src_list = NULL;
		u->dst_list = NULL;
		u->clock = NULL;
		u->mixent_list = NULL;
		u->nch = 0;
		u->name[0] = 0;
		u->cap_freqctl = 0;
		uaudio_ranges_init(&u->rates);
		u->unit_next = sc->unit_list;
		sc->unit_list = u;
	} else {
		switch (u->type) {
		case UAUDIO_AC_CLKSRC:
		case UAUDIO_AC_CLKSEL:
		case UAUDIO_AC_CLKMULT:
		case UAUDIO_AC_RATECONV:
			/* not using 'dest' list */
			*rchild = u;
			return 1;
		}
	}

	if (dest) {
		dest->dst_next = u->dst_list;
		u->dst_list = dest;
		if (dest->dst_next != NULL) {
			/* already seen */
			*rchild = u;
			return 1;
		}
	}

	switch (u->type) {
	case UAUDIO_AC_INPUT:
		if (!uaudio_getnum(&p, 2, &u->term))
			return 0;
		if (!uaudio_getnum(&p, 1, &assoc))
			return 0;
		if (u->term >> 8 != 1)
			sc->nin++;
		switch (sc->version) {
		case UAUDIO_V1:
			break;
		case UAUDIO_V2:
			if (!uaudio_getnum(&p, 1, &clk))
				return 0;
			if (!uaudio_process_unit(sc, NULL,
				clk, units, &u->clock))
				return 0;
			break;
		}
		if (!uaudio_getnum(&p, 1, &u->nch))
			return 0;
		DPRINTF("%02u: "
		    "in, nch = %d, term = 0x%x, assoc = %d\n",
		    u->id, u->nch, u->term, assoc);
		break;
	case UAUDIO_AC_OUTPUT:
		if (!uaudio_getnum(&p, 2, &u->term))
			return 0;
		if (!uaudio_getnum(&p, 1, &assoc))
			return 0;
		if (!uaudio_getnum(&p, 1, &id))
			return 0;
		if (!uaudio_process_unit(sc, u, id, units, &s))
			return 0;
		if (u->term >> 8 != 1)
			sc->nout++;
		switch (sc->version) {
		case UAUDIO_V1:
			break;
		case UAUDIO_V2:
			if (!uaudio_getnum(&p, 1, &clk))
				return 0;
			if (!uaudio_process_unit(sc, NULL,
				clk, units, &u->clock))
				return 0;
			break;
		}
		u->src_list = s;
		s->src_next = NULL;
		u->nch = s->nch;
		DPRINTF("%02u: "
		    "out, id = %d, nch = %d, term = 0x%x, assoc = %d\n",
		    u->id, id, u->nch, u->term, assoc);
		break;
	case UAUDIO_AC_MIXER:
		if (!uaudio_process_srcs(sc, u, units, &p))
			return 0;
		if (!uaudio_process_nch(sc, u, &p))
			return 0;
		DPRINTF("%02u: mixer, nch = %u:\n", u->id, u->nch);

#ifdef UAUDIO_DEBUG
		/*
		 * Print the list of available mixer's unit knobs (a bit
		 * matrix). Matrix mixers are rare because levels are
		 * already controlled by feature units, making the mixer
		 * knobs redundant with the feature's knobs. So, for
		 * now, we don't add clutter to the mixer(4) interface
		 * and ignore all knobs. Other popular OSes doesn't
		 * seem to expose them either.
		 */
		bit = 0;
		for (s = u->src_list; s != NULL; s = s->src_next) {
			for (i = 0; i < s->nch; i++) {
				for (j = 0; j < u->nch; j++) {
					if ((bit++ & 7) == 0) {
						if (!uaudio_getnum(&p, 1, &ctl))
							return 0;
					}
					if (ctl & 0x80)
						DPRINTF("\t%02u[%d] -> [%d]\n",
						    s->id, i, j);
					ctl <<= 1;
				}
			}
		}
#endif
		break;
	case UAUDIO_AC_SELECTOR:
		/*
		 * Selectors are extremely rare, so not supported yet.
		 */
		if (!uaudio_process_srcs(sc, u, units, &p))
			return 0;
		if (u->src_list == NULL) {
			printf("%s: selector %02u has no sources\n",
			    DEVNAME(sc), u->id);
			return 0;
		}
		u->nch = u->src_list->nch;
		DPRINTF("%02u: selector, nch = %u\n", u->id, u->nch);
		break;
	case UAUDIO_AC_FEATURE:
		if (!uaudio_getnum(&p, 1, &id))
			return 0;
		if (!uaudio_process_unit(sc, u, id, units, &s))
			return 0;
		s->src_next = u->src_list;
		u->src_list = s;
		u->nch = s->nch;
		switch (sc->version) {
		case UAUDIO_V1:
			if (!uaudio_getnum(&p, 1, &size))
				return 0;
			break;
		case UAUDIO_V2:
			size = 4;
			break;
		}
		DPRINTF("%02d: feature id = %d, nch = %d, size = %d\n",
		    u->id, id, u->nch, size);

		if (!uaudio_getnum(&p, size, &ctl))
			return 0;
		ctl = uaudio_feature_fixup(sc, ctl);
		for (i = 0; i < 16; i++) {
			if ((ctl & 3) == 3)
				uaudio_feature_addent(sc, u, i, -1);
			ctl >>= 2;
		}

		/*
		 * certain devices provide no per-channel control descriptors
		 */
		if (p.wptr - p.rptr == 1)
			break;

		for (j = 0; j < u->nch; j++) {
			if (!uaudio_getnum(&p, size, &ctl))
				return 0;
			ctl = uaudio_feature_fixup(sc, ctl);
			for (i = 0; i < 16; i++) {
				if ((ctl & 3) == 3)
					uaudio_feature_addent(sc, u, i, j);
				ctl >>= 2;
			}
		}
		break;
	case UAUDIO_AC_EFFECT:
		if (!uaudio_getnum(&p, 2, &type))
			return 0;
		if (!uaudio_getnum(&p, 1, &id))
			return 0;
		if (!uaudio_process_unit(sc, u, id, units, &s))
			return 0;
		s->src_next = u->src_list;
		u->src_list = s;
		u->nch = s->nch;
		DPRINTF("%02d: effect, type = %u, id = %d, nch = %d\n",
		    u->id, type, id, u->nch);
		break;
	case UAUDIO_AC_PROCESSING:
	case UAUDIO_AC_EXTENSION:
		if (!uaudio_getnum(&p, 2, &type))
			return 0;
		if (!uaudio_process_srcs(sc, u, units, &p))
			return 0;
		if (!uaudio_process_nch(sc, u, &p))
			return 0;
		DPRINTF("%02u: proc/ext, type = 0x%x, nch = %u\n",
		    u->id, type, u->nch);
		for (s = u->src_list; s != NULL; s = s->src_next) {
			DPRINTF("%u:\tpin %u:\n", u->id, s->id);
		}
		break;
	case UAUDIO_AC_CLKSRC:
		if (!uaudio_getnum(&p, 1, &u->term))
			return 0;
		if (!uaudio_getnum(&p, 1, &ctl))
			return 0;
		DPRINTF("%02u: clock source, attr = 0x%x, ctl = 0x%x\n",
		    u->id, u->term, ctl);
		u->cap_freqctl = !!(ctl & UAUDIO_CLKSRC_FREQCTL);
		break;
	case UAUDIO_AC_CLKSEL:
		DPRINTF("%02u: clock sel\n", u->id);
		if (!uaudio_process_srcs(sc, u, units, &p))
			return 0;
		if (u->src_list == NULL) {
			printf("%s: clock selector %02u with no srcs\n",
			    DEVNAME(sc), u->id);
			return 0;
		}
		break;
	case UAUDIO_AC_CLKMULT:
		DPRINTF("%02u: clock mult\n", u->id);

		/* XXX: fetch multiplier */
		printf("%s: clock multiplier not supported\n", DEVNAME(sc));
		break;
	case UAUDIO_AC_RATECONV:
		DPRINTF("%02u: rate conv\n", u->id);

		/* XXX: fetch multiplier */
		printf("%s: rate converter not supported\n", DEVNAME(sc));
		break;
	}
	if (rchild)
		*rchild = u;
	return 1;
}

/*
 * Try to set the unit name to the name of its destination terminal. If
 * the name is ambiguous (already given to another source unit or having
 * multiple destinations) then return 0.
 */
int
uaudio_setname_dsts(struct uaudio_softc *sc, struct uaudio_unit *u, char *name)
{
	struct uaudio_unit *d = u;

	while (d != NULL) {
		if (d->dst_list == NULL || d->dst_list->dst_next != NULL)
			break;
		d = d->dst_list;
		if (d->src_list == NULL || d->src_list->src_next != NULL)
			break;
		if (d->name[0] != '\0') {
			if (name != NULL && strcmp(name, d->name) != 0)
				break;
			strlcpy(u->name, d->name, UAUDIO_NAMEMAX);
			return 1;
		}
	}
	return 0;
}

/*
 * Try to set the unit name to the name of its source terminal. If the
 * name is ambiguous (already given to another destination unit or
 * having multiple sources) then return 0.
 */
int
uaudio_setname_srcs(struct uaudio_softc *sc, struct uaudio_unit *u, char *name)
{
	struct uaudio_unit *s = u;

	while (s != NULL) {
		if (s->src_list == NULL || s->src_list->src_next != NULL)
			break;
		s = s->src_list;
		if (s->dst_list == NULL || s->dst_list->dst_next != NULL)
			break;
		if (s->name[0] != '\0') {
			if (name != NULL && strcmp(name, s->name) != 0)
				break;
			strlcpy(u->name, s->name, UAUDIO_NAMEMAX);
			return 1;
		}
	}
	return 0;
}

/*
 * Set the name of the given unit by using both its source and
 * destination units. This is naming scheme is only useful to units
 * that would have ambiguous names if only sources or only destination
 * were used.
 */
void
uaudio_setname_middle(struct uaudio_softc *sc, struct uaudio_unit *u)
{
	struct uaudio_unit *s, *d;
	char name[UAUDIO_NAMEMAX];

	s = u->src_list;
	while (1) {
		if (s == NULL) {
			DPRINTF("%s: %02u: has no srcs\n",
			    __func__, u->id);
			return;
		}
		if (s->name[0] != '\0')
			break;
		s = s->src_list;
	}

	d = u->dst_list;
	while (1) {
		if (d == NULL) {
			DPRINTF("%s: %02u: has no dests\n",
			    __func__, u->id);
			return;
		}
		if (d->name[0] != '\0')
			break;
		d = d->dst_list;
	}

	snprintf(name, UAUDIO_NAMEMAX, "%s_%s", d->name, s->name);
	uaudio_mkname(sc, name, u->name);
}

#ifdef UAUDIO_DEBUG
/*
 * Return the synchronization type name, for debug purposes only.
 */
char *
uaudio_isoname(int isotype)
{
	switch (isotype) {
	case UE_ISO_ASYNC:
		return "async";
	case UE_ISO_ADAPT:
		return "adapt";
	case UE_ISO_SYNC:
		return "sync";
	default:
		return "unk";
	}
}

/*
 * Return the name of the given mode, debug only
 */
char *
uaudio_modename(int mode)
{
	switch (mode) {
	case 0:
		return "none";
	case AUMODE_PLAY:
		return "play";
	case AUMODE_RECORD:
		return "rec";
	case AUMODE_PLAY | AUMODE_RECORD:
		return "duplex";
	default:
		return "unk";
	}
}

/*
 * Return UAC v2.0 endpoint usage, debug only
 */
char *
uaudio_usagename(int usage)
{
	switch (usage) {
	case UE_ISO_USAGE_DATA:
		return "data";
	case UE_ISO_USAGE_FEEDBACK:
		return "feed";
	case UE_ISO_USAGE_IMPL:
		return "impl";
	default:
		return "unk";
	}
}

/*
 * Print a bitmap of rates on the console.
 */
void
uaudio_rates_print(int rates)
{
	unsigned int i;

	for (i = 0; i < nitems(uaudio_rates); i++) {
		if (rates & (1 << i))
			printf(" %d", uaudio_rates[i]);
	}
	printf("\n");
}


/*
 * Print uaudio_ranges to console.
 */
void
uaudio_ranges_print(struct uaudio_ranges *r)
{
	struct uaudio_ranges_el *e;
	int more = 0;

	for (e = r->el; e != NULL; e = e->next) {
		if (more)
			printf(", ");
		if (e->min == e->max)
			printf("%d", e->min);
		else
			printf("[%d:%d]/%d", e->min, e->max, e->res);
		more = 1;
	}
	printf(" (%d vals)\n", r->nval);
}

/*
 * Print unit to the console.
 */
void
uaudio_print_unit(struct uaudio_softc *sc, struct uaudio_unit *u)
{
	struct uaudio_unit *s;

	switch (u->type) {
	case UAUDIO_AC_INPUT:
		printf("%02u: input <%s>, dest = %02u <%s>\n",
		    u->id, u->name, u->dst_list->id, u->dst_list->name);
		break;
	case UAUDIO_AC_OUTPUT:
		printf("%02u: output <%s>, source = %02u <%s>\n",
		    u->id, u->name, u->src_list->id, u->src_list->name);
		break;
	case UAUDIO_AC_MIXER:
		printf("%02u: mixer <%s>:\n", u->id, u->name);
		for (s = u->src_list; s != NULL; s = s->src_next)
			printf("%02u:\tsource %u <%s>:\n",
			    u->id, s->id, s->name);
		break;
	case UAUDIO_AC_SELECTOR:
		printf("%02u: selector <%s>:\n", u->id, u->name);
		for (s = u->src_list; s != NULL; s = s->src_next)
			printf("%02u:\tsource %u <%s>:\n",
			    u->id, s->id, s->name);
		break;
	case UAUDIO_AC_FEATURE:
		printf("%02u: feature <%s>, "
		    "src = %02u <%s>, dst = %02u <%s>, cls = %d\n",
		    u->id, u->name,
		    u->src_list->id, u->src_list->name,
		    u->dst_list->id, u->dst_list->name, u->mixer_class);
		break;
	case UAUDIO_AC_EFFECT:
		printf("%02u: effect <%s>, "
		    "src = %02u <%s>, dst = %02u <%s>\n",
		    u->id, u->name,
		    u->src_list->id, u->src_list->name,
		    u->dst_list->id, u->dst_list->name);
		break;
	case UAUDIO_AC_PROCESSING:
	case UAUDIO_AC_EXTENSION:
		printf("%02u: proc/ext <%s>:\n", u->id, u->name);
		for (s = u->src_list; s != NULL; s = s->src_next)
			printf("%02u:\tsource %u <%s>:\n",
			    u->id, s->id, s->name);
		break;
	case UAUDIO_AC_CLKSRC:
		printf("%02u: clock source <%s>\n", u->id, u->name);
		break;
	case UAUDIO_AC_CLKSEL:
		printf("%02u: clock sel <%s>\n", u->id, u->name);
		break;
	case UAUDIO_AC_CLKMULT:
		printf("%02u: clock mult\n", u->id);
		break;
	case UAUDIO_AC_RATECONV:
		printf("%02u: rate conv\n", u->id);
		break;
	}
}

/*
 * Print the full mixer on the console.
 */
void
uaudio_mixer_print(struct uaudio_softc *sc)
{
	struct uaudio_mixent *m;
	struct uaudio_unit *u;

	for (u = sc->unit_list; u != NULL; u = u->unit_next) {
		for (m = u->mixent_list; m != NULL; m = m->next) {
			printf("%02u:\t%s.%s",
			    u->id, u->name, m->fname);
			if (m->chan >= 0)
				printf("[%u]", m->chan);
			printf("\n");
		}
	}
}

/*
 * Print the full device configuration on the console.
 */
void
uaudio_conf_print(struct uaudio_softc *sc)
{
	struct uaudio_alt *a;
	struct uaudio_params *p;
	struct mixer_devinfo mi;
	struct mixer_ctrl ctl;
	int i, rates;

	mi.index = 0;
	while (1) {
		if (uaudio_query_devinfo(sc, &mi) != 0)
			break;

		if (mi.type != AUDIO_MIXER_CLASS) {
			ctl.dev = mi.index;
			if (uaudio_get_port(sc, &ctl) != 0) {
				printf("%02u: failed to get port\n", mi.index);
				memset(&ctl.un, 0, sizeof(ctl.un));
			}
		}

		printf("%02u: <%s>, next = %d, prev = %d, class = %d",
		    mi.index, mi.label.name, mi.next, mi.prev, mi.mixer_class);

		switch (mi.type) {
		case AUDIO_MIXER_CLASS:
			break;
		case AUDIO_MIXER_VALUE:
			printf(", nch = %d, delta = %d",
			    mi.un.v.num_channels, mi.un.v.delta);
			printf(", val =");
			for (i = 0; i < mi.un.v.num_channels; i++)
				printf(" %d", ctl.un.value.level[i]);
			break;
		case AUDIO_MIXER_ENUM:
			printf(", members:");
			for (i = 0; i != mi.un.e.num_mem; i++) {
				printf(" %s(=%d)",
				    mi.un.e.member[i].label.name,
				    mi.un.e.member[i].ord);
			}
			printf(", val = %d", ctl.un.ord);
			break;
		}

		printf("\n");
		mi.index++;
	}

	printf("%d controls\n", mi.index);

	printf("alts:\n");
	for (a = sc->alts; a != NULL; a = a->next) {
		rates = uaudio_alt_getrates(sc, a);
		printf("mode = %s, ifnum = %d, altnum = %d, "
		    "addr = 0x%x, maxpkt = %d, sync = 0x%x, "
		    "nch = %d, fmt = s%dle%d, rates:",
		    uaudio_modename(a->mode),
		    a->ifnum, a->altnum,
		    a->data_addr, a->maxpkt,
		    a->sync_addr,
		    a->nch, a->bits, a->bps);
		uaudio_rates_print(rates);
	}

	printf("parameters:\n");
	for (p = sc->params_list; p != NULL; p = p->next) {
		switch (sc->version) {
		case UAUDIO_V1:
			rates = p->v1_rates;
			break;
		case UAUDIO_V2:
			rates = uaudio_getrates(sc, p);
			break;
		}
		printf("pchan = %d, s%dle%d, rchan = %d, s%dle%d, rates:",
		    p->palt ? p->palt->nch : 0,
		    p->palt ? p->palt->bits : 0,
		    p->palt ? p->palt->bps : 0,
		    p->ralt ? p->ralt->nch : 0,
		    p->ralt ? p->ralt->bits : 0,
		    p->ralt ? p->ralt->bps : 0);
		uaudio_rates_print(rates);
	}
}
#endif

/*
 * Return the number of mixer controls that have the same name but
 * control different channels of the same stream.
 */
int
uaudio_mixer_nchan(struct uaudio_mixent *m, struct uaudio_mixent **rnext)
{
	char *name;
	int i;

	i = 0;
	name = m->fname;
	while (m != NULL && strcmp(name, m->fname) == 0) {
		m = m->next;
		i++;
	}
	if (rnext)
		*rnext = m;
	return i;
}

/*
 * Skip redundant mixer entries that we don't want to expose to userland.
 * For instance if there is a mute-all-channels control and per-channel
 * mute controls, we don't want both (we expose the per-channel mute)
 */
void
uaudio_mixer_skip(struct uaudio_mixent **pm)
{
	struct uaudio_mixent *m = *pm;

	if (m != NULL &&
	    m->chan == -1 &&
	    m->next != NULL &&
	    strcmp(m->fname, m->next->fname) == 0)
		m = m->next;

	*pm = m;
}

/*
 * Return pointer to the unit and mixer entry which have the given
 * index exposed by the mixer(4) API.
 */
int
uaudio_mixer_byindex(struct uaudio_softc *sc, int index,
    struct uaudio_unit **ru, struct uaudio_mixent **rm)
{
	struct uaudio_unit *u;
	struct uaudio_mixent *m;
	char *name;
	int i;

	i = UAUDIO_CLASS_COUNT;
	for (u = sc->unit_list; u != NULL; u = u->unit_next) {
		m = u->mixent_list;
		while (1) {
			uaudio_mixer_skip(&m);
			if (m == NULL)
				break;
			if (index == i) {
				*ru = u;
				*rm = m;
				return 1;
			}
			if (m->type == UAUDIO_MIX_NUM) {
				name = m->fname;
				while (m != NULL &&
				    strcmp(name, m->fname) == 0)
					m = m->next;
			} else
				m = m->next;
			i++;
		}
	}
	return 0;
}

/*
 * Parse AC header descriptor, we use it only to determine UAC
 * version. Other properties (like wTotalLength) can be determined
 * using other descriptors, so we try to no rely on them to avoid
 * inconsistencies and the need for certain quirks.
 */
int
uaudio_process_header(struct uaudio_softc *sc, struct uaudio_blob *p)
{
	struct uaudio_blob ph;
	unsigned int type, subtype;

	if (!uaudio_getdesc(p, &ph))
		return 0;
	if (!uaudio_getnum(&ph, 1, &type))
		return 0;
	if (type != UDESC_CS_INTERFACE) {
		DPRINTF("%s: expected cs iface desc\n", __func__);
		return 0;
	}
	if (!uaudio_getnum(&ph, 1, &subtype))
		return 0;
	if (subtype != UAUDIO_AC_HEADER) {
		DPRINTF("%s: expected header desc\n", __func__);
		return 0;
	}
	if (!uaudio_getnum(&ph, 2, &sc->version))
		return 0;

	DPRINTF("%s: version 0x%x\n", __func__, sc->version);
	return 1;
}

/*
 * Process AC interrupt endpoint descriptor, this is mainly to skip
 * the descriptor as we use neither of its properties. Our mixer
 * interface doesn't support unsolicited state changes, so we've no
 * use of it yet.
 */
int
uaudio_process_ac_ep(struct uaudio_softc *sc, struct uaudio_blob *p)
{
#ifdef UAUDIO_DEBUG
	static const char *xfer[] = {
		"ctl", "iso", "bulk", "intr"
	};
#endif
	struct uaudio_blob dp;
	unsigned int type, addr, attr, maxpkt, ival;
	unsigned char *savepos;

	/*
	 * parse optional interrupt endpoint descriptor
	 */
	if (p->rptr == p->wptr)
		return 1;
	savepos = p->rptr;
	if (!uaudio_getdesc(p, &dp))
		return 0;
	if (!uaudio_getnum(&dp, 1, &type))
		return 0;
	if (type != UDESC_ENDPOINT) {
		p->rptr = savepos;
		return 1;
	}

	if (!uaudio_getnum(&dp, 1, &addr))
		return 0;
	if (!uaudio_getnum(&dp, 1, &attr))
		return 0;
	if (!uaudio_getnum(&dp, 2, &maxpkt))
		return 0;
	if (!uaudio_getnum(&dp, 1, &ival))
		return 0;

	DPRINTF("%s: addr = 0x%x, type = %s, maxpkt = %d, ival = %d\n",
	    __func__, addr, xfer[UE_GET_XFERTYPE(attr)],
	    UE_GET_SIZE(maxpkt), ival);

	return 1;
}

/*
 * Process the AC interface descriptors: mainly build the mixer and,
 * for UAC v2.0, find the clock source.
 *
 * The audio device exposes an audio control (AC) interface with a big
 * set of USB descriptors which expose the complete circuit the
 * device. The circuit describes how the signal flows between the USB
 * streaming interfaces to the terminal connectors (jacks, speakers,
 * mics, ...). The circuit is build of mixers, source selectors, gain
 * controls, mutters, processors, and alike; each comes with its own
 * set of controls. Most of the boring driver work is to parse the
 * circuit and build a human-usable set of controls that could be
 * exposed through the mixer(4) interface.
 */
int
uaudio_process_ac(struct uaudio_softc *sc, struct uaudio_blob *p, int ifnum)
{
	struct uaudio_blob units, pu;
	struct uaudio_unit *u, *v;
	unsigned char *savepos;
	unsigned int type, subtype, id;
	char *name, val;

	DPRINTF("%s: ifnum = %d, %zd bytes to process\n", __func__,
	    ifnum, p->wptr - p->rptr);

	sc->ctl_ifnum = ifnum;

	/* The first AC class-specific descriptor is the AC header */
	if (!uaudio_process_header(sc, p))
		return 0;

	/*
	 * Determine the size of the AC descriptors array: scan
	 * descriptors until we get the first non-class-specific
	 * descriptor. This avoids relying on the wTotalLength field.
	 */
	savepos = p->rptr;
	units.rptr = units.wptr = p->rptr;
	while (p->rptr != p->wptr) {
		if (!uaudio_getdesc(p, &pu))
			return 0;
		if (!uaudio_getnum(&pu, 1, &type))
			return 0;
		if (type != UDESC_CS_INTERFACE)
			break;
		units.wptr = p->rptr;
	}
	p->rptr = savepos;

	/*
	 * Load units, walking from outputs to inputs, as
	 * the usb audio class spec requires.
	 */
	while (p->rptr != units.wptr) {
		if (!uaudio_getdesc(p, &pu))
			return 0;
		if (!uaudio_getnum(&pu, 1, &type))
			return 0;
		if (!uaudio_getnum(&pu, 1, &subtype))
			return 0;
		if (subtype == UAUDIO_AC_OUTPUT) {
			if (!uaudio_getnum(&pu, 1, &id))
				return 0;
			if (!uaudio_process_unit(sc, NULL, id, units, NULL))
				return 0;
		}
	}

	/*
	 * set terminal, effect, and processor unit names
	 */
	for (u = sc->unit_list; u != NULL; u = u->unit_next) {
		switch (u->type) {
		case UAUDIO_AC_INPUT:
			uaudio_mkname(sc, uaudio_tname(sc, u->term, 0), u->name);
			break;
		case UAUDIO_AC_OUTPUT:
			uaudio_mkname(sc, uaudio_tname(sc, u->term, 1), u->name);
			break;
		case UAUDIO_AC_CLKSRC:
			uaudio_mkname(sc, uaudio_clkname(u->term), u->name);
			break;
		case UAUDIO_AC_CLKSEL:
			uaudio_mkname(sc, "clksel", u->name);
			break;
		case UAUDIO_AC_EFFECT:
			uaudio_mkname(sc, "fx", u->name);
			break;
		case UAUDIO_AC_PROCESSING:
			uaudio_mkname(sc, "proc", u->name);
			break;
		case UAUDIO_AC_EXTENSION:
			uaudio_mkname(sc, "ext", u->name);
			break;
		}
	}

	/*
	 * set mixer/selector unit names
	 */
	for (u = sc->unit_list; u != NULL; u = u->unit_next) {
		if (u->type != UAUDIO_AC_MIXER &&
		    u->type != UAUDIO_AC_SELECTOR)
			continue;
		if (!uaudio_setname_dsts(sc, u, NULL)) {
			switch (u->type) {
			case UAUDIO_AC_MIXER:
				name = "mix";
				break;
			case UAUDIO_AC_SELECTOR:
				name = "sel";
				break;
			}
			uaudio_mkname(sc, name, u->name);
		}
	}

	/*
	 * set feature unit names and classes
	 */
	for (u = sc->unit_list; u != NULL; u = u->unit_next) {
		if (u->type != UAUDIO_AC_FEATURE)
			continue;
		if (uaudio_setname_dsts(sc, u, UAUDIO_NAME_REC)) {
			u->mixer_class = UAUDIO_CLASS_IN;
			continue;
		}
		if (uaudio_setname_srcs(sc, u, UAUDIO_NAME_PLAY)) {
			u->mixer_class = UAUDIO_CLASS_OUT;
			continue;
		}
		if (uaudio_setname_dsts(sc, u, NULL)) {
			u->mixer_class = UAUDIO_CLASS_OUT;
			continue;
		}
		if (uaudio_setname_srcs(sc, u, NULL)) {
			u->mixer_class = UAUDIO_CLASS_IN;
			continue;
		}
		uaudio_setname_middle(sc, u);
		u->mixer_class = UAUDIO_CLASS_IN;
	}

#ifdef UAUDIO_DEBUG
	if (uaudio_debug) {
		printf("%s: units list:\n", DEVNAME(sc));
		for (u = sc->unit_list; u != NULL; u = u->unit_next)
			uaudio_print_unit(sc, u);

		printf("%s: mixer controls:\n", DEVNAME(sc));
		uaudio_mixer_print(sc);
	}
#endif

	/* follows optional interrupt endpoint descriptor */
	if (!uaudio_process_ac_ep(sc, p))
		return 0;

	/* fetch clock source rates */
	for (u = sc->unit_list; u != NULL; u = u->unit_next) {
		switch (u->type) {
		case UAUDIO_AC_CLKSRC:
			if (!uaudio_req_ranges(sc, 4,
				UAUDIO_V2_REQSEL_CLKFREQ,
				0, /* channel (not used) */
				sc->ctl_ifnum,
				u->id,
				&u->rates)) {
				printf("%s: failed to read clock rates\n",
				    DEVNAME(sc));
				return 0;
			}
#ifdef UAUDIO_DEBUG
			if (uaudio_debug) {
				printf("%02u: clock rates: ", u->id);
				uaudio_ranges_print(&u->rates);
				if (!u->cap_freqctl)
					printf("%02u: no rate control\n", u->id);
			}
#endif
			break;
		case UAUDIO_AC_CLKSEL:
			if (!uaudio_req(sc, UT_READ_CLASS_INTERFACE,
				UAUDIO_V2_REQ_CUR,
				UAUDIO_V2_REQSEL_CLKSEL, 0,
				sc->ctl_ifnum, u->id,
				&val, 1)) {
				printf("%s: failed to read clock selector\n",
				    DEVNAME(sc));
				return 0;
			}
			for (v = u->src_list; v != NULL; v = v->src_next) {
				if (--val == 0)
					break;
			}
			u->clock = v;
			break;
		}
	}

	if (sc->version == UAUDIO_V2) {
		/*
		 * Find common clock unit. We assume all terminals
		 * belong to the same clock domain (ie are connected
		 * to the same source)
		 */
		sc->clock = NULL;
		for (u = sc->unit_list; u != NULL; u = u->unit_next) {
			if (u->type != UAUDIO_AC_INPUT &&
			    u->type != UAUDIO_AC_OUTPUT)
				continue;
			if (sc->clock == NULL) {
				if (u->clock == NULL) {
					printf("%s: terminal with no clock\n",
					    DEVNAME(sc));
					return 0;
				}
				sc->clock = u->clock;
			} else if (u->clock != sc->clock) {
				printf("%s: only one clock domain supported\n",
				    DEVNAME(sc));
				return 0;
			}
		}

		if (sc->clock == NULL) {
			printf("%s: no clock found\n", DEVNAME(sc));
			return 0;
		}
	}
	return 1;
}

/*
 * Parse endpoint descriptor with the following format:
 *
 * For playback there's a output data endpoint, of the
 * following types:
 *
 *  type	sync	descr
 *  -------------------------------------------------------
 *  async:	Yes	the device uses its own clock but
 *			sends feedback on a (input) sync endpoint
 *			for the host to adjust next packet size
 *
 *  sync:	-	data rate is constant, and device
 *			is clocked to the usb bus.
 *
 *  adapt:	-	the device adapts to data rate of the
 *			host. If fixed packet size is used,
 *			data rate is equivalent to the usb clock
 *			so this mode is the same as the
 *			sync mode.
 *
 * For recording there's and input data endpoint, of
 * the following types:
 *
 *  type	sync	descr
 *  -------------------------------------------------------
 *  async:	-	the device uses its own clock and
 *			adjusts packet sizes.
 *
 *  sync:	-	the device uses usb clock rate
 *
 *  adapt:	Yes	the device uses host's feedback (on
 *			on a dedicated (output) sync endpoint
 *			to adapt to software's desired rate
 *
 *
 * For usb1.1 ival is hardcoded to 1 for isochronous
 * transfers, which means one transfer every ms. I.e one
 * transfer every frame period.
 *
 * For usb2, ival the poll interval is:
 *
 *	frame_period * 2^(ival - 1)
 *
 * so, if we use this formula, we get something working in all
 * cases.
 *
 * The MaxPacketsOnly attribute is used only by "Type II" encodings,
 * so we don't care about it.
 */
int
uaudio_process_as_ep(struct uaudio_softc *sc,
	struct uaudio_blob *p, struct uaudio_alt *a, int nep)
{
	unsigned int addr, attr, maxpkt, isotype, ival;

	if (!uaudio_getnum(p, 1, &addr))
		return 0;
	if (!uaudio_getnum(p, 1, &attr))
		return 0;
	if (!uaudio_getnum(p, 2, &maxpkt))
		return 0;
	if (!uaudio_getnum(p, 1, &ival)) /* bInterval */
		return 0;

	DPRINTF("%s: addr = 0x%x, %s/%s, "
	    "maxpktsz = %d, ival = %d\n",
	    __func__, addr,
	    uaudio_isoname(UE_GET_ISO_TYPE(attr)),
	    uaudio_usagename(UE_GET_ISO_USAGE(attr)),
	    maxpkt, ival);

	if (UE_GET_XFERTYPE(attr) != UE_ISOCHRONOUS) {
		printf("%s: skipped non-isoc endpt.\n", DEVNAME(sc));
		return 1;
	}

	/*
	 * For each AS interface setting, there's a single data
	 * endpoint and an optional feedback endpoint. The
	 * synchronization type is non-zero and must be set in the data
	 * endpoints.
	 *
	 * However, the isoc sync type field of the attribute can't be
	 * trusted: a lot of devices have it wrong. If the isoc sync
	 * type is set it's necessarily a data endpoint, if it's not,
	 * then if it is the only endpoint, it necessarily the data
	 * endpoint.
	 */
	isotype = UE_GET_ISO_TYPE(attr);
	if (isotype || nep == 1) {
		/* this is the data endpoint */

		if (a->data_addr && addr != a->data_addr) {
			printf("%s: skipped extra data endpt.\n", DEVNAME(sc));
			return 1;
		}

		a->mode = (UE_GET_DIR(addr) == UE_DIR_IN) ?
		    AUMODE_RECORD : AUMODE_PLAY;
		a->data_addr = addr;
		a->fps = sc->ufps / (1 << (ival - 1));
		a->maxpkt = UE_GET_SIZE(maxpkt);
	} else {
		/* this is the sync endpoint */

		if (a->sync_addr && addr != a->sync_addr) {
			printf("%s: skipped extra sync endpt.\n", DEVNAME(sc));
			return 1;
		}
		a->sync_addr = addr;
	}

	return 1;
}

/*
 * Parse AS class-specific endpoint descriptor
 */
int
uaudio_process_as_cs_ep(struct uaudio_softc *sc,
	struct uaudio_blob *p, struct uaudio_alt *a, int nep)
{
	unsigned int subtype, attr;

	if (!uaudio_getnum(p, 1, &subtype))
		return 0;
	if (subtype != UAUDIO_AS_EP_GENERAL) {
		DPRINTF("%s: %d: bad cs ep subtype\n", __func__, subtype);
		return 0;
	}
	if (!uaudio_getnum(p, 1, &attr))
		return 0;
	if (sc->version == UAUDIO_V1) {
		a->v1_cap_freqctl = !!(attr & UAUDIO_EP_FREQCTL);
		if (!a->v1_cap_freqctl)
			DPRINTF("alt %d: no rate control\n", a->altnum);
	}
	return 1;
}

/*
 * Parse AS general descriptor. Non-PCM interfaces are skipped. UAC
 * v2.0 report the number of channels. For UAC v1.0 we set the number
 * of channels to zero, it will be determined later from the format
 * descriptor.
 */
int
uaudio_process_as_general(struct uaudio_softc *sc,
	struct uaudio_blob *p, int *rispcm, struct uaudio_alt *a)
{
	unsigned int term, fmt, ctl, fmt_type, fmt_map, nch;

	if (!uaudio_getnum(p, 1, &term))
		return 0;
	switch (sc->version) {
	case UAUDIO_V1:
		if (!uaudio_getnum(p, 1, NULL))	/* bDelay */
			return 0;
		if (!uaudio_getnum(p, 1, &fmt))
			return 0;
		*rispcm = (fmt == UAUDIO_V1_FMT_PCM);
		break;
	case UAUDIO_V2:
		/* XXX: should we check if alt setting control is valid ? */
		if (!uaudio_getnum(p, 1, &ctl))
			return 0;
		if (!uaudio_getnum(p, 1, &fmt_type))
			return 0;
		if (!uaudio_getnum(p, 4, &fmt_map))
			return 0;
		if (!uaudio_getnum(p, 1, &nch))
			return 0;
		a->nch = nch;
		*rispcm = (fmt_type == 1) && (fmt_map & UAUDIO_V2_FMT_PCM);
	}
	return 1;
}

/*
 * Parse AS format descriptor: we support only "Type 1" formats, aka
 * PCM. Other formats are not really audio, they are data-only
 * interfaces that we don't want to support: ethernet is much better
 * for raw data transfers.
 *
 * XXX: handle ieee 754 32-bit floating point formats.
 */
int
uaudio_process_as_format(struct uaudio_softc *sc,
	struct uaudio_blob *p, struct uaudio_alt *a, int *ispcm)
{
	unsigned int type, bps, bits, nch, nrates, rate_min, rate_max, rates;
	int i, j;

	switch (sc->version) {
	case UAUDIO_V1:
		if (!uaudio_getnum(p, 1, &type))
			return 0;
		if (type != 1) {
			DPRINTF("%s: class v1: "
			    "skipped unsupported type = %d\n", __func__, type);
			*ispcm = 0;
			return 1;
		}
		if (!uaudio_getnum(p, 1, &nch))
			return 0;
		if (!uaudio_getnum(p, 1, &bps))
			return 0;
		if (!uaudio_getnum(p, 1, &bits))
			return 0;
		if (!uaudio_getnum(p, 1, &nrates))
			return 0;
		rates = 0;
		if (nrates == 0) {
			if (!uaudio_getnum(p, 3, &rate_min))
				return 0;
			if (!uaudio_getnum(p, 3, &rate_max))
				return 0;
			for (i = 0; i < nitems(uaudio_rates); i++) {
				if (uaudio_rates[i] >= rate_min &&
				    uaudio_rates[i] <= rate_max)
					rates |= 1 << i;
			}
		} else {
			for (j = 0; j < nrates; j++) {
				if (!uaudio_getnum(p, 3, &rate_min))
					return 0;
				for (i = 0; i < nitems(uaudio_rates); i++) {
					if (uaudio_rates[i] == rate_min)
						rates |= 1 << i;
				}
			}
		}
		a->v1_rates = rates;
		a->nch = nch;
		break;
	case UAUDIO_V2:
		/*
		 * sample rate ranges are obtained with requests to
		 * the clock source, as defined by the clock source
		 * descriptor
		 *
		 * the number of channels is in the GENERAL descriptor
		 */
		if (!uaudio_getnum(p, 1, &type))
			return 0;
		if (type != 1) {
			DPRINTF("%s: class v2: "
			    "skipped unsupported type = %d\n", __func__, type);
			*ispcm = 0;
			return 1;
		}
		if (!uaudio_getnum(p, 1, &bps))
			return 0;
		if (!uaudio_getnum(p, 1, &bits))
			return 0;

		/*
		 * nch is in the v2 general desc, rates come from the
		 * clock source, so we're done.
		 */
		break;
	}
	a->bps = bps;
	a->bits = bits;
	*ispcm = 1;
	return 1;
}

/*
 * Parse AS descriptors.
 *
 * The audio streaming (AS) interfaces are used to move data between
 * the host and the device. On the one hand, the device has
 * analog-to-digital (ADC) and digital-to-analog (DAC) converters
 * which have their own low-jitter clock source. On other hand, the
 * USB host runs a bus clock using another clock source. So both
 * drift. That's why, the device sends feedback to the driver for the
 * host to adjust continuously its data rate, hence the need for sync
 * endpoints.
 */
int
uaudio_process_as(struct uaudio_softc *sc,
    struct uaudio_blob *p, int ifnum, int altnum, int nep)
{
	struct uaudio_alt *a, *anext, **pa;
	struct uaudio_blob dp;
	unsigned char *savep;
	unsigned int type, subtype;
	int ispcm = 0;

	a = malloc(sizeof(struct uaudio_alt), M_USBDEV, M_WAITOK);
	a->mode = 0;
	a->nch = 0;
	a->v1_cap_freqctl = 0;
	a->v1_rates = 0;
	a->data_addr = 0;
	a->sync_addr = 0;
	a->ifnum = ifnum;
	a->altnum = altnum;

	while (p->rptr != p->wptr) {
		savep = p->rptr;
		if (!uaudio_getdesc(p, &dp))
			goto failed;
		if (!uaudio_getnum(&dp, 1, &type))
			goto failed;
		if (type != UDESC_CS_INTERFACE) {
			p->rptr = savep;
			break;
		}
		if (!uaudio_getnum(&dp, 1, &subtype))
			goto failed;
		switch (subtype) {
		case UAUDIO_AS_GENERAL:
			if (!uaudio_process_as_general(sc, &dp, &ispcm, a))
				goto failed;
			break;
		case UAUDIO_AS_FORMAT:
			if (!uaudio_process_as_format(sc, &dp, a, &ispcm))
				goto failed;
			break;
		default:
			DPRINTF("%s: unknown desc\n", __func__);
			continue;
		}
		if (!ispcm) {
			DPRINTF("%s: non-pcm iface\n", __func__);
			free(a, M_USBDEV, sizeof(struct uaudio_alt));
			return 1;
		}
	}

	while (p->rptr != p->wptr) {
		savep = p->rptr;
		if (!uaudio_getdesc(p, &dp))
			goto failed;
		if (!uaudio_getnum(&dp, 1, &type))
			goto failed;
		if (type == UDESC_CS_ENDPOINT) {
			if (!uaudio_process_as_cs_ep(sc, &dp, a, nep))
				goto failed;
		} else if (type == UDESC_ENDPOINT) {
			if (!uaudio_process_as_ep(sc, &dp, a, nep))
				goto failed;
		} else {
			p->rptr = savep;
			break;
		}
	}

	if (a->mode == 0) {
		printf("%s: no data endpoints found\n", DEVNAME(sc));
		free(a, M_USBDEV, sizeof(struct uaudio_alt));
		return 1;
	}

	/*
	 * Append to list of alts, but keep the list sorted by number
	 * of channels, bits and rate. From the most capable to the
	 * less capable.
	 */
	pa = &sc->alts;
	while (1) {
		if ((anext = *pa) == NULL)
			break;
		if (a->nch > anext->nch)
			break;
		else if (a->nch == anext->nch) {
			if (a->bits > anext->bits)
				break;
			else if (sc->version == UAUDIO_V1 &&
				a->v1_rates > anext->v1_rates)
				break;
		}
		pa = &anext->next;
	}
	a->next = *pa;
	*pa = a;
	return 1;
failed:
	free(a, M_USBDEV, sizeof(struct uaudio_alt));
	return 0;
}

/*
 * Populate the sc->params_list with combinations of play and rec alt
 * settings that work together in full-duplex.
 */
void
uaudio_fixup_params(struct uaudio_softc *sc)
{
	struct uaudio_alt *ap, *ar, *a;
	struct uaudio_params *p, **pp;
	int rates;

	/*
	 * Add full-duplex parameter combinations.
	 */
	pp = &sc->params_list;
	for (ap = sc->alts; ap != NULL; ap = ap->next) {
		if (ap->mode != AUMODE_PLAY)
			continue;
		for (ar = sc->alts; ar != NULL; ar = ar->next) {
			if (ar->mode != AUMODE_RECORD)
				continue;
			if (ar->bps != ap->bps || ar->bits != ap->bits)
				continue;
			switch (sc->version) {
			case UAUDIO_V1:
				rates = ap->v1_rates & ar->v1_rates;
				if (rates == 0)
					continue;
				break;
			case UAUDIO_V2:
				/* UAC v2.0 common rates */
				rates = 0;
				break;
			}
			p = malloc(sizeof(struct uaudio_params),
			    M_USBDEV, M_WAITOK);
			p->palt = ap;
			p->ralt = ar;
			p->v1_rates = rates;
			p->next = NULL;
			*pp = p;
			pp = &p->next;
		}
	}

	/*
	 * For unidirectional devices, add play-only and or rec-only
	 * parameters.
	 */
	if (sc->params_list == NULL) {
		for (a = sc->alts; a != NULL; a = a->next) {
			p = malloc(sizeof(struct uaudio_params),
			    M_USBDEV, M_WAITOK);
			if (a->mode == AUMODE_PLAY) {
				p->palt = a;
				p->ralt = NULL;
			} else {
				p->palt = NULL;
				p->ralt = a;
			}
			p->v1_rates = a->v1_rates;
			p->next = NULL;
			*pp = p;
			pp = &p->next;
		}
	}
}

int
uaudio_iface_index(struct uaudio_softc *sc, int ifnum)
{
	int i, nifaces;

	nifaces = sc->udev->cdesc->bNumInterfaces;

	for (i = 0; i < nifaces; i++) {
		if (sc->udev->ifaces[i].idesc->bInterfaceNumber == ifnum)
			return i;
	}

	printf("%s: %d: invalid interface number\n", __func__, ifnum);
	return -1;
}

/*
 * Parse all descriptors and build configuration of the device.
 */
int
uaudio_process_conf(struct uaudio_softc *sc, struct uaudio_blob *p)
{
	struct uaudio_blob dp;
	struct uaudio_alt *a;
	unsigned int type, ifnum, altnum, nep, class, subclass;
	int i;

	while (p->rptr != p->wptr) {
		if (!uaudio_getdesc(p, &dp))
			return 0;
		if (!uaudio_getnum(&dp, 1, &type))
			return 0;
		if (type != UDESC_INTERFACE)
			continue;
		if (!uaudio_getnum(&dp, 1, &ifnum))
			return 0;
		if (!uaudio_getnum(&dp, 1, &altnum))
			return 0;
		if (!uaudio_getnum(&dp, 1, &nep))
			return 0;
		if (!uaudio_getnum(&dp, 1, &class))
			return 0;
		if (!uaudio_getnum(&dp, 1, &subclass))
			return 0;
		if (class != UICLASS_AUDIO) {
			DPRINTF("%s: skipped iface\n", __func__);
			continue;
		}

		switch (subclass) {
		case UISUBCLASS_AUDIOCONTROL:
			i = uaudio_iface_index(sc, ifnum);
			if (i != -1 && usbd_iface_claimed(sc->udev, i)) {
				DPRINTF("%s: %d: AC already claimed\n", __func__, ifnum);
				break;
			}
			if (sc->unit_list != NULL) {
				DPRINTF("%s: >1 AC ifaces\n", __func__);
				goto done;
			}
			if (!uaudio_process_ac(sc, p, ifnum))
				return 0;
			break;
		case UISUBCLASS_AUDIOSTREAM:
			i = uaudio_iface_index(sc, ifnum);
			if (i != -1 && usbd_iface_claimed(sc->udev, i)) {
				DPRINTF("%s: %d: AS already claimed\n", __func__, ifnum);
				break;
			}
			if (nep == 0) {
				DPRINTF("%s: stop altnum %d, ifnum %d\n",
				    __func__, altnum, ifnum);
				break;	/* 0 is "stop sound", skip it */
			}
			if (!uaudio_process_as(sc, p, ifnum, altnum, nep))
				return 0;
		}
	}
done:
	uaudio_fixup_params(sc);

	/*
	 * Claim all interfaces we use. This prevents other uaudio(4)
	 * devices from trying to use them.
	 */
	for (a = sc->alts; a != NULL; a = a->next) {
		i = uaudio_iface_index(sc, a->ifnum);
		if (i != -1) {
			DPRINTF("%s: claim: %d at %d\n", __func__, a->ifnum, i);
			usbd_claim_iface(sc->udev, i);
		}
	}

	i = uaudio_iface_index(sc, sc->ctl_ifnum);
	if (i != -1) {
		DPRINTF("%s: claim: ac %d at %d\n", __func__, sc->ctl_ifnum, i);
		usbd_claim_iface(sc->udev, i);
	}

	return 1;
}

/*
 * Allocate a isochronous transfer and its bounce-buffers with the
 * given maximum framesize and maximum frames per transfer.
 */
int
uaudio_xfer_alloc(struct uaudio_softc *sc, struct uaudio_xfer *xfer,
	unsigned int framesize, unsigned int count)
{
	xfer->usb_xfer = usbd_alloc_xfer(sc->udev);
	if (xfer->usb_xfer == NULL)
		return ENOMEM;

	xfer->buf = usbd_alloc_buffer(xfer->usb_xfer, framesize * count);
	if (xfer->buf == NULL)
		return ENOMEM;

	xfer->sizes = mallocarray(count,
	    sizeof(xfer->sizes[0]), M_USBDEV, M_WAITOK);
	if (xfer->sizes == NULL)
		return ENOMEM;

	return 0;
}

/*
 * Free a isochronous transfer and its bounce-buffers.
 */
void
uaudio_xfer_free(struct uaudio_softc *sc, struct uaudio_xfer *xfer,
	unsigned int count)
{
	if (xfer->usb_xfer != NULL) {
		/* frees request buffer as well */
		usbd_free_xfer(xfer->usb_xfer);
		xfer->usb_xfer = NULL;
	}
	if (xfer->sizes != NULL) {
		free(xfer->sizes, M_USBDEV,
		    sizeof(xfer->sizes[0]) * count);
		xfer->sizes = NULL;
	}
}

/*
 * Close a stream and free all associated resources
 */
void
uaudio_stream_close(struct uaudio_softc *sc, int dir)
{
	struct uaudio_stream *s = &sc->pstream;
	struct uaudio_alt *a = sc->params->palt;
	struct usbd_interface *iface;
	int err, i;

	if (dir == AUMODE_PLAY) {
		s = &sc->pstream;
		a = sc->params->palt;
	} else {
		s = &sc->rstream;
		a = sc->params->ralt;
	}

	if (s->data_pipe) {
		usbd_close_pipe(s->data_pipe);
		s->data_pipe = NULL;
	}

	if (s->sync_pipe) {
		usbd_close_pipe(s->sync_pipe);
		s->sync_pipe = NULL;
	}

	err = usbd_device2interface_handle(sc->udev, a->ifnum, &iface);
	if (err)
		printf("%s: can't get iface handle\n", DEVNAME(sc));
	else {
		err = usbd_set_interface(iface, 0);
		if (err)
			printf("%s: can't reset interface\n", DEVNAME(sc));
	}

	for (i = 0; i < UAUDIO_NXFERS_MAX; i++) {
		uaudio_xfer_free(sc, s->data_xfers + i, s->nframes_max);
		uaudio_xfer_free(sc, s->sync_xfers + i, 1);
	}
}

/*
 * Open a stream with the given buffer settings and set the current
 * interface alt setting.
 */
int
uaudio_stream_open(struct uaudio_softc *sc, int dir,
    void *start, void *end, size_t blksz, void (*intr)(void *), void *arg)
{
	struct uaudio_stream *s;
	struct uaudio_alt *a;
	struct uaudio_unit *clock;
	struct usbd_interface *iface;
	unsigned char req_buf[4];
	unsigned int bpa, spf_max, min_blksz;
	int err, i;

	if (dir == AUMODE_PLAY) {
		s = &sc->pstream;
		a = sc->params->palt;
	} else {
		s = &sc->rstream;
		a = sc->params->ralt;
	}

	for (i = 0; i < UAUDIO_NXFERS_MAX; i++) {
		s->data_xfers[i].usb_xfer = NULL;
		s->data_xfers[i].sizes = NULL;
		s->sync_xfers[i].usb_xfer = NULL;
		s->sync_xfers[i].sizes = NULL;
	}
	s->data_pipe = NULL;
	s->sync_pipe = NULL;

	s->nframes_mask = 0;
	i = a->fps;
	while (i > 1000) {
		s->nframes_mask = (s->nframes_mask << 1) | 1;
		i >>= 1;
	}

	/* bytes per audio frame */
	bpa = a->bps * a->nch;

	/* ideal samples per usb frame, fixed-point */
	s->spf = (uint64_t)sc->rate * UAUDIO_SPF_DIV / a->fps;

	/*
	 * UAC2.0 spec allows 1000PPM tolerance in sample frequency,
	 * while USB1.1 requires 1Hz, which is 125PPM at 8kHz. We
	 * accept as much as 1/256, which is 2500PPM.
	 */
	s->spf_min = (uint64_t)s->spf * 255 / 256;
	s->spf_max = (uint64_t)s->spf * 257 / 256;

	/* max spf can't exceed the device usb packet size */
	spf_max = (a->maxpkt / bpa) * UAUDIO_SPF_DIV;
	if (s->spf > spf_max) {
		printf("%s: samples per frame too large\n", DEVNAME(sc));
		return EIO;
	}
	if (s->spf_max > spf_max)
		s->spf_max = spf_max;

	/*
	 * Upon transfer completion the device must reach the audio
	 * block boundary, which is propagated to upper layers. In the
	 * worst case, we schedule only frames of spf_max samples, but
	 * the device returns only frames of spf_min samples; in this
	 * case the amount actually transferred is at least:
	 *
	 *		min_blksz = blksz / spf_max * spf_min
	 *
	 * As we've UAUDIO_NXFERS outstanding blocks, worst-case
	 * remaining bytes is at most:
	 *
	 *		UAUDIO_NXFERS * (blksz - min_blksz)
	 */
	min_blksz = (((uint64_t)blksz << 32) / s->spf_max * s->spf_min) >> 32;

	/* round to sample size */
	min_blksz -= min_blksz % bpa;

	/* finally this is what ensures we cross block boundary */
	s->safe_blksz = blksz + UAUDIO_NXFERS_MAX * (blksz - min_blksz);

	/* max number of (micro-)frames we'll ever use */
	s->nframes_max = (uint64_t)(s->safe_blksz / bpa) *
	    UAUDIO_SPF_DIV / s->spf_min + 1;

	/* round to next usb1.1 frame */
	s->nframes_max = (s->nframes_max + s->nframes_mask) &
	    ~s->nframes_mask;

	/* this is the max packet size we'll ever need */
	s->maxpkt = bpa *
	    ((s->spf_max + UAUDIO_SPF_DIV - 1) / UAUDIO_SPF_DIV);

	/* how many xfers we need to fill sc->host_nframes */
	s->nxfers = sc->host_nframes / s->nframes_max;
	if (s->nxfers > UAUDIO_NXFERS_MAX)
		s->nxfers = UAUDIO_NXFERS_MAX;

	DPRINTF("%s: %s: blksz = %zu, rate = %u, fps = %u\n", __func__,
	    dir == AUMODE_PLAY ? "play" : "rec", blksz, sc->rate, a->fps);
	DPRINTF("%s: spf = 0x%x in [0x%x:0x%x]\n", __func__,
	    s->spf, s->spf_min, s->spf_max);
	DPRINTF("%s: nframes_max = %u, nframes_mask = %u, maxpkt = %u\n",
	    __func__, s->nframes_max, s->nframes_mask, s->maxpkt);
	DPRINTF("%s: safe_blksz = %d, nxfers = %d\n", __func__,
	    s->safe_blksz, s->nxfers);

	if (s->nxfers < UAUDIO_NXFERS_MIN) {
		printf("%s: block size too large\n", DEVNAME(sc));
		return EIO;
	}

	/*
	 * Require at least 2ms block size to ensure no
	 * transfer exceeds two blocks.
	 * 
	 * XXX: use s->nframes_mask instead of 1000
	 */
	if (1000 * blksz < 2 * sc->rate * bpa) {
		printf("%s: audio block too small\n", DEVNAME(sc));
		return EIO;
	}

	for (i = 0; i < s->nxfers; i++) {
		err = uaudio_xfer_alloc(sc, s->data_xfers + i,
		    s->maxpkt, s->nframes_max);
		if (err)
			goto failed;
		if (a->sync_addr) {
			err = uaudio_xfer_alloc(sc, s->sync_xfers + i,
			    sc->sync_pktsz, 1);
			if (err)
				goto failed;
		}
	}

	err = usbd_device2interface_handle(sc->udev, a->ifnum, &iface);
	if (err) {
		printf("%s: can't get iface handle\n", DEVNAME(sc));
		goto failed;
	}

	err = usbd_set_interface(iface, a->altnum);
	if (err) {
		printf("%s: can't set interface\n", DEVNAME(sc));
		goto failed;
	}

	/*
	 * Set the sample rate.
	 *
	 * Certain devices are able to lock their clock to the data
	 * rate and expose no frequency control. In this case, the
	 * request to set the frequency will fail and freeze the device.
	 */
	switch (sc->version) {
	case UAUDIO_V1:
		if (!a->v1_cap_freqctl) {
			DPRINTF("%s: not setting endpoint rate\n", __func__);
			break;
		}
		req_buf[0] = sc->rate;
		req_buf[1] = sc->rate >> 8;
		req_buf[2] = sc->rate >> 16;
		if (!uaudio_req(sc, UT_WRITE_CLASS_ENDPOINT,
			UAUDIO_V1_REQ_SET_CUR, UAUDIO_REQSEL_RATE, 0,
			a->data_addr, 0, req_buf, 3)) {
			printf("%s: failed to set endpoint rate\n", DEVNAME(sc));
		}
		break;
	case UAUDIO_V2:
		req_buf[0] = sc->rate;
		req_buf[1] = sc->rate >> 8;
		req_buf[2] = sc->rate >> 16;
		req_buf[3] = sc->rate >> 24;
		clock = uaudio_clock(sc);
		if (clock == NULL) {
			printf("%s: can't get clock\n", DEVNAME(sc));
			goto failed;
		}
		if (!clock->cap_freqctl) {
			DPRINTF("%s: not setting clock rate\n", __func__);
			break;
		}
		if (!uaudio_req(sc, UT_WRITE_CLASS_INTERFACE,
			UAUDIO_V2_REQ_CUR, UAUDIO_REQSEL_RATE, 0,
			sc->ctl_ifnum, clock->id, req_buf, 4)) {
			printf("%s: failed to set clock rate\n", DEVNAME(sc));
		}
		break;
	}

	err = usbd_open_pipe(iface, a->data_addr, 0, &s->data_pipe);
	if (err) {
		printf("%s: can't open data pipe\n", DEVNAME(sc));
		goto failed;
	}

	if (a->sync_addr) {
		err = usbd_open_pipe(iface, a->sync_addr, 0, &s->sync_pipe);
		if (err) {
			printf("%s: can't open sync pipe\n", DEVNAME(sc));
			goto failed;
		}
	}

	s->data_nextxfer = 0;
	s->sync_nextxfer = 0;
	s->spf_remain = 0;

	s->intr = intr;
	s->arg = arg;
	s->ring_start = start;
	s->ring_end = end;
	s->ring_blksz = blksz;

	s->ring_pos = s->ring_start;
	s->ring_offs = 0;
	s->ring_icnt = 0;

	s->ubuf_xfer = 0;
	s->ubuf_pos = 0;
	return 0;

failed:
	uaudio_stream_close(sc, dir);
	return ENOMEM;
}

/*
 * Adjust play samples-per-frame to keep play and rec streams in sync.
 */
void
uaudio_adjspf(struct uaudio_softc *sc)
{
	struct uaudio_stream *s = &sc->pstream;
	int diff;

	if (sc->mode != (AUMODE_RECORD | AUMODE_PLAY))
		return;
	if (s->sync_pipe != NULL)
		return;

	/*
	 * number of samples play stream is ahead of record stream.
	 */
	diff = sc->diff_nsamp;
	if (sc->diff_nframes > 0) {
		diff -= (uint64_t)sc->pstream.spf *
		    sc->diff_nframes / UAUDIO_SPF_DIV;
	} else {
		diff += (uint64_t)sc->rstream.spf *
		    -sc->diff_nframes / UAUDIO_SPF_DIV;
	}

	/*
	 * adjust samples-per-frames to resync within the next second
	 */
	s->spf = (uint64_t)(sc->rate - diff) * UAUDIO_SPF_DIV / sc->ufps;
	if (s->spf > s->spf_max)
		s->spf = s->spf_max;
	else if (s->spf < s->spf_min)
		s->spf = s->spf_min;
#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 2)
		printf("%s: diff = %d, spf = 0x%x\n", __func__, diff, s->spf);
#endif
}

/*
 * Copy one audio block to the xfer buffer.
 */
void
uaudio_pdata_copy(struct uaudio_softc *sc)
{
	struct uaudio_stream *s = &sc->pstream;
	struct uaudio_xfer *xfer;
	size_t count, avail;
	int index;
#ifdef UAUDIO_DEBUG
	struct timeval tv;

	getmicrotime(&tv);
#endif
	while (sc->copy_todo > 0 && s->ubuf_xfer < s->nxfers) {
		index = s->data_nextxfer + s->ubuf_xfer;
		if (index >= s->nxfers)
			index -= s->nxfers;
		xfer = s->data_xfers + index;
		avail = s->ring_end - s->ring_pos;
		count = xfer->size - s->ubuf_pos;
		if (count > avail)
			count = avail;
		if (count > sc->copy_todo)
			count = sc->copy_todo;
#ifdef UAUDIO_DEBUG
		if (uaudio_debug >= 2) {
			printf("%s: %llu.%06lu: %zd..%zd -> %u:%u..%zu\n",
			    __func__, tv.tv_sec, tv.tv_usec,
			    s->ring_pos - s->ring_start,
			    s->ring_pos - s->ring_start + count,
			    s->ubuf_xfer, s->ubuf_pos, s->ubuf_pos + count);
		}
#endif
		memcpy(xfer->buf + s->ubuf_pos, s->ring_pos, count);
		sc->copy_todo -= count;
		s->ring_pos += count;
		if (s->ring_pos == s->ring_end) {
			s->ring_pos = s->ring_start;
		}
		s->ubuf_pos += count;
		if (s->ubuf_pos == xfer->size) {
			usb_syncmem(&xfer->usb_xfer->dmabuf, 0, xfer->size,
			    BUS_DMASYNC_PREWRITE);
			s->ubuf_pos = 0;
#ifdef DIAGNOSTIC
			if (s->ubuf_xfer == s->nxfers) {
				printf("%s: overflow\n", __func__);
				return;
			}
#endif
			s->ubuf_xfer++;
		}
	}
}

/*
 * Calculate and fill xfer frames sizes.
 */
void
uaudio_pdata_calcsizes(struct uaudio_softc *sc, struct uaudio_xfer *xfer)
{
#ifdef UAUDIO_DEBUG
	struct timeval tv;
#endif
	struct uaudio_stream *s = &sc->pstream;
	struct uaudio_alt *a = sc->params->palt;
	unsigned int fsize, bpf;
	int done;

	bpf = a->bps * a->nch;
	done = s->ring_offs;
	xfer->nframes = 0;

	while (1) {
		/*
		 * if we crossed the next block boundary, we're done
		 */
		if ((xfer->nframes & s->nframes_mask) == 0 &&
		    done > s->safe_blksz)
			break;

		/*
		 * this can't happen, debug only
		 */
		if (xfer->nframes == s->nframes_max) {
			printf("%s: too many frames for play xfer: "
			    "done = %u, blksz = %d\n",
			    DEVNAME(sc), done, s->ring_blksz);
			break;
		}

		/*
		 * calculate frame size and adjust state
		 */
		s->spf_remain += s->spf;
		fsize = s->spf_remain / UAUDIO_SPF_DIV * bpf;
		s->spf_remain %= UAUDIO_SPF_DIV;
		done += fsize;
		xfer->sizes[xfer->nframes] = fsize;
		xfer->nframes++;
	}

	xfer->size = done - s->ring_offs;
	s->ring_offs = done - s->ring_blksz;

#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 3) {
		getmicrotime(&tv);
		printf("%s: size = %d, offs -> %d\n", __func__,
		    xfer->size, s->ring_offs);
	}
#endif
	memset(xfer->buf, 0, xfer->size);
}

/*
 * Submit a play data transfer to the USB driver.
 */
void
uaudio_pdata_xfer(struct uaudio_softc *sc)
{
#ifdef UAUDIO_DEBUG
	struct timeval tv;
#endif
	struct uaudio_stream *s = &sc->pstream;
	struct uaudio_xfer *xfer;
	int err;

	xfer = s->data_xfers + s->data_nextxfer;

#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 3) {
		getmicrotime(&tv);
		printf("%s: %llu.%06lu: "
		    "%d bytes, %u frames, remain = 0x%x, offs = %d\n",
		    __func__, tv.tv_sec, tv.tv_usec,
		    xfer->size, xfer->nframes,
		    s->spf_remain, s->ring_offs);
	}
#endif

	/* this can't happen, debug only */
	if (xfer->nframes == 0) {
		printf("%s: zero frame play xfer\n", DEVNAME(sc));
		return;
	}

	/*
	 * We accept short transfers because in case of babble/stale frames
	 * the transfer will be short
	 */
	usbd_setup_isoc_xfer(xfer->usb_xfer, s->data_pipe, sc,
	    xfer->sizes, xfer->nframes,
	    USBD_NO_COPY | USBD_SHORT_XFER_OK,
	    uaudio_pdata_intr);

	err = usbd_transfer(xfer->usb_xfer);
	if (err != 0 && err != USBD_IN_PROGRESS)
		printf("%s: play xfer, err = %d\n", DEVNAME(sc), err);

	if (++s->data_nextxfer == s->nxfers)
		s->data_nextxfer = 0;
}

/*
 * Callback called by the USB driver upon completion of play data transfer.
 */
void
uaudio_pdata_intr(struct usbd_xfer *usb_xfer, void *arg, usbd_status status)
{
#ifdef UAUDIO_DEBUG
	struct timeval tv;
#endif
	struct uaudio_softc *sc = arg;
	struct uaudio_stream *s = &sc->pstream;
	struct uaudio_xfer *xfer;
	uint32_t size;
	int nintr;

	if (status != 0 && status != USBD_IOERROR) {
		DPRINTF("%s: xfer status = %d\n", __func__, status);
		return;
	}

	xfer = s->data_xfers + s->data_nextxfer;
	if (xfer->usb_xfer != usb_xfer) {
		DPRINTF("%s: wrong xfer\n", __func__);
		return;
	}

	sc->diff_nsamp += xfer->size /
	    (sc->params->palt->nch * sc->params->palt->bps);
	sc->diff_nframes += xfer->nframes;

#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 2) {
		getmicrotime(&tv);
		printf("%s: %llu.%06lu: %u: %u bytes\n",
		    __func__, tv.tv_sec, tv.tv_usec,
		    s->data_nextxfer, xfer->size);
	}
#endif
	usbd_get_xfer_status(usb_xfer, NULL, NULL, &size, NULL);
	if (size != xfer->size) {
		DPRINTF("%s: %u bytes out of %u: incomplete play xfer\n",
		    DEVNAME(sc), size, xfer->size);
	}

	/*
	 * Upper layer call-back may call uaudio_underrun(), which
	 * needs the current size of this transfer. So, don't
	 * recalculate the sizes and don't schedule the transfer yet.
	 */
	s->ring_icnt += xfer->size;
	nintr = 0;
	mtx_enter(&audio_lock);
	while (s->ring_icnt >= s->ring_blksz) {
		s->intr(s->arg);
		s->ring_icnt -= s->ring_blksz;
		nintr++;
	}
	mtx_leave(&audio_lock);
	if (nintr != 1)
		printf("%s: %d: bad play intr count\n", __func__, nintr);

	uaudio_pdata_calcsizes(sc, xfer);
	uaudio_pdata_xfer(sc);
#ifdef DIAGNOSTIC
	if (s->ubuf_xfer == 0) {
		printf("%s: underflow\n", __func__);
		return;
	}
#endif
	s->ubuf_xfer--;
	uaudio_pdata_copy(sc);
}

/*
 * Submit a play sync transfer to the USB driver.
 */
void
uaudio_psync_xfer(struct uaudio_softc *sc)
{
#ifdef UAUDIO_DEBUG
	struct timeval tv;
#endif
	struct uaudio_stream *s = &sc->pstream;
	struct uaudio_xfer *xfer;
	unsigned int i;
	int err;

	xfer = s->sync_xfers + s->sync_nextxfer;
	xfer->nframes = 1;

	for (i = 0; i < xfer->nframes; i++)
		xfer->sizes[i] = sc->sync_pktsz;

	xfer->size = xfer->nframes * sc->sync_pktsz;

#ifdef UAUDIO_DEBUG
	memset(xfer->buf, 0xd0, sc->sync_pktsz * xfer->nframes);
#endif

	usbd_setup_isoc_xfer(xfer->usb_xfer, s->sync_pipe, sc,
	    xfer->sizes, xfer->nframes,
	    USBD_NO_COPY | USBD_SHORT_XFER_OK,
	    uaudio_psync_intr);

	err = usbd_transfer(xfer->usb_xfer);
	if (err != 0 && err != USBD_IN_PROGRESS)
		printf("%s: sync play xfer, err = %d\n", DEVNAME(sc), err);

	if (++s->sync_nextxfer == s->nxfers)
		s->sync_nextxfer = 0;

#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 3) {
		getmicrotime(&tv);
		printf("%s: %llu.%06lu: %dB, %d fr\n", __func__,
		    tv.tv_sec, tv.tv_usec, sc->sync_pktsz, xfer->nframes);
	}
#endif
}

/*
 * Callback called by the USB driver upon completion of play sync transfer.
 */
void
uaudio_psync_intr(struct usbd_xfer *usb_xfer, void *arg, usbd_status status)
{
#ifdef UAUDIO_DEBUG
	struct timeval tv;
#endif
	struct uaudio_softc *sc = arg;
	struct uaudio_stream *s = &sc->pstream;
	struct uaudio_xfer *xfer;
	unsigned char *buf;
	unsigned int i;
	int32_t val;

	if (status != 0) {
		DPRINTF("%s: xfer status = %d\n", __func__, status);
		return;
	}

	xfer = s->sync_xfers + s->sync_nextxfer;
	if (xfer->usb_xfer != usb_xfer) {
		DPRINTF("%s: wrong xfer\n", __func__);
		return;
	}

	/* XXX: there's only one frame, the loop is not necessary */

	buf = xfer->buf;
	for (i = 0; i < xfer->nframes; i++) {
		if (xfer->sizes[i] == sc->sync_pktsz) {
			val = buf[0] | buf[1] << 8 | buf[2] << 16;
			if (sc->sync_pktsz == 4)
				val |= xfer->buf[3] << 24;
			else
				val <<= 2;
			val *= UAUDIO_SPF_DIV / (1 << 16);
#ifdef UAUDIO_DEBUG
			if (uaudio_debug >= 2) {
				getmicrotime(&tv);
				printf("%s: %llu.%06lu: spf: %08x\n",
				    __func__, tv.tv_sec, tv.tv_usec, val);
			}
#endif
			if (val > s->spf_max)
				s->spf = s->spf_max;
			else if (val < s->spf_min)
				s->spf = s->spf_min;
			else
				s->spf = val;
		}
		buf += sc->sync_pktsz;
	}

	uaudio_psync_xfer(sc);
}

/*
 * Submit a rec data transfer to the USB driver.
 */
void
uaudio_rdata_xfer(struct uaudio_softc *sc)
{
#ifdef UAUDIO_DEBUG
	struct timeval tv;
#endif
	struct uaudio_stream *s = &sc->rstream;
	struct uaudio_alt *a = sc->params->ralt;
	struct uaudio_xfer *xfer;
	unsigned int fsize, bpf;
	int done;
	int err;

	xfer = s->data_xfers + s->data_nextxfer;
	bpf = a->bps * a->nch;
	xfer->nframes = 0;
	done = s->ring_offs;

	while (1) {
		/*
		 * if we crossed the next block boundary, we're done
		 */
		if ((xfer->nframes & s->nframes_mask) == 0 &&
		    done > s->safe_blksz) {
		done:
			xfer->size = done - s->ring_offs;
			s->ring_offs = done - s->ring_blksz;
			break;
		}

		/*
		 * this can't happen, debug only
		 */
		if (xfer->nframes == s->nframes_max) {
			printf("%s: too many frames for rec xfer: "
			    "done = %d, blksz = %d\n",
			    DEVNAME(sc), done, s->ring_blksz);
			goto done;
		}

		/*
		 * estimate next block using s->spf, but allow
		 * transfers up to maxpkt
		 */
		s->spf_remain += s->spf;
		fsize = s->spf_remain / UAUDIO_SPF_DIV * bpf;
		s->spf_remain %= UAUDIO_SPF_DIV;
		done += fsize;
		xfer->sizes[xfer->nframes] = s->maxpkt;
		xfer->nframes++;
	}

#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 3) {
		getmicrotime(&tv);
		printf("%s: %llu.%06lu: "
		    "%u fr, %d bytes (max %d), offs = %d\n",
		    __func__, tv.tv_sec, tv.tv_usec,
		    xfer->nframes, xfer->size,
		    s->maxpkt * xfer->nframes, s->ring_offs);
	}
#endif

	/* this can't happen, debug only */
	if (xfer->nframes == 0) {
		printf("%s: zero frame rec xfer\n", DEVNAME(sc));
		return;
	}

#ifdef UAUDIO_DEBUG
	memset(xfer->buf, 0xd0, s->maxpkt * xfer->nframes);
#endif
	usbd_setup_isoc_xfer(xfer->usb_xfer, s->data_pipe, sc,
	    xfer->sizes, xfer->nframes, USBD_NO_COPY | USBD_SHORT_XFER_OK,
	    uaudio_rdata_intr);

	err = usbd_transfer(xfer->usb_xfer);
	if (err != 0 && err != USBD_IN_PROGRESS)
		printf("%s: rec xfer, err = %d\n", DEVNAME(sc), err);

	if (++s->data_nextxfer == s->nxfers)
		s->data_nextxfer = 0;
}

/*
 * Callback called by the USB driver upon completion of rec data transfer.
 */
void
uaudio_rdata_intr(struct usbd_xfer *usb_xfer, void *arg, usbd_status status)
{
#ifdef UAUDIO_DEBUG
	struct timeval tv;
#endif
	struct uaudio_softc *sc = arg;
	struct uaudio_stream *s = &sc->rstream;
	struct uaudio_alt *a = sc->params->ralt;
	struct uaudio_xfer *xfer;
	unsigned char *buf, *framebuf;
	unsigned int count, fsize, fsize_min, nframes, bpf;
	unsigned int data_size, null_count;
	unsigned int nintr;

	if (status != 0) {
		DPRINTF("%s: xfer status = %d\n", __func__, status);
		return;
	}

	xfer = s->data_xfers + s->data_nextxfer;
	if (xfer->usb_xfer != usb_xfer) {
		DPRINTF("%s: wrong xfer\n", __func__);
		return;
	}

	bpf = a->bps * a->nch;
	framebuf = xfer->buf;
	nframes = 0;
	null_count = 0;
	data_size = 0;
	fsize_min = s->spf_min / UAUDIO_SPF_DIV;
	for (nframes = 0; nframes < xfer->nframes; nframes++) {

		/*
		 * Device clock may take some time to lock during which
		 * we'd receive empty or incomplete packets for which we
		 * need to generate silence.
		 */
		fsize = xfer->sizes[nframes];
		if (fsize < fsize_min) {
			s->spf_remain += s->spf;
			fsize = s->spf_remain / UAUDIO_SPF_DIV * bpf;
			s->spf_remain %= UAUDIO_SPF_DIV;
			memset(framebuf, 0, fsize);
			null_count++;
		}
		data_size += fsize;

		/*
		 * fill ring from frame buffer, handling
		 * boundary conditions
		 */
		buf = framebuf;
		while (fsize > 0) {
			count = s->ring_end - s->ring_pos;
			if (count > fsize)
				count = fsize;
			memcpy(s->ring_pos, buf, count);
			s->ring_pos += count;
			if (s->ring_pos == s->ring_end)
				s->ring_pos = s->ring_start;
			buf += count;
			fsize -= count;
		}

		framebuf += s->maxpkt;
	}

	s->ring_offs += data_size - xfer->size;
	s->ring_icnt += data_size;

	sc->diff_nsamp -= data_size /
	    (sc->params->ralt->nch * sc->params->ralt->bps);
	sc->diff_nframes -= xfer->nframes;

	sc->adjspf_age += xfer->nframes;
	if (sc->adjspf_age >= sc->ufps / 8) {
		sc->adjspf_age -= sc->ufps / 8;
		uaudio_adjspf(sc);
	}

#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 2) {
		getmicrotime(&tv);
		printf("%s: %llu.%06lu: %u: "
		    "%u bytes of %u, offs -> %d\n",
		    __func__, tv.tv_sec, tv.tv_usec,
		    s->data_nextxfer, data_size, xfer->size, s->ring_offs);
	}
	if (null_count > 0) {
		DPRINTF("%s: %u null frames out of %u: incomplete record xfer\n",
		    DEVNAME(sc), null_count, xfer->nframes);
	}
#endif
	uaudio_rdata_xfer(sc);

	nintr = 0;
	mtx_enter(&audio_lock);
	while (s->ring_icnt >= s->ring_blksz) {
		s->intr(s->arg);
		s->ring_icnt -= s->ring_blksz;
		nintr++;
	}
	mtx_leave(&audio_lock);
	if (nintr != 1)
		printf("%s: %u: bad rec intr count\n", DEVNAME(sc), nintr);
}

/*
 * Start simultaneously playback and recording, unless trigger_input()
 * and trigger_output() were not both called yet.
 */
void
uaudio_trigger(struct uaudio_softc *sc)
{
	int i, s;

	if (sc->mode != sc->trigger_mode)
		return;

	DPRINTF("%s: preparing\n", __func__);
	if (sc->mode & AUMODE_PLAY) {
		for (i = 0; i < sc->pstream.nxfers; i++)
			uaudio_pdata_calcsizes(sc, sc->pstream.data_xfers + i);

		uaudio_pdata_copy(sc);
	}

	sc->diff_nsamp = 0;
	sc->diff_nframes = 0;
	sc->adjspf_age = 0;

	DPRINTF("%s: starting\n", __func__);
	s = splusb();
	for (i = 0; i < UAUDIO_NXFERS_MAX; i++) {
		if ((sc->mode & AUMODE_PLAY) && i < sc->pstream.nxfers) {
			if (sc->pstream.sync_pipe)
				uaudio_psync_xfer(sc);
			uaudio_pdata_xfer(sc);
		}
		if ((sc->mode & AUMODE_RECORD) && i < sc->rstream.nxfers)
			uaudio_rdata_xfer(sc);
	}
	splx(s);
}

void
uaudio_print(struct uaudio_softc *sc)
{
	struct uaudio_unit *u;
	struct uaudio_mixent *m;
	struct uaudio_params *p;
	int pchan = 0, rchan = 0, async = 0;
	int nctl = 0;

	for (u = sc->unit_list; u != NULL; u = u->unit_next) {
		m = u->mixent_list;
		while (1) {
			uaudio_mixer_skip(&m);
			if (m == NULL)
				break;
			m = m->next;
			nctl++;
		}
	}

	for (p = sc->params_list; p != NULL; p = p->next) {
		if (p->palt && p->palt->nch > pchan)
			pchan = p->palt->nch;
		if (p->ralt && p->ralt->nch > rchan)
			rchan = p->ralt->nch;
		if (p->palt && p->palt->sync_addr)
			async = 1;
		if (p->ralt && p->ralt->sync_addr)
			async = 1;
	}

	printf("%s: class v%d, %s, %s, channels: %d play, %d rec, %d ctls\n",
	    DEVNAME(sc),
	    sc->version >> 8,
	    sc->ufps == 1000 ? "full-speed" : "high-speed",
	    async ? "async" : "sync",
	    pchan, rchan, nctl);
}

int
uaudio_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *arg = aux;
	struct usb_interface_descriptor *idesc;

	if (arg->iface == NULL || arg->device == NULL)
		return UMATCH_NONE;

	idesc = usbd_get_interface_descriptor(arg->iface);
	if (idesc == NULL) {
		DPRINTF("%s: couldn't get idesc\n", __func__);
		return UMATCH_NONE;
	}

	if (idesc->bInterfaceClass != UICLASS_AUDIO ||
	    idesc->bInterfaceSubClass != UISUBCLASS_AUDIOSTREAM)
		return UMATCH_NONE;

	return UMATCH_VENDOR_PRODUCT_CONF_IFACE;
}

void
uaudio_attach(struct device *parent, struct device *self, void *aux)
{
	struct uaudio_softc *sc = (struct uaudio_softc *)self;
	struct usb_attach_arg *arg = aux;
	struct usb_config_descriptor *cdesc;
	struct uaudio_blob desc;

	/*
	 * this device has audio AC or AS or MS interface, get the
	 * full config descriptor and attach audio devices
	 */

	cdesc = usbd_get_config_descriptor(arg->device);
	if (cdesc == NULL)
		return;

	desc.rptr = (unsigned char *)cdesc;
	desc.wptr = desc.rptr + UGETW(cdesc->wTotalLength);

	sc->udev = arg->device;
	sc->unit_list = NULL;
	sc->names = NULL;
	sc->alts = NULL;
	sc->params_list = NULL;
	sc->clock = NULL;
	sc->params = NULL;
	sc->rate = 0;
	sc->mode = 0;
	sc->trigger_mode = 0;
	sc->copy_todo = 0;

	/*
	 * Ideally the USB host controller should expose the number of
	 * frames we're allowed to schedule, but there's no such
	 * interface. The uhci(4) driver can buffer up to 128 frames
	 * (or it crashes), ehci(4) starts recording null frames if we
	 * exceed 256 (micro-)frames, ohci(4) works with at most 50
	 * frames.
	 */
	switch (sc->udev->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
		sc->ufps = 1000;
		sc->sync_pktsz = 3;
		sc->host_nframes = 50;
		break;
	case USB_SPEED_HIGH:
	case USB_SPEED_SUPER:
		sc->ufps = 8000;
		sc->sync_pktsz = 4;
		sc->host_nframes = 240;
		break;
	default:
		printf("%s: unsupported bus speed\n", DEVNAME(sc));
		return;
	}

	if (!uaudio_process_conf(sc, &desc))
		return;

#ifdef UAUDIO_DEBUG
	if (uaudio_debug)
		uaudio_conf_print(sc);
#endif
	/* print a nice uaudio attach line */
	uaudio_print(sc);

	audio_attach_mi(&uaudio_hw_if, sc, arg->cookie, &sc->dev);
}

int
uaudio_detach(struct device *self, int flags)
{
	struct uaudio_softc *sc = (struct uaudio_softc *)self;
	struct uaudio_unit *unit;
	struct uaudio_params *params;
	struct uaudio_alt *alt;
	struct uaudio_name *name;
	struct uaudio_mixent *mixent;
	int rv;

	rv = config_detach_children(self, flags);

	usbd_ref_wait(sc->udev);

	while ((alt = sc->alts) != NULL) {
		sc->alts = alt->next;
		free(alt, M_USBDEV, sizeof(struct uaudio_alt));
	}

	while ((params = sc->params_list) != NULL) {
		sc->params_list = params->next;
		free(params, M_USBDEV, sizeof(struct uaudio_params));
	}

	while ((unit = sc->unit_list) != NULL) {
		sc->unit_list = unit->unit_next;
		while ((mixent = unit->mixent_list) != NULL) {
			unit->mixent_list = mixent->next;
			uaudio_ranges_clear(&mixent->ranges);
			free(mixent, M_USBDEV, sizeof(struct uaudio_mixent));
		}
		uaudio_ranges_clear(&unit->rates);
		free(unit, M_USBDEV, sizeof(struct uaudio_unit));
	}

	while ((name = sc->names)) {
		sc->names = name->next;
		free(name, M_USBDEV, sizeof(struct uaudio_name));
	}

	return rv;
}

int
uaudio_open(void *self, int flags)
{
	struct uaudio_softc *sc = self;
	struct uaudio_params *p;

	if (usbd_is_dying(sc->udev))
		return EIO;

	usbd_ref_incr(sc->udev);

	flags &= (FREAD | FWRITE);

	for (p = sc->params_list; p != NULL; p = p->next) {
		switch (flags) {
		case FWRITE:
			if (!p->palt)
				break;
			sc->mode = AUMODE_PLAY;
			return 0;
		case FREAD:
			if (!p->ralt)
				break;
			sc->mode = AUMODE_RECORD;
			return 0;
		case FREAD | FWRITE:
			if (!(p->ralt && p->palt))
				break;
			sc->mode = AUMODE_RECORD | AUMODE_PLAY;
			return 0;
		}
	}

	usbd_ref_decr(sc->udev);
	return ENXIO;
}

void
uaudio_close(void *self)
{
	struct uaudio_softc *sc = self;

	sc->mode = 0;
	usbd_ref_decr(sc->udev);
}

int
uaudio_set_params(void *self, int setmode, int usemode,
    struct audio_params *ap, struct audio_params *ar)
{
	struct uaudio_softc *sc = (struct uaudio_softc *)self;
	struct uaudio_params *p, *best_mode, *best_rate, *best_nch;
	int rate, rateindex;

#ifdef DIAGNOSTIC
	if (setmode != usemode || setmode != sc->mode) {
		printf("%s: bad call to uaudio_set_params()\n", DEVNAME(sc));
		return EINVAL;
	}
	if (sc->mode == 0) {
		printf("%s: uaudio_set_params(): not open\n", DEVNAME(sc));
		return EINVAL;
	}
#endif
	/*
	 * audio(4) layer requests equal play and record rates
	 */
	rate = (sc->mode & AUMODE_PLAY) ? ap->sample_rate : ar->sample_rate;
	rateindex = uaudio_rates_indexof(~0, rate);

	DPRINTF("%s: rate %d -> %d (index %d)\n", __func__,
	    rate, uaudio_rates[rateindex], rateindex);

	best_mode = best_rate = best_nch = NULL;

	for (p = sc->params_list; p != NULL; p = p->next) {

		/* test if params match the requested mode */
		if (sc->mode & AUMODE_PLAY) {
			if (p->palt == NULL)
				continue;
		}
		if (sc->mode & AUMODE_RECORD) {
			if (p->ralt == NULL)
				continue;
		}
		if (best_mode == NULL)
			best_mode = p;

		/* test if params match the requested rate */
		if ((uaudio_getrates(sc, p) & (1 << rateindex)) == 0)
			continue;
		if (best_rate == NULL)
			best_rate = p;

		/* test if params match the requested channel counts */
		if (sc->mode & AUMODE_PLAY) {
			if (p->palt->nch != ap->channels)
				continue;
		}
		if (sc->mode & AUMODE_RECORD) {
			if (p->ralt->nch != ar->channels)
				continue;
		}
		if (best_nch == NULL)
			best_nch = p;

		/* test if params match the requested precision */
		if (sc->mode & AUMODE_PLAY) {
			if (p->palt->bits != ap->precision)
				continue;
		}
		if (sc->mode & AUMODE_RECORD) {
			if (p->ralt->bits != ar->precision)
				continue;
		}

		/* everything matched, we're done */
		break;
	}

	if (p == NULL) {
		if (best_nch)
			p = best_nch;
		else if (best_rate)
			p = best_rate;
		else if (best_mode)
			p = best_mode;
		else
			return ENOTTY;
	}

	/*
	 * Recalculate rate index, because the chosen parameters
	 * may not support the requested one
	 */
	rateindex = uaudio_rates_indexof(uaudio_getrates(sc, p), rate);
	if (rateindex < 0)
		return ENOTTY;

	sc->params = p;
	sc->rate = uaudio_rates[rateindex];

	DPRINTF("%s: rate = %u\n", __func__, sc->rate);

	if (sc->mode & AUMODE_PLAY) {
		ap->sample_rate = sc->rate;
		ap->precision = p->palt->bits;
		ap->encoding = AUDIO_ENCODING_SLINEAR_LE;
		ap->bps = p->palt->bps;
		ap->msb = 1;
		ap->channels = p->palt->nch;
	}
	if (sc->mode & AUMODE_RECORD) {
		ar->sample_rate = sc->rate;
		ar->precision = p->ralt->bits;
		ar->encoding = AUDIO_ENCODING_SLINEAR_LE;
		ar->bps = p->ralt->bps;
		ar->msb = 1;
		ar->channels = p->ralt->nch;
	}

	return 0;
}

unsigned int
uaudio_set_blksz(void *self, int mode,
    struct audio_params *p, struct audio_params *r, unsigned int blksz)
{
	struct uaudio_softc *sc = self;
	unsigned int fps, fps_min;
	unsigned int blksz_max, blksz_min;

	/*
	 * minimum block size is two transfers, see uaudio_stream_open()
	 */
	fps_min = sc->ufps;
	if (mode & AUMODE_PLAY) {
		fps = sc->params->palt->fps;
		if (fps_min > fps)
			fps_min = fps;
	}
	if (mode & AUMODE_RECORD) {
		fps = sc->params->ralt->fps;
		if (fps_min > fps)
			fps_min = fps;
	}
	blksz_min = (sc->rate * 2 + fps_min - 1) / fps_min;

	/*
	 * max block size is only limited by the number of frames the
	 * host can schedule
	 */
	blksz_max = sc->rate * (sc->host_nframes / UAUDIO_NXFERS_MIN) /
	    sc->ufps * 85 / 100;

	if (blksz > blksz_max)
		blksz = blksz_max;
	else if (blksz < blksz_min)
		blksz = blksz_min;

	return blksz;
}

int
uaudio_trigger_output(void *self, void *start, void *end, int blksz,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct uaudio_softc *sc = self;
	int err;

	err = uaudio_stream_open(sc,
	    AUMODE_PLAY, start, end, blksz, intr, arg);
	if (err)
		return err;

	sc->trigger_mode |= AUMODE_PLAY;
	uaudio_trigger(sc);
	return 0;
}

int
uaudio_trigger_input(void *self, void *start, void *end, int blksz,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct uaudio_softc *sc = self;
	int err;

	err = uaudio_stream_open(sc,
	    AUMODE_RECORD, start, end, blksz, intr, arg);
	if (err)
		return err;

	sc->trigger_mode |= AUMODE_RECORD;
	uaudio_trigger(sc);
	return 0;
}

void
uaudio_copy_output(void *self, size_t todo)
{
	struct uaudio_softc *sc = (struct uaudio_softc *)self;
	int s;

	s = splusb();
	sc->copy_todo += todo;

#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 3) {
		printf("%s: copy_todo -> %zd (+%zd)\n", __func__,
		    sc->copy_todo, todo);
	}
#endif

	if (sc->mode == sc->trigger_mode)
		uaudio_pdata_copy(sc);
	splx(s);
}

void
uaudio_underrun(void *self)
{
	struct uaudio_softc *sc = (struct uaudio_softc *)self;
	struct uaudio_stream *s = &sc->pstream;

	sc->copy_todo += s->ring_blksz;

#ifdef UAUDIO_DEBUG
	if (uaudio_debug >= 3)
		printf("%s: copy_todo -> %zd\n", __func__, sc->copy_todo);
#endif

	/* copy data (actually silence) produced by the audio(4) layer */
	uaudio_pdata_copy(sc);
}

int
uaudio_halt_output(void *self)
{
	struct uaudio_softc *sc = (struct uaudio_softc *)self;

	uaudio_stream_close(sc, AUMODE_PLAY);
	sc->trigger_mode &= ~AUMODE_PLAY;
	sc->copy_todo = 0;
	return 0;
}

int
uaudio_halt_input(void *self)
{
	struct uaudio_softc *sc = (struct uaudio_softc *)self;

	uaudio_stream_close(sc, AUMODE_RECORD);
	sc->trigger_mode &= ~AUMODE_RECORD;
	return 0;
}

int
uaudio_get_port_do(struct uaudio_softc *sc, struct mixer_ctrl *ctl)
{
	struct uaudio_unit *u;
	struct uaudio_mixent *m;
	unsigned char req_buf[4];
	struct uaudio_blob p;
	int i, nch, val, req_num;

	if (!uaudio_mixer_byindex(sc, ctl->dev, &u, &m))
		return ENOENT;

	switch (sc->version) {
	case UAUDIO_V1:
		req_num = UAUDIO_V1_REQ_GET_CUR;
		break;
	case UAUDIO_V2:
		req_num = UAUDIO_V2_REQ_CUR;
	}

	switch (m->type) {
	case UAUDIO_MIX_SW:
		p.rptr = p.wptr = req_buf;
		if (!uaudio_req(sc,
			UT_READ_CLASS_INTERFACE,
			req_num,
			m->req_sel,
			m->chan < 0 ? 0 : m->chan,
			sc->ctl_ifnum,
			u->id,
			req_buf,
			1))
			return EIO;
		p.wptr++;
		if (!uaudio_getnum(&p, 1, &val))
			return EIO;
		ctl->un.ord = !!val;
		break;
	case UAUDIO_MIX_NUM:
		nch = uaudio_mixer_nchan(m, NULL);
		ctl->un.value.num_channels = nch;
		for (i = 0; i < nch; i++) {
			p.rptr = p.wptr = req_buf;
			if (!uaudio_req(sc,
				UT_READ_CLASS_INTERFACE,
				req_num,
				m->req_sel,
				m->chan < 0 ? 0 : i + 1,
				sc->ctl_ifnum,
				u->id,
				req_buf,
				2))
				return EIO;
			p.wptr += 2;
			if (!uaudio_getnum(&p, 2, &val))
				return EIO;
			ctl->un.value.level[i] =
			    uaudio_ranges_decode(&m->ranges,
				uaudio_sign_expand(val, 2));
			m = m->next;
		}
		break;
	case UAUDIO_MIX_ENUM:
		/* XXX: not used yet */
		break;
	}
	return 0;
}

int
uaudio_set_port_do(struct uaudio_softc *sc, struct mixer_ctrl *ctl)
{
	struct uaudio_unit *u;
	struct uaudio_mixent *m;
	unsigned char req_buf[4];
	unsigned int val;
	int i, nch;

	if (!uaudio_mixer_byindex(sc, ctl->dev, &u, &m))
		return ENOENT;

	switch (m->type) {
	case UAUDIO_MIX_SW:
		if (ctl->un.ord < 0 || ctl->un.ord > 1)
			return EINVAL;
		req_buf[0] = ctl->un.ord;
		if (!uaudio_req(sc,
			UT_WRITE_CLASS_INTERFACE,
			UAUDIO_V1_REQ_SET_CUR,
			m->req_sel,
			m->chan < 0 ? 0 : m->chan,
			sc->ctl_ifnum,
			u->id,
			req_buf,
			1))
			return EIO;
		break;
	case UAUDIO_MIX_NUM:
		nch = uaudio_mixer_nchan(m, NULL);
		ctl->un.value.num_channels = nch;
		for (i = 0; i < nch; i++) {
			val = uaudio_ranges_encode(&m->ranges,
			    ctl->un.value.level[i]);
			DPRINTF("%s: ch %d, ctl %d, num val %d\n", __func__,
			    i, ctl->un.value.level[i], val);
			req_buf[0] = val;
			req_buf[1] = val >> 8;
			if (!uaudio_req(sc,
				UT_WRITE_CLASS_INTERFACE,
				UAUDIO_V1_REQ_SET_CUR,
				m->req_sel,
				m->chan < 0 ? 0 : i + 1,
				sc->ctl_ifnum,
				u->id,
				req_buf,
				2))
				return EIO;
			m = m->next;
		}
		break;
	case UAUDIO_MIX_ENUM:
		/* XXX: not used yet */
		break;
	}
	return 0;
}

int
uaudio_query_devinfo_do(struct uaudio_softc *sc, struct mixer_devinfo *devinfo)
{
	struct uaudio_unit *u;
	struct uaudio_mixent *m;

	devinfo->next = -1;
	devinfo->prev = -1;
	switch (devinfo->index) {
	case UAUDIO_CLASS_IN:
		strlcpy(devinfo->label.name, AudioCinputs, MAX_AUDIO_DEV_LEN);
		devinfo->type = AUDIO_MIXER_CLASS;
		devinfo->mixer_class = -1;
		return 0;
	case UAUDIO_CLASS_OUT:
		strlcpy(devinfo->label.name, AudioCoutputs, MAX_AUDIO_DEV_LEN);
		devinfo->type = AUDIO_MIXER_CLASS;
		devinfo->mixer_class = -1;
		return 0;
	}

	/*
	 * find the unit & mixent structure for the given index
	 */
	if (!uaudio_mixer_byindex(sc, devinfo->index, &u, &m))
		return ENOENT;

	if (strcmp(m->fname, "level") == 0) {
		/*
		 * mixer(4) interface doesn't give a names to level
		 * controls
		 */
		strlcpy(devinfo->label.name, u->name, MAX_AUDIO_DEV_LEN);
	} else {
		if (m->chan == -1) {
			snprintf(devinfo->label.name, MAX_AUDIO_DEV_LEN,
			    "%s_%s", u->name, m->fname);
		} else {
			snprintf(devinfo->label.name, MAX_AUDIO_DEV_LEN,
			    "%s_%s%u", u->name, m->fname, m->chan);
		}
	}

	devinfo->mixer_class = u->mixer_class;
	switch (m->type) {
	case UAUDIO_MIX_SW:
		devinfo->type = AUDIO_MIXER_ENUM;
		devinfo->un.e.num_mem = 2;
		devinfo->un.e.member[0].ord = 0;
		strlcpy(devinfo->un.e.member[0].label.name, "off",
		    MAX_AUDIO_DEV_LEN);
		devinfo->un.e.member[1].ord = 1;
		strlcpy(devinfo->un.e.member[1].label.name, "on",
		    MAX_AUDIO_DEV_LEN);
		break;
	case UAUDIO_MIX_NUM:
		devinfo->type = AUDIO_MIXER_VALUE;
		devinfo->un.v.num_channels = uaudio_mixer_nchan(m, NULL);
		devinfo->un.v.delta = 1;
		break;
	case UAUDIO_MIX_ENUM:
		/* XXX: not used yet */
		devinfo->type = AUDIO_MIXER_ENUM;
		devinfo->un.e.num_mem = 0;
		break;
	}
	return 0;
}

int
uaudio_get_port(void *arg, struct mixer_ctrl *ctl)
{
	struct uaudio_softc *sc = arg;
	int rc;

	usbd_ref_incr(sc->udev);
	rc = uaudio_get_port_do(sc, ctl);
	usbd_ref_decr(sc->udev);
	return rc;
}

int
uaudio_set_port(void *arg, struct mixer_ctrl *ctl)
{
	struct uaudio_softc *sc = arg;
	int rc;

	usbd_ref_incr(sc->udev);
	rc = uaudio_set_port_do(sc, ctl);
	usbd_ref_decr(sc->udev);
	return rc;
}

int
uaudio_query_devinfo(void *arg, struct mixer_devinfo *devinfo)
{
	struct uaudio_softc *sc = arg;
	int rc;

	usbd_ref_incr(sc->udev);
	rc = uaudio_query_devinfo_do(sc, devinfo);
	usbd_ref_decr(sc->udev);
	return rc;
}
