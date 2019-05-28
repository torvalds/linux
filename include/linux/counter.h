/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Counter interface
 * Copyright (C) 2018 William Breathitt Gray
 */
#ifndef _COUNTER_H_
#define _COUNTER_H_

#include <linux/counter_enum.h>
#include <linux/device.h>
#include <linux/types.h>

enum counter_count_direction {
	COUNTER_COUNT_DIRECTION_FORWARD = 0,
	COUNTER_COUNT_DIRECTION_BACKWARD
};
extern const char *const counter_count_direction_str[2];

enum counter_count_mode {
	COUNTER_COUNT_MODE_NORMAL = 0,
	COUNTER_COUNT_MODE_RANGE_LIMIT,
	COUNTER_COUNT_MODE_NON_RECYCLE,
	COUNTER_COUNT_MODE_MODULO_N
};
extern const char *const counter_count_mode_str[4];

struct counter_device;
struct counter_signal;

/**
 * struct counter_signal_ext - Counter Signal extensions
 * @name:	attribute name
 * @read:	read callback for this attribute; may be NULL
 * @write:	write callback for this attribute; may be NULL
 * @priv:	data private to the driver
 */
struct counter_signal_ext {
	const char *name;
	ssize_t (*read)(struct counter_device *counter,
			struct counter_signal *signal, void *priv, char *buf);
	ssize_t (*write)(struct counter_device *counter,
			 struct counter_signal *signal, void *priv,
			 const char *buf, size_t len);
	void *priv;
};

/**
 * struct counter_signal - Counter Signal node
 * @id:		unique ID used to identify signal
 * @name:	device-specific Signal name; ideally, this should match the name
 *		as it appears in the datasheet documentation
 * @ext:	optional array of Counter Signal extensions
 * @num_ext:	number of Counter Signal extensions specified in @ext
 * @priv:	optional private data supplied by driver
 */
struct counter_signal {
	int id;
	const char *name;

	const struct counter_signal_ext *ext;
	size_t num_ext;

	void *priv;
};

/**
 * struct counter_signal_enum_ext - Signal enum extension attribute
 * @items:	Array of strings
 * @num_items:	Number of items specified in @items
 * @set:	Set callback function; may be NULL
 * @get:	Get callback function; may be NULL
 *
 * The counter_signal_enum_ext structure can be used to implement enum style
 * Signal extension attributes. Enum style attributes are those which have a set
 * of strings that map to unsigned integer values. The Generic Counter Signal
 * enum extension helper code takes care of mapping between value and string, as
 * well as generating a "_available" file which contains a list of all available
 * items. The get callback is used to query the currently active item; the index
 * of the item within the respective items array is returned via the 'item'
 * parameter. The set callback is called when the attribute is updated; the
 * 'item' parameter contains the index of the newly activated item within the
 * respective items array.
 */
struct counter_signal_enum_ext {
	const char * const *items;
	size_t num_items;
	int (*get)(struct counter_device *counter,
		   struct counter_signal *signal, size_t *item);
	int (*set)(struct counter_device *counter,
		   struct counter_signal *signal, size_t item);
};

/**
 * COUNTER_SIGNAL_ENUM() - Initialize Signal enum extension
 * @_name:	Attribute name
 * @_e:		Pointer to a counter_signal_enum_ext structure
 *
 * This should usually be used together with COUNTER_SIGNAL_ENUM_AVAILABLE()
 */
#define COUNTER_SIGNAL_ENUM(_name, _e) \
{ \
	.name = (_name), \
	.read = counter_signal_enum_read, \
	.write = counter_signal_enum_write, \
	.priv = (_e) \
}

/**
 * COUNTER_SIGNAL_ENUM_AVAILABLE() - Initialize Signal enum available extension
 * @_name:	Attribute name ("_available" will be appended to the name)
 * @_e:		Pointer to a counter_signal_enum_ext structure
 *
 * Creates a read only attribute that lists all the available enum items in a
 * newline separated list. This should usually be used together with
 * COUNTER_SIGNAL_ENUM()
 */
#define COUNTER_SIGNAL_ENUM_AVAILABLE(_name, _e) \
{ \
	.name = (_name "_available"), \
	.read = counter_signal_enum_available_read, \
	.priv = (_e) \
}

enum counter_synapse_action {
	COUNTER_SYNAPSE_ACTION_NONE = 0,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES
};

/**
 * struct counter_synapse - Counter Synapse node
 * @action:		index of current action mode
 * @actions_list:	array of available action modes
 * @num_actions:	number of action modes specified in @actions_list
 * @signal:		pointer to associated signal
 */
struct counter_synapse {
	size_t action;
	const enum counter_synapse_action *actions_list;
	size_t num_actions;

	struct counter_signal *signal;
};

struct counter_count;

/**
 * struct counter_count_ext - Counter Count extension
 * @name:	attribute name
 * @read:	read callback for this attribute; may be NULL
 * @write:	write callback for this attribute; may be NULL
 * @priv:	data private to the driver
 */
struct counter_count_ext {
	const char *name;
	ssize_t (*read)(struct counter_device *counter,
			struct counter_count *count, void *priv, char *buf);
	ssize_t (*write)(struct counter_device *counter,
			 struct counter_count *count, void *priv,
			 const char *buf, size_t len);
	void *priv;
};

enum counter_count_function {
	COUNTER_COUNT_FUNCTION_INCREASE = 0,
	COUNTER_COUNT_FUNCTION_DECREASE,
	COUNTER_COUNT_FUNCTION_PULSE_DIRECTION,
	COUNTER_COUNT_FUNCTION_QUADRATURE_X1_A,
	COUNTER_COUNT_FUNCTION_QUADRATURE_X1_B,
	COUNTER_COUNT_FUNCTION_QUADRATURE_X2_A,
	COUNTER_COUNT_FUNCTION_QUADRATURE_X2_B,
	COUNTER_COUNT_FUNCTION_QUADRATURE_X4
};

/**
 * struct counter_count - Counter Count node
 * @id:			unique ID used to identify Count
 * @name:		device-specific Count name; ideally, this should match
 *			the name as it appears in the datasheet documentation
 * @function:		index of current function mode
 * @functions_list:	array available function modes
 * @num_functions:	number of function modes specified in @functions_list
 * @synapses:		array of synapses for initialization
 * @num_synapses:	number of synapses specified in @synapses
 * @ext:		optional array of Counter Count extensions
 * @num_ext:		number of Counter Count extensions specified in @ext
 * @priv:		optional private data supplied by driver
 */
struct counter_count {
	int id;
	const char *name;

	size_t function;
	const enum counter_count_function *functions_list;
	size_t num_functions;

	struct counter_synapse *synapses;
	size_t num_synapses;

	const struct counter_count_ext *ext;
	size_t num_ext;

	void *priv;
};

/**
 * struct counter_count_enum_ext - Count enum extension attribute
 * @items:	Array of strings
 * @num_items:	Number of items specified in @items
 * @set:	Set callback function; may be NULL
 * @get:	Get callback function; may be NULL
 *
 * The counter_count_enum_ext structure can be used to implement enum style
 * Count extension attributes. Enum style attributes are those which have a set
 * of strings that map to unsigned integer values. The Generic Counter Count
 * enum extension helper code takes care of mapping between value and string, as
 * well as generating a "_available" file which contains a list of all available
 * items. The get callback is used to query the currently active item; the index
 * of the item within the respective items array is returned via the 'item'
 * parameter. The set callback is called when the attribute is updated; the
 * 'item' parameter contains the index of the newly activated item within the
 * respective items array.
 */
struct counter_count_enum_ext {
	const char * const *items;
	size_t num_items;
	int (*get)(struct counter_device *counter, struct counter_count *count,
		   size_t *item);
	int (*set)(struct counter_device *counter, struct counter_count *count,
		   size_t item);
};

/**
 * COUNTER_COUNT_ENUM() - Initialize Count enum extension
 * @_name:	Attribute name
 * @_e:		Pointer to a counter_count_enum_ext structure
 *
 * This should usually be used together with COUNTER_COUNT_ENUM_AVAILABLE()
 */
#define COUNTER_COUNT_ENUM(_name, _e) \
{ \
	.name = (_name), \
	.read = counter_count_enum_read, \
	.write = counter_count_enum_write, \
	.priv = (_e) \
}

/**
 * COUNTER_COUNT_ENUM_AVAILABLE() - Initialize Count enum available extension
 * @_name:	Attribute name ("_available" will be appended to the name)
 * @_e:		Pointer to a counter_count_enum_ext structure
 *
 * Creates a read only attribute that lists all the available enum items in a
 * newline separated list. This should usually be used together with
 * COUNTER_COUNT_ENUM()
 */
#define COUNTER_COUNT_ENUM_AVAILABLE(_name, _e) \
{ \
	.name = (_name "_available"), \
	.read = counter_count_enum_available_read, \
	.priv = (_e) \
}

/**
 * struct counter_device_attr_group - internal container for attribute group
 * @attr_group:	Counter sysfs attributes group
 * @attr_list:	list to keep track of created Counter sysfs attributes
 * @num_attr:	number of Counter sysfs attributes
 */
struct counter_device_attr_group {
	struct attribute_group attr_group;
	struct list_head attr_list;
	size_t num_attr;
};

/**
 * struct counter_device_state - internal state container for a Counter device
 * @id:			unique ID used to identify the Counter
 * @dev:		internal device structure
 * @groups_list:	attribute groups list (for Signals, Counts, and ext)
 * @num_groups:		number of attribute groups containers
 * @groups:		Counter sysfs attribute groups (to populate @dev.groups)
 */
struct counter_device_state {
	int id;
	struct device dev;
	struct counter_device_attr_group *groups_list;
	size_t num_groups;
	const struct attribute_group **groups;
};

/**
 * struct counter_signal_read_value - Opaque Signal read value
 * @buf:	string representation of Signal read value
 * @len:	length of string in @buf
 */
struct counter_signal_read_value {
	char *buf;
	size_t len;
};

/**
 * struct counter_count_read_value - Opaque Count read value
 * @buf:	string representation of Count read value
 * @len:	length of string in @buf
 */
struct counter_count_read_value {
	char *buf;
	size_t len;
};

/**
 * struct counter_count_write_value - Opaque Count write value
 * @buf:	string representation of Count write value
 */
struct counter_count_write_value {
	const char *buf;
};

/**
 * struct counter_ops - Callbacks from driver
 * @signal_read:	optional read callback for Signal attribute. The read
 *			value of the respective Signal should be passed back via
 *			the val parameter. val points to an opaque type which
 *			should be set only by calling the
 *			counter_signal_read_value_set function from within the
 *			signal_read callback.
 * @count_read:		optional read callback for Count attribute. The read
 *			value of the respective Count should be passed back via
 *			the val parameter. val points to an opaque type which
 *			should be set only by calling the
 *			counter_count_read_value_set function from within the
 *			count_read callback.
 * @count_write:	optional write callback for Count attribute. The write
 *			value for the respective Count is passed in via the val
 *			parameter. val points to an opaque type which should be
 *			accessed only by calling the
 *			counter_count_write_value_get function.
 * @function_get:	function to get the current count function mode. Returns
 *			0 on success and negative error code on error. The index
 *			of the respective Count's returned function mode should
 *			be passed back via the function parameter.
 * @function_set:	function to set the count function mode. function is the
 *			index of the requested function mode from the respective
 *			Count's functions_list array.
 * @action_get:		function to get the current action mode. Returns 0 on
 *			success and negative error code on error. The index of
 *			the respective Signal's returned action mode should be
 *			passed back via the action parameter.
 * @action_set:		function to set the action mode. action is the index of
 *			the requested action mode from the respective Synapse's
 *			actions_list array.
 */
struct counter_ops {
	int (*signal_read)(struct counter_device *counter,
			   struct counter_signal *signal,
			   struct counter_signal_read_value *val);
	int (*count_read)(struct counter_device *counter,
			  struct counter_count *count,
			  struct counter_count_read_value *val);
	int (*count_write)(struct counter_device *counter,
			   struct counter_count *count,
			   struct counter_count_write_value *val);
	int (*function_get)(struct counter_device *counter,
			    struct counter_count *count, size_t *function);
	int (*function_set)(struct counter_device *counter,
			    struct counter_count *count, size_t function);
	int (*action_get)(struct counter_device *counter,
			  struct counter_count *count,
			  struct counter_synapse *synapse, size_t *action);
	int (*action_set)(struct counter_device *counter,
			  struct counter_count *count,
			  struct counter_synapse *synapse, size_t action);
};

/**
 * struct counter_device_ext - Counter device extension
 * @name:	attribute name
 * @read:	read callback for this attribute; may be NULL
 * @write:	write callback for this attribute; may be NULL
 * @priv:	data private to the driver
 */
struct counter_device_ext {
	const char *name;
	ssize_t (*read)(struct counter_device *counter, void *priv, char *buf);
	ssize_t (*write)(struct counter_device *counter, void *priv,
			 const char *buf, size_t len);
	void *priv;
};

/**
 * struct counter_device_enum_ext - Counter enum extension attribute
 * @items:	Array of strings
 * @num_items:	Number of items specified in @items
 * @set:	Set callback function; may be NULL
 * @get:	Get callback function; may be NULL
 *
 * The counter_device_enum_ext structure can be used to implement enum style
 * Counter extension attributes. Enum style attributes are those which have a
 * set of strings that map to unsigned integer values. The Generic Counter enum
 * extension helper code takes care of mapping between value and string, as well
 * as generating a "_available" file which contains a list of all available
 * items. The get callback is used to query the currently active item; the index
 * of the item within the respective items array is returned via the 'item'
 * parameter. The set callback is called when the attribute is updated; the
 * 'item' parameter contains the index of the newly activated item within the
 * respective items array.
 */
struct counter_device_enum_ext {
	const char * const *items;
	size_t num_items;
	int (*get)(struct counter_device *counter, size_t *item);
	int (*set)(struct counter_device *counter, size_t item);
};

/**
 * COUNTER_DEVICE_ENUM() - Initialize Counter enum extension
 * @_name:	Attribute name
 * @_e:		Pointer to a counter_device_enum_ext structure
 *
 * This should usually be used together with COUNTER_DEVICE_ENUM_AVAILABLE()
 */
#define COUNTER_DEVICE_ENUM(_name, _e) \
{ \
	.name = (_name), \
	.read = counter_device_enum_read, \
	.write = counter_device_enum_write, \
	.priv = (_e) \
}

/**
 * COUNTER_DEVICE_ENUM_AVAILABLE() - Initialize Counter enum available extension
 * @_name:	Attribute name ("_available" will be appended to the name)
 * @_e:		Pointer to a counter_device_enum_ext structure
 *
 * Creates a read only attribute that lists all the available enum items in a
 * newline separated list. This should usually be used together with
 * COUNTER_DEVICE_ENUM()
 */
#define COUNTER_DEVICE_ENUM_AVAILABLE(_name, _e) \
{ \
	.name = (_name "_available"), \
	.read = counter_device_enum_available_read, \
	.priv = (_e) \
}

/**
 * struct counter_device - Counter data structure
 * @name:		name of the device as it appears in the datasheet
 * @parent:		optional parent device providing the counters
 * @device_state:	internal device state container
 * @ops:		callbacks from driver
 * @signals:		array of Signals
 * @num_signals:	number of Signals specified in @signals
 * @counts:		array of Counts
 * @num_counts:		number of Counts specified in @counts
 * @ext:		optional array of Counter device extensions
 * @num_ext:		number of Counter device extensions specified in @ext
 * @priv:		optional private data supplied by driver
 */
struct counter_device {
	const char *name;
	struct device *parent;
	struct counter_device_state *device_state;

	const struct counter_ops *ops;

	struct counter_signal *signals;
	size_t num_signals;
	struct counter_count *counts;
	size_t num_counts;

	const struct counter_device_ext *ext;
	size_t num_ext;

	void *priv;
};

enum counter_signal_level {
	COUNTER_SIGNAL_LEVEL_LOW = 0,
	COUNTER_SIGNAL_LEVEL_HIGH
};

enum counter_signal_value_type {
	COUNTER_SIGNAL_LEVEL = 0
};

enum counter_count_value_type {
	COUNTER_COUNT_POSITION = 0,
};

void counter_signal_read_value_set(struct counter_signal_read_value *const val,
				   const enum counter_signal_value_type type,
				   void *const data);
void counter_count_read_value_set(struct counter_count_read_value *const val,
				  const enum counter_count_value_type type,
				  void *const data);
int counter_count_write_value_get(void *const data,
				  const enum counter_count_value_type type,
				  const struct counter_count_write_value *const val);

int counter_register(struct counter_device *const counter);
void counter_unregister(struct counter_device *const counter);
int devm_counter_register(struct device *dev,
			  struct counter_device *const counter);
void devm_counter_unregister(struct device *dev,
			     struct counter_device *const counter);

#endif /* _COUNTER_H_ */
