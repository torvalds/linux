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
 * cvmx-log supplies a fast log buffer implementation. Each core writes
 * log data to a differnet buffer to avoid synchronization overhead. Function
 * call logging can be turned on with the GCC option "-pg".
 *
 * <hr>$Revision: 70030 $<hr>
 */
#include "cvmx.h"
#include "cvmx-core.h"
#include "cvmx-log.h"

#define CVMX_LOG_BUFFER_SIZE (1<<15)
#define CVMX_LOG_NUM_BUFFERS 4

/**
 * The possible types of log data that can be stored in the
 * buffer.
 */
typedef enum
{
    CVMX_LOG_TYPE_PC = 0,   /**< Log of the program counter location. used for code profiling / tracing */
    CVMX_LOG_TYPE_PRINTF,   /**< Constant printf format string with two 64bit arguments */
    CVMX_LOG_TYPE_DATA,     /**< Arbitrary array of dwords. Max size is 31 dwords */
    CVMX_LOG_TYPE_STRUCTURE,/**< Log a structured data element. Max size is 30 dwords */
    CVMX_LOG_TYPE_PERF,     /**< Mips performance counters control registers followed by the data */
} cvmx_log_type_t;

/**
 * Header definition for each log entry.
 */
typedef union
{
    uint64_t u64;
    struct
    {
        cvmx_log_type_t     type    : 3; /* Data in the log entry */
        uint64_t            size    : 8; /* Data size in 64bit words */
        uint64_t            cycle   :53; /* Low bits of the cycle counter as a timestamp */
    } s;
} cvmx_log_header_t;

/**
 * Circular log buffer. Each processor gets a private one to
 * write to. Log entries are added at the current write
 * location, then the write location is incremented. The
 * buffer may wrap in the middle of a log entry.
 */
static uint64_t cvmx_log_buffers[CVMX_LOG_NUM_BUFFERS][CVMX_LOG_BUFFER_SIZE];

/**
 * Current locations in the log.
 */
uint64_t *cvmx_log_buffer_write_ptr             = NULL; /* The next write will occur here */
uint64_t *cvmx_log_buffer_end_ptr               = NULL; /* Write must move to the next buffer when it equals this */
uint64_t *cvmx_log_buffer_head_ptr              = NULL; /* Pointer to begin extracting log data from */
static uint64_t *cvmx_log_buffer_read_ptr       = NULL; /* Location cvmx_display is reading from */
static uint64_t *cvmx_log_buffer_read_end_ptr   = NULL; /* Location where read will need the next buffer */
uint64_t cvmx_log_mcd0_on_full                  = 0;    /* If this is set, cvm-log will assert MCD0 when the log
                                                            is full. This is set by the remote logging utility through
                                                            the debugger interface. */


/**
 * @INTERNAL
 * Initialize the log for writing
 */
static void __cvmx_log_initialize(void) CVMX_LOG_DISABLE_PC_LOGGING;
static void __cvmx_log_initialize(void)
{
    int buf_num;

    /* Link the buffers together using the last element in each buffer */
    for (buf_num=0; buf_num<CVMX_LOG_NUM_BUFFERS-1; buf_num++)
        cvmx_log_buffers[buf_num][CVMX_LOG_BUFFER_SIZE-1] = CAST64(cvmx_log_buffers[buf_num+1]);
    cvmx_log_buffers[CVMX_LOG_NUM_BUFFERS-1][CVMX_LOG_BUFFER_SIZE-1] = CAST64(NULL);

    cvmx_log_buffer_head_ptr = &cvmx_log_buffers[0][0];
    cvmx_log_buffer_write_ptr = &cvmx_log_buffers[0][0];
    cvmx_log_buffer_end_ptr = cvmx_log_buffer_write_ptr + CVMX_LOG_BUFFER_SIZE-1;
}


/**
 * @INTERNAL
 * Called when the log is full of data. This function must
 * make room for more log data before returning.
 */
static void __cvmx_log_full_process(void) CVMX_LOG_DISABLE_PC_LOGGING;
static void __cvmx_log_full_process(void)
{
    if (cvmx_log_mcd0_on_full)
    {
        register uint64_t tmp;
        /* Pulse MCD0 signal so a remote utility can extract the data */
        asm volatile (
            "dmfc0 %0, $22\n"
	        "ori   %0, %0, 0x1110\n"
            "dmtc0 %0, $22\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            : "=r" (tmp));
    }
    /* The write ptr may have been modifed by the debugger, check it again */
    if (!(volatile uint64_t)CAST64(cvmx_log_buffer_write_ptr))
    {
        #ifndef __KERNEL__
            /* Disabled for the Linux kernel since printk is also profiled */
            cvmx_dprintf("Log is full, reusing first buffer\n");
        #endif
        *cvmx_log_buffer_end_ptr = CAST64(cvmx_log_buffer_head_ptr);
        cvmx_log_buffer_write_ptr = cvmx_log_buffer_head_ptr;
        cvmx_log_buffer_end_ptr = cvmx_log_buffer_write_ptr + CVMX_LOG_BUFFER_SIZE-1;
        cvmx_log_buffer_head_ptr = CASTPTR(uint64_t, *cvmx_log_buffer_end_ptr);
        *cvmx_log_buffer_end_ptr = CAST64(NULL);
    }
}


/**
 * @INTERNAL
 * Simple inline function to build a log header
 *
 * @param type   Type of header to build
 * @param size   Amount of data that follows the header in dwords
 * @return The header
 */
static inline uint64_t __cvmx_log_build_header(cvmx_log_type_t type, uint64_t size) CVMX_LOG_DISABLE_PC_LOGGING;
static inline uint64_t __cvmx_log_build_header(cvmx_log_type_t type, uint64_t size)
{
    cvmx_log_header_t header;
    header.u64 = 0;
    header.s.type = type;
    header.s.size = size;
    header.s.cycle = cvmx_get_cycle();
    return header.u64;
}


/**
 * @INTERNAL
 * Function to write and increment the position. It rotates
 * to the next log buffer as necessary.
 *
 * @param data   Data to write to the log
 */
static inline void __cvmx_log_write(uint64_t data) CVMX_LOG_DISABLE_PC_LOGGING;
static inline void __cvmx_log_write(uint64_t data)
{
    /* Check and see if we need to rotate the log */
    if (cvmx_likely(cvmx_log_buffer_write_ptr != cvmx_log_buffer_end_ptr))
    {
        /* No rotate is necessary, just write the data */
        *cvmx_log_buffer_write_ptr++ = data;
    }
    else
    {
        /* Initialize the log if necessary */
        if (cvmx_unlikely(cvmx_log_buffer_head_ptr == NULL))
            __cvmx_log_initialize();
        else
        {
            cvmx_log_buffer_write_ptr = CASTPTR(uint64_t, *cvmx_log_buffer_end_ptr);
            if (cvmx_likely(cvmx_log_buffer_write_ptr))
            {
                /* Rotate the log. Might be a good time to send the old buffer
                    somewhere */
                cvmx_log_buffer_end_ptr = cvmx_log_buffer_write_ptr + CVMX_LOG_BUFFER_SIZE-1;
            }
            else
                __cvmx_log_full_process();    /* After this function returns, the log must be ready for updates */
        }
        *cvmx_log_buffer_write_ptr++ = data;
    }
}


/**
 * Log a program counter address to the log. This is caused by
 * the assembly code function mcount when writing the PC value
 * is more complicated that the simple case support by it.
 *
 * @param pc     Program counter address to log
 */
void cvmx_log_pc(uint64_t pc) CVMX_LOG_DISABLE_PC_LOGGING;
void cvmx_log_pc(uint64_t pc)
{
    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_PC, 1));
    __cvmx_log_write(pc);
}


/**
 * Log a constant printf style format string with 0 to 4
 * arguments. The string must persist until the log is read,
 * but the parameters are copied into the log.
 *
 * @param format  Constant printf style format string.
 */
void cvmx_log_printf0(const char *format)
{
    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_PRINTF, 1));
    __cvmx_log_write(CAST64(format));
}


/**
 * Log a constant printf style format string with 0 to 4
 * arguments. The string must persist until the log is read,
 * but the parameters are copied into the log.
 *
 * @param format  Constant printf style format string.
 * @param number1 64bit argument to the printf format string
 */
void cvmx_log_printf1(const char *format, uint64_t number1)
{
    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_PRINTF, 2));
    __cvmx_log_write(CAST64(format));
    __cvmx_log_write(number1);
}


/**
 * Log a constant printf style format string with 0 to 4
 * arguments. The string must persist until the log is read,
 * but the parameters are copied into the log.
 *
 * @param format  Constant printf style format string.
 * @param number1 64bit argument to the printf format string
 * @param number2 64bit argument to the printf format string
 */
void cvmx_log_printf2(const char *format, uint64_t number1, uint64_t number2)
{
    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_PRINTF, 3));
    __cvmx_log_write(CAST64(format));
    __cvmx_log_write(number1);
    __cvmx_log_write(number2);
}


/**
 * Log a constant printf style format string with 0 to 4
 * arguments. The string must persist until the log is read,
 * but the parameters are copied into the log.
 *
 * @param format  Constant printf style format string.
 * @param number1 64bit argument to the printf format string
 * @param number2 64bit argument to the printf format string
 * @param number3 64bit argument to the printf format string
 */
void cvmx_log_printf3(const char *format, uint64_t number1, uint64_t number2, uint64_t number3)
{
    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_PRINTF, 4));
    __cvmx_log_write(CAST64(format));
    __cvmx_log_write(number1);
    __cvmx_log_write(number2);
    __cvmx_log_write(number3);
}


/**
 * Log a constant printf style format string with 0 to 4
 * arguments. The string must persist until the log is read,
 * but the parameters are copied into the log.
 *
 * @param format  Constant printf style format string.
 * @param number1 64bit argument to the printf format string
 * @param number2 64bit argument to the printf format string
 * @param number3 64bit argument to the printf format string
 * @param number4 64bit argument to the printf format string
 */
void cvmx_log_printf4(const char *format, uint64_t number1, uint64_t number2, uint64_t number3, uint64_t number4)
{
    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_PRINTF, 5));
    __cvmx_log_write(CAST64(format));
    __cvmx_log_write(number1);
    __cvmx_log_write(number2);
    __cvmx_log_write(number3);
    __cvmx_log_write(number4);
}


/**
 * Log an arbitrary block of 64bit words. At most 255 64bit
 * words can be logged. The words are copied into the log.
 *
 * @param size_in_dwords
 *               Number of 64bit dwords to copy into the log.
 * @param data   Array of 64bit dwords to copy
 */
void cvmx_log_data(uint64_t size_in_dwords, const uint64_t *data)
{
    if (size_in_dwords > 255)
        size_in_dwords = 255;

    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_DATA, size_in_dwords));
    while (size_in_dwords--)
        __cvmx_log_write(*data++);
}


/**
 * Log a structured data object. Post processing will use the
 * debugging information in the ELF file to determine how to
 * display the structure. Max of 2032 bytes.
 *
 * Example:
 * cvmx_log_structure("cvmx_wqe_t", work, sizeof(*work));
 *
 * @param type   C typedef expressed as a string. This will be used to
 *               lookup the structure in the debugging infirmation.
 * @param data   Data to be written to the log.
 * @param size_in_bytes
 *               Size if the data in bytes. Normally you'll use the
 *               sizeof() operator here.
 */
void cvmx_log_structure(const char *type, void *data, int size_in_bytes)
{
    uint64_t size_in_dwords = (size_in_bytes + 7) >> 3;
    uint64_t *ptr = (uint64_t*)data;

    if (size_in_dwords > 254)
        size_in_dwords = 254;

    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_STRUCTURE, size_in_dwords + 1));
    __cvmx_log_write(CAST64(type));
    while (size_in_dwords--)
        __cvmx_log_write(*ptr++);
}


/**
 * Setup the mips performance counters
 *
 * @param counter1 Event type for counter 1
 * @param counter2 Event type for counter 2
 */
void cvmx_log_perf_setup(cvmx_core_perf_t counter1, cvmx_core_perf_t counter2)
{
    cvmx_core_perf_control_t control;

    control.u32 = 0;
    control.s.event = counter1;
    control.s.u = 1;
    control.s.s = 1;
    control.s.k = 1;
    control.s.ex = 1;
    asm ("mtc0 %0, $25, 0\n" : : "r"(control.u32));
    control.s.event = counter2;
    asm ("mtc0 %0, $25, 2\n" : : "r"(control.u32));
}


/**
 * Log the performance counters
 */
void cvmx_log_perf(void)
{
    uint64_t control1;
    uint64_t control2;
    uint64_t data1;
    uint64_t data2;
    asm ("dmfc0 %0, $25, 1\n" : "=r"(data1));
    asm ("dmfc0 %0, $25, 3\n" : "=r"(data2));
    asm ("mfc0 %0, $25, 0\n" : "=r"(control1));
    asm ("mfc0 %0, $25, 2\n" : "=r"(control2));
    __cvmx_log_write(__cvmx_log_build_header(CVMX_LOG_TYPE_PERF, 3));
    __cvmx_log_write(((control1 & 0xffffffff) << 32) | (control2 & 0xffffffff));
    __cvmx_log_write(data1);
    __cvmx_log_write(data2);
}


/**
 * @INTERNAL
 * Read a dword from the log
 *
 * @return the dword
 */
static uint64_t __cvmx_log_read(void) CVMX_LOG_DISABLE_PC_LOGGING;
static uint64_t __cvmx_log_read(void)
{
    uint64_t data;

    /* Check and see if we need to rotate the log */
    if (cvmx_likely(cvmx_log_buffer_read_ptr != cvmx_log_buffer_read_end_ptr))
    {
        /* No rotate is necessary, just read the data */
        data = *cvmx_log_buffer_read_ptr++;
    }
    else
    {
        cvmx_log_buffer_read_ptr = CASTPTR(uint64_t, *cvmx_log_buffer_read_end_ptr);
        if (cvmx_likely(cvmx_log_buffer_read_ptr))
        {
            /* Rotate to the next log buffer */
            cvmx_log_buffer_read_end_ptr = cvmx_log_buffer_read_ptr + CVMX_LOG_BUFFER_SIZE-1;
            data = *cvmx_log_buffer_read_ptr++;
        }
        else
        {
            /* No more log buffers, return 0 */
            cvmx_log_buffer_read_end_ptr = NULL;
            data = 0;
        }
    }

    return data;
}


/**
 * Display the current log in a human readable format.
 */
void cvmx_log_display(void)
{
    unsigned int i;
    cvmx_log_header_t header;

    cvmx_log_buffer_read_ptr = cvmx_log_buffer_head_ptr;
    cvmx_log_buffer_read_end_ptr = cvmx_log_buffer_read_ptr + CVMX_LOG_BUFFER_SIZE-1;

    while (cvmx_log_buffer_read_ptr && (cvmx_log_buffer_read_ptr != cvmx_log_buffer_write_ptr))
    {
        header.u64 = __cvmx_log_read();
        if (header.s.cycle == 0)
            continue;
        printf("%llu: ", (unsigned long long)header.s.cycle);
        switch (header.s.type)
        {
            case CVMX_LOG_TYPE_PC:
                if (header.s.size == 1)
                    printf("pc 0x%016llx\n", (unsigned long long)__cvmx_log_read());
                else
                    printf("Illegal size (%d) for log entry: pc\n", header.s.size);
                break;
            case CVMX_LOG_TYPE_PRINTF:
                switch (header.s.size)
                {
                    case 1:
                        printf(CASTPTR(const char, __cvmx_log_read()));
                        break;
                    case 2:
                        printf(CASTPTR(const char, __cvmx_log_read()), __cvmx_log_read());
                        break;
                    case 3:
                        printf(CASTPTR(const char, __cvmx_log_read()), __cvmx_log_read(), __cvmx_log_read());
                        break;
                    case 4:
                        printf(CASTPTR(const char, __cvmx_log_read()), __cvmx_log_read(), __cvmx_log_read(), __cvmx_log_read());
                        break;
                    case 5:
                        printf(CASTPTR(const char, __cvmx_log_read()), __cvmx_log_read(), __cvmx_log_read(), __cvmx_log_read(), __cvmx_log_read());
                        break;
                    default:
                        printf("Illegal size (%d) for log entry: printf\n", header.s.size);
                        break;
                }
                printf("\n");
                break;
            case CVMX_LOG_TYPE_DATA:
                printf("data");
                for (i=0; i<header.s.size; i++)
                    printf(" 0x%016llx", (unsigned long long)__cvmx_log_read());
                printf("\n");
                break;
            case CVMX_LOG_TYPE_STRUCTURE:
                printf("struct %s", CASTPTR(const char, __cvmx_log_read()));
                for (i=1; i<header.s.size; i++)
                    printf(" 0x%016llx", (unsigned long long)__cvmx_log_read());
                printf("\n");
                break;
            case CVMX_LOG_TYPE_PERF:
                if (header.s.size == 3)
                {
                    unsigned long long control = __cvmx_log_read();
                    unsigned long long data1 = __cvmx_log_read();
                    unsigned long long data2 = __cvmx_log_read();
                    printf("perf control=0x%016llx data1=0x%016llx data2=0x%016llx\n", control, data1, data2);
                }
                else
                    printf("Illegal size (%d) for log entry: perf\n", header.s.size);
                break;
            default:
                break;
        }
    }
}

