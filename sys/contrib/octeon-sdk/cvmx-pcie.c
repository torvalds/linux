/***********************license start***************
 * Copyright (c) 2003-2011  Cavium, Inc. <support@cavium.com>.  All rights
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
 * Interface to PCIe as a host(RC) or target(EP)
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-ciu-defs.h>
#include <asm/octeon/cvmx-dpi-defs.h>
#include <asm/octeon/cvmx-mio-defs.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-npei-defs.h>
#include <asm/octeon/cvmx-pci-defs.h>
#include <asm/octeon/cvmx-pcieepx-defs.h>
#include <asm/octeon/cvmx-pciercx-defs.h>
#include <asm/octeon/cvmx-pemx-defs.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/cvmx-pescx-defs.h>
#include <asm/octeon/cvmx-sli-defs.h>
#include <asm/octeon/cvmx-sriox-defs.h>
#include <asm/octeon/cvmx-helper-jtag.h>

#ifdef CONFIG_CAVIUM_DECODE_RSL
#include <asm/octeon/cvmx-error.h>
#endif
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>
#include <asm/octeon/cvmx-helper-errata.h>
#include <asm/octeon/cvmx-qlm.h>
#include <asm/octeon/cvmx-pcie.h>
#include <asm/octeon/cvmx-sysinfo.h>
#include <asm/octeon/cvmx-swap.h>
#include <asm/octeon/cvmx-wqe.h>
#else
#include "cvmx.h"
#if !defined(CVMX_BUILD_FOR_FREEBSD_KERNEL)
#include "cvmx-csr-db.h"
#endif
#include "cvmx-pcie.h"
#include "cvmx-sysinfo.h"
#include "cvmx-swap.h"
#include "cvmx-wqe.h"
#if !defined(CVMX_BUILD_FOR_FREEBSD_KERNEL)
#include "cvmx-error.h"
#endif
#include "cvmx-helper-errata.h"
#include "cvmx-qlm.h"
#endif

#define MRRS_CN5XXX 0 /* 128 byte Max Read Request Size */
#define MPS_CN5XXX  0 /* 128 byte Max Packet Size (Limit of most PCs) */
#define MRRS_CN6XXX 3 /* 1024 byte Max Read Request Size */
#define MPS_CN6XXX  0 /* 128 byte Max Packet Size (Limit of most PCs) */

/**
 * Return the Core virtual base address for PCIe IO access. IOs are
 * read/written as an offset from this address.
 *
 * @param pcie_port PCIe port the IO is for
 *
 * @return 64bit Octeon IO base address for read/write
 */
uint64_t cvmx_pcie_get_io_base_address(int pcie_port)
{
    cvmx_pcie_address_t pcie_addr;
    pcie_addr.u64 = 0;
    pcie_addr.io.upper = 0;
    pcie_addr.io.io = 1;
    pcie_addr.io.did = 3;
    pcie_addr.io.subdid = 2;
    pcie_addr.io.es = 1;
    pcie_addr.io.port = pcie_port;
    return pcie_addr.u64;
}


/**
 * Size of the IO address region returned at address
 * cvmx_pcie_get_io_base_address()
 *
 * @param pcie_port PCIe port the IO is for
 *
 * @return Size of the IO window
 */
uint64_t cvmx_pcie_get_io_size(int pcie_port)
{
    return 1ull<<32;
}


/**
 * Return the Core virtual base address for PCIe MEM access. Memory is
 * read/written as an offset from this address.
 *
 * @param pcie_port PCIe port the IO is for
 *
 * @return 64bit Octeon IO base address for read/write
 */
uint64_t cvmx_pcie_get_mem_base_address(int pcie_port)
{
    cvmx_pcie_address_t pcie_addr;
    pcie_addr.u64 = 0;
    pcie_addr.mem.upper = 0;
    pcie_addr.mem.io = 1;
    pcie_addr.mem.did = 3;
    pcie_addr.mem.subdid = 3 + pcie_port;
    return pcie_addr.u64;
}


/**
 * Size of the Mem address region returned at address
 * cvmx_pcie_get_mem_base_address()
 *
 * @param pcie_port PCIe port the IO is for
 *
 * @return Size of the Mem window
 */
uint64_t cvmx_pcie_get_mem_size(int pcie_port)
{
    return 1ull<<36;
}


/**
 * @INTERNAL
 * Initialize the RC config space CSRs
 *
 * @param pcie_port PCIe port to initialize
 */
static void __cvmx_pcie_rc_initialize_config_space(int pcie_port)
{
    /* Max Payload Size (PCIE*_CFG030[MPS]) */
    /* Max Read Request Size (PCIE*_CFG030[MRRS]) */
    /* Relaxed-order, no-snoop enables (PCIE*_CFG030[RO_EN,NS_EN] */
    /* Error Message Enables (PCIE*_CFG030[CE_EN,NFE_EN,FE_EN,UR_EN]) */
    {
        cvmx_pciercx_cfg030_t pciercx_cfg030;
        pciercx_cfg030.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG030(pcie_port));
        if (OCTEON_IS_MODEL(OCTEON_CN5XXX))
        {
            pciercx_cfg030.s.mps = MPS_CN5XXX;
            pciercx_cfg030.s.mrrs = MRRS_CN5XXX;
        }
        else
        {
            pciercx_cfg030.s.mps = MPS_CN6XXX;
            pciercx_cfg030.s.mrrs = MRRS_CN6XXX;
        }
        pciercx_cfg030.s.ro_en = 1; /* Enable relaxed order processing. This will allow devices to affect read response ordering */
        pciercx_cfg030.s.ns_en = 1; /* Enable no snoop processing. Not used by Octeon */
        pciercx_cfg030.s.ce_en = 1; /* Correctable error reporting enable. */
        pciercx_cfg030.s.nfe_en = 1; /* Non-fatal error reporting enable. */
        pciercx_cfg030.s.fe_en = 1; /* Fatal error reporting enable. */
        pciercx_cfg030.s.ur_en = 1; /* Unsupported request reporting enable. */
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG030(pcie_port), pciercx_cfg030.u32);
    }

    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        /* Max Payload Size (NPEI_CTL_STATUS2[MPS]) must match PCIE*_CFG030[MPS] */
        /* Max Read Request Size (NPEI_CTL_STATUS2[MRRS]) must not exceed PCIE*_CFG030[MRRS] */
        cvmx_npei_ctl_status2_t npei_ctl_status2;
        npei_ctl_status2.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_STATUS2);
        npei_ctl_status2.s.mps = MPS_CN5XXX; /* Max payload size = 128 bytes for best Octeon DMA performance */
        npei_ctl_status2.s.mrrs = MRRS_CN5XXX; /* Max read request size = 128 bytes for best Octeon DMA performance */
        if (pcie_port)
            npei_ctl_status2.s.c1_b1_s = 3; /* Port1 BAR1 Size 256MB */
        else
            npei_ctl_status2.s.c0_b1_s = 3; /* Port0 BAR1 Size 256MB */

        cvmx_write_csr(CVMX_PEXP_NPEI_CTL_STATUS2, npei_ctl_status2.u64);
    }
    else
    {
        /* Max Payload Size (DPI_SLI_PRTX_CFG[MPS]) must match PCIE*_CFG030[MPS] */
        /* Max Read Request Size (DPI_SLI_PRTX_CFG[MRRS]) must not exceed PCIE*_CFG030[MRRS] */
        cvmx_dpi_sli_prtx_cfg_t prt_cfg;
        cvmx_sli_s2m_portx_ctl_t sli_s2m_portx_ctl;
        prt_cfg.u64 = cvmx_read_csr(CVMX_DPI_SLI_PRTX_CFG(pcie_port));
        prt_cfg.s.mps = MPS_CN6XXX;
        prt_cfg.s.mrrs = MRRS_CN6XXX;
        /* Max outstanding load request. */
        prt_cfg.s.molr = 32;
        cvmx_write_csr(CVMX_DPI_SLI_PRTX_CFG(pcie_port), prt_cfg.u64);

        sli_s2m_portx_ctl.u64 = cvmx_read_csr(CVMX_PEXP_SLI_S2M_PORTX_CTL(pcie_port));
        sli_s2m_portx_ctl.s.mrrs = MRRS_CN6XXX;
        cvmx_write_csr(CVMX_PEXP_SLI_S2M_PORTX_CTL(pcie_port), sli_s2m_portx_ctl.u64);
    }

    /* ECRC Generation (PCIE*_CFG070[GE,CE]) */
    {
        cvmx_pciercx_cfg070_t pciercx_cfg070;
        pciercx_cfg070.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG070(pcie_port));
        pciercx_cfg070.s.ge = 1; /* ECRC generation enable. */
        pciercx_cfg070.s.ce = 1; /* ECRC check enable. */
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG070(pcie_port), pciercx_cfg070.u32);
    }

    /* Access Enables (PCIE*_CFG001[MSAE,ME]) */
        /* ME and MSAE should always be set. */
    /* Interrupt Disable (PCIE*_CFG001[I_DIS]) */
    /* System Error Message Enable (PCIE*_CFG001[SEE]) */
    {
        cvmx_pciercx_cfg001_t pciercx_cfg001;
        pciercx_cfg001.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG001(pcie_port));
        pciercx_cfg001.s.msae = 1; /* Memory space enable. */
        pciercx_cfg001.s.me = 1; /* Bus master enable. */
        pciercx_cfg001.s.i_dis = 1; /* INTx assertion disable. */
        pciercx_cfg001.s.see = 1; /* SERR# enable */
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG001(pcie_port), pciercx_cfg001.u32);
    }


    /* Advanced Error Recovery Message Enables */
    /* (PCIE*_CFG066,PCIE*_CFG067,PCIE*_CFG069) */
    cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG066(pcie_port), 0);
    /* Use CVMX_PCIERCX_CFG067 hardware default */
    cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG069(pcie_port), 0);


    /* Active State Power Management (PCIE*_CFG032[ASLPC]) */
    {
        cvmx_pciercx_cfg032_t pciercx_cfg032;
        pciercx_cfg032.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG032(pcie_port));
        pciercx_cfg032.s.aslpc = 0; /* Active state Link PM control. */
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG032(pcie_port), pciercx_cfg032.u32);
    }

    /* Link Width Mode (PCIERCn_CFG452[LME]) - Set during cvmx_pcie_rc_initialize_link() */
    /* Primary Bus Number (PCIERCn_CFG006[PBNUM]) */
    {
        /* We set the primary bus number to 1 so IDT bridges are happy. They don't like zero */
        cvmx_pciercx_cfg006_t pciercx_cfg006;
        pciercx_cfg006.u32 = 0;
        pciercx_cfg006.s.pbnum = 1;
        pciercx_cfg006.s.sbnum = 1;
        pciercx_cfg006.s.subbnum = 1;
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG006(pcie_port), pciercx_cfg006.u32);
    }

    /* Memory-mapped I/O BAR (PCIERCn_CFG008) */
    /* Most applications should disable the memory-mapped I/O BAR by */
    /* setting PCIERCn_CFG008[ML_ADDR] < PCIERCn_CFG008[MB_ADDR] */
    {
        cvmx_pciercx_cfg008_t pciercx_cfg008;
        pciercx_cfg008.u32 = 0;
        pciercx_cfg008.s.mb_addr = 0x100;
        pciercx_cfg008.s.ml_addr = 0;
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG008(pcie_port), pciercx_cfg008.u32);
    }

    /* Prefetchable BAR (PCIERCn_CFG009,PCIERCn_CFG010,PCIERCn_CFG011) */
    /* Most applications should disable the prefetchable BAR by setting */
    /* PCIERCn_CFG011[UMEM_LIMIT],PCIERCn_CFG009[LMEM_LIMIT] < */
    /* PCIERCn_CFG010[UMEM_BASE],PCIERCn_CFG009[LMEM_BASE] */
    {
        cvmx_pciercx_cfg009_t pciercx_cfg009;
        cvmx_pciercx_cfg010_t pciercx_cfg010;
        cvmx_pciercx_cfg011_t pciercx_cfg011;
        pciercx_cfg009.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG009(pcie_port));
        pciercx_cfg010.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG010(pcie_port));
        pciercx_cfg011.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG011(pcie_port));
        pciercx_cfg009.s.lmem_base = 0x100;
        pciercx_cfg009.s.lmem_limit = 0;
        pciercx_cfg010.s.umem_base = 0x100;
        pciercx_cfg011.s.umem_limit = 0;
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG009(pcie_port), pciercx_cfg009.u32);
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG010(pcie_port), pciercx_cfg010.u32);
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG011(pcie_port), pciercx_cfg011.u32);
    }

    /* System Error Interrupt Enables (PCIERCn_CFG035[SECEE,SEFEE,SENFEE]) */
    /* PME Interrupt Enables (PCIERCn_CFG035[PMEIE]) */
    {
        cvmx_pciercx_cfg035_t pciercx_cfg035;
        pciercx_cfg035.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG035(pcie_port));
        pciercx_cfg035.s.secee = 1; /* System error on correctable error enable. */
        pciercx_cfg035.s.sefee = 1; /* System error on fatal error enable. */
        pciercx_cfg035.s.senfee = 1; /* System error on non-fatal error enable. */
        pciercx_cfg035.s.pmeie = 1; /* PME interrupt enable. */
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG035(pcie_port), pciercx_cfg035.u32);
    }

    /* Advanced Error Recovery Interrupt Enables */
    /* (PCIERCn_CFG075[CERE,NFERE,FERE]) */
    {
        cvmx_pciercx_cfg075_t pciercx_cfg075;
        pciercx_cfg075.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG075(pcie_port));
        pciercx_cfg075.s.cere = 1; /* Correctable error reporting enable. */
        pciercx_cfg075.s.nfere = 1; /* Non-fatal error reporting enable. */
        pciercx_cfg075.s.fere = 1; /* Fatal error reporting enable. */
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG075(pcie_port), pciercx_cfg075.u32);
    }

    /* HP Interrupt Enables (PCIERCn_CFG034[HPINT_EN], */
    /* PCIERCn_CFG034[DLLS_EN,CCINT_EN]) */
    {
        cvmx_pciercx_cfg034_t pciercx_cfg034;
        pciercx_cfg034.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG034(pcie_port));
        pciercx_cfg034.s.hpint_en = 1; /* Hot-plug interrupt enable. */
        pciercx_cfg034.s.dlls_en = 1; /* Data Link Layer state changed enable */
        pciercx_cfg034.s.ccint_en = 1; /* Command completed interrupt enable. */
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG034(pcie_port), pciercx_cfg034.u32);
    }
}

/**
 * @INTERNAL
 * Initialize a host mode PCIe gen 1 link. This function takes a PCIe
 * port from reset to a link up state. Software can then begin
 * configuring the rest of the link.
 *
 * @param pcie_port PCIe port to initialize
 *
 * @return Zero on success
 */
static int __cvmx_pcie_rc_initialize_link_gen1(int pcie_port)
{
    uint64_t start_cycle;
    cvmx_pescx_ctl_status_t pescx_ctl_status;
    cvmx_pciercx_cfg452_t pciercx_cfg452;
    cvmx_pciercx_cfg032_t pciercx_cfg032;
    cvmx_pciercx_cfg448_t pciercx_cfg448;

    /* Set the lane width */
    pciercx_cfg452.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG452(pcie_port));
    pescx_ctl_status.u64 = cvmx_read_csr(CVMX_PESCX_CTL_STATUS(pcie_port));
    if (pescx_ctl_status.s.qlm_cfg == 0)
    {
        /* We're in 8 lane (56XX) or 4 lane (54XX) mode */
        pciercx_cfg452.s.lme = 0xf;
    }
    else
    {
        /* We're in 4 lane (56XX) or 2 lane (52XX) mode */
        pciercx_cfg452.s.lme = 0x7;
    }
    cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG452(pcie_port), pciercx_cfg452.u32);

    /* CN52XX pass 1.x has an errata where length mismatches on UR responses can
        cause bus errors on 64bit memory reads. Turning off length error
        checking fixes this */
    if (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X))
    {
        cvmx_pciercx_cfg455_t pciercx_cfg455;
        pciercx_cfg455.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG455(pcie_port));
        pciercx_cfg455.s.m_cpl_len_err = 1;
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG455(pcie_port), pciercx_cfg455.u32);
    }

    /* Lane swap needs to be manually enabled for CN52XX */
    if (OCTEON_IS_MODEL(OCTEON_CN52XX) && (pcie_port == 1))
    {
      switch (cvmx_sysinfo_get()->board_type)
      {
#if defined(OCTEON_VENDOR_LANNER)
	case CVMX_BOARD_TYPE_CUST_LANNER_MR730:
	  break;
#endif
	default:
	  pescx_ctl_status.s.lane_swp = 1;
	  break;
      }
      cvmx_write_csr(CVMX_PESCX_CTL_STATUS(pcie_port),pescx_ctl_status.u64);
    }

    /* Bring up the link */
    pescx_ctl_status.u64 = cvmx_read_csr(CVMX_PESCX_CTL_STATUS(pcie_port));
    pescx_ctl_status.s.lnk_enb = 1;
    cvmx_write_csr(CVMX_PESCX_CTL_STATUS(pcie_port), pescx_ctl_status.u64);

    /* CN52XX pass 1.0: Due to a bug in 2nd order CDR, it needs to be disabled */
    if (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_0))
        __cvmx_helper_errata_qlm_disable_2nd_order_cdr(0);

    /* Wait for the link to come up */
    start_cycle = cvmx_get_cycle();
    do
    {
        if (cvmx_get_cycle() - start_cycle > 100*cvmx_clock_get_rate(CVMX_CLOCK_CORE))
        {
            cvmx_dprintf("PCIe: Port %d link timeout\n", pcie_port);
            return -1;
        }
        cvmx_wait(50000);
        pciercx_cfg032.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG032(pcie_port));
    } while (pciercx_cfg032.s.dlla == 0);

    /* Clear all pending errors */
    cvmx_write_csr(CVMX_PEXP_NPEI_INT_SUM, cvmx_read_csr(CVMX_PEXP_NPEI_INT_SUM));

    /* Update the Replay Time Limit. Empirically, some PCIe devices take a
        little longer to respond than expected under load. As a workaround for
        this we configure the Replay Time Limit to the value expected for a 512
        byte MPS instead of our actual 256 byte MPS. The numbers below are
        directly from the PCIe spec table 3-4 */
    pciercx_cfg448.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG448(pcie_port));
    switch (pciercx_cfg032.s.nlw)
    {
        case 1: /* 1 lane */
            pciercx_cfg448.s.rtl = 1677;
            break;
        case 2: /* 2 lanes */
            pciercx_cfg448.s.rtl = 867;
            break;
        case 4: /* 4 lanes */
            pciercx_cfg448.s.rtl = 462;
            break;
        case 8: /* 8 lanes */
            pciercx_cfg448.s.rtl = 258;
            break;
    }
    cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG448(pcie_port), pciercx_cfg448.u32);

    return 0;
}

static inline void __cvmx_increment_ba(cvmx_sli_mem_access_subidx_t *pmas)
{   
    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
        pmas->cn68xx.ba++;
    else
        pmas->cn63xx.ba++;
}

/**
 * Initialize a PCIe gen 1 port for use in host(RC) mode. It doesn't enumerate
 * the bus.
 *
 * @param pcie_port PCIe port to initialize
 *
 * @return Zero on success
 */
static int __cvmx_pcie_rc_initialize_gen1(int pcie_port)
{
    int i;
    int base;
    uint64_t addr_swizzle;
    cvmx_ciu_soft_prst_t ciu_soft_prst;
    cvmx_pescx_bist_status_t pescx_bist_status;
    cvmx_pescx_bist_status2_t pescx_bist_status2;
    cvmx_npei_ctl_status_t npei_ctl_status;
    cvmx_npei_mem_access_ctl_t npei_mem_access_ctl;
    cvmx_npei_mem_access_subidx_t mem_access_subid;
    cvmx_npei_dbg_data_t npei_dbg_data;
    cvmx_pescx_ctl_status2_t pescx_ctl_status2;
    cvmx_pciercx_cfg032_t pciercx_cfg032;
    cvmx_npei_bar1_indexx_t bar1_index;

retry:
    /* Make sure we aren't trying to setup a target mode interface in host mode */
    npei_ctl_status.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_STATUS);
    if ((pcie_port==0) && !npei_ctl_status.s.host_mode)
    {
        cvmx_dprintf("PCIe: Port %d in endpoint mode\n", pcie_port);
        return -1;
    }

    /* Make sure a CN52XX isn't trying to bring up port 1 when it is disabled */
    if (OCTEON_IS_MODEL(OCTEON_CN52XX))
    {
        npei_dbg_data.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DBG_DATA);
        if ((pcie_port==1) && npei_dbg_data.cn52xx.qlm0_link_width)
        {
            cvmx_dprintf("PCIe: ERROR: cvmx_pcie_rc_initialize() called on port1, but port1 is disabled\n");
            return -1;
        }
    }

    /* Make sure a CN56XX pass 1 isn't trying to do anything; errata for PASS 1 */
    if (OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X)) {
        cvmx_dprintf ("PCIe port %d: CN56XX_PASS_1, skipping\n", pcie_port);
        return -1;
    }

    /* PCIe switch arbitration mode. '0' == fixed priority NPEI, PCIe0, then PCIe1. '1' == round robin. */
    npei_ctl_status.s.arb = 1;
    /* Allow up to 0x20 config retries */
    npei_ctl_status.s.cfg_rtry = 0x20;
    /* CN52XX pass1.x has an errata where P0_NTAGS and P1_NTAGS don't reset */
    if (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X))
    {
        npei_ctl_status.s.p0_ntags = 0x20;
        npei_ctl_status.s.p1_ntags = 0x20;
    }
    cvmx_write_csr(CVMX_PEXP_NPEI_CTL_STATUS, npei_ctl_status.u64);

    /* Bring the PCIe out of reset */
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_EBH5200)
    {
        /* The EBH5200 board swapped the PCIe reset lines on the board. As a
            workaround for this bug, we bring both PCIe ports out of reset at
            the same time instead of on separate calls. So for port 0, we bring
            both out of reset and do nothing on port 1 */
        if (pcie_port == 0)
        {
            ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
            /* After a chip reset the PCIe will also be in reset. If it isn't,
                most likely someone is trying to init it again without a proper
                PCIe reset */
            if (ciu_soft_prst.s.soft_prst == 0)
            {
		/* Reset the ports */
		ciu_soft_prst.s.soft_prst = 1;
		cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
		ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
		ciu_soft_prst.s.soft_prst = 1;
		cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
		/* Wait until pcie resets the ports. */
		cvmx_wait_usec(2000);
            }
            ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
            ciu_soft_prst.s.soft_prst = 0;
            cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
            ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
            ciu_soft_prst.s.soft_prst = 0;
            cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
        }
    }
    else
    {
        /* The normal case: The PCIe ports are completely separate and can be
            brought out of reset independently */
        if (pcie_port)
            ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
        else
            ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
        /* After a chip reset the PCIe will also be in reset. If it isn't,
            most likely someone is trying to init it again without a proper
            PCIe reset */
        if (ciu_soft_prst.s.soft_prst == 0)
        {
	    /* Reset the port */
	    ciu_soft_prst.s.soft_prst = 1;
	    if (pcie_port)
		cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
 	    else
		cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
	    /* Wait until pcie resets the ports. */
	    cvmx_wait_usec(2000);
        }
        if (pcie_port)
        {
            ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
            ciu_soft_prst.s.soft_prst = 0;
            cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
        }
        else
        {
            ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
            ciu_soft_prst.s.soft_prst = 0;
            cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
        }
    }

    /* Wait for PCIe reset to complete. Due to errata PCIE-700, we don't poll
       PESCX_CTL_STATUS2[PCIERST], but simply wait a fixed number of cycles */
    cvmx_wait(400000);

    /* PESCX_BIST_STATUS2[PCLK_RUN] was missing on pass 1 of CN56XX and
        CN52XX, so we only probe it on newer chips */
    if (!OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X) && !OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X))
    {
        /* Clear PCLK_RUN so we can check if the clock is running */
        pescx_ctl_status2.u64 = cvmx_read_csr(CVMX_PESCX_CTL_STATUS2(pcie_port));
        pescx_ctl_status2.s.pclk_run = 1;
        cvmx_write_csr(CVMX_PESCX_CTL_STATUS2(pcie_port), pescx_ctl_status2.u64);
        /* Now that we cleared PCLK_RUN, wait for it to be set again telling
            us the clock is running */
        if (CVMX_WAIT_FOR_FIELD64(CVMX_PESCX_CTL_STATUS2(pcie_port),
            cvmx_pescx_ctl_status2_t, pclk_run, ==, 1, 10000))
        {
            cvmx_dprintf("PCIe: Port %d isn't clocked, skipping.\n", pcie_port);
            return -1;
        }
    }

    /* Check and make sure PCIe came out of reset. If it doesn't the board
        probably hasn't wired the clocks up and the interface should be
        skipped */
    pescx_ctl_status2.u64 = cvmx_read_csr(CVMX_PESCX_CTL_STATUS2(pcie_port));
    if (pescx_ctl_status2.s.pcierst)
    {
        cvmx_dprintf("PCIe: Port %d stuck in reset, skipping.\n", pcie_port);
        return -1;
    }

    /* Check BIST2 status. If any bits are set skip this interface. This
        is an attempt to catch PCIE-813 on pass 1 parts */
    pescx_bist_status2.u64 = cvmx_read_csr(CVMX_PESCX_BIST_STATUS2(pcie_port));
    if (pescx_bist_status2.u64)
    {
        cvmx_dprintf("PCIe: Port %d BIST2 failed. Most likely this port isn't hooked up, skipping.\n", pcie_port);
        return -1;
    }

    /* Check BIST status */
    pescx_bist_status.u64 = cvmx_read_csr(CVMX_PESCX_BIST_STATUS(pcie_port));
    if (pescx_bist_status.u64)
        cvmx_dprintf("PCIe: BIST FAILED for port %d (0x%016llx)\n", pcie_port, CAST64(pescx_bist_status.u64));

    /* Initialize the config space CSRs */
    __cvmx_pcie_rc_initialize_config_space(pcie_port);

    /* Bring the link up */
    if (__cvmx_pcie_rc_initialize_link_gen1(pcie_port))
    {
        cvmx_dprintf("PCIe: Failed to initialize port %d, probably the slot is empty\n", pcie_port);
        return -1;
    }

    /* Store merge control (NPEI_MEM_ACCESS_CTL[TIMER,MAX_WORD]) */
    npei_mem_access_ctl.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_MEM_ACCESS_CTL);
    npei_mem_access_ctl.s.max_word = 0;     /* Allow 16 words to combine */
    npei_mem_access_ctl.s.timer = 127;      /* Wait up to 127 cycles for more data */
    cvmx_write_csr(CVMX_PEXP_NPEI_MEM_ACCESS_CTL, npei_mem_access_ctl.u64);

    /* Setup Mem access SubDIDs */
    mem_access_subid.u64 = 0;
    mem_access_subid.s.port = pcie_port; /* Port the request is sent to. */
    mem_access_subid.s.nmerge = 1;  /* Due to an errata on pass 1 chips, no merging is allowed. */
    mem_access_subid.s.esr = 1;     /* Endian-swap for Reads. */
    mem_access_subid.s.esw = 1;     /* Endian-swap for Writes. */
    mem_access_subid.s.nsr = 0;     /* Enable Snooping for Reads. Octeon doesn't care, but devices might want this more conservative setting */
    mem_access_subid.s.nsw = 0;     /* Enable Snoop for Writes. */
    mem_access_subid.s.ror = 0;     /* Disable Relaxed Ordering for Reads. */
    mem_access_subid.s.row = 0;     /* Disable Relaxed Ordering for Writes. */
    mem_access_subid.s.ba = 0;      /* PCIe Adddress Bits <63:34>. */

    /* Setup mem access 12-15 for port 0, 16-19 for port 1, supplying 36 bits of address space */
    for (i=12 + pcie_port*4; i<16 + pcie_port*4; i++)
    {
        cvmx_write_csr(CVMX_PEXP_NPEI_MEM_ACCESS_SUBIDX(i), mem_access_subid.u64);
        mem_access_subid.s.ba += 1; /* Set each SUBID to extend the addressable range */
    }

    /* Disable the peer to peer forwarding register. This must be setup
        by the OS after it enumerates the bus and assigns addresses to the
        PCIe busses */
    for (i=0; i<4; i++)
    {
        cvmx_write_csr(CVMX_PESCX_P2P_BARX_START(i, pcie_port), -1);
        cvmx_write_csr(CVMX_PESCX_P2P_BARX_END(i, pcie_port), -1);
    }

    /* Set Octeon's BAR0 to decode 0-16KB. It overlaps with Bar2 */
    cvmx_write_csr(CVMX_PESCX_P2N_BAR0_START(pcie_port), 0);

    /* BAR1 follows BAR2 with a gap so it has the same address as for gen2. */
    cvmx_write_csr(CVMX_PESCX_P2N_BAR1_START(pcie_port), CVMX_PCIE_BAR1_RC_BASE);

    bar1_index.u32 = 0;
    bar1_index.s.addr_idx = (CVMX_PCIE_BAR1_PHYS_BASE >> 22);
    bar1_index.s.ca = 1;       /* Not Cached */
    bar1_index.s.end_swp = 1;  /* Endian Swap mode */
    bar1_index.s.addr_v = 1;   /* Valid entry */

    base = pcie_port ? 16 : 0;

    /* Big endian swizzle for 32-bit PEXP_NCB register. */
#ifdef __MIPSEB__
    addr_swizzle = 4;
#else
    addr_swizzle = 0;
#endif
    for (i = 0; i < 16; i++) {
        cvmx_write64_uint32((CVMX_PEXP_NPEI_BAR1_INDEXX(base) ^ addr_swizzle), bar1_index.u32);
        base++;
        /* 256MB / 16 >> 22 == 4 */
        bar1_index.s.addr_idx += (((1ull << 28) / 16ull) >> 22);
    }

    /* Set Octeon's BAR2 to decode 0-2^39. Bar0 and Bar1 take precedence
        where they overlap. It also overlaps with the device addresses, so
        make sure the peer to peer forwarding is set right */
    cvmx_write_csr(CVMX_PESCX_P2N_BAR2_START(pcie_port), 0);

    /* Setup BAR2 attributes */
    /* Relaxed Ordering (NPEI_CTL_PORTn[PTLP_RO,CTLP_RO, WAIT_COM]) */
    /* - PTLP_RO,CTLP_RO should normally be set (except for debug). */
    /* - WAIT_COM=0 will likely work for all applications. */
    /* Load completion relaxed ordering (NPEI_CTL_PORTn[WAITL_COM]) */
    if (pcie_port)
    {
        cvmx_npei_ctl_port1_t npei_ctl_port;
        npei_ctl_port.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_PORT1);
        npei_ctl_port.s.bar2_enb = 1;
        npei_ctl_port.s.bar2_esx = 1;
        npei_ctl_port.s.bar2_cax = 0;
        npei_ctl_port.s.ptlp_ro = 1;
        npei_ctl_port.s.ctlp_ro = 1;
        npei_ctl_port.s.wait_com = 0;
        npei_ctl_port.s.waitl_com = 0;
        cvmx_write_csr(CVMX_PEXP_NPEI_CTL_PORT1, npei_ctl_port.u64);
    }
    else
    {
        cvmx_npei_ctl_port0_t npei_ctl_port;
        npei_ctl_port.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_PORT0);
        npei_ctl_port.s.bar2_enb = 1;
        npei_ctl_port.s.bar2_esx = 1;
        npei_ctl_port.s.bar2_cax = 0;
        npei_ctl_port.s.ptlp_ro = 1;
        npei_ctl_port.s.ctlp_ro = 1;
        npei_ctl_port.s.wait_com = 0;
        npei_ctl_port.s.waitl_com = 0;
        cvmx_write_csr(CVMX_PEXP_NPEI_CTL_PORT0, npei_ctl_port.u64);
    }

    /* Both pass 1 and pass 2 of CN52XX and CN56XX have an errata that causes
        TLP ordering to not be preserved after multiple PCIe port resets. This
        code detects this fault and corrects it by aligning the TLP counters
        properly. Another link reset is then performed. See PCIE-13340 */
    if (OCTEON_IS_MODEL(OCTEON_CN56XX_PASS2_X) || OCTEON_IS_MODEL(OCTEON_CN52XX_PASS2_X) ||
        OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X) || OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X))
    {
        cvmx_npei_dbg_data_t dbg_data;
        int old_in_fif_p_count;
        int in_fif_p_count;
        int out_p_count;
        int in_p_offset = (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X) || OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X)) ? 4 : 1;
        int i;

        /* Choose a write address of 1MB. It should be harmless as all bars
            haven't been setup */
        uint64_t write_address = (cvmx_pcie_get_mem_base_address(pcie_port) + 0x100000) | (1ull<<63);

        /* Make sure at least in_p_offset have been executed before we try and
            read in_fif_p_count */
        i = in_p_offset;
        while (i--)
        {
            cvmx_write64_uint32(write_address, 0);
            cvmx_wait(10000);
        }

        /* Read the IN_FIF_P_COUNT from the debug select. IN_FIF_P_COUNT can be
            unstable sometimes so read it twice with a write between the reads.
            This way we can tell the value is good as it will increment by one
            due to the write */
        cvmx_write_csr(CVMX_PEXP_NPEI_DBG_SELECT, (pcie_port) ? 0xd7fc : 0xcffc);
        cvmx_read_csr(CVMX_PEXP_NPEI_DBG_SELECT);
        do
        {
            dbg_data.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DBG_DATA);
            old_in_fif_p_count = dbg_data.s.data & 0xff;
            cvmx_write64_uint32(write_address, 0);
            cvmx_wait(10000);
            dbg_data.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DBG_DATA);
            in_fif_p_count = dbg_data.s.data & 0xff;
        } while (in_fif_p_count != ((old_in_fif_p_count+1) & 0xff));

        /* Update in_fif_p_count for it's offset with respect to out_p_count */
        in_fif_p_count = (in_fif_p_count + in_p_offset) & 0xff;

        /* Read the OUT_P_COUNT from the debug select */
        cvmx_write_csr(CVMX_PEXP_NPEI_DBG_SELECT, (pcie_port) ? 0xd00f : 0xc80f);
        cvmx_read_csr(CVMX_PEXP_NPEI_DBG_SELECT);
        dbg_data.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DBG_DATA);
        out_p_count = (dbg_data.s.data>>1) & 0xff;

        /* Check that the two counters are aligned */
        if (out_p_count != in_fif_p_count)
        {
            cvmx_dprintf("PCIe: Port %d aligning TLP counters as workaround to maintain ordering\n", pcie_port);
            while (in_fif_p_count != 0)
            {
                cvmx_write64_uint32(write_address, 0);
                cvmx_wait(10000);
                in_fif_p_count = (in_fif_p_count + 1) & 0xff;
            }
            /* The EBH5200 board swapped the PCIe reset lines on the board. This
                means we must bring both links down and up, which will cause the
                PCIe0 to need alignment again. Lots of messages will be displayed,
                but everything should work */
            if ((cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_EBH5200) &&
                (pcie_port == 1))
                cvmx_pcie_rc_initialize(0);
            /* Rety bringing this port up */
            goto retry;
        }
    }

    /* Display the link status */
    pciercx_cfg032.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG032(pcie_port));
    cvmx_dprintf("PCIe: Port %d link active, %d lanes\n", pcie_port, pciercx_cfg032.s.nlw);

    return 0;
}

/**
 * @INTERNAL
 * Initialize a host mode PCIe gen 2 link. This function takes a PCIe
 * port from reset to a link up state. Software can then begin
 * configuring the rest of the link.
 *
 * @param pcie_port PCIe port to initialize
 *
 * @return Zero on success
 */
static int __cvmx_pcie_rc_initialize_link_gen2(int pcie_port)
{
    uint64_t start_cycle;
    cvmx_pemx_ctl_status_t pem_ctl_status;
    cvmx_pciercx_cfg032_t pciercx_cfg032;
    cvmx_pciercx_cfg448_t pciercx_cfg448;

    /* Bring up the link */
    pem_ctl_status.u64 = cvmx_read_csr(CVMX_PEMX_CTL_STATUS(pcie_port));
    pem_ctl_status.s.lnk_enb = 1;
    cvmx_write_csr(CVMX_PEMX_CTL_STATUS(pcie_port), pem_ctl_status.u64);

    /* Wait for the link to come up */
    start_cycle = cvmx_get_cycle();
    do
    {
        if (cvmx_get_cycle() - start_cycle > cvmx_clock_get_rate(CVMX_CLOCK_CORE))
            return -1;
        cvmx_wait(10000);
        pciercx_cfg032.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG032(pcie_port));
    } while ((pciercx_cfg032.s.dlla == 0) || (pciercx_cfg032.s.lt == 1));

    /* Update the Replay Time Limit. Empirically, some PCIe devices take a
        little longer to respond than expected under load. As a workaround for
        this we configure the Replay Time Limit to the value expected for a 512
        byte MPS instead of our actual 256 byte MPS. The numbers below are
        directly from the PCIe spec table 3-4 */
    pciercx_cfg448.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG448(pcie_port));
    switch (pciercx_cfg032.s.nlw)
    {
        case 1: /* 1 lane */
            pciercx_cfg448.s.rtl = 1677;
            break;
        case 2: /* 2 lanes */
            pciercx_cfg448.s.rtl = 867;
            break;
        case 4: /* 4 lanes */
            pciercx_cfg448.s.rtl = 462;
            break;
        case 8: /* 8 lanes */
            pciercx_cfg448.s.rtl = 258;
            break;
    }
    cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG448(pcie_port), pciercx_cfg448.u32);

    return 0;
}


/**
 * Initialize a PCIe gen 2 port for use in host(RC) mode. It doesn't enumerate
 * the bus.
 *
 * @param pcie_port PCIe port to initialize
 *
 * @return Zero on success
 */
static int __cvmx_pcie_rc_initialize_gen2(int pcie_port)
{
    int i;
    cvmx_ciu_soft_prst_t ciu_soft_prst;
    cvmx_mio_rst_ctlx_t mio_rst_ctl;
    cvmx_pemx_bar_ctl_t pemx_bar_ctl;
    cvmx_pemx_ctl_status_t pemx_ctl_status;
    cvmx_pemx_bist_status_t pemx_bist_status;
    cvmx_pemx_bist_status2_t pemx_bist_status2;
    cvmx_pciercx_cfg032_t pciercx_cfg032;
    cvmx_pciercx_cfg515_t pciercx_cfg515;
    cvmx_sli_ctl_portx_t sli_ctl_portx;
    cvmx_sli_mem_access_ctl_t sli_mem_access_ctl;
    cvmx_sli_mem_access_subidx_t mem_access_subid;
    cvmx_pemx_bar1_indexx_t bar1_index;
    int ep_mode;

    /* Make sure this interface is PCIe */
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF71XX))
    {
        /* Requires reading the MIO_QLMX_CFG register to figure
           out the port type. */
        int qlm = pcie_port;
        int status;
        if (OCTEON_IS_MODEL(OCTEON_CN68XX))
            qlm = 3 - (pcie_port * 2);
        else if (OCTEON_IS_MODEL(OCTEON_CN61XX))
        {
            cvmx_mio_qlmx_cfg_t qlm_cfg;
            qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(1));
            if (qlm_cfg.s.qlm_cfg == 1)
                qlm = 1;
        }
        /* PCIe is allowed only in QLM1, 1 PCIe port in x2 or 
           2 PCIe ports in x1 */
        else if (OCTEON_IS_MODEL(OCTEON_CNF71XX))
            qlm = 1;
        status = cvmx_qlm_get_status(qlm);
        if (status == 4 || status == 5)
        {
            cvmx_dprintf("PCIe: Port %d is SRIO, skipping.\n", pcie_port);
            return -1;
        }
        if (status == 1)
        {
            cvmx_dprintf("PCIe: Port %d is SGMII, skipping.\n", pcie_port);
            return -1;
        }
        if (status == 2)
        {
            cvmx_dprintf("PCIe: Port %d is XAUI, skipping.\n", pcie_port);
            return -1;
        }
        if (status == -1)
        {
            cvmx_dprintf("PCIe: Port %d is unknown, skipping.\n", pcie_port);
            return -1;
        }
    }

#if 0
    /* This code is so that the PCIe analyzer is able to see 63XX traffic */
    cvmx_dprintf("PCIE : init for pcie analyzer.\n");
    cvmx_helper_qlm_jtag_init();
    cvmx_helper_qlm_jtag_shift_zeros(pcie_port, 85);
    cvmx_helper_qlm_jtag_shift(pcie_port, 1, 1);
    cvmx_helper_qlm_jtag_shift_zeros(pcie_port, 300-86);
    cvmx_helper_qlm_jtag_shift_zeros(pcie_port, 85);
    cvmx_helper_qlm_jtag_shift(pcie_port, 1, 1);
    cvmx_helper_qlm_jtag_shift_zeros(pcie_port, 300-86);
    cvmx_helper_qlm_jtag_shift_zeros(pcie_port, 85);
    cvmx_helper_qlm_jtag_shift(pcie_port, 1, 1);
    cvmx_helper_qlm_jtag_shift_zeros(pcie_port, 300-86);
    cvmx_helper_qlm_jtag_shift_zeros(pcie_port, 85);
    cvmx_helper_qlm_jtag_shift(pcie_port, 1, 1);
    cvmx_helper_qlm_jtag_shift_zeros(pcie_port, 300-86);
    cvmx_helper_qlm_jtag_update(pcie_port);
#endif

    /* Make sure we aren't trying to setup a target mode interface in host mode */
    mio_rst_ctl.u64 = cvmx_read_csr(CVMX_MIO_RST_CTLX(pcie_port));
    ep_mode = ((OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)) ? (mio_rst_ctl.s.prtmode != 1) : (!mio_rst_ctl.s.host_mode));
    if (ep_mode)
    {
        cvmx_dprintf("PCIe: Port %d in endpoint mode.\n", pcie_port);
        return -1;
    }

    /* CN63XX Pass 1.0 errata G-14395 requires the QLM De-emphasis be programmed */
    if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_0))
    {
        if (pcie_port)
        {
            cvmx_ciu_qlm1_t ciu_qlm;
            ciu_qlm.u64 = cvmx_read_csr(CVMX_CIU_QLM1);
            ciu_qlm.s.txbypass = 1;
            ciu_qlm.s.txdeemph = 5;
            ciu_qlm.s.txmargin = 0x17;
            cvmx_write_csr(CVMX_CIU_QLM1, ciu_qlm.u64);
        }
        else
        {
            cvmx_ciu_qlm0_t ciu_qlm;
            ciu_qlm.u64 = cvmx_read_csr(CVMX_CIU_QLM0);
            ciu_qlm.s.txbypass = 1;
            ciu_qlm.s.txdeemph = 5;
            ciu_qlm.s.txmargin = 0x17;
            cvmx_write_csr(CVMX_CIU_QLM0, ciu_qlm.u64);
        }
    }
    /* Bring the PCIe out of reset */
    if (pcie_port)
        ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
    else
        ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
    /* After a chip reset the PCIe will also be in reset. If it isn't,
        most likely someone is trying to init it again without a proper
        PCIe reset */
    if (ciu_soft_prst.s.soft_prst == 0)
    {
        /* Reset the port */
        ciu_soft_prst.s.soft_prst = 1;
        if (pcie_port)
            cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
        else
            cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
        /* Wait until pcie resets the ports. */
        cvmx_wait_usec(2000);
    }
    if (pcie_port)
    {
        ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
        ciu_soft_prst.s.soft_prst = 0;
        cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
    }
    else
    {
        ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
        ciu_soft_prst.s.soft_prst = 0;
        cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
    }

    /* Wait for PCIe reset to complete */
    cvmx_wait_usec(1000);

    /* Check and make sure PCIe came out of reset. If it doesn't the board
        probably hasn't wired the clocks up and the interface should be
        skipped */
    if (CVMX_WAIT_FOR_FIELD64(CVMX_MIO_RST_CTLX(pcie_port), cvmx_mio_rst_ctlx_t, rst_done, ==, 1, 10000))
    {
        cvmx_dprintf("PCIe: Port %d stuck in reset, skipping.\n", pcie_port);
        return -1;
    }

    /* Check BIST status */
    pemx_bist_status.u64 = cvmx_read_csr(CVMX_PEMX_BIST_STATUS(pcie_port));
    if (pemx_bist_status.u64)
        cvmx_dprintf("PCIe: BIST FAILED for port %d (0x%016llx)\n", pcie_port, CAST64(pemx_bist_status.u64));
    pemx_bist_status2.u64 = cvmx_read_csr(CVMX_PEMX_BIST_STATUS2(pcie_port));
    /* Errata PCIE-14766 may cause the lower 6 bits to be randomly set on CN63XXp1 */
    if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X))
        pemx_bist_status2.u64 &= ~0x3full;
    if (pemx_bist_status2.u64)
        cvmx_dprintf("PCIe: BIST2 FAILED for port %d (0x%016llx)\n", pcie_port, CAST64(pemx_bist_status2.u64));

    /* Initialize the config space CSRs */
    __cvmx_pcie_rc_initialize_config_space(pcie_port);

    /* Enable gen2 speed selection */
    pciercx_cfg515.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG515(pcie_port));
    pciercx_cfg515.s.dsc = 1;
    cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG515(pcie_port), pciercx_cfg515.u32);

    /* Bring the link up */
    if (__cvmx_pcie_rc_initialize_link_gen2(pcie_port))
    {
        /* Some gen1 devices don't handle the gen 2 training correctly. Disable
            gen2 and try again with only gen1 */
        cvmx_pciercx_cfg031_t pciercx_cfg031;
        pciercx_cfg031.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG031(pcie_port));
        pciercx_cfg031.s.mls = 1;
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG031(pcie_port), pciercx_cfg031.u32);
        if (__cvmx_pcie_rc_initialize_link_gen2(pcie_port))
        {
            cvmx_dprintf("PCIe: Link timeout on port %d, probably the slot is empty\n", pcie_port);
            return -1;
        }
    }

    /* Store merge control (SLI_MEM_ACCESS_CTL[TIMER,MAX_WORD]) */
    sli_mem_access_ctl.u64 = cvmx_read_csr(CVMX_PEXP_SLI_MEM_ACCESS_CTL);
    sli_mem_access_ctl.s.max_word = 0;     /* Allow 16 words to combine */
    sli_mem_access_ctl.s.timer = 127;      /* Wait up to 127 cycles for more data */
    cvmx_write_csr(CVMX_PEXP_SLI_MEM_ACCESS_CTL, sli_mem_access_ctl.u64);

    /* Setup Mem access SubDIDs */
    mem_access_subid.u64 = 0;
    mem_access_subid.s.port = pcie_port; /* Port the request is sent to. */
    mem_access_subid.s.nmerge = 0;  /* Allow merging as it works on CN6XXX. */
    mem_access_subid.s.esr = 1;     /* Endian-swap for Reads. */
    mem_access_subid.s.esw = 1;     /* Endian-swap for Writes. */
    mem_access_subid.s.wtype = 0;   /* "No snoop" and "Relaxed ordering" are not set */
    mem_access_subid.s.rtype = 0;   /* "No snoop" and "Relaxed ordering" are not set */
    /* PCIe Adddress Bits <63:34>. */
    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
        mem_access_subid.cn68xx.ba = 0;
    else
        mem_access_subid.cn63xx.ba = 0;

    /* Setup mem access 12-15 for port 0, 16-19 for port 1, supplying 36 bits of address space */
    for (i=12 + pcie_port*4; i<16 + pcie_port*4; i++)
    {
        cvmx_write_csr(CVMX_PEXP_SLI_MEM_ACCESS_SUBIDX(i), mem_access_subid.u64);
        /* Set each SUBID to extend the addressable range */
	__cvmx_increment_ba(&mem_access_subid);
    }

    if (!OCTEON_IS_MODEL(OCTEON_CN61XX))
    {
        /* Disable the peer to peer forwarding register. This must be setup
            by the OS after it enumerates the bus and assigns addresses to the
            PCIe busses */
        for (i=0; i<4; i++)
        {
            cvmx_write_csr(CVMX_PEMX_P2P_BARX_START(i, pcie_port), -1);
            cvmx_write_csr(CVMX_PEMX_P2P_BARX_END(i, pcie_port), -1);
        }
    }

    /* Set Octeon's BAR0 to decode 0-16KB. It overlaps with Bar2 */
    cvmx_write_csr(CVMX_PEMX_P2N_BAR0_START(pcie_port), 0);

    /* Set Octeon's BAR2 to decode 0-2^41. Bar0 and Bar1 take precedence
        where they overlap. It also overlaps with the device addresses, so
        make sure the peer to peer forwarding is set right */
    cvmx_write_csr(CVMX_PEMX_P2N_BAR2_START(pcie_port), 0);

    /* Setup BAR2 attributes */
    /* Relaxed Ordering (NPEI_CTL_PORTn[PTLP_RO,CTLP_RO, WAIT_COM]) */
    /* - PTLP_RO,CTLP_RO should normally be set (except for debug). */
    /* - WAIT_COM=0 will likely work for all applications. */
    /* Load completion relaxed ordering (NPEI_CTL_PORTn[WAITL_COM]) */
    pemx_bar_ctl.u64 = cvmx_read_csr(CVMX_PEMX_BAR_CTL(pcie_port));
    pemx_bar_ctl.s.bar1_siz = 3;  /* 256MB BAR1*/
    pemx_bar_ctl.s.bar2_enb = 1;
    pemx_bar_ctl.s.bar2_esx = 1;
    pemx_bar_ctl.s.bar2_cax = 0;
    cvmx_write_csr(CVMX_PEMX_BAR_CTL(pcie_port), pemx_bar_ctl.u64);
    sli_ctl_portx.u64 = cvmx_read_csr(CVMX_PEXP_SLI_CTL_PORTX(pcie_port));
    sli_ctl_portx.s.ptlp_ro = 1;
    sli_ctl_portx.s.ctlp_ro = 1;
    sli_ctl_portx.s.wait_com = 0;
    sli_ctl_portx.s.waitl_com = 0;
    cvmx_write_csr(CVMX_PEXP_SLI_CTL_PORTX(pcie_port), sli_ctl_portx.u64);

    /* BAR1 follows BAR2 */
    cvmx_write_csr(CVMX_PEMX_P2N_BAR1_START(pcie_port), CVMX_PCIE_BAR1_RC_BASE);

    bar1_index.u64 = 0;
    bar1_index.s.addr_idx = (CVMX_PCIE_BAR1_PHYS_BASE >> 22);
    bar1_index.s.ca = 1;       /* Not Cached */
    bar1_index.s.end_swp = 1;  /* Endian Swap mode */
    bar1_index.s.addr_v = 1;   /* Valid entry */

    for (i = 0; i < 16; i++) {
        cvmx_write_csr(CVMX_PEMX_BAR1_INDEXX(i, pcie_port), bar1_index.u64);
        /* 256MB / 16 >> 22 == 4 */
        bar1_index.s.addr_idx += (((1ull << 28) / 16ull) >> 22);
    }

    /* Allow config retries for 250ms. Count is based off the 5Ghz SERDES
        clock */
    pemx_ctl_status.u64 = cvmx_read_csr(CVMX_PEMX_CTL_STATUS(pcie_port));
    pemx_ctl_status.s.cfg_rtry = 250 * 5000000 / 0x10000;
    cvmx_write_csr(CVMX_PEMX_CTL_STATUS(pcie_port), pemx_ctl_status.u64);

    /* Display the link status */
    pciercx_cfg032.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG032(pcie_port));
    cvmx_dprintf("PCIe: Port %d link active, %d lanes, speed gen%d\n", pcie_port, pciercx_cfg032.s.nlw, pciercx_cfg032.s.ls);

    return 0;
}

/**
 * Initialize a PCIe port for use in host(RC) mode. It doesn't enumerate the bus.
 *
 * @param pcie_port PCIe port to initialize
 *
 * @return Zero on success
 */
int cvmx_pcie_rc_initialize(int pcie_port)
{
    int result;
    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
        result = __cvmx_pcie_rc_initialize_gen1(pcie_port);
    else
        result = __cvmx_pcie_rc_initialize_gen2(pcie_port);
#if (!defined(CVMX_BUILD_FOR_LINUX_KERNEL) && !defined(CVMX_BUILD_FOR_FREEBSD_KERNEL)) || defined(CONFIG_CAVIUM_DECODE_RSL)
    if (result == 0)
        cvmx_error_enable_group(CVMX_ERROR_GROUP_PCI, pcie_port);
#endif
    return result;
}


/**
 * Shutdown a PCIe port and put it in reset
 *
 * @param pcie_port PCIe port to shutdown
 *
 * @return Zero on success
 */
int cvmx_pcie_rc_shutdown(int pcie_port)
{
#if (!defined(CVMX_BUILD_FOR_LINUX_KERNEL) && !defined(CVMX_BUILD_FOR_FREEBSD_KERNEL)) || defined(CONFIG_CAVIUM_DECODE_RSL)
    cvmx_error_disable_group(CVMX_ERROR_GROUP_PCI, pcie_port);
#endif
    /* Wait for all pending operations to complete */
    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        if (CVMX_WAIT_FOR_FIELD64(CVMX_PESCX_CPL_LUT_VALID(pcie_port), cvmx_pescx_cpl_lut_valid_t, tag, ==, 0, 2000))
            cvmx_dprintf("PCIe: Port %d shutdown timeout\n", pcie_port);
    }
    else
    {
        if (CVMX_WAIT_FOR_FIELD64(CVMX_PEMX_CPL_LUT_VALID(pcie_port), cvmx_pemx_cpl_lut_valid_t, tag, ==, 0, 2000))
            cvmx_dprintf("PCIe: Port %d shutdown timeout\n", pcie_port);
    }

    /* Force reset */
    if (pcie_port)
    {
        cvmx_ciu_soft_prst_t ciu_soft_prst;
        ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
        ciu_soft_prst.s.soft_prst = 1;
        cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
    }
    else
    {
        cvmx_ciu_soft_prst_t ciu_soft_prst;
        ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
        ciu_soft_prst.s.soft_prst = 1;
        cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
    }
    return 0;
}


/**
 * @INTERNAL
 * Build a PCIe config space request address for a device
 *
 * @param pcie_port PCIe port to access
 * @param bus       Sub bus
 * @param dev       Device ID
 * @param fn        Device sub function
 * @param reg       Register to access
 *
 * @return 64bit Octeon IO address
 */
static inline uint64_t __cvmx_pcie_build_config_addr(int pcie_port, int bus, int dev, int fn, int reg)
{
    cvmx_pcie_address_t pcie_addr;
    cvmx_pciercx_cfg006_t pciercx_cfg006;

    pciercx_cfg006.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG006(pcie_port));
    if ((bus <= pciercx_cfg006.s.pbnum) && (dev != 0))
        return 0;

    pcie_addr.u64 = 0;
    pcie_addr.config.upper = 2;
    pcie_addr.config.io = 1;
    pcie_addr.config.did = 3;
    pcie_addr.config.subdid = 1;
    pcie_addr.config.es = 1;
    pcie_addr.config.port = pcie_port;
    pcie_addr.config.ty = (bus > pciercx_cfg006.s.pbnum);
    pcie_addr.config.bus = bus;
    pcie_addr.config.dev = dev;
    pcie_addr.config.func = fn;
    pcie_addr.config.reg = reg;
    return pcie_addr.u64;
}


/**
 * Read 8bits from a Device's config space
 *
 * @param pcie_port PCIe port the device is on
 * @param bus       Sub bus
 * @param dev       Device ID
 * @param fn        Device sub function
 * @param reg       Register to access
 *
 * @return Result of the read
 */
uint8_t cvmx_pcie_config_read8(int pcie_port, int bus, int dev, int fn, int reg)
{
    uint64_t address = __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
    if (address)
        return cvmx_read64_uint8(address);
    else
        return 0xff;
}


/**
 * Read 16bits from a Device's config space
 *
 * @param pcie_port PCIe port the device is on
 * @param bus       Sub bus
 * @param dev       Device ID
 * @param fn        Device sub function
 * @param reg       Register to access
 *
 * @return Result of the read
 */
uint16_t cvmx_pcie_config_read16(int pcie_port, int bus, int dev, int fn, int reg)
{
    uint64_t address = __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
    if (address)
        return cvmx_le16_to_cpu(cvmx_read64_uint16(address));
    else
        return 0xffff;
}


/**
 * Read 32bits from a Device's config space
 *
 * @param pcie_port PCIe port the device is on
 * @param bus       Sub bus
 * @param dev       Device ID
 * @param fn        Device sub function
 * @param reg       Register to access
 *
 * @return Result of the read
 */
uint32_t cvmx_pcie_config_read32(int pcie_port, int bus, int dev, int fn, int reg)
{
    uint64_t address;

    address = __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
    if (address)
        return cvmx_le32_to_cpu(cvmx_read64_uint32(address));
    else
        return 0xffffffff;
}


/**
 * Write 8bits to a Device's config space
 *
 * @param pcie_port PCIe port the device is on
 * @param bus       Sub bus
 * @param dev       Device ID
 * @param fn        Device sub function
 * @param reg       Register to access
 * @param val       Value to write
 */
void cvmx_pcie_config_write8(int pcie_port, int bus, int dev, int fn, int reg, uint8_t val)
{
    uint64_t address = __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
    if (address)
        cvmx_write64_uint8(address, val);
}


/**
 * Write 16bits to a Device's config space
 *
 * @param pcie_port PCIe port the device is on
 * @param bus       Sub bus
 * @param dev       Device ID
 * @param fn        Device sub function
 * @param reg       Register to access
 * @param val       Value to write
 */
void cvmx_pcie_config_write16(int pcie_port, int bus, int dev, int fn, int reg, uint16_t val)
{
    uint64_t address = __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
    if (address)
        cvmx_write64_uint16(address, cvmx_cpu_to_le16(val));
}


/**
 * Write 32bits to a Device's config space
 *
 * @param pcie_port PCIe port the device is on
 * @param bus       Sub bus
 * @param dev       Device ID
 * @param fn        Device sub function
 * @param reg       Register to access
 * @param val       Value to write
 */
void cvmx_pcie_config_write32(int pcie_port, int bus, int dev, int fn, int reg, uint32_t val)
{
    uint64_t address = __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
    if (address)
        cvmx_write64_uint32(address, cvmx_cpu_to_le32(val));
}


/**
 * Read a PCIe config space register indirectly. This is used for
 * registers of the form PCIEEP_CFG??? and PCIERC?_CFG???.
 *
 * @param pcie_port  PCIe port to read from
 * @param cfg_offset Address to read
 *
 * @return Value read
 */
uint32_t cvmx_pcie_cfgx_read(int pcie_port, uint32_t cfg_offset)
{
    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        cvmx_pescx_cfg_rd_t pescx_cfg_rd;
        pescx_cfg_rd.u64 = 0;
        pescx_cfg_rd.s.addr = cfg_offset;
        cvmx_write_csr(CVMX_PESCX_CFG_RD(pcie_port), pescx_cfg_rd.u64);
        pescx_cfg_rd.u64 = cvmx_read_csr(CVMX_PESCX_CFG_RD(pcie_port));
        return pescx_cfg_rd.s.data;
    }
    else
    {
        cvmx_pemx_cfg_rd_t pemx_cfg_rd;
        pemx_cfg_rd.u64 = 0;
        pemx_cfg_rd.s.addr = cfg_offset;
        cvmx_write_csr(CVMX_PEMX_CFG_RD(pcie_port), pemx_cfg_rd.u64);
        pemx_cfg_rd.u64 = cvmx_read_csr(CVMX_PEMX_CFG_RD(pcie_port));
        return pemx_cfg_rd.s.data;
    }
}


/**
 * Write a PCIe config space register indirectly. This is used for
 * registers of the form PCIEEP_CFG??? and PCIERC?_CFG???.
 *
 * @param pcie_port  PCIe port to write to
 * @param cfg_offset Address to write
 * @param val        Value to write
 */
void cvmx_pcie_cfgx_write(int pcie_port, uint32_t cfg_offset, uint32_t val)
{
    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        cvmx_pescx_cfg_wr_t pescx_cfg_wr;
        pescx_cfg_wr.u64 = 0;
        pescx_cfg_wr.s.addr = cfg_offset;
        pescx_cfg_wr.s.data = val;
        cvmx_write_csr(CVMX_PESCX_CFG_WR(pcie_port), pescx_cfg_wr.u64);
    }
    else
    {
        cvmx_pemx_cfg_wr_t pemx_cfg_wr;
        pemx_cfg_wr.u64 = 0;
        pemx_cfg_wr.s.addr = cfg_offset;
        pemx_cfg_wr.s.data = val;
        cvmx_write_csr(CVMX_PEMX_CFG_WR(pcie_port), pemx_cfg_wr.u64);
    }
}


/**
 * Initialize a PCIe port for use in target(EP) mode.
 *
 * @param pcie_port PCIe port to initialize
 *
 * @return Zero on success
 */
int cvmx_pcie_ep_initialize(int pcie_port)
{
    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        cvmx_npei_ctl_status_t npei_ctl_status;
        npei_ctl_status.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_STATUS);
        if (npei_ctl_status.s.host_mode)
            return -1;
    }
    else
    {
        cvmx_mio_rst_ctlx_t mio_rst_ctl;
        int ep_mode;
        mio_rst_ctl.u64 = cvmx_read_csr(CVMX_MIO_RST_CTLX(pcie_port));
        ep_mode = (OCTEON_IS_MODEL(OCTEON_CN61XX) ? (mio_rst_ctl.s.prtmode != 0) : mio_rst_ctl.s.host_mode);
        if (ep_mode)
            return -1;
    }

    /* CN63XX Pass 1.0 errata G-14395 requires the QLM De-emphasis be programmed */
    if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_0))
    {
        if (pcie_port)
        {
            cvmx_ciu_qlm1_t ciu_qlm;
            ciu_qlm.u64 = cvmx_read_csr(CVMX_CIU_QLM1);
            ciu_qlm.s.txbypass = 1;
            ciu_qlm.s.txdeemph = 5;
            ciu_qlm.s.txmargin = 0x17;
            cvmx_write_csr(CVMX_CIU_QLM1, ciu_qlm.u64);
        }
        else
        {
            cvmx_ciu_qlm0_t ciu_qlm;
            ciu_qlm.u64 = cvmx_read_csr(CVMX_CIU_QLM0);
            ciu_qlm.s.txbypass = 1;
            ciu_qlm.s.txdeemph = 5;
            ciu_qlm.s.txmargin = 0x17;
            cvmx_write_csr(CVMX_CIU_QLM0, ciu_qlm.u64);
        }
    }

    /* Enable bus master and memory */
    cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIEEPX_CFG001(pcie_port), 0x6);

    /* Max Payload Size (PCIE*_CFG030[MPS]) */
    /* Max Read Request Size (PCIE*_CFG030[MRRS]) */
    /* Relaxed-order, no-snoop enables (PCIE*_CFG030[RO_EN,NS_EN] */
    /* Error Message Enables (PCIE*_CFG030[CE_EN,NFE_EN,FE_EN,UR_EN]) */
    {
        cvmx_pcieepx_cfg030_t pcieepx_cfg030;
        pcieepx_cfg030.u32 = cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIEEPX_CFG030(pcie_port));
        if (OCTEON_IS_MODEL(OCTEON_CN5XXX))
        {
            pcieepx_cfg030.s.mps = MPS_CN5XXX;
            pcieepx_cfg030.s.mrrs = MRRS_CN5XXX;
        }
        else
        {
            pcieepx_cfg030.s.mps = MPS_CN6XXX;
            pcieepx_cfg030.s.mrrs = MRRS_CN6XXX;
        }
        pcieepx_cfg030.s.ro_en = 1; /* Enable relaxed ordering. */
        pcieepx_cfg030.s.ns_en = 1; /* Enable no snoop. */
        pcieepx_cfg030.s.ce_en = 1; /* Correctable error reporting enable. */
        pcieepx_cfg030.s.nfe_en = 1; /* Non-fatal error reporting enable. */
        pcieepx_cfg030.s.fe_en = 1; /* Fatal error reporting enable. */
        pcieepx_cfg030.s.ur_en = 1; /* Unsupported request reporting enable. */
        cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIEEPX_CFG030(pcie_port), pcieepx_cfg030.u32);
    }

    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        /* Max Payload Size (NPEI_CTL_STATUS2[MPS]) must match PCIE*_CFG030[MPS] */
        /* Max Read Request Size (NPEI_CTL_STATUS2[MRRS]) must not exceed PCIE*_CFG030[MRRS] */
        cvmx_npei_ctl_status2_t npei_ctl_status2;
        npei_ctl_status2.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_STATUS2);
        npei_ctl_status2.s.mps = MPS_CN5XXX; /* Max payload size = 128 bytes (Limit of most PCs) */
        npei_ctl_status2.s.mrrs = MRRS_CN5XXX; /* Max read request size = 128 bytes for best Octeon DMA performance */
        cvmx_write_csr(CVMX_PEXP_NPEI_CTL_STATUS2, npei_ctl_status2.u64);
    }
    else
    {
        /* Max Payload Size (DPI_SLI_PRTX_CFG[MPS]) must match PCIE*_CFG030[MPS] */
        /* Max Read Request Size (DPI_SLI_PRTX_CFG[MRRS]) must not exceed PCIE*_CFG030[MRRS] */
        cvmx_dpi_sli_prtx_cfg_t prt_cfg;
        cvmx_sli_s2m_portx_ctl_t sli_s2m_portx_ctl;
        prt_cfg.u64 = cvmx_read_csr(CVMX_DPI_SLI_PRTX_CFG(pcie_port));
        prt_cfg.s.mps = MPS_CN6XXX;
        prt_cfg.s.mrrs = MRRS_CN6XXX;
        /* Max outstanding load request. */
        prt_cfg.s.molr = 32;
        cvmx_write_csr(CVMX_DPI_SLI_PRTX_CFG(pcie_port), prt_cfg.u64);

        sli_s2m_portx_ctl.u64 = cvmx_read_csr(CVMX_PEXP_SLI_S2M_PORTX_CTL(pcie_port));
        sli_s2m_portx_ctl.s.mrrs = MRRS_CN6XXX;
        cvmx_write_csr(CVMX_PEXP_SLI_S2M_PORTX_CTL(pcie_port), sli_s2m_portx_ctl.u64);
    }

    /* Setup Mem access SubDID 12 to access Host memory */
    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        cvmx_npei_mem_access_subidx_t mem_access_subid;
        mem_access_subid.u64 = 0;
        mem_access_subid.s.port = pcie_port; /* Port the request is sent to. */
        mem_access_subid.s.nmerge = 1;  /* Merging is not allowed in this window. */
        mem_access_subid.s.esr = 0;     /* Endian-swap for Reads. */
        mem_access_subid.s.esw = 0;     /* Endian-swap for Writes. */
        mem_access_subid.s.nsr = 0;     /* Enable Snooping for Reads. Octeon doesn't care, but devices might want this more conservative setting */
        mem_access_subid.s.nsw = 0;     /* Enable Snoop for Writes. */
        mem_access_subid.s.ror = 0;     /* Disable Relaxed Ordering for Reads. */
        mem_access_subid.s.row = 0;     /* Disable Relaxed Ordering for Writes. */
        mem_access_subid.s.ba = 0;      /* PCIe Adddress Bits <63:34>. */
        cvmx_write_csr(CVMX_PEXP_NPEI_MEM_ACCESS_SUBIDX(12), mem_access_subid.u64);
    }
    else
    {
        cvmx_sli_mem_access_subidx_t mem_access_subid;
        mem_access_subid.u64 = 0;
        mem_access_subid.s.port = pcie_port; /* Port the request is sent to. */
        mem_access_subid.s.nmerge = 0;  /* Merging is allowed in this window. */
        mem_access_subid.s.esr = 0;     /* Endian-swap for Reads. */
        mem_access_subid.s.esw = 0;     /* Endian-swap for Writes. */
        mem_access_subid.s.wtype = 0;   /* "No snoop" and "Relaxed ordering" are not set */
        mem_access_subid.s.rtype = 0;   /* "No snoop" and "Relaxed ordering" are not set */
        /* PCIe Adddress Bits <63:34>. */
        if (OCTEON_IS_MODEL(OCTEON_CN68XX))
            mem_access_subid.cn68xx.ba = 0;
        else
            mem_access_subid.cn63xx.ba = 0;
        cvmx_write_csr(CVMX_PEXP_SLI_MEM_ACCESS_SUBIDX(12 + pcie_port*4), mem_access_subid.u64);
    }
    return 0;
}


/**
 * Wait for posted PCIe read/writes to reach the other side of
 * the internal PCIe switch. This will insure that core
 * read/writes are posted before anything after this function
 * is called. This may be necessary when writing to memory that
 * will later be read using the DMA/PKT engines.
 *
 * @param pcie_port PCIe port to wait for
 */
void cvmx_pcie_wait_for_pending(int pcie_port)
{
    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        cvmx_npei_data_out_cnt_t npei_data_out_cnt;
        int a;
        int b;
        int c;

        /* See section 9.8, PCIe Core-initiated Requests, in the manual for a
            description of how this code works */
        npei_data_out_cnt.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DATA_OUT_CNT);
        if (pcie_port)
        {
            if (!npei_data_out_cnt.s.p1_fcnt)
                return;
            a = npei_data_out_cnt.s.p1_ucnt;
            b = (a + npei_data_out_cnt.s.p1_fcnt-1) & 0xffff;
        }
        else
        {
            if (!npei_data_out_cnt.s.p0_fcnt)
                return;
            a = npei_data_out_cnt.s.p0_ucnt;
            b = (a + npei_data_out_cnt.s.p0_fcnt-1) & 0xffff;
        }

        while (1)
        {
            npei_data_out_cnt.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DATA_OUT_CNT);
            c = (pcie_port) ? npei_data_out_cnt.s.p1_ucnt : npei_data_out_cnt.s.p0_ucnt;
            if (a<=b)
            {
                if ((c<a) || (c>b))
                    return;
            }
            else
            {
                if ((c>b) && (c<a))
                    return;
            }
        }
    }
    else
    {
        cvmx_sli_data_out_cnt_t sli_data_out_cnt;
        int a;
        int b;
        int c;

        sli_data_out_cnt.u64 = cvmx_read_csr(CVMX_PEXP_SLI_DATA_OUT_CNT);
        if (pcie_port)
        {
            if (!sli_data_out_cnt.s.p1_fcnt)
                return;
            a = sli_data_out_cnt.s.p1_ucnt;
            b = (a + sli_data_out_cnt.s.p1_fcnt-1) & 0xffff;
        }
        else
        {
            if (!sli_data_out_cnt.s.p0_fcnt)
                return;
            a = sli_data_out_cnt.s.p0_ucnt;
            b = (a + sli_data_out_cnt.s.p0_fcnt-1) & 0xffff;
        }

        while (1)
        {
            sli_data_out_cnt.u64 = cvmx_read_csr(CVMX_PEXP_SLI_DATA_OUT_CNT);
            c = (pcie_port) ? sli_data_out_cnt.s.p1_ucnt : sli_data_out_cnt.s.p0_ucnt;
            if (a<=b)
            {
                if ((c<a) || (c>b))
                    return;
            }
            else
            {
                if ((c>b) && (c<a))
                    return;
            }
        }
    }
}
