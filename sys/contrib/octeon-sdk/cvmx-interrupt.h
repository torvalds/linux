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
 * Interface to the Mips interrupts.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifndef __CVMX_INTERRUPT_H__
#define __CVMX_INTERRUPT_H__

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Enumeration of Interrupt numbers
 */
typedef enum
{
    /* 0 - 7 represent the 8 MIPS standard interrupt sources */
    CVMX_IRQ_SW0        = 0,
    CVMX_IRQ_SW1,
    CVMX_IRQ_MIPS2,
    CVMX_IRQ_MIPS3,
    CVMX_IRQ_MIPS4,
    CVMX_IRQ_MIPS5,
    CVMX_IRQ_MIPS6,
    CVMX_IRQ_MIPS7,
    /* 64 WORKQ interrupts. */
    CVMX_IRQ_WORKQ0,
    /* 16 GPIO interrupts. */
    CVMX_IRQ_GPIO0      = CVMX_IRQ_WORKQ0 + 64,
    /* 4  MBOX interrupts. */
    CVMX_IRQ_MBOX0      = CVMX_IRQ_GPIO0 + 16,
    /* 3  UART interrupts. */
    CVMX_IRQ_UART0      = CVMX_IRQ_MBOX0 + 4,
    CVMX_IRQ_PCI_INT0   = CVMX_IRQ_UART0 + 3,
    CVMX_IRQ_PCI_INT1,
    CVMX_IRQ_PCI_INT2,
    CVMX_IRQ_PCI_INT3,
    CVMX_IRQ_PCI_MSI0,
    CVMX_IRQ_PCI_MSI1,
    CVMX_IRQ_PCI_MSI2,
    CVMX_IRQ_PCI_MSI3,
    /* 2 TWSI interrupts */
    CVMX_IRQ_TWSI0,
    CVMX_IRQ_RML        = CVMX_IRQ_TWSI0 + 2,
    /* 4 TRACE interrupts added in CN68XX */
    CVMX_IRQ_TRACE0,
    /* 5 GMX_DRP interrupts added in CN68XX */
    CVMX_IRQ_GMX_DRP0 = CVMX_IRQ_TRACE0 + 4,
    CVMX_IRQ_GMX_DRP1,   /* Doesn't apply on CN52XX or CN63XX */
    CVMX_IRQ_IPD_DRP = CVMX_IRQ_GMX_DRP0 + 5,
    CVMX_IRQ_KEY_ZERO,   /* Doesn't apply on CN52XX or CN63XX */
    /* 4 TIMER interrupts. */
    CVMX_IRQ_TIMER0,
    /* 2 USB interrupts. */
    CVMX_IRQ_USB0       = CVMX_IRQ_TIMER0 + 4,   /* Doesn't apply on CN38XX or CN58XX */
    CVMX_IRQ_PCM        = CVMX_IRQ_USB0 + 2,   /* Doesn't apply on CN52XX or CN63XX */
    CVMX_IRQ_MPI,   /* Doesn't apply on CN52XX or CN63XX */
    CVMX_IRQ_POWIQ,   /* Added in CN56XX */
    CVMX_IRQ_IPDPPTHR,   /* Added in CN56XX */
    /* 2 MII interrupts. */
    CVMX_IRQ_MII0,   /* Added in CN56XX */
    CVMX_IRQ_BOOTDMA    = CVMX_IRQ_MII0 + 2,   /* Added in CN56XX */

    /* 32 WDOG interrupts. */
    CVMX_IRQ_WDOG0,
    CVMX_IRQ_NAND  = CVMX_IRQ_WDOG0 + 32,           /* Added in CN52XX */
    CVMX_IRQ_MIO,           /* Added in CN63XX */
    CVMX_IRQ_IOB,           /* Added in CN63XX */
    CVMX_IRQ_FPA,           /* Added in CN63XX */
    CVMX_IRQ_POW,           /* Added in CN63XX */
    CVMX_IRQ_L2C,           /* Added in CN63XX */
    CVMX_IRQ_IPD,           /* Added in CN63XX */
    CVMX_IRQ_PIP,           /* Added in CN63XX */
    CVMX_IRQ_PKO,           /* Added in CN63XX */
    CVMX_IRQ_ZIP,          /* Added in CN63XX */
    CVMX_IRQ_TIM,          /* Added in CN63XX */
    CVMX_IRQ_RAD,          /* Added in CN63XX */
    CVMX_IRQ_KEY,          /* Added in CN63XX */
    CVMX_IRQ_DFA,          /* Added in CN63XX */
    CVMX_IRQ_USBCTL,          /* Added in CN63XX */
    CVMX_IRQ_SLI,          /* Added in CN63XX */
    CVMX_IRQ_DPI,          /* Added in CN63XX */
    /* 5 AGX interrupts added in CN68XX. */
    CVMX_IRQ_AGX0,          /* Added in CN63XX */

    CVMX_IRQ_AGL = CVMX_IRQ_AGX0 + 5,          /* Added in CN63XX */
    CVMX_IRQ_PTP,          /* Added in CN63XX */
    CVMX_IRQ_PEM0,          /* Added in CN63XX */
    CVMX_IRQ_PEM1,          /* Added in CN63XX */
    CVMX_IRQ_SRIO0,          /* Added in CN63XX */
    CVMX_IRQ_SRIO1,          /* Added in CN63XX */
    CVMX_IRQ_LMC0,          /* Added in CN63XX */
    /* 4 LMC interrupts added in CN68XX. */
    CVMX_IRQ_DFM = CVMX_IRQ_LMC0 + 4,          /* Added in CN63XX */
    CVMX_IRQ_RST,          /* Added in CN63XX */
    CVMX_IRQ_ILK,         /* Added for CN68XX */
    CVMX_IRQ_SRIO2,       /* Added in CN66XX */
    CVMX_IRQ_DPI_DMA,     /* Added in CN61XX */
    /* 6 addition timers added in CN61XX */
    CVMX_IRQ_TIMER4,      /* Added in CN61XX */
    CVMX_IRQ_MAX = CVMX_IRQ_TIMER4 + 6          /* One greater than the last valid number.*/
} cvmx_irq_t;

/**
 * Function prototype for the exception handler
 */
typedef void (*cvmx_interrupt_exception_t)(uint64_t *registers);

/**
 * Function prototype for interrupt handlers
 */
typedef void (*cvmx_interrupt_func_t)(int irq_number, uint64_t *registers, void *user_arg);

/**
 * Register an interrupt handler for the specified interrupt number.
 *
 * @param irq_number Interrupt number to register for (0-135)
 * @param func       Function to call on interrupt.
 * @param user_arg   User data to pass to the interrupt handler
 */
void cvmx_interrupt_register(int irq_number, cvmx_interrupt_func_t func, void *user_arg);

/**
 * Set the exception handler for all non interrupt sources.
 *
 * @param handler New exception handler
 * @return Old exception handler
 */
cvmx_interrupt_exception_t cvmx_interrupt_set_exception(cvmx_interrupt_exception_t handler);


/**
 * Masks a given interrupt number.
 *
 * @param irq_number interrupt number to mask
 */
extern void (*cvmx_interrupt_mask_irq)(int irq_number);


/**
 * Unmasks a given interrupt number
 *
 * @param irq_number interrupt number to unmask
 */
extern void (*cvmx_interrupt_unmask_irq)(int irq_number);


/* Disable interrupts by clearing bit 0 of the COP0 status register,
** and return the previous contents of the status register.
** Note: this is only used to track interrupt status. */
static inline uint32_t cvmx_interrupt_disable_save(void)
{
    uint32_t flags;
    asm volatile (
        "DI   %[flags]\n"
        : [flags]"=r" (flags));
    return(flags);
}

/* Restore the contents of the cop0 status register.  Used with
** cvmx_interrupt_disable_save to allow recursive interrupt disabling */
static inline void cvmx_interrupt_restore(uint32_t flags)
{
    /* If flags value indicates interrupts should be enabled, then enable them */
    if (flags & 1)
    {
        asm volatile (
            "EI     \n"
            ::);
    }
}

#define cvmx_local_irq_save(x) ({x = cvmx_interrupt_disable_save();})
#define cvmx_local_irq_restore(x) cvmx_interrupt_restore(x)

/**
 * Utility function to do interrupt safe printf
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
        #define cvmx_safe_printf printk
#elif defined(CVMX_BUILD_FOR_LINUX_USER)
        #define cvmx_safe_printf printf
#else
        extern void cvmx_safe_printf(const char* format, ... ) __attribute__ ((format(printf, 1, 2)));
#endif

#define PRINT_ERROR(format, ...) cvmx_safe_printf("ERROR " format, ##__VA_ARGS__)

#ifdef  __cplusplus
}
#endif

#endif
