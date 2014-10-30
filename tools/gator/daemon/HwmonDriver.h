/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef HWMONDRIVER_H
#define HWMONDRIVER_H

#include "Driver.h"

class HwmonDriver : public PolledDriver {
public:
	HwmonDriver();
	~HwmonDriver();

	void readEvents(mxml_node_t *const root);

	void writeEvents(mxml_node_t *root) const;

	void start();

private:
	// Intentionally unimplemented
	HwmonDriver(const HwmonDriver &);
	HwmonDriver &operator=(const HwmonDriver &);
};

#endif // HWMONDRIVER_H
