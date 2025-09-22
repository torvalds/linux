// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2023 */

#include <linux/completion.h>

#include "afk.h"
#include "dcp.h"

static void disp_service_init(struct apple_epic_service *service, const char *name,
			const char *class, s64 unit)
{
}


static const struct apple_epic_service_ops ibootep_ops[] = {
	{
		.name = "disp0-service",
		.init = disp_service_init,
	},
	{}
};

int ibootep_init(struct apple_dcp *dcp)
{
	dcp->ibootep = afk_init(dcp, DISP0_ENDPOINT, ibootep_ops);
	afk_start(dcp->ibootep);

	return 0;
}
