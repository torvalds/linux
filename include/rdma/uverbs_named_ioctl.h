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

#define DECLARE_UVERBS_NAMED_METHOD(id, ...)	\
	DECLARE_UVERBS_METHOD(UVERBS_METHOD(id), id, UVERBS_HANDLER(id), ##__VA_ARGS__)

#define DECLARE_UVERBS_NAMED_METHOD_WITH_HANDLER(id, handler, ...)	\
	DECLARE_UVERBS_METHOD(UVERBS_METHOD(id), id, handler, ##__VA_ARGS__)

#define DECLARE_UVERBS_NAMED_METHOD_NO_OVERRIDE(id, handler, ...)	\
	DECLARE_UVERBS_METHOD(UVERBS_METHOD(id), id, NULL, ##__VA_ARGS__)

#define DECLARE_UVERBS_NAMED_OBJECT(id, ...)	\
	DECLARE_UVERBS_OBJECT(UVERBS_OBJECT(id), id, ##__VA_ARGS__)

#endif
