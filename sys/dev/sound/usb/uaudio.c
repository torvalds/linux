/*	$NetBSD: uaudio.c,v 1.91 2004/11/05 17:46:14 kent Exp $	*/
/*	$FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * USB audio specs: http://www.usb.org/developers/devclass_docs/audio10.pdf
 *                  http://www.usb.org/developers/devclass_docs/frmts10.pdf
 *                  http://www.usb.org/developers/devclass_docs/termt10.pdf
 */

/*
 * Also merged:
 *  $NetBSD: uaudio.c,v 1.94 2005/01/15 15:19:53 kent Exp $
 *  $NetBSD: uaudio.c,v 1.95 2005/01/16 06:02:19 dsainty Exp $
 *  $NetBSD: uaudio.c,v 1.96 2005/01/16 12:46:00 kent Exp $
 *  $NetBSD: uaudio.c,v 1.97 2005/02/24 08:19:38 martin Exp $
 */

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include "usbdevs.h"
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usb_request.h>
#include <dev/usb/usb_process.h>

#define	USB_DEBUG_VAR uaudio_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/quirk/usb_quirk.h>

#include <sys/reboot.h>			/* for bootverbose */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/usb/uaudioreg.h>
#include <dev/sound/usb/uaudio.h>
#include <dev/sound/chip.h>
#include "feeder_if.h"

static int uaudio_default_rate = 0;		/* use rate list */
static int uaudio_default_bits = 32;
static int uaudio_default_channels = 0;		/* use default */
static int uaudio_buffer_ms = 8;

#ifdef USB_DEBUG
static int uaudio_debug;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uaudio, CTLFLAG_RW, 0, "USB uaudio");

SYSCTL_INT(_hw_usb_uaudio, OID_AUTO, debug, CTLFLAG_RWTUN,
    &uaudio_debug, 0, "uaudio debug level");
SYSCTL_INT(_hw_usb_uaudio, OID_AUTO, default_rate, CTLFLAG_RWTUN,
    &uaudio_default_rate, 0, "uaudio default sample rate");
SYSCTL_INT(_hw_usb_uaudio, OID_AUTO, default_bits, CTLFLAG_RWTUN,
    &uaudio_default_bits, 0, "uaudio default sample bits");
SYSCTL_INT(_hw_usb_uaudio, OID_AUTO, default_channels, CTLFLAG_RWTUN,
    &uaudio_default_channels, 0, "uaudio default sample channels");

static int
uaudio_buffer_ms_sysctl(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = uaudio_buffer_ms;
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err != 0 || req->newptr == NULL || val == uaudio_buffer_ms)
		return (err);

	if (val > 8)
		val = 8;
	else if (val < 2)
		val = 2;

	uaudio_buffer_ms = val;

	return (0);
}
SYSCTL_PROC(_hw_usb_uaudio, OID_AUTO, buffer_ms, CTLTYPE_INT | CTLFLAG_RWTUN,
    0, sizeof(int), uaudio_buffer_ms_sysctl, "I",
    "uaudio buffering delay from 2ms to 8ms");
#else
#define	uaudio_debug 0
#endif

#define	UAUDIO_NFRAMES		64	/* must be factor of 8 due HS-USB */
#define	UAUDIO_NCHANBUFS	2	/* number of outstanding request */
#define	UAUDIO_RECURSE_LIMIT	255	/* rounds */
#define	UAUDIO_CHANNELS_MAX	MIN(64, AFMT_CHANNEL_MAX)
#define	UAUDIO_MATRIX_MAX	8	/* channels */

#define	MAKE_WORD(h,l) (((h) << 8) | (l))
#define	BIT_TEST(bm,bno) (((bm)[(bno) / 8] >> (7 - ((bno) % 8))) & 1)
#define	UAUDIO_MAX_CHAN(x) (x)
#define	MIX(sc) ((sc)->sc_mixer_node)

union uaudio_asid {
	const struct usb_audio_streaming_interface_descriptor *v1;
	const struct usb_audio20_streaming_interface_descriptor *v2;
};

union uaudio_asf1d {
	const struct usb_audio_streaming_type1_descriptor *v1;
	const struct usb_audio20_streaming_type1_descriptor *v2;
};

union uaudio_sed {
	const struct usb_audio_streaming_endpoint_descriptor *v1;
	const struct usb_audio20_streaming_endpoint_descriptor *v2;
};

struct uaudio_mixer_node {
	const char *name;

	int32_t	minval;
	int32_t	maxval;
#define	MIX_MAX_CHAN 16
	int32_t	wValue[MIX_MAX_CHAN];	/* using nchan */
	uint32_t mul;
	uint32_t ctl;

	int wData[MIX_MAX_CHAN];	/* using nchan */
	uint16_t wIndex;

	uint8_t	update[(MIX_MAX_CHAN + 7) / 8];
	uint8_t	nchan;
	uint8_t	type;
#define	MIX_ON_OFF	1
#define	MIX_SIGNED_16	2
#define	MIX_UNSIGNED_16	3
#define	MIX_SIGNED_8	4
#define	MIX_SELECTOR	5
#define	MIX_UNKNOWN     6
#define	MIX_SIZE(n) ((((n) == MIX_SIGNED_16) || \
		      ((n) == MIX_UNSIGNED_16)) ? 2 : 1)
#define	MIX_UNSIGNED(n) ((n) == MIX_UNSIGNED_16)

#define	MAX_SELECTOR_INPUT_PIN 256
	uint8_t	slctrtype[MAX_SELECTOR_INPUT_PIN];
	uint8_t	class;
	uint8_t val_default;

	uint8_t desc[64];

	struct uaudio_mixer_node *next;
};

struct uaudio_configure_msg {
	struct usb_proc_msg hdr;
	struct uaudio_softc *sc;
};

#define	CHAN_MAX_ALT 24

struct uaudio_chan_alt {
	union uaudio_asf1d p_asf1d;
	union uaudio_sed p_sed;
	const usb_endpoint_descriptor_audio_t *p_ed1;
	const struct uaudio_format *p_fmt;
	const struct usb_config *usb_cfg;
	uint32_t sample_rate;	/* in Hz */
	uint16_t sample_size;
	uint8_t	iface_index;
	uint8_t	iface_alt_index;
	uint8_t channels;
};

struct uaudio_chan {
	struct pcmchan_caps pcm_cap;	/* capabilities */
	struct uaudio_chan_alt usb_alt[CHAN_MAX_ALT];
	struct snd_dbuf *pcm_buf;
	struct mtx *pcm_mtx;		/* lock protecting this structure */
	struct uaudio_softc *priv_sc;
	struct pcm_channel *pcm_ch;
	struct usb_xfer *xfer[UAUDIO_NCHANBUFS + 1];

	uint8_t *buf;			/* pointer to buffer */
	uint8_t *start;			/* upper layer buffer start */
	uint8_t *end;			/* upper layer buffer end */
	uint8_t *cur;			/* current position in upper layer
					 * buffer */

	uint32_t intr_frames;		/* in units */
	uint32_t frames_per_second;
	uint32_t sample_rem;
	uint32_t sample_curr;
	uint32_t max_buf;
	int32_t jitter_rem;
	int32_t jitter_curr;

	int feedback_rate;

	uint32_t pcm_format[2];

	uint16_t bytes_per_frame[2];

	uint32_t intr_counter;
	uint32_t running;
	uint32_t num_alt;
	uint32_t cur_alt;
	uint32_t set_alt;
	uint32_t operation;
#define	CHAN_OP_NONE 0
#define	CHAN_OP_START 1
#define	CHAN_OP_STOP 2
#define	CHAN_OP_DRAIN 3
};

#define	UMIDI_EMB_JACK_MAX   16		/* units */
#define	UMIDI_TX_FRAMES	   256		/* units */
#define	UMIDI_TX_BUFFER    (UMIDI_TX_FRAMES * 4)	/* bytes */

enum {
	UMIDI_TX_TRANSFER,
	UMIDI_RX_TRANSFER,
	UMIDI_N_TRANSFER,
};

struct umidi_sub_chan {
	struct usb_fifo_sc fifo;
	uint8_t *temp_cmd;
	uint8_t	temp_0[4];
	uint8_t	temp_1[4];
	uint8_t	state;
#define	UMIDI_ST_UNKNOWN   0		/* scan for command */
#define	UMIDI_ST_1PARAM    1
#define	UMIDI_ST_2PARAM_1  2
#define	UMIDI_ST_2PARAM_2  3
#define	UMIDI_ST_SYSEX_0   4
#define	UMIDI_ST_SYSEX_1   5
#define	UMIDI_ST_SYSEX_2   6

	uint8_t	read_open:1;
	uint8_t	write_open:1;
	uint8_t	unused:6;
};

struct umidi_chan {

	struct umidi_sub_chan sub[UMIDI_EMB_JACK_MAX];
	struct mtx mtx;

	struct usb_xfer *xfer[UMIDI_N_TRANSFER];

	uint8_t	iface_index;
	uint8_t	iface_alt_index;

	uint8_t	read_open_refcount;
	uint8_t	write_open_refcount;

	uint8_t	curr_cable;
	uint8_t	max_emb_jack;
	uint8_t	valid;
	uint8_t single_command;
};

struct uaudio_search_result {
	uint8_t	bit_input[(256 + 7) / 8];
	uint8_t	bit_output[(256 + 7) / 8];
	uint8_t	recurse_level;
	uint8_t	id_max;
	uint8_t is_input;
};

enum {
	UAUDIO_HID_RX_TRANSFER,
	UAUDIO_HID_N_TRANSFER,
};

struct uaudio_hid {
	struct usb_xfer *xfer[UAUDIO_HID_N_TRANSFER];
	struct hid_location volume_up_loc;
	struct hid_location volume_down_loc;
	struct hid_location mute_loc;
	uint32_t flags;
#define	UAUDIO_HID_VALID		0x0001
#define	UAUDIO_HID_HAS_ID		0x0002
#define	UAUDIO_HID_HAS_VOLUME_UP	0x0004
#define	UAUDIO_HID_HAS_VOLUME_DOWN	0x0008
#define	UAUDIO_HID_HAS_MUTE		0x0010
	uint8_t iface_index;
	uint8_t volume_up_id;
	uint8_t volume_down_id;
	uint8_t mute_id;
};

#define	UAUDIO_SPDIF_OUT	0x01	/* Enable S/PDIF output */
#define	UAUDIO_SPDIF_OUT_48K	0x02	/* Out sample rate = 48K */
#define	UAUDIO_SPDIF_OUT_96K	0x04	/* Out sample rate = 96K */
#define	UAUDIO_SPDIF_IN_MIX	0x10	/* Input mix enable */

struct uaudio_softc {
	struct sbuf sc_sndstat;
	struct sndcard_func sc_sndcard_func;
	struct uaudio_chan sc_rec_chan;
	struct uaudio_chan sc_play_chan;
	struct umidi_chan sc_midi_chan;
	struct uaudio_hid sc_hid;
	struct uaudio_search_result sc_mixer_clocks;
	struct uaudio_mixer_node sc_mixer_node;
	struct uaudio_configure_msg sc_config_msg[2];

	struct mtx *sc_mixer_lock;
	struct snd_mixer *sc_mixer_dev;
	struct usb_device *sc_udev;
	struct usb_xfer *sc_mixer_xfer[1];
	struct uaudio_mixer_node *sc_mixer_root;
	struct uaudio_mixer_node *sc_mixer_curr;
	int     (*sc_set_spdif_fn) (struct uaudio_softc *, int);

	uint32_t sc_mix_info;
	uint32_t sc_recsrc_info;

	uint16_t sc_audio_rev;
	uint16_t sc_mixer_count;

	uint8_t	sc_sndstat_valid;
	uint8_t	sc_mixer_iface_index;
	uint8_t	sc_mixer_iface_no;
	uint8_t	sc_mixer_chan;
	uint8_t	sc_pcm_registered:1;
	uint8_t	sc_mixer_init:1;
	uint8_t	sc_uq_audio_swap_lr:1;
	uint8_t	sc_uq_au_inp_async:1;
	uint8_t	sc_uq_au_no_xu:1;
	uint8_t	sc_uq_bad_adc:1;
	uint8_t	sc_uq_au_vendor_class:1;
	uint8_t	sc_pcm_bitperfect:1;
};

struct uaudio_terminal_node {
	union {
		const struct usb_descriptor *desc;
		const struct usb_audio_input_terminal *it_v1;
		const struct usb_audio_output_terminal *ot_v1;
		const struct usb_audio_mixer_unit_0 *mu_v1;
		const struct usb_audio_selector_unit *su_v1;
		const struct usb_audio_feature_unit *fu_v1;
		const struct usb_audio_processing_unit_0 *pu_v1;
		const struct usb_audio_extension_unit_0 *eu_v1;
		const struct usb_audio20_clock_source_unit *csrc_v2;
		const struct usb_audio20_clock_selector_unit_0 *csel_v2;
		const struct usb_audio20_clock_multiplier_unit *cmul_v2;
		const struct usb_audio20_input_terminal *it_v2;
		const struct usb_audio20_output_terminal *ot_v2;
		const struct usb_audio20_mixer_unit_0 *mu_v2;
		const struct usb_audio20_selector_unit *su_v2;
		const struct usb_audio20_feature_unit *fu_v2;
		const struct usb_audio20_sample_rate_unit *ru_v2;
		const struct usb_audio20_processing_unit_0 *pu_v2;
		const struct usb_audio20_extension_unit_0 *eu_v2;
		const struct usb_audio20_effect_unit *ef_v2;
	}	u;
	struct uaudio_search_result usr;
	struct uaudio_terminal_node *root;
};

struct uaudio_format {
	uint16_t wFormat;
	uint8_t	bPrecision;
	uint32_t freebsd_fmt;
	const char *description;
};

static const struct uaudio_format uaudio10_formats[] = {

	{UA_FMT_PCM8, 8, AFMT_U8, "8-bit U-LE PCM"},
	{UA_FMT_PCM8, 16, AFMT_U16_LE, "16-bit U-LE PCM"},
	{UA_FMT_PCM8, 24, AFMT_U24_LE, "24-bit U-LE PCM"},
	{UA_FMT_PCM8, 32, AFMT_U32_LE, "32-bit U-LE PCM"},

	{UA_FMT_PCM, 8, AFMT_S8, "8-bit S-LE PCM"},
	{UA_FMT_PCM, 16, AFMT_S16_LE, "16-bit S-LE PCM"},
	{UA_FMT_PCM, 24, AFMT_S24_LE, "24-bit S-LE PCM"},
	{UA_FMT_PCM, 32, AFMT_S32_LE, "32-bit S-LE PCM"},

	{UA_FMT_ALAW, 8, AFMT_A_LAW, "8-bit A-Law"},
	{UA_FMT_MULAW, 8, AFMT_MU_LAW, "8-bit mu-Law"},

	{0, 0, 0, NULL}
};

static const struct uaudio_format uaudio20_formats[] = {

	{UA20_FMT_PCM, 8, AFMT_S8, "8-bit S-LE PCM"},
	{UA20_FMT_PCM, 16, AFMT_S16_LE, "16-bit S-LE PCM"},
	{UA20_FMT_PCM, 24, AFMT_S24_LE, "24-bit S-LE PCM"},
	{UA20_FMT_PCM, 32, AFMT_S32_LE, "32-bit S-LE PCM"},

	{UA20_FMT_PCM8, 8, AFMT_U8, "8-bit U-LE PCM"},
	{UA20_FMT_PCM8, 16, AFMT_U16_LE, "16-bit U-LE PCM"},
	{UA20_FMT_PCM8, 24, AFMT_U24_LE, "24-bit U-LE PCM"},
	{UA20_FMT_PCM8, 32, AFMT_U32_LE, "32-bit U-LE PCM"},

	{UA20_FMT_ALAW, 8, AFMT_A_LAW, "8-bit A-Law"},
	{UA20_FMT_MULAW, 8, AFMT_MU_LAW, "8-bit mu-Law"},

	{0, 0, 0, NULL}
};

#define	UAC_OUTPUT	0
#define	UAC_INPUT	1
#define	UAC_EQUAL	2
#define	UAC_RECORD	3
#define	UAC_NCLASSES	4

#ifdef USB_DEBUG
static const char *uac_names[] = {
	"outputs", "inputs", "equalization", "record"
};

#endif

/* prototypes */

static device_probe_t uaudio_probe;
static device_attach_t uaudio_attach;
static device_detach_t uaudio_detach;

static usb_callback_t uaudio_chan_play_callback;
static usb_callback_t uaudio_chan_play_sync_callback;
static usb_callback_t uaudio_chan_record_callback;
static usb_callback_t uaudio_chan_record_sync_callback;
static usb_callback_t uaudio_mixer_write_cfg_callback;
static usb_callback_t umidi_bulk_read_callback;
static usb_callback_t umidi_bulk_write_callback;
static usb_callback_t uaudio_hid_rx_callback;

static usb_proc_callback_t uaudio_configure_msg;

/* ==== USB mixer ==== */

static int uaudio_mixer_sysctl_handler(SYSCTL_HANDLER_ARGS);
static void uaudio_mixer_ctl_free(struct uaudio_softc *);
static void uaudio_mixer_register_sysctl(struct uaudio_softc *, device_t);
static void uaudio_mixer_reload_all(struct uaudio_softc *);
static void uaudio_mixer_controls_create_ftu(struct uaudio_softc *);

/* ==== USB audio v1.0 ==== */

static void	uaudio_mixer_add_mixer(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static void	uaudio_mixer_add_selector(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static uint32_t	uaudio_mixer_feature_get_bmaControls(
		    const struct usb_audio_feature_unit *, uint8_t);
static void	uaudio_mixer_add_feature(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static void	uaudio_mixer_add_processing_updown(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static void	uaudio_mixer_add_processing(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static void	uaudio_mixer_add_extension(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static struct	usb_audio_cluster uaudio_mixer_get_cluster(uint8_t,
		    const struct uaudio_terminal_node *);
static uint16_t	uaudio_mixer_determine_class(const struct uaudio_terminal_node *,
		    struct uaudio_mixer_node *);
static uint16_t	uaudio_mixer_feature_name(const struct uaudio_terminal_node *,
		    struct uaudio_mixer_node *);
static void	uaudio_mixer_find_inputs_sub(struct uaudio_terminal_node *,
		    const uint8_t *, uint8_t, struct uaudio_search_result *);
static const void *uaudio_mixer_verify_desc(const void *, uint32_t);
static usb_error_t uaudio_set_speed(struct usb_device *, uint8_t, uint32_t);
static int	uaudio_mixer_get(struct usb_device *, uint16_t, uint8_t,
		    struct uaudio_mixer_node *);

/* ==== USB audio v2.0 ==== */

static void	uaudio20_mixer_add_mixer(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static void	uaudio20_mixer_add_selector(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static void	uaudio20_mixer_add_feature(struct uaudio_softc *,
		    const struct uaudio_terminal_node *, int);
static struct	usb_audio20_cluster uaudio20_mixer_get_cluster(uint8_t,
		    const struct uaudio_terminal_node *);
static uint16_t	uaudio20_mixer_determine_class(const struct uaudio_terminal_node *,
		    struct uaudio_mixer_node *);
static uint16_t	uaudio20_mixer_feature_name(const struct uaudio_terminal_node *,
		    struct uaudio_mixer_node *);
static void	uaudio20_mixer_find_inputs_sub(struct uaudio_terminal_node *,
		    const uint8_t *, uint8_t, struct uaudio_search_result *);
static const void *uaudio20_mixer_verify_desc(const void *, uint32_t);
static usb_error_t uaudio20_set_speed(struct usb_device *, uint8_t,
		    uint8_t, uint32_t);

/* USB audio v1.0 and v2.0 */

static void	uaudio_chan_fill_info_sub(struct uaudio_softc *,
		    struct usb_device *, uint32_t, uint8_t, uint8_t);
static void	uaudio_chan_fill_info(struct uaudio_softc *,
		    struct usb_device *);
static void	uaudio_mixer_add_ctl_sub(struct uaudio_softc *,
		    struct uaudio_mixer_node *);
static void	uaudio_mixer_add_ctl(struct uaudio_softc *,
		    struct uaudio_mixer_node *);
static void	uaudio_mixer_fill_info(struct uaudio_softc *,
		    struct usb_device *, void *);
static void	uaudio_mixer_ctl_set(struct uaudio_softc *,
		    struct uaudio_mixer_node *, uint8_t, int32_t val);
static int	uaudio_mixer_signext(uint8_t, int);
static int	uaudio_mixer_bsd2value(struct uaudio_mixer_node *, int32_t val);
static void	uaudio_mixer_init(struct uaudio_softc *);
static const struct uaudio_terminal_node *uaudio_mixer_get_input(
		    const struct uaudio_terminal_node *, uint8_t);
static const struct uaudio_terminal_node *uaudio_mixer_get_output(
		    const struct uaudio_terminal_node *, uint8_t);
static void	uaudio_mixer_find_outputs_sub(struct uaudio_terminal_node *,
		    uint8_t, uint8_t, struct uaudio_search_result *);
static uint8_t	umidi_convert_to_usb(struct umidi_sub_chan *, uint8_t, uint8_t);
static struct	umidi_sub_chan *umidi_sub_by_fifo(struct usb_fifo *);
static void	umidi_start_read(struct usb_fifo *);
static void	umidi_stop_read(struct usb_fifo *);
static void	umidi_start_write(struct usb_fifo *);
static void	umidi_stop_write(struct usb_fifo *);
static int	umidi_open(struct usb_fifo *, int);
static int	umidi_ioctl(struct usb_fifo *, u_long cmd, void *, int);
static void	umidi_close(struct usb_fifo *, int);
static void	umidi_init(device_t dev);
static int	umidi_probe(device_t dev);
static int	umidi_detach(device_t dev);
static int	uaudio_hid_probe(struct uaudio_softc *sc,
		    struct usb_attach_arg *uaa);
static void	uaudio_hid_detach(struct uaudio_softc *sc);

#ifdef USB_DEBUG
static void	uaudio_chan_dump_ep_desc(
		    const usb_endpoint_descriptor_audio_t *);
#endif

static const struct usb_config
	uaudio_cfg_record[UAUDIO_NCHANBUFS + 1] = {
	[0] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 0,	/* use "wMaxPacketSize * frames" */
		.frames = UAUDIO_NFRAMES,
		.flags = {.short_xfer_ok = 1,},
		.callback = &uaudio_chan_record_callback,
	},

	[1] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 0,	/* use "wMaxPacketSize * frames" */
		.frames = UAUDIO_NFRAMES,
		.flags = {.short_xfer_ok = 1,},
		.callback = &uaudio_chan_record_callback,
	},

	[2] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = 0,	/* use "wMaxPacketSize * frames" */
		.frames = 1,
		.flags = {.no_pipe_ok = 1,.short_xfer_ok = 1,},
		.callback = &uaudio_chan_record_sync_callback,
	},
};

static const struct usb_config
	uaudio_cfg_play[UAUDIO_NCHANBUFS + 1] = {
	[0] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = 0,	/* use "wMaxPacketSize * frames" */
		.frames = UAUDIO_NFRAMES,
		.flags = {.short_xfer_ok = 1,},
		.callback = &uaudio_chan_play_callback,
	},

	[1] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = 0,	/* use "wMaxPacketSize * frames" */
		.frames = UAUDIO_NFRAMES,
		.flags = {.short_xfer_ok = 1,},
		.callback = &uaudio_chan_play_callback,
	},

	[2] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 0,	/* use "wMaxPacketSize * frames" */
		.frames = 1,
		.flags = {.no_pipe_ok = 1,.short_xfer_ok = 1,},
		.callback = &uaudio_chan_play_sync_callback,
	},
};

static const struct usb_config
	uaudio_mixer_config[1] = {
	[0] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = (sizeof(struct usb_device_request) + 4),
		.callback = &uaudio_mixer_write_cfg_callback,
		.timeout = 1000,	/* 1 second */
	},
};

static const
uint8_t	umidi_cmd_to_len[16] = {
	[0x0] = 0,			/* reserved */
	[0x1] = 0,			/* reserved */
	[0x2] = 2,			/* bytes */
	[0x3] = 3,			/* bytes */
	[0x4] = 3,			/* bytes */
	[0x5] = 1,			/* bytes */
	[0x6] = 2,			/* bytes */
	[0x7] = 3,			/* bytes */
	[0x8] = 3,			/* bytes */
	[0x9] = 3,			/* bytes */
	[0xA] = 3,			/* bytes */
	[0xB] = 3,			/* bytes */
	[0xC] = 2,			/* bytes */
	[0xD] = 2,			/* bytes */
	[0xE] = 3,			/* bytes */
	[0xF] = 1,			/* bytes */
};

static const struct usb_config
	umidi_config[UMIDI_N_TRANSFER] = {
	[UMIDI_TX_TRANSFER] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UMIDI_TX_BUFFER,
		.flags = {.no_pipe_ok = 1},
		.callback = &umidi_bulk_write_callback,
	},

	[UMIDI_RX_TRANSFER] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 4,	/* bytes */
		.flags = {.short_xfer_ok = 1,.proxy_buffer = 1,.no_pipe_ok = 1},
		.callback = &umidi_bulk_read_callback,
	},
};

static const struct usb_config
	uaudio_hid_config[UAUDIO_HID_N_TRANSFER] = {
	[UAUDIO_HID_RX_TRANSFER] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 0,	/* use wMaxPacketSize */
		.flags = {.short_xfer_ok = 1,},
		.callback = &uaudio_hid_rx_callback,
	},
};

static devclass_t uaudio_devclass;

static device_method_t uaudio_methods[] = {
	DEVMETHOD(device_probe, uaudio_probe),
	DEVMETHOD(device_attach, uaudio_attach),
	DEVMETHOD(device_detach, uaudio_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t uaudio_driver = {
	.name = "uaudio",
	.methods = uaudio_methods,
	.size = sizeof(struct uaudio_softc),
};

/* The following table is derived from Linux's quirks-table.h */ 
static const STRUCT_USB_HOST_ID uaudio_vendor_midi[] = {
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1000, 0) }, /* UX256 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1001, 0) }, /* MU1000 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1002, 0) }, /* MU2000 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1003, 0) }, /* MU500 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1004, 3) }, /* UW500 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1005, 0) }, /* MOTIF6 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1006, 0) }, /* MOTIF7 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1007, 0) }, /* MOTIF8 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1008, 0) }, /* UX96 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1009, 0) }, /* UX16 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x100a, 3) }, /* EOS BX */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x100c, 0) }, /* UC-MX */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x100d, 0) }, /* UC-KX */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x100e, 0) }, /* S08 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x100f, 0) }, /* CLP-150 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1010, 0) }, /* CLP-170 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1011, 0) }, /* P-250 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1012, 0) }, /* TYROS */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1013, 0) }, /* PF-500 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1014, 0) }, /* S90 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1015, 0) }, /* MOTIF-R */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1016, 0) }, /* MDP-5 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1017, 0) }, /* CVP-204 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1018, 0) }, /* CVP-206 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1019, 0) }, /* CVP-208 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x101a, 0) }, /* CVP-210 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x101b, 0) }, /* PSR-1100 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x101c, 0) }, /* PSR-2100 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x101d, 0) }, /* CLP-175 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x101e, 0) }, /* PSR-K1 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x101f, 0) }, /* EZ-J24 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1020, 0) }, /* EZ-250i */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1021, 0) }, /* MOTIF ES 6 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1022, 0) }, /* MOTIF ES 7 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1023, 0) }, /* MOTIF ES 8 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1024, 0) }, /* CVP-301 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1025, 0) }, /* CVP-303 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1026, 0) }, /* CVP-305 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1027, 0) }, /* CVP-307 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1028, 0) }, /* CVP-309 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1029, 0) }, /* CVP-309GP */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x102a, 0) }, /* PSR-1500 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x102b, 0) }, /* PSR-3000 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x102e, 0) }, /* ELS-01/01C */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1030, 0) }, /* PSR-295/293 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1031, 0) }, /* DGX-205/203 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1032, 0) }, /* DGX-305 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1033, 0) }, /* DGX-505 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1034, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1035, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1036, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1037, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1038, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1039, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x103a, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x103b, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x103c, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x103d, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x103e, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x103f, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1040, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1041, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1042, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1043, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1044, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1045, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x104e, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x104f, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1050, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1051, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1052, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1053, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1054, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1055, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1056, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1057, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1058, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1059, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x105a, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x105b, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x105c, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x105d, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x1503, 3) }, /* MOX6/MOX8 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x2000, 0) }, /* DGP-7 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x2001, 0) }, /* DGP-5 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x2002, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x2003, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5000, 0) }, /* CS1D */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5001, 0) }, /* DSP1D */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5002, 0) }, /* DME32 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5003, 0) }, /* DM2000 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5004, 0) }, /* 02R96 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5005, 0) }, /* ACU16-C */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5006, 0) }, /* NHB32-C */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5007, 0) }, /* DM1000 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5008, 0) }, /* 01V96 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x5009, 0) }, /* SPX2000 */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x500a, 0) }, /* PM5D */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x500b, 0) }, /* DME64N */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x500c, 0) }, /* DME24N */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x500d, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x500e, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x500f, 0) }, /* NULL */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x7000, 0) }, /* DTX */
	{ USB_VPI(USB_VENDOR_YAMAHA, 0x7010, 0) }, /* UB99 */
};

static const STRUCT_USB_HOST_ID __used uaudio_devs[] = {
	/* Generic USB audio class match */
	{USB_IFACE_CLASS(UICLASS_AUDIO),
	 USB_IFACE_SUBCLASS(UISUBCLASS_AUDIOCONTROL),},
	/* Generic USB MIDI class match */
	{USB_IFACE_CLASS(UICLASS_AUDIO),
	 USB_IFACE_SUBCLASS(UISUBCLASS_MIDISTREAM),},
};

static int
uaudio_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	/* lookup non-standard device(s) */

	if (usbd_lookup_id_by_uaa(uaudio_vendor_midi,
	    sizeof(uaudio_vendor_midi), uaa) == 0) {
		return (BUS_PROBE_SPECIFIC);
	}

	if (uaa->info.bInterfaceClass != UICLASS_AUDIO) {
		if (uaa->info.bInterfaceClass != UICLASS_VENDOR ||
		    usb_test_quirk(uaa, UQ_AU_VENDOR_CLASS) == 0)
			return (ENXIO);
	}

	/* check for AUDIO control interface */

	if (uaa->info.bInterfaceSubClass == UISUBCLASS_AUDIOCONTROL) {
		if (usb_test_quirk(uaa, UQ_BAD_AUDIO))
			return (ENXIO);
		else
			return (BUS_PROBE_GENERIC);
	}

	/* check for MIDI stream */

	if (uaa->info.bInterfaceSubClass == UISUBCLASS_MIDISTREAM) {
		if (usb_test_quirk(uaa, UQ_BAD_MIDI))
			return (ENXIO);
		else
			return (BUS_PROBE_GENERIC);
	}
	return (ENXIO);
}

/*
 * Set Cmedia CM6206 S/PDIF settings
 * Source: CM6206 Datasheet v2.3.
 */
static int
uaudio_set_spdif_cm6206(struct uaudio_softc *sc, int flags)
{
	uint8_t cmd[2][4] = {
		{0x20, 0x20, 0x00, 0},
		{0x20, 0x30, 0x02, 1}
	};
	int i;

	if (flags & UAUDIO_SPDIF_OUT)
		cmd[1][1] = 0x00;
	else
		cmd[1][1] = 0x02;

	if (flags & UAUDIO_SPDIF_OUT_96K)
		cmd[0][1] = 0x60;	/* 96K: 3'b110 */

	if (flags & UAUDIO_SPDIF_IN_MIX)
		cmd[1][1] = 0x03;	/* SPDIFMIX */

	for (i = 0; i < 2; i++) {
		if (usbd_req_set_report(sc->sc_udev, NULL,
		    cmd[i], sizeof(cmd[0]),
		    sc->sc_mixer_iface_index, UHID_OUTPUT_REPORT, 0) != 0) {
			return (ENXIO);
		}
	}
	return (0);
}

static int
uaudio_set_spdif_dummy(struct uaudio_softc *sc, int flags)
{
	return (0);
}

static int
uaudio_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct uaudio_softc *sc = device_get_softc(dev);
	struct usb_interface_descriptor *id;
	usb_error_t err;
	device_t child;

	sc->sc_play_chan.priv_sc = sc;
	sc->sc_rec_chan.priv_sc = sc;
	sc->sc_udev = uaa->device;
	sc->sc_mixer_iface_index = uaa->info.bIfaceIndex;
	sc->sc_mixer_iface_no = uaa->info.bIfaceNum;
	sc->sc_config_msg[0].hdr.pm_callback = &uaudio_configure_msg;
	sc->sc_config_msg[0].sc = sc;
	sc->sc_config_msg[1].hdr.pm_callback = &uaudio_configure_msg;
	sc->sc_config_msg[1].sc = sc;

	if (usb_test_quirk(uaa, UQ_AUDIO_SWAP_LR))
		sc->sc_uq_audio_swap_lr = 1;

	if (usb_test_quirk(uaa, UQ_AU_INP_ASYNC))
		sc->sc_uq_au_inp_async = 1;

	if (usb_test_quirk(uaa, UQ_AU_NO_XU))
		sc->sc_uq_au_no_xu = 1;

	if (usb_test_quirk(uaa, UQ_BAD_ADC))
		sc->sc_uq_bad_adc = 1;

	if (usb_test_quirk(uaa, UQ_AU_VENDOR_CLASS))
		sc->sc_uq_au_vendor_class = 1;

	/* set S/PDIF function */
	if (usb_test_quirk(uaa, UQ_AU_SET_SPDIF_CM6206))
		sc->sc_set_spdif_fn = uaudio_set_spdif_cm6206;
	else
		sc->sc_set_spdif_fn = uaudio_set_spdif_dummy;

	umidi_init(dev);

	device_set_usb_desc(dev);

	id = usbd_get_interface_descriptor(uaa->iface);

	/* must fill mixer info before channel info */
	uaudio_mixer_fill_info(sc, uaa->device, id);

	/* fill channel info */
	uaudio_chan_fill_info(sc, uaa->device);

	DPRINTF("audio rev %d.%02x\n",
	    sc->sc_audio_rev >> 8,
	    sc->sc_audio_rev & 0xff);

	if (sc->sc_mixer_count == 0) {
		if (uaa->info.idVendor == USB_VENDOR_MAUDIO &&
		    (uaa->info.idProduct == USB_PRODUCT_MAUDIO_FASTTRACKULTRA ||
		    uaa->info.idProduct == USB_PRODUCT_MAUDIO_FASTTRACKULTRA8R)) {
			DPRINTF("Generating mixer descriptors\n");
			uaudio_mixer_controls_create_ftu(sc);
		}
	}

	DPRINTF("%d mixer controls\n",
	    sc->sc_mixer_count);

	if (sc->sc_play_chan.num_alt > 0) {
		uint8_t x;

		/*
		 * Need to set a default alternate interface, else
		 * some USB audio devices might go into an infinte
		 * re-enumeration loop:
		 */
		err = usbd_set_alt_interface_index(sc->sc_udev,
		    sc->sc_play_chan.usb_alt[0].iface_index,
		    sc->sc_play_chan.usb_alt[0].iface_alt_index);
		if (err) {
			DPRINTF("setting of alternate index failed: %s!\n",
			    usbd_errstr(err));
		}
		for (x = 0; x != sc->sc_play_chan.num_alt; x++) {
			device_printf(dev, "Play: %d Hz, %d ch, %s format, "
			    "2x8ms buffer.\n",
			    sc->sc_play_chan.usb_alt[x].sample_rate,
			    sc->sc_play_chan.usb_alt[x].channels,
			    sc->sc_play_chan.usb_alt[x].p_fmt->description);
		}
	} else {
		device_printf(dev, "No playback.\n");
	}

	if (sc->sc_rec_chan.num_alt > 0) {
		uint8_t x;

		/*
		 * Need to set a default alternate interface, else
		 * some USB audio devices might go into an infinte
		 * re-enumeration loop:
		 */
		err = usbd_set_alt_interface_index(sc->sc_udev,
		    sc->sc_rec_chan.usb_alt[0].iface_index,
		    sc->sc_rec_chan.usb_alt[0].iface_alt_index);
		if (err) {
			DPRINTF("setting of alternate index failed: %s!\n",
			    usbd_errstr(err));
		}
		for (x = 0; x != sc->sc_rec_chan.num_alt; x++) {
			device_printf(dev, "Record: %d Hz, %d ch, %s format, "
			    "2x8ms buffer.\n",
			    sc->sc_rec_chan.usb_alt[x].sample_rate,
			    sc->sc_rec_chan.usb_alt[x].channels,
			    sc->sc_rec_chan.usb_alt[x].p_fmt->description);
		}
	} else {
		device_printf(dev, "No recording.\n");
	}

	if (sc->sc_midi_chan.valid == 0) {
		if (usbd_lookup_id_by_uaa(uaudio_vendor_midi,
		    sizeof(uaudio_vendor_midi), uaa) == 0) {
			sc->sc_midi_chan.iface_index =
			    (uint8_t)uaa->driver_info;
			sc->sc_midi_chan.iface_alt_index = 0;
			sc->sc_midi_chan.valid = 1;
		}
	}

	if (sc->sc_midi_chan.valid) {

		if (umidi_probe(dev)) {
			goto detach;
		}
		device_printf(dev, "MIDI sequencer.\n");
	} else {
		device_printf(dev, "No MIDI sequencer.\n");
	}

	DPRINTF("doing child attach\n");

	/* attach the children */

	sc->sc_sndcard_func.func = SCF_PCM;

	/*
	 * Only attach a PCM device if we have a playback, recording
	 * or mixer device present:
	 */
	if (sc->sc_play_chan.num_alt > 0 ||
	    sc->sc_rec_chan.num_alt > 0 ||
	    sc->sc_mix_info) {
		child = device_add_child(dev, "pcm", -1);

		if (child == NULL) {
			DPRINTF("out of memory\n");
			goto detach;
		}
		device_set_ivars(child, &sc->sc_sndcard_func);
	}

	if (bus_generic_attach(dev)) {
		DPRINTF("child attach failed\n");
		goto detach;
	}

	if (uaudio_hid_probe(sc, uaa) == 0) {
		device_printf(dev, "HID volume keys found.\n");
	} else {
		device_printf(dev, "No HID volume keys found.\n");
	}

	/* reload all mixer settings */
	uaudio_mixer_reload_all(sc);

	/* enable S/PDIF output, if any */
	if (sc->sc_set_spdif_fn(sc,
	    UAUDIO_SPDIF_OUT | UAUDIO_SPDIF_OUT_48K) != 0) {
		device_printf(dev, "Failed to enable S/PDIF at 48K\n");
	}
	return (0);			/* success */

detach:
	uaudio_detach(dev);
	return (ENXIO);
}

static void
uaudio_pcm_setflags(device_t dev, uint32_t flags)
{
	pcm_setflags(dev, pcm_getflags(dev) | flags);
}

int
uaudio_attach_sub(device_t dev, kobj_class_t mixer_class, kobj_class_t chan_class)
{
	struct uaudio_softc *sc = device_get_softc(device_get_parent(dev));
	char status[SND_STATUSLEN];

	uaudio_mixer_init(sc);

	if (sc->sc_uq_audio_swap_lr) {
		DPRINTF("hardware has swapped left and right\n");
		/* uaudio_pcm_setflags(dev, SD_F_PSWAPLR); */
	}
	if (!(sc->sc_mix_info & SOUND_MASK_PCM)) {

		DPRINTF("emulating master volume\n");

		/*
		 * Emulate missing pcm mixer controller
		 * through FEEDER_VOLUME
		 */
		uaudio_pcm_setflags(dev, SD_F_SOFTPCMVOL);
	}
	if (sc->sc_pcm_bitperfect) {
		DPRINTF("device needs bitperfect by default\n");
		uaudio_pcm_setflags(dev, SD_F_BITPERFECT);
	}
	if (mixer_init(dev, mixer_class, sc))
		goto detach;
	sc->sc_mixer_init = 1;

	mixer_hwvol_init(dev);

	snprintf(status, sizeof(status), "at ? %s", PCM_KLDSTRING(snd_uaudio));

	if (pcm_register(dev, sc,
	    (sc->sc_play_chan.num_alt > 0) ? 1 : 0,
	    (sc->sc_rec_chan.num_alt > 0) ? 1 : 0)) {
		goto detach;
	}

	uaudio_pcm_setflags(dev, SD_F_MPSAFE);
	sc->sc_pcm_registered = 1;

	if (sc->sc_play_chan.num_alt > 0) {
		pcm_addchan(dev, PCMDIR_PLAY, chan_class, sc);
	}
	if (sc->sc_rec_chan.num_alt > 0) {
		pcm_addchan(dev, PCMDIR_REC, chan_class, sc);
	}
	pcm_setstatus(dev, status);

	uaudio_mixer_register_sysctl(sc, dev);

	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "feedback_rate", CTLFLAG_RD, &sc->sc_play_chan.feedback_rate,
	    0, "Feedback sample rate in Hz");

	return (0);			/* success */

detach:
	uaudio_detach_sub(dev);
	return (ENXIO);
}

int
uaudio_detach_sub(device_t dev)
{
	struct uaudio_softc *sc = device_get_softc(device_get_parent(dev));
	int error = 0;

	/* disable S/PDIF output, if any */
	(void) sc->sc_set_spdif_fn(sc, 0);

repeat:
	if (sc->sc_pcm_registered) {
		error = pcm_unregister(dev);
	} else {
		if (sc->sc_mixer_init) {
			error = mixer_uninit(dev);
		}
	}

	if (error) {
		device_printf(dev, "Waiting for sound application to exit!\n");
		usb_pause_mtx(NULL, 2 * hz);
		goto repeat;		/* try again */
	}
	return (0);			/* success */
}

static int
uaudio_detach(device_t dev)
{
	struct uaudio_softc *sc = device_get_softc(dev);

	/*
	 * Stop USB transfers early so that any audio applications
	 * will time out and close opened /dev/dspX.Y device(s), if
	 * any.
	 */
	usb_proc_explore_lock(sc->sc_udev);
	sc->sc_play_chan.operation = CHAN_OP_DRAIN;
	sc->sc_rec_chan.operation = CHAN_OP_DRAIN;
	usb_proc_explore_mwait(sc->sc_udev,
	    &sc->sc_config_msg[0], &sc->sc_config_msg[1]);
	usb_proc_explore_unlock(sc->sc_udev);

	usbd_transfer_unsetup(sc->sc_play_chan.xfer, UAUDIO_NCHANBUFS + 1);
	usbd_transfer_unsetup(sc->sc_rec_chan.xfer, UAUDIO_NCHANBUFS + 1);

	uaudio_hid_detach(sc);

	if (bus_generic_detach(dev) != 0) {
		DPRINTF("detach failed!\n");
	}
	sbuf_delete(&sc->sc_sndstat);
	sc->sc_sndstat_valid = 0;

	umidi_detach(dev);

	/* free mixer data */

	uaudio_mixer_ctl_free(sc);

	return (0);
}

static uint32_t
uaudio_get_buffer_size(struct uaudio_chan *ch, uint8_t alt)
{
	struct uaudio_chan_alt *chan_alt = &ch->usb_alt[alt];
	/* We use 2 times 8ms of buffer */
	uint32_t buf_size = chan_alt->sample_size *
	    howmany(chan_alt->sample_rate * (UAUDIO_NFRAMES / 8), 1000);
	return (buf_size);
}

static void
uaudio_configure_msg_sub(struct uaudio_softc *sc,
    struct uaudio_chan *chan, int dir)
{
	struct uaudio_chan_alt *chan_alt;
	uint32_t frames;
	uint32_t buf_size;
	uint16_t fps;
	uint8_t set_alt;
	uint8_t fps_shift;
	uint8_t operation;
	usb_error_t err;

	if (chan->num_alt <= 0)
		return;

	DPRINTF("\n");

	usb_proc_explore_lock(sc->sc_udev);
	operation = chan->operation;
	chan->operation = CHAN_OP_NONE;
	usb_proc_explore_unlock(sc->sc_udev);

	mtx_lock(chan->pcm_mtx);
	if (chan->cur_alt != chan->set_alt)
		set_alt = chan->set_alt;
	else
		set_alt = CHAN_MAX_ALT;
	mtx_unlock(chan->pcm_mtx);

	if (set_alt >= chan->num_alt)
		goto done;

	chan_alt = chan->usb_alt + set_alt;

	usbd_transfer_unsetup(chan->xfer, UAUDIO_NCHANBUFS + 1);

	err = usbd_set_alt_interface_index(sc->sc_udev,
	    chan_alt->iface_index, chan_alt->iface_alt_index);
	if (err) {
		DPRINTF("setting of alternate index failed: %s!\n",
		    usbd_errstr(err));
		goto error;
	}

	/*
	 * Only set the sample rate if the channel reports that it
	 * supports the frequency control.
	 */

	if (sc->sc_audio_rev >= UAUDIO_VERSION_30) {
		/* FALLTHROUGH */
	} else if (sc->sc_audio_rev >= UAUDIO_VERSION_20) {
		unsigned int x;
	  
		for (x = 0; x != 256; x++) {
			if (dir == PCMDIR_PLAY) {
				if (!(sc->sc_mixer_clocks.bit_output[x / 8] &
				    (1 << (x % 8)))) {
					continue;
				}
			} else {
				if (!(sc->sc_mixer_clocks.bit_input[x / 8] &
				    (1 << (x % 8)))) {
					continue;
				}
			}

			if (uaudio20_set_speed(sc->sc_udev,
			    sc->sc_mixer_iface_no, x, chan_alt->sample_rate)) {
				/*
				 * If the endpoint is adaptive setting
				 * the speed may fail.
				 */
				DPRINTF("setting of sample rate failed! "
				    "(continuing anyway)\n");
			}
		}
	} else if (chan_alt->p_sed.v1->bmAttributes & UA_SED_FREQ_CONTROL) {
		if (uaudio_set_speed(sc->sc_udev,
		    chan_alt->p_ed1->bEndpointAddress, chan_alt->sample_rate)) {
			/*
			 * If the endpoint is adaptive setting the
			 * speed may fail.
			 */
			DPRINTF("setting of sample rate failed! "
			    "(continuing anyway)\n");
		}
	}
	if (usbd_transfer_setup(sc->sc_udev, &chan_alt->iface_index, chan->xfer,
	    chan_alt->usb_cfg, UAUDIO_NCHANBUFS + 1, chan, chan->pcm_mtx)) {
		DPRINTF("could not allocate USB transfers!\n");
		goto error;
	}

	fps = usbd_get_isoc_fps(sc->sc_udev);

	if (fps < 8000) {
		/* FULL speed USB */
		frames = uaudio_buffer_ms;
	} else {
		/* HIGH speed USB */
		frames = uaudio_buffer_ms * 8;
	}

	fps_shift = usbd_xfer_get_fps_shift(chan->xfer[0]);

	/* down shift number of frames per second, if any */
	fps >>= fps_shift;
	frames >>= fps_shift;

	/* bytes per frame should not be zero */
	chan->bytes_per_frame[0] =
	    ((chan_alt->sample_rate / fps) * chan_alt->sample_size);
	chan->bytes_per_frame[1] = howmany(chan_alt->sample_rate, fps) *
	    chan_alt->sample_size;

	/* setup data rate dithering, if any */
	chan->frames_per_second = fps;
	chan->sample_rem = chan_alt->sample_rate % fps;
	chan->sample_curr = 0;

	/* compute required buffer size */
	buf_size = (chan->bytes_per_frame[1] * frames);

	if (buf_size > (chan->end - chan->start)) {
		DPRINTF("buffer size is too big\n");
		goto error;
	}

	chan->intr_frames = frames;

	DPRINTF("fps=%d sample_rem=%d\n", (int)fps, (int)chan->sample_rem);

	if (chan->intr_frames == 0) {
		DPRINTF("frame shift is too high!\n");
		goto error;
	}

	mtx_lock(chan->pcm_mtx);
	chan->cur_alt = set_alt;
	mtx_unlock(chan->pcm_mtx);

done:
#if (UAUDIO_NCHANBUFS != 2)
#error "please update code"
#endif
	switch (operation) {
	case CHAN_OP_START:
		mtx_lock(chan->pcm_mtx);
		usbd_transfer_start(chan->xfer[0]);
		usbd_transfer_start(chan->xfer[1]);
		mtx_unlock(chan->pcm_mtx);
		break;
	case CHAN_OP_STOP:
		mtx_lock(chan->pcm_mtx);
		usbd_transfer_stop(chan->xfer[0]);
		usbd_transfer_stop(chan->xfer[1]);
		mtx_unlock(chan->pcm_mtx);
		break;
	default:
		break;
	}
	return;

error:
	usbd_transfer_unsetup(chan->xfer, UAUDIO_NCHANBUFS + 1);

	mtx_lock(chan->pcm_mtx);
	chan->cur_alt = CHAN_MAX_ALT;
	mtx_unlock(chan->pcm_mtx);
}

static void
uaudio_configure_msg(struct usb_proc_msg *pm)
{
	struct uaudio_softc *sc = ((struct uaudio_configure_msg *)pm)->sc;

	usb_proc_explore_unlock(sc->sc_udev);
	uaudio_configure_msg_sub(sc, &sc->sc_play_chan, PCMDIR_PLAY);
	uaudio_configure_msg_sub(sc, &sc->sc_rec_chan, PCMDIR_REC);
	usb_proc_explore_lock(sc->sc_udev);
}

/*========================================================================*
 * AS - Audio Stream - routines
 *========================================================================*/

#ifdef USB_DEBUG
static void
uaudio_chan_dump_ep_desc(const usb_endpoint_descriptor_audio_t *ed)
{
	if (ed) {
		DPRINTF("endpoint=%p bLength=%d bDescriptorType=%d \n"
		    "bEndpointAddress=%d bmAttributes=0x%x \n"
		    "wMaxPacketSize=%d bInterval=%d \n"
		    "bRefresh=%d bSynchAddress=%d\n",
		    ed, ed->bLength, ed->bDescriptorType,
		    ed->bEndpointAddress, ed->bmAttributes,
		    UGETW(ed->wMaxPacketSize), ed->bInterval,
		    UEP_HAS_REFRESH(ed) ? ed->bRefresh : 0,
		    UEP_HAS_SYNCADDR(ed) ? ed->bSynchAddress : 0);
	}
}

#endif

/*
 * The following is a workaround for broken no-name USB audio devices
 * sold by dealextreme called "3D sound". The problem is that the
 * manufacturer computed wMaxPacketSize is too small to hold the
 * actual data sent. In other words the device sometimes sends more
 * data than it actually reports it can send in a single isochronous
 * packet.
 */
static void
uaudio_record_fix_fs(usb_endpoint_descriptor_audio_t *ep,
    uint32_t xps, uint32_t add)
{
	uint32_t mps;

	mps = UGETW(ep->wMaxPacketSize);

	/*
	 * If the device indicates it can send more data than what the
	 * sample rate indicates, we apply the workaround.
	 */
	if (mps > xps) {

		/* allow additional data */
		xps += add;

		/* check against the maximum USB 1.x length */
		if (xps > 1023)
			xps = 1023;

		/* check if we should do an update */
		if (mps < xps) {
			/* simply update the wMaxPacketSize field */
			USETW(ep->wMaxPacketSize, xps);
			DPRINTF("Workaround: Updated wMaxPacketSize "
			    "from %d to %d bytes.\n",
			    (int)mps, (int)xps);
		}
	}
}

static usb_error_t
uaudio20_check_rate(struct usb_device *udev, uint8_t iface_no,
    uint8_t clockid, uint32_t rate)
{
	struct usb_device_request req;
	usb_error_t error;
#define	UAUDIO20_MAX_RATES 32	/* we support at maxium 32 rates */
	uint8_t data[2 + UAUDIO20_MAX_RATES * 12];
	uint16_t actlen;
	uint16_t rates;
	uint16_t x;

	DPRINTFN(6, "ifaceno=%d clockid=%d rate=%u\n",
	    iface_no, clockid, rate);

	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UA20_CS_RANGE;
	USETW2(req.wValue, UA20_CS_SAM_FREQ_CONTROL, 0);
	USETW2(req.wIndex, clockid, iface_no);
	/*
	 * Assume there is at least one rate to begin with, else some
	 * devices might refuse to return the USB descriptor:
	 */
	USETW(req.wLength, (2 + 1 * 12));

	error = usbd_do_request_flags(udev, NULL, &req, data,
	    USB_SHORT_XFER_OK, &actlen, USB_DEFAULT_TIMEOUT);

	if (error != 0 || actlen < 2) {
		/*
		 * Likely the descriptor doesn't fit into the supplied
		 * buffer. Try using a larger buffer and see if that
		 * helps:
		 */
		rates = MIN(UAUDIO20_MAX_RATES, (255 - 2) / 12);
		error = USB_ERR_INVAL;
	} else {
		rates = UGETW(data);

		if (rates > UAUDIO20_MAX_RATES) {
			DPRINTF("Too many rates truncating to %d\n", UAUDIO20_MAX_RATES);
			rates = UAUDIO20_MAX_RATES;
			error = USB_ERR_INVAL;
		} else if (rates > 1) {
			DPRINTF("Need to read full rate descriptor\n");
			error = USB_ERR_INVAL;
		}
	}

	if (error != 0) {
		/*
		 * Try to read full rate descriptor.
		 */
		actlen = (2 + rates * 12);

		USETW(req.wLength, actlen);

	        error = usbd_do_request_flags(udev, NULL, &req, data,
		    USB_SHORT_XFER_OK, &actlen, USB_DEFAULT_TIMEOUT);
	
		if (error != 0 || actlen < 2)
			return (USB_ERR_INVAL);

		rates = UGETW(data);
	}

	actlen = (actlen - 2) / 12;

	if (rates > actlen) {
		DPRINTF("Too many rates truncating to %d\n", actlen);
		rates = actlen;
	}

	for (x = 0; x != rates; x++) {
		uint32_t min = UGETDW(data + 2 + (12 * x));
		uint32_t max = UGETDW(data + 6 + (12 * x));
		uint32_t res = UGETDW(data + 10 + (12 * x));

		if (res == 0) {
			DPRINTF("Zero residue\n");
			res = 1;
		}

		if (min > max) {
			DPRINTF("Swapped max and min\n");
			uint32_t temp;
			temp = min;
			min = max;
			max = temp;
		}

		if (rate >= min && rate <= max &&
		    (((rate - min) % res) == 0)) {
			return (0);
		}
	}
	return (USB_ERR_INVAL);
}

static void
uaudio_chan_fill_info_sub(struct uaudio_softc *sc, struct usb_device *udev,
    uint32_t rate, uint8_t channels, uint8_t bit_resolution)
{
	struct usb_descriptor *desc = NULL;
	union uaudio_asid asid = { NULL };
	union uaudio_asf1d asf1d = { NULL };
	union uaudio_sed sed = { NULL };
	struct usb_midi_streaming_endpoint_descriptor *msid = NULL;
	usb_endpoint_descriptor_audio_t *ed1 = NULL;
	const struct usb_audio_control_descriptor *acdp = NULL;
	struct usb_config_descriptor *cd = usbd_get_config_descriptor(udev);
	struct usb_interface_descriptor *id;
	const struct uaudio_format *p_fmt = NULL;
	struct uaudio_chan *chan;
	struct uaudio_chan_alt *chan_alt;
	uint32_t format;
	uint16_t curidx = 0xFFFF;
	uint16_t lastidx = 0xFFFF;
	uint16_t alt_index = 0;
	uint16_t audio_rev = 0;
	uint16_t x;
	uint8_t ep_dir;
	uint8_t bChannels;
	uint8_t bBitResolution;
	uint8_t audio_if = 0;
	uint8_t midi_if = 0;
	uint8_t uma_if_class;

	while ((desc = usb_desc_foreach(cd, desc))) {

		if ((desc->bDescriptorType == UDESC_INTERFACE) &&
		    (desc->bLength >= sizeof(*id))) {

			id = (void *)desc;

			if (id->bInterfaceNumber != lastidx) {
				lastidx = id->bInterfaceNumber;
				curidx++;
				alt_index = 0;

			} else {
				alt_index++;
			}

			if ((!(sc->sc_hid.flags & UAUDIO_HID_VALID)) &&
			    (id->bInterfaceClass == UICLASS_HID) &&
			    (id->bInterfaceSubClass == 0) &&
			    (id->bInterfaceProtocol == 0) &&
			    (alt_index == 0) &&
			    usbd_get_iface(udev, curidx) != NULL) {
				DPRINTF("Found HID interface at %d\n",
				    curidx);
				sc->sc_hid.flags |= UAUDIO_HID_VALID;
				sc->sc_hid.iface_index = curidx;
			}

			uma_if_class =
			    ((id->bInterfaceClass == UICLASS_AUDIO) ||
			    ((id->bInterfaceClass == UICLASS_VENDOR) &&
			    (sc->sc_uq_au_vendor_class != 0)));

			if ((uma_if_class != 0) &&
			    (id->bInterfaceSubClass == UISUBCLASS_AUDIOSTREAM)) {
				audio_if = 1;
			} else {
				audio_if = 0;
			}

			if ((uma_if_class != 0) &&
			    (id->bInterfaceSubClass == UISUBCLASS_MIDISTREAM)) {

				/*
				 * XXX could allow multiple MIDI interfaces
				 */
				midi_if = 1;

				if ((sc->sc_midi_chan.valid == 0) &&
				    (usbd_get_iface(udev, curidx) != NULL)) {
					sc->sc_midi_chan.iface_index = curidx;
					sc->sc_midi_chan.iface_alt_index = alt_index;
					sc->sc_midi_chan.valid = 1;
				}
			} else {
				midi_if = 0;
			}
			asid.v1 = NULL;
			asf1d.v1 = NULL;
			ed1 = NULL;
			sed.v1 = NULL;

			/*
			 * There can only be one USB audio instance
			 * per USB device. Grab all USB audio
			 * interfaces on this USB device so that we
			 * don't attach USB audio twice:
			 */
			if (alt_index == 0 && curidx != sc->sc_mixer_iface_index &&
			    (id->bInterfaceClass == UICLASS_AUDIO || audio_if != 0 ||
			    midi_if != 0)) {
				usbd_set_parent_iface(sc->sc_udev, curidx,
				    sc->sc_mixer_iface_index);
			}
		}

		if (audio_if == 0) {
			if (midi_if == 0) {
				if ((acdp == NULL) &&
				    (desc->bDescriptorType == UDESC_CS_INTERFACE) &&
				    (desc->bDescriptorSubtype == UDESCSUB_AC_HEADER) &&
				    (desc->bLength >= sizeof(*acdp))) {
					acdp = (void *)desc;
					audio_rev = UGETW(acdp->bcdADC);
				}
			} else {
				msid = (void *)desc;

				/* get the maximum number of embedded jacks in use, if any */
				if (msid->bLength >= sizeof(*msid) &&
				    msid->bDescriptorType == UDESC_CS_ENDPOINT &&
				    msid->bDescriptorSubtype == MS_GENERAL &&
				    msid->bNumEmbMIDIJack > sc->sc_midi_chan.max_emb_jack) {
					sc->sc_midi_chan.max_emb_jack = msid->bNumEmbMIDIJack;
				}
			}
			/*
			 * Don't collect any USB audio descriptors if
			 * this is not an USB audio stream interface.
			 */
			continue;
		}

		if ((acdp != NULL || sc->sc_uq_au_vendor_class != 0) &&
		    (desc->bDescriptorType == UDESC_CS_INTERFACE) &&
		    (desc->bDescriptorSubtype == AS_GENERAL) &&
		    (asid.v1 == NULL)) {
			if (audio_rev >= UAUDIO_VERSION_30) {
				/* FALLTHROUGH */
			} else if (audio_rev >= UAUDIO_VERSION_20) {
				if (desc->bLength >= sizeof(*asid.v2)) {
					asid.v2 = (void *)desc;
				}
			} else {
				if (desc->bLength >= sizeof(*asid.v1)) {
					asid.v1 = (void *)desc;
				}
			}
		}
		if ((acdp != NULL || sc->sc_uq_au_vendor_class != 0) &&
		    (desc->bDescriptorType == UDESC_CS_INTERFACE) &&
		    (desc->bDescriptorSubtype == FORMAT_TYPE) &&
		    (asf1d.v1 == NULL)) {
			if (audio_rev >= UAUDIO_VERSION_30) {
				/* FALLTHROUGH */
			} else if (audio_rev >= UAUDIO_VERSION_20) {
				if (desc->bLength >= sizeof(*asf1d.v2))
					asf1d.v2 = (void *)desc;
			} else {
				if (desc->bLength >= sizeof(*asf1d.v1)) {
					asf1d.v1 = (void *)desc;

					if (asf1d.v1->bFormatType != FORMAT_TYPE_I) {
						DPRINTFN(11, "ignored bFormatType = %d\n",
						    asf1d.v1->bFormatType);
						asf1d.v1 = NULL;
						continue;
					}
					if (desc->bLength < (sizeof(*asf1d.v1) +
					    ((asf1d.v1->bSamFreqType == 0) ? 6 :
					    (asf1d.v1->bSamFreqType * 3)))) {
						DPRINTFN(11, "invalid descriptor, "
						    "too short\n");
						asf1d.v1 = NULL;
						continue;
					}
				}
			}
		}
		if ((desc->bDescriptorType == UDESC_ENDPOINT) &&
		    (desc->bLength >= UEP_MINSIZE) &&
		    (ed1 == NULL)) {
			ed1 = (void *)desc;
			if (UE_GET_XFERTYPE(ed1->bmAttributes) != UE_ISOCHRONOUS) {
				ed1 = NULL;
				continue;
			}
		}
		if ((acdp != NULL || sc->sc_uq_au_vendor_class != 0) &&
		    (desc->bDescriptorType == UDESC_CS_ENDPOINT) &&
		    (desc->bDescriptorSubtype == AS_GENERAL) &&
		    (sed.v1 == NULL)) {
			if (audio_rev >= UAUDIO_VERSION_30) {
				/* FALLTHROUGH */
			} else if (audio_rev >= UAUDIO_VERSION_20) {
				if (desc->bLength >= sizeof(*sed.v2))
					sed.v2 = (void *)desc;
			} else {
				if (desc->bLength >= sizeof(*sed.v1))
					sed.v1 = (void *)desc;
			}
		}
		if (asid.v1 == NULL || asf1d.v1 == NULL ||
		    ed1 == NULL || sed.v1 == NULL) {
			/* need more descriptors */
			continue;
		}

		ep_dir = UE_GET_DIR(ed1->bEndpointAddress);

		/* We ignore sync endpoint information until further. */

		if (audio_rev >= UAUDIO_VERSION_30) {
			goto next_ep;
		} else if (audio_rev >= UAUDIO_VERSION_20) {

			uint32_t dwFormat;

			dwFormat = UGETDW(asid.v2->bmFormats);
			bChannels = asid.v2->bNrChannels;
			bBitResolution = asf1d.v2->bSubslotSize * 8;

			if ((bChannels != channels) ||
			    (bBitResolution != bit_resolution)) {
				DPRINTF("Wrong number of channels\n");
				goto next_ep;
			}

			for (p_fmt = uaudio20_formats;
			    p_fmt->wFormat != 0; p_fmt++) {
				if ((p_fmt->wFormat & dwFormat) &&
				    (p_fmt->bPrecision == bBitResolution))
					break;
			}

			if (p_fmt->wFormat == 0) {
				DPRINTF("Unsupported audio format\n");
				goto next_ep;
			}

			for (x = 0; x != 256; x++) {
				if (ep_dir == UE_DIR_OUT) {
					if (!(sc->sc_mixer_clocks.bit_output[x / 8] &
					    (1 << (x % 8)))) {
						continue;
					}
				} else {
					if (!(sc->sc_mixer_clocks.bit_input[x / 8] &
					    (1 << (x % 8)))) {
						continue;
					}
				}

				DPRINTF("Checking clock ID=%d\n", x);

				if (uaudio20_check_rate(udev,
				    sc->sc_mixer_iface_no, x, rate)) {
					DPRINTF("Unsupported sampling "
					    "rate, id=%d\n", x);
					goto next_ep;
				}
			}
		} else {
			uint16_t wFormat;

			wFormat = UGETW(asid.v1->wFormatTag);
			bChannels = UAUDIO_MAX_CHAN(asf1d.v1->bNrChannels);
			bBitResolution = asf1d.v1->bSubFrameSize * 8;

			if (asf1d.v1->bSamFreqType == 0) {
				DPRINTFN(16, "Sample rate: %d-%dHz\n",
				    UA_SAMP_LO(asf1d.v1),
				    UA_SAMP_HI(asf1d.v1));

				if ((rate >= UA_SAMP_LO(asf1d.v1)) &&
				    (rate <= UA_SAMP_HI(asf1d.v1)))
					goto found_rate;
			} else {

				for (x = 0; x < asf1d.v1->bSamFreqType; x++) {
					DPRINTFN(16, "Sample rate = %dHz\n",
					    UA_GETSAMP(asf1d.v1, x));

					if (rate == UA_GETSAMP(asf1d.v1, x))
						goto found_rate;
				}
			}
			goto next_ep;

	found_rate:
			for (p_fmt = uaudio10_formats;
			    p_fmt->wFormat != 0; p_fmt++) {
				if ((p_fmt->wFormat == wFormat) &&
				    (p_fmt->bPrecision == bBitResolution))
					break;
			}
			if (p_fmt->wFormat == 0) {
				DPRINTF("Unsupported audio format\n");
				goto next_ep;
			}

			if ((bChannels != channels) ||
			    (bBitResolution != bit_resolution)) {
				DPRINTF("Wrong number of channels\n");
				goto next_ep;
			}
		}

		chan = (ep_dir == UE_DIR_IN) ?
		    &sc->sc_rec_chan : &sc->sc_play_chan;

		if (usbd_get_iface(udev, curidx) == NULL) {
			DPRINTF("Interface is not valid\n");
			goto next_ep;
		}
		if (chan->num_alt == CHAN_MAX_ALT) {
			DPRINTF("Too many alternate settings\n");
			goto next_ep;
		}
		chan->set_alt = 0;
		chan->cur_alt = CHAN_MAX_ALT;

		chan_alt = &chan->usb_alt[chan->num_alt++];

#ifdef USB_DEBUG
		uaudio_chan_dump_ep_desc(ed1);
#endif
		DPRINTF("Sample rate = %dHz, channels = %d, "
		    "bits = %d, format = %s\n", rate, channels,
		    bit_resolution, p_fmt->description);

		chan_alt->sample_rate = rate;
		chan_alt->p_asf1d = asf1d;
		chan_alt->p_ed1 = ed1;
		chan_alt->p_fmt = p_fmt;
		chan_alt->p_sed = sed;
		chan_alt->iface_index = curidx;
		chan_alt->iface_alt_index = alt_index;

		if (ep_dir == UE_DIR_IN)
			chan_alt->usb_cfg = uaudio_cfg_record;
		else
			chan_alt->usb_cfg = uaudio_cfg_play;

		chan_alt->sample_size = (UAUDIO_MAX_CHAN(channels) *
		    p_fmt->bPrecision) / 8;
		chan_alt->channels = channels;

		if (ep_dir == UE_DIR_IN &&
		    usbd_get_speed(udev) == USB_SPEED_FULL) {
			uaudio_record_fix_fs(ed1,
			    chan_alt->sample_size * (rate / 1000),
			    chan_alt->sample_size * (rate / 4000));
		}

		/* setup play/record format */

		format = chan_alt->p_fmt->freebsd_fmt;

		/* get default SND_FORMAT() */
		format = SND_FORMAT(format, chan_alt->channels, 0);

		switch (chan_alt->channels) {
		uint32_t temp_fmt;
		case 1:
		case 2:
			/* mono and stereo */
			break;
		default:
			/* surround and more */
			temp_fmt = feeder_matrix_default_format(format);
			/* if multichannel, then format can be zero */
			if (temp_fmt != 0)
				format = temp_fmt;
			break;
		}

		/* check if format is not supported */
		if (format == 0) {
			DPRINTF("The selected audio format is not supported\n");
			chan->num_alt--;
			goto next_ep;
		}
		if (chan->num_alt > 1) {
			/* we only accumulate one format at different sample rates */
			if (chan->pcm_format[0] != format) {
				DPRINTF("Multiple formats is not supported\n");
				chan->num_alt--;
				goto next_ep;
			}
			/* ignore if duplicate sample rate entry */
			if (rate == chan->usb_alt[chan->num_alt - 2].sample_rate) {
				DPRINTF("Duplicate sample rate detected\n");
				chan->num_alt--;
				goto next_ep;
			}
		}
		chan->pcm_cap.fmtlist = chan->pcm_format;
		chan->pcm_cap.fmtlist[0] = format;

		/* check if device needs bitperfect */
		if (chan_alt->channels > UAUDIO_MATRIX_MAX)
			sc->sc_pcm_bitperfect = 1;

		if (rate < chan->pcm_cap.minspeed || chan->pcm_cap.minspeed == 0)
			chan->pcm_cap.minspeed = rate;
		if (rate > chan->pcm_cap.maxspeed || chan->pcm_cap.maxspeed == 0)
			chan->pcm_cap.maxspeed = rate;

		if (sc->sc_sndstat_valid != 0) {
			sbuf_printf(&sc->sc_sndstat, "\n\t"
			    "mode %d.%d:(%s) %dch, %dbit, %s, %dHz",
			    curidx, alt_index,
			    (ep_dir == UE_DIR_IN) ? "input" : "output",
				    channels, p_fmt->bPrecision,
				    p_fmt->description, rate);
		}

	next_ep:
		sed.v1 = NULL;
		ed1 = NULL;
	}
}

/* This structure defines all the supported rates. */

static const uint32_t uaudio_rate_list[CHAN_MAX_ALT] = {
	384000,
	352800,
	192000,
	176400,
	96000,
	88200,
	88000,
	80000,
	72000,
	64000,
	56000,
	48000,
	44100,
	40000,
	32000,
	24000,
	22050,
	16000,
	11025,
	8000,
	0
};

static void
uaudio_chan_fill_info(struct uaudio_softc *sc, struct usb_device *udev)
{
	uint32_t rate = uaudio_default_rate;
	uint8_t z;
	uint8_t bits = uaudio_default_bits;
	uint8_t y;
	uint8_t channels = uaudio_default_channels;
	uint8_t x;

	bits -= (bits % 8);
	if ((bits == 0) || (bits > 32)) {
		/* set a valid value */
		bits = 32;
	}
	if (channels == 0) {
		switch (usbd_get_speed(udev)) {
		case USB_SPEED_LOW:
		case USB_SPEED_FULL:
			/*
			 * Due to high bandwidth usage and problems
			 * with HIGH-speed split transactions we
			 * disable surround setups on FULL-speed USB
			 * by default
			 */
			channels = 4;
			break;
		default:
			channels = UAUDIO_CHANNELS_MAX;
			break;
		}
	} else if (channels > UAUDIO_CHANNELS_MAX)
		channels = UAUDIO_CHANNELS_MAX;

	if (sbuf_new(&sc->sc_sndstat, NULL, 4096, SBUF_AUTOEXTEND))
		sc->sc_sndstat_valid = 1;

	/* try to search for a valid config */

	for (x = channels; x; x--) {
		for (y = bits; y; y -= 8) {

			/* try user defined rate, if any */
			if (rate != 0)
				uaudio_chan_fill_info_sub(sc, udev, rate, x, y);

			/* try find a matching rate, if any */
			for (z = 0; uaudio_rate_list[z]; z++)
				uaudio_chan_fill_info_sub(sc, udev, uaudio_rate_list[z], x, y);
		}
	}
	if (sc->sc_sndstat_valid)
		sbuf_finish(&sc->sc_sndstat);
}

static void
uaudio_chan_play_sync_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uaudio_chan *ch = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint64_t sample_rate;
	uint8_t buf[4];
	uint64_t temp;
	int len;
	int actlen;
	int nframes;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, &nframes);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTFN(6, "transferred %d bytes\n", actlen);

		if (nframes == 0)
			break;
		len = usbd_xfer_frame_len(xfer, 0);
		if (len == 0)
			break;
		if (len > sizeof(buf))
			len = sizeof(buf);

		memset(buf, 0, sizeof(buf));

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, len);

		temp = UGETDW(buf);

		DPRINTF("Value = 0x%08x\n", (int)temp);

		/* auto-detect SYNC format */

		if (len == 4)
			temp &= 0x0fffffff;

		/* check for no data */

		if (temp == 0)
			break;

		temp *= 125ULL;

		sample_rate = ch->usb_alt[ch->cur_alt].sample_rate;

		/* auto adjust */
		while (temp < (sample_rate - (sample_rate / 4)))
			temp *= 2;
 
		while (temp > (sample_rate + (sample_rate / 2)))
			temp /= 2;

		DPRINTF("Comparing %d Hz :: %d Hz\n",
		    (int)temp, (int)sample_rate);

		/*
		 * Use feedback value as fallback when there is no
		 * recording channel:
		 */
		if (ch->priv_sc->sc_rec_chan.num_alt == 0) {
			int32_t jitter_max = howmany(sample_rate, 16000);

			/*
			 * Range check the jitter values to avoid
			 * bogus sample rate adjustments. The expected
			 * deviation should not be more than 1Hz per
			 * second. The USB v2.0 specification also
			 * mandates this requirement. Refer to chapter
			 * 5.12.4.2 about feedback.
			 */
			ch->jitter_curr = temp - sample_rate;
			if (ch->jitter_curr > jitter_max)
				ch->jitter_curr = jitter_max;
			else if (ch->jitter_curr < -jitter_max)
				ch->jitter_curr = -jitter_max;
		}
		ch->feedback_rate = temp;
		break;

	case USB_ST_SETUP:
		/*
		 * Check if the recording stream can be used as a
		 * source of jitter information to save some
		 * isochronous bandwidth:
		 */
		if (ch->priv_sc->sc_rec_chan.num_alt != 0 &&
		    uaudio_debug == 0)
			break;
		usbd_xfer_set_frames(xfer, 1);
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_framelen(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		break;
	}
}

static int
uaudio_chan_is_async(struct uaudio_chan *ch, uint8_t alt)
{
	uint8_t attr = ch->usb_alt[alt].p_ed1->bmAttributes;
	return (UE_GET_ISO_TYPE(attr) == UE_ISO_ASYNC);
}

static void
uaudio_chan_play_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uaudio_chan *ch = usbd_xfer_softc(xfer);
	struct uaudio_chan *ch_rec;
	struct usb_page_cache *pc;
	uint32_t mfl;
	uint32_t total;
	uint32_t blockcount;
	uint32_t n;
	uint32_t offset;
	int sample_size;
	int actlen;
	int sumlen;

	if (ch->running == 0 || ch->start == ch->end) {
		DPRINTF("not running or no buffer!\n");
		return;
	}

	/* check if there is a record channel */
	if (ch->priv_sc->sc_rec_chan.num_alt > 0)
		ch_rec = &ch->priv_sc->sc_rec_chan;
	else
		ch_rec = NULL;

	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
tr_setup:
		if (ch_rec != NULL) {
			/* reset receive jitter counters */
			mtx_lock(ch_rec->pcm_mtx);
			ch_rec->jitter_curr = 0;
			ch_rec->jitter_rem = 0;
			mtx_unlock(ch_rec->pcm_mtx);
		}

		/* reset transmit jitter counters */
		ch->jitter_curr = 0;
		ch->jitter_rem = 0;

		/* FALLTHROUGH */
	case USB_ST_TRANSFERRED:
		if (actlen < sumlen) {
			DPRINTF("short transfer, "
			    "%d of %d bytes\n", actlen, sumlen);
		}
		chn_intr(ch->pcm_ch);

		/*
		 * Check for asynchronous playback endpoint and that
		 * the playback endpoint is properly configured:
		 */
		if (ch_rec != NULL &&
		    uaudio_chan_is_async(ch, ch->cur_alt) != 0) {
			mtx_lock(ch_rec->pcm_mtx);
			if (ch_rec->cur_alt < ch_rec->num_alt) {
				int64_t tx_jitter;
				int64_t rx_rate;

				/* translate receive jitter into transmit jitter */
				tx_jitter = ch->usb_alt[ch->cur_alt].sample_rate;
				tx_jitter = (tx_jitter * ch_rec->jitter_curr) +
				    ch->jitter_rem;

				/* reset receive jitter counters */
				ch_rec->jitter_curr = 0;
				ch_rec->jitter_rem = 0;
		
				/* compute exact number of transmit jitter samples */
				rx_rate = ch_rec->usb_alt[ch_rec->cur_alt].sample_rate;
				ch->jitter_curr += tx_jitter / rx_rate;
				ch->jitter_rem = tx_jitter % rx_rate;
			}
			mtx_unlock(ch_rec->pcm_mtx);
		}

		/* start the SYNC transfer one time per second, if any */
		ch->intr_counter += ch->intr_frames;
		if (ch->intr_counter >= ch->frames_per_second) {
			ch->intr_counter -= ch->frames_per_second;
			usbd_transfer_start(ch->xfer[UAUDIO_NCHANBUFS]);
		}

		mfl = usbd_xfer_max_framelen(xfer);

		if (ch->bytes_per_frame[1] > mfl) {
			DPRINTF("bytes per transfer, %d, "
			    "exceeds maximum, %d!\n",
			    ch->bytes_per_frame[1],
			    mfl);
			break;
		}

		blockcount = ch->intr_frames;

		/* setup number of frames */
		usbd_xfer_set_frames(xfer, blockcount);

		/* get sample size */
		sample_size = ch->usb_alt[ch->cur_alt].sample_size;

		/* reset total length */
		total = 0;

		/* setup frame lengths */
		for (n = 0; n != blockcount; n++) {
			uint32_t frame_len;

			ch->sample_curr += ch->sample_rem;
			if (ch->sample_curr >= ch->frames_per_second) {
				ch->sample_curr -= ch->frames_per_second;
				frame_len = ch->bytes_per_frame[1];
			} else {
				frame_len = ch->bytes_per_frame[0];
			}

			/* handle free running clock case */
			if (ch->jitter_curr > 0 &&
			    (frame_len + sample_size) <= mfl) {
				DPRINTFN(6, "sending one sample more\n");
				ch->jitter_curr--;
				frame_len += sample_size;
			} else if (ch->jitter_curr < 0 &&
			    frame_len >= sample_size) {
				DPRINTFN(6, "sending one sample less\n");
				ch->jitter_curr++;
				frame_len -= sample_size;
			}
			usbd_xfer_set_frame_len(xfer, n, frame_len);
			total += frame_len;
		}

		DPRINTFN(6, "transferring %d bytes\n", total);

		offset = 0;

		pc = usbd_xfer_get_frame(xfer, 0);
		while (total > 0) {

			n = (ch->end - ch->cur);
			if (n > total)
				n = total;

			usbd_copy_in(pc, offset, ch->cur, n);

			total -= n;
			ch->cur += n;
			offset += n;

			if (ch->cur >= ch->end)
				ch->cur = ch->start;
		}
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED)
			goto tr_setup;
		break;
	}
}

static void
uaudio_chan_record_sync_callback(struct usb_xfer *xfer, usb_error_t error)
{
	/* TODO */
}

static void
uaudio_chan_record_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uaudio_chan *ch = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t offset0;
	uint32_t mfl;
	int m;
	int n;
	int len;
	int actlen;
	int nframes;
	int expected_bytes;
	int sample_size;

	if (ch->start == ch->end) {
		DPRINTF("no buffer!\n");
		return;
	}

	usbd_xfer_status(xfer, &actlen, NULL, NULL, &nframes);
	mfl = usbd_xfer_max_framelen(xfer);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		offset0 = 0;
		pc = usbd_xfer_get_frame(xfer, 0);

		/* try to compute the number of expected bytes */
		ch->sample_curr += (ch->sample_rem * ch->intr_frames);

		/* compute number of expected bytes */
		expected_bytes = (ch->intr_frames * ch->bytes_per_frame[0]) +
		    ((ch->sample_curr / ch->frames_per_second) *
		    (ch->bytes_per_frame[1] - ch->bytes_per_frame[0]));

		/* keep remainder */
		ch->sample_curr %= ch->frames_per_second;

		/* get current sample size */
		sample_size = ch->usb_alt[ch->cur_alt].sample_size;

		for (n = 0; n != nframes; n++) {
			uint32_t offset1 = offset0;

			len = usbd_xfer_frame_len(xfer, n);

			/* make sure we only receive complete samples */
			len = len - (len % sample_size);

			/* subtract bytes received from expected payload */
			expected_bytes -= len;

			/* don't receive data when not ready */
			if (ch->running == 0 || ch->cur_alt != ch->set_alt)
				continue;

			/* fill ring buffer with samples, if any */
			while (len > 0) {

				m = (ch->end - ch->cur);

				if (m > len)
					m = len;

				usbd_copy_out(pc, offset1, ch->cur, m);

				len -= m;
				offset1 += m;
				ch->cur += m;

				if (ch->cur >= ch->end)
					ch->cur = ch->start;
			}

			offset0 += mfl;
		}

		/* update current jitter */
		ch->jitter_curr -= (expected_bytes / sample_size);

		/* don't allow a huge amount of jitter to accumulate */
		nframes = 2 * ch->intr_frames;

		/* range check current jitter */
		if (ch->jitter_curr < -nframes)
			ch->jitter_curr = -nframes;
		else if (ch->jitter_curr > nframes)
			ch->jitter_curr = nframes;

		DPRINTFN(6, "transferred %d bytes, jitter %d samples\n",
		    actlen, ch->jitter_curr);

		if (ch->running != 0)
			chn_intr(ch->pcm_ch);

	case USB_ST_SETUP:
tr_setup:
		nframes = ch->intr_frames;

		usbd_xfer_set_frames(xfer, nframes);
		for (n = 0; n != nframes; n++)
			usbd_xfer_set_frame_len(xfer, n, mfl);

		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED)
			goto tr_setup;
		break;
	}
}

void   *
uaudio_chan_init(struct uaudio_softc *sc, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct uaudio_chan *ch = ((dir == PCMDIR_PLAY) ?
	    &sc->sc_play_chan : &sc->sc_rec_chan);
	uint32_t buf_size;
	uint8_t x;

	/* store mutex and PCM channel */

	ch->pcm_ch = c;
	ch->pcm_mtx = c->lock;

	/* compute worst case buffer */

	buf_size = 0;
	for (x = 0; x != ch->num_alt; x++) {
		uint32_t temp = uaudio_get_buffer_size(ch, x);
		if (temp > buf_size)
			buf_size = temp;
	}

	/* allow double buffering */
	buf_size *= 2;

	DPRINTF("Worst case buffer is %d bytes\n", (int)buf_size);

	ch->buf = malloc(buf_size, M_DEVBUF, M_WAITOK | M_ZERO);
	if (ch->buf == NULL)
		goto error;
	if (sndbuf_setup(b, ch->buf, buf_size) != 0)
		goto error;

	ch->start = ch->buf;
	ch->end = ch->buf + buf_size;
	ch->cur = ch->buf;
	ch->pcm_buf = b;
	ch->max_buf = buf_size;

	if (ch->pcm_mtx == NULL) {
		DPRINTF("ERROR: PCM channels does not have a mutex!\n");
		goto error;
	}
	return (ch);

error:
	uaudio_chan_free(ch);
	return (NULL);
}

int
uaudio_chan_free(struct uaudio_chan *ch)
{
	if (ch->buf != NULL) {
		free(ch->buf, M_DEVBUF);
		ch->buf = NULL;
	}
	usbd_transfer_unsetup(ch->xfer, UAUDIO_NCHANBUFS + 1);

	ch->num_alt = 0;

	return (0);
}

int
uaudio_chan_set_param_blocksize(struct uaudio_chan *ch, uint32_t blocksize)
{
	uint32_t temp = 2 * uaudio_get_buffer_size(ch, ch->set_alt);
	sndbuf_setup(ch->pcm_buf, ch->buf, temp);
	return (temp / 2);
}

int
uaudio_chan_set_param_fragments(struct uaudio_chan *ch, uint32_t blocksize,
    uint32_t blockcount)
{
	return (1);
}

int
uaudio_chan_set_param_speed(struct uaudio_chan *ch, uint32_t speed)
{
	struct uaudio_softc *sc;
	uint8_t x;

	sc = ch->priv_sc;

	for (x = 0; x < ch->num_alt; x++) {
		if (ch->usb_alt[x].sample_rate < speed) {
			/* sample rate is too low */
			break;
		}
	}

	if (x != 0)
		x--;

	usb_proc_explore_lock(sc->sc_udev);
	ch->set_alt = x;
	usb_proc_explore_unlock(sc->sc_udev);

	DPRINTF("Selecting alt %d\n", (int)x);

	return (ch->usb_alt[x].sample_rate);
}

int
uaudio_chan_getptr(struct uaudio_chan *ch)
{
	return (ch->cur - ch->start);
}

struct pcmchan_caps *
uaudio_chan_getcaps(struct uaudio_chan *ch)
{
	return (&ch->pcm_cap);
}

static struct pcmchan_matrix uaudio_chan_matrix_swap_2_0 = {
	.id = SND_CHN_MATRIX_DRV,
	.channels = 2,
	.ext = 0,
	.map = {
		/* Right */
		[0] = {
			.type = SND_CHN_T_FR,
			.members =
			    SND_CHN_T_MASK_FR | SND_CHN_T_MASK_FC |
			    SND_CHN_T_MASK_LF | SND_CHN_T_MASK_BR |
			    SND_CHN_T_MASK_BC | SND_CHN_T_MASK_SR
		},
		/* Left */
		[1] = {
			.type = SND_CHN_T_FL,
			.members =
			    SND_CHN_T_MASK_FL | SND_CHN_T_MASK_FC |
			    SND_CHN_T_MASK_LF | SND_CHN_T_MASK_BL |
			    SND_CHN_T_MASK_BC | SND_CHN_T_MASK_SL
		},
		[2] = {
			.type = SND_CHN_T_MAX,
			.members = 0
		}
	},
	.mask = SND_CHN_T_MASK_FR | SND_CHN_T_MASK_FL,
	.offset = {  1,  0, -1, -1, -1, -1, -1, -1, -1,
		    -1, -1, -1, -1, -1, -1, -1, -1, -1  }
};

struct pcmchan_matrix *
uaudio_chan_getmatrix(struct uaudio_chan *ch, uint32_t format)
{
	struct uaudio_softc *sc;

	sc = ch->priv_sc;

	if (sc != NULL && sc->sc_uq_audio_swap_lr != 0 &&
	    AFMT_CHANNEL(format) == 2)
		return (&uaudio_chan_matrix_swap_2_0);

	return (feeder_matrix_format_map(format));
}

int
uaudio_chan_set_param_format(struct uaudio_chan *ch, uint32_t format)
{
	DPRINTF("Selecting format 0x%08x\n", (unsigned int)format);
	return (0);
}

static void
uaudio_chan_start_sub(struct uaudio_chan *ch)
{
	struct uaudio_softc *sc = ch->priv_sc;
	int do_start = 0;

	if (ch->operation != CHAN_OP_DRAIN) {
		if (ch->cur_alt == ch->set_alt &&
		    ch->operation == CHAN_OP_NONE &&
		    mtx_owned(ch->pcm_mtx) != 0) {
			/* save doing the explore task */
			do_start = 1;
		} else {
			ch->operation = CHAN_OP_START;
			(void)usb_proc_explore_msignal(sc->sc_udev,
			    &sc->sc_config_msg[0], &sc->sc_config_msg[1]);
		}
	}
	if (do_start) {
		usbd_transfer_start(ch->xfer[0]);
		usbd_transfer_start(ch->xfer[1]);
	}
}

static int
uaudio_chan_need_both(struct uaudio_softc *sc)
{
	return (sc->sc_play_chan.num_alt > 0 &&
	    sc->sc_play_chan.running != 0 &&
	    uaudio_chan_is_async(&sc->sc_play_chan,
	    sc->sc_play_chan.set_alt) != 0 &&
	    sc->sc_rec_chan.num_alt > 0 &&
	    sc->sc_rec_chan.running == 0);
}

static int
uaudio_chan_need_none(struct uaudio_softc *sc)
{
	return (sc->sc_play_chan.num_alt > 0 &&
	    sc->sc_play_chan.running == 0 &&
	    sc->sc_rec_chan.num_alt > 0 &&
	    sc->sc_rec_chan.running == 0);
}

void
uaudio_chan_start(struct uaudio_chan *ch)
{
	struct uaudio_softc *sc = ch->priv_sc;

	/* make operation atomic */
	usb_proc_explore_lock(sc->sc_udev);

	/* check if not running */
	if (ch->running == 0) {
	  	uint32_t temp;

		/* get current buffer size */
		temp = 2 * uaudio_get_buffer_size(ch, ch->set_alt);

		/* set running flag */
		ch->running = 1;

		/* ensure the hardware buffer is reset */
		ch->start = ch->buf;
		ch->end = ch->buf + temp;
		ch->cur = ch->buf;

		if (uaudio_chan_need_both(sc)) {
			/*
			 * Start both endpoints because of need for
			 * jitter information:
			 */
			uaudio_chan_start_sub(&sc->sc_rec_chan);
			uaudio_chan_start_sub(&sc->sc_play_chan);
		} else {
			uaudio_chan_start_sub(ch);
		}
	}

	/* exit atomic operation */
	usb_proc_explore_unlock(sc->sc_udev);
}

static void
uaudio_chan_stop_sub(struct uaudio_chan *ch)
{
	struct uaudio_softc *sc = ch->priv_sc;
	int do_stop = 0;

	if (ch->operation != CHAN_OP_DRAIN) {
		if (ch->cur_alt == ch->set_alt &&
		    ch->operation == CHAN_OP_NONE &&
		    mtx_owned(ch->pcm_mtx) != 0) {
			/* save doing the explore task */
			do_stop = 1;
		} else {
			ch->operation = CHAN_OP_STOP;
			(void)usb_proc_explore_msignal(sc->sc_udev,
			    &sc->sc_config_msg[0], &sc->sc_config_msg[1]);
		}
	}
	if (do_stop) {
		usbd_transfer_stop(ch->xfer[0]);
		usbd_transfer_stop(ch->xfer[1]);
	}
}

void
uaudio_chan_stop(struct uaudio_chan *ch)
{
	struct uaudio_softc *sc = ch->priv_sc;

	/* make operation atomic */
	usb_proc_explore_lock(sc->sc_udev);

	/* check if running */
	if (ch->running != 0) {
		/* clear running flag */
		ch->running = 0;

		if (uaudio_chan_need_both(sc)) {
			/*
			 * Leave the endpoints running because we need
			 * information about jitter!
			 */
		} else if (uaudio_chan_need_none(sc)) {
			/*
			 * Stop both endpoints in case the one was used for
			 * jitter information:
			 */
			uaudio_chan_stop_sub(&sc->sc_rec_chan);
			uaudio_chan_stop_sub(&sc->sc_play_chan);
		} else {
			uaudio_chan_stop_sub(ch);
		}
	}

	/* exit atomic operation */
	usb_proc_explore_unlock(sc->sc_udev);
}

/*========================================================================*
 * AC - Audio Controller - routines
 *========================================================================*/

static int
uaudio_mixer_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	struct uaudio_softc *sc;
	struct uaudio_mixer_node *pmc;
	int hint;
	int error;
	int temp = 0;
	int chan = 0;

	sc = (struct uaudio_softc *)oidp->oid_arg1;
	hint = oidp->oid_arg2;

	if (sc->sc_mixer_lock == NULL)
		return (ENXIO);

	/* lookup mixer node */

	mtx_lock(sc->sc_mixer_lock);
	for (pmc = sc->sc_mixer_root; pmc != NULL; pmc = pmc->next) {
		for (chan = 0; chan != (int)pmc->nchan; chan++) {
			if (pmc->wValue[chan] != -1 &&
			    pmc->wValue[chan] == hint) {
				temp = pmc->wData[chan];
				goto found;
			}
		}
	}
found:
	mtx_unlock(sc->sc_mixer_lock);

	error = sysctl_handle_int(oidp, &temp, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/* update mixer value */

	mtx_lock(sc->sc_mixer_lock);
	if (pmc != NULL &&
	    temp >= pmc->minval &&
	    temp <= pmc->maxval) {

		pmc->wData[chan] = temp;
		pmc->update[(chan / 8)] |= (1 << (chan % 8));

		/* start the transfer, if not already started */
		usbd_transfer_start(sc->sc_mixer_xfer[0]);
	}
	mtx_unlock(sc->sc_mixer_lock);

	return (0);
}

static void
uaudio_mixer_ctl_free(struct uaudio_softc *sc)
{
	struct uaudio_mixer_node *p_mc;

	while ((p_mc = sc->sc_mixer_root) != NULL) {
		sc->sc_mixer_root = p_mc->next;
		free(p_mc, M_USBDEV);
	}
}

static void
uaudio_mixer_register_sysctl(struct uaudio_softc *sc, device_t dev)
{
	struct uaudio_mixer_node *pmc;
	struct sysctl_oid *mixer_tree;
	struct sysctl_oid *control_tree;
	char buf[32];
	int chan;
	int n;

	mixer_tree = SYSCTL_ADD_NODE(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "mixer",
	    CTLFLAG_RD, NULL, "");

	if (mixer_tree == NULL)
		return;

	for (n = 0, pmc = sc->sc_mixer_root; pmc != NULL;
	    pmc = pmc->next, n++) {

		for (chan = 0; chan < pmc->nchan; chan++) {

			if (pmc->nchan > 1) {
				snprintf(buf, sizeof(buf), "%s_%d_%d",
				    pmc->name, n, chan);
			} else {
				snprintf(buf, sizeof(buf), "%s_%d",
				    pmc->name, n);
			}

			control_tree = SYSCTL_ADD_NODE(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(mixer_tree), OID_AUTO, buf,
			    CTLFLAG_RD, NULL, "Mixer control nodes");

			if (control_tree == NULL)
				continue;

			SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(control_tree),
			    OID_AUTO, "val", CTLTYPE_INT | CTLFLAG_RWTUN, sc,
			    pmc->wValue[chan],
			    uaudio_mixer_sysctl_handler, "I", "Current value");

			SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(control_tree),
			    OID_AUTO, "min", CTLFLAG_RD, 0, pmc->minval,
			    "Minimum value");

			SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(control_tree),
			    OID_AUTO, "max", CTLFLAG_RD, 0, pmc->maxval,
			    "Maximum value");

			SYSCTL_ADD_STRING(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(control_tree),
			    OID_AUTO, "desc", CTLFLAG_RD, pmc->desc, 0,
			    "Description");
		}
	}
}

/* M-Audio FastTrack Ultra Mixer Description */
/* Origin: Linux USB Audio driver */
static void
uaudio_mixer_controls_create_ftu(struct uaudio_softc *sc)
{
	int chx;
	int chy;

	memset(&MIX(sc), 0, sizeof(MIX(sc)));
	MIX(sc).wIndex = MAKE_WORD(6, sc->sc_mixer_iface_no);
	MIX(sc).wValue[0] = MAKE_WORD(8, 0);
	MIX(sc).class = UAC_OUTPUT;
	MIX(sc).type = MIX_UNSIGNED_16;
	MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
	MIX(sc).name = "effect";
	MIX(sc).minval = 0;
	MIX(sc).maxval = 7;
	MIX(sc).mul = 7;
	MIX(sc).nchan = 1;
	MIX(sc).update[0] = 1;
	strlcpy(MIX(sc).desc, "Room1,2,3,Hall1,2,Plate,Delay,Echo", sizeof(MIX(sc).desc));
	uaudio_mixer_add_ctl_sub(sc, &MIX(sc));

	memset(&MIX(sc), 0, sizeof(MIX(sc)));
	MIX(sc).wIndex = MAKE_WORD(5, sc->sc_mixer_iface_no);

	for (chx = 0; chx != 8; chx++) {
		for (chy = 0; chy != 8; chy++) {

			MIX(sc).wValue[0] = MAKE_WORD(chx + 1, chy + 1);
			MIX(sc).type = MIX_SIGNED_16;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
			MIX(sc).name = "mix_rec";
			MIX(sc).nchan = 1;
			MIX(sc).update[0] = 1;
			MIX(sc).val_default = 0;
			snprintf(MIX(sc).desc, sizeof(MIX(sc).desc),
			    "AIn%d - Out%d Record Volume", chy + 1, chx + 1);

			uaudio_mixer_add_ctl(sc, &MIX(sc));

			MIX(sc).wValue[0] = MAKE_WORD(chx + 1, chy + 1 + 8);
			MIX(sc).type = MIX_SIGNED_16;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
			MIX(sc).name = "mix_play";
			MIX(sc).nchan = 1;
			MIX(sc).update[0] = 1;
			MIX(sc).val_default = (chx == chy) ? 2 : 0;
			snprintf(MIX(sc).desc, sizeof(MIX(sc).desc),
			    "DIn%d - Out%d Playback Volume", chy + 1, chx + 1);

			uaudio_mixer_add_ctl(sc, &MIX(sc));
		}
	}

	memset(&MIX(sc), 0, sizeof(MIX(sc)));
	MIX(sc).wIndex = MAKE_WORD(6, sc->sc_mixer_iface_no);
	MIX(sc).wValue[0] = MAKE_WORD(2, 0);
	MIX(sc).class = UAC_OUTPUT;
	MIX(sc).type = MIX_SIGNED_8;
	MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
	MIX(sc).name = "effect_vol";
	MIX(sc).nchan = 1;
	MIX(sc).update[0] = 1;
	MIX(sc).minval = 0;
	MIX(sc).maxval = 0x7f;
	MIX(sc).mul = 0x7f;
	MIX(sc).nchan = 1;
	MIX(sc).update[0] = 1;
	strlcpy(MIX(sc).desc, "Effect Volume", sizeof(MIX(sc).desc));
	uaudio_mixer_add_ctl_sub(sc, &MIX(sc));

	memset(&MIX(sc), 0, sizeof(MIX(sc)));
	MIX(sc).wIndex = MAKE_WORD(6, sc->sc_mixer_iface_no);
	MIX(sc).wValue[0] = MAKE_WORD(3, 0);
	MIX(sc).class = UAC_OUTPUT;
	MIX(sc).type = MIX_SIGNED_16;
	MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
	MIX(sc).name = "effect_dur";
	MIX(sc).nchan = 1;
	MIX(sc).update[0] = 1;
	MIX(sc).minval = 0;
	MIX(sc).maxval = 0x7f00;
	MIX(sc).mul = 0x7f00;
	MIX(sc).nchan = 1;
	MIX(sc).update[0] = 1;
	strlcpy(MIX(sc).desc, "Effect Duration", sizeof(MIX(sc).desc));
	uaudio_mixer_add_ctl_sub(sc, &MIX(sc));

	memset(&MIX(sc), 0, sizeof(MIX(sc)));
	MIX(sc).wIndex = MAKE_WORD(6, sc->sc_mixer_iface_no);
	MIX(sc).wValue[0] = MAKE_WORD(4, 0);
	MIX(sc).class = UAC_OUTPUT;
	MIX(sc).type = MIX_SIGNED_8;
	MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
	MIX(sc).name = "effect_fb";
	MIX(sc).nchan = 1;
	MIX(sc).update[0] = 1;
	MIX(sc).minval = 0;
	MIX(sc).maxval = 0x7f;
	MIX(sc).mul = 0x7f;
	MIX(sc).nchan = 1;
	MIX(sc).update[0] = 1;
	strlcpy(MIX(sc).desc, "Effect Feedback Volume", sizeof(MIX(sc).desc));
	uaudio_mixer_add_ctl_sub(sc, &MIX(sc));

	memset(&MIX(sc), 0, sizeof(MIX(sc)));
	MIX(sc).wIndex = MAKE_WORD(7, sc->sc_mixer_iface_no);
	for (chy = 0; chy != 4; chy++) {

		MIX(sc).wValue[0] = MAKE_WORD(7, chy + 1);
		MIX(sc).type = MIX_SIGNED_16;
		MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
		MIX(sc).name = "effect_ret";
		MIX(sc).nchan = 1;
		MIX(sc).update[0] = 1;
		snprintf(MIX(sc).desc, sizeof(MIX(sc).desc),
		    "Effect Return %d Volume", chy + 1);

		uaudio_mixer_add_ctl(sc, &MIX(sc));
	}

	memset(&MIX(sc), 0, sizeof(MIX(sc)));
	MIX(sc).wIndex = MAKE_WORD(5, sc->sc_mixer_iface_no);

	for (chy = 0; chy != 8; chy++) {
		MIX(sc).wValue[0] = MAKE_WORD(9, chy + 1);
		MIX(sc).type = MIX_SIGNED_16;
		MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
		MIX(sc).name = "effect_send";
		MIX(sc).nchan = 1;
		MIX(sc).update[0] = 1;
		snprintf(MIX(sc).desc, sizeof(MIX(sc).desc),
		    "Effect Send AIn%d Volume", chy + 1);

		uaudio_mixer_add_ctl(sc, &MIX(sc));

		MIX(sc).wValue[0] = MAKE_WORD(9, chy + 1 + 8);
		MIX(sc).type = MIX_SIGNED_16;
		MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
		MIX(sc).name = "effect_send";
		MIX(sc).nchan = 1;
		MIX(sc).update[0] = 1;
		snprintf(MIX(sc).desc, sizeof(MIX(sc).desc),
		    "Effect Send DIn%d Volume", chy + 1);

		uaudio_mixer_add_ctl(sc, &MIX(sc));
	}
}

static void
uaudio_mixer_reload_all(struct uaudio_softc *sc)
{
	struct uaudio_mixer_node *pmc;
	int chan;

	if (sc->sc_mixer_lock == NULL)
		return;

	mtx_lock(sc->sc_mixer_lock);
	for (pmc = sc->sc_mixer_root; pmc != NULL; pmc = pmc->next) {
		/* use reset defaults for non-oss controlled settings */
		if (pmc->ctl == SOUND_MIXER_NRDEVICES)
			continue;
		for (chan = 0; chan < pmc->nchan; chan++)
			pmc->update[chan / 8] |= (1 << (chan % 8));
	}
	usbd_transfer_start(sc->sc_mixer_xfer[0]);

	/* start HID volume keys, if any */
	usbd_transfer_start(sc->sc_hid.xfer[0]);
	mtx_unlock(sc->sc_mixer_lock);
}

static void
uaudio_mixer_add_ctl_sub(struct uaudio_softc *sc, struct uaudio_mixer_node *mc)
{
	struct uaudio_mixer_node *p_mc_new =
	    malloc(sizeof(*p_mc_new), M_USBDEV, M_WAITOK);
	int ch;

	if (p_mc_new != NULL) {
		memcpy(p_mc_new, mc, sizeof(*p_mc_new));
		p_mc_new->next = sc->sc_mixer_root;
		sc->sc_mixer_root = p_mc_new;
		sc->sc_mixer_count++;

		/* set default value for all channels */
		for (ch = 0; ch < p_mc_new->nchan; ch++) {
			switch (p_mc_new->val_default) {
			case 1:
				/* 50% */
				p_mc_new->wData[ch] = (p_mc_new->maxval + p_mc_new->minval) / 2;
				break;
			case 2:
				/* 100% */
				p_mc_new->wData[ch] = p_mc_new->maxval;
				break;
			default:
				/* 0% */
				p_mc_new->wData[ch] = p_mc_new->minval;
				break;
			}
		}
	} else {
		DPRINTF("out of memory\n");
	}
}

static void
uaudio_mixer_add_ctl(struct uaudio_softc *sc, struct uaudio_mixer_node *mc)
{
	int32_t res;

	if (mc->class < UAC_NCLASSES) {
		DPRINTF("adding %s.%d\n",
		    uac_names[mc->class], mc->ctl);
	} else {
		DPRINTF("adding %d\n", mc->ctl);
	}

	if (mc->type == MIX_ON_OFF) {
		mc->minval = 0;
		mc->maxval = 1;
	} else if (mc->type == MIX_SELECTOR) {
	} else {

		/* determine min and max values */

		mc->minval = uaudio_mixer_get(sc->sc_udev,
		    sc->sc_audio_rev, GET_MIN, mc);
		mc->maxval = uaudio_mixer_get(sc->sc_udev,
		    sc->sc_audio_rev, GET_MAX, mc);

		/* check if max and min was swapped */

		if (mc->maxval < mc->minval) {
			res = mc->maxval;
			mc->maxval = mc->minval;
			mc->minval = res;
		}

		/* compute value range */
		mc->mul = mc->maxval - mc->minval;
		if (mc->mul == 0)
			mc->mul = 1;

		/* compute value alignment */
		res = uaudio_mixer_get(sc->sc_udev,
		    sc->sc_audio_rev, GET_RES, mc);

		DPRINTF("Resolution = %d\n", (int)res);
	}

	uaudio_mixer_add_ctl_sub(sc, mc);

#ifdef USB_DEBUG
	if (uaudio_debug > 2) {
		uint8_t i;

		for (i = 0; i < mc->nchan; i++) {
			DPRINTF("[mix] wValue=%04x\n", mc->wValue[0]);
		}
		DPRINTF("[mix] wIndex=%04x type=%d ctl='%d' "
		    "min=%d max=%d\n",
		    mc->wIndex, mc->type, mc->ctl,
		    mc->minval, mc->maxval);
	}
#endif
}

static void
uaudio_mixer_add_mixer(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio_mixer_unit_0 *d0 = iot[id].u.mu_v1;
	const struct usb_audio_mixer_unit_1 *d1;

	uint32_t bno;			/* bit number */
	uint32_t p;			/* bit number accumulator */
	uint32_t mo;			/* matching outputs */
	uint32_t mc;			/* matching channels */
	uint32_t ichs;			/* input channels */
	uint32_t ochs;			/* output channels */
	uint32_t c;
	uint32_t chs;			/* channels */
	uint32_t i;
	uint32_t o;

	DPRINTFN(3, "bUnitId=%d bNrInPins=%d\n",
	    d0->bUnitId, d0->bNrInPins);

	/* compute the number of input channels */

	ichs = 0;
	for (i = 0; i < d0->bNrInPins; i++) {
		ichs += uaudio_mixer_get_cluster(
		    d0->baSourceId[i], iot).bNrChannels;
	}

	d1 = (const void *)(d0->baSourceId + d0->bNrInPins);

	/* and the number of output channels */

	ochs = d1->bNrChannels;

	DPRINTFN(3, "ichs=%d ochs=%d\n", ichs, ochs);

	memset(&MIX(sc), 0, sizeof(MIX(sc)));

	MIX(sc).wIndex = MAKE_WORD(d0->bUnitId, sc->sc_mixer_iface_no);
	uaudio_mixer_determine_class(&iot[id], &MIX(sc));
	MIX(sc).type = MIX_SIGNED_16;

	if (uaudio_mixer_verify_desc(d0, ((ichs * ochs) + 7) / 8) == NULL)
		return;

	for (p = i = 0; i < d0->bNrInPins; i++) {
		chs = uaudio_mixer_get_cluster(
		    d0->baSourceId[i], iot).bNrChannels;
		mc = 0;
		for (c = 0; c < chs; c++) {
			mo = 0;
			for (o = 0; o < ochs; o++) {
				bno = ((p + c) * ochs) + o;
				if (BIT_TEST(d1->bmControls, bno))
					mo++;
			}
			if (mo == 1)
				mc++;
		}
		if ((mc == chs) && (chs <= MIX_MAX_CHAN)) {

			/* repeat bit-scan */

			mc = 0;
			for (c = 0; c < chs; c++) {
				for (o = 0; o < ochs; o++) {
					bno = ((p + c) * ochs) + o;
					if (BIT_TEST(d1->bmControls, bno))
						MIX(sc).wValue[mc++] = MAKE_WORD(p + c + 1, o + 1);
				}
			}
			MIX(sc).nchan = chs;
			uaudio_mixer_add_ctl(sc, &MIX(sc));
		}
		p += chs;
	}
}

static void
uaudio20_mixer_add_mixer(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio20_mixer_unit_0 *d0 = iot[id].u.mu_v2;
	const struct usb_audio20_mixer_unit_1 *d1;

	uint32_t bno;			/* bit number */
	uint32_t p;			/* bit number accumulator */
	uint32_t mo;			/* matching outputs */
	uint32_t mc;			/* matching channels */
	uint32_t ichs;			/* input channels */
	uint32_t ochs;			/* output channels */
	uint32_t c;
	uint32_t chs;			/* channels */
	uint32_t i;
	uint32_t o;

	DPRINTFN(3, "bUnitId=%d bNrInPins=%d\n",
	    d0->bUnitId, d0->bNrInPins);

	/* compute the number of input channels */

	ichs = 0;
	for (i = 0; i < d0->bNrInPins; i++) {
		ichs += uaudio20_mixer_get_cluster(
		    d0->baSourceId[i], iot).bNrChannels;
	}

	d1 = (const void *)(d0->baSourceId + d0->bNrInPins);

	/* and the number of output channels */

	ochs = d1->bNrChannels;

	DPRINTFN(3, "ichs=%d ochs=%d\n", ichs, ochs);

	memset(&MIX(sc), 0, sizeof(MIX(sc)));

	MIX(sc).wIndex = MAKE_WORD(d0->bUnitId, sc->sc_mixer_iface_no);
	uaudio20_mixer_determine_class(&iot[id], &MIX(sc));
	MIX(sc).type = MIX_SIGNED_16;

	if (uaudio20_mixer_verify_desc(d0, ((ichs * ochs) + 7) / 8) == NULL)
		return;

	for (p = i = 0; i < d0->bNrInPins; i++) {
		chs = uaudio20_mixer_get_cluster(
		    d0->baSourceId[i], iot).bNrChannels;
		mc = 0;
		for (c = 0; c < chs; c++) {
			mo = 0;
			for (o = 0; o < ochs; o++) {
				bno = ((p + c) * ochs) + o;
				if (BIT_TEST(d1->bmControls, bno))
					mo++;
			}
			if (mo == 1)
				mc++;
		}
		if ((mc == chs) && (chs <= MIX_MAX_CHAN)) {

			/* repeat bit-scan */

			mc = 0;
			for (c = 0; c < chs; c++) {
				for (o = 0; o < ochs; o++) {
					bno = ((p + c) * ochs) + o;
					if (BIT_TEST(d1->bmControls, bno))
						MIX(sc).wValue[mc++] = MAKE_WORD(p + c + 1, o + 1);
				}
			}
			MIX(sc).nchan = chs;
			uaudio_mixer_add_ctl(sc, &MIX(sc));
		}
		p += chs;
	}
}

static void
uaudio_mixer_add_selector(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio_selector_unit *d = iot[id].u.su_v1;
	uint16_t i;

	DPRINTFN(3, "bUnitId=%d bNrInPins=%d\n",
	    d->bUnitId, d->bNrInPins);

	if (d->bNrInPins == 0)
		return;

	memset(&MIX(sc), 0, sizeof(MIX(sc)));

	MIX(sc).wIndex = MAKE_WORD(d->bUnitId, sc->sc_mixer_iface_no);
	MIX(sc).wValue[0] = MAKE_WORD(0, 0);
	uaudio_mixer_determine_class(&iot[id], &MIX(sc));
	MIX(sc).nchan = 1;
	MIX(sc).type = MIX_SELECTOR;
	MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
	MIX(sc).minval = 1;
	MIX(sc).maxval = d->bNrInPins;
	MIX(sc).name = "selector";

	i = d->baSourceId[d->bNrInPins];
	if (i == 0 ||
	    usbd_req_get_string_any(sc->sc_udev, NULL,
	    MIX(sc).desc, sizeof(MIX(sc).desc), i) != 0) {
		MIX(sc).desc[0] = 0;
	}

	if (MIX(sc).maxval > MAX_SELECTOR_INPUT_PIN) {
		MIX(sc).maxval = MAX_SELECTOR_INPUT_PIN;
	}
	MIX(sc).mul = (MIX(sc).maxval - MIX(sc).minval);
	for (i = 0; i < MAX_SELECTOR_INPUT_PIN; i++) {
		MIX(sc).slctrtype[i] = SOUND_MIXER_NRDEVICES;
	}

	for (i = 0; i < MIX(sc).maxval; i++) {
		MIX(sc).slctrtype[i] = uaudio_mixer_feature_name(
		    &iot[d->baSourceId[i]], &MIX(sc));
	}

	MIX(sc).class = 0;			/* not used */

	uaudio_mixer_add_ctl(sc, &MIX(sc));
}

static void
uaudio20_mixer_add_selector(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio20_selector_unit *d = iot[id].u.su_v2;
	uint16_t i;

	DPRINTFN(3, "bUnitId=%d bNrInPins=%d\n",
	    d->bUnitId, d->bNrInPins);

	if (d->bNrInPins == 0)
		return;

	memset(&MIX(sc), 0, sizeof(MIX(sc)));

	MIX(sc).wIndex = MAKE_WORD(d->bUnitId, sc->sc_mixer_iface_no);
	MIX(sc).wValue[0] = MAKE_WORD(0, 0);
	uaudio20_mixer_determine_class(&iot[id], &MIX(sc));
	MIX(sc).nchan = 1;
	MIX(sc).type = MIX_SELECTOR;
	MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
	MIX(sc).minval = 1;
	MIX(sc).maxval = d->bNrInPins;
	MIX(sc).name = "selector";

	i = d->baSourceId[d->bNrInPins];
	if (i == 0 ||
	    usbd_req_get_string_any(sc->sc_udev, NULL,
	    MIX(sc).desc, sizeof(MIX(sc).desc), i) != 0) {
		MIX(sc).desc[0] = 0;
	}

	if (MIX(sc).maxval > MAX_SELECTOR_INPUT_PIN)
		MIX(sc).maxval = MAX_SELECTOR_INPUT_PIN;

	MIX(sc).mul = (MIX(sc).maxval - MIX(sc).minval);
	for (i = 0; i < MAX_SELECTOR_INPUT_PIN; i++)
		MIX(sc).slctrtype[i] = SOUND_MIXER_NRDEVICES;

	for (i = 0; i < MIX(sc).maxval; i++) {
		MIX(sc).slctrtype[i] = uaudio20_mixer_feature_name(
		    &iot[d->baSourceId[i]], &MIX(sc));
	}

	MIX(sc).class = 0;			/* not used */

	uaudio_mixer_add_ctl(sc, &MIX(sc));
}

static uint32_t
uaudio_mixer_feature_get_bmaControls(const struct usb_audio_feature_unit *d,
    uint8_t i)
{
	uint32_t temp = 0;
	uint32_t offset = (i * d->bControlSize);

	if (d->bControlSize > 0) {
		temp |= d->bmaControls[offset];
		if (d->bControlSize > 1) {
			temp |= d->bmaControls[offset + 1] << 8;
			if (d->bControlSize > 2) {
				temp |= d->bmaControls[offset + 2] << 16;
				if (d->bControlSize > 3) {
					temp |= d->bmaControls[offset + 3] << 24;
				}
			}
		}
	}
	return (temp);
}

static void
uaudio_mixer_add_feature(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio_feature_unit *d = iot[id].u.fu_v1;
	uint32_t fumask;
	uint32_t mmask;
	uint32_t cmask;
	uint16_t mixernumber;
	uint8_t nchan;
	uint8_t chan;
	uint8_t ctl;
	uint8_t i;

	if (d->bControlSize == 0)
		return;

	memset(&MIX(sc), 0, sizeof(MIX(sc)));

	nchan = (d->bLength - 7) / d->bControlSize;
	mmask = uaudio_mixer_feature_get_bmaControls(d, 0);
	cmask = 0;

	if (nchan == 0)
		return;

	/* figure out what we can control */

	for (chan = 1; chan < nchan; chan++) {
		DPRINTFN(10, "chan=%d mask=%x\n",
		    chan, uaudio_mixer_feature_get_bmaControls(d, chan));

		cmask |= uaudio_mixer_feature_get_bmaControls(d, chan);
	}

	if (nchan > MIX_MAX_CHAN) {
		nchan = MIX_MAX_CHAN;
	}
	MIX(sc).wIndex = MAKE_WORD(d->bUnitId, sc->sc_mixer_iface_no);

	i = d->bmaControls[d->bControlSize];
	if (i == 0 ||
	    usbd_req_get_string_any(sc->sc_udev, NULL,
	    MIX(sc).desc, sizeof(MIX(sc).desc), i) != 0) {
		MIX(sc).desc[0] = 0;
	}

	for (ctl = 1; ctl <= LOUDNESS_CONTROL; ctl++) {

		fumask = FU_MASK(ctl);

		DPRINTFN(5, "ctl=%d fumask=0x%04x\n",
		    ctl, fumask);

		if (mmask & fumask) {
			MIX(sc).nchan = 1;
			MIX(sc).wValue[0] = MAKE_WORD(ctl, 0);
		} else if (cmask & fumask) {
			MIX(sc).nchan = nchan - 1;
			for (i = 1; i < nchan; i++) {
				if (uaudio_mixer_feature_get_bmaControls(d, i) & fumask)
					MIX(sc).wValue[i - 1] = MAKE_WORD(ctl, i);
				else
					MIX(sc).wValue[i - 1] = -1;
			}
		} else {
			continue;
		}

		mixernumber = uaudio_mixer_feature_name(&iot[id], &MIX(sc));

		switch (ctl) {
		case MUTE_CONTROL:
			MIX(sc).type = MIX_ON_OFF;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
			MIX(sc).name = "mute";
			break;

		case VOLUME_CONTROL:
			MIX(sc).type = MIX_SIGNED_16;
			MIX(sc).ctl = mixernumber;
			MIX(sc).name = "vol";
			break;

		case BASS_CONTROL:
			MIX(sc).type = MIX_SIGNED_8;
			MIX(sc).ctl = SOUND_MIXER_BASS;
			MIX(sc).name = "bass";
			break;

		case MID_CONTROL:
			MIX(sc).type = MIX_SIGNED_8;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			MIX(sc).name = "mid";
			break;

		case TREBLE_CONTROL:
			MIX(sc).type = MIX_SIGNED_8;
			MIX(sc).ctl = SOUND_MIXER_TREBLE;
			MIX(sc).name = "treble";
			break;

		case GRAPHIC_EQUALIZER_CONTROL:
			continue;	/* XXX don't add anything */

		case AGC_CONTROL:
			MIX(sc).type = MIX_ON_OFF;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			MIX(sc).name = "agc";
			break;

		case DELAY_CONTROL:
			MIX(sc).type = MIX_UNSIGNED_16;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			MIX(sc).name = "delay";
			break;

		case BASS_BOOST_CONTROL:
			MIX(sc).type = MIX_ON_OFF;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			MIX(sc).name = "boost";
			break;

		case LOUDNESS_CONTROL:
			MIX(sc).type = MIX_ON_OFF;
			MIX(sc).ctl = SOUND_MIXER_LOUD;	/* Is this correct ? */
			MIX(sc).name = "loudness";
			break;

		default:
			MIX(sc).type = MIX_UNKNOWN;
			break;
		}

		if (MIX(sc).type != MIX_UNKNOWN)
			uaudio_mixer_add_ctl(sc, &MIX(sc));
	}
}

static void
uaudio20_mixer_add_feature(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio20_feature_unit *d = iot[id].u.fu_v2;
	uint32_t ctl;
	uint32_t mmask;
	uint32_t cmask;
	uint16_t mixernumber;
	uint8_t nchan;
	uint8_t chan;
	uint8_t i;
	uint8_t what;

	if (UGETDW(d->bmaControls[0]) == 0)
		return;

	memset(&MIX(sc), 0, sizeof(MIX(sc)));

	nchan = (d->bLength - 6) / 4;
	mmask = UGETDW(d->bmaControls[0]);
	cmask = 0;

	if (nchan == 0)
		return;

	/* figure out what we can control */

	for (chan = 1; chan < nchan; chan++)
		cmask |= UGETDW(d->bmaControls[chan]);

	if (nchan > MIX_MAX_CHAN)
		nchan = MIX_MAX_CHAN;

	MIX(sc).wIndex = MAKE_WORD(d->bUnitId, sc->sc_mixer_iface_no);

	i = d->bmaControls[nchan][0];
	if (i == 0 ||
	    usbd_req_get_string_any(sc->sc_udev, NULL,
	    MIX(sc).desc, sizeof(MIX(sc).desc), i) != 0) {
		MIX(sc).desc[0] = 0;
	}

	for (ctl = 3; ctl != 0; ctl <<= 2) {

		mixernumber = uaudio20_mixer_feature_name(&iot[id], &MIX(sc));

		switch (ctl) {
		case (3 << 0):
			MIX(sc).type = MIX_ON_OFF;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;
			MIX(sc).name = "mute";
			what = MUTE_CONTROL;
			break;
		case (3 << 2): 
			MIX(sc).type = MIX_SIGNED_16;
			MIX(sc).ctl = mixernumber;
			MIX(sc).name = "vol";
			what = VOLUME_CONTROL;
			break;
		case (3 << 4):
			MIX(sc).type = MIX_SIGNED_8;
			MIX(sc).ctl = SOUND_MIXER_BASS;
			MIX(sc).name = "bass";
			what = BASS_CONTROL;
			break;
		case (3 << 6):
			MIX(sc).type = MIX_SIGNED_8;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			MIX(sc).name = "mid";
			what = MID_CONTROL;
			break;
		case (3 << 8):
			MIX(sc).type = MIX_SIGNED_8;
			MIX(sc).ctl = SOUND_MIXER_TREBLE;
			MIX(sc).name = "treble";
			what = TREBLE_CONTROL;
			break;
		case (3 << 12):
			MIX(sc).type = MIX_ON_OFF;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			MIX(sc).name = "agc";
			what = AGC_CONTROL;
			break;
		case (3 << 14):
			MIX(sc).type = MIX_UNSIGNED_16;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			MIX(sc).name = "delay";
			what = DELAY_CONTROL;
			break;
		case (3 << 16):
			MIX(sc).type = MIX_ON_OFF;
			MIX(sc).ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			MIX(sc).name = "boost";
			what = BASS_BOOST_CONTROL;
			break;
		case (3 << 18):
			MIX(sc).type = MIX_ON_OFF;
			MIX(sc).ctl = SOUND_MIXER_LOUD;	/* Is this correct ? */
			MIX(sc).name = "loudness";
			what = LOUDNESS_CONTROL;
			break;
		case (3 << 20):
			MIX(sc).type = MIX_SIGNED_16;
			MIX(sc).ctl = mixernumber;
			MIX(sc).name = "igain";
			what = INPUT_GAIN_CONTROL;
			break;
		case (3 << 22):
			MIX(sc).type = MIX_SIGNED_16;
			MIX(sc).ctl = mixernumber;
			MIX(sc).name = "igainpad";
			what = INPUT_GAIN_PAD_CONTROL;
			break;
		default:
			continue;
		}

		if ((mmask & ctl) == ctl) {
			MIX(sc).nchan = 1;
			MIX(sc).wValue[0] = MAKE_WORD(what, 0);
		} else if ((cmask & ctl) == ctl) {
			MIX(sc).nchan = nchan - 1;
			for (i = 1; i < nchan; i++) {
				if ((UGETDW(d->bmaControls[i]) & ctl) == ctl)
					MIX(sc).wValue[i - 1] = MAKE_WORD(what, i);
				else
					MIX(sc).wValue[i - 1] = -1;
			}
		} else {
			continue;
		}

		if (MIX(sc).type != MIX_UNKNOWN)
			uaudio_mixer_add_ctl(sc, &MIX(sc));
	}
}

static void
uaudio_mixer_add_processing_updown(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio_processing_unit_0 *d0 = iot[id].u.pu_v1;
	const struct usb_audio_processing_unit_1 *d1 =
	    (const void *)(d0->baSourceId + d0->bNrInPins);
	const struct usb_audio_processing_unit_updown *ud =
	    (const void *)(d1->bmControls + d1->bControlSize);
	uint8_t i;

	if (uaudio_mixer_verify_desc(d0, sizeof(*ud)) == NULL) {
		return;
	}
	if (uaudio_mixer_verify_desc(d0, sizeof(*ud) + (2 * ud->bNrModes))
	    == NULL) {
		return;
	}
	DPRINTFN(3, "bUnitId=%d bNrModes=%d\n",
	    d0->bUnitId, ud->bNrModes);

	if (!(d1->bmControls[0] & UA_PROC_MASK(UD_MODE_SELECT_CONTROL))) {
		DPRINTF("no mode select\n");
		return;
	}
	memset(&MIX(sc), 0, sizeof(MIX(sc)));

	MIX(sc).wIndex = MAKE_WORD(d0->bUnitId, sc->sc_mixer_iface_no);
	MIX(sc).nchan = 1;
	MIX(sc).wValue[0] = MAKE_WORD(UD_MODE_SELECT_CONTROL, 0);
	uaudio_mixer_determine_class(&iot[id], &MIX(sc));
	MIX(sc).type = MIX_ON_OFF;		/* XXX */

	for (i = 0; i < ud->bNrModes; i++) {
		DPRINTFN(3, "i=%d bm=0x%x\n", i, UGETW(ud->waModes[i]));
		/* XXX */
	}

	uaudio_mixer_add_ctl(sc, &MIX(sc));
}

static void
uaudio_mixer_add_processing(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio_processing_unit_0 *d0 = iot[id].u.pu_v1;
	const struct usb_audio_processing_unit_1 *d1 =
	    (const void *)(d0->baSourceId + d0->bNrInPins);
	uint16_t ptype;

	memset(&MIX(sc), 0, sizeof(MIX(sc)));

	ptype = UGETW(d0->wProcessType);

	DPRINTFN(3, "wProcessType=%d bUnitId=%d "
	    "bNrInPins=%d\n", ptype, d0->bUnitId, d0->bNrInPins);

	if (d1->bControlSize == 0) {
		return;
	}
	if (d1->bmControls[0] & UA_PROC_ENABLE_MASK) {
		MIX(sc).wIndex = MAKE_WORD(d0->bUnitId, sc->sc_mixer_iface_no);
		MIX(sc).nchan = 1;
		MIX(sc).wValue[0] = MAKE_WORD(XX_ENABLE_CONTROL, 0);
		uaudio_mixer_determine_class(&iot[id], &MIX(sc));
		MIX(sc).type = MIX_ON_OFF;
		uaudio_mixer_add_ctl(sc, &MIX(sc));
	}
	switch (ptype) {
	case UPDOWNMIX_PROCESS:
		uaudio_mixer_add_processing_updown(sc, iot, id);
		break;

	case DOLBY_PROLOGIC_PROCESS:
	case P3D_STEREO_EXTENDER_PROCESS:
	case REVERBATION_PROCESS:
	case CHORUS_PROCESS:
	case DYN_RANGE_COMP_PROCESS:
	default:
		DPRINTF("unit %d, type=%d is not implemented\n",
		    d0->bUnitId, ptype);
		break;
	}
}

static void
uaudio_mixer_add_extension(struct uaudio_softc *sc,
    const struct uaudio_terminal_node *iot, int id)
{
	const struct usb_audio_extension_unit_0 *d0 = iot[id].u.eu_v1;
	const struct usb_audio_extension_unit_1 *d1 =
	    (const void *)(d0->baSourceId + d0->bNrInPins);

	DPRINTFN(3, "bUnitId=%d bNrInPins=%d\n",
	    d0->bUnitId, d0->bNrInPins);

	if (sc->sc_uq_au_no_xu) {
		return;
	}
	if (d1->bControlSize == 0) {
		return;
	}
	if (d1->bmControls[0] & UA_EXT_ENABLE_MASK) {

		memset(&MIX(sc), 0, sizeof(MIX(sc)));

		MIX(sc).wIndex = MAKE_WORD(d0->bUnitId, sc->sc_mixer_iface_no);
		MIX(sc).nchan = 1;
		MIX(sc).wValue[0] = MAKE_WORD(UA_EXT_ENABLE, 0);
		uaudio_mixer_determine_class(&iot[id], &MIX(sc));
		MIX(sc).type = MIX_ON_OFF;

		uaudio_mixer_add_ctl(sc, &MIX(sc));
	}
}

static const void *
uaudio_mixer_verify_desc(const void *arg, uint32_t len)
{
	const struct usb_audio_mixer_unit_1 *d1;
	const struct usb_audio_extension_unit_1 *e1;
	const struct usb_audio_processing_unit_1 *u1;

	union {
		const struct usb_descriptor *desc;
		const struct usb_audio_input_terminal *it;
		const struct usb_audio_output_terminal *ot;
		const struct usb_audio_mixer_unit_0 *mu;
		const struct usb_audio_selector_unit *su;
		const struct usb_audio_feature_unit *fu;
		const struct usb_audio_processing_unit_0 *pu;
		const struct usb_audio_extension_unit_0 *eu;
	}     u;

	u.desc = arg;

	if (u.desc == NULL) {
		goto error;
	}
	if (u.desc->bDescriptorType != UDESC_CS_INTERFACE) {
		goto error;
	}
	switch (u.desc->bDescriptorSubtype) {
	case UDESCSUB_AC_INPUT:
		len += sizeof(*u.it);
		break;

	case UDESCSUB_AC_OUTPUT:
		len += sizeof(*u.ot);
		break;

	case UDESCSUB_AC_MIXER:
		len += sizeof(*u.mu);

		if (u.desc->bLength < len) {
			goto error;
		}
		len += u.mu->bNrInPins;

		if (u.desc->bLength < len) {
			goto error;
		}
		d1 = (const void *)(u.mu->baSourceId + u.mu->bNrInPins);

		len += sizeof(*d1);
		break;

	case UDESCSUB_AC_SELECTOR:
		len += sizeof(*u.su);

		if (u.desc->bLength < len) {
			goto error;
		}
		len += u.su->bNrInPins + 1;
		break;

	case UDESCSUB_AC_FEATURE:
		len += sizeof(*u.fu) + 1;

		if (u.desc->bLength < len)
			goto error;

		len += u.fu->bControlSize;
		break;

	case UDESCSUB_AC_PROCESSING:
		len += sizeof(*u.pu);

		if (u.desc->bLength < len) {
			goto error;
		}
		len += u.pu->bNrInPins;

		if (u.desc->bLength < len) {
			goto error;
		}
		u1 = (const void *)(u.pu->baSourceId + u.pu->bNrInPins);

		len += sizeof(*u1);

		if (u.desc->bLength < len) {
			goto error;
		}
		len += u1->bControlSize;

		break;

	case UDESCSUB_AC_EXTENSION:
		len += sizeof(*u.eu);

		if (u.desc->bLength < len) {
			goto error;
		}
		len += u.eu->bNrInPins;

		if (u.desc->bLength < len) {
			goto error;
		}
		e1 = (const void *)(u.eu->baSourceId + u.eu->bNrInPins);

		len += sizeof(*e1);

		if (u.desc->bLength < len) {
			goto error;
		}
		len += e1->bControlSize;
		break;

	default:
		goto error;
	}

	if (u.desc->bLength < len) {
		goto error;
	}
	return (u.desc);

error:
	if (u.desc) {
		DPRINTF("invalid descriptor, type=%d, "
		    "sub_type=%d, len=%d of %d bytes\n",
		    u.desc->bDescriptorType,
		    u.desc->bDescriptorSubtype,
		    u.desc->bLength, len);
	}
	return (NULL);
}

static const void *
uaudio20_mixer_verify_desc(const void *arg, uint32_t len)
{
	const struct usb_audio20_mixer_unit_1 *d1;
	const struct usb_audio20_extension_unit_1 *e1;
	const struct usb_audio20_processing_unit_1 *u1;
	const struct usb_audio20_clock_selector_unit_1 *c1;

	union {
		const struct usb_descriptor *desc;
		const struct usb_audio20_clock_source_unit *csrc;
		const struct usb_audio20_clock_selector_unit_0 *csel;
		const struct usb_audio20_clock_multiplier_unit *cmul;
		const struct usb_audio20_input_terminal *it;
		const struct usb_audio20_output_terminal *ot;
		const struct usb_audio20_mixer_unit_0 *mu;
		const struct usb_audio20_selector_unit *su;
		const struct usb_audio20_feature_unit *fu;
		const struct usb_audio20_sample_rate_unit *ru;
		const struct usb_audio20_processing_unit_0 *pu;
		const struct usb_audio20_extension_unit_0 *eu;
		const struct usb_audio20_effect_unit *ef;
	}     u;

	u.desc = arg;

	if (u.desc == NULL)
		goto error;

	if (u.desc->bDescriptorType != UDESC_CS_INTERFACE)
		goto error;

	switch (u.desc->bDescriptorSubtype) {
	case UDESCSUB_AC_INPUT:
		len += sizeof(*u.it);
		break;

	case UDESCSUB_AC_OUTPUT:
		len += sizeof(*u.ot);
		break;

	case UDESCSUB_AC_MIXER:
		len += sizeof(*u.mu);

		if (u.desc->bLength < len)
			goto error;
		len += u.mu->bNrInPins;

		if (u.desc->bLength < len)
			goto error;

		d1 = (const void *)(u.mu->baSourceId + u.mu->bNrInPins);

		len += sizeof(*d1) + d1->bNrChannels;
		break;

	case UDESCSUB_AC_SELECTOR:
		len += sizeof(*u.su);

		if (u.desc->bLength < len)
			goto error;

		len += u.su->bNrInPins + 1;
		break;

	case UDESCSUB_AC_FEATURE:
		len += sizeof(*u.fu) + 1;
		break;

	case UDESCSUB_AC_EFFECT:
		len += sizeof(*u.ef) + 4;
		break;

	case UDESCSUB_AC_PROCESSING_V2:
		len += sizeof(*u.pu);

		if (u.desc->bLength < len)
			goto error;

		len += u.pu->bNrInPins;

		if (u.desc->bLength < len)
			goto error;

		u1 = (const void *)(u.pu->baSourceId + u.pu->bNrInPins);

		len += sizeof(*u1);
		break;

	case UDESCSUB_AC_EXTENSION_V2:
		len += sizeof(*u.eu);

		if (u.desc->bLength < len)
			goto error;

		len += u.eu->bNrInPins;

		if (u.desc->bLength < len)
			goto error;

		e1 = (const void *)(u.eu->baSourceId + u.eu->bNrInPins);

		len += sizeof(*e1);
		break;

	case UDESCSUB_AC_CLOCK_SRC:
		len += sizeof(*u.csrc);
		break;

	case UDESCSUB_AC_CLOCK_SEL:
		len += sizeof(*u.csel);

		if (u.desc->bLength < len)
			goto error;

		len += u.csel->bNrInPins;

		if (u.desc->bLength < len)
			goto error;

		c1 = (const void *)(u.csel->baCSourceId + u.csel->bNrInPins);

		len += sizeof(*c1);
		break;

	case UDESCSUB_AC_CLOCK_MUL:
		len += sizeof(*u.cmul);
		break;

	case UDESCSUB_AC_SAMPLE_RT:
		len += sizeof(*u.ru);
		break;

	default:
		goto error;
	}

	if (u.desc->bLength < len)
		goto error;

	return (u.desc);

error:
	if (u.desc) {
		DPRINTF("invalid descriptor, type=%d, "
		    "sub_type=%d, len=%d of %d bytes\n",
		    u.desc->bDescriptorType,
		    u.desc->bDescriptorSubtype,
		    u.desc->bLength, len);
	}
	return (NULL);
}

static struct usb_audio_cluster
uaudio_mixer_get_cluster(uint8_t id, const struct uaudio_terminal_node *iot)
{
	struct usb_audio_cluster r;
	const struct usb_descriptor *dp;
	uint8_t i;

	for (i = 0; i < UAUDIO_RECURSE_LIMIT; i++) {	/* avoid infinite loops */
		dp = iot[id].u.desc;
		if (dp == NULL) {
			goto error;
		}
		switch (dp->bDescriptorSubtype) {
		case UDESCSUB_AC_INPUT:
			r.bNrChannels = iot[id].u.it_v1->bNrChannels;
			r.wChannelConfig[0] = iot[id].u.it_v1->wChannelConfig[0];
			r.wChannelConfig[1] = iot[id].u.it_v1->wChannelConfig[1];
			r.iChannelNames = iot[id].u.it_v1->iChannelNames;
			goto done;

		case UDESCSUB_AC_OUTPUT:
			id = iot[id].u.ot_v1->bSourceId;
			break;

		case UDESCSUB_AC_MIXER:
			r = *(const struct usb_audio_cluster *)
			    &iot[id].u.mu_v1->baSourceId[
			    iot[id].u.mu_v1->bNrInPins];
			goto done;

		case UDESCSUB_AC_SELECTOR:
			if (iot[id].u.su_v1->bNrInPins > 0) {
				/* XXX This is not really right */
				id = iot[id].u.su_v1->baSourceId[0];
			}
			break;

		case UDESCSUB_AC_FEATURE:
			id = iot[id].u.fu_v1->bSourceId;
			break;

		case UDESCSUB_AC_PROCESSING:
			r = *((const struct usb_audio_cluster *)
			    &iot[id].u.pu_v1->baSourceId[
			    iot[id].u.pu_v1->bNrInPins]);
			goto done;

		case UDESCSUB_AC_EXTENSION:
			r = *((const struct usb_audio_cluster *)
			    &iot[id].u.eu_v1->baSourceId[
			    iot[id].u.eu_v1->bNrInPins]);
			goto done;

		default:
			goto error;
		}
	}
error:
	DPRINTF("bad data\n");
	memset(&r, 0, sizeof(r));
done:
	return (r);
}

static struct usb_audio20_cluster
uaudio20_mixer_get_cluster(uint8_t id, const struct uaudio_terminal_node *iot)
{
	struct usb_audio20_cluster r;
	const struct usb_descriptor *dp;
	uint8_t i;

	for (i = 0; i < UAUDIO_RECURSE_LIMIT; i++) {	/* avoid infinite loops */
		dp = iot[id].u.desc;
		if (dp == NULL)
			goto error;

		switch (dp->bDescriptorSubtype) {
		case UDESCSUB_AC_INPUT:
			r.bNrChannels = iot[id].u.it_v2->bNrChannels;
			r.bmChannelConfig[0] = iot[id].u.it_v2->bmChannelConfig[0];
			r.bmChannelConfig[1] = iot[id].u.it_v2->bmChannelConfig[1];
			r.bmChannelConfig[2] = iot[id].u.it_v2->bmChannelConfig[2];
			r.bmChannelConfig[3] = iot[id].u.it_v2->bmChannelConfig[3];
			r.iChannelNames = iot[id].u.it_v2->iTerminal;
			goto done;

		case UDESCSUB_AC_OUTPUT:
			id = iot[id].u.ot_v2->bSourceId;
			break;

		case UDESCSUB_AC_MIXER:
			r = *(const struct usb_audio20_cluster *)
			    &iot[id].u.mu_v2->baSourceId[
			    iot[id].u.mu_v2->bNrInPins];
			goto done;

		case UDESCSUB_AC_SELECTOR:
			if (iot[id].u.su_v2->bNrInPins > 0) {
				/* XXX This is not really right */
				id = iot[id].u.su_v2->baSourceId[0];
			}
			break;

		case UDESCSUB_AC_SAMPLE_RT:
			id = iot[id].u.ru_v2->bSourceId;
			break;

		case UDESCSUB_AC_EFFECT:
			id = iot[id].u.ef_v2->bSourceId;
			break;

		case UDESCSUB_AC_FEATURE:
			id = iot[id].u.fu_v2->bSourceId;
			break;

		case UDESCSUB_AC_PROCESSING_V2:
			r = *((const struct usb_audio20_cluster *)
			    &iot[id].u.pu_v2->baSourceId[
			    iot[id].u.pu_v2->bNrInPins]);
			goto done;

		case UDESCSUB_AC_EXTENSION_V2:
			r = *((const struct usb_audio20_cluster *)
			    &iot[id].u.eu_v2->baSourceId[
			    iot[id].u.eu_v2->bNrInPins]);
			goto done;

		default:
			goto error;
		}
	}
error:
	DPRINTF("Bad data!\n");
	memset(&r, 0, sizeof(r));
done:
	return (r);
}

static uint16_t
uaudio_mixer_determine_class(const struct uaudio_terminal_node *iot,
    struct uaudio_mixer_node *mix)
{
	uint16_t terminal_type = 0x0000;
	const struct uaudio_terminal_node *input[2];
	const struct uaudio_terminal_node *output[2];

	input[0] = uaudio_mixer_get_input(iot, 0);
	input[1] = uaudio_mixer_get_input(iot, 1);

	output[0] = uaudio_mixer_get_output(iot, 0);
	output[1] = uaudio_mixer_get_output(iot, 1);

	/*
	 * check if there is only
	 * one output terminal:
	 */
	if (output[0] && (!output[1])) {
		terminal_type =
		    UGETW(output[0]->u.ot_v1->wTerminalType);
	}
	/*
	 * If the only output terminal is USB,
	 * the class is UAC_RECORD.
	 */
	if ((terminal_type & 0xff00) == (UAT_UNDEFINED & 0xff00)) {

		mix->class = UAC_RECORD;
		if (input[0] && (!input[1])) {
			terminal_type =
			    UGETW(input[0]->u.it_v1->wTerminalType);
		} else {
			terminal_type = 0;
		}
		goto done;
	}
	/*
	 * if the unit is connected to just
	 * one input terminal, the
	 * class is UAC_INPUT:
	 */
	if (input[0] && (!input[1])) {
		mix->class = UAC_INPUT;
		terminal_type =
		    UGETW(input[0]->u.it_v1->wTerminalType);
		goto done;
	}
	/*
	 * Otherwise, the class is UAC_OUTPUT.
	 */
	mix->class = UAC_OUTPUT;
done:
	return (terminal_type);
}

static uint16_t
uaudio20_mixer_determine_class(const struct uaudio_terminal_node *iot,
    struct uaudio_mixer_node *mix)
{
	uint16_t terminal_type = 0x0000;
	const struct uaudio_terminal_node *input[2];
	const struct uaudio_terminal_node *output[2];

	input[0] = uaudio_mixer_get_input(iot, 0);
	input[1] = uaudio_mixer_get_input(iot, 1);

	output[0] = uaudio_mixer_get_output(iot, 0);
	output[1] = uaudio_mixer_get_output(iot, 1);

	/*
	 * check if there is only
	 * one output terminal:
	 */
	if (output[0] && (!output[1]))
		terminal_type = UGETW(output[0]->u.ot_v2->wTerminalType);
	/*
	 * If the only output terminal is USB,
	 * the class is UAC_RECORD.
	 */
	if ((terminal_type & 0xff00) == (UAT_UNDEFINED & 0xff00)) {

		mix->class = UAC_RECORD;
		if (input[0] && (!input[1])) {
			terminal_type =
			    UGETW(input[0]->u.it_v2->wTerminalType);
		} else {
			terminal_type = 0;
		}
		goto done;
	}
	/*
	 * if the unit is connected to just
	 * one input terminal, the
	 * class is UAC_INPUT:
	 */
	if (input[0] && (!input[1])) {
		mix->class = UAC_INPUT;
		terminal_type =
		    UGETW(input[0]->u.it_v2->wTerminalType);
		goto done;
	}
	/*
	 * Otherwise, the class is UAC_OUTPUT.
	 */
	mix->class = UAC_OUTPUT;
done:
	return (terminal_type);
}

struct uaudio_tt_to_feature {
	uint16_t terminal_type;
	uint16_t feature;
};

static const struct uaudio_tt_to_feature uaudio_tt_to_feature[] = {

	{UAT_STREAM, SOUND_MIXER_PCM},

	{UATI_MICROPHONE, SOUND_MIXER_MIC},
	{UATI_DESKMICROPHONE, SOUND_MIXER_MIC},
	{UATI_PERSONALMICROPHONE, SOUND_MIXER_MIC},
	{UATI_OMNIMICROPHONE, SOUND_MIXER_MIC},
	{UATI_MICROPHONEARRAY, SOUND_MIXER_MIC},
	{UATI_PROCMICROPHONEARR, SOUND_MIXER_MIC},

	{UATO_SPEAKER, SOUND_MIXER_SPEAKER},
	{UATO_DESKTOPSPEAKER, SOUND_MIXER_SPEAKER},
	{UATO_ROOMSPEAKER, SOUND_MIXER_SPEAKER},
	{UATO_COMMSPEAKER, SOUND_MIXER_SPEAKER},

	{UATE_ANALOGCONN, SOUND_MIXER_LINE},
	{UATE_LINECONN, SOUND_MIXER_LINE},
	{UATE_LEGACYCONN, SOUND_MIXER_LINE},

	{UATE_DIGITALAUIFC, SOUND_MIXER_ALTPCM},
	{UATE_SPDIF, SOUND_MIXER_ALTPCM},
	{UATE_1394DA, SOUND_MIXER_ALTPCM},
	{UATE_1394DV, SOUND_MIXER_ALTPCM},

	{UATF_CDPLAYER, SOUND_MIXER_CD},

	{UATF_SYNTHESIZER, SOUND_MIXER_SYNTH},

	{UATF_VIDEODISCAUDIO, SOUND_MIXER_VIDEO},
	{UATF_DVDAUDIO, SOUND_MIXER_VIDEO},
	{UATF_TVTUNERAUDIO, SOUND_MIXER_VIDEO},

	/* telephony terminal types */
	{UATT_UNDEFINED, SOUND_MIXER_PHONEIN},	/* SOUND_MIXER_PHONEOUT */
	{UATT_PHONELINE, SOUND_MIXER_PHONEIN},	/* SOUND_MIXER_PHONEOUT */
	{UATT_TELEPHONE, SOUND_MIXER_PHONEIN},	/* SOUND_MIXER_PHONEOUT */
	{UATT_DOWNLINEPHONE, SOUND_MIXER_PHONEIN},	/* SOUND_MIXER_PHONEOUT */

	{UATF_RADIORECV, SOUND_MIXER_RADIO},
	{UATF_RADIOXMIT, SOUND_MIXER_RADIO},

	{UAT_UNDEFINED, SOUND_MIXER_VOLUME},
	{UAT_VENDOR, SOUND_MIXER_VOLUME},
	{UATI_UNDEFINED, SOUND_MIXER_VOLUME},

	/* output terminal types */
	{UATO_UNDEFINED, SOUND_MIXER_VOLUME},
	{UATO_DISPLAYAUDIO, SOUND_MIXER_VOLUME},
	{UATO_SUBWOOFER, SOUND_MIXER_VOLUME},
	{UATO_HEADPHONES, SOUND_MIXER_VOLUME},

	/* bidir terminal types */
	{UATB_UNDEFINED, SOUND_MIXER_VOLUME},
	{UATB_HANDSET, SOUND_MIXER_VOLUME},
	{UATB_HEADSET, SOUND_MIXER_VOLUME},
	{UATB_SPEAKERPHONE, SOUND_MIXER_VOLUME},
	{UATB_SPEAKERPHONEESUP, SOUND_MIXER_VOLUME},
	{UATB_SPEAKERPHONEECANC, SOUND_MIXER_VOLUME},

	/* external terminal types */
	{UATE_UNDEFINED, SOUND_MIXER_VOLUME},

	/* embedded function terminal types */
	{UATF_UNDEFINED, SOUND_MIXER_VOLUME},
	{UATF_CALIBNOISE, SOUND_MIXER_VOLUME},
	{UATF_EQUNOISE, SOUND_MIXER_VOLUME},
	{UATF_DAT, SOUND_MIXER_VOLUME},
	{UATF_DCC, SOUND_MIXER_VOLUME},
	{UATF_MINIDISK, SOUND_MIXER_VOLUME},
	{UATF_ANALOGTAPE, SOUND_MIXER_VOLUME},
	{UATF_PHONOGRAPH, SOUND_MIXER_VOLUME},
	{UATF_VCRAUDIO, SOUND_MIXER_VOLUME},
	{UATF_SATELLITE, SOUND_MIXER_VOLUME},
	{UATF_CABLETUNER, SOUND_MIXER_VOLUME},
	{UATF_DSS, SOUND_MIXER_VOLUME},
	{UATF_MULTITRACK, SOUND_MIXER_VOLUME},
	{0xffff, SOUND_MIXER_VOLUME},

	/* default */
	{0x0000, SOUND_MIXER_VOLUME},
};

static uint16_t
uaudio_mixer_feature_name(const struct uaudio_terminal_node *iot,
    struct uaudio_mixer_node *mix)
{
	const struct uaudio_tt_to_feature *uat = uaudio_tt_to_feature;
	uint16_t terminal_type = uaudio_mixer_determine_class(iot, mix);

	if ((mix->class == UAC_RECORD) && (terminal_type == 0)) {
		return (SOUND_MIXER_IMIX);
	}
	while (uat->terminal_type) {
		if (uat->terminal_type == terminal_type) {
			break;
		}
		uat++;
	}

	DPRINTF("terminal_type=0x%04x -> %d\n",
	    terminal_type, uat->feature);

	return (uat->feature);
}

static uint16_t
uaudio20_mixer_feature_name(const struct uaudio_terminal_node *iot,
    struct uaudio_mixer_node *mix)
{
	const struct uaudio_tt_to_feature *uat;
	uint16_t terminal_type = uaudio20_mixer_determine_class(iot, mix);

	if ((mix->class == UAC_RECORD) && (terminal_type == 0))
		return (SOUND_MIXER_IMIX);
	
	for (uat = uaudio_tt_to_feature; uat->terminal_type != 0; uat++) {
		if (uat->terminal_type == terminal_type)
			break;
	}

	DPRINTF("terminal_type=0x%04x -> %d\n",
	    terminal_type, uat->feature);

	return (uat->feature);
}

static const struct uaudio_terminal_node *
uaudio_mixer_get_input(const struct uaudio_terminal_node *iot, uint8_t i)
{
	struct uaudio_terminal_node *root = iot->root;
	uint8_t n;

	n = iot->usr.id_max;
	do {
		if (iot->usr.bit_input[n / 8] & (1 << (n % 8))) {
			if (!i--)
				return (root + n);
		}
	} while (n--);

	return (NULL);
}

static const struct uaudio_terminal_node *
uaudio_mixer_get_output(const struct uaudio_terminal_node *iot, uint8_t i)
{
	struct uaudio_terminal_node *root = iot->root;
	uint8_t n;

	n = iot->usr.id_max;
	do {
		if (iot->usr.bit_output[n / 8] & (1 << (n % 8))) {
			if (!i--)
				return (root + n);
		}
	} while (n--);

	return (NULL);
}

static void
uaudio_mixer_find_inputs_sub(struct uaudio_terminal_node *root,
    const uint8_t *p_id, uint8_t n_id,
    struct uaudio_search_result *info)
{
	struct uaudio_terminal_node *iot;
	uint8_t n;
	uint8_t i;
	uint8_t is_last;

top:
	for (n = 0; n < n_id; n++) {

		i = p_id[n];

		if (info->recurse_level == UAUDIO_RECURSE_LIMIT) {
			DPRINTF("avoided going into a circle at id=%d!\n", i);
			return;
		}

		info->recurse_level++;

		iot = (root + i);

		if (iot->u.desc == NULL)
			continue;

		is_last = ((n + 1) == n_id);

		switch (iot->u.desc->bDescriptorSubtype) {
		case UDESCSUB_AC_INPUT:
			info->bit_input[i / 8] |= (1 << (i % 8));
			break;

		case UDESCSUB_AC_FEATURE:
			if (is_last) {
				p_id = &iot->u.fu_v1->bSourceId;
				n_id = 1;
				goto top;
			}
			uaudio_mixer_find_inputs_sub(
			    root, &iot->u.fu_v1->bSourceId, 1, info);
			break;

		case UDESCSUB_AC_OUTPUT:
			if (is_last) {
				p_id = &iot->u.ot_v1->bSourceId;
				n_id = 1;
				goto top;
			}
			uaudio_mixer_find_inputs_sub(
			    root, &iot->u.ot_v1->bSourceId, 1, info);
			break;

		case UDESCSUB_AC_MIXER:
			if (is_last) {
				p_id = iot->u.mu_v1->baSourceId;
				n_id = iot->u.mu_v1->bNrInPins;
				goto top;
			}
			uaudio_mixer_find_inputs_sub(
			    root, iot->u.mu_v1->baSourceId,
			    iot->u.mu_v1->bNrInPins, info);
			break;

		case UDESCSUB_AC_SELECTOR:
			if (is_last) {
				p_id = iot->u.su_v1->baSourceId;
				n_id = iot->u.su_v1->bNrInPins;
				goto top;
			}
			uaudio_mixer_find_inputs_sub(
			    root, iot->u.su_v1->baSourceId,
			    iot->u.su_v1->bNrInPins, info);
			break;

		case UDESCSUB_AC_PROCESSING:
			if (is_last) {
				p_id = iot->u.pu_v1->baSourceId;
				n_id = iot->u.pu_v1->bNrInPins;
				goto top;
			}
			uaudio_mixer_find_inputs_sub(
			    root, iot->u.pu_v1->baSourceId,
			    iot->u.pu_v1->bNrInPins, info);
			break;

		case UDESCSUB_AC_EXTENSION:
			if (is_last) {
				p_id = iot->u.eu_v1->baSourceId;
				n_id = iot->u.eu_v1->bNrInPins;
				goto top;
			}
			uaudio_mixer_find_inputs_sub(
			    root, iot->u.eu_v1->baSourceId,
			    iot->u.eu_v1->bNrInPins, info);
			break;

		default:
			break;
		}
	}
}

static void
uaudio20_mixer_find_inputs_sub(struct uaudio_terminal_node *root,
    const uint8_t *p_id, uint8_t n_id,
    struct uaudio_search_result *info)
{
	struct uaudio_terminal_node *iot;
	uint8_t n;
	uint8_t i;
	uint8_t is_last;

top:
	for (n = 0; n < n_id; n++) {

		i = p_id[n];

		if (info->recurse_level == UAUDIO_RECURSE_LIMIT) {
			DPRINTF("avoided going into a circle at id=%d!\n", i);
			return;
		}

		info->recurse_level++;

		iot = (root + i);

		if (iot->u.desc == NULL)
			continue;

		is_last = ((n + 1) == n_id);

		switch (iot->u.desc->bDescriptorSubtype) {
		case UDESCSUB_AC_INPUT:
			info->bit_input[i / 8] |= (1 << (i % 8));
			break;

		case UDESCSUB_AC_OUTPUT:
			if (is_last) {
				p_id = &iot->u.ot_v2->bSourceId;
				n_id = 1;
				goto top;
			}
			uaudio20_mixer_find_inputs_sub(
			    root, &iot->u.ot_v2->bSourceId, 1, info);
			break;

		case UDESCSUB_AC_MIXER:
			if (is_last) {
				p_id = iot->u.mu_v2->baSourceId;
				n_id = iot->u.mu_v2->bNrInPins;
				goto top;
			}
			uaudio20_mixer_find_inputs_sub(
			    root, iot->u.mu_v2->baSourceId,
			    iot->u.mu_v2->bNrInPins, info);
			break;

		case UDESCSUB_AC_SELECTOR:
			if (is_last) {
				p_id = iot->u.su_v2->baSourceId;
				n_id = iot->u.su_v2->bNrInPins;
				goto top;
			}
			uaudio20_mixer_find_inputs_sub(
			    root, iot->u.su_v2->baSourceId,
			    iot->u.su_v2->bNrInPins, info);
			break;

		case UDESCSUB_AC_SAMPLE_RT:
			if (is_last) {
				p_id = &iot->u.ru_v2->bSourceId;
				n_id = 1;
				goto top;
			}
			uaudio20_mixer_find_inputs_sub(
			    root, &iot->u.ru_v2->bSourceId,
			    1, info);
			break;

		case UDESCSUB_AC_EFFECT:
			if (is_last) {
				p_id = &iot->u.ef_v2->bSourceId;
				n_id = 1;
				goto top;
			}
			uaudio20_mixer_find_inputs_sub(
			    root, &iot->u.ef_v2->bSourceId,
			    1, info);
			break;

		case UDESCSUB_AC_FEATURE:
			if (is_last) {
				p_id = &iot->u.fu_v2->bSourceId;
				n_id = 1;
				goto top;
			}
			uaudio20_mixer_find_inputs_sub(
			    root, &iot->u.fu_v2->bSourceId, 1, info);
			break;

		case UDESCSUB_AC_PROCESSING_V2:
			if (is_last) {
				p_id = iot->u.pu_v2->baSourceId;
				n_id = iot->u.pu_v2->bNrInPins;
				goto top;
			}
			uaudio20_mixer_find_inputs_sub(
			    root, iot->u.pu_v2->baSourceId,
			    iot->u.pu_v2->bNrInPins, info);
			break;

		case UDESCSUB_AC_EXTENSION_V2:
			if (is_last) {
				p_id = iot->u.eu_v2->baSourceId;
				n_id = iot->u.eu_v2->bNrInPins;
				goto top;
			}
			uaudio20_mixer_find_inputs_sub(
			    root, iot->u.eu_v2->baSourceId,
			    iot->u.eu_v2->bNrInPins, info);
			break;
		default:
			break;
		}
	}
}

static void
uaudio20_mixer_find_clocks_sub(struct uaudio_terminal_node *root,
    const uint8_t *p_id, uint8_t n_id,
    struct uaudio_search_result *info)
{
	struct uaudio_terminal_node *iot;
	uint8_t n;
	uint8_t i;
	uint8_t is_last;
	uint8_t id;

top:
	for (n = 0; n < n_id; n++) {

		i = p_id[n];

		if (info->recurse_level == UAUDIO_RECURSE_LIMIT) {
			DPRINTF("avoided going into a circle at id=%d!\n", i);
			return;
		}

		info->recurse_level++;

		iot = (root + i);

		if (iot->u.desc == NULL)
			continue;

		is_last = ((n + 1) == n_id);

		switch (iot->u.desc->bDescriptorSubtype) {
		case UDESCSUB_AC_INPUT:
			info->is_input = 1;
			if (is_last) {
				p_id = &iot->u.it_v2->bCSourceId;
				n_id = 1;
				goto top;
			}
			uaudio20_mixer_find_clocks_sub(root,
			    &iot->u.it_v2->bCSourceId, 1, info);
			break;

		case UDESCSUB_AC_OUTPUT:
			info->is_input = 0;
			if (is_last) {
				p_id = &iot->u.ot_v2->bCSourceId;
				n_id = 1;
				goto top;
			}
			uaudio20_mixer_find_clocks_sub(root,
			    &iot->u.ot_v2->bCSourceId, 1, info);
			break;

		case UDESCSUB_AC_CLOCK_SEL:
			if (is_last) {
				p_id = iot->u.csel_v2->baCSourceId;
				n_id = iot->u.csel_v2->bNrInPins;
				goto top;
			}
			uaudio20_mixer_find_clocks_sub(root,
			    iot->u.csel_v2->baCSourceId,
			    iot->u.csel_v2->bNrInPins, info);
			break;

		case UDESCSUB_AC_CLOCK_MUL:
			if (is_last) {
				p_id = &iot->u.cmul_v2->bCSourceId;
				n_id = 1;
				goto top;
			}
			uaudio20_mixer_find_clocks_sub(root,
			    &iot->u.cmul_v2->bCSourceId,
			    1, info);
			break;

		case UDESCSUB_AC_CLOCK_SRC:

			id = iot->u.csrc_v2->bClockId;

			switch (info->is_input) {
			case 0:
				info->bit_output[id / 8] |= (1 << (id % 8));
				break;
			case 1:
				info->bit_input[id / 8] |= (1 << (id % 8));
				break;
			default:
				break;
			}
			break;

		default:
			break;
		}
	}
}

static void
uaudio_mixer_find_outputs_sub(struct uaudio_terminal_node *root, uint8_t id,
    uint8_t n_id, struct uaudio_search_result *info)
{
	struct uaudio_terminal_node *iot = (root + id);
	uint8_t j;

	j = n_id;
	do {
		if ((j != id) && ((root + j)->u.desc) &&
		    ((root + j)->u.desc->bDescriptorSubtype == UDESCSUB_AC_OUTPUT)) {

			/*
			 * "j" (output) <--- virtual wire <--- "id" (input)
			 *
			 * if "j" has "id" on the input, then "id" have "j" on
			 * the output, because they are connected:
			 */
			if ((root + j)->usr.bit_input[id / 8] & (1 << (id % 8))) {
				iot->usr.bit_output[j / 8] |= (1 << (j % 8));
			}
		}
	} while (j--);
}

static void
uaudio_mixer_fill_info(struct uaudio_softc *sc,
    struct usb_device *udev, void *desc)
{
	const struct usb_audio_control_descriptor *acdp;
	struct usb_config_descriptor *cd = usbd_get_config_descriptor(udev);
	const struct usb_descriptor *dp;
	const struct usb_audio_unit *au;
	struct uaudio_terminal_node *iot = NULL;
	uint16_t wTotalLen;
	uint8_t ID_max = 0;		/* inclusive */
	uint8_t i;

	desc = usb_desc_foreach(cd, desc);

	if (desc == NULL) {
		DPRINTF("no Audio Control header\n");
		goto done;
	}
	acdp = desc;

	if ((acdp->bLength < sizeof(*acdp)) ||
	    (acdp->bDescriptorType != UDESC_CS_INTERFACE) ||
	    (acdp->bDescriptorSubtype != UDESCSUB_AC_HEADER)) {
		DPRINTF("invalid Audio Control header\n");
		goto done;
	}
	/* "wTotalLen" is allowed to be corrupt */
	wTotalLen = UGETW(acdp->wTotalLength) - acdp->bLength;

	/* get USB audio revision */
	sc->sc_audio_rev = UGETW(acdp->bcdADC);

	DPRINTFN(3, "found AC header, vers=%03x, len=%d\n",
	    sc->sc_audio_rev, wTotalLen);

	iot = malloc(sizeof(struct uaudio_terminal_node) * 256, M_TEMP,
	    M_WAITOK | M_ZERO);

	if (iot == NULL) {
		DPRINTF("no memory!\n");
		goto done;
	}
	while ((desc = usb_desc_foreach(cd, desc))) {

		dp = desc;

		if (dp->bLength > wTotalLen) {
			break;
		} else {
			wTotalLen -= dp->bLength;
		}

		if (sc->sc_audio_rev >= UAUDIO_VERSION_30)
			au = NULL;
		else if (sc->sc_audio_rev >= UAUDIO_VERSION_20)
			au = uaudio20_mixer_verify_desc(dp, 0);
		else
			au = uaudio_mixer_verify_desc(dp, 0);

		if (au) {
			iot[au->bUnitId].u.desc = (const void *)au;
			if (au->bUnitId > ID_max)
				ID_max = au->bUnitId;
		}
	}

	DPRINTF("Maximum ID=%d\n", ID_max);

	/*
	 * determine sourcing inputs for
	 * all nodes in the tree:
	 */
	i = ID_max;
	do {
		if (sc->sc_audio_rev >= UAUDIO_VERSION_30) {
			/* FALLTHROUGH */
		} else if (sc->sc_audio_rev >= UAUDIO_VERSION_20) {
			uaudio20_mixer_find_inputs_sub(iot,
			    &i, 1, &((iot + i)->usr));

			sc->sc_mixer_clocks.is_input = 255;
			sc->sc_mixer_clocks.recurse_level = 0;

			uaudio20_mixer_find_clocks_sub(iot,
			    &i, 1, &sc->sc_mixer_clocks);
		} else {
			uaudio_mixer_find_inputs_sub(iot,
			    &i, 1, &((iot + i)->usr));
		}
	} while (i--);

	/*
	 * determine outputs for
	 * all nodes in the tree:
	 */
	i = ID_max;
	do {
		uaudio_mixer_find_outputs_sub(iot,
		    i, ID_max, &((iot + i)->usr));
	} while (i--);

	/* set "id_max" and "root" */

	i = ID_max;
	do {
		(iot + i)->usr.id_max = ID_max;
		(iot + i)->root = iot;
	} while (i--);

	/*
	 * Scan the config to create a linked list of "mixer" nodes:
	 */

	i = ID_max;
	do {
		dp = iot[i].u.desc;

		if (dp == NULL)
			continue;

		DPRINTFN(11, "id=%d subtype=%d\n",
		    i, dp->bDescriptorSubtype);

		if (sc->sc_audio_rev >= UAUDIO_VERSION_30) {
			continue;
		} else if (sc->sc_audio_rev >= UAUDIO_VERSION_20) {

			switch (dp->bDescriptorSubtype) {
			case UDESCSUB_AC_HEADER:
				DPRINTF("unexpected AC header\n");
				break;

			case UDESCSUB_AC_INPUT:
			case UDESCSUB_AC_OUTPUT:
			case UDESCSUB_AC_PROCESSING_V2:
			case UDESCSUB_AC_EXTENSION_V2:
			case UDESCSUB_AC_EFFECT:
			case UDESCSUB_AC_CLOCK_SRC:
			case UDESCSUB_AC_CLOCK_SEL:
			case UDESCSUB_AC_CLOCK_MUL:
			case UDESCSUB_AC_SAMPLE_RT:
				break;

			case UDESCSUB_AC_MIXER:
				uaudio20_mixer_add_mixer(sc, iot, i);
				break;

			case UDESCSUB_AC_SELECTOR:
				uaudio20_mixer_add_selector(sc, iot, i);
				break;

			case UDESCSUB_AC_FEATURE:
				uaudio20_mixer_add_feature(sc, iot, i);
				break;

			default:
				DPRINTF("bad AC desc subtype=0x%02x\n",
				    dp->bDescriptorSubtype);
				break;
			}
			continue;
		}

		switch (dp->bDescriptorSubtype) {
		case UDESCSUB_AC_HEADER:
			DPRINTF("unexpected AC header\n");
			break;

		case UDESCSUB_AC_INPUT:
		case UDESCSUB_AC_OUTPUT:
			break;

		case UDESCSUB_AC_MIXER:
			uaudio_mixer_add_mixer(sc, iot, i);
			break;

		case UDESCSUB_AC_SELECTOR:
			uaudio_mixer_add_selector(sc, iot, i);
			break;

		case UDESCSUB_AC_FEATURE:
			uaudio_mixer_add_feature(sc, iot, i);
			break;

		case UDESCSUB_AC_PROCESSING:
			uaudio_mixer_add_processing(sc, iot, i);
			break;

		case UDESCSUB_AC_EXTENSION:
			uaudio_mixer_add_extension(sc, iot, i);
			break;

		default:
			DPRINTF("bad AC desc subtype=0x%02x\n",
			    dp->bDescriptorSubtype);
			break;
		}

	} while (i--);

done:
	free(iot, M_TEMP);
}

static int
uaudio_mixer_get(struct usb_device *udev, uint16_t audio_rev,
    uint8_t what, struct uaudio_mixer_node *mc)
{
	struct usb_device_request req;
	int val;
	uint8_t data[2 + (2 * 3)];
	usb_error_t err;

	if (mc->wValue[0] == -1)
		return (0);

	if (audio_rev >= UAUDIO_VERSION_30)
		return (0);
	else if (audio_rev >= UAUDIO_VERSION_20) {
		if (what == GET_CUR) {
			req.bRequest = UA20_CS_CUR;
			USETW(req.wLength, 2);
		} else {
			req.bRequest = UA20_CS_RANGE;
			USETW(req.wLength, 8);
		}
	} else {
		uint16_t len = MIX_SIZE(mc->type);

		req.bRequest = what;
		USETW(req.wLength, len);
	}

	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	USETW(req.wValue, mc->wValue[0]);
	USETW(req.wIndex, mc->wIndex);

	memset(data, 0, sizeof(data));

	err = usbd_do_request(udev, NULL, &req, data);
	if (err) {
		DPRINTF("err=%s\n", usbd_errstr(err));
		return (0);
	}

	if (audio_rev >= UAUDIO_VERSION_30) {
		val = 0;
	} else if (audio_rev >= UAUDIO_VERSION_20) {
		switch (what) {
		case GET_CUR:
			val = (data[0] | (data[1] << 8));
			break;
		case GET_MIN:
			val = (data[2] | (data[3] << 8));
			break;
		case GET_MAX:
			val = (data[4] | (data[5] << 8));
			break;
		case GET_RES:
			val = (data[6] | (data[7] << 8));
			break;
		default:
			val = 0;
			break;
		}
	} else {
		val = (data[0] | (data[1] << 8));
	}

	if (what == GET_CUR || what == GET_MIN || what == GET_MAX)
		val = uaudio_mixer_signext(mc->type, val);

	DPRINTFN(3, "val=%d\n", val);

	return (val);
}

static void
uaudio_mixer_write_cfg_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usb_device_request req;
	struct uaudio_softc *sc = usbd_xfer_softc(xfer);
	struct uaudio_mixer_node *mc = sc->sc_mixer_curr;
	struct usb_page_cache *pc;
	uint16_t len;
	uint8_t repeat = 1;
	uint8_t update;
	uint8_t chan;
	uint8_t buf[2];

	DPRINTF("\n");

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
tr_transferred:
	case USB_ST_SETUP:
tr_setup:

		if (mc == NULL) {
			mc = sc->sc_mixer_root;
			sc->sc_mixer_curr = mc;
			sc->sc_mixer_chan = 0;
			repeat = 0;
		}
		while (mc) {
			while (sc->sc_mixer_chan < mc->nchan) {

				chan = sc->sc_mixer_chan;

				sc->sc_mixer_chan++;

				update = ((mc->update[chan / 8] & (1 << (chan % 8))) &&
				    (mc->wValue[chan] != -1));

				mc->update[chan / 8] &= ~(1 << (chan % 8));

				if (update) {

					req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
					USETW(req.wValue, mc->wValue[chan]);
					USETW(req.wIndex, mc->wIndex);

					if (sc->sc_audio_rev >= UAUDIO_VERSION_30) {
						return;
					} else if (sc->sc_audio_rev >= UAUDIO_VERSION_20) {
						len = 2;
						req.bRequest = UA20_CS_CUR;
						USETW(req.wLength, len);
					} else {
						len = MIX_SIZE(mc->type);
						req.bRequest = SET_CUR;
						USETW(req.wLength, len);
					}

					buf[0] = (mc->wData[chan] & 0xFF);
					buf[1] = (mc->wData[chan] >> 8) & 0xFF;

					pc = usbd_xfer_get_frame(xfer, 0);
					usbd_copy_in(pc, 0, &req, sizeof(req));
					pc = usbd_xfer_get_frame(xfer, 1);
					usbd_copy_in(pc, 0, buf, len);

					usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
					usbd_xfer_set_frame_len(xfer, 1, len);
					usbd_xfer_set_frames(xfer, len ? 2 : 1);
					usbd_transfer_submit(xfer);
					return;
				}
			}

			mc = mc->next;
			sc->sc_mixer_curr = mc;
			sc->sc_mixer_chan = 0;
		}

		if (repeat) {
			goto tr_setup;
		}
		break;

	default:			/* Error */
		DPRINTF("error=%s\n", usbd_errstr(error));
		if (error == USB_ERR_CANCELLED) {
			/* do nothing - we are detaching */
			break;
		}
		goto tr_transferred;
	}
}

static usb_error_t
uaudio_set_speed(struct usb_device *udev, uint8_t endpt, uint32_t speed)
{
	struct usb_device_request req;
	uint8_t data[3];

	DPRINTFN(6, "endpt=%d speed=%u\n", endpt, speed);

	req.bmRequestType = UT_WRITE_CLASS_ENDPOINT;
	req.bRequest = SET_CUR;
	USETW2(req.wValue, SAMPLING_FREQ_CONTROL, 0);
	USETW(req.wIndex, endpt);
	USETW(req.wLength, 3);
	data[0] = speed;
	data[1] = speed >> 8;
	data[2] = speed >> 16;

	return (usbd_do_request(udev, NULL, &req, data));
}

static usb_error_t
uaudio20_set_speed(struct usb_device *udev, uint8_t iface_no,
    uint8_t clockid, uint32_t speed)
{
	struct usb_device_request req;
	uint8_t data[4];

	DPRINTFN(6, "ifaceno=%d clockid=%d speed=%u\n",
	    iface_no, clockid, speed);

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UA20_CS_CUR;
	USETW2(req.wValue, UA20_CS_SAM_FREQ_CONTROL, 0);
	USETW2(req.wIndex, clockid, iface_no);
	USETW(req.wLength, 4);
	data[0] = speed;
	data[1] = speed >> 8;
	data[2] = speed >> 16;
	data[3] = speed >> 24;

	return (usbd_do_request(udev, NULL, &req, data));
}

static int
uaudio_mixer_signext(uint8_t type, int val)
{
	if (!MIX_UNSIGNED(type)) {
		if (MIX_SIZE(type) == 2) {
			val = (int16_t)val;
		} else {
			val = (int8_t)val;
		}
	}
	return (val);
}

static int
uaudio_mixer_bsd2value(struct uaudio_mixer_node *mc, int32_t val)
{
	if (mc->type == MIX_ON_OFF) {
		val = (val != 0);
	} else if (mc->type == MIX_SELECTOR) {
		if ((val < mc->minval) ||
		    (val > mc->maxval)) {
			val = mc->minval;
		}
	} else {

		/* compute actual volume */
		val = (val * mc->mul) / 255;

		/* add lower offset */
		val = val + mc->minval;

		/* make sure we don't write a value out of range */
		if (val > mc->maxval)
			val = mc->maxval;
		else if (val < mc->minval)
			val = mc->minval;
	}

	DPRINTFN(6, "type=0x%03x val=%d min=%d max=%d val=%d\n",
	    mc->type, val, mc->minval, mc->maxval, val);
	return (val);
}

static void
uaudio_mixer_ctl_set(struct uaudio_softc *sc, struct uaudio_mixer_node *mc,
    uint8_t chan, int32_t val)
{
	val = uaudio_mixer_bsd2value(mc, val);

	mc->update[chan / 8] |= (1 << (chan % 8));
	mc->wData[chan] = val;

	/* start the transfer, if not already started */

	usbd_transfer_start(sc->sc_mixer_xfer[0]);
}

static void
uaudio_mixer_init(struct uaudio_softc *sc)
{
	struct uaudio_mixer_node *mc;
	int32_t i;

	for (mc = sc->sc_mixer_root; mc;
	    mc = mc->next) {

		if (mc->ctl != SOUND_MIXER_NRDEVICES) {
			/*
			 * Set device mask bits. See
			 * /usr/include/machine/soundcard.h
			 */
			sc->sc_mix_info |= (1 << mc->ctl);
		}
		if ((mc->ctl == SOUND_MIXER_NRDEVICES) &&
		    (mc->type == MIX_SELECTOR)) {

			for (i = mc->minval; (i > 0) && (i <= mc->maxval); i++) {
				if (mc->slctrtype[i - 1] == SOUND_MIXER_NRDEVICES) {
					continue;
				}
				sc->sc_recsrc_info |= 1 << mc->slctrtype[i - 1];
			}
		}
	}
}

int
uaudio_mixer_init_sub(struct uaudio_softc *sc, struct snd_mixer *m)
{
	DPRINTF("\n");

	sc->sc_mixer_lock = mixer_get_lock(m);
	sc->sc_mixer_dev = m;

	if (usbd_transfer_setup(sc->sc_udev, &sc->sc_mixer_iface_index,
	    sc->sc_mixer_xfer, uaudio_mixer_config, 1, sc,
	    sc->sc_mixer_lock)) {
		DPRINTFN(0, "could not allocate USB "
		    "transfer for audio mixer!\n");
		return (ENOMEM);
	}
	if (!(sc->sc_mix_info & SOUND_MASK_VOLUME)) {
		mix_setparentchild(m, SOUND_MIXER_VOLUME, SOUND_MASK_PCM);
		mix_setrealdev(m, SOUND_MIXER_VOLUME, SOUND_MIXER_NONE);
	}
	mix_setdevs(m, sc->sc_mix_info);
	mix_setrecdevs(m, sc->sc_recsrc_info);
	return (0);
}

int
uaudio_mixer_uninit_sub(struct uaudio_softc *sc)
{
	DPRINTF("\n");

	usbd_transfer_unsetup(sc->sc_mixer_xfer, 1);

	sc->sc_mixer_lock = NULL;

	return (0);
}

void
uaudio_mixer_set(struct uaudio_softc *sc, unsigned type,
    unsigned left, unsigned right)
{
	struct uaudio_mixer_node *mc;
	int chan;

	for (mc = sc->sc_mixer_root; mc != NULL; mc = mc->next) {

		if (mc->ctl == type) {
			for (chan = 0; chan < mc->nchan; chan++) {
				uaudio_mixer_ctl_set(sc, mc, chan,
				    (int)((chan == 0 ? left : right) *
				    255) / 100);
			}
		}
	}
}

uint32_t
uaudio_mixer_setrecsrc(struct uaudio_softc *sc, uint32_t src)
{
	struct uaudio_mixer_node *mc;
	uint32_t mask;
	uint32_t temp;
	int32_t i;

	for (mc = sc->sc_mixer_root; mc;
	    mc = mc->next) {

		if ((mc->ctl == SOUND_MIXER_NRDEVICES) &&
		    (mc->type == MIX_SELECTOR)) {

			/* compute selector mask */

			mask = 0;
			for (i = mc->minval; (i > 0) && (i <= mc->maxval); i++) {
				mask |= (1 << mc->slctrtype[i - 1]);
			}

			temp = mask & src;
			if (temp == 0) {
				continue;
			}
			/* find the first set bit */
			temp = (-temp) & temp;

			/* update "src" */
			src &= ~mask;
			src |= temp;

			for (i = mc->minval; (i > 0) && (i <= mc->maxval); i++) {
				if (temp != (1 << mc->slctrtype[i - 1])) {
					continue;
				}
				uaudio_mixer_ctl_set(sc, mc, 0, i);
				break;
			}
		}
	}
	return (src);
}

/*========================================================================*
 * MIDI support routines
 *========================================================================*/

static void
umidi_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umidi_chan *chan = usbd_xfer_softc(xfer);
	struct umidi_sub_chan *sub;
	struct usb_page_cache *pc;
	uint8_t buf[4];
	uint8_t cmd_len;
	uint8_t cn;
	uint16_t pos;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTF("actlen=%d bytes\n", actlen);

		pos = 0;
		pc = usbd_xfer_get_frame(xfer, 0);

		while (actlen >= 4) {

			/* copy out the MIDI data */
			usbd_copy_out(pc, pos, buf, 4);
			/* command length */
			cmd_len = umidi_cmd_to_len[buf[0] & 0xF];
			/* cable number */
			cn = buf[0] >> 4;
			/*
			 * Lookup sub-channel. The index is range
			 * checked below.
			 */
			sub = &chan->sub[cn];

			if ((cmd_len != 0) && (cn < chan->max_emb_jack) &&
			    (sub->read_open != 0)) {

				/* Send data to the application */
				usb_fifo_put_data_linear(
				    sub->fifo.fp[USB_FIFO_RX],
				    buf + 1, cmd_len, 1);
			}
			actlen -= 4;
			pos += 4;
		}

	case USB_ST_SETUP:
		DPRINTF("start\n");
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:
		DPRINTF("error=%s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

/*
 * The following statemachine, that converts MIDI commands to
 * USB MIDI packets, derives from Linux's usbmidi.c, which
 * was written by "Clemens Ladisch":
 *
 * Returns:
 *    0: No command
 * Else: Command is complete
 */
static uint8_t
umidi_convert_to_usb(struct umidi_sub_chan *sub, uint8_t cn, uint8_t b)
{
	uint8_t p0 = (cn << 4);

	if (b >= 0xf8) {
		sub->temp_0[0] = p0 | 0x0f;
		sub->temp_0[1] = b;
		sub->temp_0[2] = 0;
		sub->temp_0[3] = 0;
		sub->temp_cmd = sub->temp_0;
		return (1);

	} else if (b >= 0xf0) {
		switch (b) {
		case 0xf0:		/* system exclusive begin */
			sub->temp_1[1] = b;
			sub->state = UMIDI_ST_SYSEX_1;
			break;
		case 0xf1:		/* MIDI time code */
		case 0xf3:		/* song select */
			sub->temp_1[1] = b;
			sub->state = UMIDI_ST_1PARAM;
			break;
		case 0xf2:		/* song position pointer */
			sub->temp_1[1] = b;
			sub->state = UMIDI_ST_2PARAM_1;
			break;
		case 0xf4:		/* unknown */
		case 0xf5:		/* unknown */
			sub->state = UMIDI_ST_UNKNOWN;
			break;
		case 0xf6:		/* tune request */
			sub->temp_1[0] = p0 | 0x05;
			sub->temp_1[1] = 0xf6;
			sub->temp_1[2] = 0;
			sub->temp_1[3] = 0;
			sub->temp_cmd = sub->temp_1;
			sub->state = UMIDI_ST_UNKNOWN;
			return (1);

		case 0xf7:		/* system exclusive end */
			switch (sub->state) {
			case UMIDI_ST_SYSEX_0:
				sub->temp_1[0] = p0 | 0x05;
				sub->temp_1[1] = 0xf7;
				sub->temp_1[2] = 0;
				sub->temp_1[3] = 0;
				sub->temp_cmd = sub->temp_1;
				sub->state = UMIDI_ST_UNKNOWN;
				return (1);
			case UMIDI_ST_SYSEX_1:
				sub->temp_1[0] = p0 | 0x06;
				sub->temp_1[2] = 0xf7;
				sub->temp_1[3] = 0;
				sub->temp_cmd = sub->temp_1;
				sub->state = UMIDI_ST_UNKNOWN;
				return (1);
			case UMIDI_ST_SYSEX_2:
				sub->temp_1[0] = p0 | 0x07;
				sub->temp_1[3] = 0xf7;
				sub->temp_cmd = sub->temp_1;
				sub->state = UMIDI_ST_UNKNOWN;
				return (1);
			}
			sub->state = UMIDI_ST_UNKNOWN;
			break;
		}
	} else if (b >= 0x80) {
		sub->temp_1[1] = b;
		if ((b >= 0xc0) && (b <= 0xdf)) {
			sub->state = UMIDI_ST_1PARAM;
		} else {
			sub->state = UMIDI_ST_2PARAM_1;
		}
	} else {			/* b < 0x80 */
		switch (sub->state) {
		case UMIDI_ST_1PARAM:
			if (sub->temp_1[1] < 0xf0) {
				p0 |= sub->temp_1[1] >> 4;
			} else {
				p0 |= 0x02;
				sub->state = UMIDI_ST_UNKNOWN;
			}
			sub->temp_1[0] = p0;
			sub->temp_1[2] = b;
			sub->temp_1[3] = 0;
			sub->temp_cmd = sub->temp_1;
			return (1);
		case UMIDI_ST_2PARAM_1:
			sub->temp_1[2] = b;
			sub->state = UMIDI_ST_2PARAM_2;
			break;
		case UMIDI_ST_2PARAM_2:
			if (sub->temp_1[1] < 0xf0) {
				p0 |= sub->temp_1[1] >> 4;
				sub->state = UMIDI_ST_2PARAM_1;
			} else {
				p0 |= 0x03;
				sub->state = UMIDI_ST_UNKNOWN;
			}
			sub->temp_1[0] = p0;
			sub->temp_1[3] = b;
			sub->temp_cmd = sub->temp_1;
			return (1);
		case UMIDI_ST_SYSEX_0:
			sub->temp_1[1] = b;
			sub->state = UMIDI_ST_SYSEX_1;
			break;
		case UMIDI_ST_SYSEX_1:
			sub->temp_1[2] = b;
			sub->state = UMIDI_ST_SYSEX_2;
			break;
		case UMIDI_ST_SYSEX_2:
			sub->temp_1[0] = p0 | 0x04;
			sub->temp_1[3] = b;
			sub->temp_cmd = sub->temp_1;
			sub->state = UMIDI_ST_SYSEX_0;
			return (1);
		default:
			break;
		}
	}
	return (0);
}

static void
umidi_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umidi_chan *chan = usbd_xfer_softc(xfer);
	struct umidi_sub_chan *sub;
	struct usb_page_cache *pc;
	uint32_t actlen;
	uint16_t nframes;
	uint8_t buf;
	uint8_t start_cable;
	uint8_t tr_any;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	/*
	 * NOTE: Some MIDI devices only accept 4 bytes of data per
	 * short terminated USB transfer.
	 */
	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTF("actlen=%d bytes\n", len);

	case USB_ST_SETUP:
tr_setup:
		DPRINTF("start\n");

		nframes = 0;	/* reset */
		start_cable = chan->curr_cable;
		tr_any = 0;
		pc = usbd_xfer_get_frame(xfer, 0);

		while (1) {

			/* round robin de-queueing */

			sub = &chan->sub[chan->curr_cable];

			if (sub->write_open) {
				usb_fifo_get_data_linear(sub->fifo.fp[USB_FIFO_TX],
				    &buf, 1, &actlen, 0);
			} else {
				actlen = 0;
			}

			if (actlen) {

				tr_any = 1;

				DPRINTF("byte=0x%02x from FIFO %u\n", buf,
				    (unsigned int)chan->curr_cable);

				if (umidi_convert_to_usb(sub, chan->curr_cable, buf)) {

					DPRINTF("sub=0x%02x 0x%02x 0x%02x 0x%02x\n",
					    sub->temp_cmd[0], sub->temp_cmd[1],
					    sub->temp_cmd[2], sub->temp_cmd[3]);

					usbd_copy_in(pc, nframes * 4, sub->temp_cmd, 4);

					nframes++;

					if ((nframes >= UMIDI_TX_FRAMES) || (chan->single_command != 0))
						break;
				} else {
					continue;
				}
			}

			chan->curr_cable++;
			if (chan->curr_cable >= chan->max_emb_jack)
				chan->curr_cable = 0;

			if (chan->curr_cable == start_cable) {
				if (tr_any == 0)
					break;
				tr_any = 0;
			}
		}

		if (nframes != 0) {
			DPRINTF("Transferring %d frames\n", (int)nframes);
			usbd_xfer_set_frame_len(xfer, 0, 4 * nframes);
			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */

		DPRINTF("error=%s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static struct umidi_sub_chan *
umidi_sub_by_fifo(struct usb_fifo *fifo)
{
	struct umidi_chan *chan = usb_fifo_softc(fifo);
	struct umidi_sub_chan *sub;
	uint32_t n;

	for (n = 0; n < UMIDI_EMB_JACK_MAX; n++) {
		sub = &chan->sub[n];
		if ((sub->fifo.fp[USB_FIFO_RX] == fifo) ||
		    (sub->fifo.fp[USB_FIFO_TX] == fifo)) {
			return (sub);
		}
	}

	panic("%s:%d cannot find usb_fifo!\n",
	    __FILE__, __LINE__);

	return (NULL);
}

static void
umidi_start_read(struct usb_fifo *fifo)
{
	struct umidi_chan *chan = usb_fifo_softc(fifo);

	usbd_transfer_start(chan->xfer[UMIDI_RX_TRANSFER]);
}

static void
umidi_stop_read(struct usb_fifo *fifo)
{
	struct umidi_chan *chan = usb_fifo_softc(fifo);
	struct umidi_sub_chan *sub = umidi_sub_by_fifo(fifo);

	DPRINTF("\n");

	sub->read_open = 0;

	if (--(chan->read_open_refcount) == 0) {
		/*
		 * XXX don't stop the read transfer here, hence that causes
		 * problems with some MIDI adapters
		 */
		DPRINTF("(stopping read transfer)\n");
	}
}

static void
umidi_start_write(struct usb_fifo *fifo)
{
	struct umidi_chan *chan = usb_fifo_softc(fifo);

	if (chan->xfer[UMIDI_TX_TRANSFER] == NULL) {
		uint8_t buf[1];
		int actlen;
		do {
			/* dump data */
			usb_fifo_get_data_linear(fifo, buf, 1, &actlen, 0);
		} while (actlen > 0);
	} else {
		usbd_transfer_start(chan->xfer[UMIDI_TX_TRANSFER]);
	}
}

static void
umidi_stop_write(struct usb_fifo *fifo)
{
	struct umidi_chan *chan = usb_fifo_softc(fifo);
	struct umidi_sub_chan *sub = umidi_sub_by_fifo(fifo);

	DPRINTF("\n");

	sub->write_open = 0;

	if (--(chan->write_open_refcount) == 0) {
		DPRINTF("(stopping write transfer)\n");
		usbd_transfer_stop(chan->xfer[UMIDI_TX_TRANSFER]);
	}
}

static int
umidi_open(struct usb_fifo *fifo, int fflags)
{
	struct umidi_chan *chan = usb_fifo_softc(fifo);
	struct umidi_sub_chan *sub = umidi_sub_by_fifo(fifo);

	if (fflags & FREAD) {
		if (usb_fifo_alloc_buffer(fifo, 4, (1024 / 4))) {
			return (ENOMEM);
		}
		mtx_lock(&chan->mtx);
		chan->read_open_refcount++;
		sub->read_open = 1;
		mtx_unlock(&chan->mtx);
	}
	if (fflags & FWRITE) {
		if (usb_fifo_alloc_buffer(fifo, 32, (1024 / 32))) {
			return (ENOMEM);
		}
		/* clear stall first */
		mtx_lock(&chan->mtx);
		chan->write_open_refcount++;
		sub->write_open = 1;

		/* reset */
		sub->state = UMIDI_ST_UNKNOWN;
		mtx_unlock(&chan->mtx);
	}
	return (0);			/* success */
}

static void
umidi_close(struct usb_fifo *fifo, int fflags)
{
	if (fflags & FREAD) {
		usb_fifo_free_buffer(fifo);
	}
	if (fflags & FWRITE) {
		usb_fifo_free_buffer(fifo);
	}
}


static int
umidi_ioctl(struct usb_fifo *fifo, u_long cmd, void *data,
    int fflags)
{
	return (ENODEV);
}

static void
umidi_init(device_t dev)
{
	struct uaudio_softc *sc = device_get_softc(dev);
	struct umidi_chan *chan = &sc->sc_midi_chan;

	mtx_init(&chan->mtx, "umidi lock", NULL, MTX_DEF | MTX_RECURSE);
}

static struct usb_fifo_methods umidi_fifo_methods = {
	.f_start_read = &umidi_start_read,
	.f_start_write = &umidi_start_write,
	.f_stop_read = &umidi_stop_read,
	.f_stop_write = &umidi_stop_write,
	.f_open = &umidi_open,
	.f_close = &umidi_close,
	.f_ioctl = &umidi_ioctl,
	.basename[0] = "umidi",
};

static int
umidi_probe(device_t dev)
{
	struct uaudio_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct umidi_chan *chan = &sc->sc_midi_chan;
	struct umidi_sub_chan *sub;
	int unit = device_get_unit(dev);
	int error;
	uint32_t n;

	if (usb_test_quirk(uaa, UQ_SINGLE_CMD_MIDI))
		chan->single_command = 1;

	if (usbd_set_alt_interface_index(sc->sc_udev, chan->iface_index,
	    chan->iface_alt_index)) {
		DPRINTF("setting of alternate index failed!\n");
		goto detach;
	}
	usbd_set_parent_iface(sc->sc_udev, chan->iface_index,
	    sc->sc_mixer_iface_index);

	error = usbd_transfer_setup(uaa->device, &chan->iface_index,
	    chan->xfer, umidi_config, UMIDI_N_TRANSFER,
	    chan, &chan->mtx);
	if (error) {
		DPRINTF("error=%s\n", usbd_errstr(error));
		goto detach;
	}
	if (chan->xfer[UMIDI_TX_TRANSFER] == NULL &&
	    chan->xfer[UMIDI_RX_TRANSFER] == NULL) {
		DPRINTF("no BULK or INTERRUPT MIDI endpoint(s) found\n");
		goto detach;
	}

	/*
	 * Some USB MIDI device makers couldn't resist using
	 * wMaxPacketSize = 4 for RX and TX BULK endpoints, although
	 * that size is an unsupported value for FULL speed BULK
	 * endpoints. The same applies to some HIGH speed MIDI devices
	 * which are using a wMaxPacketSize different from 512 bytes.
	 *
	 * Refer to section 5.8.3 in USB 2.0 PDF: Cite: "All Host
	 * Controllers are required to have support for 8-, 16-, 32-,
	 * and 64-byte maximum packet sizes for full-speed bulk
	 * endpoints and 512 bytes for high-speed bulk endpoints."
	 */
	if (chan->xfer[UMIDI_TX_TRANSFER] != NULL &&
	    usbd_xfer_maxp_was_clamped(chan->xfer[UMIDI_TX_TRANSFER]))
		chan->single_command = 1;

	if (chan->single_command != 0)
		device_printf(dev, "Single command MIDI quirk enabled\n");

	if ((chan->max_emb_jack == 0) ||
	    (chan->max_emb_jack > UMIDI_EMB_JACK_MAX)) {
		chan->max_emb_jack = UMIDI_EMB_JACK_MAX;
	}

	for (n = 0; n < chan->max_emb_jack; n++) {

		sub = &chan->sub[n];

		error = usb_fifo_attach(sc->sc_udev, chan, &chan->mtx,
		    &umidi_fifo_methods, &sub->fifo, unit, n,
		    chan->iface_index,
		    UID_ROOT, GID_OPERATOR, 0644);
		if (error) {
			goto detach;
		}
	}

	mtx_lock(&chan->mtx);

	/*
	 * NOTE: At least one device will not work properly unless the
	 * BULK IN pipe is open all the time. This might have to do
	 * about that the internal queues of the device overflow if we
	 * don't read them regularly.
	 */
	usbd_transfer_start(chan->xfer[UMIDI_RX_TRANSFER]);

	mtx_unlock(&chan->mtx);

	return (0);			/* success */

detach:
	return (ENXIO);			/* failure */
}

static int
umidi_detach(device_t dev)
{
	struct uaudio_softc *sc = device_get_softc(dev);
	struct umidi_chan *chan = &sc->sc_midi_chan;
	uint32_t n;

	for (n = 0; n < UMIDI_EMB_JACK_MAX; n++)
		usb_fifo_detach(&chan->sub[n].fifo);

	mtx_lock(&chan->mtx);

	usbd_transfer_stop(chan->xfer[UMIDI_RX_TRANSFER]);

	mtx_unlock(&chan->mtx);

	usbd_transfer_unsetup(chan->xfer, UMIDI_N_TRANSFER);

	mtx_destroy(&chan->mtx);

	return (0);
}

static void
uaudio_hid_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uaudio_softc *sc = usbd_xfer_softc(xfer);
	const uint8_t *buffer = usbd_xfer_get_frame_buffer(xfer, 0);
	struct snd_mixer *m;
	uint8_t id;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTF("actlen=%d\n", actlen);

		if (actlen != 0 &&
		    (sc->sc_hid.flags & UAUDIO_HID_HAS_ID)) {
			id = *buffer;
			buffer++;
			actlen--;
		} else {
			id = 0;
		}

		m = sc->sc_mixer_dev;

		if ((sc->sc_hid.flags & UAUDIO_HID_HAS_MUTE) &&
		    (sc->sc_hid.mute_id == id) &&
		    hid_get_data(buffer, actlen,
		    &sc->sc_hid.mute_loc)) {

			DPRINTF("Mute toggle\n");

			mixer_hwvol_mute_locked(m);
		}

		if ((sc->sc_hid.flags & UAUDIO_HID_HAS_VOLUME_UP) &&
		    (sc->sc_hid.volume_up_id == id) &&
		    hid_get_data(buffer, actlen,
		    &sc->sc_hid.volume_up_loc)) {

			DPRINTF("Volume Up\n");

			mixer_hwvol_step_locked(m, 1, 1);
		}

		if ((sc->sc_hid.flags & UAUDIO_HID_HAS_VOLUME_DOWN) &&
		    (sc->sc_hid.volume_down_id == id) &&
		    hid_get_data(buffer, actlen,
		    &sc->sc_hid.volume_down_loc)) {

			DPRINTF("Volume Down\n");

			mixer_hwvol_step_locked(m, -1, -1);
		}

	case USB_ST_SETUP:
tr_setup:
		/* check if we can put more data into the FIFO */
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */

		DPRINTF("error=%s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static int
uaudio_hid_probe(struct uaudio_softc *sc,
    struct usb_attach_arg *uaa)
{
	void *d_ptr;
	uint32_t flags;
	uint16_t d_len;
	uint8_t id;
	int error;

	if (!(sc->sc_hid.flags & UAUDIO_HID_VALID))
		return (-1);

	if (sc->sc_mixer_lock == NULL)
		return (-1);

	/* Get HID descriptor */
	error = usbd_req_get_hid_desc(uaa->device, NULL, &d_ptr,
	    &d_len, M_TEMP, sc->sc_hid.iface_index);

	if (error) {
		DPRINTF("error reading report description\n");
		return (-1);
	}

	/* check if there is an ID byte */
	hid_report_size(d_ptr, d_len, hid_input, &id);

	if (id != 0)
		sc->sc_hid.flags |= UAUDIO_HID_HAS_ID;

	if (hid_locate(d_ptr, d_len,
	    HID_USAGE2(HUP_CONSUMER, 0xE9 /* Volume Increment */),
	    hid_input, 0, &sc->sc_hid.volume_up_loc, &flags,
	    &sc->sc_hid.volume_up_id)) {
		if (flags & HIO_VARIABLE)
			sc->sc_hid.flags |= UAUDIO_HID_HAS_VOLUME_UP;
		DPRINTFN(1, "Found Volume Up key\n");
	}

	if (hid_locate(d_ptr, d_len,
	    HID_USAGE2(HUP_CONSUMER, 0xEA /* Volume Decrement */),
	    hid_input, 0, &sc->sc_hid.volume_down_loc, &flags,
	    &sc->sc_hid.volume_down_id)) {
		if (flags & HIO_VARIABLE)
			sc->sc_hid.flags |= UAUDIO_HID_HAS_VOLUME_DOWN;
		DPRINTFN(1, "Found Volume Down key\n");
	}

	if (hid_locate(d_ptr, d_len,
	    HID_USAGE2(HUP_CONSUMER, 0xE2 /* Mute */),
	    hid_input, 0, &sc->sc_hid.mute_loc, &flags,
	    &sc->sc_hid.mute_id)) {
		if (flags & HIO_VARIABLE)
			sc->sc_hid.flags |= UAUDIO_HID_HAS_MUTE;
		DPRINTFN(1, "Found Mute key\n");
	}

	free(d_ptr, M_TEMP);

	if (!(sc->sc_hid.flags & (UAUDIO_HID_HAS_VOLUME_UP |
	    UAUDIO_HID_HAS_VOLUME_DOWN |
	    UAUDIO_HID_HAS_MUTE))) {
		DPRINTFN(1, "Did not find any volume related keys\n");
		return (-1);
	}

	/* prevent the uhid driver from attaching */
	usbd_set_parent_iface(uaa->device, sc->sc_hid.iface_index,
	    sc->sc_mixer_iface_index);

	/* allocate USB transfers */
	error = usbd_transfer_setup(uaa->device, &sc->sc_hid.iface_index,
	    sc->sc_hid.xfer, uaudio_hid_config, UAUDIO_HID_N_TRANSFER,
	    sc, sc->sc_mixer_lock);
	if (error) {
		DPRINTF("error=%s\n", usbd_errstr(error));
		return (-1);
	}
	return (0);
}

static void
uaudio_hid_detach(struct uaudio_softc *sc)
{
	usbd_transfer_unsetup(sc->sc_hid.xfer, UAUDIO_HID_N_TRANSFER);
}

DRIVER_MODULE_ORDERED(uaudio, uhub, uaudio_driver, uaudio_devclass, NULL, 0, SI_ORDER_ANY);
MODULE_DEPEND(uaudio, usb, 1, 1, 1);
MODULE_DEPEND(uaudio, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(uaudio, 1);
USB_PNP_HOST_INFO(uaudio_devs);
USB_PNP_HOST_INFO(uaudio_vendor_midi);
