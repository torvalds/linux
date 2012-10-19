/*
 * HID Sensors Driver
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef _HID_SENSORS_HUB_H
#define _HID_SENSORS_HUB_H

#include <linux/hid.h>
#include <linux/hid-sensor-ids.h>

/**
 * struct hid_sensor_hub_attribute_info - Attribute info
 * @usage_id:		Parent usage id of a physical device.
 * @attrib_id:		Attribute id for this attribute.
 * @report_id:		Report id in which this information resides.
 * @index:		Field index in the report.
 * @units:		Measurment unit for this attribute.
 * @unit_expo:		Exponent used in the data.
 * @size:		Size in bytes for data size.
 */
struct hid_sensor_hub_attribute_info {
	u32 usage_id;
	u32 attrib_id;
	s32 report_id;
	s32 index;
	s32 units;
	s32 unit_expo;
	s32 size;
};

/**
 * struct hid_sensor_hub_device - Stores the hub instance data
 * @hdev:		Stores the hid instance.
 * @vendor_id:		Vendor id of hub device.
 * @product_id:		Product id of hub device.
 */
struct hid_sensor_hub_device {
	struct hid_device *hdev;
	u32 vendor_id;
	u32 product_id;
};

/**
 * struct hid_sensor_hub_callbacks - Client callback functions
 * @pdev:		Platform device instance of the client driver.
 * @suspend:		Suspend callback.
 * @resume:		Resume callback.
 * @capture_sample:	Callback to get a sample.
 * @send_event:		Send notification to indicate all samples are
 *			captured, process and send event
 */
struct hid_sensor_hub_callbacks {
	struct platform_device *pdev;
	int (*suspend)(struct hid_sensor_hub_device *hsdev, void *priv);
	int (*resume)(struct hid_sensor_hub_device *hsdev, void *priv);
	int (*capture_sample)(struct hid_sensor_hub_device *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv);
	int (*send_event)(struct hid_sensor_hub_device *hsdev, u32 usage_id,
			 void *priv);
};

/* Registration functions */

/**
* sensor_hub_register_callback() - Register client callbacks
* @hsdev:	Hub device instance.
* @usage_id:	Usage id of the client (E.g. 0x200076 for Gyro).
* @usage_callback: Callback function storage
*
* Used to register callbacks by client processing drivers. Sensor
* hub core driver will call these callbacks to offload processing
* of data streams and notifications.
*/
int sensor_hub_register_callback(struct hid_sensor_hub_device *hsdev,
			u32 usage_id,
			struct hid_sensor_hub_callbacks *usage_callback);

/**
* sensor_hub_remove_callback() - Remove client callbacks
* @hsdev:	Hub device instance.
* @usage_id:	Usage id of the client (E.g. 0x200076 for Gyro).
*
* If there is a callback registred, this call will remove that
* callbacks, so that it will stop data and event notifications.
*/
int sensor_hub_remove_callback(struct hid_sensor_hub_device *hsdev,
			u32 usage_id);


/* Hid sensor hub core interfaces */

/**
* sensor_hub_input_get_attribute_info() - Get an attribute information
* @hsdev:	Hub device instance.
* @type:	Type of this attribute, input/output/feature
* @usage_id:	Attribute usage id of parent physical device as per spec
* @attr_usage_id:	Attribute usage id as per spec
* @info:	return information about attribute after parsing report
*
* Parses report and returns the attribute information such as report id,
* field index, units and exponet etc.
*/
int sensor_hub_input_get_attribute_info(struct hid_sensor_hub_device *hsdev,
			u8 type,
			u32 usage_id, u32 attr_usage_id,
			struct hid_sensor_hub_attribute_info *info);

/**
* sensor_hub_input_attr_get_raw_value() - Synchronous read request
* @usage_id:	Attribute usage id of parent physical device as per spec
* @attr_usage_id:	Attribute usage id as per spec
* @report_id:	Report id to look for
*
* Issues a synchronous read request for an input attribute. Returns
* data upto 32 bits. Since client can get events, so this call should
* not be used for data paths, this will impact performance.
*/

int sensor_hub_input_attr_get_raw_value(struct hid_sensor_hub_device *hsdev,
			u32 usage_id,
			u32 attr_usage_id, u32 report_id);
/**
* sensor_hub_set_feature() - Feature set request
* @report_id:	Report id to look for
* @field_index:	Field index inside a report
* @value:	Value to set
*
* Used to set a field in feature report. For example this can set polling
* interval, sensitivity, activate/deactivate state.
*/
int sensor_hub_set_feature(struct hid_sensor_hub_device *hsdev, u32 report_id,
			u32 field_index, s32 value);

/**
* sensor_hub_get_feature() - Feature get request
* @report_id:	Report id to look for
* @field_index:	Field index inside a report
* @value:	Place holder for return value
*
* Used to get a field in feature report. For example this can get polling
* interval, sensitivity, activate/deactivate state.
*/
int sensor_hub_get_feature(struct hid_sensor_hub_device *hsdev, u32 report_id,
			u32 field_index, s32 *value);
#endif
