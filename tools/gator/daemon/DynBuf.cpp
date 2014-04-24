/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "DynBuf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "Logging.h"

// Pick an aggressive size as buffer is primarily used for disk IO
#define MIN_BUFFER_FREE (1 << 12)

int DynBuf::resize(const size_t minCapacity) {
	size_t scaledCapacity = 2 * capacity;
	if (scaledCapacity < minCapacity) {
		scaledCapacity = minCapacity;
	}
	if (scaledCapacity < 2 * MIN_BUFFER_FREE) {
		scaledCapacity = 2 * MIN_BUFFER_FREE;
	}
	capacity = scaledCapacity;

	buf = static_cast<char *>(realloc(buf, capacity));
	if (buf == NULL) {
		return -errno;
	}

	return 0;
}

bool DynBuf::read(const char *const path) {
	int result = false;

	const int fd = open(path, O_RDONLY);
	if (fd < 0) {
		logg->logMessage("%s(%s:%i): open failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	length = 0;

	for (;;) {
		const size_t minCapacity = length + MIN_BUFFER_FREE + 1;
		if (capacity < minCapacity) {
			if (resize(minCapacity) != 0) {
				logg->logMessage("%s(%s:%i): DynBuf::resize failed", __FUNCTION__, __FILE__, __LINE__);
				goto fail;
			}
		}

		const ssize_t bytes = ::read(fd, buf + length, capacity - length - 1);
		if (bytes < 0) {
			logg->logMessage("%s(%s:%i): read failed", __FUNCTION__, __FILE__, __LINE__);
			goto fail;
		} else if (bytes == 0) {
			break;
		}
		length += bytes;
	}

	buf[length] = '\0';
	result = true;

 fail:
	close(fd);

	return result;
}

int DynBuf::readlink(const char *const path) {
	ssize_t bytes = MIN_BUFFER_FREE;

	for (;;) {
		if (static_cast<size_t>(bytes) >= capacity) {
			const int err = resize(2 * bytes);
			if (err != 0) {
				return err;
			}
		}
		bytes = ::readlink(path, buf, capacity);
		if (bytes < 0) {
			return -errno;
		} else if (static_cast<size_t>(bytes) < capacity) {
			break;
		}
	}

	length = bytes;
	buf[bytes] = '\0';

	return 0;
}

bool DynBuf::printf(const char *format, ...) {
	va_list ap;

	if (capacity <= 0) {
		if (resize(2 * MIN_BUFFER_FREE) != 0) {
			logg->logMessage("%s(%s:%i): DynBuf::resize failed", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}
	}

	va_start(ap, format);
	int bytes = vsnprintf(buf, capacity, format, ap);
	va_end(ap);
	if (bytes < 0) {
		logg->logMessage("%s(%s:%i): fsnprintf failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	if (static_cast<size_t>(bytes) > capacity) {
		if (resize(bytes + 1) != 0) {
			logg->logMessage("%s(%s:%i): DynBuf::resize failed", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}

		va_start(ap, format);
		bytes = vsnprintf(buf, capacity, format, ap);
		va_end(ap);
		if (bytes < 0) {
			logg->logMessage("%s(%s:%i): fsnprintf failed", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}
	}

	length = bytes;

	return true;
}
