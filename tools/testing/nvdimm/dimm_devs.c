// SPDX-License-Identifier: GPL-2.0
/* Copyright Intel Corp. 2018 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/nd.h>
#include "pmem.h"
#include "pfn.h"
#include "nd.h"
#include "nd-core.h"

ssize_t security_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	/*
	 * For the test version we need to poll the "hardware" in order
	 * to get the updated status for unlock testing.
	 */
	nvdimm->sec.state = nvdimm_security_state(nvdimm, NVDIMM_USER);
	nvdimm->sec.ext_state = nvdimm_security_state(nvdimm, NVDIMM_MASTER);

	switch (nvdimm->sec.state) {
	case NVDIMM_SECURITY_DISABLED:
		return sprintf(buf, "disabled\n");
	case NVDIMM_SECURITY_UNLOCKED:
		return sprintf(buf, "unlocked\n");
	case NVDIMM_SECURITY_LOCKED:
		return sprintf(buf, "locked\n");
	case NVDIMM_SECURITY_FROZEN:
		return sprintf(buf, "frozen\n");
	case NVDIMM_SECURITY_OVERWRITE:
		return sprintf(buf, "overwrite\n");
	default:
		return -ENOTTY;
	}

	return -ENOTTY;
}

