/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Channel numbers used by the microchip-tcb-capture driver
 * Copyright (C) 2025 Bence Csókás
 */
#ifndef _UAPI_COUNTER_MCHP_TCB_H_
#define _UAPI_COUNTER_MCHP_TCB_H_

/*
 * The driver defines the following components:
 *
 * Count 0
 * \__  Synapse 0 -- Signal 0 (Channel A, i.e. TIOA)
 * \__  Synapse 1 -- Signal 1 (Channel B, i.e. TIOB)
 * \__  Extension capture0    (RA register)
 * \__  Extension capture1    (RB register)
 *
 * It also supports the following events:
 *
 * Channel 0:
 * - CV register changed
 * - CV overflowed
 * - RA captured
 * Channel 1:
 * - RB captured
 * Channel 2:
 * - RC compare triggered
 */

/* Capture extensions */
#define COUNTER_MCHP_EXCAP_RA 0
#define COUNTER_MCHP_EXCAP_RB 1

/* Event channels */
#define COUNTER_MCHP_EVCHN_CV 0
#define COUNTER_MCHP_EVCHN_RA 0
#define COUNTER_MCHP_EVCHN_RB 1
#define COUNTER_MCHP_EVCHN_RC 2

#endif /* _UAPI_COUNTER_MCHP_TCB_H_ */
