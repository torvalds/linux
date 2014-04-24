/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DYNBUF_H
#define DYNBUF_H

#include <stdlib.h>

class DynBuf {
public:
	DynBuf() : capacity(0), length(0), buf(NULL) {}
	~DynBuf() {
		reset();
	}

	inline void reset() {
		capacity = 0;
		length = 0;
		if (buf != NULL) {
			free(buf);
			buf = NULL;
		}
	}

	bool read(const char *const path);
	// On error instead of printing the error and returning false, this returns -errno
	int readlink(const char *const path);
	__attribute__ ((format(printf, 2, 3)))
	bool printf(const char *format, ...);

	size_t getLength() const { return length; }
	const char *getBuf() const { return buf; }
	char *getBuf() { return buf; }

private:
	int resize(const size_t minCapacity);

	size_t capacity;
	size_t length;
	char *buf;

	// Intentionally undefined
	DynBuf(const DynBuf &);
	DynBuf &operator=(const DynBuf &);
};

#endif // DYNBUF_H
