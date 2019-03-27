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
 * interface to the serial port UART hardware
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifndef __CVMX_UART_H__
#define __CVMX_UART_H__

#ifdef	__cplusplus
extern "C" {
#endif

#define CVMX_UART_NUM_PORTS     2
#define CVMX_UART_TX_FIFO_SIZE  64
#define CVMX_UART_RX_FIFO_SIZE  64

/* CSR typedefs have been moved to cvmx-uart-defs.h */

typedef void (*cvmx_uart_intr_handler_t)(int, uint64_t[], void *);

extern void cvmx_uart_enable_intr(int, cvmx_uart_intr_handler_t);
extern int cvmx_uart_setup2(int, int, int);
extern int cvmx_uart_setup(int);

/* Defined in libc.  */
unsigned __octeon_uart_trylock (void);
void __octeon_uart_unlock (void);

/**
 * Get a single byte from serial port.
 *
 * @param uart_index Uart to read from (0 or 1)
 * @return The byte read
 */
static inline uint8_t cvmx_uart_read_byte(int uart_index)
{
    cvmx_uart_lsr_t lsrval;

    /* Spin until data is available */
    do
    {
        lsrval.u64 = cvmx_read_csr(CVMX_MIO_UARTX_LSR(uart_index));
    } while (!lsrval.s.dr);

    /* Read and return the data */
    return cvmx_read_csr(CVMX_MIO_UARTX_RBR(uart_index));
}

/**
 * Get a single byte from serial port with a timeout.
 *     
 * @param uart_index Uart to read from (0 or 1)
 * @param timedout Record if a timeout has happened
 * @param timeout the timeout count
 * @return The byte read
 */
static inline uint8_t cvmx_uart_read_byte_with_timeout(int uart_index, int *timedout, volatile unsigned timeout)
{
    cvmx_uart_lsr_t lsrval;

    /* Spin until data is available */
    *timedout = 0;
    do
    {
        if(timeout == 0)
        {
            *timedout = 1;
            return -1;
        }
        lsrval.u64 = cvmx_read_csr(CVMX_MIO_UARTX_LSR(uart_index));
        timeout --;
    } while (!lsrval.s.dr);

    /* Read and return the data */
    return cvmx_read_csr(CVMX_MIO_UARTX_RBR(uart_index));
}


/**
 * Put a single byte to uart port.
 *
 * @param uart_index Uart to write to (0 or 1)
 * @param ch         Byte to write
 */
static inline void cvmx_uart_write_byte(int uart_index, uint8_t ch)
{
    cvmx_uart_lsr_t lsrval;

    /* Spin until there is room */
    do
    {
        lsrval.u64 = cvmx_read_csr(CVMX_MIO_UARTX_LSR(uart_index));
    }
    while (lsrval.s.thre == 0);

    /* Write the byte */
    cvmx_write_csr(CVMX_MIO_UARTX_THR(uart_index), ch);
}

/**
 * Write a string to the uart
 *
 * @param uart_index Uart to use (0 or 1)
 * @param str        String to write
 */
static inline void cvmx_uart_write_string(int uart_index, const char *str)
{
    /* Just loop writing one byte at a time */
    while (*str)
    {
        cvmx_uart_write_byte(uart_index, *str);
        str++;
    }
}

#ifdef	__cplusplus
}
#endif

#endif /*  __CVM_UART_H__ */
