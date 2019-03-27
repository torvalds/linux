/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 * @defgroup group_services Platform Services API
 *  @{
 * The Platform Services API provides miscellaneous system services to HAL
 * drivers, such as:
 * - Registers read/write
 * - Assertions
 * - Memory barriers
 * - Endianness conversions
 *
 * And more.
 * @file   plat_api/sample/al_hal_plat_services.h
 *
 * @brief  API for Platform services provided for to HAL drivers
 *
 *
 */

#ifndef __PLAT_SERVICES_H__
#define __PLAT_SERVICES_H__

#include <machine/atomic.h>
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/bus.h>

/* Prototypes for all the bus_space structure functions */
uint8_t	generic_bs_r_1(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset);

uint16_t generic_bs_r_2(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset);

uint32_t generic_bs_r_4(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset);

void generic_bs_w_1(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value);

void generic_bs_w_2(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value);

void generic_bs_w_4(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value);

void generic_bs_w_8(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, uint64_t value);

#define __UNUSED __attribute__((unused))

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
  * Make sure data will be visible by other masters (other CPUS and DMA).
  * usually this is achieved by the ARM DMB instruction.
  */
static void al_data_memory_barrier(void);
static void al_smp_data_memory_barrier(void);

/**
  * Make sure data will be visible by DMA masters, no restriction for other cpus
  */
static inline void
al_data_memory_barrier(void)
{
#ifndef __aarch64__
	dsb();
#else
	dsb(sy);
#endif
}

/**
  * Make sure data will be visible in order by other cpus masters.
  */
static inline void
al_smp_data_memory_barrier(void)
{
#ifndef __aarch64__
	dmb();
#else
	dmb(ish);
#endif
}

/**
  * Make sure write data will be visible in order by other cpus masters.
  */
static inline void
al_local_data_memory_barrier(void)
{
#ifndef __aarch64__
	dsb();
#else
	dsb(sy);
#endif
}

/*
 * WMA: This is a hack which allows not modifying the __iomem accessing HAL code.
 * On ARMv7, bus_handle holds the information about VA of accessed memory. It
 * is possible to use direct load/store instruction instead of bus_dma machinery.
 * WARNING: This is not guaranteed to stay that way forever, nor that
 * on other architectures these variables behave similarly. Keep that
 * in mind during porting to other systems.
 */
/**
 * Read MMIO 8 bits register
 * @param  offset	register offset
 *
 * @return register value
 */
static uint8_t al_reg_read8(uint8_t * offset);

/**
 * Read MMIO 16 bits register
 * @param  offset	register offset
 *
 * @return register value
 */
static uint16_t al_reg_read16(uint16_t * offset);

/**
 * Read MMIO 32 bits register
 * @param  offset	register offset
 *
 * @return register value
 */
static uint32_t al_reg_read32(uint32_t * offset);

/**
 * Read MMIO 64 bits register
 * @param  offset	register offset
 *
 * @return register value
 */
uint64_t al_reg_read64(uint64_t * offset);

/**
 * Relaxed read MMIO 32 bits register
 *
 * Relaxed register read/write functions don't involve cpu instructions that
 * force syncronization, nor ordering between the register access and memory
 * data access.
 * These instructions are used in performance critical code to avoid the
 * overhead of the synchronization instructions.
 *
 * @param  offset	register offset
 *
 * @return register value
 */
#define al_bus_dma_to_va(bus_tag, bus_handle)	((void*)bus_handle)

/**
 * Relaxed read MMIO 32 bits register
 *
 * Relaxed register read/write functions don't involve cpu instructions that
 * force syncronization, nor ordering between the register access and memory
 * data access.
 * These instructions are used in performance critical code to avoid the
 * overhead of the synchronization instructions.
 *
 * @param  offset	register offset
 *
 * @return register value
 */
#define al_reg_read32_relaxed(l)	generic_bs_r_4(NULL, (bus_space_handle_t)l, 0)

/**
 * Relaxed write to MMIO 32 bits register
 *
 * Relaxed register read/write functions don't involve cpu instructions that
 * force syncronization, nor ordering between the register access and memory
 * data access.
 * These instructions are used in performance critical code to avoid the
 * overhead of the synchronization instructions.
 *
 * @param  offset	register offset
 * @param  val		value to write to the register
 */
#define al_reg_write32_relaxed(l,v)	generic_bs_w_4(NULL, (bus_space_handle_t)l, 0, v)

/**
 * Write to MMIO 8 bits register
 * @param  offset	register offset
 * @param  val		value to write to the register
 */
#define al_reg_write8(l, v) do {				\
	al_data_memory_barrier();				\
	generic_bs_w_1(NULL, (bus_space_handle_t)l, 0, v);	\
	al_smp_data_memory_barrier();				\
} while (0)

/**
 * Write to MMIO 16 bits register
 * @param  offset	register offset
 * @param  val		value to write to the register
 */
#define al_reg_write16(l, v) do {				\
	al_data_memory_barrier();				\
	generic_bs_w_2(NULL, (bus_space_handle_t)l, 0, v);	\
	al_smp_data_memory_barrier();				\
} while (0)

/**
 * Write to MMIO 32 bits register
 * @param  offset	register offset
 * @param  val		value to write to the register
 */
#define al_reg_write32(l, v) do {				\
	al_data_memory_barrier();				\
	generic_bs_w_4(NULL, (bus_space_handle_t)l, 0, v);	\
	al_smp_data_memory_barrier();				\
} while (0)

/**
 * Write to MMIO 64 bits register
 * @param  offset	register offset
 * @param  val		value to write to the register
 */
#define al_reg_write64(l, v) do {				\
	al_data_memory_barrier();				\
	generic_bs_w_8(NULL, (bus_space_handle_t)l, 0, v);	\
	al_smp_data_memory_barrier();				\
} while (0)

static inline uint8_t
al_reg_read8(uint8_t *l)
{

	al_data_memory_barrier();
	return (generic_bs_r_1(NULL, (bus_space_handle_t)l, 0));
}

static inline uint16_t
al_reg_read16(uint16_t *l)
{

	al_data_memory_barrier();
	return (generic_bs_r_2(NULL, (bus_space_handle_t)l, 0));
}

static inline uint32_t
al_reg_read32(uint32_t *l)
{

	al_data_memory_barrier();
	return (generic_bs_r_4(NULL, (bus_space_handle_t)l, 0));
}

#define AL_DBG_LEVEL_NONE 0
#define AL_DBG_LEVEL_ERR 1
#define AL_DBG_LEVEL_WARN 2
#define AL_DBG_LEVEL_INFO 3
#define AL_DBG_LEVEL_DBG 4

#define AL_DBG_LEVEL AL_DBG_LEVEL_ERR

#define AL_DBG_LOCK()
#define AL_DBG_UNLOCK()

/**
 * print message
 *
 * @param format The format string
 * @param ... Additional arguments
 */
#define al_print(type, fmt, ...) 		do { if (AL_DBG_LEVEL >= AL_DBG_LEVEL_NONE) { AL_DBG_LOCK(); printf(fmt, ##__VA_ARGS__); AL_DBG_UNLOCK(); } } while(0)

/**
 * print error message
 *
 * @param format
 */
#define al_err(...)			do { if (AL_DBG_LEVEL >= AL_DBG_LEVEL_ERR) { AL_DBG_LOCK(); printf(__VA_ARGS__); AL_DBG_UNLOCK(); } } while(0)

/**
 * print warning message
 *
 * @param format
 */
#define al_warn(...)			do { if (AL_DBG_LEVEL >= AL_DBG_LEVEL_WARN) { AL_DBG_LOCK(); printf(__VA_ARGS__); AL_DBG_UNLOCK(); } } while(0)

/**
 * print info message
 *
 * @param format
 */
#define al_info(...)			do { if (AL_DBG_LEVEL >= AL_DBG_LEVEL_INFO) { AL_DBG_LOCK(); printf(__VA_ARGS__); AL_DBG_UNLOCK(); } } while(0)

/**
 * print debug message
 *
 * @param format
 */
#define al_dbg(...)			do { if (AL_DBG_LEVEL >= AL_DBG_LEVEL_DBG) { AL_DBG_LOCK(); printf(__VA_ARGS__); AL_DBG_UNLOCK(); } } while(0)

/**
 * Assertion
 *
 * @param condition
 */
#define al_assert(COND)		\
	do {			\
		if (!(COND))	\
			al_err(	\
			"%s:%d:%s: Assertion failed! (%s)\n",	\
			__FILE__, __LINE__, __func__, #COND);	\
	} while(AL_FALSE)

/**
 * al_udelay - micro sec delay
 */
#define al_udelay(u)		DELAY(u)

/**
 * al_msleep - mili sec delay
 */
#define al_msleep(m)		DELAY((m) * 1000)

/**
 * swap half word to little endian
 *
 * @param x 16 bit value
 *
 * @return the value in little endian
 */
#define swap16_to_le(x)		htole16(x)
/**
 * swap word to little endian
 *
 * @param x 32 bit value
 *
 * @return the value in little endian
 */
#define swap32_to_le(x)		htole32(x)

/**
 * swap 8 bytes to little endian
 *
 * @param x 64 bit value
 *
 * @return the value in little endian
 */
#define swap64_to_le(x)		htole64(x)

/**
 * swap half word from little endian
 *
 * @param x 16 bit value
 *
 * @return the value in the cpu endianess
 */
#define swap16_from_le(x)	le16toh(x)

/**
 * swap word from little endian
 *
 * @param x 32 bit value
 *
 * @return the value in the cpu endianess
 */
#define swap32_from_le(x)	le32toh(x)

/**
 * swap 8 bytes from little endian
 *
 * @param x 64 bit value
 *
 * @return the value in the cpu endianess
 */
#define swap64_from_le(x)	le64toh(x)

/**
 * Memory set
 *
 * @param p memory pointer
 * @param val value for setting
 * @param cnt number of bytes to set
 */
#define al_memset(p, val, cnt)	memset(p, val, cnt)

/**
 * Memory copy
 *
 * @param p1 memory pointer
 * @param p2 memory pointer
 * @param cnt number of bytes to copy
 */
#define al_memcpy(p1, p2, cnt)	memcpy(p1, p2, cnt)

/**
 * Memory compare
 *
 * @param p1 memory pointer
 * @param p2 memory pointer
 * @param cnt number of bytes to compare
 */
#define al_memcmp(p1, p2, cnt)	memcmp(p1, p2, cnt)

/**
 * String compare
 *
 * @param s1 string pointer
 * @param s2 string pointer
 */
#define al_strcmp(s1, s2)	strcmp(s1, s2)

#define al_get_cpu_id()		0

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */
/** @} end of Platform Services API group */
#endif				/* __PLAT_SERVICES_H__ */
