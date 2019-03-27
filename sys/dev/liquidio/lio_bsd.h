/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#ifndef __LIO_BSD_H__
#define __LIO_BSD_H__

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net/if_types.h>
#include <net/if_vlan_var.h>
#include <net/if_gif.h>

#include <netinet/in.h>
#include <netinet/tcp_lro.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/smp.h>
#include <sys/kthread.h>
#include <sys/firmware.h>

#include <vm/vm_extern.h>
#include <vm/vm_kern.h>

#ifndef PCI_VENDOR_ID_CAVIUM
#define PCI_VENDOR_ID_CAVIUM	0x177D
#endif

#define BIT(nr)		(1UL << (nr))

#define lio_check_timeout(a, b)  ((int)((b) - (a)) < 0)

#define lio_ms_to_ticks(x)				\
	((hz > 1000) ? ((x) * (hz/1000)) : ((x) / (1000/hz)))

#define lio_mdelay(x) do {				\
	if (cold)					\
		DELAY(1000 * (x));			\
	else						\
		pause("Wait", lio_ms_to_ticks(x));	\
} while(0)

#define lio_sleep_timeout(timeout)	lio_mdelay((timeout))

typedef uint32_t __be32;
typedef uint64_t __be64;

#define lio_dev_info(oct, format, args...)		\
	device_printf(oct->device, "Info: " format, ##args)
#define lio_dev_warn(oct, format, args...)		\
	device_printf(oct->device, "Warn: " format, ##args)
#define lio_dev_err(oct, format, args...)		\
	device_printf(oct->device, "Error: " format, ##args)

#ifdef LIO_DEBUG
#define lio_dev_dbg(oct, format, args...)		\
	device_printf(oct->device, "Debug: " format, ##args)
#else
#define lio_dev_dbg(oct, format, args...)	{do { } while (0); }
#endif

struct lio_stailq_node {
	STAILQ_ENTRY (lio_stailq_node) entries;
};
STAILQ_HEAD (lio_stailq_head, lio_stailq_node);

static inline struct lio_stailq_node *
lio_delete_first_node(struct lio_stailq_head *root)
{
	struct lio_stailq_node *node;

	if (STAILQ_EMPTY(root))
		node = NULL;
	else
		node = STAILQ_FIRST(root);

	if (node != NULL)
		STAILQ_REMOVE_HEAD(root, entries);

	return (node);
}

#endif	/* __LIO_BSD_H__ */
