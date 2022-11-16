/* SPDX-License-Identifier: GPL-2.0-only or BSD-2-Clause */
/*
 * This header provides constants for most AHCI bindings.
 */

#ifndef _DT_BINDINGS_ATA_AHCI_H
#define _DT_BINDINGS_ATA_AHCI_H

/* Host Bus Adapter generic platform capabilities */
#define HBA_SSS		(1 << 27)
#define HBA_SMPS	(1 << 28)

/* Host Bus Adapter port-specific platform capabilities */
#define HBA_PORT_HPCP	(1 << 18)
#define HBA_PORT_MPSP	(1 << 19)
#define HBA_PORT_CPD	(1 << 20)
#define HBA_PORT_ESP	(1 << 21)
#define HBA_PORT_FBSCP	(1 << 22)

#endif
