/*
 * Copyright © 2008 Keith Packard
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

#ifndef _DRM_DP_HELPER_H_
#define _DRM_DP_HELPER_H_

#include <linux/delay.h>
#include <linux/i2c.h>

#include <drm/display/drm_dp.h>
#include <drm/drm_connector.h>

struct drm_device;
struct drm_dp_aux;
struct drm_panel;

bool drm_dp_channel_eq_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			  int lane_count);
bool drm_dp_clock_recovery_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			      int lane_count);
u8 drm_dp_get_adjust_request_voltage(const u8 link_status[DP_LINK_STATUS_SIZE],
				     int lane);
u8 drm_dp_get_adjust_request_pre_emphasis(const u8 link_status[DP_LINK_STATUS_SIZE],
					  int lane);
u8 drm_dp_get_adjust_tx_ffe_preset(const u8 link_status[DP_LINK_STATUS_SIZE],
				   int lane);

int drm_dp_read_clock_recovery_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				     enum drm_dp_phy dp_phy, bool uhbr);
int drm_dp_read_channel_eq_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				 enum drm_dp_phy dp_phy, bool uhbr);

void drm_dp_link_train_clock_recovery_delay(const struct drm_dp_aux *aux,
					    const u8 dpcd[DP_RECEIVER_CAP_SIZE]);
void drm_dp_lttpr_link_train_clock_recovery_delay(void);
void drm_dp_link_train_channel_eq_delay(const struct drm_dp_aux *aux,
					const u8 dpcd[DP_RECEIVER_CAP_SIZE]);
void drm_dp_lttpr_link_train_channel_eq_delay(const struct drm_dp_aux *aux,
					      const u8 caps[DP_LTTPR_PHY_CAP_SIZE]);

int drm_dp_128b132b_read_aux_rd_interval(struct drm_dp_aux *aux);
bool drm_dp_128b132b_lane_channel_eq_done(const u8 link_status[DP_LINK_STATUS_SIZE],
					  int lane_count);
bool drm_dp_128b132b_lane_symbol_locked(const u8 link_status[DP_LINK_STATUS_SIZE],
					int lane_count);
bool drm_dp_128b132b_eq_interlane_align_done(const u8 link_status[DP_LINK_STATUS_SIZE]);
bool drm_dp_128b132b_cds_interlane_align_done(const u8 link_status[DP_LINK_STATUS_SIZE]);
bool drm_dp_128b132b_link_training_failed(const u8 link_status[DP_LINK_STATUS_SIZE]);

u8 drm_dp_link_rate_to_bw_code(int link_rate);
int drm_dp_bw_code_to_link_rate(u8 link_bw);

const char *drm_dp_phy_name(enum drm_dp_phy dp_phy);

/**
 * struct drm_dp_vsc_sdp - drm DP VSC SDP
 *
 * This structure represents a DP VSC SDP of drm
 * It is based on DP 1.4 spec [Table 2-116: VSC SDP Header Bytes] and
 * [Table 2-117: VSC SDP Payload for DB16 through DB18]
 *
 * @sdp_type: secondary-data packet type
 * @revision: revision number
 * @length: number of valid data bytes
 * @pixelformat: pixel encoding format
 * @colorimetry: colorimetry format
 * @bpc: bit per color
 * @dynamic_range: dynamic range information
 * @content_type: CTA-861-G defines content types and expected processing by a sink device
 */
struct drm_dp_vsc_sdp {
	unsigned char sdp_type;
	unsigned char revision;
	unsigned char length;
	enum dp_pixelformat pixelformat;
	enum dp_colorimetry colorimetry;
	int bpc;
	enum dp_dynamic_range dynamic_range;
	enum dp_content_type content_type;
};

void drm_dp_vsc_sdp_log(struct drm_printer *p, const struct drm_dp_vsc_sdp *vsc);

bool drm_dp_vsc_sdp_supported(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE]);

int drm_dp_psr_setup_time(const u8 psr_cap[EDP_PSR_RECEIVER_CAP_SIZE]);

static inline int
drm_dp_max_link_rate(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return drm_dp_bw_code_to_link_rate(dpcd[DP_MAX_LINK_RATE]);
}

static inline u8
drm_dp_max_lane_count(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_MAX_LANE_COUNT] & DP_MAX_LANE_COUNT_MASK;
}

static inline bool
drm_dp_enhanced_frame_cap(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x11 &&
		(dpcd[DP_MAX_LANE_COUNT] & DP_ENHANCED_FRAME_CAP);
}

static inline bool
drm_dp_fast_training_cap(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x11 &&
		(dpcd[DP_MAX_DOWNSPREAD] & DP_NO_AUX_HANDSHAKE_LINK_TRAINING);
}

static inline bool
drm_dp_tps3_supported(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x12 &&
		dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED;
}

static inline bool
drm_dp_max_downspread(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x11 ||
		dpcd[DP_MAX_DOWNSPREAD] & DP_MAX_DOWNSPREAD_0_5;
}

static inline bool
drm_dp_tps4_supported(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x14 &&
		dpcd[DP_MAX_DOWNSPREAD] & DP_TPS4_SUPPORTED;
}

static inline u8
drm_dp_training_pattern_mask(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return (dpcd[DP_DPCD_REV] >= 0x14) ? DP_TRAINING_PATTERN_MASK_1_4 :
		DP_TRAINING_PATTERN_MASK;
}

static inline bool
drm_dp_is_branch(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_PRESENT;
}

/* DP/eDP DSC support */
u8 drm_dp_dsc_sink_bpp_incr(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE]);
u8 drm_dp_dsc_sink_max_slice_count(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE],
				   bool is_edp);
u8 drm_dp_dsc_sink_line_buf_depth(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE]);
int drm_dp_dsc_sink_supported_input_bpcs(const u8 dsc_dpc[DP_DSC_RECEIVER_CAP_SIZE],
					 u8 dsc_bpc[3]);

static inline bool
drm_dp_sink_supports_dsc(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE])
{
	return dsc_dpcd[DP_DSC_SUPPORT - DP_DSC_SUPPORT] &
		DP_DSC_DECOMPRESSION_IS_SUPPORTED;
}

static inline u16
drm_edp_dsc_sink_output_bpp(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE])
{
	return dsc_dpcd[DP_DSC_MAX_BITS_PER_PIXEL_LOW - DP_DSC_SUPPORT] |
		((dsc_dpcd[DP_DSC_MAX_BITS_PER_PIXEL_HI - DP_DSC_SUPPORT] &
		  DP_DSC_MAX_BITS_PER_PIXEL_HI_MASK) << 8);
}

static inline u32
drm_dp_dsc_sink_max_slice_width(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE])
{
	/* Max Slicewidth = Number of Pixels * 320 */
	return dsc_dpcd[DP_DSC_MAX_SLICE_WIDTH - DP_DSC_SUPPORT] *
		DP_DSC_SLICE_WIDTH_MULTIPLIER;
}

/**
 * drm_dp_dsc_sink_supports_format() - check if sink supports DSC with given output format
 * @dsc_dpcd : DSC-capability DPCDs of the sink
 * @output_format: output_format which is to be checked
 *
 * Returns true if the sink supports DSC with the given output_format, false otherwise.
 */
static inline bool
drm_dp_dsc_sink_supports_format(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE], u8 output_format)
{
	return dsc_dpcd[DP_DSC_DEC_COLOR_FORMAT_CAP - DP_DSC_SUPPORT] & output_format;
}

/* Forward Error Correction Support on DP 1.4 */
static inline bool
drm_dp_sink_supports_fec(const u8 fec_capable)
{
	return fec_capable & DP_FEC_CAPABLE;
}

static inline bool
drm_dp_channel_coding_supported(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_MAIN_LINK_CHANNEL_CODING] & DP_CAP_ANSI_8B10B;
}

static inline bool
drm_dp_alternate_scrambler_reset_cap(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_EDP_CONFIGURATION_CAP] &
			DP_ALTERNATE_SCRAMBLER_RESET_CAP;
}

/* Ignore MSA timing for Adaptive Sync support on DP 1.4 */
static inline bool
drm_dp_sink_can_do_video_without_timing_msa(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DOWN_STREAM_PORT_COUNT] &
		DP_MSA_TIMING_PAR_IGNORED;
}

/**
 * drm_edp_backlight_supported() - Check an eDP DPCD for VESA backlight support
 * @edp_dpcd: The DPCD to check
 *
 * Note that currently this function will return %false for panels which support various DPCD
 * backlight features but which require the brightness be set through PWM, and don't support setting
 * the brightness level via the DPCD.
 *
 * Returns: %True if @edp_dpcd indicates that VESA backlight controls are supported, %false
 * otherwise
 */
static inline bool
drm_edp_backlight_supported(const u8 edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE])
{
	return !!(edp_dpcd[1] & DP_EDP_TCON_BACKLIGHT_ADJUSTMENT_CAP);
}

/**
 * drm_dp_is_uhbr_rate - Determine if a link rate is UHBR
 * @link_rate: link rate in 10kbits/s units
 *
 * Determine if the provided link rate is an UHBR rate.
 *
 * Returns: %True if @link_rate is an UHBR rate.
 */
static inline bool drm_dp_is_uhbr_rate(int link_rate)
{
	return link_rate >= 1000000;
}

/*
 * DisplayPort AUX channel
 */

/**
 * struct drm_dp_aux_msg - DisplayPort AUX channel transaction
 * @address: address of the (first) register to access
 * @request: contains the type of transaction (see DP_AUX_* macros)
 * @reply: upon completion, contains the reply type of the transaction
 * @buffer: pointer to a transmission or reception buffer
 * @size: size of @buffer
 */
struct drm_dp_aux_msg {
	unsigned int address;
	u8 request;
	u8 reply;
	void *buffer;
	size_t size;
};

struct cec_adapter;
struct drm_connector;
struct drm_edid;

/**
 * struct drm_dp_aux_cec - DisplayPort CEC-Tunneling-over-AUX
 * @lock: mutex protecting this struct
 * @adap: the CEC adapter for CEC-Tunneling-over-AUX support.
 * @connector: the connector this CEC adapter is associated with
 * @unregister_work: unregister the CEC adapter
 */
struct drm_dp_aux_cec {
	struct mutex lock;
	struct cec_adapter *adap;
	struct drm_connector *connector;
	struct delayed_work unregister_work;
};

/**
 * struct drm_dp_aux - DisplayPort AUX channel
 *
 * An AUX channel can also be used to transport I2C messages to a sink. A
 * typical application of that is to access an EDID that's present in the sink
 * device. The @transfer() function can also be used to execute such
 * transactions. The drm_dp_aux_register() function registers an I2C adapter
 * that can be passed to drm_probe_ddc(). Upon removal, drivers should call
 * drm_dp_aux_unregister() to remove the I2C adapter. The I2C adapter uses long
 * transfers by default; if a partial response is received, the adapter will
 * drop down to the size given by the partial response for this transaction
 * only.
 */
struct drm_dp_aux {
	/**
	 * @name: user-visible name of this AUX channel and the
	 * I2C-over-AUX adapter.
	 *
	 * It's also used to specify the name of the I2C adapter. If set
	 * to %NULL, dev_name() of @dev will be used.
	 */
	const char *name;

	/**
	 * @ddc: I2C adapter that can be used for I2C-over-AUX
	 * communication
	 */
	struct i2c_adapter ddc;

	/**
	 * @dev: pointer to struct device that is the parent for this
	 * AUX channel.
	 */
	struct device *dev;

	/**
	 * @drm_dev: pointer to the &drm_device that owns this AUX channel.
	 * Beware, this may be %NULL before drm_dp_aux_register() has been
	 * called.
	 *
	 * It should be set to the &drm_device that will be using this AUX
	 * channel as early as possible. For many graphics drivers this should
	 * happen before drm_dp_aux_init(), however it's perfectly fine to set
	 * this field later so long as it's assigned before calling
	 * drm_dp_aux_register().
	 */
	struct drm_device *drm_dev;

	/**
	 * @crtc: backpointer to the crtc that is currently using this
	 * AUX channel
	 */
	struct drm_crtc *crtc;

	/**
	 * @hw_mutex: internal mutex used for locking transfers.
	 *
	 * Note that if the underlying hardware is shared among multiple
	 * channels, the driver needs to do additional locking to
	 * prevent concurrent access.
	 */
	struct mutex hw_mutex;

	/**
	 * @crc_work: worker that captures CRCs for each frame
	 */
	struct work_struct crc_work;

	/**
	 * @crc_count: counter of captured frame CRCs
	 */
	u8 crc_count;

	/**
	 * @transfer: transfers a message representing a single AUX
	 * transaction.
	 *
	 * This is a hardware-specific implementation of how
	 * transactions are executed that the drivers must provide.
	 *
	 * A pointer to a &drm_dp_aux_msg structure describing the
	 * transaction is passed into this function. Upon success, the
	 * implementation should return the number of payload bytes that
	 * were transferred, or a negative error-code on failure.
	 *
	 * Helpers will propagate these errors, with the exception of
	 * the %-EBUSY error, which causes a transaction to be retried.
	 * On a short, helpers will return %-EPROTO to make it simpler
	 * to check for failure.
	 *
	 * The @transfer() function must only modify the reply field of
	 * the &drm_dp_aux_msg structure. The retry logic and i2c
	 * helpers assume this is the case.
	 *
	 * Also note that this callback can be called no matter the
	 * state @dev is in and also no matter what state the panel is
	 * in. It's expected:
	 *
	 * - If the @dev providing the AUX bus is currently unpowered then
	 *   it will power itself up for the transfer.
	 *
	 * - If we're on eDP (using a drm_panel) and the panel is not in a
	 *   state where it can respond (it's not powered or it's in a
	 *   low power state) then this function may return an error, but
	 *   not crash. It's up to the caller of this code to make sure that
	 *   the panel is powered on if getting an error back is not OK. If a
	 *   drm_panel driver is initiating a DP AUX transfer it may power
	 *   itself up however it wants. All other code should ensure that
	 *   the pre_enable() bridge chain (which eventually calls the
	 *   drm_panel prepare function) has powered the panel.
	 */
	ssize_t (*transfer)(struct drm_dp_aux *aux,
			    struct drm_dp_aux_msg *msg);

	/**
	 * @wait_hpd_asserted: wait for HPD to be asserted
	 *
	 * This is mainly useful for eDP panels drivers to wait for an eDP
	 * panel to finish powering on. This is an optional function.
	 *
	 * This function will efficiently wait for the HPD signal to be
	 * asserted. The `wait_us` parameter that is passed in says that we
	 * know that the HPD signal is expected to be asserted within `wait_us`
	 * microseconds. This function could wait for longer than `wait_us` if
	 * the logic in the DP controller has a long debouncing time. The
	 * important thing is that if this function returns success that the
	 * DP controller is ready to send AUX transactions.
	 *
	 * This function returns 0 if HPD was asserted or -ETIMEDOUT if time
	 * expired and HPD wasn't asserted. This function should not print
	 * timeout errors to the log.
	 *
	 * The semantics of this function are designed to match the
	 * readx_poll_timeout() function. That means a `wait_us` of 0 means
	 * to wait forever. Like readx_poll_timeout(), this function may sleep.
	 *
	 * NOTE: this function specifically reports the state of the HPD pin
	 * that's associated with the DP AUX channel. This is different from
	 * the HPD concept in much of the rest of DRM which is more about
	 * physical presence of a display. For eDP, for instance, a display is
	 * assumed always present even if the HPD pin is deasserted.
	 */
	int (*wait_hpd_asserted)(struct drm_dp_aux *aux, unsigned long wait_us);

	/**
	 * @i2c_nack_count: Counts I2C NACKs, used for DP validation.
	 */
	unsigned i2c_nack_count;
	/**
	 * @i2c_defer_count: Counts I2C DEFERs, used for DP validation.
	 */
	unsigned i2c_defer_count;
	/**
	 * @cec: struct containing fields used for CEC-Tunneling-over-AUX.
	 */
	struct drm_dp_aux_cec cec;
	/**
	 * @is_remote: Is this AUX CH actually using sideband messaging.
	 */
	bool is_remote;

	/**
	 * @powered_down: If true then the remote endpoint is powered down.
	 */
	bool powered_down;
};

int drm_dp_dpcd_probe(struct drm_dp_aux *aux, unsigned int offset);
void drm_dp_dpcd_set_powered(struct drm_dp_aux *aux, bool powered);
ssize_t drm_dp_dpcd_read(struct drm_dp_aux *aux, unsigned int offset,
			 void *buffer, size_t size);
ssize_t drm_dp_dpcd_write(struct drm_dp_aux *aux, unsigned int offset,
			  void *buffer, size_t size);

/**
 * drm_dp_dpcd_readb() - read a single byte from the DPCD
 * @aux: DisplayPort AUX channel
 * @offset: address of the register to read
 * @valuep: location where the value of the register will be stored
 *
 * Returns the number of bytes transferred (1) on success, or a negative
 * error code on failure.
 */
static inline ssize_t drm_dp_dpcd_readb(struct drm_dp_aux *aux,
					unsigned int offset, u8 *valuep)
{
	return drm_dp_dpcd_read(aux, offset, valuep, 1);
}

/**
 * drm_dp_dpcd_writeb() - write a single byte to the DPCD
 * @aux: DisplayPort AUX channel
 * @offset: address of the register to write
 * @value: value to write to the register
 *
 * Returns the number of bytes transferred (1) on success, or a negative
 * error code on failure.
 */
static inline ssize_t drm_dp_dpcd_writeb(struct drm_dp_aux *aux,
					 unsigned int offset, u8 value)
{
	return drm_dp_dpcd_write(aux, offset, &value, 1);
}

int drm_dp_read_dpcd_caps(struct drm_dp_aux *aux,
			  u8 dpcd[DP_RECEIVER_CAP_SIZE]);

int drm_dp_dpcd_read_link_status(struct drm_dp_aux *aux,
				 u8 status[DP_LINK_STATUS_SIZE]);

int drm_dp_dpcd_read_phy_link_status(struct drm_dp_aux *aux,
				     enum drm_dp_phy dp_phy,
				     u8 link_status[DP_LINK_STATUS_SIZE]);

bool drm_dp_send_real_edid_checksum(struct drm_dp_aux *aux,
				    u8 real_edid_checksum);

int drm_dp_read_downstream_info(struct drm_dp_aux *aux,
				const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				u8 downstream_ports[DP_MAX_DOWNSTREAM_PORTS]);
bool drm_dp_downstream_is_type(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			       const u8 port_cap[4], u8 type);
bool drm_dp_downstream_is_tmds(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			       const u8 port_cap[4],
			       const struct drm_edid *drm_edid);
int drm_dp_downstream_max_dotclock(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				   const u8 port_cap[4]);
int drm_dp_downstream_max_tmds_clock(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				     const u8 port_cap[4],
				     const struct drm_edid *drm_edid);
int drm_dp_downstream_min_tmds_clock(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				     const u8 port_cap[4],
				     const struct drm_edid *drm_edid);
int drm_dp_downstream_max_bpc(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			      const u8 port_cap[4],
			      const struct drm_edid *drm_edid);
bool drm_dp_downstream_420_passthrough(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				       const u8 port_cap[4]);
bool drm_dp_downstream_444_to_420_conversion(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
					     const u8 port_cap[4]);
struct drm_display_mode *drm_dp_downstream_mode(struct drm_device *dev,
						const u8 dpcd[DP_RECEIVER_CAP_SIZE],
						const u8 port_cap[4]);
int drm_dp_downstream_id(struct drm_dp_aux *aux, char id[6]);
void drm_dp_downstream_debug(struct seq_file *m,
			     const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			     const u8 port_cap[4],
			     const struct drm_edid *drm_edid,
			     struct drm_dp_aux *aux);
enum drm_mode_subconnector
drm_dp_subconnector_type(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			 const u8 port_cap[4]);
void drm_dp_set_subconnector_property(struct drm_connector *connector,
				      enum drm_connector_status status,
				      const u8 *dpcd,
				      const u8 port_cap[4]);

struct drm_dp_desc;
bool drm_dp_read_sink_count_cap(struct drm_connector *connector,
				const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				const struct drm_dp_desc *desc);
int drm_dp_read_sink_count(struct drm_dp_aux *aux);

int drm_dp_read_lttpr_common_caps(struct drm_dp_aux *aux,
				  const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				  u8 caps[DP_LTTPR_COMMON_CAP_SIZE]);
int drm_dp_read_lttpr_phy_caps(struct drm_dp_aux *aux,
			       const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			       enum drm_dp_phy dp_phy,
			       u8 caps[DP_LTTPR_PHY_CAP_SIZE]);
int drm_dp_lttpr_count(const u8 cap[DP_LTTPR_COMMON_CAP_SIZE]);
int drm_dp_lttpr_max_link_rate(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE]);
int drm_dp_lttpr_max_lane_count(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE]);
bool drm_dp_lttpr_voltage_swing_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE]);
bool drm_dp_lttpr_pre_emphasis_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE]);

void drm_dp_remote_aux_init(struct drm_dp_aux *aux);
void drm_dp_aux_init(struct drm_dp_aux *aux);
int drm_dp_aux_register(struct drm_dp_aux *aux);
void drm_dp_aux_unregister(struct drm_dp_aux *aux);

int drm_dp_start_crc(struct drm_dp_aux *aux, struct drm_crtc *crtc);
int drm_dp_stop_crc(struct drm_dp_aux *aux);

struct drm_dp_dpcd_ident {
	u8 oui[3];
	u8 device_id[6];
	u8 hw_rev;
	u8 sw_major_rev;
	u8 sw_minor_rev;
} __packed;

/**
 * struct drm_dp_desc - DP branch/sink device descriptor
 * @ident: DP device identification from DPCD 0x400 (sink) or 0x500 (branch).
 * @quirks: Quirks; use drm_dp_has_quirk() to query for the quirks.
 */
struct drm_dp_desc {
	struct drm_dp_dpcd_ident ident;
	u32 quirks;
};

int drm_dp_read_desc(struct drm_dp_aux *aux, struct drm_dp_desc *desc,
		     bool is_branch);

/**
 * enum drm_dp_quirk - Display Port sink/branch device specific quirks
 *
 * Display Port sink and branch devices in the wild have a variety of bugs, try
 * to collect them here. The quirks are shared, but it's up to the drivers to
 * implement workarounds for them.
 */
enum drm_dp_quirk {
	/**
	 * @DP_DPCD_QUIRK_CONSTANT_N:
	 *
	 * The device requires main link attributes Mvid and Nvid to be limited
	 * to 16 bits. So will give a constant value (0x8000) for compatability.
	 */
	DP_DPCD_QUIRK_CONSTANT_N,
	/**
	 * @DP_DPCD_QUIRK_NO_PSR:
	 *
	 * The device does not support PSR even if reports that it supports or
	 * driver still need to implement proper handling for such device.
	 */
	DP_DPCD_QUIRK_NO_PSR,
	/**
	 * @DP_DPCD_QUIRK_NO_SINK_COUNT:
	 *
	 * The device does not set SINK_COUNT to a non-zero value.
	 * The driver should ignore SINK_COUNT during detection. Note that
	 * drm_dp_read_sink_count_cap() automatically checks for this quirk.
	 */
	DP_DPCD_QUIRK_NO_SINK_COUNT,
	/**
	 * @DP_DPCD_QUIRK_DSC_WITHOUT_VIRTUAL_DPCD:
	 *
	 * The device supports MST DSC despite not supporting Virtual DPCD.
	 * The DSC caps can be read from the physical aux instead.
	 */
	DP_DPCD_QUIRK_DSC_WITHOUT_VIRTUAL_DPCD,
	/**
	 * @DP_DPCD_QUIRK_CAN_DO_MAX_LINK_RATE_3_24_GBPS:
	 *
	 * The device supports a link rate of 3.24 Gbps (multiplier 0xc) despite
	 * the DP_MAX_LINK_RATE register reporting a lower max multiplier.
	 */
	DP_DPCD_QUIRK_CAN_DO_MAX_LINK_RATE_3_24_GBPS,
	/**
	 * @DP_DPCD_QUIRK_HBLANK_EXPANSION_REQUIRES_DSC:
	 *
	 * The device applies HBLANK expansion for some modes, but this
	 * requires enabling DSC.
	 */
	DP_DPCD_QUIRK_HBLANK_EXPANSION_REQUIRES_DSC,
};

/**
 * drm_dp_has_quirk() - does the DP device have a specific quirk
 * @desc: Device descriptor filled by drm_dp_read_desc()
 * @quirk: Quirk to query for
 *
 * Return true if DP device identified by @desc has @quirk.
 */
static inline bool
drm_dp_has_quirk(const struct drm_dp_desc *desc, enum drm_dp_quirk quirk)
{
	return desc->quirks & BIT(quirk);
}

/**
 * struct drm_edp_backlight_info - Probed eDP backlight info struct
 * @pwmgen_bit_count: The pwmgen bit count
 * @pwm_freq_pre_divider: The PWM frequency pre-divider value being used for this backlight, if any
 * @max: The maximum backlight level that may be set
 * @lsb_reg_used: Do we also write values to the DP_EDP_BACKLIGHT_BRIGHTNESS_LSB register?
 * @aux_enable: Does the panel support the AUX enable cap?
 * @aux_set: Does the panel support setting the brightness through AUX?
 *
 * This structure contains various data about an eDP backlight, which can be populated by using
 * drm_edp_backlight_init().
 */
struct drm_edp_backlight_info {
	u8 pwmgen_bit_count;
	u8 pwm_freq_pre_divider;
	u16 max;

	bool lsb_reg_used : 1;
	bool aux_enable : 1;
	bool aux_set : 1;
};

int
drm_edp_backlight_init(struct drm_dp_aux *aux, struct drm_edp_backlight_info *bl,
		       u16 driver_pwm_freq_hz, const u8 edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE],
		       u16 *current_level, u8 *current_mode);
int drm_edp_backlight_set_level(struct drm_dp_aux *aux, const struct drm_edp_backlight_info *bl,
				u16 level);
int drm_edp_backlight_enable(struct drm_dp_aux *aux, const struct drm_edp_backlight_info *bl,
			     u16 level);
int drm_edp_backlight_disable(struct drm_dp_aux *aux, const struct drm_edp_backlight_info *bl);

#if IS_ENABLED(CONFIG_DRM_KMS_HELPER) && (IS_BUILTIN(CONFIG_BACKLIGHT_CLASS_DEVICE) || \
	(IS_MODULE(CONFIG_DRM_KMS_HELPER) && IS_MODULE(CONFIG_BACKLIGHT_CLASS_DEVICE)))

int drm_panel_dp_aux_backlight(struct drm_panel *panel, struct drm_dp_aux *aux);

#else

static inline int drm_panel_dp_aux_backlight(struct drm_panel *panel,
					     struct drm_dp_aux *aux)
{
	return 0;
}

#endif

#ifdef CONFIG_DRM_DP_CEC
void drm_dp_cec_irq(struct drm_dp_aux *aux);
void drm_dp_cec_register_connector(struct drm_dp_aux *aux,
				   struct drm_connector *connector);
void drm_dp_cec_unregister_connector(struct drm_dp_aux *aux);
void drm_dp_cec_attach(struct drm_dp_aux *aux, u16 source_physical_address);
void drm_dp_cec_set_edid(struct drm_dp_aux *aux, const struct edid *edid);
void drm_dp_cec_unset_edid(struct drm_dp_aux *aux);
#else
static inline void drm_dp_cec_irq(struct drm_dp_aux *aux)
{
}

static inline void
drm_dp_cec_register_connector(struct drm_dp_aux *aux,
			      struct drm_connector *connector)
{
}

static inline void drm_dp_cec_unregister_connector(struct drm_dp_aux *aux)
{
}

static inline void drm_dp_cec_attach(struct drm_dp_aux *aux,
				     u16 source_physical_address)
{
}

static inline void drm_dp_cec_set_edid(struct drm_dp_aux *aux,
				       const struct edid *edid)
{
}

static inline void drm_dp_cec_unset_edid(struct drm_dp_aux *aux)
{
}

#endif

/**
 * struct drm_dp_phy_test_params - DP Phy Compliance parameters
 * @link_rate: Requested Link rate from DPCD 0x219
 * @num_lanes: Number of lanes requested by sing through DPCD 0x220
 * @phy_pattern: DP Phy test pattern from DPCD 0x248
 * @hbr2_reset: DP HBR2_COMPLIANCE_SCRAMBLER_RESET from DCPD 0x24A and 0x24B
 * @custom80: DP Test_80BIT_CUSTOM_PATTERN from DPCDs 0x250 through 0x259
 * @enhanced_frame_cap: flag for enhanced frame capability.
 */
struct drm_dp_phy_test_params {
	int link_rate;
	u8 num_lanes;
	u8 phy_pattern;
	u8 hbr2_reset[2];
	u8 custom80[10];
	bool enhanced_frame_cap;
};

int drm_dp_get_phy_test_pattern(struct drm_dp_aux *aux,
				struct drm_dp_phy_test_params *data);
int drm_dp_set_phy_test_pattern(struct drm_dp_aux *aux,
				struct drm_dp_phy_test_params *data, u8 dp_rev);
int drm_dp_get_pcon_max_frl_bw(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			       const u8 port_cap[4]);
int drm_dp_pcon_frl_prepare(struct drm_dp_aux *aux, bool enable_frl_ready_hpd);
bool drm_dp_pcon_is_frl_ready(struct drm_dp_aux *aux);
int drm_dp_pcon_frl_configure_1(struct drm_dp_aux *aux, int max_frl_gbps,
				u8 frl_mode);
int drm_dp_pcon_frl_configure_2(struct drm_dp_aux *aux, int max_frl_mask,
				u8 frl_type);
int drm_dp_pcon_reset_frl_config(struct drm_dp_aux *aux);
int drm_dp_pcon_frl_enable(struct drm_dp_aux *aux);

bool drm_dp_pcon_hdmi_link_active(struct drm_dp_aux *aux);
int drm_dp_pcon_hdmi_link_mode(struct drm_dp_aux *aux, u8 *frl_trained_mask);
void drm_dp_pcon_hdmi_frl_link_error_count(struct drm_dp_aux *aux,
					   struct drm_connector *connector);
bool drm_dp_pcon_enc_is_dsc_1_2(const u8 pcon_dsc_dpcd[DP_PCON_DSC_ENCODER_CAP_SIZE]);
int drm_dp_pcon_dsc_max_slices(const u8 pcon_dsc_dpcd[DP_PCON_DSC_ENCODER_CAP_SIZE]);
int drm_dp_pcon_dsc_max_slice_width(const u8 pcon_dsc_dpcd[DP_PCON_DSC_ENCODER_CAP_SIZE]);
int drm_dp_pcon_dsc_bpp_incr(const u8 pcon_dsc_dpcd[DP_PCON_DSC_ENCODER_CAP_SIZE]);
int drm_dp_pcon_pps_default(struct drm_dp_aux *aux);
int drm_dp_pcon_pps_override_buf(struct drm_dp_aux *aux, u8 pps_buf[128]);
int drm_dp_pcon_pps_override_param(struct drm_dp_aux *aux, u8 pps_param[6]);
bool drm_dp_downstream_rgb_to_ycbcr_conversion(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
					       const u8 port_cap[4], u8 color_spc);
int drm_dp_pcon_convert_rgb_to_ycbcr(struct drm_dp_aux *aux, u8 color_spc);

#define DRM_DP_BW_OVERHEAD_MST		BIT(0)
#define DRM_DP_BW_OVERHEAD_UHBR		BIT(1)
#define DRM_DP_BW_OVERHEAD_SSC_REF_CLK	BIT(2)
#define DRM_DP_BW_OVERHEAD_FEC		BIT(3)
#define DRM_DP_BW_OVERHEAD_DSC		BIT(4)

int drm_dp_bw_overhead(int lane_count, int hactive,
		       int dsc_slice_count,
		       int bpp_x16, unsigned long flags);
int drm_dp_bw_channel_coding_efficiency(bool is_uhbr);
int drm_dp_max_dprx_data_rate(int max_link_rate, int max_lanes);

ssize_t drm_dp_vsc_sdp_pack(const struct drm_dp_vsc_sdp *vsc, struct dp_sdp *sdp);

#endif /* _DRM_DP_HELPER_H_ */
