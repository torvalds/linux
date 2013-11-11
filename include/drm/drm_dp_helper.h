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

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>

/*
 * Unless otherwise noted, all values are from the DP 1.1a spec.  Note that
 * DP and DPCD versions are independent.  Differences from 1.0 are not noted,
 * 1.0 devices basically don't exist in the wild.
 *
 * Abbreviations, in chronological order:
 *
 * eDP: Embedded DisplayPort version 1
 * DPI: DisplayPort Interoperability Guideline v1.1a
 * 1.2: DisplayPort 1.2
 *
 * 1.2 formally includes both eDP and DPI definitions.
 */

#define AUX_NATIVE_WRITE	0x8
#define AUX_NATIVE_READ		0x9
#define AUX_I2C_WRITE		0x0
#define AUX_I2C_READ		0x1
#define AUX_I2C_STATUS		0x2
#define AUX_I2C_MOT		0x4

#define AUX_NATIVE_REPLY_ACK	(0x0 << 4)
#define AUX_NATIVE_REPLY_NACK	(0x1 << 4)
#define AUX_NATIVE_REPLY_DEFER	(0x2 << 4)
#define AUX_NATIVE_REPLY_MASK	(0x3 << 4)

#define AUX_I2C_REPLY_ACK	(0x0 << 6)
#define AUX_I2C_REPLY_NACK	(0x1 << 6)
#define AUX_I2C_REPLY_DEFER	(0x2 << 6)
#define AUX_I2C_REPLY_MASK	(0x3 << 6)

/* AUX CH addresses */
/* DPCD */
#define DP_DPCD_REV                         0x000

#define DP_MAX_LINK_RATE                    0x001

#define DP_MAX_LANE_COUNT                   0x002
# define DP_MAX_LANE_COUNT_MASK		    0x1f
# define DP_TPS3_SUPPORTED		    (1 << 6) /* 1.2 */
# define DP_ENHANCED_FRAME_CAP		    (1 << 7)

#define DP_MAX_DOWNSPREAD                   0x003
# define DP_NO_AUX_HANDSHAKE_LINK_TRAINING  (1 << 6)

#define DP_NORP                             0x004

#define DP_DOWNSTREAMPORT_PRESENT           0x005
# define DP_DWN_STRM_PORT_PRESENT           (1 << 0)
# define DP_DWN_STRM_PORT_TYPE_MASK         0x06
/* 00b = DisplayPort */
/* 01b = Analog */
/* 10b = TMDS or HDMI */
/* 11b = Other */
# define DP_FORMAT_CONVERSION               (1 << 3)
# define DP_DETAILED_CAP_INFO_AVAILABLE	    (1 << 4) /* DPI */

#define DP_MAIN_LINK_CHANNEL_CODING         0x006

#define DP_DOWN_STREAM_PORT_COUNT	    0x007
# define DP_PORT_COUNT_MASK		    0x0f
# define DP_MSA_TIMING_PAR_IGNORED	    (1 << 6) /* eDP */
# define DP_OUI_SUPPORT			    (1 << 7)

#define DP_I2C_SPEED_CAP		    0x00c    /* DPI */
# define DP_I2C_SPEED_1K		    0x01
# define DP_I2C_SPEED_5K		    0x02
# define DP_I2C_SPEED_10K		    0x04
# define DP_I2C_SPEED_100K		    0x08
# define DP_I2C_SPEED_400K		    0x10
# define DP_I2C_SPEED_1M		    0x20

#define DP_EDP_CONFIGURATION_CAP            0x00d   /* XXX 1.2? */
#define DP_TRAINING_AUX_RD_INTERVAL         0x00e   /* XXX 1.2? */

/* Multiple stream transport */
#define DP_MSTM_CAP			    0x021   /* 1.2 */
# define DP_MST_CAP			    (1 << 0)

#define DP_PSR_SUPPORT                      0x070   /* XXX 1.2? */
# define DP_PSR_IS_SUPPORTED                1
#define DP_PSR_CAPS                         0x071   /* XXX 1.2? */
# define DP_PSR_NO_TRAIN_ON_EXIT            1
# define DP_PSR_SETUP_TIME_330              (0 << 1)
# define DP_PSR_SETUP_TIME_275              (1 << 1)
# define DP_PSR_SETUP_TIME_220              (2 << 1)
# define DP_PSR_SETUP_TIME_165              (3 << 1)
# define DP_PSR_SETUP_TIME_110              (4 << 1)
# define DP_PSR_SETUP_TIME_55               (5 << 1)
# define DP_PSR_SETUP_TIME_0                (6 << 1)
# define DP_PSR_SETUP_TIME_MASK             (7 << 1)
# define DP_PSR_SETUP_TIME_SHIFT            1

/*
 * 0x80-0x8f describe downstream port capabilities, but there are two layouts
 * based on whether DP_DETAILED_CAP_INFO_AVAILABLE was set.  If it was not,
 * each port's descriptor is one byte wide.  If it was set, each port's is
 * four bytes wide, starting with the one byte from the base info.  As of
 * DP interop v1.1a only VGA defines additional detail.
 */

/* offset 0 */
#define DP_DOWNSTREAM_PORT_0		    0x80
# define DP_DS_PORT_TYPE_MASK		    (7 << 0)
# define DP_DS_PORT_TYPE_DP		    0
# define DP_DS_PORT_TYPE_VGA		    1
# define DP_DS_PORT_TYPE_DVI		    2
# define DP_DS_PORT_TYPE_HDMI		    3
# define DP_DS_PORT_TYPE_NON_EDID	    4
# define DP_DS_PORT_HPD			    (1 << 3)
/* offset 1 for VGA is maximum megapixels per second / 8 */
/* offset 2 */
# define DP_DS_VGA_MAX_BPC_MASK		    (3 << 0)
# define DP_DS_VGA_8BPC			    0
# define DP_DS_VGA_10BPC		    1
# define DP_DS_VGA_12BPC		    2
# define DP_DS_VGA_16BPC		    3

/* link configuration */
#define	DP_LINK_BW_SET		            0x100
# define DP_LINK_BW_1_62		    0x06
# define DP_LINK_BW_2_7			    0x0a
# define DP_LINK_BW_5_4			    0x14    /* 1.2 */

#define DP_LANE_COUNT_SET	            0x101
# define DP_LANE_COUNT_MASK		    0x0f
# define DP_LANE_COUNT_ENHANCED_FRAME_EN    (1 << 7)

#define DP_TRAINING_PATTERN_SET	            0x102
# define DP_TRAINING_PATTERN_DISABLE	    0
# define DP_TRAINING_PATTERN_1		    1
# define DP_TRAINING_PATTERN_2		    2
# define DP_TRAINING_PATTERN_3		    3	    /* 1.2 */
# define DP_TRAINING_PATTERN_MASK	    0x3

# define DP_LINK_QUAL_PATTERN_DISABLE	    (0 << 2)
# define DP_LINK_QUAL_PATTERN_D10_2	    (1 << 2)
# define DP_LINK_QUAL_PATTERN_ERROR_RATE    (2 << 2)
# define DP_LINK_QUAL_PATTERN_PRBS7	    (3 << 2)
# define DP_LINK_QUAL_PATTERN_MASK	    (3 << 2)

# define DP_RECOVERED_CLOCK_OUT_EN	    (1 << 4)
# define DP_LINK_SCRAMBLING_DISABLE	    (1 << 5)

# define DP_SYMBOL_ERROR_COUNT_BOTH	    (0 << 6)
# define DP_SYMBOL_ERROR_COUNT_DISPARITY    (1 << 6)
# define DP_SYMBOL_ERROR_COUNT_SYMBOL	    (2 << 6)
# define DP_SYMBOL_ERROR_COUNT_MASK	    (3 << 6)

#define DP_TRAINING_LANE0_SET		    0x103
#define DP_TRAINING_LANE1_SET		    0x104
#define DP_TRAINING_LANE2_SET		    0x105
#define DP_TRAINING_LANE3_SET		    0x106

# define DP_TRAIN_VOLTAGE_SWING_MASK	    0x3
# define DP_TRAIN_VOLTAGE_SWING_SHIFT	    0
# define DP_TRAIN_MAX_SWING_REACHED	    (1 << 2)
# define DP_TRAIN_VOLTAGE_SWING_400	    (0 << 0)
# define DP_TRAIN_VOLTAGE_SWING_600	    (1 << 0)
# define DP_TRAIN_VOLTAGE_SWING_800	    (2 << 0)
# define DP_TRAIN_VOLTAGE_SWING_1200	    (3 << 0)

# define DP_TRAIN_PRE_EMPHASIS_MASK	    (3 << 3)
# define DP_TRAIN_PRE_EMPHASIS_0	    (0 << 3)
# define DP_TRAIN_PRE_EMPHASIS_3_5	    (1 << 3)
# define DP_TRAIN_PRE_EMPHASIS_6	    (2 << 3)
# define DP_TRAIN_PRE_EMPHASIS_9_5	    (3 << 3)

# define DP_TRAIN_PRE_EMPHASIS_SHIFT	    3
# define DP_TRAIN_MAX_PRE_EMPHASIS_REACHED  (1 << 5)

#define DP_DOWNSPREAD_CTRL		    0x107
# define DP_SPREAD_AMP_0_5		    (1 << 4)
# define DP_MSA_TIMING_PAR_IGNORE_EN	    (1 << 7) /* eDP */

#define DP_MAIN_LINK_CHANNEL_CODING_SET	    0x108
# define DP_SET_ANSI_8B10B		    (1 << 0)

#define DP_I2C_SPEED_CONTROL_STATUS	    0x109   /* DPI */
/* bitmask as for DP_I2C_SPEED_CAP */

#define DP_EDP_CONFIGURATION_SET            0x10a   /* XXX 1.2? */

#define DP_MSTM_CTRL			    0x111   /* 1.2 */
# define DP_MST_EN			    (1 << 0)
# define DP_UP_REQ_EN			    (1 << 1)
# define DP_UPSTREAM_IS_SRC		    (1 << 2)

#define DP_PSR_EN_CFG			    0x170   /* XXX 1.2? */
# define DP_PSR_ENABLE			    (1 << 0)
# define DP_PSR_MAIN_LINK_ACTIVE	    (1 << 1)
# define DP_PSR_CRC_VERIFICATION	    (1 << 2)
# define DP_PSR_FRAME_CAPTURE		    (1 << 3)

#define DP_SINK_COUNT			    0x200
/* prior to 1.2 bit 7 was reserved mbz */
# define DP_GET_SINK_COUNT(x)		    ((((x) & 0x80) >> 1) | ((x) & 0x3f))
# define DP_SINK_CP_READY		    (1 << 6)

#define DP_DEVICE_SERVICE_IRQ_VECTOR	    0x201
# define DP_REMOTE_CONTROL_COMMAND_PENDING  (1 << 0)
# define DP_AUTOMATED_TEST_REQUEST	    (1 << 1)
# define DP_CP_IRQ			    (1 << 2)
# define DP_SINK_SPECIFIC_IRQ		    (1 << 6)

#define DP_LANE0_1_STATUS		    0x202
#define DP_LANE2_3_STATUS		    0x203
# define DP_LANE_CR_DONE		    (1 << 0)
# define DP_LANE_CHANNEL_EQ_DONE	    (1 << 1)
# define DP_LANE_SYMBOL_LOCKED		    (1 << 2)

#define DP_CHANNEL_EQ_BITS (DP_LANE_CR_DONE |		\
			    DP_LANE_CHANNEL_EQ_DONE |	\
			    DP_LANE_SYMBOL_LOCKED)

#define DP_LANE_ALIGN_STATUS_UPDATED	    0x204

#define DP_INTERLANE_ALIGN_DONE		    (1 << 0)
#define DP_DOWNSTREAM_PORT_STATUS_CHANGED   (1 << 6)
#define DP_LINK_STATUS_UPDATED		    (1 << 7)

#define DP_SINK_STATUS			    0x205

#define DP_RECEIVE_PORT_0_STATUS	    (1 << 0)
#define DP_RECEIVE_PORT_1_STATUS	    (1 << 1)

#define DP_ADJUST_REQUEST_LANE0_1	    0x206
#define DP_ADJUST_REQUEST_LANE2_3	    0x207
# define DP_ADJUST_VOLTAGE_SWING_LANE0_MASK  0x03
# define DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT 0
# define DP_ADJUST_PRE_EMPHASIS_LANE0_MASK   0x0c
# define DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT  2
# define DP_ADJUST_VOLTAGE_SWING_LANE1_MASK  0x30
# define DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT 4
# define DP_ADJUST_PRE_EMPHASIS_LANE1_MASK   0xc0
# define DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT  6

#define DP_TEST_REQUEST			    0x218
# define DP_TEST_LINK_TRAINING		    (1 << 0)
# define DP_TEST_LINK_PATTERN		    (1 << 1)
# define DP_TEST_LINK_EDID_READ		    (1 << 2)
# define DP_TEST_LINK_PHY_TEST_PATTERN	    (1 << 3) /* DPCD >= 1.1 */

#define DP_TEST_LINK_RATE		    0x219
# define DP_LINK_RATE_162		    (0x6)
# define DP_LINK_RATE_27		    (0xa)

#define DP_TEST_LANE_COUNT		    0x220

#define DP_TEST_PATTERN			    0x221

#define DP_TEST_RESPONSE		    0x260
# define DP_TEST_ACK			    (1 << 0)
# define DP_TEST_NAK			    (1 << 1)
# define DP_TEST_EDID_CHECKSUM_WRITE	    (1 << 2)

#define DP_SOURCE_OUI			    0x300
#define DP_SINK_OUI			    0x400
#define DP_BRANCH_OUI			    0x500

#define DP_SET_POWER                        0x600
# define DP_SET_POWER_D0                    0x1
# define DP_SET_POWER_D3                    0x2

#define DP_PSR_ERROR_STATUS                 0x2006  /* XXX 1.2? */
# define DP_PSR_LINK_CRC_ERROR              (1 << 0)
# define DP_PSR_RFB_STORAGE_ERROR           (1 << 1)

#define DP_PSR_ESI                          0x2007  /* XXX 1.2? */
# define DP_PSR_CAPS_CHANGE                 (1 << 0)

#define DP_PSR_STATUS                       0x2008  /* XXX 1.2? */
# define DP_PSR_SINK_INACTIVE               0
# define DP_PSR_SINK_ACTIVE_SRC_SYNCED      1
# define DP_PSR_SINK_ACTIVE_RFB             2
# define DP_PSR_SINK_ACTIVE_SINK_SYNCED     3
# define DP_PSR_SINK_ACTIVE_RESYNC          4
# define DP_PSR_SINK_INTERNAL_ERROR         7
# define DP_PSR_SINK_STATE_MASK             0x07

#define MODE_I2C_START	1
#define MODE_I2C_WRITE	2
#define MODE_I2C_READ	4
#define MODE_I2C_STOP	8

/**
 * struct i2c_algo_dp_aux_data - driver interface structure for i2c over dp
 * 				 aux algorithm
 * @running: set by the algo indicating whether an i2c is ongoing or whether
 * 	     the i2c bus is quiescent
 * @address: i2c target address for the currently ongoing transfer
 * @aux_ch: driver callback to transfer a single byte of the i2c payload
 */
struct i2c_algo_dp_aux_data {
	bool running;
	u16 address;
	int (*aux_ch) (struct i2c_adapter *adapter,
		       int mode, uint8_t write_byte,
		       uint8_t *read_byte);
};

int
i2c_dp_aux_add_bus(struct i2c_adapter *adapter);


#define DP_LINK_STATUS_SIZE	   6
bool drm_dp_channel_eq_ok(u8 link_status[DP_LINK_STATUS_SIZE],
			  int lane_count);
bool drm_dp_clock_recovery_ok(u8 link_status[DP_LINK_STATUS_SIZE],
			      int lane_count);
u8 drm_dp_get_adjust_request_voltage(u8 link_status[DP_LINK_STATUS_SIZE],
				     int lane);
u8 drm_dp_get_adjust_request_pre_emphasis(u8 link_status[DP_LINK_STATUS_SIZE],
					  int lane);

#define DP_RECEIVER_CAP_SIZE	0xf
void drm_dp_link_train_clock_recovery_delay(u8 dpcd[DP_RECEIVER_CAP_SIZE]);
void drm_dp_link_train_channel_eq_delay(u8 dpcd[DP_RECEIVER_CAP_SIZE]);

u8 drm_dp_link_rate_to_bw_code(int link_rate);
int drm_dp_bw_code_to_link_rate(u8 link_bw);

static inline int
drm_dp_max_link_rate(u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return drm_dp_bw_code_to_link_rate(dpcd[DP_MAX_LINK_RATE]);
}

static inline u8
drm_dp_max_lane_count(u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_MAX_LANE_COUNT] & DP_MAX_LANE_COUNT_MASK;
}

#endif /* _DRM_DP_HELPER_H_ */
