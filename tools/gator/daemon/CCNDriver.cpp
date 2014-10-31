/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "CCNDriver.h"

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "k/perf_event.h"

#include "Config.h"
#include "DriverSource.h"
#include "Logging.h"

static const char TAG_CATEGORY[] = "category";
static const char TAG_COUNTER_SET[] = "counter_set";
static const char TAG_EVENT[] = "event";
static const char TAG_OPTION[] = "option";
static const char TAG_OPTION_SET[] = "option_set";

static const char ATTR_AVERAGE_SELECTION[] = "average_selection";
static const char ATTR_COUNTER[] = "counter";
static const char ATTR_COUNTER_SET[] = "counter_set";
static const char ATTR_COUNT[] = "count";
static const char ATTR_DESCRIPTION[] = "description";
static const char ATTR_DISPLAY[] = "display";
static const char ATTR_EVENT[] = "event";
static const char ATTR_EVENT_DELTA[] = "event_delta";
static const char ATTR_NAME[] = "name";
static const char ATTR_OPTION_SET[] = "option_set";
static const char ATTR_TITLE[] = "title";
static const char ATTR_UNITS[] = "units";

static const char XP_REGION[] = "XP_Region";
static const char HNF_REGION[] = "HN-F_Region";
static const char RNI_REGION[] = "RN-I_Region";
static const char SBAS_REGION[] = "SBAS_Region";
static const char CCN_5XX[] = "CCN-5xx";
#define ARM_CCN_5XX "ARM_CCN_5XX_"

static const char *const VC_TYPES[] = { "REQ", "RSP", "SNP", "DAT" };
static const char *const XP_EVENT_NAMES[] = { NULL, "H-bit", "S-bit", "P-Cnt", "TknV" };
static const char *const XP_EVENT_DESCRIPTIONS[] = { NULL, "Set H-bit, signaled when this XP sets the H-bit.", "Set S-bit, signaled when this XP sets the S-bit.", "Set P-Cnt, signaled when this XP sets the P-Cnt. This is not applicable for the SNP VC.", "No TknV, signaled when this XP transmits a valid packet." };
static const char *const HNF_EVENT_NAMES[] = { NULL, "Cache Miss", "L3 SF Cache Access", "Cache Fill", "POCQ Retry", "POCQ Reqs Recvd", "SF Hit", "SF Evictions", "Snoops Sent", "Snoops Broadcast", "L3 Eviction", "L3 Fill Invalid Way", "MC Retries", "MC Reqs", "QOS HH Retry" };
static const char *const HNF_EVENT_DESCRIPTIONS[] = { NULL, "Counts the total cache misses. This is the first time lookup result, and is high priority.", "Counts the number of cache accesses. This is the first time access, and is high priority.", "Counts the total allocations in the HN L3 cache, and all cache line allocations to the L3 cache.", "Counts the number of requests that have been retried.", "Counts the number of requests received by HN.", "Counts the number of snoop filter hits.", "Counts the number of snoop filter evictions. Cache invalidations are initiated.", "Counts the number of snoops sent. Does not differentiate between broadcast or directed snoops.", "Counts the number of snoop broadcasts sent.", "Counts the number of L3 evictions.", "Counts the number of L3 fills to an invalid way.", "Counts the number of transactions retried by the memory controller.", "Counts the number of requests to the memory controller.", "Counts the number of times a highest-priority QoS class was retried at the HN-F." };
static const char *const RNI_EVENT_NAMES[] = { NULL, "S0 RDataBeats", "S1 RDataBeats", "S2 RDataBeats", "RXDAT Flits received", "TXDAT Flits sent", "Total TXREQ Flits sent", "Retried TXREQ Flits sent", "RRT full", "WRT full", "Replayed TXREQ Flits" };
static const char *const RNI_EVENT_DESCRIPTIONS[] = { NULL, "S0 RDataBeats.", "S1 RDataBeats.", "S2 RDataBeats.", "RXDAT Flits received.", "TXDAT Flits sent.", "Total TXREQ Flits sent.", "Retried TXREQ Flits sent.", "RRT full.", "WRT full.", "Replayed TXREQ Flits." };
static const char *const SBAS_EVENT_NAMES[] = { NULL, "S0 RDataBeats", NULL, NULL, "RXDAT Flits received", "TXDAT Flits sent", "Total TXREQ Flits sent", "Retried TXREQ Flits sent", "RRT full", "WRT full", "Replayed TXREQ Flits" };
static const char *const SBAS_EVENT_DESCRIPTIONS[] = { NULL, "S0 RDataBeats.", NULL, NULL, "RXDAT Flits received.", "TXDAT Flits sent.", "Total TXREQ Flits sent.", "Retried TXREQ Flits sent.", "RRT full.", "WRT full.", "Replayed TXREQ Flits." };

// This class is used only to poll for CCN-5xx configuration and emit events XML for it. All other operations are handled by PerfDriver

static int sys_perf_event_open(struct perf_event_attr *const attr, const pid_t pid, const int cpu, const int group_fd, const unsigned long flags) {
	return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static unsigned int getConfig(unsigned int node, unsigned int type, unsigned int event, unsigned int port, unsigned int vc) {
  return
    ((node  & 0xff) <<  0) |
    ((type  & 0xff) <<  8) |
    ((event & 0xff) << 16) |
    ((port  & 0x03) << 24) |
    ((vc    & 0x07) << 26) |
    0;
}

static bool perfPoll(struct perf_event_attr *const pea) {
	int fd = sys_perf_event_open(pea, -1, 0, -1, 0);
	if (fd < 0) {
		return false;
	}
	close(fd);
	return true;
}

CCNDriver::CCNDriver() : mNodeTypes(NULL), mXpCount(0) {
}

CCNDriver::~CCNDriver() {
	delete mNodeTypes;
}

bool CCNDriver::claimCounter(const Counter &) const {
	// Handled by PerfDriver
	return false;
}

void CCNDriver::resetCounters() {
	// Handled by PerfDriver
}

void CCNDriver::setupCounter(Counter &) {
	// Handled by PerfDriver
}

void CCNDriver::readEvents(mxml_node_t *const) {
	struct stat st;
	if (stat("/sys/bus/event_source/devices/ccn", &st) != 0) {
		// Not found
		return;
	}

	int type;
	if (DriverSource::readIntDriver("/sys/bus/event_source/devices/ccn/type", &type) != 0) {
		logg->logError(__FILE__, __LINE__, "Unable to read CCN-5xx type");
		handleException();
	}

	// Detect number of xps
	struct perf_event_attr pea;
	memset(&pea, 0, sizeof(pea));
	pea.type = type;
	pea.size = sizeof(pea);

	mXpCount = 1;
	while (true) {
		pea.config = getConfig(0, 0x08, 1, 0, 1) | mXpCount;
		if (!perfPoll(&pea)) {
			break;
		}
		mXpCount *= 2;
	};
	{
		int lower = mXpCount/2 + 1;
		while (lower < mXpCount) {
			int mid = (lower + mXpCount)/2;
			pea.config = getConfig(0, 0x08, 1, 0, 1) | mid;
			if (perfPoll(&pea)) {
				lower = mid + 1;
			} else {
				mXpCount = mid;
			}
		}
	}

	mNodeTypes = new NodeType[2*mXpCount];

	// Detect node types
	for (int i = 0; i < 2*mXpCount; ++i) {
		pea.config = getConfig(0, 0x04, 1, 0, 0) | i;
		if (perfPoll(&pea)) {
			mNodeTypes[i] = NT_HNF;
			continue;
		}

		pea.config = getConfig(0, 0x16, 1, 0, 0) | i;
		if (perfPoll(&pea)) {
			mNodeTypes[i] = NT_RNI;
			continue;
		}

		pea.config = getConfig(0, 0x10, 1, 0, 0) | i;
		if (perfPoll(&pea)) {
			mNodeTypes[i] = NT_SBAS;
			continue;
		}

		mNodeTypes[i] = NT_UNKNOWN;
	}
}

int CCNDriver::writeCounters(mxml_node_t *const) const {
	// Handled by PerfDriver
	return 0;
}

void CCNDriver::writeEvents(mxml_node_t *const root) const {
	mxml_node_t *const counter_set = mxmlNewElement(root, TAG_COUNTER_SET);
	mxmlElementSetAttr(counter_set, ATTR_NAME, ARM_CCN_5XX "cnt");
	mxmlElementSetAttr(counter_set, ATTR_COUNT, "8");

	mxml_node_t *const category = mxmlNewElement(root, TAG_CATEGORY);
	mxmlElementSetAttr(category, ATTR_NAME, CCN_5XX);
	mxmlElementSetAttr(category, TAG_COUNTER_SET, ARM_CCN_5XX "cnt");

	mxml_node_t *const clock_event = mxmlNewElement(category, TAG_EVENT);
	mxmlElementSetAttr(clock_event, ATTR_COUNTER, ARM_CCN_5XX "ccnt");
	mxmlElementSetAttr(clock_event, ATTR_EVENT, "0xff00");
	mxmlElementSetAttr(clock_event, ATTR_TITLE, "CCN-5xx Clock");
	mxmlElementSetAttr(clock_event, ATTR_NAME, "Cycles");
	mxmlElementSetAttr(clock_event, ATTR_DISPLAY, "hertz");
	mxmlElementSetAttr(clock_event, ATTR_UNITS, "Hz");
	mxmlElementSetAttr(clock_event, ATTR_AVERAGE_SELECTION, "yes");
	mxmlElementSetAttr(clock_event, ATTR_DESCRIPTION, "The number of core clock cycles");

	mxml_node_t *const xp_option_set = mxmlNewElement(category, TAG_OPTION_SET);
	mxmlElementSetAttr(xp_option_set, ATTR_NAME, XP_REGION);

	for (int i = 0; i < mXpCount; ++i) {
		mxml_node_t *const option = mxmlNewElement(xp_option_set, TAG_OPTION);
		mxmlElementSetAttrf(option, ATTR_EVENT_DELTA, "0x%x", getConfig(i, 0, 0, 0, 0));
		mxmlElementSetAttrf(option, ATTR_NAME, "XP %i", i);
		mxmlElementSetAttrf(option, ATTR_DESCRIPTION, "Crosspoint %i", i);
	}

	for (int vc = 0; vc < ARRAY_LENGTH(VC_TYPES); ++vc) {
		if (VC_TYPES[vc] == NULL) {
			continue;
		}
		for (int bus = 0; bus < 2; ++bus) {
			for (int eventId = 0; eventId < ARRAY_LENGTH(XP_EVENT_NAMES); ++eventId) {
				if (XP_EVENT_NAMES[eventId] == NULL) {
					continue;
				}
				mxml_node_t *const event = mxmlNewElement(category, TAG_EVENT);
				mxmlElementSetAttrf(event, ATTR_EVENT, "0x%x", getConfig(0, 0x08, eventId, bus, vc));
				mxmlElementSetAttr(event, ATTR_OPTION_SET, XP_REGION);
				mxmlElementSetAttr(event, ATTR_TITLE, CCN_5XX);
				mxmlElementSetAttrf(event, ATTR_NAME, "Bus %i: %s: %s", bus, VC_TYPES[vc], XP_EVENT_NAMES[eventId]);
				mxmlElementSetAttrf(event, ATTR_DESCRIPTION, "Bus %i: %s: %s", bus, VC_TYPES[vc], XP_EVENT_DESCRIPTIONS[eventId]);
			}
		}
	}

	mxml_node_t *const hnf_option_set = mxmlNewElement(category, TAG_OPTION_SET);
	mxmlElementSetAttr(hnf_option_set, ATTR_NAME, HNF_REGION);

	for (int eventId = 0; eventId < ARRAY_LENGTH(HNF_EVENT_NAMES); ++eventId) {
		if (HNF_EVENT_NAMES[eventId] == NULL) {
			continue;
		}
		mxml_node_t *const event = mxmlNewElement(category, TAG_EVENT);
		mxmlElementSetAttrf(event, ATTR_EVENT, "0x%x", getConfig(0, 0x04, eventId, 0, 0));
		mxmlElementSetAttr(event, ATTR_OPTION_SET, HNF_REGION);
		mxmlElementSetAttr(event, ATTR_TITLE, CCN_5XX);
		mxmlElementSetAttr(event, ATTR_NAME, HNF_EVENT_NAMES[eventId]);
		mxmlElementSetAttr(event, ATTR_DESCRIPTION, HNF_EVENT_DESCRIPTIONS[eventId]);
	}

	mxml_node_t *const rni_option_set = mxmlNewElement(category, TAG_OPTION_SET);
	mxmlElementSetAttr(rni_option_set, ATTR_NAME, RNI_REGION);

	for (int eventId = 0; eventId < ARRAY_LENGTH(RNI_EVENT_NAMES); ++eventId) {
		if (RNI_EVENT_NAMES[eventId] == NULL) {
			continue;
		}
		mxml_node_t *const event = mxmlNewElement(category, TAG_EVENT);
		mxmlElementSetAttrf(event, ATTR_EVENT, "0x%x", getConfig(0, 0x16, eventId, 0, 0));
		mxmlElementSetAttr(event, ATTR_OPTION_SET, RNI_REGION);
		mxmlElementSetAttr(event, ATTR_TITLE, CCN_5XX);
		mxmlElementSetAttr(event, ATTR_NAME, RNI_EVENT_NAMES[eventId]);
		mxmlElementSetAttr(event, ATTR_DESCRIPTION, RNI_EVENT_DESCRIPTIONS[eventId]);
	}

	mxml_node_t *const sbas_option_set = mxmlNewElement(category, TAG_OPTION_SET);
	mxmlElementSetAttr(sbas_option_set, ATTR_NAME, SBAS_REGION);

	for (int eventId = 0; eventId < ARRAY_LENGTH(SBAS_EVENT_NAMES); ++eventId) {
		if (SBAS_EVENT_NAMES[eventId] == NULL) {
			continue;
		}
		mxml_node_t *const event = mxmlNewElement(category, TAG_EVENT);
		mxmlElementSetAttrf(event, ATTR_EVENT, "0x%x", getConfig(0, 0x10, eventId, 0, 0));
		mxmlElementSetAttr(event, ATTR_OPTION_SET, SBAS_REGION);
		mxmlElementSetAttr(event, ATTR_TITLE, CCN_5XX);
		mxmlElementSetAttr(event, ATTR_NAME, SBAS_EVENT_NAMES[eventId]);
		mxmlElementSetAttr(event, ATTR_DESCRIPTION, SBAS_EVENT_DESCRIPTIONS[eventId]);
	}

	for (int i = 0; i < 2*mXpCount; ++i) {
		switch (mNodeTypes[i]) {
		case NT_HNF: {
			mxml_node_t *const option = mxmlNewElement(hnf_option_set, TAG_OPTION);
			mxmlElementSetAttrf(option, ATTR_EVENT_DELTA, "0x%x", getConfig(i, 0, 0, 0, 0));
			mxmlElementSetAttrf(option, ATTR_NAME, "HN-F %i", i);
			mxmlElementSetAttrf(option, ATTR_DESCRIPTION, "Fully-coherent Home Node %i", i);
			break;
		}
		case NT_RNI: {
			mxml_node_t *const option = mxmlNewElement(rni_option_set, TAG_OPTION);
			mxmlElementSetAttrf(option, ATTR_EVENT_DELTA, "0x%x", getConfig(i, 0, 0, 0, 0));
			mxmlElementSetAttrf(option, ATTR_NAME, "RN-I %i", i);
			mxmlElementSetAttrf(option, ATTR_DESCRIPTION, "I/O-coherent Requesting Node %i", i);
			break;
		}
		case NT_SBAS: {
			mxml_node_t *const option = mxmlNewElement(sbas_option_set, TAG_OPTION);
			mxmlElementSetAttrf(option, ATTR_EVENT_DELTA, "0x%x", getConfig(i, 0, 0, 0, 0));
			mxmlElementSetAttrf(option, ATTR_NAME, "SBAS %i", i);
			mxmlElementSetAttrf(option, ATTR_DESCRIPTION, "ACE master to CHI protocol bridge %i", i);
			break;
		}
		default:
			continue;
		}
	}
}
