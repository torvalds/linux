/**************************************************************************

Copyright (c) 2001-2005, Intel Corporation
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

***************************************************************************/

/* $OpenBSD: if_ixgb_osdep.h,v 1.5 2024/10/22 21:50:02 jsg Exp $ */

#ifndef _IXGB_OPENBSD_OS_H_
#define _IXGB_OPENBSD_OS_H_

#define ASSERT(x)	if(!(x)) panic("IXGB: x")

#define usec_delay(x)	DELAY(x)
#define msec_delay(x)	DELAY(1000*(x))

#define DBG 0 
#define MSGOUT(S, A, B)	printf(S "\n", A, B)
#define DEBUGFUNC(F)	DEBUGOUT(F);
#if DBG
	#define DEBUGOUT(S)			printf(S "\n")
	#define DEBUGOUT1(S,A)			printf(S "\n",A)
	#define DEBUGOUT2(S,A,B)		printf(S "\n",A,B)
	#define DEBUGOUT3(S,A,B,C)		printf(S "\n",A,B,C)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)	printf(S "\n",A,B,C,D,E,F,G)
#else
	#define DEBUGOUT(S)
	#define DEBUGOUT1(S,A)
	#define DEBUGOUT2(S,A,B)
	#define DEBUGOUT3(S,A,B,C)
	#define DEBUGOUT7(S,A,B,C,D,E,F,G)
#endif

#define CMD_MEM_WRT_INVALIDATE		0x0010	/* BIT_4 */

#define le16_to_cpu		letoh16

struct ixgb_osdep {
	bus_space_tag_t		mem_bus_space_tag;
	bus_space_handle_t	mem_bus_space_handle;
	struct device		*dev;

	struct pci_attach_args	ixgb_pa;

	bus_size_t		ixgb_memsize;
	bus_addr_t		ixgb_membase;
};

#define IXGB_WRITE_FLUSH(a)	IXGB_READ_REG(a, STATUS)

#define IXGB_READ_REG(a, reg)						\
   bus_space_read_4( ((struct ixgb_osdep *)(a)->back)->mem_bus_space_tag, \
                     ((struct ixgb_osdep *)(a)->back)->mem_bus_space_handle, \
                     IXGB_##reg)

#define IXGB_WRITE_REG(a, reg, value)					\
   bus_space_write_4( ((struct ixgb_osdep *)(a)->back)->mem_bus_space_tag, \
                     ((struct ixgb_osdep *)(a)->back)->mem_bus_space_handle, \
                     IXGB_##reg, value)

#define IXGB_READ_REG_ARRAY(a, reg, offset)				\
   bus_space_read_4( ((struct ixgb_osdep *)(a)->back)->mem_bus_space_tag, \
                     ((struct ixgb_osdep *)(a)->back)->mem_bus_space_handle, \
                     (IXGB_##reg + ((offset) << 2)))

#define IXGB_WRITE_REG_ARRAY(a, reg, offset, value)			\
      bus_space_write_4( ((struct ixgb_osdep *)(a)->back)->mem_bus_space_tag, \
                      ((struct ixgb_osdep *)(a)->back)->mem_bus_space_handle, \
                      (IXGB_##reg + ((offset) << 2)), value)

#ifdef DEBUG
#define IXGB_KASSERT(exp,msg)	do { if (!(exp)) panic msg; } while (0)
#else
#define IXGB_KASSERT(exp,msg)
#endif

#endif  /* _IXGB_OPENBSD_OS_H_ */
