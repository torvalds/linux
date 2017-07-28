/*
 * This header provides constants for binding nvidia,tegra186-hsp.
 */

#ifndef _DT_BINDINGS_MAILBOX_TEGRA186_HSP_H
#define _DT_BINDINGS_MAILBOX_TEGRA186_HSP_H

/*
 * These define the type of mailbox that is to be used (doorbell, shared
 * mailbox, shared semaphore or arbitrated semaphore).
 */
#define TEGRA_HSP_MBOX_TYPE_DB 0x0
#define TEGRA_HSP_MBOX_TYPE_SM 0x1
#define TEGRA_HSP_MBOX_TYPE_SS 0x2
#define TEGRA_HSP_MBOX_TYPE_AS 0x3

/*
 * These defines represent the bit associated with the given master ID in the
 * doorbell registers.
 */
#define TEGRA_HSP_DB_MASTER_CCPLEX 17
#define TEGRA_HSP_DB_MASTER_BPMP 19

#endif
