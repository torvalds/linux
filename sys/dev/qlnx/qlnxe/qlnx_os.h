/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

/*
 * File: qlnx_os.h
 * Author : David C Somayajulu, Cavium, Inc., San Jose, CA 95131.
 */

#ifndef _QLNX_OS_H_
#define _QLNX_OS_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/types.h>
#include <machine/atomic.h>
#include <machine/_inttypes.h>
#include <sys/conf.h>

#if __FreeBSD_version < 1000000
#error FreeBSD Version not supported - use version >= 1000000
#endif

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/in_var.h>
#include <netinet/tcp_lro.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/taskqueue.h>
#include <sys/pcpu.h>

#include <sys/unistd.h>
#include <sys/kthread.h>
#include <sys/libkern.h>
#include <sys/smp.h>
#include <sys/sched.h>

#ifdef CONFIG_ECORE_SRIOV
#include <sys/nv.h>
#include <sys/iov_schema.h>
#include <dev/pci/pci_iov.h>
#endif /* #ifdef CONFIG_ECORE_SRIOV */

static __inline int qlnx_ms_to_hz(int ms)
{
	int qlnx_hz;

	struct timeval t;

	t.tv_sec = ms / 1000;
	t.tv_usec = (ms % 1000) * 1000;

	qlnx_hz = tvtohz(&t);

	if (qlnx_hz < 0)
		qlnx_hz = 0x7fffffff;
	if (!qlnx_hz)
		qlnx_hz = 1;

	return (qlnx_hz);
}

static __inline int qlnx_sec_to_hz(int sec)
{
	struct timeval t;

	t.tv_sec = sec;
	t.tv_usec = 0;

	return (tvtohz(&t));
}

MALLOC_DECLARE(M_QLNXBUF);

#define qlnx_mdelay(fn, msecs)	\
	{\
		if (cold) \
			DELAY((msecs * 1000)); \
		else  \
			pause(fn, qlnx_ms_to_hz(msecs)); \
	}
	
/*
 * Locks
 */
#define QLNX_LOCK(ha)		mtx_lock(&ha->hw_lock)
#define QLNX_UNLOCK(ha)		mtx_unlock(&ha->hw_lock)

/*
 * structure encapsulating a DMA buffer
 */
struct qlnx_dma {
        bus_size_t              alignment;
        uint32_t                size;
        void                    *dma_b;
        bus_addr_t              dma_addr;
        bus_dmamap_t            dma_map;
        bus_dma_tag_t           dma_tag;
};
typedef struct qlnx_dma qlnx_dma_t;


#endif /* #ifndef _QLNX_OS_H_ */
