/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ratelimit.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/priv.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/pciio.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>
#include <sys/firmware.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/if_vlan_var.h>
#ifdef RSS
#include <net/rss_config.h>
#endif
#include <netinet/in.h>
#include <netinet/ip.h>
#if defined(__i386__) || defined(__amd64__)
#include <machine/md_var.h>
#include <machine/cputypes.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#endif
#include <crypto/rijndael/rijndael.h>
#ifdef DDB
#include <ddb/ddb.h>
#include <ddb/db_lex.h>
#endif

#include "common/common.h"
#include "common/t4_msg.h"
#include "common/t4_regs.h"
#include "common/t4_regs_values.h"
#include "cudbg/cudbg.h"
#include "t4_clip.h"
#include "t4_ioctl.h"
#include "t4_l2t.h"
#include "t4_mp_ring.h"
#include "t4_if.h"
#include "t4_smt.h"

/* T4 bus driver interface */
static int t4_probe(device_t);
static int t4_attach(device_t);
static int t4_detach(device_t);
static int t4_child_location_str(device_t, device_t, char *, size_t);
static int t4_ready(device_t);
static int t4_read_port_device(device_t, int, device_t *);
static device_method_t t4_methods[] = {
	DEVMETHOD(device_probe,		t4_probe),
	DEVMETHOD(device_attach,	t4_attach),
	DEVMETHOD(device_detach,	t4_detach),

	DEVMETHOD(bus_child_location_str, t4_child_location_str),

	DEVMETHOD(t4_is_main_ready,	t4_ready),
	DEVMETHOD(t4_read_port_device,	t4_read_port_device),

	DEVMETHOD_END
};
static driver_t t4_driver = {
	"t4nex",
	t4_methods,
	sizeof(struct adapter)
};


/* T4 port (cxgbe) interface */
static int cxgbe_probe(device_t);
static int cxgbe_attach(device_t);
static int cxgbe_detach(device_t);
device_method_t cxgbe_methods[] = {
	DEVMETHOD(device_probe,		cxgbe_probe),
	DEVMETHOD(device_attach,	cxgbe_attach),
	DEVMETHOD(device_detach,	cxgbe_detach),
	{ 0, 0 }
};
static driver_t cxgbe_driver = {
	"cxgbe",
	cxgbe_methods,
	sizeof(struct port_info)
};

/* T4 VI (vcxgbe) interface */
static int vcxgbe_probe(device_t);
static int vcxgbe_attach(device_t);
static int vcxgbe_detach(device_t);
static device_method_t vcxgbe_methods[] = {
	DEVMETHOD(device_probe,		vcxgbe_probe),
	DEVMETHOD(device_attach,	vcxgbe_attach),
	DEVMETHOD(device_detach,	vcxgbe_detach),
	{ 0, 0 }
};
static driver_t vcxgbe_driver = {
	"vcxgbe",
	vcxgbe_methods,
	sizeof(struct vi_info)
};

static d_ioctl_t t4_ioctl;

static struct cdevsw t4_cdevsw = {
       .d_version = D_VERSION,
       .d_ioctl = t4_ioctl,
       .d_name = "t4nex",
};

/* T5 bus driver interface */
static int t5_probe(device_t);
static device_method_t t5_methods[] = {
	DEVMETHOD(device_probe,		t5_probe),
	DEVMETHOD(device_attach,	t4_attach),
	DEVMETHOD(device_detach,	t4_detach),

	DEVMETHOD(bus_child_location_str, t4_child_location_str),

	DEVMETHOD(t4_is_main_ready,	t4_ready),
	DEVMETHOD(t4_read_port_device,	t4_read_port_device),

	DEVMETHOD_END
};
static driver_t t5_driver = {
	"t5nex",
	t5_methods,
	sizeof(struct adapter)
};


/* T5 port (cxl) interface */
static driver_t cxl_driver = {
	"cxl",
	cxgbe_methods,
	sizeof(struct port_info)
};

/* T5 VI (vcxl) interface */
static driver_t vcxl_driver = {
	"vcxl",
	vcxgbe_methods,
	sizeof(struct vi_info)
};

/* T6 bus driver interface */
static int t6_probe(device_t);
static device_method_t t6_methods[] = {
	DEVMETHOD(device_probe,		t6_probe),
	DEVMETHOD(device_attach,	t4_attach),
	DEVMETHOD(device_detach,	t4_detach),

	DEVMETHOD(bus_child_location_str, t4_child_location_str),

	DEVMETHOD(t4_is_main_ready,	t4_ready),
	DEVMETHOD(t4_read_port_device,	t4_read_port_device),

	DEVMETHOD_END
};
static driver_t t6_driver = {
	"t6nex",
	t6_methods,
	sizeof(struct adapter)
};


/* T6 port (cc) interface */
static driver_t cc_driver = {
	"cc",
	cxgbe_methods,
	sizeof(struct port_info)
};

/* T6 VI (vcc) interface */
static driver_t vcc_driver = {
	"vcc",
	vcxgbe_methods,
	sizeof(struct vi_info)
};

/* ifnet interface */
static void cxgbe_init(void *);
static int cxgbe_ioctl(struct ifnet *, unsigned long, caddr_t);
static int cxgbe_transmit(struct ifnet *, struct mbuf *);
static void cxgbe_qflush(struct ifnet *);

MALLOC_DEFINE(M_CXGBE, "cxgbe", "Chelsio T4/T5 Ethernet driver and services");

/*
 * Correct lock order when you need to acquire multiple locks is t4_list_lock,
 * then ADAPTER_LOCK, then t4_uld_list_lock.
 */
static struct sx t4_list_lock;
SLIST_HEAD(, adapter) t4_list;
#ifdef TCP_OFFLOAD
static struct sx t4_uld_list_lock;
SLIST_HEAD(, uld_info) t4_uld_list;
#endif

/*
 * Tunables.  See tweak_tunables() too.
 *
 * Each tunable is set to a default value here if it's known at compile-time.
 * Otherwise it is set to -n as an indication to tweak_tunables() that it should
 * provide a reasonable default (upto n) when the driver is loaded.
 *
 * Tunables applicable to both T4 and T5 are under hw.cxgbe.  Those specific to
 * T5 are under hw.cxl.
 */
SYSCTL_NODE(_hw, OID_AUTO, cxgbe, CTLFLAG_RD, 0, "cxgbe(4) parameters");
SYSCTL_NODE(_hw, OID_AUTO, cxl, CTLFLAG_RD, 0, "cxgbe(4) T5+ parameters");
SYSCTL_NODE(_hw_cxgbe, OID_AUTO, toe, CTLFLAG_RD, 0, "cxgbe(4) TOE parameters");

/*
 * Number of queues for tx and rx, NIC and offload.
 */
#define NTXQ 16
int t4_ntxq = -NTXQ;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, ntxq, CTLFLAG_RDTUN, &t4_ntxq, 0,
    "Number of TX queues per port");
TUNABLE_INT("hw.cxgbe.ntxq10g", &t4_ntxq);	/* Old name, undocumented */

#define NRXQ 8
int t4_nrxq = -NRXQ;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nrxq, CTLFLAG_RDTUN, &t4_nrxq, 0,
    "Number of RX queues per port");
TUNABLE_INT("hw.cxgbe.nrxq10g", &t4_nrxq);	/* Old name, undocumented */

#define NTXQ_VI 1
static int t4_ntxq_vi = -NTXQ_VI;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, ntxq_vi, CTLFLAG_RDTUN, &t4_ntxq_vi, 0,
    "Number of TX queues per VI");

#define NRXQ_VI 1
static int t4_nrxq_vi = -NRXQ_VI;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nrxq_vi, CTLFLAG_RDTUN, &t4_nrxq_vi, 0,
    "Number of RX queues per VI");

static int t4_rsrv_noflowq = 0;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, rsrv_noflowq, CTLFLAG_RDTUN, &t4_rsrv_noflowq,
    0, "Reserve TX queue 0 of each VI for non-flowid packets");

#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
#define NOFLDTXQ 8
static int t4_nofldtxq = -NOFLDTXQ;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nofldtxq, CTLFLAG_RDTUN, &t4_nofldtxq, 0,
    "Number of offload TX queues per port");

#define NOFLDRXQ 2
static int t4_nofldrxq = -NOFLDRXQ;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nofldrxq, CTLFLAG_RDTUN, &t4_nofldrxq, 0,
    "Number of offload RX queues per port");

#define NOFLDTXQ_VI 1
static int t4_nofldtxq_vi = -NOFLDTXQ_VI;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nofldtxq_vi, CTLFLAG_RDTUN, &t4_nofldtxq_vi, 0,
    "Number of offload TX queues per VI");

#define NOFLDRXQ_VI 1
static int t4_nofldrxq_vi = -NOFLDRXQ_VI;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nofldrxq_vi, CTLFLAG_RDTUN, &t4_nofldrxq_vi, 0,
    "Number of offload RX queues per VI");

#define TMR_IDX_OFLD 1
int t4_tmr_idx_ofld = TMR_IDX_OFLD;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, holdoff_timer_idx_ofld, CTLFLAG_RDTUN,
    &t4_tmr_idx_ofld, 0, "Holdoff timer index for offload queues");

#define PKTC_IDX_OFLD (-1)
int t4_pktc_idx_ofld = PKTC_IDX_OFLD;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, holdoff_pktc_idx_ofld, CTLFLAG_RDTUN,
    &t4_pktc_idx_ofld, 0, "holdoff packet counter index for offload queues");

/* 0 means chip/fw default, non-zero number is value in microseconds */
static u_long t4_toe_keepalive_idle = 0;
SYSCTL_ULONG(_hw_cxgbe_toe, OID_AUTO, keepalive_idle, CTLFLAG_RDTUN,
    &t4_toe_keepalive_idle, 0, "TOE keepalive idle timer (us)");

/* 0 means chip/fw default, non-zero number is value in microseconds */
static u_long t4_toe_keepalive_interval = 0;
SYSCTL_ULONG(_hw_cxgbe_toe, OID_AUTO, keepalive_interval, CTLFLAG_RDTUN,
    &t4_toe_keepalive_interval, 0, "TOE keepalive interval timer (us)");

/* 0 means chip/fw default, non-zero number is # of keepalives before abort */
static int t4_toe_keepalive_count = 0;
SYSCTL_INT(_hw_cxgbe_toe, OID_AUTO, keepalive_count, CTLFLAG_RDTUN,
    &t4_toe_keepalive_count, 0, "Number of TOE keepalive probes before abort");

/* 0 means chip/fw default, non-zero number is value in microseconds */
static u_long t4_toe_rexmt_min = 0;
SYSCTL_ULONG(_hw_cxgbe_toe, OID_AUTO, rexmt_min, CTLFLAG_RDTUN,
    &t4_toe_rexmt_min, 0, "Minimum TOE retransmit interval (us)");

/* 0 means chip/fw default, non-zero number is value in microseconds */
static u_long t4_toe_rexmt_max = 0;
SYSCTL_ULONG(_hw_cxgbe_toe, OID_AUTO, rexmt_max, CTLFLAG_RDTUN,
    &t4_toe_rexmt_max, 0, "Maximum TOE retransmit interval (us)");

/* 0 means chip/fw default, non-zero number is # of rexmt before abort */
static int t4_toe_rexmt_count = 0;
SYSCTL_INT(_hw_cxgbe_toe, OID_AUTO, rexmt_count, CTLFLAG_RDTUN,
    &t4_toe_rexmt_count, 0, "Number of TOE retransmissions before abort");

/* -1 means chip/fw default, other values are raw backoff values to use */
static int t4_toe_rexmt_backoff[16] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};
SYSCTL_NODE(_hw_cxgbe_toe, OID_AUTO, rexmt_backoff, CTLFLAG_RD, 0,
    "cxgbe(4) TOE retransmit backoff values");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 0, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[0], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 1, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[1], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 2, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[2], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 3, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[3], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 4, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[4], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 5, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[5], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 6, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[6], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 7, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[7], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 8, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[8], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 9, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[9], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 10, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[10], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 11, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[11], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 12, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[12], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 13, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[13], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 14, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[14], 0, "");
SYSCTL_INT(_hw_cxgbe_toe_rexmt_backoff, OID_AUTO, 15, CTLFLAG_RDTUN,
    &t4_toe_rexmt_backoff[15], 0, "");
#endif

#ifdef DEV_NETMAP
#define NNMTXQ_VI 2
static int t4_nnmtxq_vi = -NNMTXQ_VI;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nnmtxq_vi, CTLFLAG_RDTUN, &t4_nnmtxq_vi, 0,
    "Number of netmap TX queues per VI");

#define NNMRXQ_VI 2
static int t4_nnmrxq_vi = -NNMRXQ_VI;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nnmrxq_vi, CTLFLAG_RDTUN, &t4_nnmrxq_vi, 0,
    "Number of netmap RX queues per VI");
#endif

/*
 * Holdoff parameters for ports.
 */
#define TMR_IDX 1
int t4_tmr_idx = TMR_IDX;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, holdoff_timer_idx, CTLFLAG_RDTUN, &t4_tmr_idx,
    0, "Holdoff timer index");
TUNABLE_INT("hw.cxgbe.holdoff_timer_idx_10G", &t4_tmr_idx);	/* Old name */

#define PKTC_IDX (-1)
int t4_pktc_idx = PKTC_IDX;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, holdoff_pktc_idx, CTLFLAG_RDTUN, &t4_pktc_idx,
    0, "Holdoff packet counter index");
TUNABLE_INT("hw.cxgbe.holdoff_pktc_idx_10G", &t4_pktc_idx);	/* Old name */

/*
 * Size (# of entries) of each tx and rx queue.
 */
unsigned int t4_qsize_txq = TX_EQ_QSIZE;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, qsize_txq, CTLFLAG_RDTUN, &t4_qsize_txq, 0,
    "Number of descriptors in each TX queue");

unsigned int t4_qsize_rxq = RX_IQ_QSIZE;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, qsize_rxq, CTLFLAG_RDTUN, &t4_qsize_rxq, 0,
    "Number of descriptors in each RX queue");

/*
 * Interrupt types allowed (bits 0, 1, 2 = INTx, MSI, MSI-X respectively).
 */
int t4_intr_types = INTR_MSIX | INTR_MSI | INTR_INTX;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, interrupt_types, CTLFLAG_RDTUN, &t4_intr_types,
    0, "Interrupt types allowed (bit 0 = INTx, 1 = MSI, 2 = MSI-X)");

/*
 * Configuration file.  All the _CF names here are special.
 */
#define DEFAULT_CF	"default"
#define BUILTIN_CF	"built-in"
#define FLASH_CF	"flash"
#define UWIRE_CF	"uwire"
#define FPGA_CF		"fpga"
static char t4_cfg_file[32] = DEFAULT_CF;
SYSCTL_STRING(_hw_cxgbe, OID_AUTO, config_file, CTLFLAG_RDTUN, t4_cfg_file,
    sizeof(t4_cfg_file), "Firmware configuration file");

/*
 * PAUSE settings (bit 0, 1, 2 = rx_pause, tx_pause, pause_autoneg respectively).
 * rx_pause = 1 to heed incoming PAUSE frames, 0 to ignore them.
 * tx_pause = 1 to emit PAUSE frames when the rx FIFO reaches its high water
 *            mark or when signalled to do so, 0 to never emit PAUSE.
 * pause_autoneg = 1 means PAUSE will be negotiated if possible and the
 *                 negotiated settings will override rx_pause/tx_pause.
 *                 Otherwise rx_pause/tx_pause are applied forcibly.
 */
static int t4_pause_settings = PAUSE_RX | PAUSE_TX | PAUSE_AUTONEG;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, pause_settings, CTLFLAG_RDTUN,
    &t4_pause_settings, 0,
    "PAUSE settings (bit 0 = rx_pause, 1 = tx_pause, 2 = pause_autoneg)");

/*
 * Forward Error Correction settings (bit 0, 1 = RS, BASER respectively).
 * -1 to run with the firmware default.  Same as FEC_AUTO (bit 5)
 *  0 to disable FEC.
 */
static int t4_fec = -1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, fec, CTLFLAG_RDTUN, &t4_fec, 0,
    "Forward Error Correction (bit 0 = RS, bit 1 = BASER_RS)");

/*
 * Link autonegotiation.
 * -1 to run with the firmware default.
 *  0 to disable.
 *  1 to enable.
 */
static int t4_autoneg = -1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, autoneg, CTLFLAG_RDTUN, &t4_autoneg, 0,
    "Link autonegotiation");

/*
 * Firmware auto-install by driver during attach (0, 1, 2 = prohibited, allowed,
 * encouraged respectively).  '-n' is the same as 'n' except the firmware
 * version used in the checks is read from the firmware bundled with the driver.
 */
static int t4_fw_install = 1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, fw_install, CTLFLAG_RDTUN, &t4_fw_install, 0,
    "Firmware auto-install (0 = prohibited, 1 = allowed, 2 = encouraged)");

/*
 * ASIC features that will be used.  Disable the ones you don't want so that the
 * chip resources aren't wasted on features that will not be used.
 */
static int t4_nbmcaps_allowed = 0;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, nbmcaps_allowed, CTLFLAG_RDTUN,
    &t4_nbmcaps_allowed, 0, "Default NBM capabilities");

static int t4_linkcaps_allowed = 0;	/* No DCBX, PPP, etc. by default */
SYSCTL_INT(_hw_cxgbe, OID_AUTO, linkcaps_allowed, CTLFLAG_RDTUN,
    &t4_linkcaps_allowed, 0, "Default link capabilities");

static int t4_switchcaps_allowed = FW_CAPS_CONFIG_SWITCH_INGRESS |
    FW_CAPS_CONFIG_SWITCH_EGRESS;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, switchcaps_allowed, CTLFLAG_RDTUN,
    &t4_switchcaps_allowed, 0, "Default switch capabilities");

#ifdef RATELIMIT
static int t4_niccaps_allowed = FW_CAPS_CONFIG_NIC |
	FW_CAPS_CONFIG_NIC_HASHFILTER | FW_CAPS_CONFIG_NIC_ETHOFLD;
#else
static int t4_niccaps_allowed = FW_CAPS_CONFIG_NIC |
	FW_CAPS_CONFIG_NIC_HASHFILTER;
#endif
SYSCTL_INT(_hw_cxgbe, OID_AUTO, niccaps_allowed, CTLFLAG_RDTUN,
    &t4_niccaps_allowed, 0, "Default NIC capabilities");

static int t4_toecaps_allowed = -1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, toecaps_allowed, CTLFLAG_RDTUN,
    &t4_toecaps_allowed, 0, "Default TCP offload capabilities");

static int t4_rdmacaps_allowed = -1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, rdmacaps_allowed, CTLFLAG_RDTUN,
    &t4_rdmacaps_allowed, 0, "Default RDMA capabilities");

static int t4_cryptocaps_allowed = -1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, cryptocaps_allowed, CTLFLAG_RDTUN,
    &t4_cryptocaps_allowed, 0, "Default crypto capabilities");

static int t4_iscsicaps_allowed = -1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, iscsicaps_allowed, CTLFLAG_RDTUN,
    &t4_iscsicaps_allowed, 0, "Default iSCSI capabilities");

static int t4_fcoecaps_allowed = 0;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, fcoecaps_allowed, CTLFLAG_RDTUN,
    &t4_fcoecaps_allowed, 0, "Default FCoE capabilities");

static int t5_write_combine = 0;
SYSCTL_INT(_hw_cxl, OID_AUTO, write_combine, CTLFLAG_RDTUN, &t5_write_combine,
    0, "Use WC instead of UC for BAR2");

static int t4_num_vis = 1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, num_vis, CTLFLAG_RDTUN, &t4_num_vis, 0,
    "Number of VIs per port");

/*
 * PCIe Relaxed Ordering.
 * -1: driver should figure out a good value.
 * 0: disable RO.
 * 1: enable RO.
 * 2: leave RO alone.
 */
static int pcie_relaxed_ordering = -1;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, pcie_relaxed_ordering, CTLFLAG_RDTUN,
    &pcie_relaxed_ordering, 0,
    "PCIe Relaxed Ordering: 0 = disable, 1 = enable, 2 = leave alone");

static int t4_panic_on_fatal_err = 0;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, panic_on_fatal_err, CTLFLAG_RDTUN,
    &t4_panic_on_fatal_err, 0, "panic on fatal errors");

#ifdef TCP_OFFLOAD
/*
 * TOE tunables.
 */
static int t4_cop_managed_offloading = 0;
SYSCTL_INT(_hw_cxgbe, OID_AUTO, cop_managed_offloading, CTLFLAG_RDTUN,
    &t4_cop_managed_offloading, 0,
    "COP (Connection Offload Policy) controls all TOE offload");
#endif

/* Functions used by VIs to obtain unique MAC addresses for each VI. */
static int vi_mac_funcs[] = {
	FW_VI_FUNC_ETH,
	FW_VI_FUNC_OFLD,
	FW_VI_FUNC_IWARP,
	FW_VI_FUNC_OPENISCSI,
	FW_VI_FUNC_OPENFCOE,
	FW_VI_FUNC_FOISCSI,
	FW_VI_FUNC_FOFCOE,
};

struct intrs_and_queues {
	uint16_t intr_type;	/* INTx, MSI, or MSI-X */
	uint16_t num_vis;	/* number of VIs for each port */
	uint16_t nirq;		/* Total # of vectors */
	uint16_t ntxq;		/* # of NIC txq's for each port */
	uint16_t nrxq;		/* # of NIC rxq's for each port */
	uint16_t nofldtxq;	/* # of TOE/ETHOFLD txq's for each port */
	uint16_t nofldrxq;	/* # of TOE rxq's for each port */

	/* The vcxgbe/vcxl interfaces use these and not the ones above. */
	uint16_t ntxq_vi;	/* # of NIC txq's */
	uint16_t nrxq_vi;	/* # of NIC rxq's */
	uint16_t nofldtxq_vi;	/* # of TOE txq's */
	uint16_t nofldrxq_vi;	/* # of TOE rxq's */
	uint16_t nnmtxq_vi;	/* # of netmap txq's */
	uint16_t nnmrxq_vi;	/* # of netmap rxq's */
};

static void setup_memwin(struct adapter *);
static void position_memwin(struct adapter *, int, uint32_t);
static int validate_mem_range(struct adapter *, uint32_t, uint32_t);
static int fwmtype_to_hwmtype(int);
static int validate_mt_off_len(struct adapter *, int, uint32_t, uint32_t,
    uint32_t *);
static int fixup_devlog_params(struct adapter *);
static int cfg_itype_and_nqueues(struct adapter *, struct intrs_and_queues *);
static int contact_firmware(struct adapter *);
static int partition_resources(struct adapter *);
static int get_params__pre_init(struct adapter *);
static int set_params__pre_init(struct adapter *);
static int get_params__post_init(struct adapter *);
static int set_params__post_init(struct adapter *);
static void t4_set_desc(struct adapter *);
static bool fixed_ifmedia(struct port_info *);
static void build_medialist(struct port_info *);
static void init_link_config(struct port_info *);
static int fixup_link_config(struct port_info *);
static int apply_link_config(struct port_info *);
static int cxgbe_init_synchronized(struct vi_info *);
static int cxgbe_uninit_synchronized(struct vi_info *);
static void quiesce_txq(struct adapter *, struct sge_txq *);
static void quiesce_wrq(struct adapter *, struct sge_wrq *);
static void quiesce_iq(struct adapter *, struct sge_iq *);
static void quiesce_fl(struct adapter *, struct sge_fl *);
static int t4_alloc_irq(struct adapter *, struct irq *, int rid,
    driver_intr_t *, void *, char *);
static int t4_free_irq(struct adapter *, struct irq *);
static void get_regs(struct adapter *, struct t4_regdump *, uint8_t *);
static void vi_refresh_stats(struct adapter *, struct vi_info *);
static void cxgbe_refresh_stats(struct adapter *, struct port_info *);
static void cxgbe_tick(void *);
static void cxgbe_sysctls(struct port_info *);
static int sysctl_int_array(SYSCTL_HANDLER_ARGS);
static int sysctl_bitfield_8b(SYSCTL_HANDLER_ARGS);
static int sysctl_bitfield_16b(SYSCTL_HANDLER_ARGS);
static int sysctl_btphy(SYSCTL_HANDLER_ARGS);
static int sysctl_noflowq(SYSCTL_HANDLER_ARGS);
static int sysctl_holdoff_tmr_idx(SYSCTL_HANDLER_ARGS);
static int sysctl_holdoff_pktc_idx(SYSCTL_HANDLER_ARGS);
static int sysctl_qsize_rxq(SYSCTL_HANDLER_ARGS);
static int sysctl_qsize_txq(SYSCTL_HANDLER_ARGS);
static int sysctl_pause_settings(SYSCTL_HANDLER_ARGS);
static int sysctl_fec(SYSCTL_HANDLER_ARGS);
static int sysctl_autoneg(SYSCTL_HANDLER_ARGS);
static int sysctl_handle_t4_reg64(SYSCTL_HANDLER_ARGS);
static int sysctl_temperature(SYSCTL_HANDLER_ARGS);
static int sysctl_loadavg(SYSCTL_HANDLER_ARGS);
static int sysctl_cctrl(SYSCTL_HANDLER_ARGS);
static int sysctl_cim_ibq_obq(SYSCTL_HANDLER_ARGS);
static int sysctl_cim_la(SYSCTL_HANDLER_ARGS);
static int sysctl_cim_ma_la(SYSCTL_HANDLER_ARGS);
static int sysctl_cim_pif_la(SYSCTL_HANDLER_ARGS);
static int sysctl_cim_qcfg(SYSCTL_HANDLER_ARGS);
static int sysctl_cpl_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_ddp_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_devlog(SYSCTL_HANDLER_ARGS);
static int sysctl_fcoe_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_hw_sched(SYSCTL_HANDLER_ARGS);
static int sysctl_lb_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_linkdnrc(SYSCTL_HANDLER_ARGS);
static int sysctl_meminfo(SYSCTL_HANDLER_ARGS);
static int sysctl_mps_tcam(SYSCTL_HANDLER_ARGS);
static int sysctl_mps_tcam_t6(SYSCTL_HANDLER_ARGS);
static int sysctl_path_mtus(SYSCTL_HANDLER_ARGS);
static int sysctl_pm_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_rdma_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_tcp_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_tids(SYSCTL_HANDLER_ARGS);
static int sysctl_tp_err_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_tp_la_mask(SYSCTL_HANDLER_ARGS);
static int sysctl_tp_la(SYSCTL_HANDLER_ARGS);
static int sysctl_tx_rate(SYSCTL_HANDLER_ARGS);
static int sysctl_ulprx_la(SYSCTL_HANDLER_ARGS);
static int sysctl_wcwr_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_cpus(SYSCTL_HANDLER_ARGS);
#ifdef TCP_OFFLOAD
static int sysctl_tls_rx_ports(SYSCTL_HANDLER_ARGS);
static int sysctl_tp_tick(SYSCTL_HANDLER_ARGS);
static int sysctl_tp_dack_timer(SYSCTL_HANDLER_ARGS);
static int sysctl_tp_timer(SYSCTL_HANDLER_ARGS);
static int sysctl_tp_shift_cnt(SYSCTL_HANDLER_ARGS);
static int sysctl_tp_backoff(SYSCTL_HANDLER_ARGS);
static int sysctl_holdoff_tmr_idx_ofld(SYSCTL_HANDLER_ARGS);
static int sysctl_holdoff_pktc_idx_ofld(SYSCTL_HANDLER_ARGS);
#endif
static int get_sge_context(struct adapter *, struct t4_sge_context *);
static int load_fw(struct adapter *, struct t4_data *);
static int load_cfg(struct adapter *, struct t4_data *);
static int load_boot(struct adapter *, struct t4_bootrom *);
static int load_bootcfg(struct adapter *, struct t4_data *);
static int cudbg_dump(struct adapter *, struct t4_cudbg_dump *);
static void free_offload_policy(struct t4_offload_policy *);
static int set_offload_policy(struct adapter *, struct t4_offload_policy *);
static int read_card_mem(struct adapter *, int, struct t4_mem_range *);
static int read_i2c(struct adapter *, struct t4_i2c_data *);
#ifdef TCP_OFFLOAD
static int toe_capability(struct vi_info *, int);
#endif
static int mod_event(module_t, int, void *);
static int notify_siblings(device_t, int);

struct {
	uint16_t device;
	char *desc;
} t4_pciids[] = {
	{0xa000, "Chelsio Terminator 4 FPGA"},
	{0x4400, "Chelsio T440-dbg"},
	{0x4401, "Chelsio T420-CR"},
	{0x4402, "Chelsio T422-CR"},
	{0x4403, "Chelsio T440-CR"},
	{0x4404, "Chelsio T420-BCH"},
	{0x4405, "Chelsio T440-BCH"},
	{0x4406, "Chelsio T440-CH"},
	{0x4407, "Chelsio T420-SO"},
	{0x4408, "Chelsio T420-CX"},
	{0x4409, "Chelsio T420-BT"},
	{0x440a, "Chelsio T404-BT"},
	{0x440e, "Chelsio T440-LP-CR"},
}, t5_pciids[] = {
	{0xb000, "Chelsio Terminator 5 FPGA"},
	{0x5400, "Chelsio T580-dbg"},
	{0x5401,  "Chelsio T520-CR"},		/* 2 x 10G */
	{0x5402,  "Chelsio T522-CR"},		/* 2 x 10G, 2 X 1G */
	{0x5403,  "Chelsio T540-CR"},		/* 4 x 10G */
	{0x5407,  "Chelsio T520-SO"},		/* 2 x 10G, nomem */
	{0x5409,  "Chelsio T520-BT"},		/* 2 x 10GBaseT */
	{0x540a,  "Chelsio T504-BT"},		/* 4 x 1G */
	{0x540d,  "Chelsio T580-CR"},		/* 2 x 40G */
	{0x540e,  "Chelsio T540-LP-CR"},	/* 4 x 10G */
	{0x5410,  "Chelsio T580-LP-CR"},	/* 2 x 40G */
	{0x5411,  "Chelsio T520-LL-CR"},	/* 2 x 10G */
	{0x5412,  "Chelsio T560-CR"},		/* 1 x 40G, 2 x 10G */
	{0x5414,  "Chelsio T580-LP-SO-CR"},	/* 2 x 40G, nomem */
	{0x5415,  "Chelsio T502-BT"},		/* 2 x 1G */
	{0x5418,  "Chelsio T540-BT"},		/* 4 x 10GBaseT */
	{0x5419,  "Chelsio T540-LP-BT"},	/* 4 x 10GBaseT */
	{0x541a,  "Chelsio T540-SO-BT"},	/* 4 x 10GBaseT, nomem */
	{0x541b,  "Chelsio T540-SO-CR"},	/* 4 x 10G, nomem */

	/* Custom */
	{0x5483, "Custom T540-CR"},
	{0x5484, "Custom T540-BT"},
}, t6_pciids[] = {
	{0xc006, "Chelsio Terminator 6 FPGA"},	/* T6 PE10K6 FPGA (PF0) */
	{0x6400, "Chelsio T6-DBG-25"},		/* 2 x 10/25G, debug */
	{0x6401, "Chelsio T6225-CR"},		/* 2 x 10/25G */
	{0x6402, "Chelsio T6225-SO-CR"},	/* 2 x 10/25G, nomem */
	{0x6403, "Chelsio T6425-CR"},		/* 4 x 10/25G */
	{0x6404, "Chelsio T6425-SO-CR"},	/* 4 x 10/25G, nomem */
	{0x6405, "Chelsio T6225-OCP-SO"},	/* 2 x 10/25G, nomem */
	{0x6406, "Chelsio T62100-OCP-SO"},	/* 2 x 40/50/100G, nomem */
	{0x6407, "Chelsio T62100-LP-CR"},	/* 2 x 40/50/100G */
	{0x6408, "Chelsio T62100-SO-CR"},	/* 2 x 40/50/100G, nomem */
	{0x6409, "Chelsio T6210-BT"},		/* 2 x 10GBASE-T */
	{0x640d, "Chelsio T62100-CR"},		/* 2 x 40/50/100G */
	{0x6410, "Chelsio T6-DBG-100"},		/* 2 x 40/50/100G, debug */
	{0x6411, "Chelsio T6225-LL-CR"},	/* 2 x 10/25G */
	{0x6414, "Chelsio T61100-OCP-SO"},	/* 1 x 40/50/100G, nomem */
	{0x6415, "Chelsio T6201-BT"},		/* 2 x 1000BASE-T */

	/* Custom */
	{0x6480, "Custom T6225-CR"},
	{0x6481, "Custom T62100-CR"},
	{0x6482, "Custom T6225-CR"},
	{0x6483, "Custom T62100-CR"},
	{0x6484, "Custom T64100-CR"},
	{0x6485, "Custom T6240-SO"},
	{0x6486, "Custom T6225-SO-CR"},
	{0x6487, "Custom T6225-CR"},
};

#ifdef TCP_OFFLOAD
/*
 * service_iq_fl() has an iq and needs the fl.  Offset of fl from the iq should
 * be exactly the same for both rxq and ofld_rxq.
 */
CTASSERT(offsetof(struct sge_ofld_rxq, iq) == offsetof(struct sge_rxq, iq));
CTASSERT(offsetof(struct sge_ofld_rxq, fl) == offsetof(struct sge_rxq, fl));
#endif
CTASSERT(sizeof(struct cluster_metadata) <= CL_METADATA_SIZE);

static int
t4_probe(device_t dev)
{
	int i;
	uint16_t v = pci_get_vendor(dev);
	uint16_t d = pci_get_device(dev);
	uint8_t f = pci_get_function(dev);

	if (v != PCI_VENDOR_ID_CHELSIO)
		return (ENXIO);

	/* Attach only to PF0 of the FPGA */
	if (d == 0xa000 && f != 0)
		return (ENXIO);

	for (i = 0; i < nitems(t4_pciids); i++) {
		if (d == t4_pciids[i].device) {
			device_set_desc(dev, t4_pciids[i].desc);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
t5_probe(device_t dev)
{
	int i;
	uint16_t v = pci_get_vendor(dev);
	uint16_t d = pci_get_device(dev);
	uint8_t f = pci_get_function(dev);

	if (v != PCI_VENDOR_ID_CHELSIO)
		return (ENXIO);

	/* Attach only to PF0 of the FPGA */
	if (d == 0xb000 && f != 0)
		return (ENXIO);

	for (i = 0; i < nitems(t5_pciids); i++) {
		if (d == t5_pciids[i].device) {
			device_set_desc(dev, t5_pciids[i].desc);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
t6_probe(device_t dev)
{
	int i;
	uint16_t v = pci_get_vendor(dev);
	uint16_t d = pci_get_device(dev);

	if (v != PCI_VENDOR_ID_CHELSIO)
		return (ENXIO);

	for (i = 0; i < nitems(t6_pciids); i++) {
		if (d == t6_pciids[i].device) {
			device_set_desc(dev, t6_pciids[i].desc);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static void
t5_attribute_workaround(device_t dev)
{
	device_t root_port;
	uint32_t v;

	/*
	 * The T5 chips do not properly echo the No Snoop and Relaxed
	 * Ordering attributes when replying to a TLP from a Root
	 * Port.  As a workaround, find the parent Root Port and
	 * disable No Snoop and Relaxed Ordering.  Note that this
	 * affects all devices under this root port.
	 */
	root_port = pci_find_pcie_root_port(dev);
	if (root_port == NULL) {
		device_printf(dev, "Unable to find parent root port\n");
		return;
	}

	v = pcie_adjust_config(root_port, PCIER_DEVICE_CTL,
	    PCIEM_CTL_RELAXED_ORD_ENABLE | PCIEM_CTL_NOSNOOP_ENABLE, 0, 2);
	if ((v & (PCIEM_CTL_RELAXED_ORD_ENABLE | PCIEM_CTL_NOSNOOP_ENABLE)) !=
	    0)
		device_printf(dev, "Disabled No Snoop/Relaxed Ordering on %s\n",
		    device_get_nameunit(root_port));
}

static const struct devnames devnames[] = {
	{
		.nexus_name = "t4nex",
		.ifnet_name = "cxgbe",
		.vi_ifnet_name = "vcxgbe",
		.pf03_drv_name = "t4iov",
		.vf_nexus_name = "t4vf",
		.vf_ifnet_name = "cxgbev"
	}, {
		.nexus_name = "t5nex",
		.ifnet_name = "cxl",
		.vi_ifnet_name = "vcxl",
		.pf03_drv_name = "t5iov",
		.vf_nexus_name = "t5vf",
		.vf_ifnet_name = "cxlv"
	}, {
		.nexus_name = "t6nex",
		.ifnet_name = "cc",
		.vi_ifnet_name = "vcc",
		.pf03_drv_name = "t6iov",
		.vf_nexus_name = "t6vf",
		.vf_ifnet_name = "ccv"
	}
};

void
t4_init_devnames(struct adapter *sc)
{
	int id;

	id = chip_id(sc);
	if (id >= CHELSIO_T4 && id - CHELSIO_T4 < nitems(devnames))
		sc->names = &devnames[id - CHELSIO_T4];
	else {
		device_printf(sc->dev, "chip id %d is not supported.\n", id);
		sc->names = NULL;
	}
}

static int
t4_ifnet_unit(struct adapter *sc, struct port_info *pi)
{
	const char *parent, *name;
	long value;
	int line, unit;

	line = 0;
	parent = device_get_nameunit(sc->dev);
	name = sc->names->ifnet_name;
	while (resource_find_dev(&line, name, &unit, "at", parent) == 0) {
		if (resource_long_value(name, unit, "port", &value) == 0 &&
		    value == pi->port_id)
			return (unit);
	}
	return (-1);
}

static int
t4_attach(device_t dev)
{
	struct adapter *sc;
	int rc = 0, i, j, rqidx, tqidx, nports;
	struct make_dev_args mda;
	struct intrs_and_queues iaq;
	struct sge *s;
	uint32_t *buf;
#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	int ofld_tqidx;
#endif
#ifdef TCP_OFFLOAD
	int ofld_rqidx;
#endif
#ifdef DEV_NETMAP
	int nm_rqidx, nm_tqidx;
#endif
	int num_vis;

	sc = device_get_softc(dev);
	sc->dev = dev;
	TUNABLE_INT_FETCH("hw.cxgbe.dflags", &sc->debug_flags);

	if ((pci_get_device(dev) & 0xff00) == 0x5400)
		t5_attribute_workaround(dev);
	pci_enable_busmaster(dev);
	if (pci_find_cap(dev, PCIY_EXPRESS, &i) == 0) {
		uint32_t v;

		pci_set_max_read_req(dev, 4096);
		v = pci_read_config(dev, i + PCIER_DEVICE_CTL, 2);
		sc->params.pci.mps = 128 << ((v & PCIEM_CTL_MAX_PAYLOAD) >> 5);
		if (pcie_relaxed_ordering == 0 &&
		    (v & PCIEM_CTL_RELAXED_ORD_ENABLE) != 0) {
			v &= ~PCIEM_CTL_RELAXED_ORD_ENABLE;
			pci_write_config(dev, i + PCIER_DEVICE_CTL, v, 2);
		} else if (pcie_relaxed_ordering == 1 &&
		    (v & PCIEM_CTL_RELAXED_ORD_ENABLE) == 0) {
			v |= PCIEM_CTL_RELAXED_ORD_ENABLE;
			pci_write_config(dev, i + PCIER_DEVICE_CTL, v, 2);
		}
	}

	sc->sge_gts_reg = MYPF_REG(A_SGE_PF_GTS);
	sc->sge_kdoorbell_reg = MYPF_REG(A_SGE_PF_KDOORBELL);
	sc->traceq = -1;
	mtx_init(&sc->ifp_lock, sc->ifp_lockname, 0, MTX_DEF);
	snprintf(sc->ifp_lockname, sizeof(sc->ifp_lockname), "%s tracer",
	    device_get_nameunit(dev));

	snprintf(sc->lockname, sizeof(sc->lockname), "%s",
	    device_get_nameunit(dev));
	mtx_init(&sc->sc_lock, sc->lockname, 0, MTX_DEF);
	t4_add_adapter(sc);

	mtx_init(&sc->sfl_lock, "starving freelists", 0, MTX_DEF);
	TAILQ_INIT(&sc->sfl);
	callout_init_mtx(&sc->sfl_callout, &sc->sfl_lock, 0);

	mtx_init(&sc->reg_lock, "indirect register access", 0, MTX_DEF);

	sc->policy = NULL;
	rw_init(&sc->policy_lock, "connection offload policy");

	rc = t4_map_bars_0_and_4(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	memset(sc->chan_map, 0xff, sizeof(sc->chan_map));

	/* Prepare the adapter for operation. */
	buf = malloc(PAGE_SIZE, M_CXGBE, M_ZERO | M_WAITOK);
	rc = -t4_prep_adapter(sc, buf);
	free(buf, M_CXGBE);
	if (rc != 0) {
		device_printf(dev, "failed to prepare adapter: %d.\n", rc);
		goto done;
	}

	/*
	 * This is the real PF# to which we're attaching.  Works from within PCI
	 * passthrough environments too, where pci_get_function() could return a
	 * different PF# depending on the passthrough configuration.  We need to
	 * use the real PF# in all our communication with the firmware.
	 */
	j = t4_read_reg(sc, A_PL_WHOAMI);
	sc->pf = chip_id(sc) <= CHELSIO_T5 ? G_SOURCEPF(j) : G_T6_SOURCEPF(j);
	sc->mbox = sc->pf;

	t4_init_devnames(sc);
	if (sc->names == NULL) {
		rc = ENOTSUP;
		goto done; /* error message displayed already */
	}

	/*
	 * Do this really early, with the memory windows set up even before the
	 * character device.  The userland tool's register i/o and mem read
	 * will work even in "recovery mode".
	 */
	setup_memwin(sc);
	if (t4_init_devlog_params(sc, 0) == 0)
		fixup_devlog_params(sc);
	make_dev_args_init(&mda);
	mda.mda_devsw = &t4_cdevsw;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_WHEEL;
	mda.mda_mode = 0600;
	mda.mda_si_drv1 = sc;
	rc = make_dev_s(&mda, &sc->cdev, "%s", device_get_nameunit(dev));
	if (rc != 0)
		device_printf(dev, "failed to create nexus char device: %d.\n",
		    rc);

	/* Go no further if recovery mode has been requested. */
	if (TUNABLE_INT_FETCH("hw.cxgbe.sos", &i) && i != 0) {
		device_printf(dev, "recovery mode.\n");
		goto done;
	}

#if defined(__i386__)
	if ((cpu_feature & CPUID_CX8) == 0) {
		device_printf(dev, "64 bit atomics not available.\n");
		rc = ENOTSUP;
		goto done;
	}
#endif

	/* Contact the firmware and try to become the master driver. */
	rc = contact_firmware(sc);
	if (rc != 0)
		goto done; /* error message displayed already */
	MPASS(sc->flags & FW_OK);

	rc = get_params__pre_init(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	if (sc->flags & MASTER_PF) {
		rc = partition_resources(sc);
		if (rc != 0)
			goto done; /* error message displayed already */
		t4_intr_clear(sc);
	}

	rc = get_params__post_init(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	rc = set_params__post_init(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	rc = t4_map_bar_2(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	rc = t4_create_dma_tag(sc);
	if (rc != 0)
		goto done; /* error message displayed already */

	/*
	 * First pass over all the ports - allocate VIs and initialize some
	 * basic parameters like mac address, port type, etc.
	 */
	for_each_port(sc, i) {
		struct port_info *pi;

		pi = malloc(sizeof(*pi), M_CXGBE, M_ZERO | M_WAITOK);
		sc->port[i] = pi;

		/* These must be set before t4_port_init */
		pi->adapter = sc;
		pi->port_id = i;
		/*
		 * XXX: vi[0] is special so we can't delay this allocation until
		 * pi->nvi's final value is known.
		 */
		pi->vi = malloc(sizeof(struct vi_info) * t4_num_vis, M_CXGBE,
		    M_ZERO | M_WAITOK);

		/*
		 * Allocate the "main" VI and initialize parameters
		 * like mac addr.
		 */
		rc = -t4_port_init(sc, sc->mbox, sc->pf, 0, i);
		if (rc != 0) {
			device_printf(dev, "unable to initialize port %d: %d\n",
			    i, rc);
			free(pi->vi, M_CXGBE);
			free(pi, M_CXGBE);
			sc->port[i] = NULL;
			goto done;
		}

		snprintf(pi->lockname, sizeof(pi->lockname), "%sp%d",
		    device_get_nameunit(dev), i);
		mtx_init(&pi->pi_lock, pi->lockname, 0, MTX_DEF);
		sc->chan_map[pi->tx_chan] = i;

		/* All VIs on this port share this media. */
		ifmedia_init(&pi->media, IFM_IMASK, cxgbe_media_change,
		    cxgbe_media_status);

		PORT_LOCK(pi);
		init_link_config(pi);
		fixup_link_config(pi);
		build_medialist(pi);
		if (fixed_ifmedia(pi))
			pi->flags |= FIXED_IFMEDIA;
		PORT_UNLOCK(pi);

		pi->dev = device_add_child(dev, sc->names->ifnet_name,
		    t4_ifnet_unit(sc, pi));
		if (pi->dev == NULL) {
			device_printf(dev,
			    "failed to add device for port %d.\n", i);
			rc = ENXIO;
			goto done;
		}
		pi->vi[0].dev = pi->dev;
		device_set_softc(pi->dev, pi);
	}

	/*
	 * Interrupt type, # of interrupts, # of rx/tx queues, etc.
	 */
	nports = sc->params.nports;
	rc = cfg_itype_and_nqueues(sc, &iaq);
	if (rc != 0)
		goto done; /* error message displayed already */

	num_vis = iaq.num_vis;
	sc->intr_type = iaq.intr_type;
	sc->intr_count = iaq.nirq;

	s = &sc->sge;
	s->nrxq = nports * iaq.nrxq;
	s->ntxq = nports * iaq.ntxq;
	if (num_vis > 1) {
		s->nrxq += nports * (num_vis - 1) * iaq.nrxq_vi;
		s->ntxq += nports * (num_vis - 1) * iaq.ntxq_vi;
	}
	s->neq = s->ntxq + s->nrxq;	/* the free list in an rxq is an eq */
	s->neq += nports;		/* ctrl queues: 1 per port */
	s->niq = s->nrxq + 1;		/* 1 extra for firmware event queue */
#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	if (is_offload(sc) || is_ethoffload(sc)) {
		s->nofldtxq = nports * iaq.nofldtxq;
		if (num_vis > 1)
			s->nofldtxq += nports * (num_vis - 1) * iaq.nofldtxq_vi;
		s->neq += s->nofldtxq;

		s->ofld_txq = malloc(s->nofldtxq * sizeof(struct sge_wrq),
		    M_CXGBE, M_ZERO | M_WAITOK);
	}
#endif
#ifdef TCP_OFFLOAD
	if (is_offload(sc)) {
		s->nofldrxq = nports * iaq.nofldrxq;
		if (num_vis > 1)
			s->nofldrxq += nports * (num_vis - 1) * iaq.nofldrxq_vi;
		s->neq += s->nofldrxq;	/* free list */
		s->niq += s->nofldrxq;

		s->ofld_rxq = malloc(s->nofldrxq * sizeof(struct sge_ofld_rxq),
		    M_CXGBE, M_ZERO | M_WAITOK);
	}
#endif
#ifdef DEV_NETMAP
	if (num_vis > 1) {
		s->nnmrxq = nports * (num_vis - 1) * iaq.nnmrxq_vi;
		s->nnmtxq = nports * (num_vis - 1) * iaq.nnmtxq_vi;
	}
	s->neq += s->nnmtxq + s->nnmrxq;
	s->niq += s->nnmrxq;

	s->nm_rxq = malloc(s->nnmrxq * sizeof(struct sge_nm_rxq),
	    M_CXGBE, M_ZERO | M_WAITOK);
	s->nm_txq = malloc(s->nnmtxq * sizeof(struct sge_nm_txq),
	    M_CXGBE, M_ZERO | M_WAITOK);
#endif

	s->ctrlq = malloc(nports * sizeof(struct sge_wrq), M_CXGBE,
	    M_ZERO | M_WAITOK);
	s->rxq = malloc(s->nrxq * sizeof(struct sge_rxq), M_CXGBE,
	    M_ZERO | M_WAITOK);
	s->txq = malloc(s->ntxq * sizeof(struct sge_txq), M_CXGBE,
	    M_ZERO | M_WAITOK);
	s->iqmap = malloc(s->niq * sizeof(struct sge_iq *), M_CXGBE,
	    M_ZERO | M_WAITOK);
	s->eqmap = malloc(s->neq * sizeof(struct sge_eq *), M_CXGBE,
	    M_ZERO | M_WAITOK);

	sc->irq = malloc(sc->intr_count * sizeof(struct irq), M_CXGBE,
	    M_ZERO | M_WAITOK);

	t4_init_l2t(sc, M_WAITOK);
	t4_init_smt(sc, M_WAITOK);
	t4_init_tx_sched(sc);
#ifdef RATELIMIT
	t4_init_etid_table(sc);
#endif
#ifdef INET6
	t4_init_clip_table(sc);
#endif
	if (sc->vres.key.size != 0)
		sc->key_map = vmem_create("T4TLS key map", sc->vres.key.start,
		    sc->vres.key.size, 32, 0, M_FIRSTFIT | M_WAITOK);

	/*
	 * Second pass over the ports.  This time we know the number of rx and
	 * tx queues that each port should get.
	 */
	rqidx = tqidx = 0;
#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	ofld_tqidx = 0;
#endif
#ifdef TCP_OFFLOAD
	ofld_rqidx = 0;
#endif
#ifdef DEV_NETMAP
	nm_rqidx = nm_tqidx = 0;
#endif
	for_each_port(sc, i) {
		struct port_info *pi = sc->port[i];
		struct vi_info *vi;

		if (pi == NULL)
			continue;

		pi->nvi = num_vis;
		for_each_vi(pi, j, vi) {
			vi->pi = pi;
			vi->qsize_rxq = t4_qsize_rxq;
			vi->qsize_txq = t4_qsize_txq;

			vi->first_rxq = rqidx;
			vi->first_txq = tqidx;
			vi->tmr_idx = t4_tmr_idx;
			vi->pktc_idx = t4_pktc_idx;
			vi->nrxq = j == 0 ? iaq.nrxq : iaq.nrxq_vi;
			vi->ntxq = j == 0 ? iaq.ntxq : iaq.ntxq_vi;

			rqidx += vi->nrxq;
			tqidx += vi->ntxq;

			if (j == 0 && vi->ntxq > 1)
				vi->rsrv_noflowq = t4_rsrv_noflowq ? 1 : 0;
			else
				vi->rsrv_noflowq = 0;

#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
			vi->first_ofld_txq = ofld_tqidx;
			vi->nofldtxq = j == 0 ? iaq.nofldtxq : iaq.nofldtxq_vi;
			ofld_tqidx += vi->nofldtxq;
#endif
#ifdef TCP_OFFLOAD
			vi->ofld_tmr_idx = t4_tmr_idx_ofld;
			vi->ofld_pktc_idx = t4_pktc_idx_ofld;
			vi->first_ofld_rxq = ofld_rqidx;
			vi->nofldrxq = j == 0 ? iaq.nofldrxq : iaq.nofldrxq_vi;

			ofld_rqidx += vi->nofldrxq;
#endif
#ifdef DEV_NETMAP
			if (j > 0) {
				vi->first_nm_rxq = nm_rqidx;
				vi->first_nm_txq = nm_tqidx;
				vi->nnmrxq = iaq.nnmrxq_vi;
				vi->nnmtxq = iaq.nnmtxq_vi;
				nm_rqidx += vi->nnmrxq;
				nm_tqidx += vi->nnmtxq;
			}
#endif
		}
	}

	rc = t4_setup_intr_handlers(sc);
	if (rc != 0) {
		device_printf(dev,
		    "failed to setup interrupt handlers: %d\n", rc);
		goto done;
	}

	rc = bus_generic_probe(dev);
	if (rc != 0) {
		device_printf(dev, "failed to probe child drivers: %d\n", rc);
		goto done;
	}

	/*
	 * Ensure thread-safe mailbox access (in debug builds).
	 *
	 * So far this was the only thread accessing the mailbox but various
	 * ifnets and sysctls are about to be created and their handlers/ioctls
	 * will access the mailbox from different threads.
	 */
	sc->flags |= CHK_MBOX_ACCESS;

	rc = bus_generic_attach(dev);
	if (rc != 0) {
		device_printf(dev,
		    "failed to attach all child ports: %d\n", rc);
		goto done;
	}

	device_printf(dev,
	    "PCIe gen%d x%d, %d ports, %d %s interrupt%s, %d eq, %d iq\n",
	    sc->params.pci.speed, sc->params.pci.width, sc->params.nports,
	    sc->intr_count, sc->intr_type == INTR_MSIX ? "MSI-X" :
	    (sc->intr_type == INTR_MSI ? "MSI" : "INTx"),
	    sc->intr_count > 1 ? "s" : "", sc->sge.neq, sc->sge.niq);

	t4_set_desc(sc);

	notify_siblings(dev, 0);

done:
	if (rc != 0 && sc->cdev) {
		/* cdev was created and so cxgbetool works; recover that way. */
		device_printf(dev,
		    "error during attach, adapter is now in recovery mode.\n");
		rc = 0;
	}

	if (rc != 0)
		t4_detach_common(dev);
	else
		t4_sysctls(sc);

	return (rc);
}

static int
t4_child_location_str(device_t bus, device_t dev, char *buf, size_t buflen)
{
	struct adapter *sc;
	struct port_info *pi;
	int i;

	sc = device_get_softc(bus);
	buf[0] = '\0';
	for_each_port(sc, i) {
		pi = sc->port[i];
		if (pi != NULL && pi->dev == dev) {
			snprintf(buf, buflen, "port=%d", pi->port_id);
			break;
		}
	}
	return (0);
}

static int
t4_ready(device_t dev)
{
	struct adapter *sc;

	sc = device_get_softc(dev);
	if (sc->flags & FW_OK)
		return (0);
	return (ENXIO);
}

static int
t4_read_port_device(device_t dev, int port, device_t *child)
{
	struct adapter *sc;
	struct port_info *pi;

	sc = device_get_softc(dev);
	if (port < 0 || port >= MAX_NPORTS)
		return (EINVAL);
	pi = sc->port[port];
	if (pi == NULL || pi->dev == NULL)
		return (ENXIO);
	*child = pi->dev;
	return (0);
}

static int
notify_siblings(device_t dev, int detaching)
{
	device_t sibling;
	int error, i;

	error = 0;
	for (i = 0; i < PCI_FUNCMAX; i++) {
		if (i == pci_get_function(dev))
			continue;
		sibling = pci_find_dbsf(pci_get_domain(dev), pci_get_bus(dev),
		    pci_get_slot(dev), i);
		if (sibling == NULL || !device_is_attached(sibling))
			continue;
		if (detaching)
			error = T4_DETACH_CHILD(sibling);
		else
			(void)T4_ATTACH_CHILD(sibling);
		if (error)
			break;
	}
	return (error);
}

/*
 * Idempotent
 */
static int
t4_detach(device_t dev)
{
	struct adapter *sc;
	int rc;

	sc = device_get_softc(dev);

	rc = notify_siblings(dev, 1);
	if (rc) {
		device_printf(dev,
		    "failed to detach sibling devices: %d\n", rc);
		return (rc);
	}

	return (t4_detach_common(dev));
}

int
t4_detach_common(device_t dev)
{
	struct adapter *sc;
	struct port_info *pi;
	int i, rc;

	sc = device_get_softc(dev);

	if (sc->cdev) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	sc->flags &= ~CHK_MBOX_ACCESS;
	if (sc->flags & FULL_INIT_DONE) {
		if (!(sc->flags & IS_VF))
			t4_intr_disable(sc);
	}

	if (device_is_attached(dev)) {
		rc = bus_generic_detach(dev);
		if (rc) {
			device_printf(dev,
			    "failed to detach child devices: %d\n", rc);
			return (rc);
		}
	}

	for (i = 0; i < sc->intr_count; i++)
		t4_free_irq(sc, &sc->irq[i]);

	if ((sc->flags & (IS_VF | FW_OK)) == FW_OK)
		t4_free_tx_sched(sc);

	for (i = 0; i < MAX_NPORTS; i++) {
		pi = sc->port[i];
		if (pi) {
			t4_free_vi(sc, sc->mbox, sc->pf, 0, pi->vi[0].viid);
			if (pi->dev)
				device_delete_child(dev, pi->dev);

			mtx_destroy(&pi->pi_lock);
			free(pi->vi, M_CXGBE);
			free(pi, M_CXGBE);
		}
	}

	device_delete_children(dev);

	if (sc->flags & FULL_INIT_DONE)
		adapter_full_uninit(sc);

	if ((sc->flags & (IS_VF | FW_OK)) == FW_OK)
		t4_fw_bye(sc, sc->mbox);

	if (sc->intr_type == INTR_MSI || sc->intr_type == INTR_MSIX)
		pci_release_msi(dev);

	if (sc->regs_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->regs_rid,
		    sc->regs_res);

	if (sc->udbs_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->udbs_rid,
		    sc->udbs_res);

	if (sc->msix_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->msix_rid,
		    sc->msix_res);

	if (sc->l2t)
		t4_free_l2t(sc->l2t);
	if (sc->smt)
		t4_free_smt(sc->smt);
#ifdef RATELIMIT
	t4_free_etid_table(sc);
#endif
	if (sc->key_map)
		vmem_destroy(sc->key_map);
#ifdef INET6
	t4_destroy_clip_table(sc);
#endif

#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	free(sc->sge.ofld_txq, M_CXGBE);
#endif
#ifdef TCP_OFFLOAD
	free(sc->sge.ofld_rxq, M_CXGBE);
#endif
#ifdef DEV_NETMAP
	free(sc->sge.nm_rxq, M_CXGBE);
	free(sc->sge.nm_txq, M_CXGBE);
#endif
	free(sc->irq, M_CXGBE);
	free(sc->sge.rxq, M_CXGBE);
	free(sc->sge.txq, M_CXGBE);
	free(sc->sge.ctrlq, M_CXGBE);
	free(sc->sge.iqmap, M_CXGBE);
	free(sc->sge.eqmap, M_CXGBE);
	free(sc->tids.ftid_tab, M_CXGBE);
	free(sc->tids.hpftid_tab, M_CXGBE);
	free_hftid_hash(&sc->tids);
	free(sc->tids.atid_tab, M_CXGBE);
	free(sc->tids.tid_tab, M_CXGBE);
	free(sc->tt.tls_rx_ports, M_CXGBE);
	t4_destroy_dma_tag(sc);
	if (mtx_initialized(&sc->sc_lock)) {
		sx_xlock(&t4_list_lock);
		SLIST_REMOVE(&t4_list, sc, adapter, link);
		sx_xunlock(&t4_list_lock);
		mtx_destroy(&sc->sc_lock);
	}

	callout_drain(&sc->sfl_callout);
	if (mtx_initialized(&sc->tids.ftid_lock)) {
		mtx_destroy(&sc->tids.ftid_lock);
		cv_destroy(&sc->tids.ftid_cv);
	}
	if (mtx_initialized(&sc->tids.atid_lock))
		mtx_destroy(&sc->tids.atid_lock);
	if (mtx_initialized(&sc->sfl_lock))
		mtx_destroy(&sc->sfl_lock);
	if (mtx_initialized(&sc->ifp_lock))
		mtx_destroy(&sc->ifp_lock);
	if (mtx_initialized(&sc->reg_lock))
		mtx_destroy(&sc->reg_lock);

	if (rw_initialized(&sc->policy_lock)) {
		rw_destroy(&sc->policy_lock);
#ifdef TCP_OFFLOAD
		if (sc->policy != NULL)
			free_offload_policy(sc->policy);
#endif
	}

	for (i = 0; i < NUM_MEMWIN; i++) {
		struct memwin *mw = &sc->memwin[i];

		if (rw_initialized(&mw->mw_lock))
			rw_destroy(&mw->mw_lock);
	}

	bzero(sc, sizeof(*sc));

	return (0);
}

static int
cxgbe_probe(device_t dev)
{
	char buf[128];
	struct port_info *pi = device_get_softc(dev);

	snprintf(buf, sizeof(buf), "port %d", pi->port_id);
	device_set_desc_copy(dev, buf);

	return (BUS_PROBE_DEFAULT);
}

#define T4_CAP (IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU | IFCAP_HWCSUM | \
    IFCAP_VLAN_HWCSUM | IFCAP_TSO | IFCAP_JUMBO_MTU | IFCAP_LRO | \
    IFCAP_VLAN_HWTSO | IFCAP_LINKSTATE | IFCAP_HWCSUM_IPV6 | IFCAP_HWSTATS | \
    IFCAP_HWRXTSTMP)
#define T4_CAP_ENABLE (T4_CAP)

static int
cxgbe_vi_attach(device_t dev, struct vi_info *vi)
{
	struct ifnet *ifp;
	struct sbuf *sb;

	vi->xact_addr_filt = -1;
	callout_init(&vi->tick, 1);

	/* Allocate an ifnet and set it up */
	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "Cannot allocate ifnet\n");
		return (ENOMEM);
	}
	vi->ifp = ifp;
	ifp->if_softc = vi;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	ifp->if_init = cxgbe_init;
	ifp->if_ioctl = cxgbe_ioctl;
	ifp->if_transmit = cxgbe_transmit;
	ifp->if_qflush = cxgbe_qflush;
	ifp->if_get_counter = cxgbe_get_counter;
#ifdef RATELIMIT
	ifp->if_snd_tag_alloc = cxgbe_snd_tag_alloc;
	ifp->if_snd_tag_modify = cxgbe_snd_tag_modify;
	ifp->if_snd_tag_query = cxgbe_snd_tag_query;
	ifp->if_snd_tag_free = cxgbe_snd_tag_free;
#endif

	ifp->if_capabilities = T4_CAP;
	ifp->if_capenable = T4_CAP_ENABLE;
#ifdef TCP_OFFLOAD
	if (vi->nofldrxq != 0)
		ifp->if_capabilities |= IFCAP_TOE;
#endif
#ifdef RATELIMIT
	if (is_ethoffload(vi->pi->adapter) && vi->nofldtxq != 0) {
		ifp->if_capabilities |= IFCAP_TXRTLMT;
		ifp->if_capenable |= IFCAP_TXRTLMT;
	}
#endif
	ifp->if_hwassist = CSUM_TCP | CSUM_UDP | CSUM_IP | CSUM_TSO |
	    CSUM_UDP_IPV6 | CSUM_TCP_IPV6;

	ifp->if_hw_tsomax = IP_MAXPACKET;
	ifp->if_hw_tsomaxsegcount = TX_SGL_SEGS_TSO;
#ifdef RATELIMIT
	if (is_ethoffload(vi->pi->adapter) && vi->nofldtxq != 0)
		ifp->if_hw_tsomaxsegcount = TX_SGL_SEGS_EO_TSO;
#endif
	ifp->if_hw_tsomaxsegsize = 65536;

	ether_ifattach(ifp, vi->hw_addr);
#ifdef DEV_NETMAP
	if (vi->nnmrxq != 0)
		cxgbe_nm_attach(vi);
#endif
	sb = sbuf_new_auto();
	sbuf_printf(sb, "%d txq, %d rxq (NIC)", vi->ntxq, vi->nrxq);
#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	switch (ifp->if_capabilities & (IFCAP_TOE | IFCAP_TXRTLMT)) {
	case IFCAP_TOE:
		sbuf_printf(sb, "; %d txq (TOE)", vi->nofldtxq);
		break;
	case IFCAP_TOE | IFCAP_TXRTLMT:
		sbuf_printf(sb, "; %d txq (TOE/ETHOFLD)", vi->nofldtxq);
		break;
	case IFCAP_TXRTLMT:
		sbuf_printf(sb, "; %d txq (ETHOFLD)", vi->nofldtxq);
		break;
	}
#endif
#ifdef TCP_OFFLOAD
	if (ifp->if_capabilities & IFCAP_TOE)
		sbuf_printf(sb, ", %d rxq (TOE)", vi->nofldrxq);
#endif
#ifdef DEV_NETMAP
	if (ifp->if_capabilities & IFCAP_NETMAP)
		sbuf_printf(sb, "; %d txq, %d rxq (netmap)",
		    vi->nnmtxq, vi->nnmrxq);
#endif
	sbuf_finish(sb);
	device_printf(dev, "%s\n", sbuf_data(sb));
	sbuf_delete(sb);

	vi_sysctls(vi);

	return (0);
}

static int
cxgbe_attach(device_t dev)
{
	struct port_info *pi = device_get_softc(dev);
	struct adapter *sc = pi->adapter;
	struct vi_info *vi;
	int i, rc;

	callout_init_mtx(&pi->tick, &pi->pi_lock, 0);

	rc = cxgbe_vi_attach(dev, &pi->vi[0]);
	if (rc)
		return (rc);

	for_each_vi(pi, i, vi) {
		if (i == 0)
			continue;
		vi->dev = device_add_child(dev, sc->names->vi_ifnet_name, -1);
		if (vi->dev == NULL) {
			device_printf(dev, "failed to add VI %d\n", i);
			continue;
		}
		device_set_softc(vi->dev, vi);
	}

	cxgbe_sysctls(pi);

	bus_generic_attach(dev);

	return (0);
}

static void
cxgbe_vi_detach(struct vi_info *vi)
{
	struct ifnet *ifp = vi->ifp;

	ether_ifdetach(ifp);

	/* Let detach proceed even if these fail. */
#ifdef DEV_NETMAP
	if (ifp->if_capabilities & IFCAP_NETMAP)
		cxgbe_nm_detach(vi);
#endif
	cxgbe_uninit_synchronized(vi);
	callout_drain(&vi->tick);
	vi_full_uninit(vi);

	if_free(vi->ifp);
	vi->ifp = NULL;
}

static int
cxgbe_detach(device_t dev)
{
	struct port_info *pi = device_get_softc(dev);
	struct adapter *sc = pi->adapter;
	int rc;

	/* Detach the extra VIs first. */
	rc = bus_generic_detach(dev);
	if (rc)
		return (rc);
	device_delete_children(dev);

	doom_vi(sc, &pi->vi[0]);

	if (pi->flags & HAS_TRACEQ) {
		sc->traceq = -1;	/* cloner should not create ifnet */
		t4_tracer_port_detach(sc);
	}

	cxgbe_vi_detach(&pi->vi[0]);
	callout_drain(&pi->tick);
	ifmedia_removeall(&pi->media);

	end_synchronized_op(sc, 0);

	return (0);
}

static void
cxgbe_init(void *arg)
{
	struct vi_info *vi = arg;
	struct adapter *sc = vi->pi->adapter;

	if (begin_synchronized_op(sc, vi, SLEEP_OK | INTR_OK, "t4init") != 0)
		return;
	cxgbe_init_synchronized(vi);
	end_synchronized_op(sc, 0);
}

static int
cxgbe_ioctl(struct ifnet *ifp, unsigned long cmd, caddr_t data)
{
	int rc = 0, mtu, flags;
	struct vi_info *vi = ifp->if_softc;
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	struct ifreq *ifr = (struct ifreq *)data;
	uint32_t mask;

	switch (cmd) {
	case SIOCSIFMTU:
		mtu = ifr->ifr_mtu;
		if (mtu < ETHERMIN || mtu > MAX_MTU)
			return (EINVAL);

		rc = begin_synchronized_op(sc, vi, SLEEP_OK | INTR_OK, "t4mtu");
		if (rc)
			return (rc);
		ifp->if_mtu = mtu;
		if (vi->flags & VI_INIT_DONE) {
			t4_update_fl_bufsize(ifp);
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				rc = update_mac_settings(ifp, XGMAC_MTU);
		}
		end_synchronized_op(sc, 0);
		break;

	case SIOCSIFFLAGS:
		rc = begin_synchronized_op(sc, vi, SLEEP_OK | INTR_OK, "t4flg");
		if (rc)
			return (rc);

		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				flags = vi->if_flags;
				if ((ifp->if_flags ^ flags) &
				    (IFF_PROMISC | IFF_ALLMULTI)) {
					rc = update_mac_settings(ifp,
					    XGMAC_PROMISC | XGMAC_ALLMULTI);
				}
			} else {
				rc = cxgbe_init_synchronized(vi);
			}
			vi->if_flags = ifp->if_flags;
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			rc = cxgbe_uninit_synchronized(vi);
		}
		end_synchronized_op(sc, 0);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		rc = begin_synchronized_op(sc, vi, SLEEP_OK | INTR_OK, "t4multi");
		if (rc)
			return (rc);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			rc = update_mac_settings(ifp, XGMAC_MCADDRS);
		end_synchronized_op(sc, 0);
		break;

	case SIOCSIFCAP:
		rc = begin_synchronized_op(sc, vi, SLEEP_OK | INTR_OK, "t4cap");
		if (rc)
			return (rc);

		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_TXCSUM) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			ifp->if_hwassist ^= (CSUM_TCP | CSUM_UDP | CSUM_IP);

			if (IFCAP_TSO4 & ifp->if_capenable &&
			    !(IFCAP_TXCSUM & ifp->if_capenable)) {
				ifp->if_capenable &= ~IFCAP_TSO4;
				if_printf(ifp,
				    "tso4 disabled due to -txcsum.\n");
			}
		}
		if (mask & IFCAP_TXCSUM_IPV6) {
			ifp->if_capenable ^= IFCAP_TXCSUM_IPV6;
			ifp->if_hwassist ^= (CSUM_UDP_IPV6 | CSUM_TCP_IPV6);

			if (IFCAP_TSO6 & ifp->if_capenable &&
			    !(IFCAP_TXCSUM_IPV6 & ifp->if_capenable)) {
				ifp->if_capenable &= ~IFCAP_TSO6;
				if_printf(ifp,
				    "tso6 disabled due to -txcsum6.\n");
			}
		}
		if (mask & IFCAP_RXCSUM)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		if (mask & IFCAP_RXCSUM_IPV6)
			ifp->if_capenable ^= IFCAP_RXCSUM_IPV6;

		/*
		 * Note that we leave CSUM_TSO alone (it is always set).  The
		 * kernel takes both IFCAP_TSOx and CSUM_TSO into account before
		 * sending a TSO request our way, so it's sufficient to toggle
		 * IFCAP_TSOx only.
		 */
		if (mask & IFCAP_TSO4) {
			if (!(IFCAP_TSO4 & ifp->if_capenable) &&
			    !(IFCAP_TXCSUM & ifp->if_capenable)) {
				if_printf(ifp, "enable txcsum first.\n");
				rc = EAGAIN;
				goto fail;
			}
			ifp->if_capenable ^= IFCAP_TSO4;
		}
		if (mask & IFCAP_TSO6) {
			if (!(IFCAP_TSO6 & ifp->if_capenable) &&
			    !(IFCAP_TXCSUM_IPV6 & ifp->if_capenable)) {
				if_printf(ifp, "enable txcsum6 first.\n");
				rc = EAGAIN;
				goto fail;
			}
			ifp->if_capenable ^= IFCAP_TSO6;
		}
		if (mask & IFCAP_LRO) {
#if defined(INET) || defined(INET6)
			int i;
			struct sge_rxq *rxq;

			ifp->if_capenable ^= IFCAP_LRO;
			for_each_rxq(vi, i, rxq) {
				if (ifp->if_capenable & IFCAP_LRO)
					rxq->iq.flags |= IQ_LRO_ENABLED;
				else
					rxq->iq.flags &= ~IQ_LRO_ENABLED;
			}
#endif
		}
#ifdef TCP_OFFLOAD
		if (mask & IFCAP_TOE) {
			int enable = (ifp->if_capenable ^ mask) & IFCAP_TOE;

			rc = toe_capability(vi, enable);
			if (rc != 0)
				goto fail;

			ifp->if_capenable ^= mask;
		}
#endif
		if (mask & IFCAP_VLAN_HWTAGGING) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				rc = update_mac_settings(ifp, XGMAC_VLANEX);
		}
		if (mask & IFCAP_VLAN_MTU) {
			ifp->if_capenable ^= IFCAP_VLAN_MTU;

			/* Need to find out how to disable auto-mtu-inflation */
		}
		if (mask & IFCAP_VLAN_HWTSO)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
		if (mask & IFCAP_VLAN_HWCSUM)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
#ifdef RATELIMIT
		if (mask & IFCAP_TXRTLMT)
			ifp->if_capenable ^= IFCAP_TXRTLMT;
#endif
		if (mask & IFCAP_HWRXTSTMP) {
			int i;
			struct sge_rxq *rxq;

			ifp->if_capenable ^= IFCAP_HWRXTSTMP;
			for_each_rxq(vi, i, rxq) {
				if (ifp->if_capenable & IFCAP_HWRXTSTMP)
					rxq->iq.flags |= IQ_RX_TIMESTAMP;
				else
					rxq->iq.flags &= ~IQ_RX_TIMESTAMP;
			}
		}

#ifdef VLAN_CAPABILITIES
		VLAN_CAPABILITIES(ifp);
#endif
fail:
		end_synchronized_op(sc, 0);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
	case SIOCGIFXMEDIA:
		ifmedia_ioctl(ifp, ifr, &pi->media, cmd);
		break;

	case SIOCGI2C: {
		struct ifi2creq i2c;

		rc = copyin(ifr_data_get_ptr(ifr), &i2c, sizeof(i2c));
		if (rc != 0)
			break;
		if (i2c.dev_addr != 0xA0 && i2c.dev_addr != 0xA2) {
			rc = EPERM;
			break;
		}
		if (i2c.len > sizeof(i2c.data)) {
			rc = EINVAL;
			break;
		}
		rc = begin_synchronized_op(sc, vi, SLEEP_OK | INTR_OK, "t4i2c");
		if (rc)
			return (rc);
		rc = -t4_i2c_rd(sc, sc->mbox, pi->port_id, i2c.dev_addr,
		    i2c.offset, i2c.len, &i2c.data[0]);
		end_synchronized_op(sc, 0);
		if (rc == 0)
			rc = copyout(&i2c, ifr_data_get_ptr(ifr), sizeof(i2c));
		break;
	}

	default:
		rc = ether_ioctl(ifp, cmd, data);
	}

	return (rc);
}

static int
cxgbe_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct vi_info *vi = ifp->if_softc;
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	struct sge_txq *txq;
	void *items[1];
	int rc;

	M_ASSERTPKTHDR(m);
	MPASS(m->m_nextpkt == NULL);	/* not quite ready for this yet */

	if (__predict_false(pi->link_cfg.link_ok == false)) {
		m_freem(m);
		return (ENETDOWN);
	}

	rc = parse_pkt(sc, &m);
	if (__predict_false(rc != 0)) {
		MPASS(m == NULL);			/* was freed already */
		atomic_add_int(&pi->tx_parse_error, 1);	/* rare, atomic is ok */
		return (rc);
	}
#ifdef RATELIMIT
	if (m->m_pkthdr.snd_tag != NULL) {
		/* EAGAIN tells the stack we are not the correct interface. */
		if (__predict_false(ifp != m->m_pkthdr.snd_tag->ifp)) {
			m_freem(m);
			return (EAGAIN);
		}

		return (ethofld_transmit(ifp, m));
	}
#endif

	/* Select a txq. */
	txq = &sc->sge.txq[vi->first_txq];
	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
		txq += ((m->m_pkthdr.flowid % (vi->ntxq - vi->rsrv_noflowq)) +
		    vi->rsrv_noflowq);

	items[0] = m;
	rc = mp_ring_enqueue(txq->r, items, 1, 4096);
	if (__predict_false(rc != 0))
		m_freem(m);

	return (rc);
}

static void
cxgbe_qflush(struct ifnet *ifp)
{
	struct vi_info *vi = ifp->if_softc;
	struct sge_txq *txq;
	int i;

	/* queues do not exist if !VI_INIT_DONE. */
	if (vi->flags & VI_INIT_DONE) {
		for_each_txq(vi, i, txq) {
			TXQ_LOCK(txq);
			txq->eq.flags |= EQ_QFLUSH;
			TXQ_UNLOCK(txq);
			while (!mp_ring_is_idle(txq->r)) {
				mp_ring_check_drainage(txq->r, 0);
				pause("qflush", 1);
			}
			TXQ_LOCK(txq);
			txq->eq.flags &= ~EQ_QFLUSH;
			TXQ_UNLOCK(txq);
		}
	}
	if_qflush(ifp);
}

static uint64_t
vi_get_counter(struct ifnet *ifp, ift_counter c)
{
	struct vi_info *vi = ifp->if_softc;
	struct fw_vi_stats_vf *s = &vi->stats;

	vi_refresh_stats(vi->pi->adapter, vi);

	switch (c) {
	case IFCOUNTER_IPACKETS:
		return (s->rx_bcast_frames + s->rx_mcast_frames +
		    s->rx_ucast_frames);
	case IFCOUNTER_IERRORS:
		return (s->rx_err_frames);
	case IFCOUNTER_OPACKETS:
		return (s->tx_bcast_frames + s->tx_mcast_frames +
		    s->tx_ucast_frames + s->tx_offload_frames);
	case IFCOUNTER_OERRORS:
		return (s->tx_drop_frames);
	case IFCOUNTER_IBYTES:
		return (s->rx_bcast_bytes + s->rx_mcast_bytes +
		    s->rx_ucast_bytes);
	case IFCOUNTER_OBYTES:
		return (s->tx_bcast_bytes + s->tx_mcast_bytes +
		    s->tx_ucast_bytes + s->tx_offload_bytes);
	case IFCOUNTER_IMCASTS:
		return (s->rx_mcast_frames);
	case IFCOUNTER_OMCASTS:
		return (s->tx_mcast_frames);
	case IFCOUNTER_OQDROPS: {
		uint64_t drops;

		drops = 0;
		if (vi->flags & VI_INIT_DONE) {
			int i;
			struct sge_txq *txq;

			for_each_txq(vi, i, txq)
				drops += counter_u64_fetch(txq->r->drops);
		}

		return (drops);

	}

	default:
		return (if_get_counter_default(ifp, c));
	}
}

uint64_t
cxgbe_get_counter(struct ifnet *ifp, ift_counter c)
{
	struct vi_info *vi = ifp->if_softc;
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	struct port_stats *s = &pi->stats;

	if (pi->nvi > 1 || sc->flags & IS_VF)
		return (vi_get_counter(ifp, c));

	cxgbe_refresh_stats(sc, pi);

	switch (c) {
	case IFCOUNTER_IPACKETS:
		return (s->rx_frames);

	case IFCOUNTER_IERRORS:
		return (s->rx_jabber + s->rx_runt + s->rx_too_long +
		    s->rx_fcs_err + s->rx_len_err);

	case IFCOUNTER_OPACKETS:
		return (s->tx_frames);

	case IFCOUNTER_OERRORS:
		return (s->tx_error_frames);

	case IFCOUNTER_IBYTES:
		return (s->rx_octets);

	case IFCOUNTER_OBYTES:
		return (s->tx_octets);

	case IFCOUNTER_IMCASTS:
		return (s->rx_mcast_frames);

	case IFCOUNTER_OMCASTS:
		return (s->tx_mcast_frames);

	case IFCOUNTER_IQDROPS:
		return (s->rx_ovflow0 + s->rx_ovflow1 + s->rx_ovflow2 +
		    s->rx_ovflow3 + s->rx_trunc0 + s->rx_trunc1 + s->rx_trunc2 +
		    s->rx_trunc3 + pi->tnl_cong_drops);

	case IFCOUNTER_OQDROPS: {
		uint64_t drops;

		drops = s->tx_drop;
		if (vi->flags & VI_INIT_DONE) {
			int i;
			struct sge_txq *txq;

			for_each_txq(vi, i, txq)
				drops += counter_u64_fetch(txq->r->drops);
		}

		return (drops);

	}

	default:
		return (if_get_counter_default(ifp, c));
	}
}

/*
 * The kernel picks a media from the list we had provided but we still validate
 * the requeste.
 */
int
cxgbe_media_change(struct ifnet *ifp)
{
	struct vi_info *vi = ifp->if_softc;
	struct port_info *pi = vi->pi;
	struct ifmedia *ifm = &pi->media;
	struct link_config *lc = &pi->link_cfg;
	struct adapter *sc = pi->adapter;
	int rc;

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4mec");
	if (rc != 0)
		return (rc);
	PORT_LOCK(pi);
	if (IFM_SUBTYPE(ifm->ifm_media) == IFM_AUTO) {
		/* ifconfig .. media autoselect */
		if (!(lc->supported & FW_PORT_CAP32_ANEG)) {
			rc = ENOTSUP; /* AN not supported by transceiver */
			goto done;
		}
		lc->requested_aneg = AUTONEG_ENABLE;
		lc->requested_speed = 0;
		lc->requested_fc |= PAUSE_AUTONEG;
	} else {
		lc->requested_aneg = AUTONEG_DISABLE;
		lc->requested_speed =
		    ifmedia_baudrate(ifm->ifm_media) / 1000000;
		lc->requested_fc = 0;
		if (IFM_OPTIONS(ifm->ifm_media) & IFM_ETH_RXPAUSE)
			lc->requested_fc |= PAUSE_RX;
		if (IFM_OPTIONS(ifm->ifm_media) & IFM_ETH_TXPAUSE)
			lc->requested_fc |= PAUSE_TX;
	}
	if (pi->up_vis > 0) {
		fixup_link_config(pi);
		rc = apply_link_config(pi);
	}
done:
	PORT_UNLOCK(pi);
	end_synchronized_op(sc, 0);
	return (rc);
}

/*
 * Base media word (without ETHER, pause, link active, etc.) for the port at the
 * given speed.
 */
static int
port_mword(struct port_info *pi, uint32_t speed)
{

	MPASS(speed & M_FW_PORT_CAP32_SPEED);
	MPASS(powerof2(speed));

	switch(pi->port_type) {
	case FW_PORT_TYPE_BT_SGMII:
	case FW_PORT_TYPE_BT_XFI:
	case FW_PORT_TYPE_BT_XAUI:
		/* BaseT */
		switch (speed) {
		case FW_PORT_CAP32_SPEED_100M:
			return (IFM_100_T);
		case FW_PORT_CAP32_SPEED_1G:
			return (IFM_1000_T);
		case FW_PORT_CAP32_SPEED_10G:
			return (IFM_10G_T);
		}
		break;
	case FW_PORT_TYPE_KX4:
		if (speed == FW_PORT_CAP32_SPEED_10G)
			return (IFM_10G_KX4);
		break;
	case FW_PORT_TYPE_CX4:
		if (speed == FW_PORT_CAP32_SPEED_10G)
			return (IFM_10G_CX4);
		break;
	case FW_PORT_TYPE_KX:
		if (speed == FW_PORT_CAP32_SPEED_1G)
			return (IFM_1000_KX);
		break;
	case FW_PORT_TYPE_KR:
	case FW_PORT_TYPE_BP_AP:
	case FW_PORT_TYPE_BP4_AP:
	case FW_PORT_TYPE_BP40_BA:
	case FW_PORT_TYPE_KR4_100G:
	case FW_PORT_TYPE_KR_SFP28:
	case FW_PORT_TYPE_KR_XLAUI:
		switch (speed) {
		case FW_PORT_CAP32_SPEED_1G:
			return (IFM_1000_KX);
		case FW_PORT_CAP32_SPEED_10G:
			return (IFM_10G_KR);
		case FW_PORT_CAP32_SPEED_25G:
			return (IFM_25G_KR);
		case FW_PORT_CAP32_SPEED_40G:
			return (IFM_40G_KR4);
		case FW_PORT_CAP32_SPEED_50G:
			return (IFM_50G_KR2);
		case FW_PORT_CAP32_SPEED_100G:
			return (IFM_100G_KR4);
		}
		break;
	case FW_PORT_TYPE_FIBER_XFI:
	case FW_PORT_TYPE_FIBER_XAUI:
	case FW_PORT_TYPE_SFP:
	case FW_PORT_TYPE_QSFP_10G:
	case FW_PORT_TYPE_QSA:
	case FW_PORT_TYPE_QSFP:
	case FW_PORT_TYPE_CR4_QSFP:
	case FW_PORT_TYPE_CR_QSFP:
	case FW_PORT_TYPE_CR2_QSFP:
	case FW_PORT_TYPE_SFP28:
		/* Pluggable transceiver */
		switch (pi->mod_type) {
		case FW_PORT_MOD_TYPE_LR:
			switch (speed) {
			case FW_PORT_CAP32_SPEED_1G:
				return (IFM_1000_LX);
			case FW_PORT_CAP32_SPEED_10G:
				return (IFM_10G_LR);
			case FW_PORT_CAP32_SPEED_25G:
				return (IFM_25G_LR);
			case FW_PORT_CAP32_SPEED_40G:
				return (IFM_40G_LR4);
			case FW_PORT_CAP32_SPEED_50G:
				return (IFM_50G_LR2);
			case FW_PORT_CAP32_SPEED_100G:
				return (IFM_100G_LR4);
			}
			break;
		case FW_PORT_MOD_TYPE_SR:
			switch (speed) {
			case FW_PORT_CAP32_SPEED_1G:
				return (IFM_1000_SX);
			case FW_PORT_CAP32_SPEED_10G:
				return (IFM_10G_SR);
			case FW_PORT_CAP32_SPEED_25G:
				return (IFM_25G_SR);
			case FW_PORT_CAP32_SPEED_40G:
				return (IFM_40G_SR4);
			case FW_PORT_CAP32_SPEED_50G:
				return (IFM_50G_SR2);
			case FW_PORT_CAP32_SPEED_100G:
				return (IFM_100G_SR4);
			}
			break;
		case FW_PORT_MOD_TYPE_ER:
			if (speed == FW_PORT_CAP32_SPEED_10G)
				return (IFM_10G_ER);
			break;
		case FW_PORT_MOD_TYPE_TWINAX_PASSIVE:
		case FW_PORT_MOD_TYPE_TWINAX_ACTIVE:
			switch (speed) {
			case FW_PORT_CAP32_SPEED_1G:
				return (IFM_1000_CX);
			case FW_PORT_CAP32_SPEED_10G:
				return (IFM_10G_TWINAX);
			case FW_PORT_CAP32_SPEED_25G:
				return (IFM_25G_CR);
			case FW_PORT_CAP32_SPEED_40G:
				return (IFM_40G_CR4);
			case FW_PORT_CAP32_SPEED_50G:
				return (IFM_50G_CR2);
			case FW_PORT_CAP32_SPEED_100G:
				return (IFM_100G_CR4);
			}
			break;
		case FW_PORT_MOD_TYPE_LRM:
			if (speed == FW_PORT_CAP32_SPEED_10G)
				return (IFM_10G_LRM);
			break;
		case FW_PORT_MOD_TYPE_NA:
			MPASS(0);	/* Not pluggable? */
			/* fall throough */
		case FW_PORT_MOD_TYPE_ERROR:
		case FW_PORT_MOD_TYPE_UNKNOWN:
		case FW_PORT_MOD_TYPE_NOTSUPPORTED:
			break;
		case FW_PORT_MOD_TYPE_NONE:
			return (IFM_NONE);
		}
		break;
	case FW_PORT_TYPE_NONE:
		return (IFM_NONE);
	}

	return (IFM_UNKNOWN);
}

void
cxgbe_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vi_info *vi = ifp->if_softc;
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	struct link_config *lc = &pi->link_cfg;

	if (begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4med") != 0)
		return;
	PORT_LOCK(pi);

	if (pi->up_vis == 0) {
		/*
		 * If all the interfaces are administratively down the firmware
		 * does not report transceiver changes.  Refresh port info here
		 * so that ifconfig displays accurate ifmedia at all times.
		 * This is the only reason we have a synchronized op in this
		 * function.  Just PORT_LOCK would have been enough otherwise.
		 */
		t4_update_port_info(pi);
		build_medialist(pi);
	}

	/* ifm_status */
	ifmr->ifm_status = IFM_AVALID;
	if (lc->link_ok == false)
		goto done;
	ifmr->ifm_status |= IFM_ACTIVE;

	/* ifm_active */
	ifmr->ifm_active = IFM_ETHER | IFM_FDX;
	ifmr->ifm_active &= ~(IFM_ETH_TXPAUSE | IFM_ETH_RXPAUSE);
	if (lc->fc & PAUSE_RX)
		ifmr->ifm_active |= IFM_ETH_RXPAUSE;
	if (lc->fc & PAUSE_TX)
		ifmr->ifm_active |= IFM_ETH_TXPAUSE;
	ifmr->ifm_active |= port_mword(pi, speed_to_fwcap(lc->speed));
done:
	PORT_UNLOCK(pi);
	end_synchronized_op(sc, 0);
}

static int
vcxgbe_probe(device_t dev)
{
	char buf[128];
	struct vi_info *vi = device_get_softc(dev);

	snprintf(buf, sizeof(buf), "port %d vi %td", vi->pi->port_id,
	    vi - vi->pi->vi);
	device_set_desc_copy(dev, buf);

	return (BUS_PROBE_DEFAULT);
}

static int
alloc_extra_vi(struct adapter *sc, struct port_info *pi, struct vi_info *vi)
{
	int func, index, rc;
	uint32_t param, val;

	ASSERT_SYNCHRONIZED_OP(sc);

	index = vi - pi->vi;
	MPASS(index > 0);	/* This function deals with _extra_ VIs only */
	KASSERT(index < nitems(vi_mac_funcs),
	    ("%s: VI %s doesn't have a MAC func", __func__,
	    device_get_nameunit(vi->dev)));
	func = vi_mac_funcs[index];
	rc = t4_alloc_vi_func(sc, sc->mbox, pi->tx_chan, sc->pf, 0, 1,
	    vi->hw_addr, &vi->rss_size, &vi->vfvld, &vi->vin, func, 0);
	if (rc < 0) {
		device_printf(vi->dev, "failed to allocate virtual interface %d"
		    "for port %d: %d\n", index, pi->port_id, -rc);
		return (-rc);
	}
	vi->viid = rc;

	if (vi->rss_size == 1) {
		/*
		 * This VI didn't get a slice of the RSS table.  Reduce the
		 * number of VIs being created (hw.cxgbe.num_vis) or modify the
		 * configuration file (nvi, rssnvi for this PF) if this is a
		 * problem.
		 */
		device_printf(vi->dev, "RSS table not available.\n");
		vi->rss_base = 0xffff;

		return (0);
	}

	param = V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) |
	    V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_RSSINFO) |
	    V_FW_PARAMS_PARAM_YZ(vi->viid);
	rc = t4_query_params(sc, sc->mbox, sc->pf, 0, 1, &param, &val);
	if (rc)
		vi->rss_base = 0xffff;
	else {
		MPASS((val >> 16) == vi->rss_size);
		vi->rss_base = val & 0xffff;
	}

	return (0);
}

static int
vcxgbe_attach(device_t dev)
{
	struct vi_info *vi;
	struct port_info *pi;
	struct adapter *sc;
	int rc;

	vi = device_get_softc(dev);
	pi = vi->pi;
	sc = pi->adapter;

	rc = begin_synchronized_op(sc, vi, SLEEP_OK | INTR_OK, "t4via");
	if (rc)
		return (rc);
	rc = alloc_extra_vi(sc, pi, vi);
	end_synchronized_op(sc, 0);
	if (rc)
		return (rc);

	rc = cxgbe_vi_attach(dev, vi);
	if (rc) {
		t4_free_vi(sc, sc->mbox, sc->pf, 0, vi->viid);
		return (rc);
	}
	return (0);
}

static int
vcxgbe_detach(device_t dev)
{
	struct vi_info *vi;
	struct adapter *sc;

	vi = device_get_softc(dev);
	sc = vi->pi->adapter;

	doom_vi(sc, vi);

	cxgbe_vi_detach(vi);
	t4_free_vi(sc, sc->mbox, sc->pf, 0, vi->viid);

	end_synchronized_op(sc, 0);

	return (0);
}

static struct callout fatal_callout;

static void
delayed_panic(void *arg)
{
	struct adapter *sc = arg;

	panic("%s: panic on fatal error", device_get_nameunit(sc->dev));
}

void
t4_fatal_err(struct adapter *sc, bool fw_error)
{

	t4_shutdown_adapter(sc);
	log(LOG_ALERT, "%s: encountered fatal error, adapter stopped.\n",
	    device_get_nameunit(sc->dev));
	if (fw_error) {
		ASSERT_SYNCHRONIZED_OP(sc);
		sc->flags |= ADAP_ERR;
	} else {
		ADAPTER_LOCK(sc);
		sc->flags |= ADAP_ERR;
		ADAPTER_UNLOCK(sc);
	}

	if (t4_panic_on_fatal_err) {
		log(LOG_ALERT, "%s: panic on fatal error after 30s",
		    device_get_nameunit(sc->dev));
		callout_reset(&fatal_callout, hz * 30, delayed_panic, sc);
	}
}

void
t4_add_adapter(struct adapter *sc)
{
	sx_xlock(&t4_list_lock);
	SLIST_INSERT_HEAD(&t4_list, sc, link);
	sx_xunlock(&t4_list_lock);
}

int
t4_map_bars_0_and_4(struct adapter *sc)
{
	sc->regs_rid = PCIR_BAR(0);
	sc->regs_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &sc->regs_rid, RF_ACTIVE);
	if (sc->regs_res == NULL) {
		device_printf(sc->dev, "cannot map registers.\n");
		return (ENXIO);
	}
	sc->bt = rman_get_bustag(sc->regs_res);
	sc->bh = rman_get_bushandle(sc->regs_res);
	sc->mmio_len = rman_get_size(sc->regs_res);
	setbit(&sc->doorbells, DOORBELL_KDB);

	sc->msix_rid = PCIR_BAR(4);
	sc->msix_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &sc->msix_rid, RF_ACTIVE);
	if (sc->msix_res == NULL) {
		device_printf(sc->dev, "cannot map MSI-X BAR.\n");
		return (ENXIO);
	}

	return (0);
}

int
t4_map_bar_2(struct adapter *sc)
{

	/*
	 * T4: only iWARP driver uses the userspace doorbells.  There is no need
	 * to map it if RDMA is disabled.
	 */
	if (is_t4(sc) && sc->rdmacaps == 0)
		return (0);

	sc->udbs_rid = PCIR_BAR(2);
	sc->udbs_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &sc->udbs_rid, RF_ACTIVE);
	if (sc->udbs_res == NULL) {
		device_printf(sc->dev, "cannot map doorbell BAR.\n");
		return (ENXIO);
	}
	sc->udbs_base = rman_get_virtual(sc->udbs_res);

	if (chip_id(sc) >= CHELSIO_T5) {
		setbit(&sc->doorbells, DOORBELL_UDB);
#if defined(__i386__) || defined(__amd64__)
		if (t5_write_combine) {
			int rc, mode;

			/*
			 * Enable write combining on BAR2.  This is the
			 * userspace doorbell BAR and is split into 128B
			 * (UDBS_SEG_SIZE) doorbell regions, each associated
			 * with an egress queue.  The first 64B has the doorbell
			 * and the second 64B can be used to submit a tx work
			 * request with an implicit doorbell.
			 */

			rc = pmap_change_attr((vm_offset_t)sc->udbs_base,
			    rman_get_size(sc->udbs_res), PAT_WRITE_COMBINING);
			if (rc == 0) {
				clrbit(&sc->doorbells, DOORBELL_UDB);
				setbit(&sc->doorbells, DOORBELL_WCWR);
				setbit(&sc->doorbells, DOORBELL_UDBWC);
			} else {
				device_printf(sc->dev,
				    "couldn't enable write combining: %d\n",
				    rc);
			}

			mode = is_t5(sc) ? V_STATMODE(0) : V_T6_STATMODE(0);
			t4_write_reg(sc, A_SGE_STAT_CFG,
			    V_STATSOURCE_T5(7) | mode);
		}
#endif
	}
	sc->iwt.wc_en = isset(&sc->doorbells, DOORBELL_UDBWC) ? 1 : 0;

	return (0);
}

struct memwin_init {
	uint32_t base;
	uint32_t aperture;
};

static const struct memwin_init t4_memwin[NUM_MEMWIN] = {
	{ MEMWIN0_BASE, MEMWIN0_APERTURE },
	{ MEMWIN1_BASE, MEMWIN1_APERTURE },
	{ MEMWIN2_BASE_T4, MEMWIN2_APERTURE_T4 }
};

static const struct memwin_init t5_memwin[NUM_MEMWIN] = {
	{ MEMWIN0_BASE, MEMWIN0_APERTURE },
	{ MEMWIN1_BASE, MEMWIN1_APERTURE },
	{ MEMWIN2_BASE_T5, MEMWIN2_APERTURE_T5 },
};

static void
setup_memwin(struct adapter *sc)
{
	const struct memwin_init *mw_init;
	struct memwin *mw;
	int i;
	uint32_t bar0;

	if (is_t4(sc)) {
		/*
		 * Read low 32b of bar0 indirectly via the hardware backdoor
		 * mechanism.  Works from within PCI passthrough environments
		 * too, where rman_get_start() can return a different value.  We
		 * need to program the T4 memory window decoders with the actual
		 * addresses that will be coming across the PCIe link.
		 */
		bar0 = t4_hw_pci_read_cfg4(sc, PCIR_BAR(0));
		bar0 &= (uint32_t) PCIM_BAR_MEM_BASE;

		mw_init = &t4_memwin[0];
	} else {
		/* T5+ use the relative offset inside the PCIe BAR */
		bar0 = 0;

		mw_init = &t5_memwin[0];
	}

	for (i = 0, mw = &sc->memwin[0]; i < NUM_MEMWIN; i++, mw_init++, mw++) {
		rw_init(&mw->mw_lock, "memory window access");
		mw->mw_base = mw_init->base;
		mw->mw_aperture = mw_init->aperture;
		mw->mw_curpos = 0;
		t4_write_reg(sc,
		    PCIE_MEM_ACCESS_REG(A_PCIE_MEM_ACCESS_BASE_WIN, i),
		    (mw->mw_base + bar0) | V_BIR(0) |
		    V_WINDOW(ilog2(mw->mw_aperture) - 10));
		rw_wlock(&mw->mw_lock);
		position_memwin(sc, i, 0);
		rw_wunlock(&mw->mw_lock);
	}

	/* flush */
	t4_read_reg(sc, PCIE_MEM_ACCESS_REG(A_PCIE_MEM_ACCESS_BASE_WIN, 2));
}

/*
 * Positions the memory window at the given address in the card's address space.
 * There are some alignment requirements and the actual position may be at an
 * address prior to the requested address.  mw->mw_curpos always has the actual
 * position of the window.
 */
static void
position_memwin(struct adapter *sc, int idx, uint32_t addr)
{
	struct memwin *mw;
	uint32_t pf;
	uint32_t reg;

	MPASS(idx >= 0 && idx < NUM_MEMWIN);
	mw = &sc->memwin[idx];
	rw_assert(&mw->mw_lock, RA_WLOCKED);

	if (is_t4(sc)) {
		pf = 0;
		mw->mw_curpos = addr & ~0xf;	/* start must be 16B aligned */
	} else {
		pf = V_PFNUM(sc->pf);
		mw->mw_curpos = addr & ~0x7f;	/* start must be 128B aligned */
	}
	reg = PCIE_MEM_ACCESS_REG(A_PCIE_MEM_ACCESS_OFFSET, idx);
	t4_write_reg(sc, reg, mw->mw_curpos | pf);
	t4_read_reg(sc, reg);	/* flush */
}

int
rw_via_memwin(struct adapter *sc, int idx, uint32_t addr, uint32_t *val,
    int len, int rw)
{
	struct memwin *mw;
	uint32_t mw_end, v;

	MPASS(idx >= 0 && idx < NUM_MEMWIN);

	/* Memory can only be accessed in naturally aligned 4 byte units */
	if (addr & 3 || len & 3 || len <= 0)
		return (EINVAL);

	mw = &sc->memwin[idx];
	while (len > 0) {
		rw_rlock(&mw->mw_lock);
		mw_end = mw->mw_curpos + mw->mw_aperture;
		if (addr >= mw_end || addr < mw->mw_curpos) {
			/* Will need to reposition the window */
			if (!rw_try_upgrade(&mw->mw_lock)) {
				rw_runlock(&mw->mw_lock);
				rw_wlock(&mw->mw_lock);
			}
			rw_assert(&mw->mw_lock, RA_WLOCKED);
			position_memwin(sc, idx, addr);
			rw_downgrade(&mw->mw_lock);
			mw_end = mw->mw_curpos + mw->mw_aperture;
		}
		rw_assert(&mw->mw_lock, RA_RLOCKED);
		while (addr < mw_end && len > 0) {
			if (rw == 0) {
				v = t4_read_reg(sc, mw->mw_base + addr -
				    mw->mw_curpos);
				*val++ = le32toh(v);
			} else {
				v = *val++;
				t4_write_reg(sc, mw->mw_base + addr -
				    mw->mw_curpos, htole32(v));
			}
			addr += 4;
			len -= 4;
		}
		rw_runlock(&mw->mw_lock);
	}

	return (0);
}

int
alloc_atid_tab(struct tid_info *t, int flags)
{
	int i;

	MPASS(t->natids > 0);
	MPASS(t->atid_tab == NULL);

	t->atid_tab = malloc(t->natids * sizeof(*t->atid_tab), M_CXGBE,
	    M_ZERO | flags);
	if (t->atid_tab == NULL)
		return (ENOMEM);
	mtx_init(&t->atid_lock, "atid lock", NULL, MTX_DEF);
	t->afree = t->atid_tab;
	t->atids_in_use = 0;
	for (i = 1; i < t->natids; i++)
		t->atid_tab[i - 1].next = &t->atid_tab[i];
	t->atid_tab[t->natids - 1].next = NULL;

	return (0);
}

void
free_atid_tab(struct tid_info *t)
{

	KASSERT(t->atids_in_use == 0,
	    ("%s: %d atids still in use.", __func__, t->atids_in_use));

	if (mtx_initialized(&t->atid_lock))
		mtx_destroy(&t->atid_lock);
	free(t->atid_tab, M_CXGBE);
	t->atid_tab = NULL;
}

int
alloc_atid(struct adapter *sc, void *ctx)
{
	struct tid_info *t = &sc->tids;
	int atid = -1;

	mtx_lock(&t->atid_lock);
	if (t->afree) {
		union aopen_entry *p = t->afree;

		atid = p - t->atid_tab;
		MPASS(atid <= M_TID_TID);
		t->afree = p->next;
		p->data = ctx;
		t->atids_in_use++;
	}
	mtx_unlock(&t->atid_lock);
	return (atid);
}

void *
lookup_atid(struct adapter *sc, int atid)
{
	struct tid_info *t = &sc->tids;

	return (t->atid_tab[atid].data);
}

void
free_atid(struct adapter *sc, int atid)
{
	struct tid_info *t = &sc->tids;
	union aopen_entry *p = &t->atid_tab[atid];

	mtx_lock(&t->atid_lock);
	p->next = t->afree;
	t->afree = p;
	t->atids_in_use--;
	mtx_unlock(&t->atid_lock);
}

static void
queue_tid_release(struct adapter *sc, int tid)
{

	CXGBE_UNIMPLEMENTED("deferred tid release");
}

void
release_tid(struct adapter *sc, int tid, struct sge_wrq *ctrlq)
{
	struct wrqe *wr;
	struct cpl_tid_release *req;

	wr = alloc_wrqe(sizeof(*req), ctrlq);
	if (wr == NULL) {
		queue_tid_release(sc, tid);	/* defer */
		return;
	}
	req = wrtod(wr);

	INIT_TP_WR_MIT_CPL(req, CPL_TID_RELEASE, tid);

	t4_wrq_tx(sc, wr);
}

static int
t4_range_cmp(const void *a, const void *b)
{
	return ((const struct t4_range *)a)->start -
	       ((const struct t4_range *)b)->start;
}

/*
 * Verify that the memory range specified by the addr/len pair is valid within
 * the card's address space.
 */
static int
validate_mem_range(struct adapter *sc, uint32_t addr, uint32_t len)
{
	struct t4_range mem_ranges[4], *r, *next;
	uint32_t em, addr_len;
	int i, n, remaining;

	/* Memory can only be accessed in naturally aligned 4 byte units */
	if (addr & 3 || len & 3 || len == 0)
		return (EINVAL);

	/* Enabled memories */
	em = t4_read_reg(sc, A_MA_TARGET_MEM_ENABLE);

	r = &mem_ranges[0];
	n = 0;
	bzero(r, sizeof(mem_ranges));
	if (em & F_EDRAM0_ENABLE) {
		addr_len = t4_read_reg(sc, A_MA_EDRAM0_BAR);
		r->size = G_EDRAM0_SIZE(addr_len) << 20;
		if (r->size > 0) {
			r->start = G_EDRAM0_BASE(addr_len) << 20;
			if (addr >= r->start &&
			    addr + len <= r->start + r->size)
				return (0);
			r++;
			n++;
		}
	}
	if (em & F_EDRAM1_ENABLE) {
		addr_len = t4_read_reg(sc, A_MA_EDRAM1_BAR);
		r->size = G_EDRAM1_SIZE(addr_len) << 20;
		if (r->size > 0) {
			r->start = G_EDRAM1_BASE(addr_len) << 20;
			if (addr >= r->start &&
			    addr + len <= r->start + r->size)
				return (0);
			r++;
			n++;
		}
	}
	if (em & F_EXT_MEM_ENABLE) {
		addr_len = t4_read_reg(sc, A_MA_EXT_MEMORY_BAR);
		r->size = G_EXT_MEM_SIZE(addr_len) << 20;
		if (r->size > 0) {
			r->start = G_EXT_MEM_BASE(addr_len) << 20;
			if (addr >= r->start &&
			    addr + len <= r->start + r->size)
				return (0);
			r++;
			n++;
		}
	}
	if (is_t5(sc) && em & F_EXT_MEM1_ENABLE) {
		addr_len = t4_read_reg(sc, A_MA_EXT_MEMORY1_BAR);
		r->size = G_EXT_MEM1_SIZE(addr_len) << 20;
		if (r->size > 0) {
			r->start = G_EXT_MEM1_BASE(addr_len) << 20;
			if (addr >= r->start &&
			    addr + len <= r->start + r->size)
				return (0);
			r++;
			n++;
		}
	}
	MPASS(n <= nitems(mem_ranges));

	if (n > 1) {
		/* Sort and merge the ranges. */
		qsort(mem_ranges, n, sizeof(struct t4_range), t4_range_cmp);

		/* Start from index 0 and examine the next n - 1 entries. */
		r = &mem_ranges[0];
		for (remaining = n - 1; remaining > 0; remaining--, r++) {

			MPASS(r->size > 0);	/* r is a valid entry. */
			next = r + 1;
			MPASS(next->size > 0);	/* and so is the next one. */

			while (r->start + r->size >= next->start) {
				/* Merge the next one into the current entry. */
				r->size = max(r->start + r->size,
				    next->start + next->size) - r->start;
				n--;	/* One fewer entry in total. */
				if (--remaining == 0)
					goto done;	/* short circuit */
				next++;
			}
			if (next != r + 1) {
				/*
				 * Some entries were merged into r and next
				 * points to the first valid entry that couldn't
				 * be merged.
				 */
				MPASS(next->size > 0);	/* must be valid */
				memcpy(r + 1, next, remaining * sizeof(*r));
#ifdef INVARIANTS
				/*
				 * This so that the foo->size assertion in the
				 * next iteration of the loop do the right
				 * thing for entries that were pulled up and are
				 * no longer valid.
				 */
				MPASS(n < nitems(mem_ranges));
				bzero(&mem_ranges[n], (nitems(mem_ranges) - n) *
				    sizeof(struct t4_range));
#endif
			}
		}
done:
		/* Done merging the ranges. */
		MPASS(n > 0);
		r = &mem_ranges[0];
		for (i = 0; i < n; i++, r++) {
			if (addr >= r->start &&
			    addr + len <= r->start + r->size)
				return (0);
		}
	}

	return (EFAULT);
}

static int
fwmtype_to_hwmtype(int mtype)
{

	switch (mtype) {
	case FW_MEMTYPE_EDC0:
		return (MEM_EDC0);
	case FW_MEMTYPE_EDC1:
		return (MEM_EDC1);
	case FW_MEMTYPE_EXTMEM:
		return (MEM_MC0);
	case FW_MEMTYPE_EXTMEM1:
		return (MEM_MC1);
	default:
		panic("%s: cannot translate fw mtype %d.", __func__, mtype);
	}
}

/*
 * Verify that the memory range specified by the memtype/offset/len pair is
 * valid and lies entirely within the memtype specified.  The global address of
 * the start of the range is returned in addr.
 */
static int
validate_mt_off_len(struct adapter *sc, int mtype, uint32_t off, uint32_t len,
    uint32_t *addr)
{
	uint32_t em, addr_len, maddr;

	/* Memory can only be accessed in naturally aligned 4 byte units */
	if (off & 3 || len & 3 || len == 0)
		return (EINVAL);

	em = t4_read_reg(sc, A_MA_TARGET_MEM_ENABLE);
	switch (fwmtype_to_hwmtype(mtype)) {
	case MEM_EDC0:
		if (!(em & F_EDRAM0_ENABLE))
			return (EINVAL);
		addr_len = t4_read_reg(sc, A_MA_EDRAM0_BAR);
		maddr = G_EDRAM0_BASE(addr_len) << 20;
		break;
	case MEM_EDC1:
		if (!(em & F_EDRAM1_ENABLE))
			return (EINVAL);
		addr_len = t4_read_reg(sc, A_MA_EDRAM1_BAR);
		maddr = G_EDRAM1_BASE(addr_len) << 20;
		break;
	case MEM_MC:
		if (!(em & F_EXT_MEM_ENABLE))
			return (EINVAL);
		addr_len = t4_read_reg(sc, A_MA_EXT_MEMORY_BAR);
		maddr = G_EXT_MEM_BASE(addr_len) << 20;
		break;
	case MEM_MC1:
		if (!is_t5(sc) || !(em & F_EXT_MEM1_ENABLE))
			return (EINVAL);
		addr_len = t4_read_reg(sc, A_MA_EXT_MEMORY1_BAR);
		maddr = G_EXT_MEM1_BASE(addr_len) << 20;
		break;
	default:
		return (EINVAL);
	}

	*addr = maddr + off;	/* global address */
	return (validate_mem_range(sc, *addr, len));
}

static int
fixup_devlog_params(struct adapter *sc)
{
	struct devlog_params *dparams = &sc->params.devlog;
	int rc;

	rc = validate_mt_off_len(sc, dparams->memtype, dparams->start,
	    dparams->size, &dparams->addr);

	return (rc);
}

static void
update_nirq(struct intrs_and_queues *iaq, int nports)
{
	int extra = T4_EXTRA_INTR;

	iaq->nirq = extra;
	iaq->nirq += nports * (iaq->nrxq + iaq->nofldrxq);
	iaq->nirq += nports * (iaq->num_vis - 1) *
	    max(iaq->nrxq_vi, iaq->nnmrxq_vi);
	iaq->nirq += nports * (iaq->num_vis - 1) * iaq->nofldrxq_vi;
}

/*
 * Adjust requirements to fit the number of interrupts available.
 */
static void
calculate_iaq(struct adapter *sc, struct intrs_and_queues *iaq, int itype,
    int navail)
{
	int old_nirq;
	const int nports = sc->params.nports;

	MPASS(nports > 0);
	MPASS(navail > 0);

	bzero(iaq, sizeof(*iaq));
	iaq->intr_type = itype;
	iaq->num_vis = t4_num_vis;
	iaq->ntxq = t4_ntxq;
	iaq->ntxq_vi = t4_ntxq_vi;
	iaq->nrxq = t4_nrxq;
	iaq->nrxq_vi = t4_nrxq_vi;
#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	if (is_offload(sc) || is_ethoffload(sc)) {
		iaq->nofldtxq = t4_nofldtxq;
		iaq->nofldtxq_vi = t4_nofldtxq_vi;
	}
#endif
#ifdef TCP_OFFLOAD
	if (is_offload(sc)) {
		iaq->nofldrxq = t4_nofldrxq;
		iaq->nofldrxq_vi = t4_nofldrxq_vi;
	}
#endif
#ifdef DEV_NETMAP
	iaq->nnmtxq_vi = t4_nnmtxq_vi;
	iaq->nnmrxq_vi = t4_nnmrxq_vi;
#endif

	update_nirq(iaq, nports);
	if (iaq->nirq <= navail &&
	    (itype != INTR_MSI || powerof2(iaq->nirq))) {
		/*
		 * This is the normal case -- there are enough interrupts for
		 * everything.
		 */
		goto done;
	}

	/*
	 * If extra VIs have been configured try reducing their count and see if
	 * that works.
	 */
	while (iaq->num_vis > 1) {
		iaq->num_vis--;
		update_nirq(iaq, nports);
		if (iaq->nirq <= navail &&
		    (itype != INTR_MSI || powerof2(iaq->nirq))) {
			device_printf(sc->dev, "virtual interfaces per port "
			    "reduced to %d from %d.  nrxq=%u, nofldrxq=%u, "
			    "nrxq_vi=%u nofldrxq_vi=%u, nnmrxq_vi=%u.  "
			    "itype %d, navail %u, nirq %d.\n",
			    iaq->num_vis, t4_num_vis, iaq->nrxq, iaq->nofldrxq,
			    iaq->nrxq_vi, iaq->nofldrxq_vi, iaq->nnmrxq_vi,
			    itype, navail, iaq->nirq);
			goto done;
		}
	}

	/*
	 * Extra VIs will not be created.  Log a message if they were requested.
	 */
	MPASS(iaq->num_vis == 1);
	iaq->ntxq_vi = iaq->nrxq_vi = 0;
	iaq->nofldtxq_vi = iaq->nofldrxq_vi = 0;
	iaq->nnmtxq_vi = iaq->nnmrxq_vi = 0;
	if (iaq->num_vis != t4_num_vis) {
		device_printf(sc->dev, "extra virtual interfaces disabled.  "
		    "nrxq=%u, nofldrxq=%u, nrxq_vi=%u nofldrxq_vi=%u, "
		    "nnmrxq_vi=%u.  itype %d, navail %u, nirq %d.\n",
		    iaq->nrxq, iaq->nofldrxq, iaq->nrxq_vi, iaq->nofldrxq_vi,
		    iaq->nnmrxq_vi, itype, navail, iaq->nirq);
	}

	/*
	 * Keep reducing the number of NIC rx queues to the next lower power of
	 * 2 (for even RSS distribution) and halving the TOE rx queues and see
	 * if that works.
	 */
	do {
		if (iaq->nrxq > 1) {
			do {
				iaq->nrxq--;
			} while (!powerof2(iaq->nrxq));
		}
		if (iaq->nofldrxq > 1)
			iaq->nofldrxq >>= 1;

		old_nirq = iaq->nirq;
		update_nirq(iaq, nports);
		if (iaq->nirq <= navail &&
		    (itype != INTR_MSI || powerof2(iaq->nirq))) {
			device_printf(sc->dev, "running with reduced number of "
			    "rx queues because of shortage of interrupts.  "
			    "nrxq=%u, nofldrxq=%u.  "
			    "itype %d, navail %u, nirq %d.\n", iaq->nrxq,
			    iaq->nofldrxq, itype, navail, iaq->nirq);
			goto done;
		}
	} while (old_nirq != iaq->nirq);

	/* One interrupt for everything.  Ugh. */
	device_printf(sc->dev, "running with minimal number of queues.  "
	    "itype %d, navail %u.\n", itype, navail);
	iaq->nirq = 1;
	MPASS(iaq->nrxq == 1);
	iaq->ntxq = 1;
	if (iaq->nofldrxq > 1)
		iaq->nofldtxq = 1;
done:
	MPASS(iaq->num_vis > 0);
	if (iaq->num_vis > 1) {
		MPASS(iaq->nrxq_vi > 0);
		MPASS(iaq->ntxq_vi > 0);
	}
	MPASS(iaq->nirq > 0);
	MPASS(iaq->nrxq > 0);
	MPASS(iaq->ntxq > 0);
	if (itype == INTR_MSI) {
		MPASS(powerof2(iaq->nirq));
	}
}

static int
cfg_itype_and_nqueues(struct adapter *sc, struct intrs_and_queues *iaq)
{
	int rc, itype, navail, nalloc;

	for (itype = INTR_MSIX; itype; itype >>= 1) {

		if ((itype & t4_intr_types) == 0)
			continue;	/* not allowed */

		if (itype == INTR_MSIX)
			navail = pci_msix_count(sc->dev);
		else if (itype == INTR_MSI)
			navail = pci_msi_count(sc->dev);
		else
			navail = 1;
restart:
		if (navail == 0)
			continue;

		calculate_iaq(sc, iaq, itype, navail);
		nalloc = iaq->nirq;
		rc = 0;
		if (itype == INTR_MSIX)
			rc = pci_alloc_msix(sc->dev, &nalloc);
		else if (itype == INTR_MSI)
			rc = pci_alloc_msi(sc->dev, &nalloc);

		if (rc == 0 && nalloc > 0) {
			if (nalloc == iaq->nirq)
				return (0);

			/*
			 * Didn't get the number requested.  Use whatever number
			 * the kernel is willing to allocate.
			 */
			device_printf(sc->dev, "fewer vectors than requested, "
			    "type=%d, req=%d, rcvd=%d; will downshift req.\n",
			    itype, iaq->nirq, nalloc);
			pci_release_msi(sc->dev);
			navail = nalloc;
			goto restart;
		}

		device_printf(sc->dev,
		    "failed to allocate vectors:%d, type=%d, req=%d, rcvd=%d\n",
		    itype, rc, iaq->nirq, nalloc);
	}

	device_printf(sc->dev,
	    "failed to find a usable interrupt type.  "
	    "allowed=%d, msi-x=%d, msi=%d, intx=1", t4_intr_types,
	    pci_msix_count(sc->dev), pci_msi_count(sc->dev));

	return (ENXIO);
}

#define FW_VERSION(chip) ( \
    V_FW_HDR_FW_VER_MAJOR(chip##FW_VERSION_MAJOR) | \
    V_FW_HDR_FW_VER_MINOR(chip##FW_VERSION_MINOR) | \
    V_FW_HDR_FW_VER_MICRO(chip##FW_VERSION_MICRO) | \
    V_FW_HDR_FW_VER_BUILD(chip##FW_VERSION_BUILD))
#define FW_INTFVER(chip, intf) (chip##FW_HDR_INTFVER_##intf)

/* Just enough of fw_hdr to cover all version info. */
struct fw_h {
	__u8	ver;
	__u8	chip;
	__be16	len512;
	__be32	fw_ver;
	__be32	tp_microcode_ver;
	__u8	intfver_nic;
	__u8	intfver_vnic;
	__u8	intfver_ofld;
	__u8	intfver_ri;
	__u8	intfver_iscsipdu;
	__u8	intfver_iscsi;
	__u8	intfver_fcoepdu;
	__u8	intfver_fcoe;
};
/* Spot check a couple of fields. */
CTASSERT(offsetof(struct fw_h, fw_ver) == offsetof(struct fw_hdr, fw_ver));
CTASSERT(offsetof(struct fw_h, intfver_nic) == offsetof(struct fw_hdr, intfver_nic));
CTASSERT(offsetof(struct fw_h, intfver_fcoe) == offsetof(struct fw_hdr, intfver_fcoe));

struct fw_info {
	uint8_t chip;
	char *kld_name;
	char *fw_mod_name;
	struct fw_h fw_h;
} fw_info[] = {
	{
		.chip = CHELSIO_T4,
		.kld_name = "t4fw_cfg",
		.fw_mod_name = "t4fw",
		.fw_h = {
			.chip = FW_HDR_CHIP_T4,
			.fw_ver = htobe32(FW_VERSION(T4)),
			.intfver_nic = FW_INTFVER(T4, NIC),
			.intfver_vnic = FW_INTFVER(T4, VNIC),
			.intfver_ofld = FW_INTFVER(T4, OFLD),
			.intfver_ri = FW_INTFVER(T4, RI),
			.intfver_iscsipdu = FW_INTFVER(T4, ISCSIPDU),
			.intfver_iscsi = FW_INTFVER(T4, ISCSI),
			.intfver_fcoepdu = FW_INTFVER(T4, FCOEPDU),
			.intfver_fcoe = FW_INTFVER(T4, FCOE),
		},
	}, {
		.chip = CHELSIO_T5,
		.kld_name = "t5fw_cfg",
		.fw_mod_name = "t5fw",
		.fw_h = {
			.chip = FW_HDR_CHIP_T5,
			.fw_ver = htobe32(FW_VERSION(T5)),
			.intfver_nic = FW_INTFVER(T5, NIC),
			.intfver_vnic = FW_INTFVER(T5, VNIC),
			.intfver_ofld = FW_INTFVER(T5, OFLD),
			.intfver_ri = FW_INTFVER(T5, RI),
			.intfver_iscsipdu = FW_INTFVER(T5, ISCSIPDU),
			.intfver_iscsi = FW_INTFVER(T5, ISCSI),
			.intfver_fcoepdu = FW_INTFVER(T5, FCOEPDU),
			.intfver_fcoe = FW_INTFVER(T5, FCOE),
		},
	}, {
		.chip = CHELSIO_T6,
		.kld_name = "t6fw_cfg",
		.fw_mod_name = "t6fw",
		.fw_h = {
			.chip = FW_HDR_CHIP_T6,
			.fw_ver = htobe32(FW_VERSION(T6)),
			.intfver_nic = FW_INTFVER(T6, NIC),
			.intfver_vnic = FW_INTFVER(T6, VNIC),
			.intfver_ofld = FW_INTFVER(T6, OFLD),
			.intfver_ri = FW_INTFVER(T6, RI),
			.intfver_iscsipdu = FW_INTFVER(T6, ISCSIPDU),
			.intfver_iscsi = FW_INTFVER(T6, ISCSI),
			.intfver_fcoepdu = FW_INTFVER(T6, FCOEPDU),
			.intfver_fcoe = FW_INTFVER(T6, FCOE),
		},
	}
};

static struct fw_info *
find_fw_info(int chip)
{
	int i;

	for (i = 0; i < nitems(fw_info); i++) {
		if (fw_info[i].chip == chip)
			return (&fw_info[i]);
	}
	return (NULL);
}

/*
 * Is the given firmware API compatible with the one the driver was compiled
 * with?
 */
static int
fw_compatible(const struct fw_h *hdr1, const struct fw_h *hdr2)
{

	/* short circuit if it's the exact same firmware version */
	if (hdr1->chip == hdr2->chip && hdr1->fw_ver == hdr2->fw_ver)
		return (1);

	/*
	 * XXX: Is this too conservative?  Perhaps I should limit this to the
	 * features that are supported in the driver.
	 */
#define SAME_INTF(x) (hdr1->intfver_##x == hdr2->intfver_##x)
	if (hdr1->chip == hdr2->chip && SAME_INTF(nic) && SAME_INTF(vnic) &&
	    SAME_INTF(ofld) && SAME_INTF(ri) && SAME_INTF(iscsipdu) &&
	    SAME_INTF(iscsi) && SAME_INTF(fcoepdu) && SAME_INTF(fcoe))
		return (1);
#undef SAME_INTF

	return (0);
}

static int
load_fw_module(struct adapter *sc, const struct firmware **dcfg,
    const struct firmware **fw)
{
	struct fw_info *fw_info;

	*dcfg = NULL;
	if (fw != NULL)
		*fw = NULL;

	fw_info = find_fw_info(chip_id(sc));
	if (fw_info == NULL) {
		device_printf(sc->dev,
		    "unable to look up firmware information for chip %d.\n",
		    chip_id(sc));
		return (EINVAL);
	}

	*dcfg = firmware_get(fw_info->kld_name);
	if (*dcfg != NULL) {
		if (fw != NULL)
			*fw = firmware_get(fw_info->fw_mod_name);
		return (0);
	}

	return (ENOENT);
}

static void
unload_fw_module(struct adapter *sc, const struct firmware *dcfg,
    const struct firmware *fw)
{

	if (fw != NULL)
		firmware_put(fw, FIRMWARE_UNLOAD);
	if (dcfg != NULL)
		firmware_put(dcfg, FIRMWARE_UNLOAD);
}

/*
 * Return values:
 * 0 means no firmware install attempted.
 * ERESTART means a firmware install was attempted and was successful.
 * +ve errno means a firmware install was attempted but failed.
 */
static int
install_kld_firmware(struct adapter *sc, struct fw_h *card_fw,
    const struct fw_h *drv_fw, const char *reason, int *already)
{
	const struct firmware *cfg, *fw;
	const uint32_t c = be32toh(card_fw->fw_ver);
	uint32_t d, k;
	int rc, fw_install;
	struct fw_h bundled_fw;
	bool load_attempted;

	cfg = fw = NULL;
	load_attempted = false;
	fw_install = t4_fw_install < 0 ? -t4_fw_install : t4_fw_install;

	if (reason != NULL)
		goto install;

	if ((sc->flags & FW_OK) == 0) {

		if (c == 0xffffffff) {
			reason = "missing";
			goto install;
		}

		return (0);
	}

	memcpy(&bundled_fw, drv_fw, sizeof(bundled_fw));
	if (t4_fw_install < 0) {
		rc = load_fw_module(sc, &cfg, &fw);
		if (rc != 0 || fw == NULL) {
			device_printf(sc->dev,
			    "failed to load firmware module: %d. cfg %p, fw %p;"
			    " will use compiled-in firmware version for"
			    "hw.cxgbe.fw_install checks.\n",
			    rc, cfg, fw);
		} else {
			memcpy(&bundled_fw, fw->data, sizeof(bundled_fw));
		}
		load_attempted = true;
	}
	d = be32toh(bundled_fw.fw_ver);

	if (!fw_compatible(card_fw, &bundled_fw)) {
		reason = "incompatible or unusable";
		goto install;
	}

	if (d > c) {
		reason = "older than the version bundled with this driver";
		goto install;
	}

	if (fw_install == 2 && d != c) {
		reason = "different than the version bundled with this driver";
		goto install;
	}

	/* No reason to do anything to the firmware already on the card. */
	rc = 0;
	goto done;

install:
	rc = 0;
	if ((*already)++)
		goto done;

	if (fw_install == 0) {
		device_printf(sc->dev, "firmware on card (%u.%u.%u.%u) is %s, "
		    "but the driver is prohibited from installing a firmware "
		    "on the card.\n",
		    G_FW_HDR_FW_VER_MAJOR(c), G_FW_HDR_FW_VER_MINOR(c),
		    G_FW_HDR_FW_VER_MICRO(c), G_FW_HDR_FW_VER_BUILD(c), reason);

		goto done;
	}

	/*
	 * We'll attempt to install a firmware.  Load the module first (if it
	 * hasn't been loaded already).
	 */
	if (!load_attempted) {
		rc = load_fw_module(sc, &cfg, &fw);
		if (rc != 0 || fw == NULL) {
			device_printf(sc->dev,
			    "failed to load firmware module: %d. cfg %p, fw %p\n",
			    rc, cfg, fw);
			/* carry on */
		}
	}
	if (fw == NULL) {
		device_printf(sc->dev, "firmware on card (%u.%u.%u.%u) is %s, "
		    "but the driver cannot take corrective action because it "
		    "is unable to load the firmware module.\n",
		    G_FW_HDR_FW_VER_MAJOR(c), G_FW_HDR_FW_VER_MINOR(c),
		    G_FW_HDR_FW_VER_MICRO(c), G_FW_HDR_FW_VER_BUILD(c), reason);
		rc = sc->flags & FW_OK ? 0 : ENOENT;
		goto done;
	}
	k = be32toh(((const struct fw_hdr *)fw->data)->fw_ver);
	if (k != d) {
		MPASS(t4_fw_install > 0);
		device_printf(sc->dev,
		    "firmware in KLD (%u.%u.%u.%u) is not what the driver was "
		    "expecting (%u.%u.%u.%u) and will not be used.\n",
		    G_FW_HDR_FW_VER_MAJOR(k), G_FW_HDR_FW_VER_MINOR(k),
		    G_FW_HDR_FW_VER_MICRO(k), G_FW_HDR_FW_VER_BUILD(k),
		    G_FW_HDR_FW_VER_MAJOR(d), G_FW_HDR_FW_VER_MINOR(d),
		    G_FW_HDR_FW_VER_MICRO(d), G_FW_HDR_FW_VER_BUILD(d));
		rc = sc->flags & FW_OK ? 0 : EINVAL;
		goto done;
	}

	device_printf(sc->dev, "firmware on card (%u.%u.%u.%u) is %s, "
	    "installing firmware %u.%u.%u.%u on card.\n",
	    G_FW_HDR_FW_VER_MAJOR(c), G_FW_HDR_FW_VER_MINOR(c),
	    G_FW_HDR_FW_VER_MICRO(c), G_FW_HDR_FW_VER_BUILD(c), reason,
	    G_FW_HDR_FW_VER_MAJOR(d), G_FW_HDR_FW_VER_MINOR(d),
	    G_FW_HDR_FW_VER_MICRO(d), G_FW_HDR_FW_VER_BUILD(d));

	rc = -t4_fw_upgrade(sc, sc->mbox, fw->data, fw->datasize, 0);
	if (rc != 0) {
		device_printf(sc->dev, "failed to install firmware: %d\n", rc);
	} else {
		/* Installed successfully, update the cached header too. */
		rc = ERESTART;
		memcpy(card_fw, fw->data, sizeof(*card_fw));
	}
done:
	unload_fw_module(sc, cfg, fw);

	return (rc);
}

/*
 * Establish contact with the firmware and attempt to become the master driver.
 *
 * A firmware will be installed to the card if needed (if the driver is allowed
 * to do so).
 */
static int
contact_firmware(struct adapter *sc)
{
	int rc, already = 0;
	enum dev_state state;
	struct fw_info *fw_info;
	struct fw_hdr *card_fw;		/* fw on the card */
	const struct fw_h *drv_fw;

	fw_info = find_fw_info(chip_id(sc));
	if (fw_info == NULL) {
		device_printf(sc->dev,
		    "unable to look up firmware information for chip %d.\n",
		    chip_id(sc));
		return (EINVAL);
	}
	drv_fw = &fw_info->fw_h;

	/* Read the header of the firmware on the card */
	card_fw = malloc(sizeof(*card_fw), M_CXGBE, M_ZERO | M_WAITOK);
restart:
	rc = -t4_get_fw_hdr(sc, card_fw);
	if (rc != 0) {
		device_printf(sc->dev,
		    "unable to read firmware header from card's flash: %d\n",
		    rc);
		goto done;
	}

	rc = install_kld_firmware(sc, (struct fw_h *)card_fw, drv_fw, NULL,
	    &already);
	if (rc == ERESTART)
		goto restart;
	if (rc != 0)
		goto done;

	rc = t4_fw_hello(sc, sc->mbox, sc->mbox, MASTER_MAY, &state);
	if (rc < 0 || state == DEV_STATE_ERR) {
		rc = -rc;
		device_printf(sc->dev,
		    "failed to connect to the firmware: %d, %d.  "
		    "PCIE_FW 0x%08x\n", rc, state, t4_read_reg(sc, A_PCIE_FW));
#if 0
		if (install_kld_firmware(sc, (struct fw_h *)card_fw, drv_fw,
		    "not responding properly to HELLO", &already) == ERESTART)
			goto restart;
#endif
		goto done;
	}
	MPASS(be32toh(card_fw->flags) & FW_HDR_FLAGS_RESET_HALT);
	sc->flags |= FW_OK;	/* The firmware responded to the FW_HELLO. */

	if (rc == sc->pf) {
		sc->flags |= MASTER_PF;
		rc = install_kld_firmware(sc, (struct fw_h *)card_fw, drv_fw,
		    NULL, &already);
		if (rc == ERESTART)
			rc = 0;
		else if (rc != 0)
			goto done;
	} else if (state == DEV_STATE_UNINIT) {
		/*
		 * We didn't get to be the master so we definitely won't be
		 * configuring the chip.  It's a bug if someone else hasn't
		 * configured it already.
		 */
		device_printf(sc->dev, "couldn't be master(%d), "
		    "device not already initialized either(%d).  "
		    "PCIE_FW 0x%08x\n", rc, state, t4_read_reg(sc, A_PCIE_FW));
		rc = EPROTO;
		goto done;
	} else {
		/*
		 * Some other PF is the master and has configured the chip.
		 * This is allowed but untested.
		 */
		device_printf(sc->dev, "PF%d is master, device state %d.  "
		    "PCIE_FW 0x%08x\n", rc, state, t4_read_reg(sc, A_PCIE_FW));
		snprintf(sc->cfg_file, sizeof(sc->cfg_file), "pf%d", rc);
		sc->cfcsum = 0;
		rc = 0;
	}
done:
	if (rc != 0 && sc->flags & FW_OK) {
		t4_fw_bye(sc, sc->mbox);
		sc->flags &= ~FW_OK;
	}
	free(card_fw, M_CXGBE);
	return (rc);
}

static int
copy_cfg_file_to_card(struct adapter *sc, char *cfg_file,
    uint32_t mtype, uint32_t moff)
{
	struct fw_info *fw_info;
	const struct firmware *dcfg, *rcfg = NULL;
	const uint32_t *cfdata;
	uint32_t cflen, addr;
	int rc;

	load_fw_module(sc, &dcfg, NULL);

	/* Card specific interpretation of "default". */
	if (strncmp(cfg_file, DEFAULT_CF, sizeof(t4_cfg_file)) == 0) {
		if (pci_get_device(sc->dev) == 0x440a)
			snprintf(cfg_file, sizeof(t4_cfg_file), UWIRE_CF);
		if (is_fpga(sc))
			snprintf(cfg_file, sizeof(t4_cfg_file), FPGA_CF);
	}

	if (strncmp(cfg_file, DEFAULT_CF, sizeof(t4_cfg_file)) == 0) {
		if (dcfg == NULL) {
			device_printf(sc->dev,
			    "KLD with default config is not available.\n");
			rc = ENOENT;
			goto done;
		}
		cfdata = dcfg->data;
		cflen = dcfg->datasize & ~3;
	} else {
		char s[32];

		fw_info = find_fw_info(chip_id(sc));
		if (fw_info == NULL) {
			device_printf(sc->dev,
			    "unable to look up firmware information for chip %d.\n",
			    chip_id(sc));
			rc = EINVAL;
			goto done;
		}
		snprintf(s, sizeof(s), "%s_%s", fw_info->kld_name, cfg_file);

		rcfg = firmware_get(s);
		if (rcfg == NULL) {
			device_printf(sc->dev,
			    "unable to load module \"%s\" for configuration "
			    "profile \"%s\".\n", s, cfg_file);
			rc = ENOENT;
			goto done;
		}
		cfdata = rcfg->data;
		cflen = rcfg->datasize & ~3;
	}

	if (cflen > FLASH_CFG_MAX_SIZE) {
		device_printf(sc->dev,
		    "config file too long (%d, max allowed is %d).\n",
		    cflen, FLASH_CFG_MAX_SIZE);
		rc = EINVAL;
		goto done;
	}

	rc = validate_mt_off_len(sc, mtype, moff, cflen, &addr);
	if (rc != 0) {
		device_printf(sc->dev,
		    "%s: addr (%d/0x%x) or len %d is not valid: %d.\n",
		    __func__, mtype, moff, cflen, rc);
		rc = EINVAL;
		goto done;
	}
	write_via_memwin(sc, 2, addr, cfdata, cflen);
done:
	if (rcfg != NULL)
		firmware_put(rcfg, FIRMWARE_UNLOAD);
	unload_fw_module(sc, dcfg, NULL);
	return (rc);
}

struct caps_allowed {
	uint16_t nbmcaps;
	uint16_t linkcaps;
	uint16_t switchcaps;
	uint16_t niccaps;
	uint16_t toecaps;
	uint16_t rdmacaps;
	uint16_t cryptocaps;
	uint16_t iscsicaps;
	uint16_t fcoecaps;
};

#define FW_PARAM_DEV(param) \
	(V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) | \
	 V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_##param))
#define FW_PARAM_PFVF(param) \
	(V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_PFVF) | \
	 V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_PFVF_##param))

/*
 * Provide a configuration profile to the firmware and have it initialize the
 * chip accordingly.  This may involve uploading a configuration file to the
 * card.
 */
static int
apply_cfg_and_initialize(struct adapter *sc, char *cfg_file,
    const struct caps_allowed *caps_allowed)
{
	int rc;
	struct fw_caps_config_cmd caps;
	uint32_t mtype, moff, finicsum, cfcsum, param, val;

	rc = -t4_fw_reset(sc, sc->mbox, F_PIORSTMODE | F_PIORST);
	if (rc != 0) {
		device_printf(sc->dev, "firmware reset failed: %d.\n", rc);
		return (rc);
	}

	bzero(&caps, sizeof(caps));
	caps.op_to_write = htobe32(V_FW_CMD_OP(FW_CAPS_CONFIG_CMD) |
	    F_FW_CMD_REQUEST | F_FW_CMD_READ);
	if (strncmp(cfg_file, BUILTIN_CF, sizeof(t4_cfg_file)) == 0) {
		mtype = 0;
		moff = 0;
		caps.cfvalid_to_len16 = htobe32(FW_LEN16(caps));
	} else if (strncmp(cfg_file, FLASH_CF, sizeof(t4_cfg_file)) == 0) {
		mtype = FW_MEMTYPE_FLASH;
		moff = t4_flash_cfg_addr(sc);
		caps.cfvalid_to_len16 = htobe32(F_FW_CAPS_CONFIG_CMD_CFVALID |
		    V_FW_CAPS_CONFIG_CMD_MEMTYPE_CF(mtype) |
		    V_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF(moff >> 16) |
		    FW_LEN16(caps));
	} else {
		/*
		 * Ask the firmware where it wants us to upload the config file.
		 */
		param = FW_PARAM_DEV(CF);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 1, &param, &val);
		if (rc != 0) {
			/* No support for config file?  Shouldn't happen. */
			device_printf(sc->dev,
			    "failed to query config file location: %d.\n", rc);
			goto done;
		}
		mtype = G_FW_PARAMS_PARAM_Y(val);
		moff = G_FW_PARAMS_PARAM_Z(val) << 16;
		caps.cfvalid_to_len16 = htobe32(F_FW_CAPS_CONFIG_CMD_CFVALID |
		    V_FW_CAPS_CONFIG_CMD_MEMTYPE_CF(mtype) |
		    V_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF(moff >> 16) |
		    FW_LEN16(caps));

		rc = copy_cfg_file_to_card(sc, cfg_file, mtype, moff);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to upload config file to card: %d.\n", rc);
			goto done;
		}
	}
	rc = -t4_wr_mbox(sc, sc->mbox, &caps, sizeof(caps), &caps);
	if (rc != 0) {
		device_printf(sc->dev, "failed to pre-process config file: %d "
		    "(mtype %d, moff 0x%x).\n", rc, mtype, moff);
		goto done;
	}

	finicsum = be32toh(caps.finicsum);
	cfcsum = be32toh(caps.cfcsum);	/* actual */
	if (finicsum != cfcsum) {
		device_printf(sc->dev,
		    "WARNING: config file checksum mismatch: %08x %08x\n",
		    finicsum, cfcsum);
	}
	sc->cfcsum = cfcsum;
	snprintf(sc->cfg_file, sizeof(sc->cfg_file), "%s", cfg_file);

	/*
	 * Let the firmware know what features will (not) be used so it can tune
	 * things accordingly.
	 */
#define LIMIT_CAPS(x) do { \
	caps.x##caps &= htobe16(caps_allowed->x##caps); \
} while (0)
	LIMIT_CAPS(nbm);
	LIMIT_CAPS(link);
	LIMIT_CAPS(switch);
	LIMIT_CAPS(nic);
	LIMIT_CAPS(toe);
	LIMIT_CAPS(rdma);
	LIMIT_CAPS(crypto);
	LIMIT_CAPS(iscsi);
	LIMIT_CAPS(fcoe);
#undef LIMIT_CAPS
	if (caps.niccaps & htobe16(FW_CAPS_CONFIG_NIC_HASHFILTER)) {
		/*
		 * TOE and hashfilters are mutually exclusive.  It is a config
		 * file or firmware bug if both are reported as available.  Try
		 * to cope with the situation in non-debug builds by disabling
		 * TOE.
		 */
		MPASS(caps.toecaps == 0);

		caps.toecaps = 0;
		caps.rdmacaps = 0;
		caps.iscsicaps = 0;
	}

	caps.op_to_write = htobe32(V_FW_CMD_OP(FW_CAPS_CONFIG_CMD) |
	    F_FW_CMD_REQUEST | F_FW_CMD_WRITE);
	caps.cfvalid_to_len16 = htobe32(FW_LEN16(caps));
	rc = -t4_wr_mbox(sc, sc->mbox, &caps, sizeof(caps), NULL);
	if (rc != 0) {
		device_printf(sc->dev,
		    "failed to process config file: %d.\n", rc);
		goto done;
	}

	t4_tweak_chip_settings(sc);
	set_params__pre_init(sc);

	/* get basic stuff going */
	rc = -t4_fw_initialize(sc, sc->mbox);
	if (rc != 0) {
		device_printf(sc->dev, "fw_initialize failed: %d.\n", rc);
		goto done;
	}
done:
	return (rc);
}

/*
 * Partition chip resources for use between various PFs, VFs, etc.
 */
static int
partition_resources(struct adapter *sc)
{
	char cfg_file[sizeof(t4_cfg_file)];
	struct caps_allowed caps_allowed;
	int rc;
	bool fallback;

	/* Only the master driver gets to configure the chip resources. */
	MPASS(sc->flags & MASTER_PF);

#define COPY_CAPS(x) do { \
	caps_allowed.x##caps = t4_##x##caps_allowed; \
} while (0)
	bzero(&caps_allowed, sizeof(caps_allowed));
	COPY_CAPS(nbm);
	COPY_CAPS(link);
	COPY_CAPS(switch);
	COPY_CAPS(nic);
	COPY_CAPS(toe);
	COPY_CAPS(rdma);
	COPY_CAPS(crypto);
	COPY_CAPS(iscsi);
	COPY_CAPS(fcoe);
	fallback = sc->debug_flags & DF_DISABLE_CFG_RETRY ? false : true;
	snprintf(cfg_file, sizeof(cfg_file), "%s", t4_cfg_file);
retry:
	rc = apply_cfg_and_initialize(sc, cfg_file, &caps_allowed);
	if (rc != 0 && fallback) {
		device_printf(sc->dev,
		    "failed (%d) to configure card with \"%s\" profile, "
		    "will fall back to a basic configuration and retry.\n",
		    rc, cfg_file);
		snprintf(cfg_file, sizeof(cfg_file), "%s", BUILTIN_CF);
		bzero(&caps_allowed, sizeof(caps_allowed));
		COPY_CAPS(nbm);
		COPY_CAPS(link);
		COPY_CAPS(switch);
		COPY_CAPS(nic);
		fallback = false;
		goto retry;
	}
#undef COPY_CAPS
	return (rc);
}

/*
 * Retrieve parameters that are needed (or nice to have) very early.
 */
static int
get_params__pre_init(struct adapter *sc)
{
	int rc;
	uint32_t param[2], val[2];

	t4_get_version_info(sc);

	snprintf(sc->fw_version, sizeof(sc->fw_version), "%u.%u.%u.%u",
	    G_FW_HDR_FW_VER_MAJOR(sc->params.fw_vers),
	    G_FW_HDR_FW_VER_MINOR(sc->params.fw_vers),
	    G_FW_HDR_FW_VER_MICRO(sc->params.fw_vers),
	    G_FW_HDR_FW_VER_BUILD(sc->params.fw_vers));

	snprintf(sc->bs_version, sizeof(sc->bs_version), "%u.%u.%u.%u",
	    G_FW_HDR_FW_VER_MAJOR(sc->params.bs_vers),
	    G_FW_HDR_FW_VER_MINOR(sc->params.bs_vers),
	    G_FW_HDR_FW_VER_MICRO(sc->params.bs_vers),
	    G_FW_HDR_FW_VER_BUILD(sc->params.bs_vers));

	snprintf(sc->tp_version, sizeof(sc->tp_version), "%u.%u.%u.%u",
	    G_FW_HDR_FW_VER_MAJOR(sc->params.tp_vers),
	    G_FW_HDR_FW_VER_MINOR(sc->params.tp_vers),
	    G_FW_HDR_FW_VER_MICRO(sc->params.tp_vers),
	    G_FW_HDR_FW_VER_BUILD(sc->params.tp_vers));

	snprintf(sc->er_version, sizeof(sc->er_version), "%u.%u.%u.%u",
	    G_FW_HDR_FW_VER_MAJOR(sc->params.er_vers),
	    G_FW_HDR_FW_VER_MINOR(sc->params.er_vers),
	    G_FW_HDR_FW_VER_MICRO(sc->params.er_vers),
	    G_FW_HDR_FW_VER_BUILD(sc->params.er_vers));

	param[0] = FW_PARAM_DEV(PORTVEC);
	param[1] = FW_PARAM_DEV(CCLK);
	rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 2, param, val);
	if (rc != 0) {
		device_printf(sc->dev,
		    "failed to query parameters (pre_init): %d.\n", rc);
		return (rc);
	}

	sc->params.portvec = val[0];
	sc->params.nports = bitcount32(val[0]);
	sc->params.vpd.cclk = val[1];

	/* Read device log parameters. */
	rc = -t4_init_devlog_params(sc, 1);
	if (rc == 0)
		fixup_devlog_params(sc);
	else {
		device_printf(sc->dev,
		    "failed to get devlog parameters: %d.\n", rc);
		rc = 0;	/* devlog isn't critical for device operation */
	}

	return (rc);
}

/*
 * Any params that need to be set before FW_INITIALIZE.
 */
static int
set_params__pre_init(struct adapter *sc)
{
	int rc = 0;
	uint32_t param, val;

	if (chip_id(sc) >= CHELSIO_T6) {
		param = FW_PARAM_DEV(HPFILTER_REGION_SUPPORT);
		val = 1;
		rc = -t4_set_params(sc, sc->mbox, sc->pf, 0, 1, &param, &val);
		/* firmwares < 1.20.1.0 do not have this param. */
		if (rc == FW_EINVAL && sc->params.fw_vers <
		    (V_FW_HDR_FW_VER_MAJOR(1) | V_FW_HDR_FW_VER_MINOR(20) |
		    V_FW_HDR_FW_VER_MICRO(1) | V_FW_HDR_FW_VER_BUILD(0))) {
			rc = 0;
		}
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to enable high priority filters :%d.\n",
			    rc);
		}
	}

	/* Enable opaque VIIDs with firmwares that support it. */
	param = FW_PARAM_DEV(OPAQUE_VIID_SMT_EXTN);
	val = 1;
	rc = -t4_set_params(sc, sc->mbox, sc->pf, 0, 1, &param, &val);
	if (rc == 0 && val == 1)
		sc->params.viid_smt_extn_support = true;
	else
		sc->params.viid_smt_extn_support = false;

	return (rc);
}

/*
 * Retrieve various parameters that are of interest to the driver.  The device
 * has been initialized by the firmware at this point.
 */
static int
get_params__post_init(struct adapter *sc)
{
	int rc;
	uint32_t param[7], val[7];
	struct fw_caps_config_cmd caps;

	param[0] = FW_PARAM_PFVF(IQFLINT_START);
	param[1] = FW_PARAM_PFVF(EQ_START);
	param[2] = FW_PARAM_PFVF(FILTER_START);
	param[3] = FW_PARAM_PFVF(FILTER_END);
	param[4] = FW_PARAM_PFVF(L2T_START);
	param[5] = FW_PARAM_PFVF(L2T_END);
	param[6] = V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) |
	    V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_DIAG) |
	    V_FW_PARAMS_PARAM_Y(FW_PARAM_DEV_DIAG_VDD);
	rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 7, param, val);
	if (rc != 0) {
		device_printf(sc->dev,
		    "failed to query parameters (post_init): %d.\n", rc);
		return (rc);
	}

	sc->sge.iq_start = val[0];
	sc->sge.eq_start = val[1];
	if ((int)val[3] > (int)val[2]) {
		sc->tids.ftid_base = val[2];
		sc->tids.ftid_end = val[3];
		sc->tids.nftids = val[3] - val[2] + 1;
	}
	sc->vres.l2t.start = val[4];
	sc->vres.l2t.size = val[5] - val[4] + 1;
	KASSERT(sc->vres.l2t.size <= L2T_SIZE,
	    ("%s: L2 table size (%u) larger than expected (%u)",
	    __func__, sc->vres.l2t.size, L2T_SIZE));
	sc->params.core_vdd = val[6];

	if (chip_id(sc) >= CHELSIO_T6) {

		sc->tids.tid_base = t4_read_reg(sc,
		    A_LE_DB_ACTIVE_TABLE_START_INDEX);

		param[0] = FW_PARAM_PFVF(HPFILTER_START);
		param[1] = FW_PARAM_PFVF(HPFILTER_END);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 2, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			   "failed to query hpfilter parameters: %d.\n", rc);
			return (rc);
		}
		if ((int)val[1] > (int)val[0]) {
			sc->tids.hpftid_base = val[0];
			sc->tids.hpftid_end = val[1];
			sc->tids.nhpftids = val[1] - val[0] + 1;

			/*
			 * These should go off if the layout changes and the
			 * driver needs to catch up.
			 */
			MPASS(sc->tids.hpftid_base == 0);
			MPASS(sc->tids.tid_base == sc->tids.nhpftids);
		}
	}

	/*
	 * MPSBGMAP is queried separately because only recent firmwares support
	 * it as a parameter and we don't want the compound query above to fail
	 * on older firmwares.
	 */
	param[0] = FW_PARAM_DEV(MPSBGMAP);
	val[0] = 0;
	rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 1, param, val);
	if (rc == 0)
		sc->params.mps_bg_map = val[0];
	else
		sc->params.mps_bg_map = 0;

	/*
	 * Determine whether the firmware supports the filter2 work request.
	 * This is queried separately for the same reason as MPSBGMAP above.
	 */
	param[0] = FW_PARAM_DEV(FILTER2_WR);
	val[0] = 0;
	rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 1, param, val);
	if (rc == 0)
		sc->params.filter2_wr_support = val[0] != 0;
	else
		sc->params.filter2_wr_support = 0;

	/*
	 * Find out whether we're allowed to use the ULPTX MEMWRITE DSGL.
	 * This is queried separately for the same reason as other params above.
	 */
	param[0] = FW_PARAM_DEV(ULPTX_MEMWRITE_DSGL);
	val[0] = 0;
	rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 1, param, val);
	if (rc == 0)
		sc->params.ulptx_memwrite_dsgl = val[0] != 0;
	else
		sc->params.ulptx_memwrite_dsgl = false;

	/* get capabilites */
	bzero(&caps, sizeof(caps));
	caps.op_to_write = htobe32(V_FW_CMD_OP(FW_CAPS_CONFIG_CMD) |
	    F_FW_CMD_REQUEST | F_FW_CMD_READ);
	caps.cfvalid_to_len16 = htobe32(FW_LEN16(caps));
	rc = -t4_wr_mbox(sc, sc->mbox, &caps, sizeof(caps), &caps);
	if (rc != 0) {
		device_printf(sc->dev,
		    "failed to get card capabilities: %d.\n", rc);
		return (rc);
	}

#define READ_CAPS(x) do { \
	sc->x = htobe16(caps.x); \
} while (0)
	READ_CAPS(nbmcaps);
	READ_CAPS(linkcaps);
	READ_CAPS(switchcaps);
	READ_CAPS(niccaps);
	READ_CAPS(toecaps);
	READ_CAPS(rdmacaps);
	READ_CAPS(cryptocaps);
	READ_CAPS(iscsicaps);
	READ_CAPS(fcoecaps);

	if (sc->niccaps & FW_CAPS_CONFIG_NIC_HASHFILTER) {
		MPASS(chip_id(sc) > CHELSIO_T4);
		MPASS(sc->toecaps == 0);
		sc->toecaps = 0;

		param[0] = FW_PARAM_DEV(NTID);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 1, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to query HASHFILTER parameters: %d.\n", rc);
			return (rc);
		}
		sc->tids.ntids = val[0];
		if (sc->params.fw_vers <
		    (V_FW_HDR_FW_VER_MAJOR(1) | V_FW_HDR_FW_VER_MINOR(20) |
		    V_FW_HDR_FW_VER_MICRO(5) | V_FW_HDR_FW_VER_BUILD(0))) {
			MPASS(sc->tids.ntids >= sc->tids.nhpftids);
			sc->tids.ntids -= sc->tids.nhpftids;
		}
		sc->tids.natids = min(sc->tids.ntids / 2, MAX_ATIDS);
		sc->params.hash_filter = 1;
	}
	if (sc->niccaps & FW_CAPS_CONFIG_NIC_ETHOFLD) {
		param[0] = FW_PARAM_PFVF(ETHOFLD_START);
		param[1] = FW_PARAM_PFVF(ETHOFLD_END);
		param[2] = FW_PARAM_DEV(FLOWC_BUFFIFO_SZ);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 3, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to query NIC parameters: %d.\n", rc);
			return (rc);
		}
		if ((int)val[1] > (int)val[0]) {
			sc->tids.etid_base = val[0];
			sc->tids.etid_end = val[1];
			sc->tids.netids = val[1] - val[0] + 1;
			sc->params.eo_wr_cred = val[2];
			sc->params.ethoffload = 1;
		}
	}
	if (sc->toecaps) {
		/* query offload-related parameters */
		param[0] = FW_PARAM_DEV(NTID);
		param[1] = FW_PARAM_PFVF(SERVER_START);
		param[2] = FW_PARAM_PFVF(SERVER_END);
		param[3] = FW_PARAM_PFVF(TDDP_START);
		param[4] = FW_PARAM_PFVF(TDDP_END);
		param[5] = FW_PARAM_DEV(FLOWC_BUFFIFO_SZ);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 6, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to query TOE parameters: %d.\n", rc);
			return (rc);
		}
		sc->tids.ntids = val[0];
		if (sc->params.fw_vers <
		    (V_FW_HDR_FW_VER_MAJOR(1) | V_FW_HDR_FW_VER_MINOR(20) |
		    V_FW_HDR_FW_VER_MICRO(5) | V_FW_HDR_FW_VER_BUILD(0))) {
			MPASS(sc->tids.ntids >= sc->tids.nhpftids);
			sc->tids.ntids -= sc->tids.nhpftids;
		}
		sc->tids.natids = min(sc->tids.ntids / 2, MAX_ATIDS);
		if ((int)val[2] > (int)val[1]) {
			sc->tids.stid_base = val[1];
			sc->tids.nstids = val[2] - val[1] + 1;
		}
		sc->vres.ddp.start = val[3];
		sc->vres.ddp.size = val[4] - val[3] + 1;
		sc->params.ofldq_wr_cred = val[5];
		sc->params.offload = 1;
	} else {
		/*
		 * The firmware attempts memfree TOE configuration for -SO cards
		 * and will report toecaps=0 if it runs out of resources (this
		 * depends on the config file).  It may not report 0 for other
		 * capabilities dependent on the TOE in this case.  Set them to
		 * 0 here so that the driver doesn't bother tracking resources
		 * that will never be used.
		 */
		sc->iscsicaps = 0;
		sc->rdmacaps = 0;
	}
	if (sc->rdmacaps) {
		param[0] = FW_PARAM_PFVF(STAG_START);
		param[1] = FW_PARAM_PFVF(STAG_END);
		param[2] = FW_PARAM_PFVF(RQ_START);
		param[3] = FW_PARAM_PFVF(RQ_END);
		param[4] = FW_PARAM_PFVF(PBL_START);
		param[5] = FW_PARAM_PFVF(PBL_END);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 6, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to query RDMA parameters(1): %d.\n", rc);
			return (rc);
		}
		sc->vres.stag.start = val[0];
		sc->vres.stag.size = val[1] - val[0] + 1;
		sc->vres.rq.start = val[2];
		sc->vres.rq.size = val[3] - val[2] + 1;
		sc->vres.pbl.start = val[4];
		sc->vres.pbl.size = val[5] - val[4] + 1;

		param[0] = FW_PARAM_PFVF(SQRQ_START);
		param[1] = FW_PARAM_PFVF(SQRQ_END);
		param[2] = FW_PARAM_PFVF(CQ_START);
		param[3] = FW_PARAM_PFVF(CQ_END);
		param[4] = FW_PARAM_PFVF(OCQ_START);
		param[5] = FW_PARAM_PFVF(OCQ_END);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 6, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to query RDMA parameters(2): %d.\n", rc);
			return (rc);
		}
		sc->vres.qp.start = val[0];
		sc->vres.qp.size = val[1] - val[0] + 1;
		sc->vres.cq.start = val[2];
		sc->vres.cq.size = val[3] - val[2] + 1;
		sc->vres.ocq.start = val[4];
		sc->vres.ocq.size = val[5] - val[4] + 1;

		param[0] = FW_PARAM_PFVF(SRQ_START);
		param[1] = FW_PARAM_PFVF(SRQ_END);
		param[2] = FW_PARAM_DEV(MAXORDIRD_QP);
		param[3] = FW_PARAM_DEV(MAXIRD_ADAPTER);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 4, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to query RDMA parameters(3): %d.\n", rc);
			return (rc);
		}
		sc->vres.srq.start = val[0];
		sc->vres.srq.size = val[1] - val[0] + 1;
		sc->params.max_ordird_qp = val[2];
		sc->params.max_ird_adapter = val[3];
	}
	if (sc->iscsicaps) {
		param[0] = FW_PARAM_PFVF(ISCSI_START);
		param[1] = FW_PARAM_PFVF(ISCSI_END);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 2, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to query iSCSI parameters: %d.\n", rc);
			return (rc);
		}
		sc->vres.iscsi.start = val[0];
		sc->vres.iscsi.size = val[1] - val[0] + 1;
	}
	if (sc->cryptocaps & FW_CAPS_CONFIG_TLSKEYS) {
		param[0] = FW_PARAM_PFVF(TLS_START);
		param[1] = FW_PARAM_PFVF(TLS_END);
		rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 2, param, val);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to query TLS parameters: %d.\n", rc);
			return (rc);
		}
		sc->vres.key.start = val[0];
		sc->vres.key.size = val[1] - val[0] + 1;
	}

	t4_init_sge_params(sc);

	/*
	 * We've got the params we wanted to query via the firmware.  Now grab
	 * some others directly from the chip.
	 */
	rc = t4_read_chip_settings(sc);

	return (rc);
}

static int
set_params__post_init(struct adapter *sc)
{
	uint32_t param, val;
#ifdef TCP_OFFLOAD
	int i, v, shift;
#endif

	/* ask for encapsulated CPLs */
	param = FW_PARAM_PFVF(CPLFW4MSG_ENCAP);
	val = 1;
	(void)t4_set_params(sc, sc->mbox, sc->pf, 0, 1, &param, &val);

	/* Enable 32b port caps if the firmware supports it. */
	param = FW_PARAM_PFVF(PORT_CAPS32);
	val = 1;
	if (t4_set_params(sc, sc->mbox, sc->pf, 0, 1, &param, &val) == 0)
		sc->params.port_caps32 = 1;

	/* Let filter + maskhash steer to a part of the VI's RSS region. */
	val = 1 << (G_MASKSIZE(t4_read_reg(sc, A_TP_RSS_CONFIG_TNL)) - 1);
	t4_set_reg_field(sc, A_TP_RSS_CONFIG_TNL, V_MASKFILTER(M_MASKFILTER),
	    V_MASKFILTER(val - 1));

#ifdef TCP_OFFLOAD
	/*
	 * Override the TOE timers with user provided tunables.  This is not the
	 * recommended way to change the timers (the firmware config file is) so
	 * these tunables are not documented.
	 *
	 * All the timer tunables are in microseconds.
	 */
	if (t4_toe_keepalive_idle != 0) {
		v = us_to_tcp_ticks(sc, t4_toe_keepalive_idle);
		v &= M_KEEPALIVEIDLE;
		t4_set_reg_field(sc, A_TP_KEEP_IDLE,
		    V_KEEPALIVEIDLE(M_KEEPALIVEIDLE), V_KEEPALIVEIDLE(v));
	}
	if (t4_toe_keepalive_interval != 0) {
		v = us_to_tcp_ticks(sc, t4_toe_keepalive_interval);
		v &= M_KEEPALIVEINTVL;
		t4_set_reg_field(sc, A_TP_KEEP_INTVL,
		    V_KEEPALIVEINTVL(M_KEEPALIVEINTVL), V_KEEPALIVEINTVL(v));
	}
	if (t4_toe_keepalive_count != 0) {
		v = t4_toe_keepalive_count & M_KEEPALIVEMAXR2;
		t4_set_reg_field(sc, A_TP_SHIFT_CNT,
		    V_KEEPALIVEMAXR1(M_KEEPALIVEMAXR1) |
		    V_KEEPALIVEMAXR2(M_KEEPALIVEMAXR2),
		    V_KEEPALIVEMAXR1(1) | V_KEEPALIVEMAXR2(v));
	}
	if (t4_toe_rexmt_min != 0) {
		v = us_to_tcp_ticks(sc, t4_toe_rexmt_min);
		v &= M_RXTMIN;
		t4_set_reg_field(sc, A_TP_RXT_MIN,
		    V_RXTMIN(M_RXTMIN), V_RXTMIN(v));
	}
	if (t4_toe_rexmt_max != 0) {
		v = us_to_tcp_ticks(sc, t4_toe_rexmt_max);
		v &= M_RXTMAX;
		t4_set_reg_field(sc, A_TP_RXT_MAX,
		    V_RXTMAX(M_RXTMAX), V_RXTMAX(v));
	}
	if (t4_toe_rexmt_count != 0) {
		v = t4_toe_rexmt_count & M_RXTSHIFTMAXR2;
		t4_set_reg_field(sc, A_TP_SHIFT_CNT,
		    V_RXTSHIFTMAXR1(M_RXTSHIFTMAXR1) |
		    V_RXTSHIFTMAXR2(M_RXTSHIFTMAXR2),
		    V_RXTSHIFTMAXR1(1) | V_RXTSHIFTMAXR2(v));
	}
	for (i = 0; i < nitems(t4_toe_rexmt_backoff); i++) {
		if (t4_toe_rexmt_backoff[i] != -1) {
			v = t4_toe_rexmt_backoff[i] & M_TIMERBACKOFFINDEX0;
			shift = (i & 3) << 3;
			t4_set_reg_field(sc, A_TP_TCP_BACKOFF_REG0 + (i & ~3),
			    M_TIMERBACKOFFINDEX0 << shift, v << shift);
		}
	}
#endif
	return (0);
}

#undef FW_PARAM_PFVF
#undef FW_PARAM_DEV

static void
t4_set_desc(struct adapter *sc)
{
	char buf[128];
	struct adapter_params *p = &sc->params;

	snprintf(buf, sizeof(buf), "Chelsio %s", p->vpd.id);

	device_set_desc_copy(sc->dev, buf);
}

static inline void
ifmedia_add4(struct ifmedia *ifm, int m)
{

	ifmedia_add(ifm, m, 0, NULL);
	ifmedia_add(ifm, m | IFM_ETH_TXPAUSE, 0, NULL);
	ifmedia_add(ifm, m | IFM_ETH_RXPAUSE, 0, NULL);
	ifmedia_add(ifm, m | IFM_ETH_TXPAUSE | IFM_ETH_RXPAUSE, 0, NULL);
}

/*
 * This is the selected media, which is not quite the same as the active media.
 * The media line in ifconfig is "media: Ethernet selected (active)" if selected
 * and active are not the same, and "media: Ethernet selected" otherwise.
 */
static void
set_current_media(struct port_info *pi)
{
	struct link_config *lc;
	struct ifmedia *ifm;
	int mword;
	u_int speed;

	PORT_LOCK_ASSERT_OWNED(pi);

	/* Leave current media alone if it's already set to IFM_NONE. */
	ifm = &pi->media;
	if (ifm->ifm_cur != NULL &&
	    IFM_SUBTYPE(ifm->ifm_cur->ifm_media) == IFM_NONE)
		return;

	lc = &pi->link_cfg;
	if (lc->requested_aneg != AUTONEG_DISABLE &&
	    lc->supported & FW_PORT_CAP32_ANEG) {
		ifmedia_set(ifm, IFM_ETHER | IFM_AUTO);
		return;
	}
	mword = IFM_ETHER | IFM_FDX;
	if (lc->requested_fc & PAUSE_TX)
		mword |= IFM_ETH_TXPAUSE;
	if (lc->requested_fc & PAUSE_RX)
		mword |= IFM_ETH_RXPAUSE;
	if (lc->requested_speed == 0)
		speed = port_top_speed(pi) * 1000;	/* Gbps -> Mbps */
	else
		speed = lc->requested_speed;
	mword |= port_mword(pi, speed_to_fwcap(speed));
	ifmedia_set(ifm, mword);
}

/*
 * Returns true if the ifmedia list for the port cannot change.
 */
static bool
fixed_ifmedia(struct port_info *pi)
{

	return (pi->port_type == FW_PORT_TYPE_BT_SGMII ||
	    pi->port_type == FW_PORT_TYPE_BT_XFI ||
	    pi->port_type == FW_PORT_TYPE_BT_XAUI ||
	    pi->port_type == FW_PORT_TYPE_KX4 ||
	    pi->port_type == FW_PORT_TYPE_KX ||
	    pi->port_type == FW_PORT_TYPE_KR ||
	    pi->port_type == FW_PORT_TYPE_BP_AP ||
	    pi->port_type == FW_PORT_TYPE_BP4_AP ||
	    pi->port_type == FW_PORT_TYPE_BP40_BA ||
	    pi->port_type == FW_PORT_TYPE_KR4_100G ||
	    pi->port_type == FW_PORT_TYPE_KR_SFP28 ||
	    pi->port_type == FW_PORT_TYPE_KR_XLAUI);
}

static void
build_medialist(struct port_info *pi)
{
	uint32_t ss, speed;
	int unknown, mword, bit;
	struct link_config *lc;
	struct ifmedia *ifm;

	PORT_LOCK_ASSERT_OWNED(pi);

	if (pi->flags & FIXED_IFMEDIA)
		return;

	/*
	 * Rebuild the ifmedia list.
	 */
	ifm = &pi->media;
	ifmedia_removeall(ifm);
	lc = &pi->link_cfg;
	ss = G_FW_PORT_CAP32_SPEED(lc->supported); /* Supported Speeds */
	if (__predict_false(ss == 0)) {	/* not supposed to happen. */
		MPASS(ss != 0);
no_media:
		MPASS(LIST_EMPTY(&ifm->ifm_list));
		ifmedia_add(ifm, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(ifm, IFM_ETHER | IFM_NONE);
		return;
	}

	unknown = 0;
	for (bit = S_FW_PORT_CAP32_SPEED; bit < fls(ss); bit++) {
		speed = 1 << bit;
		MPASS(speed & M_FW_PORT_CAP32_SPEED);
		if (ss & speed) {
			mword = port_mword(pi, speed);
			if (mword == IFM_NONE) {
				goto no_media;
			} else if (mword == IFM_UNKNOWN)
				unknown++;
			else
				ifmedia_add4(ifm, IFM_ETHER | IFM_FDX | mword);
		}
	}
	if (unknown > 0) /* Add one unknown for all unknown media types. */
		ifmedia_add4(ifm, IFM_ETHER | IFM_FDX | IFM_UNKNOWN);
	if (lc->supported & FW_PORT_CAP32_ANEG)
		ifmedia_add(ifm, IFM_ETHER | IFM_AUTO, 0, NULL);

	set_current_media(pi);
}

/*
 * Initialize the requested fields in the link config based on driver tunables.
 */
static void
init_link_config(struct port_info *pi)
{
	struct link_config *lc = &pi->link_cfg;

	PORT_LOCK_ASSERT_OWNED(pi);

	lc->requested_speed = 0;

	if (t4_autoneg == 0)
		lc->requested_aneg = AUTONEG_DISABLE;
	else if (t4_autoneg == 1)
		lc->requested_aneg = AUTONEG_ENABLE;
	else
		lc->requested_aneg = AUTONEG_AUTO;

	lc->requested_fc = t4_pause_settings & (PAUSE_TX | PAUSE_RX |
	    PAUSE_AUTONEG);

	if (t4_fec == -1 || t4_fec & FEC_AUTO)
		lc->requested_fec = FEC_AUTO;
	else {
		lc->requested_fec = FEC_NONE;
		if (t4_fec & FEC_RS)
			lc->requested_fec |= FEC_RS;
		if (t4_fec & FEC_BASER_RS)
			lc->requested_fec |= FEC_BASER_RS;
	}
}

/*
 * Makes sure that all requested settings comply with what's supported by the
 * port.  Returns the number of settings that were invalid and had to be fixed.
 */
static int
fixup_link_config(struct port_info *pi)
{
	int n = 0;
	struct link_config *lc = &pi->link_cfg;
	uint32_t fwspeed;

	PORT_LOCK_ASSERT_OWNED(pi);

	/* Speed (when not autonegotiating) */
	if (lc->requested_speed != 0) {
		fwspeed = speed_to_fwcap(lc->requested_speed);
		if ((fwspeed & lc->supported) == 0) {
			n++;
			lc->requested_speed = 0;
		}
	}

	/* Link autonegotiation */
	MPASS(lc->requested_aneg == AUTONEG_ENABLE ||
	    lc->requested_aneg == AUTONEG_DISABLE ||
	    lc->requested_aneg == AUTONEG_AUTO);
	if (lc->requested_aneg == AUTONEG_ENABLE &&
	    !(lc->supported & FW_PORT_CAP32_ANEG)) {
		n++;
		lc->requested_aneg = AUTONEG_AUTO;
	}

	/* Flow control */
	MPASS((lc->requested_fc & ~(PAUSE_TX | PAUSE_RX | PAUSE_AUTONEG)) == 0);
	if (lc->requested_fc & PAUSE_TX &&
	    !(lc->supported & FW_PORT_CAP32_FC_TX)) {
		n++;
		lc->requested_fc &= ~PAUSE_TX;
	}
	if (lc->requested_fc & PAUSE_RX &&
	    !(lc->supported & FW_PORT_CAP32_FC_RX)) {
		n++;
		lc->requested_fc &= ~PAUSE_RX;
	}
	if (!(lc->requested_fc & PAUSE_AUTONEG) &&
	    !(lc->supported & FW_PORT_CAP32_FORCE_PAUSE)) {
		n++;
		lc->requested_fc |= PAUSE_AUTONEG;
	}

	/* FEC */
	if ((lc->requested_fec & FEC_RS &&
	    !(lc->supported & FW_PORT_CAP32_FEC_RS)) ||
	    (lc->requested_fec & FEC_BASER_RS &&
	    !(lc->supported & FW_PORT_CAP32_FEC_BASER_RS))) {
		n++;
		lc->requested_fec = FEC_AUTO;
	}

	return (n);
}

/*
 * Apply the requested L1 settings, which are expected to be valid, to the
 * hardware.
 */
static int
apply_link_config(struct port_info *pi)
{
	struct adapter *sc = pi->adapter;
	struct link_config *lc = &pi->link_cfg;
	int rc;

#ifdef INVARIANTS
	ASSERT_SYNCHRONIZED_OP(sc);
	PORT_LOCK_ASSERT_OWNED(pi);

	if (lc->requested_aneg == AUTONEG_ENABLE)
		MPASS(lc->supported & FW_PORT_CAP32_ANEG);
	if (!(lc->requested_fc & PAUSE_AUTONEG))
		MPASS(lc->supported & FW_PORT_CAP32_FORCE_PAUSE);
	if (lc->requested_fc & PAUSE_TX)
		MPASS(lc->supported & FW_PORT_CAP32_FC_TX);
	if (lc->requested_fc & PAUSE_RX)
		MPASS(lc->supported & FW_PORT_CAP32_FC_RX);
	if (lc->requested_fec & FEC_RS)
		MPASS(lc->supported & FW_PORT_CAP32_FEC_RS);
	if (lc->requested_fec & FEC_BASER_RS)
		MPASS(lc->supported & FW_PORT_CAP32_FEC_BASER_RS);
#endif
	rc = -t4_link_l1cfg(sc, sc->mbox, pi->tx_chan, lc);
	if (rc != 0) {
		/* Don't complain if the VF driver gets back an EPERM. */
		if (!(sc->flags & IS_VF) || rc != FW_EPERM)
			device_printf(pi->dev, "l1cfg failed: %d\n", rc);
	} else {
		/*
		 * An L1_CFG will almost always result in a link-change event if
		 * the link is up, and the driver will refresh the actual
		 * fec/fc/etc. when the notification is processed.  If the link
		 * is down then the actual settings are meaningless.
		 *
		 * This takes care of the case where a change in the L1 settings
		 * may not result in a notification.
		 */
		if (lc->link_ok && !(lc->requested_fc & PAUSE_AUTONEG))
			lc->fc = lc->requested_fc & (PAUSE_TX | PAUSE_RX);
	}
	return (rc);
}

#define FW_MAC_EXACT_CHUNK	7

/*
 * Program the port's XGMAC based on parameters in ifnet.  The caller also
 * indicates which parameters should be programmed (the rest are left alone).
 */
int
update_mac_settings(struct ifnet *ifp, int flags)
{
	int rc = 0;
	struct vi_info *vi = ifp->if_softc;
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	int mtu = -1, promisc = -1, allmulti = -1, vlanex = -1;

	ASSERT_SYNCHRONIZED_OP(sc);
	KASSERT(flags, ("%s: not told what to update.", __func__));

	if (flags & XGMAC_MTU)
		mtu = ifp->if_mtu;

	if (flags & XGMAC_PROMISC)
		promisc = ifp->if_flags & IFF_PROMISC ? 1 : 0;

	if (flags & XGMAC_ALLMULTI)
		allmulti = ifp->if_flags & IFF_ALLMULTI ? 1 : 0;

	if (flags & XGMAC_VLANEX)
		vlanex = ifp->if_capenable & IFCAP_VLAN_HWTAGGING ? 1 : 0;

	if (flags & (XGMAC_MTU|XGMAC_PROMISC|XGMAC_ALLMULTI|XGMAC_VLANEX)) {
		rc = -t4_set_rxmode(sc, sc->mbox, vi->viid, mtu, promisc,
		    allmulti, 1, vlanex, false);
		if (rc) {
			if_printf(ifp, "set_rxmode (%x) failed: %d\n", flags,
			    rc);
			return (rc);
		}
	}

	if (flags & XGMAC_UCADDR) {
		uint8_t ucaddr[ETHER_ADDR_LEN];

		bcopy(IF_LLADDR(ifp), ucaddr, sizeof(ucaddr));
		rc = t4_change_mac(sc, sc->mbox, vi->viid, vi->xact_addr_filt,
		    ucaddr, true, &vi->smt_idx);
		if (rc < 0) {
			rc = -rc;
			if_printf(ifp, "change_mac failed: %d\n", rc);
			return (rc);
		} else {
			vi->xact_addr_filt = rc;
			rc = 0;
		}
	}

	if (flags & XGMAC_MCADDRS) {
		const uint8_t *mcaddr[FW_MAC_EXACT_CHUNK];
		int del = 1;
		uint64_t hash = 0;
		struct ifmultiaddr *ifma;
		int i = 0, j;

		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			mcaddr[i] =
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
			MPASS(ETHER_IS_MULTICAST(mcaddr[i]));
			i++;

			if (i == FW_MAC_EXACT_CHUNK) {
				rc = t4_alloc_mac_filt(sc, sc->mbox, vi->viid,
				    del, i, mcaddr, NULL, &hash, 0);
				if (rc < 0) {
					rc = -rc;
					for (j = 0; j < i; j++) {
						if_printf(ifp,
						    "failed to add mc address"
						    " %02x:%02x:%02x:"
						    "%02x:%02x:%02x rc=%d\n",
						    mcaddr[j][0], mcaddr[j][1],
						    mcaddr[j][2], mcaddr[j][3],
						    mcaddr[j][4], mcaddr[j][5],
						    rc);
					}
					goto mcfail;
				}
				del = 0;
				i = 0;
			}
		}
		if (i > 0) {
			rc = t4_alloc_mac_filt(sc, sc->mbox, vi->viid, del, i,
			    mcaddr, NULL, &hash, 0);
			if (rc < 0) {
				rc = -rc;
				for (j = 0; j < i; j++) {
					if_printf(ifp,
					    "failed to add mc address"
					    " %02x:%02x:%02x:"
					    "%02x:%02x:%02x rc=%d\n",
					    mcaddr[j][0], mcaddr[j][1],
					    mcaddr[j][2], mcaddr[j][3],
					    mcaddr[j][4], mcaddr[j][5],
					    rc);
				}
				goto mcfail;
			}
		}

		rc = -t4_set_addr_hash(sc, sc->mbox, vi->viid, 0, hash, 0);
		if (rc != 0)
			if_printf(ifp, "failed to set mc address hash: %d", rc);
mcfail:
		if_maddr_runlock(ifp);
	}

	return (rc);
}

/*
 * {begin|end}_synchronized_op must be called from the same thread.
 */
int
begin_synchronized_op(struct adapter *sc, struct vi_info *vi, int flags,
    char *wmesg)
{
	int rc, pri;

#ifdef WITNESS
	/* the caller thinks it's ok to sleep, but is it really? */
	if (flags & SLEEP_OK)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "begin_synchronized_op");
#endif

	if (INTR_OK)
		pri = PCATCH;
	else
		pri = 0;

	ADAPTER_LOCK(sc);
	for (;;) {

		if (vi && IS_DOOMED(vi)) {
			rc = ENXIO;
			goto done;
		}

		if (!IS_BUSY(sc)) {
			rc = 0;
			break;
		}

		if (!(flags & SLEEP_OK)) {
			rc = EBUSY;
			goto done;
		}

		if (mtx_sleep(&sc->flags, &sc->sc_lock, pri, wmesg, 0)) {
			rc = EINTR;
			goto done;
		}
	}

	KASSERT(!IS_BUSY(sc), ("%s: controller busy.", __func__));
	SET_BUSY(sc);
#ifdef INVARIANTS
	sc->last_op = wmesg;
	sc->last_op_thr = curthread;
	sc->last_op_flags = flags;
#endif

done:
	if (!(flags & HOLD_LOCK) || rc)
		ADAPTER_UNLOCK(sc);

	return (rc);
}

/*
 * Tell if_ioctl and if_init that the VI is going away.  This is
 * special variant of begin_synchronized_op and must be paired with a
 * call to end_synchronized_op.
 */
void
doom_vi(struct adapter *sc, struct vi_info *vi)
{

	ADAPTER_LOCK(sc);
	SET_DOOMED(vi);
	wakeup(&sc->flags);
	while (IS_BUSY(sc))
		mtx_sleep(&sc->flags, &sc->sc_lock, 0, "t4detach", 0);
	SET_BUSY(sc);
#ifdef INVARIANTS
	sc->last_op = "t4detach";
	sc->last_op_thr = curthread;
	sc->last_op_flags = 0;
#endif
	ADAPTER_UNLOCK(sc);
}

/*
 * {begin|end}_synchronized_op must be called from the same thread.
 */
void
end_synchronized_op(struct adapter *sc, int flags)
{

	if (flags & LOCK_HELD)
		ADAPTER_LOCK_ASSERT_OWNED(sc);
	else
		ADAPTER_LOCK(sc);

	KASSERT(IS_BUSY(sc), ("%s: controller not busy.", __func__));
	CLR_BUSY(sc);
	wakeup(&sc->flags);
	ADAPTER_UNLOCK(sc);
}

static int
cxgbe_init_synchronized(struct vi_info *vi)
{
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	struct ifnet *ifp = vi->ifp;
	int rc = 0, i;
	struct sge_txq *txq;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return (0);	/* already running */

	if (!(sc->flags & FULL_INIT_DONE) &&
	    ((rc = adapter_full_init(sc)) != 0))
		return (rc);	/* error message displayed already */

	if (!(vi->flags & VI_INIT_DONE) &&
	    ((rc = vi_full_init(vi)) != 0))
		return (rc); /* error message displayed already */

	rc = update_mac_settings(ifp, XGMAC_ALL);
	if (rc)
		goto done;	/* error message displayed already */

	PORT_LOCK(pi);
	if (pi->up_vis == 0) {
		t4_update_port_info(pi);
		fixup_link_config(pi);
		build_medialist(pi);
		apply_link_config(pi);
	}

	rc = -t4_enable_vi(sc, sc->mbox, vi->viid, true, true);
	if (rc != 0) {
		if_printf(ifp, "enable_vi failed: %d\n", rc);
		PORT_UNLOCK(pi);
		goto done;
	}

	/*
	 * Can't fail from this point onwards.  Review cxgbe_uninit_synchronized
	 * if this changes.
	 */

	for_each_txq(vi, i, txq) {
		TXQ_LOCK(txq);
		txq->eq.flags |= EQ_ENABLED;
		TXQ_UNLOCK(txq);
	}

	/*
	 * The first iq of the first port to come up is used for tracing.
	 */
	if (sc->traceq < 0 && IS_MAIN_VI(vi)) {
		sc->traceq = sc->sge.rxq[vi->first_rxq].iq.abs_id;
		t4_write_reg(sc, is_t4(sc) ?  A_MPS_TRC_RSS_CONTROL :
		    A_MPS_T5_TRC_RSS_CONTROL, V_RSSCONTROL(pi->tx_chan) |
		    V_QUEUENUMBER(sc->traceq));
		pi->flags |= HAS_TRACEQ;
	}

	/* all ok */
	pi->up_vis++;
	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	if (pi->nvi > 1 || sc->flags & IS_VF)
		callout_reset(&vi->tick, hz, vi_tick, vi);
	else
		callout_reset(&pi->tick, hz, cxgbe_tick, pi);
	if (pi->link_cfg.link_ok)
		t4_os_link_changed(pi);
	PORT_UNLOCK(pi);
done:
	if (rc != 0)
		cxgbe_uninit_synchronized(vi);

	return (rc);
}

/*
 * Idempotent.
 */
static int
cxgbe_uninit_synchronized(struct vi_info *vi)
{
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	struct ifnet *ifp = vi->ifp;
	int rc, i;
	struct sge_txq *txq;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (!(vi->flags & VI_INIT_DONE)) {
		if (__predict_false(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
			KASSERT(0, ("uninited VI is running"));
			if_printf(ifp, "uninited VI with running ifnet.  "
			    "vi->flags 0x%016lx, if_flags 0x%08x, "
			    "if_drv_flags 0x%08x\n", vi->flags, ifp->if_flags,
			    ifp->if_drv_flags);
		}
		return (0);
	}

	/*
	 * Disable the VI so that all its data in either direction is discarded
	 * by the MPS.  Leave everything else (the queues, interrupts, and 1Hz
	 * tick) intact as the TP can deliver negative advice or data that it's
	 * holding in its RAM (for an offloaded connection) even after the VI is
	 * disabled.
	 */
	rc = -t4_enable_vi(sc, sc->mbox, vi->viid, false, false);
	if (rc) {
		if_printf(ifp, "disable_vi failed: %d\n", rc);
		return (rc);
	}

	for_each_txq(vi, i, txq) {
		TXQ_LOCK(txq);
		txq->eq.flags &= ~EQ_ENABLED;
		TXQ_UNLOCK(txq);
	}

	PORT_LOCK(pi);
	if (pi->nvi > 1 || sc->flags & IS_VF)
		callout_stop(&vi->tick);
	else
		callout_stop(&pi->tick);
	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		PORT_UNLOCK(pi);
		return (0);
	}
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	pi->up_vis--;
	if (pi->up_vis > 0) {
		PORT_UNLOCK(pi);
		return (0);
	}

	pi->link_cfg.link_ok = false;
	pi->link_cfg.speed = 0;
	pi->link_cfg.link_down_rc = 255;
	t4_os_link_changed(pi);
	PORT_UNLOCK(pi);

	return (0);
}

/*
 * It is ok for this function to fail midway and return right away.  t4_detach
 * will walk the entire sc->irq list and clean up whatever is valid.
 */
int
t4_setup_intr_handlers(struct adapter *sc)
{
	int rc, rid, p, q, v;
	char s[8];
	struct irq *irq;
	struct port_info *pi;
	struct vi_info *vi;
	struct sge *sge = &sc->sge;
	struct sge_rxq *rxq;
#ifdef TCP_OFFLOAD
	struct sge_ofld_rxq *ofld_rxq;
#endif
#ifdef DEV_NETMAP
	struct sge_nm_rxq *nm_rxq;
#endif
#ifdef RSS
	int nbuckets = rss_getnumbuckets();
#endif

	/*
	 * Setup interrupts.
	 */
	irq = &sc->irq[0];
	rid = sc->intr_type == INTR_INTX ? 0 : 1;
	if (forwarding_intr_to_fwq(sc))
		return (t4_alloc_irq(sc, irq, rid, t4_intr_all, sc, "all"));

	/* Multiple interrupts. */
	if (sc->flags & IS_VF)
		KASSERT(sc->intr_count >= T4VF_EXTRA_INTR + sc->params.nports,
		    ("%s: too few intr.", __func__));
	else
		KASSERT(sc->intr_count >= T4_EXTRA_INTR + sc->params.nports,
		    ("%s: too few intr.", __func__));

	/* The first one is always error intr on PFs */
	if (!(sc->flags & IS_VF)) {
		rc = t4_alloc_irq(sc, irq, rid, t4_intr_err, sc, "err");
		if (rc != 0)
			return (rc);
		irq++;
		rid++;
	}

	/* The second one is always the firmware event queue (first on VFs) */
	rc = t4_alloc_irq(sc, irq, rid, t4_intr_evt, &sge->fwq, "evt");
	if (rc != 0)
		return (rc);
	irq++;
	rid++;

	for_each_port(sc, p) {
		pi = sc->port[p];
		for_each_vi(pi, v, vi) {
			vi->first_intr = rid - 1;

			if (vi->nnmrxq > 0) {
				int n = max(vi->nrxq, vi->nnmrxq);

				rxq = &sge->rxq[vi->first_rxq];
#ifdef DEV_NETMAP
				nm_rxq = &sge->nm_rxq[vi->first_nm_rxq];
#endif
				for (q = 0; q < n; q++) {
					snprintf(s, sizeof(s), "%x%c%x", p,
					    'a' + v, q);
					if (q < vi->nrxq)
						irq->rxq = rxq++;
#ifdef DEV_NETMAP
					if (q < vi->nnmrxq)
						irq->nm_rxq = nm_rxq++;

					if (irq->nm_rxq != NULL &&
					    irq->rxq == NULL) {
						/* Netmap rx only */
						rc = t4_alloc_irq(sc, irq, rid,
						    t4_nm_intr, irq->nm_rxq, s);
					}
					if (irq->nm_rxq != NULL &&
					    irq->rxq != NULL) {
						/* NIC and Netmap rx */
						rc = t4_alloc_irq(sc, irq, rid,
						    t4_vi_intr, irq, s);
					}
#endif
					if (irq->rxq != NULL &&
					    irq->nm_rxq == NULL) {
						/* NIC rx only */
						rc = t4_alloc_irq(sc, irq, rid,
						    t4_intr, irq->rxq, s);
					}
					if (rc != 0)
						return (rc);
#ifdef RSS
					if (q < vi->nrxq) {
						bus_bind_intr(sc->dev, irq->res,
						    rss_getcpu(q % nbuckets));
					}
#endif
					irq++;
					rid++;
					vi->nintr++;
				}
			} else {
				for_each_rxq(vi, q, rxq) {
					snprintf(s, sizeof(s), "%x%c%x", p,
					    'a' + v, q);
					rc = t4_alloc_irq(sc, irq, rid,
					    t4_intr, rxq, s);
					if (rc != 0)
						return (rc);
#ifdef RSS
					bus_bind_intr(sc->dev, irq->res,
					    rss_getcpu(q % nbuckets));
#endif
					irq++;
					rid++;
					vi->nintr++;
				}
			}
#ifdef TCP_OFFLOAD
			for_each_ofld_rxq(vi, q, ofld_rxq) {
				snprintf(s, sizeof(s), "%x%c%x", p, 'A' + v, q);
				rc = t4_alloc_irq(sc, irq, rid, t4_intr,
				    ofld_rxq, s);
				if (rc != 0)
					return (rc);
				irq++;
				rid++;
				vi->nintr++;
			}
#endif
		}
	}
	MPASS(irq == &sc->irq[sc->intr_count]);

	return (0);
}

int
adapter_full_init(struct adapter *sc)
{
	int rc, i;
#ifdef RSS
	uint32_t raw_rss_key[RSS_KEYSIZE / sizeof(uint32_t)];
	uint32_t rss_key[RSS_KEYSIZE / sizeof(uint32_t)];
#endif

	ASSERT_SYNCHRONIZED_OP(sc);
	ADAPTER_LOCK_ASSERT_NOTOWNED(sc);
	KASSERT((sc->flags & FULL_INIT_DONE) == 0,
	    ("%s: FULL_INIT_DONE already", __func__));

	/*
	 * queues that belong to the adapter (not any particular port).
	 */
	rc = t4_setup_adapter_queues(sc);
	if (rc != 0)
		goto done;

	for (i = 0; i < nitems(sc->tq); i++) {
		sc->tq[i] = taskqueue_create("t4 taskq", M_NOWAIT,
		    taskqueue_thread_enqueue, &sc->tq[i]);
		if (sc->tq[i] == NULL) {
			device_printf(sc->dev,
			    "failed to allocate task queue %d\n", i);
			rc = ENOMEM;
			goto done;
		}
		taskqueue_start_threads(&sc->tq[i], 1, PI_NET, "%s tq%d",
		    device_get_nameunit(sc->dev), i);
	}
#ifdef RSS
	MPASS(RSS_KEYSIZE == 40);
	rss_getkey((void *)&raw_rss_key[0]);
	for (i = 0; i < nitems(rss_key); i++) {
		rss_key[i] = htobe32(raw_rss_key[nitems(rss_key) - 1 - i]);
	}
	t4_write_rss_key(sc, &rss_key[0], -1, 1);
#endif

	if (!(sc->flags & IS_VF))
		t4_intr_enable(sc);
	sc->flags |= FULL_INIT_DONE;
done:
	if (rc != 0)
		adapter_full_uninit(sc);

	return (rc);
}

int
adapter_full_uninit(struct adapter *sc)
{
	int i;

	ADAPTER_LOCK_ASSERT_NOTOWNED(sc);

	t4_teardown_adapter_queues(sc);

	for (i = 0; i < nitems(sc->tq) && sc->tq[i]; i++) {
		taskqueue_free(sc->tq[i]);
		sc->tq[i] = NULL;
	}

	sc->flags &= ~FULL_INIT_DONE;

	return (0);
}

#ifdef RSS
#define SUPPORTED_RSS_HASHTYPES (RSS_HASHTYPE_RSS_IPV4 | \
    RSS_HASHTYPE_RSS_TCP_IPV4 | RSS_HASHTYPE_RSS_IPV6 | \
    RSS_HASHTYPE_RSS_TCP_IPV6 | RSS_HASHTYPE_RSS_UDP_IPV4 | \
    RSS_HASHTYPE_RSS_UDP_IPV6)

/* Translates kernel hash types to hardware. */
static int
hashconfig_to_hashen(int hashconfig)
{
	int hashen = 0;

	if (hashconfig & RSS_HASHTYPE_RSS_IPV4)
		hashen |= F_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN;
	if (hashconfig & RSS_HASHTYPE_RSS_IPV6)
		hashen |= F_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN;
	if (hashconfig & RSS_HASHTYPE_RSS_UDP_IPV4) {
		hashen |= F_FW_RSS_VI_CONFIG_CMD_UDPEN |
		    F_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN;
	}
	if (hashconfig & RSS_HASHTYPE_RSS_UDP_IPV6) {
		hashen |= F_FW_RSS_VI_CONFIG_CMD_UDPEN |
		    F_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN;
	}
	if (hashconfig & RSS_HASHTYPE_RSS_TCP_IPV4)
		hashen |= F_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN;
	if (hashconfig & RSS_HASHTYPE_RSS_TCP_IPV6)
		hashen |= F_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN;

	return (hashen);
}

/* Translates hardware hash types to kernel. */
static int
hashen_to_hashconfig(int hashen)
{
	int hashconfig = 0;

	if (hashen & F_FW_RSS_VI_CONFIG_CMD_UDPEN) {
		/*
		 * If UDP hashing was enabled it must have been enabled for
		 * either IPv4 or IPv6 (inclusive or).  Enabling UDP without
		 * enabling any 4-tuple hash is nonsense configuration.
		 */
		MPASS(hashen & (F_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN |
		    F_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN));

		if (hashen & F_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN)
			hashconfig |= RSS_HASHTYPE_RSS_UDP_IPV4;
		if (hashen & F_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN)
			hashconfig |= RSS_HASHTYPE_RSS_UDP_IPV6;
	}
	if (hashen & F_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN)
		hashconfig |= RSS_HASHTYPE_RSS_TCP_IPV4;
	if (hashen & F_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN)
		hashconfig |= RSS_HASHTYPE_RSS_TCP_IPV6;
	if (hashen & F_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN)
		hashconfig |= RSS_HASHTYPE_RSS_IPV4;
	if (hashen & F_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN)
		hashconfig |= RSS_HASHTYPE_RSS_IPV6;

	return (hashconfig);
}
#endif

int
vi_full_init(struct vi_info *vi)
{
	struct adapter *sc = vi->pi->adapter;
	struct ifnet *ifp = vi->ifp;
	uint16_t *rss;
	struct sge_rxq *rxq;
	int rc, i, j;
#ifdef RSS
	int nbuckets = rss_getnumbuckets();
	int hashconfig = rss_gethashconfig();
	int extra;
#endif

	ASSERT_SYNCHRONIZED_OP(sc);
	KASSERT((vi->flags & VI_INIT_DONE) == 0,
	    ("%s: VI_INIT_DONE already", __func__));

	sysctl_ctx_init(&vi->ctx);
	vi->flags |= VI_SYSCTL_CTX;

	/*
	 * Allocate tx/rx/fl queues for this VI.
	 */
	rc = t4_setup_vi_queues(vi);
	if (rc != 0)
		goto done;	/* error message displayed already */

	/*
	 * Setup RSS for this VI.  Save a copy of the RSS table for later use.
	 */
	if (vi->nrxq > vi->rss_size) {
		if_printf(ifp, "nrxq (%d) > hw RSS table size (%d); "
		    "some queues will never receive traffic.\n", vi->nrxq,
		    vi->rss_size);
	} else if (vi->rss_size % vi->nrxq) {
		if_printf(ifp, "nrxq (%d), hw RSS table size (%d); "
		    "expect uneven traffic distribution.\n", vi->nrxq,
		    vi->rss_size);
	}
#ifdef RSS
	if (vi->nrxq != nbuckets) {
		if_printf(ifp, "nrxq (%d) != kernel RSS buckets (%d);"
		    "performance will be impacted.\n", vi->nrxq, nbuckets);
	}
#endif
	rss = malloc(vi->rss_size * sizeof (*rss), M_CXGBE, M_ZERO | M_WAITOK);
	for (i = 0; i < vi->rss_size;) {
#ifdef RSS
		j = rss_get_indirection_to_bucket(i);
		j %= vi->nrxq;
		rxq = &sc->sge.rxq[vi->first_rxq + j];
		rss[i++] = rxq->iq.abs_id;
#else
		for_each_rxq(vi, j, rxq) {
			rss[i++] = rxq->iq.abs_id;
			if (i == vi->rss_size)
				break;
		}
#endif
	}

	rc = -t4_config_rss_range(sc, sc->mbox, vi->viid, 0, vi->rss_size, rss,
	    vi->rss_size);
	if (rc != 0) {
		free(rss, M_CXGBE);
		if_printf(ifp, "rss_config failed: %d\n", rc);
		goto done;
	}

#ifdef RSS
	vi->hashen = hashconfig_to_hashen(hashconfig);

	/*
	 * We may have had to enable some hashes even though the global config
	 * wants them disabled.  This is a potential problem that must be
	 * reported to the user.
	 */
	extra = hashen_to_hashconfig(vi->hashen) ^ hashconfig;

	/*
	 * If we consider only the supported hash types, then the enabled hashes
	 * are a superset of the requested hashes.  In other words, there cannot
	 * be any supported hash that was requested but not enabled, but there
	 * can be hashes that were not requested but had to be enabled.
	 */
	extra &= SUPPORTED_RSS_HASHTYPES;
	MPASS((extra & hashconfig) == 0);

	if (extra) {
		if_printf(ifp,
		    "global RSS config (0x%x) cannot be accommodated.\n",
		    hashconfig);
	}
	if (extra & RSS_HASHTYPE_RSS_IPV4)
		if_printf(ifp, "IPv4 2-tuple hashing forced on.\n");
	if (extra & RSS_HASHTYPE_RSS_TCP_IPV4)
		if_printf(ifp, "TCP/IPv4 4-tuple hashing forced on.\n");
	if (extra & RSS_HASHTYPE_RSS_IPV6)
		if_printf(ifp, "IPv6 2-tuple hashing forced on.\n");
	if (extra & RSS_HASHTYPE_RSS_TCP_IPV6)
		if_printf(ifp, "TCP/IPv6 4-tuple hashing forced on.\n");
	if (extra & RSS_HASHTYPE_RSS_UDP_IPV4)
		if_printf(ifp, "UDP/IPv4 4-tuple hashing forced on.\n");
	if (extra & RSS_HASHTYPE_RSS_UDP_IPV6)
		if_printf(ifp, "UDP/IPv6 4-tuple hashing forced on.\n");
#else
	vi->hashen = F_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN |
	    F_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN |
	    F_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN |
	    F_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN | F_FW_RSS_VI_CONFIG_CMD_UDPEN;
#endif
	rc = -t4_config_vi_rss(sc, sc->mbox, vi->viid, vi->hashen, rss[0], 0, 0);
	if (rc != 0) {
		free(rss, M_CXGBE);
		if_printf(ifp, "rss hash/defaultq config failed: %d\n", rc);
		goto done;
	}

	vi->rss = rss;
	vi->flags |= VI_INIT_DONE;
done:
	if (rc != 0)
		vi_full_uninit(vi);

	return (rc);
}

/*
 * Idempotent.
 */
int
vi_full_uninit(struct vi_info *vi)
{
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	int i;
	struct sge_rxq *rxq;
	struct sge_txq *txq;
#ifdef TCP_OFFLOAD
	struct sge_ofld_rxq *ofld_rxq;
#endif
#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	struct sge_wrq *ofld_txq;
#endif

	if (vi->flags & VI_INIT_DONE) {

		/* Need to quiesce queues.  */

		/* XXX: Only for the first VI? */
		if (IS_MAIN_VI(vi) && !(sc->flags & IS_VF))
			quiesce_wrq(sc, &sc->sge.ctrlq[pi->port_id]);

		for_each_txq(vi, i, txq) {
			quiesce_txq(sc, txq);
		}

#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
		for_each_ofld_txq(vi, i, ofld_txq) {
			quiesce_wrq(sc, ofld_txq);
		}
#endif

		for_each_rxq(vi, i, rxq) {
			quiesce_iq(sc, &rxq->iq);
			quiesce_fl(sc, &rxq->fl);
		}

#ifdef TCP_OFFLOAD
		for_each_ofld_rxq(vi, i, ofld_rxq) {
			quiesce_iq(sc, &ofld_rxq->iq);
			quiesce_fl(sc, &ofld_rxq->fl);
		}
#endif
		free(vi->rss, M_CXGBE);
		free(vi->nm_rss, M_CXGBE);
	}

	t4_teardown_vi_queues(vi);
	vi->flags &= ~VI_INIT_DONE;

	return (0);
}

static void
quiesce_txq(struct adapter *sc, struct sge_txq *txq)
{
	struct sge_eq *eq = &txq->eq;
	struct sge_qstat *spg = (void *)&eq->desc[eq->sidx];

	(void) sc;	/* unused */

#ifdef INVARIANTS
	TXQ_LOCK(txq);
	MPASS((eq->flags & EQ_ENABLED) == 0);
	TXQ_UNLOCK(txq);
#endif

	/* Wait for the mp_ring to empty. */
	while (!mp_ring_is_idle(txq->r)) {
		mp_ring_check_drainage(txq->r, 0);
		pause("rquiesce", 1);
	}

	/* Then wait for the hardware to finish. */
	while (spg->cidx != htobe16(eq->pidx))
		pause("equiesce", 1);

	/* Finally, wait for the driver to reclaim all descriptors. */
	while (eq->cidx != eq->pidx)
		pause("dquiesce", 1);
}

static void
quiesce_wrq(struct adapter *sc, struct sge_wrq *wrq)
{

	/* XXXTX */
}

static void
quiesce_iq(struct adapter *sc, struct sge_iq *iq)
{
	(void) sc;	/* unused */

	/* Synchronize with the interrupt handler */
	while (!atomic_cmpset_int(&iq->state, IQS_IDLE, IQS_DISABLED))
		pause("iqfree", 1);
}

static void
quiesce_fl(struct adapter *sc, struct sge_fl *fl)
{
	mtx_lock(&sc->sfl_lock);
	FL_LOCK(fl);
	fl->flags |= FL_DOOMED;
	FL_UNLOCK(fl);
	callout_stop(&sc->sfl_callout);
	mtx_unlock(&sc->sfl_lock);

	KASSERT((fl->flags & FL_STARVING) == 0,
	    ("%s: still starving", __func__));
}

static int
t4_alloc_irq(struct adapter *sc, struct irq *irq, int rid,
    driver_intr_t *handler, void *arg, char *name)
{
	int rc;

	irq->rid = rid;
	irq->res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &irq->rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (irq->res == NULL) {
		device_printf(sc->dev,
		    "failed to allocate IRQ for rid %d, name %s.\n", rid, name);
		return (ENOMEM);
	}

	rc = bus_setup_intr(sc->dev, irq->res, INTR_MPSAFE | INTR_TYPE_NET,
	    NULL, handler, arg, &irq->tag);
	if (rc != 0) {
		device_printf(sc->dev,
		    "failed to setup interrupt for rid %d, name %s: %d\n",
		    rid, name, rc);
	} else if (name)
		bus_describe_intr(sc->dev, irq->res, irq->tag, "%s", name);

	return (rc);
}

static int
t4_free_irq(struct adapter *sc, struct irq *irq)
{
	if (irq->tag)
		bus_teardown_intr(sc->dev, irq->res, irq->tag);
	if (irq->res)
		bus_release_resource(sc->dev, SYS_RES_IRQ, irq->rid, irq->res);

	bzero(irq, sizeof(*irq));

	return (0);
}

static void
get_regs(struct adapter *sc, struct t4_regdump *regs, uint8_t *buf)
{

	regs->version = chip_id(sc) | chip_rev(sc) << 10;
	t4_get_regs(sc, buf, regs->len);
}

#define	A_PL_INDIR_CMD	0x1f8

#define	S_PL_AUTOINC	31
#define	M_PL_AUTOINC	0x1U
#define	V_PL_AUTOINC(x)	((x) << S_PL_AUTOINC)
#define	G_PL_AUTOINC(x)	(((x) >> S_PL_AUTOINC) & M_PL_AUTOINC)

#define	S_PL_VFID	20
#define	M_PL_VFID	0xffU
#define	V_PL_VFID(x)	((x) << S_PL_VFID)
#define	G_PL_VFID(x)	(((x) >> S_PL_VFID) & M_PL_VFID)

#define	S_PL_ADDR	0
#define	M_PL_ADDR	0xfffffU
#define	V_PL_ADDR(x)	((x) << S_PL_ADDR)
#define	G_PL_ADDR(x)	(((x) >> S_PL_ADDR) & M_PL_ADDR)

#define	A_PL_INDIR_DATA	0x1fc

static uint64_t
read_vf_stat(struct adapter *sc, u_int vin, int reg)
{
	u32 stats[2];

	mtx_assert(&sc->reg_lock, MA_OWNED);
	if (sc->flags & IS_VF) {
		stats[0] = t4_read_reg(sc, VF_MPS_REG(reg));
		stats[1] = t4_read_reg(sc, VF_MPS_REG(reg + 4));
	} else {
		t4_write_reg(sc, A_PL_INDIR_CMD, V_PL_AUTOINC(1) |
		    V_PL_VFID(vin) | V_PL_ADDR(VF_MPS_REG(reg)));
		stats[0] = t4_read_reg(sc, A_PL_INDIR_DATA);
		stats[1] = t4_read_reg(sc, A_PL_INDIR_DATA);
	}
	return (((uint64_t)stats[1]) << 32 | stats[0]);
}

static void
t4_get_vi_stats(struct adapter *sc, u_int vin, struct fw_vi_stats_vf *stats)
{

#define GET_STAT(name) \
	read_vf_stat(sc, vin, A_MPS_VF_STAT_##name##_L)

	stats->tx_bcast_bytes    = GET_STAT(TX_VF_BCAST_BYTES);
	stats->tx_bcast_frames   = GET_STAT(TX_VF_BCAST_FRAMES);
	stats->tx_mcast_bytes    = GET_STAT(TX_VF_MCAST_BYTES);
	stats->tx_mcast_frames   = GET_STAT(TX_VF_MCAST_FRAMES);
	stats->tx_ucast_bytes    = GET_STAT(TX_VF_UCAST_BYTES);
	stats->tx_ucast_frames   = GET_STAT(TX_VF_UCAST_FRAMES);
	stats->tx_drop_frames    = GET_STAT(TX_VF_DROP_FRAMES);
	stats->tx_offload_bytes  = GET_STAT(TX_VF_OFFLOAD_BYTES);
	stats->tx_offload_frames = GET_STAT(TX_VF_OFFLOAD_FRAMES);
	stats->rx_bcast_bytes    = GET_STAT(RX_VF_BCAST_BYTES);
	stats->rx_bcast_frames   = GET_STAT(RX_VF_BCAST_FRAMES);
	stats->rx_mcast_bytes    = GET_STAT(RX_VF_MCAST_BYTES);
	stats->rx_mcast_frames   = GET_STAT(RX_VF_MCAST_FRAMES);
	stats->rx_ucast_bytes    = GET_STAT(RX_VF_UCAST_BYTES);
	stats->rx_ucast_frames   = GET_STAT(RX_VF_UCAST_FRAMES);
	stats->rx_err_frames     = GET_STAT(RX_VF_ERR_FRAMES);

#undef GET_STAT
}

static void
t4_clr_vi_stats(struct adapter *sc, u_int vin)
{
	int reg;

	t4_write_reg(sc, A_PL_INDIR_CMD, V_PL_AUTOINC(1) | V_PL_VFID(vin) |
	    V_PL_ADDR(VF_MPS_REG(A_MPS_VF_STAT_TX_VF_BCAST_BYTES_L)));
	for (reg = A_MPS_VF_STAT_TX_VF_BCAST_BYTES_L;
	     reg <= A_MPS_VF_STAT_RX_VF_ERR_FRAMES_H; reg += 4)
		t4_write_reg(sc, A_PL_INDIR_DATA, 0);
}

static void
vi_refresh_stats(struct adapter *sc, struct vi_info *vi)
{
	struct timeval tv;
	const struct timeval interval = {0, 250000};	/* 250ms */

	if (!(vi->flags & VI_INIT_DONE))
		return;

	getmicrotime(&tv);
	timevalsub(&tv, &interval);
	if (timevalcmp(&tv, &vi->last_refreshed, <))
		return;

	mtx_lock(&sc->reg_lock);
	t4_get_vi_stats(sc, vi->vin, &vi->stats);
	getmicrotime(&vi->last_refreshed);
	mtx_unlock(&sc->reg_lock);
}

static void
cxgbe_refresh_stats(struct adapter *sc, struct port_info *pi)
{
	u_int i, v, tnl_cong_drops, bg_map;
	struct timeval tv;
	const struct timeval interval = {0, 250000};	/* 250ms */

	getmicrotime(&tv);
	timevalsub(&tv, &interval);
	if (timevalcmp(&tv, &pi->last_refreshed, <))
		return;

	tnl_cong_drops = 0;
	t4_get_port_stats(sc, pi->tx_chan, &pi->stats);
	bg_map = pi->mps_bg_map;
	while (bg_map) {
		i = ffs(bg_map) - 1;
		mtx_lock(&sc->reg_lock);
		t4_read_indirect(sc, A_TP_MIB_INDEX, A_TP_MIB_DATA, &v, 1,
		    A_TP_MIB_TNL_CNG_DROP_0 + i);
		mtx_unlock(&sc->reg_lock);
		tnl_cong_drops += v;
		bg_map &= ~(1 << i);
	}
	pi->tnl_cong_drops = tnl_cong_drops;
	getmicrotime(&pi->last_refreshed);
}

static void
cxgbe_tick(void *arg)
{
	struct port_info *pi = arg;
	struct adapter *sc = pi->adapter;

	PORT_LOCK_ASSERT_OWNED(pi);
	cxgbe_refresh_stats(sc, pi);

	callout_schedule(&pi->tick, hz);
}

void
vi_tick(void *arg)
{
	struct vi_info *vi = arg;
	struct adapter *sc = vi->pi->adapter;

	vi_refresh_stats(sc, vi);

	callout_schedule(&vi->tick, hz);
}

/*
 * Should match fw_caps_config_<foo> enums in t4fw_interface.h
 */
static char *caps_decoder[] = {
	"\20\001IPMI\002NCSI",				/* 0: NBM */
	"\20\001PPP\002QFC\003DCBX",			/* 1: link */
	"\20\001INGRESS\002EGRESS",			/* 2: switch */
	"\20\001NIC\002VM\003IDS\004UM\005UM_ISGL"	/* 3: NIC */
	    "\006HASHFILTER\007ETHOFLD",
	"\20\001TOE",					/* 4: TOE */
	"\20\001RDDP\002RDMAC",				/* 5: RDMA */
	"\20\001INITIATOR_PDU\002TARGET_PDU"		/* 6: iSCSI */
	    "\003INITIATOR_CNXOFLD\004TARGET_CNXOFLD"
	    "\005INITIATOR_SSNOFLD\006TARGET_SSNOFLD"
	    "\007T10DIF"
	    "\010INITIATOR_CMDOFLD\011TARGET_CMDOFLD",
	"\20\001LOOKASIDE\002TLSKEYS",			/* 7: Crypto */
	"\20\001INITIATOR\002TARGET\003CTRL_OFLD"	/* 8: FCoE */
		    "\004PO_INITIATOR\005PO_TARGET",
};

void
t4_sysctls(struct adapter *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *children, *c0;
	static char *doorbells = {"\20\1UDB\2WCWR\3UDBWC\4KDB"};

	ctx = device_get_sysctl_ctx(sc->dev);

	/*
	 * dev.t4nex.X.
	 */
	oid = device_get_sysctl_tree(sc->dev);
	c0 = children = SYSCTL_CHILDREN(oid);

	sc->sc_do_rxcopy = 1;
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "do_rx_copy", CTLFLAG_RW,
	    &sc->sc_do_rxcopy, 1, "Do RX copy of small frames");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "nports", CTLFLAG_RD, NULL,
	    sc->params.nports, "# of ports");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "doorbells",
	    CTLTYPE_STRING | CTLFLAG_RD, doorbells, (uintptr_t)&sc->doorbells,
	    sysctl_bitfield_8b, "A", "available doorbells");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "core_clock", CTLFLAG_RD, NULL,
	    sc->params.vpd.cclk, "core clock frequency (in KHz)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "holdoff_timers",
	    CTLTYPE_STRING | CTLFLAG_RD, sc->params.sge.timer_val,
	    sizeof(sc->params.sge.timer_val), sysctl_int_array, "A",
	    "interrupt holdoff timer values (us)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "holdoff_pkt_counts",
	    CTLTYPE_STRING | CTLFLAG_RD, sc->params.sge.counter_val,
	    sizeof(sc->params.sge.counter_val), sysctl_int_array, "A",
	    "interrupt holdoff packet counter values");

	t4_sge_sysctls(sc, ctx, children);

	sc->lro_timeout = 100;
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "lro_timeout", CTLFLAG_RW,
	    &sc->lro_timeout, 0, "lro inactive-flush timeout (in us)");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "dflags", CTLFLAG_RW,
	    &sc->debug_flags, 0, "flags to enable runtime debugging");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "tp_version",
	    CTLFLAG_RD, sc->tp_version, 0, "TP microcode version");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "firmware_version",
	    CTLFLAG_RD, sc->fw_version, 0, "firmware version");

	if (sc->flags & IS_VF)
		return;

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "hw_revision", CTLFLAG_RD,
	    NULL, chip_rev(sc), "chip hardware revision");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "sn",
	    CTLFLAG_RD, sc->params.vpd.sn, 0, "serial number");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "pn",
	    CTLFLAG_RD, sc->params.vpd.pn, 0, "part number");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "ec",
	    CTLFLAG_RD, sc->params.vpd.ec, 0, "engineering change");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "md_version",
	    CTLFLAG_RD, sc->params.vpd.md, 0, "manufacturing diags version");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "na",
	    CTLFLAG_RD, sc->params.vpd.na, 0, "network address");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "er_version", CTLFLAG_RD,
	    sc->er_version, 0, "expansion ROM version");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "bs_version", CTLFLAG_RD,
	    sc->bs_version, 0, "bootstrap firmware version");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "scfg_version", CTLFLAG_RD,
	    NULL, sc->params.scfg_vers, "serial config version");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "vpd_version", CTLFLAG_RD,
	    NULL, sc->params.vpd_vers, "VPD version");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "cf",
	    CTLFLAG_RD, sc->cfg_file, 0, "configuration file");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "cfcsum", CTLFLAG_RD, NULL,
	    sc->cfcsum, "config file checksum");

#define SYSCTL_CAP(name, n, text) \
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, #name, \
	    CTLTYPE_STRING | CTLFLAG_RD, caps_decoder[n], (uintptr_t)&sc->name, \
	    sysctl_bitfield_16b, "A", "available " text " capabilities")

	SYSCTL_CAP(nbmcaps, 0, "NBM");
	SYSCTL_CAP(linkcaps, 1, "link");
	SYSCTL_CAP(switchcaps, 2, "switch");
	SYSCTL_CAP(niccaps, 3, "NIC");
	SYSCTL_CAP(toecaps, 4, "TCP offload");
	SYSCTL_CAP(rdmacaps, 5, "RDMA");
	SYSCTL_CAP(iscsicaps, 6, "iSCSI");
	SYSCTL_CAP(cryptocaps, 7, "crypto");
	SYSCTL_CAP(fcoecaps, 8, "FCoE");
#undef SYSCTL_CAP

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "nfilters", CTLFLAG_RD,
	    NULL, sc->tids.nftids, "number of filters");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "temperature", CTLTYPE_INT |
	    CTLFLAG_RD, sc, 0, sysctl_temperature, "I",
	    "chip temperature (in Celsius)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "loadavg", CTLTYPE_STRING |
	    CTLFLAG_RD, sc, 0, sysctl_loadavg, "A",
	    "microprocessor load averages (debug firmwares only)");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "core_vdd", CTLFLAG_RD,
	    &sc->params.core_vdd, 0, "core Vdd (in mV)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "local_cpus",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, LOCAL_CPUS,
	    sysctl_cpus, "A", "local CPUs");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "intr_cpus",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, INTR_CPUS,
	    sysctl_cpus, "A", "preferred CPUs for interrupts");

	/*
	 * dev.t4nex.X.misc.  Marked CTLFLAG_SKIP to avoid information overload.
	 */
	oid = SYSCTL_ADD_NODE(ctx, c0, OID_AUTO, "misc",
	    CTLFLAG_RD | CTLFLAG_SKIP, NULL,
	    "logs and miscellaneous information");
	children = SYSCTL_CHILDREN(oid);

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cctrl",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_cctrl, "A", "congestion control");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_ibq_tp0",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_cim_ibq_obq, "A", "CIM IBQ 0 (TP0)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_ibq_tp1",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 1,
	    sysctl_cim_ibq_obq, "A", "CIM IBQ 1 (TP1)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_ibq_ulp",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 2,
	    sysctl_cim_ibq_obq, "A", "CIM IBQ 2 (ULP)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_ibq_sge0",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 3,
	    sysctl_cim_ibq_obq, "A", "CIM IBQ 3 (SGE0)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_ibq_sge1",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 4,
	    sysctl_cim_ibq_obq, "A", "CIM IBQ 4 (SGE1)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_ibq_ncsi",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 5,
	    sysctl_cim_ibq_obq, "A", "CIM IBQ 5 (NCSI)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_la",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0, sysctl_cim_la,
	    "A", "CIM logic analyzer");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_ma_la",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_cim_ma_la, "A", "CIM MA logic analyzer");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_obq_ulp0",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0 + CIM_NUM_IBQ,
	    sysctl_cim_ibq_obq, "A", "CIM OBQ 0 (ULP0)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_obq_ulp1",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 1 + CIM_NUM_IBQ,
	    sysctl_cim_ibq_obq, "A", "CIM OBQ 1 (ULP1)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_obq_ulp2",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 2 + CIM_NUM_IBQ,
	    sysctl_cim_ibq_obq, "A", "CIM OBQ 2 (ULP2)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_obq_ulp3",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 3 + CIM_NUM_IBQ,
	    sysctl_cim_ibq_obq, "A", "CIM OBQ 3 (ULP3)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_obq_sge",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 4 + CIM_NUM_IBQ,
	    sysctl_cim_ibq_obq, "A", "CIM OBQ 4 (SGE)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_obq_ncsi",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 5 + CIM_NUM_IBQ,
	    sysctl_cim_ibq_obq, "A", "CIM OBQ 5 (NCSI)");

	if (chip_id(sc) > CHELSIO_T4) {
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_obq_sge0_rx",
		    CTLTYPE_STRING | CTLFLAG_RD, sc, 6 + CIM_NUM_IBQ,
		    sysctl_cim_ibq_obq, "A", "CIM OBQ 6 (SGE0-RX)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_obq_sge1_rx",
		    CTLTYPE_STRING | CTLFLAG_RD, sc, 7 + CIM_NUM_IBQ,
		    sysctl_cim_ibq_obq, "A", "CIM OBQ 7 (SGE1-RX)");
	}

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_pif_la",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_cim_pif_la, "A", "CIM PIF logic analyzer");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cim_qcfg",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_cim_qcfg, "A", "CIM queue configuration");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "cpl_stats",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_cpl_stats, "A", "CPL statistics");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "ddp_stats",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_ddp_stats, "A", "non-TCP DDP statistics");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "devlog",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_devlog, "A", "firmware's device log");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "fcoe_stats",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_fcoe_stats, "A", "FCoE statistics");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "hw_sched",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_hw_sched, "A", "hardware scheduler ");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "l2t",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_l2t, "A", "hardware L2 table");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "smt",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_smt, "A", "hardware source MAC table");

#ifdef INET6
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "clip",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_clip, "A", "active CLIP table entries");
#endif

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "lb_stats",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_lb_stats, "A", "loopback statistics");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "meminfo",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_meminfo, "A", "memory regions");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "mps_tcam",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    chip_id(sc) <= CHELSIO_T5 ? sysctl_mps_tcam : sysctl_mps_tcam_t6,
	    "A", "MPS TCAM entries");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "path_mtus",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_path_mtus, "A", "path MTUs");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "pm_stats",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_pm_stats, "A", "PM statistics");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rdma_stats",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_rdma_stats, "A", "RDMA statistics");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tcp_stats",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_tcp_stats, "A", "TCP statistics");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tids",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_tids, "A", "TID information");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tp_err_stats",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_tp_err_stats, "A", "TP error statistics");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tp_la_mask",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, sysctl_tp_la_mask, "I",
	    "TP logic analyzer event capture mask");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tp_la",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_tp_la, "A", "TP logic analyzer");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_rate",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_tx_rate, "A", "Tx rate");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "ulprx_la",
	    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_ulprx_la, "A", "ULPRX logic analyzer");

	if (chip_id(sc) >= CHELSIO_T5) {
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "wcwr_stats",
		    CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
		    sysctl_wcwr_stats, "A", "write combined work requests");
	}

#ifdef TCP_OFFLOAD
	if (is_offload(sc)) {
		int i;
		char s[4];

		/*
		 * dev.t4nex.X.toe.
		 */
		oid = SYSCTL_ADD_NODE(ctx, c0, OID_AUTO, "toe", CTLFLAG_RD,
		    NULL, "TOE parameters");
		children = SYSCTL_CHILDREN(oid);

		sc->tt.cong_algorithm = -1;
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "cong_algorithm",
		    CTLFLAG_RW, &sc->tt.cong_algorithm, 0, "congestion control "
		    "(-1 = default, 0 = reno, 1 = tahoe, 2 = newreno, "
		    "3 = highspeed)");

		sc->tt.sndbuf = 256 * 1024;
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "sndbuf", CTLFLAG_RW,
		    &sc->tt.sndbuf, 0, "max hardware send buffer size");

		sc->tt.ddp = 0;
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "ddp", CTLFLAG_RW,
		    &sc->tt.ddp, 0, "DDP allowed");

		sc->tt.rx_coalesce = 1;
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "rx_coalesce",
		    CTLFLAG_RW, &sc->tt.rx_coalesce, 0, "receive coalescing");

		sc->tt.tls = 0;
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tls", CTLFLAG_RW,
		    &sc->tt.tls, 0, "Inline TLS allowed");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tls_rx_ports",
		    CTLTYPE_INT | CTLFLAG_RW, sc, 0, sysctl_tls_rx_ports,
		    "I", "TCP ports that use inline TLS+TOE RX");

		sc->tt.tx_align = 1;
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_align",
		    CTLFLAG_RW, &sc->tt.tx_align, 0, "chop and align payload");

		sc->tt.tx_zcopy = 0;
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_zcopy",
		    CTLFLAG_RW, &sc->tt.tx_zcopy, 0,
		    "Enable zero-copy aio_write(2)");

		sc->tt.cop_managed_offloading = !!t4_cop_managed_offloading;
		SYSCTL_ADD_INT(ctx, children, OID_AUTO,
		    "cop_managed_offloading", CTLFLAG_RW,
		    &sc->tt.cop_managed_offloading, 0,
		    "COP (Connection Offload Policy) controls all TOE offload");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "timer_tick",
		    CTLTYPE_STRING | CTLFLAG_RD, sc, 0, sysctl_tp_tick, "A",
		    "TP timer tick (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "timestamp_tick",
		    CTLTYPE_STRING | CTLFLAG_RD, sc, 1, sysctl_tp_tick, "A",
		    "TCP timestamp tick (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dack_tick",
		    CTLTYPE_STRING | CTLFLAG_RD, sc, 2, sysctl_tp_tick, "A",
		    "DACK tick (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "dack_timer",
		    CTLTYPE_UINT | CTLFLAG_RD, sc, 0, sysctl_tp_dack_timer,
		    "IU", "DACK timer (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rexmt_min",
		    CTLTYPE_ULONG | CTLFLAG_RD, sc, A_TP_RXT_MIN,
		    sysctl_tp_timer, "LU", "Minimum retransmit interval (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rexmt_max",
		    CTLTYPE_ULONG | CTLFLAG_RD, sc, A_TP_RXT_MAX,
		    sysctl_tp_timer, "LU", "Maximum retransmit interval (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "persist_min",
		    CTLTYPE_ULONG | CTLFLAG_RD, sc, A_TP_PERS_MIN,
		    sysctl_tp_timer, "LU", "Persist timer min (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "persist_max",
		    CTLTYPE_ULONG | CTLFLAG_RD, sc, A_TP_PERS_MAX,
		    sysctl_tp_timer, "LU", "Persist timer max (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "keepalive_idle",
		    CTLTYPE_ULONG | CTLFLAG_RD, sc, A_TP_KEEP_IDLE,
		    sysctl_tp_timer, "LU", "Keepalive idle timer (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "keepalive_interval",
		    CTLTYPE_ULONG | CTLFLAG_RD, sc, A_TP_KEEP_INTVL,
		    sysctl_tp_timer, "LU", "Keepalive interval timer (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "initial_srtt",
		    CTLTYPE_ULONG | CTLFLAG_RD, sc, A_TP_INIT_SRTT,
		    sysctl_tp_timer, "LU", "Initial SRTT (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "finwait2_timer",
		    CTLTYPE_ULONG | CTLFLAG_RD, sc, A_TP_FINWAIT2_TIMER,
		    sysctl_tp_timer, "LU", "FINWAIT2 timer (us)");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "syn_rexmt_count",
		    CTLTYPE_UINT | CTLFLAG_RD, sc, S_SYNSHIFTMAX,
		    sysctl_tp_shift_cnt, "IU",
		    "Number of SYN retransmissions before abort");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rexmt_count",
		    CTLTYPE_UINT | CTLFLAG_RD, sc, S_RXTSHIFTMAXR2,
		    sysctl_tp_shift_cnt, "IU",
		    "Number of retransmissions before abort");

		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "keepalive_count",
		    CTLTYPE_UINT | CTLFLAG_RD, sc, S_KEEPALIVEMAXR2,
		    sysctl_tp_shift_cnt, "IU",
		    "Number of keepalive probes before abort");

		oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "rexmt_backoff",
		    CTLFLAG_RD, NULL, "TOE retransmit backoffs");
		children = SYSCTL_CHILDREN(oid);
		for (i = 0; i < 16; i++) {
			snprintf(s, sizeof(s), "%u", i);
			SYSCTL_ADD_PROC(ctx, children, OID_AUTO, s,
			    CTLTYPE_UINT | CTLFLAG_RD, sc, i, sysctl_tp_backoff,
			    "IU", "TOE retransmit backoff");
		}
	}
#endif
}

void
vi_sysctls(struct vi_info *vi)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *children;

	ctx = device_get_sysctl_ctx(vi->dev);

	/*
	 * dev.v?(cxgbe|cxl).X.
	 */
	oid = device_get_sysctl_tree(vi->dev);
	children = SYSCTL_CHILDREN(oid);

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "viid", CTLFLAG_RD, NULL,
	    vi->viid, "VI identifer");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "nrxq", CTLFLAG_RD,
	    &vi->nrxq, 0, "# of rx queues");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "ntxq", CTLFLAG_RD,
	    &vi->ntxq, 0, "# of tx queues");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "first_rxq", CTLFLAG_RD,
	    &vi->first_rxq, 0, "index of first rx queue");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "first_txq", CTLFLAG_RD,
	    &vi->first_txq, 0, "index of first tx queue");
	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "rss_base", CTLFLAG_RD, NULL,
	    vi->rss_base, "start of RSS indirection table");
	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "rss_size", CTLFLAG_RD, NULL,
	    vi->rss_size, "size of RSS indirection table");

	if (IS_MAIN_VI(vi)) {
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rsrv_noflowq",
		    CTLTYPE_INT | CTLFLAG_RW, vi, 0, sysctl_noflowq, "IU",
		    "Reserve queue 0 for non-flowid packets");
	}

#ifdef TCP_OFFLOAD
	if (vi->nofldrxq != 0) {
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "nofldrxq", CTLFLAG_RD,
		    &vi->nofldrxq, 0,
		    "# of rx queues for offloaded TCP connections");
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "first_ofld_rxq",
		    CTLFLAG_RD, &vi->first_ofld_rxq, 0,
		    "index of first TOE rx queue");
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "holdoff_tmr_idx_ofld",
		    CTLTYPE_INT | CTLFLAG_RW, vi, 0,
		    sysctl_holdoff_tmr_idx_ofld, "I",
		    "holdoff timer index for TOE queues");
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "holdoff_pktc_idx_ofld",
		    CTLTYPE_INT | CTLFLAG_RW, vi, 0,
		    sysctl_holdoff_pktc_idx_ofld, "I",
		    "holdoff packet counter index for TOE queues");
	}
#endif
#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	if (vi->nofldtxq != 0) {
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "nofldtxq", CTLFLAG_RD,
		    &vi->nofldtxq, 0,
		    "# of tx queues for TOE/ETHOFLD");
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "first_ofld_txq",
		    CTLFLAG_RD, &vi->first_ofld_txq, 0,
		    "index of first TOE/ETHOFLD tx queue");
	}
#endif
#ifdef DEV_NETMAP
	if (vi->nnmrxq != 0) {
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "nnmrxq", CTLFLAG_RD,
		    &vi->nnmrxq, 0, "# of netmap rx queues");
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "nnmtxq", CTLFLAG_RD,
		    &vi->nnmtxq, 0, "# of netmap tx queues");
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "first_nm_rxq",
		    CTLFLAG_RD, &vi->first_nm_rxq, 0,
		    "index of first netmap rx queue");
		SYSCTL_ADD_INT(ctx, children, OID_AUTO, "first_nm_txq",
		    CTLFLAG_RD, &vi->first_nm_txq, 0,
		    "index of first netmap tx queue");
	}
#endif

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "holdoff_tmr_idx",
	    CTLTYPE_INT | CTLFLAG_RW, vi, 0, sysctl_holdoff_tmr_idx, "I",
	    "holdoff timer index");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "holdoff_pktc_idx",
	    CTLTYPE_INT | CTLFLAG_RW, vi, 0, sysctl_holdoff_pktc_idx, "I",
	    "holdoff packet counter index");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "qsize_rxq",
	    CTLTYPE_INT | CTLFLAG_RW, vi, 0, sysctl_qsize_rxq, "I",
	    "rx queue size");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "qsize_txq",
	    CTLTYPE_INT | CTLFLAG_RW, vi, 0, sysctl_qsize_txq, "I",
	    "tx queue size");
}

static void
cxgbe_sysctls(struct port_info *pi)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *children, *children2;
	struct adapter *sc = pi->adapter;
	int i;
	char name[16];
	static char *tc_flags = {"\20\1USER\2SYNC\3ASYNC\4ERR"};

	ctx = device_get_sysctl_ctx(pi->dev);

	/*
	 * dev.cxgbe.X.
	 */
	oid = device_get_sysctl_tree(pi->dev);
	children = SYSCTL_CHILDREN(oid);

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "linkdnrc", CTLTYPE_STRING |
	   CTLFLAG_RD, pi, 0, sysctl_linkdnrc, "A", "reason why link is down");
	if (pi->port_type == FW_PORT_TYPE_BT_XAUI) {
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "temperature",
		    CTLTYPE_INT | CTLFLAG_RD, pi, 0, sysctl_btphy, "I",
		    "PHY temperature (in Celsius)");
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "fw_version",
		    CTLTYPE_INT | CTLFLAG_RD, pi, 1, sysctl_btphy, "I",
		    "PHY firmware version");
	}

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "pause_settings",
	    CTLTYPE_STRING | CTLFLAG_RW, pi, 0, sysctl_pause_settings, "A",
    "PAUSE settings (bit 0 = rx_pause, 1 = tx_pause, 2 = pause_autoneg)");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "fec",
	    CTLTYPE_STRING | CTLFLAG_RW, pi, 0, sysctl_fec, "A",
	    "Forward Error Correction (bit 0 = RS, bit 1 = BASER_RS)");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "autoneg",
	    CTLTYPE_INT | CTLFLAG_RW, pi, 0, sysctl_autoneg, "I",
	    "autonegotiation (-1 = not supported)");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "max_speed", CTLFLAG_RD, NULL,
	    port_top_speed(pi), "max speed (in Gbps)");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "mps_bg_map", CTLFLAG_RD, NULL,
	    pi->mps_bg_map, "MPS buffer group map");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "rx_e_chan_map", CTLFLAG_RD,
	    NULL, pi->rx_e_chan_map, "TP rx e-channel map");

	if (sc->flags & IS_VF)
		return;

	/*
	 * dev.(cxgbe|cxl).X.tc.
	 */
	oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "tc", CTLFLAG_RD, NULL,
	    "Tx scheduler traffic classes (cl_rl)");
	children2 = SYSCTL_CHILDREN(oid);
	SYSCTL_ADD_UINT(ctx, children2, OID_AUTO, "pktsize",
	    CTLFLAG_RW, &pi->sched_params->pktsize, 0,
	    "pktsize for per-flow cl-rl (0 means up to the driver )");
	SYSCTL_ADD_UINT(ctx, children2, OID_AUTO, "burstsize",
	    CTLFLAG_RW, &pi->sched_params->burstsize, 0,
	    "burstsize for per-flow cl-rl (0 means up to the driver)");
	for (i = 0; i < sc->chip_params->nsched_cls; i++) {
		struct tx_cl_rl_params *tc = &pi->sched_params->cl_rl[i];

		snprintf(name, sizeof(name), "%d", i);
		children2 = SYSCTL_CHILDREN(SYSCTL_ADD_NODE(ctx,
		    SYSCTL_CHILDREN(oid), OID_AUTO, name, CTLFLAG_RD, NULL,
		    "traffic class"));
		SYSCTL_ADD_PROC(ctx, children2, OID_AUTO, "flags",
		    CTLTYPE_STRING | CTLFLAG_RD, tc_flags, (uintptr_t)&tc->flags,
		    sysctl_bitfield_8b, "A", "flags");
		SYSCTL_ADD_UINT(ctx, children2, OID_AUTO, "refcount",
		    CTLFLAG_RD, &tc->refcount, 0, "references to this class");
		SYSCTL_ADD_PROC(ctx, children2, OID_AUTO, "params",
		    CTLTYPE_STRING | CTLFLAG_RD, sc, (pi->port_id << 16) | i,
		    sysctl_tc_params, "A", "traffic class parameters");
	}

	/*
	 * dev.cxgbe.X.stats.
	 */
	oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "port statistics");
	children = SYSCTL_CHILDREN(oid);
	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "tx_parse_error", CTLFLAG_RD,
	    &pi->tx_parse_error, 0,
	    "# of tx packets with invalid length or # of segments");

#define SYSCTL_ADD_T4_REG64(pi, name, desc, reg) \
	SYSCTL_ADD_OID(ctx, children, OID_AUTO, name, \
	    CTLTYPE_U64 | CTLFLAG_RD, sc, reg, \
	    sysctl_handle_t4_reg64, "QU", desc)

	SYSCTL_ADD_T4_REG64(pi, "tx_octets", "# of octets in good frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_BYTES_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_frames", "total # of good frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_FRAMES_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_bcast_frames", "# of broadcast frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_BCAST_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_mcast_frames", "# of multicast frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_MCAST_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ucast_frames", "# of unicast frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_UCAST_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_error_frames", "# of error frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_ERROR_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_frames_64",
	    "# of tx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_64B_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_frames_65_127",
	    "# of tx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_65B_127B_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_frames_128_255",
	    "# of tx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_128B_255B_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_frames_256_511",
	    "# of tx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_256B_511B_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_frames_512_1023",
	    "# of tx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_512B_1023B_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_frames_1024_1518",
	    "# of tx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_1024B_1518B_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_frames_1519_max",
	    "# of tx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_1519B_MAX_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_drop", "# of dropped tx frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_DROP_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_pause", "# of pause frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PAUSE_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ppp0", "# of PPP prio 0 frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PPP0_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ppp1", "# of PPP prio 1 frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PPP1_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ppp2", "# of PPP prio 2 frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PPP2_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ppp3", "# of PPP prio 3 frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PPP3_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ppp4", "# of PPP prio 4 frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PPP4_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ppp5", "# of PPP prio 5 frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PPP5_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ppp6", "# of PPP prio 6 frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PPP6_L));
	SYSCTL_ADD_T4_REG64(pi, "tx_ppp7", "# of PPP prio 7 frames transmitted",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_TX_PORT_PPP7_L));

	SYSCTL_ADD_T4_REG64(pi, "rx_octets", "# of octets in good frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_BYTES_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_frames", "total # of good frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_FRAMES_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_bcast_frames", "# of broadcast frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_BCAST_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_mcast_frames", "# of multicast frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_MCAST_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ucast_frames", "# of unicast frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_UCAST_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_too_long", "# of frames exceeding MTU",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_MTU_ERROR_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_jabber", "# of jabber frames",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_MTU_CRC_ERROR_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_fcs_err",
	    "# of frames received with bad FCS",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_CRC_ERROR_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_len_err",
	    "# of frames received with length error",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_LEN_ERROR_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_symbol_err", "symbol errors",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_SYM_ERROR_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_runt", "# of short frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_LESS_64B_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_frames_64",
	    "# of rx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_64B_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_frames_65_127",
	    "# of rx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_65B_127B_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_frames_128_255",
	    "# of rx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_128B_255B_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_frames_256_511",
	    "# of rx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_256B_511B_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_frames_512_1023",
	    "# of rx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_512B_1023B_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_frames_1024_1518",
	    "# of rx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_1024B_1518B_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_frames_1519_max",
	    "# of rx frames in this range",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_1519B_MAX_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_pause", "# of pause frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PAUSE_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ppp0", "# of PPP prio 0 frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PPP0_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ppp1", "# of PPP prio 1 frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PPP1_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ppp2", "# of PPP prio 2 frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PPP2_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ppp3", "# of PPP prio 3 frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PPP3_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ppp4", "# of PPP prio 4 frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PPP4_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ppp5", "# of PPP prio 5 frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PPP5_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ppp6", "# of PPP prio 6 frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PPP6_L));
	SYSCTL_ADD_T4_REG64(pi, "rx_ppp7", "# of PPP prio 7 frames received",
	    PORT_REG(pi->tx_chan, A_MPS_PORT_STAT_RX_PORT_PPP7_L));

#undef SYSCTL_ADD_T4_REG64

#define SYSCTL_ADD_T4_PORTSTAT(name, desc) \
	SYSCTL_ADD_UQUAD(ctx, children, OID_AUTO, #name, CTLFLAG_RD, \
	    &pi->stats.name, desc)

	/* We get these from port_stats and they may be stale by up to 1s */
	SYSCTL_ADD_T4_PORTSTAT(rx_ovflow0,
	    "# drops due to buffer-group 0 overflows");
	SYSCTL_ADD_T4_PORTSTAT(rx_ovflow1,
	    "# drops due to buffer-group 1 overflows");
	SYSCTL_ADD_T4_PORTSTAT(rx_ovflow2,
	    "# drops due to buffer-group 2 overflows");
	SYSCTL_ADD_T4_PORTSTAT(rx_ovflow3,
	    "# drops due to buffer-group 3 overflows");
	SYSCTL_ADD_T4_PORTSTAT(rx_trunc0,
	    "# of buffer-group 0 truncated packets");
	SYSCTL_ADD_T4_PORTSTAT(rx_trunc1,
	    "# of buffer-group 1 truncated packets");
	SYSCTL_ADD_T4_PORTSTAT(rx_trunc2,
	    "# of buffer-group 2 truncated packets");
	SYSCTL_ADD_T4_PORTSTAT(rx_trunc3,
	    "# of buffer-group 3 truncated packets");

#undef SYSCTL_ADD_T4_PORTSTAT

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, "tx_tls_records",
	    CTLFLAG_RD, &pi->tx_tls_records,
	    "# of TLS records transmitted");
	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, "tx_tls_octets",
	    CTLFLAG_RD, &pi->tx_tls_octets,
	    "# of payload octets in transmitted TLS records");
	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, "rx_tls_records",
	    CTLFLAG_RD, &pi->rx_tls_records,
	    "# of TLS records received");
	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, "rx_tls_octets",
	    CTLFLAG_RD, &pi->rx_tls_octets,
	    "# of payload octets in received TLS records");
}

static int
sysctl_int_array(SYSCTL_HANDLER_ARGS)
{
	int rc, *i, space = 0;
	struct sbuf sb;

	sbuf_new_for_sysctl(&sb, NULL, 64, req);
	for (i = arg1; arg2; arg2 -= sizeof(int), i++) {
		if (space)
			sbuf_printf(&sb, " ");
		sbuf_printf(&sb, "%d", *i);
		space = 1;
	}
	rc = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (rc);
}

static int
sysctl_bitfield_8b(SYSCTL_HANDLER_ARGS)
{
	int rc;
	struct sbuf *sb;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return(rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (sb == NULL)
		return (ENOMEM);

	sbuf_printf(sb, "%b", *(uint8_t *)(uintptr_t)arg2, (char *)arg1);
	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_bitfield_16b(SYSCTL_HANDLER_ARGS)
{
	int rc;
	struct sbuf *sb;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return(rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (sb == NULL)
		return (ENOMEM);

	sbuf_printf(sb, "%b", *(uint16_t *)(uintptr_t)arg2, (char *)arg1);
	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_btphy(SYSCTL_HANDLER_ARGS)
{
	struct port_info *pi = arg1;
	int op = arg2;
	struct adapter *sc = pi->adapter;
	u_int v;
	int rc;

	rc = begin_synchronized_op(sc, &pi->vi[0], SLEEP_OK | INTR_OK, "t4btt");
	if (rc)
		return (rc);
	/* XXX: magic numbers */
	rc = -t4_mdio_rd(sc, sc->mbox, pi->mdio_addr, 0x1e, op ? 0x20 : 0xc820,
	    &v);
	end_synchronized_op(sc, 0);
	if (rc)
		return (rc);
	if (op == 0)
		v /= 256;

	rc = sysctl_handle_int(oidp, &v, 0, req);
	return (rc);
}

static int
sysctl_noflowq(SYSCTL_HANDLER_ARGS)
{
	struct vi_info *vi = arg1;
	int rc, val;

	val = vi->rsrv_noflowq;
	rc = sysctl_handle_int(oidp, &val, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);

	if ((val >= 1) && (vi->ntxq > 1))
		vi->rsrv_noflowq = 1;
	else
		vi->rsrv_noflowq = 0;

	return (rc);
}

static int
sysctl_holdoff_tmr_idx(SYSCTL_HANDLER_ARGS)
{
	struct vi_info *vi = arg1;
	struct adapter *sc = vi->pi->adapter;
	int idx, rc, i;
	struct sge_rxq *rxq;
	uint8_t v;

	idx = vi->tmr_idx;

	rc = sysctl_handle_int(oidp, &idx, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);

	if (idx < 0 || idx >= SGE_NTIMERS)
		return (EINVAL);

	rc = begin_synchronized_op(sc, vi, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4tmr");
	if (rc)
		return (rc);

	v = V_QINTR_TIMER_IDX(idx) | V_QINTR_CNT_EN(vi->pktc_idx != -1);
	for_each_rxq(vi, i, rxq) {
#ifdef atomic_store_rel_8
		atomic_store_rel_8(&rxq->iq.intr_params, v);
#else
		rxq->iq.intr_params = v;
#endif
	}
	vi->tmr_idx = idx;

	end_synchronized_op(sc, LOCK_HELD);
	return (0);
}

static int
sysctl_holdoff_pktc_idx(SYSCTL_HANDLER_ARGS)
{
	struct vi_info *vi = arg1;
	struct adapter *sc = vi->pi->adapter;
	int idx, rc;

	idx = vi->pktc_idx;

	rc = sysctl_handle_int(oidp, &idx, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);

	if (idx < -1 || idx >= SGE_NCOUNTERS)
		return (EINVAL);

	rc = begin_synchronized_op(sc, vi, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4pktc");
	if (rc)
		return (rc);

	if (vi->flags & VI_INIT_DONE)
		rc = EBUSY; /* cannot be changed once the queues are created */
	else
		vi->pktc_idx = idx;

	end_synchronized_op(sc, LOCK_HELD);
	return (rc);
}

static int
sysctl_qsize_rxq(SYSCTL_HANDLER_ARGS)
{
	struct vi_info *vi = arg1;
	struct adapter *sc = vi->pi->adapter;
	int qsize, rc;

	qsize = vi->qsize_rxq;

	rc = sysctl_handle_int(oidp, &qsize, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);

	if (qsize < 128 || (qsize & 7))
		return (EINVAL);

	rc = begin_synchronized_op(sc, vi, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4rxqs");
	if (rc)
		return (rc);

	if (vi->flags & VI_INIT_DONE)
		rc = EBUSY; /* cannot be changed once the queues are created */
	else
		vi->qsize_rxq = qsize;

	end_synchronized_op(sc, LOCK_HELD);
	return (rc);
}

static int
sysctl_qsize_txq(SYSCTL_HANDLER_ARGS)
{
	struct vi_info *vi = arg1;
	struct adapter *sc = vi->pi->adapter;
	int qsize, rc;

	qsize = vi->qsize_txq;

	rc = sysctl_handle_int(oidp, &qsize, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);

	if (qsize < 128 || qsize > 65536)
		return (EINVAL);

	rc = begin_synchronized_op(sc, vi, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4txqs");
	if (rc)
		return (rc);

	if (vi->flags & VI_INIT_DONE)
		rc = EBUSY; /* cannot be changed once the queues are created */
	else
		vi->qsize_txq = qsize;

	end_synchronized_op(sc, LOCK_HELD);
	return (rc);
}

static int
sysctl_pause_settings(SYSCTL_HANDLER_ARGS)
{
	struct port_info *pi = arg1;
	struct adapter *sc = pi->adapter;
	struct link_config *lc = &pi->link_cfg;
	int rc;

	if (req->newptr == NULL) {
		struct sbuf *sb;
		static char *bits = "\20\1RX\2TX\3AUTO";

		rc = sysctl_wire_old_buffer(req, 0);
		if (rc != 0)
			return(rc);

		sb = sbuf_new_for_sysctl(NULL, NULL, 128, req);
		if (sb == NULL)
			return (ENOMEM);

		if (lc->link_ok) {
			sbuf_printf(sb, "%b", (lc->fc & (PAUSE_TX | PAUSE_RX)) |
			    (lc->requested_fc & PAUSE_AUTONEG), bits);
		} else {
			sbuf_printf(sb, "%b", lc->requested_fc & (PAUSE_TX |
			    PAUSE_RX | PAUSE_AUTONEG), bits);
		}
		rc = sbuf_finish(sb);
		sbuf_delete(sb);
	} else {
		char s[2];
		int n;

		s[0] = '0' + (lc->requested_fc & (PAUSE_TX | PAUSE_RX |
		    PAUSE_AUTONEG));
		s[1] = 0;

		rc = sysctl_handle_string(oidp, s, sizeof(s), req);
		if (rc != 0)
			return(rc);

		if (s[1] != 0)
			return (EINVAL);
		if (s[0] < '0' || s[0] > '9')
			return (EINVAL);	/* not a number */
		n = s[0] - '0';
		if (n & ~(PAUSE_TX | PAUSE_RX | PAUSE_AUTONEG))
			return (EINVAL);	/* some other bit is set too */

		rc = begin_synchronized_op(sc, &pi->vi[0], SLEEP_OK | INTR_OK,
		    "t4PAUSE");
		if (rc)
			return (rc);
		PORT_LOCK(pi);
		lc->requested_fc = n;
		fixup_link_config(pi);
		if (pi->up_vis > 0)
			rc = apply_link_config(pi);
		set_current_media(pi);
		PORT_UNLOCK(pi);
		end_synchronized_op(sc, 0);
	}

	return (rc);
}

static int
sysctl_fec(SYSCTL_HANDLER_ARGS)
{
	struct port_info *pi = arg1;
	struct adapter *sc = pi->adapter;
	struct link_config *lc = &pi->link_cfg;
	int rc;
	int8_t old;

	if (req->newptr == NULL) {
		struct sbuf *sb;
		static char *bits = "\20\1RS\2BASE-R\3RSVD1\4RSVD2\5RSVD3\6AUTO";

		rc = sysctl_wire_old_buffer(req, 0);
		if (rc != 0)
			return(rc);

		sb = sbuf_new_for_sysctl(NULL, NULL, 128, req);
		if (sb == NULL)
			return (ENOMEM);

		/*
		 * Display the requested_fec when the link is down -- the actual
		 * FEC makes sense only when the link is up.
		 */
		if (lc->link_ok) {
			sbuf_printf(sb, "%b", (lc->fec & M_FW_PORT_CAP32_FEC) |
			    (lc->requested_fec & FEC_AUTO), bits);
		} else {
			sbuf_printf(sb, "%b", lc->requested_fec, bits);
		}
		rc = sbuf_finish(sb);
		sbuf_delete(sb);
	} else {
		char s[3];
		int n;

		snprintf(s, sizeof(s), "%d",
		    lc->requested_fec == FEC_AUTO ? -1 :
		    lc->requested_fec & M_FW_PORT_CAP32_FEC);

		rc = sysctl_handle_string(oidp, s, sizeof(s), req);
		if (rc != 0)
			return(rc);

		n = strtol(&s[0], NULL, 0);
		if (n < 0 || n & FEC_AUTO)
			n = FEC_AUTO;
		else {
			if (n & ~M_FW_PORT_CAP32_FEC)
				return (EINVAL);/* some other bit is set too */
			if (!powerof2(n))
				return (EINVAL);/* one bit can be set at most */
		}

		rc = begin_synchronized_op(sc, &pi->vi[0], SLEEP_OK | INTR_OK,
		    "t4fec");
		if (rc)
			return (rc);
		PORT_LOCK(pi);
		old = lc->requested_fec;
		if (n == FEC_AUTO)
			lc->requested_fec = FEC_AUTO;
		else if (n == 0)
			lc->requested_fec = FEC_NONE;
		else {
			if ((lc->supported | V_FW_PORT_CAP32_FEC(n)) !=
			    lc->supported) {
				rc = ENOTSUP;
				goto done;
			}
			lc->requested_fec = n;
		}
		fixup_link_config(pi);
		if (pi->up_vis > 0) {
			rc = apply_link_config(pi);
			if (rc != 0) {
				lc->requested_fec = old;
				if (rc == FW_EPROTO)
					rc = ENOTSUP;
			}
		}
done:
		PORT_UNLOCK(pi);
		end_synchronized_op(sc, 0);
	}

	return (rc);
}

static int
sysctl_autoneg(SYSCTL_HANDLER_ARGS)
{
	struct port_info *pi = arg1;
	struct adapter *sc = pi->adapter;
	struct link_config *lc = &pi->link_cfg;
	int rc, val;

	if (lc->supported & FW_PORT_CAP32_ANEG)
		val = lc->requested_aneg == AUTONEG_DISABLE ? 0 : 1;
	else
		val = -1;
	rc = sysctl_handle_int(oidp, &val, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);
	if (val == 0)
		val = AUTONEG_DISABLE;
	else if (val == 1)
		val = AUTONEG_ENABLE;
	else
		val = AUTONEG_AUTO;

	rc = begin_synchronized_op(sc, &pi->vi[0], SLEEP_OK | INTR_OK,
	    "t4aneg");
	if (rc)
		return (rc);
	PORT_LOCK(pi);
	if (val == AUTONEG_ENABLE && !(lc->supported & FW_PORT_CAP32_ANEG)) {
		rc = ENOTSUP;
		goto done;
	}
	lc->requested_aneg = val;
	fixup_link_config(pi);
	if (pi->up_vis > 0)
		rc = apply_link_config(pi);
	set_current_media(pi);
done:
	PORT_UNLOCK(pi);
	end_synchronized_op(sc, 0);
	return (rc);
}

static int
sysctl_handle_t4_reg64(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	int reg = arg2;
	uint64_t val;

	val = t4_read_reg64(sc, reg);

	return (sysctl_handle_64(oidp, &val, 0, req));
}

static int
sysctl_temperature(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	int rc, t;
	uint32_t param, val;

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4temp");
	if (rc)
		return (rc);
	param = V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) |
	    V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_DIAG) |
	    V_FW_PARAMS_PARAM_Y(FW_PARAM_DEV_DIAG_TMP);
	rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 1, &param, &val);
	end_synchronized_op(sc, 0);
	if (rc)
		return (rc);

	/* unknown is returned as 0 but we display -1 in that case */
	t = val == 0 ? -1 : val;

	rc = sysctl_handle_int(oidp, &t, 0, req);
	return (rc);
}

static int
sysctl_loadavg(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	uint32_t param, val;

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4lavg");
	if (rc)
		return (rc);
	param = V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) |
	    V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_LOAD);
	rc = -t4_query_params(sc, sc->mbox, sc->pf, 0, 1, &param, &val);
	end_synchronized_op(sc, 0);
	if (rc)
		return (rc);

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	if (val == 0xffffffff) {
		/* Only debug and custom firmwares report load averages. */
		sbuf_printf(sb, "not available");
	} else {
		sbuf_printf(sb, "%d %d %d", val & 0xff, (val >> 8) & 0xff,
		    (val >> 16) & 0xff);
	}
	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_cctrl(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i;
	uint16_t incr[NMTUS][NCCTRL_WIN];
	static const char *dec_fac[] = {
		"0.5", "0.5625", "0.625", "0.6875", "0.75", "0.8125", "0.875",
		"0.9375"
	};

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	t4_read_cong_tbl(sc, incr);

	for (i = 0; i < NCCTRL_WIN; ++i) {
		sbuf_printf(sb, "%2d: %4u %4u %4u %4u %4u %4u %4u %4u\n", i,
		    incr[0][i], incr[1][i], incr[2][i], incr[3][i], incr[4][i],
		    incr[5][i], incr[6][i], incr[7][i]);
		sbuf_printf(sb, "%8u %4u %4u %4u %4u %4u %4u %4u %5u %s\n",
		    incr[8][i], incr[9][i], incr[10][i], incr[11][i],
		    incr[12][i], incr[13][i], incr[14][i], incr[15][i],
		    sc->params.a_wnd[i], dec_fac[sc->params.b_wnd[i]]);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static const char *qname[CIM_NUM_IBQ + CIM_NUM_OBQ_T5] = {
	"TP0", "TP1", "ULP", "SGE0", "SGE1", "NC-SI",	/* ibq's */
	"ULP0", "ULP1", "ULP2", "ULP3", "SGE", "NC-SI",	/* obq's */
	"SGE0-RX", "SGE1-RX"	/* additional obq's (T5 onwards) */
};

static int
sysctl_cim_ibq_obq(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i, n, qid = arg2;
	uint32_t *buf, *p;
	char *qtype;
	u_int cim_num_obq = sc->chip_params->cim_num_obq;

	KASSERT(qid >= 0 && qid < CIM_NUM_IBQ + cim_num_obq,
	    ("%s: bad qid %d\n", __func__, qid));

	if (qid < CIM_NUM_IBQ) {
		/* inbound queue */
		qtype = "IBQ";
		n = 4 * CIM_IBQ_SIZE;
		buf = malloc(n * sizeof(uint32_t), M_CXGBE, M_ZERO | M_WAITOK);
		rc = t4_read_cim_ibq(sc, qid, buf, n);
	} else {
		/* outbound queue */
		qtype = "OBQ";
		qid -= CIM_NUM_IBQ;
		n = 4 * cim_num_obq * CIM_OBQ_SIZE;
		buf = malloc(n * sizeof(uint32_t), M_CXGBE, M_ZERO | M_WAITOK);
		rc = t4_read_cim_obq(sc, qid, buf, n);
	}

	if (rc < 0) {
		rc = -rc;
		goto done;
	}
	n = rc * sizeof(uint32_t);	/* rc has # of words actually read */

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		goto done;

	sb = sbuf_new_for_sysctl(NULL, NULL, PAGE_SIZE, req);
	if (sb == NULL) {
		rc = ENOMEM;
		goto done;
	}

	sbuf_printf(sb, "%s%d %s", qtype , qid, qname[arg2]);
	for (i = 0, p = buf; i < n; i += 16, p += 4)
		sbuf_printf(sb, "\n%#06x: %08x %08x %08x %08x", i, p[0], p[1],
		    p[2], p[3]);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);
done:
	free(buf, M_CXGBE);
	return (rc);
}

static void
sbuf_cim_la4(struct adapter *sc, struct sbuf *sb, uint32_t *buf, uint32_t cfg)
{
	uint32_t *p;

	sbuf_printf(sb, "Status   Data      PC%s",
	    cfg & F_UPDBGLACAPTPCONLY ? "" :
	    "     LS0Stat  LS0Addr             LS0Data");

	for (p = buf; p <= &buf[sc->params.cim_la_size - 8]; p += 8) {
		if (cfg & F_UPDBGLACAPTPCONLY) {
			sbuf_printf(sb, "\n  %02x   %08x %08x", p[5] & 0xff,
			    p[6], p[7]);
			sbuf_printf(sb, "\n  %02x   %02x%06x %02x%06x",
			    (p[3] >> 8) & 0xff, p[3] & 0xff, p[4] >> 8,
			    p[4] & 0xff, p[5] >> 8);
			sbuf_printf(sb, "\n  %02x   %x%07x %x%07x",
			    (p[0] >> 4) & 0xff, p[0] & 0xf, p[1] >> 4,
			    p[1] & 0xf, p[2] >> 4);
		} else {
			sbuf_printf(sb,
			    "\n  %02x   %x%07x %x%07x %08x %08x "
			    "%08x%08x%08x%08x",
			    (p[0] >> 4) & 0xff, p[0] & 0xf, p[1] >> 4,
			    p[1] & 0xf, p[2] >> 4, p[2] & 0xf, p[3], p[4], p[5],
			    p[6], p[7]);
		}
	}
}

static void
sbuf_cim_la6(struct adapter *sc, struct sbuf *sb, uint32_t *buf, uint32_t cfg)
{
	uint32_t *p;

	sbuf_printf(sb, "Status   Inst    Data      PC%s",
	    cfg & F_UPDBGLACAPTPCONLY ? "" :
	    "     LS0Stat  LS0Addr  LS0Data  LS1Stat  LS1Addr  LS1Data");

	for (p = buf; p <= &buf[sc->params.cim_la_size - 10]; p += 10) {
		if (cfg & F_UPDBGLACAPTPCONLY) {
			sbuf_printf(sb, "\n  %02x   %08x %08x %08x",
			    p[3] & 0xff, p[2], p[1], p[0]);
			sbuf_printf(sb, "\n  %02x   %02x%06x %02x%06x %02x%06x",
			    (p[6] >> 8) & 0xff, p[6] & 0xff, p[5] >> 8,
			    p[5] & 0xff, p[4] >> 8, p[4] & 0xff, p[3] >> 8);
			sbuf_printf(sb, "\n  %02x   %04x%04x %04x%04x %04x%04x",
			    (p[9] >> 16) & 0xff, p[9] & 0xffff, p[8] >> 16,
			    p[8] & 0xffff, p[7] >> 16, p[7] & 0xffff,
			    p[6] >> 16);
		} else {
			sbuf_printf(sb, "\n  %02x   %04x%04x %04x%04x %04x%04x "
			    "%08x %08x %08x %08x %08x %08x",
			    (p[9] >> 16) & 0xff,
			    p[9] & 0xffff, p[8] >> 16,
			    p[8] & 0xffff, p[7] >> 16,
			    p[7] & 0xffff, p[6] >> 16,
			    p[2], p[1], p[0], p[5], p[4], p[3]);
		}
	}
}

static int
sbuf_cim_la(struct adapter *sc, struct sbuf *sb, int flags)
{
	uint32_t cfg, *buf;
	int rc;

	rc = -t4_cim_read(sc, A_UP_UP_DBG_LA_CFG, 1, &cfg);
	if (rc != 0)
		return (rc);

	MPASS(flags == M_WAITOK || flags == M_NOWAIT);
	buf = malloc(sc->params.cim_la_size * sizeof(uint32_t), M_CXGBE,
	    M_ZERO | flags);
	if (buf == NULL)
		return (ENOMEM);

	rc = -t4_cim_read_la(sc, buf, NULL);
	if (rc != 0)
		goto done;
	if (chip_id(sc) < CHELSIO_T6)
		sbuf_cim_la4(sc, sb, buf, cfg);
	else
		sbuf_cim_la6(sc, sb, buf, cfg);

done:
	free(buf, M_CXGBE);
	return (rc);
}

static int
sysctl_cim_la(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);
	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	rc = sbuf_cim_la(sc, sb, M_WAITOK);
	if (rc == 0)
		rc = sbuf_finish(sb);
	sbuf_delete(sb);
	return (rc);
}

bool
t4_os_dump_cimla(struct adapter *sc, int arg, bool verbose)
{
	struct sbuf sb;
	int rc;

	if (sbuf_new(&sb, NULL, 4096, SBUF_AUTOEXTEND) != &sb)
		return (false);
	rc = sbuf_cim_la(sc, &sb, M_NOWAIT);
	if (rc == 0) {
		rc = sbuf_finish(&sb);
		if (rc == 0) {
			log(LOG_DEBUG, "%s: CIM LA dump follows.\n%s",
		    		device_get_nameunit(sc->dev), sbuf_data(&sb));
		}
	}
	sbuf_delete(&sb);
	return (false);
}

static int
sysctl_cim_ma_la(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	u_int i;
	struct sbuf *sb;
	uint32_t *buf, *p;
	int rc;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	buf = malloc(2 * CIM_MALA_SIZE * 5 * sizeof(uint32_t), M_CXGBE,
	    M_ZERO | M_WAITOK);

	t4_cim_read_ma_la(sc, buf, buf + 5 * CIM_MALA_SIZE);
	p = buf;

	for (i = 0; i < CIM_MALA_SIZE; i++, p += 5) {
		sbuf_printf(sb, "\n%02x%08x%08x%08x%08x", p[4], p[3], p[2],
		    p[1], p[0]);
	}

	sbuf_printf(sb, "\n\nCnt ID Tag UE       Data       RDY VLD");
	for (i = 0; i < CIM_MALA_SIZE; i++, p += 5) {
		sbuf_printf(sb, "\n%3u %2u  %x   %u %08x%08x  %u   %u",
		    (p[2] >> 10) & 0xff, (p[2] >> 7) & 7,
		    (p[2] >> 3) & 0xf, (p[2] >> 2) & 1,
		    (p[1] >> 2) | ((p[2] & 3) << 30),
		    (p[0] >> 2) | ((p[1] & 3) << 30), (p[0] >> 1) & 1,
		    p[0] & 1);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);
	free(buf, M_CXGBE);
	return (rc);
}

static int
sysctl_cim_pif_la(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	u_int i;
	struct sbuf *sb;
	uint32_t *buf, *p;
	int rc;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	buf = malloc(2 * CIM_PIFLA_SIZE * 6 * sizeof(uint32_t), M_CXGBE,
	    M_ZERO | M_WAITOK);

	t4_cim_read_pif_la(sc, buf, buf + 6 * CIM_PIFLA_SIZE, NULL, NULL);
	p = buf;

	sbuf_printf(sb, "Cntl ID DataBE   Addr                 Data");
	for (i = 0; i < CIM_PIFLA_SIZE; i++, p += 6) {
		sbuf_printf(sb, "\n %02x  %02x  %04x  %08x %08x%08x%08x%08x",
		    (p[5] >> 22) & 0xff, (p[5] >> 16) & 0x3f, p[5] & 0xffff,
		    p[4], p[3], p[2], p[1], p[0]);
	}

	sbuf_printf(sb, "\n\nCntl ID               Data");
	for (i = 0; i < CIM_PIFLA_SIZE; i++, p += 6) {
		sbuf_printf(sb, "\n %02x  %02x %08x%08x%08x%08x",
		    (p[4] >> 6) & 0xff, p[4] & 0x3f, p[3], p[2], p[1], p[0]);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);
	free(buf, M_CXGBE);
	return (rc);
}

static int
sysctl_cim_qcfg(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i;
	uint16_t base[CIM_NUM_IBQ + CIM_NUM_OBQ_T5];
	uint16_t size[CIM_NUM_IBQ + CIM_NUM_OBQ_T5];
	uint16_t thres[CIM_NUM_IBQ];
	uint32_t obq_wr[2 * CIM_NUM_OBQ_T5], *wr = obq_wr;
	uint32_t stat[4 * (CIM_NUM_IBQ + CIM_NUM_OBQ_T5)], *p = stat;
	u_int cim_num_obq, ibq_rdaddr, obq_rdaddr, nq;

	cim_num_obq = sc->chip_params->cim_num_obq;
	if (is_t4(sc)) {
		ibq_rdaddr = A_UP_IBQ_0_RDADDR;
		obq_rdaddr = A_UP_OBQ_0_REALADDR;
	} else {
		ibq_rdaddr = A_UP_IBQ_0_SHADOW_RDADDR;
		obq_rdaddr = A_UP_OBQ_0_SHADOW_REALADDR;
	}
	nq = CIM_NUM_IBQ + cim_num_obq;

	rc = -t4_cim_read(sc, ibq_rdaddr, 4 * nq, stat);
	if (rc == 0)
		rc = -t4_cim_read(sc, obq_rdaddr, 2 * cim_num_obq, obq_wr);
	if (rc != 0)
		return (rc);

	t4_read_cimq_cfg(sc, base, size, thres);

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, PAGE_SIZE, req);
	if (sb == NULL)
		return (ENOMEM);

	sbuf_printf(sb,
	    "  Queue  Base  Size Thres  RdPtr WrPtr  SOP  EOP Avail");

	for (i = 0; i < CIM_NUM_IBQ; i++, p += 4)
		sbuf_printf(sb, "\n%7s %5x %5u %5u %6x  %4x %4u %4u %5u",
		    qname[i], base[i], size[i], thres[i], G_IBQRDADDR(p[0]),
		    G_IBQWRADDR(p[1]), G_QUESOPCNT(p[3]), G_QUEEOPCNT(p[3]),
		    G_QUEREMFLITS(p[2]) * 16);
	for ( ; i < nq; i++, p += 4, wr += 2)
		sbuf_printf(sb, "\n%7s %5x %5u %12x  %4x %4u %4u %5u", qname[i],
		    base[i], size[i], G_QUERDADDR(p[0]) & 0x3fff,
		    wr[0] - base[i], G_QUESOPCNT(p[3]), G_QUEEOPCNT(p[3]),
		    G_QUEREMFLITS(p[2]) * 16);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_cpl_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	struct tp_cpl_stats stats;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->reg_lock);
	t4_tp_get_cpl_stats(sc, &stats, 0);
	mtx_unlock(&sc->reg_lock);

	if (sc->chip_params->nchan > 2) {
		sbuf_printf(sb, "                 channel 0  channel 1"
		    "  channel 2  channel 3");
		sbuf_printf(sb, "\nCPL requests:   %10u %10u %10u %10u",
		    stats.req[0], stats.req[1], stats.req[2], stats.req[3]);
		sbuf_printf(sb, "\nCPL responses:   %10u %10u %10u %10u",
		    stats.rsp[0], stats.rsp[1], stats.rsp[2], stats.rsp[3]);
	} else {
		sbuf_printf(sb, "                 channel 0  channel 1");
		sbuf_printf(sb, "\nCPL requests:   %10u %10u",
		    stats.req[0], stats.req[1]);
		sbuf_printf(sb, "\nCPL responses:   %10u %10u",
		    stats.rsp[0], stats.rsp[1]);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_ddp_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	struct tp_usm_stats stats;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return(rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	t4_get_usm_stats(sc, &stats, 1);

	sbuf_printf(sb, "Frames: %u\n", stats.frames);
	sbuf_printf(sb, "Octets: %ju\n", stats.octets);
	sbuf_printf(sb, "Drops:  %u", stats.drops);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static const char * const devlog_level_strings[] = {
	[FW_DEVLOG_LEVEL_EMERG]		= "EMERG",
	[FW_DEVLOG_LEVEL_CRIT]		= "CRIT",
	[FW_DEVLOG_LEVEL_ERR]		= "ERR",
	[FW_DEVLOG_LEVEL_NOTICE]	= "NOTICE",
	[FW_DEVLOG_LEVEL_INFO]		= "INFO",
	[FW_DEVLOG_LEVEL_DEBUG]		= "DEBUG"
};

static const char * const devlog_facility_strings[] = {
	[FW_DEVLOG_FACILITY_CORE]	= "CORE",
	[FW_DEVLOG_FACILITY_CF]		= "CF",
	[FW_DEVLOG_FACILITY_SCHED]	= "SCHED",
	[FW_DEVLOG_FACILITY_TIMER]	= "TIMER",
	[FW_DEVLOG_FACILITY_RES]	= "RES",
	[FW_DEVLOG_FACILITY_HW]		= "HW",
	[FW_DEVLOG_FACILITY_FLR]	= "FLR",
	[FW_DEVLOG_FACILITY_DMAQ]	= "DMAQ",
	[FW_DEVLOG_FACILITY_PHY]	= "PHY",
	[FW_DEVLOG_FACILITY_MAC]	= "MAC",
	[FW_DEVLOG_FACILITY_PORT]	= "PORT",
	[FW_DEVLOG_FACILITY_VI]		= "VI",
	[FW_DEVLOG_FACILITY_FILTER]	= "FILTER",
	[FW_DEVLOG_FACILITY_ACL]	= "ACL",
	[FW_DEVLOG_FACILITY_TM]		= "TM",
	[FW_DEVLOG_FACILITY_QFC]	= "QFC",
	[FW_DEVLOG_FACILITY_DCB]	= "DCB",
	[FW_DEVLOG_FACILITY_ETH]	= "ETH",
	[FW_DEVLOG_FACILITY_OFLD]	= "OFLD",
	[FW_DEVLOG_FACILITY_RI]		= "RI",
	[FW_DEVLOG_FACILITY_ISCSI]	= "ISCSI",
	[FW_DEVLOG_FACILITY_FCOE]	= "FCOE",
	[FW_DEVLOG_FACILITY_FOISCSI]	= "FOISCSI",
	[FW_DEVLOG_FACILITY_FOFCOE]	= "FOFCOE",
	[FW_DEVLOG_FACILITY_CHNET]	= "CHNET",
};

static int
sbuf_devlog(struct adapter *sc, struct sbuf *sb, int flags)
{
	int i, j, rc, nentries, first = 0;
	struct devlog_params *dparams = &sc->params.devlog;
	struct fw_devlog_e *buf, *e;
	uint64_t ftstamp = UINT64_MAX;

	if (dparams->addr == 0)
		return (ENXIO);

	MPASS(flags == M_WAITOK || flags == M_NOWAIT);
	buf = malloc(dparams->size, M_CXGBE, M_ZERO | flags);
	if (buf == NULL)
		return (ENOMEM);

	rc = read_via_memwin(sc, 1, dparams->addr, (void *)buf, dparams->size);
	if (rc != 0)
		goto done;

	nentries = dparams->size / sizeof(struct fw_devlog_e);
	for (i = 0; i < nentries; i++) {
		e = &buf[i];

		if (e->timestamp == 0)
			break;	/* end */

		e->timestamp = be64toh(e->timestamp);
		e->seqno = be32toh(e->seqno);
		for (j = 0; j < 8; j++)
			e->params[j] = be32toh(e->params[j]);

		if (e->timestamp < ftstamp) {
			ftstamp = e->timestamp;
			first = i;
		}
	}

	if (buf[first].timestamp == 0)
		goto done;	/* nothing in the log */

	sbuf_printf(sb, "%10s  %15s  %8s  %8s  %s\n",
	    "Seq#", "Tstamp", "Level", "Facility", "Message");

	i = first;
	do {
		e = &buf[i];
		if (e->timestamp == 0)
			break;	/* end */

		sbuf_printf(sb, "%10d  %15ju  %8s  %8s  ",
		    e->seqno, e->timestamp,
		    (e->level < nitems(devlog_level_strings) ?
			devlog_level_strings[e->level] : "UNKNOWN"),
		    (e->facility < nitems(devlog_facility_strings) ?
			devlog_facility_strings[e->facility] : "UNKNOWN"));
		sbuf_printf(sb, e->fmt, e->params[0], e->params[1],
		    e->params[2], e->params[3], e->params[4],
		    e->params[5], e->params[6], e->params[7]);

		if (++i == nentries)
			i = 0;
	} while (i != first);
done:
	free(buf, M_CXGBE);
	return (rc);
}

static int
sysctl_devlog(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	int rc;
	struct sbuf *sb;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);
	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	rc = sbuf_devlog(sc, sb, M_WAITOK);
	if (rc == 0)
		rc = sbuf_finish(sb);
	sbuf_delete(sb);
	return (rc);
}

void
t4_os_dump_devlog(struct adapter *sc)
{
	int rc;
	struct sbuf sb;

	if (sbuf_new(&sb, NULL, 4096, SBUF_AUTOEXTEND) != &sb)
		return;
	rc = sbuf_devlog(sc, &sb, M_NOWAIT);
	if (rc == 0) {
		rc = sbuf_finish(&sb);
		if (rc == 0) {
			log(LOG_DEBUG, "%s: device log follows.\n%s",
		    		device_get_nameunit(sc->dev), sbuf_data(&sb));
		}
	}
	sbuf_delete(&sb);
}

static int
sysctl_fcoe_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	struct tp_fcoe_stats stats[MAX_NCHAN];
	int i, nchan = sc->chip_params->nchan;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	for (i = 0; i < nchan; i++)
		t4_get_fcoe_stats(sc, i, &stats[i], 1);

	if (nchan > 2) {
		sbuf_printf(sb, "                   channel 0        channel 1"
		    "        channel 2        channel 3");
		sbuf_printf(sb, "\noctetsDDP:  %16ju %16ju %16ju %16ju",
		    stats[0].octets_ddp, stats[1].octets_ddp,
		    stats[2].octets_ddp, stats[3].octets_ddp);
		sbuf_printf(sb, "\nframesDDP:  %16u %16u %16u %16u",
		    stats[0].frames_ddp, stats[1].frames_ddp,
		    stats[2].frames_ddp, stats[3].frames_ddp);
		sbuf_printf(sb, "\nframesDrop: %16u %16u %16u %16u",
		    stats[0].frames_drop, stats[1].frames_drop,
		    stats[2].frames_drop, stats[3].frames_drop);
	} else {
		sbuf_printf(sb, "                   channel 0        channel 1");
		sbuf_printf(sb, "\noctetsDDP:  %16ju %16ju",
		    stats[0].octets_ddp, stats[1].octets_ddp);
		sbuf_printf(sb, "\nframesDDP:  %16u %16u",
		    stats[0].frames_ddp, stats[1].frames_ddp);
		sbuf_printf(sb, "\nframesDrop: %16u %16u",
		    stats[0].frames_drop, stats[1].frames_drop);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_hw_sched(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i;
	unsigned int map, kbps, ipg, mode;
	unsigned int pace_tab[NTX_SCHED];

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	map = t4_read_reg(sc, A_TP_TX_MOD_QUEUE_REQ_MAP);
	mode = G_TIMERMODE(t4_read_reg(sc, A_TP_MOD_CONFIG));
	t4_read_pace_tbl(sc, pace_tab);

	sbuf_printf(sb, "Scheduler  Mode   Channel  Rate (Kbps)   "
	    "Class IPG (0.1 ns)   Flow IPG (us)");

	for (i = 0; i < NTX_SCHED; ++i, map >>= 2) {
		t4_get_tx_sched(sc, i, &kbps, &ipg, 1);
		sbuf_printf(sb, "\n    %u      %-5s     %u     ", i,
		    (mode & (1 << i)) ? "flow" : "class", map & 3);
		if (kbps)
			sbuf_printf(sb, "%9u     ", kbps);
		else
			sbuf_printf(sb, " disabled     ");

		if (ipg)
			sbuf_printf(sb, "%13u        ", ipg);
		else
			sbuf_printf(sb, "     disabled        ");

		if (pace_tab[i])
			sbuf_printf(sb, "%10u", pace_tab[i]);
		else
			sbuf_printf(sb, "  disabled");
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_lb_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i, j;
	uint64_t *p0, *p1;
	struct lb_port_stats s[2];
	static const char *stat_name[] = {
		"OctetsOK:", "FramesOK:", "BcastFrames:", "McastFrames:",
		"UcastFrames:", "ErrorFrames:", "Frames64:", "Frames65To127:",
		"Frames128To255:", "Frames256To511:", "Frames512To1023:",
		"Frames1024To1518:", "Frames1519ToMax:", "FramesDropped:",
		"BG0FramesDropped:", "BG1FramesDropped:", "BG2FramesDropped:",
		"BG3FramesDropped:", "BG0FramesTrunc:", "BG1FramesTrunc:",
		"BG2FramesTrunc:", "BG3FramesTrunc:"
	};

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	memset(s, 0, sizeof(s));

	for (i = 0; i < sc->chip_params->nchan; i += 2) {
		t4_get_lb_stats(sc, i, &s[0]);
		t4_get_lb_stats(sc, i + 1, &s[1]);

		p0 = &s[0].octets;
		p1 = &s[1].octets;
		sbuf_printf(sb, "%s                       Loopback %u"
		    "           Loopback %u", i == 0 ? "" : "\n", i, i + 1);

		for (j = 0; j < nitems(stat_name); j++)
			sbuf_printf(sb, "\n%-17s %20ju %20ju", stat_name[j],
				   *p0++, *p1++);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_linkdnrc(SYSCTL_HANDLER_ARGS)
{
	int rc = 0;
	struct port_info *pi = arg1;
	struct link_config *lc = &pi->link_cfg;
	struct sbuf *sb;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return(rc);
	sb = sbuf_new_for_sysctl(NULL, NULL, 64, req);
	if (sb == NULL)
		return (ENOMEM);

	if (lc->link_ok || lc->link_down_rc == 255)
		sbuf_printf(sb, "n/a");
	else
		sbuf_printf(sb, "%s", t4_link_down_rc_str(lc->link_down_rc));

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

struct mem_desc {
	unsigned int base;
	unsigned int limit;
	unsigned int idx;
};

static int
mem_desc_cmp(const void *a, const void *b)
{
	return ((const struct mem_desc *)a)->base -
	       ((const struct mem_desc *)b)->base;
}

static void
mem_region_show(struct sbuf *sb, const char *name, unsigned int from,
    unsigned int to)
{
	unsigned int size;

	if (from == to)
		return;

	size = to - from + 1;
	if (size == 0)
		return;

	/* XXX: need humanize_number(3) in libkern for a more readable 'size' */
	sbuf_printf(sb, "%-15s %#x-%#x [%u]\n", name, from, to, size);
}

static int
sysctl_meminfo(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i, n;
	uint32_t lo, hi, used, alloc;
	static const char *memory[] = {"EDC0:", "EDC1:", "MC:", "MC0:", "MC1:"};
	static const char *region[] = {
		"DBQ contexts:", "IMSG contexts:", "FLM cache:", "TCBs:",
		"Pstructs:", "Timers:", "Rx FL:", "Tx FL:", "Pstruct FL:",
		"Tx payload:", "Rx payload:", "LE hash:", "iSCSI region:",
		"TDDP region:", "TPT region:", "STAG region:", "RQ region:",
		"RQUDP region:", "PBL region:", "TXPBL region:",
		"DBVFIFO region:", "ULPRX state:", "ULPTX state:",
		"On-chip queues:", "TLS keys:",
	};
	struct mem_desc avail[4];
	struct mem_desc mem[nitems(region) + 3];	/* up to 3 holes */
	struct mem_desc *md = mem;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	for (i = 0; i < nitems(mem); i++) {
		mem[i].limit = 0;
		mem[i].idx = i;
	}

	/* Find and sort the populated memory ranges */
	i = 0;
	lo = t4_read_reg(sc, A_MA_TARGET_MEM_ENABLE);
	if (lo & F_EDRAM0_ENABLE) {
		hi = t4_read_reg(sc, A_MA_EDRAM0_BAR);
		avail[i].base = G_EDRAM0_BASE(hi) << 20;
		avail[i].limit = avail[i].base + (G_EDRAM0_SIZE(hi) << 20);
		avail[i].idx = 0;
		i++;
	}
	if (lo & F_EDRAM1_ENABLE) {
		hi = t4_read_reg(sc, A_MA_EDRAM1_BAR);
		avail[i].base = G_EDRAM1_BASE(hi) << 20;
		avail[i].limit = avail[i].base + (G_EDRAM1_SIZE(hi) << 20);
		avail[i].idx = 1;
		i++;
	}
	if (lo & F_EXT_MEM_ENABLE) {
		hi = t4_read_reg(sc, A_MA_EXT_MEMORY_BAR);
		avail[i].base = G_EXT_MEM_BASE(hi) << 20;
		avail[i].limit = avail[i].base +
		    (G_EXT_MEM_SIZE(hi) << 20);
		avail[i].idx = is_t5(sc) ? 3 : 2;	/* Call it MC0 for T5 */
		i++;
	}
	if (is_t5(sc) && lo & F_EXT_MEM1_ENABLE) {
		hi = t4_read_reg(sc, A_MA_EXT_MEMORY1_BAR);
		avail[i].base = G_EXT_MEM1_BASE(hi) << 20;
		avail[i].limit = avail[i].base +
		    (G_EXT_MEM1_SIZE(hi) << 20);
		avail[i].idx = 4;
		i++;
	}
	if (!i)                                    /* no memory available */
		return 0;
	qsort(avail, i, sizeof(struct mem_desc), mem_desc_cmp);

	(md++)->base = t4_read_reg(sc, A_SGE_DBQ_CTXT_BADDR);
	(md++)->base = t4_read_reg(sc, A_SGE_IMSG_CTXT_BADDR);
	(md++)->base = t4_read_reg(sc, A_SGE_FLM_CACHE_BADDR);
	(md++)->base = t4_read_reg(sc, A_TP_CMM_TCB_BASE);
	(md++)->base = t4_read_reg(sc, A_TP_CMM_MM_BASE);
	(md++)->base = t4_read_reg(sc, A_TP_CMM_TIMER_BASE);
	(md++)->base = t4_read_reg(sc, A_TP_CMM_MM_RX_FLST_BASE);
	(md++)->base = t4_read_reg(sc, A_TP_CMM_MM_TX_FLST_BASE);
	(md++)->base = t4_read_reg(sc, A_TP_CMM_MM_PS_FLST_BASE);

	/* the next few have explicit upper bounds */
	md->base = t4_read_reg(sc, A_TP_PMM_TX_BASE);
	md->limit = md->base - 1 +
		    t4_read_reg(sc, A_TP_PMM_TX_PAGE_SIZE) *
		    G_PMTXMAXPAGE(t4_read_reg(sc, A_TP_PMM_TX_MAX_PAGE));
	md++;

	md->base = t4_read_reg(sc, A_TP_PMM_RX_BASE);
	md->limit = md->base - 1 +
		    t4_read_reg(sc, A_TP_PMM_RX_PAGE_SIZE) *
		    G_PMRXMAXPAGE(t4_read_reg(sc, A_TP_PMM_RX_MAX_PAGE));
	md++;

	if (t4_read_reg(sc, A_LE_DB_CONFIG) & F_HASHEN) {
		if (chip_id(sc) <= CHELSIO_T5)
			md->base = t4_read_reg(sc, A_LE_DB_HASH_TID_BASE);
		else
			md->base = t4_read_reg(sc, A_LE_DB_HASH_TBL_BASE_ADDR);
		md->limit = 0;
	} else {
		md->base = 0;
		md->idx = nitems(region);  /* hide it */
	}
	md++;

#define ulp_region(reg) \
	md->base = t4_read_reg(sc, A_ULP_ ## reg ## _LLIMIT);\
	(md++)->limit = t4_read_reg(sc, A_ULP_ ## reg ## _ULIMIT)

	ulp_region(RX_ISCSI);
	ulp_region(RX_TDDP);
	ulp_region(TX_TPT);
	ulp_region(RX_STAG);
	ulp_region(RX_RQ);
	ulp_region(RX_RQUDP);
	ulp_region(RX_PBL);
	ulp_region(TX_PBL);
#undef ulp_region

	md->base = 0;
	md->idx = nitems(region);
	if (!is_t4(sc)) {
		uint32_t size = 0;
		uint32_t sge_ctrl = t4_read_reg(sc, A_SGE_CONTROL2);
		uint32_t fifo_size = t4_read_reg(sc, A_SGE_DBVFIFO_SIZE);

		if (is_t5(sc)) {
			if (sge_ctrl & F_VFIFO_ENABLE)
				size = G_DBVFIFO_SIZE(fifo_size);
		} else
			size = G_T6_DBVFIFO_SIZE(fifo_size);

		if (size) {
			md->base = G_BASEADDR(t4_read_reg(sc,
			    A_SGE_DBVFIFO_BADDR));
			md->limit = md->base + (size << 2) - 1;
		}
	}
	md++;

	md->base = t4_read_reg(sc, A_ULP_RX_CTX_BASE);
	md->limit = 0;
	md++;
	md->base = t4_read_reg(sc, A_ULP_TX_ERR_TABLE_BASE);
	md->limit = 0;
	md++;

	md->base = sc->vres.ocq.start;
	if (sc->vres.ocq.size)
		md->limit = md->base + sc->vres.ocq.size - 1;
	else
		md->idx = nitems(region);  /* hide it */
	md++;

	md->base = sc->vres.key.start;
	if (sc->vres.key.size)
		md->limit = md->base + sc->vres.key.size - 1;
	else
		md->idx = nitems(region);  /* hide it */
	md++;

	/* add any address-space holes, there can be up to 3 */
	for (n = 0; n < i - 1; n++)
		if (avail[n].limit < avail[n + 1].base)
			(md++)->base = avail[n].limit;
	if (avail[n].limit)
		(md++)->base = avail[n].limit;

	n = md - mem;
	qsort(mem, n, sizeof(struct mem_desc), mem_desc_cmp);

	for (lo = 0; lo < i; lo++)
		mem_region_show(sb, memory[avail[lo].idx], avail[lo].base,
				avail[lo].limit - 1);

	sbuf_printf(sb, "\n");
	for (i = 0; i < n; i++) {
		if (mem[i].idx >= nitems(region))
			continue;                        /* skip holes */
		if (!mem[i].limit)
			mem[i].limit = i < n - 1 ? mem[i + 1].base - 1 : ~0;
		mem_region_show(sb, region[mem[i].idx], mem[i].base,
				mem[i].limit);
	}

	sbuf_printf(sb, "\n");
	lo = t4_read_reg(sc, A_CIM_SDRAM_BASE_ADDR);
	hi = t4_read_reg(sc, A_CIM_SDRAM_ADDR_SIZE) + lo - 1;
	mem_region_show(sb, "uP RAM:", lo, hi);

	lo = t4_read_reg(sc, A_CIM_EXTMEM2_BASE_ADDR);
	hi = t4_read_reg(sc, A_CIM_EXTMEM2_ADDR_SIZE) + lo - 1;
	mem_region_show(sb, "uP Extmem2:", lo, hi);

	lo = t4_read_reg(sc, A_TP_PMM_RX_MAX_PAGE);
	sbuf_printf(sb, "\n%u Rx pages of size %uKiB for %u channels\n",
		   G_PMRXMAXPAGE(lo),
		   t4_read_reg(sc, A_TP_PMM_RX_PAGE_SIZE) >> 10,
		   (lo & F_PMRXNUMCHN) ? 2 : 1);

	lo = t4_read_reg(sc, A_TP_PMM_TX_MAX_PAGE);
	hi = t4_read_reg(sc, A_TP_PMM_TX_PAGE_SIZE);
	sbuf_printf(sb, "%u Tx pages of size %u%ciB for %u channels\n",
		   G_PMTXMAXPAGE(lo),
		   hi >= (1 << 20) ? (hi >> 20) : (hi >> 10),
		   hi >= (1 << 20) ? 'M' : 'K', 1 << G_PMTXNUMCHN(lo));
	sbuf_printf(sb, "%u p-structs\n",
		   t4_read_reg(sc, A_TP_CMM_MM_MAX_PSTRUCT));

	for (i = 0; i < 4; i++) {
		if (chip_id(sc) > CHELSIO_T5)
			lo = t4_read_reg(sc, A_MPS_RX_MAC_BG_PG_CNT0 + i * 4);
		else
			lo = t4_read_reg(sc, A_MPS_RX_PG_RSV0 + i * 4);
		if (is_t5(sc)) {
			used = G_T5_USED(lo);
			alloc = G_T5_ALLOC(lo);
		} else {
			used = G_USED(lo);
			alloc = G_ALLOC(lo);
		}
		/* For T6 these are MAC buffer groups */
		sbuf_printf(sb, "\nPort %d using %u pages out of %u allocated",
		    i, used, alloc);
	}
	for (i = 0; i < sc->chip_params->nchan; i++) {
		if (chip_id(sc) > CHELSIO_T5)
			lo = t4_read_reg(sc, A_MPS_RX_LPBK_BG_PG_CNT0 + i * 4);
		else
			lo = t4_read_reg(sc, A_MPS_RX_PG_RSV4 + i * 4);
		if (is_t5(sc)) {
			used = G_T5_USED(lo);
			alloc = G_T5_ALLOC(lo);
		} else {
			used = G_USED(lo);
			alloc = G_ALLOC(lo);
		}
		/* For T6 these are MAC buffer groups */
		sbuf_printf(sb,
		    "\nLoopback %d using %u pages out of %u allocated",
		    i, used, alloc);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static inline void
tcamxy2valmask(uint64_t x, uint64_t y, uint8_t *addr, uint64_t *mask)
{
	*mask = x | y;
	y = htobe64(y);
	memcpy(addr, (char *)&y + 2, ETHER_ADDR_LEN);
}

static int
sysctl_mps_tcam(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i;

	MPASS(chip_id(sc) <= CHELSIO_T5);

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	sbuf_printf(sb,
	    "Idx  Ethernet address     Mask     Vld Ports PF"
	    "  VF              Replication             P0 P1 P2 P3  ML");
	for (i = 0; i < sc->chip_params->mps_tcam_size; i++) {
		uint64_t tcamx, tcamy, mask;
		uint32_t cls_lo, cls_hi;
		uint8_t addr[ETHER_ADDR_LEN];

		tcamy = t4_read_reg64(sc, MPS_CLS_TCAM_Y_L(i));
		tcamx = t4_read_reg64(sc, MPS_CLS_TCAM_X_L(i));
		if (tcamx & tcamy)
			continue;
		tcamxy2valmask(tcamx, tcamy, addr, &mask);
		cls_lo = t4_read_reg(sc, MPS_CLS_SRAM_L(i));
		cls_hi = t4_read_reg(sc, MPS_CLS_SRAM_H(i));
		sbuf_printf(sb, "\n%3u %02x:%02x:%02x:%02x:%02x:%02x %012jx"
			   "  %c   %#x%4u%4d", i, addr[0], addr[1], addr[2],
			   addr[3], addr[4], addr[5], (uintmax_t)mask,
			   (cls_lo & F_SRAM_VLD) ? 'Y' : 'N',
			   G_PORTMAP(cls_hi), G_PF(cls_lo),
			   (cls_lo & F_VF_VALID) ? G_VF(cls_lo) : -1);

		if (cls_lo & F_REPLICATE) {
			struct fw_ldst_cmd ldst_cmd;

			memset(&ldst_cmd, 0, sizeof(ldst_cmd));
			ldst_cmd.op_to_addrspace =
			    htobe32(V_FW_CMD_OP(FW_LDST_CMD) |
				F_FW_CMD_REQUEST | F_FW_CMD_READ |
				V_FW_LDST_CMD_ADDRSPACE(FW_LDST_ADDRSPC_MPS));
			ldst_cmd.cycles_to_len16 = htobe32(FW_LEN16(ldst_cmd));
			ldst_cmd.u.mps.rplc.fid_idx =
			    htobe16(V_FW_LDST_CMD_FID(FW_LDST_MPS_RPLC) |
				V_FW_LDST_CMD_IDX(i));

			rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK,
			    "t4mps");
			if (rc)
				break;
			rc = -t4_wr_mbox(sc, sc->mbox, &ldst_cmd,
			    sizeof(ldst_cmd), &ldst_cmd);
			end_synchronized_op(sc, 0);

			if (rc != 0) {
				sbuf_printf(sb, "%36d", rc);
				rc = 0;
			} else {
				sbuf_printf(sb, " %08x %08x %08x %08x",
				    be32toh(ldst_cmd.u.mps.rplc.rplc127_96),
				    be32toh(ldst_cmd.u.mps.rplc.rplc95_64),
				    be32toh(ldst_cmd.u.mps.rplc.rplc63_32),
				    be32toh(ldst_cmd.u.mps.rplc.rplc31_0));
			}
		} else
			sbuf_printf(sb, "%36s", "");

		sbuf_printf(sb, "%4u%3u%3u%3u %#3x", G_SRAM_PRIO0(cls_lo),
		    G_SRAM_PRIO1(cls_lo), G_SRAM_PRIO2(cls_lo),
		    G_SRAM_PRIO3(cls_lo), (cls_lo >> S_MULTILISTEN0) & 0xf);
	}

	if (rc)
		(void) sbuf_finish(sb);
	else
		rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_mps_tcam_t6(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i;

	MPASS(chip_id(sc) > CHELSIO_T5);

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	sbuf_printf(sb, "Idx  Ethernet address     Mask       VNI   Mask"
	    "   IVLAN Vld DIP_Hit   Lookup  Port Vld Ports PF  VF"
	    "                           Replication"
	    "                                    P0 P1 P2 P3  ML\n");

	for (i = 0; i < sc->chip_params->mps_tcam_size; i++) {
		uint8_t dip_hit, vlan_vld, lookup_type, port_num;
		uint16_t ivlan;
		uint64_t tcamx, tcamy, val, mask;
		uint32_t cls_lo, cls_hi, ctl, data2, vnix, vniy;
		uint8_t addr[ETHER_ADDR_LEN];

		ctl = V_CTLREQID(1) | V_CTLCMDTYPE(0) | V_CTLXYBITSEL(0);
		if (i < 256)
			ctl |= V_CTLTCAMINDEX(i) | V_CTLTCAMSEL(0);
		else
			ctl |= V_CTLTCAMINDEX(i - 256) | V_CTLTCAMSEL(1);
		t4_write_reg(sc, A_MPS_CLS_TCAM_DATA2_CTL, ctl);
		val = t4_read_reg(sc, A_MPS_CLS_TCAM_RDATA1_REQ_ID1);
		tcamy = G_DMACH(val) << 32;
		tcamy |= t4_read_reg(sc, A_MPS_CLS_TCAM_RDATA0_REQ_ID1);
		data2 = t4_read_reg(sc, A_MPS_CLS_TCAM_RDATA2_REQ_ID1);
		lookup_type = G_DATALKPTYPE(data2);
		port_num = G_DATAPORTNUM(data2);
		if (lookup_type && lookup_type != M_DATALKPTYPE) {
			/* Inner header VNI */
			vniy = ((data2 & F_DATAVIDH2) << 23) |
				       (G_DATAVIDH1(data2) << 16) | G_VIDL(val);
			dip_hit = data2 & F_DATADIPHIT;
			vlan_vld = 0;
		} else {
			vniy = 0;
			dip_hit = 0;
			vlan_vld = data2 & F_DATAVIDH2;
			ivlan = G_VIDL(val);
		}

		ctl |= V_CTLXYBITSEL(1);
		t4_write_reg(sc, A_MPS_CLS_TCAM_DATA2_CTL, ctl);
		val = t4_read_reg(sc, A_MPS_CLS_TCAM_RDATA1_REQ_ID1);
		tcamx = G_DMACH(val) << 32;
		tcamx |= t4_read_reg(sc, A_MPS_CLS_TCAM_RDATA0_REQ_ID1);
		data2 = t4_read_reg(sc, A_MPS_CLS_TCAM_RDATA2_REQ_ID1);
		if (lookup_type && lookup_type != M_DATALKPTYPE) {
			/* Inner header VNI mask */
			vnix = ((data2 & F_DATAVIDH2) << 23) |
			       (G_DATAVIDH1(data2) << 16) | G_VIDL(val);
		} else
			vnix = 0;

		if (tcamx & tcamy)
			continue;
		tcamxy2valmask(tcamx, tcamy, addr, &mask);

		cls_lo = t4_read_reg(sc, MPS_CLS_SRAM_L(i));
		cls_hi = t4_read_reg(sc, MPS_CLS_SRAM_H(i));

		if (lookup_type && lookup_type != M_DATALKPTYPE) {
			sbuf_printf(sb, "\n%3u %02x:%02x:%02x:%02x:%02x:%02x "
			    "%012jx %06x %06x    -    -   %3c"
			    "      'I'  %4x   %3c   %#x%4u%4d", i, addr[0],
			    addr[1], addr[2], addr[3], addr[4], addr[5],
			    (uintmax_t)mask, vniy, vnix, dip_hit ? 'Y' : 'N',
			    port_num, cls_lo & F_T6_SRAM_VLD ? 'Y' : 'N',
			    G_PORTMAP(cls_hi), G_T6_PF(cls_lo),
			    cls_lo & F_T6_VF_VALID ? G_T6_VF(cls_lo) : -1);
		} else {
			sbuf_printf(sb, "\n%3u %02x:%02x:%02x:%02x:%02x:%02x "
			    "%012jx    -       -   ", i, addr[0], addr[1],
			    addr[2], addr[3], addr[4], addr[5],
			    (uintmax_t)mask);

			if (vlan_vld)
				sbuf_printf(sb, "%4u   Y     ", ivlan);
			else
				sbuf_printf(sb, "  -    N     ");

			sbuf_printf(sb, "-      %3c  %4x   %3c   %#x%4u%4d",
			    lookup_type ? 'I' : 'O', port_num,
			    cls_lo & F_T6_SRAM_VLD ? 'Y' : 'N',
			    G_PORTMAP(cls_hi), G_T6_PF(cls_lo),
			    cls_lo & F_T6_VF_VALID ? G_T6_VF(cls_lo) : -1);
		}


		if (cls_lo & F_T6_REPLICATE) {
			struct fw_ldst_cmd ldst_cmd;

			memset(&ldst_cmd, 0, sizeof(ldst_cmd));
			ldst_cmd.op_to_addrspace =
			    htobe32(V_FW_CMD_OP(FW_LDST_CMD) |
				F_FW_CMD_REQUEST | F_FW_CMD_READ |
				V_FW_LDST_CMD_ADDRSPACE(FW_LDST_ADDRSPC_MPS));
			ldst_cmd.cycles_to_len16 = htobe32(FW_LEN16(ldst_cmd));
			ldst_cmd.u.mps.rplc.fid_idx =
			    htobe16(V_FW_LDST_CMD_FID(FW_LDST_MPS_RPLC) |
				V_FW_LDST_CMD_IDX(i));

			rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK,
			    "t6mps");
			if (rc)
				break;
			rc = -t4_wr_mbox(sc, sc->mbox, &ldst_cmd,
			    sizeof(ldst_cmd), &ldst_cmd);
			end_synchronized_op(sc, 0);

			if (rc != 0) {
				sbuf_printf(sb, "%72d", rc);
				rc = 0;
			} else {
				sbuf_printf(sb, " %08x %08x %08x %08x"
				    " %08x %08x %08x %08x",
				    be32toh(ldst_cmd.u.mps.rplc.rplc255_224),
				    be32toh(ldst_cmd.u.mps.rplc.rplc223_192),
				    be32toh(ldst_cmd.u.mps.rplc.rplc191_160),
				    be32toh(ldst_cmd.u.mps.rplc.rplc159_128),
				    be32toh(ldst_cmd.u.mps.rplc.rplc127_96),
				    be32toh(ldst_cmd.u.mps.rplc.rplc95_64),
				    be32toh(ldst_cmd.u.mps.rplc.rplc63_32),
				    be32toh(ldst_cmd.u.mps.rplc.rplc31_0));
			}
		} else
			sbuf_printf(sb, "%72s", "");

		sbuf_printf(sb, "%4u%3u%3u%3u %#x",
		    G_T6_SRAM_PRIO0(cls_lo), G_T6_SRAM_PRIO1(cls_lo),
		    G_T6_SRAM_PRIO2(cls_lo), G_T6_SRAM_PRIO3(cls_lo),
		    (cls_lo >> S_T6_MULTILISTEN0) & 0xf);
	}

	if (rc)
		(void) sbuf_finish(sb);
	else
		rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_path_mtus(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	uint16_t mtus[NMTUS];

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	t4_read_mtu_tbl(sc, mtus, NULL);

	sbuf_printf(sb, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u",
	    mtus[0], mtus[1], mtus[2], mtus[3], mtus[4], mtus[5], mtus[6],
	    mtus[7], mtus[8], mtus[9], mtus[10], mtus[11], mtus[12], mtus[13],
	    mtus[14], mtus[15]);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_pm_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, i;
	uint32_t tx_cnt[MAX_PM_NSTATS], rx_cnt[MAX_PM_NSTATS];
	uint64_t tx_cyc[MAX_PM_NSTATS], rx_cyc[MAX_PM_NSTATS];
	static const char *tx_stats[MAX_PM_NSTATS] = {
		"Read:", "Write bypass:", "Write mem:", "Bypass + mem:",
		"Tx FIFO wait", NULL, "Tx latency"
	};
	static const char *rx_stats[MAX_PM_NSTATS] = {
		"Read:", "Write bypass:", "Write mem:", "Flush:",
		"Rx FIFO wait", NULL, "Rx latency"
	};

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	t4_pmtx_get_stats(sc, tx_cnt, tx_cyc);
	t4_pmrx_get_stats(sc, rx_cnt, rx_cyc);

	sbuf_printf(sb, "                Tx pcmds             Tx bytes");
	for (i = 0; i < 4; i++) {
		sbuf_printf(sb, "\n%-13s %10u %20ju", tx_stats[i], tx_cnt[i],
		    tx_cyc[i]);
	}

	sbuf_printf(sb, "\n                Rx pcmds             Rx bytes");
	for (i = 0; i < 4; i++) {
		sbuf_printf(sb, "\n%-13s %10u %20ju", rx_stats[i], rx_cnt[i],
		    rx_cyc[i]);
	}

	if (chip_id(sc) > CHELSIO_T5) {
		sbuf_printf(sb,
		    "\n              Total wait      Total occupancy");
		sbuf_printf(sb, "\n%-13s %10u %20ju", tx_stats[i], tx_cnt[i],
		    tx_cyc[i]);
		sbuf_printf(sb, "\n%-13s %10u %20ju", rx_stats[i], rx_cnt[i],
		    rx_cyc[i]);

		i += 2;
		MPASS(i < nitems(tx_stats));

		sbuf_printf(sb,
		    "\n                   Reads           Total wait");
		sbuf_printf(sb, "\n%-13s %10u %20ju", tx_stats[i], tx_cnt[i],
		    tx_cyc[i]);
		sbuf_printf(sb, "\n%-13s %10u %20ju", rx_stats[i], rx_cnt[i],
		    rx_cyc[i]);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_rdma_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	struct tp_rdma_stats stats;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->reg_lock);
	t4_tp_get_rdma_stats(sc, &stats, 0);
	mtx_unlock(&sc->reg_lock);

	sbuf_printf(sb, "NoRQEModDefferals: %u\n", stats.rqe_dfr_mod);
	sbuf_printf(sb, "NoRQEPktDefferals: %u", stats.rqe_dfr_pkt);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_tcp_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	struct tp_tcp_stats v4, v6;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->reg_lock);
	t4_tp_get_tcp_stats(sc, &v4, &v6, 0);
	mtx_unlock(&sc->reg_lock);

	sbuf_printf(sb,
	    "                                IP                 IPv6\n");
	sbuf_printf(sb, "OutRsts:      %20u %20u\n",
	    v4.tcp_out_rsts, v6.tcp_out_rsts);
	sbuf_printf(sb, "InSegs:       %20ju %20ju\n",
	    v4.tcp_in_segs, v6.tcp_in_segs);
	sbuf_printf(sb, "OutSegs:      %20ju %20ju\n",
	    v4.tcp_out_segs, v6.tcp_out_segs);
	sbuf_printf(sb, "RetransSegs:  %20ju %20ju",
	    v4.tcp_retrans_segs, v6.tcp_retrans_segs);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_tids(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	struct tid_info *t = &sc->tids;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	if (t->natids) {
		sbuf_printf(sb, "ATID range: 0-%u, in use: %u\n", t->natids - 1,
		    t->atids_in_use);
	}

	if (t->nhpftids) {
		sbuf_printf(sb, "HPFTID range: %u-%u, in use: %u\n",
		    t->hpftid_base, t->hpftid_end, t->hpftids_in_use);
	}

	if (t->ntids) {
		sbuf_printf(sb, "TID range: ");
		if (t4_read_reg(sc, A_LE_DB_CONFIG) & F_HASHEN) {
			uint32_t b, hb;

			if (chip_id(sc) <= CHELSIO_T5) {
				b = t4_read_reg(sc, A_LE_DB_SERVER_INDEX) / 4;
				hb = t4_read_reg(sc, A_LE_DB_TID_HASHBASE) / 4;
			} else {
				b = t4_read_reg(sc, A_LE_DB_SRVR_START_INDEX);
				hb = t4_read_reg(sc, A_T6_LE_DB_HASH_TID_BASE);
			}

			if (b)
				sbuf_printf(sb, "%u-%u, ", t->tid_base, b - 1);
			sbuf_printf(sb, "%u-%u", hb, t->ntids - 1);
		} else
			sbuf_printf(sb, "%u-%u", t->tid_base, t->ntids - 1);
		sbuf_printf(sb, ", in use: %u\n",
		    atomic_load_acq_int(&t->tids_in_use));
	}

	if (t->nstids) {
		sbuf_printf(sb, "STID range: %u-%u, in use: %u\n", t->stid_base,
		    t->stid_base + t->nstids - 1, t->stids_in_use);
	}

	if (t->nftids) {
		sbuf_printf(sb, "FTID range: %u-%u, in use: %u\n", t->ftid_base,
		    t->ftid_end, t->ftids_in_use);
	}

	if (t->netids) {
		sbuf_printf(sb, "ETID range: %u-%u, in use: %u\n", t->etid_base,
		    t->etid_base + t->netids - 1, t->etids_in_use);
	}

	sbuf_printf(sb, "HW TID usage: %u IP users, %u IPv6 users",
	    t4_read_reg(sc, A_LE_DB_ACT_CNT_IPV4),
	    t4_read_reg(sc, A_LE_DB_ACT_CNT_IPV6));

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_tp_err_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	struct tp_err_stats stats;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->reg_lock);
	t4_tp_get_err_stats(sc, &stats, 0);
	mtx_unlock(&sc->reg_lock);

	if (sc->chip_params->nchan > 2) {
		sbuf_printf(sb, "                 channel 0  channel 1"
		    "  channel 2  channel 3\n");
		sbuf_printf(sb, "macInErrs:      %10u %10u %10u %10u\n",
		    stats.mac_in_errs[0], stats.mac_in_errs[1],
		    stats.mac_in_errs[2], stats.mac_in_errs[3]);
		sbuf_printf(sb, "hdrInErrs:      %10u %10u %10u %10u\n",
		    stats.hdr_in_errs[0], stats.hdr_in_errs[1],
		    stats.hdr_in_errs[2], stats.hdr_in_errs[3]);
		sbuf_printf(sb, "tcpInErrs:      %10u %10u %10u %10u\n",
		    stats.tcp_in_errs[0], stats.tcp_in_errs[1],
		    stats.tcp_in_errs[2], stats.tcp_in_errs[3]);
		sbuf_printf(sb, "tcp6InErrs:     %10u %10u %10u %10u\n",
		    stats.tcp6_in_errs[0], stats.tcp6_in_errs[1],
		    stats.tcp6_in_errs[2], stats.tcp6_in_errs[3]);
		sbuf_printf(sb, "tnlCongDrops:   %10u %10u %10u %10u\n",
		    stats.tnl_cong_drops[0], stats.tnl_cong_drops[1],
		    stats.tnl_cong_drops[2], stats.tnl_cong_drops[3]);
		sbuf_printf(sb, "tnlTxDrops:     %10u %10u %10u %10u\n",
		    stats.tnl_tx_drops[0], stats.tnl_tx_drops[1],
		    stats.tnl_tx_drops[2], stats.tnl_tx_drops[3]);
		sbuf_printf(sb, "ofldVlanDrops:  %10u %10u %10u %10u\n",
		    stats.ofld_vlan_drops[0], stats.ofld_vlan_drops[1],
		    stats.ofld_vlan_drops[2], stats.ofld_vlan_drops[3]);
		sbuf_printf(sb, "ofldChanDrops:  %10u %10u %10u %10u\n\n",
		    stats.ofld_chan_drops[0], stats.ofld_chan_drops[1],
		    stats.ofld_chan_drops[2], stats.ofld_chan_drops[3]);
	} else {
		sbuf_printf(sb, "                 channel 0  channel 1\n");
		sbuf_printf(sb, "macInErrs:      %10u %10u\n",
		    stats.mac_in_errs[0], stats.mac_in_errs[1]);
		sbuf_printf(sb, "hdrInErrs:      %10u %10u\n",
		    stats.hdr_in_errs[0], stats.hdr_in_errs[1]);
		sbuf_printf(sb, "tcpInErrs:      %10u %10u\n",
		    stats.tcp_in_errs[0], stats.tcp_in_errs[1]);
		sbuf_printf(sb, "tcp6InErrs:     %10u %10u\n",
		    stats.tcp6_in_errs[0], stats.tcp6_in_errs[1]);
		sbuf_printf(sb, "tnlCongDrops:   %10u %10u\n",
		    stats.tnl_cong_drops[0], stats.tnl_cong_drops[1]);
		sbuf_printf(sb, "tnlTxDrops:     %10u %10u\n",
		    stats.tnl_tx_drops[0], stats.tnl_tx_drops[1]);
		sbuf_printf(sb, "ofldVlanDrops:  %10u %10u\n",
		    stats.ofld_vlan_drops[0], stats.ofld_vlan_drops[1]);
		sbuf_printf(sb, "ofldChanDrops:  %10u %10u\n\n",
		    stats.ofld_chan_drops[0], stats.ofld_chan_drops[1]);
	}

	sbuf_printf(sb, "ofldNoNeigh:    %u\nofldCongDefer:  %u",
	    stats.ofld_no_neigh, stats.ofld_cong_defer);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_tp_la_mask(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct tp_params *tpp = &sc->params.tp;
	u_int mask;
	int rc;

	mask = tpp->la_mask >> 16;
	rc = sysctl_handle_int(oidp, &mask, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);
	if (mask > 0xffff)
		return (EINVAL);
	tpp->la_mask = mask << 16;
	t4_set_reg_field(sc, A_TP_DBG_LA_CONFIG, 0xffff0000U, tpp->la_mask);

	return (0);
}

struct field_desc {
	const char *name;
	u_int start;
	u_int width;
};

static void
field_desc_show(struct sbuf *sb, uint64_t v, const struct field_desc *f)
{
	char buf[32];
	int line_size = 0;

	while (f->name) {
		uint64_t mask = (1ULL << f->width) - 1;
		int len = snprintf(buf, sizeof(buf), "%s: %ju", f->name,
		    ((uintmax_t)v >> f->start) & mask);

		if (line_size + len >= 79) {
			line_size = 8;
			sbuf_printf(sb, "\n        ");
		}
		sbuf_printf(sb, "%s ", buf);
		line_size += len + 1;
		f++;
	}
	sbuf_printf(sb, "\n");
}

static const struct field_desc tp_la0[] = {
	{ "RcfOpCodeOut", 60, 4 },
	{ "State", 56, 4 },
	{ "WcfState", 52, 4 },
	{ "RcfOpcSrcOut", 50, 2 },
	{ "CRxError", 49, 1 },
	{ "ERxError", 48, 1 },
	{ "SanityFailed", 47, 1 },
	{ "SpuriousMsg", 46, 1 },
	{ "FlushInputMsg", 45, 1 },
	{ "FlushInputCpl", 44, 1 },
	{ "RssUpBit", 43, 1 },
	{ "RssFilterHit", 42, 1 },
	{ "Tid", 32, 10 },
	{ "InitTcb", 31, 1 },
	{ "LineNumber", 24, 7 },
	{ "Emsg", 23, 1 },
	{ "EdataOut", 22, 1 },
	{ "Cmsg", 21, 1 },
	{ "CdataOut", 20, 1 },
	{ "EreadPdu", 19, 1 },
	{ "CreadPdu", 18, 1 },
	{ "TunnelPkt", 17, 1 },
	{ "RcfPeerFin", 16, 1 },
	{ "RcfReasonOut", 12, 4 },
	{ "TxCchannel", 10, 2 },
	{ "RcfTxChannel", 8, 2 },
	{ "RxEchannel", 6, 2 },
	{ "RcfRxChannel", 5, 1 },
	{ "RcfDataOutSrdy", 4, 1 },
	{ "RxDvld", 3, 1 },
	{ "RxOoDvld", 2, 1 },
	{ "RxCongestion", 1, 1 },
	{ "TxCongestion", 0, 1 },
	{ NULL }
};

static const struct field_desc tp_la1[] = {
	{ "CplCmdIn", 56, 8 },
	{ "CplCmdOut", 48, 8 },
	{ "ESynOut", 47, 1 },
	{ "EAckOut", 46, 1 },
	{ "EFinOut", 45, 1 },
	{ "ERstOut", 44, 1 },
	{ "SynIn", 43, 1 },
	{ "AckIn", 42, 1 },
	{ "FinIn", 41, 1 },
	{ "RstIn", 40, 1 },
	{ "DataIn", 39, 1 },
	{ "DataInVld", 38, 1 },
	{ "PadIn", 37, 1 },
	{ "RxBufEmpty", 36, 1 },
	{ "RxDdp", 35, 1 },
	{ "RxFbCongestion", 34, 1 },
	{ "TxFbCongestion", 33, 1 },
	{ "TxPktSumSrdy", 32, 1 },
	{ "RcfUlpType", 28, 4 },
	{ "Eread", 27, 1 },
	{ "Ebypass", 26, 1 },
	{ "Esave", 25, 1 },
	{ "Static0", 24, 1 },
	{ "Cread", 23, 1 },
	{ "Cbypass", 22, 1 },
	{ "Csave", 21, 1 },
	{ "CPktOut", 20, 1 },
	{ "RxPagePoolFull", 18, 2 },
	{ "RxLpbkPkt", 17, 1 },
	{ "TxLpbkPkt", 16, 1 },
	{ "RxVfValid", 15, 1 },
	{ "SynLearned", 14, 1 },
	{ "SetDelEntry", 13, 1 },
	{ "SetInvEntry", 12, 1 },
	{ "CpcmdDvld", 11, 1 },
	{ "CpcmdSave", 10, 1 },
	{ "RxPstructsFull", 8, 2 },
	{ "EpcmdDvld", 7, 1 },
	{ "EpcmdFlush", 6, 1 },
	{ "EpcmdTrimPrefix", 5, 1 },
	{ "EpcmdTrimPostfix", 4, 1 },
	{ "ERssIp4Pkt", 3, 1 },
	{ "ERssIp6Pkt", 2, 1 },
	{ "ERssTcpUdpPkt", 1, 1 },
	{ "ERssFceFipPkt", 0, 1 },
	{ NULL }
};

static const struct field_desc tp_la2[] = {
	{ "CplCmdIn", 56, 8 },
	{ "MpsVfVld", 55, 1 },
	{ "MpsPf", 52, 3 },
	{ "MpsVf", 44, 8 },
	{ "SynIn", 43, 1 },
	{ "AckIn", 42, 1 },
	{ "FinIn", 41, 1 },
	{ "RstIn", 40, 1 },
	{ "DataIn", 39, 1 },
	{ "DataInVld", 38, 1 },
	{ "PadIn", 37, 1 },
	{ "RxBufEmpty", 36, 1 },
	{ "RxDdp", 35, 1 },
	{ "RxFbCongestion", 34, 1 },
	{ "TxFbCongestion", 33, 1 },
	{ "TxPktSumSrdy", 32, 1 },
	{ "RcfUlpType", 28, 4 },
	{ "Eread", 27, 1 },
	{ "Ebypass", 26, 1 },
	{ "Esave", 25, 1 },
	{ "Static0", 24, 1 },
	{ "Cread", 23, 1 },
	{ "Cbypass", 22, 1 },
	{ "Csave", 21, 1 },
	{ "CPktOut", 20, 1 },
	{ "RxPagePoolFull", 18, 2 },
	{ "RxLpbkPkt", 17, 1 },
	{ "TxLpbkPkt", 16, 1 },
	{ "RxVfValid", 15, 1 },
	{ "SynLearned", 14, 1 },
	{ "SetDelEntry", 13, 1 },
	{ "SetInvEntry", 12, 1 },
	{ "CpcmdDvld", 11, 1 },
	{ "CpcmdSave", 10, 1 },
	{ "RxPstructsFull", 8, 2 },
	{ "EpcmdDvld", 7, 1 },
	{ "EpcmdFlush", 6, 1 },
	{ "EpcmdTrimPrefix", 5, 1 },
	{ "EpcmdTrimPostfix", 4, 1 },
	{ "ERssIp4Pkt", 3, 1 },
	{ "ERssIp6Pkt", 2, 1 },
	{ "ERssTcpUdpPkt", 1, 1 },
	{ "ERssFceFipPkt", 0, 1 },
	{ NULL }
};

static void
tp_la_show(struct sbuf *sb, uint64_t *p, int idx)
{

	field_desc_show(sb, *p, tp_la0);
}

static void
tp_la_show2(struct sbuf *sb, uint64_t *p, int idx)
{

	if (idx)
		sbuf_printf(sb, "\n");
	field_desc_show(sb, p[0], tp_la0);
	if (idx < (TPLA_SIZE / 2 - 1) || p[1] != ~0ULL)
		field_desc_show(sb, p[1], tp_la0);
}

static void
tp_la_show3(struct sbuf *sb, uint64_t *p, int idx)
{

	if (idx)
		sbuf_printf(sb, "\n");
	field_desc_show(sb, p[0], tp_la0);
	if (idx < (TPLA_SIZE / 2 - 1) || p[1] != ~0ULL)
		field_desc_show(sb, p[1], (p[0] & (1 << 17)) ? tp_la2 : tp_la1);
}

static int
sysctl_tp_la(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	uint64_t *buf, *p;
	int rc;
	u_int i, inc;
	void (*show_func)(struct sbuf *, uint64_t *, int);

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	buf = malloc(TPLA_SIZE * sizeof(uint64_t), M_CXGBE, M_ZERO | M_WAITOK);

	t4_tp_read_la(sc, buf, NULL);
	p = buf;

	switch (G_DBGLAMODE(t4_read_reg(sc, A_TP_DBG_LA_CONFIG))) {
	case 2:
		inc = 2;
		show_func = tp_la_show2;
		break;
	case 3:
		inc = 2;
		show_func = tp_la_show3;
		break;
	default:
		inc = 1;
		show_func = tp_la_show;
	}

	for (i = 0; i < TPLA_SIZE / inc; i++, p += inc)
		(*show_func)(sb, p, i);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);
	free(buf, M_CXGBE);
	return (rc);
}

static int
sysctl_tx_rate(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc;
	u64 nrate[MAX_NCHAN], orate[MAX_NCHAN];

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);

	t4_get_chan_txrate(sc, nrate, orate);

	if (sc->chip_params->nchan > 2) {
		sbuf_printf(sb, "              channel 0   channel 1"
		    "   channel 2   channel 3\n");
		sbuf_printf(sb, "NIC B/s:     %10ju  %10ju  %10ju  %10ju\n",
		    nrate[0], nrate[1], nrate[2], nrate[3]);
		sbuf_printf(sb, "Offload B/s: %10ju  %10ju  %10ju  %10ju",
		    orate[0], orate[1], orate[2], orate[3]);
	} else {
		sbuf_printf(sb, "              channel 0   channel 1\n");
		sbuf_printf(sb, "NIC B/s:     %10ju  %10ju\n",
		    nrate[0], nrate[1]);
		sbuf_printf(sb, "Offload B/s: %10ju  %10ju",
		    orate[0], orate[1]);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_ulprx_la(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	uint32_t *buf, *p;
	int rc, i;

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	buf = malloc(ULPRX_LA_SIZE * 8 * sizeof(uint32_t), M_CXGBE,
	    M_ZERO | M_WAITOK);

	t4_ulprx_read_la(sc, buf);
	p = buf;

	sbuf_printf(sb, "      Pcmd        Type   Message"
	    "                Data");
	for (i = 0; i < ULPRX_LA_SIZE; i++, p += 8) {
		sbuf_printf(sb, "\n%08x%08x  %4x  %08x  %08x%08x%08x%08x",
		    p[1], p[0], p[2], p[3], p[7], p[6], p[5], p[4]);
	}

	rc = sbuf_finish(sb);
	sbuf_delete(sb);
	free(buf, M_CXGBE);
	return (rc);
}

static int
sysctl_wcwr_stats(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct sbuf *sb;
	int rc, v;

	MPASS(chip_id(sc) >= CHELSIO_T5);

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	v = t4_read_reg(sc, A_SGE_STAT_CFG);
	if (G_STATSOURCE_T5(v) == 7) {
		int mode;

		mode = is_t5(sc) ? G_STATMODE(v) : G_T6_STATMODE(v);
		if (mode == 0) {
			sbuf_printf(sb, "total %d, incomplete %d",
			    t4_read_reg(sc, A_SGE_STAT_TOTAL),
			    t4_read_reg(sc, A_SGE_STAT_MATCH));
		} else if (mode == 1) {
			sbuf_printf(sb, "total %d, data overflow %d",
			    t4_read_reg(sc, A_SGE_STAT_TOTAL),
			    t4_read_reg(sc, A_SGE_STAT_MATCH));
		} else {
			sbuf_printf(sb, "unknown mode %d", mode);
		}
	}
	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

static int
sysctl_cpus(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	enum cpu_sets op = arg2;
	cpuset_t cpuset;
	struct sbuf *sb;
	int i, rc;

	MPASS(op == LOCAL_CPUS || op == INTR_CPUS);

	CPU_ZERO(&cpuset);
	rc = bus_get_cpus(sc->dev, op, sizeof(cpuset), &cpuset);
	if (rc != 0)
		return (rc);

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	CPU_FOREACH(i)
		sbuf_printf(sb, "%d ", i);
	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

#ifdef TCP_OFFLOAD
static int
sysctl_tls_rx_ports(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	int *old_ports, *new_ports;
	int i, new_count, rc;

	if (req->newptr == NULL && req->oldptr == NULL)
		return (SYSCTL_OUT(req, NULL, imax(sc->tt.num_tls_rx_ports, 1) *
		    sizeof(sc->tt.tls_rx_ports[0])));

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4tlsrx");
	if (rc)
		return (rc);

	if (sc->tt.num_tls_rx_ports == 0) {
		i = -1;
		rc = SYSCTL_OUT(req, &i, sizeof(i));
	} else
		rc = SYSCTL_OUT(req, sc->tt.tls_rx_ports,
		    sc->tt.num_tls_rx_ports * sizeof(sc->tt.tls_rx_ports[0]));
	if (rc == 0 && req->newptr != NULL) {
		new_count = req->newlen / sizeof(new_ports[0]);
		new_ports = malloc(new_count * sizeof(new_ports[0]), M_CXGBE,
		    M_WAITOK);
		rc = SYSCTL_IN(req, new_ports, new_count *
		    sizeof(new_ports[0]));
		if (rc)
			goto err;

		/* Allow setting to a single '-1' to clear the list. */
		if (new_count == 1 && new_ports[0] == -1) {
			ADAPTER_LOCK(sc);
			old_ports = sc->tt.tls_rx_ports;
			sc->tt.tls_rx_ports = NULL;
			sc->tt.num_tls_rx_ports = 0;
			ADAPTER_UNLOCK(sc);
			free(old_ports, M_CXGBE);
		} else {
			for (i = 0; i < new_count; i++) {
				if (new_ports[i] < 1 ||
				    new_ports[i] > IPPORT_MAX) {
					rc = EINVAL;
					goto err;
				}
			}

			ADAPTER_LOCK(sc);
			old_ports = sc->tt.tls_rx_ports;
			sc->tt.tls_rx_ports = new_ports;
			sc->tt.num_tls_rx_ports = new_count;
			ADAPTER_UNLOCK(sc);
			free(old_ports, M_CXGBE);
			new_ports = NULL;
		}
	err:
		free(new_ports, M_CXGBE);
	}
	end_synchronized_op(sc, 0);
	return (rc);
}

static void
unit_conv(char *buf, size_t len, u_int val, u_int factor)
{
	u_int rem = val % factor;

	if (rem == 0)
		snprintf(buf, len, "%u", val / factor);
	else {
		while (rem % 10 == 0)
			rem /= 10;
		snprintf(buf, len, "%u.%u", val / factor, rem);
	}
}

static int
sysctl_tp_tick(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	char buf[16];
	u_int res, re;
	u_int cclk_ps = 1000000000 / sc->params.vpd.cclk;

	res = t4_read_reg(sc, A_TP_TIMER_RESOLUTION);
	switch (arg2) {
	case 0:
		/* timer_tick */
		re = G_TIMERRESOLUTION(res);
		break;
	case 1:
		/* TCP timestamp tick */
		re = G_TIMESTAMPRESOLUTION(res);
		break;
	case 2:
		/* DACK tick */
		re = G_DELAYEDACKRESOLUTION(res);
		break;
	default:
		return (EDOOFUS);
	}

	unit_conv(buf, sizeof(buf), (cclk_ps << re), 1000000);

	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

static int
sysctl_tp_dack_timer(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	u_int res, dack_re, v;
	u_int cclk_ps = 1000000000 / sc->params.vpd.cclk;

	res = t4_read_reg(sc, A_TP_TIMER_RESOLUTION);
	dack_re = G_DELAYEDACKRESOLUTION(res);
	v = ((cclk_ps << dack_re) / 1000000) * t4_read_reg(sc, A_TP_DACK_TIMER);

	return (sysctl_handle_int(oidp, &v, 0, req));
}

static int
sysctl_tp_timer(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	int reg = arg2;
	u_int tre;
	u_long tp_tick_us, v;
	u_int cclk_ps = 1000000000 / sc->params.vpd.cclk;

	MPASS(reg == A_TP_RXT_MIN || reg == A_TP_RXT_MAX ||
	    reg == A_TP_PERS_MIN  || reg == A_TP_PERS_MAX ||
	    reg == A_TP_KEEP_IDLE || reg == A_TP_KEEP_INTVL ||
	    reg == A_TP_INIT_SRTT || reg == A_TP_FINWAIT2_TIMER);

	tre = G_TIMERRESOLUTION(t4_read_reg(sc, A_TP_TIMER_RESOLUTION));
	tp_tick_us = (cclk_ps << tre) / 1000000;

	if (reg == A_TP_INIT_SRTT)
		v = tp_tick_us * G_INITSRTT(t4_read_reg(sc, reg));
	else
		v = tp_tick_us * t4_read_reg(sc, reg);

	return (sysctl_handle_long(oidp, &v, 0, req));
}

/*
 * All fields in TP_SHIFT_CNT are 4b and the starting location of the field is
 * passed to this function.
 */
static int
sysctl_tp_shift_cnt(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	int idx = arg2;
	u_int v;

	MPASS(idx >= 0 && idx <= 24);

	v = (t4_read_reg(sc, A_TP_SHIFT_CNT) >> idx) & 0xf;

	return (sysctl_handle_int(oidp, &v, 0, req));
}

static int
sysctl_tp_backoff(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	int idx = arg2;
	u_int shift, v, r;

	MPASS(idx >= 0 && idx < 16);

	r = A_TP_TCP_BACKOFF_REG0 + (idx & ~3);
	shift = (idx & 3) << 3;
	v = (t4_read_reg(sc, r) >> shift) & M_TIMERBACKOFFINDEX0;

	return (sysctl_handle_int(oidp, &v, 0, req));
}

static int
sysctl_holdoff_tmr_idx_ofld(SYSCTL_HANDLER_ARGS)
{
	struct vi_info *vi = arg1;
	struct adapter *sc = vi->pi->adapter;
	int idx, rc, i;
	struct sge_ofld_rxq *ofld_rxq;
	uint8_t v;

	idx = vi->ofld_tmr_idx;

	rc = sysctl_handle_int(oidp, &idx, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);

	if (idx < 0 || idx >= SGE_NTIMERS)
		return (EINVAL);

	rc = begin_synchronized_op(sc, vi, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4otmr");
	if (rc)
		return (rc);

	v = V_QINTR_TIMER_IDX(idx) | V_QINTR_CNT_EN(vi->ofld_pktc_idx != -1);
	for_each_ofld_rxq(vi, i, ofld_rxq) {
#ifdef atomic_store_rel_8
		atomic_store_rel_8(&ofld_rxq->iq.intr_params, v);
#else
		ofld_rxq->iq.intr_params = v;
#endif
	}
	vi->ofld_tmr_idx = idx;

	end_synchronized_op(sc, LOCK_HELD);
	return (0);
}

static int
sysctl_holdoff_pktc_idx_ofld(SYSCTL_HANDLER_ARGS)
{
	struct vi_info *vi = arg1;
	struct adapter *sc = vi->pi->adapter;
	int idx, rc;

	idx = vi->ofld_pktc_idx;

	rc = sysctl_handle_int(oidp, &idx, 0, req);
	if (rc != 0 || req->newptr == NULL)
		return (rc);

	if (idx < -1 || idx >= SGE_NCOUNTERS)
		return (EINVAL);

	rc = begin_synchronized_op(sc, vi, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4opktc");
	if (rc)
		return (rc);

	if (vi->flags & VI_INIT_DONE)
		rc = EBUSY; /* cannot be changed once the queues are created */
	else
		vi->ofld_pktc_idx = idx;

	end_synchronized_op(sc, LOCK_HELD);
	return (rc);
}
#endif

static int
get_sge_context(struct adapter *sc, struct t4_sge_context *cntxt)
{
	int rc;

	if (cntxt->cid > M_CTXTQID)
		return (EINVAL);

	if (cntxt->mem_id != CTXT_EGRESS && cntxt->mem_id != CTXT_INGRESS &&
	    cntxt->mem_id != CTXT_FLM && cntxt->mem_id != CTXT_CNM)
		return (EINVAL);

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4ctxt");
	if (rc)
		return (rc);

	if (sc->flags & FW_OK) {
		rc = -t4_sge_ctxt_rd(sc, sc->mbox, cntxt->cid, cntxt->mem_id,
		    &cntxt->data[0]);
		if (rc == 0)
			goto done;
	}

	/*
	 * Read via firmware failed or wasn't even attempted.  Read directly via
	 * the backdoor.
	 */
	rc = -t4_sge_ctxt_rd_bd(sc, cntxt->cid, cntxt->mem_id, &cntxt->data[0]);
done:
	end_synchronized_op(sc, 0);
	return (rc);
}

static int
load_fw(struct adapter *sc, struct t4_data *fw)
{
	int rc;
	uint8_t *fw_data;

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4ldfw");
	if (rc)
		return (rc);

	/*
	 * The firmware, with the sole exception of the memory parity error
	 * handler, runs from memory and not flash.  It is almost always safe to
	 * install a new firmware on a running system.  Just set bit 1 in
	 * hw.cxgbe.dflags or dev.<nexus>.<n>.dflags first.
	 */
	if (sc->flags & FULL_INIT_DONE &&
	    (sc->debug_flags & DF_LOAD_FW_ANYTIME) == 0) {
		rc = EBUSY;
		goto done;
	}

	fw_data = malloc(fw->len, M_CXGBE, M_WAITOK);
	if (fw_data == NULL) {
		rc = ENOMEM;
		goto done;
	}

	rc = copyin(fw->data, fw_data, fw->len);
	if (rc == 0)
		rc = -t4_load_fw(sc, fw_data, fw->len);

	free(fw_data, M_CXGBE);
done:
	end_synchronized_op(sc, 0);
	return (rc);
}

static int
load_cfg(struct adapter *sc, struct t4_data *cfg)
{
	int rc;
	uint8_t *cfg_data = NULL;

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4ldcf");
	if (rc)
		return (rc);

	if (cfg->len == 0) {
		/* clear */
		rc = -t4_load_cfg(sc, NULL, 0);
		goto done;
	}

	cfg_data = malloc(cfg->len, M_CXGBE, M_WAITOK);
	if (cfg_data == NULL) {
		rc = ENOMEM;
		goto done;
	}

	rc = copyin(cfg->data, cfg_data, cfg->len);
	if (rc == 0)
		rc = -t4_load_cfg(sc, cfg_data, cfg->len);

	free(cfg_data, M_CXGBE);
done:
	end_synchronized_op(sc, 0);
	return (rc);
}

static int
load_boot(struct adapter *sc, struct t4_bootrom *br)
{
	int rc;
	uint8_t *br_data = NULL;
	u_int offset;

	if (br->len > 1024 * 1024)
		return (EFBIG);

	if (br->pf_offset == 0) {
		/* pfidx */
		if (br->pfidx_addr > 7)
			return (EINVAL);
		offset = G_OFFSET(t4_read_reg(sc, PF_REG(br->pfidx_addr,
		    A_PCIE_PF_EXPROM_OFST)));
	} else if (br->pf_offset == 1) {
		/* offset */
		offset = G_OFFSET(br->pfidx_addr);
	} else {
		return (EINVAL);
	}

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4ldbr");
	if (rc)
		return (rc);

	if (br->len == 0) {
		/* clear */
		rc = -t4_load_boot(sc, NULL, offset, 0);
		goto done;
	}

	br_data = malloc(br->len, M_CXGBE, M_WAITOK);
	if (br_data == NULL) {
		rc = ENOMEM;
		goto done;
	}

	rc = copyin(br->data, br_data, br->len);
	if (rc == 0)
		rc = -t4_load_boot(sc, br_data, offset, br->len);

	free(br_data, M_CXGBE);
done:
	end_synchronized_op(sc, 0);
	return (rc);
}

static int
load_bootcfg(struct adapter *sc, struct t4_data *bc)
{
	int rc;
	uint8_t *bc_data = NULL;

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4ldcf");
	if (rc)
		return (rc);

	if (bc->len == 0) {
		/* clear */
		rc = -t4_load_bootcfg(sc, NULL, 0);
		goto done;
	}

	bc_data = malloc(bc->len, M_CXGBE, M_WAITOK);
	if (bc_data == NULL) {
		rc = ENOMEM;
		goto done;
	}

	rc = copyin(bc->data, bc_data, bc->len);
	if (rc == 0)
		rc = -t4_load_bootcfg(sc, bc_data, bc->len);

	free(bc_data, M_CXGBE);
done:
	end_synchronized_op(sc, 0);
	return (rc);
}

static int
cudbg_dump(struct adapter *sc, struct t4_cudbg_dump *dump)
{
	int rc;
	struct cudbg_init *cudbg;
	void *handle, *buf;

	/* buf is large, don't block if no memory is available */
	buf = malloc(dump->len, M_CXGBE, M_NOWAIT | M_ZERO);
	if (buf == NULL)
		return (ENOMEM);

	handle = cudbg_alloc_handle();
	if (handle == NULL) {
		rc = ENOMEM;
		goto done;
	}

	cudbg = cudbg_get_init(handle);
	cudbg->adap = sc;
	cudbg->print = (cudbg_print_cb)printf;

#ifndef notyet
	device_printf(sc->dev, "%s: wr_flash %u, len %u, data %p.\n",
	    __func__, dump->wr_flash, dump->len, dump->data);
#endif

	if (dump->wr_flash)
		cudbg->use_flash = 1;
	MPASS(sizeof(cudbg->dbg_bitmap) == sizeof(dump->bitmap));
	memcpy(cudbg->dbg_bitmap, dump->bitmap, sizeof(cudbg->dbg_bitmap));

	rc = cudbg_collect(handle, buf, &dump->len);
	if (rc != 0)
		goto done;

	rc = copyout(buf, dump->data, dump->len);
done:
	cudbg_free_handle(handle);
	free(buf, M_CXGBE);
	return (rc);
}

static void
free_offload_policy(struct t4_offload_policy *op)
{
	struct offload_rule *r;
	int i;

	if (op == NULL)
		return;

	r = &op->rule[0];
	for (i = 0; i < op->nrules; i++, r++) {
		free(r->bpf_prog.bf_insns, M_CXGBE);
	}
	free(op->rule, M_CXGBE);
	free(op, M_CXGBE);
}

static int
set_offload_policy(struct adapter *sc, struct t4_offload_policy *uop)
{
	int i, rc, len;
	struct t4_offload_policy *op, *old;
	struct bpf_program *bf;
	const struct offload_settings *s;
	struct offload_rule *r;
	void *u;

	if (!is_offload(sc))
		return (ENODEV);

	if (uop->nrules == 0) {
		/* Delete installed policies. */
		op = NULL;
		goto set_policy;
	} if (uop->nrules > 256) { /* arbitrary */
		return (E2BIG);
	}

	/* Copy userspace offload policy to kernel */
	op = malloc(sizeof(*op), M_CXGBE, M_ZERO | M_WAITOK);
	op->nrules = uop->nrules;
	len = op->nrules * sizeof(struct offload_rule);
	op->rule = malloc(len, M_CXGBE, M_ZERO | M_WAITOK);
	rc = copyin(uop->rule, op->rule, len);
	if (rc) {
		free(op->rule, M_CXGBE);
		free(op, M_CXGBE);
		return (rc);
	}

	r = &op->rule[0];
	for (i = 0; i < op->nrules; i++, r++) {

		/* Validate open_type */
		if (r->open_type != OPEN_TYPE_LISTEN &&
		    r->open_type != OPEN_TYPE_ACTIVE &&
		    r->open_type != OPEN_TYPE_PASSIVE &&
		    r->open_type != OPEN_TYPE_DONTCARE) {
error:
			/*
			 * Rules 0 to i have malloc'd filters that need to be
			 * freed.  Rules i+1 to nrules have userspace pointers
			 * and should be left alone.
			 */
			op->nrules = i;
			free_offload_policy(op);
			return (rc);
		}

		/* Validate settings */
		s = &r->settings;
		if ((s->offload != 0 && s->offload != 1) ||
		    s->cong_algo < -1 || s->cong_algo > CONG_ALG_HIGHSPEED ||
		    s->sched_class < -1 ||
		    s->sched_class >= sc->chip_params->nsched_cls) {
			rc = EINVAL;
			goto error;
		}

		bf = &r->bpf_prog;
		u = bf->bf_insns;	/* userspace ptr */
		bf->bf_insns = NULL;
		if (bf->bf_len == 0) {
			/* legal, matches everything */
			continue;
		}
		len = bf->bf_len * sizeof(*bf->bf_insns);
		bf->bf_insns = malloc(len, M_CXGBE, M_ZERO | M_WAITOK);
		rc = copyin(u, bf->bf_insns, len);
		if (rc != 0)
			goto error;

		if (!bpf_validate(bf->bf_insns, bf->bf_len)) {
			rc = EINVAL;
			goto error;
		}
	}
set_policy:
	rw_wlock(&sc->policy_lock);
	old = sc->policy;
	sc->policy = op;
	rw_wunlock(&sc->policy_lock);
	free_offload_policy(old);

	return (0);
}

#define MAX_READ_BUF_SIZE (128 * 1024)
static int
read_card_mem(struct adapter *sc, int win, struct t4_mem_range *mr)
{
	uint32_t addr, remaining, n;
	uint32_t *buf;
	int rc;
	uint8_t *dst;

	rc = validate_mem_range(sc, mr->addr, mr->len);
	if (rc != 0)
		return (rc);

	buf = malloc(min(mr->len, MAX_READ_BUF_SIZE), M_CXGBE, M_WAITOK);
	addr = mr->addr;
	remaining = mr->len;
	dst = (void *)mr->data;

	while (remaining) {
		n = min(remaining, MAX_READ_BUF_SIZE);
		read_via_memwin(sc, 2, addr, buf, n);

		rc = copyout(buf, dst, n);
		if (rc != 0)
			break;

		dst += n;
		remaining -= n;
		addr += n;
	}

	free(buf, M_CXGBE);
	return (rc);
}
#undef MAX_READ_BUF_SIZE

static int
read_i2c(struct adapter *sc, struct t4_i2c_data *i2cd)
{
	int rc;

	if (i2cd->len == 0 || i2cd->port_id >= sc->params.nports)
		return (EINVAL);

	if (i2cd->len > sizeof(i2cd->data))
		return (EFBIG);

	rc = begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4i2crd");
	if (rc)
		return (rc);
	rc = -t4_i2c_rd(sc, sc->mbox, i2cd->port_id, i2cd->dev_addr,
	    i2cd->offset, i2cd->len, &i2cd->data[0]);
	end_synchronized_op(sc, 0);

	return (rc);
}

int
t4_os_find_pci_capability(struct adapter *sc, int cap)
{
	int i;

	return (pci_find_cap(sc->dev, cap, &i) == 0 ? i : 0);
}

int
t4_os_pci_save_state(struct adapter *sc)
{
	device_t dev;
	struct pci_devinfo *dinfo;

	dev = sc->dev;
	dinfo = device_get_ivars(dev);

	pci_cfg_save(dev, dinfo, 0);
	return (0);
}

int
t4_os_pci_restore_state(struct adapter *sc)
{
	device_t dev;
	struct pci_devinfo *dinfo;

	dev = sc->dev;
	dinfo = device_get_ivars(dev);

	pci_cfg_restore(dev, dinfo);
	return (0);
}

void
t4_os_portmod_changed(struct port_info *pi)
{
	struct adapter *sc = pi->adapter;
	struct vi_info *vi;
	struct ifnet *ifp;
	static const char *mod_str[] = {
		NULL, "LR", "SR", "ER", "TWINAX", "active TWINAX", "LRM"
	};

	KASSERT((pi->flags & FIXED_IFMEDIA) == 0,
	    ("%s: port_type %u", __func__, pi->port_type));

	vi = &pi->vi[0];
	if (begin_synchronized_op(sc, vi, HOLD_LOCK, "t4mod") == 0) {
		PORT_LOCK(pi);
		build_medialist(pi);
		if (pi->mod_type != FW_PORT_MOD_TYPE_NONE) {
			fixup_link_config(pi);
			apply_link_config(pi);
		}
		PORT_UNLOCK(pi);
		end_synchronized_op(sc, LOCK_HELD);
	}

	ifp = vi->ifp;
	if (pi->mod_type == FW_PORT_MOD_TYPE_NONE)
		if_printf(ifp, "transceiver unplugged.\n");
	else if (pi->mod_type == FW_PORT_MOD_TYPE_UNKNOWN)
		if_printf(ifp, "unknown transceiver inserted.\n");
	else if (pi->mod_type == FW_PORT_MOD_TYPE_NOTSUPPORTED)
		if_printf(ifp, "unsupported transceiver inserted.\n");
	else if (pi->mod_type > 0 && pi->mod_type < nitems(mod_str)) {
		if_printf(ifp, "%dGbps %s transceiver inserted.\n",
		    port_top_speed(pi), mod_str[pi->mod_type]);
	} else {
		if_printf(ifp, "transceiver (type %d) inserted.\n",
		    pi->mod_type);
	}
}

void
t4_os_link_changed(struct port_info *pi)
{
	struct vi_info *vi;
	struct ifnet *ifp;
	struct link_config *lc;
	int v;

	PORT_LOCK_ASSERT_OWNED(pi);

	for_each_vi(pi, v, vi) {
		ifp = vi->ifp;
		if (ifp == NULL)
			continue;

		lc = &pi->link_cfg;
		if (lc->link_ok) {
			ifp->if_baudrate = IF_Mbps(lc->speed);
			if_link_state_change(ifp, LINK_STATE_UP);
		} else {
			if_link_state_change(ifp, LINK_STATE_DOWN);
		}
	}
}

void
t4_iterate(void (*func)(struct adapter *, void *), void *arg)
{
	struct adapter *sc;

	sx_slock(&t4_list_lock);
	SLIST_FOREACH(sc, &t4_list, link) {
		/*
		 * func should not make any assumptions about what state sc is
		 * in - the only guarantee is that sc->sc_lock is a valid lock.
		 */
		func(sc, arg);
	}
	sx_sunlock(&t4_list_lock);
}

static int
t4_ioctl(struct cdev *dev, unsigned long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	int rc;
	struct adapter *sc = dev->si_drv1;

	rc = priv_check(td, PRIV_DRIVER);
	if (rc != 0)
		return (rc);

	switch (cmd) {
	case CHELSIO_T4_GETREG: {
		struct t4_reg *edata = (struct t4_reg *)data;

		if ((edata->addr & 0x3) != 0 || edata->addr >= sc->mmio_len)
			return (EFAULT);

		if (edata->size == 4)
			edata->val = t4_read_reg(sc, edata->addr);
		else if (edata->size == 8)
			edata->val = t4_read_reg64(sc, edata->addr);
		else
			return (EINVAL);

		break;
	}
	case CHELSIO_T4_SETREG: {
		struct t4_reg *edata = (struct t4_reg *)data;

		if ((edata->addr & 0x3) != 0 || edata->addr >= sc->mmio_len)
			return (EFAULT);

		if (edata->size == 4) {
			if (edata->val & 0xffffffff00000000)
				return (EINVAL);
			t4_write_reg(sc, edata->addr, (uint32_t) edata->val);
		} else if (edata->size == 8)
			t4_write_reg64(sc, edata->addr, edata->val);
		else
			return (EINVAL);
		break;
	}
	case CHELSIO_T4_REGDUMP: {
		struct t4_regdump *regs = (struct t4_regdump *)data;
		int reglen = t4_get_regs_len(sc);
		uint8_t *buf;

		if (regs->len < reglen) {
			regs->len = reglen; /* hint to the caller */
			return (ENOBUFS);
		}

		regs->len = reglen;
		buf = malloc(reglen, M_CXGBE, M_WAITOK | M_ZERO);
		get_regs(sc, regs, buf);
		rc = copyout(buf, regs->data, reglen);
		free(buf, M_CXGBE);
		break;
	}
	case CHELSIO_T4_GET_FILTER_MODE:
		rc = get_filter_mode(sc, (uint32_t *)data);
		break;
	case CHELSIO_T4_SET_FILTER_MODE:
		rc = set_filter_mode(sc, *(uint32_t *)data);
		break;
	case CHELSIO_T4_GET_FILTER:
		rc = get_filter(sc, (struct t4_filter *)data);
		break;
	case CHELSIO_T4_SET_FILTER:
		rc = set_filter(sc, (struct t4_filter *)data);
		break;
	case CHELSIO_T4_DEL_FILTER:
		rc = del_filter(sc, (struct t4_filter *)data);
		break;
	case CHELSIO_T4_GET_SGE_CONTEXT:
		rc = get_sge_context(sc, (struct t4_sge_context *)data);
		break;
	case CHELSIO_T4_LOAD_FW:
		rc = load_fw(sc, (struct t4_data *)data);
		break;
	case CHELSIO_T4_GET_MEM:
		rc = read_card_mem(sc, 2, (struct t4_mem_range *)data);
		break;
	case CHELSIO_T4_GET_I2C:
		rc = read_i2c(sc, (struct t4_i2c_data *)data);
		break;
	case CHELSIO_T4_CLEAR_STATS: {
		int i, v, bg_map;
		u_int port_id = *(uint32_t *)data;
		struct port_info *pi;
		struct vi_info *vi;

		if (port_id >= sc->params.nports)
			return (EINVAL);
		pi = sc->port[port_id];
		if (pi == NULL)
			return (EIO);

		/* MAC stats */
		t4_clr_port_stats(sc, pi->tx_chan);
		pi->tx_parse_error = 0;
		pi->tnl_cong_drops = 0;
		mtx_lock(&sc->reg_lock);
		for_each_vi(pi, v, vi) {
			if (vi->flags & VI_INIT_DONE)
				t4_clr_vi_stats(sc, vi->vin);
		}
		bg_map = pi->mps_bg_map;
		v = 0;	/* reuse */
		while (bg_map) {
			i = ffs(bg_map) - 1;
			t4_write_indirect(sc, A_TP_MIB_INDEX, A_TP_MIB_DATA, &v,
			    1, A_TP_MIB_TNL_CNG_DROP_0 + i);
			bg_map &= ~(1 << i);
		}
		mtx_unlock(&sc->reg_lock);

		/*
		 * Since this command accepts a port, clear stats for
		 * all VIs on this port.
		 */
		for_each_vi(pi, v, vi) {
			if (vi->flags & VI_INIT_DONE) {
				struct sge_rxq *rxq;
				struct sge_txq *txq;
				struct sge_wrq *wrq;

				for_each_rxq(vi, i, rxq) {
#if defined(INET) || defined(INET6)
					rxq->lro.lro_queued = 0;
					rxq->lro.lro_flushed = 0;
#endif
					rxq->rxcsum = 0;
					rxq->vlan_extraction = 0;
				}

				for_each_txq(vi, i, txq) {
					txq->txcsum = 0;
					txq->tso_wrs = 0;
					txq->vlan_insertion = 0;
					txq->imm_wrs = 0;
					txq->sgl_wrs = 0;
					txq->txpkt_wrs = 0;
					txq->txpkts0_wrs = 0;
					txq->txpkts1_wrs = 0;
					txq->txpkts0_pkts = 0;
					txq->txpkts1_pkts = 0;
					txq->raw_wrs = 0;
					mp_ring_reset_stats(txq->r);
				}

#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
				/* nothing to clear for each ofld_rxq */

				for_each_ofld_txq(vi, i, wrq) {
					wrq->tx_wrs_direct = 0;
					wrq->tx_wrs_copied = 0;
				}
#endif

				if (IS_MAIN_VI(vi)) {
					wrq = &sc->sge.ctrlq[pi->port_id];
					wrq->tx_wrs_direct = 0;
					wrq->tx_wrs_copied = 0;
				}
			}
		}
		break;
	}
	case CHELSIO_T4_SCHED_CLASS:
		rc = t4_set_sched_class(sc, (struct t4_sched_params *)data);
		break;
	case CHELSIO_T4_SCHED_QUEUE:
		rc = t4_set_sched_queue(sc, (struct t4_sched_queue *)data);
		break;
	case CHELSIO_T4_GET_TRACER:
		rc = t4_get_tracer(sc, (struct t4_tracer *)data);
		break;
	case CHELSIO_T4_SET_TRACER:
		rc = t4_set_tracer(sc, (struct t4_tracer *)data);
		break;
	case CHELSIO_T4_LOAD_CFG:
		rc = load_cfg(sc, (struct t4_data *)data);
		break;
	case CHELSIO_T4_LOAD_BOOT:
		rc = load_boot(sc, (struct t4_bootrom *)data);
		break;
	case CHELSIO_T4_LOAD_BOOTCFG:
		rc = load_bootcfg(sc, (struct t4_data *)data);
		break;
	case CHELSIO_T4_CUDBG_DUMP:
		rc = cudbg_dump(sc, (struct t4_cudbg_dump *)data);
		break;
	case CHELSIO_T4_SET_OFLD_POLICY:
		rc = set_offload_policy(sc, (struct t4_offload_policy *)data);
		break;
	default:
		rc = ENOTTY;
	}

	return (rc);
}

#ifdef TCP_OFFLOAD
static int
toe_capability(struct vi_info *vi, int enable)
{
	int rc;
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (!is_offload(sc))
		return (ENODEV);

	if (enable) {
		if ((vi->ifp->if_capenable & IFCAP_TOE) != 0) {
			/* TOE is already enabled. */
			return (0);
		}

		/*
		 * We need the port's queues around so that we're able to send
		 * and receive CPLs to/from the TOE even if the ifnet for this
		 * port has never been UP'd administratively.
		 */
		if (!(vi->flags & VI_INIT_DONE)) {
			rc = vi_full_init(vi);
			if (rc)
				return (rc);
		}
		if (!(pi->vi[0].flags & VI_INIT_DONE)) {
			rc = vi_full_init(&pi->vi[0]);
			if (rc)
				return (rc);
		}

		if (isset(&sc->offload_map, pi->port_id)) {
			/* TOE is enabled on another VI of this port. */
			pi->uld_vis++;
			return (0);
		}

		if (!uld_active(sc, ULD_TOM)) {
			rc = t4_activate_uld(sc, ULD_TOM);
			if (rc == EAGAIN) {
				log(LOG_WARNING,
				    "You must kldload t4_tom.ko before trying "
				    "to enable TOE on a cxgbe interface.\n");
			}
			if (rc != 0)
				return (rc);
			KASSERT(sc->tom_softc != NULL,
			    ("%s: TOM activated but softc NULL", __func__));
			KASSERT(uld_active(sc, ULD_TOM),
			    ("%s: TOM activated but flag not set", __func__));
		}

		/* Activate iWARP and iSCSI too, if the modules are loaded. */
		if (!uld_active(sc, ULD_IWARP))
			(void) t4_activate_uld(sc, ULD_IWARP);
		if (!uld_active(sc, ULD_ISCSI))
			(void) t4_activate_uld(sc, ULD_ISCSI);

		pi->uld_vis++;
		setbit(&sc->offload_map, pi->port_id);
	} else {
		pi->uld_vis--;

		if (!isset(&sc->offload_map, pi->port_id) || pi->uld_vis > 0)
			return (0);

		KASSERT(uld_active(sc, ULD_TOM),
		    ("%s: TOM never initialized?", __func__));
		clrbit(&sc->offload_map, pi->port_id);
	}

	return (0);
}

/*
 * Add an upper layer driver to the global list.
 */
int
t4_register_uld(struct uld_info *ui)
{
	int rc = 0;
	struct uld_info *u;

	sx_xlock(&t4_uld_list_lock);
	SLIST_FOREACH(u, &t4_uld_list, link) {
	    if (u->uld_id == ui->uld_id) {
		    rc = EEXIST;
		    goto done;
	    }
	}

	SLIST_INSERT_HEAD(&t4_uld_list, ui, link);
	ui->refcount = 0;
done:
	sx_xunlock(&t4_uld_list_lock);
	return (rc);
}

int
t4_unregister_uld(struct uld_info *ui)
{
	int rc = EINVAL;
	struct uld_info *u;

	sx_xlock(&t4_uld_list_lock);

	SLIST_FOREACH(u, &t4_uld_list, link) {
	    if (u == ui) {
		    if (ui->refcount > 0) {
			    rc = EBUSY;
			    goto done;
		    }

		    SLIST_REMOVE(&t4_uld_list, ui, uld_info, link);
		    rc = 0;
		    goto done;
	    }
	}
done:
	sx_xunlock(&t4_uld_list_lock);
	return (rc);
}

int
t4_activate_uld(struct adapter *sc, int id)
{
	int rc;
	struct uld_info *ui;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (id < 0 || id > ULD_MAX)
		return (EINVAL);
	rc = EAGAIN;	/* kldoad the module with this ULD and try again. */

	sx_slock(&t4_uld_list_lock);

	SLIST_FOREACH(ui, &t4_uld_list, link) {
		if (ui->uld_id == id) {
			if (!(sc->flags & FULL_INIT_DONE)) {
				rc = adapter_full_init(sc);
				if (rc != 0)
					break;
			}

			rc = ui->activate(sc);
			if (rc == 0) {
				setbit(&sc->active_ulds, id);
				ui->refcount++;
			}
			break;
		}
	}

	sx_sunlock(&t4_uld_list_lock);

	return (rc);
}

int
t4_deactivate_uld(struct adapter *sc, int id)
{
	int rc;
	struct uld_info *ui;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (id < 0 || id > ULD_MAX)
		return (EINVAL);
	rc = ENXIO;

	sx_slock(&t4_uld_list_lock);

	SLIST_FOREACH(ui, &t4_uld_list, link) {
		if (ui->uld_id == id) {
			rc = ui->deactivate(sc);
			if (rc == 0) {
				clrbit(&sc->active_ulds, id);
				ui->refcount--;
			}
			break;
		}
	}

	sx_sunlock(&t4_uld_list_lock);

	return (rc);
}

int
uld_active(struct adapter *sc, int uld_id)
{

	MPASS(uld_id >= 0 && uld_id <= ULD_MAX);

	return (isset(&sc->active_ulds, uld_id));
}
#endif

/*
 * t  = ptr to tunable.
 * nc = number of CPUs.
 * c  = compiled in default for that tunable.
 */
static void
calculate_nqueues(int *t, int nc, const int c)
{
	int nq;

	if (*t > 0)
		return;
	nq = *t < 0 ? -*t : c;
	*t = min(nc, nq);
}

/*
 * Come up with reasonable defaults for some of the tunables, provided they're
 * not set by the user (in which case we'll use the values as is).
 */
static void
tweak_tunables(void)
{
	int nc = mp_ncpus;	/* our snapshot of the number of CPUs */

	if (t4_ntxq < 1) {
#ifdef RSS
		t4_ntxq = rss_getnumbuckets();
#else
		calculate_nqueues(&t4_ntxq, nc, NTXQ);
#endif
	}

	calculate_nqueues(&t4_ntxq_vi, nc, NTXQ_VI);

	if (t4_nrxq < 1) {
#ifdef RSS
		t4_nrxq = rss_getnumbuckets();
#else
		calculate_nqueues(&t4_nrxq, nc, NRXQ);
#endif
	}

	calculate_nqueues(&t4_nrxq_vi, nc, NRXQ_VI);

#if defined(TCP_OFFLOAD) || defined(RATELIMIT)
	calculate_nqueues(&t4_nofldtxq, nc, NOFLDTXQ);
	calculate_nqueues(&t4_nofldtxq_vi, nc, NOFLDTXQ_VI);
#endif
#ifdef TCP_OFFLOAD
	calculate_nqueues(&t4_nofldrxq, nc, NOFLDRXQ);
	calculate_nqueues(&t4_nofldrxq_vi, nc, NOFLDRXQ_VI);

	if (t4_toecaps_allowed == -1)
		t4_toecaps_allowed = FW_CAPS_CONFIG_TOE;

	if (t4_rdmacaps_allowed == -1) {
		t4_rdmacaps_allowed = FW_CAPS_CONFIG_RDMA_RDDP |
		    FW_CAPS_CONFIG_RDMA_RDMAC;
	}

	if (t4_iscsicaps_allowed == -1) {
		t4_iscsicaps_allowed = FW_CAPS_CONFIG_ISCSI_INITIATOR_PDU |
		    FW_CAPS_CONFIG_ISCSI_TARGET_PDU |
		    FW_CAPS_CONFIG_ISCSI_T10DIF;
	}

	if (t4_tmr_idx_ofld < 0 || t4_tmr_idx_ofld >= SGE_NTIMERS)
		t4_tmr_idx_ofld = TMR_IDX_OFLD;

	if (t4_pktc_idx_ofld < -1 || t4_pktc_idx_ofld >= SGE_NCOUNTERS)
		t4_pktc_idx_ofld = PKTC_IDX_OFLD;
#else
	if (t4_toecaps_allowed == -1)
		t4_toecaps_allowed = 0;

	if (t4_rdmacaps_allowed == -1)
		t4_rdmacaps_allowed = 0;

	if (t4_iscsicaps_allowed == -1)
		t4_iscsicaps_allowed = 0;
#endif

#ifdef DEV_NETMAP
	calculate_nqueues(&t4_nnmtxq_vi, nc, NNMTXQ_VI);
	calculate_nqueues(&t4_nnmrxq_vi, nc, NNMRXQ_VI);
#endif

	if (t4_tmr_idx < 0 || t4_tmr_idx >= SGE_NTIMERS)
		t4_tmr_idx = TMR_IDX;

	if (t4_pktc_idx < -1 || t4_pktc_idx >= SGE_NCOUNTERS)
		t4_pktc_idx = PKTC_IDX;

	if (t4_qsize_txq < 128)
		t4_qsize_txq = 128;

	if (t4_qsize_rxq < 128)
		t4_qsize_rxq = 128;
	while (t4_qsize_rxq & 7)
		t4_qsize_rxq++;

	t4_intr_types &= INTR_MSIX | INTR_MSI | INTR_INTX;

	/*
	 * Number of VIs to create per-port.  The first VI is the "main" regular
	 * VI for the port.  The rest are additional virtual interfaces on the
	 * same physical port.  Note that the main VI does not have native
	 * netmap support but the extra VIs do.
	 *
	 * Limit the number of VIs per port to the number of available
	 * MAC addresses per port.
	 */
	if (t4_num_vis < 1)
		t4_num_vis = 1;
	if (t4_num_vis > nitems(vi_mac_funcs)) {
		t4_num_vis = nitems(vi_mac_funcs);
		printf("cxgbe: number of VIs limited to %d\n", t4_num_vis);
	}

	if (pcie_relaxed_ordering < 0 || pcie_relaxed_ordering > 2) {
		pcie_relaxed_ordering = 1;
#if defined(__i386__) || defined(__amd64__)
		if (cpu_vendor_id == CPU_VENDOR_INTEL)
			pcie_relaxed_ordering = 0;
#endif
	}
}

#ifdef DDB
static void
t4_dump_tcb(struct adapter *sc, int tid)
{
	uint32_t base, i, j, off, pf, reg, save, tcb_addr, win_pos;

	reg = PCIE_MEM_ACCESS_REG(A_PCIE_MEM_ACCESS_OFFSET, 2);
	save = t4_read_reg(sc, reg);
	base = sc->memwin[2].mw_base;

	/* Dump TCB for the tid */
	tcb_addr = t4_read_reg(sc, A_TP_CMM_TCB_BASE);
	tcb_addr += tid * TCB_SIZE;

	if (is_t4(sc)) {
		pf = 0;
		win_pos = tcb_addr & ~0xf;	/* start must be 16B aligned */
	} else {
		pf = V_PFNUM(sc->pf);
		win_pos = tcb_addr & ~0x7f;	/* start must be 128B aligned */
	}
	t4_write_reg(sc, reg, win_pos | pf);
	t4_read_reg(sc, reg);

	off = tcb_addr - win_pos;
	for (i = 0; i < 4; i++) {
		uint32_t buf[8];
		for (j = 0; j < 8; j++, off += 4)
			buf[j] = htonl(t4_read_reg(sc, base + off));

		db_printf("%08x %08x %08x %08x %08x %08x %08x %08x\n",
		    buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
		    buf[7]);
	}

	t4_write_reg(sc, reg, save);
	t4_read_reg(sc, reg);
}

static void
t4_dump_devlog(struct adapter *sc)
{
	struct devlog_params *dparams = &sc->params.devlog;
	struct fw_devlog_e e;
	int i, first, j, m, nentries, rc;
	uint64_t ftstamp = UINT64_MAX;

	if (dparams->start == 0) {
		db_printf("devlog params not valid\n");
		return;
	}

	nentries = dparams->size / sizeof(struct fw_devlog_e);
	m = fwmtype_to_hwmtype(dparams->memtype);

	/* Find the first entry. */
	first = -1;
	for (i = 0; i < nentries && !db_pager_quit; i++) {
		rc = -t4_mem_read(sc, m, dparams->start + i * sizeof(e),
		    sizeof(e), (void *)&e);
		if (rc != 0)
			break;

		if (e.timestamp == 0)
			break;

		e.timestamp = be64toh(e.timestamp);
		if (e.timestamp < ftstamp) {
			ftstamp = e.timestamp;
			first = i;
		}
	}

	if (first == -1)
		return;

	i = first;
	do {
		rc = -t4_mem_read(sc, m, dparams->start + i * sizeof(e),
		    sizeof(e), (void *)&e);
		if (rc != 0)
			return;

		if (e.timestamp == 0)
			return;

		e.timestamp = be64toh(e.timestamp);
		e.seqno = be32toh(e.seqno);
		for (j = 0; j < 8; j++)
			e.params[j] = be32toh(e.params[j]);

		db_printf("%10d  %15ju  %8s  %8s  ",
		    e.seqno, e.timestamp,
		    (e.level < nitems(devlog_level_strings) ?
			devlog_level_strings[e.level] : "UNKNOWN"),
		    (e.facility < nitems(devlog_facility_strings) ?
			devlog_facility_strings[e.facility] : "UNKNOWN"));
		db_printf(e.fmt, e.params[0], e.params[1], e.params[2],
		    e.params[3], e.params[4], e.params[5], e.params[6],
		    e.params[7]);

		if (++i == nentries)
			i = 0;
	} while (i != first && !db_pager_quit);
}

static struct command_table db_t4_table = LIST_HEAD_INITIALIZER(db_t4_table);
_DB_SET(_show, t4, NULL, db_show_table, 0, &db_t4_table);

DB_FUNC(devlog, db_show_devlog, db_t4_table, CS_OWN, NULL)
{
	device_t dev;
	int t;
	bool valid;

	valid = false;
	t = db_read_token();
	if (t == tIDENT) {
		dev = device_lookup_by_name(db_tok_string);
		valid = true;
	}
	db_skip_to_eol();
	if (!valid) {
		db_printf("usage: show t4 devlog <nexus>\n");
		return;
	}

	if (dev == NULL) {
		db_printf("device not found\n");
		return;
	}

	t4_dump_devlog(device_get_softc(dev));
}

DB_FUNC(tcb, db_show_t4tcb, db_t4_table, CS_OWN, NULL)
{
	device_t dev;
	int radix, tid, t;
	bool valid;

	valid = false;
	radix = db_radix;
	db_radix = 10;
	t = db_read_token();
	if (t == tIDENT) {
		dev = device_lookup_by_name(db_tok_string);
		t = db_read_token();
		if (t == tNUMBER) {
			tid = db_tok_number;
			valid = true;
		}
	}	
	db_radix = radix;
	db_skip_to_eol();
	if (!valid) {
		db_printf("usage: show t4 tcb <nexus> <tid>\n");
		return;
	}

	if (dev == NULL) {
		db_printf("device not found\n");
		return;
	}
	if (tid < 0) {
		db_printf("invalid tid\n");
		return;
	}

	t4_dump_tcb(device_get_softc(dev), tid);
}
#endif

/*
 * Borrowed from cesa_prep_aes_key().
 *
 * NB: The crypto engine wants the words in the decryption key in reverse
 * order.
 */
void
t4_aes_getdeckey(void *dec_key, const void *enc_key, unsigned int kbits)
{
	uint32_t ek[4 * (RIJNDAEL_MAXNR + 1)];
	uint32_t *dkey;
	int i;

	rijndaelKeySetupEnc(ek, enc_key, kbits);
	dkey = dec_key;
	dkey += (kbits / 8) / 4;

	switch (kbits) {
	case 128:
		for (i = 0; i < 4; i++)
			*--dkey = htobe32(ek[4 * 10 + i]);
		break;
	case 192:
		for (i = 0; i < 2; i++)
			*--dkey = htobe32(ek[4 * 11 + 2 + i]);
		for (i = 0; i < 4; i++)
			*--dkey = htobe32(ek[4 * 12 + i]);
		break;
	case 256:
		for (i = 0; i < 4; i++)
			*--dkey = htobe32(ek[4 * 13 + i]);
		for (i = 0; i < 4; i++)
			*--dkey = htobe32(ek[4 * 14 + i]);
		break;
	}
	MPASS(dkey == dec_key);
}

static struct sx mlu;	/* mod load unload */
SX_SYSINIT(cxgbe_mlu, &mlu, "cxgbe mod load/unload");

static int
mod_event(module_t mod, int cmd, void *arg)
{
	int rc = 0;
	static int loaded = 0;

	switch (cmd) {
	case MOD_LOAD:
		sx_xlock(&mlu);
		if (loaded++ == 0) {
			t4_sge_modload();
			t4_register_shared_cpl_handler(CPL_SET_TCB_RPL,
			    t4_filter_rpl, CPL_COOKIE_FILTER);
			t4_register_shared_cpl_handler(CPL_L2T_WRITE_RPL,
			    do_l2t_write_rpl, CPL_COOKIE_FILTER);
			t4_register_shared_cpl_handler(CPL_ACT_OPEN_RPL,
			    t4_hashfilter_ao_rpl, CPL_COOKIE_HASHFILTER);
			t4_register_shared_cpl_handler(CPL_SET_TCB_RPL,
			    t4_hashfilter_tcb_rpl, CPL_COOKIE_HASHFILTER);
			t4_register_shared_cpl_handler(CPL_ABORT_RPL_RSS,
			    t4_del_hashfilter_rpl, CPL_COOKIE_HASHFILTER);
			t4_register_cpl_handler(CPL_TRACE_PKT, t4_trace_pkt);
			t4_register_cpl_handler(CPL_T5_TRACE_PKT, t5_trace_pkt);
			t4_register_cpl_handler(CPL_SMT_WRITE_RPL,
			    do_smt_write_rpl);
			sx_init(&t4_list_lock, "T4/T5 adapters");
			SLIST_INIT(&t4_list);
			callout_init(&fatal_callout, 1);
#ifdef TCP_OFFLOAD
			sx_init(&t4_uld_list_lock, "T4/T5 ULDs");
			SLIST_INIT(&t4_uld_list);
#endif
#ifdef INET6
			t4_clip_modload();
#endif
			t4_tracer_modload();
			tweak_tunables();
		}
		sx_xunlock(&mlu);
		break;

	case MOD_UNLOAD:
		sx_xlock(&mlu);
		if (--loaded == 0) {
			int tries;

			sx_slock(&t4_list_lock);
			if (!SLIST_EMPTY(&t4_list)) {
				rc = EBUSY;
				sx_sunlock(&t4_list_lock);
				goto done_unload;
			}
#ifdef TCP_OFFLOAD
			sx_slock(&t4_uld_list_lock);
			if (!SLIST_EMPTY(&t4_uld_list)) {
				rc = EBUSY;
				sx_sunlock(&t4_uld_list_lock);
				sx_sunlock(&t4_list_lock);
				goto done_unload;
			}
#endif
			tries = 0;
			while (tries++ < 5 && t4_sge_extfree_refs() != 0) {
				uprintf("%ju clusters with custom free routine "
				    "still is use.\n", t4_sge_extfree_refs());
				pause("t4unload", 2 * hz);
			}
#ifdef TCP_OFFLOAD
			sx_sunlock(&t4_uld_list_lock);
#endif
			sx_sunlock(&t4_list_lock);

			if (t4_sge_extfree_refs() == 0) {
				t4_tracer_modunload();
#ifdef INET6
				t4_clip_modunload();
#endif
#ifdef TCP_OFFLOAD
				sx_destroy(&t4_uld_list_lock);
#endif
				sx_destroy(&t4_list_lock);
				t4_sge_modunload();
				loaded = 0;
			} else {
				rc = EBUSY;
				loaded++;	/* undo earlier decrement */
			}
		}
done_unload:
		sx_xunlock(&mlu);
		break;
	}

	return (rc);
}

static devclass_t t4_devclass, t5_devclass, t6_devclass;
static devclass_t cxgbe_devclass, cxl_devclass, cc_devclass;
static devclass_t vcxgbe_devclass, vcxl_devclass, vcc_devclass;

DRIVER_MODULE(t4nex, pci, t4_driver, t4_devclass, mod_event, 0);
MODULE_VERSION(t4nex, 1);
MODULE_DEPEND(t4nex, firmware, 1, 1, 1);
#ifdef DEV_NETMAP
MODULE_DEPEND(t4nex, netmap, 1, 1, 1);
#endif /* DEV_NETMAP */

DRIVER_MODULE(t5nex, pci, t5_driver, t5_devclass, mod_event, 0);
MODULE_VERSION(t5nex, 1);
MODULE_DEPEND(t5nex, firmware, 1, 1, 1);
#ifdef DEV_NETMAP
MODULE_DEPEND(t5nex, netmap, 1, 1, 1);
#endif /* DEV_NETMAP */

DRIVER_MODULE(t6nex, pci, t6_driver, t6_devclass, mod_event, 0);
MODULE_VERSION(t6nex, 1);
MODULE_DEPEND(t6nex, firmware, 1, 1, 1);
#ifdef DEV_NETMAP
MODULE_DEPEND(t6nex, netmap, 1, 1, 1);
#endif /* DEV_NETMAP */

DRIVER_MODULE(cxgbe, t4nex, cxgbe_driver, cxgbe_devclass, 0, 0);
MODULE_VERSION(cxgbe, 1);

DRIVER_MODULE(cxl, t5nex, cxl_driver, cxl_devclass, 0, 0);
MODULE_VERSION(cxl, 1);

DRIVER_MODULE(cc, t6nex, cc_driver, cc_devclass, 0, 0);
MODULE_VERSION(cc, 1);

DRIVER_MODULE(vcxgbe, cxgbe, vcxgbe_driver, vcxgbe_devclass, 0, 0);
MODULE_VERSION(vcxgbe, 1);

DRIVER_MODULE(vcxl, cxl, vcxl_driver, vcxl_devclass, 0, 0);
MODULE_VERSION(vcxl, 1);

DRIVER_MODULE(vcc, cc, vcc_driver, vcc_devclass, 0, 0);
MODULE_VERSION(vcc, 1);
