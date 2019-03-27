/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2015, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/


#ifndef _FREEBSD_OS_H_
#define _FREEBSD_OS_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/iflib.h>



#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/clock.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>


#define ASSERT(x) if(!(x)) panic("EM: x")
#define us_scale(x)  max(1, (x/(1000000/hz)))
static inline int
ms_scale(int x) {
	if (hz == 1000) {
		return (x);
	} else if (hz > 1000) {
		return (x*(hz/1000));
	} else {
		return (max(1, x/(1000/hz)));
	}
}

static inline void
safe_pause_us(int x) {
	if (cold) {
		DELAY(x);
	} else {
		pause("e1000_delay", max(1,  x/(1000000/hz)));
	}
}

static inline void
safe_pause_ms(int x) {
	if (cold) {
		DELAY(x*1000);
	} else {
		pause("e1000_delay", ms_scale(x));
	}
}

#define usec_delay(x) safe_pause_us(x)
#define usec_delay_irq(x) usec_delay(x)
#define msec_delay(x) safe_pause_ms(x)
#define msec_delay_irq(x) msec_delay(x)

/* Enable/disable debugging statements in shared code */
#define DBG		0

#define DEBUGOUT(...) \
    do { if (DBG) printf(__VA_ARGS__); } while (0)
#define DEBUGOUT1(...)			DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT2(...)			DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT3(...)			DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT7(...)			DEBUGOUT(__VA_ARGS__)
#define DEBUGFUNC(F)			DEBUGOUT(F "\n")

#define STATIC			static
#define FALSE			0
#define TRUE			1
#define CMD_MEM_WRT_INVALIDATE	0x0010  /* BIT_4 */
#define PCI_COMMAND_REGISTER	PCIR_COMMAND

typedef uint64_t	u64;
typedef uint32_t	u32;
typedef uint16_t	u16;
typedef uint8_t		u8;
typedef int64_t		s64;
typedef int32_t		s32;
typedef int16_t		s16;
typedef int8_t		s8;

#define __le16		u16
#define __le32		u32
#define __le64		u64

#if __FreeBSD_version < 800000
#if defined(__i386__) || defined(__amd64__)
#define mb()	__asm volatile("mfence" ::: "memory")
#define wmb()	__asm volatile("sfence" ::: "memory")
#define rmb()	__asm volatile("lfence" ::: "memory")
#else
#define mb()
#define rmb()
#define wmb()
#endif
#endif /*__FreeBSD_version < 800000 */

#ifdef INVARIANTS
#define ASSERT_CTX_LOCK_HELD(hw) (sx_assert(iflib_ctx_lock_get(((struct e1000_osdep *)hw->back)->ctx), SX_XLOCKED))
#else
#define ASSERT_CTX_LOCK_HELD(hw)
#endif

#if defined(__i386__) || defined(__amd64__)
static __inline
void prefetch(void *x)
{
	__asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
}
#else
#define prefetch(x)
#endif

struct e1000_osdep
{
	bus_space_tag_t    mem_bus_space_tag;
	bus_space_handle_t mem_bus_space_handle;
	bus_space_tag_t    io_bus_space_tag;
	bus_space_handle_t io_bus_space_handle;
	bus_space_tag_t    flash_bus_space_tag;
	bus_space_handle_t flash_bus_space_handle;
	device_t	   dev;
	if_ctx_t	   ctx;
};

#define E1000_REGISTER(hw, reg) (((hw)->mac.type >= e1000_82543) \
    ? reg : e1000_translate_register_82542(reg))

#define E1000_WRITE_FLUSH(a) E1000_READ_REG(a, E1000_STATUS)

/* Read from an absolute offset in the adapter's memory space */
#define E1000_READ_OFFSET(hw, offset) \
    bus_space_read_4(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
    ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, offset)

/* Write to an absolute offset in the adapter's memory space */
#define E1000_WRITE_OFFSET(hw, offset, value) \
    bus_space_write_4(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
    ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, offset, value)

/* Register READ/WRITE macros */

#define E1000_READ_REG(hw, reg) \
    bus_space_read_4(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, \
        E1000_REGISTER(hw, reg))

#define E1000_WRITE_REG(hw, reg, value) \
    bus_space_write_4(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, \
        E1000_REGISTER(hw, reg), value)

#define E1000_READ_REG_ARRAY(hw, reg, index) \
    bus_space_read_4(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, \
        E1000_REGISTER(hw, reg) + ((index)<< 2))

#define E1000_WRITE_REG_ARRAY(hw, reg, index, value) \
    bus_space_write_4(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, \
        E1000_REGISTER(hw, reg) + ((index)<< 2), value)

#define E1000_READ_REG_ARRAY_DWORD E1000_READ_REG_ARRAY
#define E1000_WRITE_REG_ARRAY_DWORD E1000_WRITE_REG_ARRAY

#define E1000_READ_REG_ARRAY_BYTE(hw, reg, index) \
    bus_space_read_1(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, \
        E1000_REGISTER(hw, reg) + index)

#define E1000_WRITE_REG_ARRAY_BYTE(hw, reg, index, value) \
    bus_space_write_1(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, \
        E1000_REGISTER(hw, reg) + index, value)

#define E1000_WRITE_REG_ARRAY_WORD(hw, reg, index, value) \
    bus_space_write_2(((struct e1000_osdep *)(hw)->back)->mem_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->mem_bus_space_handle, \
        E1000_REGISTER(hw, reg) + (index << 1), value)

#define E1000_WRITE_REG_IO(hw, reg, value) do {\
    bus_space_write_4(((struct e1000_osdep *)(hw)->back)->io_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->io_bus_space_handle, \
        (hw)->io_base, reg); \
    bus_space_write_4(((struct e1000_osdep *)(hw)->back)->io_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->io_bus_space_handle, \
        (hw)->io_base + 4, value); } while (0)

#define E1000_READ_FLASH_REG(hw, reg) \
    bus_space_read_4(((struct e1000_osdep *)(hw)->back)->flash_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->flash_bus_space_handle, reg)

#define E1000_READ_FLASH_REG16(hw, reg) \
    bus_space_read_2(((struct e1000_osdep *)(hw)->back)->flash_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->flash_bus_space_handle, reg)

#define E1000_WRITE_FLASH_REG(hw, reg, value) \
    bus_space_write_4(((struct e1000_osdep *)(hw)->back)->flash_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->flash_bus_space_handle, reg, value)

#define E1000_WRITE_FLASH_REG16(hw, reg, value) \
    bus_space_write_2(((struct e1000_osdep *)(hw)->back)->flash_bus_space_tag, \
        ((struct e1000_osdep *)(hw)->back)->flash_bus_space_handle, reg, value)


#if defined(INVARIANTS)
#include <sys/proc.h>

#define ASSERT_NO_LOCKS()				\
	do {						\
	     int unknown_locks = curthread->td_locks - mtx_owned(&Giant);	\
	     if (unknown_locks > 0) {					\
		     WITNESS_WARN(WARN_GIANTOK|WARN_SLEEPOK|WARN_PANIC, NULL, "unexpected non-sleepable lock"); \
	     }								\
	     MPASS(curthread->td_rw_rlocks == 0);			\
	     MPASS(curthread->td_lk_slocks == 0);			\
	} while (0)
#else
#define ASSERT_NO_LOCKS()
#endif

#endif  /* _FREEBSD_OS_H_ */

