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
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>

/**
 * struct hid_sensor_hub_attribute_info - Attribute info
 * @usage_id:		Parent usage id of a physical device.
 * @attrib_id:		Attribute id for this attribute.
 * @report_id:		Report id in which this information resides.
 * @index:		Field index in the report.
 * @units:		Measurment unit for this attribute.
 * @unit_expo:		Exponent used in the data.
 * @size:		Size in bytes for data size.
 * @logical_minimum:	Logical minimum value for this attribute.
 * @logical_maximum:	Logical maximum value for this attribute.
 */
struct hid_sensor_hub_attribute_info {
	u32 usage_id;
	u32 attrib_id;
	s32 report_id;
	s32 index;
	s32 units;
	s32 unit_expo;
	s32 size;
	s32 logical_minimum;
	s32 logical_maximum;
};

/**
 * struct sensor_hub_pending - Synchronous read pending information
 * @status:		Pending status true/false.
 * @ready:		Completion synchronization data.
 * @usage_id:		Usage id for physical device, E.g. Gyro usage id.
 * @attr_usage_id:	Usage Id of a field, E.g. X-AXIS for a gyro.
 * @raw_size:		Response size for a read request.
 * @raw_data:		Place holder for received response.
 */
struct sensor_hub_pending {
	bool status;
	struct completion ready;
	u32 usage_id;
	u32 attr_usage_id;
	int raw_size;
	u8  *raw_data;
};

/**
 * struct hid_sensor_hub_device - Stores the hub instance data
 * @hdev:		Stores the hid instance.
 * @vendor_id:		Vendor id of hub device.
 * @product_id:		Product id of hub device.
 * @usage:		Usage id for this hub device instance.
 * @start_collection_index: Starting index for a phy type collection
 * @end_collection_index: Last index for a phy type collection
 * @mutex_ptr:		synchronizing mutex pointer.
 * @pending:		Holds information of pending sync read request.
 */
struct hid_sensor_hub_device {
	struct hid_device *hdev;
	u32 vendor_id;
	u32 product_id;
	u32 usage;
	int start_collection_index;
	int end_collection_index;
	struct mutex *mutex_ptr;
	struct sensor_hub_pending pending;
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

/**
* sensor_hub_device_open() - Open hub device
* @hsdev:	Hub device instance.
*
* Used to open hid device for sensor hub.
*/
int sensor_hub_device_open(struct hid_sensor_hub_device *hsdev);

/**
* sensor_hub_device_clode() - Close hub device
* @hsdev:	Hub device instance.
*
* Used to clode hid device for sensor hub.
*/
void sensor_hub_device_close(struct hid_sensor_hub_device *hsdev);

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
* @hsdev:	Hub device instance.
* @usage_id:	Attribute usage id of parent physical device as per spec
* @attr_usage_id:	Attribute usage id as per spec
* @report_id:	Report id to look for
* @flag:      Synchronous or asynchronous read
*
* Issues a synchronous or asynchronous read request for an input attribute.
* Returns data upto 32 bits.
*/

enum sensor_hub_read_flags {
	SENSOR_HUB_SYNC,
	SENSOR_HUB_ASYNC,
};

int sensor_hub_input_attr_get_raw_value(struct hid_sensor_hub_device *hsdev,
 					u32 usage_id,
 					u32 attr_usage_id, u32 report_id,
 					enum sensor_hub_read_flags flag
);

/**
* sensor_hub_set_feature() - Feature set request
* @hsdev:	Hub device instance.
* @report_id:	Report id to look for
* @field_index:	Field index inside a report
* @buffer_size: size of the buffer
* @buffer:	buffer to use in the feature set
*
* Used to set a field in feature report. For example this can set polling
* interval, sensitivity, activate/deactivate state.
*/
int sensor_hub_set_feature(struct hid_sensor_hub_device *hsdev, u32 report_id,
			   u32 field_index, int buffer_size, void *buffer);

/**
* sensor_hub_get_feature() - Feature get request
* @hsdev:	Hub device instance.
* @report_id:	Report id to look for
* @field_index:	Field index inside a report
* @buffer_size:	size of the buffer
* @buffer:	buffer to copy output
*
* Used to get a field in feature report. For example this can get polling
* interval, sensitivity, activate/deactivate state. On success it returns
* number of bytes copied to buffer. On failure, it returns value < 0.
*/
int sensor_hub_get_feature(struct hid_sensor_hub_device *hsdev, u32 report_id,
			   u32 field_index, int buffer_size, void *buffer);

/* hid-sensor-attributes */

/* Common hid sensor iio structure */
struct hid_sensor_common {
	struct hid_sensor_hub_device *hsdev;
	struct platform_device *pdev;
	unsigned usage_id;
	atomic_t data_ready;
	atomic_t user_requested_state;
	struct iio_trigger *trigger;
	struct hid_sensor_hub_attribute_info poll;
	struct hid_sensor_hub_attribute_info report_state;
	struct hid_sensor_hub_attribute_info power_state;
	struct hid_sensor_hub_attribute_info sensitivity;
};

/* Convert from hid unit expo to regular exponent */
static inline int hid_sensor_convert_exponent(int unit_expo)
{
	if (unit_expo < 0x08)
		return unit_expo;
	else if (unit_expo <= 0x0f)
		return -(0x0f-unit_expo+1);
	else
		return 0;
}

int hid_sensor_parse_common_attributes(struct hid_sensor_hub_device *hsdev,
					u32 usage_id,
					struct hid_sensor_common *st);
int hid_sensor_write_raw_hyst_value(struct hid_sensor_common *st,
					int val1, int val2);
int hid_sensor_read_raw_hyst_value(struct hid_sensor_common *st,
					int *val1, int *val2);
int hid_sensor_write_samp_freq_value(struct hid_sensor_common *st,
					int val1, int val2);
int hid_sensor_read_samp_freq_value(struct hid_sensor_common *st,
					int *val1, int *val2);

int hid_sensor_get_usage_index(struct hid_sensor_hub_device *hsdev,
				u32 report_id, int field_index, u32 usage_id);

int hid_sensor_format_scale(u32 usage_id,
			    struct hid_sensor_hub_attribute_info *attr_info,
			    int *val0, int *val1);

s32 hid_sensor_read_poll_value(struct hid_sensor_common *st);

#endif
