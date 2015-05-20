/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "CPUFreqDriver.h"

#include "Buffer.h"
#include "DriverSource.h"
#include "Logging.h"
#include "SessionData.h"

CPUFreqDriver::CPUFreqDriver() : mPrev() {
}

CPUFreqDriver::~CPUFreqDriver() {
}

void CPUFreqDriver::readEvents(mxml_node_t *const) {
	// Only for use with perf
	if (!gSessionData->perf.isSetup()) {
		return;
	}

	setCounters(new DriverCounter(getCounters(), strdup("Linux_power_cpu_freq")));
}

void CPUFreqDriver::read(Buffer *const buffer) {
	char buf[64];
	const DriverCounter *const counter = getCounters();
	if ((counter == NULL) || !counter->isEnabled()) {
		return;
	}

	const int key = getCounters()->getKey();
	bool resetCores = false;
	for (int i = 0; i < gSessionData->mCores; ++i) {
		snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_cur_freq", i);
		int64_t freq;
		if (DriverSource::readInt64Driver(buf, &freq) != 0) {
			freq = 0;
		}
		if (mPrev[i] != freq) {
			mPrev[i] = freq;
			// Change cores
			buffer->event64(2, i);
			resetCores = true;
			buffer->event64(key, 1000*freq);
		}
	}
	if (resetCores) {
		// Revert cores, UserSpaceSource is all on core 0
		buffer->event64(2, 0);
	}
}
