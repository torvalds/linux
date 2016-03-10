/*
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _RMI_H
#define _RMI_H
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/types.h>

#define NAME_BUFFER_SIZE 256

/**
 * struct rmi_2d_axis_alignment - target axis alignment
 * @swap_axes: set to TRUE if desired to swap x- and y-axis
 * @flip_x: set to TRUE if desired to flip direction on x-axis
 * @flip_y: set to TRUE if desired to flip direction on y-axis
 * @clip_x_low - reported X coordinates below this setting will be clipped to
 *               the specified value
 * @clip_x_high - reported X coordinates above this setting will be clipped to
 *               the specified value
 * @clip_y_low - reported Y coordinates below this setting will be clipped to
 *               the specified value
 * @clip_y_high - reported Y coordinates above this setting will be clipped to
 *               the specified value
 * @offset_x - this value will be added to all reported X coordinates
 * @offset_y - this value will be added to all reported Y coordinates
 * @rel_report_enabled - if set to true, the relative reporting will be
 *               automatically enabled for this sensor.
 */
struct rmi_2d_axis_alignment {
	bool swap_axes;
	bool flip_x;
	bool flip_y;
	u16 clip_x_low;
	u16 clip_y_low;
	u16 clip_x_high;
	u16 clip_y_high;
	u16 offset_x;
	u16 offset_y;
	u8 delta_x_threshold;
	u8 delta_y_threshold;
};

/** This is used to override any hints an F11 2D sensor might have provided
 * as to what type of sensor it is.
 *
 * @rmi_f11_sensor_default - do not override, determine from F11_2D_QUERY14 if
 * available.
 * @rmi_f11_sensor_touchscreen - treat the sensor as a touchscreen (direct
 * pointing).
 * @rmi_f11_sensor_touchpad - thread the sensor as a touchpad (indirect
 * pointing).
 */
enum rmi_sensor_type {
	rmi_sensor_default = 0,
	rmi_sensor_touchscreen,
	rmi_sensor_touchpad
};

#define RMI_F11_DISABLE_ABS_REPORT      BIT(0)

/**
 * struct rmi_2d_sensor_data - overrides defaults for a 2D sensor.
 * @axis_align - provides axis alignment overrides (see above).
 * @sensor_type - Forces the driver to treat the sensor as an indirect
 * pointing device (touchpad) rather than a direct pointing device
 * (touchscreen).  This is useful when F11_2D_QUERY14 register is not
 * available.
 * @disable_report_mask - Force data to not be reported even if it is supported
 * by the firware.
 * @topbuttonpad - Used with the "5 buttons touchpads" found on the Lenovo 40
 * series
 * @kernel_tracking - most moderns RMI f11 firmwares implement Multifinger
 * Type B protocol. However, there are some corner cases where the user
 * triggers some jumps by tapping with two fingers on the touchpad.
 * Use this setting and dmax to filter out these jumps.
 * Also, when using an old sensor using MF Type A behavior, set to true to
 * report an actual MT protocol B.
 * @dmax - the maximum distance (in sensor units) the kernel tracking allows two
 * distincts fingers to be considered the same.
 */
struct rmi_2d_sensor_platform_data {
	struct rmi_2d_axis_alignment axis_align;
	enum rmi_sensor_type sensor_type;
	int x_mm;
	int y_mm;
	int disable_report_mask;
	u16 rezero_wait;
	bool topbuttonpad;
	bool kernel_tracking;
	int dmax;
};

/**
 * struct rmi_f01_power - override default power management settings.
 *
 */
enum rmi_f01_nosleep {
	RMI_F01_NOSLEEP_DEFAULT = 0,
	RMI_F01_NOSLEEP_OFF = 1,
	RMI_F01_NOSLEEP_ON = 2
};

/**
 * struct rmi_f01_power_management -When non-zero, these values will be written
 * to the touch sensor to override the default firmware settigns.  For a
 * detailed explanation of what each field does, see the corresponding
 * documention in the RMI4 specification.
 *
 * @nosleep - specifies whether the device is permitted to sleep or doze (that
 * is, enter a temporary low power state) when no fingers are touching the
 * sensor.
 * @wakeup_threshold - controls the capacitance threshold at which the touch
 * sensor will decide to wake up from that low power state.
 * @doze_holdoff - controls how long the touch sensor waits after the last
 * finger lifts before entering the doze state, in units of 100ms.
 * @doze_interval - controls the interval between checks for finger presence
 * when the touch sensor is in doze mode, in units of 10ms.
 */
struct rmi_f01_power_management {
	enum rmi_f01_nosleep nosleep;
	u8 wakeup_threshold;
	u8 doze_holdoff;
	u8 doze_interval;
};

/**
 * struct rmi_device_platform_data - system specific configuration info.
 *
 * @reset_delay_ms - after issuing a reset command to the touch sensor, the
 * driver waits a few milliseconds to give the firmware a chance to
 * to re-initialize.  You can override the default wait period here.
 */
struct rmi_device_platform_data {
	int reset_delay_ms;

	/* function handler pdata */
	struct rmi_2d_sensor_platform_data *sensor_pdata;
	struct rmi_f01_power_management power_management;
};

/**
 * struct rmi_function_descriptor - RMI function base addresses
 *
 * @query_base_addr: The RMI Query base address
 * @command_base_addr: The RMI Command base address
 * @control_base_addr: The RMI Control base address
 * @data_base_addr: The RMI Data base address
 * @interrupt_source_count: The number of irqs this RMI function needs
 * @function_number: The RMI function number
 *
 * This struct is used when iterating the Page Description Table. The addresses
 * are 16-bit values to include the current page address.
 *
 */
struct rmi_function_descriptor {
	u16 query_base_addr;
	u16 command_base_addr;
	u16 control_base_addr;
	u16 data_base_addr;
	u8 interrupt_source_count;
	u8 function_number;
	u8 function_version;
};

struct rmi_device;

/**
 * struct rmi_transport_dev - represent an RMI transport device
 *
 * @dev: Pointer to the communication device, e.g. i2c or spi
 * @rmi_dev: Pointer to the RMI device
 * @proto_name: name of the transport protocol (SPI, i2c, etc)
 * @ops: pointer to transport operations implementation
 *
 * The RMI transport device implements the glue between different communication
 * buses such as I2C and SPI.
 *
 */
struct rmi_transport_dev {
	struct device *dev;
	struct rmi_device *rmi_dev;

	const char *proto_name;
	const struct rmi_transport_ops *ops;

	struct rmi_device_platform_data pdata;

	struct input_dev *input;

	void *attn_data;
	int attn_size;
};

/**
 * struct rmi_transport_ops - defines transport protocol operations.
 *
 * @write_block: Writing a block of data to the specified address
 * @read_block: Read a block of data from the specified address.
 */
struct rmi_transport_ops {
	int (*write_block)(struct rmi_transport_dev *xport, u16 addr,
			   const void *buf, size_t len);
	int (*read_block)(struct rmi_transport_dev *xport, u16 addr,
			  void *buf, size_t len);
	int (*reset)(struct rmi_transport_dev *xport, u16 reset_addr);
};

/**
 * struct rmi_driver - driver for an RMI4 sensor on the RMI bus.
 *
 * @driver: Device driver model driver
 * @reset_handler: Called when a reset is detected.
 * @clear_irq_bits: Clear the specified bits in the current interrupt mask.
 * @set_irq_bist: Set the specified bits in the current interrupt mask.
 * @store_productid: Callback for cache product id from function 01
 * @data: Private data pointer
 *
 */
struct rmi_driver {
	struct device_driver driver;

	int (*reset_handler)(struct rmi_device *rmi_dev);
	int (*clear_irq_bits)(struct rmi_device *rmi_dev, unsigned long *mask);
	int (*set_irq_bits)(struct rmi_device *rmi_dev, unsigned long *mask);
	int (*store_productid)(struct rmi_device *rmi_dev);
	int (*set_input_params)(struct rmi_device *rmi_dev,
			struct input_dev *input);
	void *data;
};

/**
 * struct rmi_device - represents an RMI4 sensor device on the RMI bus.
 *
 * @dev: The device created for the RMI bus
 * @number: Unique number for the device on the bus.
 * @driver: Pointer to associated driver
 * @xport: Pointer to the transport interface
 *
 */
struct rmi_device {
	struct device dev;
	int number;

	struct rmi_driver *driver;
	struct rmi_transport_dev *xport;

};

struct rmi_driver_data {
	struct list_head function_list;

	struct rmi_device *rmi_dev;

	struct rmi_function *f01_container;
	bool f01_bootloader_mode;

	u32 attn_count;
	int num_of_irq_regs;
	int irq_count;
	unsigned long *irq_status;
	unsigned long *fn_irq_bits;
	unsigned long *current_irq_mask;
	unsigned long *new_irq_mask;
	struct mutex irq_mutex;
	struct input_dev *input;

	u8 pdt_props;
	u8 bsr;

	bool enabled;

	void *data;
};

int rmi_register_transport_device(struct rmi_transport_dev *xport);
void rmi_unregister_transport_device(struct rmi_transport_dev *xport);
int rmi_process_interrupt_requests(struct rmi_device *rmi_dev);

int rmi_driver_suspend(struct rmi_device *rmi_dev);
int rmi_driver_resume(struct rmi_device *rmi_dev);
#endif
