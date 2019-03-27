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
 * "cvmx-usbd.c" defines a set of low level USB functions to help
 * developers create Octeon USB devices for various operating
 * systems. These functions provide a generic API to the Octeon
 * USB blocks, hiding the internal hardware specific
 * operations.
 *
 * <hr>$Revision: 32636 $<hr>
 */

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-sysinfo.h>
#include <asm/octeon/cvmx-usbnx-defs.h>
#include <asm/octeon/cvmx-usbcx-defs.h>
#include <asm/octeon/cvmx-usbd.h>
#include <asm/octeon/cvmx-swap.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>
#else
#include "cvmx.h"
#include "cvmx-clock.h"
#include "cvmx-sysinfo.h"
#include "cvmx-usbd.h"
#include "cvmx-swap.h"
#include "cvmx-helper.h"
#include "cvmx-helper-board.h"
#endif

#define ULL unsigned long long

/**
 * @INTERNAL
 * Read a USB 32bit CSR. It performs the necessary address swizzle for 32bit
 * CSRs.
 *
 * @param usb     USB device state populated by
 *                cvmx_usbd_initialize().
 * @param address 64bit address to read
 *
 * @return Result of the read
 */
static inline uint32_t __cvmx_usbd_read_csr32(cvmx_usbd_state_t *usb, uint64_t address)
{
    uint32_t result = cvmx_read64_uint32(address ^ 4);
    return result;
}


/**
 * @INTERNAL
 * Write a USB 32bit CSR. It performs the necessary address swizzle for 32bit
 * CSRs.
 *
 * @param usb     USB device state populated by
 *                cvmx_usbd_initialize().
 * @param address 64bit address to write
 * @param value   Value to write
 */
static inline void __cvmx_usbd_write_csr32(cvmx_usbd_state_t *usb, uint64_t address, uint32_t value)
{
    cvmx_write64_uint32(address ^ 4, value);
    cvmx_read64_uint64(CVMX_USBNX_DMA0_INB_CHN0(usb->index));
}

/**
 * @INTERNAL
 * Calls the user supplied callback when an event happens.
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param reason Reason for the callback
 * @param endpoint_num
 *               Endpoint number
 * @param bytes_transferred
 *               Bytes transferred
 */
static void __cvmx_usbd_callback(cvmx_usbd_state_t *usb, cvmx_usbd_callback_t reason, int endpoint_num, int bytes_transferred)
{
    if (usb->callback[reason])
    {
        if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
            cvmx_dprintf("%s: Calling callback reason=%d endpoint=%d bytes=%d func=%p data=%p\n",
                __FUNCTION__, reason, endpoint_num, bytes_transferred, usb->callback[reason], usb->callback_data[reason]);
        usb->callback[reason](reason, endpoint_num, bytes_transferred, usb->callback_data[reason]);
    }
    else
    {
        if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
            cvmx_dprintf("%s: No callback for reason=%d endpoint=%d bytes=%d\n",
                __FUNCTION__, reason, endpoint_num, bytes_transferred);
    }
}

/**
 * @INTERNAL
 * Perform USB device mode initialization after a reset completes.
 * This should be called after USBC0/1_GINTSTS[USBRESET] and
 * corresponds to section 22.6.1.1, "Initialization on USB Reset",
 * in the manual.
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return Zero or negative on error.
 */
static int __cvmx_usbd_device_reset_complete(cvmx_usbd_state_t *usb)
{
    cvmx_usbcx_ghwcfg2_t usbcx_ghwcfg2;
    cvmx_usbcx_ghwcfg3_t usbcx_ghwcfg3;
    cvmx_usbcx_doepmsk_t usbcx_doepmsk;
    cvmx_usbcx_diepmsk_t usbcx_diepmsk;
    cvmx_usbcx_daintmsk_t usbc_daintmsk;
    cvmx_usbcx_gnptxfsiz_t gnptxfsiz;
    int fifo_space;
    int i;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: Processing reset\n", __FUNCTION__);

    usbcx_ghwcfg2.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_GHWCFG2(usb->index));
    usbcx_ghwcfg3.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_GHWCFG3(usb->index));

    /* Set up the data FIFO RAM for each of the FIFOs */
    fifo_space = usbcx_ghwcfg3.s.dfifodepth;

    /* Start at the top of the FIFO and assign space for each periodic fifo */
    for (i=usbcx_ghwcfg2.s.numdeveps; i>0; i--)
    {
        cvmx_usbcx_dptxfsizx_t siz;
        siz.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DPTXFSIZX(i, usb->index));
        fifo_space -= siz.s.dptxfsize;
        siz.s.dptxfstaddr = fifo_space;
        __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DPTXFSIZX(i, usb->index), siz.u32);
    }

    /* Assign half the leftover space to the non periodic tx fifo */
    gnptxfsiz.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_GNPTXFSIZ(usb->index));
    gnptxfsiz.s.nptxfdep = fifo_space / 2;
    fifo_space -= gnptxfsiz.s.nptxfdep;
    gnptxfsiz.s.nptxfstaddr = fifo_space;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_GNPTXFSIZ(usb->index), gnptxfsiz.u32);

    /* Assign the remain space to the RX fifo */
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_GRXFSIZ(usb->index), fifo_space);

    /* Unmask the common endpoint interrupts */
    usbcx_doepmsk.u32 = 0;
    usbcx_doepmsk.s.setupmsk = 1;
    usbcx_doepmsk.s.epdisbldmsk = 1;
    usbcx_doepmsk.s.xfercomplmsk = 1;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DOEPMSK(usb->index), usbcx_doepmsk.u32);
    usbcx_diepmsk.u32 = 0;
    usbcx_diepmsk.s.epdisbldmsk = 1;
    usbcx_diepmsk.s.xfercomplmsk = 1;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DIEPMSK(usb->index), usbcx_diepmsk.u32);

    usbc_daintmsk.u32 = 0;
    usbc_daintmsk.s.inepmsk = -1;
    usbc_daintmsk.s.outepmsk = -1;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DAINTMSK(usb->index), usbc_daintmsk.u32);

    /* Set all endpoints to NAK */
    for (i=0; i<usbcx_ghwcfg2.s.numdeveps+1; i++)
    {
        cvmx_usbcx_doepctlx_t usbc_doepctl;
        usbc_doepctl.u32 = 0;
        usbc_doepctl.s.snak = 1;
        usbc_doepctl.s.usbactep = 1;
        usbc_doepctl.s.mps = (i==0) ? 0 : 64;
        __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DOEPCTLX(i, usb->index), usbc_doepctl.u32);
    }

    return 0;
}


/**
 * Initialize a USB port for use. This must be called before any
 * other access to the Octeon USB port is made. The port starts
 * off in the disabled state.
 *
 * @param usb    Pointer to an empty cvmx_usbd_state_t structure
 *               that will be populated by the initialize call.
 *               This structure is then passed to all other USB
 *               functions.
 * @param usb_port_number
 *               Which Octeon USB port to initialize.
 * @param flags  Flags to control hardware initialization. See
 *               cvmx_usbd_initialize_flags_t for the flag
 *               definitions. Some flags are mandatory.
 *
 * @return Zero or a negative on error.
 */
int cvmx_usbd_initialize(cvmx_usbd_state_t *usb,
                                      int usb_port_number,
                                      cvmx_usbd_initialize_flags_t flags)
{
    cvmx_usbnx_clk_ctl_t usbn_clk_ctl;
    cvmx_usbnx_usbp_ctl_status_t usbn_usbp_ctl_status;

    if (cvmx_unlikely(flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: Called\n", __FUNCTION__);

    memset(usb, 0, sizeof(*usb));
    usb->init_flags = flags;
    usb->index = usb_port_number;

    /* Try to determine clock type automatically */
    if ((usb->init_flags & (CVMX_USBD_INITIALIZE_FLAGS_CLOCK_XO_XI |
                  CVMX_USBD_INITIALIZE_FLAGS_CLOCK_XO_GND)) == 0)
    {
        if (__cvmx_helper_board_usb_get_clock_type() == USB_CLOCK_TYPE_CRYSTAL_12)
            usb->init_flags |= CVMX_USBD_INITIALIZE_FLAGS_CLOCK_XO_XI;  /* Only 12 MHZ crystals are supported */
        else
            usb->init_flags |= CVMX_USBD_INITIALIZE_FLAGS_CLOCK_XO_GND;
    }

    if (usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_CLOCK_XO_GND)
    {
        /* Check for auto ref clock frequency */
        if (!(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_CLOCK_MHZ_MASK))
            switch (__cvmx_helper_board_usb_get_clock_type())
            {
                case USB_CLOCK_TYPE_REF_12:
                    usb->init_flags |= CVMX_USBD_INITIALIZE_FLAGS_CLOCK_12MHZ;
                    break;
                case USB_CLOCK_TYPE_REF_24:
                    usb->init_flags |= CVMX_USBD_INITIALIZE_FLAGS_CLOCK_24MHZ;
                    break;
                case USB_CLOCK_TYPE_REF_48:
                default:
                    usb->init_flags |= CVMX_USBD_INITIALIZE_FLAGS_CLOCK_48MHZ;
                    break;
            }
    }

    /* Power On Reset and PHY Initialization */

    /* 1. Wait for DCOK to assert (nothing to do) */
    /* 2a. Write USBN0/1_CLK_CTL[POR] = 1 and
        USBN0/1_CLK_CTL[HRST,PRST,HCLK_RST] = 0 */
    usbn_clk_ctl.u64 = cvmx_read_csr(CVMX_USBNX_CLK_CTL(usb->index));
    usbn_clk_ctl.s.por = 1;
    usbn_clk_ctl.s.hrst = 0;
    usbn_clk_ctl.s.prst = 0;
    usbn_clk_ctl.s.hclk_rst = 0;
    usbn_clk_ctl.s.enable = 0;
    /* 2b. Select the USB reference clock/crystal parameters by writing
        appropriate values to USBN0/1_CLK_CTL[P_C_SEL, P_RTYPE, P_COM_ON] */
    if (usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_CLOCK_XO_GND)
    {
        /* The USB port uses 12/24/48MHz 2.5V board clock
            source at USB_XO. USB_XI should be tied to GND.
            Most Octeon evaluation boards require this setting */
        if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
        {
            usbn_clk_ctl.cn31xx.p_rclk  = 1; /* From CN31XX,CN30XX manual */
            usbn_clk_ctl.cn31xx.p_xenbn = 0;
        }
        else if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
            usbn_clk_ctl.cn56xx.p_rtype = 2; /* From CN56XX,CN50XX manual */
        else
            usbn_clk_ctl.cn52xx.p_rtype = 1; /* From CN52XX manual */

        switch (usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_CLOCK_MHZ_MASK)
        {
            case CVMX_USBD_INITIALIZE_FLAGS_CLOCK_12MHZ:
                usbn_clk_ctl.s.p_c_sel = 0;
                break;
            case CVMX_USBD_INITIALIZE_FLAGS_CLOCK_24MHZ:
                usbn_clk_ctl.s.p_c_sel = 1;
                break;
            case CVMX_USBD_INITIALIZE_FLAGS_CLOCK_48MHZ:
                usbn_clk_ctl.s.p_c_sel = 2;
                break;
        }
    }
    else
    {
        /* The USB port uses a 12MHz crystal as clock source
            at USB_XO and USB_XI */
        if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
        {
            usbn_clk_ctl.cn31xx.p_rclk  = 1; /* From CN31XX,CN30XX manual */
            usbn_clk_ctl.cn31xx.p_xenbn = 1;
        }
        else if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
            usbn_clk_ctl.cn56xx.p_rtype = 0; /* From CN56XX,CN50XX manual */
        else
            usbn_clk_ctl.cn52xx.p_rtype = 0; /* From CN52XX manual */

        usbn_clk_ctl.s.p_c_sel = 0;
    }
    /* 2c. Select the HCLK via writing USBN0/1_CLK_CTL[DIVIDE, DIVIDE2] and
        setting USBN0/1_CLK_CTL[ENABLE] = 1.  Divide the core clock down such
        that USB is as close as possible to 125Mhz */
    {
        int divisor = (cvmx_clock_get_rate(CVMX_CLOCK_CORE)+125000000-1)/125000000;
        if (divisor < 4)  /* Lower than 4 doesn't seem to work properly */
            divisor = 4;
        usbn_clk_ctl.s.divide = divisor;
        usbn_clk_ctl.s.divide2 = 0;
    }
    cvmx_write_csr(CVMX_USBNX_CLK_CTL(usb->index), usbn_clk_ctl.u64);
    /* 2d. Write USBN0/1_CLK_CTL[HCLK_RST] = 1 */
    usbn_clk_ctl.s.hclk_rst = 1;
    cvmx_write_csr(CVMX_USBNX_CLK_CTL(usb->index), usbn_clk_ctl.u64);
    /* 2e.  Wait 64 core-clock cycles for HCLK to stabilize */
    cvmx_wait(64);
    /* 3. Program the power-on reset field in the USBN clock-control register:
        USBN_CLK_CTL[POR] = 0 */
    usbn_clk_ctl.s.por = 0;
    cvmx_write_csr(CVMX_USBNX_CLK_CTL(usb->index), usbn_clk_ctl.u64);
    /* 4. Wait 1 ms for PHY clock to start */
    cvmx_wait_usec(1000);
    /* 5. Program the Reset input from automatic test equipment field in the
        USBP control and status register: USBN_USBP_CTL_STATUS[ATE_RESET] = 1 */
    usbn_usbp_ctl_status.u64 = cvmx_read_csr(CVMX_USBNX_USBP_CTL_STATUS(usb->index));
    usbn_usbp_ctl_status.s.ate_reset = 1;
    cvmx_write_csr(CVMX_USBNX_USBP_CTL_STATUS(usb->index), usbn_usbp_ctl_status.u64);
    /* 6. Wait 10 cycles */
    cvmx_wait(10);
    /* 7. Clear ATE_RESET field in the USBN clock-control register:
        USBN_USBP_CTL_STATUS[ATE_RESET] = 0 */
    usbn_usbp_ctl_status.s.ate_reset = 0;
    cvmx_write_csr(CVMX_USBNX_USBP_CTL_STATUS(usb->index), usbn_usbp_ctl_status.u64);
    /* 8. Program the PHY reset field in the USBN clock-control register:
        USBN_CLK_CTL[PRST] = 1 */
    usbn_clk_ctl.s.prst = 1;
    cvmx_write_csr(CVMX_USBNX_CLK_CTL(usb->index), usbn_clk_ctl.u64);
    /* 9. Program the USBP control and status register to select host or
        device mode. USBN_USBP_CTL_STATUS[HST_MODE] = 0 for host, = 1 for
        device */
    usbn_usbp_ctl_status.s.hst_mode = 1;
    usbn_usbp_ctl_status.s.dm_pulld = 0;
    usbn_usbp_ctl_status.s.dp_pulld = 0;
    cvmx_write_csr(CVMX_USBNX_USBP_CTL_STATUS(usb->index), usbn_usbp_ctl_status.u64);
    /* 10. Wait 1 µs */
    cvmx_wait_usec(1);
    /* 11. Program the hreset_n field in the USBN clock-control register:
        USBN_CLK_CTL[HRST] = 1 */
    usbn_clk_ctl.s.hrst = 1;
    cvmx_write_csr(CVMX_USBNX_CLK_CTL(usb->index), usbn_clk_ctl.u64);
    /* 12. Proceed to USB core initialization */
    usbn_clk_ctl.s.enable = 1;
    cvmx_write_csr(CVMX_USBNX_CLK_CTL(usb->index), usbn_clk_ctl.u64);
    cvmx_wait_usec(1);

    /* Program the following fields in the global AHB configuration
        register (USBC_GAHBCFG)
        DMA mode, USBC_GAHBCFG[DMAEn]: 1 = DMA mode, 0 = slave mode
        Burst length, USBC_GAHBCFG[HBSTLEN] = 0
        Nonperiodic TxFIFO empty level (slave mode only),
        USBC_GAHBCFG[NPTXFEMPLVL]
        Periodic TxFIFO empty level (slave mode only),
        USBC_GAHBCFG[PTXFEMPLVL]
        Global interrupt mask, USBC_GAHBCFG[GLBLINTRMSK] = 1 */
    {
        cvmx_usbcx_gahbcfg_t usbcx_gahbcfg;
        usbcx_gahbcfg.u32 = 0;
        usbcx_gahbcfg.s.dmaen = 1;
        usbcx_gahbcfg.s.hbstlen = 0;
        usbcx_gahbcfg.s.nptxfemplvl = 1;
        usbcx_gahbcfg.s.ptxfemplvl = 1;
        usbcx_gahbcfg.s.glblintrmsk = 1;
        __cvmx_usbd_write_csr32(usb, CVMX_USBCX_GAHBCFG(usb->index), usbcx_gahbcfg.u32);
    }

    /* Program the following fields in USBC_GUSBCFG register.
        HS/FS timeout calibration, USBC_GUSBCFG[TOUTCAL] = 0
        ULPI DDR select, USBC_GUSBCFG[DDRSEL] = 0
        USB turnaround time, USBC_GUSBCFG[USBTRDTIM] = 0x5
        PHY low-power clock select, USBC_GUSBCFG[PHYLPWRCLKSEL] = 0 */
    {
        cvmx_usbcx_gusbcfg_t usbcx_gusbcfg;
        usbcx_gusbcfg.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_GUSBCFG(usb->index));
        usbcx_gusbcfg.s.toutcal = 0;
        usbcx_gusbcfg.s.ddrsel = 0;
        usbcx_gusbcfg.s.usbtrdtim = 0x5;
        usbcx_gusbcfg.s.phylpwrclksel = 0;
        __cvmx_usbd_write_csr32(usb, CVMX_USBCX_GUSBCFG(usb->index), usbcx_gusbcfg.u32);
    }

    /* Program the following fields in the USBC0/1_DCFG register:
        Device speed, USBC0/1_DCFG[DEVSPD] = 0 (high speed)
        Non-zero-length status OUT handshake, USBC0/1_DCFG[NZSTSOUTHSHK]=0
        Periodic frame interval (if periodic endpoints are supported),
        USBC0/1_DCFG[PERFRINT] = 1 */
    {
        cvmx_usbcx_dcfg_t usbcx_dcfg;
        usbcx_dcfg.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DCFG(usb->index));
        usbcx_dcfg.s.devspd = 0;
        usbcx_dcfg.s.nzstsouthshk = 0;
        usbcx_dcfg.s.perfrint = 1;
        __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DCFG(usb->index), usbcx_dcfg.u32);
    }

    /* Program the USBC0/1_GINTMSK register */
    {
        cvmx_usbcx_gintmsk_t usbcx_gintmsk;
        usbcx_gintmsk.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_GINTMSK(usb->index));
        usbcx_gintmsk.s.oepintmsk = 1;
        usbcx_gintmsk.s.inepintmsk = 1;
        usbcx_gintmsk.s.enumdonemsk = 1;
        usbcx_gintmsk.s.usbrstmsk = 1;
        usbcx_gintmsk.s.usbsuspmsk = 1;
        __cvmx_usbd_write_csr32(usb, CVMX_USBCX_GINTMSK(usb->index), usbcx_gintmsk.u32);
    }

    cvmx_usbd_disable(usb);
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_initialize);
#endif


/**
 * Shutdown a USB port after a call to cvmx_usbd_initialize().
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return Zero or a negative on error.
 */
int cvmx_usbd_shutdown(cvmx_usbd_state_t *usb)
{
    cvmx_usbnx_clk_ctl_t usbn_clk_ctl;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: Called\n", __FUNCTION__);

    /* Disable the clocks and put them in power on reset */
    usbn_clk_ctl.u64 = cvmx_read_csr(CVMX_USBNX_CLK_CTL(usb->index));
    usbn_clk_ctl.s.enable = 1;
    usbn_clk_ctl.s.por = 1;
    usbn_clk_ctl.s.hclk_rst = 1;
    usbn_clk_ctl.s.prst = 0;
    usbn_clk_ctl.s.hrst = 0;
    cvmx_write_csr(CVMX_USBNX_CLK_CTL(usb->index), usbn_clk_ctl.u64);
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_shutdown);
#endif


/**
 * Enable a USB port. After this call succeeds, the USB port is
 * online and servicing requests.
 *
 * @param usb  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return Zero or negative on error.
 */
int cvmx_usbd_enable(cvmx_usbd_state_t *usb)
{
    cvmx_usbcx_dctl_t usbcx_dctl;
    usbcx_dctl.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DCTL(usb->index));
    usbcx_dctl.s.cgoutnak = 1;
    usbcx_dctl.s.sftdiscon = 0;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DCTL(usb->index), usbcx_dctl.u32);
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_enable);
#endif


/**
 * Disable a USB port. After this call the USB port will not
 * generate data transfers and will not generate events.
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return Zero or negative on error.
 */
int cvmx_usbd_disable(cvmx_usbd_state_t *usb)
{
    cvmx_usbcx_dctl_t usbcx_dctl;
    usbcx_dctl.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DCTL(usb->index));
    usbcx_dctl.s.sgoutnak = 1;
    usbcx_dctl.s.sftdiscon = 1;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DCTL(usb->index), usbcx_dctl.u32);
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_disable);
#endif


/**
 * Register a callback function to process USB events
 *
 * @param usb       USB device state populated by
 *                  cvmx_usbd_initialize().
 * @param reason    The reason this callback should be called
 * @param func      Function to call
 * @param user_data User supplied data for the callback
 *
 * @return Zero on succes, negative on failure
 */
int cvmx_usbd_register(cvmx_usbd_state_t *usb, cvmx_usbd_callback_t reason, cvmx_usbd_callback_func_t func, void *user_data)
{
    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: Register reason=%d func=%p data=%p\n",
            __FUNCTION__, reason, func, user_data);
    usb->callback[reason] = func;
    usb->callback_data[reason] = user_data;
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_register);
#endif

/**
 * @INTERNAL
 * Poll a device mode endpoint for status
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint to poll
 *
 * @return Zero on success
 */
static int __cvmx_usbd_poll_in_endpoint(cvmx_usbd_state_t *usb, int endpoint_num)
{
    cvmx_usbcx_diepintx_t usbc_diepint;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: endpoint=%d\n", __FUNCTION__, endpoint_num);

    usbc_diepint.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DIEPINTX(endpoint_num, usb->index));
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DIEPINTX(endpoint_num, usb->index), usbc_diepint.u32);

    if (usbc_diepint.s.epdisbld)
    {
        /* Endpoint Disabled Interrupt (EPDisbld)
            This bit indicates that the endpoint is disabled per the
            application's request. */
        /* Nothing to do */
    }
    if (usbc_diepint.s.xfercompl)
    {
        cvmx_usbcx_dieptsizx_t usbc_dieptsiz;
        int bytes_transferred;
        /* Transfer Completed Interrupt (XferCompl)
            Indicates that the programmed transfer is complete on the AHB
            as well as on the USB, for this endpoint. */
        usbc_dieptsiz.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DIEPTSIZX(endpoint_num, usb->index));
        bytes_transferred = usb->endpoint[endpoint_num].buffer_length - usbc_dieptsiz.s.xfersize;
        __cvmx_usbd_callback(usb, CVMX_USBD_CALLBACK_IN_COMPLETE, endpoint_num, bytes_transferred);
    }
    return 0;
}


/**
 * @INTERNAL
 * Poll a device mode endpoint for status
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint to poll
 *
 * @return Zero on success
 */
static int __cvmx_usbd_poll_out_endpoint(cvmx_usbd_state_t *usb, int endpoint_num)
{
    cvmx_usbcx_doepintx_t usbc_doepint;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: endpoint=%d\n", __FUNCTION__, endpoint_num);

    usbc_doepint.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DOEPINTX(endpoint_num, usb->index));
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DOEPINTX(endpoint_num, usb->index), usbc_doepint.u32);

    if (usbc_doepint.s.setup)
    {
        /* SETUP Phase Done (SetUp)
            Applies to control OUT endpoints only.
            Indicates that the SETUP phase for the control endpoint is
            complete and no more back-to-back SETUP packets were
            received for the current control transfer. On this interrupt, the
            application can decode the received SETUP data packet. */
        __cvmx_usbd_callback(usb, CVMX_USBD_CALLBACK_DEVICE_SETUP, endpoint_num, 0);
    }
    if (usbc_doepint.s.epdisbld)
    {
        /* Endpoint Disabled Interrupt (EPDisbld)
            This bit indicates that the endpoint is disabled per the
            application's request. */
        /* Nothing to do */
    }
    if (usbc_doepint.s.xfercompl)
    {
        cvmx_usbcx_doeptsizx_t usbc_doeptsiz;
        int bytes_transferred;
        /* Transfer Completed Interrupt (XferCompl)
            Indicates that the programmed transfer is complete on the AHB
            as well as on the USB, for this endpoint. */
        usbc_doeptsiz.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DOEPTSIZX(endpoint_num, usb->index));
        bytes_transferred = usb->endpoint[endpoint_num].buffer_length - usbc_doeptsiz.s.xfersize;
        __cvmx_usbd_callback(usb, CVMX_USBD_CALLBACK_OUT_COMPLETE, endpoint_num, bytes_transferred);
    }

    return 0;
}


/**
 * Poll the USB block for status and call all needed callback
 * handlers. This function is meant to be called in the interrupt
 * handler for the USB controller. It can also be called
 * periodically in a loop for non-interrupt based operation.
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return Zero or negative on error.
 */
int cvmx_usbd_poll(cvmx_usbd_state_t *usb)
{
    cvmx_usbcx_gintsts_t usbc_gintsts;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: Called\n", __FUNCTION__);

    /* Read the pending interrupts */
    usbc_gintsts.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_GINTSTS(usb->index));
    usbc_gintsts.u32 &= __cvmx_usbd_read_csr32(usb, CVMX_USBCX_GINTMSK(usb->index));

    /* Clear the interrupts now that we know about them */
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_GINTSTS(usb->index), usbc_gintsts.u32);

    if (usbc_gintsts.s.usbsusp)
        __cvmx_usbd_callback(usb, CVMX_USBD_CALLBACK_SUSPEND, 0, 0);

    if (usbc_gintsts.s.enumdone)
        __cvmx_usbd_callback(usb, CVMX_USBD_CALLBACK_ENUM_COMPLETE, 0, 0);

    if (usbc_gintsts.s.usbrst)
    {
        /* USB Reset (USBRst)
            The core sets this bit to indicate that a reset is
            detected on the USB. */
        __cvmx_usbd_device_reset_complete(usb);
        __cvmx_usbd_callback(usb, CVMX_USBD_CALLBACK_RESET, 0, 0);
    }

    if (usbc_gintsts.s.oepint || usbc_gintsts.s.iepint)
    {
        cvmx_usbcx_daint_t usbc_daint;
        usbc_daint.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DAINT(usb->index));
        if (usbc_daint.s.inepint)
        {
            int active_endpoints = usbc_daint.s.inepint;

            while (active_endpoints)
            {
                int endpoint;
                CVMX_CLZ(endpoint, active_endpoints);
                endpoint = 31 - endpoint;
                __cvmx_usbd_poll_in_endpoint(usb, endpoint);
                active_endpoints ^= 1<<endpoint;
            }
        }
        if (usbc_daint.s.outepint)
        {
            int active_endpoints = usbc_daint.s.outepint;

            while (active_endpoints)
            {
                int endpoint;
                CVMX_CLZ(endpoint, active_endpoints);
                endpoint = 31 - endpoint;
                __cvmx_usbd_poll_out_endpoint(usb, endpoint);
                active_endpoints ^= 1<<endpoint;
            }
        }
    }

    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_poll);
#endif

/**
 * Get the current USB address
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return The USB address
 */
int cvmx_usbd_get_address(cvmx_usbd_state_t *usb)
{
    cvmx_usbcx_dcfg_t usbc_dcfg;
    usbc_dcfg.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DCFG(usb->index));
    return usbc_dcfg.s.devaddr;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_get_address);
#endif

/**
 * Set the current USB address
 *
 * @param usb     USB device state populated by
 *                cvmx_usbd_initialize().
 * @param address Address to set
 */
void cvmx_usbd_set_address(cvmx_usbd_state_t *usb, int address)
{
    cvmx_usbcx_dcfg_t usbc_dcfg;
    usbc_dcfg.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DCFG(usb->index));
    usbc_dcfg.s.devaddr = address;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DCFG(usb->index), usbc_dcfg.u32);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_set_address);
#endif

/**
 * Get the current USB speed
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return The USB speed
 */
cvmx_usbd_speed_t cvmx_usbd_get_speed(cvmx_usbd_state_t *usb)
{
    cvmx_usbcx_dsts_t usbcx_dsts;
    usbcx_dsts.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DSTS(usb->index));
    return usbcx_dsts.s.enumspd;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_get_speed);
#endif

/**
 * Set the current USB speed
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param speed  The requested speed
 */
void cvmx_usbd_set_speed(cvmx_usbd_state_t *usb, cvmx_usbd_speed_t speed)
{
    cvmx_usbcx_dcfg_t usbcx_dcfg;
    usbcx_dcfg.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DCFG(usb->index));
    usbcx_dcfg.s.devspd = speed;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DCFG(usb->index), usbcx_dcfg.u32);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_set_speed);
#endif

/**
 * Enable an endpoint to respond to an OUT transaction
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint number to enable
 * @param transfer_type
 *               Transfer type for the endpoint
 * @param max_packet_size
 *               Maximum packet size for the endpoint
 * @param buffer Buffer to receive the data
 * @param buffer_length
 *               Length of the buffer in bytes
 *
 * @return Zero on success, negative on failure
 */
int cvmx_usbd_out_endpoint_enable(cvmx_usbd_state_t *usb,
    int endpoint_num, cvmx_usbd_transfer_t transfer_type,
    int max_packet_size, uint64_t buffer, int buffer_length)
{
    cvmx_usbcx_doepctlx_t usbc_doepctl;
    cvmx_usbcx_doeptsizx_t usbc_doeptsiz;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: endpoint=%d buffer=0x%llx length=%d\n",
            __FUNCTION__, endpoint_num, (ULL)buffer, buffer_length);

    usb->endpoint[endpoint_num].buffer_length = buffer_length;

    CVMX_SYNCW; /* Flush out pending writes before enable */

    /* Clear any pending interrupts */
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DOEPINTX(endpoint_num, usb->index),
        __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DOEPINTX(endpoint_num, usb->index)));

    /* Setup the locations the DMA engines use  */
    cvmx_write_csr(CVMX_USBNX_DMA0_INB_CHN0(usb->index) + endpoint_num*8, buffer);

    usbc_doeptsiz.u32 = 0;
    usbc_doeptsiz.s.mc = 1;
    usbc_doeptsiz.s.pktcnt = (buffer_length + max_packet_size - 1) / max_packet_size;
    if (usbc_doeptsiz.s.pktcnt == 0)
        usbc_doeptsiz.s.pktcnt = 1;
    usbc_doeptsiz.s.xfersize = buffer_length;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DOEPTSIZX(endpoint_num, usb->index), usbc_doeptsiz.u32);

    usbc_doepctl.u32 = 0;
    usbc_doepctl.s.epena = 1;
    usbc_doepctl.s.setd1pid = 0;
    usbc_doepctl.s.setd0pid = 0;
    usbc_doepctl.s.cnak = 1;
    usbc_doepctl.s.eptype = transfer_type;
    usbc_doepctl.s.usbactep = 1;
    if (endpoint_num == 0)
    {
        switch (max_packet_size)
        {
            case 8:
                usbc_doepctl.s.mps = 3;
                break;
            case 16:
                usbc_doepctl.s.mps = 2;
                break;
            case 32:
                usbc_doepctl.s.mps = 1;
                break;
            default:
                usbc_doepctl.s.mps = 0;
                break;
        }
    }
    else
        usbc_doepctl.s.mps = max_packet_size;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DOEPCTLX(endpoint_num, usb->index), usbc_doepctl.u32);

    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_out_endpoint_enable);
#endif


/**
 * Disable an OUT endpoint
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint number to disable
 *
 * @return Zero on success, negative on failure
 */
int cvmx_usbd_out_endpoint_disable(cvmx_usbd_state_t *usb, int endpoint_num)
{
    cvmx_usbcx_doepctlx_t usbc_doepctl;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: endpoint=%d\n", __FUNCTION__, endpoint_num);

    usbc_doepctl.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DOEPCTLX(endpoint_num, usb->index));
    if (usbc_doepctl.s.epena && !usbc_doepctl.s.epdis)
    {
        usbc_doepctl.s.epdis = 1;
        __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DOEPCTLX(endpoint_num, usb->index), usbc_doepctl.u32);
    }
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_out_endpoint_disable);
#endif


/**
 * Enable an endpoint to respond to an IN transaction
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint number to enable
 * @param transfer_type
 *               Transfer type for the endpoint
 * @param max_packet_size
 *               Maximum packet size for the endpoint
 * @param buffer Buffer to send
 * @param buffer_length
 *               Length of the buffer in bytes
 *
 * @return Zero on success, negative on failure
 */
int cvmx_usbd_in_endpoint_enable(cvmx_usbd_state_t *usb,
    int endpoint_num, cvmx_usbd_transfer_t transfer_type,
    int max_packet_size, uint64_t buffer, int buffer_length)
{
    cvmx_usbcx_diepctlx_t usbc_diepctl;
    cvmx_usbcx_dieptsizx_t usbc_dieptsiz;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: endpoint=%d buffer=0x%llx length=%d\n",
            __FUNCTION__, endpoint_num, (ULL)buffer, buffer_length);

    usb->endpoint[endpoint_num].buffer_length = buffer_length;

    CVMX_SYNCW; /* Flush out pending writes before enable */

    /* Clear any pending interrupts */
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DIEPINTX(endpoint_num, usb->index),
        __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DIEPINTX(endpoint_num, usb->index)));

    usbc_dieptsiz.u32 = 0;
    usbc_dieptsiz.s.mc = 1;
    if (buffer)
    {
        cvmx_write_csr(CVMX_USBNX_DMA0_OUTB_CHN0(usb->index) + endpoint_num*8, buffer);
        usbc_dieptsiz.s.pktcnt = (buffer_length + max_packet_size - 1) / max_packet_size;
        if (usbc_dieptsiz.s.pktcnt == 0)
            usbc_dieptsiz.s.pktcnt = 1;
        usbc_dieptsiz.s.xfersize = buffer_length;
    }
    else
    {
        usbc_dieptsiz.s.pktcnt = 0;
        usbc_dieptsiz.s.xfersize = 0;
    }
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DIEPTSIZX(endpoint_num, usb->index), usbc_dieptsiz.u32);

    usbc_diepctl.u32 = 0;
    usbc_diepctl.s.epena = (buffer != 0);
    usbc_diepctl.s.setd1pid = 0;
    usbc_diepctl.s.setd0pid = (buffer == 0);
    usbc_diepctl.s.cnak = 1;
    usbc_diepctl.s.txfnum = endpoint_num;
    usbc_diepctl.s.eptype = transfer_type;
    usbc_diepctl.s.usbactep = 1;
    usbc_diepctl.s.nextep = endpoint_num;
    if (endpoint_num == 0)
    {
        switch (max_packet_size)
        {
            case 8:
                usbc_diepctl.s.mps = 3;
                break;
            case 16:
                usbc_diepctl.s.mps = 2;
                break;
            case 32:
                usbc_diepctl.s.mps = 1;
                break;
            default:
                usbc_diepctl.s.mps = 0;
                break;
        }
    }
    else
        usbc_diepctl.s.mps = max_packet_size;
    __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DIEPCTLX(endpoint_num, usb->index), usbc_diepctl.u32);

    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_in_endpoint_enable);
#endif


/**
 * Disable an IN endpoint
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint number to disable
 *
 * @return Zero on success, negative on failure
 */
int cvmx_usbd_in_endpoint_disable(cvmx_usbd_state_t *usb, int endpoint_num)
{
    cvmx_usbcx_diepctlx_t usbc_diepctl;

    if (cvmx_unlikely(usb->init_flags & CVMX_USBD_INITIALIZE_FLAGS_DEBUG))
        cvmx_dprintf("%s: endpoint=%d\n", __FUNCTION__, endpoint_num);

    usbc_diepctl.u32 = __cvmx_usbd_read_csr32(usb, CVMX_USBCX_DIEPCTLX(endpoint_num, usb->index));
    if (usbc_diepctl.s.epena && !usbc_diepctl.s.epdis)
    {
        usbc_diepctl.s.epdis = 1;
        __cvmx_usbd_write_csr32(usb, CVMX_USBCX_DIEPCTLX(endpoint_num, usb->index), usbc_diepctl.u32);
    }
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_usbd_in_endpoint_disable);
#endif

