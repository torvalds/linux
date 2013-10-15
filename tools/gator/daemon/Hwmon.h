/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	HWMON_H
#define	HWMON_H

#include "Driver.h"

class Buffer;
class HwmonCounter;

class Hwmon : public Driver {
public:
	Hwmon();
	~Hwmon();

	bool claimCounter(const Counter &counter) const;
	bool countersEnabled() const;
	void resetCounters();
	void setupCounter(Counter &counter);

	void writeCounters(mxml_node_t *root) const;
	void writeEvents(mxml_node_t *root) const;

	void start();
	void read(Buffer * buffer);

private:
	HwmonCounter *findCounter(const Counter &counter) const;

	HwmonCounter *counters;
};

#endif // HWMON_H
