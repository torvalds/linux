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


#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-uart.h>
#else
#include "cvmx.h"
#include "cvmx-uart.h"
#include "cvmx-interrupt.h"
#endif

#ifndef CVMX_BUILD_FOR_TOOLCHAIN
void cvmx_uart_enable_intr(int uart, cvmx_uart_intr_handler_t handler)
{
#ifndef CVMX_BUILD_FOR_LINUX_KERNEL
    cvmx_uart_ier_t ier;

    cvmx_interrupt_register(CVMX_IRQ_UART0 + uart, handler, NULL);
    /* Enable uart interrupts for debugger Control-C processing */
    ier.u64 = cvmx_read_csr(CVMX_MIO_UARTX_IER(uart));
    ier.s.erbfi = 1;
    cvmx_write_csr(CVMX_MIO_UARTX_IER(uart), ier.u64);

    cvmx_interrupt_unmask_irq(CVMX_IRQ_UART0 + uart);
#endif
}
#endif

static int cvmx_uart_simulator_p(void)
{
#ifndef CVMX_BUILD_FOR_TOOLCHAIN
  return cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM;
#else
  extern int __octeon_simulator_p;
  return __octeon_simulator_p;
#endif
}


/**
 * Function that does the real work of setting up the Octeon uart.
 * Takes all parameters as arguments, so it does not require gd
 * structure to be set up.
 * 
 * @param uart_index Index of uart to configure
 * @param cpu_clock_hertz
 *                   CPU clock frequency in Hz
 * @param baudrate   Baudrate to configure
 * 
 * @return 0 on success
 *         !0 on error
 */
int cvmx_uart_setup2(int uart_index, int cpu_clock_hertz, int baudrate)
{
    uint16_t divisor;
    cvmx_uart_fcr_t fcrval;
    cvmx_uart_mcr_t mcrval;
    cvmx_uart_lcr_t lcrval;

    fcrval.u64 = 0;
    fcrval.s.en = 1;    /* enable the FIFO's */
    fcrval.s.rxfr = 1;  /* reset the RX fifo */
    fcrval.s.txfr = 1;  /* reset the TX fifo */

    if (cvmx_uart_simulator_p())
        divisor = 1;
    else
        divisor = ((unsigned long)(cpu_clock_hertz + 8 * baudrate) / (unsigned long)(16 * baudrate));

    cvmx_write_csr(CVMX_MIO_UARTX_FCR(uart_index), fcrval.u64);

    mcrval.u64 = 0;
    if (uart_index == 1 && cvmx_uart_simulator_p())
      mcrval.s.afce = 1;  /* enable auto flow control for simulator. Needed for  gdb regression callfuncs.exp. */
    else
      mcrval.s.afce = 0;  /* disable auto flow control so board can power on without serial port connected */

    mcrval.s.rts = 1;   /* looks like this must be set for auto flow control to work */

    cvmx_read_csr(CVMX_MIO_UARTX_LSR(uart_index));

    lcrval.u64 = 0;
    lcrval.s.cls = CVMX_UART_BITS8;
    lcrval.s.stop = 0;  /* stop bit included? */
    lcrval.s.pen = 0;   /* no parity? */
    lcrval.s.eps = 1;   /* even parity? */
    lcrval.s.dlab = 1;  /* temporary to program the divisor */
    cvmx_write_csr(CVMX_MIO_UARTX_LCR(uart_index), lcrval.u64);

    cvmx_write_csr(CVMX_MIO_UARTX_DLL(uart_index), divisor & 0xff);
    cvmx_write_csr(CVMX_MIO_UARTX_DLH(uart_index), (divisor>>8) & 0xff);

    lcrval.s.dlab = 0;  /* divisor is programmed now, set this back to normal */
    cvmx_write_csr(CVMX_MIO_UARTX_LCR(uart_index), lcrval.u64);

    /* spec says need to wait after you program the divisor */
    if (!cvmx_uart_simulator_p())
    {
        uint64_t read_cycle;
        CVMX_MF_CYCLE (read_cycle);
        read_cycle += (2 * divisor * 16) + 10000;

        /* Spin */
        while (1)
        {
            uint64_t new_cycle;
            CVMX_MF_CYCLE (new_cycle);
            if (new_cycle >= read_cycle)
                break;
        }
    }

    /* Don't enable flow control until after baud rate is configured. - we don't want
    ** to allow characters in until after the baud rate is fully configured */
    cvmx_write_csr(CVMX_MIO_UARTX_MCR(uart_index), mcrval.u64);
    return 0;

}

/**
 * Setup a uart for use
 *
 * @param uart_index Uart to setup (0 or 1)
 * @return Zero on success
 */
int cvmx_uart_setup (int uart_index)
{
    return cvmx_uart_setup2(uart_index, cvmx_clock_get_rate (CVMX_CLOCK_SCLK), 115200);
}

