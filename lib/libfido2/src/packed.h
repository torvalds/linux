/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#ifndef _PACKED_H
#define _PACKED_H

#if defined(__GNUC__)
#define PACKED_TYPE(type, def)	\
	typedef def __attribute__ ((__packed__)) type;
#elif defined(_MSC_VER)
#define PACKED_TYPE(type, def)	\
	__pragma(pack(push, 1))	\
	typedef def type;	\
	__pragma(pack(pop))
#else
#error "please provide a way to define packed types on your platform"
#endif

#endif /* !_PACKED_H */
