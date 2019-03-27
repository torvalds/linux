/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011-2014 Matteo Landi
 * Copyright (C) 2011-2016 Luigi Rizzo
 * Copyright (C) 2011-2016 Giuseppe Lettieri
 * Copyright (C) 2011-2016 Vincenzo Maffione
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/*
 * $FreeBSD$
 *
 * This module supports memory mapped access to network devices,
 * see netmap(4).
 *
 * The module uses a large, memory pool allocated by the kernel
 * and accessible as mmapped memory by multiple userspace threads/processes.
 * The memory pool contains packet buffers and "netmap rings",
 * i.e. user-accessible copies of the interface's queues.
 *
 * Access to the network card works like this:
 * 1. a process/thread issues one or more open() on /dev/netmap, to create
 *    select()able file descriptor on which events are reported.
 * 2. on each descriptor, the process issues an ioctl() to identify
 *    the interface that should report events to the file descriptor.
 * 3. on each descriptor, the process issues an mmap() request to
 *    map the shared memory region within the process' address space.
 *    The list of interesting queues is indicated by a location in
 *    the shared memory region.
 * 4. using the functions in the netmap(4) userspace API, a process
 *    can look up the occupation state of a queue, access memory buffers,
 *    and retrieve received packets or enqueue packets to transmit.
 * 5. using some ioctl()s the process can synchronize the userspace view
 *    of the queue with the actual status in the kernel. This includes both
 *    receiving the notification of new packets, and transmitting new
 *    packets on the output interface.
 * 6. select() or poll() can be used to wait for events on individual
 *    transmit or receive queues (or all queues for a given interface).
 *

		SYNCHRONIZATION (USER)

The netmap rings and data structures may be shared among multiple
user threads or even independent processes.
Any synchronization among those threads/processes is delegated
to the threads themselves. Only one thread at a time can be in
a system call on the same netmap ring. The OS does not enforce
this and only guarantees against system crashes in case of
invalid usage.

		LOCKING (INTERNAL)

Within the kernel, access to the netmap rings is protected as follows:

- a spinlock on each ring, to handle producer/consumer races on
  RX rings attached to the host stack (against multiple host
  threads writing from the host stack to the same ring),
  and on 'destination' rings attached to a VALE switch
  (i.e. RX rings in VALE ports, and TX rings in NIC/host ports)
  protecting multiple active senders for the same destination)

- an atomic variable to guarantee that there is at most one
  instance of *_*xsync() on the ring at any time.
  For rings connected to user file
  descriptors, an atomic_test_and_set() protects this, and the
  lock on the ring is not actually used.
  For NIC RX rings connected to a VALE switch, an atomic_test_and_set()
  is also used to prevent multiple executions (the driver might indeed
  already guarantee this).
  For NIC TX rings connected to a VALE switch, the lock arbitrates
  access to the queue (both when allocating buffers and when pushing
  them out).

- *xsync() should be protected against initializations of the card.
  On FreeBSD most devices have the reset routine protected by
  a RING lock (ixgbe, igb, em) or core lock (re). lem is missing
  the RING protection on rx_reset(), this should be added.

  On linux there is an external lock on the tx path, which probably
  also arbitrates access to the reset routine. XXX to be revised

- a per-interface core_lock protecting access from the host stack
  while interfaces may be detached from netmap mode.
  XXX there should be no need for this lock if we detach the interfaces
  only while they are down.


--- VALE SWITCH ---

NMG_LOCK() serializes all modifications to switches and ports.
A switch cannot be deleted until all ports are gone.

For each switch, an SX lock (RWlock on linux) protects
deletion of ports. When configuring or deleting a new port, the
lock is acquired in exclusive mode (after holding NMG_LOCK).
When forwarding, the lock is acquired in shared mode (without NMG_LOCK).
The lock is held throughout the entire forwarding cycle,
during which the thread may incur in a page fault.
Hence it is important that sleepable shared locks are used.

On the rx ring, the per-port lock is grabbed initially to reserve
a number of slot in the ring, then the lock is released,
packets are copied from source to destination, and then
the lock is acquired again and the receive ring is updated.
(A similar thing is done on the tx ring for NIC and host stack
ports attached to the switch)

 */


/* --- internals ----
 *
 * Roadmap to the code that implements the above.
 *
 * > 1. a process/thread issues one or more open() on /dev/netmap, to create
 * >    select()able file descriptor on which events are reported.
 *
 *  	Internally, we allocate a netmap_priv_d structure, that will be
 *  	initialized on ioctl(NIOCREGIF). There is one netmap_priv_d
 *  	structure for each open().
 *
 *      os-specific:
 *  	    FreeBSD: see netmap_open() (netmap_freebsd.c)
 *  	    linux:   see linux_netmap_open() (netmap_linux.c)
 *
 * > 2. on each descriptor, the process issues an ioctl() to identify
 * >    the interface that should report events to the file descriptor.
 *
 * 	Implemented by netmap_ioctl(), NIOCREGIF case, with nmr->nr_cmd==0.
 * 	Most important things happen in netmap_get_na() and
 * 	netmap_do_regif(), called from there. Additional details can be
 * 	found in the comments above those functions.
 *
 * 	In all cases, this action creates/takes-a-reference-to a
 * 	netmap_*_adapter describing the port, and allocates a netmap_if
 * 	and all necessary netmap rings, filling them with netmap buffers.
 *
 *      In this phase, the sync callbacks for each ring are set (these are used
 *      in steps 5 and 6 below).  The callbacks depend on the type of adapter.
 *      The adapter creation/initialization code puts them in the
 * 	netmap_adapter (fields na->nm_txsync and na->nm_rxsync).  Then, they
 * 	are copied from there to the netmap_kring's during netmap_do_regif(), by
 * 	the nm_krings_create() callback.  All the nm_krings_create callbacks
 * 	actually call netmap_krings_create() to perform this and the other
 * 	common stuff. netmap_krings_create() also takes care of the host rings,
 * 	if needed, by setting their sync callbacks appropriately.
 *
 * 	Additional actions depend on the kind of netmap_adapter that has been
 * 	registered:
 *
 * 	- netmap_hw_adapter:  	     [netmap.c]
 * 	     This is a system netdev/ifp with native netmap support.
 * 	     The ifp is detached from the host stack by redirecting:
 * 	       - transmissions (from the network stack) to netmap_transmit()
 * 	       - receive notifications to the nm_notify() callback for
 * 	         this adapter. The callback is normally netmap_notify(), unless
 * 	         the ifp is attached to a bridge using bwrap, in which case it
 * 	         is netmap_bwrap_intr_notify().
 *
 * 	- netmap_generic_adapter:      [netmap_generic.c]
 * 	      A system netdev/ifp without native netmap support.
 *
 * 	(the decision about native/non native support is taken in
 * 	 netmap_get_hw_na(), called by netmap_get_na())
 *
 * 	- netmap_vp_adapter 		[netmap_vale.c]
 * 	      Returned by netmap_get_bdg_na().
 * 	      This is a persistent or ephemeral VALE port. Ephemeral ports
 * 	      are created on the fly if they don't already exist, and are
 * 	      always attached to a bridge.
 * 	      Persistent VALE ports must must be created separately, and i
 * 	      then attached like normal NICs. The NIOCREGIF we are examining
 * 	      will find them only if they had previosly been created and
 * 	      attached (see VALE_CTL below).
 *
 * 	- netmap_pipe_adapter 	      [netmap_pipe.c]
 * 	      Returned by netmap_get_pipe_na().
 * 	      Both pipe ends are created, if they didn't already exist.
 *
 * 	- netmap_monitor_adapter      [netmap_monitor.c]
 * 	      Returned by netmap_get_monitor_na().
 * 	      If successful, the nm_sync callbacks of the monitored adapter
 * 	      will be intercepted by the returned monitor.
 *
 * 	- netmap_bwrap_adapter	      [netmap_vale.c]
 * 	      Cannot be obtained in this way, see VALE_CTL below
 *
 *
 * 	os-specific:
 * 	    linux: we first go through linux_netmap_ioctl() to
 * 	           adapt the FreeBSD interface to the linux one.
 *
 *
 * > 3. on each descriptor, the process issues an mmap() request to
 * >    map the shared memory region within the process' address space.
 * >    The list of interesting queues is indicated by a location in
 * >    the shared memory region.
 *
 *      os-specific:
 *  	    FreeBSD: netmap_mmap_single (netmap_freebsd.c).
 *  	    linux:   linux_netmap_mmap (netmap_linux.c).
 *
 * > 4. using the functions in the netmap(4) userspace API, a process
 * >    can look up the occupation state of a queue, access memory buffers,
 * >    and retrieve received packets or enqueue packets to transmit.
 *
 * 	these actions do not involve the kernel.
 *
 * > 5. using some ioctl()s the process can synchronize the userspace view
 * >    of the queue with the actual status in the kernel. This includes both
 * >    receiving the notification of new packets, and transmitting new
 * >    packets on the output interface.
 *
 * 	These are implemented in netmap_ioctl(), NIOCTXSYNC and NIOCRXSYNC
 * 	cases. They invoke the nm_sync callbacks on the netmap_kring
 * 	structures, as initialized in step 2 and maybe later modified
 * 	by a monitor. Monitors, however, will always call the original
 * 	callback before doing anything else.
 *
 *
 * > 6. select() or poll() can be used to wait for events on individual
 * >    transmit or receive queues (or all queues for a given interface).
 *
 * 	Implemented in netmap_poll(). This will call the same nm_sync()
 * 	callbacks as in step 5 above.
 *
 * 	os-specific:
 * 		linux: we first go through linux_netmap_poll() to adapt
 * 		       the FreeBSD interface to the linux one.
 *
 *
 *  ----  VALE_CTL -----
 *
 *  VALE switches are controlled by issuing a NIOCREGIF with a non-null
 *  nr_cmd in the nmreq structure. These subcommands are handled by
 *  netmap_bdg_ctl() in netmap_vale.c. Persistent VALE ports are created
 *  and destroyed by issuing the NETMAP_BDG_NEWIF and NETMAP_BDG_DELIF
 *  subcommands, respectively.
 *
 *  Any network interface known to the system (including a persistent VALE
 *  port) can be attached to a VALE switch by issuing the
 *  NETMAP_REQ_VALE_ATTACH command. After the attachment, persistent VALE ports
 *  look exactly like ephemeral VALE ports (as created in step 2 above).  The
 *  attachment of other interfaces, instead, requires the creation of a
 *  netmap_bwrap_adapter.  Moreover, the attached interface must be put in
 *  netmap mode. This may require the creation of a netmap_generic_adapter if
 *  we have no native support for the interface, or if generic adapters have
 *  been forced by sysctl.
 *
 *  Both persistent VALE ports and bwraps are handled by netmap_get_bdg_na(),
 *  called by nm_bdg_ctl_attach(), and discriminated by the nm_bdg_attach()
 *  callback.  In the case of the bwrap, the callback creates the
 *  netmap_bwrap_adapter.  The initialization of the bwrap is then
 *  completed by calling netmap_do_regif() on it, in the nm_bdg_ctl()
 *  callback (netmap_bwrap_bdg_ctl in netmap_vale.c).
 *  A generic adapter for the wrapped ifp will be created if needed, when
 *  netmap_get_bdg_na() calls netmap_get_hw_na().
 *
 *
 *  ---- DATAPATHS -----
 *
 *              -= SYSTEM DEVICE WITH NATIVE SUPPORT =-
 *
 *    na == NA(ifp) == netmap_hw_adapter created in DEVICE_netmap_attach()
 *
 *    - tx from netmap userspace:
 *	 concurrently:
 *           1) ioctl(NIOCTXSYNC)/netmap_poll() in process context
 *                kring->nm_sync() == DEVICE_netmap_txsync()
 *           2) device interrupt handler
 *                na->nm_notify()  == netmap_notify()
 *    - rx from netmap userspace:
 *       concurrently:
 *           1) ioctl(NIOCRXSYNC)/netmap_poll() in process context
 *                kring->nm_sync() == DEVICE_netmap_rxsync()
 *           2) device interrupt handler
 *                na->nm_notify()  == netmap_notify()
 *    - rx from host stack
 *       concurrently:
 *           1) host stack
 *                netmap_transmit()
 *                  na->nm_notify  == netmap_notify()
 *           2) ioctl(NIOCRXSYNC)/netmap_poll() in process context
 *                kring->nm_sync() == netmap_rxsync_from_host
 *                  netmap_rxsync_from_host(na, NULL, NULL)
 *    - tx to host stack
 *           ioctl(NIOCTXSYNC)/netmap_poll() in process context
 *             kring->nm_sync() == netmap_txsync_to_host
 *               netmap_txsync_to_host(na)
 *                 nm_os_send_up()
 *                   FreeBSD: na->if_input() == ether_input()
 *                   linux: netif_rx() with NM_MAGIC_PRIORITY_RX
 *
 *
 *               -= SYSTEM DEVICE WITH GENERIC SUPPORT =-
 *
 *    na == NA(ifp) == generic_netmap_adapter created in generic_netmap_attach()
 *
 *    - tx from netmap userspace:
 *       concurrently:
 *           1) ioctl(NIOCTXSYNC)/netmap_poll() in process context
 *               kring->nm_sync() == generic_netmap_txsync()
 *                   nm_os_generic_xmit_frame()
 *                       linux:   dev_queue_xmit() with NM_MAGIC_PRIORITY_TX
 *                           ifp->ndo_start_xmit == generic_ndo_start_xmit()
 *                               gna->save_start_xmit == orig. dev. start_xmit
 *                       FreeBSD: na->if_transmit() == orig. dev if_transmit
 *           2) generic_mbuf_destructor()
 *                   na->nm_notify() == netmap_notify()
 *    - rx from netmap userspace:
 *           1) ioctl(NIOCRXSYNC)/netmap_poll() in process context
 *               kring->nm_sync() == generic_netmap_rxsync()
 *                   mbq_safe_dequeue()
 *           2) device driver
 *               generic_rx_handler()
 *                   mbq_safe_enqueue()
 *                   na->nm_notify() == netmap_notify()
 *    - rx from host stack
 *        FreeBSD: same as native
 *        Linux: same as native except:
 *           1) host stack
 *               dev_queue_xmit() without NM_MAGIC_PRIORITY_TX
 *                   ifp->ndo_start_xmit == generic_ndo_start_xmit()
 *                       netmap_transmit()
 *                           na->nm_notify() == netmap_notify()
 *    - tx to host stack (same as native):
 *
 *
 *                           -= VALE =-
 *
 *   INCOMING:
 *
 *      - VALE ports:
 *          ioctl(NIOCTXSYNC)/netmap_poll() in process context
 *              kring->nm_sync() == netmap_vp_txsync()
 *
 *      - system device with native support:
 *         from cable:
 *             interrupt
 *                na->nm_notify() == netmap_bwrap_intr_notify(ring_nr != host ring)
 *                     kring->nm_sync() == DEVICE_netmap_rxsync()
 *                     netmap_vp_txsync()
 *                     kring->nm_sync() == DEVICE_netmap_rxsync()
 *         from host stack:
 *             netmap_transmit()
 *                na->nm_notify() == netmap_bwrap_intr_notify(ring_nr == host ring)
 *                     kring->nm_sync() == netmap_rxsync_from_host()
 *                     netmap_vp_txsync()
 *
 *      - system device with generic support:
 *         from device driver:
 *            generic_rx_handler()
 *                na->nm_notify() == netmap_bwrap_intr_notify(ring_nr != host ring)
 *                     kring->nm_sync() == generic_netmap_rxsync()
 *                     netmap_vp_txsync()
 *                     kring->nm_sync() == generic_netmap_rxsync()
 *         from host stack:
 *            netmap_transmit()
 *                na->nm_notify() == netmap_bwrap_intr_notify(ring_nr == host ring)
 *                     kring->nm_sync() == netmap_rxsync_from_host()
 *                     netmap_vp_txsync()
 *
 *   (all cases) --> nm_bdg_flush()
 *                      dest_na->nm_notify() == (see below)
 *
 *   OUTGOING:
 *
 *      - VALE ports:
 *         concurrently:
 *             1) ioctl(NIOCRXSYNC)/netmap_poll() in process context
 *                    kring->nm_sync() == netmap_vp_rxsync()
 *             2) from nm_bdg_flush()
 *                    na->nm_notify() == netmap_notify()
 *
 *      - system device with native support:
 *          to cable:
 *             na->nm_notify() == netmap_bwrap_notify()
 *                 netmap_vp_rxsync()
 *                 kring->nm_sync() == DEVICE_netmap_txsync()
 *                 netmap_vp_rxsync()
 *          to host stack:
 *                 netmap_vp_rxsync()
 *                 kring->nm_sync() == netmap_txsync_to_host
 *                 netmap_vp_rxsync_locked()
 *
 *      - system device with generic adapter:
 *          to device driver:
 *             na->nm_notify() == netmap_bwrap_notify()
 *                 netmap_vp_rxsync()
 *                 kring->nm_sync() == generic_netmap_txsync()
 *                 netmap_vp_rxsync()
 *          to host stack:
 *                 netmap_vp_rxsync()
 *                 kring->nm_sync() == netmap_txsync_to_host
 *                 netmap_vp_rxsync()
 *
 */

/*
 * OS-specific code that is used only within this file.
 * Other OS-specific code that must be accessed by drivers
 * is present in netmap_kern.h
 */

#if defined(__FreeBSD__)
#include <sys/cdefs.h> /* prerequisite */
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>	/* defines used in kernel.h */
#include <sys/kernel.h>	/* types used in module initialization */
#include <sys/conf.h>	/* cdevsw struct, UID, GID */
#include <sys/filio.h>	/* FIONBIO */
#include <sys/sockio.h>
#include <sys/socketvar.h>	/* struct socket */
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/rwlock.h>
#include <sys/socket.h> /* sockaddrs */
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/jail.h>
#include <net/vnet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/bpf.h>		/* BIOCIMMEDIATE */
#include <machine/bus.h>	/* bus_dmamap_* */
#include <sys/endian.h>
#include <sys/refcount.h>
#include <net/ethernet.h>	/* ETHER_BPF_MTAP */


#elif defined(linux)

#include "bsd_glue.h"

#elif defined(__APPLE__)

#warning OSX support is only partial
#include "osx_glue.h"

#elif defined (_WIN32)

#include "win_glue.h"

#else

#error	Unsupported platform

#endif /* unsupported */

/*
 * common headers
 */
#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <dev/netmap/netmap_mem2.h>


/* user-controlled variables */
int netmap_verbose;
#ifdef CONFIG_NETMAP_DEBUG
int netmap_debug;
#endif /* CONFIG_NETMAP_DEBUG */

static int netmap_no_timestamp; /* don't timestamp on rxsync */
int netmap_no_pendintr = 1;
int netmap_txsync_retry = 2;
static int netmap_fwd = 0;	/* force transparent forwarding */

/*
 * netmap_admode selects the netmap mode to use.
 * Invalid values are reset to NETMAP_ADMODE_BEST
 */
enum {	NETMAP_ADMODE_BEST = 0,	/* use native, fallback to generic */
	NETMAP_ADMODE_NATIVE,	/* either native or none */
	NETMAP_ADMODE_GENERIC,	/* force generic */
	NETMAP_ADMODE_LAST };
static int netmap_admode = NETMAP_ADMODE_BEST;

/* netmap_generic_mit controls mitigation of RX notifications for
 * the generic netmap adapter. The value is a time interval in
 * nanoseconds. */
int netmap_generic_mit = 100*1000;

/* We use by default netmap-aware qdiscs with generic netmap adapters,
 * even if there can be a little performance hit with hardware NICs.
 * However, using the qdisc is the safer approach, for two reasons:
 * 1) it prevents non-fifo qdiscs to break the TX notification
 *    scheme, which is based on mbuf destructors when txqdisc is
 *    not used.
 * 2) it makes it possible to transmit over software devices that
 *    change skb->dev, like bridge, veth, ...
 *
 * Anyway users looking for the best performance should
 * use native adapters.
 */
#ifdef linux
int netmap_generic_txqdisc = 1;
#endif

/* Default number of slots and queues for generic adapters. */
int netmap_generic_ringsize = 1024;
int netmap_generic_rings = 1;

/* Non-zero to enable checksum offloading in NIC drivers */
int netmap_generic_hwcsum = 0;

/* Non-zero if ptnet devices are allowed to use virtio-net headers. */
int ptnet_vnet_hdr = 1;

/*
 * SYSCTL calls are grouped between SYSBEGIN and SYSEND to be emulated
 * in some other operating systems
 */
SYSBEGIN(main_init);

SYSCTL_DECL(_dev_netmap);
SYSCTL_NODE(_dev, OID_AUTO, netmap, CTLFLAG_RW, 0, "Netmap args");
SYSCTL_INT(_dev_netmap, OID_AUTO, verbose,
		CTLFLAG_RW, &netmap_verbose, 0, "Verbose mode");
#ifdef CONFIG_NETMAP_DEBUG
SYSCTL_INT(_dev_netmap, OID_AUTO, debug,
		CTLFLAG_RW, &netmap_debug, 0, "Debug messages");
#endif /* CONFIG_NETMAP_DEBUG */
SYSCTL_INT(_dev_netmap, OID_AUTO, no_timestamp,
		CTLFLAG_RW, &netmap_no_timestamp, 0, "no_timestamp");
SYSCTL_INT(_dev_netmap, OID_AUTO, no_pendintr, CTLFLAG_RW, &netmap_no_pendintr,
		0, "Always look for new received packets.");
SYSCTL_INT(_dev_netmap, OID_AUTO, txsync_retry, CTLFLAG_RW,
		&netmap_txsync_retry, 0, "Number of txsync loops in bridge's flush.");

SYSCTL_INT(_dev_netmap, OID_AUTO, fwd, CTLFLAG_RW, &netmap_fwd, 0,
		"Force NR_FORWARD mode");
SYSCTL_INT(_dev_netmap, OID_AUTO, admode, CTLFLAG_RW, &netmap_admode, 0,
		"Adapter mode. 0 selects the best option available,"
		"1 forces native adapter, 2 forces emulated adapter");
SYSCTL_INT(_dev_netmap, OID_AUTO, generic_hwcsum, CTLFLAG_RW, &netmap_generic_hwcsum,
		0, "Hardware checksums. 0 to disable checksum generation by the NIC (default),"
		"1 to enable checksum generation by the NIC");
SYSCTL_INT(_dev_netmap, OID_AUTO, generic_mit, CTLFLAG_RW, &netmap_generic_mit,
		0, "RX notification interval in nanoseconds");
SYSCTL_INT(_dev_netmap, OID_AUTO, generic_ringsize, CTLFLAG_RW,
		&netmap_generic_ringsize, 0,
		"Number of per-ring slots for emulated netmap mode");
SYSCTL_INT(_dev_netmap, OID_AUTO, generic_rings, CTLFLAG_RW,
		&netmap_generic_rings, 0,
		"Number of TX/RX queues for emulated netmap adapters");
#ifdef linux
SYSCTL_INT(_dev_netmap, OID_AUTO, generic_txqdisc, CTLFLAG_RW,
		&netmap_generic_txqdisc, 0, "Use qdisc for generic adapters");
#endif
SYSCTL_INT(_dev_netmap, OID_AUTO, ptnet_vnet_hdr, CTLFLAG_RW, &ptnet_vnet_hdr,
		0, "Allow ptnet devices to use virtio-net headers");

SYSEND;

NMG_LOCK_T	netmap_global_lock;

/*
 * mark the ring as stopped, and run through the locks
 * to make sure other users get to see it.
 * stopped must be either NR_KR_STOPPED (for unbounded stop)
 * of NR_KR_LOCKED (brief stop for mutual exclusion purposes)
 */
static void
netmap_disable_ring(struct netmap_kring *kr, int stopped)
{
	nm_kr_stop(kr, stopped);
	// XXX check if nm_kr_stop is sufficient
	mtx_lock(&kr->q_lock);
	mtx_unlock(&kr->q_lock);
	nm_kr_put(kr);
}

/* stop or enable a single ring */
void
netmap_set_ring(struct netmap_adapter *na, u_int ring_id, enum txrx t, int stopped)
{
	if (stopped)
		netmap_disable_ring(NMR(na, t)[ring_id], stopped);
	else
		NMR(na, t)[ring_id]->nkr_stopped = 0;
}


/* stop or enable all the rings of na */
void
netmap_set_all_rings(struct netmap_adapter *na, int stopped)
{
	int i;
	enum txrx t;

	if (!nm_netmap_on(na))
		return;

	for_rx_tx(t) {
		for (i = 0; i < netmap_real_rings(na, t); i++) {
			netmap_set_ring(na, i, t, stopped);
		}
	}
}

/*
 * Convenience function used in drivers.  Waits for current txsync()s/rxsync()s
 * to finish and prevents any new one from starting.  Call this before turning
 * netmap mode off, or before removing the hardware rings (e.g., on module
 * onload).
 */
void
netmap_disable_all_rings(struct ifnet *ifp)
{
	if (NM_NA_VALID(ifp)) {
		netmap_set_all_rings(NA(ifp), NM_KR_STOPPED);
	}
}

/*
 * Convenience function used in drivers.  Re-enables rxsync and txsync on the
 * adapter's rings In linux drivers, this should be placed near each
 * napi_enable().
 */
void
netmap_enable_all_rings(struct ifnet *ifp)
{
	if (NM_NA_VALID(ifp)) {
		netmap_set_all_rings(NA(ifp), 0 /* enabled */);
	}
}

void
netmap_make_zombie(struct ifnet *ifp)
{
	if (NM_NA_VALID(ifp)) {
		struct netmap_adapter *na = NA(ifp);
		netmap_set_all_rings(na, NM_KR_LOCKED);
		na->na_flags |= NAF_ZOMBIE;
		netmap_set_all_rings(na, 0);
	}
}

void
netmap_undo_zombie(struct ifnet *ifp)
{
	if (NM_NA_VALID(ifp)) {
		struct netmap_adapter *na = NA(ifp);
		if (na->na_flags & NAF_ZOMBIE) {
			netmap_set_all_rings(na, NM_KR_LOCKED);
			na->na_flags &= ~NAF_ZOMBIE;
			netmap_set_all_rings(na, 0);
		}
	}
}

/*
 * generic bound_checking function
 */
u_int
nm_bound_var(u_int *v, u_int dflt, u_int lo, u_int hi, const char *msg)
{
	u_int oldv = *v;
	const char *op = NULL;

	if (dflt < lo)
		dflt = lo;
	if (dflt > hi)
		dflt = hi;
	if (oldv < lo) {
		*v = dflt;
		op = "Bump";
	} else if (oldv > hi) {
		*v = hi;
		op = "Clamp";
	}
	if (op && msg)
		nm_prinf("%s %s to %d (was %d)", op, msg, *v, oldv);
	return *v;
}


/*
 * packet-dump function, user-supplied or static buffer.
 * The destination buffer must be at least 30+4*len
 */
const char *
nm_dump_buf(char *p, int len, int lim, char *dst)
{
	static char _dst[8192];
	int i, j, i0;
	static char hex[] ="0123456789abcdef";
	char *o;	/* output position */

#define P_HI(x)	hex[((x) & 0xf0)>>4]
#define P_LO(x)	hex[((x) & 0xf)]
#define P_C(x)	((x) >= 0x20 && (x) <= 0x7e ? (x) : '.')
	if (!dst)
		dst = _dst;
	if (lim <= 0 || lim > len)
		lim = len;
	o = dst;
	sprintf(o, "buf 0x%p len %d lim %d\n", p, len, lim);
	o += strlen(o);
	/* hexdump routine */
	for (i = 0; i < lim; ) {
		sprintf(o, "%5d: ", i);
		o += strlen(o);
		memset(o, ' ', 48);
		i0 = i;
		for (j=0; j < 16 && i < lim; i++, j++) {
			o[j*3] = P_HI(p[i]);
			o[j*3+1] = P_LO(p[i]);
		}
		i = i0;
		for (j=0; j < 16 && i < lim; i++, j++)
			o[j + 48] = P_C(p[i]);
		o[j+48] = '\n';
		o += j+49;
	}
	*o = '\0';
#undef P_HI
#undef P_LO
#undef P_C
	return dst;
}


/*
 * Fetch configuration from the device, to cope with dynamic
 * reconfigurations after loading the module.
 */
/* call with NMG_LOCK held */
int
netmap_update_config(struct netmap_adapter *na)
{
	struct nm_config_info info;

	bzero(&info, sizeof(info));
	if (na->nm_config == NULL ||
	    na->nm_config(na, &info)) {
		/* take whatever we had at init time */
		info.num_tx_rings = na->num_tx_rings;
		info.num_tx_descs = na->num_tx_desc;
		info.num_rx_rings = na->num_rx_rings;
		info.num_rx_descs = na->num_rx_desc;
		info.rx_buf_maxsize = na->rx_buf_maxsize;
	}

	if (na->num_tx_rings == info.num_tx_rings &&
	    na->num_tx_desc == info.num_tx_descs &&
	    na->num_rx_rings == info.num_rx_rings &&
	    na->num_rx_desc == info.num_rx_descs &&
	    na->rx_buf_maxsize == info.rx_buf_maxsize)
		return 0; /* nothing changed */
	if (na->active_fds == 0) {
		na->num_tx_rings = info.num_tx_rings;
		na->num_tx_desc = info.num_tx_descs;
		na->num_rx_rings = info.num_rx_rings;
		na->num_rx_desc = info.num_rx_descs;
		na->rx_buf_maxsize = info.rx_buf_maxsize;
		if (netmap_verbose)
			nm_prinf("configuration changed for %s: txring %d x %d, "
				"rxring %d x %d, rxbufsz %d",
				na->name, na->num_tx_rings, na->num_tx_desc,
				na->num_rx_rings, na->num_rx_desc, na->rx_buf_maxsize);
		return 0;
	}
	nm_prerr("WARNING: configuration changed for %s while active: "
		"txring %d x %d, rxring %d x %d, rxbufsz %d",
		na->name, info.num_tx_rings, info.num_tx_descs,
		info.num_rx_rings, info.num_rx_descs,
		info.rx_buf_maxsize);
	return 1;
}

/* nm_sync callbacks for the host rings */
static int netmap_txsync_to_host(struct netmap_kring *kring, int flags);
static int netmap_rxsync_from_host(struct netmap_kring *kring, int flags);

/* create the krings array and initialize the fields common to all adapters.
 * The array layout is this:
 *
 *                    +----------+
 * na->tx_rings ----->|          | \
 *                    |          |  } na->num_tx_ring
 *                    |          | /
 *                    +----------+
 *                    |          |    host tx kring
 * na->rx_rings ----> +----------+
 *                    |          | \
 *                    |          |  } na->num_rx_rings
 *                    |          | /
 *                    +----------+
 *                    |          |    host rx kring
 *                    +----------+
 * na->tailroom ----->|          | \
 *                    |          |  } tailroom bytes
 *                    |          | /
 *                    +----------+
 *
 * Note: for compatibility, host krings are created even when not needed.
 * The tailroom space is currently used by vale ports for allocating leases.
 */
/* call with NMG_LOCK held */
int
netmap_krings_create(struct netmap_adapter *na, u_int tailroom)
{
	u_int i, len, ndesc;
	struct netmap_kring *kring;
	u_int n[NR_TXRX];
	enum txrx t;
	int err = 0;

	if (na->tx_rings != NULL) {
		if (netmap_debug & NM_DEBUG_ON)
			nm_prerr("warning: krings were already created");
		return 0;
	}

	/* account for the (possibly fake) host rings */
	n[NR_TX] = netmap_all_rings(na, NR_TX);
	n[NR_RX] = netmap_all_rings(na, NR_RX);

	len = (n[NR_TX] + n[NR_RX]) *
		(sizeof(struct netmap_kring) + sizeof(struct netmap_kring *))
		+ tailroom;

	na->tx_rings = nm_os_malloc((size_t)len);
	if (na->tx_rings == NULL) {
		nm_prerr("Cannot allocate krings");
		return ENOMEM;
	}
	na->rx_rings = na->tx_rings + n[NR_TX];
	na->tailroom = na->rx_rings + n[NR_RX];

	/* link the krings in the krings array */
	kring = (struct netmap_kring *)((char *)na->tailroom + tailroom);
	for (i = 0; i < n[NR_TX] + n[NR_RX]; i++) {
		na->tx_rings[i] = kring;
		kring++;
	}

	/*
	 * All fields in krings are 0 except the one initialized below.
	 * but better be explicit on important kring fields.
	 */
	for_rx_tx(t) {
		ndesc = nma_get_ndesc(na, t);
		for (i = 0; i < n[t]; i++) {
			kring = NMR(na, t)[i];
			bzero(kring, sizeof(*kring));
			kring->notify_na = na;
			kring->ring_id = i;
			kring->tx = t;
			kring->nkr_num_slots = ndesc;
			kring->nr_mode = NKR_NETMAP_OFF;
			kring->nr_pending_mode = NKR_NETMAP_OFF;
			if (i < nma_get_nrings(na, t)) {
				kring->nm_sync = (t == NR_TX ? na->nm_txsync : na->nm_rxsync);
			} else {
				if (!(na->na_flags & NAF_HOST_RINGS))
					kring->nr_kflags |= NKR_FAKERING;
				kring->nm_sync = (t == NR_TX ?
						netmap_txsync_to_host:
						netmap_rxsync_from_host);
			}
			kring->nm_notify = na->nm_notify;
			kring->rhead = kring->rcur = kring->nr_hwcur = 0;
			/*
			 * IMPORTANT: Always keep one slot empty.
			 */
			kring->rtail = kring->nr_hwtail = (t == NR_TX ? ndesc - 1 : 0);
			snprintf(kring->name, sizeof(kring->name) - 1, "%s %s%d", na->name,
					nm_txrx2str(t), i);
			nm_prdis("ktx %s h %d c %d t %d",
				kring->name, kring->rhead, kring->rcur, kring->rtail);
			err = nm_os_selinfo_init(&kring->si, kring->name);
			if (err) {
				netmap_krings_delete(na);
				return err;
			}
			mtx_init(&kring->q_lock, (t == NR_TX ? "nm_txq_lock" : "nm_rxq_lock"), NULL, MTX_DEF);
			kring->na = na;	/* setting this field marks the mutex as initialized */
		}
		err = nm_os_selinfo_init(&na->si[t], na->name);
		if (err) {
			netmap_krings_delete(na);
			return err;
		}
	}

	return 0;
}


/* undo the actions performed by netmap_krings_create */
/* call with NMG_LOCK held */
void
netmap_krings_delete(struct netmap_adapter *na)
{
	struct netmap_kring **kring = na->tx_rings;
	enum txrx t;

	if (na->tx_rings == NULL) {
		if (netmap_debug & NM_DEBUG_ON)
			nm_prerr("warning: krings were already deleted");
		return;
	}

	for_rx_tx(t)
		nm_os_selinfo_uninit(&na->si[t]);

	/* we rely on the krings layout described above */
	for ( ; kring != na->tailroom; kring++) {
		if ((*kring)->na != NULL)
			mtx_destroy(&(*kring)->q_lock);
		nm_os_selinfo_uninit(&(*kring)->si);
	}
	nm_os_free(na->tx_rings);
	na->tx_rings = na->rx_rings = na->tailroom = NULL;
}


/*
 * Destructor for NIC ports. They also have an mbuf queue
 * on the rings connected to the host so we need to purge
 * them first.
 */
/* call with NMG_LOCK held */
void
netmap_hw_krings_delete(struct netmap_adapter *na)
{
	u_int lim = netmap_real_rings(na, NR_RX), i;

	for (i = nma_get_nrings(na, NR_RX); i < lim; i++) {
		struct mbq *q = &NMR(na, NR_RX)[i]->rx_queue;
		nm_prdis("destroy sw mbq with len %d", mbq_len(q));
		mbq_purge(q);
		mbq_safe_fini(q);
	}
	netmap_krings_delete(na);
}

static void
netmap_mem_drop(struct netmap_adapter *na)
{
	int last = netmap_mem_deref(na->nm_mem, na);
	/* if the native allocator had been overrided on regif,
	 * restore it now and drop the temporary one
	 */
	if (last && na->nm_mem_prev) {
		netmap_mem_put(na->nm_mem);
		na->nm_mem = na->nm_mem_prev;
		na->nm_mem_prev = NULL;
	}
}

/*
 * Undo everything that was done in netmap_do_regif(). In particular,
 * call nm_register(ifp,0) to stop netmap mode on the interface and
 * revert to normal operation.
 */
/* call with NMG_LOCK held */
static void netmap_unset_ringid(struct netmap_priv_d *);
static void netmap_krings_put(struct netmap_priv_d *);
void
netmap_do_unregif(struct netmap_priv_d *priv)
{
	struct netmap_adapter *na = priv->np_na;

	NMG_LOCK_ASSERT();
	na->active_fds--;
	/* unset nr_pending_mode and possibly release exclusive mode */
	netmap_krings_put(priv);

#ifdef	WITH_MONITOR
	/* XXX check whether we have to do something with monitor
	 * when rings change nr_mode. */
	if (na->active_fds <= 0) {
		/* walk through all the rings and tell any monitor
		 * that the port is going to exit netmap mode
		 */
		netmap_monitor_stop(na);
	}
#endif

	if (na->active_fds <= 0 || nm_kring_pending(priv)) {
		na->nm_register(na, 0);
	}

	/* delete rings and buffers that are no longer needed */
	netmap_mem_rings_delete(na);

	if (na->active_fds <= 0) {	/* last instance */
		/*
		 * (TO CHECK) We enter here
		 * when the last reference to this file descriptor goes
		 * away. This means we cannot have any pending poll()
		 * or interrupt routine operating on the structure.
		 * XXX The file may be closed in a thread while
		 * another thread is using it.
		 * Linux keeps the file opened until the last reference
		 * by any outstanding ioctl/poll or mmap is gone.
		 * FreeBSD does not track mmap()s (but we do) and
		 * wakes up any sleeping poll(). Need to check what
		 * happens if the close() occurs while a concurrent
		 * syscall is running.
		 */
		if (netmap_debug & NM_DEBUG_ON)
			nm_prinf("deleting last instance for %s", na->name);

		if (nm_netmap_on(na)) {
			nm_prerr("BUG: netmap on while going to delete the krings");
		}

		na->nm_krings_delete(na);

		/* restore the default number of host tx and rx rings */
		na->num_host_tx_rings = 1;
		na->num_host_rx_rings = 1;
	}

	/* possibily decrement counter of tx_si/rx_si users */
	netmap_unset_ringid(priv);
	/* delete the nifp */
	netmap_mem_if_delete(na, priv->np_nifp);
	/* drop the allocator */
	netmap_mem_drop(na);
	/* mark the priv as unregistered */
	priv->np_na = NULL;
	priv->np_nifp = NULL;
}

struct netmap_priv_d*
netmap_priv_new(void)
{
	struct netmap_priv_d *priv;

	priv = nm_os_malloc(sizeof(struct netmap_priv_d));
	if (priv == NULL)
		return NULL;
	priv->np_refs = 1;
	nm_os_get_module();
	return priv;
}

/*
 * Destructor of the netmap_priv_d, called when the fd is closed
 * Action: undo all the things done by NIOCREGIF,
 * On FreeBSD we need to track whether there are active mmap()s,
 * and we use np_active_mmaps for that. On linux, the field is always 0.
 * Return: 1 if we can free priv, 0 otherwise.
 *
 */
/* call with NMG_LOCK held */
void
netmap_priv_delete(struct netmap_priv_d *priv)
{
	struct netmap_adapter *na = priv->np_na;

	/* number of active references to this fd */
	if (--priv->np_refs > 0) {
		return;
	}
	nm_os_put_module();
	if (na) {
		netmap_do_unregif(priv);
	}
	netmap_unget_na(na, priv->np_ifp);
	bzero(priv, sizeof(*priv));	/* for safety */
	nm_os_free(priv);
}


/* call with NMG_LOCK *not* held */
void
netmap_dtor(void *data)
{
	struct netmap_priv_d *priv = data;

	NMG_LOCK();
	netmap_priv_delete(priv);
	NMG_UNLOCK();
}


/*
 * Handlers for synchronization of the rings from/to the host stack.
 * These are associated to a network interface and are just another
 * ring pair managed by userspace.
 *
 * Netmap also supports transparent forwarding (NS_FORWARD and NR_FORWARD
 * flags):
 *
 * - Before releasing buffers on hw RX rings, the application can mark
 *   them with the NS_FORWARD flag. During the next RXSYNC or poll(), they
 *   will be forwarded to the host stack, similarly to what happened if
 *   the application moved them to the host TX ring.
 *
 * - Before releasing buffers on the host RX ring, the application can
 *   mark them with the NS_FORWARD flag. During the next RXSYNC or poll(),
 *   they will be forwarded to the hw TX rings, saving the application
 *   from doing the same task in user-space.
 *
 * Transparent fowarding can be enabled per-ring, by setting the NR_FORWARD
 * flag, or globally with the netmap_fwd sysctl.
 *
 * The transfer NIC --> host is relatively easy, just encapsulate
 * into mbufs and we are done. The host --> NIC side is slightly
 * harder because there might not be room in the tx ring so it
 * might take a while before releasing the buffer.
 */


/*
 * Pass a whole queue of mbufs to the host stack as coming from 'dst'
 * We do not need to lock because the queue is private.
 * After this call the queue is empty.
 */
static void
netmap_send_up(struct ifnet *dst, struct mbq *q)
{
	struct mbuf *m;
	struct mbuf *head = NULL, *prev = NULL;

	/* Send packets up, outside the lock; head/prev machinery
	 * is only useful for Windows. */
	while ((m = mbq_dequeue(q)) != NULL) {
		if (netmap_debug & NM_DEBUG_HOST)
			nm_prinf("sending up pkt %p size %d", m, MBUF_LEN(m));
		prev = nm_os_send_up(dst, m, prev);
		if (head == NULL)
			head = prev;
	}
	if (head)
		nm_os_send_up(dst, NULL, head);
	mbq_fini(q);
}


/*
 * Scan the buffers from hwcur to ring->head, and put a copy of those
 * marked NS_FORWARD (or all of them if forced) into a queue of mbufs.
 * Drop remaining packets in the unlikely event
 * of an mbuf shortage.
 */
static void
netmap_grab_packets(struct netmap_kring *kring, struct mbq *q, int force)
{
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	u_int n;
	struct netmap_adapter *na = kring->na;

	for (n = kring->nr_hwcur; n != head; n = nm_next(n, lim)) {
		struct mbuf *m;
		struct netmap_slot *slot = &kring->ring->slot[n];

		if ((slot->flags & NS_FORWARD) == 0 && !force)
			continue;
		if (slot->len < 14 || slot->len > NETMAP_BUF_SIZE(na)) {
			nm_prlim(5, "bad pkt at %d len %d", n, slot->len);
			continue;
		}
		slot->flags &= ~NS_FORWARD; // XXX needed ?
		/* XXX TODO: adapt to the case of a multisegment packet */
		m = m_devget(NMB(na, slot), slot->len, 0, na->ifp, NULL);

		if (m == NULL)
			break;
		mbq_enqueue(q, m);
	}
}

static inline int
_nm_may_forward(struct netmap_kring *kring)
{
	return	((netmap_fwd || kring->ring->flags & NR_FORWARD) &&
		 kring->na->na_flags & NAF_HOST_RINGS &&
		 kring->tx == NR_RX);
}

static inline int
nm_may_forward_up(struct netmap_kring *kring)
{
	return	_nm_may_forward(kring) &&
		 kring->ring_id != kring->na->num_rx_rings;
}

static inline int
nm_may_forward_down(struct netmap_kring *kring, int sync_flags)
{
	return	_nm_may_forward(kring) &&
		 (sync_flags & NAF_CAN_FORWARD_DOWN) &&
		 kring->ring_id == kring->na->num_rx_rings;
}

/*
 * Send to the NIC rings packets marked NS_FORWARD between
 * kring->nr_hwcur and kring->rhead.
 * Called under kring->rx_queue.lock on the sw rx ring.
 *
 * It can only be called if the user opened all the TX hw rings,
 * see NAF_CAN_FORWARD_DOWN flag.
 * We can touch the TX netmap rings (slots, head and cur) since
 * we are in poll/ioctl system call context, and the application
 * is not supposed to touch the ring (using a different thread)
 * during the execution of the system call.
 */
static u_int
netmap_sw_to_nic(struct netmap_adapter *na)
{
	struct netmap_kring *kring = na->rx_rings[na->num_rx_rings];
	struct netmap_slot *rxslot = kring->ring->slot;
	u_int i, rxcur = kring->nr_hwcur;
	u_int const head = kring->rhead;
	u_int const src_lim = kring->nkr_num_slots - 1;
	u_int sent = 0;

	/* scan rings to find space, then fill as much as possible */
	for (i = 0; i < na->num_tx_rings; i++) {
		struct netmap_kring *kdst = na->tx_rings[i];
		struct netmap_ring *rdst = kdst->ring;
		u_int const dst_lim = kdst->nkr_num_slots - 1;

		/* XXX do we trust ring or kring->rcur,rtail ? */
		for (; rxcur != head && !nm_ring_empty(rdst);
		     rxcur = nm_next(rxcur, src_lim) ) {
			struct netmap_slot *src, *dst, tmp;
			u_int dst_head = rdst->head;

			src = &rxslot[rxcur];
			if ((src->flags & NS_FORWARD) == 0 && !netmap_fwd)
				continue;

			sent++;

			dst = &rdst->slot[dst_head];

			tmp = *src;

			src->buf_idx = dst->buf_idx;
			src->flags = NS_BUF_CHANGED;

			dst->buf_idx = tmp.buf_idx;
			dst->len = tmp.len;
			dst->flags = NS_BUF_CHANGED;

			rdst->head = rdst->cur = nm_next(dst_head, dst_lim);
		}
		/* if (sent) XXX txsync ? it would be just an optimization */
	}
	return sent;
}


/*
 * netmap_txsync_to_host() passes packets up. We are called from a
 * system call in user process context, and the only contention
 * can be among multiple user threads erroneously calling
 * this routine concurrently.
 */
static int
netmap_txsync_to_host(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	struct mbq q;

	/* Take packets from hwcur to head and pass them up.
	 * Force hwcur = head since netmap_grab_packets() stops at head
	 */
	mbq_init(&q);
	netmap_grab_packets(kring, &q, 1 /* force */);
	nm_prdis("have %d pkts in queue", mbq_len(&q));
	kring->nr_hwcur = head;
	kring->nr_hwtail = head + lim;
	if (kring->nr_hwtail > lim)
		kring->nr_hwtail -= lim + 1;

	netmap_send_up(na->ifp, &q);
	return 0;
}


/*
 * rxsync backend for packets coming from the host stack.
 * They have been put in kring->rx_queue by netmap_transmit().
 * We protect access to the kring using kring->rx_queue.lock
 *
 * also moves to the nic hw rings any packet the user has marked
 * for transparent-mode forwarding, then sets the NR_FORWARD
 * flag in the kring to let the caller push them out
 */
static int
netmap_rxsync_from_host(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct netmap_ring *ring = kring->ring;
	u_int nm_i, n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	int ret = 0;
	struct mbq *q = &kring->rx_queue, fq;

	mbq_init(&fq); /* fq holds packets to be freed */

	mbq_lock(q);

	/* First part: import newly received packets */
	n = mbq_len(q);
	if (n) { /* grab packets from the queue */
		struct mbuf *m;
		uint32_t stop_i;

		nm_i = kring->nr_hwtail;
		stop_i = nm_prev(kring->nr_hwcur, lim);
		while ( nm_i != stop_i && (m = mbq_dequeue(q)) != NULL ) {
			int len = MBUF_LEN(m);
			struct netmap_slot *slot = &ring->slot[nm_i];

			m_copydata(m, 0, len, NMB(na, slot));
			nm_prdis("nm %d len %d", nm_i, len);
			if (netmap_debug & NM_DEBUG_HOST)
				nm_prinf("%s", nm_dump_buf(NMB(na, slot),len, 128, NULL));

			slot->len = len;
			slot->flags = 0;
			nm_i = nm_next(nm_i, lim);
			mbq_enqueue(&fq, m);
		}
		kring->nr_hwtail = nm_i;
	}

	/*
	 * Second part: skip past packets that userspace has released.
	 */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) { /* something was released */
		if (nm_may_forward_down(kring, flags)) {
			ret = netmap_sw_to_nic(na);
			if (ret > 0) {
				kring->nr_kflags |= NR_FORWARD;
				ret = 0;
			}
		}
		kring->nr_hwcur = head;
	}

	mbq_unlock(q);

	mbq_purge(&fq);
	mbq_fini(&fq);

	return ret;
}


/* Get a netmap adapter for the port.
 *
 * If it is possible to satisfy the request, return 0
 * with *na containing the netmap adapter found.
 * Otherwise return an error code, with *na containing NULL.
 *
 * When the port is attached to a bridge, we always return
 * EBUSY.
 * Otherwise, if the port is already bound to a file descriptor,
 * then we unconditionally return the existing adapter into *na.
 * In all the other cases, we return (into *na) either native,
 * generic or NULL, according to the following table:
 *
 *					native_support
 * active_fds   dev.netmap.admode         YES     NO
 * -------------------------------------------------------
 *    >0              *                 NA(ifp) NA(ifp)
 *
 *     0        NETMAP_ADMODE_BEST      NATIVE  GENERIC
 *     0        NETMAP_ADMODE_NATIVE    NATIVE   NULL
 *     0        NETMAP_ADMODE_GENERIC   GENERIC GENERIC
 *
 */
static void netmap_hw_dtor(struct netmap_adapter *); /* needed by NM_IS_NATIVE() */
int
netmap_get_hw_na(struct ifnet *ifp, struct netmap_mem_d *nmd, struct netmap_adapter **na)
{
	/* generic support */
	int i = netmap_admode;	/* Take a snapshot. */
	struct netmap_adapter *prev_na;
	int error = 0;

	*na = NULL; /* default */

	/* reset in case of invalid value */
	if (i < NETMAP_ADMODE_BEST || i >= NETMAP_ADMODE_LAST)
		i = netmap_admode = NETMAP_ADMODE_BEST;

	if (NM_NA_VALID(ifp)) {
		prev_na = NA(ifp);
		/* If an adapter already exists, return it if
		 * there are active file descriptors or if
		 * netmap is not forced to use generic
		 * adapters.
		 */
		if (NETMAP_OWNED_BY_ANY(prev_na)
			|| i != NETMAP_ADMODE_GENERIC
			|| prev_na->na_flags & NAF_FORCE_NATIVE
#ifdef WITH_PIPES
			/* ugly, but we cannot allow an adapter switch
			 * if some pipe is referring to this one
			 */
			|| prev_na->na_next_pipe > 0
#endif
		) {
			*na = prev_na;
			goto assign_mem;
		}
	}

	/* If there isn't native support and netmap is not allowed
	 * to use generic adapters, we cannot satisfy the request.
	 */
	if (!NM_IS_NATIVE(ifp) && i == NETMAP_ADMODE_NATIVE)
		return EOPNOTSUPP;

	/* Otherwise, create a generic adapter and return it,
	 * saving the previously used netmap adapter, if any.
	 *
	 * Note that here 'prev_na', if not NULL, MUST be a
	 * native adapter, and CANNOT be a generic one. This is
	 * true because generic adapters are created on demand, and
	 * destroyed when not used anymore. Therefore, if the adapter
	 * currently attached to an interface 'ifp' is generic, it
	 * must be that
	 * (NA(ifp)->active_fds > 0 || NETMAP_OWNED_BY_KERN(NA(ifp))).
	 * Consequently, if NA(ifp) is generic, we will enter one of
	 * the branches above. This ensures that we never override
	 * a generic adapter with another generic adapter.
	 */
	error = generic_netmap_attach(ifp);
	if (error)
		return error;

	*na = NA(ifp);

assign_mem:
	if (nmd != NULL && !((*na)->na_flags & NAF_MEM_OWNER) &&
	    (*na)->active_fds == 0 && ((*na)->nm_mem != nmd)) {
		(*na)->nm_mem_prev = (*na)->nm_mem;
		(*na)->nm_mem = netmap_mem_get(nmd);
	}

	return 0;
}

/*
 * MUST BE CALLED UNDER NMG_LOCK()
 *
 * Get a refcounted reference to a netmap adapter attached
 * to the interface specified by req.
 * This is always called in the execution of an ioctl().
 *
 * Return ENXIO if the interface specified by the request does
 * not exist, ENOTSUP if netmap is not supported by the interface,
 * EBUSY if the interface is already attached to a bridge,
 * EINVAL if parameters are invalid, ENOMEM if needed resources
 * could not be allocated.
 * If successful, hold a reference to the netmap adapter.
 *
 * If the interface specified by req is a system one, also keep
 * a reference to it and return a valid *ifp.
 */
int
netmap_get_na(struct nmreq_header *hdr,
	      struct netmap_adapter **na, struct ifnet **ifp,
	      struct netmap_mem_d *nmd, int create)
{
	struct nmreq_register *req = (struct nmreq_register *)(uintptr_t)hdr->nr_body;
	int error = 0;
	struct netmap_adapter *ret = NULL;
	int nmd_ref = 0;

	*na = NULL;     /* default return value */
	*ifp = NULL;

	if (hdr->nr_reqtype != NETMAP_REQ_REGISTER) {
		return EINVAL;
	}

	if (req->nr_mode == NR_REG_PIPE_MASTER ||
			req->nr_mode == NR_REG_PIPE_SLAVE) {
		/* Do not accept deprecated pipe modes. */
		nm_prerr("Deprecated pipe nr_mode, use xx{yy or xx}yy syntax");
		return EINVAL;
	}

	NMG_LOCK_ASSERT();

	/* if the request contain a memid, try to find the
	 * corresponding memory region
	 */
	if (nmd == NULL && req->nr_mem_id) {
		nmd = netmap_mem_find(req->nr_mem_id);
		if (nmd == NULL)
			return EINVAL;
		/* keep the rereference */
		nmd_ref = 1;
	}

	/* We cascade through all possible types of netmap adapter.
	 * All netmap_get_*_na() functions return an error and an na,
	 * with the following combinations:
	 *
	 * error    na
	 *   0	   NULL		type doesn't match
	 *  !0	   NULL		type matches, but na creation/lookup failed
	 *   0	  !NULL		type matches and na created/found
	 *  !0    !NULL		impossible
	 */
	error = netmap_get_null_na(hdr, na, nmd, create);
	if (error || *na != NULL)
		goto out;

	/* try to see if this is a monitor port */
	error = netmap_get_monitor_na(hdr, na, nmd, create);
	if (error || *na != NULL)
		goto out;

	/* try to see if this is a pipe port */
	error = netmap_get_pipe_na(hdr, na, nmd, create);
	if (error || *na != NULL)
		goto out;

	/* try to see if this is a bridge port */
	error = netmap_get_vale_na(hdr, na, nmd, create);
	if (error)
		goto out;

	if (*na != NULL) /* valid match in netmap_get_bdg_na() */
		goto out;

	/*
	 * This must be a hardware na, lookup the name in the system.
	 * Note that by hardware we actually mean "it shows up in ifconfig".
	 * This may still be a tap, a veth/epair, or even a
	 * persistent VALE port.
	 */
	*ifp = ifunit_ref(hdr->nr_name);
	if (*ifp == NULL) {
		error = ENXIO;
		goto out;
	}

	error = netmap_get_hw_na(*ifp, nmd, &ret);
	if (error)
		goto out;

	*na = ret;
	netmap_adapter_get(ret);

	/*
	 * if the adapter supports the host rings and it is not alread open,
	 * try to set the number of host rings as requested by the user
	 */
	if (((*na)->na_flags & NAF_HOST_RINGS) && (*na)->active_fds == 0) {
		if (req->nr_host_tx_rings)
			(*na)->num_host_tx_rings = req->nr_host_tx_rings;
		if (req->nr_host_rx_rings)
			(*na)->num_host_rx_rings = req->nr_host_rx_rings;
	}
	nm_prdis("%s: host tx %d rx %u", (*na)->name, (*na)->num_host_tx_rings,
			(*na)->num_host_rx_rings);

out:
	if (error) {
		if (ret)
			netmap_adapter_put(ret);
		if (*ifp) {
			if_rele(*ifp);
			*ifp = NULL;
		}
	}
	if (nmd_ref)
		netmap_mem_put(nmd);

	return error;
}

/* undo netmap_get_na() */
void
netmap_unget_na(struct netmap_adapter *na, struct ifnet *ifp)
{
	if (ifp)
		if_rele(ifp);
	if (na)
		netmap_adapter_put(na);
}


#define NM_FAIL_ON(t) do {						\
	if (unlikely(t)) {						\
		nm_prlim(5, "%s: fail '" #t "' "				\
			"h %d c %d t %d "				\
			"rh %d rc %d rt %d "				\
			"hc %d ht %d",					\
			kring->name,					\
			head, cur, ring->tail,				\
			kring->rhead, kring->rcur, kring->rtail,	\
			kring->nr_hwcur, kring->nr_hwtail);		\
		return kring->nkr_num_slots;				\
	}								\
} while (0)

/*
 * validate parameters on entry for *_txsync()
 * Returns ring->cur if ok, or something >= kring->nkr_num_slots
 * in case of error.
 *
 * rhead, rcur and rtail=hwtail are stored from previous round.
 * hwcur is the next packet to send to the ring.
 *
 * We want
 *    hwcur <= *rhead <= head <= cur <= tail = *rtail <= hwtail
 *
 * hwcur, rhead, rtail and hwtail are reliable
 */
u_int
nm_txsync_prologue(struct netmap_kring *kring, struct netmap_ring *ring)
{
	u_int head = ring->head; /* read only once */
	u_int cur = ring->cur; /* read only once */
	u_int n = kring->nkr_num_slots;

	nm_prdis(5, "%s kcur %d ktail %d head %d cur %d tail %d",
		kring->name,
		kring->nr_hwcur, kring->nr_hwtail,
		ring->head, ring->cur, ring->tail);
#if 1 /* kernel sanity checks; but we can trust the kring. */
	NM_FAIL_ON(kring->nr_hwcur >= n || kring->rhead >= n ||
	    kring->rtail >= n ||  kring->nr_hwtail >= n);
#endif /* kernel sanity checks */
	/*
	 * user sanity checks. We only use head,
	 * A, B, ... are possible positions for head:
	 *
	 *  0    A  rhead   B  rtail   C  n-1
	 *  0    D  rtail   E  rhead   F  n-1
	 *
	 * B, F, D are valid. A, C, E are wrong
	 */
	if (kring->rtail >= kring->rhead) {
		/* want rhead <= head <= rtail */
		NM_FAIL_ON(head < kring->rhead || head > kring->rtail);
		/* and also head <= cur <= rtail */
		NM_FAIL_ON(cur < head || cur > kring->rtail);
	} else { /* here rtail < rhead */
		/* we need head outside rtail .. rhead */
		NM_FAIL_ON(head > kring->rtail && head < kring->rhead);

		/* two cases now: head <= rtail or head >= rhead  */
		if (head <= kring->rtail) {
			/* want head <= cur <= rtail */
			NM_FAIL_ON(cur < head || cur > kring->rtail);
		} else { /* head >= rhead */
			/* cur must be outside rtail..head */
			NM_FAIL_ON(cur > kring->rtail && cur < head);
		}
	}
	if (ring->tail != kring->rtail) {
		nm_prlim(5, "%s tail overwritten was %d need %d", kring->name,
			ring->tail, kring->rtail);
		ring->tail = kring->rtail;
	}
	kring->rhead = head;
	kring->rcur = cur;
	return head;
}


/*
 * validate parameters on entry for *_rxsync()
 * Returns ring->head if ok, kring->nkr_num_slots on error.
 *
 * For a valid configuration,
 * hwcur <= head <= cur <= tail <= hwtail
 *
 * We only consider head and cur.
 * hwcur and hwtail are reliable.
 *
 */
u_int
nm_rxsync_prologue(struct netmap_kring *kring, struct netmap_ring *ring)
{
	uint32_t const n = kring->nkr_num_slots;
	uint32_t head, cur;

	nm_prdis(5,"%s kc %d kt %d h %d c %d t %d",
		kring->name,
		kring->nr_hwcur, kring->nr_hwtail,
		ring->head, ring->cur, ring->tail);
	/*
	 * Before storing the new values, we should check they do not
	 * move backwards. However:
	 * - head is not an issue because the previous value is hwcur;
	 * - cur could in principle go back, however it does not matter
	 *   because we are processing a brand new rxsync()
	 */
	cur = kring->rcur = ring->cur;	/* read only once */
	head = kring->rhead = ring->head;	/* read only once */
#if 1 /* kernel sanity checks */
	NM_FAIL_ON(kring->nr_hwcur >= n || kring->nr_hwtail >= n);
#endif /* kernel sanity checks */
	/* user sanity checks */
	if (kring->nr_hwtail >= kring->nr_hwcur) {
		/* want hwcur <= rhead <= hwtail */
		NM_FAIL_ON(head < kring->nr_hwcur || head > kring->nr_hwtail);
		/* and also rhead <= rcur <= hwtail */
		NM_FAIL_ON(cur < head || cur > kring->nr_hwtail);
	} else {
		/* we need rhead outside hwtail..hwcur */
		NM_FAIL_ON(head < kring->nr_hwcur && head > kring->nr_hwtail);
		/* two cases now: head <= hwtail or head >= hwcur  */
		if (head <= kring->nr_hwtail) {
			/* want head <= cur <= hwtail */
			NM_FAIL_ON(cur < head || cur > kring->nr_hwtail);
		} else {
			/* cur must be outside hwtail..head */
			NM_FAIL_ON(cur < head && cur > kring->nr_hwtail);
		}
	}
	if (ring->tail != kring->rtail) {
		nm_prlim(5, "%s tail overwritten was %d need %d",
			kring->name,
			ring->tail, kring->rtail);
		ring->tail = kring->rtail;
	}
	return head;
}


/*
 * Error routine called when txsync/rxsync detects an error.
 * Can't do much more than resetting head = cur = hwcur, tail = hwtail
 * Return 1 on reinit.
 *
 * This routine is only called by the upper half of the kernel.
 * It only reads hwcur (which is changed only by the upper half, too)
 * and hwtail (which may be changed by the lower half, but only on
 * a tx ring and only to increase it, so any error will be recovered
 * on the next call). For the above, we don't strictly need to call
 * it under lock.
 */
int
netmap_ring_reinit(struct netmap_kring *kring)
{
	struct netmap_ring *ring = kring->ring;
	u_int i, lim = kring->nkr_num_slots - 1;
	int errors = 0;

	// XXX KASSERT nm_kr_tryget
	nm_prlim(10, "called for %s", kring->name);
	// XXX probably wrong to trust userspace
	kring->rhead = ring->head;
	kring->rcur  = ring->cur;
	kring->rtail = ring->tail;

	if (ring->cur > lim)
		errors++;
	if (ring->head > lim)
		errors++;
	if (ring->tail > lim)
		errors++;
	for (i = 0; i <= lim; i++) {
		u_int idx = ring->slot[i].buf_idx;
		u_int len = ring->slot[i].len;
		if (idx < 2 || idx >= kring->na->na_lut.objtotal) {
			nm_prlim(5, "bad index at slot %d idx %d len %d ", i, idx, len);
			ring->slot[i].buf_idx = 0;
			ring->slot[i].len = 0;
		} else if (len > NETMAP_BUF_SIZE(kring->na)) {
			ring->slot[i].len = 0;
			nm_prlim(5, "bad len at slot %d idx %d len %d", i, idx, len);
		}
	}
	if (errors) {
		nm_prlim(10, "total %d errors", errors);
		nm_prlim(10, "%s reinit, cur %d -> %d tail %d -> %d",
			kring->name,
			ring->cur, kring->nr_hwcur,
			ring->tail, kring->nr_hwtail);
		ring->head = kring->rhead = kring->nr_hwcur;
		ring->cur  = kring->rcur  = kring->nr_hwcur;
		ring->tail = kring->rtail = kring->nr_hwtail;
	}
	return (errors ? 1 : 0);
}

/* interpret the ringid and flags fields of an nmreq, by translating them
 * into a pair of intervals of ring indices:
 *
 * [priv->np_txqfirst, priv->np_txqlast) and
 * [priv->np_rxqfirst, priv->np_rxqlast)
 *
 */
int
netmap_interp_ringid(struct netmap_priv_d *priv, uint32_t nr_mode,
			uint16_t nr_ringid, uint64_t nr_flags)
{
	struct netmap_adapter *na = priv->np_na;
	int excluded_direction[] = { NR_TX_RINGS_ONLY, NR_RX_RINGS_ONLY };
	enum txrx t;
	u_int j;

	for_rx_tx(t) {
		if (nr_flags & excluded_direction[t]) {
			priv->np_qfirst[t] = priv->np_qlast[t] = 0;
			continue;
		}
		switch (nr_mode) {
		case NR_REG_ALL_NIC:
		case NR_REG_NULL:
			priv->np_qfirst[t] = 0;
			priv->np_qlast[t] = nma_get_nrings(na, t);
			nm_prdis("ALL/PIPE: %s %d %d", nm_txrx2str(t),
				priv->np_qfirst[t], priv->np_qlast[t]);
			break;
		case NR_REG_SW:
		case NR_REG_NIC_SW:
			if (!(na->na_flags & NAF_HOST_RINGS)) {
				nm_prerr("host rings not supported");
				return EINVAL;
			}
			priv->np_qfirst[t] = (nr_mode == NR_REG_SW ?
				nma_get_nrings(na, t) : 0);
			priv->np_qlast[t] = netmap_all_rings(na, t);
			nm_prdis("%s: %s %d %d", nr_mode == NR_REG_SW ? "SW" : "NIC+SW",
				nm_txrx2str(t),
				priv->np_qfirst[t], priv->np_qlast[t]);
			break;
		case NR_REG_ONE_NIC:
			if (nr_ringid >= na->num_tx_rings &&
					nr_ringid >= na->num_rx_rings) {
				nm_prerr("invalid ring id %d", nr_ringid);
				return EINVAL;
			}
			/* if not enough rings, use the first one */
			j = nr_ringid;
			if (j >= nma_get_nrings(na, t))
				j = 0;
			priv->np_qfirst[t] = j;
			priv->np_qlast[t] = j + 1;
			nm_prdis("ONE_NIC: %s %d %d", nm_txrx2str(t),
				priv->np_qfirst[t], priv->np_qlast[t]);
			break;
		case NR_REG_ONE_SW:
			if (!(na->na_flags & NAF_HOST_RINGS)) {
				nm_prerr("host rings not supported");
				return EINVAL;
			}
			if (nr_ringid >= na->num_host_tx_rings &&
					nr_ringid >= na->num_host_rx_rings) {
				nm_prerr("invalid ring id %d", nr_ringid);
				return EINVAL;
			}
			/* if not enough rings, use the first one */
			j = nr_ringid;
			if (j >= nma_get_host_nrings(na, t))
				j = 0;
			priv->np_qfirst[t] = nma_get_nrings(na, t) + j;
			priv->np_qlast[t] = nma_get_nrings(na, t) + j + 1;
			nm_prdis("ONE_SW: %s %d %d", nm_txrx2str(t),
				priv->np_qfirst[t], priv->np_qlast[t]);
			break;
		default:
			nm_prerr("invalid regif type %d", nr_mode);
			return EINVAL;
		}
	}
	priv->np_flags = nr_flags;

	/* Allow transparent forwarding mode in the host --> nic
	 * direction only if all the TX hw rings have been opened. */
	if (priv->np_qfirst[NR_TX] == 0 &&
			priv->np_qlast[NR_TX] >= na->num_tx_rings) {
		priv->np_sync_flags |= NAF_CAN_FORWARD_DOWN;
	}

	if (netmap_verbose) {
		nm_prinf("%s: tx [%d,%d) rx [%d,%d) id %d",
			na->name,
			priv->np_qfirst[NR_TX],
			priv->np_qlast[NR_TX],
			priv->np_qfirst[NR_RX],
			priv->np_qlast[NR_RX],
			nr_ringid);
	}
	return 0;
}


/*
 * Set the ring ID. For devices with a single queue, a request
 * for all rings is the same as a single ring.
 */
static int
netmap_set_ringid(struct netmap_priv_d *priv, uint32_t nr_mode,
		uint16_t nr_ringid, uint64_t nr_flags)
{
	struct netmap_adapter *na = priv->np_na;
	int error;
	enum txrx t;

	error = netmap_interp_ringid(priv, nr_mode, nr_ringid, nr_flags);
	if (error) {
		return error;
	}

	priv->np_txpoll = (nr_flags & NR_NO_TX_POLL) ? 0 : 1;

	/* optimization: count the users registered for more than
	 * one ring, which are the ones sleeping on the global queue.
	 * The default netmap_notify() callback will then
	 * avoid signaling the global queue if nobody is using it
	 */
	for_rx_tx(t) {
		if (nm_si_user(priv, t))
			na->si_users[t]++;
	}
	return 0;
}

static void
netmap_unset_ringid(struct netmap_priv_d *priv)
{
	struct netmap_adapter *na = priv->np_na;
	enum txrx t;

	for_rx_tx(t) {
		if (nm_si_user(priv, t))
			na->si_users[t]--;
		priv->np_qfirst[t] = priv->np_qlast[t] = 0;
	}
	priv->np_flags = 0;
	priv->np_txpoll = 0;
	priv->np_kloop_state = 0;
}


/* Set the nr_pending_mode for the requested rings.
 * If requested, also try to get exclusive access to the rings, provided
 * the rings we want to bind are not exclusively owned by a previous bind.
 */
static int
netmap_krings_get(struct netmap_priv_d *priv)
{
	struct netmap_adapter *na = priv->np_na;
	u_int i;
	struct netmap_kring *kring;
	int excl = (priv->np_flags & NR_EXCLUSIVE);
	enum txrx t;

	if (netmap_debug & NM_DEBUG_ON)
		nm_prinf("%s: grabbing tx [%d, %d) rx [%d, %d)",
			na->name,
			priv->np_qfirst[NR_TX],
			priv->np_qlast[NR_TX],
			priv->np_qfirst[NR_RX],
			priv->np_qlast[NR_RX]);

	/* first round: check that all the requested rings
	 * are neither alread exclusively owned, nor we
	 * want exclusive ownership when they are already in use
	 */
	for_rx_tx(t) {
		for (i = priv->np_qfirst[t]; i < priv->np_qlast[t]; i++) {
			kring = NMR(na, t)[i];
			if ((kring->nr_kflags & NKR_EXCLUSIVE) ||
			    (kring->users && excl))
			{
				nm_prdis("ring %s busy", kring->name);
				return EBUSY;
			}
		}
	}

	/* second round: increment usage count (possibly marking them
	 * as exclusive) and set the nr_pending_mode
	 */
	for_rx_tx(t) {
		for (i = priv->np_qfirst[t]; i < priv->np_qlast[t]; i++) {
			kring = NMR(na, t)[i];
			kring->users++;
			if (excl)
				kring->nr_kflags |= NKR_EXCLUSIVE;
	                kring->nr_pending_mode = NKR_NETMAP_ON;
		}
	}

	return 0;

}

/* Undo netmap_krings_get(). This is done by clearing the exclusive mode
 * if was asked on regif, and unset the nr_pending_mode if we are the
 * last users of the involved rings. */
static void
netmap_krings_put(struct netmap_priv_d *priv)
{
	struct netmap_adapter *na = priv->np_na;
	u_int i;
	struct netmap_kring *kring;
	int excl = (priv->np_flags & NR_EXCLUSIVE);
	enum txrx t;

	nm_prdis("%s: releasing tx [%d, %d) rx [%d, %d)",
			na->name,
			priv->np_qfirst[NR_TX],
			priv->np_qlast[NR_TX],
			priv->np_qfirst[NR_RX],
			priv->np_qlast[MR_RX]);

	for_rx_tx(t) {
		for (i = priv->np_qfirst[t]; i < priv->np_qlast[t]; i++) {
			kring = NMR(na, t)[i];
			if (excl)
				kring->nr_kflags &= ~NKR_EXCLUSIVE;
			kring->users--;
			if (kring->users == 0)
				kring->nr_pending_mode = NKR_NETMAP_OFF;
		}
	}
}

static int
nm_priv_rx_enabled(struct netmap_priv_d *priv)
{
	return (priv->np_qfirst[NR_RX] != priv->np_qlast[NR_RX]);
}

/* Validate the CSB entries for both directions (atok and ktoa).
 * To be called under NMG_LOCK(). */
static int
netmap_csb_validate(struct netmap_priv_d *priv, struct nmreq_opt_csb *csbo)
{
	struct nm_csb_atok *csb_atok_base =
		(struct nm_csb_atok *)(uintptr_t)csbo->csb_atok;
	struct nm_csb_ktoa *csb_ktoa_base =
		(struct nm_csb_ktoa *)(uintptr_t)csbo->csb_ktoa;
	enum txrx t;
	int num_rings[NR_TXRX], tot_rings;
	size_t entry_size[2];
	void *csb_start[2];
	int i;

	if (priv->np_kloop_state & NM_SYNC_KLOOP_RUNNING) {
		nm_prerr("Cannot update CSB while kloop is running");
		return EBUSY;
	}

	tot_rings = 0;
	for_rx_tx(t) {
		num_rings[t] = priv->np_qlast[t] - priv->np_qfirst[t];
		tot_rings += num_rings[t];
	}
	if (tot_rings <= 0)
		return 0;

	if (!(priv->np_flags & NR_EXCLUSIVE)) {
		nm_prerr("CSB mode requires NR_EXCLUSIVE");
		return EINVAL;
	}

	entry_size[0] = sizeof(*csb_atok_base);
	entry_size[1] = sizeof(*csb_ktoa_base);
	csb_start[0] = (void *)csb_atok_base;
	csb_start[1] = (void *)csb_ktoa_base;

	for (i = 0; i < 2; i++) {
		/* On Linux we could use access_ok() to simplify
		 * the validation. However, the advantage of
		 * this approach is that it works also on
		 * FreeBSD. */
		size_t csb_size = tot_rings * entry_size[i];
		void *tmp;
		int err;

		if ((uintptr_t)csb_start[i] & (entry_size[i]-1)) {
			nm_prerr("Unaligned CSB address");
			return EINVAL;
		}

		tmp = nm_os_malloc(csb_size);
		if (!tmp)
			return ENOMEM;
		if (i == 0) {
			/* Application --> kernel direction. */
			err = copyin(csb_start[i], tmp, csb_size);
		} else {
			/* Kernel --> application direction. */
			memset(tmp, 0, csb_size);
			err = copyout(tmp, csb_start[i], csb_size);
		}
		nm_os_free(tmp);
		if (err) {
			nm_prerr("Invalid CSB address");
			return err;
		}
	}

	priv->np_csb_atok_base = csb_atok_base;
	priv->np_csb_ktoa_base = csb_ktoa_base;

	/* Initialize the CSB. */
	for_rx_tx(t) {
		for (i = 0; i < num_rings[t]; i++) {
			struct netmap_kring *kring =
				NMR(priv->np_na, t)[i + priv->np_qfirst[t]];
			struct nm_csb_atok *csb_atok = csb_atok_base + i;
			struct nm_csb_ktoa *csb_ktoa = csb_ktoa_base + i;

			if (t == NR_RX) {
				csb_atok += num_rings[NR_TX];
				csb_ktoa += num_rings[NR_TX];
			}

			CSB_WRITE(csb_atok, head, kring->rhead);
			CSB_WRITE(csb_atok, cur, kring->rcur);
			CSB_WRITE(csb_atok, appl_need_kick, 1);
			CSB_WRITE(csb_atok, sync_flags, 1);
			CSB_WRITE(csb_ktoa, hwcur, kring->nr_hwcur);
			CSB_WRITE(csb_ktoa, hwtail, kring->nr_hwtail);
			CSB_WRITE(csb_ktoa, kern_need_kick, 1);

			nm_prinf("csb_init for kring %s: head %u, cur %u, "
				"hwcur %u, hwtail %u", kring->name,
				kring->rhead, kring->rcur, kring->nr_hwcur,
				kring->nr_hwtail);
		}
	}

	return 0;
}

/* Ensure that the netmap adapter can support the given MTU.
 * @return EINVAL if the na cannot be set to mtu, 0 otherwise.
 */
int
netmap_buf_size_validate(const struct netmap_adapter *na, unsigned mtu) {
	unsigned nbs = NETMAP_BUF_SIZE(na);

	if (mtu <= na->rx_buf_maxsize) {
		/* The MTU fits a single NIC slot. We only
		 * Need to check that netmap buffers are
		 * large enough to hold an MTU. NS_MOREFRAG
		 * cannot be used in this case. */
		if (nbs < mtu) {
			nm_prerr("error: netmap buf size (%u) "
				 "< device MTU (%u)", nbs, mtu);
			return EINVAL;
		}
	} else {
		/* More NIC slots may be needed to receive
		 * or transmit a single packet. Check that
		 * the adapter supports NS_MOREFRAG and that
		 * netmap buffers are large enough to hold
		 * the maximum per-slot size. */
		if (!(na->na_flags & NAF_MOREFRAG)) {
			nm_prerr("error: large MTU (%d) needed "
				 "but %s does not support "
				 "NS_MOREFRAG", mtu,
				 na->ifp->if_xname);
			return EINVAL;
		} else if (nbs < na->rx_buf_maxsize) {
			nm_prerr("error: using NS_MOREFRAG on "
				 "%s requires netmap buf size "
				 ">= %u", na->ifp->if_xname,
				 na->rx_buf_maxsize);
			return EINVAL;
		} else {
			nm_prinf("info: netmap application on "
				 "%s needs to support "
				 "NS_MOREFRAG "
				 "(MTU=%u,netmap_buf_size=%u)",
				 na->ifp->if_xname, mtu, nbs);
		}
	}
	return 0;
}


/*
 * possibly move the interface to netmap-mode.
 * If success it returns a pointer to netmap_if, otherwise NULL.
 * This must be called with NMG_LOCK held.
 *
 * The following na callbacks are called in the process:
 *
 * na->nm_config()			[by netmap_update_config]
 * (get current number and size of rings)
 *
 *  	We have a generic one for linux (netmap_linux_config).
 *  	The bwrap has to override this, since it has to forward
 *  	the request to the wrapped adapter (netmap_bwrap_config).
 *
 *
 * na->nm_krings_create()
 * (create and init the krings array)
 *
 * 	One of the following:
 *
 *	* netmap_hw_krings_create, 			(hw ports)
 *		creates the standard layout for the krings
 * 		and adds the mbq (used for the host rings).
 *
 * 	* netmap_vp_krings_create			(VALE ports)
 * 		add leases and scratchpads
 *
 * 	* netmap_pipe_krings_create			(pipes)
 * 		create the krings and rings of both ends and
 * 		cross-link them
 *
 *      * netmap_monitor_krings_create 			(monitors)
 *      	avoid allocating the mbq
 *
 *      * netmap_bwrap_krings_create			(bwraps)
 *      	create both the brap krings array,
 *      	the krings array of the wrapped adapter, and
 *      	(if needed) the fake array for the host adapter
 *
 * na->nm_register(, 1)
 * (put the adapter in netmap mode)
 *
 * 	This may be one of the following:
 *
 * 	* netmap_hw_reg				        (hw ports)
 * 		checks that the ifp is still there, then calls
 * 		the hardware specific callback;
 *
 * 	* netmap_vp_reg					(VALE ports)
 *		If the port is connected to a bridge,
 *		set the NAF_NETMAP_ON flag under the
 *		bridge write lock.
 *
 *	* netmap_pipe_reg				(pipes)
 *		inform the other pipe end that it is no
 *		longer responsible for the lifetime of this
 *		pipe end
 *
 *	* netmap_monitor_reg				(monitors)
 *		intercept the sync callbacks of the monitored
 *		rings
 *
 *	* netmap_bwrap_reg				(bwraps)
 *		cross-link the bwrap and hwna rings,
 *		forward the request to the hwna, override
 *		the hwna notify callback (to get the frames
 *		coming from outside go through the bridge).
 *
 *
 */
int
netmap_do_regif(struct netmap_priv_d *priv, struct netmap_adapter *na,
	uint32_t nr_mode, uint16_t nr_ringid, uint64_t nr_flags)
{
	struct netmap_if *nifp = NULL;
	int error;

	NMG_LOCK_ASSERT();
	priv->np_na = na;     /* store the reference */
	error = netmap_mem_finalize(na->nm_mem, na);
	if (error)
		goto err;

	if (na->active_fds == 0) {

		/* cache the allocator info in the na */
		error = netmap_mem_get_lut(na->nm_mem, &na->na_lut);
		if (error)
			goto err_drop_mem;
		nm_prdis("lut %p bufs %u size %u", na->na_lut.lut, na->na_lut.objtotal,
					    na->na_lut.objsize);

		/* ring configuration may have changed, fetch from the card */
		netmap_update_config(na);
	}

	/* compute the range of tx and rx rings to monitor */
	error = netmap_set_ringid(priv, nr_mode, nr_ringid, nr_flags);
	if (error)
		goto err_put_lut;

	if (na->active_fds == 0) {
		/*
		 * If this is the first registration of the adapter,
		 * perform sanity checks and create the in-kernel view
		 * of the netmap rings (the netmap krings).
		 */
		if (na->ifp && nm_priv_rx_enabled(priv)) {
			/* This netmap adapter is attached to an ifnet. */
			unsigned mtu = nm_os_ifnet_mtu(na->ifp);

			nm_prdis("%s: mtu %d rx_buf_maxsize %d netmap_buf_size %d",
				na->name, mtu, na->rx_buf_maxsize, NETMAP_BUF_SIZE(na));

			if (na->rx_buf_maxsize == 0) {
				nm_prerr("%s: error: rx_buf_maxsize == 0", na->name);
				error = EIO;
				goto err_drop_mem;
			}

			error = netmap_buf_size_validate(na, mtu);
			if (error)
				goto err_drop_mem;
		}

		/*
		 * Depending on the adapter, this may also create
		 * the netmap rings themselves
		 */
		error = na->nm_krings_create(na);
		if (error)
			goto err_put_lut;

	}

	/* now the krings must exist and we can check whether some
	 * previous bind has exclusive ownership on them, and set
	 * nr_pending_mode
	 */
	error = netmap_krings_get(priv);
	if (error)
		goto err_del_krings;

	/* create all needed missing netmap rings */
	error = netmap_mem_rings_create(na);
	if (error)
		goto err_rel_excl;

	/* in all cases, create a new netmap if */
	nifp = netmap_mem_if_new(na, priv);
	if (nifp == NULL) {
		error = ENOMEM;
		goto err_rel_excl;
	}

	if (nm_kring_pending(priv)) {
		/* Some kring is switching mode, tell the adapter to
		 * react on this. */
		error = na->nm_register(na, 1);
		if (error)
			goto err_del_if;
	}

	/* Commit the reference. */
	na->active_fds++;

	/*
	 * advertise that the interface is ready by setting np_nifp.
	 * The barrier is needed because readers (poll, *SYNC and mmap)
	 * check for priv->np_nifp != NULL without locking
	 */
	mb(); /* make sure previous writes are visible to all CPUs */
	priv->np_nifp = nifp;

	return 0;

err_del_if:
	netmap_mem_if_delete(na, nifp);
err_rel_excl:
	netmap_krings_put(priv);
	netmap_mem_rings_delete(na);
err_del_krings:
	if (na->active_fds == 0)
		na->nm_krings_delete(na);
err_put_lut:
	if (na->active_fds == 0)
		memset(&na->na_lut, 0, sizeof(na->na_lut));
err_drop_mem:
	netmap_mem_drop(na);
err:
	priv->np_na = NULL;
	return error;
}


/*
 * update kring and ring at the end of rxsync/txsync.
 */
static inline void
nm_sync_finalize(struct netmap_kring *kring)
{
	/*
	 * Update ring tail to what the kernel knows
	 * After txsync: head/rhead/hwcur might be behind cur/rcur
	 * if no carrier.
	 */
	kring->ring->tail = kring->rtail = kring->nr_hwtail;

	nm_prdis(5, "%s now hwcur %d hwtail %d head %d cur %d tail %d",
		kring->name, kring->nr_hwcur, kring->nr_hwtail,
		kring->rhead, kring->rcur, kring->rtail);
}

/* set ring timestamp */
static inline void
ring_timestamp_set(struct netmap_ring *ring)
{
	if (netmap_no_timestamp == 0 || ring->flags & NR_TIMESTAMP) {
		microtime(&ring->ts);
	}
}

static int nmreq_copyin(struct nmreq_header *, int);
static int nmreq_copyout(struct nmreq_header *, int);
static int nmreq_checkoptions(struct nmreq_header *);

/*
 * ioctl(2) support for the "netmap" device.
 *
 * Following a list of accepted commands:
 * - NIOCCTRL		device control API
 * - NIOCTXSYNC		sync TX rings
 * - NIOCRXSYNC		sync RX rings
 * - SIOCGIFADDR	just for convenience
 * - NIOCGINFO		deprecated (legacy API)
 * - NIOCREGIF		deprecated (legacy API)
 *
 * Return 0 on success, errno otherwise.
 */
int
netmap_ioctl(struct netmap_priv_d *priv, u_long cmd, caddr_t data,
		struct thread *td, int nr_body_is_user)
{
	struct mbq q;	/* packets from RX hw queues to host stack */
	struct netmap_adapter *na = NULL;
	struct netmap_mem_d *nmd = NULL;
	struct ifnet *ifp = NULL;
	int error = 0;
	u_int i, qfirst, qlast;
	struct netmap_kring **krings;
	int sync_flags;
	enum txrx t;

	switch (cmd) {
	case NIOCCTRL: {
		struct nmreq_header *hdr = (struct nmreq_header *)data;

		if (hdr->nr_version < NETMAP_MIN_API ||
		    hdr->nr_version > NETMAP_MAX_API) {
			nm_prerr("API mismatch: got %d need %d",
				hdr->nr_version, NETMAP_API);
			return EINVAL;
		}

		/* Make a kernel-space copy of the user-space nr_body.
		 * For convenince, the nr_body pointer and the pointers
		 * in the options list will be replaced with their
		 * kernel-space counterparts. The original pointers are
		 * saved internally and later restored by nmreq_copyout
		 */
		error = nmreq_copyin(hdr, nr_body_is_user);
		if (error) {
			return error;
		}

		/* Sanitize hdr->nr_name. */
		hdr->nr_name[sizeof(hdr->nr_name) - 1] = '\0';

		switch (hdr->nr_reqtype) {
		case NETMAP_REQ_REGISTER: {
			struct nmreq_register *req =
				(struct nmreq_register *)(uintptr_t)hdr->nr_body;
			struct netmap_if *nifp;

			/* Protect access to priv from concurrent requests. */
			NMG_LOCK();
			do {
				struct nmreq_option *opt;
				u_int memflags;

				if (priv->np_nifp != NULL) {	/* thread already registered */
					error = EBUSY;
					break;
				}

#ifdef WITH_EXTMEM
				opt = nmreq_findoption((struct nmreq_option *)(uintptr_t)hdr->nr_options,
						NETMAP_REQ_OPT_EXTMEM);
				if (opt != NULL) {
					struct nmreq_opt_extmem *e =
						(struct nmreq_opt_extmem *)opt;

					error = nmreq_checkduplicate(opt);
					if (error) {
						opt->nro_status = error;
						break;
					}
					nmd = netmap_mem_ext_create(e->nro_usrptr,
							&e->nro_info, &error);
					opt->nro_status = error;
					if (nmd == NULL)
						break;
				}
#endif /* WITH_EXTMEM */

				if (nmd == NULL && req->nr_mem_id) {
					/* find the allocator and get a reference */
					nmd = netmap_mem_find(req->nr_mem_id);
					if (nmd == NULL) {
						if (netmap_verbose) {
							nm_prerr("%s: failed to find mem_id %u",
									hdr->nr_name, req->nr_mem_id);
						}
						error = EINVAL;
						break;
					}
				}
				/* find the interface and a reference */
				error = netmap_get_na(hdr, &na, &ifp, nmd,
						      1 /* create */); /* keep reference */
				if (error)
					break;
				if (NETMAP_OWNED_BY_KERN(na)) {
					error = EBUSY;
					break;
				}

				if (na->virt_hdr_len && !(req->nr_flags & NR_ACCEPT_VNET_HDR)) {
					nm_prerr("virt_hdr_len=%d, but application does "
						"not accept it", na->virt_hdr_len);
					error = EIO;
					break;
				}

				error = netmap_do_regif(priv, na, req->nr_mode,
							req->nr_ringid, req->nr_flags);
				if (error) {    /* reg. failed, release priv and ref */
					break;
				}

				opt = nmreq_findoption((struct nmreq_option *)(uintptr_t)hdr->nr_options,
							NETMAP_REQ_OPT_CSB);
				if (opt != NULL) {
					struct nmreq_opt_csb *csbo =
						(struct nmreq_opt_csb *)opt;
					error = nmreq_checkduplicate(opt);
					if (!error) {
						error = netmap_csb_validate(priv, csbo);
					}
					opt->nro_status = error;
					if (error) {
						netmap_do_unregif(priv);
						break;
					}
				}

				nifp = priv->np_nifp;

				/* return the offset of the netmap_if object */
				req->nr_rx_rings = na->num_rx_rings;
				req->nr_tx_rings = na->num_tx_rings;
				req->nr_rx_slots = na->num_rx_desc;
				req->nr_tx_slots = na->num_tx_desc;
				req->nr_host_tx_rings = na->num_host_tx_rings;
				req->nr_host_rx_rings = na->num_host_rx_rings;
				error = netmap_mem_get_info(na->nm_mem, &req->nr_memsize, &memflags,
					&req->nr_mem_id);
				if (error) {
					netmap_do_unregif(priv);
					break;
				}
				if (memflags & NETMAP_MEM_PRIVATE) {
					*(uint32_t *)(uintptr_t)&nifp->ni_flags |= NI_PRIV_MEM;
				}
				for_rx_tx(t) {
					priv->np_si[t] = nm_si_user(priv, t) ?
						&na->si[t] : &NMR(na, t)[priv->np_qfirst[t]]->si;
				}

				if (req->nr_extra_bufs) {
					if (netmap_verbose)
						nm_prinf("requested %d extra buffers",
							req->nr_extra_bufs);
					req->nr_extra_bufs = netmap_extra_alloc(na,
						&nifp->ni_bufs_head, req->nr_extra_bufs);
					if (netmap_verbose)
						nm_prinf("got %d extra buffers", req->nr_extra_bufs);
				}
				req->nr_offset = netmap_mem_if_offset(na->nm_mem, nifp);

				error = nmreq_checkoptions(hdr);
				if (error) {
					netmap_do_unregif(priv);
					break;
				}

				/* store ifp reference so that priv destructor may release it */
				priv->np_ifp = ifp;
			} while (0);
			if (error) {
				netmap_unget_na(na, ifp);
			}
			/* release the reference from netmap_mem_find() or
			 * netmap_mem_ext_create()
			 */
			if (nmd)
				netmap_mem_put(nmd);
			NMG_UNLOCK();
			break;
		}

		case NETMAP_REQ_PORT_INFO_GET: {
			struct nmreq_port_info_get *req =
				(struct nmreq_port_info_get *)(uintptr_t)hdr->nr_body;

			NMG_LOCK();
			do {
				u_int memflags;

				if (hdr->nr_name[0] != '\0') {
					/* Build a nmreq_register out of the nmreq_port_info_get,
					 * so that we can call netmap_get_na(). */
					struct nmreq_register regreq;
					bzero(&regreq, sizeof(regreq));
					regreq.nr_mode = NR_REG_ALL_NIC;
					regreq.nr_tx_slots = req->nr_tx_slots;
					regreq.nr_rx_slots = req->nr_rx_slots;
					regreq.nr_tx_rings = req->nr_tx_rings;
					regreq.nr_rx_rings = req->nr_rx_rings;
					regreq.nr_host_tx_rings = req->nr_host_tx_rings;
					regreq.nr_host_rx_rings = req->nr_host_rx_rings;
					regreq.nr_mem_id = req->nr_mem_id;

					/* get a refcount */
					hdr->nr_reqtype = NETMAP_REQ_REGISTER;
					hdr->nr_body = (uintptr_t)&regreq;
					error = netmap_get_na(hdr, &na, &ifp, NULL, 1 /* create */);
					hdr->nr_reqtype = NETMAP_REQ_PORT_INFO_GET; /* reset type */
					hdr->nr_body = (uintptr_t)req; /* reset nr_body */
					if (error) {
						na = NULL;
						ifp = NULL;
						break;
					}
					nmd = na->nm_mem; /* get memory allocator */
				} else {
					nmd = netmap_mem_find(req->nr_mem_id ? req->nr_mem_id : 1);
					if (nmd == NULL) {
						if (netmap_verbose)
							nm_prerr("%s: failed to find mem_id %u",
									hdr->nr_name,
									req->nr_mem_id ? req->nr_mem_id : 1);
						error = EINVAL;
						break;
					}
				}

				error = netmap_mem_get_info(nmd, &req->nr_memsize, &memflags,
					&req->nr_mem_id);
				if (error)
					break;
				if (na == NULL) /* only memory info */
					break;
				netmap_update_config(na);
				req->nr_rx_rings = na->num_rx_rings;
				req->nr_tx_rings = na->num_tx_rings;
				req->nr_rx_slots = na->num_rx_desc;
				req->nr_tx_slots = na->num_tx_desc;
				req->nr_host_tx_rings = na->num_host_tx_rings;
				req->nr_host_rx_rings = na->num_host_rx_rings;
			} while (0);
			netmap_unget_na(na, ifp);
			NMG_UNLOCK();
			break;
		}
#ifdef WITH_VALE
		case NETMAP_REQ_VALE_ATTACH: {
			error = netmap_vale_attach(hdr, NULL /* userspace request */);
			break;
		}

		case NETMAP_REQ_VALE_DETACH: {
			error = netmap_vale_detach(hdr, NULL /* userspace request */);
			break;
		}

		case NETMAP_REQ_VALE_LIST: {
			error = netmap_vale_list(hdr);
			break;
		}

		case NETMAP_REQ_PORT_HDR_SET: {
			struct nmreq_port_hdr *req =
				(struct nmreq_port_hdr *)(uintptr_t)hdr->nr_body;
			/* Build a nmreq_register out of the nmreq_port_hdr,
			 * so that we can call netmap_get_bdg_na(). */
			struct nmreq_register regreq;
			bzero(&regreq, sizeof(regreq));
			regreq.nr_mode = NR_REG_ALL_NIC;

			/* For now we only support virtio-net headers, and only for
			 * VALE ports, but this may change in future. Valid lengths
			 * for the virtio-net header are 0 (no header), 10 and 12. */
			if (req->nr_hdr_len != 0 &&
				req->nr_hdr_len != sizeof(struct nm_vnet_hdr) &&
					req->nr_hdr_len != 12) {
				if (netmap_verbose)
					nm_prerr("invalid hdr_len %u", req->nr_hdr_len);
				error = EINVAL;
				break;
			}
			NMG_LOCK();
			hdr->nr_reqtype = NETMAP_REQ_REGISTER;
			hdr->nr_body = (uintptr_t)&regreq;
			error = netmap_get_vale_na(hdr, &na, NULL, 0);
			hdr->nr_reqtype = NETMAP_REQ_PORT_HDR_SET;
			hdr->nr_body = (uintptr_t)req;
			if (na && !error) {
				struct netmap_vp_adapter *vpna =
					(struct netmap_vp_adapter *)na;
				na->virt_hdr_len = req->nr_hdr_len;
				if (na->virt_hdr_len) {
					vpna->mfs = NETMAP_BUF_SIZE(na);
				}
				if (netmap_verbose)
					nm_prinf("Using vnet_hdr_len %d for %p", na->virt_hdr_len, na);
				netmap_adapter_put(na);
			} else if (!na) {
				error = ENXIO;
			}
			NMG_UNLOCK();
			break;
		}

		case NETMAP_REQ_PORT_HDR_GET: {
			/* Get vnet-header length for this netmap port */
			struct nmreq_port_hdr *req =
				(struct nmreq_port_hdr *)(uintptr_t)hdr->nr_body;
			/* Build a nmreq_register out of the nmreq_port_hdr,
			 * so that we can call netmap_get_bdg_na(). */
			struct nmreq_register regreq;
			struct ifnet *ifp;

			bzero(&regreq, sizeof(regreq));
			regreq.nr_mode = NR_REG_ALL_NIC;
			NMG_LOCK();
			hdr->nr_reqtype = NETMAP_REQ_REGISTER;
			hdr->nr_body = (uintptr_t)&regreq;
			error = netmap_get_na(hdr, &na, &ifp, NULL, 0);
			hdr->nr_reqtype = NETMAP_REQ_PORT_HDR_GET;
			hdr->nr_body = (uintptr_t)req;
			if (na && !error) {
				req->nr_hdr_len = na->virt_hdr_len;
			}
			netmap_unget_na(na, ifp);
			NMG_UNLOCK();
			break;
		}

		case NETMAP_REQ_VALE_NEWIF: {
			error = nm_vi_create(hdr);
			break;
		}

		case NETMAP_REQ_VALE_DELIF: {
			error = nm_vi_destroy(hdr->nr_name);
			break;
		}

		case NETMAP_REQ_VALE_POLLING_ENABLE:
		case NETMAP_REQ_VALE_POLLING_DISABLE: {
			error = nm_bdg_polling(hdr);
			break;
		}
#endif  /* WITH_VALE */
		case NETMAP_REQ_POOLS_INFO_GET: {
			/* Get information from the memory allocator used for
			 * hdr->nr_name. */
			struct nmreq_pools_info *req =
				(struct nmreq_pools_info *)(uintptr_t)hdr->nr_body;
			NMG_LOCK();
			do {
				/* Build a nmreq_register out of the nmreq_pools_info,
				 * so that we can call netmap_get_na(). */
				struct nmreq_register regreq;
				bzero(&regreq, sizeof(regreq));
				regreq.nr_mem_id = req->nr_mem_id;
				regreq.nr_mode = NR_REG_ALL_NIC;

				hdr->nr_reqtype = NETMAP_REQ_REGISTER;
				hdr->nr_body = (uintptr_t)&regreq;
				error = netmap_get_na(hdr, &na, &ifp, NULL, 1 /* create */);
				hdr->nr_reqtype = NETMAP_REQ_POOLS_INFO_GET; /* reset type */
				hdr->nr_body = (uintptr_t)req; /* reset nr_body */
				if (error) {
					na = NULL;
					ifp = NULL;
					break;
				}
				nmd = na->nm_mem; /* grab the memory allocator */
				if (nmd == NULL) {
					error = EINVAL;
					break;
				}

				/* Finalize the memory allocator, get the pools
				 * information and release the allocator. */
				error = netmap_mem_finalize(nmd, na);
				if (error) {
					break;
				}
				error = netmap_mem_pools_info_get(req, nmd);
				netmap_mem_drop(na);
			} while (0);
			netmap_unget_na(na, ifp);
			NMG_UNLOCK();
			break;
		}

		case NETMAP_REQ_CSB_ENABLE: {
			struct nmreq_option *opt;

			opt = nmreq_findoption((struct nmreq_option *)(uintptr_t)hdr->nr_options,
						NETMAP_REQ_OPT_CSB);
			if (opt == NULL) {
				error = EINVAL;
			} else {
				struct nmreq_opt_csb *csbo =
					(struct nmreq_opt_csb *)opt;
				error = nmreq_checkduplicate(opt);
				if (!error) {
					NMG_LOCK();
					error = netmap_csb_validate(priv, csbo);
					NMG_UNLOCK();
				}
				opt->nro_status = error;
			}
			break;
		}

		case NETMAP_REQ_SYNC_KLOOP_START: {
			error = netmap_sync_kloop(priv, hdr);
			break;
		}

		case NETMAP_REQ_SYNC_KLOOP_STOP: {
			error = netmap_sync_kloop_stop(priv);
			break;
		}

		default: {
			error = EINVAL;
			break;
		}
		}
		/* Write back request body to userspace and reset the
		 * user-space pointer. */
		error = nmreq_copyout(hdr, error);
		break;
	}

	case NIOCTXSYNC:
	case NIOCRXSYNC: {
		if (unlikely(priv->np_nifp == NULL)) {
			error = ENXIO;
			break;
		}
		mb(); /* make sure following reads are not from cache */

		if (unlikely(priv->np_csb_atok_base)) {
			nm_prerr("Invalid sync in CSB mode");
			error = EBUSY;
			break;
		}

		na = priv->np_na;      /* we have a reference */

		mbq_init(&q);
		t = (cmd == NIOCTXSYNC ? NR_TX : NR_RX);
		krings = NMR(na, t);
		qfirst = priv->np_qfirst[t];
		qlast = priv->np_qlast[t];
		sync_flags = priv->np_sync_flags;

		for (i = qfirst; i < qlast; i++) {
			struct netmap_kring *kring = krings[i];
			struct netmap_ring *ring = kring->ring;

			if (unlikely(nm_kr_tryget(kring, 1, &error))) {
				error = (error ? EIO : 0);
				continue;
			}

			if (cmd == NIOCTXSYNC) {
				if (netmap_debug & NM_DEBUG_TXSYNC)
					nm_prinf("pre txsync ring %d cur %d hwcur %d",
					    i, ring->cur,
					    kring->nr_hwcur);
				if (nm_txsync_prologue(kring, ring) >= kring->nkr_num_slots) {
					netmap_ring_reinit(kring);
				} else if (kring->nm_sync(kring, sync_flags | NAF_FORCE_RECLAIM) == 0) {
					nm_sync_finalize(kring);
				}
				if (netmap_debug & NM_DEBUG_TXSYNC)
					nm_prinf("post txsync ring %d cur %d hwcur %d",
					    i, ring->cur,
					    kring->nr_hwcur);
			} else {
				if (nm_rxsync_prologue(kring, ring) >= kring->nkr_num_slots) {
					netmap_ring_reinit(kring);
				}
				if (nm_may_forward_up(kring)) {
					/* transparent forwarding, see netmap_poll() */
					netmap_grab_packets(kring, &q, netmap_fwd);
				}
				if (kring->nm_sync(kring, sync_flags | NAF_FORCE_READ) == 0) {
					nm_sync_finalize(kring);
				}
				ring_timestamp_set(ring);
			}
			nm_kr_put(kring);
		}

		if (mbq_peek(&q)) {
			netmap_send_up(na->ifp, &q);
		}

		break;
	}

	default: {
		return netmap_ioctl_legacy(priv, cmd, data, td);
		break;
	}
	}

	return (error);
}

size_t
nmreq_size_by_type(uint16_t nr_reqtype)
{
	switch (nr_reqtype) {
	case NETMAP_REQ_REGISTER:
		return sizeof(struct nmreq_register);
	case NETMAP_REQ_PORT_INFO_GET:
		return sizeof(struct nmreq_port_info_get);
	case NETMAP_REQ_VALE_ATTACH:
		return sizeof(struct nmreq_vale_attach);
	case NETMAP_REQ_VALE_DETACH:
		return sizeof(struct nmreq_vale_detach);
	case NETMAP_REQ_VALE_LIST:
		return sizeof(struct nmreq_vale_list);
	case NETMAP_REQ_PORT_HDR_SET:
	case NETMAP_REQ_PORT_HDR_GET:
		return sizeof(struct nmreq_port_hdr);
	case NETMAP_REQ_VALE_NEWIF:
		return sizeof(struct nmreq_vale_newif);
	case NETMAP_REQ_VALE_DELIF:
	case NETMAP_REQ_SYNC_KLOOP_STOP:
	case NETMAP_REQ_CSB_ENABLE:
		return 0;
	case NETMAP_REQ_VALE_POLLING_ENABLE:
	case NETMAP_REQ_VALE_POLLING_DISABLE:
		return sizeof(struct nmreq_vale_polling);
	case NETMAP_REQ_POOLS_INFO_GET:
		return sizeof(struct nmreq_pools_info);
	case NETMAP_REQ_SYNC_KLOOP_START:
		return sizeof(struct nmreq_sync_kloop_start);
	}
	return 0;
}

static size_t
nmreq_opt_size_by_type(uint32_t nro_reqtype, uint64_t nro_size)
{
	size_t rv = sizeof(struct nmreq_option);
#ifdef NETMAP_REQ_OPT_DEBUG
	if (nro_reqtype & NETMAP_REQ_OPT_DEBUG)
		return (nro_reqtype & ~NETMAP_REQ_OPT_DEBUG);
#endif /* NETMAP_REQ_OPT_DEBUG */
	switch (nro_reqtype) {
#ifdef WITH_EXTMEM
	case NETMAP_REQ_OPT_EXTMEM:
		rv = sizeof(struct nmreq_opt_extmem);
		break;
#endif /* WITH_EXTMEM */
	case NETMAP_REQ_OPT_SYNC_KLOOP_EVENTFDS:
		if (nro_size >= rv)
			rv = nro_size;
		break;
	case NETMAP_REQ_OPT_CSB:
		rv = sizeof(struct nmreq_opt_csb);
		break;
	case NETMAP_REQ_OPT_SYNC_KLOOP_MODE:
		rv = sizeof(struct nmreq_opt_sync_kloop_mode);
		break;
	}
	/* subtract the common header */
	return rv - sizeof(struct nmreq_option);
}

int
nmreq_copyin(struct nmreq_header *hdr, int nr_body_is_user)
{
	size_t rqsz, optsz, bufsz;
	int error;
	char *ker = NULL, *p;
	struct nmreq_option **next, *src;
	struct nmreq_option buf;
	uint64_t *ptrs;

	if (hdr->nr_reserved) {
		if (netmap_verbose)
			nm_prerr("nr_reserved must be zero");
		return EINVAL;
	}

	if (!nr_body_is_user)
		return 0;

	hdr->nr_reserved = nr_body_is_user;

	/* compute the total size of the buffer */
	rqsz = nmreq_size_by_type(hdr->nr_reqtype);
	if (rqsz > NETMAP_REQ_MAXSIZE) {
		error = EMSGSIZE;
		goto out_err;
	}
	if ((rqsz && hdr->nr_body == (uintptr_t)NULL) ||
		(!rqsz && hdr->nr_body != (uintptr_t)NULL)) {
		/* Request body expected, but not found; or
		 * request body found but unexpected. */
		if (netmap_verbose)
			nm_prerr("nr_body expected but not found, or vice versa");
		error = EINVAL;
		goto out_err;
	}

	bufsz = 2 * sizeof(void *) + rqsz;
	optsz = 0;
	for (src = (struct nmreq_option *)(uintptr_t)hdr->nr_options; src;
	     src = (struct nmreq_option *)(uintptr_t)buf.nro_next)
	{
		error = copyin(src, &buf, sizeof(*src));
		if (error)
			goto out_err;
		optsz += sizeof(*src);
		optsz += nmreq_opt_size_by_type(buf.nro_reqtype, buf.nro_size);
		if (rqsz + optsz > NETMAP_REQ_MAXSIZE) {
			error = EMSGSIZE;
			goto out_err;
		}
		bufsz += optsz + sizeof(void *);
	}

	ker = nm_os_malloc(bufsz);
	if (ker == NULL) {
		error = ENOMEM;
		goto out_err;
	}
	p = ker;

	/* make a copy of the user pointers */
	ptrs = (uint64_t*)p;
	*ptrs++ = hdr->nr_body;
	*ptrs++ = hdr->nr_options;
	p = (char *)ptrs;

	/* copy the body */
	error = copyin((void *)(uintptr_t)hdr->nr_body, p, rqsz);
	if (error)
		goto out_restore;
	/* overwrite the user pointer with the in-kernel one */
	hdr->nr_body = (uintptr_t)p;
	p += rqsz;

	/* copy the options */
	next = (struct nmreq_option **)&hdr->nr_options;
	src = *next;
	while (src) {
		struct nmreq_option *opt;

		/* copy the option header */
		ptrs = (uint64_t *)p;
		opt = (struct nmreq_option *)(ptrs + 1);
		error = copyin(src, opt, sizeof(*src));
		if (error)
			goto out_restore;
		/* make a copy of the user next pointer */
		*ptrs = opt->nro_next;
		/* overwrite the user pointer with the in-kernel one */
		*next = opt;

		/* initialize the option as not supported.
		 * Recognized options will update this field.
		 */
		opt->nro_status = EOPNOTSUPP;

		p = (char *)(opt + 1);

		/* copy the option body */
		optsz = nmreq_opt_size_by_type(opt->nro_reqtype,
						opt->nro_size);
		if (optsz) {
			/* the option body follows the option header */
			error = copyin(src + 1, p, optsz);
			if (error)
				goto out_restore;
			p += optsz;
		}

		/* move to next option */
		next = (struct nmreq_option **)&opt->nro_next;
		src = *next;
	}
	return 0;

out_restore:
	ptrs = (uint64_t *)ker;
	hdr->nr_body = *ptrs++;
	hdr->nr_options = *ptrs++;
	hdr->nr_reserved = 0;
	nm_os_free(ker);
out_err:
	return error;
}

static int
nmreq_copyout(struct nmreq_header *hdr, int rerror)
{
	struct nmreq_option *src, *dst;
	void *ker = (void *)(uintptr_t)hdr->nr_body, *bufstart;
	uint64_t *ptrs;
	size_t bodysz;
	int error;

	if (!hdr->nr_reserved)
		return rerror;

	/* restore the user pointers in the header */
	ptrs = (uint64_t *)ker - 2;
	bufstart = ptrs;
	hdr->nr_body = *ptrs++;
	src = (struct nmreq_option *)(uintptr_t)hdr->nr_options;
	hdr->nr_options = *ptrs;

	if (!rerror) {
		/* copy the body */
		bodysz = nmreq_size_by_type(hdr->nr_reqtype);
		error = copyout(ker, (void *)(uintptr_t)hdr->nr_body, bodysz);
		if (error) {
			rerror = error;
			goto out;
		}
	}

	/* copy the options */
	dst = (struct nmreq_option *)(uintptr_t)hdr->nr_options;
	while (src) {
		size_t optsz;
		uint64_t next;

		/* restore the user pointer */
		next = src->nro_next;
		ptrs = (uint64_t *)src - 1;
		src->nro_next = *ptrs;

		/* always copy the option header */
		error = copyout(src, dst, sizeof(*src));
		if (error) {
			rerror = error;
			goto out;
		}

		/* copy the option body only if there was no error */
		if (!rerror && !src->nro_status) {
			optsz = nmreq_opt_size_by_type(src->nro_reqtype,
							src->nro_size);
			if (optsz) {
				error = copyout(src + 1, dst + 1, optsz);
				if (error) {
					rerror = error;
					goto out;
				}
			}
		}
		src = (struct nmreq_option *)(uintptr_t)next;
		dst = (struct nmreq_option *)(uintptr_t)*ptrs;
	}


out:
	hdr->nr_reserved = 0;
	nm_os_free(bufstart);
	return rerror;
}

struct nmreq_option *
nmreq_findoption(struct nmreq_option *opt, uint16_t reqtype)
{
	for ( ; opt; opt = (struct nmreq_option *)(uintptr_t)opt->nro_next)
		if (opt->nro_reqtype == reqtype)
			return opt;
	return NULL;
}

int
nmreq_checkduplicate(struct nmreq_option *opt) {
	uint16_t type = opt->nro_reqtype;
	int dup = 0;

	while ((opt = nmreq_findoption((struct nmreq_option *)(uintptr_t)opt->nro_next,
			type))) {
		dup++;
		opt->nro_status = EINVAL;
	}
	return (dup ? EINVAL : 0);
}

static int
nmreq_checkoptions(struct nmreq_header *hdr)
{
	struct nmreq_option *opt;
	/* return error if there is still any option
	 * marked as not supported
	 */

	for (opt = (struct nmreq_option *)(uintptr_t)hdr->nr_options; opt;
	     opt = (struct nmreq_option *)(uintptr_t)opt->nro_next)
		if (opt->nro_status == EOPNOTSUPP)
			return EOPNOTSUPP;

	return 0;
}

/*
 * select(2) and poll(2) handlers for the "netmap" device.
 *
 * Can be called for one or more queues.
 * Return true the event mask corresponding to ready events.
 * If there are no ready events (and 'sr' is not NULL), do a
 * selrecord on either individual selinfo or on the global one.
 * Device-dependent parts (locking and sync of tx/rx rings)
 * are done through callbacks.
 *
 * On linux, arguments are really pwait, the poll table, and 'td' is struct file *
 * The first one is remapped to pwait as selrecord() uses the name as an
 * hidden argument.
 */
int
netmap_poll(struct netmap_priv_d *priv, int events, NM_SELRECORD_T *sr)
{
	struct netmap_adapter *na;
	struct netmap_kring *kring;
	struct netmap_ring *ring;
	u_int i, want[NR_TXRX], revents = 0;
	NM_SELINFO_T *si[NR_TXRX];
#define want_tx want[NR_TX]
#define want_rx want[NR_RX]
	struct mbq q;	/* packets from RX hw queues to host stack */

	/*
	 * In order to avoid nested locks, we need to "double check"
	 * txsync and rxsync if we decide to do a selrecord().
	 * retry_tx (and retry_rx, later) prevent looping forever.
	 */
	int retry_tx = 1, retry_rx = 1;

	/* Transparent mode: send_down is 1 if we have found some
	 * packets to forward (host RX ring --> NIC) during the rx
	 * scan and we have not sent them down to the NIC yet.
	 * Transparent mode requires to bind all rings to a single
	 * file descriptor.
	 */
	int send_down = 0;
	int sync_flags = priv->np_sync_flags;

	mbq_init(&q);

	if (unlikely(priv->np_nifp == NULL)) {
		return POLLERR;
	}
	mb(); /* make sure following reads are not from cache */

	na = priv->np_na;

	if (unlikely(!nm_netmap_on(na)))
		return POLLERR;

	if (unlikely(priv->np_csb_atok_base)) {
		nm_prerr("Invalid poll in CSB mode");
		return POLLERR;
	}

	if (netmap_debug & NM_DEBUG_ON)
		nm_prinf("device %s events 0x%x", na->name, events);
	want_tx = events & (POLLOUT | POLLWRNORM);
	want_rx = events & (POLLIN | POLLRDNORM);

	/*
	 * If the card has more than one queue AND the file descriptor is
	 * bound to all of them, we sleep on the "global" selinfo, otherwise
	 * we sleep on individual selinfo (FreeBSD only allows two selinfo's
	 * per file descriptor).
	 * The interrupt routine in the driver wake one or the other
	 * (or both) depending on which clients are active.
	 *
	 * rxsync() is only called if we run out of buffers on a POLLIN.
	 * txsync() is called if we run out of buffers on POLLOUT, or
	 * there are pending packets to send. The latter can be disabled
	 * passing NETMAP_NO_TX_POLL in the NIOCREG call.
	 */
	si[NR_RX] = priv->np_si[NR_RX];
	si[NR_TX] = priv->np_si[NR_TX];

#ifdef __FreeBSD__
	/*
	 * We start with a lock free round which is cheap if we have
	 * slots available. If this fails, then lock and call the sync
	 * routines. We can't do this on Linux, as the contract says
	 * that we must call nm_os_selrecord() unconditionally.
	 */
	if (want_tx) {
		const enum txrx t = NR_TX;
		for (i = priv->np_qfirst[t]; i < priv->np_qlast[t]; i++) {
			kring = NMR(na, t)[i];
			if (kring->ring->cur != kring->ring->tail) {
				/* Some unseen TX space is available, so what
				 * we don't need to run txsync. */
				revents |= want[t];
				want[t] = 0;
				break;
			}
		}
	}
	if (want_rx) {
		const enum txrx t = NR_RX;
		int rxsync_needed = 0;

		for (i = priv->np_qfirst[t]; i < priv->np_qlast[t]; i++) {
			kring = NMR(na, t)[i];
			if (kring->ring->cur == kring->ring->tail
				|| kring->rhead != kring->ring->head) {
				/* There are no unseen packets on this ring,
				 * or there are some buffers to be returned
				 * to the netmap port. We therefore go ahead
				 * and run rxsync. */
				rxsync_needed = 1;
				break;
			}
		}
		if (!rxsync_needed) {
			revents |= want_rx;
			want_rx = 0;
		}
	}
#endif

#ifdef linux
	/* The selrecord must be unconditional on linux. */
	nm_os_selrecord(sr, si[NR_RX]);
	nm_os_selrecord(sr, si[NR_TX]);
#endif /* linux */

	/*
	 * If we want to push packets out (priv->np_txpoll) or
	 * want_tx is still set, we must issue txsync calls
	 * (on all rings, to avoid that the tx rings stall).
	 * Fortunately, normal tx mode has np_txpoll set.
	 */
	if (priv->np_txpoll || want_tx) {
		/*
		 * The first round checks if anyone is ready, if not
		 * do a selrecord and another round to handle races.
		 * want_tx goes to 0 if any space is found, and is
		 * used to skip rings with no pending transmissions.
		 */
flush_tx:
		for (i = priv->np_qfirst[NR_TX]; i < priv->np_qlast[NR_TX]; i++) {
			int found = 0;

			kring = na->tx_rings[i];
			ring = kring->ring;

			/*
			 * Don't try to txsync this TX ring if we already found some
			 * space in some of the TX rings (want_tx == 0) and there are no
			 * TX slots in this ring that need to be flushed to the NIC
			 * (head == hwcur).
			 */
			if (!send_down && !want_tx && ring->head == kring->nr_hwcur)
				continue;

			if (nm_kr_tryget(kring, 1, &revents))
				continue;

			if (nm_txsync_prologue(kring, ring) >= kring->nkr_num_slots) {
				netmap_ring_reinit(kring);
				revents |= POLLERR;
			} else {
				if (kring->nm_sync(kring, sync_flags))
					revents |= POLLERR;
				else
					nm_sync_finalize(kring);
			}

			/*
			 * If we found new slots, notify potential
			 * listeners on the same ring.
			 * Since we just did a txsync, look at the copies
			 * of cur,tail in the kring.
			 */
			found = kring->rcur != kring->rtail;
			nm_kr_put(kring);
			if (found) { /* notify other listeners */
				revents |= want_tx;
				want_tx = 0;
#ifndef linux
				kring->nm_notify(kring, 0);
#endif /* linux */
			}
		}
		/* if there were any packet to forward we must have handled them by now */
		send_down = 0;
		if (want_tx && retry_tx && sr) {
#ifndef linux
			nm_os_selrecord(sr, si[NR_TX]);
#endif /* !linux */
			retry_tx = 0;
			goto flush_tx;
		}
	}

	/*
	 * If want_rx is still set scan receive rings.
	 * Do it on all rings because otherwise we starve.
	 */
	if (want_rx) {
		/* two rounds here for race avoidance */
do_retry_rx:
		for (i = priv->np_qfirst[NR_RX]; i < priv->np_qlast[NR_RX]; i++) {
			int found = 0;

			kring = na->rx_rings[i];
			ring = kring->ring;

			if (unlikely(nm_kr_tryget(kring, 1, &revents)))
				continue;

			if (nm_rxsync_prologue(kring, ring) >= kring->nkr_num_slots) {
				netmap_ring_reinit(kring);
				revents |= POLLERR;
			}
			/* now we can use kring->rcur, rtail */

			/*
			 * transparent mode support: collect packets from
			 * hw rxring(s) that have been released by the user
			 */
			if (nm_may_forward_up(kring)) {
				netmap_grab_packets(kring, &q, netmap_fwd);
			}

			/* Clear the NR_FORWARD flag anyway, it may be set by
			 * the nm_sync() below only on for the host RX ring (see
			 * netmap_rxsync_from_host()). */
			kring->nr_kflags &= ~NR_FORWARD;
			if (kring->nm_sync(kring, sync_flags))
				revents |= POLLERR;
			else
				nm_sync_finalize(kring);
			send_down |= (kring->nr_kflags & NR_FORWARD);
			ring_timestamp_set(ring);
			found = kring->rcur != kring->rtail;
			nm_kr_put(kring);
			if (found) {
				revents |= want_rx;
				retry_rx = 0;
#ifndef linux
				kring->nm_notify(kring, 0);
#endif /* linux */
			}
		}

#ifndef linux
		if (retry_rx && sr) {
			nm_os_selrecord(sr, si[NR_RX]);
		}
#endif /* !linux */
		if (send_down || retry_rx) {
			retry_rx = 0;
			if (send_down)
				goto flush_tx; /* and retry_rx */
			else
				goto do_retry_rx;
		}
	}

	/*
	 * Transparent mode: released bufs (i.e. between kring->nr_hwcur and
	 * ring->head) marked with NS_FORWARD on hw rx rings are passed up
	 * to the host stack.
	 */

	if (mbq_peek(&q)) {
		netmap_send_up(na->ifp, &q);
	}

	return (revents);
#undef want_tx
#undef want_rx
}

int
nma_intr_enable(struct netmap_adapter *na, int onoff)
{
	bool changed = false;
	enum txrx t;
	int i;

	for_rx_tx(t) {
		for (i = 0; i < nma_get_nrings(na, t); i++) {
			struct netmap_kring *kring = NMR(na, t)[i];
			int on = !(kring->nr_kflags & NKR_NOINTR);

			if (!!onoff != !!on) {
				changed = true;
			}
			if (onoff) {
				kring->nr_kflags &= ~NKR_NOINTR;
			} else {
				kring->nr_kflags |= NKR_NOINTR;
			}
		}
	}

	if (!changed) {
		return 0; /* nothing to do */
	}

	if (!na->nm_intr) {
		nm_prerr("Cannot %s interrupts for %s", onoff ? "enable" : "disable",
		  na->name);
		return -1;
	}

	na->nm_intr(na, onoff);

	return 0;
}


/*-------------------- driver support routines -------------------*/

/* default notify callback */
static int
netmap_notify(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->notify_na;
	enum txrx t = kring->tx;

	nm_os_selwakeup(&kring->si);
	/* optimization: avoid a wake up on the global
	 * queue if nobody has registered for more
	 * than one ring
	 */
	if (na->si_users[t] > 0)
		nm_os_selwakeup(&na->si[t]);

	return NM_IRQ_COMPLETED;
}

/* called by all routines that create netmap_adapters.
 * provide some defaults and get a reference to the
 * memory allocator
 */
int
netmap_attach_common(struct netmap_adapter *na)
{
	if (!na->rx_buf_maxsize) {
		/* Set a conservative default (larger is safer). */
		na->rx_buf_maxsize = PAGE_SIZE;
	}

#ifdef __FreeBSD__
	if (na->na_flags & NAF_HOST_RINGS && na->ifp) {
		na->if_input = na->ifp->if_input; /* for netmap_send_up */
	}
	na->pdev = na; /* make sure netmap_mem_map() is called */
#endif /* __FreeBSD__ */
	if (na->na_flags & NAF_HOST_RINGS) {
		if (na->num_host_rx_rings == 0)
			na->num_host_rx_rings = 1;
		if (na->num_host_tx_rings == 0)
			na->num_host_tx_rings = 1;
	}
	if (na->nm_krings_create == NULL) {
		/* we assume that we have been called by a driver,
		 * since other port types all provide their own
		 * nm_krings_create
		 */
		na->nm_krings_create = netmap_hw_krings_create;
		na->nm_krings_delete = netmap_hw_krings_delete;
	}
	if (na->nm_notify == NULL)
		na->nm_notify = netmap_notify;
	na->active_fds = 0;

	if (na->nm_mem == NULL) {
		/* use the global allocator */
		na->nm_mem = netmap_mem_get(&nm_mem);
	}
#ifdef WITH_VALE
	if (na->nm_bdg_attach == NULL)
		/* no special nm_bdg_attach callback. On VALE
		 * attach, we need to interpose a bwrap
		 */
		na->nm_bdg_attach = netmap_default_bdg_attach;
#endif

	return 0;
}

/* Wrapper for the register callback provided netmap-enabled
 * hardware drivers.
 * nm_iszombie(na) means that the driver module has been
 * unloaded, so we cannot call into it.
 * nm_os_ifnet_lock() must guarantee mutual exclusion with
 * module unloading.
 */
static int
netmap_hw_reg(struct netmap_adapter *na, int onoff)
{
	struct netmap_hw_adapter *hwna =
		(struct netmap_hw_adapter*)na;
	int error = 0;

	nm_os_ifnet_lock();

	if (nm_iszombie(na)) {
		if (onoff) {
			error = ENXIO;
		} else if (na != NULL) {
			na->na_flags &= ~NAF_NETMAP_ON;
		}
		goto out;
	}

	error = hwna->nm_hw_register(na, onoff);

out:
	nm_os_ifnet_unlock();

	return error;
}

static void
netmap_hw_dtor(struct netmap_adapter *na)
{
	if (na->ifp == NULL)
		return;

	NM_DETACH_NA(na->ifp);
}


/*
 * Allocate a netmap_adapter object, and initialize it from the
 * 'arg' passed by the driver on attach.
 * We allocate a block of memory of 'size' bytes, which has room
 * for struct netmap_adapter plus additional room private to
 * the caller.
 * Return 0 on success, ENOMEM otherwise.
 */
int
netmap_attach_ext(struct netmap_adapter *arg, size_t size, int override_reg)
{
	struct netmap_hw_adapter *hwna = NULL;
	struct ifnet *ifp = NULL;

	if (size < sizeof(struct netmap_hw_adapter)) {
		if (netmap_debug & NM_DEBUG_ON)
			nm_prerr("Invalid netmap adapter size %d", (int)size);
		return EINVAL;
	}

	if (arg == NULL || arg->ifp == NULL) {
		if (netmap_debug & NM_DEBUG_ON)
			nm_prerr("either arg or arg->ifp is NULL");
		return EINVAL;
	}

	if (arg->num_tx_rings == 0 || arg->num_rx_rings == 0) {
		if (netmap_debug & NM_DEBUG_ON)
			nm_prerr("%s: invalid rings tx %d rx %d",
				arg->name, arg->num_tx_rings, arg->num_rx_rings);
		return EINVAL;
	}

	ifp = arg->ifp;
	if (NM_NA_CLASH(ifp)) {
		/* If NA(ifp) is not null but there is no valid netmap
		 * adapter it means that someone else is using the same
		 * pointer (e.g. ax25_ptr on linux). This happens for
		 * instance when also PF_RING is in use. */
		nm_prerr("Error: netmap adapter hook is busy");
		return EBUSY;
	}

	hwna = nm_os_malloc(size);
	if (hwna == NULL)
		goto fail;
	hwna->up = *arg;
	hwna->up.na_flags |= NAF_HOST_RINGS | NAF_NATIVE;
	strlcpy(hwna->up.name, ifp->if_xname, sizeof(hwna->up.name));
	if (override_reg) {
		hwna->nm_hw_register = hwna->up.nm_register;
		hwna->up.nm_register = netmap_hw_reg;
	}
	if (netmap_attach_common(&hwna->up)) {
		nm_os_free(hwna);
		goto fail;
	}
	netmap_adapter_get(&hwna->up);

	NM_ATTACH_NA(ifp, &hwna->up);

	nm_os_onattach(ifp);

	if (arg->nm_dtor == NULL) {
		hwna->up.nm_dtor = netmap_hw_dtor;
	}

	if_printf(ifp, "netmap queues/slots: TX %d/%d, RX %d/%d\n",
	    hwna->up.num_tx_rings, hwna->up.num_tx_desc,
	    hwna->up.num_rx_rings, hwna->up.num_rx_desc);
	return 0;

fail:
	nm_prerr("fail, arg %p ifp %p na %p", arg, ifp, hwna);
	return (hwna ? EINVAL : ENOMEM);
}


int
netmap_attach(struct netmap_adapter *arg)
{
	return netmap_attach_ext(arg, sizeof(struct netmap_hw_adapter),
			1 /* override nm_reg */);
}


void
NM_DBG(netmap_adapter_get)(struct netmap_adapter *na)
{
	if (!na) {
		return;
	}

	refcount_acquire(&na->na_refcount);
}


/* returns 1 iff the netmap_adapter is destroyed */
int
NM_DBG(netmap_adapter_put)(struct netmap_adapter *na)
{
	if (!na)
		return 1;

	if (!refcount_release(&na->na_refcount))
		return 0;

	if (na->nm_dtor)
		na->nm_dtor(na);

	if (na->tx_rings) { /* XXX should not happen */
		if (netmap_debug & NM_DEBUG_ON)
			nm_prerr("freeing leftover tx_rings");
		na->nm_krings_delete(na);
	}
	netmap_pipe_dealloc(na);
	if (na->nm_mem)
		netmap_mem_put(na->nm_mem);
	bzero(na, sizeof(*na));
	nm_os_free(na);

	return 1;
}

/* nm_krings_create callback for all hardware native adapters */
int
netmap_hw_krings_create(struct netmap_adapter *na)
{
	int ret = netmap_krings_create(na, 0);
	if (ret == 0) {
		/* initialize the mbq for the sw rx ring */
		u_int lim = netmap_real_rings(na, NR_RX), i;
		for (i = na->num_rx_rings; i < lim; i++) {
			mbq_safe_init(&NMR(na, NR_RX)[i]->rx_queue);
		}
		nm_prdis("initialized sw rx queue %d", na->num_rx_rings);
	}
	return ret;
}



/*
 * Called on module unload by the netmap-enabled drivers
 */
void
netmap_detach(struct ifnet *ifp)
{
	struct netmap_adapter *na = NA(ifp);

	if (!na)
		return;

	NMG_LOCK();
	netmap_set_all_rings(na, NM_KR_LOCKED);
	/*
	 * if the netmap adapter is not native, somebody
	 * changed it, so we can not release it here.
	 * The NAF_ZOMBIE flag will notify the new owner that
	 * the driver is gone.
	 */
	if (!(na->na_flags & NAF_NATIVE) || !netmap_adapter_put(na)) {
		na->na_flags |= NAF_ZOMBIE;
	}
	/* give active users a chance to notice that NAF_ZOMBIE has been
	 * turned on, so that they can stop and return an error to userspace.
	 * Note that this becomes a NOP if there are no active users and,
	 * therefore, the put() above has deleted the na, since now NA(ifp) is
	 * NULL.
	 */
	netmap_enable_all_rings(ifp);
	NMG_UNLOCK();
}


/*
 * Intercept packets from the network stack and pass them
 * to netmap as incoming packets on the 'software' ring.
 *
 * We only store packets in a bounded mbq and then copy them
 * in the relevant rxsync routine.
 *
 * We rely on the OS to make sure that the ifp and na do not go
 * away (typically the caller checks for IFF_DRV_RUNNING or the like).
 * In nm_register() or whenever there is a reinitialization,
 * we make sure to make the mode change visible here.
 */
int
netmap_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct netmap_adapter *na = NA(ifp);
	struct netmap_kring *kring, *tx_kring;
	u_int len = MBUF_LEN(m);
	u_int error = ENOBUFS;
	unsigned int txr;
	struct mbq *q;
	int busy;
	u_int i;

	i = MBUF_TXQ(m);
	if (i >= na->num_host_rx_rings) {
		i = i % na->num_host_rx_rings;
	}
	kring = NMR(na, NR_RX)[nma_get_nrings(na, NR_RX) + i];

	// XXX [Linux] we do not need this lock
	// if we follow the down/configure/up protocol -gl
	// mtx_lock(&na->core_lock);

	if (!nm_netmap_on(na)) {
		nm_prerr("%s not in netmap mode anymore", na->name);
		error = ENXIO;
		goto done;
	}

	txr = MBUF_TXQ(m);
	if (txr >= na->num_tx_rings) {
		txr %= na->num_tx_rings;
	}
	tx_kring = NMR(na, NR_TX)[txr];

	if (tx_kring->nr_mode == NKR_NETMAP_OFF) {
		return MBUF_TRANSMIT(na, ifp, m);
	}

	q = &kring->rx_queue;

	// XXX reconsider long packets if we handle fragments
	if (len > NETMAP_BUF_SIZE(na)) { /* too long for us */
		nm_prerr("%s from_host, drop packet size %d > %d", na->name,
			len, NETMAP_BUF_SIZE(na));
		goto done;
	}

	if (!netmap_generic_hwcsum) {
		if (nm_os_mbuf_has_csum_offld(m)) {
			nm_prlim(1, "%s drop mbuf that needs checksum offload", na->name);
			goto done;
		}
	}

	if (nm_os_mbuf_has_seg_offld(m)) {
		nm_prlim(1, "%s drop mbuf that needs generic segmentation offload", na->name);
		goto done;
	}

#ifdef __FreeBSD__
	ETHER_BPF_MTAP(ifp, m);
#endif /* __FreeBSD__ */

	/* protect against netmap_rxsync_from_host(), netmap_sw_to_nic()
	 * and maybe other instances of netmap_transmit (the latter
	 * not possible on Linux).
	 * We enqueue the mbuf only if we are sure there is going to be
	 * enough room in the host RX ring, otherwise we drop it.
	 */
	mbq_lock(q);

	busy = kring->nr_hwtail - kring->nr_hwcur;
	if (busy < 0)
		busy += kring->nkr_num_slots;
	if (busy + mbq_len(q) >= kring->nkr_num_slots - 1) {
		nm_prlim(2, "%s full hwcur %d hwtail %d qlen %d", na->name,
			kring->nr_hwcur, kring->nr_hwtail, mbq_len(q));
	} else {
		mbq_enqueue(q, m);
		nm_prdis(2, "%s %d bufs in queue", na->name, mbq_len(q));
		/* notify outside the lock */
		m = NULL;
		error = 0;
	}
	mbq_unlock(q);

done:
	if (m)
		m_freem(m);
	/* unconditionally wake up listeners */
	kring->nm_notify(kring, 0);
	/* this is normally netmap_notify(), but for nics
	 * connected to a bridge it is netmap_bwrap_intr_notify(),
	 * that possibly forwards the frames through the switch
	 */

	return (error);
}


/*
 * netmap_reset() is called by the driver routines when reinitializing
 * a ring. The driver is in charge of locking to protect the kring.
 * If native netmap mode is not set just return NULL.
 * If native netmap mode is set, in particular, we have to set nr_mode to
 * NKR_NETMAP_ON.
 */
struct netmap_slot *
netmap_reset(struct netmap_adapter *na, enum txrx tx, u_int n,
	u_int new_cur)
{
	struct netmap_kring *kring;
	int new_hwofs, lim;

	if (!nm_native_on(na)) {
		nm_prdis("interface not in native netmap mode");
		return NULL;	/* nothing to reinitialize */
	}

	/* XXX note- in the new scheme, we are not guaranteed to be
	 * under lock (e.g. when called on a device reset).
	 * In this case, we should set a flag and do not trust too
	 * much the values. In practice: TODO
	 * - set a RESET flag somewhere in the kring
	 * - do the processing in a conservative way
	 * - let the *sync() fixup at the end.
	 */
	if (tx == NR_TX) {
		if (n >= na->num_tx_rings)
			return NULL;

		kring = na->tx_rings[n];

		if (kring->nr_pending_mode == NKR_NETMAP_OFF) {
			kring->nr_mode = NKR_NETMAP_OFF;
			return NULL;
		}

		// XXX check whether we should use hwcur or rcur
		new_hwofs = kring->nr_hwcur - new_cur;
	} else {
		if (n >= na->num_rx_rings)
			return NULL;
		kring = na->rx_rings[n];

		if (kring->nr_pending_mode == NKR_NETMAP_OFF) {
			kring->nr_mode = NKR_NETMAP_OFF;
			return NULL;
		}

		new_hwofs = kring->nr_hwtail - new_cur;
	}
	lim = kring->nkr_num_slots - 1;
	if (new_hwofs > lim)
		new_hwofs -= lim + 1;

	/* Always set the new offset value and realign the ring. */
	if (netmap_debug & NM_DEBUG_ON)
	    nm_prinf("%s %s%d hwofs %d -> %d, hwtail %d -> %d",
		na->name,
		tx == NR_TX ? "TX" : "RX", n,
		kring->nkr_hwofs, new_hwofs,
		kring->nr_hwtail,
		tx == NR_TX ? lim : kring->nr_hwtail);
	kring->nkr_hwofs = new_hwofs;
	if (tx == NR_TX) {
		kring->nr_hwtail = kring->nr_hwcur + lim;
		if (kring->nr_hwtail > lim)
			kring->nr_hwtail -= lim + 1;
	}

	/*
	 * Wakeup on the individual and global selwait
	 * We do the wakeup here, but the ring is not yet reconfigured.
	 * However, we are under lock so there are no races.
	 */
	kring->nr_mode = NKR_NETMAP_ON;
	kring->nm_notify(kring, 0);
	return kring->ring->slot;
}


/*
 * Dispatch rx/tx interrupts to the netmap rings.
 *
 * "work_done" is non-null on the RX path, NULL for the TX path.
 * We rely on the OS to make sure that there is only one active
 * instance per queue, and that there is appropriate locking.
 *
 * The 'notify' routine depends on what the ring is attached to.
 * - for a netmap file descriptor, do a selwakeup on the individual
 *   waitqueue, plus one on the global one if needed
 *   (see netmap_notify)
 * - for a nic connected to a switch, call the proper forwarding routine
 *   (see netmap_bwrap_intr_notify)
 */
int
netmap_common_irq(struct netmap_adapter *na, u_int q, u_int *work_done)
{
	struct netmap_kring *kring;
	enum txrx t = (work_done ? NR_RX : NR_TX);

	q &= NETMAP_RING_MASK;

	if (netmap_debug & (NM_DEBUG_RXINTR|NM_DEBUG_TXINTR)) {
	        nm_prlim(5, "received %s queue %d", work_done ? "RX" : "TX" , q);
	}

	if (q >= nma_get_nrings(na, t))
		return NM_IRQ_PASS; // not a physical queue

	kring = NMR(na, t)[q];

	if (kring->nr_mode == NKR_NETMAP_OFF) {
		return NM_IRQ_PASS;
	}

	if (t == NR_RX) {
		kring->nr_kflags |= NKR_PENDINTR;	// XXX atomic ?
		*work_done = 1; /* do not fire napi again */
	}

	return kring->nm_notify(kring, 0);
}


/*
 * Default functions to handle rx/tx interrupts from a physical device.
 * "work_done" is non-null on the RX path, NULL for the TX path.
 *
 * If the card is not in netmap mode, simply return NM_IRQ_PASS,
 * so that the caller proceeds with regular processing.
 * Otherwise call netmap_common_irq().
 *
 * If the card is connected to a netmap file descriptor,
 * do a selwakeup on the individual queue, plus one on the global one
 * if needed (multiqueue card _and_ there are multiqueue listeners),
 * and return NR_IRQ_COMPLETED.
 *
 * Finally, if called on rx from an interface connected to a switch,
 * calls the proper forwarding routine.
 */
int
netmap_rx_irq(struct ifnet *ifp, u_int q, u_int *work_done)
{
	struct netmap_adapter *na = NA(ifp);

	/*
	 * XXX emulated netmap mode sets NAF_SKIP_INTR so
	 * we still use the regular driver even though the previous
	 * check fails. It is unclear whether we should use
	 * nm_native_on() here.
	 */
	if (!nm_netmap_on(na))
		return NM_IRQ_PASS;

	if (na->na_flags & NAF_SKIP_INTR) {
		nm_prdis("use regular interrupt");
		return NM_IRQ_PASS;
	}

	return netmap_common_irq(na, q, work_done);
}

/* set/clear native flags and if_transmit/netdev_ops */
void
nm_set_native_flags(struct netmap_adapter *na)
{
	struct ifnet *ifp = na->ifp;

	/* We do the setup for intercepting packets only if we are the
	 * first user of this adapapter. */
	if (na->active_fds > 0) {
		return;
	}

	na->na_flags |= NAF_NETMAP_ON;
	nm_os_onenter(ifp);
	nm_update_hostrings_mode(na);
}

void
nm_clear_native_flags(struct netmap_adapter *na)
{
	struct ifnet *ifp = na->ifp;

	/* We undo the setup for intercepting packets only if we are the
	 * last user of this adapter. */
	if (na->active_fds > 0) {
		return;
	}

	nm_update_hostrings_mode(na);
	nm_os_onexit(ifp);

	na->na_flags &= ~NAF_NETMAP_ON;
}

void
netmap_krings_mode_commit(struct netmap_adapter *na, int onoff)
{
	enum txrx t;

	for_rx_tx(t) {
		int i;

		for (i = 0; i < netmap_real_rings(na, t); i++) {
			struct netmap_kring *kring = NMR(na, t)[i];

			if (onoff && nm_kring_pending_on(kring))
				kring->nr_mode = NKR_NETMAP_ON;
			else if (!onoff && nm_kring_pending_off(kring))
				kring->nr_mode = NKR_NETMAP_OFF;
		}
	}
}

/*
 * Module loader and unloader
 *
 * netmap_init() creates the /dev/netmap device and initializes
 * all global variables. Returns 0 on success, errno on failure
 * (but there is no chance)
 *
 * netmap_fini() destroys everything.
 */

static struct cdev *netmap_dev; /* /dev/netmap character device. */
extern struct cdevsw netmap_cdevsw;


void
netmap_fini(void)
{
	if (netmap_dev)
		destroy_dev(netmap_dev);
	/* we assume that there are no longer netmap users */
	nm_os_ifnet_fini();
	netmap_uninit_bridges();
	netmap_mem_fini();
	NMG_LOCK_DESTROY();
	nm_prinf("netmap: unloaded module.");
}


int
netmap_init(void)
{
	int error;

	NMG_LOCK_INIT();

	error = netmap_mem_init();
	if (error != 0)
		goto fail;
	/*
	 * MAKEDEV_ETERNAL_KLD avoids an expensive check on syscalls
	 * when the module is compiled in.
	 * XXX could use make_dev_credv() to get error number
	 */
	netmap_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD,
		&netmap_cdevsw, 0, NULL, UID_ROOT, GID_WHEEL, 0600,
			      "netmap");
	if (!netmap_dev)
		goto fail;

	error = netmap_init_bridges();
	if (error)
		goto fail;

#ifdef __FreeBSD__
	nm_os_vi_init_index();
#endif

	error = nm_os_ifnet_init();
	if (error)
		goto fail;

	nm_prinf("netmap: loaded module");
	return (0);
fail:
	netmap_fini();
	return (EINVAL); /* may be incorrect */
}
