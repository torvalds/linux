/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/

/**
 * @file
 *
 * Interface to power-throttle control, measurement, and debugging
 * facilities.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifndef __CVMX_POWER_THROTTLE_H__
#define __CVMX_POWER_THROTTLE_H__
#ifdef	__cplusplus
extern "C" {
#endif

enum cvmx_power_throttle_field_index {
    CVMX_PTH_INDEX_MAXPOW,
    CVMX_PTH_INDEX_POWER,
    CVMX_PTH_INDEX_THROTT,
    CVMX_PTH_INDEX_RESERVED,
    CVMX_PTH_INDEX_DISTAG,
    CVMX_PTH_INDEX_PERIOD,
    CVMX_PTH_INDEX_POWLIM,
    CVMX_PTH_INDEX_MAXTHR,
    CVMX_PTH_INDEX_MINTHR,
    CVMX_PTH_INDEX_HRMPOWADJ,
    CVMX_PTH_INDEX_OVRRD,
    CVMX_PTH_INDEX_MAX
};
typedef enum cvmx_power_throttle_field_index cvmx_power_throttle_field_index_t;

/**
 * Throttle power to percentage% of configured maximum (MAXPOW).
 *
 * @param percentage	0 to 100
 * @return 0 for success and -1 for error.
 */
extern int cvmx_power_throttle_self(uint8_t percentage);

/**
 * Throttle power to percentage% of configured maximum (MAXPOW)
 * for the cores identified in coremask.
 *
 * @param percentage 	0 to 100
 * @param coremask	bit mask where each bit identifies a core.
 * @return 0 for success and -1 for error.
 */
extern int cvmx_power_throttle(uint8_t percentage, uint64_t coremask);

/**
 * The same functionality as cvmx_power_throttle() but it takes a
 * bitmap-based coremask as a parameter.
 */
extern int cvmx_power_throttle_bmp(uint8_t percentage,
    struct cvmx_coremask *pcm);

/**
 * Get the i'th field of the power throttle register
 *
 * @param r is the value of the power throttle register
 * @param i is the index of the field
 *
 * @return (uint64_t)-1 on failure.
 */
extern uint64_t cvmx_power_throttle_get_field(uint64_t r,
    cvmx_power_throttle_field_index_t i);

/**
 * Retrieve the content of the power throttle register of a core
 *
 * @param ppid is the core id
 *
 * @return (uint64_t)-1 on failure.
 */
extern uint64_t cvmx_power_throttle_get_register(int ppid);

#ifdef	__cplusplus
}
#endif
#endif /* __CVMX_POWER_THROTTLE_H__ */
