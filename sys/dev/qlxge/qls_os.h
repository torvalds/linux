/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Qlogic Corporation
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
 */
/*
 * File: qls_os.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#ifndef _QLS_OS_H_
#define _QLS_OS_H_

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

#if __FreeBSD_version < 900044
#error FreeBSD Version not supported - use version >= 900044
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
#include <machine/in_cksum.h>

#define QLA_USEC_DELAY(usec)	DELAY(usec)

static __inline int qls_ms_to_hz(int ms)
{
	int qla_hz;

	struct timeval t;

	t.tv_sec = ms / 1000;
	t.tv_usec = (ms % 1000) * 1000;

	qla_hz = tvtohz(&t);

	if (qla_hz < 0)
		qla_hz = 0x7fffffff;
	if (!qla_hz)
		qla_hz = 1;

	return (qla_hz);
}

static __inline int qls_sec_to_hz(int sec)
{
	struct timeval t;

	t.tv_sec = sec;
	t.tv_usec = 0;

	return (tvtohz(&t));
}


#define qla_host_to_le16(x)	htole16(x)
#define qla_host_to_le32(x)	htole32(x)
#define qla_host_to_le64(x)	htole64(x)
#define qla_host_to_be16(x)	htobe16(x)
#define qla_host_to_be32(x)	htobe32(x)
#define qla_host_to_be64(x)	htobe64(x)

#define qla_le16_to_host(x)	le16toh(x)
#define qla_le32_to_host(x)	le32toh(x)
#define qla_le64_to_host(x)	le64toh(x)
#define qla_be16_to_host(x)	be16toh(x)
#define qla_be32_to_host(x)	be32toh(x)
#define qla_be64_to_host(x)	be64toh(x)

MALLOC_DECLARE(M_QLA8XXXBUF);

#define qls_mdelay(fn, msecs)	\
	{\
		if (cold) \
			DELAY((msecs * 1000)); \
		else  \
			pause(fn, qls_ms_to_hz(msecs)); \
	}
	
/*
 * Locks
 */
#define QLA_LOCK(ha, str, no_delay) qls_lock(ha, str, no_delay)
#define QLA_UNLOCK(ha, str) qls_unlock(ha, str)
 
#define QLA_TX_LOCK(ha)		mtx_lock(&ha->tx_lock);
#define QLA_TX_UNLOCK(ha)	mtx_unlock(&ha->tx_lock);

#endif /* #ifndef _QLS_OS_H_ */
