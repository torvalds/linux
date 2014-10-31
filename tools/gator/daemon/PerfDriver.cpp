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
#include <unistd.h>

#include "Buffer.h"
#include "Config.h"
#include "ConfigurationXML.h"
#include "Counter.h"
#include "DriverSource.h"
#include "DynBuf.h"
#include "Logging.h"
#include "PerfGroup.h"
#include "SessionData.h"
#include "Setup.h"

#define PERF_DEVICES "/sys/bus/event_source/devices"

#define TYPE_DERIVED ~0U

// From gator.h
struct gator_cpu {
	const int cpuid;
	// Human readable name
	const char *const core_name;
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

struct uncore_counter {
	// Perf PMU name
	const char *const perfName;
	// gatorfs event name
	const char *const gatorName;
	const int count;
};

static const struct uncore_counter uncore_counters[] = {
	{ "CCI_400", "CCI_400", 4 },
	{ "CCI_400-r1", "CCI_400-r1", 4 },
	{ "ccn", "ARM_CCN_5XX", 8 },
};

class PerfCounter : public DriverCounter {
public:
	PerfCounter(DriverCounter *next, const char *name, uint32_t type, uint64_t config, bool perCpu) : DriverCounter(next, name), mType(type), mCount(0), mConfig(config), mPerCpu(perCpu) {}

	~PerfCounter() {
	}

	uint32_t getType() const { return mType; }
	int getCount() const { return mCount; }
	void setCount(const int count) { mCount = count; }
	uint64_t getConfig() const { return mConfig; }
	void setConfig(const uint64_t config) { mConfig = config; }
	bool isPerCpu() const { return mPerCpu; }

private:
	const uint32_t mType;
	int mCount;
	uint64_t mConfig;
	bool mPerCpu;
};

PerfDriver::PerfDriver() : mIsSetup(false), mLegacySupport(false) {
}

PerfDriver::~PerfDriver() {
}

void PerfDriver::addCpuCounters(const char *const counterName, const int type, const int numCounters) {
	int len = snprintf(NULL, 0, "%s_ccnt", counterName) + 1;
	char *name = new char[len];
	snprintf(name, len, "%s_ccnt", counterName);
	setCounters(new PerfCounter(getCounters(), name, type, -1, true));

	for (int j = 0; j < numCounters; ++j) {
		len = snprintf(NULL, 0, "%s_cnt%d", counterName, j) + 1;
		name = new char[len];
		snprintf(name, len, "%s_cnt%d", counterName, j);
		setCounters(new PerfCounter(getCounters(), name, type, -1, true));
	}
}

void PerfDriver::addUncoreCounters(const char *const counterName, const int type, const int numCounters) {
	int len = snprintf(NULL, 0, "%s_ccnt", counterName) + 1;
	char *name = new char[len];
	snprintf(name, len, "%s_ccnt", counterName);
	setCounters(new PerfCounter(getCounters(), name, type, -1, false));

	for (int j = 0; j < numCounters; ++j) {
		len = snprintf(NULL, 0, "%s_cnt%d", counterName, j) + 1;
		name = new char[len];
		snprintf(name, len, "%s_cnt%d", counterName, j);
		setCounters(new PerfCounter(getCounters(), name, type, -1, false));
	}
}

bool PerfDriver::setup() {
	// Check the kernel version
	int release[3];
	if (!getLinuxVersion(release)) {
		logg->logMessage("%s(%s:%i): getLinuxVersion failed", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}

	if (KERNEL_VERSION(release[0], release[1], release[2]) < KERNEL_VERSION(3, 4, 0)) {
		logg->logMessage("%s(%s:%i): Unsupported kernel version", __FUNCTION__, __FILE__, __LINE__);
		return false;
	}
	mLegacySupport = KERNEL_VERSION(release[0], release[1], release[2]) < KERNEL_VERSION(3, 12, 0);

	if (access(EVENTS_PATH, R_OK) != 0) {
		logg->logMessage("%s(%s:%i): " EVENTS_PATH " does not exist, is CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?", __FUNCTION__, __FILE__, __LINE__);
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
			const struct gator_cpu *const gator_cpu = &gator_cpus[i];

			// Do the names match exactly?
			if (strcasecmp(gator_cpu->pmnc_name, dirent->d_name) != 0 &&
			    // Do these names match but have the old vs new prefix?
			    ((strncasecmp(dirent->d_name, OLD_PMU_PREFIX, sizeof(OLD_PMU_PREFIX) - 1) != 0 ||
			      strncasecmp(gator_cpu->pmnc_name, NEW_PMU_PREFIX, sizeof(NEW_PMU_PREFIX) - 1) != 0 ||
			      strcasecmp(dirent->d_name + sizeof(OLD_PMU_PREFIX) - 1, gator_cpu->pmnc_name + sizeof(NEW_PMU_PREFIX) - 1) != 0))) {
				continue;
			}

			int type;
			char buf[256];
			snprintf(buf, sizeof(buf), PERF_DEVICES "/%s/type", dirent->d_name);
			if (DriverSource::readIntDriver(buf, &type) != 0) {
				continue;
			}

			foundCpu = true;
			logg->logMessage("Adding cpu counters for %s", gator_cpu->pmnc_name);
			addCpuCounters(gator_cpu->pmnc_name, type, gator_cpu->pmnc_counters);
		}

		for (int i = 0; i < ARRAY_LENGTH(uncore_counters); ++i) {
			if (strcmp(dirent->d_name, uncore_counters[i].perfName) != 0) {
				continue;
			}

			int type;
			char buf[256];
			snprintf(buf, sizeof(buf), PERF_DEVICES "/%s/type", dirent->d_name);
			if (DriverSource::readIntDriver(buf, &type) != 0) {
				continue;
			}

			logg->logMessage("Adding uncore counters for %s", uncore_counters[i].gatorName);
			addUncoreCounters(uncore_counters[i].gatorName, type, uncore_counters[i].count);
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
			logg->logMessage("Adding cpu counters (based on cpuid) for %s", gator_cpus[i].pmnc_name);
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
		setCounters(new PerfCounter(getCounters(), "Linux_irq_softirq", PERF_TYPE_TRACEPOINT, id, true));
	}

	id = getTracepointId("irq/irq_handler_exit", &printb);
	if (id >= 0) {
		setCounters(new PerfCounter(getCounters(), "Linux_irq_irq", PERF_TYPE_TRACEPOINT, id, true));
	}

	id = getTracepointId(SCHED_SWITCH, &printb);
	if (id >= 0) {
		setCounters(new PerfCounter(getCounters(), "Linux_sched_switch", PERF_TYPE_TRACEPOINT, id, true));
	}

	setCounters(new PerfCounter(getCounters(), "Linux_cpu_wait_contention", TYPE_DERIVED, -1, false));

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
	const int64_t timestamp = (int64_t)ts.tv_sec * NS_PER_S + ts.tv_nsec;

	const uint64_t monotonicStarted = getTime();
	gSessionData->mMonotonicStarted = monotonicStarted;

	buffer->summary(monotonicStarted, timestamp, monotonicStarted, monotonicStarted, buf);

	for (int i = 0; i < gSessionData->mCores; ++i) {
		coreName(monotonicStarted, buffer, i);
	}
	buffer->commit(monotonicStarted);

	return true;
}

void PerfDriver::coreName(const uint32_t startTime, Buffer *const buffer, const int cpu) {
	// Don't send information on a cpu we know nothing about
	if (gSessionData->mCpuIds[cpu] == -1) {
		return;
	}

	int j;
	for (j = 0; j < ARRAY_LENGTH(gator_cpus); ++j) {
		if (gator_cpus[j].cpuid == gSessionData->mCpuIds[cpu]) {
			break;
		}
	}
	if (gator_cpus[j].cpuid == gSessionData->mCpuIds[cpu]) {
		buffer->coreName(startTime, cpu, gSessionData->mCpuIds[cpu], gator_cpus[j].core_name);
	} else {
		char buf[32];
		if (gSessionData->mCpuIds[cpu] == -1) {
			snprintf(buf, sizeof(buf), "Unknown");
		} else {
			snprintf(buf, sizeof(buf), "Unknown (0x%.3x)", gSessionData->mCpuIds[cpu]);
		}
		buffer->coreName(startTime, cpu, gSessionData->mCpuIds[cpu], buf);
	}
}

void PerfDriver::setupCounter(Counter &counter) {
	PerfCounter *const perfCounter = static_cast<PerfCounter *>(findCounter(counter));
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

bool PerfDriver::enable(const uint64_t currTime, PerfGroup *const group, Buffer *const buffer) const {
	for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL; counter = static_cast<PerfCounter *>(counter->getNext())) {
		if (counter->isEnabled() && (counter->getType() != TYPE_DERIVED)) {
			if (!group->add(currTime, buffer, counter->getKey(), counter->getType(), counter->getConfig(), counter->getCount(), counter->getCount() > 0 ? PERF_SAMPLE_TID | PERF_SAMPLE_IP : 0, counter->isPerCpu() ? PERF_GROUP_PER_CPU : 0)) {
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
