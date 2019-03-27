/***********************license start***************
 * Copyright (c) 2011  Cavium Inc. (support@cavium.com). All rights
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
 * Helper utilities for qlm.
 *
 * <hr>$Revision: 70030 $<hr>
 */


#ifndef __CVMX_QLM_H__
#define __CVMX_QLM_H__

#include "cvmx.h"

typedef struct
{
    const char *name;
    int stop_bit;
    int start_bit;
} __cvmx_qlm_jtag_field_t;

/**
 * Return the number of QLMs supported by the chip
 * 
 * @return  Number of QLMs
 */
extern int cvmx_qlm_get_num(void);

/**
 * Return the qlm number based on the interface
 *
 * @param interface  Interface to look up
 */
extern int cvmx_qlm_interface(int interface);

/**
 * Return number of lanes for a given qlm
 * 
 * @return  Number of lanes
 */
extern int cvmx_qlm_get_lanes(int qlm);

/**
 * Get the QLM JTAG fields based on Octeon model on the supported chips. 
 *
 * @return  qlm_jtag_field_t structure
 */
extern const __cvmx_qlm_jtag_field_t *cvmx_qlm_jtag_get_field(void);

/**
 * Get the QLM JTAG length by going through qlm_jtag_field for each
 * Octeon model that is supported
 *
 * @return return the length.
 */
extern int cvmx_qlm_jtag_get_length(void);

/**
 * Initialize the QLM layer
 */
extern void cvmx_qlm_init(void);

/**
 * Get a field in a QLM JTAG chain
 *
 * @param qlm    QLM to get
 * @param lane   Lane in QLM to get
 * @param name   String name of field
 *
 * @return JTAG field value
 */
extern uint64_t cvmx_qlm_jtag_get(int qlm, int lane, const char *name);

/**
 * Set a field in a QLM JTAG chain
 *
 * @param qlm    QLM to set
 * @param lane   Lane in QLM to set, or -1 for all lanes
 * @param name   String name of field
 * @param value  Value of the field
 */
extern void cvmx_qlm_jtag_set(int qlm, int lane, const char *name, uint64_t value);

/**
 * Errata G-16094: QLM Gen2 Equalizer Default Setting Change.
 * CN68XX pass 1.x and CN66XX pass 1.x QLM tweak. This function tweaks the
 * JTAG setting for a QLMs to run better at 5 and 6.25Ghz.
 */
extern void __cvmx_qlm_speed_tweak(void);

/**
 * Errata G-16174: QLM Gen2 PCIe IDLE DAC change.
 * CN68XX pass 1.x, CN66XX pass 1.x and CN63XX pass 1.0-2.2 QLM tweak.
 * This function tweaks the JTAG setting for a QLMs for PCIe to run better.
 */
extern void __cvmx_qlm_pcie_idle_dac_tweak(void);

#ifndef CVMX_BUILD_FOR_LINUX_HOST
/**
 * Get the speed (Gbaud) of the QLM in Mhz.
 *
 * @param qlm    QLM to examine
 *
 * @return Speed in Mhz
 */
extern int cvmx_qlm_get_gbaud_mhz(int qlm);
#endif

/*
 * Read QLM and return status based on CN66XX.
 * @return  Return 1 if QLM is SGMII
 *                 2 if QLM is XAUI
 *                 3 if QLM is PCIe gen2 / gen1
 *                 4 if QLM is SRIO 1x4 short / long
 *                 5 if QLM is SRIO 2x2 short / long
 *                 6 is reserved
 *                 7 if QLM is PCIe 1x2 gen2 / gen1
 *                 8 if QLM is PCIe 2x1 gen2 / gen1
 *                 9 if QLM is ILK
 *                 10 if QLM is RXAUI
 *                 -1 otherwise
 */
extern int cvmx_qlm_get_status(int qlm);

#endif /* __CVMX_QLM_H__ */
