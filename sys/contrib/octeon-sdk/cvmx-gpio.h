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
 * General Purpose IO interface.
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_GPIO_H__
#define __CVMX_GPIO_H__

#ifdef	__cplusplus
extern "C" {
#endif

/* CSR typedefs have been moved to cvmx-gpio-defs.h */

/**
 * Clear the interrupt rising edge detector for the supplied
 * pins in the mask. Chips which have more than 16 GPIO pins
 * can't use them for interrupts.
 e
 * @param clear_mask Mask of pins to clear
 */
static inline void cvmx_gpio_interrupt_clear(uint16_t clear_mask)
{
    if (OCTEON_IS_MODEL(OCTEON_CN61XX))
    {
        cvmx_gpio_multi_cast_t multi_cast;
        cvmx_gpio_bit_cfgx_t gpio_bit;
        int core = cvmx_get_core_num();
        
        multi_cast.u64 = cvmx_read_csr(CVMX_GPIO_MULTI_CAST);
        gpio_bit.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(core));

        /* If Multicast mode is enabled, and GPIO interrupt is enabled for
           edge detection, then GPIO<4..7> interrupts are per core */
        if (multi_cast.s.en && gpio_bit.s.int_en && gpio_bit.s.int_type)
        {
            /* Clear GPIO<4..7> per core */
            cvmx_ciu_intx_sum0_t ciu_sum0;
            ciu_sum0.u64 = cvmx_read_csr(CVMX_CIU_INTX_SUM0(core * 2));
            ciu_sum0.s.gpio = clear_mask & 0xf0;
            cvmx_write_csr(CVMX_CIU_INTX_SUM0(core * 2), ciu_sum0.u64);

            /* Clear other GPIO pins for all cores. */
            cvmx_write_csr(CVMX_GPIO_INT_CLR, (clear_mask & ~0xf0));
            return;
        }
    }
    /* Clear GPIO pins state across all cores and common interrupt states. */ 
    cvmx_gpio_int_clr_t gpio_int_clr;
    gpio_int_clr.u64 = 0;
    gpio_int_clr.s.type = clear_mask;
    cvmx_write_csr(CVMX_GPIO_INT_CLR, gpio_int_clr.u64);
}

/**
 * GPIO Output Pin
 *
 * @param bit   The GPIO to use
 * @param mode  Drive GPIO as output pin or not.
 *
 */
static inline void cvmx_gpio_cfg(int bit, int mode)
{
    if (bit > 15 && bit < 20)
    {
        /* CN61XX/CN66XX has 20 GPIO pins and only 16 are interruptable. */
        if (OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN66XX))
        {
            cvmx_gpio_xbit_cfgx_t gpio_xbit;
            gpio_xbit.u64 = cvmx_read_csr(CVMX_GPIO_XBIT_CFGX(bit));
            if (mode)
                gpio_xbit.s.tx_oe = 1;
            else
                gpio_xbit.s.tx_oe = 0;
            cvmx_write_csr(CVMX_GPIO_XBIT_CFGX(bit), gpio_xbit.u64);
        }
        else
            cvmx_dprintf("cvmx_gpio_cfg: Invalid GPIO bit(%d)\n", bit);
    }
    else
    {
        cvmx_gpio_bit_cfgx_t gpio_bit;
        gpio_bit.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(bit));
        if (mode)
            gpio_bit.s.tx_oe = 1;
        else
            gpio_bit.s.tx_oe = 0;
        cvmx_write_csr(CVMX_GPIO_BIT_CFGX(bit), gpio_bit.u64);
    }
}

/**
 * GPIO Read Data
 *
 * @return Status of the GPIO pins
 */
static inline uint32_t cvmx_gpio_read(void)
{
    cvmx_gpio_rx_dat_t gpio_rx_dat;
    gpio_rx_dat.u64 = cvmx_read_csr(CVMX_GPIO_RX_DAT);
    return gpio_rx_dat.s.dat;
}


/**
 * GPIO Clear pin
 *
 * @param clear_mask Bit mask to indicate which bits to drive to '0'.
 */
static inline void cvmx_gpio_clear(uint32_t clear_mask)
{
    cvmx_gpio_tx_clr_t gpio_tx_clr;
    gpio_tx_clr.u64 = 0;
    gpio_tx_clr.s.clr = clear_mask;
    cvmx_write_csr(CVMX_GPIO_TX_CLR, gpio_tx_clr.u64);
}


/**
 * GPIO Set pin
 *
 * @param set_mask Bit mask to indicate which bits to drive to '1'.
 */
static inline void cvmx_gpio_set(uint32_t set_mask)
{
    cvmx_gpio_tx_set_t gpio_tx_set;
    gpio_tx_set.u64 = 0;
    gpio_tx_set.s.set = set_mask;
    cvmx_write_csr(CVMX_GPIO_TX_SET, gpio_tx_set.u64);
}

#ifdef	__cplusplus
}
#endif

#endif

