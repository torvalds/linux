/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <drm/display/drm_dp_dual_mode_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_print.h>

/**
 * DOC: dp dual mode helpers
 *
 * Helper functions to deal with DP dual mode (aka. DP++) adaptors.
 *
 * Type 1:
 * Adaptor registers (if any) and the sink DDC bus may be accessed via I2C.
 *
 * Type 2:
 * Adaptor registers and sink DDC bus can be accessed either via I2C or
 * I2C-over-AUX. Source devices may choose to implement either of these
 * access methods.
 */

#define DP_DUAL_MODE_SLAVE_ADDRESS 0x40

/**
 * drm_dp_dual_mode_read - Read from the DP dual mode adaptor register(s)
 * @adapter: I2C adapter for the DDC bus
 * @offset: register offset
 * @buffer: buffer for return data
 * @size: size of the buffer
 *
 * Reads @size bytes from the DP dual mode adaptor registers
 * starting at @offset.
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
ssize_t drm_dp_dual_mode_read(struct i2c_adapter *adapter,
			      u8 offset, void *buffer, size_t size)
{
	u8 zero = 0;
	char *tmpbuf = NULL;
	/*
	 * As sub-addressing is not supported by all adaptors,
	 * always explicitly read from the start and discard
	 * any bytes that come before the requested offset.
	 * This way, no matter whether the adaptor supports it
	 * or not, we'll end up reading the proper data.
	 */
	struct i2c_msg msgs[] = {
		{
			.addr = DP_DUAL_MODE_SLAVE_ADDRESS,
			.flags = 0,
			.len = 1,
			.buf = &zero,
		},
		{
			.addr = DP_DUAL_MODE_SLAVE_ADDRESS,
			.flags = I2C_M_RD,
			.len = size + offset,
			.buf = buffer,
		},
	};
	int ret;

	if (offset) {
		tmpbuf = kmalloc(size + offset, GFP_KERNEL);
		if (!tmpbuf)
			return -ENOMEM;

		msgs[1].buf = tmpbuf;
	}

	ret = i2c_transfer(adapter, msgs, ARRAY_SIZE(msgs));
	if (tmpbuf)
		memcpy(buffer, tmpbuf + offset, size);

	kfree(tmpbuf);

	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msgs))
		return -EPROTO;

	return 0;
}
EXPORT_SYMBOL(drm_dp_dual_mode_read);

/**
 * drm_dp_dual_mode_write - Write to the DP dual mode adaptor register(s)
 * @adapter: I2C adapter for the DDC bus
 * @offset: register offset
 * @buffer: buffer for write data
 * @size: size of the buffer
 *
 * Writes @size bytes to the DP dual mode adaptor registers
 * starting at @offset.
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
ssize_t drm_dp_dual_mode_write(struct i2c_adapter *adapter,
			       u8 offset, const void *buffer, size_t size)
{
	struct i2c_msg msg = {
		.addr = DP_DUAL_MODE_SLAVE_ADDRESS,
		.flags = 0,
		.len = 1 + size,
		.buf = NULL,
	};
	void *data;
	int ret;

	data = kmalloc(msg.len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	msg.buf = data;

	memcpy(data, &offset, 1);
	memcpy(data + 1, buffer, size);

	ret = i2c_transfer(adapter, &msg, 1);

	kfree(data);

	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EPROTO;

	return 0;
}
EXPORT_SYMBOL(drm_dp_dual_mode_write);

static bool is_hdmi_adaptor(const char hdmi_id[DP_DUAL_MODE_HDMI_ID_LEN])
{
	static const char dp_dual_mode_hdmi_id[DP_DUAL_MODE_HDMI_ID_LEN + 1] =
		"DP-HDMI ADAPTOR\x04";

	return memcmp(hdmi_id, dp_dual_mode_hdmi_id,
		      DP_DUAL_MODE_HDMI_ID_LEN) == 0;
}

static bool is_type1_adaptor(uint8_t adaptor_id)
{
	return adaptor_id == 0 || adaptor_id == 0xff;
}

static bool is_type2_adaptor(uint8_t adaptor_id)
{
	return adaptor_id == (DP_DUAL_MODE_TYPE_TYPE2 |
			      DP_DUAL_MODE_REV_TYPE2);
}

static bool is_lspcon_adaptor(const char hdmi_id[DP_DUAL_MODE_HDMI_ID_LEN],
			      const uint8_t adaptor_id)
{
	return is_hdmi_adaptor(hdmi_id) &&
		(adaptor_id == (DP_DUAL_MODE_TYPE_TYPE2 |
		 DP_DUAL_MODE_TYPE_HAS_DPCD));
}

/**
 * drm_dp_dual_mode_detect - Identify the DP dual mode adaptor
 * @dev: &drm_device to use
 * @adapter: I2C adapter for the DDC bus
 *
 * Attempt to identify the type of the DP dual mode adaptor used.
 *
 * Note that when the answer is @DRM_DP_DUAL_MODE_UNKNOWN it's not
 * certain whether we're dealing with a native HDMI port or
 * a type 1 DVI dual mode adaptor. The driver will have to use
 * some other hardware/driver specific mechanism to make that
 * distinction.
 *
 * Returns:
 * The type of the DP dual mode adaptor used
 */
enum drm_dp_dual_mode_type drm_dp_dual_mode_detect(const struct drm_device *dev,
						   struct i2c_adapter *adapter)
{
	char hdmi_id[DP_DUAL_MODE_HDMI_ID_LEN] = {};
	uint8_t adaptor_id = 0x00;
	ssize_t ret;

	/*
	 * Let's see if the adaptor is there the by reading the
	 * HDMI ID registers.
	 *
	 * Note that type 1 DVI adaptors are not required to implemnt
	 * any registers, and that presents a problem for detection.
	 * If the i2c transfer is nacked, we may or may not be dealing
	 * with a type 1 DVI adaptor. Some other mechanism of detecting
	 * the presence of the adaptor is required. One way would be
	 * to check the state of the CONFIG1 pin, Another method would
	 * simply require the driver to know whether the port is a DP++
	 * port or a native HDMI port. Both of these methods are entirely
	 * hardware/driver specific so we can't deal with them here.
	 */
	ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_HDMI_ID,
				    hdmi_id, sizeof(hdmi_id));
	drm_dbg_kms(dev, "DP dual mode HDMI ID: %*pE (err %zd)\n",
		    ret ? 0 : (int)sizeof(hdmi_id), hdmi_id, ret);
	if (ret)
		return DRM_DP_DUAL_MODE_UNKNOWN;

	ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_ADAPTOR_ID,
				    &adaptor_id, sizeof(adaptor_id));
	drm_dbg_kms(dev, "DP dual mode adaptor ID: %02x (err %zd)\n", adaptor_id, ret);
	if (ret == 0) {
		if (is_lspcon_adaptor(hdmi_id, adaptor_id))
			return DRM_DP_DUAL_MODE_LSPCON;
		if (is_type2_adaptor(adaptor_id)) {
			if (is_hdmi_adaptor(hdmi_id))
				return DRM_DP_DUAL_MODE_TYPE2_HDMI;
			else
				return DRM_DP_DUAL_MODE_TYPE2_DVI;
		}
		/*
		 * If not a proper type 1 ID, still assume type 1, but let
		 * the user know that we may have misdetected the type.
		 */
		if (!is_type1_adaptor(adaptor_id))
			drm_err(dev, "Unexpected DP dual mode adaptor ID %02x\n", adaptor_id);

	}

	if (is_hdmi_adaptor(hdmi_id))
		return DRM_DP_DUAL_MODE_TYPE1_HDMI;
	else
		return DRM_DP_DUAL_MODE_TYPE1_DVI;
}
EXPORT_SYMBOL(drm_dp_dual_mode_detect);

/**
 * drm_dp_dual_mode_max_tmds_clock - Max TMDS clock for DP dual mode adaptor
 * @dev: &drm_device to use
 * @type: DP dual mode adaptor type
 * @adapter: I2C adapter for the DDC bus
 *
 * Determine the max TMDS clock the adaptor supports based on the
 * type of the dual mode adaptor and the DP_DUAL_MODE_MAX_TMDS_CLOCK
 * register (on type2 adaptors). As some type 1 adaptors have
 * problems with registers (see comments in drm_dp_dual_mode_detect())
 * we don't read the register on those, instead we simply assume
 * a 165 MHz limit based on the specification.
 *
 * Returns:
 * Maximum supported TMDS clock rate for the DP dual mode adaptor in kHz.
 */
int drm_dp_dual_mode_max_tmds_clock(const struct drm_device *dev, enum drm_dp_dual_mode_type type,
				    struct i2c_adapter *adapter)
{
	uint8_t max_tmds_clock;
	ssize_t ret;

	/* native HDMI so no limit */
	if (type == DRM_DP_DUAL_MODE_NONE)
		return 0;

	/*
	 * Type 1 adaptors are limited to 165MHz
	 * Type 2 adaptors can tells us their limit
	 */
	if (type < DRM_DP_DUAL_MODE_TYPE2_DVI)
		return 165000;

	ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_MAX_TMDS_CLOCK,
				    &max_tmds_clock, sizeof(max_tmds_clock));
	if (ret || max_tmds_clock == 0x00 || max_tmds_clock == 0xff) {
		drm_dbg_kms(dev, "Failed to query max TMDS clock\n");
		return 165000;
	}

	return max_tmds_clock * 5000 / 2;
}
EXPORT_SYMBOL(drm_dp_dual_mode_max_tmds_clock);

/**
 * drm_dp_dual_mode_get_tmds_output - Get the state of the TMDS output buffers in the DP dual mode adaptor
 * @dev: &drm_device to use
 * @type: DP dual mode adaptor type
 * @adapter: I2C adapter for the DDC bus
 * @enabled: current state of the TMDS output buffers
 *
 * Get the state of the TMDS output buffers in the adaptor. For
 * type2 adaptors this is queried from the DP_DUAL_MODE_TMDS_OEN
 * register. As some type 1 adaptors have problems with registers
 * (see comments in drm_dp_dual_mode_detect()) we don't read the
 * register on those, instead we simply assume that the buffers
 * are always enabled.
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
int drm_dp_dual_mode_get_tmds_output(const struct drm_device *dev,
				     enum drm_dp_dual_mode_type type, struct i2c_adapter *adapter,
				     bool *enabled)
{
	uint8_t tmds_oen;
	ssize_t ret;

	if (type < DRM_DP_DUAL_MODE_TYPE2_DVI) {
		*enabled = true;
		return 0;
	}

	ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_TMDS_OEN,
				    &tmds_oen, sizeof(tmds_oen));
	if (ret) {
		drm_dbg_kms(dev, "Failed to query state of TMDS output buffers\n");
		return ret;
	}

	*enabled = !(tmds_oen & DP_DUAL_MODE_TMDS_DISABLE);

	return 0;
}
EXPORT_SYMBOL(drm_dp_dual_mode_get_tmds_output);

/**
 * drm_dp_dual_mode_set_tmds_output - Enable/disable TMDS output buffers in the DP dual mode adaptor
 * @dev: &drm_device to use
 * @type: DP dual mode adaptor type
 * @adapter: I2C adapter for the DDC bus
 * @enable: enable (as opposed to disable) the TMDS output buffers
 *
 * Set the state of the TMDS output buffers in the adaptor. For
 * type2 this is set via the DP_DUAL_MODE_TMDS_OEN register.
 * Type1 adaptors do not support any register writes.
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
int drm_dp_dual_mode_set_tmds_output(const struct drm_device *dev, enum drm_dp_dual_mode_type type,
				     struct i2c_adapter *adapter, bool enable)
{
	uint8_t tmds_oen = enable ? 0 : DP_DUAL_MODE_TMDS_DISABLE;
	ssize_t ret;
	int retry;

	if (type < DRM_DP_DUAL_MODE_TYPE2_DVI)
		return 0;

	/*
	 * LSPCON adapters in low-power state may ignore the first write, so
	 * read back and verify the written value a few times.
	 */
	for (retry = 0; retry < 3; retry++) {
		uint8_t tmp;

		ret = drm_dp_dual_mode_write(adapter, DP_DUAL_MODE_TMDS_OEN,
					     &tmds_oen, sizeof(tmds_oen));
		if (ret) {
			drm_dbg_kms(dev, "Failed to %s TMDS output buffers (%d attempts)\n",
				    enable ? "enable" : "disable", retry + 1);
			return ret;
		}

		ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_TMDS_OEN,
					    &tmp, sizeof(tmp));
		if (ret) {
			drm_dbg_kms(dev,
				    "I2C read failed during TMDS output buffer %s (%d attempts)\n",
				    enable ? "enabling" : "disabling", retry + 1);
			return ret;
		}

		if (tmp == tmds_oen)
			return 0;
	}

	drm_dbg_kms(dev, "I2C write value mismatch during TMDS output buffer %s\n",
		    enable ? "enabling" : "disabling");

	return -EIO;
}
EXPORT_SYMBOL(drm_dp_dual_mode_set_tmds_output);

/**
 * drm_dp_get_dual_mode_type_name - Get the name of the DP dual mode adaptor type as a string
 * @type: DP dual mode adaptor type
 *
 * Returns:
 * String representation of the DP dual mode adaptor type
 */
const char *drm_dp_get_dual_mode_type_name(enum drm_dp_dual_mode_type type)
{
	switch (type) {
	case DRM_DP_DUAL_MODE_NONE:
		return "none";
	case DRM_DP_DUAL_MODE_TYPE1_DVI:
		return "type 1 DVI";
	case DRM_DP_DUAL_MODE_TYPE1_HDMI:
		return "type 1 HDMI";
	case DRM_DP_DUAL_MODE_TYPE2_DVI:
		return "type 2 DVI";
	case DRM_DP_DUAL_MODE_TYPE2_HDMI:
		return "type 2 HDMI";
	case DRM_DP_DUAL_MODE_LSPCON:
		return "lspcon";
	default:
		WARN_ON(type != DRM_DP_DUAL_MODE_UNKNOWN);
		return "unknown";
	}
}
EXPORT_SYMBOL(drm_dp_get_dual_mode_type_name);

/**
 * drm_lspcon_get_mode: Get LSPCON's current mode of operation by
 * reading offset (0x80, 0x41)
 * @dev: &drm_device to use
 * @adapter: I2C-over-aux adapter
 * @mode: current lspcon mode of operation output variable
 *
 * Returns:
 * 0 on success, sets the current_mode value to appropriate mode
 * -error on failure
 */
int drm_lspcon_get_mode(const struct drm_device *dev, struct i2c_adapter *adapter,
			enum drm_lspcon_mode *mode)
{
	u8 data;
	int ret = 0;
	int retry;

	if (!mode) {
		drm_err(dev, "NULL input\n");
		return -EINVAL;
	}

	/* Read Status: i2c over aux */
	for (retry = 0; retry < 6; retry++) {
		if (retry)
			usleep_range(500, 1000);

		ret = drm_dp_dual_mode_read(adapter,
					    DP_DUAL_MODE_LSPCON_CURRENT_MODE,
					    &data, sizeof(data));
		if (!ret)
			break;
	}

	if (ret < 0) {
		drm_dbg_kms(dev, "LSPCON read(0x80, 0x41) failed\n");
		return -EFAULT;
	}

	if (data & DP_DUAL_MODE_LSPCON_MODE_PCON)
		*mode = DRM_LSPCON_MODE_PCON;
	else
		*mode = DRM_LSPCON_MODE_LS;
	return 0;
}
EXPORT_SYMBOL(drm_lspcon_get_mode);

/**
 * drm_lspcon_set_mode: Change LSPCON's mode of operation by
 * writing offset (0x80, 0x40)
 * @dev: &drm_device to use
 * @adapter: I2C-over-aux adapter
 * @mode: required mode of operation
 *
 * Returns:
 * 0 on success, -error on failure/timeout
 */
int drm_lspcon_set_mode(const struct drm_device *dev, struct i2c_adapter *adapter,
			enum drm_lspcon_mode mode)
{
	u8 data = 0;
	int ret;
	int time_out = 200;
	enum drm_lspcon_mode current_mode;

	if (mode == DRM_LSPCON_MODE_PCON)
		data = DP_DUAL_MODE_LSPCON_MODE_PCON;

	/* Change mode */
	ret = drm_dp_dual_mode_write(adapter, DP_DUAL_MODE_LSPCON_MODE_CHANGE,
				     &data, sizeof(data));
	if (ret < 0) {
		drm_err(dev, "LSPCON mode change failed\n");
		return ret;
	}

	/*
	 * Confirm mode change by reading the status bit.
	 * Sometimes, it takes a while to change the mode,
	 * so wait and retry until time out or done.
	 */
	do {
		ret = drm_lspcon_get_mode(dev, adapter, &current_mode);
		if (ret) {
			drm_err(dev, "can't confirm LSPCON mode change\n");
			return ret;
		} else {
			if (current_mode != mode) {
				drm_msleep(10);
				time_out -= 10;
			} else {
				drm_dbg_kms(dev, "LSPCON mode changed to %s\n",
					    mode == DRM_LSPCON_MODE_LS ? "LS" : "PCON");
				return 0;
			}
		}
	} while (time_out);

	drm_err(dev, "LSPCON mode change timed out\n");
	return -ETIMEDOUT;
}
EXPORT_SYMBOL(drm_lspcon_set_mode);
