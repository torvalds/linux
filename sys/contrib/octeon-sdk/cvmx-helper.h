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
 * Helper functions for common, but complicated tasks.
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_HELPER_H__
#define __CVMX_HELPER_H__

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#elif !defined(CVMX_BUILD_FOR_FREEBSD_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#endif

#include "cvmx-fpa.h"
#include "cvmx-wqe.h"

#ifdef  __cplusplus
extern "C" {
#endif

/* Max number of GMXX */
#define CVMX_HELPER_MAX_GMX             (OCTEON_IS_MODEL(OCTEON_CN68XX) ? 5 : 2)

#define CVMX_HELPER_CSR_INIT0           0       /* Do not change as 
                                                   CVMX_HELPER_WRITE_CSR()
                                                   assumes it */
#define CVMX_HELPER_CSR_INIT_READ       -1

/*
 * CVMX_HELPER_WRITE_CSR--set a field in a CSR with a value.
 *
 * @param chcsr_init    intial value of the csr (CVMX_HELPER_CSR_INIT_READ
 *                      means to use the existing csr value as the
 *                      initial value.)
 * @param chcsr_csr     the name of the csr
 * @param chcsr_type    the type of the csr (see the -defs.h)
 * @param chcsr_chip    the chip for the csr/field
 * @param chcsr_fld     the field in the csr
 * @param chcsr_val     the value for field
 */
#define CVMX_HELPER_WRITE_CSR(chcsr_init, chcsr_csr, chcsr_type,        \
    chcsr_chip, chcsr_fld, chcsr_val)                                   \
        do {                                                            \
                chcsr_type csr;                                         \
                if ((chcsr_init) == CVMX_HELPER_CSR_INIT_READ)          \
                        csr.u64 = cvmx_read_csr(chcsr_csr);             \
                else                                                    \
                        csr.u64 = (chcsr_init);                         \
                csr.chcsr_chip.chcsr_fld = (chcsr_val);                 \
                cvmx_write_csr((chcsr_csr), csr.u64);                   \
        } while(0)

/*
 * CVMX_HELPER_WRITE_CSR0--set a field in a CSR with the initial value of 0
 */
#define CVMX_HELPER_WRITE_CSR0(chcsr_csr, chcsr_type, chcsr_chip,       \
    chcsr_fld, chcsr_val)                                               \
        CVMX_HELPER_WRITE_CSR(CVMX_HELPER_CSR_INIT0, chcsr_csr,         \
            chcsr_type, chcsr_chip, chcsr_fld, chcsr_val)

/*
 * CVMX_HELPER_WRITE_CSR1--set a field in a CSR with the initial value of
 *                      the CSR's current value.
 */
#define CVMX_HELPER_WRITE_CSR1(chcsr_csr, chcsr_type, chcsr_chip,       \
    chcsr_fld, chcsr_val)                                               \
        CVMX_HELPER_WRITE_CSR(CVMX_HELPER_CSR_INIT_READ, chcsr_csr,     \
            chcsr_type, chcsr_chip, chcsr_fld, chcsr_val)


typedef enum
{
    CVMX_HELPER_INTERFACE_MODE_DISABLED,
    CVMX_HELPER_INTERFACE_MODE_RGMII,
    CVMX_HELPER_INTERFACE_MODE_GMII,
    CVMX_HELPER_INTERFACE_MODE_SPI,
    CVMX_HELPER_INTERFACE_MODE_PCIE,
    CVMX_HELPER_INTERFACE_MODE_XAUI,
    CVMX_HELPER_INTERFACE_MODE_SGMII,
    CVMX_HELPER_INTERFACE_MODE_PICMG,
    CVMX_HELPER_INTERFACE_MODE_NPI,
    CVMX_HELPER_INTERFACE_MODE_LOOP,
    CVMX_HELPER_INTERFACE_MODE_SRIO,
    CVMX_HELPER_INTERFACE_MODE_ILK,
    CVMX_HELPER_INTERFACE_MODE_RXAUI,
} cvmx_helper_interface_mode_t;

typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t    reserved_20_63  : 44;
        uint64_t    link_up         : 1;    /**< Is the physical link up? */
        uint64_t    full_duplex     : 1;    /**< 1 if the link is full duplex */
        uint64_t    speed           : 18;   /**< Speed of the link in Mbps */
    } s;
} cvmx_helper_link_info_t;

#include "cvmx-helper-fpa.h"

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

#include "cvmx-helper-errata.h"
#include "cvmx-helper-ilk.h"
#include "cvmx-helper-loop.h"
#include "cvmx-helper-npi.h"
#include "cvmx-helper-rgmii.h"
#include "cvmx-helper-sgmii.h"
#include "cvmx-helper-spi.h"
#include "cvmx-helper-srio.h"
#include "cvmx-helper-xaui.h"

/**
 * cvmx_override_pko_queue_priority(int ipd_port, uint64_t
 * priorities[16]) is a function pointer. It is meant to allow
 * customization of the PKO queue priorities based on the port
 * number. Users should set this pointer to a function before
 * calling any cvmx-helper operations.
 */
extern CVMX_SHARED void (*cvmx_override_pko_queue_priority)(int ipd_port, uint64_t *priorities);

/**
 * cvmx_override_ipd_port_setup(int ipd_port) is a function
 * pointer. It is meant to allow customization of the IPD port/port kind
 * setup before packet input/output comes online. It is called
 * after cvmx-helper does the default IPD configuration, but
 * before IPD is enabled. Users should set this pointer to a
 * function before calling any cvmx-helper operations.
 */
extern CVMX_SHARED void (*cvmx_override_ipd_port_setup)(int ipd_port);

/**
 * This function enables the IPD and also enables the packet interfaces.
 * The packet interfaces (RGMII and SPI) must be enabled after the
 * IPD.  This should be called by the user program after any additional
 * IPD configuration changes are made if CVMX_HELPER_ENABLE_IPD
 * is not set in the executive-config.h file.
 *
 * @return 0 on success
 *         -1 on failure
 */
extern int cvmx_helper_ipd_and_packet_input_enable(void);

/**
 * Initialize and allocate memory for the SSO.
 *
 * @param wqe_entries The maximum number of work queue entries to be
 * supported.
 *
 * @return Zero on success, non-zero on failure.
 */
extern int cvmx_helper_initialize_sso(int wqe_entries);

/**
 * Undo the effect of cvmx_helper_initialize_sso().
 *
 * Warning: since cvmx_bootmem_alloc() memory cannot be freed, the
 * memory allocated by cvmx_helper_initialize_sso() will be leaked.
 *
 * @return Zero on success, non-zero on failure.
 */
extern int cvmx_helper_uninitialize_sso(void);

/**
 * Initialize the PIP, IPD, and PKO hardware to support
 * simple priority based queues for the ethernet ports. Each
 * port is configured with a number of priority queues based
 * on CVMX_PKO_QUEUES_PER_PORT_* where each queue is lower
 * priority than the previous.
 *
 * @return Zero on success, non-zero on failure
 */
extern int cvmx_helper_initialize_packet_io_global(void);

/**
 * Does core local initialization for packet io
 *
 * @return Zero on success, non-zero on failure
 */
extern int cvmx_helper_initialize_packet_io_local(void);

/**
 * Undo the initialization performed in
 * cvmx_helper_initialize_packet_io_global(). After calling this routine and the
 * local version on each core, packet IO for Octeon will be disabled and placed
 * in the initial reset state. It will then be safe to call the initialize
 * later on. Note that this routine does not empty the FPA pools. It frees all
 * buffers used by the packet IO hardware to the FPA so a function emptying the
 * FPA after shutdown should find all packet buffers in the FPA.
 *
 * @return Zero on success, negative on failure.
 */
extern int cvmx_helper_shutdown_packet_io_global(void);

/**
 * Does core local shutdown of packet io
 *
 * @return Zero on success, non-zero on failure
 */
extern int cvmx_helper_shutdown_packet_io_local(void);

/**
 * Returns the number of ports on the given interface.
 * The interface must be initialized before the port count
 * can be returned.
 *
 * @param interface Which interface to return port count for.
 *
 * @return Port count for interface
 *         -1 for uninitialized interface
 */
extern int cvmx_helper_ports_on_interface(int interface);

/**
 * Return the number of interfaces the chip has. Each interface
 * may have multiple ports. Most chips support two interfaces,
 * but the CNX0XX and CNX1XX are exceptions. These only support
 * one interface.
 *
 * @return Number of interfaces on chip
 */
extern int cvmx_helper_get_number_of_interfaces(void);

/**
 * Get the operating mode of an interface. Depending on the Octeon
 * chip and configuration, this function returns an enumeration
 * of the type of packet I/O supported by an interface.
 *
 * @param interface Interface to probe
 *
 * @return Mode of the interface. Unknown or unsupported interfaces return
 *         DISABLED.
 */
extern cvmx_helper_interface_mode_t cvmx_helper_interface_get_mode(int interface);

/**
 * Auto configure an IPD/PKO port link state and speed. This
 * function basically does the equivalent of:
 * cvmx_helper_link_set(ipd_port, cvmx_helper_link_get(ipd_port));
 *
 * @param ipd_port IPD/PKO port to auto configure
 *
 * @return Link state after configure
 */
extern cvmx_helper_link_info_t cvmx_helper_link_autoconf(int ipd_port);

/**
 * Return the link state of an IPD/PKO port as returned by
 * auto negotiation. The result of this function may not match
 * Octeon's link config if auto negotiation has changed since
 * the last call to cvmx_helper_link_set().
 *
 * @param ipd_port IPD/PKO port to query
 *
 * @return Link state
 */
extern cvmx_helper_link_info_t cvmx_helper_link_get(int ipd_port);

/**
 * Configure an IPD/PKO port for the specified link state. This
 * function does not influence auto negotiation at the PHY level.
 * The passed link state must always match the link state returned
 * by cvmx_helper_link_get(). It is normally best to use
 * cvmx_helper_link_autoconf() instead.
 *
 * @param ipd_port  IPD/PKO port to configure
 * @param link_info The new link state
 *
 * @return Zero on success, negative on failure
 */
extern int cvmx_helper_link_set(int ipd_port, cvmx_helper_link_info_t link_info);



/**
 * This function probes an interface to determine the actual number of
 * hardware ports connected to it. It does some setup the ports but
 * doesn't enable them. The main goal here is to set the global
 * interface_port_count[interface] correctly. Final hardware setup of
 * the ports will be performed later.
 *
 * @param interface Interface to probe
 *
 * @return Zero on success, negative on failure
 */
extern int cvmx_helper_interface_probe(int interface);

/**
 * Determine the actual number of hardware ports connected to an
 * interface. It doesn't setup the ports or enable them.
 *
 * @param interface Interface to enumerate
 *
 * @return Zero on success, negative on failure
 */
extern int cvmx_helper_interface_enumerate(int interface);

/**
 * Configure a port for internal and/or external loopback. Internal loopback
 * causes packets sent by the port to be received by Octeon. External loopback
 * causes packets received from the wire to sent out again.
 *
 * @param ipd_port IPD/PKO port to loopback.
 * @param enable_internal
 *                 Non zero if you want internal loopback
 * @param enable_external
 *                 Non zero if you want external loopback
 *
 * @return Zero on success, negative on failure.
 */
extern int cvmx_helper_configure_loopback(int ipd_port, int enable_internal, int enable_external);

#include "cvmx-helper-util.h"

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */

#ifdef  __cplusplus
}
#endif

#endif  /* __CVMX_HELPER_H__ */
