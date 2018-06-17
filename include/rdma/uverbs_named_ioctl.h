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
#define UVERBS_OBJECT(id)	_UVERBS_NAME(UVERBS_MOUDLE_NAME, _object_##id)

#define DECLARE_UVERBS_NAMED_METHOD(id, ...)	\
	DECLARE_UVERBS_METHOD(UVERBS_METHOD(id), id, UVERBS_HANDLER(id), ##__VA_ARGS__)

#define DECLARE_UVERBS_NAMED_METHOD_WITH_HANDLER(id, handler, ...)	\
	DECLARE_UVERBS_METHOD(UVERBS_METHOD(id), id, handler, ##__VA_ARGS__)

#define DECLARE_UVERBS_NAMED_METHOD_NO_OVERRIDE(id, handler, ...)	\
	DECLARE_UVERBS_METHOD(UVERBS_METHOD(id), id, NULL, ##__VA_ARGS__)

#define DECLARE_UVERBS_NAMED_OBJECT(id, ...)	\
	DECLARE_UVERBS_OBJECT(UVERBS_OBJECT(id), id, ##__VA_ARGS__)

#define DECLARE_UVERBS_GLOBAL_METHODS(_name, ...)	\
	DECLARE_UVERBS_NAMED_OBJECT(_name, NULL, ##__VA_ARGS__)

#define _UVERBS_COMP_NAME(x, y, z) _UVERBS_NAME(_UVERBS_NAME(x, y), z)

#define UVERBS_NO_OVERRIDE	NULL

/* This declares a parsing tree with one object and one method. This is usually
 * used for merging driver attributes to the common attributes. The driver has
 * a chance to override the handler and type attrs of the original object.
 * The __VA_ARGS__ just contains a list of attributes.
 */
#define ADD_UVERBS_ATTRIBUTES(_name, _object, _method, _type_attrs, _handler, ...) \
static DECLARE_UVERBS_METHOD(_UVERBS_COMP_NAME(UVERBS_MODULE_NAME,	     \
					       _method_, _name),	     \
			     _method, _handler, ##__VA_ARGS__);		     \
									     \
static DECLARE_UVERBS_OBJECT(_UVERBS_COMP_NAME(UVERBS_MODULE_NAME,	     \
					       _object_, _name),	     \
			     _object, _type_attrs,			     \
			     &_UVERBS_COMP_NAME(UVERBS_MODULE_NAME,	     \
					       _method_, _name));	     \
									     \
static DECLARE_UVERBS_OBJECT_TREE(_name,				     \
				  &_UVERBS_COMP_NAME(UVERBS_MODULE_NAME,     \
						     _object_, _name))

/* A very common use case is that the driver doesn't override the handler and
 * type_attrs. Therefore, we provide a simplified macro for this common case.
 */
#define ADD_UVERBS_ATTRIBUTES_SIMPLE(_name, _object, _method, ...)	     \
	ADD_UVERBS_ATTRIBUTES(_name, _object, _method, UVERBS_NO_OVERRIDE,   \
			      UVERBS_NO_OVERRIDE, ##__VA_ARGS__)

#endif
