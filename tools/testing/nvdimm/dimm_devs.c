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
	nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);

	if (test_bit(NVDIMM_SECURITY_DISABLED, &nvdimm->sec.flags))
		return sprintf(buf, "disabled\n");
	if (test_bit(NVDIMM_SECURITY_UNLOCKED, &nvdimm->sec.flags))
		return sprintf(buf, "unlocked\n");
	if (test_bit(NVDIMM_SECURITY_LOCKED, &nvdimm->sec.flags))
		return sprintf(buf, "locked\n");
	return -ENOTTY;
}
