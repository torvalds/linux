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
 * This file contains defines for the SPI interface

 * <hr>$Revision: 70030 $<hr>
 *
 *
 */
#ifndef __CVMX_SPI_H__
#define __CVMX_SPI_H__

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include "cvmx-gmxx-defs.h"
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* CSR typedefs have been moved to cvmx-spi-defs.h */

typedef enum
{
    CVMX_SPI_MODE_UNKNOWN = 0,
    CVMX_SPI_MODE_TX_HALFPLEX = 1,
    CVMX_SPI_MODE_RX_HALFPLEX = 2,
    CVMX_SPI_MODE_DUPLEX = 3
} cvmx_spi_mode_t;

/** Callbacks structure to customize SPI4 initialization sequence */
typedef struct
{
    /** Called to reset SPI4 DLL */
    int (*reset_cb)(int interface, cvmx_spi_mode_t mode);

    /** Called to setup calendar */
    int (*calendar_setup_cb)(int interface, cvmx_spi_mode_t mode, int num_ports);

    /** Called for Tx and Rx clock detection */
    int (*clock_detect_cb)(int interface, cvmx_spi_mode_t mode, int timeout);

    /** Called to perform link training */
    int (*training_cb)(int interface, cvmx_spi_mode_t mode, int timeout);

    /** Called for calendar data synchronization */
    int (*calendar_sync_cb)(int interface, cvmx_spi_mode_t mode, int timeout);

    /** Called when interface is up */
    int (*interface_up_cb)(int interface, cvmx_spi_mode_t mode);

} cvmx_spi_callbacks_t;


/**
 * Return true if the supplied interface is configured for SPI
 *
 * @param interface Interface to check
 * @return True if interface is SPI
 */
static inline int cvmx_spi_is_spi_interface(int interface)
{
    uint64_t gmxState = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));
    return ((gmxState & 0x2) && (gmxState & 0x1));
}

/**
 * Initialize and start the SPI interface.
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @param mode      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @param timeout   Timeout to wait for clock synchronization in seconds
 * @param num_ports Number of SPI ports to configure
 *
 * @return Zero on success, negative of failure.
 */
extern int cvmx_spi_start_interface(int interface, cvmx_spi_mode_t mode, int timeout, int num_ports);

/**
 * This routine restarts the SPI interface after it has lost synchronization
 * with its corespondant system.
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @param mode      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @param timeout   Timeout to wait for clock synchronization in seconds
 * @return Zero on success, negative of failure.
 */
extern int cvmx_spi_restart_interface(int interface, cvmx_spi_mode_t mode, int timeout);

/**
 * Return non-zero if the SPI interface has a SPI4000 attached
 *
 * @param interface SPI interface the SPI4000 is connected to
 *
 * @return
 */
extern int cvmx_spi4000_is_present(int interface);

/**
 * Initialize the SPI4000 for use
 *
 * @param interface SPI interface the SPI4000 is connected to
 */
extern int cvmx_spi4000_initialize(int interface);

/**
 * Poll all the SPI4000 port and check its speed
 *
 * @param interface Interface the SPI4000 is on
 * @param port      Port to poll (0-9)
 * @return Status of the port. 0=down. All other values the port is up.
 */
extern cvmx_gmxx_rxx_rx_inbnd_t cvmx_spi4000_check_speed(int interface, int port);

/**
 * Get current SPI4 initialization callbacks
 *
 * @param callbacks  Pointer to the callbacks structure.to fill
 *
 * @return Pointer to cvmx_spi_callbacks_t structure.
 */
extern void cvmx_spi_get_callbacks(cvmx_spi_callbacks_t * callbacks);

/**
 * Set new SPI4 initialization callbacks
 *
 * @param new_callbacks  Pointer to an updated callbacks structure.
 */
extern void cvmx_spi_set_callbacks(cvmx_spi_callbacks_t * new_callbacks);

/**
 * Callback to perform SPI4 reset
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @param mode      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @return Zero on success, non-zero error code on failure (will cause SPI initialization to abort)
 */
extern int cvmx_spi_reset_cb(int interface, cvmx_spi_mode_t mode);

/**
 * Callback to setup calendar and miscellaneous settings before clock detection
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @param mode      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @param num_ports Number of ports to configure on SPI
 *
 * @return Zero on success, non-zero error code on failure (will cause SPI initialization to abort)
 */
extern int cvmx_spi_calendar_setup_cb(int interface, cvmx_spi_mode_t mode, int num_ports);

/**
 * Callback to perform clock detection
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @param mode      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @param timeout   Timeout to wait for clock synchronization in seconds
 * @return Zero on success, non-zero error code on failure (will cause SPI initialization to abort)
 */
extern int cvmx_spi_clock_detect_cb(int interface, cvmx_spi_mode_t mode, int timeout);

/**
 * Callback to perform link training
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @param mode      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @param timeout   Timeout to wait for link to be trained (in seconds)
 * @return Zero on success, non-zero error code on failure (will cause SPI initialization to abort)
 */
extern int cvmx_spi_training_cb(int interface, cvmx_spi_mode_t mode, int timeout);

/**
 * Callback to perform calendar data synchronization
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @param mode      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @param timeout   Timeout to wait for calendar data in seconds
 * @return Zero on success, non-zero error code on failure (will cause SPI initialization to abort)
 */
extern int cvmx_spi_calendar_sync_cb(int interface, cvmx_spi_mode_t mode, int timeout);

/**
 * Callback to handle interface up
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @param mode      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @return Zero on success, non-zero error code on failure (will cause SPI initialization to abort)
 */
extern int cvmx_spi_interface_up_cb(int interface, cvmx_spi_mode_t mode);

#ifdef	__cplusplus
}
#endif

#endif  /* __CVMX_SPI_H__ */
