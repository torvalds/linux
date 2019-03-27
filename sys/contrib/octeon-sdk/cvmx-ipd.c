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
 * IPD Support.
 *
 * <hr>$Revision: 58943 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-bootmem.h>
#include <asm/octeon/cvmx-pip-defs.h>
#include <asm/octeon/cvmx-dbg-defs.h>
#include <asm/octeon/cvmx-sso-defs.h>

#include <asm/octeon/cvmx-fpa.h>
#include <asm/octeon/cvmx-wqe.h>
#include <asm/octeon/cvmx-ipd.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-helper-errata.h>
#include <asm/octeon/cvmx-helper-cfg.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#endif
#include "cvmx.h"
#include "cvmx-sysinfo.h"
#include "cvmx-bootmem.h"
#include "cvmx-version.h"
#include "cvmx-helper-check-defines.h"
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "cvmx-error.h"
#include "cvmx-config.h"
#endif

#include "cvmx-fpa.h"
#include "cvmx-wqe.h"
#include "cvmx-ipd.h"
#include "cvmx-helper-errata.h"
#include "cvmx-helper-cfg.h"
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS
static void __cvmx_ipd_free_ptr_v1(void)
{
    /* Only CN38XXp{1,2} cannot read pointer out of the IPD */
    if (!OCTEON_IS_MODEL(OCTEON_CN38XX_PASS2)) {
	int no_wptr = 0;
	cvmx_ipd_ptr_count_t ipd_ptr_count;
	ipd_ptr_count.u64 = cvmx_read_csr(CVMX_IPD_PTR_COUNT);

	/* Handle Work Queue Entry in cn56xx and cn52xx */
	if (octeon_has_feature(OCTEON_FEATURE_NO_WPTR)) {
	    cvmx_ipd_ctl_status_t ipd_ctl_status;
	    ipd_ctl_status.u64 = cvmx_read_csr(CVMX_IPD_CTL_STATUS);
	    if (ipd_ctl_status.s.no_wptr)
		no_wptr = 1;
	}

	/* Free the prefetched WQE */
	if (ipd_ptr_count.s.wqev_cnt) {
	    cvmx_ipd_wqe_ptr_valid_t ipd_wqe_ptr_valid;
	    ipd_wqe_ptr_valid.u64 = cvmx_read_csr(CVMX_IPD_WQE_PTR_VALID);
	    if (no_wptr)
	        cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_wqe_ptr_valid.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
	    else
	        cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_wqe_ptr_valid.s.ptr<<7), CVMX_FPA_WQE_POOL, 0);
	}

	/* Free all WQE in the fifo */
	if (ipd_ptr_count.s.wqe_pcnt) {
	    int i;
	    cvmx_ipd_pwp_ptr_fifo_ctl_t ipd_pwp_ptr_fifo_ctl;
	    ipd_pwp_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PWP_PTR_FIFO_CTL);
	    for (i = 0; i < ipd_ptr_count.s.wqe_pcnt; i++) {
		ipd_pwp_ptr_fifo_ctl.s.cena = 0;
		ipd_pwp_ptr_fifo_ctl.s.raddr = ipd_pwp_ptr_fifo_ctl.s.max_cnts + (ipd_pwp_ptr_fifo_ctl.s.wraddr+i) % ipd_pwp_ptr_fifo_ctl.s.max_cnts;
		cvmx_write_csr(CVMX_IPD_PWP_PTR_FIFO_CTL, ipd_pwp_ptr_fifo_ctl.u64);
		ipd_pwp_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PWP_PTR_FIFO_CTL);
		if (no_wptr)
		    cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_pwp_ptr_fifo_ctl.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
		else
		    cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_pwp_ptr_fifo_ctl.s.ptr<<7), CVMX_FPA_WQE_POOL, 0);
	    }
	    ipd_pwp_ptr_fifo_ctl.s.cena = 1;
	    cvmx_write_csr(CVMX_IPD_PWP_PTR_FIFO_CTL, ipd_pwp_ptr_fifo_ctl.u64);
	}

	/* Free the prefetched packet */
	if (ipd_ptr_count.s.pktv_cnt) {
	    cvmx_ipd_pkt_ptr_valid_t ipd_pkt_ptr_valid;
	    ipd_pkt_ptr_valid.u64 = cvmx_read_csr(CVMX_IPD_PKT_PTR_VALID);
	    cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_pkt_ptr_valid.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
	}

	/* Free the per port prefetched packets */
	if (1) {
	    int i;
	    cvmx_ipd_prc_port_ptr_fifo_ctl_t ipd_prc_port_ptr_fifo_ctl;
	    ipd_prc_port_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PRC_PORT_PTR_FIFO_CTL);

	    for (i = 0; i < ipd_prc_port_ptr_fifo_ctl.s.max_pkt; i++) {
		ipd_prc_port_ptr_fifo_ctl.s.cena = 0;
		ipd_prc_port_ptr_fifo_ctl.s.raddr = i % ipd_prc_port_ptr_fifo_ctl.s.max_pkt;
		cvmx_write_csr(CVMX_IPD_PRC_PORT_PTR_FIFO_CTL, ipd_prc_port_ptr_fifo_ctl.u64);
		ipd_prc_port_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PRC_PORT_PTR_FIFO_CTL);
		cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_prc_port_ptr_fifo_ctl.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
	    }
	    ipd_prc_port_ptr_fifo_ctl.s.cena = 1;
	    cvmx_write_csr(CVMX_IPD_PRC_PORT_PTR_FIFO_CTL, ipd_prc_port_ptr_fifo_ctl.u64);
	}

	/* Free all packets in the holding fifo */
	if (ipd_ptr_count.s.pfif_cnt) {
	    int i;
	    cvmx_ipd_prc_hold_ptr_fifo_ctl_t ipd_prc_hold_ptr_fifo_ctl;

	    ipd_prc_hold_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL);

	    for (i = 0; i < ipd_ptr_count.s.pfif_cnt; i++) {
		ipd_prc_hold_ptr_fifo_ctl.s.cena = 0;
		ipd_prc_hold_ptr_fifo_ctl.s.raddr = (ipd_prc_hold_ptr_fifo_ctl.s.praddr + i) % ipd_prc_hold_ptr_fifo_ctl.s.max_pkt;
		cvmx_write_csr(CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL, ipd_prc_hold_ptr_fifo_ctl.u64);
		ipd_prc_hold_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL);
		cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_prc_hold_ptr_fifo_ctl.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
	    }
	    ipd_prc_hold_ptr_fifo_ctl.s.cena = 1;
	    cvmx_write_csr(CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL, ipd_prc_hold_ptr_fifo_ctl.u64);
	}

	/* Free all packets in the fifo */
	if (ipd_ptr_count.s.pkt_pcnt) {
	    int i;
	    cvmx_ipd_pwp_ptr_fifo_ctl_t ipd_pwp_ptr_fifo_ctl;
	    ipd_pwp_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PWP_PTR_FIFO_CTL);

	    for (i = 0; i < ipd_ptr_count.s.pkt_pcnt; i++) {
		ipd_pwp_ptr_fifo_ctl.s.cena = 0;
		ipd_pwp_ptr_fifo_ctl.s.raddr = (ipd_pwp_ptr_fifo_ctl.s.praddr+i) % ipd_pwp_ptr_fifo_ctl.s.max_cnts;
		cvmx_write_csr(CVMX_IPD_PWP_PTR_FIFO_CTL, ipd_pwp_ptr_fifo_ctl.u64);
		ipd_pwp_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PWP_PTR_FIFO_CTL);
		cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_pwp_ptr_fifo_ctl.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
	    }
	    ipd_pwp_ptr_fifo_ctl.s.cena = 1;
	    cvmx_write_csr(CVMX_IPD_PWP_PTR_FIFO_CTL, ipd_pwp_ptr_fifo_ctl.u64);
	}
    }
}

static void __cvmx_ipd_free_ptr_v2(void)
{
    int no_wptr = 0;
    int i;
    cvmx_ipd_port_ptr_fifo_ctl_t ipd_port_ptr_fifo_ctl;
    cvmx_ipd_ptr_count_t ipd_ptr_count;
    ipd_ptr_count.u64 = cvmx_read_csr(CVMX_IPD_PTR_COUNT);

    /* Handle Work Queue Entry in cn68xx */
    if (octeon_has_feature(OCTEON_FEATURE_NO_WPTR)) {
        cvmx_ipd_ctl_status_t ipd_ctl_status;
        ipd_ctl_status.u64 = cvmx_read_csr(CVMX_IPD_CTL_STATUS);
        if (ipd_ctl_status.s.no_wptr)
            no_wptr = 1;
    }

    /* Free the prefetched WQE */
    if (ipd_ptr_count.s.wqev_cnt) {
        cvmx_ipd_next_wqe_ptr_t ipd_next_wqe_ptr;
        ipd_next_wqe_ptr.u64 = cvmx_read_csr(CVMX_IPD_NEXT_WQE_PTR);
        if (no_wptr)
            cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_next_wqe_ptr.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
        else
            cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_next_wqe_ptr.s.ptr<<7), CVMX_FPA_WQE_POOL, 0);
    }


    /* Free all WQE in the fifo */
    if (ipd_ptr_count.s.wqe_pcnt) {
        cvmx_ipd_free_ptr_fifo_ctl_t ipd_free_ptr_fifo_ctl;
        cvmx_ipd_free_ptr_value_t ipd_free_ptr_value;
        ipd_free_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_FREE_PTR_FIFO_CTL);
        for (i = 0; i < ipd_ptr_count.s.wqe_pcnt; i++) {
            ipd_free_ptr_fifo_ctl.s.cena = 0;
            ipd_free_ptr_fifo_ctl.s.raddr = ipd_free_ptr_fifo_ctl.s.max_cnts + (ipd_free_ptr_fifo_ctl.s.wraddr+i) % ipd_free_ptr_fifo_ctl.s.max_cnts;
            cvmx_write_csr(CVMX_IPD_FREE_PTR_FIFO_CTL, ipd_free_ptr_fifo_ctl.u64);
            ipd_free_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_FREE_PTR_FIFO_CTL);
            ipd_free_ptr_value.u64 = cvmx_read_csr(CVMX_IPD_FREE_PTR_VALUE);
            if (no_wptr)
                cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_free_ptr_value.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
            else
                cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_free_ptr_value.s.ptr<<7), CVMX_FPA_WQE_POOL, 0);
        }
        ipd_free_ptr_fifo_ctl.s.cena = 1;
        cvmx_write_csr(CVMX_IPD_FREE_PTR_FIFO_CTL, ipd_free_ptr_fifo_ctl.u64);
    }

    /* Free the prefetched packet */
    if (ipd_ptr_count.s.pktv_cnt) {
        cvmx_ipd_next_pkt_ptr_t ipd_next_pkt_ptr;
        ipd_next_pkt_ptr.u64 = cvmx_read_csr(CVMX_IPD_NEXT_PKT_PTR);
        cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_next_pkt_ptr.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
    }

    /* Free the per port prefetched packets */
    ipd_port_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PORT_PTR_FIFO_CTL);

    for (i = 0; i < ipd_port_ptr_fifo_ctl.s.max_pkt; i++) {
        ipd_port_ptr_fifo_ctl.s.cena = 0;
        ipd_port_ptr_fifo_ctl.s.raddr = i % ipd_port_ptr_fifo_ctl.s.max_pkt;
        cvmx_write_csr(CVMX_IPD_PORT_PTR_FIFO_CTL, ipd_port_ptr_fifo_ctl.u64);
        ipd_port_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_PORT_PTR_FIFO_CTL);
        cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_port_ptr_fifo_ctl.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
    }
    ipd_port_ptr_fifo_ctl.s.cena = 1;
    cvmx_write_csr(CVMX_IPD_PORT_PTR_FIFO_CTL, ipd_port_ptr_fifo_ctl.u64);

    /* Free all packets in the holding fifo */
    if (ipd_ptr_count.s.pfif_cnt) {
        cvmx_ipd_hold_ptr_fifo_ctl_t ipd_hold_ptr_fifo_ctl;

        ipd_hold_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_HOLD_PTR_FIFO_CTL);

        for (i = 0; i < ipd_ptr_count.s.pfif_cnt; i++) {
            ipd_hold_ptr_fifo_ctl.s.cena = 0;
            ipd_hold_ptr_fifo_ctl.s.raddr = (ipd_hold_ptr_fifo_ctl.s.praddr + i) % ipd_hold_ptr_fifo_ctl.s.max_pkt;
            cvmx_write_csr(CVMX_IPD_HOLD_PTR_FIFO_CTL, ipd_hold_ptr_fifo_ctl.u64);
            ipd_hold_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_HOLD_PTR_FIFO_CTL);
            cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_hold_ptr_fifo_ctl.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
        }
        ipd_hold_ptr_fifo_ctl.s.cena = 1;
        cvmx_write_csr(CVMX_IPD_HOLD_PTR_FIFO_CTL, ipd_hold_ptr_fifo_ctl.u64);
    }

    /* Free all packets in the fifo */
    if (ipd_ptr_count.s.pkt_pcnt) {
        cvmx_ipd_free_ptr_fifo_ctl_t ipd_free_ptr_fifo_ctl;
        cvmx_ipd_free_ptr_value_t ipd_free_ptr_value;
        ipd_free_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_FREE_PTR_FIFO_CTL);

        for (i = 0; i < ipd_ptr_count.s.pkt_pcnt; i++) {
            ipd_free_ptr_fifo_ctl.s.cena = 0;
            ipd_free_ptr_fifo_ctl.s.raddr = (ipd_free_ptr_fifo_ctl.s.praddr+i) % ipd_free_ptr_fifo_ctl.s.max_cnts;
            cvmx_write_csr(CVMX_IPD_FREE_PTR_FIFO_CTL, ipd_free_ptr_fifo_ctl.u64);
            ipd_free_ptr_fifo_ctl.u64 = cvmx_read_csr(CVMX_IPD_FREE_PTR_FIFO_CTL);
            ipd_free_ptr_value.u64 = cvmx_read_csr(CVMX_IPD_FREE_PTR_VALUE);
            cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)ipd_free_ptr_value.s.ptr<<7), CVMX_FPA_PACKET_POOL, 0);
        }
        ipd_free_ptr_fifo_ctl.s.cena = 1;
        cvmx_write_csr(CVMX_IPD_FREE_PTR_FIFO_CTL, ipd_free_ptr_fifo_ctl.u64);
    }
}

/**
 * @INTERNAL
 * This function is called by cvmx_helper_shutdown() to extract
 * all FPA buffers out of the IPD and PIP. After this function
 * completes, all FPA buffers that were prefetched by IPD and PIP
 * wil be in the apropriate FPA pool. This functions does not reset
 * PIP or IPD as FPA pool zero must be empty before the reset can
 * be performed. WARNING: It is very important that IPD and PIP be
 * reset soon after a call to this function.
 */
void __cvmx_ipd_free_ptr(void)
{
    if (octeon_has_feature(OCTEON_FEATURE_PKND))
        __cvmx_ipd_free_ptr_v2();
    else
        __cvmx_ipd_free_ptr_v1();
}

#endif

