
/* The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef _INDUSTRIAL_IO_H_
#define _INDUSTRIAL_IO_H_

#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/iio/types.h>
#include <linux/of.h>
/* IIO TODO LIST */
/*
 * Provide means of adjusting timer accuracy.
 * Currently assumes nano seconds.
 */

enum iio_chan_info_enum {
	IIO_CHAN_INFO_RAW = 0,
	IIO_CHAN_INFO_PROCESSED,
	IIO_CHAN_INFO_SCALE,
	IIO_CHAN_INFO_OFFSET,
	IIO_CHAN_INFO_CALIBSCALE,
	IIO_CHAN_INFO_CALIBBIAS,
	IIO_CHAN_INFO_PEAK,
	IIO_CHAN_INFO_PEAK_SCALE,
	IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW,
	IIO_CHAN_INFO_AVERAGE_RAW,
	IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY,
	IIO_CHAN_INFO_SAMP_FREQ,
	IIO_CHAN_INFO_FREQUENCY,
	IIO_CHAN_INFO_PHASE,
	IIO_CHAN_INFO_HARDWAREGAIN,
	IIO_CHAN_INFO_HYSTERESIS,
	IIO_CHAN_INFO_INT_TIME,
};

enum iio_shared_by {
	IIO_SEPARATE,
	IIO_SHARED_BY_TYPE,
	IIO_SHARED_BY_DIR,
	IIO_SHARED_BY_ALL
};

enum iio_endian {
	IIO_CPU,
	IIO_BE,
	IIO_LE,
};

struct iio_chan_spec;
struct iio_dev;

/**
 * struct iio_chan_spec_ext_info - Extended channel info attribute
 * @name:	Info attribute name
 * @shared:	Whether this attribute is shared between all channels.
 * @read:	Read callback for this info attribute, may be NULL.
 * @write:	Write callback for this info attribute, may be NULL.
 * @private:	Data private to the driver.
 */
struct iio_chan_spec_ext_info {
	const char *name;
	enum iio_shared_by shared;
	ssize_t (*read)(struct iio_dev *, uintptr_t private,
			struct iio_chan_spec const *, char *buf);
	ssize_t (*write)(struct iio_dev *, uintptr_t private,
			 struct iio_chan_spec const *, const char *buf,
			 size_t len);
	uintptr_t private;
};

/**
 * struct iio_enum - Enum channel info attribute
 * @items:	An array of strings.
 * @num_items:	Length of the item array.
 * @set:	Set callback function, may be NULL.
 * @get:	Get callback function, may be NULL.
 *
 * The iio_enum struct can be used to implement enum style channel attributes.
 * Enum style attributes are those which have a set of strings which map to
 * unsigned integer values. The IIO enum helper code takes care of mapping
 * between value and string as well as generating a "_available" file which
 * contains a list of all available items. The set callback will be called when
 * the attribute is updated. The last parameter is the index to the newly
 * activated item. The get callback will be used to query the currently active
 * item and is supposed to return the index for it.
 */
struct iio_enum {
	const char * const *items;
	unsigned int num_items;
	int (*set)(struct iio_dev *, const struct iio_chan_spec *, unsigned int);
	int (*get)(struct iio_dev *, const struct iio_chan_spec *);
};

ssize_t iio_enum_available_read(struct iio_dev *indio_dev,
	uintptr_t priv, const struct iio_chan_spec *chan, char *buf);
ssize_t iio_enum_read(struct iio_dev *indio_dev,
	uintptr_t priv, const struct iio_chan_spec *chan, char *buf);
ssize_t iio_enum_write(struct iio_dev *indio_dev,
	uintptr_t priv, const struct iio_chan_spec *chan, const char *buf,
	size_t len);

/**
 * IIO_ENUM() - Initialize enum extended channel attribute
 * @_name:	Attribute name
 * @_shared:	Whether the attribute is shared between all channels
 * @_e:		Pointer to an iio_enum struct
 *
 * This should usually be used together with IIO_ENUM_AVAILABLE()
 */
#define IIO_ENUM(_name, _shared, _e) \
{ \
	.name = (_name), \
	.shared = (_shared), \
	.read = iio_enum_read, \
	.write = iio_enum_write, \
	.private = (uintptr_t)(_e), \
}

/**
 * IIO_ENUM_AVAILABLE() - Initialize enum available extended channel attribute
 * @_name:	Attribute name ("_available" will be appended to the name)
 * @_e:		Pointer to an iio_enum struct
 *
 * Creates a read only attribute which lists all the available enum items in a
 * space separated list. This should usually be used together with IIO_ENUM()
 */
#define IIO_ENUM_AVAILABLE(_name, _e) \
{ \
	.name = (_name "_available"), \
	.shared = IIO_SHARED_BY_TYPE, \
	.read = iio_enum_available_read, \
	.private = (uintptr_t)(_e), \
}

/**
 * struct iio_event_spec - specification for a channel event
 * @type:		    Type of the event
 * @dir:		    Direction of the event
 * @mask_separate:	    Bit mask of enum iio_event_info values. Attributes
 *			    set in this mask will be registered per channel.
 * @mask_shared_by_type:    Bit mask of enum iio_event_info values. Attributes
 *			    set in this mask will be shared by channel type.
 * @mask_shared_by_dir:	    Bit mask of enum iio_event_info values. Attributes
 *			    set in this mask will be shared by channel type and
 *			    direction.
 * @mask_shared_by_all:	    Bit mask of enum iio_event_info values. Attributes
 *			    set in this mask will be shared by all channels.
 */
struct iio_event_spec {
	enum iio_event_type type;
	enum iio_event_direction dir;
	unsigned long mask_separate;
	unsigned long mask_shared_by_type;
	unsigned long mask_shared_by_dir;
	unsigned long mask_shared_by_all;
};

/**
 * struct iio_chan_spec - specification of a single channel
 * @type:		What type of measurement is the channel making.
 * @channel:		What number do we wish to assign the channel.
 * @channel2:		If there is a second number for a differential
 *			channel then this is it. If modified is set then the
 *			value here specifies the modifier.
 * @address:		Driver specific identifier.
 * @scan_index:		Monotonic index to give ordering in scans when read
 *			from a buffer.
 * @scan_type:		Sign:		's' or 'u' to specify signed or unsigned
 *			realbits:	Number of valid bits of data
 *			storage_bits:	Realbits + padding
 *			shift:		Shift right by this before masking out
 *					realbits.
 *			endianness:	little or big endian
 *			repeat:		Number of times real/storage bits
 *					repeats. When the repeat element is
 *					more than 1, then the type element in
 *					sysfs will show a repeat value.
 *					Otherwise, the number of repetitions is
 *					omitted.
 * @info_mask_separate: What information is to be exported that is specific to
 *			this channel.
 * @info_mask_shared_by_type: What information is to be exported that is shared
 *			by all channels of the same type.
 * @info_mask_shared_by_dir: What information is to be exported that is shared
 *			by all channels of the same direction.
 * @info_mask_shared_by_all: What information is to be exported that is shared
 *			by all channels.
 * @event_spec:		Array of events which should be registered for this
 *			channel.
 * @num_event_specs:	Size of the event_spec array.
 * @ext_info:		Array of extended info attributes for this channel.
 *			The array is NULL terminated, the last element should
 *			have its name field set to NULL.
 * @extend_name:	Allows labeling of channel attributes with an
 *			informative name. Note this has no effect codes etc,
 *			unlike modifiers.
 * @datasheet_name:	A name used in in-kernel mapping of channels. It should
 *			correspond to the first name that the channel is referred
 *			to by in the datasheet (e.g. IND), or the nearest
 *			possible compound name (e.g. IND-INC).
 * @modified:		Does a modifier apply to this channel. What these are
 *			depends on the channel type.  Modifier is set in
 *			channel2. Examples are IIO_MOD_X for axial sensors about
 *			the 'x' axis.
 * @indexed:		Specify the channel has a numerical index. If not,
 *			the channel index number will be suppressed for sysfs
 *			attributes but not for event codes.
 * @output:		Channel is output.
 * @differential:	Channel is differential.
 */
struct iio_chan_spec {
	enum iio_chan_type	type;
	int			channel;
	int			channel2;
	unsigned long		address;
	int			scan_index;
	struct {
		char	sign;
		u8	realbits;
		u8	storagebits;
		u8	shift;
		u8	repeat;
		enum iio_endian endianness;
	} scan_type;
	long			info_mask_separate;
	long			info_mask_shared_by_type;
	long			info_mask_shared_by_dir;
	long			info_mask_shared_by_all;
	const struct iio_event_spec *event_spec;
	unsigned int		num_event_specs;
	const struct iio_chan_spec_ext_info *ext_info;
	const char		*extend_name;
	const char		*datasheet_name;
	unsigned		modified:1;
	unsigned		indexed:1;
	unsigned		output:1;
	unsigned		differential:1;
};


/**
 * iio_channel_has_info() - Checks whether a channel supports a info attribute
 * @chan: The channel to be queried
 * @type: Type of the info attribute to be checked
 *
 * Returns true if the channels supports reporting values for the given info
 * attribute type, false otherwise.
 */
static inline bool iio_channel_has_info(const struct iio_chan_spec *chan,
	enum iio_chan_info_enum type)
{
	return (chan->info_mask_separate & BIT(type)) |
		(chan->info_mask_shared_by_type & BIT(type)) |
		(chan->info_mask_shared_by_dir & BIT(type)) |
		(chan->info_mask_shared_by_all & BIT(type));
}

#define IIO_CHAN_SOFT_TIMESTAMP(_si) {					\
	.type = IIO_TIMESTAMP,						\
	.channel = -1,							\
	.scan_index = _si,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 64,					\
		.storagebits = 64,					\
		},							\
}

/**
 * iio_get_time_ns() - utility function to get a time stamp for events etc
 **/
static inline s64 iio_get_time_ns(void)
{
	return ktime_get_real_ns();
}

/* Device operating modes */
#define INDIO_DIRECT_MODE		0x01
#define INDIO_BUFFER_TRIGGERED		0x02
#define INDIO_BUFFER_HARDWARE		0x08

#define INDIO_ALL_BUFFER_MODES					\
	(INDIO_BUFFER_TRIGGERED | INDIO_BUFFER_HARDWARE)

#define INDIO_MAX_RAW_ELEMENTS		4

struct iio_trigger; /* forward declaration */
struct iio_dev;

/**
 * struct iio_info - constant information about device
 * @driver_module:	module structure used to ensure correct
 *			ownership of chrdevs etc
 * @event_attrs:	event control attributes
 * @attrs:		general purpose device attributes
 * @read_raw:		function to request a value from the device.
 *			mask specifies which value. Note 0 means a reading of
 *			the channel in question.  Return value will specify the
 *			type of value returned by the device. val and val2 will
 *			contain the elements making up the returned value.
 * @read_raw_multi:	function to return values from the device.
 *			mask specifies which value. Note 0 means a reading of
 *			the channel in question.  Return value will specify the
 *			type of value returned by the device. vals pointer
 *			contain the elements making up the returned value.
 *			max_len specifies maximum number of elements
 *			vals pointer can contain. val_len is used to return
 *			length of valid elements in vals.
 * @write_raw:		function to write a value to the device.
 *			Parameters are the same as for read_raw.
 * @write_raw_get_fmt:	callback function to query the expected
 *			format/precision. If not set by the driver, write_raw
 *			returns IIO_VAL_INT_PLUS_MICRO.
 * @read_event_config:	find out if the event is enabled.
 * @write_event_config:	set if the event is enabled.
 * @read_event_value:	read a configuration value associated with the event.
 * @write_event_value:	write a configuration value for the event.
 * @validate_trigger:	function to validate the trigger when the
 *			current trigger gets changed.
 * @update_scan_mode:	function to configure device and scan buffer when
 *			channels have changed
 * @debugfs_reg_access:	function to read or write register value of device
 * @of_xlate:		function pointer to obtain channel specifier index.
 *			When #iio-cells is greater than '0', the driver could
 *			provide a custom of_xlate function that reads the
 *			*args* and returns the appropriate index in registered
 *			IIO channels array.
 **/
struct iio_info {
	struct module			*driver_module;
	struct attribute_group		*event_attrs;
	const struct attribute_group	*attrs;

	int (*read_raw)(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val,
			int *val2,
			long mask);

	int (*read_raw_multi)(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int max_len,
			int *vals,
			int *val_len,
			long mask);

	int (*write_raw)(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 int val,
			 int val2,
			 long mask);

	int (*write_raw_get_fmt)(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 long mask);

	int (*read_event_config)(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir);

	int (*write_event_config)(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  int state);

	int (*read_event_value)(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info, int *val, int *val2);

	int (*write_event_value)(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info, int val, int val2);

	int (*validate_trigger)(struct iio_dev *indio_dev,
				struct iio_trigger *trig);
	int (*update_scan_mode)(struct iio_dev *indio_dev,
				const unsigned long *scan_mask);
	int (*debugfs_reg_access)(struct iio_dev *indio_dev,
				  unsigned reg, unsigned writeval,
				  unsigned *readval);
	int (*of_xlate)(struct iio_dev *indio_dev,
			const struct of_phandle_args *iiospec);
};

/**
 * struct iio_buffer_setup_ops - buffer setup related callbacks
 * @preenable:		[DRIVER] function to run prior to marking buffer enabled
 * @postenable:		[DRIVER] function to run after marking buffer enabled
 * @predisable:		[DRIVER] function to run prior to marking buffer
 *			disabled
 * @postdisable:	[DRIVER] function to run after marking buffer disabled
 * @validate_scan_mask: [DRIVER] function callback to check whether a given
 *			scan mask is valid for the device.
 */
struct iio_buffer_setup_ops {
	int (*preenable)(struct iio_dev *);
	int (*postenable)(struct iio_dev *);
	int (*predisable)(struct iio_dev *);
	int (*postdisable)(struct iio_dev *);
	bool (*validate_scan_mask)(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask);
};

/**
 * struct iio_dev - industrial I/O device
 * @id:			[INTERN] used to identify device internally
 * @modes:		[DRIVER] operating modes supported by device
 * @currentmode:	[DRIVER] current operating mode
 * @dev:		[DRIVER] device structure, should be assigned a parent
 *			and owner
 * @event_interface:	[INTERN] event chrdevs associated with interrupt lines
 * @buffer:		[DRIVER] any buffer present
 * @buffer_list:	[INTERN] list of all buffers currently attached
 * @scan_bytes:		[INTERN] num bytes captured to be fed to buffer demux
 * @mlock:		[INTERN] lock used to prevent simultaneous device state
 *			changes
 * @available_scan_masks: [DRIVER] optional array of allowed bitmasks
 * @masklength:		[INTERN] the length of the mask established from
 *			channels
 * @active_scan_mask:	[INTERN] union of all scan masks requested by buffers
 * @scan_timestamp:	[INTERN] set if any buffers have requested timestamp
 * @scan_index_timestamp:[INTERN] cache of the index to the timestamp
 * @trig:		[INTERN] current device trigger (buffer modes)
 * @pollfunc:		[DRIVER] function run on trigger being received
 * @channels:		[DRIVER] channel specification structure table
 * @num_channels:	[DRIVER] number of channels specified in @channels.
 * @channel_attr_list:	[INTERN] keep track of automatically created channel
 *			attributes
 * @chan_attr_group:	[INTERN] group for all attrs in base directory
 * @name:		[DRIVER] name of the device.
 * @info:		[DRIVER] callbacks and constant info from driver
 * @info_exist_lock:	[INTERN] lock to prevent use during removal
 * @setup_ops:		[DRIVER] callbacks to call before and after buffer
 *			enable/disable
 * @chrdev:		[INTERN] associated character device
 * @groups:		[INTERN] attribute groups
 * @groupcounter:	[INTERN] index of next attribute group
 * @flags:		[INTERN] file ops related flags including busy flag.
 * @debugfs_dentry:	[INTERN] device specific debugfs dentry.
 * @cached_reg_addr:	[INTERN] cached register address for debugfs reads.
 */
struct iio_dev {
	int				id;

	int				modes;
	int				currentmode;
	struct device			dev;

	struct iio_event_interface	*event_interface;

	struct iio_buffer		*buffer;
	struct list_head		buffer_list;
	int				scan_bytes;
	struct mutex			mlock;

	const unsigned long		*available_scan_masks;
	unsigned			masklength;
	const unsigned long		*active_scan_mask;
	bool				scan_timestamp;
	unsigned			scan_index_timestamp;
	struct iio_trigger		*trig;
	struct iio_poll_func		*pollfunc;

	struct iio_chan_spec const	*channels;
	int				num_channels;

	struct list_head		channel_attr_list;
	struct attribute_group		chan_attr_group;
	const char			*name;
	const struct iio_info		*info;
	struct mutex			info_exist_lock;
	const struct iio_buffer_setup_ops	*setup_ops;
	struct cdev			chrdev;
#define IIO_MAX_GROUPS 6
	const struct attribute_group	*groups[IIO_MAX_GROUPS + 1];
	int				groupcounter;

	unsigned long			flags;
#if defined(CONFIG_DEBUG_FS)
	struct dentry			*debugfs_dentry;
	unsigned			cached_reg_addr;
#endif
};

const struct iio_chan_spec
*iio_find_channel_from_si(struct iio_dev *indio_dev, int si);
int iio_device_register(struct iio_dev *indio_dev);
void iio_device_unregister(struct iio_dev *indio_dev);
int devm_iio_device_register(struct device *dev, struct iio_dev *indio_dev);
void devm_iio_device_unregister(struct device *dev, struct iio_dev *indio_dev);
int iio_push_event(struct iio_dev *indio_dev, u64 ev_code, s64 timestamp);

extern struct bus_type iio_bus_type;

/**
 * iio_device_put() - reference counted deallocation of struct device
 * @indio_dev: 		IIO device structure containing the device
 **/
static inline void iio_device_put(struct iio_dev *indio_dev)
{
	if (indio_dev)
		put_device(&indio_dev->dev);
}

/**
 * dev_to_iio_dev() - Get IIO device struct from a device struct
 * @dev: 		The device embedded in the IIO device
 *
 * Note: The device must be a IIO device, otherwise the result is undefined.
 */
static inline struct iio_dev *dev_to_iio_dev(struct device *dev)
{
	return container_of(dev, struct iio_dev, dev);
}

/**
 * iio_device_get() - increment reference count for the device
 * @indio_dev: 		IIO device structure
 *
 * Returns: The passed IIO device
 **/
static inline struct iio_dev *iio_device_get(struct iio_dev *indio_dev)
{
	return indio_dev ? dev_to_iio_dev(get_device(&indio_dev->dev)) : NULL;
}


/**
 * iio_device_set_drvdata() - Set device driver data
 * @indio_dev: IIO device structure
 * @data: Driver specific data
 *
 * Allows to attach an arbitrary pointer to an IIO device, which can later be
 * retrieved by iio_device_get_drvdata().
 */
static inline void iio_device_set_drvdata(struct iio_dev *indio_dev, void *data)
{
	dev_set_drvdata(&indio_dev->dev, data);
}

/**
 * iio_device_get_drvdata() - Get device driver data
 * @indio_dev: IIO device structure
 *
 * Returns the data previously set with iio_device_set_drvdata()
 */
static inline void *iio_device_get_drvdata(struct iio_dev *indio_dev)
{
	return dev_get_drvdata(&indio_dev->dev);
}

/* Can we make this smaller? */
#define IIO_ALIGN L1_CACHE_BYTES
struct iio_dev *iio_device_alloc(int sizeof_priv);

static inline void *iio_priv(const struct iio_dev *indio_dev)
{
	return (char *)indio_dev + ALIGN(sizeof(struct iio_dev), IIO_ALIGN);
}

static inline struct iio_dev *iio_priv_to_dev(void *priv)
{
	return (struct iio_dev *)((char *)priv -
				  ALIGN(sizeof(struct iio_dev), IIO_ALIGN));
}

void iio_device_free(struct iio_dev *indio_dev);
struct iio_dev *devm_iio_device_alloc(struct device *dev, int sizeof_priv);
void devm_iio_device_free(struct device *dev, struct iio_dev *indio_dev);
struct iio_trigger *devm_iio_trigger_alloc(struct device *dev,
						const char *fmt, ...);
void devm_iio_trigger_free(struct device *dev, struct iio_trigger *iio_trig);

/**
 * iio_buffer_enabled() - helper function to test if the buffer is enabled
 * @indio_dev:		IIO device structure for device
 **/
static inline bool iio_buffer_enabled(struct iio_dev *indio_dev)
{
	return indio_dev->currentmode
		& (INDIO_BUFFER_TRIGGERED | INDIO_BUFFER_HARDWARE);
}

/**
 * iio_get_debugfs_dentry() - helper function to get the debugfs_dentry
 * @indio_dev:		IIO device structure for device
 **/
#if defined(CONFIG_DEBUG_FS)
static inline struct dentry *iio_get_debugfs_dentry(struct iio_dev *indio_dev)
{
	return indio_dev->debugfs_dentry;
}
#else
static inline struct dentry *iio_get_debugfs_dentry(struct iio_dev *indio_dev)
{
	return NULL;
}
#endif

int iio_str_to_fixpoint(const char *str, int fract_mult, int *integer,
	int *fract);

/**
 * IIO_DEGREE_TO_RAD() - Convert degree to rad
 * @deg: A value in degree
 *
 * Returns the given value converted from degree to rad
 */
#define IIO_DEGREE_TO_RAD(deg) (((deg) * 314159ULL + 9000000ULL) / 18000000ULL)

/**
 * IIO_G_TO_M_S_2() - Convert g to meter / second**2
 * @g: A value in g
 *
 * Returns the given value converted from g to meter / second**2
 */
#define IIO_G_TO_M_S_2(g) ((g) * 980665ULL / 100000ULL)

#endif /* _INDUSTRIAL_IO_H_ */
