/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _UVERBS_NAMED_IOCTL_
#define _UVERBS_NAMED_IOCTL_

#include <rdma/uverbs_ioctl.h>

#ifndef UVERBS_MODULE_NAME
#error "Please #define UVERBS_MODULE_NAME before including rdma/uverbs_named_ioctl.h"
#endif

#define _UVERBS_PASTE(x, y)	x ## y
#define _UVERBS_NAME(x, y)	_UVERBS_PASTE(x, y)
#define UVERBS_METHOD(id)	_UVERBS_NAME(UVERBS_MODULE_NAME, _method_##id)
#define UVERBS_HANDLER(id)	_UVERBS_NAME(UVERBS_MODULE_NAME, _handler_##id)
#define UVERBS_OBJECT(id)	_UVERBS_NAME(UVERBS_MODULE_NAME, _object_##id)

/* These are static so they do not need to be qualified */
#define UVERBS_METHOD_ATTRS(method_id) _method_attrs_##method_id
#define UVERBS_OBJECT_METHODS(object_id) _object_methods_##object_id

#define DECLARE_UVERBS_NAMED_METHOD(_method_id, ...)                           \
	static const struct uverbs_attr_def *const UVERBS_METHOD_ATTRS(        \
		_method_id)[] = { __VA_ARGS__ };                               \
	static const struct uverbs_method_def UVERBS_METHOD(_method_id) = {    \
		.id = _method_id,                                              \
		.handler = UVERBS_HANDLER(_method_id),                         \
		.num_attrs = ARRAY_SIZE(UVERBS_METHOD_ATTRS(_method_id)),      \
		.attrs = &UVERBS_METHOD_ATTRS(_method_id),                     \
	}

/* Create a standard destroy method using the default handler. The handle_attr
 * argument must be the attribute specifying the handle to destroy, the
 * default handler does not support any other attributes.
 */
#define DECLARE_UVERBS_NAMED_METHOD_DESTROY(_method_id, _handle_attr)          \
	static const struct uverbs_attr_def *const UVERBS_METHOD_ATTRS(        \
		_method_id)[] = { _handle_attr };                              \
	static const struct uverbs_method_def UVERBS_METHOD(_method_id) = {    \
		.id = _method_id,                                              \
		.handler = uverbs_destroy_def_handler,                         \
		.num_attrs = ARRAY_SIZE(UVERBS_METHOD_ATTRS(_method_id)),      \
		.attrs = &UVERBS_METHOD_ATTRS(_method_id),                     \
	}

#define DECLARE_UVERBS_NAMED_OBJECT(_object_id, _type_attrs, ...)              \
	static const struct uverbs_method_def *const UVERBS_OBJECT_METHODS(    \
		_object_id)[] = { __VA_ARGS__ };                               \
	const struct uverbs_object_def UVERBS_OBJECT(_object_id) = {           \
		.id = _object_id,                                              \
		.type_attrs = &_type_attrs,                                    \
		.num_methods = ARRAY_SIZE(UVERBS_OBJECT_METHODS(_object_id)),  \
		.methods = &UVERBS_OBJECT_METHODS(_object_id)                  \
	}

/*
 * Declare global methods. These still have a unique object_id because we
 * identify all uapi methods with a (object,method) tuple. However, they have
 * no type pointer.
 */
#define DECLARE_UVERBS_GLOBAL_METHODS(_object_id, ...)	\
	static const struct uverbs_method_def *const UVERBS_OBJECT_METHODS(    \
		_object_id)[] = { __VA_ARGS__ };                               \
	const struct uverbs_object_def UVERBS_OBJECT(_object_id) = {           \
		.id = _object_id,                                              \
		.num_methods = ARRAY_SIZE(UVERBS_OBJECT_METHODS(_object_id)),  \
		.methods = &UVERBS_OBJECT_METHODS(_object_id)                  \
	}

/* Used by drivers to declare a complete parsing tree for new methods
 */
#define ADD_UVERBS_METHODS(_name, _object_id, ...)                             \
	static const struct uverbs_method_def *const UVERBS_OBJECT_METHODS(    \
		_object_id)[] = { __VA_ARGS__ };                               \
	static const struct uverbs_object_def _name = {                        \
		.id = _object_id,                                              \
		.num_methods = ARRAY_SIZE(UVERBS_OBJECT_METHODS(_object_id)),  \
		.methods = &UVERBS_OBJECT_METHODS(_object_id)                  \
	};

/* Used by drivers to declare a complete parsing tree for a single method that
 * differs only in having additional driver specific attributes.
 */
#define ADD_UVERBS_ATTRIBUTES_SIMPLE(_name, _object_id, _method_id, ...)       \
	static const struct uverbs_attr_def *const UVERBS_METHOD_ATTRS(        \
		_method_id)[] = { __VA_ARGS__ };                               \
	static const struct uverbs_method_def UVERBS_METHOD(_method_id) = {    \
		.id = _method_id,                                              \
		.num_attrs = ARRAY_SIZE(UVERBS_METHOD_ATTRS(_method_id)),      \
		.attrs = &UVERBS_METHOD_ATTRS(_method_id),                     \
	};                                                                     \
	ADD_UVERBS_METHODS(_name, _object_id, &UVERBS_METHOD(_method_id))

#endif
