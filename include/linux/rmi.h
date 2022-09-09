/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef _RMI_H
#define _RMI_H
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/kfifo.h>
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
	int dribble;
	int palm_detect;
};

/**
 * struct rmi_gpio_data - overrides defaults for a single F30/F3A GPIOs/LED
 * chip.
 * @buttonpad - the touchpad is a buttonpad, so enable only the first actual
 * button that is found.
 * @trackstick_buttons - Set when the function 30 or 3a is handling the physical
 * buttons of the trackstick (as a PS/2 passthrough device).
 * @disable - the touchpad incorrectly reports F30/F3A and it should be ignored.
 * This is a special case which is due to misconfigured firmware.
 */
struct rmi_gpio_data {
	bool buttonpad;
	bool trackstick_buttons;
	bool disable;
};


/*
 * Set the state of a register
 *	DEFAULT - use the default value set by the firmware config
 *	OFF - explicitly disable the register
 *	ON - explicitly enable the register
 */
enum rmi_reg_state {
	RMI_REG_STATE_DEFAULT = 0,
	RMI_REG_STATE_OFF = 1,
	RMI_REG_STATE_ON = 2
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
	enum rmi_reg_state nosleep;
	u8 wakeup_threshold;
	u8 doze_holdoff;
	u8 doze_interval;
};

/**
 * struct rmi_device_platform_data_spi - provides parameters used in SPI
 * communications.  All Synaptics SPI products support a standard SPI
 * interface; some also support what is called SPI V2 mode, depending on
 * firmware and/or ASIC limitations.  In V2 mode, the touch sensor can
 * support shorter delays during certain operations, and these are specified
 * separately from the standard mode delays.
 *
 * @block_delay - for standard SPI transactions consisting of both a read and
 * write operation, the delay (in microseconds) between the read and write
 * operations.
 * @split_read_block_delay_us - for V2 SPI transactions consisting of both a
 * read and write operation, the delay (in microseconds) between the read and
 * write operations.
 * @read_delay_us - the delay between each byte of a read operation in normal
 * SPI mode.
 * @write_delay_us - the delay between each byte of a write operation in normal
 * SPI mode.
 * @split_read_byte_delay_us - the delay between each byte of a read operation
 * in V2 mode.
 * @pre_delay_us - the delay before the start of a SPI transaction.  This is
 * typically useful in conjunction with custom chip select assertions (see
 * below).
 * @post_delay_us - the delay after the completion of an SPI transaction.  This
 * is typically useful in conjunction with custom chip select assertions (see
 * below).
 * @cs_assert - For systems where the SPI subsystem does not control the CS/SSB
 * line, or where such control is broken, you can provide a custom routine to
 * handle a GPIO as CS/SSB.  This routine will be called at the beginning and
 * end of each SPI transaction.  The RMI SPI implementation will wait
 * pre_delay_us after this routine returns before starting the SPI transfer;
 * and post_delay_us after completion of the SPI transfer(s) before calling it
 * with assert==FALSE.
 */
struct rmi_device_platform_data_spi {
	u32 block_delay_us;
	u32 split_read_block_delay_us;
	u32 read_delay_us;
	u32 write_delay_us;
	u32 split_read_byte_delay_us;
	u32 pre_delay_us;
	u32 post_delay_us;
	u8 bits_per_word;
	u16 mode;

	void *cs_assert_data;
	int (*cs_assert)(const void *cs_assert_data, const bool assert);
};

/**
 * struct rmi_device_platform_data - system specific configuration info.
 *
 * @reset_delay_ms - after issuing a reset command to the touch sensor, the
 * driver waits a few milliseconds to give the firmware a chance to
 * re-initialize.  You can override the default wait period here.
 * @irq: irq associated with the attn gpio line, or negative
 */
struct rmi_device_platform_data {
	int reset_delay_ms;
	int irq;

	struct rmi_device_platform_data_spi spi_data;

	/* function handler pdata */
	struct rmi_2d_sensor_platform_data sensor_pdata;
	struct rmi_f01_power_management power_management;
	struct rmi_gpio_data gpio_data;
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

struct rmi4_attn_data {
	unsigned long irq_status;
	size_t size;
	void *data;
};

struct rmi_driver_data {
	struct list_head function_list;

	struct rmi_device *rmi_dev;

	struct rmi_function *f01_container;
	struct rmi_function *f34_container;
	bool bootloader_mode;

	int num_of_irq_regs;
	int irq_count;
	void *irq_memory;
	unsigned long *irq_status;
	unsigned long *fn_irq_bits;
	unsigned long *current_irq_mask;
	unsigned long *new_irq_mask;
	struct mutex irq_mutex;
	struct input_dev *input;

	struct irq_domain *irqdomain;

	u8 pdt_props;

	u8 num_rx_electrodes;
	u8 num_tx_electrodes;

	bool enabled;
	struct mutex enabled_mutex;

	struct rmi4_attn_data attn_data;
	DECLARE_KFIFO(attn_fifo, struct rmi4_attn_data, 16);
};

int rmi_register_transport_device(struct rmi_transport_dev *xport);
void rmi_unregister_transport_device(struct rmi_transport_dev *xport);

void rmi_set_attn_data(struct rmi_device *rmi_dev, unsigned long irq_status,
		       void *data, size_t size);

int rmi_driver_suspend(struct rmi_device *rmi_dev, bool enable_wake);
int rmi_driver_resume(struct rmi_device *rmi_dev, bool clear_wake);
#endif
