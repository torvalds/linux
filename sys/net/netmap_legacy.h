/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011-2014 Matteo Landi, Luigi Rizzo. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``S IS''AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NET_NETMAP_LEGACY_H_
#define _NET_NETMAP_LEGACY_H_

/*
 * $FreeBSD$
 *
 * ioctl names and related fields
 *
 * NIOCTXSYNC, NIOCRXSYNC synchronize tx or rx queues,
 *	whose identity is set in NIOCREGIF through nr_ringid.
 *	These are non blocking and take no argument.
 *
 * NIOCGINFO takes a struct ifreq, the interface name is the input,
 *	the outputs are number of queues and number of descriptor
 *	for each queue (useful to set number of threads etc.).
 *	The info returned is only advisory and may change before
 *	the interface is bound to a file descriptor.
 *
 * NIOCREGIF takes an interface name within a struct nmre,
 *	and activates netmap mode on the interface (if possible).
 *
 * The argument to NIOCGINFO/NIOCREGIF overlays struct ifreq so we
 * can pass it down to other NIC-related ioctls.
 *
 * The actual argument (struct nmreq) has a number of options to request
 * different functions.
 * The following are used in NIOCREGIF when nr_cmd == 0:
 *
 * nr_name	(in)
 *	The name of the port (em0, valeXXX:YYY, etc.)
 *	limited to IFNAMSIZ for backward compatibility.
 *
 * nr_version	(in/out)
 *	Must match NETMAP_API as used in the kernel, error otherwise.
 *	Always returns the desired value on output.
 *
 * nr_tx_slots, nr_tx_slots, nr_tx_rings, nr_rx_rings (in/out)
 *	On input, non-zero values may be used to reconfigure the port
 *	according to the requested values, but this is not guaranteed.
 *	On output the actual values in use are reported.
 *
 * nr_ringid (in)
 *	Indicates how rings should be bound to the file descriptors.
 *	If nr_flags != 0, then the low bits (in NETMAP_RING_MASK)
 *	are used to indicate the ring number, and nr_flags specifies
 *	the actual rings to bind. NETMAP_NO_TX_POLL is unaffected.
 *
 *	NOTE: THE FOLLOWING (nr_flags == 0) IS DEPRECATED:
 *	If nr_flags == 0, NETMAP_HW_RING and NETMAP_SW_RING control
 *	the binding as follows:
 *	0 (default)			binds all physical rings
 *	NETMAP_HW_RING | ring number	binds a single ring pair
 *	NETMAP_SW_RING			binds only the host tx/rx rings
 *
 *	NETMAP_NO_TX_POLL can be OR-ed to make select()/poll() push
 *		packets on tx rings only if POLLOUT is set.
 *		The default is to push any pending packet.
 *
 *	NETMAP_DO_RX_POLL can be OR-ed to make select()/poll() release
 *		packets on rx rings also when POLLIN is NOT set.
 *		The default is to touch the rx ring only with POLLIN.
 *		Note that this is the opposite of TX because it
 *		reflects the common usage.
 *
 *	NOTE: NETMAP_PRIV_MEM IS DEPRECATED, use nr_arg2 instead.
 *	NETMAP_PRIV_MEM is set on return for ports that do not use
 *		the global memory allocator.
 *		This information is not significant and applications
 *		should look at the region id in nr_arg2
 *
 * nr_flags	is the recommended mode to indicate which rings should
 *		be bound to a file descriptor. Values are NR_REG_*
 *
 * nr_arg1 (in)	Reserved.
 *
 * nr_arg2 (in/out) The identity of the memory region used.
 *		On input, 0 means the system decides autonomously,
 *		other values may try to select a specific region.
 *		On return the actual value is reported.
 *		Region '1' is the global allocator, normally shared
 *		by all interfaces. Other values are private regions.
 *		If two ports the same region zero-copy is possible.
 *
 * nr_arg3 (in/out)	number of extra buffers to be allocated.
 *
 *
 *
 * nr_cmd (in)	if non-zero indicates a special command:
 *	NETMAP_BDG_ATTACH	 and nr_name = vale*:ifname
 *		attaches the NIC to the switch; nr_ringid specifies
 *		which rings to use. Used by vale-ctl -a ...
 *	    nr_arg1 = NETMAP_BDG_HOST also attaches the host port
 *		as in vale-ctl -h ...
 *
 *	NETMAP_BDG_DETACH	and nr_name = vale*:ifname
 *		disconnects a previously attached NIC.
 *		Used by vale-ctl -d ...
 *
 *	NETMAP_BDG_LIST
 *		list the configuration of VALE switches.
 *
 *	NETMAP_BDG_VNET_HDR
 *		Set the virtio-net header length used by the client
 *		of a VALE switch port.
 *
 *	NETMAP_BDG_NEWIF
 *		create a persistent VALE port with name nr_name.
 *		Used by vale-ctl -n ...
 *
 *	NETMAP_BDG_DELIF
 *		delete a persistent VALE port. Used by vale-ctl -d ...
 *
 * nr_arg1, nr_arg2, nr_arg3  (in/out)		command specific
 *
 *
 *
 */


/*
 * struct nmreq overlays a struct ifreq (just the name)
 */
struct nmreq {
	char		nr_name[IFNAMSIZ];
	uint32_t	nr_version;	/* API version */
	uint32_t	nr_offset;	/* nifp offset in the shared region */
	uint32_t	nr_memsize;	/* size of the shared region */
	uint32_t	nr_tx_slots;	/* slots in tx rings */
	uint32_t	nr_rx_slots;	/* slots in rx rings */
	uint16_t	nr_tx_rings;	/* number of tx rings */
	uint16_t	nr_rx_rings;	/* number of rx rings */

	uint16_t	nr_ringid;	/* ring(s) we care about */
#define NETMAP_HW_RING		0x4000	/* single NIC ring pair */
#define NETMAP_SW_RING		0x2000	/* only host ring pair */

#define NETMAP_RING_MASK	0x0fff	/* the ring number */

#define NETMAP_NO_TX_POLL	0x1000	/* no automatic txsync on poll */

#define NETMAP_DO_RX_POLL	0x8000	/* DO automatic rxsync on poll */

	uint16_t	nr_cmd;
#define NETMAP_BDG_ATTACH	1	/* attach the NIC */
#define NETMAP_BDG_DETACH	2	/* detach the NIC */
#define NETMAP_BDG_REGOPS	3	/* register bridge callbacks */
#define NETMAP_BDG_LIST		4	/* get bridge's info */
#define NETMAP_BDG_VNET_HDR     5       /* set the port virtio-net-hdr length */
#define NETMAP_BDG_NEWIF	6	/* create a virtual port */
#define NETMAP_BDG_DELIF	7	/* destroy a virtual port */
#define NETMAP_PT_HOST_CREATE	8	/* create ptnetmap kthreads */
#define NETMAP_PT_HOST_DELETE	9	/* delete ptnetmap kthreads */
#define NETMAP_BDG_POLLING_ON	10	/* delete polling kthread */
#define NETMAP_BDG_POLLING_OFF	11	/* delete polling kthread */
#define NETMAP_VNET_HDR_GET	12      /* get the port virtio-net-hdr length */
	uint16_t	nr_arg1;	/* extra arguments */
#define NETMAP_BDG_HOST		1	/* nr_arg1 value for NETMAP_BDG_ATTACH */

	uint16_t	nr_arg2;	/* id of the memory allocator */
	uint32_t	nr_arg3;	/* req. extra buffers in NIOCREGIF */
	uint32_t	nr_flags;	/* specify NR_REG_* mode and other flags */
#define NR_REG_MASK		0xf /* to extract NR_REG_* mode from nr_flags */
	/* various modes, extends nr_ringid */
	uint32_t	spare2[1];
};

#ifdef _WIN32
/*
 * Windows does not have _IOWR(). _IO(), _IOW() and _IOR() are defined
 * in ws2def.h but not sure if they are in the form we need.
 * We therefore redefine them in a convenient way to use for DeviceIoControl
 * signatures.
 */
#undef _IO	// ws2def.h
#define _WIN_NM_IOCTL_TYPE 40000
#define _IO(_c, _n)	CTL_CODE(_WIN_NM_IOCTL_TYPE, ((_n) + 0x800) , \
		METHOD_BUFFERED, FILE_ANY_ACCESS  )
#define _IO_direct(_c, _n)	CTL_CODE(_WIN_NM_IOCTL_TYPE, ((_n) + 0x800) , \
		METHOD_OUT_DIRECT, FILE_ANY_ACCESS  )

#define _IOWR(_c, _n, _s)	_IO(_c, _n)

/* We havesome internal sysctl in addition to the externally visible ones */
#define NETMAP_MMAP _IO_direct('i', 160)	// note METHOD_OUT_DIRECT
#define NETMAP_POLL _IO('i', 162)

/* and also two setsockopt for sysctl emulation */
#define NETMAP_SETSOCKOPT _IO('i', 140)
#define NETMAP_GETSOCKOPT _IO('i', 141)


/* These linknames are for the Netmap Core Driver */
#define NETMAP_NT_DEVICE_NAME			L"\\Device\\NETMAP"
#define NETMAP_DOS_DEVICE_NAME			L"\\DosDevices\\netmap"

/* Definition of a structure used to pass a virtual address within an IOCTL */
typedef struct _MEMORY_ENTRY {
	PVOID       pUsermodeVirtualAddress;
} MEMORY_ENTRY, *PMEMORY_ENTRY;

typedef struct _POLL_REQUEST_DATA {
	int events;
	int timeout;
	int revents;
} POLL_REQUEST_DATA;
#endif /* _WIN32 */

/*
 * Opaque structure that is passed to an external kernel
 * module via ioctl(fd, NIOCCONFIG, req) for a user-owned
 * bridge port (at this point ephemeral VALE interface).
 */
#define NM_IFRDATA_LEN 256
struct nm_ifreq {
	char nifr_name[IFNAMSIZ];
	char data[NM_IFRDATA_LEN];
};

/*
 * FreeBSD uses the size value embedded in the _IOWR to determine
 * how much to copy in/out. So we need it to match the actual
 * data structure we pass. We put some spares in the structure
 * to ease compatibility with other versions
 */
#define NIOCGINFO	_IOWR('i', 145, struct nmreq) /* return IF info */
#define NIOCREGIF	_IOWR('i', 146, struct nmreq) /* interface register */
#define NIOCCONFIG	_IOWR('i',150, struct nm_ifreq) /* for ext. modules */

#endif /* _NET_NETMAP_LEGACY_H_ */
