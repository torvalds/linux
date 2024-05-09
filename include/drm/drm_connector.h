/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_CONNECTOR_H__
#define __DRM_CONNECTOR_H__

#include <linux/list.h>
#include <linux/llist.h>
#include <linux/ctype.h>
#include <linux/hdmi.h>
#include <linux/notifier.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_util.h>
#include <drm/drm_property.h>

#include <uapi/drm/drm_mode.h>

struct drm_connector_helper_funcs;
struct drm_modeset_acquire_ctx;
struct drm_device;
struct drm_crtc;
struct drm_encoder;
struct drm_panel;
struct drm_property;
struct drm_property_blob;
struct drm_printer;
struct drm_privacy_screen;
struct edid;
struct i2c_adapter;

enum drm_connector_force {
	DRM_FORCE_UNSPECIFIED,
	DRM_FORCE_OFF,
	DRM_FORCE_ON,         /* force on analog part normally */
	DRM_FORCE_ON_DIGITAL, /* for DVI-I use digital connector */
};

/**
 * enum drm_connector_status - status for a &drm_connector
 *
 * This enum is used to track the connector status. There are no separate
 * #defines for the uapi!
 */
enum drm_connector_status {
	/**
	 * @connector_status_connected: The connector is definitely connected to
	 * a sink device, and can be enabled.
	 */
	connector_status_connected = 1,
	/**
	 * @connector_status_disconnected: The connector isn't connected to a
	 * sink device which can be autodetect. For digital outputs like DP or
	 * HDMI (which can be realiable probed) this means there's really
	 * nothing there. It is driver-dependent whether a connector with this
	 * status can be lit up or not.
	 */
	connector_status_disconnected = 2,
	/**
	 * @connector_status_unknown: The connector's status could not be
	 * reliably detected. This happens when probing would either cause
	 * flicker (like load-detection when the connector is in use), or when a
	 * hardware resource isn't available (like when load-detection needs a
	 * free CRTC). It should be possible to light up the connector with one
	 * of the listed fallback modes. For default configuration userspace
	 * should only try to light up connectors with unknown status when
	 * there's not connector with @connector_status_connected.
	 */
	connector_status_unknown = 3,
};

/**
 * enum drm_connector_registration_state - userspace registration status for
 * a &drm_connector
 *
 * This enum is used to track the status of initializing a connector and
 * registering it with userspace, so that DRM can prevent bogus modesets on
 * connectors that no longer exist.
 */
enum drm_connector_registration_state {
	/**
	 * @DRM_CONNECTOR_INITIALIZING: The connector has just been created,
	 * but has yet to be exposed to userspace. There should be no
	 * additional restrictions to how the state of this connector may be
	 * modified.
	 */
	DRM_CONNECTOR_INITIALIZING = 0,

	/**
	 * @DRM_CONNECTOR_REGISTERED: The connector has been fully initialized
	 * and registered with sysfs, as such it has been exposed to
	 * userspace. There should be no additional restrictions to how the
	 * state of this connector may be modified.
	 */
	DRM_CONNECTOR_REGISTERED = 1,

	/**
	 * @DRM_CONNECTOR_UNREGISTERED: The connector has either been exposed
	 * to userspace and has since been unregistered and removed from
	 * userspace, or the connector was unregistered before it had a chance
	 * to be exposed to userspace (e.g. still in the
	 * @DRM_CONNECTOR_INITIALIZING state). When a connector is
	 * unregistered, there are additional restrictions to how its state
	 * may be modified:
	 *
	 * - An unregistered connector may only have its DPMS changed from
	 *   On->Off. Once DPMS is changed to Off, it may not be switched back
	 *   to On.
	 * - Modesets are not allowed on unregistered connectors, unless they
	 *   would result in disabling its assigned CRTCs. This means
	 *   disabling a CRTC on an unregistered connector is OK, but enabling
	 *   one is not.
	 * - Removing a CRTC from an unregistered connector is OK, but new
	 *   CRTCs may never be assigned to an unregistered connector.
	 */
	DRM_CONNECTOR_UNREGISTERED = 2,
};

enum subpixel_order {
	SubPixelUnknown = 0,
	SubPixelHorizontalRGB,
	SubPixelHorizontalBGR,
	SubPixelVerticalRGB,
	SubPixelVerticalBGR,
	SubPixelNone,

};

/**
 * enum drm_connector_tv_mode - Analog TV output mode
 *
 * This enum is used to indicate the TV output mode used on an analog TV
 * connector.
 *
 * WARNING: The values of this enum is uABI since they're exposed in the
 * "TV mode" connector property.
 */
enum drm_connector_tv_mode {
	/**
	 * @DRM_MODE_TV_MODE_NTSC: CCIR System M (aka 525-lines)
	 * together with the NTSC Color Encoding.
	 */
	DRM_MODE_TV_MODE_NTSC,

	/**
	 * @DRM_MODE_TV_MODE_NTSC_443: Variant of
	 * @DRM_MODE_TV_MODE_NTSC. Uses a color subcarrier frequency
	 * of 4.43 MHz.
	 */
	DRM_MODE_TV_MODE_NTSC_443,

	/**
	 * @DRM_MODE_TV_MODE_NTSC_J: Variant of @DRM_MODE_TV_MODE_NTSC
	 * used in Japan. Uses a black level equals to the blanking
	 * level.
	 */
	DRM_MODE_TV_MODE_NTSC_J,

	/**
	 * @DRM_MODE_TV_MODE_PAL: CCIR System B together with the PAL
	 * color system.
	 */
	DRM_MODE_TV_MODE_PAL,

	/**
	 * @DRM_MODE_TV_MODE_PAL_M: CCIR System M (aka 525-lines)
	 * together with the PAL color encoding
	 */
	DRM_MODE_TV_MODE_PAL_M,

	/**
	 * @DRM_MODE_TV_MODE_PAL_N: CCIR System N together with the PAL
	 * color encoding. It uses 625 lines, but has a color subcarrier
	 * frequency of 3.58MHz, the SECAM color space, and narrower
	 * channels compared to most of the other PAL variants.
	 */
	DRM_MODE_TV_MODE_PAL_N,

	/**
	 * @DRM_MODE_TV_MODE_SECAM: CCIR System B together with the
	 * SECAM color system.
	 */
	DRM_MODE_TV_MODE_SECAM,

	/**
	 * @DRM_MODE_TV_MODE_MAX: Number of analog TV output modes.
	 *
	 * Internal implementation detail; this is not uABI.
	 */
	DRM_MODE_TV_MODE_MAX,
};

/**
 * struct drm_scrambling: sink's scrambling support.
 */
struct drm_scrambling {
	/**
	 * @supported: scrambling supported for rates > 340 Mhz.
	 */
	bool supported;
	/**
	 * @low_rates: scrambling supported for rates <= 340 Mhz.
	 */
	bool low_rates;
};

/*
 * struct drm_scdc - Information about scdc capabilities of a HDMI 2.0 sink
 *
 * Provides SCDC register support and capabilities related information on a
 * HDMI 2.0 sink. In case of a HDMI 1.4 sink, all parameter must be 0.
 */
struct drm_scdc {
	/**
	 * @supported: status control & data channel present.
	 */
	bool supported;
	/**
	 * @read_request: sink is capable of generating scdc read request.
	 */
	bool read_request;
	/**
	 * @scrambling: sink's scrambling capabilities
	 */
	struct drm_scrambling scrambling;
};

/**
 * struct drm_hdmi_dsc_cap - DSC capabilities of HDMI sink
 *
 * Describes the DSC support provided by HDMI 2.1 sink.
 * The information is fetched fom additional HFVSDB blocks defined
 * for HDMI 2.1.
 */
struct drm_hdmi_dsc_cap {
	/** @v_1p2: flag for dsc1.2 version support by sink */
	bool v_1p2;

	/** @native_420: Does sink support DSC with 4:2:0 compression */
	bool native_420;

	/**
	 * @all_bpp: Does sink support all bpp with 4:4:4: or 4:2:2
	 * compressed formats
	 */
	bool all_bpp;

	/**
	 * @bpc_supported: compressed bpc supported by sink : 10, 12 or 16 bpc
	 */
	u8 bpc_supported;

	/** @max_slices: maximum number of Horizontal slices supported by */
	u8 max_slices;

	/** @clk_per_slice : max pixel clock in MHz supported per slice */
	int clk_per_slice;

	/** @max_lanes : dsc max lanes supported for Fixed rate Link training */
	u8 max_lanes;

	/** @max_frl_rate_per_lane : maximum frl rate with DSC per lane */
	u8 max_frl_rate_per_lane;

	/** @total_chunk_kbytes: max size of chunks in KBs supported per line*/
	u8 total_chunk_kbytes;
};

/**
 * struct drm_hdmi_info - runtime information about the connected HDMI sink
 *
 * Describes if a given display supports advanced HDMI 2.0 features.
 * This information is available in CEA-861-F extension blocks (like HF-VSDB).
 */
struct drm_hdmi_info {
	/** @scdc: sink's scdc support and capabilities */
	struct drm_scdc scdc;

	/**
	 * @y420_vdb_modes: bitmap of modes which can support ycbcr420
	 * output only (not normal RGB/YCBCR444/422 outputs). The max VIC
	 * defined by the CEA-861-G spec is 219, so the size is 256 bits to map
	 * up to 256 VICs.
	 */
	unsigned long y420_vdb_modes[BITS_TO_LONGS(256)];

	/**
	 * @y420_cmdb_modes: bitmap of modes which can support ycbcr420
	 * output also, along with normal HDMI outputs. The max VIC defined by
	 * the CEA-861-G spec is 219, so the size is 256 bits to map up to 256
	 * VICs.
	 */
	unsigned long y420_cmdb_modes[BITS_TO_LONGS(256)];

	/** @y420_dc_modes: bitmap of deep color support index */
	u8 y420_dc_modes;

	/** @max_frl_rate_per_lane: support fixed rate link */
	u8 max_frl_rate_per_lane;

	/** @max_lanes: supported by sink */
	u8 max_lanes;

	/** @dsc_cap: DSC capabilities of the sink */
	struct drm_hdmi_dsc_cap dsc_cap;
};

/**
 * enum drm_link_status - connector's link_status property value
 *
 * This enum is used as the connector's link status property value.
 * It is set to the values defined in uapi.
 *
 * @DRM_LINK_STATUS_GOOD: DP Link is Good as a result of successful
 *                        link training
 * @DRM_LINK_STATUS_BAD: DP Link is BAD as a result of link training
 *                       failure
 */
enum drm_link_status {
	DRM_LINK_STATUS_GOOD = DRM_MODE_LINK_STATUS_GOOD,
	DRM_LINK_STATUS_BAD = DRM_MODE_LINK_STATUS_BAD,
};

/**
 * enum drm_panel_orientation - panel_orientation info for &drm_display_info
 *
 * This enum is used to track the (LCD) panel orientation. There are no
 * separate #defines for the uapi!
 *
 * @DRM_MODE_PANEL_ORIENTATION_UNKNOWN: The drm driver has not provided any
 *					panel orientation information (normal
 *					for non panels) in this case the "panel
 *					orientation" connector prop will not be
 *					attached.
 * @DRM_MODE_PANEL_ORIENTATION_NORMAL:	The top side of the panel matches the
 *					top side of the device's casing.
 * @DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP: The top side of the panel matches the
 *					bottom side of the device's casing, iow
 *					the panel is mounted upside-down.
 * @DRM_MODE_PANEL_ORIENTATION_LEFT_UP:	The left side of the panel matches the
 *					top side of the device's casing.
 * @DRM_MODE_PANEL_ORIENTATION_RIGHT_UP: The right side of the panel matches the
 *					top side of the device's casing.
 */
enum drm_panel_orientation {
	DRM_MODE_PANEL_ORIENTATION_UNKNOWN = -1,
	DRM_MODE_PANEL_ORIENTATION_NORMAL = 0,
	DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP,
	DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
	DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

/**
 * struct drm_monitor_range_info - Panel's Monitor range in EDID for
 * &drm_display_info
 *
 * This struct is used to store a frequency range supported by panel
 * as parsed from EDID's detailed monitor range descriptor block.
 *
 * @min_vfreq: This is the min supported refresh rate in Hz from
 *             EDID's detailed monitor range.
 * @max_vfreq: This is the max supported refresh rate in Hz from
 *             EDID's detailed monitor range
 */
struct drm_monitor_range_info {
	u16 min_vfreq;
	u16 max_vfreq;
};

/**
 * struct drm_luminance_range_info - Panel's luminance range for
 * &drm_display_info. Calculated using data in EDID
 *
 * This struct is used to store a luminance range supported by panel
 * as calculated using data from EDID's static hdr metadata.
 *
 * @min_luminance: This is the min supported luminance value
 *
 * @max_luminance: This is the max supported luminance value
 */
struct drm_luminance_range_info {
	u32 min_luminance;
	u32 max_luminance;
};

/**
 * enum drm_privacy_screen_status - privacy screen status
 *
 * This enum is used to track and control the state of the integrated privacy
 * screen present on some display panels, via the "privacy-screen sw-state"
 * and "privacy-screen hw-state" properties. Note the _LOCKED enum values
 * are only valid for the "privacy-screen hw-state" property.
 *
 * @PRIVACY_SCREEN_DISABLED:
 *  The privacy-screen on the panel is disabled
 * @PRIVACY_SCREEN_ENABLED:
 *  The privacy-screen on the panel is enabled
 * @PRIVACY_SCREEN_DISABLED_LOCKED:
 *  The privacy-screen on the panel is disabled and locked (cannot be changed)
 * @PRIVACY_SCREEN_ENABLED_LOCKED:
 *  The privacy-screen on the panel is enabled and locked (cannot be changed)
 */
enum drm_privacy_screen_status {
	PRIVACY_SCREEN_DISABLED = 0,
	PRIVACY_SCREEN_ENABLED,
	PRIVACY_SCREEN_DISABLED_LOCKED,
	PRIVACY_SCREEN_ENABLED_LOCKED,
};

/**
 * enum drm_colorspace - color space
 *
 * This enum is a consolidated colorimetry list supported by HDMI and
 * DP protocol standard. The respective connectors will register
 * a property with the subset of this list (supported by that
 * respective protocol). Userspace will set the colorspace through
 * a colorspace property which will be created and exposed to
 * userspace.
 *
 * DP definitions come from the DP v2.0 spec
 * HDMI definitions come from the CTA-861-H spec
 *
 * A note on YCC and RGB variants:
 *
 * Since userspace is not aware of the encoding on the wire
 * (RGB or YCbCr), drivers are free to pick the appropriate
 * variant, regardless of what userspace selects. E.g., if
 * BT2020_RGB is selected by userspace a driver will pick
 * BT2020_YCC if the encoding on the wire is YUV444 or YUV420.
  *
 * @DRM_MODE_COLORIMETRY_DEFAULT:
 *   Driver specific behavior.
 * @DRM_MODE_COLORIMETRY_NO_DATA:
 *   Driver specific behavior.
 * @DRM_MODE_COLORIMETRY_SMPTE_170M_YCC:
 *   (HDMI)
 *   SMPTE ST 170M colorimetry format
 * @DRM_MODE_COLORIMETRY_BT709_YCC:
 *   (HDMI, DP)
 *   ITU-R BT.709 colorimetry format
 * @DRM_MODE_COLORIMETRY_XVYCC_601:
 *   (HDMI, DP)
 *   xvYCC601 colorimetry format
 * @DRM_MODE_COLORIMETRY_XVYCC_709:
 *   (HDMI, DP)
 *   xvYCC709 colorimetry format
 * @DRM_MODE_COLORIMETRY_SYCC_601:
 *   (HDMI, DP)
 *   sYCC601 colorimetry format
 * @DRM_MODE_COLORIMETRY_OPYCC_601:
 *   (HDMI, DP)
 *   opYCC601 colorimetry format
 * @DRM_MODE_COLORIMETRY_OPRGB:
 *   (HDMI, DP)
 *   opRGB colorimetry format
 * @DRM_MODE_COLORIMETRY_BT2020_CYCC:
 *   (HDMI, DP)
 *   ITU-R BT.2020 Y'c C'bc C'rc (constant luminance) colorimetry format
 * @DRM_MODE_COLORIMETRY_BT2020_RGB:
 *   (HDMI, DP)
 *   ITU-R BT.2020 R' G' B' colorimetry format
 * @DRM_MODE_COLORIMETRY_BT2020_YCC:
 *   (HDMI, DP)
 *   ITU-R BT.2020 Y' C'b C'r colorimetry format
 * @DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65:
 *   (HDMI)
 *   SMPTE ST 2113 P3D65 colorimetry format
 * @DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER:
 *   (HDMI)
 *   SMPTE ST 2113 P3DCI colorimetry format
 * @DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED:
 *   (DP)
 *   RGB wide gamut fixed point colorimetry format
 * @DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT:
 *   (DP)
 *   RGB wide gamut floating point
 *   (scRGB (IEC 61966-2-2)) colorimetry format
 * @DRM_MODE_COLORIMETRY_BT601_YCC:
 *   (DP)
 *   ITU-R BT.601 colorimetry format
 *   The DP spec does not say whether this is the 525 or the 625
 *   line version.
 * @DRM_MODE_COLORIMETRY_COUNT:
 *   Not a valid value; merely used four counting
 */
enum drm_colorspace {
	/* For Default case, driver will set the colorspace */
	DRM_MODE_COLORIMETRY_DEFAULT 		= 0,
	/* CEA 861 Normal Colorimetry options */
	DRM_MODE_COLORIMETRY_NO_DATA		= 0,
	DRM_MODE_COLORIMETRY_SMPTE_170M_YCC	= 1,
	DRM_MODE_COLORIMETRY_BT709_YCC		= 2,
	/* CEA 861 Extended Colorimetry Options */
	DRM_MODE_COLORIMETRY_XVYCC_601		= 3,
	DRM_MODE_COLORIMETRY_XVYCC_709		= 4,
	DRM_MODE_COLORIMETRY_SYCC_601		= 5,
	DRM_MODE_COLORIMETRY_OPYCC_601		= 6,
	DRM_MODE_COLORIMETRY_OPRGB		= 7,
	DRM_MODE_COLORIMETRY_BT2020_CYCC	= 8,
	DRM_MODE_COLORIMETRY_BT2020_RGB		= 9,
	DRM_MODE_COLORIMETRY_BT2020_YCC		= 10,
	/* Additional Colorimetry extension added as part of CTA 861.G */
	DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65	= 11,
	DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER	= 12,
	/* Additional Colorimetry Options added for DP 1.4a VSC Colorimetry Format */
	DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED	= 13,
	DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT	= 14,
	DRM_MODE_COLORIMETRY_BT601_YCC		= 15,
	DRM_MODE_COLORIMETRY_COUNT
};

/**
 * enum drm_bus_flags - bus_flags info for &drm_display_info
 *
 * This enum defines signal polarities and clock edge information for signals on
 * a bus as bitmask flags.
 *
 * The clock edge information is conveyed by two sets of symbols,
 * DRM_BUS_FLAGS_*_DRIVE_\* and DRM_BUS_FLAGS_*_SAMPLE_\*. When this enum is
 * used to describe a bus from the point of view of the transmitter, the
 * \*_DRIVE_\* flags should be used. When used from the point of view of the
 * receiver, the \*_SAMPLE_\* flags should be used. The \*_DRIVE_\* and
 * \*_SAMPLE_\* flags alias each other, with the \*_SAMPLE_POSEDGE and
 * \*_SAMPLE_NEGEDGE flags being equal to \*_DRIVE_NEGEDGE and \*_DRIVE_POSEDGE
 * respectively. This simplifies code as signals are usually sampled on the
 * opposite edge of the driving edge. Transmitters and receivers may however
 * need to take other signal timings into account to convert between driving
 * and sample edges.
 */
enum drm_bus_flags {
	/**
	 * @DRM_BUS_FLAG_DE_LOW:
	 *
	 * The Data Enable signal is active low
	 */
	DRM_BUS_FLAG_DE_LOW = BIT(0),

	/**
	 * @DRM_BUS_FLAG_DE_HIGH:
	 *
	 * The Data Enable signal is active high
	 */
	DRM_BUS_FLAG_DE_HIGH = BIT(1),

	/**
	 * @DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE:
	 *
	 * Data is driven on the rising edge of the pixel clock
	 */
	DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE = BIT(2),

	/**
	 * @DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE:
	 *
	 * Data is driven on the falling edge of the pixel clock
	 */
	DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE = BIT(3),

	/**
	 * @DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE:
	 *
	 * Data is sampled on the rising edge of the pixel clock
	 */
	DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE = DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,

	/**
	 * @DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE:
	 *
	 * Data is sampled on the falling edge of the pixel clock
	 */
	DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,

	/**
	 * @DRM_BUS_FLAG_DATA_MSB_TO_LSB:
	 *
	 * Data is transmitted MSB to LSB on the bus
	 */
	DRM_BUS_FLAG_DATA_MSB_TO_LSB = BIT(4),

	/**
	 * @DRM_BUS_FLAG_DATA_LSB_TO_MSB:
	 *
	 * Data is transmitted LSB to MSB on the bus
	 */
	DRM_BUS_FLAG_DATA_LSB_TO_MSB = BIT(5),

	/**
	 * @DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE:
	 *
	 * Sync signals are driven on the rising edge of the pixel clock
	 */
	DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE = BIT(6),

	/**
	 * @DRM_BUS_FLAG_SYNC_DRIVE_NEGEDGE:
	 *
	 * Sync signals are driven on the falling edge of the pixel clock
	 */
	DRM_BUS_FLAG_SYNC_DRIVE_NEGEDGE = BIT(7),

	/**
	 * @DRM_BUS_FLAG_SYNC_SAMPLE_POSEDGE:
	 *
	 * Sync signals are sampled on the rising edge of the pixel clock
	 */
	DRM_BUS_FLAG_SYNC_SAMPLE_POSEDGE = DRM_BUS_FLAG_SYNC_DRIVE_NEGEDGE,

	/**
	 * @DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE:
	 *
	 * Sync signals are sampled on the falling edge of the pixel clock
	 */
	DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE = DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE,

	/**
	 * @DRM_BUS_FLAG_SHARP_SIGNALS:
	 *
	 *  Set if the Sharp-specific signals (SPL, CLS, PS, REV) must be used
	 */
	DRM_BUS_FLAG_SHARP_SIGNALS = BIT(8),
};

/**
 * struct drm_display_info - runtime data about the connected sink
 *
 * Describes a given display (e.g. CRT or flat panel) and its limitations. For
 * fixed display sinks like built-in panels there's not much difference between
 * this and &struct drm_connector. But for sinks with a real cable this
 * structure is meant to describe all the things at the other end of the cable.
 *
 * For sinks which provide an EDID this can be filled out by calling
 * drm_add_edid_modes().
 */
struct drm_display_info {
	/**
	 * @width_mm: Physical width in mm.
	 */
	unsigned int width_mm;

	/**
	 * @height_mm: Physical height in mm.
	 */
	unsigned int height_mm;

	/**
	 * @bpc: Maximum bits per color channel. Used by HDMI and DP outputs.
	 */
	unsigned int bpc;

	/**
	 * @subpixel_order: Subpixel order of LCD panels.
	 */
	enum subpixel_order subpixel_order;

#define DRM_COLOR_FORMAT_RGB444		(1<<0)
#define DRM_COLOR_FORMAT_YCBCR444	(1<<1)
#define DRM_COLOR_FORMAT_YCBCR422	(1<<2)
#define DRM_COLOR_FORMAT_YCBCR420	(1<<3)

	/**
	 * @panel_orientation: Read only connector property for built-in panels,
	 * indicating the orientation of the panel vs the device's casing.
	 * drm_connector_init() sets this to DRM_MODE_PANEL_ORIENTATION_UNKNOWN.
	 * When not UNKNOWN this gets used by the drm_fb_helpers to rotate the
	 * fb to compensate and gets exported as prop to userspace.
	 */
	int panel_orientation;

	/**
	 * @color_formats: HDMI Color formats, selects between RGB and YCrCb
	 * modes. Used DRM_COLOR_FORMAT\_ defines, which are _not_ the same ones
	 * as used to describe the pixel format in framebuffers, and also don't
	 * match the formats in @bus_formats which are shared with v4l.
	 */
	u32 color_formats;

	/**
	 * @bus_formats: Pixel data format on the wire, somewhat redundant with
	 * @color_formats. Array of size @num_bus_formats encoded using
	 * MEDIA_BUS_FMT\_ defines shared with v4l and media drivers.
	 */
	const u32 *bus_formats;
	/**
	 * @num_bus_formats: Size of @bus_formats array.
	 */
	unsigned int num_bus_formats;

	/**
	 * @bus_flags: Additional information (like pixel signal polarity) for
	 * the pixel data on the bus, using &enum drm_bus_flags values
	 * DRM_BUS_FLAGS\_.
	 */
	u32 bus_flags;

	/**
	 * @max_tmds_clock: Maximum TMDS clock rate supported by the
	 * sink in kHz. 0 means undefined.
	 */
	int max_tmds_clock;

	/**
	 * @dvi_dual: Dual-link DVI sink?
	 */
	bool dvi_dual;

	/**
	 * @is_hdmi: True if the sink is an HDMI device.
	 *
	 * This field shall be used instead of calling
	 * drm_detect_hdmi_monitor() when possible.
	 */
	bool is_hdmi;

	/**
	 * @has_audio: True if the sink supports audio.
	 *
	 * This field shall be used instead of calling
	 * drm_detect_monitor_audio() when possible.
	 */
	bool has_audio;

	/**
	 * @has_hdmi_infoframe: Does the sink support the HDMI infoframe?
	 */
	bool has_hdmi_infoframe;

	/**
	 * @rgb_quant_range_selectable: Does the sink support selecting
	 * the RGB quantization range?
	 */
	bool rgb_quant_range_selectable;

	/**
	 * @edid_hdmi_rgb444_dc_modes: Mask of supported hdmi deep color modes
	 * in RGB 4:4:4. Even more stuff redundant with @bus_formats.
	 */
	u8 edid_hdmi_rgb444_dc_modes;

	/**
	 * @edid_hdmi_ycbcr444_dc_modes: Mask of supported hdmi deep color
	 * modes in YCbCr 4:4:4. Even more stuff redundant with @bus_formats.
	 */
	u8 edid_hdmi_ycbcr444_dc_modes;

	/**
	 * @cea_rev: CEA revision of the HDMI sink.
	 */
	u8 cea_rev;

	/**
	 * @hdmi: advance features of a HDMI sink.
	 */
	struct drm_hdmi_info hdmi;

	/**
	 * @non_desktop: Non desktop display (HMD).
	 */
	bool non_desktop;

	/**
	 * @monitor_range: Frequency range supported by monitor range descriptor
	 */
	struct drm_monitor_range_info monitor_range;

	/**
	 * @luminance_range: Luminance range supported by panel
	 */
	struct drm_luminance_range_info luminance_range;

	/**
	 * @mso_stream_count: eDP Multi-SST Operation (MSO) stream count from
	 * the DisplayID VESA vendor block. 0 for conventional Single-Stream
	 * Transport (SST), or 2 or 4 MSO streams.
	 */
	u8 mso_stream_count;

	/**
	 * @mso_pixel_overlap: eDP MSO segment pixel overlap, 0-8 pixels.
	 */
	u8 mso_pixel_overlap;

	/**
	 * @max_dsc_bpp: Maximum DSC target bitrate, if it is set to 0 the
	 * monitor's default value is used instead.
	 */
	u32 max_dsc_bpp;

	/**
	 * @vics: Array of vics_len VICs. Internal to EDID parsing.
	 */
	u8 *vics;

	/**
	 * @vics_len: Number of elements in vics. Internal to EDID parsing.
	 */
	int vics_len;

	/**
	 * @quirks: EDID based quirks. Internal to EDID parsing.
	 */
	u32 quirks;

	/**
	 * @source_physical_address: Source Physical Address from HDMI
	 * Vendor-Specific Data Block, for CEC usage.
	 *
	 * Defaults to CEC_PHYS_ADDR_INVALID (0xffff).
	 */
	u16 source_physical_address;
};

int drm_display_info_set_bus_formats(struct drm_display_info *info,
				     const u32 *formats,
				     unsigned int num_formats);

/**
 * struct drm_connector_tv_margins - TV connector related margins
 *
 * Describes the margins in pixels to put around the image on TV
 * connectors to deal with overscan.
 */
struct drm_connector_tv_margins {
	/**
	 * @bottom: Bottom margin in pixels.
	 */
	unsigned int bottom;

	/**
	 * @left: Left margin in pixels.
	 */
	unsigned int left;

	/**
	 * @right: Right margin in pixels.
	 */
	unsigned int right;

	/**
	 * @top: Top margin in pixels.
	 */
	unsigned int top;
};

/**
 * struct drm_tv_connector_state - TV connector related states
 * @select_subconnector: selected subconnector
 * @subconnector: detected subconnector
 * @margins: TV margins
 * @legacy_mode: Legacy TV mode, driver specific value
 * @mode: TV mode
 * @brightness: brightness in percent
 * @contrast: contrast in percent
 * @flicker_reduction: flicker reduction in percent
 * @overscan: overscan in percent
 * @saturation: saturation in percent
 * @hue: hue in percent
 */
struct drm_tv_connector_state {
	enum drm_mode_subconnector select_subconnector;
	enum drm_mode_subconnector subconnector;
	struct drm_connector_tv_margins margins;
	unsigned int legacy_mode;
	unsigned int mode;
	unsigned int brightness;
	unsigned int contrast;
	unsigned int flicker_reduction;
	unsigned int overscan;
	unsigned int saturation;
	unsigned int hue;
};

/**
 * struct drm_connector_state - mutable connector state
 */
struct drm_connector_state {
	/** @connector: backpointer to the connector */
	struct drm_connector *connector;

	/**
	 * @crtc: CRTC to connect connector to, NULL if disabled.
	 *
	 * Do not change this directly, use drm_atomic_set_crtc_for_connector()
	 * instead.
	 */
	struct drm_crtc *crtc;

	/**
	 * @best_encoder:
	 *
	 * Used by the atomic helpers to select the encoder, through the
	 * &drm_connector_helper_funcs.atomic_best_encoder or
	 * &drm_connector_helper_funcs.best_encoder callbacks.
	 *
	 * This is also used in the atomic helpers to map encoders to their
	 * current and previous connectors, see
	 * drm_atomic_get_old_connector_for_encoder() and
	 * drm_atomic_get_new_connector_for_encoder().
	 *
	 * NOTE: Atomic drivers must fill this out (either themselves or through
	 * helpers), for otherwise the GETCONNECTOR and GETENCODER IOCTLs will
	 * not return correct data to userspace.
	 */
	struct drm_encoder *best_encoder;

	/**
	 * @link_status: Connector link_status to keep track of whether link is
	 * GOOD or BAD to notify userspace if retraining is necessary.
	 */
	enum drm_link_status link_status;

	/** @state: backpointer to global drm_atomic_state */
	struct drm_atomic_state *state;

	/**
	 * @commit: Tracks the pending commit to prevent use-after-free conditions.
	 *
	 * Is only set when @crtc is NULL.
	 */
	struct drm_crtc_commit *commit;

	/** @tv: TV connector state */
	struct drm_tv_connector_state tv;

	/**
	 * @self_refresh_aware:
	 *
	 * This tracks whether a connector is aware of the self refresh state.
	 * It should be set to true for those connector implementations which
	 * understand the self refresh state. This is needed since the crtc
	 * registers the self refresh helpers and it doesn't know if the
	 * connectors downstream have implemented self refresh entry/exit.
	 *
	 * Drivers should set this to true in atomic_check if they know how to
	 * handle self_refresh requests.
	 */
	bool self_refresh_aware;

	/**
	 * @picture_aspect_ratio: Connector property to control the
	 * HDMI infoframe aspect ratio setting.
	 *
	 * The %DRM_MODE_PICTURE_ASPECT_\* values much match the
	 * values for &enum hdmi_picture_aspect
	 */
	enum hdmi_picture_aspect picture_aspect_ratio;

	/**
	 * @content_type: Connector property to control the
	 * HDMI infoframe content type setting.
	 * The %DRM_MODE_CONTENT_TYPE_\* values much
	 * match the values.
	 */
	unsigned int content_type;

	/**
	 * @hdcp_content_type: Connector property to pass the type of
	 * protected content. This is most commonly used for HDCP.
	 */
	unsigned int hdcp_content_type;

	/**
	 * @scaling_mode: Connector property to control the
	 * upscaling, mostly used for built-in panels.
	 */
	unsigned int scaling_mode;

	/**
	 * @content_protection: Connector property to request content
	 * protection. This is most commonly used for HDCP.
	 */
	unsigned int content_protection;

	/**
	 * @colorspace: State variable for Connector property to request
	 * colorspace change on Sink. This is most commonly used to switch
	 * to wider color gamuts like BT2020.
	 */
	enum drm_colorspace colorspace;

	/**
	 * @writeback_job: Writeback job for writeback connectors
	 *
	 * Holds the framebuffer and out-fence for a writeback connector. As
	 * the writeback completion may be asynchronous to the normal commit
	 * cycle, the writeback job lifetime is managed separately from the
	 * normal atomic state by this object.
	 *
	 * See also: drm_writeback_queue_job() and
	 * drm_writeback_signal_completion()
	 */
	struct drm_writeback_job *writeback_job;

	/**
	 * @max_requested_bpc: Connector property to limit the maximum bit
	 * depth of the pixels.
	 */
	u8 max_requested_bpc;

	/**
	 * @max_bpc: Connector max_bpc based on the requested max_bpc property
	 * and the connector bpc limitations obtained from edid.
	 */
	u8 max_bpc;

	/**
	 * @privacy_screen_sw_state: See :ref:`Standard Connector
	 * Properties<standard_connector_properties>`
	 */
	enum drm_privacy_screen_status privacy_screen_sw_state;

	/**
	 * @hdr_output_metadata:
	 * DRM blob property for HDR output metadata
	 */
	struct drm_property_blob *hdr_output_metadata;
};

/**
 * struct drm_connector_funcs - control connectors on a given device
 *
 * Each CRTC may have one or more connectors attached to it.  The functions
 * below allow the core DRM code to control connectors, enumerate available modes,
 * etc.
 */
struct drm_connector_funcs {
	/**
	 * @dpms:
	 *
	 * Legacy entry point to set the per-connector DPMS state. Legacy DPMS
	 * is exposed as a standard property on the connector, but diverted to
	 * this callback in the drm core. Note that atomic drivers don't
	 * implement the 4 level DPMS support on the connector any more, but
	 * instead only have an on/off "ACTIVE" property on the CRTC object.
	 *
	 * This hook is not used by atomic drivers, remapping of the legacy DPMS
	 * property is entirely handled in the DRM core.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*dpms)(struct drm_connector *connector, int mode);

	/**
	 * @reset:
	 *
	 * Reset connector hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 *
	 * Atomic drivers can use drm_atomic_helper_connector_reset() to reset
	 * atomic state using this hook.
	 */
	void (*reset)(struct drm_connector *connector);

	/**
	 * @detect:
	 *
	 * Check to see if anything is attached to the connector. The parameter
	 * force is set to false whilst polling, true when checking the
	 * connector due to a user request. force can be used by the driver to
	 * avoid expensive, destructive operations during automated probing.
	 *
	 * This callback is optional, if not implemented the connector will be
	 * considered as always being attached.
	 *
	 * FIXME:
	 *
	 * Note that this hook is only called by the probe helper. It's not in
	 * the helper library vtable purely for historical reasons. The only DRM
	 * core	entry point to probe connector state is @fill_modes.
	 *
	 * Note that the helper library will already hold
	 * &drm_mode_config.connection_mutex. Drivers which need to grab additional
	 * locks to avoid races with concurrent modeset changes need to use
	 * &drm_connector_helper_funcs.detect_ctx instead.
	 *
	 * Also note that this callback can be called no matter the
	 * state the connector is in. Drivers that need the underlying
	 * device to be powered to perform the detection will first need
	 * to make sure it's been properly enabled.
	 *
	 * RETURNS:
	 *
	 * drm_connector_status indicating the connector's status.
	 */
	enum drm_connector_status (*detect)(struct drm_connector *connector,
					    bool force);

	/**
	 * @force:
	 *
	 * This function is called to update internal encoder state when the
	 * connector is forced to a certain state by userspace, either through
	 * the sysfs interfaces or on the kernel cmdline. In that case the
	 * @detect callback isn't called.
	 *
	 * FIXME:
	 *
	 * Note that this hook is only called by the probe helper. It's not in
	 * the helper library vtable purely for historical reasons. The only DRM
	 * core	entry point to probe connector state is @fill_modes.
	 */
	void (*force)(struct drm_connector *connector);

	/**
	 * @fill_modes:
	 *
	 * Entry point for output detection and basic mode validation. The
	 * driver should reprobe the output if needed (e.g. when hotplug
	 * handling is unreliable), add all detected modes to &drm_connector.modes
	 * and filter out any the device can't support in any configuration. It
	 * also needs to filter out any modes wider or higher than the
	 * parameters max_width and max_height indicate.
	 *
	 * The drivers must also prune any modes no longer valid from
	 * &drm_connector.modes. Furthermore it must update
	 * &drm_connector.status and &drm_connector.edid.  If no EDID has been
	 * received for this output connector->edid must be NULL.
	 *
	 * Drivers using the probe helpers should use
	 * drm_helper_probe_single_connector_modes() to implement this
	 * function.
	 *
	 * RETURNS:
	 *
	 * The number of modes detected and filled into &drm_connector.modes.
	 */
	int (*fill_modes)(struct drm_connector *connector, uint32_t max_width, uint32_t max_height);

	/**
	 * @set_property:
	 *
	 * This is the legacy entry point to update a property attached to the
	 * connector.
	 *
	 * This callback is optional if the driver does not support any legacy
	 * driver-private properties. For atomic drivers it is not used because
	 * property handling is done entirely in the DRM core.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_property)(struct drm_connector *connector, struct drm_property *property,
			     uint64_t val);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the connector, light backlight control, i2c,
	 * DP aux or similar interfaces. It is called late in the driver load
	 * sequence from drm_connector_register() when registering all the
	 * core drm connector interfaces. Everything added from this callback
	 * should be unregistered in the early_unregister callback.
	 *
	 * This is called while holding &drm_connector.mutex.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_connector *connector);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the connector from
	 * late_register(). It is called from drm_connector_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 *
	 * This is called while holding &drm_connector.mutex.
	 */
	void (*early_unregister)(struct drm_connector *connector);

	/**
	 * @destroy:
	 *
	 * Clean up connector resources. This is called at driver unload time
	 * through drm_mode_config_cleanup(). It can also be called at runtime
	 * when a connector is being hot-unplugged for drivers that support
	 * connector hotplugging (e.g. DisplayPort MST).
	 */
	void (*destroy)(struct drm_connector *connector);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current atomic state for this connector and return it.
	 * The core and helpers guarantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling &drm_mode_config_funcs.atomic_commit) will be
	 * cleaned up by calling the @atomic_destroy_state hook in this
	 * structure.
	 *
	 * This callback is mandatory for atomic drivers.
	 *
	 * Atomic drivers which don't subclass &struct drm_connector_state should use
	 * drm_atomic_helper_connector_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_connector_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before &drm_connector.state has been
	 * initialized correctly.
	 *
	 * NOTE:
	 *
	 * If the duplicate state references refcounted resources this hook must
	 * acquire a reference for each of them. The driver must release these
	 * references again in @atomic_destroy_state.
	 *
	 * RETURNS:
	 *
	 * Duplicated atomic state or NULL when the allocation failed.
	 */
	struct drm_connector_state *(*atomic_duplicate_state)(struct drm_connector *connector);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a state duplicated with @atomic_duplicate_state and release
	 * or unreference all resources it references
	 *
	 * This callback is mandatory for atomic drivers.
	 */
	void (*atomic_destroy_state)(struct drm_connector *connector,
				     struct drm_connector_state *state);

	/**
	 * @atomic_set_property:
	 *
	 * Decode a driver-private property value and store the decoded value
	 * into the passed-in state structure. Since the atomic core decodes all
	 * standardized properties (even for extensions beyond the core set of
	 * properties which might not be implemented by all drivers) this
	 * requires drivers to subclass the state structure.
	 *
	 * Such driver-private properties should really only be implemented for
	 * truly hardware/vendor specific state. Instead it is preferred to
	 * standardize atomic extension and decode the properties used to expose
	 * such an extension in the core.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_connector_set_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * NOTE:
	 *
	 * This function is called in the state assembly phase of atomic
	 * modesets, which can be aborted for any reason (including on
	 * userspace's request to just check whether a configuration would be
	 * possible). Drivers MUST NOT touch any persistent state (hardware or
	 * software) or data structures except the passed in @state parameter.
	 *
	 * Also since userspace controls in which order properties are set this
	 * function must not do any input validation (since the state update is
	 * incomplete and hence likely inconsistent). Instead any such input
	 * validation must be done in the various atomic_check callbacks.
	 *
	 * RETURNS:
	 *
	 * 0 if the property has been found, -EINVAL if the property isn't
	 * implemented by the driver (which shouldn't ever happen, the core only
	 * asks for properties attached to this connector). No other validation
	 * is allowed by the driver. The core already checks that the property
	 * value is within the range (integer, valid enum value, ...) the driver
	 * set when registering the property.
	 */
	int (*atomic_set_property)(struct drm_connector *connector,
				   struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t val);

	/**
	 * @atomic_get_property:
	 *
	 * Reads out the decoded driver-private property. This is used to
	 * implement the GETCONNECTOR IOCTL.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_connector_get_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the property isn't implemented by the
	 * driver (which shouldn't ever happen, the core only asks for
	 * properties attached to this connector).
	 */
	int (*atomic_get_property)(struct drm_connector *connector,
				   const struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t *val);

	/**
	 * @atomic_print_state:
	 *
	 * If driver subclasses &struct drm_connector_state, it should implement
	 * this optional hook for printing additional driver specific state.
	 *
	 * Do not call this directly, use drm_atomic_connector_print_state()
	 * instead.
	 */
	void (*atomic_print_state)(struct drm_printer *p,
				   const struct drm_connector_state *state);

	/**
	 * @oob_hotplug_event:
	 *
	 * This will get called when a hotplug-event for a drm-connector
	 * has been received from a source outside the display driver / device.
	 */
	void (*oob_hotplug_event)(struct drm_connector *connector,
				  enum drm_connector_status status);

	/**
	 * @debugfs_init:
	 *
	 * Allows connectors to create connector-specific debugfs files.
	 */
	void (*debugfs_init)(struct drm_connector *connector, struct dentry *root);
};

/**
 * struct drm_cmdline_mode - DRM Mode passed through the kernel command-line
 *
 * Each connector can have an initial mode with additional options
 * passed through the kernel command line. This structure allows to
 * express those parameters and will be filled by the command-line
 * parser.
 */
struct drm_cmdline_mode {
	/**
	 * @name:
	 *
	 * Name of the mode.
	 */
	char name[DRM_DISPLAY_MODE_LEN];

	/**
	 * @specified:
	 *
	 * Has a mode been read from the command-line?
	 */
	bool specified;

	/**
	 * @refresh_specified:
	 *
	 * Did the mode have a preferred refresh rate?
	 */
	bool refresh_specified;

	/**
	 * @bpp_specified:
	 *
	 * Did the mode have a preferred BPP?
	 */
	bool bpp_specified;

	/**
	 * @pixel_clock:
	 *
	 * Pixel Clock in kHz. Optional.
	 */
	unsigned int pixel_clock;

	/**
	 * @xres:
	 *
	 * Active resolution on the X axis, in pixels.
	 */
	int xres;

	/**
	 * @yres:
	 *
	 * Active resolution on the Y axis, in pixels.
	 */
	int yres;

	/**
	 * @bpp:
	 *
	 * Bits per pixels for the mode.
	 */
	int bpp;

	/**
	 * @refresh:
	 *
	 * Refresh rate, in Hertz.
	 */
	int refresh;

	/**
	 * @rb:
	 *
	 * Do we need to use reduced blanking?
	 */
	bool rb;

	/**
	 * @interlace:
	 *
	 * The mode is interlaced.
	 */
	bool interlace;

	/**
	 * @cvt:
	 *
	 * The timings will be calculated using the VESA Coordinated
	 * Video Timings instead of looking up the mode from a table.
	 */
	bool cvt;

	/**
	 * @margins:
	 *
	 * Add margins to the mode calculation (1.8% of xres rounded
	 * down to 8 pixels and 1.8% of yres).
	 */
	bool margins;

	/**
	 * @force:
	 *
	 * Ignore the hotplug state of the connector, and force its
	 * state to one of the DRM_FORCE_* values.
	 */
	enum drm_connector_force force;

	/**
	 * @rotation_reflection:
	 *
	 * Initial rotation and reflection of the mode setup from the
	 * command line. See DRM_MODE_ROTATE_* and
	 * DRM_MODE_REFLECT_*. The only rotations supported are
	 * DRM_MODE_ROTATE_0 and DRM_MODE_ROTATE_180.
	 */
	unsigned int rotation_reflection;

	/**
	 * @panel_orientation:
	 *
	 * drm-connector "panel orientation" property override value,
	 * DRM_MODE_PANEL_ORIENTATION_UNKNOWN if not set.
	 */
	enum drm_panel_orientation panel_orientation;

	/**
	 * @tv_margins: TV margins to apply to the mode.
	 */
	struct drm_connector_tv_margins tv_margins;

	/**
	 * @tv_mode: TV mode standard. See DRM_MODE_TV_MODE_*.
	 */
	enum drm_connector_tv_mode tv_mode;

	/**
	 * @tv_mode_specified:
	 *
	 * Did the mode have a preferred TV mode?
	 */
	bool tv_mode_specified;
};

/**
 * struct drm_connector - central DRM connector control structure
 *
 * Each connector may be connected to one or more CRTCs, or may be clonable by
 * another connector if they can share a CRTC.  Each connector also has a specific
 * position in the broader display (referred to as a 'screen' though it could
 * span multiple monitors).
 */
struct drm_connector {
	/** @dev: parent DRM device */
	struct drm_device *dev;
	/** @kdev: kernel device for sysfs attributes */
	struct device *kdev;
	/** @attr: sysfs attributes */
	struct device_attribute *attr;
	/**
	 * @fwnode: associated fwnode supplied by platform firmware
	 *
	 * Drivers can set this to associate a fwnode with a connector, drivers
	 * are expected to get a reference on the fwnode when setting this.
	 * drm_connector_cleanup() will call fwnode_handle_put() on this.
	 */
	struct fwnode_handle *fwnode;

	/**
	 * @head:
	 *
	 * List of all connectors on a @dev, linked from
	 * &drm_mode_config.connector_list. Protected by
	 * &drm_mode_config.connector_list_lock, but please only use
	 * &drm_connector_list_iter to walk this list.
	 */
	struct list_head head;

	/**
	 * @global_connector_list_entry:
	 *
	 * Connector entry in the global connector-list, used by
	 * drm_connector_find_by_fwnode().
	 */
	struct list_head global_connector_list_entry;

	/** @base: base KMS object */
	struct drm_mode_object base;

	/** @name: human readable name, can be overwritten by the driver */
	char *name;

	/**
	 * @mutex: Lock for general connector state, but currently only protects
	 * @registered. Most of the connector state is still protected by
	 * &drm_mode_config.mutex.
	 */
	struct mutex mutex;

	/**
	 * @index: Compacted connector index, which matches the position inside
	 * the mode_config.list for drivers not supporting hot-add/removing. Can
	 * be used as an array index. It is invariant over the lifetime of the
	 * connector.
	 */
	unsigned index;

	/**
	 * @connector_type:
	 * one of the DRM_MODE_CONNECTOR_<foo> types from drm_mode.h
	 */
	int connector_type;
	/** @connector_type_id: index into connector type enum */
	int connector_type_id;
	/**
	 * @interlace_allowed:
	 * Can this connector handle interlaced modes? Only used by
	 * drm_helper_probe_single_connector_modes() for mode filtering.
	 */
	bool interlace_allowed;
	/**
	 * @doublescan_allowed:
	 * Can this connector handle doublescan? Only used by
	 * drm_helper_probe_single_connector_modes() for mode filtering.
	 */
	bool doublescan_allowed;
	/**
	 * @stereo_allowed:
	 * Can this connector handle stereo modes? Only used by
	 * drm_helper_probe_single_connector_modes() for mode filtering.
	 */
	bool stereo_allowed;

	/**
	 * @ycbcr_420_allowed : This bool indicates if this connector is
	 * capable of handling YCBCR 420 output. While parsing the EDID
	 * blocks it's very helpful to know if the source is capable of
	 * handling YCBCR 420 outputs.
	 */
	bool ycbcr_420_allowed;

	/**
	 * @registration_state: Is this connector initializing, exposed
	 * (registered) with userspace, or unregistered?
	 *
	 * Protected by @mutex.
	 */
	enum drm_connector_registration_state registration_state;

	/**
	 * @modes:
	 * Modes available on this connector (from fill_modes() + user).
	 * Protected by &drm_mode_config.mutex.
	 */
	struct list_head modes;

	/**
	 * @status:
	 * One of the drm_connector_status enums (connected, not, or unknown).
	 * Protected by &drm_mode_config.mutex.
	 */
	enum drm_connector_status status;

	/**
	 * @probed_modes:
	 * These are modes added by probing with DDC or the BIOS, before
	 * filtering is applied. Used by the probe helpers. Protected by
	 * &drm_mode_config.mutex.
	 */
	struct list_head probed_modes;

	/**
	 * @display_info: Display information is filled from EDID information
	 * when a display is detected. For non hot-pluggable displays such as
	 * flat panels in embedded systems, the driver should initialize the
	 * &drm_display_info.width_mm and &drm_display_info.height_mm fields
	 * with the physical size of the display.
	 *
	 * Protected by &drm_mode_config.mutex.
	 */
	struct drm_display_info display_info;

	/** @funcs: connector control functions */
	const struct drm_connector_funcs *funcs;

	/**
	 * @edid_blob_ptr: DRM property containing EDID if present. Protected by
	 * &drm_mode_config.mutex. This should be updated only by calling
	 * drm_connector_update_edid_property().
	 */
	struct drm_property_blob *edid_blob_ptr;

	/** @properties: property tracking for this connector */
	struct drm_object_properties properties;

	/**
	 * @scaling_mode_property: Optional atomic property to control the
	 * upscaling. See drm_connector_attach_content_protection_property().
	 */
	struct drm_property *scaling_mode_property;

	/**
	 * @vrr_capable_property: Optional property to help userspace
	 * query hardware support for variable refresh rate on a connector.
	 * connector. Drivers can add the property to a connector by
	 * calling drm_connector_attach_vrr_capable_property().
	 *
	 * This should be updated only by calling
	 * drm_connector_set_vrr_capable_property().
	 */
	struct drm_property *vrr_capable_property;

	/**
	 * @colorspace_property: Connector property to set the suitable
	 * colorspace supported by the sink.
	 */
	struct drm_property *colorspace_property;

	/**
	 * @path_blob_ptr:
	 *
	 * DRM blob property data for the DP MST path property. This should only
	 * be updated by calling drm_connector_set_path_property().
	 */
	struct drm_property_blob *path_blob_ptr;

	/**
	 * @max_bpc_property: Default connector property for the max bpc to be
	 * driven out of the connector.
	 */
	struct drm_property *max_bpc_property;

	/** @privacy_screen: drm_privacy_screen for this connector, or NULL. */
	struct drm_privacy_screen *privacy_screen;

	/** @privacy_screen_notifier: privacy-screen notifier_block */
	struct notifier_block privacy_screen_notifier;

	/**
	 * @privacy_screen_sw_state_property: Optional atomic property for the
	 * connector to control the integrated privacy screen.
	 */
	struct drm_property *privacy_screen_sw_state_property;

	/**
	 * @privacy_screen_hw_state_property: Optional atomic property for the
	 * connector to report the actual integrated privacy screen state.
	 */
	struct drm_property *privacy_screen_hw_state_property;

#define DRM_CONNECTOR_POLL_HPD (1 << 0)
#define DRM_CONNECTOR_POLL_CONNECT (1 << 1)
#define DRM_CONNECTOR_POLL_DISCONNECT (1 << 2)

	/**
	 * @polled:
	 *
	 * Connector polling mode, a combination of
	 *
	 * DRM_CONNECTOR_POLL_HPD
	 *     The connector generates hotplug events and doesn't need to be
	 *     periodically polled. The CONNECT and DISCONNECT flags must not
	 *     be set together with the HPD flag.
	 *
	 * DRM_CONNECTOR_POLL_CONNECT
	 *     Periodically poll the connector for connection.
	 *
	 * DRM_CONNECTOR_POLL_DISCONNECT
	 *     Periodically poll the connector for disconnection, without
	 *     causing flickering even when the connector is in use. DACs should
	 *     rarely do this without a lot of testing.
	 *
	 * Set to 0 for connectors that don't support connection status
	 * discovery.
	 */
	uint8_t polled;

	/**
	 * @dpms: Current dpms state. For legacy drivers the
	 * &drm_connector_funcs.dpms callback must update this. For atomic
	 * drivers, this is handled by the core atomic code, and drivers must
	 * only take &drm_crtc_state.active into account.
	 */
	int dpms;

	/** @helper_private: mid-layer private data */
	const struct drm_connector_helper_funcs *helper_private;

	/** @cmdline_mode: mode line parsed from the kernel cmdline for this connector */
	struct drm_cmdline_mode cmdline_mode;
	/** @force: a DRM_FORCE_<foo> state for forced mode sets */
	enum drm_connector_force force;

	/**
	 * @edid_override: Override EDID set via debugfs.
	 *
	 * Do not modify or access outside of the drm_edid_override_* family of
	 * functions.
	 */
	const struct drm_edid *edid_override;

	/**
	 * @edid_override_mutex: Protect access to edid_override.
	 */
	struct mutex edid_override_mutex;

	/** @epoch_counter: used to detect any other changes in connector, besides status */
	u64 epoch_counter;

	/**
	 * @possible_encoders: Bit mask of encoders that can drive this
	 * connector, drm_encoder_index() determines the index into the bitfield
	 * and the bits are set with drm_connector_attach_encoder().
	 */
	u32 possible_encoders;

	/**
	 * @encoder: Currently bound encoder driving this connector, if any.
	 * Only really meaningful for non-atomic drivers. Atomic drivers should
	 * instead look at &drm_connector_state.best_encoder, and in case they
	 * need the CRTC driving this output, &drm_connector_state.crtc.
	 */
	struct drm_encoder *encoder;

#define MAX_ELD_BYTES	128
	/** @eld: EDID-like data, if present */
	uint8_t eld[MAX_ELD_BYTES];
	/** @latency_present: AV delay info from ELD, if found */
	bool latency_present[2];
	/**
	 * @video_latency: Video latency info from ELD, if found.
	 * [0]: progressive, [1]: interlaced
	 */
	int video_latency[2];
	/**
	 * @audio_latency: audio latency info from ELD, if found
	 * [0]: progressive, [1]: interlaced
	 */
	int audio_latency[2];

	/**
	 * @ddc: associated ddc adapter.
	 * A connector usually has its associated ddc adapter. If a driver uses
	 * this field, then an appropriate symbolic link is created in connector
	 * sysfs directory to make it easy for the user to tell which i2c
	 * adapter is for a particular display.
	 *
	 * The field should be set by calling drm_connector_init_with_ddc().
	 */
	struct i2c_adapter *ddc;

	/**
	 * @null_edid_counter: track sinks that give us all zeros for the EDID.
	 * Needed to workaround some HW bugs where we get all 0s
	 */
	int null_edid_counter;

	/** @bad_edid_counter: track sinks that give us an EDID with invalid checksum */
	unsigned bad_edid_counter;

	/**
	 * @edid_corrupt: Indicates whether the last read EDID was corrupt. Used
	 * in Displayport compliance testing - Displayport Link CTS Core 1.2
	 * rev1.1 4.2.2.6
	 */
	bool edid_corrupt;
	/**
	 * @real_edid_checksum: real edid checksum for corrupted edid block.
	 * Required in Displayport 1.4 compliance testing
	 * rev1.1 4.2.2.6
	 */
	u8 real_edid_checksum;

	/** @debugfs_entry: debugfs directory for this connector */
	struct dentry *debugfs_entry;

	/**
	 * @state:
	 *
	 * Current atomic state for this connector.
	 *
	 * This is protected by &drm_mode_config.connection_mutex. Note that
	 * nonblocking atomic commits access the current connector state without
	 * taking locks. Either by going through the &struct drm_atomic_state
	 * pointers, see for_each_oldnew_connector_in_state(),
	 * for_each_old_connector_in_state() and
	 * for_each_new_connector_in_state(). Or through careful ordering of
	 * atomic commit operations as implemented in the atomic helpers, see
	 * &struct drm_crtc_commit.
	 */
	struct drm_connector_state *state;

	/* DisplayID bits. FIXME: Extract into a substruct? */

	/**
	 * @tile_blob_ptr:
	 *
	 * DRM blob property data for the tile property (used mostly by DP MST).
	 * This is meant for screens which are driven through separate display
	 * pipelines represented by &drm_crtc, which might not be running with
	 * genlocked clocks. For tiled panels which are genlocked, like
	 * dual-link LVDS or dual-link DSI, the driver should try to not expose
	 * the tiling and virtualize both &drm_crtc and &drm_plane if needed.
	 *
	 * This should only be updated by calling
	 * drm_connector_set_tile_property().
	 */
	struct drm_property_blob *tile_blob_ptr;

	/** @has_tile: is this connector connected to a tiled monitor */
	bool has_tile;
	/** @tile_group: tile group for the connected monitor */
	struct drm_tile_group *tile_group;
	/** @tile_is_single_monitor: whether the tile is one monitor housing */
	bool tile_is_single_monitor;

	/** @num_h_tile: number of horizontal tiles in the tile group */
	/** @num_v_tile: number of vertical tiles in the tile group */
	uint8_t num_h_tile, num_v_tile;
	/** @tile_h_loc: horizontal location of this tile */
	/** @tile_v_loc: vertical location of this tile */
	uint8_t tile_h_loc, tile_v_loc;
	/** @tile_h_size: horizontal size of this tile. */
	/** @tile_v_size: vertical size of this tile. */
	uint16_t tile_h_size, tile_v_size;

	/**
	 * @free_node:
	 *
	 * List used only by &drm_connector_list_iter to be able to clean up a
	 * connector from any context, in conjunction with
	 * &drm_mode_config.connector_free_work.
	 */
	struct llist_node free_node;

	/** @hdr_sink_metadata: HDR Metadata Information read from sink */
	struct hdr_sink_metadata hdr_sink_metadata;
};

#define obj_to_connector(x) container_of(x, struct drm_connector, base)

int drm_connector_init(struct drm_device *dev,
		       struct drm_connector *connector,
		       const struct drm_connector_funcs *funcs,
		       int connector_type);
int drm_connector_init_with_ddc(struct drm_device *dev,
				struct drm_connector *connector,
				const struct drm_connector_funcs *funcs,
				int connector_type,
				struct i2c_adapter *ddc);
int drmm_connector_init(struct drm_device *dev,
			struct drm_connector *connector,
			const struct drm_connector_funcs *funcs,
			int connector_type,
			struct i2c_adapter *ddc);
void drm_connector_attach_edid_property(struct drm_connector *connector);
int drm_connector_register(struct drm_connector *connector);
void drm_connector_unregister(struct drm_connector *connector);
int drm_connector_attach_encoder(struct drm_connector *connector,
				      struct drm_encoder *encoder);

void drm_connector_cleanup(struct drm_connector *connector);

static inline unsigned int drm_connector_index(const struct drm_connector *connector)
{
	return connector->index;
}

static inline u32 drm_connector_mask(const struct drm_connector *connector)
{
	return 1 << connector->index;
}

/**
 * drm_connector_lookup - lookup connector object
 * @dev: DRM device
 * @file_priv: drm file to check for lease against.
 * @id: connector object id
 *
 * This function looks up the connector object specified by id
 * add takes a reference to it.
 */
static inline struct drm_connector *drm_connector_lookup(struct drm_device *dev,
		struct drm_file *file_priv,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_CONNECTOR);
	return mo ? obj_to_connector(mo) : NULL;
}

/**
 * drm_connector_get - acquire a connector reference
 * @connector: DRM connector
 *
 * This function increments the connector's refcount.
 */
static inline void drm_connector_get(struct drm_connector *connector)
{
	drm_mode_object_get(&connector->base);
}

/**
 * drm_connector_put - release a connector reference
 * @connector: DRM connector
 *
 * This function decrements the connector's reference count and frees the
 * object if the reference count drops to zero.
 */
static inline void drm_connector_put(struct drm_connector *connector)
{
	drm_mode_object_put(&connector->base);
}

/**
 * drm_connector_is_unregistered - has the connector been unregistered from
 * userspace?
 * @connector: DRM connector
 *
 * Checks whether or not @connector has been unregistered from userspace.
 *
 * Returns:
 * True if the connector was unregistered, false if the connector is
 * registered or has not yet been registered with userspace.
 */
static inline bool
drm_connector_is_unregistered(struct drm_connector *connector)
{
	return READ_ONCE(connector->registration_state) ==
		DRM_CONNECTOR_UNREGISTERED;
}

void drm_connector_oob_hotplug_event(struct fwnode_handle *connector_fwnode,
				     enum drm_connector_status status);
const char *drm_get_connector_type_name(unsigned int connector_type);
const char *drm_get_connector_status_name(enum drm_connector_status status);
const char *drm_get_subpixel_order_name(enum subpixel_order order);
const char *drm_get_dpms_name(int val);
const char *drm_get_dvi_i_subconnector_name(int val);
const char *drm_get_dvi_i_select_name(int val);
const char *drm_get_tv_mode_name(int val);
const char *drm_get_tv_subconnector_name(int val);
const char *drm_get_tv_select_name(int val);
const char *drm_get_dp_subconnector_name(int val);
const char *drm_get_content_protection_name(int val);
const char *drm_get_hdcp_content_type_name(int val);

int drm_get_tv_mode_from_name(const char *name, size_t len);

int drm_mode_create_dvi_i_properties(struct drm_device *dev);
void drm_connector_attach_dp_subconnector_property(struct drm_connector *connector);

int drm_mode_create_tv_margin_properties(struct drm_device *dev);
int drm_mode_create_tv_properties_legacy(struct drm_device *dev,
					 unsigned int num_modes,
					 const char * const modes[]);
int drm_mode_create_tv_properties(struct drm_device *dev,
				  unsigned int supported_tv_modes);
void drm_connector_attach_tv_margin_properties(struct drm_connector *conn);
int drm_mode_create_scaling_mode_property(struct drm_device *dev);
int drm_connector_attach_content_type_property(struct drm_connector *dev);
int drm_connector_attach_scaling_mode_property(struct drm_connector *connector,
					       u32 scaling_mode_mask);
int drm_connector_attach_vrr_capable_property(
		struct drm_connector *connector);
int drm_connector_attach_colorspace_property(struct drm_connector *connector);
int drm_connector_attach_hdr_output_metadata_property(struct drm_connector *connector);
bool drm_connector_atomic_hdr_metadata_equal(struct drm_connector_state *old_state,
					     struct drm_connector_state *new_state);
int drm_mode_create_aspect_ratio_property(struct drm_device *dev);
int drm_mode_create_hdmi_colorspace_property(struct drm_connector *connector,
					     u32 supported_colorspaces);
int drm_mode_create_dp_colorspace_property(struct drm_connector *connector,
					   u32 supported_colorspaces);
int drm_mode_create_content_type_property(struct drm_device *dev);
int drm_mode_create_suggested_offset_properties(struct drm_device *dev);

int drm_connector_set_path_property(struct drm_connector *connector,
				    const char *path);
int drm_connector_set_tile_property(struct drm_connector *connector);
int drm_connector_update_edid_property(struct drm_connector *connector,
				       const struct edid *edid);
void drm_connector_set_link_status_property(struct drm_connector *connector,
					    uint64_t link_status);
void drm_connector_set_vrr_capable_property(
		struct drm_connector *connector, bool capable);
int drm_connector_set_panel_orientation(
	struct drm_connector *connector,
	enum drm_panel_orientation panel_orientation);
int drm_connector_set_panel_orientation_with_quirk(
	struct drm_connector *connector,
	enum drm_panel_orientation panel_orientation,
	int width, int height);
int drm_connector_set_orientation_from_panel(
	struct drm_connector *connector,
	struct drm_panel *panel);
int drm_connector_attach_max_bpc_property(struct drm_connector *connector,
					  int min, int max);
void drm_connector_create_privacy_screen_properties(struct drm_connector *conn);
void drm_connector_attach_privacy_screen_properties(struct drm_connector *conn);
void drm_connector_attach_privacy_screen_provider(
	struct drm_connector *connector, struct drm_privacy_screen *priv);
void drm_connector_update_privacy_screen(const struct drm_connector_state *connector_state);

/**
 * struct drm_tile_group - Tile group metadata
 * @refcount: reference count
 * @dev: DRM device
 * @id: tile group id exposed to userspace
 * @group_data: Sink-private data identifying this group
 *
 * @group_data corresponds to displayid vend/prod/serial for external screens
 * with an EDID.
 */
struct drm_tile_group {
	struct kref refcount;
	struct drm_device *dev;
	int id;
	u8 group_data[8];
};

struct drm_tile_group *drm_mode_create_tile_group(struct drm_device *dev,
						  const char topology[8]);
struct drm_tile_group *drm_mode_get_tile_group(struct drm_device *dev,
					       const char topology[8]);
void drm_mode_put_tile_group(struct drm_device *dev,
			     struct drm_tile_group *tg);

/**
 * struct drm_connector_list_iter - connector_list iterator
 *
 * This iterator tracks state needed to be able to walk the connector_list
 * within struct drm_mode_config. Only use together with
 * drm_connector_list_iter_begin(), drm_connector_list_iter_end() and
 * drm_connector_list_iter_next() respectively the convenience macro
 * drm_for_each_connector_iter().
 *
 * Note that the return value of drm_connector_list_iter_next() is only valid
 * up to the next drm_connector_list_iter_next() or
 * drm_connector_list_iter_end() call. If you want to use the connector later,
 * then you need to grab your own reference first using drm_connector_get().
 */
struct drm_connector_list_iter {
/* private: */
	struct drm_device *dev;
	struct drm_connector *conn;
};

void drm_connector_list_iter_begin(struct drm_device *dev,
				   struct drm_connector_list_iter *iter);
struct drm_connector *
drm_connector_list_iter_next(struct drm_connector_list_iter *iter);
void drm_connector_list_iter_end(struct drm_connector_list_iter *iter);

bool drm_connector_has_possible_encoder(struct drm_connector *connector,
					struct drm_encoder *encoder);
const char *drm_get_colorspace_name(enum drm_colorspace colorspace);

/**
 * drm_for_each_connector_iter - connector_list iterator macro
 * @connector: &struct drm_connector pointer used as cursor
 * @iter: &struct drm_connector_list_iter
 *
 * Note that @connector is only valid within the list body, if you want to use
 * @connector after calling drm_connector_list_iter_end() then you need to grab
 * your own reference first using drm_connector_get().
 */
#define drm_for_each_connector_iter(connector, iter) \
	while ((connector = drm_connector_list_iter_next(iter)))

/**
 * drm_connector_for_each_possible_encoder - iterate connector's possible encoders
 * @connector: &struct drm_connector pointer
 * @encoder: &struct drm_encoder pointer used as cursor
 */
#define drm_connector_for_each_possible_encoder(connector, encoder) \
	drm_for_each_encoder_mask(encoder, (connector)->dev, \
				  (connector)->possible_encoders)

#endif
