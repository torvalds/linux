/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2017, Intel Corporation
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

#ifndef _IXGBE_OSDEP_H_
#define _IXGBE_OSDEP_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/clock.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#define ASSERT(x) if(!(x)) panic("IXGBE: x")
#define EWARN(H, W) printf(W)

enum {
	IXGBE_ERROR_SOFTWARE,
	IXGBE_ERROR_POLLING,
	IXGBE_ERROR_INVALID_STATE,
	IXGBE_ERROR_UNSUPPORTED,
	IXGBE_ERROR_ARGUMENT,
	IXGBE_ERROR_CAUTION,
};

/* The happy-fun DELAY macro is defined in /usr/src/sys/i386/include/clock.h */
#define usec_delay(x) DELAY(x)
#define msec_delay(x) DELAY(1000*(x))

#define DBG 0
#define MSGOUT(S, A, B)     printf(S "\n", A, B)
#define DEBUGFUNC(F)        DEBUGOUT(F);
#if DBG
	#define DEBUGOUT(S)         printf(S "\n")
	#define DEBUGOUT1(S,A)      printf(S "\n",A)
	#define DEBUGOUT2(S,A,B)    printf(S "\n",A,B)
	#define DEBUGOUT3(S,A,B,C)  printf(S "\n",A,B,C)
	#define DEBUGOUT4(S,A,B,C,D)  printf(S "\n",A,B,C,D)
	#define DEBUGOUT5(S,A,B,C,D,E)  printf(S "\n",A,B,C,D,E)
	#define DEBUGOUT6(S,A,B,C,D,E,F)  printf(S "\n",A,B,C,D,E,F)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)  printf(S "\n",A,B,C,D,E,F,G)
	#define ERROR_REPORT1 ERROR_REPORT
	#define ERROR_REPORT2 ERROR_REPORT
	#define ERROR_REPORT3 ERROR_REPORT
	#define ERROR_REPORT(level, format, arg...) do { \
		switch (level) { \
		case IXGBE_ERROR_SOFTWARE: \
		case IXGBE_ERROR_CAUTION: \
		case IXGBE_ERROR_POLLING: \
		case IXGBE_ERROR_INVALID_STATE: \
		case IXGBE_ERROR_UNSUPPORTED: \
		case IXGBE_ERROR_ARGUMENT: \
			device_printf(ixgbe_dev_from_hw(hw), format, ## arg); \
			break; \
		default: \
			break; \
		} \
	} while (0)
#else
	#define DEBUGOUT(S)
	#define DEBUGOUT1(S,A)
	#define DEBUGOUT2(S,A,B)
	#define DEBUGOUT3(S,A,B,C)
	#define DEBUGOUT4(S,A,B,C,D)
	#define DEBUGOUT5(S,A,B,C,D,E)
	#define DEBUGOUT6(S,A,B,C,D,E,F)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)

	#define ERROR_REPORT1(S,A)
	#define ERROR_REPORT2(S,A,B)
	#define ERROR_REPORT3(S,A,B,C)
#endif

#define FALSE               0
#define false               0 /* shared code requires this */
#define TRUE                1
#define true                1
#define CMD_MEM_WRT_INVALIDATE          0x0010  /* BIT_4 */
#define PCI_COMMAND_REGISTER            PCIR_COMMAND

/* Shared code dropped this define.. */
#define IXGBE_INTEL_VENDOR_ID		0x8086

/* Bunch of defines for shared code bogosity */
#define UNREFERENCED_PARAMETER(_p)
#define UNREFERENCED_1PARAMETER(_p)
#define UNREFERENCED_2PARAMETER(_p, _q)
#define UNREFERENCED_3PARAMETER(_p, _q, _r)
#define UNREFERENCED_4PARAMETER(_p, _q, _r, _s)

#define IXGBE_NTOHL(_i)	ntohl(_i)
#define IXGBE_NTOHS(_i)	ntohs(_i)

/* XXX these need to be revisited */
#define IXGBE_CPU_TO_LE16 htole16
#define IXGBE_CPU_TO_LE32 htole32
#define IXGBE_LE32_TO_CPU le32toh
#define IXGBE_LE32_TO_CPUS(x)
#define IXGBE_CPU_TO_BE16 htobe16
#define IXGBE_CPU_TO_BE32 htobe32
#define IXGBE_BE32_TO_CPU be32toh

typedef uint8_t		u8;
typedef int8_t		s8;
typedef uint16_t	u16;
typedef int16_t		s16;
typedef uint32_t	u32;
typedef int32_t		s32;
typedef uint64_t	u64;
#ifndef __bool_true_false_are_defined
typedef boolean_t	bool;
#endif

/* shared code requires this */
#define __le16  u16
#define __le32  u32
#define __le64  u64
#define __be16  u16
#define __be32  u32
#define __be64  u64

#define le16_to_cpu

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

/*
 * Optimized bcopy thanks to Luigi Rizzo's investigative work.  Assumes
 * non-overlapping regions and 32-byte padding on both src and dst.
 */
static __inline int
ixgbe_bcopy(void *restrict _src, void *restrict _dst, int l)
{
	uint64_t *src = _src;
	uint64_t *dst = _dst;

	for (; l > 0; l -= 32) {
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
	}
	return (0);
}

struct ixgbe_osdep
{
	bus_space_tag_t    mem_bus_space_tag;
	bus_space_handle_t mem_bus_space_handle;
};

/* These routines need struct ixgbe_hw declared */
struct ixgbe_hw;

/* These routines are needed by the shared code */
extern u16 ixgbe_read_pci_cfg(struct ixgbe_hw *, u32);
#define IXGBE_READ_PCIE_WORD ixgbe_read_pci_cfg

extern void ixgbe_write_pci_cfg(struct ixgbe_hw *, u32, u16);
#define IXGBE_WRITE_PCIE_WORD ixgbe_write_pci_cfg

#define IXGBE_WRITE_FLUSH(a) IXGBE_READ_REG(a, IXGBE_STATUS)

extern u32 ixgbe_read_reg(struct ixgbe_hw *, u32);
#define IXGBE_READ_REG(a, reg) ixgbe_read_reg(a, reg)

extern void ixgbe_write_reg(struct ixgbe_hw *, u32, u32);
#define IXGBE_WRITE_REG(a, reg, val) ixgbe_write_reg(a, reg, val)

extern u32 ixgbe_read_reg_array(struct ixgbe_hw *, u32, u32);
#define IXGBE_READ_REG_ARRAY(a, reg, offset) \
    ixgbe_read_reg_array(a, reg, offset)

extern void ixgbe_write_reg_array(struct ixgbe_hw *, u32, u32, u32);
#define IXGBE_WRITE_REG_ARRAY(a, reg, offset, val) \
    ixgbe_write_reg_array(a, reg, offset, val)

#endif /* _IXGBE_OSDEP_H_ */
