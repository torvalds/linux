/**
 * Copyright (C) ARM Limited 2013-2015. All rights reserved.
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
	{ 0x41b36, "ARM1136",      "ARM_ARM11",        3 },
	{ 0x41b56, "ARM1156",      "ARM_ARM11",        3 },
	{ 0x41b76, "ARM1176",      "ARM_ARM11",        3 },
	{ 0x41b02, "ARM11MPCore",  "ARM_ARM11MPCore",  3 },
	{ 0x41c05, "Cortex-A5",    "ARMv7_Cortex_A5",  2 },
	{ 0x41c07, "Cortex-A7",    "ARMv7_Cortex_A7",  4 },
	{ 0x41c08, "Cortex-A8",    "ARMv7_Cortex_A8",  4 },
	{ 0x41c09, "Cortex-A9",    "ARMv7_Cortex_A9",  6 },
	{ 0x41c0f, "Cortex-A15",   "ARMv7_Cortex_A15", 6 },
	{ 0x41c0d, "Cortex-A17",   "ARMv7_Cortex_A17", 6 },
	{ 0x41c0e, "Cortex-A17",   "ARMv7_Cortex_A17", 6 },
	{ 0x5100f, "Scorpion",     "Scorpion",         4 },
	{ 0x5102d, "ScorpionMP",   "ScorpionMP",       4 },
	{ 0x51049, "KraitSIM",     "Krait",            4 },
	{ 0x5104d, "Krait",        "Krait",            4 },
	{ 0x5106f, "Krait S4 Pro", "Krait",            4 },
	{ 0x41d03, "Cortex-A53",   "ARM_Cortex-A53",   6 },
	{ 0x41d07, "Cortex-A57",   "ARM_Cortex-A57",   6 },
	{ 0x41d08, "Cortex-A72",   "ARM_Cortex-A72",   6 },
};

static const char OLD_PMU_PREFIX[] = "ARMv7 Cortex-";
static const char NEW_PMU_PREFIX[] = "ARMv7_Cortex_";

struct uncore_counter {
	// Perf PMU name
	const char *const perfName;
	// gatorfs event name
	const char *const gatorName;
	const int count;
	const bool hasCyclesCounter;
};

static const struct uncore_counter uncore_counters[] = {
	{ "CCI_400",    "CCI_400",     4, true },
	{ "CCI_400-r1", "CCI_400-r1",  4, true },
	{ "CCI_500",    "CCI_500",     8, false },
	{ "ccn",        "ARM_CCN_5XX", 8, true },
};

class PerfCounter : public DriverCounter {
public:
	PerfCounter(DriverCounter *next, const char *name, uint32_t type, uint64_t config, uint64_t sampleType, uint64_t flags) : DriverCounter(next, name), mType(type), mConfig(config), mSampleType(sampleType), mFlags(flags), mCount(0) {}

	~PerfCounter() {
	}

	uint32_t getType() const { return mType; }
	int getCount() const { return mCount; }
	void setCount(const int count) { mCount = count; }
	uint64_t getConfig() const { return mConfig; }
	void setConfig(const uint64_t config) { mConfig = config; }
	uint64_t getSampleType() const { return mSampleType; }
	uint64_t getFlags() const { return mFlags; }
	virtual void read(Buffer *const, const int) {}

private:
	const uint32_t mType;
	uint64_t mConfig;
	const uint64_t mSampleType;
	const uint64_t mFlags;
	int mCount;

	// Intentionally undefined
	PerfCounter(const PerfCounter &);
	PerfCounter &operator=(const PerfCounter &);
};

class CPUFreqDriver : public PerfCounter {
public:
	CPUFreqDriver(DriverCounter *next, uint64_t id) : PerfCounter(next, "Linux_power_cpu_freq", PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_RAW, PERF_GROUP_LEADER | PERF_GROUP_PER_CPU) {}

	void read(Buffer *const buffer, const int cpu) {
		char buf[64];

		snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_cur_freq", cpu);
		int64_t freq;
		if (DriverSource::readInt64Driver(buf, &freq) != 0) {
			freq = 0;
		}
		buffer->perfCounter(cpu, getKey(), 1000*freq);
	}

private:
	// Intentionally undefined
	CPUFreqDriver(const CPUFreqDriver &);
	CPUFreqDriver &operator=(const CPUFreqDriver &);
};

PerfDriver::PerfDriver() : mIsSetup(false), mLegacySupport(false) {
}

PerfDriver::~PerfDriver() {
}

void PerfDriver::addCpuCounters(const char *const counterName, const int type, const int numCounters) {
	int len = snprintf(NULL, 0, "%s_ccnt", counterName) + 1;
	char *name = new char[len];
	snprintf(name, len, "%s_ccnt", counterName);
	setCounters(new PerfCounter(getCounters(), name, type, -1, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU));

	for (int j = 0; j < numCounters; ++j) {
		len = snprintf(NULL, 0, "%s_cnt%d", counterName, j) + 1;
		name = new char[len];
		snprintf(name, len, "%s_cnt%d", counterName, j);
		setCounters(new PerfCounter(getCounters(), name, type, -1, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU));
	}
}

void PerfDriver::addUncoreCounters(const char *const counterName, const int type, const int numCounters, const bool hasCyclesCounter) {
	int len;
	char *name;

	if (hasCyclesCounter) {
		len = snprintf(NULL, 0, "%s_ccnt", counterName) + 1;
		name = new char[len];
		snprintf(name, len, "%s_ccnt", counterName);
		setCounters(new PerfCounter(getCounters(), name, type, -1, PERF_SAMPLE_READ, 0));
	}

	for (int j = 0; j < numCounters; ++j) {
		len = snprintf(NULL, 0, "%s_cnt%d", counterName, j) + 1;
		name = new char[len];
		snprintf(name, len, "%s_cnt%d", counterName, j);
		setCounters(new PerfCounter(getCounters(), name, type, -1, PERF_SAMPLE_READ, 0));
	}
}

bool PerfDriver::setup() {
	// Check the kernel version
	int release[3];
	if (!getLinuxVersion(release)) {
		logg->logMessage("getLinuxVersion failed");
		return false;
	}

	if (KERNEL_VERSION(release[0], release[1], release[2]) < KERNEL_VERSION(3, 4, 0)) {
		logg->logMessage("Unsupported kernel version");
		return false;
	}
	mLegacySupport = KERNEL_VERSION(release[0], release[1], release[2]) < KERNEL_VERSION(3, 12, 0);

	if (access(EVENTS_PATH, R_OK) != 0) {
		logg->logMessage(EVENTS_PATH " does not exist, is CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?");
		return false;
	}

	// Add supported PMUs
	bool foundCpu = false;
	DIR *dir = opendir(PERF_DEVICES);
	if (dir == NULL) {
		logg->logMessage("opendir failed");
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
			addUncoreCounters(uncore_counters[i].gatorName, type, uncore_counters[i].count, uncore_counters[i].hasCyclesCounter);
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

	if (!foundCpu) {
		// If all else fails, use the ARM architected counters
		logg->logMessage("Using Other cpu");
		addCpuCounters("Other", PERF_TYPE_RAW, 6);
	}

	// Add supported software counters
	long long id;
	DynBuf printb;

	id = getTracepointId("irq/softirq_exit", &printb);
	if (id >= 0) {
		setCounters(new PerfCounter(getCounters(), "Linux_irq_softirq", PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU));
	}

	id = getTracepointId("irq/irq_handler_exit", &printb);
	if (id >= 0) {
		setCounters(new PerfCounter(getCounters(), "Linux_irq_irq", PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU));
	}

	id = getTracepointId(SCHED_SWITCH, &printb);
	if (id >= 0) {
		setCounters(new PerfCounter(getCounters(), "Linux_sched_switch", PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU));
	}

	id = getTracepointId(CPU_FREQUENCY, &printb);
	if (id >= 0) {
		setCounters(new CPUFreqDriver(getCounters(), id));
	}

	setCounters(new PerfCounter(getCounters(), "Linux_cpu_wait_contention", TYPE_DERIVED, -1, 0, 0));

	//Linux_cpu_wait_io

	mIsSetup = true;
	return true;
}

bool PerfDriver::summary(Buffer *const buffer) {
	struct utsname utsname;
	if (uname(&utsname) != 0) {
		logg->logMessage("uname failed");
		return false;
	}

	char buf[512];
	snprintf(buf, sizeof(buf), "%s %s %s %s %s GNU/Linux", utsname.sysname, utsname.nodename, utsname.release, utsname.version, utsname.machine);

	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		logg->logMessage("clock_gettime failed");
		return false;
	}
	const int64_t timestamp = (int64_t)ts.tv_sec * NS_PER_S + ts.tv_nsec;

	const uint64_t monotonicStarted = getTime();
	gSessionData->mMonotonicStarted = monotonicStarted;
	const uint64_t currTime = 0;//getTime() - gSessionData->mMonotonicStarted;

	buffer->summary(currTime, timestamp, monotonicStarted, monotonicStarted, buf);

	for (int i = 0; i < gSessionData->mCores; ++i) {
		coreName(currTime, buffer, i);
	}
	buffer->commit(currTime);

	return true;
}

void PerfDriver::coreName(const uint64_t currTime, Buffer *const buffer, const int cpu) {
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
	if (j < ARRAY_LENGTH(gator_cpus) && gator_cpus[j].cpuid == gSessionData->mCpuIds[cpu]) {
		buffer->coreName(currTime, cpu, gSessionData->mCpuIds[cpu], gator_cpus[j].core_name);
	} else {
		char buf[32];
		if (gSessionData->mCpuIds[cpu] == -1) {
			snprintf(buf, sizeof(buf), "Unknown");
		} else {
			snprintf(buf, sizeof(buf), "Unknown (0x%.3x)", gSessionData->mCpuIds[cpu]);
		}
		buffer->coreName(currTime, cpu, gSessionData->mCpuIds[cpu], buf);
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
			int count = counter->getCount();
			uint64_t sampleType = counter->getSampleType();
			if (sampleType & PERF_SAMPLE_RAW) {
				// If raw is enabled, every sample is needed
				count = 1;
			}
			if (!group->add(currTime, buffer, counter->getKey(), counter->getType(), counter->getConfig(), count,
					// use getCount instead of count as EBS counters need TID and IP but RAW tracepoints don't
					(counter->getCount() > 0 ? PERF_SAMPLE_TID | PERF_SAMPLE_IP : 0) | sampleType,
					counter->getFlags())) {
				logg->logMessage("PerfGroup::add failed");
				return false;
			}
		}
	}

	return true;
}

void PerfDriver::read(Buffer *const buffer, const int cpu) {
	for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL; counter = static_cast<PerfCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->read(buffer, cpu);
	}
}

long long PerfDriver::getTracepointId(const char *const name, DynBuf *const printb) {
	if (!printb->printf(EVENTS_PATH "/%s/id", name)) {
		logg->logMessage("DynBuf::printf failed");
		return -1;
	}

	int64_t result;
	if (DriverSource::readInt64Driver(printb->getBuf(), &result) != 0) {
		logg->logMessage("DriverSource::readInt64Driver failed");
		return -1;
	}

	return result;
}
