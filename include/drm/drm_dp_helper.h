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

/* From the VESA DisplayPort spec */

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
# define DP_TPS3_SUPPORTED		    (1 << 6)
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

#define DP_MAIN_LINK_CHANNEL_CODING         0x006

#define DP_EDP_CONFIGURATION_CAP            0x00d
#define DP_TRAINING_AUX_RD_INTERVAL         0x00e

/* link configuration */
#define	DP_LINK_BW_SET		            0x100
# define DP_LINK_BW_1_62		    0x06
# define DP_LINK_BW_2_7			    0x0a
# define DP_LINK_BW_5_4			    0x14

#define DP_LANE_COUNT_SET	            0x101
# define DP_LANE_COUNT_MASK		    0x0f
# define DP_LANE_COUNT_ENHANCED_FRAME_EN    (1 << 7)

#define DP_TRAINING_PATTERN_SET	            0x102
# define DP_TRAINING_PATTERN_DISABLE	    0
# define DP_TRAINING_PATTERN_1		    1
# define DP_TRAINING_PATTERN_2		    2
# define DP_TRAINING_PATTERN_3		    3
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

#define DP_MAIN_LINK_CHANNEL_CODING_SET	    0x108
# define DP_SET_ANSI_8B10B		    (1 << 0)

#define DP_EDP_CONFIGURATION_SET            0x10a

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

#define DP_SET_POWER                        0x600
# define DP_SET_POWER_D0                    0x1
# define DP_SET_POWER_D3                    0x2

#define MODE_I2C_START	1
#define MODE_I2C_WRITE	2
#define MODE_I2C_READ	4
#define MODE_I2C_STOP	8

struct i2c_algo_dp_aux_data {
	bool running;
	u16 address;
	int (*aux_ch) (struct i2c_adapter *adapter,
		       int mode, uint8_t write_byte,
		       uint8_t *read_byte);
};

int
i2c_dp_aux_add_bus(struct i2c_adapter *adapter);

#endif /* _DRM_DP_HELPER_H_ */
