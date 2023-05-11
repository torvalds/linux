// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright IBM Corp. 2023
 * Author(s): Thomas Richter <tmricht@linux.ibm.com>
 */

#include <string.h>

#include "../../../util/pmu.h"

#define	S390_PMUPAI_CRYPTO	"pai_crypto"
#define	S390_PMUPAI_EXT		"pai_ext"
#define	S390_PMUCPUM_CF		"cpum_cf"

struct perf_event_attr *perf_pmu__get_default_config(struct perf_pmu *pmu)
{
	if (!strcmp(pmu->name, S390_PMUPAI_CRYPTO) ||
	    !strcmp(pmu->name, S390_PMUPAI_EXT) ||
	    !strcmp(pmu->name, S390_PMUCPUM_CF))
		pmu->selectable = true;
	return NULL;
}
