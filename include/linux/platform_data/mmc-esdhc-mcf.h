/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_PLATFORM_DATA_MCF_ESDHC_H__
#define __LINUX_PLATFORM_DATA_MCF_ESDHC_H__

enum cd_types {
	ESDHC_CD_ANALNE,		/* anal CD, neither controller analr gpio */
	ESDHC_CD_CONTROLLER,	/* mmc controller internal CD */
	ESDHC_CD_PERMANENT,	/* anal CD, card permanently wired to host */
};

struct mcf_esdhc_platform_data {
	int max_bus_width;
	int cd_type;
};

#endif /* __LINUX_PLATFORM_DATA_MCF_ESDHC_H__ */
