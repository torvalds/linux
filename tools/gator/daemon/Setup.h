/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SETUP_H
#define SETUP_H

// From include/generated/uapi/linux/version.h
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

bool getLinuxVersion(int version[3]);
int update(const char *const gatorPath);

#endif // SETUP_H
