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
#ifndef __U_BOOT__
#if __GNUC__ >= 4
/* Backtrace is only available with the new toolchain.  */
#include <execinfo.h>
#endif
#endif  /* __U_BOOT__ */
#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-interrupt.h"
#include "cvmx-sysinfo.h"
#include "cvmx-uart.h"
#include "cvmx-pow.h"
#include "cvmx-ebt3000.h"
#include "cvmx-coremask.h"
#include "cvmx-spinlock.h"
#include "cvmx-atomic.h"
#include "cvmx-app-init.h"
#include "cvmx-error.h"
#include "cvmx-app-hotplug.h"
#include "cvmx-profiler.h"
#ifndef __U_BOOT__
# include <octeon_mem_map.h>
#else
# include <asm/arch/octeon_mem_map.h>
#endif
EXTERN_ASM void cvmx_interrupt_stage1(void);
EXTERN_ASM void cvmx_debug_handler_stage1(void);
EXTERN_ASM void cvmx_interrupt_cache_error(void);

int cvmx_interrupt_in_isr = 0;

struct __cvmx_interrupt_handler {
    cvmx_interrupt_func_t handler;      /**< One function to call per interrupt */
    void *data;                         /**< User data per interrupt */
    int handler_data;                   /**< Used internally */
};

/**
 * Internal status the interrupt registration
 */
typedef struct
{
    struct __cvmx_interrupt_handler handlers[CVMX_IRQ_MAX];
    cvmx_interrupt_exception_t exception_handler;
} cvmx_interrupt_state_t;

/**
 * Internal state the interrupt registration
 */
#ifndef __U_BOOT__
static CVMX_SHARED cvmx_interrupt_state_t cvmx_interrupt_state;
static CVMX_SHARED cvmx_spinlock_t cvmx_interrupt_default_lock;
/* Incremented once first core processing is finished. */
static CVMX_SHARED int32_t cvmx_interrupt_initialize_flag;
#endif  /* __U_BOOT__ */

#define ULL unsigned long long

#define HI32(data64)    ((uint32_t)(data64 >> 32))
#define LO32(data64)    ((uint32_t)(data64 & 0xFFFFFFFF))

static const char reg_names[][32] = { "r0","at","v0","v1","a0","a1","a2","a3",
                                      "t0","t1","t2","t3","t4","t5","t6","t7",
                                      "s0","s1","s2","s3","s4","s5", "s6","s7",
                                      "t8","t9", "k0","k1","gp","sp","s8","ra" };

/**
 * version of printf that works better in exception context.
 *
 * @param format
 */
void cvmx_safe_printf(const char *format, ...)
{
    char buffer[256];
    char *ptr = buffer;
    int count;
    va_list args;

    va_start(args, format);
#ifndef __U_BOOT__
    count = vsnprintf(buffer, sizeof(buffer), format, args);
#else
    count = vsprintf(buffer, format, args);
#endif
    va_end(args);

    while (count-- > 0)
    {
        cvmx_uart_lsr_t lsrval;

        /* Spin until there is room */
        do
        {
            lsrval.u64 = cvmx_read_csr(CVMX_MIO_UARTX_LSR(0));
#if !defined(CONFIG_OCTEON_SIM_SPEED)
            if (lsrval.s.temt == 0)
                cvmx_wait(10000);   /* Just to reduce the load on the system */
#endif
        }
        while (lsrval.s.temt == 0);

        if (*ptr == '\n')
            cvmx_write_csr(CVMX_MIO_UARTX_THR(0), '\r');
        cvmx_write_csr(CVMX_MIO_UARTX_THR(0), *ptr++);
    }
}

/* Textual descriptions of cause codes */
static const char cause_names[][128] = {
        /*  0 */ "Interrupt",
        /*  1 */ "TLB modification",
        /*  2 */ "tlb load/fetch",
        /*  3 */ "tlb store",
        /*  4 */ "address exc, load/fetch",
        /*  5 */ "address exc, store",
        /*  6 */ "bus error, instruction fetch",
        /*  7 */ "bus error, load/store",
        /*  8 */ "syscall",
        /*  9 */ "breakpoint",
        /* 10 */ "reserved instruction",
        /* 11 */ "cop unusable",
        /* 12 */ "arithmetic overflow",
        /* 13 */ "trap",
        /* 14 */ "",
        /* 15 */ "floating point exc",
        /* 16 */ "",
        /* 17 */ "",
        /* 18 */ "cop2 exception",
        /* 19 */ "",
        /* 20 */ "",
        /* 21 */ "",
        /* 22 */ "mdmx unusable",
        /* 23 */ "watch",
        /* 24 */ "machine check",
        /* 25 */ "",
        /* 26 */ "",
        /* 27 */ "",
        /* 28 */ "",
        /* 29 */ "",
        /* 30 */ "cache error",
        /* 31 */ ""
};

/**
 * @INTERNAL
 * print_reg64
 * @param name   Name of the value to print
 * @param reg    Value to print
 */
static inline void print_reg64(const char *name, uint64_t reg)
{
    cvmx_safe_printf("%16s: 0x%08x%08x\n", name, (unsigned int)HI32(reg),(unsigned int)LO32(reg));
}

/**
 * @INTERNAL
 * Dump all useful registers to the console
 *
 * @param registers CPU register to dump
 */
static void __cvmx_interrupt_dump_registers(uint64_t *registers)
{
    uint64_t r1, r2;
    int reg;
    for (reg=0; reg<16; reg++)
    {
        r1 = registers[reg]; r2 = registers[reg+16];
        cvmx_safe_printf("%3s ($%02d): 0x%08x%08x \t %3s ($%02d): 0x%08x%08x\n",
                           reg_names[reg], reg, (unsigned int)HI32(r1), (unsigned int)LO32(r1),
                           reg_names[reg+16], reg+16, (unsigned int)HI32(r2), (unsigned int)LO32(r2));
    }
    CVMX_MF_COP0 (r1, COP0_CAUSE);
    print_reg64 ("COP0_CAUSE", r1);
    CVMX_MF_COP0 (r2, COP0_STATUS);
    print_reg64 ("COP0_STATUS", r2);
    CVMX_MF_COP0 (r1, COP0_BADVADDR);
    print_reg64 ("COP0_BADVADDR", r1);
    CVMX_MF_COP0 (r2, COP0_EPC);
    print_reg64 ("COP0_EPC", r2);
}

/**
 * @INTERNAL
 * Default exception handler. Prints out the exception
 * cause decode and all relevant registers.
 *
 * @param registers Registers at time of the exception
 */
#ifndef __U_BOOT__
static
#endif  /* __U_BOOT__ */
void __cvmx_interrupt_default_exception_handler(uint64_t *registers)
{
    uint64_t trap_print_cause;
    const char *str;
#ifndef __U_BOOT__
    int modified_zero_pc = 0;

    ebt3000_str_write("Trap");
    cvmx_spinlock_lock(&cvmx_interrupt_default_lock);
#endif
    CVMX_MF_COP0 (trap_print_cause, COP0_CAUSE);
    str = cause_names [(trap_print_cause >> 2) & 0x1f];
    cvmx_safe_printf("Core %d: Unhandled Exception. Cause register decodes to:\n%s\n", (int)cvmx_get_core_num(), str && *str ? str : "Reserved exception cause");
    cvmx_safe_printf("******************************************************************\n");
    __cvmx_interrupt_dump_registers(registers);

#ifndef __U_BOOT__

    cvmx_safe_printf("******************************************************************\n");
#if __GNUC__ >= 4 && !defined(OCTEON_DISABLE_BACKTRACE)
    cvmx_safe_printf("Backtrace:\n\n");
    if (registers[35] == 0) {
	modified_zero_pc = 1;
	/* If PC is zero we probably did jalr $zero, in which case $31 - 8 is the call site. */
	registers[35] = registers[31] - 8;
    }
    __octeon_print_backtrace_func ((__octeon_backtrace_printf_t)cvmx_safe_printf);
    if (modified_zero_pc)
	registers[35] = 0;
    cvmx_safe_printf("******************************************************************\n");
#endif

    cvmx_spinlock_unlock(&cvmx_interrupt_default_lock);

    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
        CVMX_BREAK;

    while (1)
    {
        /* Interrupts are suppressed when we are in the exception
           handler (because of SR[EXL]).  Spin and poll the uart
           status and see if the debugger is trying to stop us. */
        cvmx_uart_lsr_t lsrval;
        lsrval.u64 = cvmx_read_csr(CVMX_MIO_UARTX_LSR(cvmx_debug_uart));
        if (lsrval.s.dr)
        {
            uint64_t tmp;
            /* Pulse the MCD0 signal. */
            asm volatile (
            ".set push\n"
            ".set noreorder\n"
            ".set mips64\n"
            "dmfc0 %0, $22\n"
            "ori   %0, %0, 0x10\n"
            "dmtc0 %0, $22\n"
            ".set pop\n"
            : "=r" (tmp));
        }
    }
#endif /* __U_BOOT__ */
}

#ifndef __U_BOOT__
/**
 * @INTERNAL
 * Default interrupt handler if the user doesn't register one.
 *
 * @param irq_number IRQ that caused this interrupt
 * @param registers  Register at the time of the interrupt
 * @param user_arg   Unused optional user data
 */
static void __cvmx_interrupt_default(int irq_number, uint64_t *registers, void *user_arg)
{
    cvmx_safe_printf("cvmx_interrupt_default: Received interrupt %d\n", irq_number);
    __cvmx_interrupt_dump_registers(registers);
}

/**
 * Map a ciu bit to an irq number.  0xff for invalid.
 * 0-63 for en0.
 * 64-127 for en1.
 */

static CVMX_SHARED uint8_t cvmx_ciu_to_irq[8][64];
#define cvmx_ciu_en0_to_irq cvmx_ciu_to_irq[0]
#define cvmx_ciu_en1_to_irq cvmx_ciu_to_irq[1]
#define cvmx_ciu2_wrkq_to_irq cvmx_ciu_to_irq[0]
#define cvmx_ciu2_wdog_to_irq cvmx_ciu_to_irq[1]
#define cvmx_ciu2_rml_to_irq cvmx_ciu_to_irq[2]
#define cvmx_ciu2_mio_to_irq cvmx_ciu_to_irq[3]
#define cvmx_ciu2_io_to_irq cvmx_ciu_to_irq[4]
#define cvmx_ciu2_mem_to_irq cvmx_ciu_to_irq[5]
#define cvmx_ciu2_eth_to_irq cvmx_ciu_to_irq[6]
#define cvmx_ciu2_gpio_to_irq cvmx_ciu_to_irq[7]

static CVMX_SHARED uint8_t cvmx_ciu2_mbox_to_irq[64];
static CVMX_SHARED uint8_t cvmx_ciu_61xx_timer_to_irq[64];

static void __cvmx_interrupt_set_mapping(int irq, unsigned int en, unsigned int bit)
{
    cvmx_interrupt_state.handlers[irq].handler_data = (en << 6) | bit;
    if (en <= 7)
        cvmx_ciu_to_irq[en][bit] = irq;
    else if (en == 8)
        cvmx_ciu_61xx_timer_to_irq[bit] = irq;
    else 
        cvmx_ciu2_mbox_to_irq[bit] = irq;
}

static uint64_t cvmx_interrupt_ciu_en0_mirror;
static uint64_t cvmx_interrupt_ciu_en1_mirror;
static uint64_t cvmx_interrupt_ciu_61xx_timer_mirror;

/**
 * @INTERNAL
 * Called for all Performance Counter interrupts. Handler for 
 * interrupt line 6
 *
 * @param irq_number Interrupt number that we're being called for
 * @param registers  Registers at the time of the interrupt
 * @param user_arg   Unused user argument*
 */ 
static void __cvmx_interrupt_perf(int irq_number, uint64_t *registers, void *user_arg)
{
    uint64_t perf_counter;
    CVMX_MF_COP0(perf_counter, COP0_PERFVALUE0);
    if (perf_counter & (1ull << 63))
        cvmx_collect_sample();
}

/**
 * @INTERNAL
 * Handler for interrupt lines 2 and 3. These are directly tied
 * to the CIU. The handler queries the status of the CIU and
 * calls the secondary handler for the CIU interrupt that
 * occurred.
 *
 * @param irq_number Interrupt number that fired (2 or 3)
 * @param registers  Registers at the time of the interrupt
 * @param user_arg   Unused user argument
 */
static void __cvmx_interrupt_ciu(int irq_number, uint64_t *registers, void *user_arg)
{
    int ciu_offset;
    uint64_t irq_mask;
    uint64_t irq;
    int bit;
    int core = cvmx_get_core_num();

    if (irq_number == CVMX_IRQ_MIPS2) {
        /* Handle EN0 sources */
        ciu_offset = core * 2;
        irq_mask = cvmx_read_csr(CVMX_CIU_INTX_SUM0(ciu_offset)) & cvmx_interrupt_ciu_en0_mirror;
        CVMX_DCLZ(bit, irq_mask);
        bit = 63 - bit;
        /* If ciu_int_sum1<sum2> is set, means its a timer interrupt */
        if (bit == 51 && (OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN66XX_PASS1_2))) {
            uint64_t irq_mask;
            int bit;
            irq_mask = cvmx_read_csr(CVMX_CIU_SUM2_PPX_IP2(core)) & cvmx_interrupt_ciu_61xx_timer_mirror;
            CVMX_DCLZ(bit, irq_mask);
            bit = 63 - bit;
            /* Handle TIMER(4..9) interrupts */
            if (bit <= 9 && bit >= 4) {
                uint64_t irq = cvmx_ciu_61xx_timer_to_irq[bit];
                if (cvmx_unlikely(irq == 0xff)) {
                    /* No mapping */
                    cvmx_interrupt_ciu_61xx_timer_mirror &= ~(1ull << bit);
                    cvmx_write_csr(CVMX_CIU_EN2_PPX_IP2(core), cvmx_interrupt_ciu_61xx_timer_mirror);
                    return;
                }
                struct __cvmx_interrupt_handler *h = cvmx_interrupt_state.handlers + irq;
                h->handler(irq, registers, h->data);
                return;
            }
        }

        if (bit >= 0) {
            irq = cvmx_ciu_en0_to_irq[bit];
            if (cvmx_unlikely(irq == 0xff)) {
                /* No mapping. */
                cvmx_interrupt_ciu_en0_mirror &= ~(1ull << bit);
                cvmx_write_csr(CVMX_CIU_INTX_EN0(ciu_offset), cvmx_interrupt_ciu_en0_mirror);
                return;
            }
            struct __cvmx_interrupt_handler *h = cvmx_interrupt_state.handlers + irq;
            h->handler(irq, registers, h->data);
            return;
        }
    } else {
        /* Handle EN1 sources */
        ciu_offset = cvmx_get_core_num() * 2 + 1;
        irq_mask = cvmx_read_csr(CVMX_CIU_INT_SUM1) & cvmx_interrupt_ciu_en1_mirror;
        CVMX_DCLZ(bit, irq_mask);
        bit = 63 - bit;
        if (bit >= 0) {
            irq = cvmx_ciu_en1_to_irq[bit];
            if (cvmx_unlikely(irq == 0xff)) {
                /* No mapping. */
                cvmx_interrupt_ciu_en1_mirror &= ~(1ull << bit);
                cvmx_write_csr(CVMX_CIU_INTX_EN1(ciu_offset), cvmx_interrupt_ciu_en1_mirror);
                return;
            }
            struct __cvmx_interrupt_handler *h = cvmx_interrupt_state.handlers + irq;
            h->handler(irq, registers, h->data);
            return;
        }
    }
}

/**
 * @INTERNAL
 * Handler for interrupt line 3, the DPI_DMA will have different value
 * per core, all other fields values are identical for different cores.
 *  These are directly tied to the CIU. The handler queries the status of
 * the CIU and calls the secondary handler for the CIU interrupt that
 * occurred.
 *
 * @param irq_number Interrupt number that fired (2 or 3)
 * @param registers  Registers at the time of the interrupt
 * @param user_arg   Unused user argument
 */
static void __cvmx_interrupt_ciu_cn61xx(int irq_number, uint64_t *registers, void *user_arg)
{
    /* Handle EN1 sources */
    int core = cvmx_get_core_num();
    int ciu_offset;
    uint64_t irq_mask;
    uint64_t irq;
    int bit;

    ciu_offset = core * 2 + 1;
    irq_mask = cvmx_read_csr(CVMX_CIU_SUM1_PPX_IP3(core)) & cvmx_interrupt_ciu_en1_mirror;
    CVMX_DCLZ(bit, irq_mask);
    bit = 63 - bit;
    if (bit >= 0) {
        irq = cvmx_ciu_en1_to_irq[bit];
        if (cvmx_unlikely(irq == 0xff)) {
            /* No mapping. */
            cvmx_interrupt_ciu_en1_mirror &= ~(1ull << bit);
            cvmx_write_csr(CVMX_CIU_INTX_EN1(ciu_offset), cvmx_interrupt_ciu_en1_mirror);
            return;
        }
        struct __cvmx_interrupt_handler *h = cvmx_interrupt_state.handlers + irq;
        h->handler(irq, registers, h->data);
        return;
    }
}

/**
 * @INTERNAL
 * Handler for interrupt line 2 on 68XX. These are directly tied
 * to the CIU2. The handler queries the status of the CIU and
 * calls the secondary handler for the CIU interrupt that
 * occurred.
 *
 * @param irq_number Interrupt number that fired (2 or 3)
 * @param registers  Registers at the time of the interrupt
 * @param user_arg   Unused user argument
 */
static void __cvmx_interrupt_ciu2(int irq_number, uint64_t *registers, void *user_arg)
{
    int sum_bit, src_bit;
    uint64_t irq;
    uint64_t src_reg, src_val;
    struct __cvmx_interrupt_handler *h;
    int core = cvmx_get_core_num();
    uint64_t sum = cvmx_read_csr(CVMX_CIU2_SUM_PPX_IP2(core));

    CVMX_DCLZ(sum_bit, sum);
    sum_bit = 63 - sum_bit;

    if (sum_bit >= 0) {
        switch (sum_bit) {
        case 63:
        case 62:
        case 61:
        case 60:
            irq = cvmx_ciu2_mbox_to_irq[sum_bit - 60];
            if (cvmx_unlikely(irq == 0xff)) {
                /* No mapping. */
                uint64_t mask_reg = CVMX_CIU2_EN_PPX_IP2_MBOX_W1C(core);
                cvmx_write_csr(mask_reg, 1ull << (sum_bit - 60));
                break;
            }
            h = cvmx_interrupt_state.handlers + irq;
            h->handler(irq, registers, h->data);
            break;

        case 7:
        case 6:
        case 5:
        case 4:
        case 3:
        case 2:
        case 1:
        case 0:
            src_reg = CVMX_CIU2_SRC_PPX_IP2_WRKQ(core) + (0x1000 * sum_bit);
            src_val = cvmx_read_csr(src_reg);
            if (!src_val)
                break;
            CVMX_DCLZ(src_bit, src_val);
            src_bit = 63 - src_bit;
            irq = cvmx_ciu_to_irq[sum_bit][src_bit];
            if (cvmx_unlikely(irq == 0xff)) {
                /* No mapping. */
                uint64_t mask_reg = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(core) + (0x1000 * sum_bit);
                cvmx_write_csr(mask_reg, 1ull << src_bit);
                break;
            }
            h = cvmx_interrupt_state.handlers + irq;
            h->handler(irq, registers, h->data);
            break;

        default:
            cvmx_safe_printf("Unknown CIU2 bit: %d\n", sum_bit);
            break;
        }
    }
    /* Clear the source to reduce the chance for spurious interrupts.  */

    /* CN68XX has an CIU-15786 errata that accessing the ACK registers
     * can stop interrupts from propagating
     */

    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
        cvmx_read_csr(CVMX_CIU2_INTR_CIU_READY);
    else
        cvmx_read_csr(CVMX_CIU2_ACK_PPX_IP2(core));
}


/**
 * @INTERNAL
 * Called for all RML interrupts. This is usually an ECC error
 *
 * @param irq_number Interrupt number that we're being called for
 * @param registers  Registers at the time of the interrupt
 * @param user_arg   Unused user argument
 */
static void __cvmx_interrupt_ecc(int irq_number, uint64_t *registers, void *user_arg)
{
    cvmx_error_poll();
}


/**
 * Process an interrupt request
 *
 * @param registers Registers at time of interrupt / exception
 * Registers 0-31 are standard MIPS, others specific to this routine
 * @return
 */
void cvmx_interrupt_do_irq(uint64_t *registers);
void cvmx_interrupt_do_irq(uint64_t *registers)
{
    uint64_t        mask;
    uint64_t        cause;
    uint64_t        status;
    uint64_t        cache_err;
    int             i;
    uint32_t exc_vec;
    /* Determine the cause of the interrupt */
    asm volatile ("dmfc0 %0,$13,0" : "=r" (cause));
    asm volatile ("dmfc0 %0,$12,0" : "=r" (status));
    /* In case of exception, clear all interrupts to avoid recursive interrupts.
       Also clear EXL bit to display the correct PC value. */
    if ((cause & 0x7c) == 0)
    {
        asm volatile ("dmtc0 %0, $12, 0" : : "r" (status & ~(0xff02)));
    }
    /* The assembly stub at each exception vector saves its address in k1 when
    ** it calls the stage 2 handler.  We use this to compute the exception vector
    ** that brought us here */
    exc_vec = (uint32_t)(registers[27] & 0x780);  /* Mask off bits we need to ignore */

    /* Check for cache errors.  The cache errors go to a separate exception vector,
    ** so we will only check these if we got here from a cache error exception, and
    ** the ERL (error level) bit is set. */
    i = cvmx_get_core_num();
    if (exc_vec == 0x100 && (status & 0x4))
    {
        CVMX_MF_CACHE_ERR(cache_err);

        /* Use copy of DCACHE_ERR register that early exception stub read */
        if (OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX))
        {
            if (registers[34] & 0x1)
                cvmx_safe_printf("Dcache error detected: core: %d, way: %d, va 7:3: 0x%x\n", i, (int)(registers[34] >> 8) & 0x3f, (int)(registers[34] >> 3) & 0x1f);
            else if (cache_err & 0x1)
                cvmx_safe_printf("Icache error detected: core: %d, set: %d, way : %d, va 6:3 = 0x%x\n", i, (int)(cache_err >> 5) & 0x3f, (int)(cache_err >> 3) & 0x3, (int)(cache_err >> 11) & 0xf);
            else
                cvmx_safe_printf("Cache error exception: core %d\n", i);
        }
        else
        {
            if (registers[34] & 0x1)
                cvmx_safe_printf("Dcache error detected: core: %d, way: %d, va 9:7: 0x%x\n", i, (int)(registers[34] >> 10) & 0x1f, (int)(registers[34] >> 7) & 0x3);
            else if (cache_err & 0x1)
                cvmx_safe_printf("Icache error detected: core: %d, way : %d, va 9:3 = 0x%x\n", i, (int)(cache_err >> 10) & 0x3f, (int)(cache_err >> 3) & 0x7f);
            else
                cvmx_safe_printf("Cache error exception: core %d\n", i);
        }
        CVMX_MT_DCACHE_ERR(1);
        CVMX_MT_CACHE_ERR(0);
    }

    /* The bus error exceptions can occur due to DID timeout or write buffer,
       check by reading COP0_CACHEERRD */
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
    {
        i = cvmx_get_core_num();
        if (registers[34] & 0x4)
        {
            cvmx_safe_printf("Bus error detected due to DID timeout: core: %d\n", i);
            CVMX_MT_DCACHE_ERR(4);
        }
        else if (registers[34] & 0x2)
        {
            cvmx_safe_printf("Bus error detected due to write buffer parity: core: %d\n", i);
            CVMX_MT_DCACHE_ERR(2);
        }
    }

    if ((cause & 0x7c) != 0)
    {
        cvmx_interrupt_state.exception_handler(registers);
        goto return_from_interrupt;
    }

    /* Convert the cause into an active mask */
    mask = ((cause & status) >> 8) & 0xff;
    if (mask == 0)
    {
        goto return_from_interrupt; /* Spurious interrupt */
    }

    for (i=0; i<8; i++)
    {
        if (mask & (1<<i))
        {
            struct __cvmx_interrupt_handler *h = cvmx_interrupt_state.handlers + i;
            h->handler(i, registers, h->data);
            goto return_from_interrupt;
        }
    }

    /* We should never get here */
    __cvmx_interrupt_default_exception_handler(registers);

return_from_interrupt:
    /* Restore Status register before returning from exception. */
    asm volatile ("dmtc0 %0, $12, 0" : : "r" (status));
}

void (*cvmx_interrupt_mask_irq)(int irq_number);
void (*cvmx_interrupt_unmask_irq)(int irq_number);

#define CLEAR_OR_MASK(V,M,O) ({\
            if (O)             \
                (V) &= ~(M);   \
            else               \
                (V) |= (M);    \
        })

static void __cvmx_interrupt_ciu2_mask_unmask_irq(int irq_number, int op)
{

    if (irq_number < 0 || irq_number >= CVMX_IRQ_MAX)
        return;

    if (irq_number <=  CVMX_IRQ_MIPS7) {
        uint32_t flags, mask;

        flags = cvmx_interrupt_disable_save();
        asm volatile ("mfc0 %0,$12,0" : "=r" (mask));
        CLEAR_OR_MASK(mask, 1 << (8 + irq_number), op);
        asm volatile ("mtc0 %0,$12,0" : : "r" (mask));
        cvmx_interrupt_restore(flags);
    } else {
        int idx;
        uint64_t reg;
        int core = cvmx_get_core_num();

        int bit = cvmx_interrupt_state.handlers[irq_number].handler_data;

        if (bit < 0)
            return;

        idx = bit >> 6;
        bit &= 0x3f;
        if (idx > 7) {
            /* MBOX */
            if (op)
                reg = CVMX_CIU2_EN_PPX_IP2_MBOX_W1C(core);
            else
                reg = CVMX_CIU2_EN_PPX_IP2_MBOX_W1S(core);
        } else {
            if (op)
                reg = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(core) + (0x1000 * idx);
            else
                reg = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(core) + (0x1000 * idx);
        }
        cvmx_write_csr(reg, 1ull << bit);
    }
}

static void __cvmx_interrupt_ciu2_mask_irq(int irq_number)
{
    __cvmx_interrupt_ciu2_mask_unmask_irq(irq_number, 1);
}

static void __cvmx_interrupt_ciu2_unmask_irq(int irq_number)
{
    __cvmx_interrupt_ciu2_mask_unmask_irq(irq_number, 0);
}

static void __cvmx_interrupt_ciu_mask_unmask_irq(int irq_number, int op)
{
    uint32_t flags;

    if (irq_number < 0 || irq_number >= CVMX_IRQ_MAX)
        return;

    flags = cvmx_interrupt_disable_save();
    if (irq_number <=  CVMX_IRQ_MIPS7) {
        uint32_t mask;
        asm volatile ("mfc0 %0,$12,0" : "=r" (mask));
        CLEAR_OR_MASK(mask, 1 << (8 + irq_number), op);
        asm volatile ("mtc0 %0,$12,0" : : "r" (mask));
    } else {
        int ciu_bit, ciu_offset;
        int bit = cvmx_interrupt_state.handlers[irq_number].handler_data;
        int is_timer_intr = bit >> 6;
        int core = cvmx_get_core_num();

        if (bit < 0)
            goto out;

        ciu_bit = bit & 0x3f;
        ciu_offset = core * 2;

        if (is_timer_intr == 8)
        {
            CLEAR_OR_MASK(cvmx_interrupt_ciu_61xx_timer_mirror, 1ull << ciu_bit, op);
            CLEAR_OR_MASK(cvmx_interrupt_ciu_en0_mirror, 1ull << 51, op); // SUM2 bit
            cvmx_write_csr(CVMX_CIU_EN2_PPX_IP2(core), cvmx_interrupt_ciu_61xx_timer_mirror);
        }
        else if (bit & 0x40) {
            /* EN1 */
            ciu_offset += 1;
            CLEAR_OR_MASK(cvmx_interrupt_ciu_en1_mirror, 1ull << ciu_bit, op);
            cvmx_write_csr(CVMX_CIU_INTX_EN1(ciu_offset), cvmx_interrupt_ciu_en1_mirror);
        } else {
            /* EN0 */
            CLEAR_OR_MASK(cvmx_interrupt_ciu_en0_mirror, 1ull << ciu_bit, op);
            cvmx_write_csr(CVMX_CIU_INTX_EN0(ciu_offset), cvmx_interrupt_ciu_en0_mirror);
        }
    }
out:
    cvmx_interrupt_restore(flags);
}

static void __cvmx_interrupt_ciu_mask_irq(int irq_number)
{
    __cvmx_interrupt_ciu_mask_unmask_irq(irq_number, 1);
}

static void __cvmx_interrupt_ciu_unmask_irq(int irq_number)
{
    __cvmx_interrupt_ciu_mask_unmask_irq(irq_number, 0);
}

/**
 * Register an interrupt handler for the specified interrupt number.
 *
 * @param irq_number Interrupt number to register for See
 *                   cvmx-interrupt.h for enumeration and description of sources.
 * @param func       Function to call on interrupt.
 * @param user_arg   User data to pass to the interrupt handler
 */
void cvmx_interrupt_register(int irq_number, cvmx_interrupt_func_t func, void *user_arg)
{
    if (irq_number >= CVMX_IRQ_MAX || irq_number < 0) {
        cvmx_warn("cvmx_interrupt_register: Illegal irq_number %d\n", irq_number);
        return;
    }
    cvmx_interrupt_state.handlers[irq_number].handler = func;
    cvmx_interrupt_state.handlers[irq_number].data = user_arg;
    CVMX_SYNCWS;
}


static void cvmx_interrupt_ciu_initialize(cvmx_sysinfo_t *sys_info_ptr)
{
    int i;
    int core = cvmx_get_core_num();

    /* Disable all CIU interrupts by default */
    cvmx_interrupt_ciu_en0_mirror = 0;
    cvmx_interrupt_ciu_en1_mirror = 0;
    cvmx_interrupt_ciu_61xx_timer_mirror = 0;
    cvmx_write_csr(CVMX_CIU_INTX_EN0(core * 2), cvmx_interrupt_ciu_en0_mirror);
    cvmx_write_csr(CVMX_CIU_INTX_EN0((core * 2)+1), cvmx_interrupt_ciu_en0_mirror);
    cvmx_write_csr(CVMX_CIU_INTX_EN1(core * 2), cvmx_interrupt_ciu_en1_mirror);
    cvmx_write_csr(CVMX_CIU_INTX_EN1((core * 2)+1), cvmx_interrupt_ciu_en1_mirror);
    if (OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN66XX_PASS1_2))
        cvmx_write_csr(CVMX_CIU_EN2_PPX_IP2(cvmx_get_core_num()), cvmx_interrupt_ciu_61xx_timer_mirror);

    if (!cvmx_coremask_first_core(sys_info_ptr->core_mask)|| is_core_being_hot_plugged())
        return;

    /* On the first core, set up the maps */
    for (i = 0; i < 64; i++) {
        cvmx_ciu_en0_to_irq[i] = 0xff;
        cvmx_ciu_en1_to_irq[i] = 0xff;
        cvmx_ciu_61xx_timer_to_irq[i] = 0xff;
    }

    /* WORKQ */
    for (i = 0; i < 16; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_WORKQ0 + i, 0, i);
    /* GPIO */
    for (i = 0; i < 16; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_GPIO0 + i, 0, i + 16);

    /* MBOX */
    for (i = 0; i < 2; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_MBOX0 + i, 0, i + 32);

    /* UART */
    __cvmx_interrupt_set_mapping(CVMX_IRQ_UART0 + 0, 0, 34);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_UART0 + 1, 0, 35);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_UART0 + 2, 1, 16);

    /* PCI */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_PCI_INT0 + i, 0, i + 36);

    /* MSI */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_PCI_MSI0 + i, 0, i + 40);

    /* TWSI */
    __cvmx_interrupt_set_mapping(CVMX_IRQ_TWSI0 + 0, 0, 45);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_TWSI0 + 1, 0, 59);

    /* other */
    __cvmx_interrupt_set_mapping(CVMX_IRQ_RML, 0, 46);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_TRACE0, 0, 47);

    /* GMX_DRP */
    for (i = 0; i < 2; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_GMX_DRP0 + i, 0, i + 48);

    __cvmx_interrupt_set_mapping(CVMX_IRQ_IPD_DRP, 0, 50);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_KEY_ZERO, 0, 51);

    /* TIMER0 */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_TIMER0 + i, 0, i + 52);

    /* TIMER4..9 */
    for(i = 0; i < 6; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_TIMER4 + i, 8, i + 4);

    __cvmx_interrupt_set_mapping(CVMX_IRQ_USB0 + 0, 0, 56);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_USB0 + 1, 1, 17);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PCM, 0, 57);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_MPI, 0, 58);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_POWIQ, 0, 60);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_IPDPPTHR, 0, 61);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_MII0 + 0, 0, 62);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_MII0 + 1, 1, 18);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_BOOTDMA, 0, 63);

    /* WDOG */
    for (i = 0; i < 16; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_WDOG0 + i, 1, i);

    __cvmx_interrupt_set_mapping(CVMX_IRQ_NAND, 1, 19);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_MIO, 1, 20);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_IOB, 1, 21);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_FPA, 1, 22);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_POW, 1, 23);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_L2C, 1, 24);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_IPD, 1, 25);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PIP, 1, 26);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PKO, 1, 27);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_ZIP, 1, 28);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_TIM, 1, 29);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_RAD, 1, 30);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_KEY, 1, 31);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_DFA, 1, 32);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_USBCTL, 1, 33);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_SLI, 1, 34);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_DPI, 1, 35);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_AGX0, 1, 36);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_AGX0 + 1, 1, 37);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_DPI_DMA, 1, 40);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_AGL, 1, 46);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PTP, 1, 47);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PEM0, 1, 48);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PEM1, 1, 49);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_SRIO0, 1, 50);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_SRIO1, 1, 51);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_LMC0, 1, 52);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_DFM, 1, 56);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_SRIO2, 1, 60);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_RST, 1, 63);
}

static void cvmx_interrupt_ciu2_initialize(cvmx_sysinfo_t *sys_info_ptr)
{
    int i;

    /* Disable all CIU2 interrupts by default */

    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_WRKQ(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_WRKQ(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_WRKQ(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_WDOG(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_WDOG(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_WDOG(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_RML(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_RML(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_RML(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_MIO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_MIO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_MIO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_IO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_IO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_IO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_MEM(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_MEM(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_MEM(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_PKT(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_PKT(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_PKT(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_GPIO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_GPIO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_GPIO(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP2_MBOX(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP3_MBOX(cvmx_get_core_num()), 0);
    cvmx_write_csr(CVMX_CIU2_EN_PPX_IP4_MBOX(cvmx_get_core_num()), 0);

    if (!cvmx_coremask_first_core(sys_info_ptr->core_mask) || is_core_being_hot_plugged())
        return;

    /* On the first core, set up the maps */
    for (i = 0; i < 64; i++) {
        cvmx_ciu2_wrkq_to_irq[i] = 0xff;
        cvmx_ciu2_wdog_to_irq[i] = 0xff;
        cvmx_ciu2_rml_to_irq[i] = 0xff;
        cvmx_ciu2_mio_to_irq[i] = 0xff;
        cvmx_ciu2_io_to_irq[i] = 0xff;
        cvmx_ciu2_mem_to_irq[i] = 0xff;
        cvmx_ciu2_eth_to_irq[i] = 0xff;
        cvmx_ciu2_gpio_to_irq[i] = 0xff;
        cvmx_ciu2_mbox_to_irq[i] = 0xff;
    }

    /* WORKQ */
    for (i = 0; i < 64; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_WORKQ0 + i, 0, i);

    /* GPIO */
    for (i = 0; i < 16; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_GPIO0 + i, 7, i);

    /* MBOX */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_MBOX0 + i, 60, i);

    /* UART */
    for (i = 0; i < 2; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_UART0 + i, 3, 36 + i);

    /* PCI */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_PCI_INT0 + i, 4, 16 + i);

    /* MSI */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_PCI_MSI0 + i, 4, 8 + i);

    /* TWSI */
    for (i = 0; i < 2; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_TWSI0 + i, 3, 32 + i);

    /* TRACE */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_TRACE0 + i, 2, 52 + i);

    /* GMX_DRP */
    for (i = 0; i < 5; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_GMX_DRP0 + i, 6, 8 + i);

    __cvmx_interrupt_set_mapping(CVMX_IRQ_IPD_DRP, 3, 2);

    /* TIMER0 */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_TIMER0 + i, 3, 8 + i);

    __cvmx_interrupt_set_mapping(CVMX_IRQ_USB0, 3, 44);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_IPDPPTHR, 3, 0);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_MII0, 6, 40);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_BOOTDMA, 3, 18);

    /* WDOG */
    for (i = 0; i < 32; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_WDOG0 + i, 1, i);

    __cvmx_interrupt_set_mapping(CVMX_IRQ_NAND, 3, 16);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_MIO, 3, 17);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_IOB, 2, 0);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_FPA, 2, 4);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_POW, 2, 16);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_L2C, 2, 48);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_IPD, 2, 5);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PIP, 2, 6);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PKO, 2, 7);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_ZIP, 2, 24);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_TIM, 2, 28);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_RAD, 2, 29);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_KEY, 2, 30);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_DFA, 2, 40);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_USBCTL, 3, 40);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_SLI, 2, 32);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_DPI, 2, 33);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_DPI_DMA, 2, 36);

    /* AGX */
    for (i = 0; i < 5; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_AGX0 + i, 6, i);

    __cvmx_interrupt_set_mapping(CVMX_IRQ_AGL, 6, 32);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PTP, 3, 48);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PEM0, 4, 32);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_PEM1, 4, 32);

    /* LMC */
    for (i = 0; i < 4; i++)
        __cvmx_interrupt_set_mapping(CVMX_IRQ_LMC0 + i, 5, i);

    __cvmx_interrupt_set_mapping(CVMX_IRQ_RST, 3, 63);
    __cvmx_interrupt_set_mapping(CVMX_IRQ_ILK, 6, 48);
}

/**
 * Initialize the interrupt routine and copy the low level
 * stub into the correct interrupt vector. This is called
 * automatically during application startup.
 */
void cvmx_interrupt_initialize(void)
{
    void *low_level_loc;
    cvmx_sysinfo_t *sys_info_ptr = cvmx_sysinfo_get();
    int i;

    if (cvmx_coremask_first_core(sys_info_ptr->core_mask) && !is_core_being_hot_plugged()) {
#ifndef CVMX_ENABLE_CSR_ADDRESS_CHECKING
        /* We assume this relationship between the registers. */
        CVMX_BUILD_ASSERT(CVMX_CIU2_SRC_PPX_IP2_WRKQ(0) + 0x1000 == CVMX_CIU2_SRC_PPX_IP2_WDOG(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_SRC_PPX_IP2_WRKQ(0) + 0x2000 == CVMX_CIU2_SRC_PPX_IP2_RML(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_SRC_PPX_IP2_WRKQ(0) + 0x3000 == CVMX_CIU2_SRC_PPX_IP2_MIO(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_SRC_PPX_IP2_WRKQ(0) + 0x4000 == CVMX_CIU2_SRC_PPX_IP2_IO(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_SRC_PPX_IP2_WRKQ(0) + 0x5000 == CVMX_CIU2_SRC_PPX_IP2_MEM(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_SRC_PPX_IP2_WRKQ(0) + 0x6000 == CVMX_CIU2_SRC_PPX_IP2_PKT(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_SRC_PPX_IP2_WRKQ(0) + 0x7000 == CVMX_CIU2_SRC_PPX_IP2_GPIO(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(0) + 0x1000 == CVMX_CIU2_EN_PPX_IP2_WDOG_W1C(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(0) + 0x2000 == CVMX_CIU2_EN_PPX_IP2_RML_W1C(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(0) + 0x3000 == CVMX_CIU2_EN_PPX_IP2_MIO_W1C(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(0) + 0x4000 == CVMX_CIU2_EN_PPX_IP2_IO_W1C(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(0) + 0x5000 == CVMX_CIU2_EN_PPX_IP2_MEM_W1C(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(0) + 0x6000 == CVMX_CIU2_EN_PPX_IP2_PKT_W1C(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(0) + 0x7000 == CVMX_CIU2_EN_PPX_IP2_GPIO_W1C(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(0) + 0x1000 == CVMX_CIU2_EN_PPX_IP2_WDOG_W1S(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(0) + 0x2000 == CVMX_CIU2_EN_PPX_IP2_RML_W1S(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(0) + 0x3000 == CVMX_CIU2_EN_PPX_IP2_MIO_W1S(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(0) + 0x4000 == CVMX_CIU2_EN_PPX_IP2_IO_W1S(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(0) + 0x5000 == CVMX_CIU2_EN_PPX_IP2_MEM_W1S(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(0) + 0x6000 == CVMX_CIU2_EN_PPX_IP2_PKT_W1S(0));
        CVMX_BUILD_ASSERT(CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(0) + 0x7000 == CVMX_CIU2_EN_PPX_IP2_GPIO_W1S(0));
#endif /* !CVMX_ENABLE_CSR_ADDRESS_CHECKING */

        for (i = 0; i < CVMX_IRQ_MAX; i++) {
            cvmx_interrupt_state.handlers[i].handler = __cvmx_interrupt_default;
            cvmx_interrupt_state.handlers[i].data = NULL;
            cvmx_interrupt_state.handlers[i].handler_data = -1;
        }
    }

    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
    {
        cvmx_interrupt_mask_irq = __cvmx_interrupt_ciu2_mask_irq;
        cvmx_interrupt_unmask_irq = __cvmx_interrupt_ciu2_unmask_irq;
        cvmx_interrupt_ciu2_initialize(sys_info_ptr);
        /* Add an interrupt handlers for chained CIU interrupt */
        cvmx_interrupt_register(CVMX_IRQ_MIPS2, __cvmx_interrupt_ciu2, NULL);
    }
    else if (OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN66XX_PASS1_2))
    {
        cvmx_interrupt_mask_irq = __cvmx_interrupt_ciu_mask_irq;
        cvmx_interrupt_unmask_irq = __cvmx_interrupt_ciu_unmask_irq;
        cvmx_interrupt_ciu_initialize(sys_info_ptr);

        /* Add an interrupt handlers for chained CIU interrupts */
        cvmx_interrupt_register(CVMX_IRQ_MIPS2, __cvmx_interrupt_ciu, NULL);
        cvmx_interrupt_register(CVMX_IRQ_MIPS3, __cvmx_interrupt_ciu_cn61xx, NULL);
    }
    else
    {
        cvmx_interrupt_mask_irq = __cvmx_interrupt_ciu_mask_irq;
        cvmx_interrupt_unmask_irq = __cvmx_interrupt_ciu_unmask_irq;
        cvmx_interrupt_ciu_initialize(sys_info_ptr);

        /* Add an interrupt handlers for chained CIU interrupts */
        cvmx_interrupt_register(CVMX_IRQ_MIPS2, __cvmx_interrupt_ciu, NULL);
        cvmx_interrupt_register(CVMX_IRQ_MIPS3, __cvmx_interrupt_ciu, NULL);
    }
   
    /* Move performance counter interrupts to IRQ 6*/
    cvmx_update_perfcnt_irq();

    /* Add an interrupt handler for Perf counter interrupts */
    cvmx_interrupt_register(CVMX_IRQ_MIPS6, __cvmx_interrupt_perf, NULL);
    
    if (cvmx_coremask_first_core(sys_info_ptr->core_mask) && !is_core_being_hot_plugged())
    {
        cvmx_interrupt_state.exception_handler = __cvmx_interrupt_default_exception_handler;

        low_level_loc = CASTPTR(void, CVMX_ADD_SEG32(CVMX_MIPS32_SPACE_KSEG0,sys_info_ptr->exception_base_addr));
        memcpy(low_level_loc + 0x80, (void*)cvmx_interrupt_stage1, 0x80);
        memcpy(low_level_loc + 0x100, (void*)cvmx_interrupt_cache_error, 0x80);
        memcpy(low_level_loc + 0x180, (void*)cvmx_interrupt_stage1, 0x80);
        memcpy(low_level_loc + 0x200, (void*)cvmx_interrupt_stage1, 0x80);

        /* Make sure the locations used to count Icache and Dcache exceptions
            starts out as zero */
        cvmx_write64_uint64(CVMX_ADD_SEG32(CVMX_MIPS32_SPACE_KSEG0, 8), 0);
        cvmx_write64_uint64(CVMX_ADD_SEG32(CVMX_MIPS32_SPACE_KSEG0, 16), 0);
        cvmx_write64_uint64(CVMX_ADD_SEG32(CVMX_MIPS32_SPACE_KSEG0, 24), 0);
        CVMX_SYNC;

        /* Add an interrupt handler for ECC failures */
        if (cvmx_error_initialize(0 /* || CVMX_ERROR_FLAGS_ECC_SINGLE_BIT */))
            cvmx_warn("cvmx_error_initialize() failed\n");

        /* Enable PIP/IPD, POW, PKO, FPA, NAND, KEY, RAD, L2C, LMC, GMX, AGL,
           DFM, DFA, error handling interrupts. */ 
        if (OCTEON_IS_MODEL(OCTEON_CN68XX))
        {
            int i;

            for (i = 0; i < 5; i++)
            {
                cvmx_interrupt_register(CVMX_IRQ_AGX0+i, __cvmx_interrupt_ecc, NULL);
                cvmx_interrupt_unmask_irq(CVMX_IRQ_AGX0+i);
            }
            cvmx_interrupt_register(CVMX_IRQ_NAND, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_NAND);
            cvmx_interrupt_register(CVMX_IRQ_MIO, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_MIO);
            cvmx_interrupt_register(CVMX_IRQ_FPA, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_FPA);
            cvmx_interrupt_register(CVMX_IRQ_IPD, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_IPD);
            cvmx_interrupt_register(CVMX_IRQ_PIP, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_PIP);
            cvmx_interrupt_register(CVMX_IRQ_POW, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_POW);
            cvmx_interrupt_register(CVMX_IRQ_L2C, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_L2C);
            cvmx_interrupt_register(CVMX_IRQ_PKO, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_PKO);
            cvmx_interrupt_register(CVMX_IRQ_ZIP, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_ZIP);
            cvmx_interrupt_register(CVMX_IRQ_RAD, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_RAD);
            cvmx_interrupt_register(CVMX_IRQ_KEY, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_KEY);
            /* Before enabling SLI interrupt clear any RML_TO interrupt */
            if (cvmx_read_csr(CVMX_PEXP_SLI_INT_SUM) & 0x1)
            {
                cvmx_safe_printf("clearing pending SLI_INT_SUM[RML_TO] interrupt (ignore)\n");
                cvmx_write_csr(CVMX_PEXP_SLI_INT_SUM, 1);
            }
            cvmx_interrupt_register(CVMX_IRQ_SLI, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_SLI);
            cvmx_interrupt_register(CVMX_IRQ_DPI, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_DPI);
            cvmx_interrupt_register(CVMX_IRQ_DFA, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_DFA);
            cvmx_interrupt_register(CVMX_IRQ_AGL, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_AGL);
            for (i = 0; i < 4; i++)
            {
                cvmx_interrupt_register(CVMX_IRQ_LMC0+i, __cvmx_interrupt_ecc, NULL);
                cvmx_interrupt_unmask_irq(CVMX_IRQ_LMC0+i);
            }
            cvmx_interrupt_register(CVMX_IRQ_DFM, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_DFM);
            cvmx_interrupt_register(CVMX_IRQ_RST, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_RST);
            cvmx_interrupt_register(CVMX_IRQ_ILK, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_ILK);
        }
        else
        {
            cvmx_interrupt_register(CVMX_IRQ_RML, __cvmx_interrupt_ecc, NULL);
            cvmx_interrupt_unmask_irq(CVMX_IRQ_RML);
        }

        cvmx_atomic_set32(&cvmx_interrupt_initialize_flag, 1);
    }

    while (!cvmx_atomic_get32(&cvmx_interrupt_initialize_flag))
        ; /* Wait for first core to finish above. */

    if (OCTEON_IS_MODEL(OCTEON_CN68XX)) {
        cvmx_interrupt_unmask_irq(CVMX_IRQ_MIPS2);
    } else {
        cvmx_interrupt_unmask_irq(CVMX_IRQ_MIPS2);
        cvmx_interrupt_unmask_irq(CVMX_IRQ_MIPS3);
    }

    CVMX_ICACHE_INVALIDATE;

    /* Enable interrupts for each core (bit0 of COP0 Status) */
    cvmx_interrupt_restore(1);
}



/**
 * Set the exception handler for all non interrupt sources.
 *
 * @param handler New exception handler
 * @return Old exception handler
 */
cvmx_interrupt_exception_t cvmx_interrupt_set_exception(cvmx_interrupt_exception_t handler)
{
    cvmx_interrupt_exception_t result = cvmx_interrupt_state.exception_handler;
    cvmx_interrupt_state.exception_handler = handler;
    CVMX_SYNCWS;
    return result;
}
#endif /* !__U_BOOT__ */


