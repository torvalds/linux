/*
 * Copyright © 2009 Keith Packard
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_dp_helper.h>

/**
 * DOC: dp helpers
 *
 * These functions contain some common logic and helpers at various abstraction
 * levels to deal with Display Port sink devices and related things like DP aux
 * channel transfers, EDID reading over DP aux channels, decoding certain DPCD
 * blocks, ...
 */

/* Helpers for DP link training */
static u8 dp_link_status(u8 link_status[DP_LINK_STATUS_SIZE], int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static u8 dp_get_lane_status(u8 link_status[DP_LINK_STATUS_SIZE],
			     int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	u8 l = dp_link_status(link_status, i);
	return (l >> s) & 0xf;
}

bool drm_dp_channel_eq_ok(u8 link_status[DP_LINK_STATUS_SIZE],
			  int lane_count)
{
	u8 lane_align;
	u8 lane_status;
	int lane;

	lane_align = dp_link_status(link_status,
				    DP_LANE_ALIGN_STATUS_UPDATED);
	if ((lane_align & DP_INTERLANE_ALIGN_DONE) == 0)
		return false;
	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_CHANNEL_EQ_BITS) != DP_CHANNEL_EQ_BITS)
			return false;
	}
	return true;
}
EXPORT_SYMBOL(drm_dp_channel_eq_ok);

bool drm_dp_clock_recovery_ok(u8 link_status[DP_LINK_STATUS_SIZE],
			      int lane_count)
{
	int lane;
	u8 lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_LANE_CR_DONE) == 0)
			return false;
	}
	return true;
}
EXPORT_SYMBOL(drm_dp_clock_recovery_ok);

u8 drm_dp_get_adjust_request_voltage(u8 link_status[DP_LINK_STATUS_SIZE],
				     int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
		 DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_VOLTAGE_SWING_SHIFT;
}
EXPORT_SYMBOL(drm_dp_get_adjust_request_voltage);

u8 drm_dp_get_adjust_request_pre_emphasis(u8 link_status[DP_LINK_STATUS_SIZE],
					  int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
		 DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_PRE_EMPHASIS_SHIFT;
}
EXPORT_SYMBOL(drm_dp_get_adjust_request_pre_emphasis);

void drm_dp_link_train_clock_recovery_delay(u8 dpcd[DP_RECEIVER_CAP_SIZE]) {
	if (dpcd[DP_TRAINING_AUX_RD_INTERVAL] == 0)
		udelay(100);
	else
		mdelay(dpcd[DP_TRAINING_AUX_RD_INTERVAL] * 4);
}
EXPORT_SYMBOL(drm_dp_link_train_clock_recovery_delay);

void drm_dp_link_train_channel_eq_delay(u8 dpcd[DP_RECEIVER_CAP_SIZE]) {
	if (dpcd[DP_TRAINING_AUX_RD_INTERVAL] == 0)
		udelay(400);
	else
		mdelay(dpcd[DP_TRAINING_AUX_RD_INTERVAL] * 4);
}
EXPORT_SYMBOL(drm_dp_link_train_channel_eq_delay);

u8 drm_dp_link_rate_to_bw_code(int link_rate)
{
	switch (link_rate) {
	case 162000:
	default:
		return DP_LINK_BW_1_62;
	case 270000:
		return DP_LINK_BW_2_7;
	case 540000:
		return DP_LINK_BW_5_4;
	}
}
EXPORT_SYMBOL(drm_dp_link_rate_to_bw_code);

int drm_dp_bw_code_to_link_rate(u8 link_bw)
{
	switch (link_bw) {
	case DP_LINK_BW_1_62:
	default:
		return 162000;
	case DP_LINK_BW_2_7:
		return 270000;
	case DP_LINK_BW_5_4:
		return 540000;
	}
}
EXPORT_SYMBOL(drm_dp_bw_code_to_link_rate);
