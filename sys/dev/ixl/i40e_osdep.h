/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
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

#ifndef _I40E_OSDEP_H_
#define _I40E_OSDEP_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
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

#define i40e_usec_delay(x) DELAY(x)
#define i40e_msec_delay(x) DELAY(1000 * (x))

#define DBG 0 
#define MSGOUT(S, A, B)     printf(S "\n", A, B)
#define DEBUGFUNC(F)        DEBUGOUT(F);
#if DBG
	#define DEBUGOUT(S)         printf(S "\n")
	#define DEBUGOUT1(S,A)      printf(S "\n",A)
	#define DEBUGOUT2(S,A,B)    printf(S "\n",A,B)
	#define DEBUGOUT3(S,A,B,C)  printf(S "\n",A,B,C)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)  printf(S "\n",A,B,C,D,E,F,G)
#else
	#define DEBUGOUT(S)
	#define DEBUGOUT1(S,A)
	#define DEBUGOUT2(S,A,B)
	#define DEBUGOUT3(S,A,B,C)
	#define DEBUGOUT6(S,A,B,C,D,E,F)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)
#endif

/* Remove unused shared code macros */
#define UNREFERENCED_PARAMETER(_p)
#define UNREFERENCED_1PARAMETER(_p)
#define UNREFERENCED_2PARAMETER(_p, _q)
#define UNREFERENCED_3PARAMETER(_p, _q, _r)
#define UNREFERENCED_4PARAMETER(_p, _q, _r, _s)
#define UNREFERENCED_5PARAMETER(_p, _q, _r, _s, _t)

#define STATIC	static
#define INLINE  inline

#define FALSE               0
#define false               0 /* shared code requires this */
#define TRUE                1
#define true                1
#define CMD_MEM_WRT_INVALIDATE          0x0010  /* BIT_4 */
#define PCI_COMMAND_REGISTER            PCIR_COMMAND
#define ARRAY_SIZE(a)		(sizeof(a) / sizeof((a)[0]))

#define i40e_memset(a, b, c, d)  memset((a), (b), (c))
#define i40e_memcpy(a, b, c, d)  memcpy((a), (b), (c))

#define CPU_TO_LE16(o)	htole16(o)
#define CPU_TO_LE32(s)	htole32(s)
#define CPU_TO_LE64(h)	htole64(h)
#define LE16_TO_CPU(a)	le16toh(a)
#define LE32_TO_CPU(c)	le32toh(c)
#define LE64_TO_CPU(k)	le64toh(k)

#define I40E_NTOHS(a)	ntohs(a)
#define I40E_NTOHL(a)	ntohl(a)
#define I40E_HTONS(a)	htons(a)
#define I40E_HTONL(a)	htonl(a)

#define FIELD_SIZEOF(x, y) (sizeof(((x*)0)->y))

typedef uint8_t		u8;
typedef int8_t		s8;
typedef uint16_t	u16;
typedef int16_t		s16;
typedef uint32_t	u32;
typedef int32_t		s32;
typedef uint64_t	u64;

/* long string relief */
typedef enum i40e_status_code i40e_status;

#define __le16  u16
#define __le32  u32
#define __le64  u64
#define __be16  u16
#define __be32  u32
#define __be64  u64

/* SW spinlock */
struct i40e_spinlock {
        struct mtx mutex;
};

#define le16_to_cpu 

#if defined(__amd64__) || defined(i386)
static __inline
void prefetch(void *x)
{
	__asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
}
#else
#define	prefetch(x)
#endif

struct i40e_osdep {
	bus_space_tag_t		mem_bus_space_tag;
	bus_space_handle_t	mem_bus_space_handle;
	bus_size_t		mem_bus_space_size;
	uint32_t		flush_reg;
	int			i2c_intfc_num;
	device_t		dev;
};

struct i40e_dma_mem {
	void			*va;
	u64			pa;
	bus_dma_tag_t		tag;
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	bus_size_t              size;
	int			nseg;
	int                     flags;
};

struct i40e_virt_mem {
	void *va;
	u32 size;
};

struct i40e_hw; /* forward decl */
u16	i40e_read_pci_cfg(struct i40e_hw *, u32);
void	i40e_write_pci_cfg(struct i40e_hw *, u32, u16);

/*
** i40e_debug - OS dependent version of shared code debug printing
*/
enum i40e_debug_mask;
#define i40e_debug(h, m, s, ...)  i40e_debug_shared(h, m, s, ##__VA_ARGS__)
extern void i40e_debug_shared(struct i40e_hw *hw, enum i40e_debug_mask mask,
    char *fmt_str, ...);

/* Non-busy-wait that uses kern_yield() */
void i40e_msec_pause(int);

const char * ixl_vc_opcode_str(uint16_t op);

/*
** This hardware supports either 16 or 32 byte rx descriptors;
** the driver only uses the 32 byte kind.
*/
#define i40e_rx_desc i40e_32byte_rx_desc

static __inline uint32_t
rd32_osdep(struct i40e_osdep *osdep, uint32_t reg)
{

	KASSERT(reg < osdep->mem_bus_space_size,
	    ("ixl: register offset %#jx too large (max is %#jx)",
	    (uintmax_t)reg, (uintmax_t)osdep->mem_bus_space_size));

	return (bus_space_read_4(osdep->mem_bus_space_tag,
	    osdep->mem_bus_space_handle, reg));
}

static __inline void
wr32_osdep(struct i40e_osdep *osdep, uint32_t reg, uint32_t value)
{

	KASSERT(reg < osdep->mem_bus_space_size,
	    ("ixl: register offset %#jx too large (max is %#jx)",
	    (uintmax_t)reg, (uintmax_t)osdep->mem_bus_space_size));

	bus_space_write_4(osdep->mem_bus_space_tag,
	    osdep->mem_bus_space_handle, reg, value);
}

static __inline void
ixl_flush_osdep(struct i40e_osdep *osdep)
{
	rd32_osdep(osdep, osdep->flush_reg);
}

#define rd32(a, reg)		rd32_osdep((a)->back, (reg))
#define wr32(a, reg, value)	wr32_osdep((a)->back, (reg), (value))

#define rd64(a, reg) (\
   bus_space_read_8( ((struct i40e_osdep *)(a)->back)->mem_bus_space_tag, \
                     ((struct i40e_osdep *)(a)->back)->mem_bus_space_handle, \
                     reg))

#define wr64(a, reg, value) (\
   bus_space_write_8( ((struct i40e_osdep *)(a)->back)->mem_bus_space_tag, \
                     ((struct i40e_osdep *)(a)->back)->mem_bus_space_handle, \
                     reg, value))

#define ixl_flush(a)		ixl_flush_osdep((a)->back)

enum i40e_status_code i40e_read_nvm_word_srctl(struct i40e_hw *hw, u16 offset,
					       u16 *data);

#endif /* _I40E_OSDEP_H_ */
