/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Counter interface
 * Copyright (C) 2018 William Breathitt Gray
 */
#ifndef _COUNTER_H_
#define _COUNTER_H_

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <uapi/linux/counter.h>

struct counter_device;
struct counter_count;
struct counter_synapse;
struct counter_signal;

enum counter_comp_type {
	COUNTER_COMP_U8,
	COUNTER_COMP_U64,
	COUNTER_COMP_BOOL,
	COUNTER_COMP_SIGNAL_LEVEL,
	COUNTER_COMP_FUNCTION,
	COUNTER_COMP_SYNAPSE_ACTION,
	COUNTER_COMP_ENUM,
	COUNTER_COMP_COUNT_DIRECTION,
	COUNTER_COMP_COUNT_MODE,
	COUNTER_COMP_SIGNAL_POLARITY,
	COUNTER_COMP_ARRAY,
};

/**
 * struct counter_comp - Counter component node
 * @type:		Counter component data type
 * @name:		device-specific component name
 * @priv:		component-relevant data
 * @action_read:	Synapse action mode read callback. The read value of the
 *			respective Synapse action mode should be passed back via
 *			the action parameter.
 * @device_u8_read:	Device u8 component read callback. The read value of the
 *			respective Device u8 component should be passed back via
 *			the val parameter.
 * @count_u8_read:	Count u8 component read callback. The read value of the
 *			respective Count u8 component should be passed back via
 *			the val parameter.
 * @signal_u8_read:	Signal u8 component read callback. The read value of the
 *			respective Signal u8 component should be passed back via
 *			the val parameter.
 * @device_u32_read:	Device u32 component read callback. The read value of
 *			the respective Device u32 component should be passed
 *			back via the val parameter.
 * @count_u32_read:	Count u32 component read callback. The read value of the
 *			respective Count u32 component should be passed back via
 *			the val parameter.
 * @signal_u32_read:	Signal u32 component read callback. The read value of
 *			the respective Signal u32 component should be passed
 *			back via the val parameter.
 * @device_u64_read:	Device u64 component read callback. The read value of
 *			the respective Device u64 component should be passed
 *			back via the val parameter.
 * @count_u64_read:	Count u64 component read callback. The read value of the
 *			respective Count u64 component should be passed back via
 *			the val parameter.
 * @signal_u64_read:	Signal u64 component read callback. The read value of
 *			the respective Signal u64 component should be passed
 *			back via the val parameter.
 * @signal_array_u32_read:	Signal u32 array component read callback. The
 *				index of the respective Count u32 array
 *				component element is passed via the idx
 *				parameter. The read value of the respective
 *				Count u32 array component element should be
 *				passed back via the val parameter.
 * @device_array_u64_read:	Device u64 array component read callback. The
 *				index of the respective Device u64 array
 *				component element is passed via the idx
 *				parameter. The read value of the respective
 *				Device u64 array component element should be
 *				passed back via the val parameter.
 * @count_array_u64_read:	Count u64 array component read callback. The
 *				index of the respective Count u64 array
 *				component element is passed via the idx
 *				parameter. The read value of the respective
 *				Count u64 array component element should be
 *				passed back via the val parameter.
 * @signal_array_u64_read:	Signal u64 array component read callback. The
 *				index of the respective Count u64 array
 *				component element is passed via the idx
 *				parameter. The read value of the respective
 *				Count u64 array component element should be
 *				passed back via the val parameter.
 * @action_write:	Synapse action mode write callback. The write value of
 *			the respective Synapse action mode is passed via the
 *			action parameter.
 * @device_u8_write:	Device u8 component write callback. The write value of
 *			the respective Device u8 component is passed via the val
 *			parameter.
 * @count_u8_write:	Count u8 component write callback. The write value of
 *			the respective Count u8 component is passed via the val
 *			parameter.
 * @signal_u8_write:	Signal u8 component write callback. The write value of
 *			the respective Signal u8 component is passed via the val
 *			parameter.
 * @device_u32_write:	Device u32 component write callback. The write value of
 *			the respective Device u32 component is passed via the
 *			val parameter.
 * @count_u32_write:	Count u32 component write callback. The write value of
 *			the respective Count u32 component is passed via the val
 *			parameter.
 * @signal_u32_write:	Signal u32 component write callback. The write value of
 *			the respective Signal u32 component is passed via the
 *			val parameter.
 * @device_u64_write:	Device u64 component write callback. The write value of
 *			the respective Device u64 component is passed via the
 *			val parameter.
 * @count_u64_write:	Count u64 component write callback. The write value of
 *			the respective Count u64 component is passed via the val
 *			parameter.
 * @signal_u64_write:	Signal u64 component write callback. The write value of
 *			the respective Signal u64 component is passed via the
 *			val parameter.
 * @signal_array_u32_write:	Signal u32 array component write callback. The
 *				index of the respective Signal u32 array
 *				component element is passed via the idx
 *				parameter. The write value of the respective
 *				Signal u32 array component element is passed via
 *				the val parameter.
 * @device_array_u64_write:	Device u64 array component write callback. The
 *				index of the respective Device u64 array
 *				component element is passed via the idx
 *				parameter. The write value of the respective
 *				Device u64 array component element is passed via
 *				the val parameter.
 * @count_array_u64_write:	Count u64 array component write callback. The
 *				index of the respective Count u64 array
 *				component element is passed via the idx
 *				parameter. The write value of the respective
 *				Count u64 array component element is passed via
 *				the val parameter.
 * @signal_array_u64_write:	Signal u64 array component write callback. The
 *				index of the respective Signal u64 array
 *				component element is passed via the idx
 *				parameter. The write value of the respective
 *				Signal u64 array component element is passed via
 *				the val parameter.
 */
struct counter_comp {
	enum counter_comp_type type;
	const char *name;
	void *priv;
	union {
		int (*action_read)(struct counter_device *counter,
				   struct counter_count *count,
				   struct counter_synapse *synapse,
				   enum counter_synapse_action *action);
		int (*device_u8_read)(struct counter_device *counter, u8 *val);
		int (*count_u8_read)(struct counter_device *counter,
				     struct counter_count *count, u8 *val);
		int (*signal_u8_read)(struct counter_device *counter,
				      struct counter_signal *signal, u8 *val);
		int (*device_u32_read)(struct counter_device *counter,
				       u32 *val);
		int (*count_u32_read)(struct counter_device *counter,
				      struct counter_count *count, u32 *val);
		int (*signal_u32_read)(struct counter_device *counter,
				       struct counter_signal *signal, u32 *val);
		int (*device_u64_read)(struct counter_device *counter,
				       u64 *val);
		int (*count_u64_read)(struct counter_device *counter,
				      struct counter_count *count, u64 *val);
		int (*signal_u64_read)(struct counter_device *counter,
				       struct counter_signal *signal, u64 *val);
		int (*signal_array_u32_read)(struct counter_device *counter,
					     struct counter_signal *signal,
					     size_t idx, u32 *val);
		int (*device_array_u64_read)(struct counter_device *counter,
					     size_t idx, u64 *val);
		int (*count_array_u64_read)(struct counter_device *counter,
					    struct counter_count *count,
					    size_t idx, u64 *val);
		int (*signal_array_u64_read)(struct counter_device *counter,
					     struct counter_signal *signal,
					     size_t idx, u64 *val);
	};
	union {
		int (*action_write)(struct counter_device *counter,
				    struct counter_count *count,
				    struct counter_synapse *synapse,
				    enum counter_synapse_action action);
		int (*device_u8_write)(struct counter_device *counter, u8 val);
		int (*count_u8_write)(struct counter_device *counter,
				      struct counter_count *count, u8 val);
		int (*signal_u8_write)(struct counter_device *counter,
				       struct counter_signal *signal, u8 val);
		int (*device_u32_write)(struct counter_device *counter,
					u32 val);
		int (*count_u32_write)(struct counter_device *counter,
				       struct counter_count *count, u32 val);
		int (*signal_u32_write)(struct counter_device *counter,
					struct counter_signal *signal, u32 val);
		int (*device_u64_write)(struct counter_device *counter,
					u64 val);
		int (*count_u64_write)(struct counter_device *counter,
				       struct counter_count *count, u64 val);
		int (*signal_u64_write)(struct counter_device *counter,
					struct counter_signal *signal, u64 val);
		int (*signal_array_u32_write)(struct counter_device *counter,
					      struct counter_signal *signal,
					      size_t idx, u32 val);
		int (*device_array_u64_write)(struct counter_device *counter,
					      size_t idx, u64 val);
		int (*count_array_u64_write)(struct counter_device *counter,
					     struct counter_count *count,
					     size_t idx, u64 val);
		int (*signal_array_u64_write)(struct counter_device *counter,
					      struct counter_signal *signal,
					      size_t idx, u64 val);
	};
};

/**
 * struct counter_signal - Counter Signal node
 * @id:		unique ID used to identify the Signal
 * @name:	device-specific Signal name
 * @ext:	optional array of Signal extensions
 * @num_ext:	number of Signal extensions specified in @ext
 */
struct counter_signal {
	int id;
	const char *name;

	struct counter_comp *ext;
	size_t num_ext;
};

/**
 * struct counter_synapse - Counter Synapse node
 * @actions_list:	array of available action modes
 * @num_actions:	number of action modes specified in @actions_list
 * @signal:		pointer to the associated Signal
 */
struct counter_synapse {
	const enum counter_synapse_action *actions_list;
	size_t num_actions;

	struct counter_signal *signal;
};

/**
 * struct counter_count - Counter Count node
 * @id:			unique ID used to identify the Count
 * @name:		device-specific Count name
 * @functions_list:	array of available function modes
 * @num_functions:	number of function modes specified in @functions_list
 * @synapses:		array of Synapses for initialization
 * @num_synapses:	number of Synapses specified in @synapses
 * @ext:		optional array of Count extensions
 * @num_ext:		number of Count extensions specified in @ext
 */
struct counter_count {
	int id;
	const char *name;

	const enum counter_function *functions_list;
	size_t num_functions;

	struct counter_synapse *synapses;
	size_t num_synapses;

	struct counter_comp *ext;
	size_t num_ext;
};

/**
 * struct counter_event_node - Counter Event node
 * @l:		list of current watching Counter events
 * @event:	event that triggers
 * @channel:	event channel
 * @comp_list:	list of components to watch when event triggers
 */
struct counter_event_node {
	struct list_head l;
	u8 event;
	u8 channel;
	struct list_head comp_list;
};

/**
 * struct counter_ops - Callbacks from driver
 * @signal_read:	optional read callback for Signals. The read level of
 *			the respective Signal should be passed back via the
 *			level parameter.
 * @count_read:		read callback for Counts. The read value of the
 *			respective Count should be passed back via the value
 *			parameter.
 * @count_write:	optional write callback for Counts. The write value for
 *			the respective Count is passed in via the value
 *			parameter.
 * @function_read:	read callback the Count function modes. The read
 *			function mode of the respective Count should be passed
 *			back via the function parameter.
 * @function_write:	optional write callback for Count function modes. The
 *			function mode to write for the respective Count is
 *			passed in via the function parameter.
 * @action_read:	optional read callback the Synapse action modes. The
 *			read action mode of the respective Synapse should be
 *			passed back via the action parameter.
 * @action_write:	optional write callback for Synapse action modes. The
 *			action mode to write for the respective Synapse is
 *			passed in via the action parameter.
 * @events_configure:	optional write callback to configure events. The list of
 *			struct counter_event_node may be accessed via the
 *			events_list member of the counter parameter.
 * @watch_validate:	optional callback to validate a watch. The Counter
 *			component watch configuration is passed in via the watch
 *			parameter. A return value of 0 indicates a valid Counter
 *			component watch configuration.
 */
struct counter_ops {
	int (*signal_read)(struct counter_device *counter,
			   struct counter_signal *signal,
			   enum counter_signal_level *level);
	int (*count_read)(struct counter_device *counter,
			  struct counter_count *count, u64 *value);
	int (*count_write)(struct counter_device *counter,
			   struct counter_count *count, u64 value);
	int (*function_read)(struct counter_device *counter,
			     struct counter_count *count,
			     enum counter_function *function);
	int (*function_write)(struct counter_device *counter,
			      struct counter_count *count,
			      enum counter_function function);
	int (*action_read)(struct counter_device *counter,
			   struct counter_count *count,
			   struct counter_synapse *synapse,
			   enum counter_synapse_action *action);
	int (*action_write)(struct counter_device *counter,
			    struct counter_count *count,
			    struct counter_synapse *synapse,
			    enum counter_synapse_action action);
	int (*events_configure)(struct counter_device *counter);
	int (*watch_validate)(struct counter_device *counter,
			      const struct counter_watch *watch);
};

/**
 * struct counter_device - Counter data structure
 * @name:		name of the device
 * @parent:		optional parent device providing the counters
 * @ops:		callbacks from driver
 * @signals:		array of Signals
 * @num_signals:	number of Signals specified in @signals
 * @counts:		array of Counts
 * @num_counts:		number of Counts specified in @counts
 * @ext:		optional array of Counter device extensions
 * @num_ext:		number of Counter device extensions specified in @ext
 * @priv:		optional private data supplied by driver
 * @dev:		internal device structure
 * @chrdev:		internal character device structure
 * @events_list:	list of current watching Counter events
 * @events_list_lock:	lock to protect Counter events list operations
 * @next_events_list:	list of next watching Counter events
 * @n_events_list_lock:	lock to protect Counter next events list operations
 * @events:		queue of detected Counter events
 * @events_wait:	wait queue to allow blocking reads of Counter events
 * @events_in_lock:	lock to protect Counter events queue in operations
 * @events_out_lock:	lock to protect Counter events queue out operations
 * @ops_exist_lock:	lock to prevent use during removal
 */
struct counter_device {
	const char *name;
	struct device *parent;

	const struct counter_ops *ops;

	struct counter_signal *signals;
	size_t num_signals;
	struct counter_count *counts;
	size_t num_counts;

	struct counter_comp *ext;
	size_t num_ext;

	struct device dev;
	struct cdev chrdev;
	struct list_head events_list;
	spinlock_t events_list_lock;
	struct list_head next_events_list;
	struct mutex n_events_list_lock;
	DECLARE_KFIFO_PTR(events, struct counter_event);
	wait_queue_head_t events_wait;
	spinlock_t events_in_lock;
	struct mutex events_out_lock;
	struct mutex ops_exist_lock;
};

void *counter_priv(const struct counter_device *const counter) __attribute_const__;

struct counter_device *counter_alloc(size_t sizeof_priv);
void counter_put(struct counter_device *const counter);
int counter_add(struct counter_device *const counter);

void counter_unregister(struct counter_device *const counter);
struct counter_device *devm_counter_alloc(struct device *dev,
					  size_t sizeof_priv);
int devm_counter_add(struct device *dev,
		     struct counter_device *const counter);
void counter_push_event(struct counter_device *const counter, const u8 event,
			const u8 channel);

#define COUNTER_COMP_DEVICE_U8(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_U8, \
	.name = (_name), \
	.device_u8_read = (_read), \
	.device_u8_write = (_write), \
}
#define COUNTER_COMP_COUNT_U8(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_U8, \
	.name = (_name), \
	.count_u8_read = (_read), \
	.count_u8_write = (_write), \
}
#define COUNTER_COMP_SIGNAL_U8(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_U8, \
	.name = (_name), \
	.signal_u8_read = (_read), \
	.signal_u8_write = (_write), \
}

#define COUNTER_COMP_DEVICE_U64(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_U64, \
	.name = (_name), \
	.device_u64_read = (_read), \
	.device_u64_write = (_write), \
}
#define COUNTER_COMP_COUNT_U64(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_U64, \
	.name = (_name), \
	.count_u64_read = (_read), \
	.count_u64_write = (_write), \
}
#define COUNTER_COMP_SIGNAL_U64(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_U64, \
	.name = (_name), \
	.signal_u64_read = (_read), \
	.signal_u64_write = (_write), \
}

#define COUNTER_COMP_DEVICE_BOOL(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_BOOL, \
	.name = (_name), \
	.device_u8_read = (_read), \
	.device_u8_write = (_write), \
}
#define COUNTER_COMP_COUNT_BOOL(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_BOOL, \
	.name = (_name), \
	.count_u8_read = (_read), \
	.count_u8_write = (_write), \
}
#define COUNTER_COMP_SIGNAL_BOOL(_name, _read, _write) \
{ \
	.type = COUNTER_COMP_BOOL, \
	.name = (_name), \
	.signal_u8_read = (_read), \
	.signal_u8_write = (_write), \
}

struct counter_available {
	union {
		const u32 *enums;
		const char *const *strs;
	};
	size_t num_items;
};

#define DEFINE_COUNTER_AVAILABLE(_name, _enums) \
	struct counter_available _name = { \
		.enums = (_enums), \
		.num_items = ARRAY_SIZE(_enums), \
	}

#define DEFINE_COUNTER_ENUM(_name, _strs) \
	struct counter_available _name = { \
		.strs = (_strs), \
		.num_items = ARRAY_SIZE(_strs), \
	}

#define COUNTER_COMP_DEVICE_ENUM(_name, _get, _set, _available) \
{ \
	.type = COUNTER_COMP_ENUM, \
	.name = (_name), \
	.device_u32_read = (_get), \
	.device_u32_write = (_set), \
	.priv = &(_available), \
}
#define COUNTER_COMP_COUNT_ENUM(_name, _get, _set, _available) \
{ \
	.type = COUNTER_COMP_ENUM, \
	.name = (_name), \
	.count_u32_read = (_get), \
	.count_u32_write = (_set), \
	.priv = &(_available), \
}
#define COUNTER_COMP_SIGNAL_ENUM(_name, _get, _set, _available) \
{ \
	.type = COUNTER_COMP_ENUM, \
	.name = (_name), \
	.signal_u32_read = (_get), \
	.signal_u32_write = (_set), \
	.priv = &(_available), \
}

struct counter_array {
	enum counter_comp_type type;
	const struct counter_available *avail;
	union {
		size_t length;
		size_t idx;
	};
};

#define DEFINE_COUNTER_ARRAY_U64(_name, _length) \
	struct counter_array _name = { \
		.type = COUNTER_COMP_U64, \
		.length = (_length), \
	}

#define DEFINE_COUNTER_ARRAY_CAPTURE(_name, _length) \
	DEFINE_COUNTER_ARRAY_U64(_name, _length)

#define DEFINE_COUNTER_ARRAY_POLARITY(_name, _available, _length) \
	struct counter_array _name = { \
		.type = COUNTER_COMP_SIGNAL_POLARITY, \
		.avail = &(_available), \
		.length = (_length), \
	}

#define COUNTER_COMP_DEVICE_ARRAY_U64(_name, _read, _write, _array) \
{ \
	.type = COUNTER_COMP_ARRAY, \
	.name = (_name), \
	.device_array_u64_read = (_read), \
	.device_array_u64_write = (_write), \
	.priv = &(_array), \
}
#define COUNTER_COMP_COUNT_ARRAY_U64(_name, _read, _write, _array) \
{ \
	.type = COUNTER_COMP_ARRAY, \
	.name = (_name), \
	.count_array_u64_read = (_read), \
	.count_array_u64_write = (_write), \
	.priv = &(_array), \
}
#define COUNTER_COMP_SIGNAL_ARRAY_U64(_name, _read, _write, _array) \
{ \
	.type = COUNTER_COMP_ARRAY, \
	.name = (_name), \
	.signal_array_u64_read = (_read), \
	.signal_array_u64_write = (_write), \
	.priv = &(_array), \
}

#define COUNTER_COMP_CAPTURE(_read, _write) \
	COUNTER_COMP_COUNT_U64("capture", _read, _write)

#define COUNTER_COMP_CEILING(_read, _write) \
	COUNTER_COMP_COUNT_U64("ceiling", _read, _write)

#define COUNTER_COMP_COUNT_MODE(_read, _write, _available) \
{ \
	.type = COUNTER_COMP_COUNT_MODE, \
	.name = "count_mode", \
	.count_u32_read = (_read), \
	.count_u32_write = (_write), \
	.priv = &(_available), \
}

#define COUNTER_COMP_DIRECTION(_read) \
{ \
	.type = COUNTER_COMP_COUNT_DIRECTION, \
	.name = "direction", \
	.count_u32_read = (_read), \
}

#define COUNTER_COMP_ENABLE(_read, _write) \
	COUNTER_COMP_COUNT_BOOL("enable", _read, _write)

#define COUNTER_COMP_FLOOR(_read, _write) \
	COUNTER_COMP_COUNT_U64("floor", _read, _write)

#define COUNTER_COMP_POLARITY(_read, _write, _available) \
{ \
	.type = COUNTER_COMP_SIGNAL_POLARITY, \
	.name = "polarity", \
	.signal_u32_read = (_read), \
	.signal_u32_write = (_write), \
	.priv = &(_available), \
}

#define COUNTER_COMP_PRESET(_read, _write) \
	COUNTER_COMP_COUNT_U64("preset", _read, _write)

#define COUNTER_COMP_PRESET_ENABLE(_read, _write) \
	COUNTER_COMP_COUNT_BOOL("preset_enable", _read, _write)

#define COUNTER_COMP_ARRAY_CAPTURE(_read, _write, _array) \
	COUNTER_COMP_COUNT_ARRAY_U64("capture", _read, _write, _array)

#define COUNTER_COMP_ARRAY_POLARITY(_read, _write, _array) \
{ \
	.type = COUNTER_COMP_ARRAY, \
	.name = "polarity", \
	.signal_array_u32_read = (_read), \
	.signal_array_u32_write = (_write), \
	.priv = &(_array), \
}

#endif /* _COUNTER_H_ */
