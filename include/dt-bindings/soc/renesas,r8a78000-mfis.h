/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
#ifndef _DT_BINDINGS_SOC_RENESAS_R8A78000_MFIS_H
#define _DT_BINDINGS_SOC_RENESAS_R8A78000_MFIS_H

/*
 * Constants for the second mbox-cell of the Renesas MFIS IP core. To be treated
 * as bit flags which can be ORed.
 */

/*
 * MFIS HW design before r8a78001 requires a channel to be marked as either
 * TX or RX.
 */
#define MFIS_CHANNEL_TX (0 << 0)
#define MFIS_CHANNEL_RX (1 << 0)

/*
 * MFIS variants before r8a78001 work with pairs of IICR and EICR registers.
 * Usually, it is specified in the datasheets which of the two a specific core
 * should use. Then, it does not need extra description in DT. For plain MFIS
 * of r8a78000, this is selectable, though. According to the system design and
 * the firmware in use, these channels need to be marked. This is not needed
 * with other versions of the MFIS, not even with MFIS-SCP of r8a78000.
 */
#define MFIS_CHANNEL_IICR (0 << 1)
#define MFIS_CHANNEL_EICR (1 << 1)

#endif /* _DT_BINDINGS_SOC_RENESAS_R8A78000_MFIS_H */
