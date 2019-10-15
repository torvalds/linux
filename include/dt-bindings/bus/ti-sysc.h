/* SPDX-License-Identifier: GPL-2.0 */
/* TI sysc interconnect target module defines */

/* Generic sysc found on omap2 and later, also known as type1 */
#define SYSC_OMAP2_CLOCKACTIVITY	(3 << 8)
#define SYSC_OMAP2_EMUFREE		(1 << 5)
#define SYSC_OMAP2_ENAWAKEUP		(1 << 2)
#define SYSC_OMAP2_SOFTRESET		(1 << 1)
#define SYSC_OMAP2_AUTOIDLE		(1 << 0)

/* Generic sysc found on omap4 and later, also known as type2 */
#define SYSC_OMAP4_DMADISABLE		(1 << 16)
#define SYSC_OMAP4_FREEEMU		(1 << 1)	/* Also known as EMUFREE */
#define SYSC_OMAP4_SOFTRESET		(1 << 0)

/* SmartReflex sysc found on 36xx and later */
#define SYSC_OMAP3_SR_ENAWAKEUP		(1 << 26)

#define SYSC_DRA7_MCAN_ENAWAKEUP	(1 << 4)

/* SYSCONFIG STANDBYMODE/MIDLEMODE/SIDLEMODE supported by hardware */
#define SYSC_IDLE_FORCE			0
#define SYSC_IDLE_NO			1
#define SYSC_IDLE_SMART			2
#define SYSC_IDLE_SMART_WKUP		3
