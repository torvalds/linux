/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PerfDriver.h"

#include <dirent.h>
#include <sys/utsname.h>
#include <time.h>

#include "Buffer.h"
#include "Config.h"
#include "ConfigurationXML.h"
#include "Counter.h"
#include "DriverSource.h"
#include "DynBuf.h"
#include "Logging.h"
#include "PerfGroup.h"
#include "SessionData.h"

#define PERF_DEVICES "/sys/bus/event_source/devices"

#define TYPE_DERIVED ~0U

// From gator.h
struct gator_cpu {
	const int cpuid;
	// Human readable name
	const char core_name[32];
	// gatorfs event and Perf PMU name
	const char *const pmnc_name;
	const int pmnc_counters;
};

// From gator_main.c
static const struct gator_cpu gator_cpus[] = {
	{ 0xb36, "ARM1136",      "ARM_ARM11",        3 },
	{ 0xb56, "ARM1156",      "ARM_ARM11",        3 },
	{ 0xb76, "ARM1176",      "ARM_ARM11",        3 },
	{ 0xb02, "ARM11MPCore",  "ARM_ARM11MPCore",  3 },
	{ 0xc05, "Cortex-A5",    "ARMv7_Cortex_A5",  2 },
	{ 0xc07, "Cortex-A7",    "ARMv7_Cortex_A7",  4 },
	{ 0xc08, "Cortex-A8",    "ARMv7_Cortex_A8",  4 },
	{ 0xc09, "Cortex-A9",    "ARMv7_Cortex_A9",  6 },
	{ 0xc0d, "Cortex-A12",   "ARMv7_Cortex_A12", 6 },
	{ 0xc0f, "Cortex-A15",   "ARMv7_Cortex_A15", 6 },
	{ 0xc0e, "Cortex-A17",   "ARMv7_Cortex_A17", 6 },
	{ 0x00f, "Scorpion",     "Scorpion",         4 },
	{ 0x02d, "ScorpionMP",   "ScorpionMP",       4 },
	{ 0x049, "KraitSIM",     "Krait",            4 },
	{ 0x04d, "Krait",        "Krait",            4 },
	{ 0x06f, "Krait S4 Pro", "Krait",            4 },
	{ 0xd03, "Cortex-A53",   "ARM_Cortex-A53",   6 },
	{ 0xd07, "Cortex-A57",   "ARM_Cortex-A57",   6 },
	{ 0xd0f, "AArch64",      "ARM_AArch64",      6 },
};

static const char OLD_PMU_PREFIX[] = "ARMv7 Cortex-";
static const char NEW_PMU_PREFIX[] = "ARMv7_Cortex_";

class PerfCounter {
public:
	PerfCounter(PerfCounter *next, const char *name, uint32_t type, uint64_t config) : mNext(next), mName(name), mType(type), mCount(0), mKey(getEventKey()), mConfig(config), mEnabled(false) {}
	~PerfCounter() {
		delete [] mName;
	}

	PerfCounter *getNext() const { return mNext; }
	const char *getName() const { return mName; }
	uint32_t getType() const { return mType; }
	int getCount() const { return mCount; }
	void setCount(const int count) { mCount = count; }
	int getKey() const { return mKey; }
	uint64_t getConfig() const { return mConfig; }
	void setConfig(const uint64_t config) { mConfig = config; }
	bool isEnabled() const { return mEnabled; }
	void setEnabled(const bool enabled) { mEnabled = enabled; }

private:
	PerfCounter *const mNext;
	const char *const mName;
	const uint32_t mType;
	int mCount;
	const int mKey;
	uint64_t mConfig;
	bool mEnabled;
};

PerfDriver::PerfDriver() : mCounters(NULL), mIsSetup(false) {
}

PerfDriver::~PerfDriver() {
	while (mCounters != NULL) {
		PerfCounter *counter = mCounters;
		mCounters = counter->getNext();
		delete counter;
	}
}

void PerfDriver::addCpuCounters(const char *const counterName, const int type, const int numCounters) {
	int len = snprintf(NULL, 0, "%s_ccnt", counterName) + 1;
	char *name = new char[len];
	snprintf(name, len, "%s_ccnt", counterName);
	mCounters = new PerfCounter(mCounters, name, type, -1);

	for (int j = 0; j < numCounters; ++j) {
		len = snprintf(NULL, 0, "%s_cnt%d", counterName, j) + 1;
		name = new char[len];
		snprintf(name, len, "%s_cnt%d", counterName, j);
		mCounters = new PerfCounter(mCounters, name, type, -1);
	}
}

// From include/generated/uapi/linux/version.h
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

bool PerfDriver::setup() {
	// Check the kernel version
	struct utsname utsname;
	if (uname(&utsname) != 0) {
		logg->logMessage("%s(%s:%i): uname failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	int release[3] = { 0, 0, 0 };
	int part = 0;
	char *ch = utsname.release;
	while (*ch >= '0' && *ch <= '9' && part < ARRAY_LENGTH(release)) {
		release[part] = 10*release[part] + *ch - '0';

		++ch;
		if (*ch == '.') {
			++part;
			++ch;
		}
	}

	if (KERNEL_VERSION(release[0], release[1], release[2]) < KERNEL_VERSION(3, 12, 0)) {
		logg->logMessage("%s(%s:%i): Unsupported kernel version", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	// Add supported PMUs
	bool foundCpu = false;
	DIR *dir = opendir(PERF_DEVICES);
	if (dir == NULL) {
		logg->logMessage("%s(%s:%i): opendif failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	struct dirent *dirent;
	while ((dirent = readdir(dir)) != NULL) {
		for (int i = 0; i < ARRAY_LENGTH(gator_cpus); ++i) {
			// Do the names match exactly?
			if (strcmp(dirent->d_name, gator_cpus[i].pmnc_name) != 0 &&
					// Do these names match but have the old vs new prefix?
			    (strncmp(dirent->d_name, OLD_PMU_PREFIX, sizeof(OLD_PMU_PREFIX) - 1) != 0 ||
			     strncmp(gator_cpus[i].pmnc_name, NEW_PMU_PREFIX, sizeof(NEW_PMU_PREFIX) - 1) != 0 ||
			     strcmp(dirent->d_name + sizeof(OLD_PMU_PREFIX) - 1, gator_cpus[i].pmnc_name + sizeof(NEW_PMU_PREFIX) - 1) != 0)) {
				continue;
			}

			int type;
			char buf[256];
			snprintf(buf, sizeof(buf), PERF_DEVICES "/%s/type", dirent->d_name);
			if (DriverSource::readIntDriver(buf, &type) != 0) {
				continue;
			}

			foundCpu = true;
			addCpuCounters(gator_cpus[i].pmnc_name, type, gator_cpus[i].pmnc_counters);
		}
	}
	closedir(dir);

	if (!foundCpu) {
		// If no cpu was found based on pmu names, try by cpuid
		for (int i = 0; i < ARRAY_LENGTH(gator_cpus); ++i) {
			if (gSessionData->mMaxCpuId != gator_cpus[i].cpuid) {
				continue;
			}

			foundCpu = true;
			addCpuCounters(gator_cpus[i].pmnc_name, PERF_TYPE_RAW, gator_cpus[i].pmnc_counters);
		}
	}

	/*
	if (!foundCpu) {
		// If all else fails, use the perf architected counters
		// 9 because that's how many are in events-Perf-Hardware.xml - assume they can all be enabled at once
		addCpuCounters("Perf_Hardware", PERF_TYPE_HARDWARE, 9);
	}
	*/

	// Add supported software counters
	long long id;
	DynBuf printb;

	id = getTracepointId("irq/softirq_exit", &printb);
	if (id >= 0) {
		mCounters = new PerfCounter(mCounters, "Linux_irq_softirq", PERF_TYPE_TRACEPOINT, id);
	}

	id = getTracepointId("irq/irq_handler_exit", &printb);
	if (id >= 0) {
		mCounters = new PerfCounter(mCounters, "Linux_irq_irq", PERF_TYPE_TRACEPOINT, id);
	}

	//Linux_block_rq_wr
	//Linux_block_rq_rd
	//Linux_net_rx
	//Linux_net_tx

	id = getTracepointId(SCHED_SWITCH, &printb);
	if (id >= 0) {
		mCounters = new PerfCounter(mCounters, "Linux_sched_switch", PERF_TYPE_TRACEPOINT, id);
	}

	//Linux_meminfo_memused
	//Linux_meminfo_memfree
	//Linux_meminfo_bufferram
	//Linux_power_cpu_freq
	//Linux_power_cpu_idle

	mCounters = new PerfCounter(mCounters, "Linux_cpu_wait_contention", TYPE_DERIVED, -1);

	//Linux_cpu_wait_io

	mIsSetup = true;
	return true;
}

bool PerfDriver::summary(Buffer *const buffer) {
	struct utsname utsname;
	if (uname(&utsname) != 0) {
		logg->logMessage("%s(%s:%i): uname failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	char buf[512];
	snprintf(buf, sizeof(buf), "%s %s %s %s %s GNU/Linux", utsname.sysname, utsname.nodename, utsname.release, utsname.version, utsname.machine);

	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		logg->logMessage("%s(%s:%i): clock_gettime failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	const int64_t timestamp = (int64_t)ts.tv_sec * 1000000000L + ts.tv_nsec;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		logg->logMessage("%s(%s:%i): clock_gettime failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	const int64_t uptime = (int64_t)ts.tv_sec * 1000000000L + ts.tv_nsec;

	buffer->summary(timestamp, uptime, 0, buf);

	for (int i = 0; i < gSessionData->mCores; ++i) {
		int j;
		for (j = 0; j < ARRAY_LENGTH(gator_cpus); ++j) {
			if (gator_cpus[j].cpuid == gSessionData->mCpuIds[i]) {
				break;
			}
		}
		if (gator_cpus[j].cpuid == gSessionData->mCpuIds[i]) {
			buffer->coreName(i, gSessionData->mCpuIds[i], gator_cpus[j].core_name);
		} else {
			snprintf(buf, sizeof(buf), "Unknown (0x%.3x)", gSessionData->mCpuIds[i]);
			buffer->coreName(i, gSessionData->mCpuIds[i], buf);
		}
	}
	buffer->commit(1);

	return true;
}

PerfCounter *PerfDriver::findCounter(const Counter &counter) const {
	for (PerfCounter * perfCounter = mCounters; perfCounter != NULL; perfCounter = perfCounter->getNext()) {
		if (strcmp(perfCounter->getName(), counter.getType()) == 0) {
			return perfCounter;
		}
	}

	return NULL;
}

bool PerfDriver::claimCounter(const Counter &counter) const {
	return findCounter(counter) != NULL;
}

void PerfDriver::resetCounters() {
	for (PerfCounter * counter = mCounters; counter != NULL; counter = counter->getNext()) {
		counter->setEnabled(false);
	}
}

void PerfDriver::setupCounter(Counter &counter) {
	PerfCounter *const perfCounter = findCounter(counter);
	if (perfCounter == NULL) {
		counter.setEnabled(false);
		return;
	}

	// Don't use the config from counters XML if it's not set, ex: software counters
	if (counter.getEvent() != -1) {
		perfCounter->setConfig(counter.getEvent());
	}
	perfCounter->setCount(counter.getCount());
	perfCounter->setEnabled(true);
	counter.setKey(perfCounter->getKey());
}

int PerfDriver::writeCounters(mxml_node_t *root) const {
	int count = 0;
	for (PerfCounter * counter = mCounters; counter != NULL; counter = counter->getNext()) {
		mxml_node_t *node = mxmlNewElement(root, "counter");
		mxmlElementSetAttr(node, "name", counter->getName());
		++count;
	}

	return count;
}

bool PerfDriver::enable(PerfGroup *group, Buffer *const buffer) const {
	for (PerfCounter * counter = mCounters; counter != NULL; counter = counter->getNext()) {
		if (counter->isEnabled() && (counter->getType() != TYPE_DERIVED)) {
			if (!group->add(buffer, counter->getKey(), counter->getType(), counter->getConfig(), counter->getCount(), 0, 0)) {
				logg->logMessage("%s(%s:%i): PerfGroup::add failed", __FUNCTION__, __FILE__, __LINE__);
				return false;
			}
		}
	}

	return true;
}

long long PerfDriver::getTracepointId(const char *const name, DynBuf *const printb) {
	if (!printb->printf(EVENTS_PATH "/%s/id", name)) {
		logg->logMessage("%s(%s:%i): DynBuf::printf failed", __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	int64_t result;
	if (DriverSource::readInt64Driver(printb->getBuf(), &result) != 0) {
		logg->logMessage("%s(%s:%i): DriverSource::readInt64Driver failed", __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	return result;
}
