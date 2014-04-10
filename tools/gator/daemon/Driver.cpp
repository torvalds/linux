/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Driver.h"

Driver *Driver::head = NULL;

Driver::Driver() : next(head) {
	head = this;
}
