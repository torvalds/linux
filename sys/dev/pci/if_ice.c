/*	$OpenBSD: if_ice.c,v 1.59 2025/09/17 12:54:19 jan Exp $	*/

/*  Copyright (c) 2024, Intel Corporation
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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
 */

/*
 * Ported from FreeBSD ice(4) and OpenBSD ixl(4) by Stefan Sperling in 2024.
 *
 * Copyright (c) 2016,2017 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2024 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rwlock.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/intrmap.h>
#include <sys/kernel.h>

#include <sys/refcnt.h>
#include <sys/task.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_media.h>
#include <net/ethertypes.h>
#include <net/toeplitz.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>

#define STRUCT_HACK_VAR_LEN

/**
 * ice_struct_size - size of struct with C99 flexible array member
 * @ptr: pointer to structure
 * @field: flexible array member (last member of the structure)
 * @num: number of elements of that flexible array member
 */
#define ice_struct_size(ptr, field, num) \
	(sizeof(*(ptr)) + sizeof(*(ptr)->field) * (num))

#define FLEX_ARRAY_SIZE(_ptr, _mem, cnt) ((cnt) * sizeof(_ptr->_mem[0]))

#include "if_icereg.h"
#include "if_icevar.h"

/*
 * Our network stack cannot handle packets greater than MAXMCLBYTES.
 * This interface cannot handle packets greater than ICE_TSO_SIZE.
 */
CTASSERT(MAXMCLBYTES < ICE_TSO_SIZE);

/**
 * @var ice_driver_version
 * @brief driver version string
 *
 * Driver version information, used as part of the driver information
 * sent to the firmware at load.
 *
 * @var ice_major_version
 * @brief driver major version number
 *
 * @var ice_minor_version
 * @brief driver minor version number
 *
 * @var ice_patch_version
 * @brief driver patch version number
 *
 * @var ice_rc_version
 * @brief driver release candidate version number
 */
const char ice_driver_version[] = "1.39.13-k";
const uint8_t ice_major_version = 1;
const uint8_t ice_minor_version = 39;
const uint8_t ice_patch_version = 13;
const uint8_t ice_rc_version = 0;

typedef void *ice_match_t;

static const struct pci_matchid ice_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_E810_C_QSFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_E810_C_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_E810_XXV_QSFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_E810_XXV_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_E823_L_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_E823_L_10G },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_E823_L_1G },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_E823_L_QSFP },
};

int
ice_match(struct device *parent, ice_match_t match __unused, void *aux)
{
	struct pci_attach_args *pa = aux;
	return pci_matchbyid(pa, ice_devices, nitems(ice_devices));
}

#ifdef ICE_DEBUG
#define DPRINTF(x...)		do { if (ice_debug) printf(x); } while(0)
#define DNPRINTF(n,x...)	do { if (ice_debug & n) printf(x); } while(0)
#define ICE_DBG_TRACE		(1UL << 0) /* for function-trace only */
#define ICE_DBG_INIT		(1UL << 1)
#define ICE_DBG_RELEASE		(1UL << 2)
#define ICE_DBG_FW_LOG		(1UL << 3)
#define ICE_DBG_LINK		(1UL << 4)
#define ICE_DBG_PHY		(1UL << 5)
#define ICE_DBG_QCTX		(1UL << 6)
#define ICE_DBG_NVM		(1UL << 7)
#define ICE_DBG_LAN		(1UL << 8)
#define ICE_DBG_FLOW		(1UL << 9)
#define ICE_DBG_DCB		(1UL << 10)
#define ICE_DBG_DIAG		(1UL << 11)
#define ICE_DBG_FD		(1UL << 12)
#define ICE_DBG_SW		(1UL << 13)
#define ICE_DBG_SCHED		(1UL << 14)
#define ICE_DBG_RDMA		(1UL << 15)
#define ICE_DBG_PKG		(1UL << 16)
#define ICE_DBG_RES		(1UL << 17)
#define ICE_DBG_AQ_MSG		(1UL << 24)
#define ICE_DBG_AQ_DESC		(1UL << 25)
#define ICE_DBG_AQ_DESC_BUF	(1UL << 26)
#define ICE_DBG_AQ_CMD		(1UL << 27)
#define ICE_DBG_AQ		(ICE_DBG_AQ_MSG		| \
				 ICE_DBG_AQ_DESC	| \
				 ICE_DBG_AQ_DESC_BUF	| \
				 ICE_DBG_AQ_CMD)
#define ICE_DBG_PARSER		(1UL << 28)
#define ICE_DBG_USER		(1UL << 31)
uint32_t	ice_debug = 0xffffffff & ~(ICE_DBG_AQ);
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

#define ICE_READ(hw, reg)						\
	bus_space_read_4((hw)->hw_sc->sc_st, (hw)->hw_sc->sc_sh, (reg))

#define ICE_READ_8(hw, reg)						\
	bus_space_read_8((hw)->hw_sc->sc_st, (hw)->hw_sc->sc_sh, (reg))

#define ICE_WRITE(hw, reg, val)						\
	bus_space_write_4((hw)->hw_sc->sc_st, (hw)->hw_sc->sc_sh, (reg), (val))

#define ice_flush(_hw) ICE_READ((_hw), GLGEN_STAT)

/* Data type manipulation macros. */
#define ICE_HI_DWORD(x)		((uint32_t)((((x) >> 16) >> 16) & 0xFFFFFFFF))
#define ICE_LO_DWORD(x)		((uint32_t)((x) & 0xFFFFFFFF))
#define ICE_HI_WORD(x)		((uint16_t)(((x) >> 16) & 0xFFFF))
#define ICE_LO_WORD(x)		((uint16_t)((x) & 0xFFFF))
#define ICE_HI_BYTE(x)		((uint8_t)(((x) >> 8) & 0xFF))
#define ICE_LO_BYTE(x)		((uint8_t)((x) & 0xFF))

uint16_t ice_lock_count;

/**
 * @enum feat_list
 * @brief driver feature enumeration
 *
 * Enumeration of possible device driver features that can be enabled or
 * disabled. Each possible value represents a different feature which can be
 * enabled or disabled.
 *
 * The driver stores a bitmap of the features that the device and OS are
 * capable of, as well as another bitmap indicating which features are
 * currently enabled for that device.
 */
enum feat_list {
	ICE_FEATURE_SRIOV,
	ICE_FEATURE_RSS,
	ICE_FEATURE_NETMAP,
	ICE_FEATURE_FDIR,
	ICE_FEATURE_MSI,
	ICE_FEATURE_MSIX,
	ICE_FEATURE_RDMA,
	ICE_FEATURE_SAFE_MODE,
	ICE_FEATURE_LENIENT_LINK_MODE,
	ICE_FEATURE_LINK_MGMT_VER_1,
	ICE_FEATURE_LINK_MGMT_VER_2,
	ICE_FEATURE_HEALTH_STATUS,
	ICE_FEATURE_FW_LOGGING,
	ICE_FEATURE_HAS_PBA,
	ICE_FEATURE_DCB,
	ICE_FEATURE_TX_BALANCE,
	ICE_FEATURE_DUAL_NAC,
	ICE_FEATURE_TEMP_SENSOR,
	/* Must be last entry */
	ICE_FEATURE_COUNT
};

struct ice_intr_vector {
	struct ice_softc	*iv_sc;
	struct ice_rx_queue	*iv_rxq;
	struct ice_tx_queue	*iv_txq;
	int			 iv_qid;
	void			*iv_ihc;
	char			 iv_name[16];
	pci_intr_handle_t	 ih;
};

#define ICE_MAX_VECTORS			8 /* XXX this is pretty arbitrary */

static struct rwlock ice_sff_lock = RWLOCK_INITIALIZER("icesff");

struct ice_softc {
	struct device sc_dev;
	struct arpcom		sc_ac;
	struct ifmedia		media;

	bus_space_tag_t sc_st;
	bus_space_handle_t sc_sh;
	bus_size_t sc_sz;
	bus_dma_tag_t sc_dmat;
	pci_product_id_t sc_pid;
	pci_chipset_tag_t sc_pct;
	pcitag_t sc_pcitag;

	pci_intr_handle_t sc_ih;
	void *sc_ihc;
	unsigned int sc_nmsix_max;
	unsigned int sc_nmsix;
	unsigned int sc_nqueues;
	struct intrmap *sc_intrmap;
	struct ice_intr_vector *sc_vectors;
	size_t sc_nvectors;

	struct task sc_admin_task;
	struct timeout sc_admin_timer;

	struct rwlock sc_cfg_lock;
	unsigned int sc_dead;

	enum ice_state state;
	struct ice_hw hw;

	struct ice_vsi pf_vsi;		/* Main PF VSI */

	/* Tri-state feature flags (capable/enabled) */
	ice_declare_bitmap(feat_cap, ICE_FEATURE_COUNT);
	ice_declare_bitmap(feat_en, ICE_FEATURE_COUNT);

	struct ice_resmgr os_imgr;

	/* isc_* fields inherited from FreeBSD iflib struct if_softc_ctx */
	int isc_tx_nsegments;
	int isc_ntxd[8];
	int isc_nrxd[8];
	int isc_tx_tso_segments_max;
	int isc_tx_tso_size_max;
	int isc_tx_tso_segsize_max;
	int isc_nrxqsets_max;
	int isc_ntxqsets_max;

	/* Tx/Rx queue managers */
	struct ice_resmgr tx_qmgr;
	struct ice_resmgr rx_qmgr;

	/* device statistics */
	struct ice_pf_hw_stats stats;
	struct ice_pf_sw_stats soft_stats;

	struct ice_vsi **all_vsi;	/* Array of VSI pointers */
	uint16_t num_available_vsi;	/* Size of VSI array */

	/* Interrupt allocation manager */
	struct ice_resmgr dev_imgr;
	uint16_t *pf_imap;
	int lan_vectors;

	/* NVM link override settings */
	struct ice_link_default_override_tlv ldo_tlv;

	bool link_up;

	int rebuild_ticks;

	int sw_intr[ICE_MAX_VECTORS];
};

/**
 * ice_driver_is_detaching - Check if the driver is detaching/unloading
 * @sc: device private softc
 *
 * Returns true if the driver is detaching, false otherwise.
 *
 * @remark on newer kernels, take advantage of iflib_in_detach in order to
 * report detachment correctly as early as possible.
 *
 * @remark this function is used by various code paths that want to avoid
 * running if the driver is about to be removed. This includes sysctls and
 * other driver access points. Note that it does not fully resolve
 * detach-based race conditions as it is possible for a thread to race with
 * iflib_in_detach.
 */
bool
ice_driver_is_detaching(struct ice_softc *sc)
{
	return (ice_test_state(&sc->state, ICE_STATE_DETACHING) || sc->sc_dead);
}

/*
 * ice_usec_delay - Delay for the specified number of microseconds
 * @time: microseconds to delay
 * @sleep: if true, sleep where possible
 *
 * If sleep is true, sleep so that another thread can execute.
 * Otherwise, use DELAY to spin the thread instead.
 */
void
ice_usec_delay(uint32_t time, bool sleep)
{
	if (sleep && !cold)
		tsleep_nsec(&sleep, 0, "icedly", USEC_TO_NSEC(time));
	else
		DELAY(time);
}

/*
 * ice_msec_delay - Delay for the specified number of milliseconds
 * @time: milliseconds to delay
 * @sleep: if true, sleep where possible
 *
 * If sleep is true, sleep so that another thread can execute.
 * Otherwise, use DELAY to spin the thread instead.
 */
void
ice_msec_delay(uint32_t time, bool sleep)
{
	if (sleep && !cold)
		tsleep_nsec(&sleep, 0, "icedly", MSEC_TO_NSEC(time));
	else
		DELAY(time * 1000);
}

/**
 * ice_aq_str - Convert an AdminQ error into a string
 * @aq_err: the AQ error code to convert
 *
 * Convert the AdminQ status into its string name, if known. Otherwise, format
 * the error as an integer.
 */
const char *
ice_aq_str(enum ice_aq_err aq_err)
{
	static char buf[ICE_STR_BUF_LEN];
	const char *str = NULL;

	switch (aq_err) {
	case ICE_AQ_RC_OK:
		str = "OK";
		break;
	case ICE_AQ_RC_EPERM:
		str = "AQ_RC_EPERM";
		break;
	case ICE_AQ_RC_ENOENT:
		str = "AQ_RC_ENOENT";
		break;
	case ICE_AQ_RC_ESRCH:
		str = "AQ_RC_ESRCH";
		break;
	case ICE_AQ_RC_EINTR:
		str = "AQ_RC_EINTR";
		break;
	case ICE_AQ_RC_EIO:
		str = "AQ_RC_EIO";
		break;
	case ICE_AQ_RC_ENXIO:
		str = "AQ_RC_ENXIO";
		break;
	case ICE_AQ_RC_E2BIG:
		str = "AQ_RC_E2BIG";
		break;
	case ICE_AQ_RC_EAGAIN:
		str = "AQ_RC_EAGAIN";
		break;
	case ICE_AQ_RC_ENOMEM:
		str = "AQ_RC_ENOMEM";
		break;
	case ICE_AQ_RC_EACCES:
		str = "AQ_RC_EACCES";
		break;
	case ICE_AQ_RC_EFAULT:
		str = "AQ_RC_EFAULT";
		break;
	case ICE_AQ_RC_EBUSY:
		str = "AQ_RC_EBUSY";
		break;
	case ICE_AQ_RC_EEXIST:
		str = "AQ_RC_EEXIST";
		break;
	case ICE_AQ_RC_EINVAL:
		str = "AQ_RC_EINVAL";
		break;
	case ICE_AQ_RC_ENOTTY:
		str = "AQ_RC_ENOTTY";
		break;
	case ICE_AQ_RC_ENOSPC:
		str = "AQ_RC_ENOSPC";
		break;
	case ICE_AQ_RC_ENOSYS:
		str = "AQ_RC_ENOSYS";
		break;
	case ICE_AQ_RC_ERANGE:
		str = "AQ_RC_ERANGE";
		break;
	case ICE_AQ_RC_EFLUSHED:
		str = "AQ_RC_EFLUSHED";
		break;
	case ICE_AQ_RC_BAD_ADDR:
		str = "AQ_RC_BAD_ADDR";
		break;
	case ICE_AQ_RC_EMODE:
		str = "AQ_RC_EMODE";
		break;
	case ICE_AQ_RC_EFBIG:
		str = "AQ_RC_EFBIG";
		break;
	case ICE_AQ_RC_ESBCOMP:
		str = "AQ_RC_ESBCOMP";
		break;
	case ICE_AQ_RC_ENOSEC:
		str = "AQ_RC_ENOSEC";
		break;
	case ICE_AQ_RC_EBADSIG:
		str = "AQ_RC_EBADSIG";
		break;
	case ICE_AQ_RC_ESVN:
		str = "AQ_RC_ESVN";
		break;
	case ICE_AQ_RC_EBADMAN:
		str = "AQ_RC_EBADMAN";
		break;
	case ICE_AQ_RC_EBADBUF:
		str = "AQ_RC_EBADBUF";
		break;
	case ICE_AQ_RC_EACCES_BMCU:
		str = "AQ_RC_EACCES_BMCU";
		break;
	}

	if (str)
		snprintf(buf, ICE_STR_BUF_LEN, "%s", str);
	else
		snprintf(buf, ICE_STR_BUF_LEN, "%d", aq_err);

	return buf;
}

/**
 * ice_status_str - convert status err code to a string
 * @status: the status error code to convert
 *
 * Convert the status code into its string name if known.
 *
 * Otherwise, use the scratch space to format the status code into a number.
 */
const char *
ice_status_str(enum ice_status status)
{
	static char buf[ICE_STR_BUF_LEN];
	const char *str = NULL;

	switch (status) {
	case ICE_SUCCESS:
		str = "OK";
		break;
	case ICE_ERR_PARAM:
		str = "ICE_ERR_PARAM";
		break;
	case ICE_ERR_NOT_IMPL:
		str = "ICE_ERR_NOT_IMPL";
		break;
	case ICE_ERR_NOT_READY:
		str = "ICE_ERR_NOT_READY";
		break;
	case ICE_ERR_NOT_SUPPORTED:
		str = "ICE_ERR_NOT_SUPPORTED";
		break;
	case ICE_ERR_BAD_PTR:
		str = "ICE_ERR_BAD_PTR";
		break;
	case ICE_ERR_INVAL_SIZE:
		str = "ICE_ERR_INVAL_SIZE";
		break;
	case ICE_ERR_DEVICE_NOT_SUPPORTED:
		str = "ICE_ERR_DEVICE_NOT_SUPPORTED";
		break;
	case ICE_ERR_RESET_FAILED:
		str = "ICE_ERR_RESET_FAILED";
		break;
	case ICE_ERR_FW_API_VER:
		str = "ICE_ERR_FW_API_VER";
		break;
	case ICE_ERR_NO_MEMORY:
		str = "ICE_ERR_NO_MEMORY";
		break;
	case ICE_ERR_CFG:
		str = "ICE_ERR_CFG";
		break;
	case ICE_ERR_OUT_OF_RANGE:
		str = "ICE_ERR_OUT_OF_RANGE";
		break;
	case ICE_ERR_ALREADY_EXISTS:
		str = "ICE_ERR_ALREADY_EXISTS";
		break;
	case ICE_ERR_NVM:
		str = "ICE_ERR_NVM";
		break;
	case ICE_ERR_NVM_CHECKSUM:
		str = "ICE_ERR_NVM_CHECKSUM";
		break;
	case ICE_ERR_BUF_TOO_SHORT:
		str = "ICE_ERR_BUF_TOO_SHORT";
		break;
	case ICE_ERR_NVM_BLANK_MODE:
		str = "ICE_ERR_NVM_BLANK_MODE";
		break;
	case ICE_ERR_IN_USE:
		str = "ICE_ERR_IN_USE";
		break;
	case ICE_ERR_MAX_LIMIT:
		str = "ICE_ERR_MAX_LIMIT";
		break;
	case ICE_ERR_RESET_ONGOING:
		str = "ICE_ERR_RESET_ONGOING";
		break;
	case ICE_ERR_HW_TABLE:
		str = "ICE_ERR_HW_TABLE";
		break;
	case ICE_ERR_FW_DDP_MISMATCH:
		str = "ICE_ERR_FW_DDP_MISMATCH";
		break;
	case ICE_ERR_DOES_NOT_EXIST:
		str = "ICE_ERR_DOES_NOT_EXIST";
		break;
	case ICE_ERR_AQ_ERROR:
		str = "ICE_ERR_AQ_ERROR";
		break;
	case ICE_ERR_AQ_TIMEOUT:
		str = "ICE_ERR_AQ_TIMEOUT";
		break;
	case ICE_ERR_AQ_FULL:
		str = "ICE_ERR_AQ_FULL";
		break;
	case ICE_ERR_AQ_NO_WORK:
		str = "ICE_ERR_AQ_NO_WORK";
		break;
	case ICE_ERR_AQ_EMPTY:
		str = "ICE_ERR_AQ_EMPTY";
		break;
	case ICE_ERR_AQ_FW_CRITICAL:
		str = "ICE_ERR_AQ_FW_CRITICAL";
		break;
	}

	if (str)
		snprintf(buf, ICE_STR_BUF_LEN, "%s", str);
	else
		snprintf(buf, ICE_STR_BUF_LEN, "%d", status);

	return buf;
}

/**
 * ice_mdd_tx_tclan_str - Convert MDD Tx TCLAN event to a string
 * @event: the MDD event number to convert
 *
 * Convert the Tx TCLAN event value from the GL_MDET_TX_TCLAN register into
 * a human readable string for logging of MDD events.
 */
const char *
ice_mdd_tx_tclan_str(uint8_t event)
{
	static char buf[ICE_STR_BUF_LEN];
	const char *str = NULL;

	switch (event) {
	case 0:
		str = "Wrong descriptor format/order";
		break;
	case 1:
		str = "Descriptor fetch failed";
		break;
	case 2:
		str = "Tail descriptor not EOP/NOP";
		break;
	case 3:
		str = "False scheduling error";
		break;
	case 4:
		str = "Tail value larger than ring len";
		break;
	case 5:
		str = "Too many data commands";
		break;
	case 6:
		str = "Zero packets sent in quanta";
		break;
	case 7:
		str = "Packet too small or too big";
		break;
	case 8:
		str = "TSO length doesn't match sum";
		break;
	case 9:
		str = "TSO tail reached before TLEN";
		break;
	case 10:
		str = "TSO max 3 descs for headers";
		break;
	case 11:
		str = "EOP on header descriptor";
		break;
	case 12:
		str = "MSS is 0 or TLEN is 0";
		break;
	case 13:
		str = "CTX desc invalid IPSec fields";
		break;
	case 14:
		str = "Quanta invalid # of SSO packets";
		break;
	case 15:
		str = "Quanta bytes exceeds pkt_len*64";
		break;
	case 16:
		str = "Quanta exceeds max_cmds_in_sq";
		break;
	case 17:
		str = "incoherent last_lso_quanta";
		break;
	case 18:
		str = "incoherent TSO TLEN";
		break;
	case 19:
		str = "Quanta: too many descriptors";
		break;
	case 20:
		str = "Quanta: # of packets mismatch";
		break;
	default:
		break;
	}

	if (str)
		snprintf(buf, ICE_STR_BUF_LEN, "%s", str);
	else {
		snprintf(buf, ICE_STR_BUF_LEN,
		    "Unknown Tx TCLAN event %u", event);
	}

	return buf;
}

/**
 * ice_mdd_tx_pqm_str - Convert MDD Tx PQM event to a string
 * @event: the MDD event number to convert
 *
 * Convert the Tx PQM event value from the GL_MDET_TX_PQM register into
 * a human readable string for logging of MDD events.
 */
const char *
ice_mdd_tx_pqm_str(uint8_t event)
{
	static char buf[ICE_STR_BUF_LEN];
	const char *str = NULL;

	switch (event) {
	case 0:
		str = "PCI_DUMMY_COMP";
		break;
	case 1:
		str = "PCI_UR_COMP";
		break;
	/* Index 2 is unused */
	case 3:
		str = "RCV_SH_BE_LSO";
		break;
	case 4:
		str = "Q_FL_MNG_EPY_CH";
		break;
	case 5:
		str = "Q_EPY_MNG_FL_CH";
		break;
	case 6:
		str = "LSO_NUMDESCS_ZERO";
		break;
	case 7:
		str = "LSO_LENGTH_ZERO";
		break;
	case 8:
		str = "LSO_MSS_BELOW_MIN";
		break;
	case 9:
		str = "LSO_MSS_ABOVE_MAX";
		break;
	case 10:
		str = "LSO_HDR_SIZE_ZERO";
		break;
	case 11:
		str = "RCV_CNT_BE_LSO";
		break;
	case 12:
		str = "SKIP_ONE_QT_ONLY";
		break;
	case 13:
		str = "LSO_PKTCNT_ZERO";
		break;
	case 14:
		str = "SSO_LENGTH_ZERO";
		break;
	case 15:
		str = "SSO_LENGTH_EXCEED";
		break;
	case 16:
		str = "SSO_PKTCNT_ZERO";
		break;
	case 17:
		str = "SSO_PKTCNT_EXCEED";
		break;
	case 18:
		str = "SSO_NUMDESCS_ZERO";
		break;
	case 19:
		str = "SSO_NUMDESCS_EXCEED";
		break;
	case 20:
		str = "TAIL_GT_RING_LENGTH";
		break;
	case 21:
		str = "RESERVED_DBL_TYPE";
		break;
	case 22:
		str = "ILLEGAL_HEAD_DROP_DBL";
		break;
	case 23:
		str = "LSO_OVER_COMMS_Q";
		break;
	case 24:
		str = "ILLEGAL_VF_QNUM";
		break;
	case 25:
		str = "QTAIL_GT_RING_LENGTH";
		break;
	default:
		break;
	}

	if (str)
		snprintf(buf, ICE_STR_BUF_LEN, "%s", str);
	else {
		snprintf(buf, ICE_STR_BUF_LEN,
		    "Unknown Tx PQM event %u", event);
	}

	return buf;
}

/**
 * ice_mdd_rx_str - Convert MDD Rx queue event to a string
 * @event: the MDD event number to convert
 *
 * Convert the Rx queue event value from the GL_MDET_RX register into a human
 * readable string for logging of MDD events.
 */
const char *
ice_mdd_rx_str(uint8_t event)
{
	static char buf[ICE_STR_BUF_LEN];
	const char *str = NULL;

	switch (event) {
	case 1:
		str = "Descriptor fetch failed";
		break;
	default:
		break;
	}

	if (str)
		snprintf(buf, ICE_STR_BUF_LEN, "%s", str);
	else
		snprintf(buf, ICE_STR_BUF_LEN, "Unknown Rx event %u", event);

	return buf;
}

/* Memory types */
enum ice_memset_type {
	ICE_NONDMA_MEM = 0,
	ICE_DMA_MEM
};

/* Memcpy types */
enum ice_memcpy_type {
	ICE_NONDMA_TO_NONDMA = 0,
	ICE_NONDMA_TO_DMA,
	ICE_DMA_TO_DMA,
	ICE_DMA_TO_NONDMA
};

/*
 * ice_calloc - Allocate an array of elements
 * @hw: the hardware private structure
 * @count: number of elements to allocate
 * @size: the size of each element
 *
 * Allocate memory for an array of items equal to size. Note that the OS
 * compatibility layer assumes all allocation functions will provide zero'd
 * memory.
 */
static inline void *
ice_calloc(struct ice_hw __unused *hw, size_t count, size_t size)
{
	return mallocarray(count, size, M_DEVBUF, M_ZERO | M_NOWAIT);
}

/*
 * ice_malloc - Allocate memory of a specified size
 * @hw: the hardware private structure
 * @size: the size to allocate
 *
 * Allocates memory of the specified size. Note that the OS compatibility
 * layer assumes that all allocations will provide zero'd memory.
 */
static inline void *
ice_malloc(struct ice_hw __unused *hw, size_t size)
{
	return malloc(size, M_DEVBUF, M_ZERO | M_NOWAIT);
}

/*
 * ice_memdup - Allocate a copy of some other memory
 * @hw: private hardware structure
 * @src: the source to copy from
 * @size: allocation size
 *
 * Allocate memory of the specified size, and copy bytes from the src to fill
 * it. We don't need to zero this memory as we immediately initialize it by
 * copying from the src pointer.
 */
static inline void *
ice_memdup(struct ice_hw __unused *hw, const void *src, size_t size)
{
	void *dst = malloc(size, M_DEVBUF, M_NOWAIT);

	if (dst != NULL)
		memcpy(dst, src, size);

	return dst;
}

/*
 * ice_free - Free previously allocated memory
 * @hw: the hardware private structure
 * @mem: pointer to the memory to free
 *
 * Free memory that was previously allocated by ice_calloc, ice_malloc, or
 * ice_memdup.
 */
static inline void
ice_free(struct ice_hw __unused *hw, void *mem)
{
	free(mem, M_DEVBUF, 0);
}

/**
 * ice_set_ctrlq_len - Configure ctrlq lengths for a device
 * @hw: the device hardware structure
 *
 * Configures the control queues for the given device, setting up the
 * specified lengths, prior to initializing hardware.
 */
void
ice_set_ctrlq_len(struct ice_hw *hw)
{
	hw->adminq.num_rq_entries = ICE_AQ_LEN;
	hw->adminq.num_sq_entries = ICE_AQ_LEN;
	hw->adminq.rq_buf_size = ICE_AQ_MAX_BUF_LEN;
	hw->adminq.sq_buf_size = ICE_AQ_MAX_BUF_LEN;

	hw->mailboxq.num_rq_entries = ICE_MBXQ_LEN;
	hw->mailboxq.num_sq_entries = ICE_MBXQ_LEN;
	hw->mailboxq.rq_buf_size = ICE_MBXQ_MAX_BUF_LEN;
	hw->mailboxq.sq_buf_size = ICE_MBXQ_MAX_BUF_LEN;

}

enum ice_fw_modes
ice_get_fw_mode(struct ice_hw *hw)
{
#define ICE_FW_MODE_DBG_M (1 << 0)
#define ICE_FW_MODE_REC_M (1 << 1)
#define ICE_FW_MODE_ROLLBACK_M (1 << 2)
	uint32_t fw_mode;

	/* check the current FW mode */
	fw_mode = ICE_READ(hw, GL_MNG_FWSM) & GL_MNG_FWSM_FW_MODES_M;
	if (fw_mode & ICE_FW_MODE_DBG_M)
		return ICE_FW_MODE_DBG;
	else if (fw_mode & ICE_FW_MODE_REC_M)
		return ICE_FW_MODE_REC;
	else if (fw_mode & ICE_FW_MODE_ROLLBACK_M)
		return ICE_FW_MODE_ROLLBACK;
	else
		return ICE_FW_MODE_NORMAL;
}

void
ice_set_mac_type(struct ice_hw *hw)
{
	struct ice_softc *sc = hw->hw_sc;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	switch (sc->sc_pid) {
#if 0
	case ICE_DEV_ID_E810C_BACKPLANE:
	case ICE_DEV_ID_E810_XXV_BACKPLANE:
#endif
	case PCI_PRODUCT_INTEL_E810_C_QSFP:
	case PCI_PRODUCT_INTEL_E810_C_SFP:
	case PCI_PRODUCT_INTEL_E810_XXV_QSFP:
	case PCI_PRODUCT_INTEL_E810_XXV_SFP:
		hw->mac_type = ICE_MAC_E810;
		break;
#if 0
	case ICE_DEV_ID_E822C_10G_BASE_T:
	case ICE_DEV_ID_E822C_BACKPLANE:
	case ICE_DEV_ID_E822C_QSFP:
	case ICE_DEV_ID_E822C_SFP:
	case ICE_DEV_ID_E822C_SGMII:
	case ICE_DEV_ID_E822L_10G_BASE_T:
	case ICE_DEV_ID_E822L_BACKPLANE:
	case ICE_DEV_ID_E822L_SFP:
	case ICE_DEV_ID_E822L_SGMII:
#endif
	case PCI_PRODUCT_INTEL_E823_L_10G:
	case PCI_PRODUCT_INTEL_E823_L_1G:
	case PCI_PRODUCT_INTEL_E823_L_QSFP:
	case PCI_PRODUCT_INTEL_E823_L_SFP:
#if 0
	case ICE_DEV_ID_E823L_BACKPLANE:
	case ICE_DEV_ID_E823C_10G_BASE_T:
	case ICE_DEV_ID_E823C_BACKPLANE:
	case ICE_DEV_ID_E823C_QSFP:
	case ICE_DEV_ID_E823C_SFP:
	case ICE_DEV_ID_E823C_SGMII:
#endif
		hw->mac_type = ICE_MAC_GENERIC;
		break;
	default:
		hw->mac_type = ICE_MAC_UNKNOWN;
		break;
	}

	DNPRINTF(ICE_DBG_INIT, "mac_type: %d\n", hw->mac_type);
}

enum ice_status
ice_check_reset(struct ice_hw *hw)
{
	uint32_t cnt, reg = 0, grst_timeout, uld_mask, reset_wait_cnt;

	/* Poll for Device Active state in case a recent CORER, GLOBR,
	 * or EMPR has occurred. The grst delay value is in 100ms units.
	 * Add 1sec for outstanding AQ commands that can take a long time.
	 */
	grst_timeout = ((ICE_READ(hw, GLGEN_RSTCTL) & GLGEN_RSTCTL_GRSTDEL_M) >>
			GLGEN_RSTCTL_GRSTDEL_S) + 10;

	for (cnt = 0; cnt < grst_timeout; cnt++) {
		ice_msec_delay(100, true);
		reg = ICE_READ(hw, GLGEN_RSTAT);
		if (!(reg & GLGEN_RSTAT_DEVSTATE_M))
			break;
	}

	if (cnt == grst_timeout) {
		DNPRINTF(ICE_DBG_INIT, "Global reset polling failed to complete.\n");
		return ICE_ERR_RESET_FAILED;
	}

#define ICE_RESET_DONE_MASK	(GLNVM_ULD_PCIER_DONE_M |\
				 GLNVM_ULD_PCIER_DONE_1_M |\
				 GLNVM_ULD_CORER_DONE_M |\
				 GLNVM_ULD_GLOBR_DONE_M |\
				 GLNVM_ULD_POR_DONE_M |\
				 GLNVM_ULD_POR_DONE_1_M |\
				 GLNVM_ULD_PCIER_DONE_2_M)

	uld_mask = ICE_RESET_DONE_MASK | (hw->func_caps.common_cap.iwarp ?
					  GLNVM_ULD_PE_DONE_M : 0);

	reset_wait_cnt = ICE_PF_RESET_WAIT_COUNT;

	/* Device is Active; check Global Reset processes are done */
	for (cnt = 0; cnt < reset_wait_cnt; cnt++) {
		reg = ICE_READ(hw, GLNVM_ULD) & uld_mask;
		if (reg == uld_mask) {
			DNPRINTF(ICE_DBG_INIT, "Global reset processes done. %d\n", cnt);
			break;
		}
		ice_msec_delay(10, true);
	}

	if (cnt == reset_wait_cnt) {
		DNPRINTF(ICE_DBG_INIT, "Wait for Reset Done timed out. "
		    "GLNVM_ULD = 0x%x\n", reg);
		return ICE_ERR_RESET_FAILED;
	}

	return ICE_SUCCESS;
}

/*
 * ice_pf_reset - Reset the PF
 *
 * If a global reset has been triggered, this function checks
 * for its completion and then issues the PF reset
 */
enum ice_status
ice_pf_reset(struct ice_hw *hw)
{
	uint32_t cnt, reg, reset_wait_cnt, cfg_lock_timeout;

	/* If at function entry a global reset was already in progress, i.e.
	 * state is not 'device active' or any of the reset done bits are not
	 * set in GLNVM_ULD, there is no need for a PF Reset; poll until the
	 * global reset is done.
	 */
	if ((ICE_READ(hw, GLGEN_RSTAT) & GLGEN_RSTAT_DEVSTATE_M) ||
	    (ICE_READ(hw, GLNVM_ULD) & ICE_RESET_DONE_MASK) ^ ICE_RESET_DONE_MASK) {
		/* poll on global reset currently in progress until done */
		if (ice_check_reset(hw))
			return ICE_ERR_RESET_FAILED;

		return ICE_SUCCESS;
	}

	/* Reset the PF */
	reg = ICE_READ(hw, PFGEN_CTRL);

	ICE_WRITE(hw, PFGEN_CTRL, (reg | PFGEN_CTRL_PFSWR_M));

	/* Wait for the PFR to complete. The wait time is the global config lock
	 * timeout plus the PFR timeout which will account for a possible reset
	 * that is occurring during a download package operation.
	 */
	reset_wait_cnt = ICE_PF_RESET_WAIT_COUNT;
	cfg_lock_timeout = ICE_GLOBAL_CFG_LOCK_TIMEOUT;

	for (cnt = 0; cnt < cfg_lock_timeout + reset_wait_cnt; cnt++) {
		reg = ICE_READ(hw, PFGEN_CTRL);
		if (!(reg & PFGEN_CTRL_PFSWR_M))
			break;

		ice_msec_delay(1, true);
	}

	if (cnt == cfg_lock_timeout + reset_wait_cnt) {
		DNPRINTF(ICE_DBG_INIT, "PF reset polling failed to complete.\n");
		return ICE_ERR_RESET_FAILED;
	}

	return ICE_SUCCESS;
}

/**
 * ice_reset - Perform different types of reset
 *
 * If anything other than a PF reset is triggered, PXE mode is restored.
 * This has to be cleared using ice_clear_pxe_mode again, once the AQ
 * interface has been restored in the rebuild flow.
 */
enum ice_status
ice_reset(struct ice_hw *hw, enum ice_reset_req req)
{
	uint32_t val = 0;

	switch (req) {
	case ICE_RESET_PFR:
		return ice_pf_reset(hw);
	case ICE_RESET_CORER:
		DNPRINTF(ICE_DBG_INIT, "CoreR requested\n");
		val = GLGEN_RTRIG_CORER_M;
		break;
	case ICE_RESET_GLOBR:
		DNPRINTF(ICE_DBG_INIT, "GlobalR requested\n");
		val = GLGEN_RTRIG_GLOBR_M;
		break;
	default:
		return ICE_ERR_PARAM;
	}

	val |= ICE_READ(hw, GLGEN_RTRIG);
	ICE_WRITE(hw, GLGEN_RTRIG, val);
	ice_flush(hw);

	/* wait for the FW to be ready */
	return ice_check_reset(hw);
}

/*
 * ice_get_itr_intrl_gran
 *
 * Determines the ITR/INTRL granularities based on the maximum aggregate
 * bandwidth according to the device's configuration during power-on.
 */
void
ice_get_itr_intrl_gran(struct ice_hw *hw)
{
	uint8_t max_agg_bw = (ICE_READ(hw, GL_PWR_MODE_CTL) &
			 GL_PWR_MODE_CTL_CAR_MAX_BW_M) >>
			GL_PWR_MODE_CTL_CAR_MAX_BW_S;

	switch (max_agg_bw) {
	case ICE_MAX_AGG_BW_200G:
	case ICE_MAX_AGG_BW_100G:
	case ICE_MAX_AGG_BW_50G:
		hw->itr_gran = ICE_ITR_GRAN_ABOVE_25;
		hw->intrl_gran = ICE_INTRL_GRAN_ABOVE_25;
		break;
	case ICE_MAX_AGG_BW_25G:
		hw->itr_gran = ICE_ITR_GRAN_MAX_25;
		hw->intrl_gran = ICE_INTRL_GRAN_MAX_25;
		break;
	}
}

/*
 * ice_destroy_ctrlq_locks - Destroy locks for a control queue
 * @cq: pointer to the control queue
 *
 * Destroys the send and receive queue locks for a given control queue.
 */
void
ice_destroy_ctrlq_locks(struct ice_ctl_q_info *cq)
{
#if 0
	ice_destroy_lock(&cq->sq_lock);
	ice_destroy_lock(&cq->rq_lock);
#endif
}

/* Returns true if Queue is enabled else false. */
bool
ice_check_sq_alive(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	/* check both queue-length and queue-enable fields */
	if (cq->sq.len && cq->sq.len_mask && cq->sq.len_ena_mask)
		return (ICE_READ(hw, cq->sq.len) & (cq->sq.len_mask |
						cq->sq.len_ena_mask)) ==
			(cq->num_sq_entries | cq->sq.len_ena_mask);

	return false;
}

#define ICE_CQ_INIT_REGS(qinfo, prefix)				\
do {								\
	(qinfo)->sq.head = prefix##_ATQH;			\
	(qinfo)->sq.tail = prefix##_ATQT;			\
	(qinfo)->sq.len = prefix##_ATQLEN;			\
	(qinfo)->sq.bah = prefix##_ATQBAH;			\
	(qinfo)->sq.bal = prefix##_ATQBAL;			\
	(qinfo)->sq.len_mask = prefix##_ATQLEN_ATQLEN_M;	\
	(qinfo)->sq.len_ena_mask = prefix##_ATQLEN_ATQENABLE_M;	\
	(qinfo)->sq.len_crit_mask = prefix##_ATQLEN_ATQCRIT_M;	\
	(qinfo)->sq.head_mask = prefix##_ATQH_ATQH_M;		\
	(qinfo)->rq.head = prefix##_ARQH;			\
	(qinfo)->rq.tail = prefix##_ARQT;			\
	(qinfo)->rq.len = prefix##_ARQLEN;			\
	(qinfo)->rq.bah = prefix##_ARQBAH;			\
	(qinfo)->rq.bal = prefix##_ARQBAL;			\
	(qinfo)->rq.len_mask = prefix##_ARQLEN_ARQLEN_M;	\
	(qinfo)->rq.len_ena_mask = prefix##_ARQLEN_ARQENABLE_M;	\
	(qinfo)->rq.len_crit_mask = prefix##_ARQLEN_ARQCRIT_M;	\
	(qinfo)->rq.head_mask = prefix##_ARQH_ARQH_M;		\
} while (0)

/*
 * ice_adminq_init_regs - Initialize AdminQ registers
 *
 * This assumes the alloc_sq and alloc_rq functions have already been called
 */
void
ice_adminq_init_regs(struct ice_hw *hw)
{
	struct ice_ctl_q_info *cq = &hw->adminq;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	ICE_CQ_INIT_REGS(cq, PF_FW);
}

/*
 * ice_mailbox_init_regs - Initialize Mailbox registers
 *
 * This assumes the alloc_sq and alloc_rq functions have already been called
 */
void
ice_mailbox_init_regs(struct ice_hw *hw)
{
	struct ice_ctl_q_info *cq = &hw->mailboxq;

	ICE_CQ_INIT_REGS(cq, PF_MBX);
}

/*
 * ice_free_dma_mem - Free DMA memory allocated by ice_alloc_dma_mem
 * @hw: the hardware private structure
 * @mem: DMA memory to free
 *
 * Release the bus DMA tag and map, and free the DMA memory associated with
 * it.
 */
void
ice_free_dma_mem(struct ice_hw __unused *hw, struct ice_dma_mem *mem)
{
	if (mem->map != NULL) {
		if (mem->va != NULL) {
			bus_dmamap_sync(mem->tag, mem->map, 0, mem->size,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(mem->tag, mem->map);
			bus_dmamem_unmap(mem->tag, mem->va, mem->size);
			bus_dmamem_free(mem->tag, &mem->seg, 1);
			mem->va = NULL;
		}

		bus_dmamap_destroy(mem->tag, mem->map);
		mem->map = NULL;
		mem->pa = 0L;
	}
}

/* ice_alloc_dma_mem - Request OS to allocate DMA memory */
void *
ice_alloc_dma_mem(struct ice_hw *hw, struct ice_dma_mem *mem, uint64_t size)
{
	struct ice_softc *sc = hw->hw_sc;
	int nsegs = 0, err;
	caddr_t va;

	mem->tag = sc->sc_dmat;

	err = bus_dmamap_create(mem->tag, size, 1, size, 0, BUS_DMA_NOWAIT,
	    &mem->map);
	if (err)
		goto fail;

	err = bus_dmamem_alloc(mem->tag, size, 1, 0, &mem->seg, 1, &nsegs,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (err || nsegs != 1)
		goto fail_1;

	err = bus_dmamem_map(mem->tag, &mem->seg, nsegs, size, &va,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (err)
		goto fail_2;

	mem->va = va;
	mem->size = size;

	err = bus_dmamap_load(mem->tag, mem->map, mem->va, size, NULL,
	    BUS_DMA_NOWAIT);
	if (err)
		goto fail_3;

	bus_dmamap_sync(mem->tag, mem->map, 0, size, BUS_DMASYNC_PREWRITE);
	mem->pa = mem->map->dm_segs[0].ds_addr;
	return (mem->va);
fail_3:
	bus_dmamem_unmap(mem->tag, mem->va, size);
fail_2:
	bus_dmamem_free(mem->tag, &mem->seg, nsegs);
fail_1:
	bus_dmamap_destroy(mem->tag, mem->map);
fail:
	mem->map = NULL;
	mem->tag = NULL;
	mem->va = NULL;
	mem->pa = 0L;
	mem->size = 0;
	return (NULL);
}

/* ice_alloc_ctrlq_sq_ring - Allocate Control Transmit Queue (ATQ) rings */
enum ice_status
ice_alloc_ctrlq_sq_ring(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	size_t size = cq->num_sq_entries * sizeof(struct ice_aq_desc);

	cq->sq.desc_buf.va = ice_alloc_dma_mem(hw, &cq->sq.desc_buf, size);
	if (!cq->sq.desc_buf.va)
		return ICE_ERR_NO_MEMORY;

	return ICE_SUCCESS;
}

/* ice_alloc_ctrlq_rq_ring - Allocate Control Receive Queue (ARQ) rings */
enum ice_status
ice_alloc_ctrlq_rq_ring(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	size_t size = cq->num_rq_entries * sizeof(struct ice_aq_desc);

	cq->rq.desc_buf.va = ice_alloc_dma_mem(hw, &cq->rq.desc_buf, size);
	if (!cq->rq.desc_buf.va)
		return ICE_ERR_NO_MEMORY;

	return ICE_SUCCESS;
}

/* ice_alloc_sq_bufs - Allocate empty buffer structs for the ATQ */
enum ice_status
ice_alloc_sq_bufs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	int i;

	/* No mapped memory needed yet, just the buffer info structures */
	cq->sq.dma_head = ice_calloc(hw, cq->num_sq_entries,
				     sizeof(cq->sq.desc_buf));
	if (!cq->sq.dma_head)
		return ICE_ERR_NO_MEMORY;
	cq->sq.r.sq_bi = (struct ice_dma_mem *)cq->sq.dma_head;

	/* allocate the mapped buffers */
	for (i = 0; i < cq->num_sq_entries; i++) {
		struct ice_dma_mem *bi;

		bi = &cq->sq.r.sq_bi[i];
		bi->va = ice_alloc_dma_mem(hw, bi, cq->sq_buf_size);
		if (!bi->va)
			goto unwind_alloc_sq_bufs;
	}
	return ICE_SUCCESS;

unwind_alloc_sq_bufs:
	/* don't try to free the one that failed... */
	i--;
	for (; i >= 0; i--)
		ice_free_dma_mem(hw, &cq->sq.r.sq_bi[i]);
	cq->sq.r.sq_bi = NULL;
	ice_free(hw, cq->sq.dma_head);
	cq->sq.dma_head = NULL;

	return ICE_ERR_NO_MEMORY;
}

/*
 * ice_free_cq_ring - Free control queue ring
 * @hw: pointer to the hardware structure
 * @ring: pointer to the specific control queue ring
 *
 * This assumes the posted buffers have already been cleaned
 * and de-allocated
 */
void
ice_free_cq_ring(struct ice_hw *hw, struct ice_ctl_q_ring *ring)
{
	ice_free_dma_mem(hw, &ring->desc_buf);
}

/* ice_alloc_rq_bufs - Allocate pre-posted buffers for the ARQ */
enum ice_status
ice_alloc_rq_bufs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	int i;

	/*
	 * We'll be allocating the buffer info memory first, then we can
	 * allocate the mapped buffers for the event processing
	 */
	cq->rq.dma_head = ice_calloc(hw, cq->num_rq_entries,
				     sizeof(cq->rq.desc_buf));
	if (!cq->rq.dma_head)
		return ICE_ERR_NO_MEMORY;

	cq->rq.r.rq_bi = (struct ice_dma_mem *)cq->rq.dma_head;

	/* allocate the mapped buffers */
	for (i = 0; i < cq->num_rq_entries; i++) {
		struct ice_aq_desc *desc;
		struct ice_dma_mem *bi;

		bi = &cq->rq.r.rq_bi[i];
		bi->va = ice_alloc_dma_mem(hw, bi, cq->rq_buf_size);
		if (!bi->va)
			goto unwind_alloc_rq_bufs;

		/* now configure the descriptors for use */
		desc = ICE_CTL_Q_DESC(cq->rq, i);

		desc->flags = htole16(ICE_AQ_FLAG_BUF);
		if (cq->rq_buf_size > ICE_AQ_LG_BUF)
			desc->flags |= htole16(ICE_AQ_FLAG_LB);
		desc->opcode = 0;
		/* This is in accordance with control queue design, there is no
		 * register for buffer size configuration
		 */
		desc->datalen = htole16(bi->size);
		desc->retval = 0;
		desc->cookie_high = 0;
		desc->cookie_low = 0;
		desc->params.generic.addr_high = htole32(ICE_HI_DWORD(bi->pa));
		desc->params.generic.addr_low = htole32(ICE_LO_DWORD(bi->pa));
		desc->params.generic.param0 = 0;
		desc->params.generic.param1 = 0;
	}
	return ICE_SUCCESS;

unwind_alloc_rq_bufs:
	/* don't try to free the one that failed... */
	i--;
	for (; i >= 0; i--)
		ice_free_dma_mem(hw, &cq->rq.r.rq_bi[i]);
	cq->rq.r.rq_bi = NULL;
	ice_free(hw, cq->rq.dma_head);
	cq->rq.dma_head = NULL;

	return ICE_ERR_NO_MEMORY;
}

enum ice_status
ice_cfg_cq_regs(struct ice_hw *hw, struct ice_ctl_q_ring *ring,
    uint16_t num_entries)
{
	/* Clear Head and Tail */
	ICE_WRITE(hw, ring->head, 0);
	ICE_WRITE(hw, ring->tail, 0);

	/* set starting point */
	ICE_WRITE(hw, ring->len, (num_entries | ring->len_ena_mask));
	ICE_WRITE(hw, ring->bal, ICE_LO_DWORD(ring->desc_buf.pa));
	ICE_WRITE(hw, ring->bah, ICE_HI_DWORD(ring->desc_buf.pa));

	/* Check one register to verify that config was applied */
	if (ICE_READ(hw, ring->bal) != ICE_LO_DWORD(ring->desc_buf.pa))
		return ICE_ERR_AQ_ERROR;

	return ICE_SUCCESS;
}

/**
 * ice_cfg_sq_regs - configure Control ATQ registers
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * Configure base address and length registers for the transmit queue
 */
enum ice_status
ice_cfg_sq_regs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	return ice_cfg_cq_regs(hw, &cq->sq, cq->num_sq_entries);
}

/*
 * ice_cfg_rq_regs - configure Control ARQ register
 * Configure base address and length registers for the receive (event queue)
 */
enum ice_status
ice_cfg_rq_regs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status status;

	status = ice_cfg_cq_regs(hw, &cq->rq, cq->num_rq_entries);
	if (status)
		return status;

	/* Update tail in the HW to post pre-allocated buffers */
	ICE_WRITE(hw, cq->rq.tail, (uint32_t)(cq->num_rq_entries - 1));

	return ICE_SUCCESS;
}

#define ICE_FREE_CQ_BUFS(hw, qi, ring)					\
do {									\
	/* free descriptors */						\
	if ((qi)->ring.r.ring##_bi) {					\
		int i;							\
									\
		for (i = 0; i < (qi)->num_##ring##_entries; i++)	\
			if ((qi)->ring.r.ring##_bi[i].pa)		\
				ice_free_dma_mem((hw),			\
					&(qi)->ring.r.ring##_bi[i]);	\
	}								\
	/* free DMA head */						\
	ice_free(hw, (qi)->ring.dma_head);				\
} while (0)

/*
 * ice_init_sq - main initialization routine for Control ATQ
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * This is the main initialization routine for the Control Send Queue
 * Prior to calling this function, the driver *MUST* set the following fields
 * in the cq->structure:
 *     - cq->num_sq_entries
 *     - cq->sq_buf_size
 *
 * Do *NOT* hold the lock when calling this as the memory allocation routines
 * called are not going to be atomic context safe
 */
enum ice_status
ice_init_sq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status ret_code;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	if (cq->sq.count > 0) {
		/* queue already initialized */
		ret_code = ICE_ERR_NOT_READY;
		goto init_ctrlq_exit;
	}

	/* verify input for valid configuration */
	if (!cq->num_sq_entries || !cq->sq_buf_size) {
		ret_code = ICE_ERR_CFG;
		goto init_ctrlq_exit;
	}

	cq->sq.next_to_use = 0;
	cq->sq.next_to_clean = 0;

	/* allocate the ring memory */
	ret_code = ice_alloc_ctrlq_sq_ring(hw, cq);
	if (ret_code)
		goto init_ctrlq_exit;

	/* allocate buffers in the rings */
	ret_code = ice_alloc_sq_bufs(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_rings;

	/* initialize base registers */
	ret_code = ice_cfg_sq_regs(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_rings;

	/* success! */
	cq->sq.count = cq->num_sq_entries;
	return ICE_SUCCESS;

init_ctrlq_free_rings:
	ICE_FREE_CQ_BUFS(hw, cq, sq);
	ice_free_cq_ring(hw, &cq->sq);
init_ctrlq_exit:
	return ret_code;
}

/*
 * ice_init_rq - initialize receive side of a control queue
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * The main initialization routine for Receive side of a control queue.
 * Prior to calling this function, the driver *MUST* set the following fields
 * in the cq->structure:
 *     - cq->num_rq_entries
 *     - cq->rq_buf_size
 *
 * Do *NOT* hold the lock when calling this as the memory allocation routines
 * called are not going to be atomic context safe
 */
enum ice_status
ice_init_rq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status ret_code;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	if (cq->rq.count > 0) {
		/* queue already initialized */
		ret_code = ICE_ERR_NOT_READY;
		goto init_ctrlq_exit;
	}

	/* verify input for valid configuration */
	if (!cq->num_rq_entries || !cq->rq_buf_size) {
		ret_code = ICE_ERR_CFG;
		goto init_ctrlq_exit;
	}

	cq->rq.next_to_use = 0;
	cq->rq.next_to_clean = 0;

	/* allocate the ring memory */
	ret_code = ice_alloc_ctrlq_rq_ring(hw, cq);
	if (ret_code)
		goto init_ctrlq_exit;

	/* allocate buffers in the rings */
	ret_code = ice_alloc_rq_bufs(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_rings;

	/* initialize base registers */
	ret_code = ice_cfg_rq_regs(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_rings;

	/* success! */
	cq->rq.count = cq->num_rq_entries;
	goto init_ctrlq_exit;

init_ctrlq_free_rings:
	ICE_FREE_CQ_BUFS(hw, cq, rq);
	ice_free_cq_ring(hw, &cq->rq);
init_ctrlq_exit:
	return ret_code;
}

/*
 * Decide if we should retry the send command routine for the ATQ, depending
 * on the opcode.
 */
bool
ice_should_retry_sq_send_cmd(uint16_t opcode)
{
	switch (opcode) {
	case ice_aqc_opc_dnl_get_status:
	case ice_aqc_opc_dnl_run:
	case ice_aqc_opc_dnl_call:
	case ice_aqc_opc_dnl_read_sto:
	case ice_aqc_opc_dnl_write_sto:
	case ice_aqc_opc_dnl_set_breakpoints:
	case ice_aqc_opc_dnl_read_log:
	case ice_aqc_opc_get_link_topo:
	case ice_aqc_opc_done_alt_write:
	case ice_aqc_opc_lldp_stop:
	case ice_aqc_opc_lldp_start:
	case ice_aqc_opc_lldp_filter_ctrl:
		return true;
	}

	return false;
}

/* based on libsa/hexdump.c */
void
ice_hexdump(const void *addr, size_t size)
{
	const unsigned char *line, *end;
	int byte;

	end = (const char *)addr + size;
	for (line = addr; line < end; line += 16) {
		DPRINTF("%08lx  ", (unsigned long)line);
		for (byte = 0; byte < 16; byte++) {
			if (&line[byte] < end)
				DPRINTF("%02x ", line[byte]);
			else
				DPRINTF("   ");
			if (byte == 7)
				DPRINTF(" ");
		}
		DPRINTF(" |");
		for (byte = 0; byte < 16; byte++) {
			if (&line[byte] < end) {
				if (line[byte] >= ' ' && line[byte] <= '~')
					DPRINTF("%c", line[byte]);
				else
					DPRINTF(".");
			} else
				break;
		}
		DPRINTF("|\n");
	}
	DPRINTF("%08lx\n", (unsigned long)end);
}

/**
 * ice_debug_array - Format and print an array of values to the console
 * @hw: private hardware structure
 * @mask: the debug message type
 * @groupsize: preferred size in bytes to print each chunk
 * @buf: the array buffer to print
 * @len: size of the array buffer
 *
 * Format the given array as a series of uint8_t values with hexadecimal
 * notation and log the contents to the console log.
 *
 * TODO: Currently only supports a group size of 1, due to the way hexdump is
 * implemented.
 */
void
ice_debug_array(struct ice_hw *hw, uint64_t mask, uint32_t rowsize,
		uint32_t __unused groupsize, uint8_t *buf, size_t len)
{
#ifdef ICE_DEBUG
	if (!(mask & ice_debug))
		return;

	/* Make sure the row-size isn't too large */
	if (rowsize > 0xFF)
		rowsize = 0xFF;

	ice_hexdump(buf, len);
#endif
}

/**
 * ice_clean_sq - cleans send side of a control queue
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * returns the number of free desc
 */
uint16_t
ice_clean_sq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	struct ice_ctl_q_ring *sq = &cq->sq;
	uint16_t ntc = sq->next_to_clean;
	struct ice_aq_desc *desc;

	desc = ICE_CTL_Q_DESC(*sq, ntc);

	while (ICE_READ(hw, cq->sq.head) != ntc) {
		DNPRINTF(ICE_DBG_AQ_MSG, "ntc %d head %d.\n", ntc,
		    ICE_READ(hw, cq->sq.head));
		memset(desc, 0, sizeof(*desc));
		ntc++;
		if (ntc == sq->count)
			ntc = 0;
		desc = ICE_CTL_Q_DESC(*sq, ntc);
	}

	sq->next_to_clean = ntc;

	return ICE_CTL_Q_DESC_UNUSED(sq);
}

/**
 * ice_ctl_q_str - Convert control queue type to string
 * @qtype: the control queue type
 *
 * Returns: A string name for the given control queue type.
 */
const char *
ice_ctl_q_str(enum ice_ctl_q qtype)
{
	switch (qtype) {
	case ICE_CTL_Q_UNKNOWN:
		return "Unknown CQ";
	case ICE_CTL_Q_ADMIN:
		return "AQ";
	case ICE_CTL_Q_MAILBOX:
		return "MBXQ";
	default:
		return "Unrecognized CQ";
	}
}

/**
 * ice_debug_cq
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 * @desc: pointer to control queue descriptor
 * @buf: pointer to command buffer
 * @buf_len: max length of buf
 * @response: true if this is the writeback response
 *
 * Dumps debug log about control command with descriptor contents.
 */
void
ice_debug_cq(struct ice_hw *hw, struct ice_ctl_q_info *cq,
	     void *desc, void *buf, uint16_t buf_len, bool response)
{
#ifdef ICE_DEBUG
	struct ice_aq_desc *cq_desc = (struct ice_aq_desc *)desc;
	uint16_t datalen, flags;

	if (!((ICE_DBG_AQ_DESC | ICE_DBG_AQ_DESC_BUF) & ice_debug))
		return;

	if (!desc)
		return;

	datalen = le16toh(cq_desc->datalen);
	flags = le16toh(cq_desc->flags);

	DNPRINTF(ICE_DBG_AQ_DESC, "%s %s: opcode 0x%04X, flags 0x%04X, "
	    "datalen 0x%04X, retval 0x%04X\n", ice_ctl_q_str(cq->qtype),
	    response ? "Response" : "Command", le16toh(cq_desc->opcode),
	    flags, datalen, le16toh(cq_desc->retval));
	DNPRINTF(ICE_DBG_AQ_DESC, "\tcookie (h,l) 0x%08X 0x%08X\n",
		  le32toh(cq_desc->cookie_high),
		  le32toh(cq_desc->cookie_low));
	DNPRINTF(ICE_DBG_AQ_DESC, "\tparam (0,1)  0x%08X 0x%08X\n",
		  le32toh(cq_desc->params.generic.param0),
		  le32toh(cq_desc->params.generic.param1));
	DNPRINTF(ICE_DBG_AQ_DESC, "\taddr (h,l)   0x%08X 0x%08X\n",
		  le32toh(cq_desc->params.generic.addr_high),
		  le32toh(cq_desc->params.generic.addr_low));
	/* Dump buffer iff 1) one exists and 2) is either a response indicated
	 * by the DD and/or CMP flag set or a command with the RD flag set.
	 */
	if (buf && cq_desc->datalen != 0 &&
	    (flags & (ICE_AQ_FLAG_DD | ICE_AQ_FLAG_CMP) ||
	     flags & ICE_AQ_FLAG_RD)) {
		DNPRINTF(ICE_DBG_AQ_DESC_BUF, "Buffer:\n");
		ice_debug_array(hw, ICE_DBG_AQ_DESC_BUF, 16, 1, (uint8_t *)buf,
		    MIN(buf_len, datalen));
	}
#endif
}

/*
 * ice_sq_done - check if the last send on a control queue has completed
 *
 * Returns: true if all the descriptors on the send side of a control queue
 *          are finished processing, false otherwise.
 */
bool
ice_sq_done(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	/* control queue designers suggest use of head for better
	 * timing reliability than DD bit
	 */
	return ICE_READ(hw, cq->sq.head) == cq->sq.next_to_use;
}

/**
 * ice_sq_send_cmd_nolock - send command to a control queue
 * @hw: pointer to the HW struct
 * @cq: pointer to the specific Control queue
 * @desc: prefilled descriptor describing the command (non DMA mem)
 * @buf: buffer to use for indirect commands (or NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (or 0 for direct commands)
 * @cd: pointer to command details structure
 *
 * This is the main send command routine for a control queue. It prepares the
 * command into a descriptor, bumps the send queue tail, waits for the command
 * to complete, captures status and data for the command, etc.
 */
enum ice_status
ice_sq_send_cmd_nolock(struct ice_hw *hw, struct ice_ctl_q_info *cq,
    struct ice_aq_desc *desc, void *buf, uint16_t buf_size,
    struct ice_sq_cd *cd)
{
	struct ice_dma_mem *dma_buf = NULL;
	struct ice_aq_desc *desc_on_ring;
	bool cmd_completed = false;
	enum ice_status status = ICE_SUCCESS;
	uint32_t total_delay = 0;
	uint16_t retval = 0;
	uint32_t val = 0;

	/* if reset is in progress return a soft error */
	if (hw->reset_ongoing)
		return ICE_ERR_RESET_ONGOING;

	cq->sq_last_status = ICE_AQ_RC_OK;

	if (!cq->sq.count) {
		DNPRINTF(ICE_DBG_AQ_MSG,
		    "Control Send queue not initialized.\n");
		status = ICE_ERR_AQ_EMPTY;
		goto sq_send_command_error;
	}

	if ((buf && !buf_size) || (!buf && buf_size)) {
		status = ICE_ERR_PARAM;
		goto sq_send_command_error;
	}

	if (buf) {
		if (buf_size > cq->sq_buf_size) {
			DNPRINTF(ICE_DBG_AQ_MSG,
			    "Invalid buffer size for Control Send queue: %d.\n",
			    buf_size);
			status = ICE_ERR_INVAL_SIZE;
			goto sq_send_command_error;
		}

		desc->flags |= htole16(ICE_AQ_FLAG_BUF);
		if (buf_size > ICE_AQ_LG_BUF)
			desc->flags |= htole16(ICE_AQ_FLAG_LB);
	}

	val = ICE_READ(hw, cq->sq.head);
	if (val >= cq->num_sq_entries) {
		DNPRINTF(ICE_DBG_AQ_MSG,
		    "head overrun at %d in the Control Send Queue ring\n", val);
		status = ICE_ERR_AQ_EMPTY;
		goto sq_send_command_error;
	}

	/* Call clean and check queue available function to reclaim the
	 * descriptors that were processed by FW/MBX; the function returns the
	 * number of desc available. The clean function called here could be
	 * called in a separate thread in case of asynchronous completions.
	 */
	if (ice_clean_sq(hw, cq) == 0) {
		DNPRINTF(ICE_DBG_AQ_MSG,
		    "Error: Control Send Queue is full.\n");
		status = ICE_ERR_AQ_FULL;
		goto sq_send_command_error;
	}

	/* initialize the temp desc pointer with the right desc */
	desc_on_ring = ICE_CTL_Q_DESC(cq->sq, cq->sq.next_to_use);

	/* if the desc is available copy the temp desc to the right place */
	memcpy(desc_on_ring, desc, sizeof(*desc_on_ring));

	/* if buf is not NULL assume indirect command */
	if (buf) {
		dma_buf = &cq->sq.r.sq_bi[cq->sq.next_to_use];
		/* copy the user buf into the respective DMA buf */
		memcpy(dma_buf->va, buf, buf_size);
		desc_on_ring->datalen = htole16(buf_size);

		/* Update the address values in the desc with the pa value
		 * for respective buffer
		 */
		desc_on_ring->params.generic.addr_high =
			htole32(ICE_HI_DWORD(dma_buf->pa));
		desc_on_ring->params.generic.addr_low =
			htole32(ICE_LO_DWORD(dma_buf->pa));
	}

	/* Debug desc and buffer */
	DNPRINTF(ICE_DBG_AQ_DESC, "ATQ: Control Send queue desc and buffer:\n");
	ice_debug_cq(hw, cq, (void *)desc_on_ring, buf, buf_size, false);

	(cq->sq.next_to_use)++;
	if (cq->sq.next_to_use == cq->sq.count)
		cq->sq.next_to_use = 0;
	ICE_WRITE(hw, cq->sq.tail, cq->sq.next_to_use);
	ice_flush(hw);

	/* Wait a short time before initial ice_sq_done() check, to allow
	 * hardware time for completion.
	 */
	ice_usec_delay(5, false);

	do {
		if (ice_sq_done(hw, cq))
			break;

		ice_usec_delay(10, false);
		total_delay++;
	} while (total_delay < cq->sq_cmd_timeout);

	/* if ready, copy the desc back to temp */
	if (ice_sq_done(hw, cq)) {
		memcpy(desc, desc_on_ring, sizeof(*desc));
		if (buf) {
			/* get returned length to copy */
			uint16_t copy_size = le16toh(desc->datalen);

			if (copy_size > buf_size) {
				DNPRINTF(ICE_DBG_AQ_MSG,
				    "Return len %d > than buf len %d\n",
				    copy_size, buf_size);
				status = ICE_ERR_AQ_ERROR;
			} else {
				memcpy(buf, dma_buf->va, copy_size);
			}
		}
		retval = le16toh(desc->retval);
		if (retval) {
			DNPRINTF(ICE_DBG_AQ_MSG, "Control Send Queue "
			    "command 0x%04X completed with error 0x%X\n",
			    le16toh(desc->opcode), retval);

			/* strip off FW internal code */
			retval &= 0xff;
		}
		cmd_completed = true;
		if (!status && retval != ICE_AQ_RC_OK)
			status = ICE_ERR_AQ_ERROR;
		cq->sq_last_status = (enum ice_aq_err)retval;
	}

	DNPRINTF(ICE_DBG_AQ_MSG, "ATQ: desc and buffer writeback:\n");
	ice_debug_cq(hw, cq, (void *)desc, buf, buf_size, true);

	/* save writeback AQ if requested */
	if (cd && cd->wb_desc)
		memcpy(cd->wb_desc, desc_on_ring, sizeof(*cd->wb_desc));

	/* update the error if time out occurred */
	if (!cmd_completed) {
		if (ICE_READ(hw, cq->rq.len) & cq->rq.len_crit_mask ||
		    ICE_READ(hw, cq->sq.len) & cq->sq.len_crit_mask) {
			DNPRINTF(ICE_DBG_AQ_MSG, "Critical FW error.\n");
			status = ICE_ERR_AQ_FW_CRITICAL;
		} else {
			DNPRINTF(ICE_DBG_AQ_MSG,
			    "Control Send Queue Writeback timeout.\n");
			status = ICE_ERR_AQ_TIMEOUT;
		}
	}

sq_send_command_error:
	return status;
}

/**
 * ice_sq_send_cmd - send command to a control queue
 * @hw: pointer to the HW struct
 * @cq: pointer to the specific Control queue
 * @desc: prefilled descriptor describing the command
 * @buf: buffer to use for indirect commands (or NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (or 0 for direct commands)
 * @cd: pointer to command details structure
 *
 * Main command for the transmit side of a control queue. It puts the command
 * on the queue, bumps the tail, waits for processing of the command, captures
 * command status and results, etc.
 */
enum ice_status
ice_sq_send_cmd(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		struct ice_aq_desc *desc, void *buf, uint16_t buf_size,
		struct ice_sq_cd *cd)
{
	enum ice_status status = ICE_SUCCESS;

	/* if reset is in progress return a soft error */
	if (hw->reset_ongoing)
		return ICE_ERR_RESET_ONGOING;
#if 0
	ice_acquire_lock(&cq->sq_lock);
#endif
	status = ice_sq_send_cmd_nolock(hw, cq, desc, buf, buf_size, cd);
#if 0
	ice_release_lock(&cq->sq_lock);
#endif
	return status;
}


/**
 * ice_sq_send_cmd_retry - send command to Control Queue (ATQ)
 * @hw: pointer to the HW struct
 * @cq: pointer to the specific Control queue
 * @desc: prefilled descriptor describing the command
 * @buf: buffer to use for indirect commands (or NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (or 0 for direct commands)
 * @cd: pointer to command details structure
 *
 * Retry sending the FW Admin Queue command, multiple times, to the FW Admin
 * Queue if the EBUSY AQ error is returned.
 */
enum ice_status
ice_sq_send_cmd_retry(struct ice_hw *hw, struct ice_ctl_q_info *cq,
    struct ice_aq_desc *desc, void *buf, uint16_t buf_size,
    struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc_cpy;
	enum ice_status status;
	bool is_cmd_for_retry;
	uint8_t *buf_cpy = NULL;
	uint8_t idx = 0;
	uint16_t opcode;

	opcode = le16toh(desc->opcode);
	is_cmd_for_retry = ice_should_retry_sq_send_cmd(opcode);
	memset(&desc_cpy, 0, sizeof(desc_cpy));

	if (is_cmd_for_retry) {
		if (buf) {
			buf_cpy = (uint8_t *)ice_malloc(hw, buf_size);
			if (!buf_cpy)
				return ICE_ERR_NO_MEMORY;
		}

		memcpy(&desc_cpy, desc, sizeof(desc_cpy));
	}

	do {
		status = ice_sq_send_cmd(hw, cq, desc, buf, buf_size, cd);

		if (!is_cmd_for_retry || status == ICE_SUCCESS ||
		    hw->adminq.sq_last_status != ICE_AQ_RC_EBUSY)
			break;

		if (buf_cpy)
			memcpy(buf, buf_cpy, buf_size);

		memcpy(desc, &desc_cpy, sizeof(desc_cpy));

		ice_msec_delay(ICE_SQ_SEND_DELAY_TIME_MS, false);

	} while (++idx < ICE_SQ_SEND_MAX_EXECUTE);

	if (buf_cpy)
		ice_free(hw, buf_cpy);

	return status;
}

/**
 * ice_aq_send_cmd - send FW Admin Queue command to FW Admin Queue
 * @hw: pointer to the HW struct
 * @desc: descriptor describing the command
 * @buf: buffer to use for indirect commands (NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (0 for direct commands)
 * @cd: pointer to command details structure
 *
 * Helper function to send FW Admin Queue commands to the FW Admin Queue.
 */
enum ice_status
ice_aq_send_cmd(struct ice_hw *hw, struct ice_aq_desc *desc, void *buf,
		uint16_t buf_size, struct ice_sq_cd *cd)
{
	return ice_sq_send_cmd_retry(hw, &hw->adminq, desc, buf, buf_size, cd);
}

/**
 * ice_write_byte - write a byte to a packed context structure
 * @src_ctx:  the context structure to read from
 * @dest_ctx: the context to be written to
 * @ce_info:  a description of the struct to be filled
 */
void
ice_write_byte(uint8_t *src_ctx, uint8_t *dest_ctx,
    const struct ice_ctx_ele *ce_info)
{
	uint8_t src_byte, dest_byte, mask;
	uint8_t *from, *dest;
	uint16_t shift_width;

	/* copy from the next struct field */
	from = src_ctx + ce_info->offset;

	/* prepare the bits and mask */
	shift_width = ce_info->lsb % 8;
	mask = (uint8_t)(BIT(ce_info->width) - 1);

	src_byte = *from;
	src_byte &= mask;

	/* shift to correct alignment */
	mask <<= shift_width;
	src_byte <<= shift_width;

	/* get the current bits from the target bit string */
	dest = dest_ctx + (ce_info->lsb / 8);

	memcpy(&dest_byte, dest, sizeof(dest_byte));

	dest_byte &= ~mask;	/* get the bits not changing */
	dest_byte |= src_byte;	/* add in the new bits */

	/* put it all back */
	memcpy(dest, &dest_byte, sizeof(dest_byte));
}

/**
 * ice_write_word - write a word to a packed context structure
 * @src_ctx:  the context structure to read from
 * @dest_ctx: the context to be written to
 * @ce_info:  a description of the struct to be filled
 */
void
ice_write_word(uint8_t *src_ctx, uint8_t *dest_ctx,
    const struct ice_ctx_ele *ce_info)
{
	uint16_t src_word, mask;
	uint16_t dest_word;
	uint8_t *from, *dest;
	uint16_t shift_width;

	/* copy from the next struct field */
	from = src_ctx + ce_info->offset;

	/* prepare the bits and mask */
	shift_width = ce_info->lsb % 8;
	mask = BIT(ce_info->width) - 1;

	/* don't swizzle the bits until after the mask because the mask bits
	 * will be in a different bit position on big endian machines
	 */
	src_word = *(uint16_t *)from;
	src_word &= mask;

	/* shift to correct alignment */
	mask <<= shift_width;
	src_word <<= shift_width;

	/* get the current bits from the target bit string */
	dest = dest_ctx + (ce_info->lsb / 8);

	memcpy(&dest_word, dest, sizeof(dest_word));

	dest_word &= ~(htole16(mask));	/* get the bits not changing */
	dest_word |= htole16(src_word);	/* add in the new bits */

	/* put it all back */
	memcpy(dest, &dest_word, sizeof(dest_word));
}

/**
 * ice_write_dword - write a dword to a packed context structure
 * @src_ctx:  the context structure to read from
 * @dest_ctx: the context to be written to
 * @ce_info:  a description of the struct to be filled
 */
void
ice_write_dword(uint8_t *src_ctx, uint8_t *dest_ctx,
    const struct ice_ctx_ele *ce_info)
{
	uint32_t src_dword, mask;
	uint32_t dest_dword;
	uint8_t *from, *dest;
	uint16_t shift_width;

	/* copy from the next struct field */
	from = src_ctx + ce_info->offset;

	/* prepare the bits and mask */
	shift_width = ce_info->lsb % 8;

	/* if the field width is exactly 32 on an x86 machine, then the shift
	 * operation will not work because the SHL instructions count is masked
	 * to 5 bits so the shift will do nothing
	 */
	if (ce_info->width < 32)
		mask = BIT(ce_info->width) - 1;
	else
		mask = (uint32_t)~0;

	/* don't swizzle the bits until after the mask because the mask bits
	 * will be in a different bit position on big endian machines
	 */
	src_dword = *(uint32_t *)from;
	src_dword &= mask;

	/* shift to correct alignment */
	mask <<= shift_width;
	src_dword <<= shift_width;

	/* get the current bits from the target bit string */
	dest = dest_ctx + (ce_info->lsb / 8);

	memcpy(&dest_dword, dest, sizeof(dest_dword));

	dest_dword &= ~(htole32(mask));	/* get the bits not changing */
	dest_dword |= htole32(src_dword);	/* add in the new bits */

	/* put it all back */
	memcpy(dest, &dest_dword, sizeof(dest_dword));
}

/**
 * ice_write_qword - write a qword to a packed context structure
 * @src_ctx:  the context structure to read from
 * @dest_ctx: the context to be written to
 * @ce_info:  a description of the struct to be filled
 */
void
ice_write_qword(uint8_t *src_ctx, uint8_t *dest_ctx,
    const struct ice_ctx_ele *ce_info)
{
	uint64_t src_qword, mask;
	uint64_t dest_qword;
	uint8_t *from, *dest;
	uint16_t shift_width;

	/* copy from the next struct field */
	from = src_ctx + ce_info->offset;

	/* prepare the bits and mask */
	shift_width = ce_info->lsb % 8;

	/* if the field width is exactly 64 on an x86 machine, then the shift
	 * operation will not work because the SHL instructions count is masked
	 * to 6 bits so the shift will do nothing
	 */
	if (ce_info->width < 64)
		mask = BIT_ULL(ce_info->width) - 1;
	else
		mask = (uint64_t)~0;

	/* don't swizzle the bits until after the mask because the mask bits
	 * will be in a different bit position on big endian machines
	 */
	src_qword = *(uint64_t *)from;
	src_qword &= mask;

	/* shift to correct alignment */
	mask <<= shift_width;
	src_qword <<= shift_width;

	/* get the current bits from the target bit string */
	dest = dest_ctx + (ce_info->lsb / 8);

	memcpy(&dest_qword, dest, sizeof(dest_qword) );

	dest_qword &= ~(htole64(mask));	/* get the bits not changing */
	dest_qword |= htole64(src_qword);	/* add in the new bits */

	/* put it all back */
	memcpy(dest, &dest_qword, sizeof(dest_qword));
}

/**
 * ice_set_ctx - set context bits in packed structure
 * @hw: pointer to the hardware structure
 * @src_ctx:  pointer to a generic non-packed context structure
 * @dest_ctx: pointer to memory for the packed structure
 * @ce_info:  a description of the structure to be transformed
 */
enum ice_status
ice_set_ctx(struct ice_hw *hw, uint8_t *src_ctx, uint8_t *dest_ctx,
    const struct ice_ctx_ele *ce_info)
{
	int f;

	for (f = 0; ce_info[f].width; f++) {
		/* We have to deal with each element of the FW response
		 * using the correct size so that we are correct regardless
		 * of the endianness of the machine.
		 */
		if (ce_info[f].width > (ce_info[f].size_of * 8)) {
			DNPRINTF(ICE_DBG_QCTX, "%s: Field %d width of %d bits "
			    "larger than size of %d byte(s); skipping write\n",
			    __func__, f, ce_info[f].width, ce_info[f].size_of);
			continue;
		}
		switch (ce_info[f].size_of) {
		case sizeof(uint8_t):
			ice_write_byte(src_ctx, dest_ctx, &ce_info[f]);
			break;
		case sizeof(uint16_t):
			ice_write_word(src_ctx, dest_ctx, &ce_info[f]);
			break;
		case sizeof(uint32_t):
			ice_write_dword(src_ctx, dest_ctx, &ce_info[f]);
			break;
		case sizeof(uint64_t):
			ice_write_qword(src_ctx, dest_ctx, &ce_info[f]);
			break;
		default:
			return ICE_ERR_INVAL_SIZE;
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_fill_dflt_direct_cmd_desc - AQ descriptor helper function
 * @desc: pointer to the temp descriptor (non DMA mem)
 * @opcode: the opcode can be used to decide which flags to turn off or on
 *
 * Fill the desc with default values
 */
void
ice_fill_dflt_direct_cmd_desc(struct ice_aq_desc *desc, uint16_t opcode)
{
	/* zero out the desc */
	memset(desc, 0, sizeof(*desc));
	desc->opcode = htole16(opcode);
	desc->flags = htole16(ICE_AQ_FLAG_SI);
}

/**
 * ice_aq_get_fw_ver
 * @hw: pointer to the HW struct
 * @cd: pointer to command details structure or NULL
 *
 * Get the firmware version (0x0001) from the admin queue commands
 */
enum ice_status
ice_aq_get_fw_ver(struct ice_hw *hw, struct ice_sq_cd *cd)
{
	struct ice_aqc_get_ver *resp;
	struct ice_aq_desc desc;
	enum ice_status status;

	resp = &desc.params.get_ver;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_ver);

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	if (!status) {
		hw->fw_branch = resp->fw_branch;
		hw->fw_maj_ver = resp->fw_major;
		hw->fw_min_ver = resp->fw_minor;
		hw->fw_patch = resp->fw_patch;
		hw->fw_build = le32toh(resp->fw_build);
		hw->api_branch = resp->api_branch;
		hw->api_maj_ver = resp->api_major;
		hw->api_min_ver = resp->api_minor;
		hw->api_patch = resp->api_patch;
	}

	return status;
}

/*
 * ice_aq_q_shutdown
 * @hw: pointer to the HW struct
 * @unloading: is the driver unloading itself
 *
 * Tell the Firmware that we're shutting down the AdminQ and whether
 * or not the driver is unloading as well (0x0003).
 */
enum ice_status
ice_aq_q_shutdown(struct ice_hw *hw, bool unloading)
{
	struct ice_aqc_q_shutdown *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.q_shutdown;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_q_shutdown);

	if (unloading)
		cmd->driver_unloading = ICE_AQC_DRIVER_UNLOADING;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_aq_req_res
 * @hw: pointer to the HW struct
 * @res: resource ID
 * @access: access type
 * @sdp_number: resource number
 * @timeout: the maximum time in ms that the driver may hold the resource
 * @cd: pointer to command details structure or NULL
 *
 * Requests common resource using the admin queue commands (0x0008).
 * When attempting to acquire the Global Config Lock, the driver can
 * learn of three states:
 *  1) ICE_SUCCESS -        acquired lock, and can perform download package
 *  2) ICE_ERR_AQ_ERROR -   did not get lock, driver should fail to load
 *  3) ICE_ERR_AQ_NO_WORK - did not get lock, but another driver has
 *                          successfully downloaded the package; the driver does
 *                          not have to download the package and can continue
 *                          loading
 *
 * Note that if the caller is in an acquire lock, perform action, release lock
 * phase of operation, it is possible that the FW may detect a timeout and issue
 * a CORER. In this case, the driver will receive a CORER interrupt and will
 * have to determine its cause. The calling thread that is handling this flow
 * will likely get an error propagated back to it indicating the Download
 * Package, Update Package or the Release Resource AQ commands timed out.
 */
enum ice_status
ice_aq_req_res(struct ice_hw *hw, enum ice_aq_res_ids res,
	       enum ice_aq_res_access_type access, uint8_t sdp_number,
	       uint32_t *timeout, struct ice_sq_cd *cd)
{
	struct ice_aqc_req_res *cmd_resp;
	struct ice_aq_desc desc;
	enum ice_status status;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	cmd_resp = &desc.params.res_owner;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_req_res);

	cmd_resp->res_id = htole16(res);
	cmd_resp->access_type = htole16(access);
	cmd_resp->res_number = htole32(sdp_number);
	cmd_resp->timeout = htole32(*timeout);
	*timeout = 0;

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	/* The completion specifies the maximum time in ms that the driver
	 * may hold the resource in the Timeout field.
	 */

	/* Global config lock response utilizes an additional status field.
	 *
	 * If the Global config lock resource is held by some other driver, the
	 * command completes with ICE_AQ_RES_GLBL_IN_PROG in the status field
	 * and the timeout field indicates the maximum time the current owner
	 * of the resource has to free it.
	 */
	if (res == ICE_GLOBAL_CFG_LOCK_RES_ID) {
		if (le16toh(cmd_resp->status) == ICE_AQ_RES_GLBL_SUCCESS) {
			*timeout = le32toh(cmd_resp->timeout);
			return ICE_SUCCESS;
		} else if (le16toh(cmd_resp->status) ==
			   ICE_AQ_RES_GLBL_IN_PROG) {
			*timeout = le32toh(cmd_resp->timeout);
			return ICE_ERR_AQ_ERROR;
		} else if (le16toh(cmd_resp->status) ==
			   ICE_AQ_RES_GLBL_DONE) {
			return ICE_ERR_AQ_NO_WORK;
		}

		/* invalid FW response, force a timeout immediately */
		*timeout = 0;
		return ICE_ERR_AQ_ERROR;
	}

	/* If the resource is held by some other driver, the command completes
	 * with a busy return value and the timeout field indicates the maximum
	 * time the current owner of the resource has to free it.
	 */
	if (!status || hw->adminq.sq_last_status == ICE_AQ_RC_EBUSY)
		*timeout = le32toh(cmd_resp->timeout);

	return status;
}

/**
 * ice_aq_release_res
 * @hw: pointer to the HW struct
 * @res: resource ID
 * @sdp_number: resource number
 * @cd: pointer to command details structure or NULL
 *
 * release common resource using the admin queue commands (0x0009)
 */
enum ice_status
ice_aq_release_res(struct ice_hw *hw, enum ice_aq_res_ids res,
    uint8_t sdp_number, struct ice_sq_cd *cd)
{
	struct ice_aqc_req_res *cmd;
	struct ice_aq_desc desc;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	cmd = &desc.params.res_owner;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_release_res);

	cmd->res_id = htole16(res);
	cmd->res_number = htole32(sdp_number);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_acquire_res
 * @hw: pointer to the HW structure
 * @res: resource ID
 * @access: access type (read or write)
 * @timeout: timeout in milliseconds
 *
 * This function will attempt to acquire the ownership of a resource.
 */
enum ice_status
ice_acquire_res(struct ice_hw *hw, enum ice_aq_res_ids res,
    enum ice_aq_res_access_type access, uint32_t timeout)
{
#define ICE_RES_POLLING_DELAY_MS	10
	uint32_t delay = ICE_RES_POLLING_DELAY_MS;
	uint32_t time_left = timeout;
	enum ice_status status;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	status = ice_aq_req_res(hw, res, access, 0, &time_left, NULL);

	/* A return code of ICE_ERR_AQ_NO_WORK means that another driver has
	 * previously acquired the resource and performed any necessary updates;
	 * in this case the caller does not obtain the resource and has no
	 * further work to do.
	 */
	if (status == ICE_ERR_AQ_NO_WORK)
		goto ice_acquire_res_exit;

	if (status)
		DNPRINTF(ICE_DBG_RES, "resource %d acquire type %d failed.\n",
		    res, access);

	/* If necessary, poll until the current lock owner timeouts */
	timeout = time_left;
	while (status && timeout && time_left) {
		ice_msec_delay(delay, true);
		timeout = (timeout > delay) ? timeout - delay : 0;
		status = ice_aq_req_res(hw, res, access, 0, &time_left, NULL);

		if (status == ICE_ERR_AQ_NO_WORK)
			/* lock free, but no work to do */
			break;

		if (!status)
			/* lock acquired */
			break;
	}
	if (status && status != ICE_ERR_AQ_NO_WORK)
		DNPRINTF(ICE_DBG_RES, "resource acquire timed out.\n");

ice_acquire_res_exit:
	if (status == ICE_ERR_AQ_NO_WORK) {
		if (access == ICE_RES_WRITE)
			DNPRINTF(ICE_DBG_RES,
			    "resource indicates no work to do.\n");
		else
			DNPRINTF(ICE_DBG_RES,
			    "Warning: ICE_ERR_AQ_NO_WORK not expected\n");
	}
	return status;
}

/**
 * ice_release_res
 * @hw: pointer to the HW structure
 * @res: resource ID
 *
 * This function will release a resource using the proper Admin Command.
 */
void ice_release_res(struct ice_hw *hw, enum ice_aq_res_ids res)
{
	enum ice_status status;
	uint32_t total_delay = 0;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	status = ice_aq_release_res(hw, res, 0, NULL);

	/* there are some rare cases when trying to release the resource
	 * results in an admin queue timeout, so handle them correctly
	 */
	while ((status == ICE_ERR_AQ_TIMEOUT) &&
	       (total_delay < hw->adminq.sq_cmd_timeout)) {
		ice_msec_delay(1, true);
		status = ice_aq_release_res(hw, res, 0, NULL);
		total_delay++;
	}
}

/*
 * ice_aq_ver_check - Check the reported AQ API version
 * Checks if the driver should load on a given AQ API version.
 * Return: 'true' iff the driver should attempt to load. 'false' otherwise.
 */
bool
ice_aq_ver_check(struct ice_hw *hw)
{
	struct ice_softc *sc = hw->hw_sc;

	if (hw->api_maj_ver > EXP_FW_API_VER_MAJOR) {
		/* Major API version is newer than expected, don't load */
		printf("%s: unsupported firmware API major version %u; "
		    "expected version is %u\n", sc->sc_dev.dv_xname,
		    hw->api_maj_ver, EXP_FW_API_VER_MAJOR);
		return false;
	}

	return true;
}

/*
 * ice_shutdown_sq - shutdown the transmit side of a control queue
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * The main shutdown routine for the Control Transmit Queue
 */
enum ice_status
ice_shutdown_sq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status ret_code = ICE_SUCCESS;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);
#if 0
	ice_acquire_lock(&cq->sq_lock);
#endif
	if (!cq->sq.count) {
		ret_code = ICE_ERR_NOT_READY;
		goto shutdown_sq_out;
	}

	/* Stop processing of the control queue */
	ICE_WRITE(hw, cq->sq.head, 0);
	ICE_WRITE(hw, cq->sq.tail, 0);
	ICE_WRITE(hw, cq->sq.len, 0);
	ICE_WRITE(hw, cq->sq.bal, 0);
	ICE_WRITE(hw, cq->sq.bah, 0);

	cq->sq.count = 0;	/* to indicate uninitialized queue */

	/* free ring buffers and the ring itself */
	ICE_FREE_CQ_BUFS(hw, cq, sq);
	ice_free_cq_ring(hw, &cq->sq);

shutdown_sq_out:
#if 0
	ice_release_lock(&cq->sq_lock);
#endif
	return ret_code;
}

/*
 * ice_shutdown_rq - shutdown Control ARQ
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * The main shutdown routine for the Control Receive Queue
 */
enum ice_status
ice_shutdown_rq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status ret_code = ICE_SUCCESS;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);
#if 0
	ice_acquire_lock(&cq->rq_lock);
#endif
	if (!cq->rq.count) {
		ret_code = ICE_ERR_NOT_READY;
		goto shutdown_rq_out;
	}

	/* Stop Control Queue processing */
	ICE_WRITE(hw, cq->rq.head, 0);
	ICE_WRITE(hw, cq->rq.tail, 0);
	ICE_WRITE(hw, cq->rq.len, 0);
	ICE_WRITE(hw, cq->rq.bal, 0);
	ICE_WRITE(hw, cq->rq.bah, 0);

	/* set rq.count to 0 to indicate uninitialized queue */
	cq->rq.count = 0;

	/* free ring buffers and the ring itself */
	ICE_FREE_CQ_BUFS(hw, cq, rq);
	ice_free_cq_ring(hw, &cq->rq);

shutdown_rq_out:
#if 0
	ice_release_lock(&cq->rq_lock);
#endif
	return ret_code;
}

/*
 * ice_init_check_adminq - Check version for Admin Queue to know if its alive
 */
enum ice_status
ice_init_check_adminq(struct ice_hw *hw)
{
	struct ice_ctl_q_info *cq = &hw->adminq;
	enum ice_status status;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	status = ice_aq_get_fw_ver(hw, NULL);
	if (status)
		goto init_ctrlq_free_rq;

	if (!ice_aq_ver_check(hw)) {
		status = ICE_ERR_FW_API_VER;
		goto init_ctrlq_free_rq;
	}

	return ICE_SUCCESS;

init_ctrlq_free_rq:
	ice_shutdown_rq(hw, cq);
	ice_shutdown_sq(hw, cq);
	return status;
}

/*
 * ice_init_ctrlq - main initialization routine for any control Queue
 * @hw: pointer to the hardware structure
 * @q_type: specific Control queue type
 *
 * Prior to calling this function, the driver *MUST* set the following fields
 * in the cq->structure:
 *     - cq->num_sq_entries
 *     - cq->num_rq_entries
 *     - cq->rq_buf_size
 *     - cq->sq_buf_size
 *
 * NOTE: this function does not initialize the controlq locks
 */
enum ice_status
ice_init_ctrlq(struct ice_hw *hw, enum ice_ctl_q q_type)
{
	struct ice_ctl_q_info *cq;
	enum ice_status ret_code;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	switch (q_type) {
	case ICE_CTL_Q_ADMIN:
		ice_adminq_init_regs(hw);
		cq = &hw->adminq;
		break;
	case ICE_CTL_Q_MAILBOX:
		ice_mailbox_init_regs(hw);
		cq = &hw->mailboxq;
		break;
	default:
		return ICE_ERR_PARAM;
	}
	cq->qtype = q_type;

	/* verify input for valid configuration */
	if (!cq->num_rq_entries || !cq->num_sq_entries ||
	    !cq->rq_buf_size || !cq->sq_buf_size) {
		return ICE_ERR_CFG;
	}

	/* setup SQ command write back timeout */
	cq->sq_cmd_timeout = ICE_CTL_Q_SQ_CMD_TIMEOUT;

	/* allocate the ATQ */
	ret_code = ice_init_sq(hw, cq);
	if (ret_code)
		return ret_code;

	/* allocate the ARQ */
	ret_code = ice_init_rq(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_sq;

	/* success! */
	return ICE_SUCCESS;

init_ctrlq_free_sq:
	ice_shutdown_sq(hw, cq);
	return ret_code;
}

/*
 * ice_shutdown_ctrlq - shutdown routine for any control queue
 * @hw: pointer to the hardware structure
 * @q_type: specific Control queue type
 * @unloading: is the driver unloading itself
 *
 * NOTE: this function does not destroy the control queue locks.
 */
void
ice_shutdown_ctrlq(struct ice_hw *hw, enum ice_ctl_q q_type,
		   bool unloading)
{
	struct ice_ctl_q_info *cq;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	switch (q_type) {
	case ICE_CTL_Q_ADMIN:
		cq = &hw->adminq;
		if (ice_check_sq_alive(hw, cq))
			ice_aq_q_shutdown(hw, unloading);
		break;
	case ICE_CTL_Q_MAILBOX:
		cq = &hw->mailboxq;
		break;
	default:
		return;
	}

	ice_shutdown_sq(hw, cq);
	ice_shutdown_rq(hw, cq);
}

/*
 * ice_shutdown_all_ctrlq - shutdown routine for all control queues
 * @hw: pointer to the hardware structure
 * @unloading: is the driver unloading itself
 *
 * NOTE: this function does not destroy the control queue locks. The driver
 * may call this at runtime to shutdown and later restart control queues, such
 * as in response to a reset event.
 */
void
ice_shutdown_all_ctrlq(struct ice_hw *hw, bool unloading)
{
	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);
	/* Shutdown FW admin queue */
	ice_shutdown_ctrlq(hw, ICE_CTL_Q_ADMIN, unloading);
	/* Shutdown PF-VF Mailbox */
	ice_shutdown_ctrlq(hw, ICE_CTL_Q_MAILBOX, unloading);
}

/**
 * ice_init_all_ctrlq - main initialization routine for all control queues
 * @hw: pointer to the hardware structure
 *
 * Prior to calling this function, the driver MUST* set the following fields
 * in the cq->structure for all control queues:
 *     - cq->num_sq_entries
 *     - cq->num_rq_entries
 *     - cq->rq_buf_size
 *     - cq->sq_buf_size
 *
 * NOTE: this function does not initialize the controlq locks.
 */
enum ice_status
ice_init_all_ctrlq(struct ice_hw *hw)
{
	enum ice_status status;
	uint32_t retry = 0;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	/* Init FW admin queue */
	do {
		status = ice_init_ctrlq(hw, ICE_CTL_Q_ADMIN);
		if (status)
			return status;

		status = ice_init_check_adminq(hw);
		if (status != ICE_ERR_AQ_FW_CRITICAL)
			break;

		DNPRINTF(ICE_DBG_AQ_MSG, "Retry Admin Queue init due to FW critical error\n");
		ice_shutdown_ctrlq(hw, ICE_CTL_Q_ADMIN, true);
		ice_msec_delay(ICE_CTL_Q_ADMIN_INIT_MSEC, true);
	} while (retry++ < ICE_CTL_Q_ADMIN_INIT_TIMEOUT);

	if (status)
		return status;

	/* Init Mailbox queue */
	return ice_init_ctrlq(hw, ICE_CTL_Q_MAILBOX);
}

/**
 * ice_init_ctrlq_locks - Initialize locks for a control queue
 * @cq: pointer to the control queue
 *
 * Initializes the send and receive queue locks for a given control queue.
 */
void
ice_init_ctrlq_locks(struct ice_ctl_q_info *cq)
{
	ice_init_lock(&cq->sq_lock);
	ice_init_lock(&cq->rq_lock);
}

/**
 * ice_create_all_ctrlq - main initialization routine for all control queues
 * @hw: pointer to the hardware structure
 *
 * Prior to calling this function, the driver *MUST* set the following fields
 * in the cq->structure for all control queues:
 *     - cq->num_sq_entries
 *     - cq->num_rq_entries
 *     - cq->rq_buf_size
 *     - cq->sq_buf_size
 *
 * This function creates all the control queue locks and then calls
 * ice_init_all_ctrlq. It should be called once during driver load. If the
 * driver needs to re-initialize control queues at run time it should call
 * ice_init_all_ctrlq instead.
 */
enum ice_status
ice_create_all_ctrlq(struct ice_hw *hw)
{
	ice_init_ctrlq_locks(&hw->adminq);
	ice_init_ctrlq_locks(&hw->mailboxq);

	return ice_init_all_ctrlq(hw);
}

/*
 * ice_destroy_all_ctrlq - exit routine for all control queues
 *
 * This function shuts down all the control queues and then destroys the
 * control queue locks. It should be called once during driver unload. The
 * driver should call ice_shutdown_all_ctrlq if it needs to shut down and
 * reinitialize control queues, such as in response to a reset event.
 */
void
ice_destroy_all_ctrlq(struct ice_hw *hw)
{
	/* shut down all the control queues first */
	ice_shutdown_all_ctrlq(hw, true);

	ice_destroy_ctrlq_locks(&hw->adminq);
	ice_destroy_ctrlq_locks(&hw->mailboxq);
}

/**
 * ice_aq_fwlog_get - Get the current firmware logging configuration (0xFF32)
 * @hw: pointer to the HW structure
 * @cfg: firmware logging configuration to populate
 */
enum ice_status
ice_aq_fwlog_get(struct ice_hw *hw, struct ice_fwlog_cfg *cfg)
{
	struct ice_aqc_fw_log_cfg_resp *fw_modules;
	struct ice_aqc_fw_log *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;
	uint16_t i, module_id_cnt;
	void *buf;

	memset(cfg, 0, sizeof(*cfg));

	buf = ice_calloc(hw, 1, ICE_AQ_MAX_BUF_LEN);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_fw_logs_query);
	cmd = &desc.params.fw_log;

	cmd->cmd_flags = ICE_AQC_FW_LOG_AQ_QUERY;

	status = ice_aq_send_cmd(hw, &desc, buf, ICE_AQ_MAX_BUF_LEN, NULL);
	if (status) {
		DNPRINTF(ICE_DBG_FW_LOG,
		    "Failed to get FW log configuration\n");
		goto status_out;
	}

	module_id_cnt = le16toh(cmd->ops.cfg.mdl_cnt);
	if (module_id_cnt < ICE_AQC_FW_LOG_ID_MAX) {
		DNPRINTF(ICE_DBG_FW_LOG, "FW returned less than the expected "
		    "number of FW log module IDs\n");
	} else {
		if (module_id_cnt > ICE_AQC_FW_LOG_ID_MAX)
			DNPRINTF(ICE_DBG_FW_LOG, "FW returned more than "
			    "expected number of FW log module IDs, setting "
			    "module_id_cnt to software expected max %u\n",
			    ICE_AQC_FW_LOG_ID_MAX);
		module_id_cnt = ICE_AQC_FW_LOG_ID_MAX;
	}

	cfg->log_resolution = le16toh(cmd->ops.cfg.log_resolution);
	if (cmd->cmd_flags & ICE_AQC_FW_LOG_CONF_AQ_EN)
		cfg->options |= ICE_FWLOG_OPTION_ARQ_ENA;
	if (cmd->cmd_flags & ICE_AQC_FW_LOG_CONF_UART_EN)
		cfg->options |= ICE_FWLOG_OPTION_UART_ENA;
	if (cmd->cmd_flags & ICE_AQC_FW_LOG_QUERY_REGISTERED)
		cfg->options |= ICE_FWLOG_OPTION_IS_REGISTERED;

	fw_modules = (struct ice_aqc_fw_log_cfg_resp *)buf;

	for (i = 0; i < module_id_cnt; i++) {
		struct ice_aqc_fw_log_cfg_resp *fw_module = &fw_modules[i];

		cfg->module_entries[i].module_id =
			le16toh(fw_module->module_identifier);
		cfg->module_entries[i].log_level = fw_module->log_level;
	}

status_out:
	ice_free(hw, buf);
	return status;
}

/**
 * ice_fwlog_set_support_ena - Set if FW logging is supported by FW
 * @hw: pointer to the HW struct
 *
 * If FW returns success to the ice_aq_fwlog_get call then it supports FW
 * logging, else it doesn't. Set the fwlog_support_ena flag accordingly.
 *
 * This function is only meant to be called during driver init to determine if
 * the FW support FW logging.
 */
void
ice_fwlog_set_support_ena(struct ice_hw *hw)
{
	struct ice_fwlog_cfg *cfg;
	enum ice_status status;

	hw->fwlog_support_ena = false;

	cfg = (struct ice_fwlog_cfg *)ice_calloc(hw, 1, sizeof(*cfg));
	if (!cfg)
		return;

	/* don't call ice_fwlog_get() because that would overwrite the cached
	 * configuration from the call to ice_fwlog_init(), which is expected to
	 * be called prior to this function
	 */
	status = ice_aq_fwlog_get(hw, cfg);
	if (status)
		DNPRINTF(ICE_DBG_FW_LOG, "ice_fwlog_get failed, FW logging "
		    "is not supported on this version of FW, status %d\n",
		    status);
	else
		hw->fwlog_support_ena = true;

	ice_free(hw, cfg);
}

/**
 * ice_aq_fwlog_set - Set FW logging configuration AQ command (0xFF30)
 * @hw: pointer to the HW structure
 * @entries: entries to configure
 * @num_entries: number of @entries
 * @options: options from ice_fwlog_cfg->options structure
 * @log_resolution: logging resolution
 */
enum ice_status
ice_aq_fwlog_set(struct ice_hw *hw, struct ice_fwlog_module_entry *entries,
		 uint16_t num_entries, uint16_t options,
		 uint16_t log_resolution)
{
	struct ice_aqc_fw_log_cfg_resp *fw_modules;
	struct ice_aqc_fw_log *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;
	uint16_t i;

	fw_modules = (struct ice_aqc_fw_log_cfg_resp *)
		ice_calloc(hw, num_entries, sizeof(*fw_modules));
	if (!fw_modules)
		return ICE_ERR_NO_MEMORY;

	for (i = 0; i < num_entries; i++) {
		fw_modules[i].module_identifier = htole16(entries[i].module_id);
		fw_modules[i].log_level = entries[i].log_level;
	}

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_fw_logs_config);
	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	cmd = &desc.params.fw_log;

	cmd->cmd_flags = ICE_AQC_FW_LOG_CONF_SET_VALID;
	cmd->ops.cfg.log_resolution = htole16(log_resolution);
	cmd->ops.cfg.mdl_cnt = htole16(num_entries);

	if (options & ICE_FWLOG_OPTION_ARQ_ENA)
		cmd->cmd_flags |= ICE_AQC_FW_LOG_CONF_AQ_EN;
	if (options & ICE_FWLOG_OPTION_UART_ENA)
		cmd->cmd_flags |= ICE_AQC_FW_LOG_CONF_UART_EN;

	status = ice_aq_send_cmd(hw, &desc, fw_modules,
				 sizeof(*fw_modules) * num_entries,
				 NULL);

	ice_free(hw, fw_modules);

	return status;
}

/**
 * ice_fwlog_supported - Cached for whether FW supports FW logging or not
 * @hw: pointer to the HW structure
 *
 * This will always return false if called before ice_init_hw(), so it must be
 * called after ice_init_hw().
 */
bool
ice_fwlog_supported(struct ice_hw *hw)
{
	return hw->fwlog_support_ena;
}

/**
 * ice_fwlog_valid_module_entries - validate all the module entry IDs and
 * log levels
 */
bool
ice_fwlog_valid_module_entries(struct ice_hw *hw,
    struct ice_fwlog_module_entry *entries, uint16_t num_entries)
{
	uint16_t i;

	if (!entries) {
		DNPRINTF(ICE_DBG_FW_LOG, "Null ice_fwlog_module_entry array\n");
		return false;
	}

	if (!num_entries) {
		DNPRINTF(ICE_DBG_FW_LOG, "num_entries must be non-zero\n");
		return false;
	}

	for (i = 0; i < num_entries; i++) {
		struct ice_fwlog_module_entry *entry = &entries[i];

		if (entry->module_id >= ICE_AQC_FW_LOG_ID_MAX) {
			DNPRINTF(ICE_DBG_FW_LOG,
			    "Invalid module_id %u, max valid module_id is %u\n",
			    entry->module_id, ICE_AQC_FW_LOG_ID_MAX - 1);
			return false;
		}

		if (entry->log_level >= ICE_FWLOG_LEVEL_INVALID) {
			DNPRINTF(ICE_DBG_FW_LOG,
			    "Invalid log_level %u, max valid log_level is %u\n",
			    entry->log_level, ICE_AQC_FW_LOG_ID_MAX - 1);
			return false;
		}
	}

	return true;
}

/**
 * ice_fwlog_valid_cfg - validate entire configuration
 * @hw: pointer to the HW structure
 * @cfg: config to validate
 */
bool
ice_fwlog_valid_cfg(struct ice_hw *hw, struct ice_fwlog_cfg *cfg)
{
	if (!cfg) {
		DNPRINTF(ICE_DBG_FW_LOG, "Null ice_fwlog_cfg\n");
		return false;
	}

	if (cfg->log_resolution < ICE_AQC_FW_LOG_MIN_RESOLUTION ||
	    cfg->log_resolution > ICE_AQC_FW_LOG_MAX_RESOLUTION) {
		DNPRINTF(ICE_DBG_FW_LOG, "Unsupported log_resolution %u, "
		    "must be between %u and %u\n",
		    cfg->log_resolution, ICE_AQC_FW_LOG_MIN_RESOLUTION,
		    ICE_AQC_FW_LOG_MAX_RESOLUTION);
		return false;
	}

	if (!ice_fwlog_valid_module_entries(hw, cfg->module_entries,
				  ICE_AQC_FW_LOG_ID_MAX))
		return false;

	return true;
}

/**
 * ice_fwlog_set - Set the firmware logging settings
 * @hw: pointer to the HW structure
 * @cfg: config used to set firmware logging
 *
 * This function should be called whenever the driver needs to set the firmware
 * logging configuration. It can be called on initialization, reset, or during
 * runtime.
 *
 * If the PF wishes to receive FW logging then it must register via
 * ice_fwlog_register. Note, that ice_fwlog_register does not need to be called
 * for init.
 */
enum ice_status
ice_fwlog_set(struct ice_hw *hw, struct ice_fwlog_cfg *cfg)
{
	enum ice_status status;

	if (!ice_fwlog_supported(hw))
		return ICE_ERR_NOT_SUPPORTED;

	if (!ice_fwlog_valid_cfg(hw, cfg))
		return ICE_ERR_PARAM;

	status = ice_aq_fwlog_set(hw, cfg->module_entries,
				  ICE_AQC_FW_LOG_ID_MAX, cfg->options,
				  cfg->log_resolution);
	if (!status)
		hw->fwlog_cfg = *cfg;

	return status;
}

/**
 * ice_aq_fwlog_register - Register PF for firmware logging events (0xFF31)
 * @hw: pointer to the HW structure
 * @reg: true to register and false to unregister
 */
enum ice_status
ice_aq_fwlog_register(struct ice_hw *hw, bool reg)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_fw_logs_register);

	if (reg)
		desc.params.fw_log.cmd_flags = ICE_AQC_FW_LOG_AQ_REGISTER;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_fwlog_register - Register the PF for firmware logging
 * @hw: pointer to the HW structure
 *
 * After this call the PF will start to receive firmware logging based on the
 * configuration set in ice_fwlog_set.
 */
enum ice_status
ice_fwlog_register(struct ice_hw *hw)
{
	enum ice_status status;

	if (!ice_fwlog_supported(hw))
		return ICE_ERR_NOT_SUPPORTED;

	status = ice_aq_fwlog_register(hw, true);
	if (status)
		DNPRINTF(ICE_DBG_FW_LOG, "Failed to register for firmware "
		    "logging events over ARQ\n");
	else
		hw->fwlog_cfg.options |= ICE_FWLOG_OPTION_IS_REGISTERED;

	return status;
}

/**
 * ice_fwlog_unregister - Unregister the PF from firmware logging
 * @hw: pointer to the HW structure
 */
enum ice_status
ice_fwlog_unregister(struct ice_hw *hw)
{
	enum ice_status status;

	if (!ice_fwlog_supported(hw))
		return ICE_ERR_NOT_SUPPORTED;

	status = ice_aq_fwlog_register(hw, false);
	if (status)
		DNPRINTF(ICE_DBG_FW_LOG, "Failed to unregister from "
		    "firmware logging events over ARQ\n");
	else
		hw->fwlog_cfg.options &= ~ICE_FWLOG_OPTION_IS_REGISTERED;

	return status;
}

/**
 * ice_acquire_nvm - Generic request for acquiring the NVM ownership
 * @hw: pointer to the HW structure
 * @access: NVM access type (read or write)
 *
 * This function will request NVM ownership.
 */
enum ice_status
ice_acquire_nvm(struct ice_hw *hw, enum ice_aq_res_access_type access)
{
	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);
	if (hw->flash.blank_nvm_mode)
		return ICE_SUCCESS;

	return ice_acquire_res(hw, ICE_NVM_RES_ID, access, ICE_NVM_TIMEOUT);
}

/**
 * ice_release_nvm - Generic request for releasing the NVM ownership
 * @hw: pointer to the HW structure
 *
 * This function will release NVM ownership.
 */
void
ice_release_nvm(struct ice_hw *hw)
{
	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	if (hw->flash.blank_nvm_mode)
		return;

	ice_release_res(hw, ICE_NVM_RES_ID);
}

/**
 * ice_get_flash_bank_offset - Get offset into requested flash bank
 * @hw: pointer to the HW structure
 * @bank: whether to read from the active or inactive flash bank
 * @module: the module to read from
 *
 * Based on the module, lookup the module offset from the beginning of the
 * flash.
 *
 * Returns the flash offset. Note that a value of zero is invalid and must be
 * treated as an error.
 */
uint32_t ice_get_flash_bank_offset(struct ice_hw *hw,
    enum ice_bank_select bank, uint16_t module)
{
	struct ice_bank_info *banks = &hw->flash.banks;
	enum ice_flash_bank active_bank;
	bool second_bank_active;
	uint32_t offset, size;

	switch (module) {
	case ICE_SR_1ST_NVM_BANK_PTR:
		offset = banks->nvm_ptr;
		size = banks->nvm_size;
		active_bank = banks->nvm_bank;
		break;
	case ICE_SR_1ST_OROM_BANK_PTR:
		offset = banks->orom_ptr;
		size = banks->orom_size;
		active_bank = banks->orom_bank;
		break;
	case ICE_SR_NETLIST_BANK_PTR:
		offset = banks->netlist_ptr;
		size = banks->netlist_size;
		active_bank = banks->netlist_bank;
		break;
	default:
		DNPRINTF(ICE_DBG_NVM,
		    "Unexpected value for flash module: 0x%04x\n", module);
		return 0;
	}

	switch (active_bank) {
	case ICE_1ST_FLASH_BANK:
		second_bank_active = false;
		break;
	case ICE_2ND_FLASH_BANK:
		second_bank_active = true;
		break;
	default:
		DNPRINTF(ICE_DBG_NVM,
		    "Unexpected value for active flash bank: %u\n",
		    active_bank);
		return 0;
	}

	/* The second flash bank is stored immediately following the first
	 * bank. Based on whether the 1st or 2nd bank is active, and whether
	 * we want the active or inactive bank, calculate the desired offset.
	 */
	switch (bank) {
	case ICE_ACTIVE_FLASH_BANK:
		return offset + (second_bank_active ? size : 0);
	case ICE_INACTIVE_FLASH_BANK:
		return offset + (second_bank_active ? 0 : size);
	}

	DNPRINTF(ICE_DBG_NVM,
	    "Unexpected value for flash bank selection: %u\n", bank);

	return 0;
}

/**
 * ice_aq_read_nvm
 * @hw: pointer to the HW struct
 * @module_typeid: module pointer location in words from the NVM beginning
 * @offset: byte offset from the module beginning
 * @length: length of the section to be read (in bytes from the offset)
 * @data: command buffer (size [bytes] = length)
 * @last_command: tells if this is the last command in a series
 * @read_shadow_ram: tell if this is a shadow RAM read
 * @cd: pointer to command details structure or NULL
 *
 * Read the NVM using the admin queue commands (0x0701)
 */
enum ice_status
ice_aq_read_nvm(struct ice_hw *hw, uint16_t module_typeid, uint32_t offset,
    uint16_t length, void *data, bool last_command, bool read_shadow_ram,
    struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	struct ice_aqc_nvm *cmd;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	cmd = &desc.params.nvm;

	if (offset > ICE_AQC_NVM_MAX_OFFSET)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_nvm_read);

	if (!read_shadow_ram && module_typeid == ICE_AQC_NVM_START_POINT)
		cmd->cmd_flags |= ICE_AQC_NVM_FLASH_ONLY;

	/* If this is the last command in a series, set the proper flag. */
	if (last_command)
		cmd->cmd_flags |= ICE_AQC_NVM_LAST_CMD;
	cmd->module_typeid = htole16(module_typeid);
	cmd->offset_low = htole16(offset & 0xFFFF);
	cmd->offset_high = (offset >> 16) & 0xFF;
	cmd->length = htole16(length);

	return ice_aq_send_cmd(hw, &desc, data, length, cd);
}

/**
 * ice_read_flat_nvm - Read portion of NVM by flat offset
 * @hw: pointer to the HW struct
 * @offset: offset from beginning of NVM
 * @length: (in) number of bytes to read; (out) number of bytes actually read
 * @data: buffer to return data in (sized to fit the specified length)
 * @read_shadow_ram: if true, read from shadow RAM instead of NVM
 *
 * Reads a portion of the NVM, as a flat memory space. This function correctly
 * breaks read requests across Shadow RAM sectors and ensures that no single
 * read request exceeds the maximum 4KB read for a single AdminQ command.
 *
 * Returns a status code on failure. Note that the data pointer may be
 * partially updated if some reads succeed before a failure.
 */
enum ice_status
ice_read_flat_nvm(struct ice_hw *hw, uint32_t offset, uint32_t *length,
    uint8_t *data, bool read_shadow_ram)
{
	enum ice_status status;
	uint32_t inlen = *length;
	uint32_t bytes_read = 0;
	bool last_cmd;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	*length = 0;

	/* Verify the length of the read if this is for the Shadow RAM */
	if (read_shadow_ram && ((offset + inlen) > (hw->flash.sr_words * 2u))) {
		DNPRINTF(ICE_DBG_NVM,
		    "NVM error: requested data is beyond Shadow RAM limit\n");
		return ICE_ERR_PARAM;
	}

	do {
		uint32_t read_size, sector_offset;

		/* ice_aq_read_nvm cannot read more than 4KB at a time.
		 * Additionally, a read from the Shadow RAM may not cross over
		 * a sector boundary. Conveniently, the sector size is also
		 * 4KB.
		 */
		sector_offset = offset % ICE_AQ_MAX_BUF_LEN;
		read_size = MIN(ICE_AQ_MAX_BUF_LEN - sector_offset,
		    inlen - bytes_read);

		last_cmd = !(bytes_read + read_size < inlen);

		/* ice_aq_read_nvm takes the length as a u16. Our read_size is
		 * calculated using a u32, but the ICE_AQ_MAX_BUF_LEN maximum
		 * size guarantees that it will fit within the 2 bytes.
		 */
		status = ice_aq_read_nvm(hw, ICE_AQC_NVM_START_POINT,
		    offset, (uint16_t)read_size, data + bytes_read, last_cmd,
		    read_shadow_ram, NULL);
		if (status)
			break;

		bytes_read += read_size;
		offset += read_size;
	} while (!last_cmd);

	*length = bytes_read;
	return status;
}

/**
 * ice_discover_flash_size - Discover the available flash size
 * @hw: pointer to the HW struct
 *
 * The device flash could be up to 16MB in size. However, it is possible that
 * the actual size is smaller. Use bisection to determine the accessible size
 * of flash memory.
 */
enum ice_status
ice_discover_flash_size(struct ice_hw *hw)
{
	uint32_t min_size = 0, max_size = ICE_AQC_NVM_MAX_OFFSET + 1;
	enum ice_status status;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (status)
		return status;

	while ((max_size - min_size) > 1) {
		uint32_t offset = (max_size + min_size) / 2;
		uint32_t len = 1;
		uint8_t data;

		status = ice_read_flat_nvm(hw, offset, &len, &data, false);
		if (status == ICE_ERR_AQ_ERROR &&
		    hw->adminq.sq_last_status == ICE_AQ_RC_EINVAL) {
			DNPRINTF(ICE_DBG_NVM,
			    "%s: New upper bound of %u bytes\n", __func__,
			    offset);
			status = ICE_SUCCESS;
			max_size = offset;
		} else if (!status) {
			DNPRINTF(ICE_DBG_NVM,
			    "%s: New lower bound of %u bytes\n", __func__,
			    offset);
			min_size = offset;
		} else {
			/* an unexpected error occurred */
			goto err_read_flat_nvm;
		}
	}

	DNPRINTF(ICE_DBG_NVM, "Predicted flash size is %u bytes\n", max_size);

	hw->flash.flash_size = max_size;

err_read_flat_nvm:
	ice_release_nvm(hw);

	return status;
}

/**
 * ice_read_sr_word_aq - Reads Shadow RAM via AQ
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using ice_read_flat_nvm.
 */
enum ice_status
ice_read_sr_word_aq(struct ice_hw *hw, uint16_t offset, uint16_t *data)
{
	uint32_t bytes = sizeof(uint16_t);
	enum ice_status status;
	uint16_t data_local;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	/* Note that ice_read_flat_nvm checks if the read is past the Shadow
	 * RAM size, and ensures we don't read across a Shadow RAM sector
	 * boundary
	 */
	status = ice_read_flat_nvm(hw, offset * sizeof(uint16_t), &bytes,
				   (uint8_t *)&data_local, true);
	if (status)
		return status;

	*data = le16toh(data_local);
	return ICE_SUCCESS;
}

/**
 * ice_read_sr_word - Reads Shadow RAM word and acquire NVM if necessary
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using the ice_read_sr_word_aq.
 */
enum ice_status
ice_read_sr_word(struct ice_hw *hw, uint16_t offset, uint16_t *data)
{
	enum ice_status status;

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (!status) {
		status = ice_read_sr_word_aq(hw, offset, data);
		ice_release_nvm(hw);
	}

	return status;
}

/**
 * ice_read_sr_pointer - Read the value of a Shadow RAM pointer word
 * @hw: pointer to the HW structure
 * @offset: the word offset of the Shadow RAM word to read
 * @pointer: pointer value read from Shadow RAM
 *
 * Read the given Shadow RAM word, and convert it to a pointer value specified
 * in bytes. This function assumes the specified offset is a valid pointer
 * word.
 *
 * Each pointer word specifies whether it is stored in word size or 4KB
 * sector size by using the highest bit. The reported pointer value will be in
 * bytes, intended for flat NVM reads.
 */
enum ice_status
ice_read_sr_pointer(struct ice_hw *hw, uint16_t offset, uint32_t *pointer)
{
	enum ice_status status;
	uint16_t value;

	status = ice_read_sr_word(hw, offset, &value);
	if (status)
		return status;

	/* Determine if the pointer is in 4KB or word units */
	if (value & ICE_SR_NVM_PTR_4KB_UNITS)
		*pointer = (value & ~ICE_SR_NVM_PTR_4KB_UNITS) * 4 * 1024;
	else
		*pointer = value * 2;

	return ICE_SUCCESS;
}

/**
 * ice_read_sr_area_size - Read an area size from a Shadow RAM word
 * @hw: pointer to the HW structure
 * @offset: the word offset of the Shadow RAM to read
 * @size: size value read from the Shadow RAM
 *
 * Read the given Shadow RAM word, and convert it to an area size value
 * specified in bytes. This function assumes the specified offset is a valid
 * area size word.
 *
 * Each area size word is specified in 4KB sector units. This function reports
 * the size in bytes, intended for flat NVM reads.
 */
enum ice_status
ice_read_sr_area_size(struct ice_hw *hw, uint16_t offset, uint32_t *size)
{
	enum ice_status status;
	uint16_t value;

	status = ice_read_sr_word(hw, offset, &value);
	if (status)
		return status;

	/* Area sizes are always specified in 4KB units */
	*size = value * 4 * 1024;

	return ICE_SUCCESS;
}

/**
 * ice_determine_active_flash_banks - Discover active bank for each module
 * @hw: pointer to the HW struct
 *
 * Read the Shadow RAM control word and determine which banks are active for
 * the NVM, OROM, and Netlist modules. Also read and calculate the associated
 * pointer and size. These values are then cached into the ice_flash_info
 * structure for later use in order to calculate the correct offset to read
 * from the active module.
 */
enum ice_status
ice_determine_active_flash_banks(struct ice_hw *hw)
{
	struct ice_bank_info *banks = &hw->flash.banks;
	enum ice_status status;
	uint16_t ctrl_word;

	status = ice_read_sr_word(hw, ICE_SR_NVM_CTRL_WORD, &ctrl_word);
	if (status) {
		DNPRINTF(ICE_DBG_NVM,
		    "Failed to read the Shadow RAM control word\n");
		return status;
	}

	/* Check that the control word indicates validity */
	if ((ctrl_word & ICE_SR_CTRL_WORD_1_M) >> ICE_SR_CTRL_WORD_1_S !=
	    ICE_SR_CTRL_WORD_VALID) {
		DNPRINTF(ICE_DBG_NVM, "Shadow RAM control word is invalid\n");
		return ICE_ERR_CFG;
	}

	if (!(ctrl_word & ICE_SR_CTRL_WORD_NVM_BANK))
		banks->nvm_bank = ICE_1ST_FLASH_BANK;
	else
		banks->nvm_bank = ICE_2ND_FLASH_BANK;

	if (!(ctrl_word & ICE_SR_CTRL_WORD_OROM_BANK))
		banks->orom_bank = ICE_1ST_FLASH_BANK;
	else
		banks->orom_bank = ICE_2ND_FLASH_BANK;

	if (!(ctrl_word & ICE_SR_CTRL_WORD_NETLIST_BANK))
		banks->netlist_bank = ICE_1ST_FLASH_BANK;
	else
		banks->netlist_bank = ICE_2ND_FLASH_BANK;

	status = ice_read_sr_pointer(hw, ICE_SR_1ST_NVM_BANK_PTR, &banks->nvm_ptr);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to read NVM bank pointer\n");
		return status;
	}

	status = ice_read_sr_area_size(hw, ICE_SR_NVM_BANK_SIZE, &banks->nvm_size);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to read NVM bank area size\n");
		return status;
	}

	status = ice_read_sr_pointer(hw, ICE_SR_1ST_OROM_BANK_PTR, &banks->orom_ptr);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to read OROM bank pointer\n");
		return status;
	}

	status = ice_read_sr_area_size(hw, ICE_SR_OROM_BANK_SIZE, &banks->orom_size);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to read OROM bank area size\n");
		return status;
	}

	status = ice_read_sr_pointer(hw, ICE_SR_NETLIST_BANK_PTR, &banks->netlist_ptr);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to read Netlist bank pointer\n");
		return status;
	}

	status = ice_read_sr_area_size(hw, ICE_SR_NETLIST_BANK_SIZE,
	    &banks->netlist_size);
	if (status) {
		DNPRINTF(ICE_DBG_NVM,
		    "Failed to read Netlist bank area size\n");
		return status;
	}

	return ICE_SUCCESS;
}

/**
 * ice_read_flash_module - Read a word from one of the main NVM modules
 * @hw: pointer to the HW structure
 * @bank: which bank of the module to read
 * @module: the module to read
 * @offset: the offset into the module in bytes
 * @data: storage for the word read from the flash
 * @length: bytes of data to read
 *
 * Read data from the specified flash module. The bank parameter indicates
 * whether or not to read from the active bank or the inactive bank of that
 * module.
 *
 * The word will be read using flat NVM access, and relies on the
 * hw->flash.banks data being setup by ice_determine_active_flash_banks()
 * during initialization.
 */
enum ice_status
ice_read_flash_module(struct ice_hw *hw, enum ice_bank_select bank,
    uint16_t module, uint32_t offset, uint8_t *data, uint32_t length)
{
	enum ice_status status;
	uint32_t start;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	start = ice_get_flash_bank_offset(hw, bank, module);
	if (!start) {
		DNPRINTF(ICE_DBG_NVM,
		    "Unable to calculate flash bank offset for module 0x%04x\n",
		    module);
		return ICE_ERR_PARAM;
	}

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (status)
		return status;

	status = ice_read_flat_nvm(hw, start + offset, &length, data, false);

	ice_release_nvm(hw);

	return status;
}

/**
 * ice_read_nvm_module - Read from the active main NVM module
 * @hw: pointer to the HW structure
 * @bank: whether to read from active or inactive NVM module
 * @offset: offset into the NVM module to read, in words
 * @data: storage for returned word value
 *
 * Read the specified word from the active NVM module. This includes the CSS
 * header at the start of the NVM module.
 */
enum ice_status
ice_read_nvm_module(struct ice_hw *hw, enum ice_bank_select bank,
    uint32_t offset, uint16_t *data)
{
	enum ice_status status;
	uint16_t data_local;

	status = ice_read_flash_module(hw, bank, ICE_SR_1ST_NVM_BANK_PTR,
	    offset * sizeof(uint16_t), (uint8_t *)&data_local,
	    sizeof(uint16_t));
	if (!status)
		*data = le16toh(data_local);

	return status;
}

/**
 * ice_get_nvm_css_hdr_len - Read the CSS header length from the NVM CSS header
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash bank
 * @hdr_len: storage for header length in words
 *
 * Read the CSS header length from the NVM CSS header and add the Authentication
 * header size, and then convert to words.
 */
enum ice_status
ice_get_nvm_css_hdr_len(struct ice_hw *hw, enum ice_bank_select bank,
    uint32_t *hdr_len)
{
	uint16_t hdr_len_l, hdr_len_h;
	enum ice_status status;
	uint32_t hdr_len_dword;

	status = ice_read_nvm_module(hw, bank, ICE_NVM_CSS_HDR_LEN_L,
	    &hdr_len_l);
	if (status)
		return status;

	status = ice_read_nvm_module(hw, bank, ICE_NVM_CSS_HDR_LEN_H,
	    &hdr_len_h);
	if (status)
		return status;

	/* CSS header length is in DWORD, so convert to words and add
	 * authentication header size
	 */
	hdr_len_dword = hdr_len_h << 16 | hdr_len_l;
	*hdr_len = (hdr_len_dword * 2) + ICE_NVM_AUTH_HEADER_LEN;

	return ICE_SUCCESS;
}

/**
 * ice_get_nvm_srev - Read the security revision from the NVM CSS header
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash bank
 * @srev: storage for security revision
 *
 * Read the security revision out of the CSS header of the active NVM module
 * bank.
 */
enum ice_status ice_get_nvm_srev(struct ice_hw *hw, enum ice_bank_select bank,
    uint32_t *srev)
{
	enum ice_status status;
	uint16_t srev_l, srev_h;

	status = ice_read_nvm_module(hw, bank, ICE_NVM_CSS_SREV_L, &srev_l);
	if (status)
		return status;

	status = ice_read_nvm_module(hw, bank, ICE_NVM_CSS_SREV_H, &srev_h);
	if (status)
		return status;

	*srev = srev_h << 16 | srev_l;

	return ICE_SUCCESS;
}

/**
 * ice_read_nvm_sr_copy - Read a word from the Shadow RAM copy in the NVM bank
 * @hw: pointer to the HW structure
 * @bank: whether to read from the active or inactive NVM module
 * @offset: offset into the Shadow RAM copy to read, in words
 * @data: storage for returned word value
 *
 * Read the specified word from the copy of the Shadow RAM found in the
 * specified NVM module.
 */
enum ice_status
ice_read_nvm_sr_copy(struct ice_hw *hw, enum ice_bank_select bank,
    uint32_t offset, uint16_t *data)
{
	enum ice_status status;
	uint32_t hdr_len;

	status = ice_get_nvm_css_hdr_len(hw, bank, &hdr_len);
	if (status)
		return status;

	hdr_len = roundup(hdr_len, 32);

	return ice_read_nvm_module(hw, bank, hdr_len + offset, data);
}

/**
 * ice_get_nvm_ver_info - Read NVM version information
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash bank
 * @nvm: pointer to NVM info structure
 *
 * Read the NVM EETRACK ID and map version of the main NVM image bank, filling
 * in the NVM info structure.
 */
enum ice_status
ice_get_nvm_ver_info(struct ice_hw *hw, enum ice_bank_select bank,
    struct ice_nvm_info *nvm)
{
	uint16_t eetrack_lo, eetrack_hi, ver;
	enum ice_status status;

	status = ice_read_nvm_sr_copy(hw, bank, ICE_SR_NVM_DEV_STARTER_VER,
	    &ver);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to read DEV starter version.\n");
		return status;
	}

	nvm->major = (ver & ICE_NVM_VER_HI_MASK) >> ICE_NVM_VER_HI_SHIFT;
	nvm->minor = (ver & ICE_NVM_VER_LO_MASK) >> ICE_NVM_VER_LO_SHIFT;

	status = ice_read_nvm_sr_copy(hw, bank, ICE_SR_NVM_EETRACK_LO,
	    &eetrack_lo);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to read EETRACK lo.\n");
		return status;
	}
	status = ice_read_nvm_sr_copy(hw, bank, ICE_SR_NVM_EETRACK_HI,
	     &eetrack_hi);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to read EETRACK hi.\n");
		return status;
	}

	nvm->eetrack = (eetrack_hi << 16) | eetrack_lo;

	status = ice_get_nvm_srev(hw, bank, &nvm->srev);
	if (status)
		DNPRINTF(ICE_DBG_NVM,
		    "Failed to read NVM security revision.\n");

	return ICE_SUCCESS;
}

/**
 * ice_read_orom_module - Read from the active Option ROM module
 * @hw: pointer to the HW structure
 * @bank: whether to read from active or inactive OROM module
 * @offset: offset into the OROM module to read, in words
 * @data: storage for returned word value
 *
 * Read the specified word from the active Option ROM module of the flash.
 * Note that unlike the NVM module, the CSS data is stored at the end of the
 * module instead of at the beginning.
 */
enum ice_status
ice_read_orom_module(struct ice_hw *hw, enum ice_bank_select bank,
    uint32_t offset, uint16_t *data)
{
	enum ice_status status;
	uint16_t data_local;

	status = ice_read_flash_module(hw, bank, ICE_SR_1ST_OROM_BANK_PTR,
	    offset * sizeof(uint16_t), (uint8_t *)&data_local,
	    sizeof(uint16_t));
	if (!status)
		*data = le16toh(data_local);

	return status;
}

/**
 * ice_get_orom_srev - Read the security revision from the OROM CSS header
 * @hw: pointer to the HW struct
 * @bank: whether to read from active or inactive flash module
 * @srev: storage for security revision
 *
 * Read the security revision out of the CSS header of the active OROM module
 * bank.
 */
enum ice_status
ice_get_orom_srev(struct ice_hw *hw, enum ice_bank_select bank, uint32_t *srev)
{
	uint32_t orom_size_word = hw->flash.banks.orom_size / 2;
	enum ice_status status;
	uint16_t srev_l, srev_h;
	uint32_t css_start;
	uint32_t hdr_len;

	status = ice_get_nvm_css_hdr_len(hw, bank, &hdr_len);
	if (status)
		return status;

	if (orom_size_word < hdr_len) {
		DNPRINTF(ICE_DBG_NVM, "Unexpected Option ROM Size of %u\n",
		    hw->flash.banks.orom_size);
		return ICE_ERR_CFG;
	}

	/* calculate how far into the Option ROM the CSS header starts. Note
	 * that ice_read_orom_module takes a word offset
	 */
	css_start = orom_size_word - hdr_len;
	status = ice_read_orom_module(hw, bank, css_start + ICE_NVM_CSS_SREV_L,
	    &srev_l);
	if (status)
		return status;

	status = ice_read_orom_module(hw, bank, css_start + ICE_NVM_CSS_SREV_H,
	    &srev_h);
	if (status)
		return status;

	*srev = srev_h << 16 | srev_l;

	return ICE_SUCCESS;
}

/**
 * ice_get_orom_civd_data - Get the combo version information from Option ROM
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash module
 * @civd: storage for the Option ROM CIVD data.
 *
 * Searches through the Option ROM flash contents to locate the CIVD data for
 * the image.
 */
enum ice_status
ice_get_orom_civd_data(struct ice_hw *hw, enum ice_bank_select bank,
		       struct ice_orom_civd_info *civd)
{
	uint8_t *orom_data;
	enum ice_status status;
	uint32_t offset;

	/* The CIVD section is located in the Option ROM aligned to 512 bytes.
	 * The first 4 bytes must contain the ASCII characters "$CIV".
	 * A simple modulo 256 sum of all of the bytes of the structure must
	 * equal 0.
	 *
	 * The exact location is unknown and varies between images but is
	 * usually somewhere in the middle of the bank. We need to scan the
	 * Option ROM bank to locate it.
	 *
	 * It's significantly faster to read the entire Option ROM up front
	 * using the maximum page size, than to read each possible location
	 * with a separate firmware command.
	 */
	orom_data = (uint8_t *)ice_calloc(hw, hw->flash.banks.orom_size,
	    sizeof(uint8_t));
	if (!orom_data)
		return ICE_ERR_NO_MEMORY;

	status = ice_read_flash_module(hw, bank, ICE_SR_1ST_OROM_BANK_PTR, 0,
	    orom_data, hw->flash.banks.orom_size);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Unable to read Option ROM data\n");
		goto exit_error;
	}

	/* Scan the memory buffer to locate the CIVD data section */
	for (offset = 0; (offset + 512) <= hw->flash.banks.orom_size;
	    offset += 512) {
		struct ice_orom_civd_info *tmp;
		uint8_t sum = 0, i;

		tmp = (struct ice_orom_civd_info *)&orom_data[offset];

		/* Skip forward until we find a matching signature */
		if (memcmp("$CIV", tmp->signature, sizeof(tmp->signature)) != 0)
			continue;

		DNPRINTF(ICE_DBG_NVM, "Found CIVD section at offset %u\n",
			  offset);

		/* Verify that the simple checksum is zero */
		for (i = 0; i < sizeof(*tmp); i++)
			sum += ((uint8_t *)tmp)[i];

		if (sum) {
			DNPRINTF(ICE_DBG_NVM,
			    "Found CIVD data with invalid checksum of %u\n",
			    sum);
			status = ICE_ERR_NVM;
			goto exit_error;
		}

		*civd = *tmp;
		ice_free(hw, orom_data);
		return ICE_SUCCESS;
	}

	status = ICE_ERR_NVM;
	DNPRINTF(ICE_DBG_NVM,
	    "Unable to locate CIVD data within the Option ROM\n");

exit_error:
	ice_free(hw, orom_data);
	return status;
}

/**
 * ice_get_orom_ver_info - Read Option ROM version information
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash module
 * @orom: pointer to Option ROM info structure
 *
 * Read Option ROM version and security revision from the Option ROM flash
 * section.
 */
enum ice_status
ice_get_orom_ver_info(struct ice_hw *hw, enum ice_bank_select bank,
    struct ice_orom_info *orom)
{
	struct ice_orom_civd_info civd;
	enum ice_status status;
	uint32_t combo_ver;

	status = ice_get_orom_civd_data(hw, bank, &civd);
	if (status) {
		DNPRINTF(ICE_DBG_NVM,
		    "Failed to locate valid Option ROM CIVD data\n");
		return status;
	}

	combo_ver = le32toh(civd.combo_ver);

	orom->major = (uint8_t)((combo_ver & ICE_OROM_VER_MASK) >>
	    ICE_OROM_VER_SHIFT);
	orom->patch = (uint8_t)(combo_ver & ICE_OROM_VER_PATCH_MASK);
	orom->build = (uint16_t)((combo_ver & ICE_OROM_VER_BUILD_MASK) >>
	    ICE_OROM_VER_BUILD_SHIFT);

	status = ice_get_orom_srev(hw, bank, &orom->srev);
	if (status) {
		DNPRINTF(ICE_DBG_NVM,
		    "Failed to read Option ROM security revision.\n");
		return status;
	}

	return ICE_SUCCESS;
}

/**
 * ice_read_netlist_module - Read data from the netlist module area
 * @hw: pointer to the HW structure
 * @bank: whether to read from the active or inactive module
 * @offset: offset into the netlist to read from
 * @data: storage for returned word value
 *
 * Read a word from the specified netlist bank.
 */
enum ice_status
ice_read_netlist_module(struct ice_hw *hw, enum ice_bank_select bank,
    uint32_t offset, uint16_t *data)
{
	enum ice_status status;
	uint16_t data_local;

	status = ice_read_flash_module(hw, bank, ICE_SR_NETLIST_BANK_PTR,
	    offset * sizeof(uint16_t), (uint8_t *)&data_local,
	    sizeof(uint16_t));
	if (!status)
		*data = le16toh(data_local);

	return status;
}

/**
 * ice_get_netlist_info
 * @hw: pointer to the HW struct
 * @bank: whether to read from the active or inactive flash bank
 * @netlist: pointer to netlist version info structure
 *
 * Get the netlist version information from the requested bank. Reads the Link
 * Topology section to find the Netlist ID block and extract the relevant
 * information into the netlist version structure.
 */
enum ice_status
ice_get_netlist_info(struct ice_hw *hw, enum ice_bank_select bank,
    struct ice_netlist_info *netlist)
{
	uint16_t module_id, length, node_count, i;
	enum ice_status status;
	uint16_t *id_blk;

	status = ice_read_netlist_module(hw, bank, ICE_NETLIST_TYPE_OFFSET,
	    &module_id);
	if (status)
		return status;

	if (module_id != ICE_NETLIST_LINK_TOPO_MOD_ID) {
		DNPRINTF(ICE_DBG_NVM,
		    "Expected netlist module_id ID of 0x%04x, but got 0x%04x\n",
		    ICE_NETLIST_LINK_TOPO_MOD_ID, module_id);
		return ICE_ERR_NVM;
	}

	status = ice_read_netlist_module(hw, bank, ICE_LINK_TOPO_MODULE_LEN,
	    &length);
	if (status)
		return status;

	if (length < ICE_NETLIST_ID_BLK_SIZE) {
		DNPRINTF(ICE_DBG_NVM, "Netlist Link Topology module too small. "
		    "Expected at least %u words, but got %u words.\n",
		    ICE_NETLIST_ID_BLK_SIZE, length);
		return ICE_ERR_NVM;
	}

	status = ice_read_netlist_module(hw, bank, ICE_LINK_TOPO_NODE_COUNT,
	    &node_count);
	if (status)
		return status;
	node_count &= ICE_LINK_TOPO_NODE_COUNT_M;

	id_blk = (uint16_t *)ice_calloc(hw, ICE_NETLIST_ID_BLK_SIZE,
	    sizeof(*id_blk));
	if (!id_blk)
		return ICE_ERR_NO_MEMORY;

	/* Read out the entire Netlist ID Block at once. */
	status = ice_read_flash_module(hw, bank, ICE_SR_NETLIST_BANK_PTR,
	    ICE_NETLIST_ID_BLK_OFFSET(node_count) * sizeof(uint16_t),
	    (uint8_t *)id_blk, ICE_NETLIST_ID_BLK_SIZE * sizeof(uint16_t));
	if (status)
		goto exit_error;

	for (i = 0; i < ICE_NETLIST_ID_BLK_SIZE; i++)
		id_blk[i] = le16toh(((uint16_t *)id_blk)[i]);

	netlist->major = id_blk[ICE_NETLIST_ID_BLK_MAJOR_VER_HIGH] << 16 |
			 id_blk[ICE_NETLIST_ID_BLK_MAJOR_VER_LOW];
	netlist->minor = id_blk[ICE_NETLIST_ID_BLK_MINOR_VER_HIGH] << 16 |
			 id_blk[ICE_NETLIST_ID_BLK_MINOR_VER_LOW];
	netlist->type = id_blk[ICE_NETLIST_ID_BLK_TYPE_HIGH] << 16 |
			id_blk[ICE_NETLIST_ID_BLK_TYPE_LOW];
	netlist->rev = id_blk[ICE_NETLIST_ID_BLK_REV_HIGH] << 16 |
		       id_blk[ICE_NETLIST_ID_BLK_REV_LOW];
	netlist->cust_ver = id_blk[ICE_NETLIST_ID_BLK_CUST_VER];
	/* Read the left most 4 bytes of SHA */
	netlist->hash = id_blk[ICE_NETLIST_ID_BLK_SHA_HASH_WORD(15)] << 16 |
			id_blk[ICE_NETLIST_ID_BLK_SHA_HASH_WORD(14)];

exit_error:
	ice_free(hw, id_blk);

	return status;
}

/**
 * ice_init_nvm - initializes NVM setting
 * @hw: pointer to the HW struct
 *
 * This function reads and populates NVM settings such as Shadow RAM size,
 * max_timeout, and blank_nvm_mode
 */
enum ice_status
ice_init_nvm(struct ice_hw *hw)
{
	struct ice_flash_info *flash = &hw->flash;
	enum ice_status status;
	uint32_t fla, gens_stat;
	uint8_t sr_size;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	/* The SR size is stored regardless of the NVM programming mode
	 * as the blank mode may be used in the factory line.
	 */
	gens_stat = ICE_READ(hw, GLNVM_GENS);
	sr_size = (gens_stat & GLNVM_GENS_SR_SIZE_M) >> GLNVM_GENS_SR_SIZE_S;

	/* Switching to words (sr_size contains power of 2) */
	flash->sr_words = BIT(sr_size) * ICE_SR_WORDS_IN_1KB;

	/* Check if we are in the normal or blank NVM programming mode */
	fla = ICE_READ(hw, GLNVM_FLA);
	if (fla & GLNVM_FLA_LOCKED_M) { /* Normal programming mode */
		flash->blank_nvm_mode = false;
	} else {
		/* Blank programming mode */
		flash->blank_nvm_mode = true;
		DNPRINTF(ICE_DBG_NVM,
		    "NVM init error: unsupported blank mode.\n");
		return ICE_ERR_NVM_BLANK_MODE;
	}

	status = ice_discover_flash_size(hw);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "NVM init error: failed to discover "
		    "flash size.\n");
		return status;
	}

	status = ice_determine_active_flash_banks(hw);
	if (status) {
		DNPRINTF(ICE_DBG_NVM, "Failed to determine active flash "
		    "banks.\n");
		return status;
	}

	status = ice_get_nvm_ver_info(hw, ICE_ACTIVE_FLASH_BANK, &flash->nvm);
	if (status) {
		DNPRINTF(ICE_DBG_INIT, "Failed to read NVM info.\n");
		return status;
	}

	status = ice_get_orom_ver_info(hw, ICE_ACTIVE_FLASH_BANK, &flash->orom);
	if (status)
		DNPRINTF(ICE_DBG_INIT, "Failed to read Option ROM info.\n");

	/* read the netlist version information */
	status = ice_get_netlist_info(hw, ICE_ACTIVE_FLASH_BANK,
	    &flash->netlist);
	if (status)
		DNPRINTF(ICE_DBG_INIT, "Failed to read netlist info.\n");

	return ICE_SUCCESS;
}

void
ice_print_rollback_msg(struct ice_hw *hw)
{
	struct ice_softc *sc = hw->hw_sc;
	char nvm_str[ICE_NVM_VER_LEN] = { 0 };
	struct ice_orom_info *orom;
	struct ice_nvm_info *nvm;

	orom = &hw->flash.orom;
	nvm = &hw->flash.nvm;

	snprintf(nvm_str, sizeof(nvm_str), "%x.%02x 0x%x %d.%d.%d",
		 nvm->major, nvm->minor, nvm->eetrack, orom->major,
		 orom->build, orom->patch);
	printf("%s: Firmware rollback mode detected. "
	    "Current version is NVM: %s, FW: %d.%d. "
	    "Device may exhibit limited functionality.\n",
	    sc->sc_dev.dv_xname, nvm_str, hw->fw_maj_ver, hw->fw_min_ver);
}

/**
 * ice_clear_pf_cfg - Clear PF configuration
 * @hw: pointer to the hardware structure
 *
 * Clears any existing PF configuration (VSIs, VSI lists, switch rules, port
 * configuration, flow director filters, etc.).
 */
enum ice_status
ice_clear_pf_cfg(struct ice_hw *hw)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_clear_pf_cfg);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_aq_clear_pxe_mode
 * @hw: pointer to the HW struct
 *
 * Tell the firmware that the driver is taking over from PXE (0x0110).
 */
enum ice_status
ice_aq_clear_pxe_mode(struct ice_hw *hw)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_clear_pxe_mode);
	desc.params.clear_pxe.rx_cnt = ICE_AQC_CLEAR_PXE_RX_CNT;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_clear_pxe_mode - clear pxe operations mode
 * @hw: pointer to the HW struct
 *
 * Make sure all PXE mode settings are cleared, including things
 * like descriptor fetch/write-back mode.
 */
void
ice_clear_pxe_mode(struct ice_hw *hw)
{
	if (ice_check_sq_alive(hw, &hw->adminq))
		ice_aq_clear_pxe_mode(hw);
}

/**
 * ice_get_num_per_func - determine number of resources per PF
 * @hw: pointer to the HW structure
 * @max: value to be evenly split between each PF
 *
 * Determine the number of valid functions by going through the bitmap returned
 * from parsing capabilities and use this to calculate the number of resources
 * per PF based on the max value passed in.
 */
uint32_t
ice_get_num_per_func(struct ice_hw *hw, uint32_t max)
{
	uint16_t funcs;

#define ICE_CAPS_VALID_FUNCS_M	0xFF
	funcs = ice_popcount16(hw->dev_caps.common_cap.valid_functions &
	    ICE_CAPS_VALID_FUNCS_M);

	if (!funcs)
		return 0;

	return max / funcs;
}

/**
 * ice_print_led_caps - print LED capabilities
 * @hw: pointer to the ice_hw instance
 * @caps: pointer to common caps instance
 * @prefix: string to prefix when printing
 * @dbg: set to indicate debug print
 */
void
ice_print_led_caps(struct ice_hw *hw, struct ice_hw_common_caps *caps,
		   char const *prefix, bool dbg)
{
	struct ice_softc *sc = hw->hw_sc;
	uint8_t i;

	if (dbg)
		DNPRINTF(ICE_DBG_INIT, "%s: led_pin_num = %d\n", prefix,
		    caps->led_pin_num);
	else
		printf("%s: %s: led_pin_num = %d\n", sc->sc_dev.dv_xname,
		   prefix, caps->led_pin_num);

	for (i = 0; i < ICE_MAX_SUPPORTED_GPIO_LED; i++) {
		if (!caps->led[i])
			continue;

		if (dbg)
			DNPRINTF(ICE_DBG_INIT, "%s: led[%d] = %d\n",
			    prefix, i, caps->led[i]);
		else
			printf("%s: %s: led[%d] = %d\n", sc->sc_dev.dv_xname,
			    prefix, i, caps->led[i]);
	}
}

/**
 * ice_print_sdp_caps - print SDP capabilities
 * @hw: pointer to the ice_hw instance
 * @caps: pointer to common caps instance
 * @prefix: string to prefix when printing
 * @dbg: set to indicate debug print
 */
void
ice_print_sdp_caps(struct ice_hw *hw, struct ice_hw_common_caps *caps,
    char const *prefix, bool dbg)
{
	struct ice_softc *sc = hw->hw_sc;
	uint8_t i;

	if (dbg)
		DNPRINTF(ICE_DBG_INIT, "%s: sdp_pin_num = %d\n", prefix,
		    caps->sdp_pin_num);
	else
		printf("%s: %s: sdp_pin_num = %d\n", sc->sc_dev.dv_xname,
		    prefix, caps->sdp_pin_num);

	for (i = 0; i < ICE_MAX_SUPPORTED_GPIO_SDP; i++) {
		if (!caps->sdp[i])
			continue;

		if (dbg)
			DNPRINTF(ICE_DBG_INIT, "%s: sdp[%d] = %d\n",
			    prefix, i, caps->sdp[i]);
		else
			printf("%s: %s: sdp[%d] = %d\n", sc->sc_dev.dv_xname,
			    prefix, i, caps->sdp[i]);
	}
}

/**
 * ice_parse_common_caps - parse common device/function capabilities
 * @hw: pointer to the HW struct
 * @caps: pointer to common capabilities structure
 * @elem: the capability element to parse
 * @prefix: message prefix for tracing capabilities
 *
 * Given a capability element, extract relevant details into the common
 * capability structure.
 *
 * Returns: true if the capability matches one of the common capability ids,
 * false otherwise.
 */
bool
ice_parse_common_caps(struct ice_hw *hw, struct ice_hw_common_caps *caps,
    struct ice_aqc_list_caps_elem *elem, const char *prefix)
{
	uint32_t logical_id = le32toh(elem->logical_id);
	uint32_t phys_id = le32toh(elem->phys_id);
	uint32_t number = le32toh(elem->number);
	uint16_t cap = le16toh(elem->cap);
	bool found = true;

	switch (cap) {
	case ICE_AQC_CAPS_SWITCHING_MODE:
		caps->switching_mode = number;
		DNPRINTF(ICE_DBG_INIT, "%s: switching_mode = %d\n", prefix,
			  caps->switching_mode);
		break;
	case ICE_AQC_CAPS_MANAGEABILITY_MODE:
		caps->mgmt_mode = number;
		caps->mgmt_protocols_mctp = logical_id;
		DNPRINTF(ICE_DBG_INIT, "%s: mgmt_mode = %d\n", prefix,
			  caps->mgmt_mode);
		DNPRINTF(ICE_DBG_INIT, "%s: mgmt_protocols_mctp = %d\n", prefix,
			  caps->mgmt_protocols_mctp);
		break;
	case ICE_AQC_CAPS_OS2BMC:
		caps->os2bmc = number;
		DNPRINTF(ICE_DBG_INIT, "%s: os2bmc = %d\n", prefix,
		    caps->os2bmc);
		break;
	case ICE_AQC_CAPS_VALID_FUNCTIONS:
		caps->valid_functions = number;
		DNPRINTF(ICE_DBG_INIT, "%s: valid_functions (bitmap) = %d\n",
		    prefix, caps->valid_functions);
		break;
	case ICE_AQC_CAPS_SRIOV:
		caps->sr_iov_1_1 = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: sr_iov_1_1 = %d\n", prefix,
			  caps->sr_iov_1_1);
		break;
	case ICE_AQC_CAPS_VMDQ:
		caps->vmdq = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: vmdq = %d\n", prefix, caps->vmdq);
		break;
	case ICE_AQC_CAPS_802_1QBG:
		caps->evb_802_1_qbg = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: evb_802_1_qbg = %d\n", prefix,
		    number);
		break;
	case ICE_AQC_CAPS_802_1BR:
		caps->evb_802_1_qbh = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: evb_802_1_qbh = %d\n", prefix,
		    number);
		break;
	case ICE_AQC_CAPS_DCB:
		caps->dcb = (number == 1);
		caps->active_tc_bitmap = logical_id;
		caps->maxtc = phys_id;
		DNPRINTF(ICE_DBG_INIT, "%s: dcb = %d\n", prefix, caps->dcb);
		DNPRINTF(ICE_DBG_INIT, "%s: active_tc_bitmap = %d\n", prefix,
			  caps->active_tc_bitmap);
		DNPRINTF(ICE_DBG_INIT, "%s: maxtc = %d\n", prefix, caps->maxtc);
		break;
	case ICE_AQC_CAPS_ISCSI:
		caps->iscsi = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: iscsi = %d\n", prefix, caps->iscsi);
		break;
	case ICE_AQC_CAPS_RSS:
		caps->rss_table_size = number;
		caps->rss_table_entry_width = logical_id;
		DNPRINTF(ICE_DBG_INIT, "%s: rss_table_size = %d\n", prefix,
			  caps->rss_table_size);
		DNPRINTF(ICE_DBG_INIT, "%s: rss_table_entry_width = %d\n",
		    prefix, caps->rss_table_entry_width);
		break;
	case ICE_AQC_CAPS_RXQS:
		caps->num_rxq = number;
		caps->rxq_first_id = phys_id;
		DNPRINTF(ICE_DBG_INIT, "%s: num_rxq = %d\n", prefix,
			  caps->num_rxq);
		DNPRINTF(ICE_DBG_INIT, "%s: rxq_first_id = %d\n", prefix,
			  caps->rxq_first_id);
		break;
	case ICE_AQC_CAPS_TXQS:
		caps->num_txq = number;
		caps->txq_first_id = phys_id;
		DNPRINTF(ICE_DBG_INIT, "%s: num_txq = %d\n", prefix,
			  caps->num_txq);
		DNPRINTF(ICE_DBG_INIT, "%s: txq_first_id = %d\n", prefix,
			  caps->txq_first_id);
		break;
	case ICE_AQC_CAPS_MSIX:
		caps->num_msix_vectors = number;
		caps->msix_vector_first_id = phys_id;
		DNPRINTF(ICE_DBG_INIT, "%s: num_msix_vectors = %d\n", prefix,
			  caps->num_msix_vectors);
		DNPRINTF(ICE_DBG_INIT, "%s: msix_vector_first_id = %d\n",
		    prefix, caps->msix_vector_first_id);
		break;
	case ICE_AQC_CAPS_NVM_MGMT:
		caps->sec_rev_disabled =
			(number & ICE_NVM_MGMT_SEC_REV_DISABLED) ?
			true : false;
		DNPRINTF(ICE_DBG_INIT, "%s: sec_rev_disabled = %d\n", prefix,
			  caps->sec_rev_disabled);
		caps->update_disabled =
			(number & ICE_NVM_MGMT_UPDATE_DISABLED) ?
			true : false;
		DNPRINTF(ICE_DBG_INIT, "%s: update_disabled = %d\n", prefix,
			  caps->update_disabled);
		caps->nvm_unified_update =
			(number & ICE_NVM_MGMT_UNIFIED_UPD_SUPPORT) ?
			true : false;
		DNPRINTF(ICE_DBG_INIT, "%s: nvm_unified_update = %d\n", prefix,
			  caps->nvm_unified_update);
		caps->netlist_auth =
			(number & ICE_NVM_MGMT_NETLIST_AUTH_SUPPORT) ?
			true : false;
		DNPRINTF(ICE_DBG_INIT, "%s: netlist_auth = %d\n", prefix,
			  caps->netlist_auth);
		break;
	case ICE_AQC_CAPS_CEM:
		caps->mgmt_cem = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: mgmt_cem = %d\n", prefix,
			  caps->mgmt_cem);
		break;
	case ICE_AQC_CAPS_IWARP:
		caps->iwarp = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: iwarp = %d\n", prefix, caps->iwarp);
		break;
	case ICE_AQC_CAPS_ROCEV2_LAG:
		caps->roce_lag = !!(number & ICE_AQC_BIT_ROCEV2_LAG);
		DNPRINTF(ICE_DBG_INIT, "%s: roce_lag = %d\n",
			  prefix, caps->roce_lag);
		break;
	case ICE_AQC_CAPS_LED:
		if (phys_id < ICE_MAX_SUPPORTED_GPIO_LED) {
			caps->led[phys_id] = true;
			caps->led_pin_num++;
			DNPRINTF(ICE_DBG_INIT, "%s: led[%d] = 1\n", prefix,
			    phys_id);
		}
		break;
	case ICE_AQC_CAPS_SDP:
		if (phys_id < ICE_MAX_SUPPORTED_GPIO_SDP) {
			caps->sdp[phys_id] = true;
			caps->sdp_pin_num++;
			DNPRINTF(ICE_DBG_INIT, "%s: sdp[%d] = 1\n", prefix,
			    phys_id);
		}
		break;
	case ICE_AQC_CAPS_WR_CSR_PROT:
		caps->wr_csr_prot = number;
		caps->wr_csr_prot |= (uint64_t)logical_id << 32;
		DNPRINTF(ICE_DBG_INIT, "%s: wr_csr_prot = 0x%llX\n", prefix,
			  (unsigned long long)caps->wr_csr_prot);
		break;
	case ICE_AQC_CAPS_WOL_PROXY:
		caps->num_wol_proxy_fltr = number;
		caps->wol_proxy_vsi_seid = logical_id;
		caps->apm_wol_support = !!(phys_id & ICE_WOL_SUPPORT_M);
		caps->acpi_prog_mthd = !!(phys_id &
					  ICE_ACPI_PROG_MTHD_M);
		caps->proxy_support = !!(phys_id & ICE_PROXY_SUPPORT_M);
		DNPRINTF(ICE_DBG_INIT, "%s: num_wol_proxy_fltr = %d\n", prefix,
			  caps->num_wol_proxy_fltr);
		DNPRINTF(ICE_DBG_INIT, "%s: wol_proxy_vsi_seid = %d\n", prefix,
			  caps->wol_proxy_vsi_seid);
		DNPRINTF(ICE_DBG_INIT, "%s: apm_wol_support = %d\n",
			  prefix, caps->apm_wol_support);
		break;
	case ICE_AQC_CAPS_MAX_MTU:
		caps->max_mtu = number;
		DNPRINTF(ICE_DBG_INIT, "%s: max_mtu = %d\n",
			  prefix, caps->max_mtu);
		break;
	case ICE_AQC_CAPS_PCIE_RESET_AVOIDANCE:
		caps->pcie_reset_avoidance = (number > 0);
		DNPRINTF(ICE_DBG_INIT,
			  "%s: pcie_reset_avoidance = %d\n", prefix,
			  caps->pcie_reset_avoidance);
		break;
	case ICE_AQC_CAPS_POST_UPDATE_RESET_RESTRICT:
		caps->reset_restrict_support = (number == 1);
		DNPRINTF(ICE_DBG_INIT,
			  "%s: reset_restrict_support = %d\n", prefix,
			  caps->reset_restrict_support);
		break;
	case ICE_AQC_CAPS_EXT_TOPO_DEV_IMG0:
	case ICE_AQC_CAPS_EXT_TOPO_DEV_IMG1:
	case ICE_AQC_CAPS_EXT_TOPO_DEV_IMG2:
	case ICE_AQC_CAPS_EXT_TOPO_DEV_IMG3:
	{
		uint8_t index = (uint8_t)(cap - ICE_AQC_CAPS_EXT_TOPO_DEV_IMG0);

		caps->ext_topo_dev_img_ver_high[index] = number;
		caps->ext_topo_dev_img_ver_low[index] = logical_id;
		caps->ext_topo_dev_img_part_num[index] =
			(phys_id & ICE_EXT_TOPO_DEV_IMG_PART_NUM_M) >>
			ICE_EXT_TOPO_DEV_IMG_PART_NUM_S;
		caps->ext_topo_dev_img_load_en[index] =
			(phys_id & ICE_EXT_TOPO_DEV_IMG_LOAD_EN) != 0;
		caps->ext_topo_dev_img_prog_en[index] =
			(phys_id & ICE_EXT_TOPO_DEV_IMG_PROG_EN) != 0;
		caps->ext_topo_dev_img_ver_schema[index] =
			(phys_id & ICE_EXT_TOPO_DEV_IMG_VER_SCHEMA) != 0;
		DNPRINTF(ICE_DBG_INIT,
			  "%s: ext_topo_dev_img_ver_high[%d] = %d\n",
			  prefix, index,
			  caps->ext_topo_dev_img_ver_high[index]);
		DNPRINTF(ICE_DBG_INIT,
			  "%s: ext_topo_dev_img_ver_low[%d] = %d\n",
			  prefix, index,
			  caps->ext_topo_dev_img_ver_low[index]);
		DNPRINTF(ICE_DBG_INIT,
			  "%s: ext_topo_dev_img_part_num[%d] = %d\n",
			  prefix, index,
			  caps->ext_topo_dev_img_part_num[index]);
		DNPRINTF(ICE_DBG_INIT,
			  "%s: ext_topo_dev_img_load_en[%d] = %d\n",
			  prefix, index,
			  caps->ext_topo_dev_img_load_en[index]);
		DNPRINTF(ICE_DBG_INIT,
			  "%s: ext_topo_dev_img_prog_en[%d] = %d\n",
			  prefix, index,
			  caps->ext_topo_dev_img_prog_en[index]);
		DNPRINTF(ICE_DBG_INIT,
			  "%s: ext_topo_dev_img_ver_schema[%d] = %d\n",
			  prefix, index,
			  caps->ext_topo_dev_img_ver_schema[index]);
		break;
	}
	case ICE_AQC_CAPS_TX_SCHED_TOPO_COMP_MODE:
		caps->tx_sched_topo_comp_mode_en = (number == 1);
		break;
	case ICE_AQC_CAPS_DYN_FLATTENING:
		caps->dyn_flattening_en = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: dyn_flattening_en = %d\n",
			  prefix, caps->dyn_flattening_en);
		break;
	case ICE_AQC_CAPS_OROM_RECOVERY_UPDATE:
		caps->orom_recovery_update = (number == 1);
		DNPRINTF(ICE_DBG_INIT, "%s: orom_recovery_update = %d\n",
			  prefix, caps->orom_recovery_update);
		break;
	default:
		/* Not one of the recognized common capabilities */
		found = false;
	}

	return found;
}

/**
 * ice_recalc_port_limited_caps - Recalculate port limited capabilities
 * @hw: pointer to the HW structure
 * @caps: pointer to capabilities structure to fix
 *
 * Re-calculate the capabilities that are dependent on the number of physical
 * ports; i.e. some features are not supported or function differently on
 * devices with more than 4 ports.
 */
void
ice_recalc_port_limited_caps(struct ice_hw *hw, struct ice_hw_common_caps *caps)
{
	/* This assumes device capabilities are always scanned before function
	 * capabilities during the initialization flow.
	 */
	if (hw->dev_caps.num_funcs > 4) {
		/* Max 4 TCs per port */
		caps->maxtc = 4;
		DNPRINTF(ICE_DBG_INIT,
		    "reducing maxtc to %d (based on #ports)\n", caps->maxtc);
		if (caps->iwarp) {
			DNPRINTF(ICE_DBG_INIT, "forcing RDMA off\n");
			caps->iwarp = 0;
		}

		/* print message only when processing device capabilities
		 * during initialization.
		 */
		if (caps == &hw->dev_caps.common_cap)
			DPRINTF("RDMA functionality is not available with "
			    "the current device configuration.\n");
	}
}

/**
 * ice_parse_vf_func_caps - Parse ICE_AQC_CAPS_VF function caps
 * @hw: pointer to the HW struct
 * @func_p: pointer to function capabilities structure
 * @cap: pointer to the capability element to parse
 *
 * Extract function capabilities for ICE_AQC_CAPS_VF.
 */
void
ice_parse_vf_func_caps(struct ice_hw *hw, struct ice_hw_func_caps *func_p,
		       struct ice_aqc_list_caps_elem *cap)
{
	uint32_t number = le32toh(cap->number);
	uint32_t logical_id = le32toh(cap->logical_id);

	func_p->num_allocd_vfs = number;
	func_p->vf_base_id = logical_id;
	DNPRINTF(ICE_DBG_INIT, "func caps: num_allocd_vfs = %d\n",
		  func_p->num_allocd_vfs);
	DNPRINTF(ICE_DBG_INIT, "func caps: vf_base_id = %d\n",
		  func_p->vf_base_id);
}

/**
 * ice_parse_vsi_func_caps - Parse ICE_AQC_CAPS_VSI function caps
 * @hw: pointer to the HW struct
 * @func_p: pointer to function capabilities structure
 * @cap: pointer to the capability element to parse
 *
 * Extract function capabilities for ICE_AQC_CAPS_VSI.
 */
void
ice_parse_vsi_func_caps(struct ice_hw *hw, struct ice_hw_func_caps *func_p,
			struct ice_aqc_list_caps_elem *cap)
{
	func_p->guar_num_vsi = ice_get_num_per_func(hw, ICE_MAX_VSI);
	DNPRINTF(ICE_DBG_INIT, "func caps: guar_num_vsi (fw) = %d\n",
	    le32toh(cap->number));
	DNPRINTF(ICE_DBG_INIT, "func caps: guar_num_vsi = %d\n",
		  func_p->guar_num_vsi);
}

/**
 * ice_parse_func_caps - Parse function capabilities
 * @hw: pointer to the HW struct
 * @func_p: pointer to function capabilities structure
 * @buf: buffer containing the function capability records
 * @cap_count: the number of capabilities
 *
 * Helper function to parse function (0x000A) capabilities list. For
 * capabilities shared between device and function, this relies on
 * ice_parse_common_caps.
 *
 * Loop through the list of provided capabilities and extract the relevant
 * data into the function capabilities structured.
 */
void
ice_parse_func_caps(struct ice_hw *hw, struct ice_hw_func_caps *func_p,
		    void *buf, uint32_t cap_count)
{
	struct ice_aqc_list_caps_elem *cap_resp;
	uint32_t i;

	cap_resp = (struct ice_aqc_list_caps_elem *)buf;

	memset(func_p, 0, sizeof(*func_p));

	for (i = 0; i < cap_count; i++) {
		uint16_t cap = le16toh(cap_resp[i].cap);
		bool found;

		found = ice_parse_common_caps(hw, &func_p->common_cap,
					      &cap_resp[i], "func caps");

		switch (cap) {
		case ICE_AQC_CAPS_VF:
			ice_parse_vf_func_caps(hw, func_p, &cap_resp[i]);
			break;
		case ICE_AQC_CAPS_VSI:
			ice_parse_vsi_func_caps(hw, func_p, &cap_resp[i]);
			break;
		default:
			/* Don't list common capabilities as unknown */
			if (!found)
				DNPRINTF(ICE_DBG_INIT, "func caps: unknown "
				    "capability[%d]: 0x%x\n", i, cap);
			break;
		}
	}

	ice_print_led_caps(hw, &func_p->common_cap, "func caps", true);
	ice_print_sdp_caps(hw, &func_p->common_cap, "func caps", true);

	ice_recalc_port_limited_caps(hw, &func_p->common_cap);
}

/**
 * ice_parse_valid_functions_cap - Parse ICE_AQC_CAPS_VALID_FUNCTIONS caps
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse ICE_AQC_CAPS_VALID_FUNCTIONS for device capabilities.
 */
void
ice_parse_valid_functions_cap(struct ice_hw *hw, struct ice_hw_dev_caps *dev_p,
			      struct ice_aqc_list_caps_elem *cap)
{
	uint32_t number = le32toh(cap->number);

	dev_p->num_funcs = ice_popcount32(number);
	DNPRINTF(ICE_DBG_INIT, "dev caps: num_funcs = %d\n",
		  dev_p->num_funcs);

}

/**
 * ice_parse_vf_dev_caps - Parse ICE_AQC_CAPS_VF device caps
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse ICE_AQC_CAPS_VF for device capabilities.
 */
void
ice_parse_vf_dev_caps(struct ice_hw *hw, struct ice_hw_dev_caps *dev_p,
		      struct ice_aqc_list_caps_elem *cap)
{
	uint32_t number = le32toh(cap->number);

	dev_p->num_vfs_exposed = number;
	DNPRINTF(ICE_DBG_INIT, "dev_caps: num_vfs_exposed = %d\n",
		  dev_p->num_vfs_exposed);
}

/**
 * ice_parse_vsi_dev_caps - Parse ICE_AQC_CAPS_VSI device caps
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse ICE_AQC_CAPS_VSI for device capabilities.
 */
void
ice_parse_vsi_dev_caps(struct ice_hw *hw, struct ice_hw_dev_caps *dev_p,
		       struct ice_aqc_list_caps_elem *cap)
{
	uint32_t number = le32toh(cap->number);

	dev_p->num_vsi_allocd_to_host = number;
	DNPRINTF(ICE_DBG_INIT, "dev caps: num_vsi_allocd_to_host = %d\n",
		  dev_p->num_vsi_allocd_to_host);
}

/**
 * ice_parse_nac_topo_dev_caps - Parse ICE_AQC_CAPS_NAC_TOPOLOGY cap
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse ICE_AQC_CAPS_NAC_TOPOLOGY for device capabilities.
 */
void
ice_parse_nac_topo_dev_caps(struct ice_hw *hw, struct ice_hw_dev_caps *dev_p,
			    struct ice_aqc_list_caps_elem *cap)
{
	dev_p->nac_topo.mode = le32toh(cap->number);
	dev_p->nac_topo.id = le32toh(cap->phys_id) & ICE_NAC_TOPO_ID_M;

	DPRINTF("PF is configured in %s mode with IP instance ID %d\n",
		 (dev_p->nac_topo.mode == 0) ? "primary" : "secondary",
		 dev_p->nac_topo.id);

	DNPRINTF(ICE_DBG_INIT, "dev caps: nac topology is_primary = %d\n",
		  !!(dev_p->nac_topo.mode & ICE_NAC_TOPO_PRIMARY_M));
	DNPRINTF(ICE_DBG_INIT, "dev caps: nac topology is_dual = %d\n",
		  !!(dev_p->nac_topo.mode & ICE_NAC_TOPO_DUAL_M));
	DNPRINTF(ICE_DBG_INIT, "dev caps: nac topology id = %d\n",
		  dev_p->nac_topo.id);
}

/**
 * ice_is_vsi_valid - check whether the VSI is valid or not
 * @hw: pointer to the HW struct
 * @vsi_handle: VSI handle
 *
 * check whether the VSI is valid or not
 */
bool
ice_is_vsi_valid(struct ice_hw *hw, uint16_t vsi_handle)
{
	return vsi_handle < ICE_MAX_VSI && hw->vsi_ctx[vsi_handle];
}

/**
 * ice_parse_sensor_reading_cap - Parse ICE_AQC_CAPS_SENSOR_READING cap
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse ICE_AQC_CAPS_SENSOR_READING for device capability for reading
 * enabled sensors.
 */
void
ice_parse_sensor_reading_cap(struct ice_hw *hw, struct ice_hw_dev_caps *dev_p,
			     struct ice_aqc_list_caps_elem *cap)
{
	dev_p->supported_sensors = le32toh(cap->number);

	DNPRINTF(ICE_DBG_INIT,
		  "dev caps: supported sensors (bitmap) = 0x%x\n",
		  dev_p->supported_sensors);
}

/**
 * ice_parse_dev_caps - Parse device capabilities
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @buf: buffer containing the device capability records
 * @cap_count: the number of capabilities
 *
 * Helper device to parse device (0x000B) capabilities list. For
 * capabilities shared between device and function, this relies on
 * ice_parse_common_caps.
 *
 * Loop through the list of provided capabilities and extract the relevant
 * data into the device capabilities structured.
 */
void
ice_parse_dev_caps(struct ice_hw *hw, struct ice_hw_dev_caps *dev_p,
		   void *buf, uint32_t cap_count)
{
	struct ice_aqc_list_caps_elem *cap_resp;
	uint32_t i;

	cap_resp = (struct ice_aqc_list_caps_elem *)buf;

	memset(dev_p, 0, sizeof(*dev_p));

	for (i = 0; i < cap_count; i++) {
		uint16_t cap = le16toh(cap_resp[i].cap);
		bool found;

		found = ice_parse_common_caps(hw, &dev_p->common_cap,
					      &cap_resp[i], "dev caps");

		switch (cap) {
		case ICE_AQC_CAPS_VALID_FUNCTIONS:
			ice_parse_valid_functions_cap(hw, dev_p, &cap_resp[i]);
			break;
		case ICE_AQC_CAPS_VF:
			ice_parse_vf_dev_caps(hw, dev_p, &cap_resp[i]);
			break;
		case ICE_AQC_CAPS_VSI:
			ice_parse_vsi_dev_caps(hw, dev_p, &cap_resp[i]);
			break;
		case ICE_AQC_CAPS_NAC_TOPOLOGY:
			ice_parse_nac_topo_dev_caps(hw, dev_p, &cap_resp[i]);
			break;
		case ICE_AQC_CAPS_SENSOR_READING:
			ice_parse_sensor_reading_cap(hw, dev_p, &cap_resp[i]);
			break;
		default:
			/* Don't list common capabilities as unknown */
			if (!found)
				DNPRINTF(ICE_DBG_INIT,
				    "dev caps: unknown capability[%d]: 0x%x\n",
				    i, cap);
			break;
		}
	}

	ice_print_led_caps(hw, &dev_p->common_cap, "dev caps", true);
	ice_print_sdp_caps(hw, &dev_p->common_cap, "dev caps", true);

	ice_recalc_port_limited_caps(hw, &dev_p->common_cap);
}

/**
 * ice_aq_list_caps - query function/device capabilities
 * @hw: pointer to the HW struct
 * @buf: a buffer to hold the capabilities
 * @buf_size: size of the buffer
 * @cap_count: if not NULL, set to the number of capabilities reported
 * @opc: capabilities type to discover, device or function
 * @cd: pointer to command details structure or NULL
 *
 * Get the function (0x000A) or device (0x000B) capabilities description from
 * firmware and store it in the buffer.
 *
 * If the cap_count pointer is not NULL, then it is set to the number of
 * capabilities firmware will report. Note that if the buffer size is too
 * small, it is possible the command will return ICE_AQ_ERR_ENOMEM. The
 * cap_count will still be updated in this case. It is recommended that the
 * buffer size be set to ICE_AQ_MAX_BUF_LEN (the largest possible buffer that
 * firmware could return) to avoid this.
 */
enum ice_status
ice_aq_list_caps(struct ice_hw *hw, void *buf, uint16_t buf_size,
    uint32_t *cap_count, enum ice_adminq_opc opc, struct ice_sq_cd *cd)
{
	struct ice_aqc_list_caps *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.get_cap;

	if (opc != ice_aqc_opc_list_func_caps &&
	    opc != ice_aqc_opc_list_dev_caps)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, opc);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);

	if (cap_count)
		*cap_count = le32toh(cmd->count);

	return status;
}

/**
 * ice_discover_dev_caps - Read and extract device capabilities
 * @hw: pointer to the hardware structure
 * @dev_caps: pointer to device capabilities structure
 *
 * Read the device capabilities and extract them into the dev_caps structure
 * for later use.
 */
enum ice_status
ice_discover_dev_caps(struct ice_hw *hw, struct ice_hw_dev_caps *dev_caps)
{
	enum ice_status status;
	uint32_t cap_count = 0;
	void *cbuf;

	cbuf = ice_malloc(hw, ICE_AQ_MAX_BUF_LEN);
	if (!cbuf)
		return ICE_ERR_NO_MEMORY;

	/* Although the driver doesn't know the number of capabilities the
	 * device will return, we can simply send a 4KB buffer, the maximum
	 * possible size that firmware can return.
	 */
	cap_count = ICE_AQ_MAX_BUF_LEN / sizeof(struct ice_aqc_list_caps_elem);

	status = ice_aq_list_caps(hw, cbuf, ICE_AQ_MAX_BUF_LEN, &cap_count,
				  ice_aqc_opc_list_dev_caps, NULL);
	if (!status)
		ice_parse_dev_caps(hw, dev_caps, cbuf, cap_count);
	ice_free(hw, cbuf);

	return status;
}

/**
 * ice_discover_func_caps - Read and extract function capabilities
 * @hw: pointer to the hardware structure
 * @func_caps: pointer to function capabilities structure
 *
 * Read the function capabilities and extract them into the func_caps structure
 * for later use.
 */
enum ice_status
ice_discover_func_caps(struct ice_hw *hw, struct ice_hw_func_caps *func_caps)
{
	enum ice_status status;
	uint32_t cap_count = 0;
	void *cbuf;

	cbuf = ice_malloc(hw, ICE_AQ_MAX_BUF_LEN);
	if (!cbuf)
		return ICE_ERR_NO_MEMORY;

	/* Although the driver doesn't know the number of capabilities the
	 * device will return, we can simply send a 4KB buffer, the maximum
	 * possible size that firmware can return.
	 */
	cap_count = ICE_AQ_MAX_BUF_LEN / sizeof(struct ice_aqc_list_caps_elem);

	status = ice_aq_list_caps(hw, cbuf, ICE_AQ_MAX_BUF_LEN, &cap_count,
				  ice_aqc_opc_list_func_caps, NULL);
	if (!status)
		ice_parse_func_caps(hw, func_caps, cbuf, cap_count);
	ice_free(hw, cbuf);

	return status;
}

/**
 * ice_set_safe_mode_caps - Override dev/func capabilities when in safe mode
 * @hw: pointer to the hardware structure
 */
void
ice_set_safe_mode_caps(struct ice_hw *hw)
{
	struct ice_hw_func_caps *func_caps = &hw->func_caps;
	struct ice_hw_dev_caps *dev_caps = &hw->dev_caps;
	struct ice_hw_common_caps cached_caps;
	uint32_t num_funcs;

	/* cache some func_caps values that should be restored after memset */
	cached_caps = func_caps->common_cap;

	/* unset func capabilities */
	memset(func_caps, 0, sizeof(*func_caps));

#define ICE_RESTORE_FUNC_CAP(name) \
	func_caps->common_cap.name = cached_caps.name

	/* restore cached values */
	ICE_RESTORE_FUNC_CAP(valid_functions);
	ICE_RESTORE_FUNC_CAP(txq_first_id);
	ICE_RESTORE_FUNC_CAP(rxq_first_id);
	ICE_RESTORE_FUNC_CAP(msix_vector_first_id);
	ICE_RESTORE_FUNC_CAP(max_mtu);
	ICE_RESTORE_FUNC_CAP(nvm_unified_update);

	/* one Tx and one Rx queue in safe mode */
	func_caps->common_cap.num_rxq = 1;
	func_caps->common_cap.num_txq = 1;

	/* two MSIX vectors, one for traffic and one for misc causes */
	func_caps->common_cap.num_msix_vectors = 2;
	func_caps->guar_num_vsi = 1;

	/* cache some dev_caps values that should be restored after memset */
	cached_caps = dev_caps->common_cap;
	num_funcs = dev_caps->num_funcs;

	/* unset dev capabilities */
	memset(dev_caps, 0, sizeof(*dev_caps));

#define ICE_RESTORE_DEV_CAP(name) \
	dev_caps->common_cap.name = cached_caps.name

	/* restore cached values */
	ICE_RESTORE_DEV_CAP(valid_functions);
	ICE_RESTORE_DEV_CAP(txq_first_id);
	ICE_RESTORE_DEV_CAP(rxq_first_id);
	ICE_RESTORE_DEV_CAP(msix_vector_first_id);
	ICE_RESTORE_DEV_CAP(max_mtu);
	ICE_RESTORE_DEV_CAP(nvm_unified_update);
	dev_caps->num_funcs = num_funcs;

	/* one Tx and one Rx queue per function in safe mode */
	dev_caps->common_cap.num_rxq = num_funcs;
	dev_caps->common_cap.num_txq = num_funcs;

	/* two MSIX vectors per function */
	dev_caps->common_cap.num_msix_vectors = 2 * num_funcs;
}

/**
 * ice_get_caps - get info about the HW
 * @hw: pointer to the hardware structure
 */
enum ice_status
ice_get_caps(struct ice_hw *hw)
{
	enum ice_status status;

	status = ice_discover_dev_caps(hw, &hw->dev_caps);
	if (status)
		return status;

	return ice_discover_func_caps(hw, &hw->func_caps);
}

/**
 * ice_aq_get_sw_cfg - get switch configuration
 * @hw: pointer to the hardware structure
 * @buf: pointer to the result buffer
 * @buf_size: length of the buffer available for response
 * @req_desc: pointer to requested descriptor
 * @num_elems: pointer to number of elements
 * @cd: pointer to command details structure or NULL
 *
 * Get switch configuration (0x0200) to be placed in buf.
 * This admin command returns information such as initial VSI/port number
 * and switch ID it belongs to.
 *
 * NOTE: *req_desc is both an input/output parameter.
 * The caller of this function first calls this function with *request_desc set
 * to 0. If the response from f/w has *req_desc set to 0, all the switch
 * configuration information has been returned; if non-zero (meaning not all
 * the information was returned), the caller should call this function again
 * with *req_desc set to the previous value returned by f/w to get the
 * next block of switch configuration information.
 *
 * *num_elems is output only parameter. This reflects the number of elements
 * in response buffer. The caller of this function to use *num_elems while
 * parsing the response buffer.
 */
enum ice_status
ice_aq_get_sw_cfg(struct ice_hw *hw, struct ice_aqc_get_sw_cfg_resp_elem *buf,
		  uint16_t buf_size, uint16_t *req_desc, uint16_t *num_elems,
		  struct ice_sq_cd *cd)
{
	struct ice_aqc_get_sw_cfg *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_sw_cfg);
	cmd = &desc.params.get_sw_conf;
	cmd->element = htole16(*req_desc);

	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status) {
		*req_desc = le16toh(cmd->element);
		*num_elems = le16toh(cmd->num_elems);
	}

	return status;
}

/* ice_init_port_info - Initialize port_info with switch configuration data
 * @pi: pointer to port_info
 * @vsi_port_num: VSI number or port number
 * @type: Type of switch element (port or VSI)
 * @swid: switch ID of the switch the element is attached to
 * @pf_vf_num: PF or VF number
 * @is_vf: true if the element is a VF, false otherwise
 */
void
ice_init_port_info(struct ice_port_info *pi, uint16_t vsi_port_num,
    uint8_t type, uint16_t swid, uint16_t pf_vf_num, bool is_vf)
{
	switch (type) {
	case ICE_AQC_GET_SW_CONF_RESP_PHYS_PORT:
		pi->lport = (uint8_t)(vsi_port_num & ICE_LPORT_MASK);
		pi->sw_id = swid;
		pi->pf_vf_num = pf_vf_num;
		pi->is_vf = is_vf;
		break;
	default:
		DNPRINTF(ICE_DBG_SW, "incorrect VSI/port type received\n");
		break;
	}
}

/* ice_get_initial_sw_cfg - Get initial port and default VSI data */
enum ice_status
ice_get_initial_sw_cfg(struct ice_hw *hw)
{
	struct ice_aqc_get_sw_cfg_resp_elem *rbuf;
	enum ice_status status;
	uint8_t num_total_ports;
	uint16_t req_desc = 0;
	uint16_t num_elems;
	uint8_t j = 0;
	uint16_t i;

	num_total_ports = 1;

	rbuf = (struct ice_aqc_get_sw_cfg_resp_elem *)
		ice_malloc(hw, ICE_SW_CFG_MAX_BUF_LEN);

	if (!rbuf)
		return ICE_ERR_NO_MEMORY;

	/* Multiple calls to ice_aq_get_sw_cfg may be required
	 * to get all the switch configuration information. The need
	 * for additional calls is indicated by ice_aq_get_sw_cfg
	 * writing a non-zero value in req_desc
	 */
	do {
		struct ice_aqc_get_sw_cfg_resp_elem *ele;

		status = ice_aq_get_sw_cfg(hw, rbuf, ICE_SW_CFG_MAX_BUF_LEN,
					   &req_desc, &num_elems, NULL);

		if (status)
			break;

		for (i = 0, ele = rbuf; i < num_elems; i++, ele++) {
			uint16_t pf_vf_num, swid, vsi_port_num;
			bool is_vf = false;
			uint8_t res_type;

			vsi_port_num = le16toh(ele->vsi_port_num) &
				ICE_AQC_GET_SW_CONF_RESP_VSI_PORT_NUM_M;

			pf_vf_num = le16toh(ele->pf_vf_num) &
				ICE_AQC_GET_SW_CONF_RESP_FUNC_NUM_M;

			swid = le16toh(ele->swid);

			if (le16toh(ele->pf_vf_num) &
			    ICE_AQC_GET_SW_CONF_RESP_IS_VF)
				is_vf = true;

			res_type = (uint8_t)(le16toh(ele->vsi_port_num) >>
					ICE_AQC_GET_SW_CONF_RESP_TYPE_S);

			switch (res_type) {
			case ICE_AQC_GET_SW_CONF_RESP_PHYS_PORT:
			case ICE_AQC_GET_SW_CONF_RESP_VIRT_PORT:
				if (j == num_total_ports) {
					DNPRINTF(ICE_DBG_SW,
					    "more ports than expected\n");
					status = ICE_ERR_CFG;
					goto out;
				}
				ice_init_port_info(hw->port_info,
						   vsi_port_num, res_type, swid,
						   pf_vf_num, is_vf);
				j++;
				break;
			default:
				break;
			}
		}
	} while (req_desc && !status);

out:
	ice_free(hw, rbuf);
	return status;
}

/**
 * ice_aq_query_sched_res - query scheduler resource
 * @hw: pointer to the HW struct
 * @buf_size: buffer size in bytes
 * @buf: pointer to buffer
 * @cd: pointer to command details structure or NULL
 *
 * Query scheduler resource allocation (0x0412)
 */
enum ice_status
ice_aq_query_sched_res(struct ice_hw *hw, uint16_t buf_size,
		       struct ice_aqc_query_txsched_res_resp *buf,
		       struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_query_sched_res);
	return ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
}

/**
 * ice_sched_query_res_alloc - query the FW for num of logical sched layers
 */
enum ice_status
ice_sched_query_res_alloc(struct ice_hw *hw)
{
	struct ice_aqc_query_txsched_res_resp *buf;
	enum ice_status status = ICE_SUCCESS;
	uint16_t max_sibl;
	uint8_t i;

	if (hw->layer_info)
		return status;

	buf = (struct ice_aqc_query_txsched_res_resp *)
		ice_malloc(hw, sizeof(*buf));
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	status = ice_aq_query_sched_res(hw, sizeof(*buf), buf, NULL);
	if (status)
		goto sched_query_out;

	hw->num_tx_sched_layers =
		(uint8_t)le16toh(buf->sched_props.logical_levels);
	hw->num_tx_sched_phys_layers =
		(uint8_t)le16toh(buf->sched_props.phys_levels);
	hw->flattened_layers = buf->sched_props.flattening_bitmap;
	hw->max_cgds = buf->sched_props.max_pf_cgds;

	/* max sibling group size of current layer refers to the max children
	 * of the below layer node.
	 * layer 1 node max children will be layer 2 max sibling group size
	 * layer 2 node max children will be layer 3 max sibling group size
	 * and so on. This array will be populated from root (index 0) to
	 * qgroup layer 7. Leaf node has no children.
	 */
	for (i = 0; i < hw->num_tx_sched_layers - 1; i++) {
		max_sibl = buf->layer_props[i + 1].max_sibl_grp_sz;
		hw->max_children[i] = le16toh(max_sibl);
	}

	hw->layer_info = (struct ice_aqc_layer_props *)
			 ice_memdup(hw, buf->layer_props,
				    (hw->num_tx_sched_layers *
				     sizeof(*hw->layer_info)));
	if (!hw->layer_info) {
		status = ICE_ERR_NO_MEMORY;
		goto sched_query_out;
	}

sched_query_out:
	ice_free(hw, buf);
	return status;
}

/**
 * ice_sched_get_psm_clk_freq - determine the PSM clock frequency
 * @hw: pointer to the HW struct
 *
 * Determine the PSM clock frequency and store in HW struct
 */
void
ice_sched_get_psm_clk_freq(struct ice_hw *hw)
{
	uint32_t val, clk_src;

	val = ICE_READ(hw, GLGEN_CLKSTAT_SRC);
	clk_src = (val & GLGEN_CLKSTAT_SRC_PSM_CLK_SRC_M) >>
		GLGEN_CLKSTAT_SRC_PSM_CLK_SRC_S;

	switch (clk_src) {
	case PSM_CLK_SRC_367_MHZ:
		hw->psm_clk_freq = ICE_PSM_CLK_367MHZ_IN_HZ;
		break;
	case PSM_CLK_SRC_416_MHZ:
		hw->psm_clk_freq = ICE_PSM_CLK_416MHZ_IN_HZ;
		break;
	case PSM_CLK_SRC_446_MHZ:
		hw->psm_clk_freq = ICE_PSM_CLK_446MHZ_IN_HZ;
		break;
	case PSM_CLK_SRC_390_MHZ:
		hw->psm_clk_freq = ICE_PSM_CLK_390MHZ_IN_HZ;
		break;

	/* default condition is not required as clk_src is restricted
	 * to a 2-bit value from GLGEN_CLKSTAT_SRC_PSM_CLK_SRC_M mask.
	 * The above switch statements cover the possible values of
	 * this variable.
	 */
	}
}

/**
 * ice_aq_get_dflt_topo - gets default scheduler topology
 * @hw: pointer to the HW struct
 * @lport: logical port number
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_branches: returns total number of queue to port branches
 * @cd: pointer to command details structure or NULL
 *
 * Get default scheduler topology (0x400)
 */
enum ice_status
ice_aq_get_dflt_topo(struct ice_hw *hw, uint8_t lport,
		     struct ice_aqc_get_topo_elem *buf, uint16_t buf_size,
		     uint8_t *num_branches, struct ice_sq_cd *cd)
{
	struct ice_aqc_get_topo *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.get_topo;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_dflt_topo);
	cmd->port_num = lport;
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && num_branches)
		*num_branches = cmd->num_branches;

	return status;
}

/**
 * ice_sched_add_root_node - Insert the Tx scheduler root node in SW DB
 * @pi: port information structure
 * @info: Scheduler element information from firmware
 *
 * This function inserts the root node of the scheduling tree topology
 * to the SW DB.
 */
enum ice_status
ice_sched_add_root_node(struct ice_port_info *pi,
			struct ice_aqc_txsched_elem_data *info)
{
	struct ice_sched_node *root;
	struct ice_hw *hw;

	if (!pi)
		return ICE_ERR_PARAM;

	hw = pi->hw;

	root = (struct ice_sched_node *)ice_malloc(hw, sizeof(*root));
	if (!root)
		return ICE_ERR_NO_MEMORY;

	root->children = (struct ice_sched_node **)
		ice_calloc(hw, hw->max_children[0], sizeof(*root->children));
	if (!root->children) {
		ice_free(hw, root);
		return ICE_ERR_NO_MEMORY;
	}

	memcpy(&root->info, info, sizeof(*info));
	pi->root = root;
	return ICE_SUCCESS;
}

/**
 * ice_sched_find_node_by_teid - Find the Tx scheduler node in SW DB
 * @start_node: pointer to the starting ice_sched_node struct in a sub-tree
 * @teid: node TEID to search
 *
 * This function searches for a node matching the TEID in the scheduling tree
 * from the SW DB. The search is recursive and is restricted by the number of
 * layers it has searched through; stopping at the max supported layer.
 *
 * This function needs to be called when holding the port_info->sched_lock
 */
struct ice_sched_node *
ice_sched_find_node_by_teid(struct ice_sched_node *start_node, uint32_t teid)
{
	uint16_t i;

	/* The TEID is same as that of the start_node */
	if (ICE_TXSCHED_GET_NODE_TEID(start_node) == teid)
		return start_node;

	/* The node has no children or is at the max layer */
	if (!start_node->num_children ||
	    start_node->tx_sched_layer >= ICE_AQC_TOPO_MAX_LEVEL_NUM ||
	    start_node->info.data.elem_type == ICE_AQC_ELEM_TYPE_LEAF)
		return NULL;

	/* Check if TEID matches to any of the children nodes */
	for (i = 0; i < start_node->num_children; i++)
		if (ICE_TXSCHED_GET_NODE_TEID(start_node->children[i]) == teid)
			return start_node->children[i];

	/* Search within each child's sub-tree */
	for (i = 0; i < start_node->num_children; i++) {
		struct ice_sched_node *tmp;

		tmp = ice_sched_find_node_by_teid(start_node->children[i],
						  teid);
		if (tmp)
			return tmp;
	}

	return NULL;
}

/**
 * ice_sched_get_tc_node - get pointer to TC node
 * @pi: port information structure
 * @tc: TC number
 *
 * This function returns the TC node pointer
 */
struct ice_sched_node *
ice_sched_get_tc_node(struct ice_port_info *pi, uint8_t tc)
{
	uint8_t i;

	if (!pi || !pi->root)
		return NULL;
	for (i = 0; i < pi->root->num_children; i++)
		if (pi->root->children[i]->tc_num == tc)
			return pi->root->children[i];
	return NULL;
}

/**
 * ice_aqc_send_sched_elem_cmd - send scheduling elements cmd
 * @hw: pointer to the HW struct
 * @cmd_opc: cmd opcode
 * @elems_req: number of elements to request
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_resp: returns total number of elements response
 * @cd: pointer to command details structure or NULL
 *
 * This function sends a scheduling elements cmd (cmd_opc)
 */
enum ice_status
ice_aqc_send_sched_elem_cmd(struct ice_hw *hw, enum ice_adminq_opc cmd_opc,
			    uint16_t elems_req, void *buf, uint16_t buf_size,
			    uint16_t *elems_resp, struct ice_sq_cd *cd)
{
	struct ice_aqc_sched_elem_cmd *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.sched_elem_cmd;
	ice_fill_dflt_direct_cmd_desc(&desc, cmd_opc);
	cmd->num_elem_req = htole16(elems_req);
	desc.flags |= htole16(ICE_AQ_FLAG_RD);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && elems_resp)
		*elems_resp = le16toh(cmd->num_elem_resp);

	return status;
}

/**
 * ice_aq_query_sched_elems - query scheduler elements
 * @hw: pointer to the HW struct
 * @elems_req: number of elements to query
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements returned
 * @cd: pointer to command details structure or NULL
 *
 * Query scheduling elements (0x0404)
 */
enum ice_status
ice_aq_query_sched_elems(struct ice_hw *hw, uint16_t elems_req,
    struct ice_aqc_txsched_elem_data *buf, uint16_t buf_size,
    uint16_t *elems_ret, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_get_sched_elems,
					   elems_req, (void *)buf, buf_size,
					   elems_ret, cd);
}

/**
 * ice_sched_query_elem - query element information from HW
 * @hw: pointer to the HW struct
 * @node_teid: node TEID to be queried
 * @buf: buffer to element information
 *
 * This function queries HW element information
 */
enum ice_status
ice_sched_query_elem(struct ice_hw *hw, uint32_t node_teid,
		     struct ice_aqc_txsched_elem_data *buf)
{
	uint16_t buf_size, num_elem_ret = 0;
	enum ice_status status;

	buf_size = sizeof(*buf);
	memset(buf, 0, buf_size);
	buf->node_teid = htole32(node_teid);
	status = ice_aq_query_sched_elems(hw, 1, buf, buf_size, &num_elem_ret,
					  NULL);
	if (status != ICE_SUCCESS || num_elem_ret != 1)
		DNPRINTF(ICE_DBG_SCHED, "query element failed\n");
	return status;
}

/**
 * ice_sched_add_node - Insert the Tx scheduler node in SW DB
 * @pi: port information structure
 * @layer: Scheduler layer of the node
 * @info: Scheduler element information from firmware
 * @prealloc_node: preallocated ice_sched_node struct for SW DB
 *
 * This function inserts a scheduler node to the SW DB.
 */
enum ice_status
ice_sched_add_node(struct ice_port_info *pi, uint8_t layer,
		   struct ice_aqc_txsched_elem_data *info,
		   struct ice_sched_node *prealloc_node)
{
	struct ice_aqc_txsched_elem_data elem;
	struct ice_sched_node *parent;
	struct ice_sched_node *node;
	enum ice_status status;
	struct ice_hw *hw;

	if (!pi)
		return ICE_ERR_PARAM;

	hw = pi->hw;

	/* A valid parent node should be there */
	parent = ice_sched_find_node_by_teid(pi->root,
					     le32toh(info->parent_teid));
	if (!parent) {
		DNPRINTF(ICE_DBG_SCHED,
		    "Parent Node not found for parent_teid=0x%x\n",
		    le32toh(info->parent_teid));
		return ICE_ERR_PARAM;
	}

	/* query the current node information from FW before adding it
	 * to the SW DB
	 */
	status = ice_sched_query_elem(hw, le32toh(info->node_teid), &elem);
	if (status)
		return status;

	if (prealloc_node)
		node = prealloc_node;
	else
		node = (struct ice_sched_node *)ice_malloc(hw, sizeof(*node));
	if (!node)
		return ICE_ERR_NO_MEMORY;
	if (hw->max_children[layer]) {
		node->children = (struct ice_sched_node **)
			ice_calloc(hw, hw->max_children[layer],
				   sizeof(*node->children));
		if (!node->children) {
			ice_free(hw, node);
			return ICE_ERR_NO_MEMORY;
		}
	}

	node->in_use = true;
	node->parent = parent;
	node->tx_sched_layer = layer;
	parent->children[parent->num_children++] = node;
	node->info = elem;
	return ICE_SUCCESS;
}

/**
 * ice_aq_delete_sched_elems - delete scheduler elements
 * @hw: pointer to the HW struct
 * @grps_req: number of groups to delete
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @grps_del: returns total number of elements deleted
 * @cd: pointer to command details structure or NULL
 *
 * Delete scheduling elements (0x040F)
 */
enum ice_status
ice_aq_delete_sched_elems(struct ice_hw *hw, uint16_t grps_req,
    struct ice_aqc_delete_elem *buf, uint16_t buf_size, uint16_t *grps_del,
    struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_delete_sched_elems,
					   grps_req, (void *)buf, buf_size,
					   grps_del, cd);
}

/**
 * ice_sched_remove_elems - remove nodes from HW
 * @hw: pointer to the HW struct
 * @parent: pointer to the parent node
 * @num_nodes: number of nodes
 * @node_teids: array of node teids to be deleted
 *
 * This function remove nodes from HW
 */
enum ice_status
ice_sched_remove_elems(struct ice_hw *hw, struct ice_sched_node *parent,
		       uint16_t num_nodes, uint32_t *node_teids)
{
	struct ice_aqc_delete_elem *buf;
	uint16_t i, num_groups_removed = 0;
	enum ice_status status;
	uint16_t buf_size;

	buf_size = ice_struct_size(buf, teid, num_nodes);
	buf = (struct ice_aqc_delete_elem *)ice_malloc(hw, buf_size);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	buf->hdr.parent_teid = parent->info.node_teid;
	buf->hdr.num_elems = htole16(num_nodes);
	for (i = 0; i < num_nodes; i++)
		buf->teid[i] = htole32(node_teids[i]);

	status = ice_aq_delete_sched_elems(hw, 1, buf, buf_size,
					   &num_groups_removed, NULL);
	if (status != ICE_SUCCESS || num_groups_removed != 1)
		DNPRINTF(ICE_DBG_SCHED, "remove node failed FW error %d\n",
			  hw->adminq.sq_last_status);

	ice_free(hw, buf);
	return status;
}

/**
 * ice_sched_get_first_node - get the first node of the given layer
 * @pi: port information structure
 * @parent: pointer the base node of the subtree
 * @layer: layer number
 *
 * This function retrieves the first node of the given layer from the subtree
 */
struct ice_sched_node *
ice_sched_get_first_node(struct ice_port_info *pi,
			 struct ice_sched_node *parent, uint8_t layer)
{
	return pi->sib_head[parent->tc_num][layer];
}

/**
 * ice_free_sched_node - Free a Tx scheduler node from SW DB
 * @pi: port information structure
 * @node: pointer to the ice_sched_node struct
 *
 * This function frees up a node from SW DB as well as from HW
 *
 * This function needs to be called with the port_info->sched_lock held
 */
void
ice_free_sched_node(struct ice_port_info *pi, struct ice_sched_node *node)
{
	struct ice_sched_node *parent;
	struct ice_hw *hw = pi->hw;
	uint8_t i, j;

	/* Free the children before freeing up the parent node
	 * The parent array is updated below and that shifts the nodes
	 * in the array. So always pick the first child if num children > 0
	 */
	while (node->num_children)
		ice_free_sched_node(pi, node->children[0]);

	/* Leaf, TC and root nodes can't be deleted by SW */
	if (node->tx_sched_layer >= hw->sw_entry_point_layer &&
	    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_TC &&
	    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_ROOT_PORT &&
	    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_LEAF) {
		uint32_t teid = le32toh(node->info.node_teid);

		ice_sched_remove_elems(hw, node->parent, 1, &teid);
	}
	parent = node->parent;
	/* root has no parent */
	if (parent) {
		struct ice_sched_node *p;

		/* update the parent */
		for (i = 0; i < parent->num_children; i++)
			if (parent->children[i] == node) {
				for (j = i + 1; j < parent->num_children; j++)
					parent->children[j - 1] =
						parent->children[j];
				parent->num_children--;
				break;
			}

		p = ice_sched_get_first_node(pi, node, node->tx_sched_layer);
		while (p) {
			if (p->sibling == node) {
				p->sibling = node->sibling;
				break;
			}
			p = p->sibling;
		}

		/* update the sibling head if head is getting removed */
		if (pi->sib_head[node->tc_num][node->tx_sched_layer] == node)
			pi->sib_head[node->tc_num][node->tx_sched_layer] =
				node->sibling;
	}

	/* leaf nodes have no children */
	if (node->children)
		ice_free(hw, node->children);
	ice_free(hw, node);
}

/**
 * ice_rm_dflt_leaf_node - remove the default leaf node in the tree
 * @pi: port information structure
 *
 * This function removes the leaf node that was created by the FW
 * during initialization
 */
void
ice_rm_dflt_leaf_node(struct ice_port_info *pi)
{
	struct ice_sched_node *node;

	node = pi->root;
	while (node) {
		if (!node->num_children)
			break;
		node = node->children[0];
	}
	if (node && node->info.data.elem_type == ICE_AQC_ELEM_TYPE_LEAF) {
		uint32_t teid = le32toh(node->info.node_teid);
		enum ice_status status;

		/* remove the default leaf node */
		status = ice_sched_remove_elems(pi->hw, node->parent, 1, &teid);
		if (!status)
			ice_free_sched_node(pi, node);
	}
}

/**
 * ice_sched_rm_dflt_nodes - free the default nodes in the tree
 * @pi: port information structure
 *
 * This function frees all the nodes except root and TC that were created by
 * the FW during initialization
 */
void
ice_sched_rm_dflt_nodes(struct ice_port_info *pi)
{
	struct ice_sched_node *node;

	ice_rm_dflt_leaf_node(pi);

	/* remove the default nodes except TC and root nodes */
	node = pi->root;
	while (node) {
		if (node->tx_sched_layer >= pi->hw->sw_entry_point_layer &&
		    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_TC &&
		    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_ROOT_PORT) {
			ice_free_sched_node(pi, node);
			break;
		}

		if (!node->num_children)
			break;
		node = node->children[0];
	}
}

/**
 * ice_sched_init_port - Initialize scheduler by querying information from FW
 * @pi: port info structure for the tree to cleanup
 *
 * This function is the initial call to find the total number of Tx scheduler
 * resources, default topology created by firmware and storing the information
 * in SW DB.
 */
enum ice_status
ice_sched_init_port(struct ice_port_info *pi)
{
	struct ice_aqc_get_topo_elem *buf;
	enum ice_status status;
	struct ice_hw *hw;
	uint8_t num_branches;
	uint16_t num_elems;
	uint8_t i, j;

	if (!pi)
		return ICE_ERR_PARAM;
	hw = pi->hw;

	/* Query the Default Topology from FW */
	buf = (struct ice_aqc_get_topo_elem *)ice_malloc(hw,
							 ICE_AQ_MAX_BUF_LEN);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	/* Query default scheduling tree topology */
	status = ice_aq_get_dflt_topo(hw, pi->lport, buf, ICE_AQ_MAX_BUF_LEN,
				      &num_branches, NULL);
	if (status)
		goto err_init_port;

	/* num_branches should be between 1-8 */
	if (num_branches < 1 || num_branches > ICE_TXSCHED_MAX_BRANCHES) {
		DNPRINTF(ICE_DBG_SCHED, "num_branches unexpected %d\n",
		    num_branches);
		status = ICE_ERR_PARAM;
		goto err_init_port;
	}

	/* get the number of elements on the default/first branch */
	num_elems = le16toh(buf[0].hdr.num_elems);

	/* num_elems should always be between 1-9 */
	if (num_elems < 1 || num_elems > ICE_AQC_TOPO_MAX_LEVEL_NUM) {
		DNPRINTF(ICE_DBG_SCHED, "num_elems unexpected %d\n", num_elems);
		status = ICE_ERR_PARAM;
		goto err_init_port;
	}

	/* If the last node is a leaf node then the index of the queue group
	 * layer is two less than the number of elements.
	 */
	if (num_elems > 2 && buf[0].generic[num_elems - 1].data.elem_type ==
	    ICE_AQC_ELEM_TYPE_LEAF)
		pi->last_node_teid =
			le32toh(buf[0].generic[num_elems - 2].node_teid);
	else
		pi->last_node_teid =
			le32toh(buf[0].generic[num_elems - 1].node_teid);

	/* Insert the Tx Sched root node */
	status = ice_sched_add_root_node(pi, &buf[0].generic[0]);
	if (status)
		goto err_init_port;

	/* Parse the default tree and cache the information */
	for (i = 0; i < num_branches; i++) {
		num_elems = le16toh(buf[i].hdr.num_elems);

		/* Skip root element as already inserted */
		for (j = 1; j < num_elems; j++) {
			/* update the sw entry point */
			if (buf[0].generic[j].data.elem_type ==
			    ICE_AQC_ELEM_TYPE_ENTRY_POINT)
				hw->sw_entry_point_layer = j;

			status = ice_sched_add_node(pi, j,
			    &buf[i].generic[j], NULL);
			if (status)
				goto err_init_port;
		}
	}

	/* Remove the default nodes. */
	if (pi->root)
		ice_sched_rm_dflt_nodes(pi);

	/* initialize the port for handling the scheduler tree */
	pi->port_state = ICE_SCHED_PORT_STATE_READY;
	ice_init_lock(&pi->sched_lock);
	for (i = 0; i < ICE_AQC_TOPO_MAX_LEVEL_NUM; i++)
		TAILQ_INIT(&hw->rl_prof_list[i]);

err_init_port:
	if (status && pi->root) {
		ice_free_sched_node(pi, pi->root);
		pi->root = NULL;
	}

	ice_free(hw, buf);
	return status;
}

/**
 * ice_aq_rl_profile - performs a rate limiting task
 * @hw: pointer to the HW struct
 * @opcode: opcode for add, query, or remove profile(s)
 * @num_profiles: the number of profiles
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_processed: number of processed add or remove profile(s) to return
 * @cd: pointer to command details structure
 *
 * RL profile function to add, query, or remove profile(s)
 */
enum ice_status
ice_aq_rl_profile(struct ice_hw *hw, enum ice_adminq_opc opcode,
    uint16_t num_profiles, struct ice_aqc_rl_profile_elem *buf,
    uint16_t buf_size, uint16_t *num_processed, struct ice_sq_cd *cd)
{
	struct ice_aqc_rl_profile *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.rl_profile;

	ice_fill_dflt_direct_cmd_desc(&desc, opcode);
	desc.flags |= htole16(ICE_AQ_FLAG_RD);
	cmd->num_profiles = htole16(num_profiles);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && num_processed)
		*num_processed = le16toh(cmd->num_processed);
	return status;
}

/**
 * ice_aq_remove_rl_profile - removes RL profile(s)
 * @hw: pointer to the HW struct
 * @num_profiles: the number of profile(s) to remove
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_profiles_removed: total number of profiles removed to return
 * @cd: pointer to command details structure or NULL
 *
 * Remove RL profile (0x0415)
 */
enum ice_status
ice_aq_remove_rl_profile(struct ice_hw *hw, uint16_t num_profiles,
			 struct ice_aqc_rl_profile_elem *buf, uint16_t buf_size,
			 uint16_t *num_profiles_removed, struct ice_sq_cd *cd)
{
	return ice_aq_rl_profile(hw, ice_aqc_opc_remove_rl_profiles,
				 num_profiles, buf, buf_size,
				 num_profiles_removed, cd);
}

/**
 * ice_sched_del_rl_profile - remove RL profile
 * @hw: pointer to the HW struct
 * @rl_head: list head
 * @rl_info: rate limit profile information
 *
 * If the profile ID is not referenced anymore, it removes profile ID with
 * its associated parameters from HW DB,and locally. The caller needs to
 * hold scheduler lock.
 */
enum ice_status
ice_sched_del_rl_profile(struct ice_hw *hw,
    struct ice_rl_prof_list_head *list_head,
    struct ice_aqc_rl_profile_info *rl_info)
{
	struct ice_aqc_rl_profile_elem *buf;
	uint16_t num_profiles_removed;
	enum ice_status status;
	uint16_t num_profiles = 1;

	if (rl_info->prof_id_ref != 0)
		return ICE_ERR_IN_USE;

	/* Safe to remove profile ID */
	buf = &rl_info->profile;
	status = ice_aq_remove_rl_profile(hw, num_profiles, buf, sizeof(*buf),
					  &num_profiles_removed, NULL);
	if (status || num_profiles_removed != num_profiles)
		return ICE_ERR_CFG;

	/* Delete stale entry now */
	TAILQ_REMOVE(list_head, rl_info, list_entry);
	ice_free(hw, rl_info);
	return status;
}

/**
 * ice_sched_clear_rl_prof - clears RL prof entries
 * @pi: port information structure
 *
 * This function removes all RL profile from HW as well as from SW DB.
 */
void
ice_sched_clear_rl_prof(struct ice_port_info *pi)
{
	uint16_t ln;
	struct ice_hw *hw = pi->hw;

	for (ln = 0; ln < hw->num_tx_sched_layers; ln++) {
		struct ice_aqc_rl_profile_info *rl_prof_elem;
		struct ice_aqc_rl_profile_info *rl_prof_tmp;

		TAILQ_FOREACH_SAFE(rl_prof_elem, &hw->rl_prof_list[ln],
		    list_entry, rl_prof_tmp) {
			enum ice_status status;

			rl_prof_elem->prof_id_ref = 0;
			status = ice_sched_del_rl_profile(hw,
			    &hw->rl_prof_list[ln], rl_prof_elem);
			if (status) {
				DNPRINTF(ICE_DBG_SCHED,
				    "Remove rl profile failed\n");
				/* On error, free mem required */
				TAILQ_REMOVE(&hw->rl_prof_list[ln],
				    rl_prof_elem, list_entry);
				ice_free(hw, rl_prof_elem);
			}
		}
	}
}

/**
 * ice_sched_clear_tx_topo - clears the scheduler tree nodes
 * @pi: port information structure
 *
 * This function removes all the nodes from HW as well as from SW DB.
 */
void
ice_sched_clear_tx_topo(struct ice_port_info *pi)
{
	if (!pi)
		return;
	/* remove RL profiles related lists */
	ice_sched_clear_rl_prof(pi);
	if (pi->root) {
		ice_free_sched_node(pi, pi->root);
		pi->root = NULL;
	}
}

/**
 * ice_sched_clear_port - clear the scheduler elements from SW DB for a port
 * @pi: port information structure
 *
 * Cleanup scheduling elements from SW DB
 */
void
ice_sched_clear_port(struct ice_port_info *pi)
{
	if (!pi || pi->port_state != ICE_SCHED_PORT_STATE_READY)
		return;

	pi->port_state = ICE_SCHED_PORT_STATE_INIT;
#if 0
	ice_acquire_lock(&pi->sched_lock);
#endif
	ice_sched_clear_tx_topo(pi);
#if 0
	ice_release_lock(&pi->sched_lock);
	ice_destroy_lock(&pi->sched_lock);
#endif
}

/**
 * ice_sched_cleanup_all - cleanup scheduler elements from SW DB for all ports
 * @hw: pointer to the HW struct
 *
 * Cleanup scheduling elements from SW DB for all the ports
 */
void
ice_sched_cleanup_all(struct ice_hw *hw)
{
	if (!hw)
		return;

	if (hw->layer_info) {
		ice_free(hw, hw->layer_info);
		hw->layer_info = NULL;
	}

	ice_sched_clear_port(hw->port_info);

	hw->num_tx_sched_layers = 0;
	hw->num_tx_sched_phys_layers = 0;
	hw->flattened_layers = 0;
	hw->max_cgds = 0;
}

/**
 * ice_is_fw_min_ver
 * @hw: pointer to the hardware structure
 * @branch: branch version
 * @maj: major version
 * @min: minor version
 * @patch: patch version
 *
 * Checks if the firmware is minimum version
 */
bool
ice_is_fw_min_ver(struct ice_hw *hw, uint8_t branch, uint8_t maj, uint8_t min,
    uint8_t patch)
{
	if (hw->fw_branch == branch) {
		if (hw->fw_maj_ver > maj)
			return true;
		if (hw->fw_maj_ver == maj) {
			if (hw->fw_min_ver > min)
				return true;
			if (hw->fw_min_ver == min && hw->fw_patch >= patch)
				return true;
		}
	}

	return false;
}

/**
 * ice_is_fw_api_min_ver
 * @hw: pointer to the hardware structure
 * @maj: major version
 * @min: minor version
 * @patch: patch version
 *
 * Checks if the firmware is minimum version
 */
bool
ice_is_fw_api_min_ver(struct ice_hw *hw, uint8_t maj, uint8_t min,
    uint8_t patch)
{
	if (hw->api_maj_ver == maj) {
		if (hw->api_min_ver > min)
			return true;
		if (hw->api_min_ver == min && hw->api_patch >= patch)
			return true;
	} else if (hw->api_maj_ver > maj) {
		return true;
	}

	return false;
}

/**
 * ice_fw_supports_report_dflt_cfg
 * @hw: pointer to the hardware structure
 *
 * Checks if the firmware supports report default configuration
 */
bool
ice_fw_supports_report_dflt_cfg(struct ice_hw *hw)
{
	return ice_is_fw_api_min_ver(hw, ICE_FW_API_REPORT_DFLT_CFG_MAJ,
				     ICE_FW_API_REPORT_DFLT_CFG_MIN,
				     ICE_FW_API_REPORT_DFLT_CFG_PATCH);
}

/**
 * ice_fw_supports_link_override
 * @hw: pointer to the hardware structure
 *
 * Checks if the firmware supports link override
 */
bool
ice_fw_supports_link_override(struct ice_hw *hw)
{
	return ice_is_fw_api_min_ver(hw, ICE_FW_API_LINK_OVERRIDE_MAJ,
				     ICE_FW_API_LINK_OVERRIDE_MIN,
				     ICE_FW_API_LINK_OVERRIDE_PATCH);
}

#define ICE_PF_RESET_WAIT_COUNT	500

#define ice_arr_elem_idx(idx, val)	[(idx)] = (val)

#ifdef ICE_DEBUG
static const char * const ice_link_mode_str_low[] = {
	ice_arr_elem_idx(0, "100BASE_TX"),
	ice_arr_elem_idx(1, "100M_SGMII"),
	ice_arr_elem_idx(2, "1000BASE_T"),
	ice_arr_elem_idx(3, "1000BASE_SX"),
	ice_arr_elem_idx(4, "1000BASE_LX"),
	ice_arr_elem_idx(5, "1000BASE_KX"),
	ice_arr_elem_idx(6, "1G_SGMII"),
	ice_arr_elem_idx(7, "2500BASE_T"),
	ice_arr_elem_idx(8, "2500BASE_X"),
	ice_arr_elem_idx(9, "2500BASE_KX"),
	ice_arr_elem_idx(10, "5GBASE_T"),
	ice_arr_elem_idx(11, "5GBASE_KR"),
	ice_arr_elem_idx(12, "10GBASE_T"),
	ice_arr_elem_idx(13, "10G_SFI_DA"),
	ice_arr_elem_idx(14, "10GBASE_SR"),
	ice_arr_elem_idx(15, "10GBASE_LR"),
	ice_arr_elem_idx(16, "10GBASE_KR_CR1"),
	ice_arr_elem_idx(17, "10G_SFI_AOC_ACC"),
	ice_arr_elem_idx(18, "10G_SFI_C2C"),
	ice_arr_elem_idx(19, "25GBASE_T"),
	ice_arr_elem_idx(20, "25GBASE_CR"),
	ice_arr_elem_idx(21, "25GBASE_CR_S"),
	ice_arr_elem_idx(22, "25GBASE_CR1"),
	ice_arr_elem_idx(23, "25GBASE_SR"),
	ice_arr_elem_idx(24, "25GBASE_LR"),
	ice_arr_elem_idx(25, "25GBASE_KR"),
	ice_arr_elem_idx(26, "25GBASE_KR_S"),
	ice_arr_elem_idx(27, "25GBASE_KR1"),
	ice_arr_elem_idx(28, "25G_AUI_AOC_ACC"),
	ice_arr_elem_idx(29, "25G_AUI_C2C"),
	ice_arr_elem_idx(30, "40GBASE_CR4"),
	ice_arr_elem_idx(31, "40GBASE_SR4"),
	ice_arr_elem_idx(32, "40GBASE_LR4"),
	ice_arr_elem_idx(33, "40GBASE_KR4"),
	ice_arr_elem_idx(34, "40G_XLAUI_AOC_ACC"),
	ice_arr_elem_idx(35, "40G_XLAUI"),
	ice_arr_elem_idx(36, "50GBASE_CR2"),
	ice_arr_elem_idx(37, "50GBASE_SR2"),
	ice_arr_elem_idx(38, "50GBASE_LR2"),
	ice_arr_elem_idx(39, "50GBASE_KR2"),
	ice_arr_elem_idx(40, "50G_LAUI2_AOC_ACC"),
	ice_arr_elem_idx(41, "50G_LAUI2"),
	ice_arr_elem_idx(42, "50G_AUI2_AOC_ACC"),
	ice_arr_elem_idx(43, "50G_AUI2"),
	ice_arr_elem_idx(44, "50GBASE_CP"),
	ice_arr_elem_idx(45, "50GBASE_SR"),
	ice_arr_elem_idx(46, "50GBASE_FR"),
	ice_arr_elem_idx(47, "50GBASE_LR"),
	ice_arr_elem_idx(48, "50GBASE_KR_PAM4"),
	ice_arr_elem_idx(49, "50G_AUI1_AOC_ACC"),
	ice_arr_elem_idx(50, "50G_AUI1"),
	ice_arr_elem_idx(51, "100GBASE_CR4"),
	ice_arr_elem_idx(52, "100GBASE_SR4"),
	ice_arr_elem_idx(53, "100GBASE_LR4"),
	ice_arr_elem_idx(54, "100GBASE_KR4"),
	ice_arr_elem_idx(55, "100G_CAUI4_AOC_ACC"),
	ice_arr_elem_idx(56, "100G_CAUI4"),
	ice_arr_elem_idx(57, "100G_AUI4_AOC_ACC"),
	ice_arr_elem_idx(58, "100G_AUI4"),
	ice_arr_elem_idx(59, "100GBASE_CR_PAM4"),
	ice_arr_elem_idx(60, "100GBASE_KR_PAM4"),
	ice_arr_elem_idx(61, "100GBASE_CP2"),
	ice_arr_elem_idx(62, "100GBASE_SR2"),
	ice_arr_elem_idx(63, "100GBASE_DR"),
};

static const char * const ice_link_mode_str_high[] = {
	ice_arr_elem_idx(0, "100GBASE_KR2_PAM4"),
	ice_arr_elem_idx(1, "100G_CAUI2_AOC_ACC"),
	ice_arr_elem_idx(2, "100G_CAUI2"),
	ice_arr_elem_idx(3, "100G_AUI2_AOC_ACC"),
	ice_arr_elem_idx(4, "100G_AUI2"),
};
#endif

/**
 * ice_dump_phy_type - helper function to dump phy_type
 * @hw: pointer to the HW structure
 * @low: 64 bit value for phy_type_low
 * @high: 64 bit value for phy_type_high
 * @prefix: prefix string to differentiate multiple dumps
 */
void
ice_dump_phy_type(struct ice_hw *hw, uint64_t low, uint64_t high,
    const char *prefix)
{
#ifdef ICE_DEBUG
	uint32_t i;

	DNPRINTF(ICE_DBG_PHY, "%s: phy_type_low: 0x%016llx\n", prefix,
		  (unsigned long long)low);

	for (i = 0; i < nitems(ice_link_mode_str_low); i++) {
		if (low & (1ULL << i))
			DNPRINTF(ICE_DBG_PHY, "%s:   bit(%d): %s\n",
				  prefix, i, ice_link_mode_str_low[i]);
	}

	DNPRINTF(ICE_DBG_PHY, "%s: phy_type_high: 0x%016llx\n", prefix,
		  (unsigned long long)high);

	for (i = 0; i < nitems(ice_link_mode_str_high); i++) {
		if (high & (1ULL << i))
			DNPRINTF(ICE_DBG_PHY, "%s:   bit(%d): %s\n",
				  prefix, i, ice_link_mode_str_high[i]);
	}
#endif
}

/**
 * ice_phy_maps_to_media
 * @phy_type_low: PHY type low bits
 * @phy_type_high: PHY type high bits
 * @media_mask_low: media type PHY type low bitmask
 * @media_mask_high: media type PHY type high bitmask
 *
 * Return true if PHY type [low|high] bits are only of media type PHY types
 * [low|high] bitmask.
 */
bool
ice_phy_maps_to_media(uint64_t phy_type_low, uint64_t phy_type_high,
		      uint64_t media_mask_low, uint64_t media_mask_high)
{
	/* check if a PHY type exist for media type */
	if (!(phy_type_low & media_mask_low ||
	      phy_type_high & media_mask_high))
		return false;

	/* check that PHY types are only of media type */
	if (!(phy_type_low & ~media_mask_low) &&
	    !(phy_type_high & ~media_mask_high))
		return true;

	return false;
}

/**
 * ice_set_media_type - Sets media type
 * @pi: port information structure
 *
 * Set ice_port_info PHY media type based on PHY type. This should be called
 * from Get PHY caps with media.
 */
void
ice_set_media_type(struct ice_port_info *pi)
{
	enum ice_media_type *media_type;
	uint64_t phy_type_high, phy_type_low;

	phy_type_high = pi->phy.phy_type_high;
	phy_type_low = pi->phy.phy_type_low;
	media_type = &pi->phy.media_type;

	/* if no media, then media type is NONE */
	if (!(pi->phy.link_info.link_info & ICE_AQ_MEDIA_AVAILABLE))
		*media_type = ICE_MEDIA_NONE;
	/* else if PHY types are only BASE-T, then media type is BASET */
	else if (ice_phy_maps_to_media(phy_type_low, phy_type_high,
				       ICE_MEDIA_BASET_PHY_TYPE_LOW_M, 0))
		*media_type = ICE_MEDIA_BASET;
	/* else if any PHY type is BACKPLANE, then media type is BACKPLANE */
	else if (phy_type_low & ICE_MEDIA_BP_PHY_TYPE_LOW_M ||
		 phy_type_high & ICE_MEDIA_BP_PHY_TYPE_HIGH_M)
		*media_type = ICE_MEDIA_BACKPLANE;
	/* else if PHY types are only optical, or optical and C2M, then media
	 * type is FIBER
	 */
	else if (ice_phy_maps_to_media(phy_type_low, phy_type_high,
				       ICE_MEDIA_OPT_PHY_TYPE_LOW_M, 0) ||
		 (phy_type_low & ICE_MEDIA_OPT_PHY_TYPE_LOW_M &&
		  phy_type_low & ICE_MEDIA_C2M_PHY_TYPE_LOW_M))
		*media_type = ICE_MEDIA_FIBER;
	/* else if PHY types are only DA, or DA and C2C, then media type DA */
	else if (ice_phy_maps_to_media(phy_type_low, phy_type_high,
				       ICE_MEDIA_DAC_PHY_TYPE_LOW_M, 0) ||
		 (phy_type_low & ICE_MEDIA_DAC_PHY_TYPE_LOW_M &&
		 (phy_type_low & ICE_MEDIA_C2C_PHY_TYPE_LOW_M ||
		  phy_type_high & ICE_MEDIA_C2C_PHY_TYPE_HIGH_M)))
		*media_type = ICE_MEDIA_DA;
	/* else if PHY types are only C2M or only C2C, then media is AUI */
	else if (ice_phy_maps_to_media(phy_type_low, phy_type_high,
				       ICE_MEDIA_C2M_PHY_TYPE_LOW_M,
				       ICE_MEDIA_C2M_PHY_TYPE_HIGH_M) ||
		 ice_phy_maps_to_media(phy_type_low, phy_type_high,
				       ICE_MEDIA_C2C_PHY_TYPE_LOW_M,
				       ICE_MEDIA_C2C_PHY_TYPE_HIGH_M))
		*media_type = ICE_MEDIA_AUI;

	else
		*media_type = ICE_MEDIA_UNKNOWN;
}

/**
 * ice_aq_get_phy_caps - returns PHY capabilities
 * @pi: port information structure
 * @qual_mods: report qualified modules
 * @report_mode: report mode capabilities
 * @pcaps: structure for PHY capabilities to be filled
 * @cd: pointer to command details structure or NULL
 *
 * Returns the various PHY capabilities supported on the Port (0x0600)
 */
enum ice_status
ice_aq_get_phy_caps(struct ice_port_info *pi, bool qual_mods,
    uint8_t report_mode, struct ice_aqc_get_phy_caps_data *pcaps,
    struct ice_sq_cd *cd)
{
	struct ice_aqc_get_phy_caps *cmd;
	uint16_t pcaps_size = sizeof(*pcaps);
	struct ice_aq_desc desc;
	enum ice_status status;
	const char *prefix;
	struct ice_hw *hw;

	cmd = &desc.params.get_phy;

	if (!pcaps || (report_mode & ~ICE_AQC_REPORT_MODE_M) || !pi)
		return ICE_ERR_PARAM;
	hw = pi->hw;

	if (report_mode == ICE_AQC_REPORT_DFLT_CFG &&
	    !ice_fw_supports_report_dflt_cfg(hw))
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_phy_caps);

	if (qual_mods)
		cmd->param0 |= htole16(ICE_AQC_GET_PHY_RQM);

	cmd->param0 |= htole16(report_mode);

	status = ice_aq_send_cmd(hw, &desc, pcaps, pcaps_size, cd);

	DNPRINTF(ICE_DBG_LINK, "get phy caps dump\n");

	switch (report_mode) {
	case ICE_AQC_REPORT_TOPO_CAP_MEDIA:
		prefix = "phy_caps_media";
		break;
	case ICE_AQC_REPORT_TOPO_CAP_NO_MEDIA:
		prefix = "phy_caps_no_media";
		break;
	case ICE_AQC_REPORT_ACTIVE_CFG:
		prefix = "phy_caps_active";
		break;
	case ICE_AQC_REPORT_DFLT_CFG:
		prefix = "phy_caps_default";
		break;
	default:
		prefix = "phy_caps_invalid";
	}

	ice_dump_phy_type(hw, le64toh(pcaps->phy_type_low),
			  le64toh(pcaps->phy_type_high), prefix);

	DNPRINTF(ICE_DBG_LINK, "%s: report_mode = 0x%x\n",
		  prefix, report_mode);
	DNPRINTF(ICE_DBG_LINK, "%s: caps = 0x%x\n", prefix, pcaps->caps);
	DNPRINTF(ICE_DBG_LINK, "%s: low_power_ctrl_an = 0x%x\n", prefix,
		  pcaps->low_power_ctrl_an);
	DNPRINTF(ICE_DBG_LINK, "%s: eee_cap = 0x%x\n", prefix,
		  pcaps->eee_cap);
	DNPRINTF(ICE_DBG_LINK, "%s: eeer_value = 0x%x\n", prefix,
		  pcaps->eeer_value);
	DNPRINTF(ICE_DBG_LINK, "%s: link_fec_options = 0x%x\n", prefix,
		  pcaps->link_fec_options);
	DNPRINTF(ICE_DBG_LINK, "%s: module_compliance_enforcement = 0x%x\n",
		  prefix, pcaps->module_compliance_enforcement);
	DNPRINTF(ICE_DBG_LINK, "%s: extended_compliance_code = 0x%x\n",
		  prefix, pcaps->extended_compliance_code);
	DNPRINTF(ICE_DBG_LINK, "%s: module_type[0] = 0x%x\n", prefix,
		  pcaps->module_type[0]);
	DNPRINTF(ICE_DBG_LINK, "%s: module_type[1] = 0x%x\n", prefix,
		  pcaps->module_type[1]);
	DNPRINTF(ICE_DBG_LINK, "%s: module_type[2] = 0x%x\n", prefix,
		  pcaps->module_type[2]);

	if (status == ICE_SUCCESS && report_mode == ICE_AQC_REPORT_TOPO_CAP_MEDIA) {
		pi->phy.phy_type_low = le64toh(pcaps->phy_type_low);
		pi->phy.phy_type_high = le64toh(pcaps->phy_type_high);
		memcpy(pi->phy.link_info.module_type, &pcaps->module_type,
		    sizeof(pi->phy.link_info.module_type));
		ice_set_media_type(pi);
		DNPRINTF(ICE_DBG_LINK, "%s: media_type = 0x%x\n", prefix,
			  pi->phy.media_type);
	}

	return status;
}

/**
 * ice_aq_get_link_info
 * @pi: port information structure
 * @ena_lse: enable/disable LinkStatusEvent reporting
 * @link: pointer to link status structure - optional
 * @cd: pointer to command details structure or NULL
 *
 * Get Link Status (0x607). Returns the link status of the adapter.
 */
enum ice_status
ice_aq_get_link_info(struct ice_port_info *pi, bool ena_lse,
		     struct ice_link_status *link, struct ice_sq_cd *cd)
{
	struct ice_aqc_get_link_status_data link_data = { 0 };
	struct ice_aqc_get_link_status *resp;
	struct ice_link_status *li_old, *li;
	struct ice_fc_info *hw_fc_info;
	bool tx_pause, rx_pause;
	struct ice_aq_desc desc;
	enum ice_status status;
	struct ice_hw *hw;
	uint16_t cmd_flags;

	if (!pi)
		return ICE_ERR_PARAM;
	hw = pi->hw;

	li_old = &pi->phy.link_info_old;
	li = &pi->phy.link_info;
	hw_fc_info = &pi->fc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_link_status);
	cmd_flags = (ena_lse) ? ICE_AQ_LSE_ENA : ICE_AQ_LSE_DIS;
	resp = &desc.params.get_link_status;
	resp->cmd_flags = htole16(cmd_flags);
	resp->lport_num = pi->lport;

	status = ice_aq_send_cmd(hw, &desc, &link_data,
	    ICE_GET_LINK_STATUS_DATALEN_V1, cd);
	if (status != ICE_SUCCESS)
		return status;

	/* save off old link status information */
	*li_old = *li;

	/* update current link status information */
	li->link_speed = le16toh(link_data.link_speed);
	li->phy_type_low = le64toh(link_data.phy_type_low);
	li->phy_type_high = le64toh(link_data.phy_type_high);
	li->link_info = link_data.link_info;
	li->link_cfg_err = link_data.link_cfg_err;
	li->an_info = link_data.an_info;
	li->ext_info = link_data.ext_info;
	li->max_frame_size = le16toh(link_data.max_frame_size);
	li->fec_info = link_data.cfg & ICE_AQ_FEC_MASK;
	li->topo_media_conflict = link_data.topo_media_conflict;
	li->pacing = link_data.cfg & (ICE_AQ_CFG_PACING_M |
				      ICE_AQ_CFG_PACING_TYPE_M);

	/* update fc info */
	tx_pause = !!(link_data.an_info & ICE_AQ_LINK_PAUSE_TX);
	rx_pause = !!(link_data.an_info & ICE_AQ_LINK_PAUSE_RX);
	if (tx_pause && rx_pause)
		hw_fc_info->current_mode = ICE_FC_FULL;
	else if (tx_pause)
		hw_fc_info->current_mode = ICE_FC_TX_PAUSE;
	else if (rx_pause)
		hw_fc_info->current_mode = ICE_FC_RX_PAUSE;
	else
		hw_fc_info->current_mode = ICE_FC_NONE;

	li->lse_ena = !!(resp->cmd_flags & htole16(ICE_AQ_LSE_IS_ENABLED));

	DNPRINTF(ICE_DBG_LINK, "get link info\n");
	DNPRINTF(ICE_DBG_LINK, "	link_speed = 0x%x\n", li->link_speed);
	DNPRINTF(ICE_DBG_LINK, "	phy_type_low = 0x%llx\n",
		  (unsigned long long)li->phy_type_low);
	DNPRINTF(ICE_DBG_LINK, "	phy_type_high = 0x%llx\n",
		  (unsigned long long)li->phy_type_high);
	DNPRINTF(ICE_DBG_LINK, "	link_info = 0x%x\n", li->link_info);
	DNPRINTF(ICE_DBG_LINK, "	link_cfg_err = 0x%x\n", li->link_cfg_err);
	DNPRINTF(ICE_DBG_LINK, "	an_info = 0x%x\n", li->an_info);
	DNPRINTF(ICE_DBG_LINK, "	ext_info = 0x%x\n", li->ext_info);
	DNPRINTF(ICE_DBG_LINK, "	fec_info = 0x%x\n", li->fec_info);
	DNPRINTF(ICE_DBG_LINK, "	lse_ena = 0x%x\n", li->lse_ena);
	DNPRINTF(ICE_DBG_LINK, "	max_frame = 0x%x\n",
		  li->max_frame_size);
	DNPRINTF(ICE_DBG_LINK, "	pacing = 0x%x\n", li->pacing);

	/* save link status information */
	if (link)
		*link = *li;

	/* flag cleared so calling functions don't call AQ again */
	pi->phy.get_link_info = false;

	return ICE_SUCCESS;
}

/**
 * ice_cfg_rl_burst_size - Set burst size value
 * @hw: pointer to the HW struct
 * @bytes: burst size in bytes
 *
 * This function configures/set the burst size to requested new value. The new
 * burst size value is used for future rate limit calls. It doesn't change the
 * existing or previously created RL profiles.
 */
enum ice_status
ice_cfg_rl_burst_size(struct ice_hw *hw, uint32_t bytes)
{
	uint16_t burst_size_to_prog;

	if (bytes < ICE_MIN_BURST_SIZE_ALLOWED ||
	    bytes > ICE_MAX_BURST_SIZE_ALLOWED)
		return ICE_ERR_PARAM;
	if (ice_round_to_num(bytes, 64) <=
	    ICE_MAX_BURST_SIZE_64_BYTE_GRANULARITY) {
		/* 64 byte granularity case */
		/* Disable MSB granularity bit */
		burst_size_to_prog = ICE_64_BYTE_GRANULARITY;
		/* round number to nearest 64 byte granularity */
		bytes = ice_round_to_num(bytes, 64);
		/* The value is in 64 byte chunks */
		burst_size_to_prog |= (uint16_t)(bytes / 64);
	} else {
		/* k bytes granularity case */
		/* Enable MSB granularity bit */
		burst_size_to_prog = ICE_KBYTE_GRANULARITY;
		/* round number to nearest 1024 granularity */
		bytes = ice_round_to_num(bytes, 1024);
		/* check rounding doesn't go beyond allowed */
		if (bytes > ICE_MAX_BURST_SIZE_KBYTE_GRANULARITY)
			bytes = ICE_MAX_BURST_SIZE_KBYTE_GRANULARITY;
		/* The value is in k bytes */
		burst_size_to_prog |= (uint16_t)(bytes / 1024);
	}
	hw->max_burst_size = burst_size_to_prog;
	return ICE_SUCCESS;
}

/**
 * ice_init_def_sw_recp - initialize the recipe book keeping tables
 * @hw: pointer to the HW struct
 * @recp_list: pointer to sw recipe list
 *
 * Allocate memory for the entire recipe table and initialize the structures/
 * entries corresponding to basic recipes.
 */
enum ice_status
ice_init_def_sw_recp(struct ice_hw *hw, struct ice_sw_recipe **recp_list)
{
	struct ice_sw_recipe *recps;
	uint8_t i;

	recps = (struct ice_sw_recipe *)
		ice_calloc(hw, ICE_MAX_NUM_RECIPES, sizeof(*recps));
	if (!recps)
		return ICE_ERR_NO_MEMORY;

	for (i = 0; i < ICE_MAX_NUM_RECIPES; i++) {
		recps[i].root_rid = i;
		TAILQ_INIT(&recps[i].filt_rules);
		TAILQ_INIT(&recps[i].adv_filt_rules);
		TAILQ_INIT(&recps[i].filt_replay_rules);
		TAILQ_INIT(&recps[i].rg_list);
		ice_init_lock(&recps[i].filt_rule_lock);
	}

	*recp_list = recps;

	return ICE_SUCCESS;
}

/**
 * ice_init_fltr_mgmt_struct - initializes filter management list and locks
 * @hw: pointer to the HW struct
 */
enum ice_status
ice_init_fltr_mgmt_struct(struct ice_hw *hw)
{
	struct ice_switch_info *sw;
	enum ice_status status;

	hw->switch_info = (struct ice_switch_info *)
			  ice_malloc(hw, sizeof(*hw->switch_info));

	sw = hw->switch_info;

	if (!sw)
		return ICE_ERR_NO_MEMORY;

	TAILQ_INIT(&sw->vsi_list_map_head);
	sw->prof_res_bm_init = 0;

	status = ice_init_def_sw_recp(hw, &hw->switch_info->recp_list);
	if (status) {
		ice_free(hw, hw->switch_info);
		return status;
	}
	return ICE_SUCCESS;
}

/**
 * ice_aq_manage_mac_read - manage MAC address read command
 * @hw: pointer to the HW struct
 * @buf: a virtual buffer to hold the manage MAC read response
 * @buf_size: Size of the virtual buffer
 * @cd: pointer to command details structure or NULL
 *
 * This function is used to return per PF station MAC address (0x0107).
 * NOTE: Upon successful completion of this command, MAC address information
 * is returned in user specified buffer. Please interpret user specified
 * buffer as "manage_mac_read" response.
 * Response such as various MAC addresses are stored in HW struct (port.mac)
 * ice_discover_dev_caps is expected to be called before this function is
 * called.
 */
enum ice_status
ice_aq_manage_mac_read(struct ice_hw *hw, void *buf, uint16_t buf_size,
		       struct ice_sq_cd *cd)
{
	struct ice_aqc_manage_mac_read_resp *resp;
	struct ice_aqc_manage_mac_read *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;
	uint16_t flags;
	uint8_t i;

	cmd = &desc.params.mac_read;

	if (buf_size < sizeof(*resp))
		return ICE_ERR_BUF_TOO_SHORT;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_manage_mac_read);

	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (status)
		return status;

	resp = (struct ice_aqc_manage_mac_read_resp *)buf;
	flags = le16toh(cmd->flags) & ICE_AQC_MAN_MAC_READ_M;

	if (!(flags & ICE_AQC_MAN_MAC_LAN_ADDR_VALID)) {
		DNPRINTF(ICE_DBG_LAN, "got invalid MAC address\n");
		return ICE_ERR_CFG;
	}

	/* A single port can report up to two (LAN and WoL) addresses */
	for (i = 0; i < cmd->num_addr; i++) {
		if (resp[i].addr_type == ICE_AQC_MAN_MAC_ADDR_TYPE_LAN) {
			memcpy(hw->port_info->mac.lan_addr,
				   resp[i].mac_addr, ETHER_ADDR_LEN);
			memcpy(hw->port_info->mac.perm_addr,
				   resp[i].mac_addr, ETHER_ADDR_LEN);
			break;
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_rem_sw_rule_info
 * @hw: pointer to the hardware structure
 * @rule_head: pointer to the switch list structure that we want to delete
 */
void
ice_rem_sw_rule_info(struct ice_hw *hw, struct ice_fltr_mgmt_list_head *rule_head)
{
	if (!TAILQ_EMPTY(rule_head)) {
		struct ice_fltr_mgmt_list_entry *entry;
		struct ice_fltr_mgmt_list_entry *tmp;

		TAILQ_FOREACH_SAFE(entry, rule_head, list_entry, tmp) {
			TAILQ_REMOVE(rule_head, entry, list_entry);
			ice_free(hw, entry);
		}
	}
}

/**
 * ice_rm_sw_replay_rule_info - helper function to delete filter replay rules
 * @hw: pointer to the HW struct
 * @sw: pointer to switch info struct for which function removes filters
 *
 * Deletes the filter replay rules for given switch
 */
void
ice_rm_sw_replay_rule_info(struct ice_hw *hw, struct ice_switch_info *sw)
{
	uint8_t i;

	if (!sw)
		return;

	for (i = 0; i < ICE_MAX_NUM_RECIPES; i++) {
		if (!TAILQ_EMPTY(&sw->recp_list[i].filt_replay_rules)) {
			struct ice_fltr_mgmt_list_head *l_head;

			l_head = &sw->recp_list[i].filt_replay_rules;
			if (!sw->recp_list[i].adv_rule)
				ice_rem_sw_rule_info(hw, l_head);
		}
	}
}
/**
 * ice_cleanup_fltr_mgmt_single - clears single filter mngt struct
 * @hw: pointer to the HW struct
 * @sw: pointer to switch info struct for which function clears filters
 */
void
ice_cleanup_fltr_mgmt_single(struct ice_hw *hw, struct ice_switch_info *sw)
{
	struct ice_vsi_list_map_info *v_pos_map;
	struct ice_vsi_list_map_info *v_tmp_map;
	struct ice_sw_recipe *recps;
	uint8_t i;

	if (!sw)
		return;

	TAILQ_FOREACH_SAFE(v_pos_map, &sw->vsi_list_map_head, list_entry,
	    v_tmp_map) {
		TAILQ_REMOVE(&sw->vsi_list_map_head, v_pos_map, list_entry);
		ice_free(hw, v_pos_map);
	}
	recps = sw->recp_list;
	for (i = 0; i < ICE_MAX_NUM_RECIPES; i++) {
		struct ice_recp_grp_entry *rg_entry, *tmprg_entry;

		recps[i].root_rid = i;
		TAILQ_FOREACH_SAFE(rg_entry, &recps[i].rg_list, l_entry,
		    tmprg_entry) {
			TAILQ_REMOVE(&recps[i].rg_list, rg_entry, l_entry);
			ice_free(hw, rg_entry);
		}

		if (recps[i].adv_rule) {
			struct ice_adv_fltr_mgmt_list_entry *tmp_entry;
			struct ice_adv_fltr_mgmt_list_entry *lst_itr;
#if 0
			ice_destroy_lock(&recps[i].filt_rule_lock);
#endif
			TAILQ_FOREACH_SAFE(lst_itr, &recps[i].adv_filt_rules,
			    list_entry, tmp_entry) {
				TAILQ_REMOVE(&recps[i].adv_filt_rules, lst_itr,
				    list_entry);
				ice_free(hw, lst_itr->lkups);
				ice_free(hw, lst_itr);
			}
		} else {
			struct ice_fltr_mgmt_list_entry *lst_itr, *tmp_entry;
#if 0
			ice_destroy_lock(&recps[i].filt_rule_lock);
#endif
			TAILQ_FOREACH_SAFE(lst_itr, &recps[i].filt_rules,
			    list_entry, tmp_entry) {
				TAILQ_REMOVE(&recps[i].filt_rules, lst_itr,
				    list_entry);
				ice_free(hw, lst_itr);
			}
		}
		if (recps[i].root_buf)
			ice_free(hw, recps[i].root_buf);
	}
	ice_rm_sw_replay_rule_info(hw, sw);
	ice_free(hw, sw->recp_list);
	ice_free(hw, sw);
}

/**
 * ice_cleanup_fltr_mgmt_struct - cleanup filter management list and locks
 * @hw: pointer to the HW struct
 */
void
ice_cleanup_fltr_mgmt_struct(struct ice_hw *hw)
{
	ice_cleanup_fltr_mgmt_single(hw, hw->switch_info);
}

/**
 * ice_is_fw_auto_drop_supported
 * @hw: pointer to the hardware structure
 *
 * Checks if the firmware supports auto drop feature
 */
bool
ice_is_fw_auto_drop_supported(struct ice_hw *hw)
{
	if (hw->api_maj_ver >= ICE_FW_API_AUTO_DROP_MAJ &&
	    hw->api_min_ver >= ICE_FW_API_AUTO_DROP_MIN)
		return true;
	return false;
}

/**
 * ice_fill_tx_timer_and_fc_thresh
 * @hw: pointer to the HW struct
 * @cmd: pointer to MAC cfg structure
 *
 * Add Tx timer and FC refresh threshold info to Set MAC Config AQ command
 * descriptor
 */
void
ice_fill_tx_timer_and_fc_thresh(struct ice_hw *hw,
				struct ice_aqc_set_mac_cfg *cmd)
{
	uint16_t fc_thres_val, tx_timer_val;
	uint32_t val;

	/* We read back the transmit timer and fc threshold value of
	 * LFC. Thus, we will use index =
	 * PRTMAC_HSEC_CTL_TX_PAUSE_QUANTA_MAX_INDEX.
	 *
	 * Also, because we are operating on transmit timer and fc
	 * threshold of LFC, we don't turn on any bit in tx_tmr_priority
	 */
#define IDX_OF_LFC PRTMAC_HSEC_CTL_TX_PAUSE_QUANTA_MAX_INDEX

	/* Retrieve the transmit timer */
	val = ICE_READ(hw, PRTMAC_HSEC_CTL_TX_PAUSE_QUANTA(IDX_OF_LFC));
	tx_timer_val = val &
		PRTMAC_HSEC_CTL_TX_PAUSE_QUANTA_HSEC_CTL_TX_PAUSE_QUANTA_M;
	cmd->tx_tmr_value = htole16(tx_timer_val);

	/* Retrieve the fc threshold */
	val = ICE_READ(hw, PRTMAC_HSEC_CTL_TX_PAUSE_REFRESH_TIMER(IDX_OF_LFC));
	fc_thres_val = val & PRTMAC_HSEC_CTL_TX_PAUSE_REFRESH_TIMER_M;

	cmd->fc_refresh_threshold = htole16(fc_thres_val);
}

/**
 * ice_aq_set_mac_cfg
 * @hw: pointer to the HW struct
 * @max_frame_size: Maximum Frame Size to be supported
 * @auto_drop: Tell HW to drop packets if TC queue is blocked
 * @cd: pointer to command details structure or NULL
 *
 * Set MAC configuration (0x0603)
 */
enum ice_status
ice_aq_set_mac_cfg(struct ice_hw *hw, uint16_t max_frame_size, bool auto_drop,
		   struct ice_sq_cd *cd)
{
	struct ice_aqc_set_mac_cfg *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.set_mac_cfg;

	if (max_frame_size == 0)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_mac_cfg);

	cmd->max_frame_size = htole16(max_frame_size);

	if (ice_is_fw_auto_drop_supported(hw) && auto_drop)
		cmd->drop_opts |= ICE_AQ_SET_MAC_AUTO_DROP_BLOCKING_PKTS;
	ice_fill_tx_timer_and_fc_thresh(hw, cmd);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_vsig_free - free VSI group
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsig: VSIG to remove
 *
 * The function will remove all VSIs associated with the input VSIG and move
 * them to the DEFAULT_VSIG and mark the VSIG available.
 */
enum ice_status
ice_vsig_free(struct ice_hw *hw, enum ice_block blk, uint16_t vsig)
{
	struct ice_vsig_prof *dtmp, *del;
	struct ice_vsig_vsi *vsi_cur;
	uint16_t idx;

	idx = vsig & ICE_VSIG_IDX_M;
	if (idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	hw->blk[blk].xlt2.vsig_tbl[idx].in_use = false;

	vsi_cur = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	/* If the VSIG has at least 1 VSI then iterate through the
	 * list and remove the VSIs before deleting the group.
	 */
	if (vsi_cur) {
		/* remove all vsis associated with this VSIG XLT2 entry */
		do {
			struct ice_vsig_vsi *tmp = vsi_cur->next_vsi;

			vsi_cur->vsig = ICE_DEFAULT_VSIG;
			vsi_cur->changed = 1;
			vsi_cur->next_vsi = NULL;
			vsi_cur = tmp;
		} while (vsi_cur);

		/* NULL terminate head of VSI list */
		hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi = NULL;
	}

	/* free characteristic list */
	TAILQ_FOREACH_SAFE(del, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
	    list, dtmp) {
		TAILQ_REMOVE(&hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst, del,
		    list);
		ice_free(hw, del);
	}

	/* if VSIG characteristic list was cleared for reset
	 * re-initialize the list head
	 */
	TAILQ_INIT(&hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst);

	return ICE_SUCCESS;
}

/**
 * ice_free_vsig_tbl - free complete VSIG table entries
 * @hw: pointer to the hardware structure
 * @blk: the HW block on which to free the VSIG table entries
 */
void
ice_free_vsig_tbl(struct ice_hw *hw, enum ice_block blk)
{
	uint16_t i;

	if (!hw->blk[blk].xlt2.vsig_tbl)
		return;

	for (i = 1; i < ICE_MAX_VSIGS; i++)
		if (hw->blk[blk].xlt2.vsig_tbl[i].in_use)
			ice_vsig_free(hw, blk, i);
}

/**
 * ice_free_prof_map - free profile map
 * @hw: pointer to the hardware structure
 * @blk_idx: HW block index
 */
void
ice_free_prof_map(struct ice_hw *hw, uint8_t blk_idx)
{
	struct ice_es *es = &hw->blk[blk_idx].es;
	struct ice_prof_map *del, *tmp;
#if 0
	ice_acquire_lock(&es->prof_map_lock);
#endif
	TAILQ_FOREACH_SAFE(del, &es->prof_map, list, tmp) { 
		TAILQ_REMOVE(&es->prof_map, del, list);
		ice_free(hw, del);
	}
	TAILQ_INIT(&es->prof_map);
#if 0
	ice_release_lock(&es->prof_map_lock);
#endif
}

/**
 * ice_free_flow_profs - free flow profile entries
 * @hw: pointer to the hardware structure
 * @blk_idx: HW block index
 */
void
ice_free_flow_profs(struct ice_hw *hw, uint8_t blk_idx)
{
	struct ice_flow_prof *p, *tmp;
#if 0
	ice_acquire_lock(&hw->fl_profs_locks[blk_idx]);
#endif
	TAILQ_FOREACH_SAFE(p, &hw->fl_profs[blk_idx], l_entry, tmp) {
		TAILQ_REMOVE(&hw->fl_profs[blk_idx], p, l_entry);

		ice_free(hw, p);
	}
#if 0
	ice_release_lock(&hw->fl_profs_locks[blk_idx]);
#endif
	/* if driver is in reset and tables are being cleared
	 * re-initialize the flow profile list heads
	 */
	TAILQ_INIT(&hw->fl_profs[blk_idx]);
}

/**
 * ice_free_hw_tbls - free hardware table memory
 * @hw: pointer to the hardware structure
 */
void
ice_free_hw_tbls(struct ice_hw *hw)
{
	struct ice_rss_cfg *r, *rt;
	uint8_t i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		if (hw->blk[i].is_list_init) {
#if 0
			struct ice_es *es = &hw->blk[i].es;
#endif
			ice_free_prof_map(hw, i);
#if 0
			ice_destroy_lock(&es->prof_map_lock);
#endif

			ice_free_flow_profs(hw, i);
#if 0
			ice_destroy_lock(&hw->fl_profs_locks[i]);
#endif

			hw->blk[i].is_list_init = false;
		}
		ice_free_vsig_tbl(hw, (enum ice_block)i);
		ice_free(hw, hw->blk[i].xlt1.ptypes);
		ice_free(hw, hw->blk[i].xlt1.ptg_tbl);
		ice_free(hw, hw->blk[i].xlt1.t);
		ice_free(hw, hw->blk[i].xlt2.t);
		ice_free(hw, hw->blk[i].xlt2.vsig_tbl);
		ice_free(hw, hw->blk[i].xlt2.vsis);
		ice_free(hw, hw->blk[i].prof.t);
		ice_free(hw, hw->blk[i].prof_redir.t);
		ice_free(hw, hw->blk[i].es.t);
		ice_free(hw, hw->blk[i].es.ref_count);
		ice_free(hw, hw->blk[i].es.written);
	}

	TAILQ_FOREACH_SAFE(r, &hw->rss_list_head, l_entry, rt) {
		TAILQ_REMOVE(&hw->rss_list_head, r, l_entry);
		ice_free(hw, r);
	}
#if 0
	ice_destroy_lock(&hw->rss_locks);
#endif
	memset(hw->blk, 0, sizeof(hw->blk));
}

/**
 * ice_init_flow_profs - init flow profile locks and list heads
 * @hw: pointer to the hardware structure
 * @blk_idx: HW block index
 */
void ice_init_flow_profs(struct ice_hw *hw, uint8_t blk_idx)
{
#if 0
	ice_init_lock(&hw->fl_profs_locks[blk_idx]);
#endif
	TAILQ_INIT(&hw->fl_profs[blk_idx]);
}

/* Block / table size info */
struct ice_blk_size_details {
	uint16_t xlt1;			/* # XLT1 entries */
	uint16_t xlt2;			/* # XLT2 entries */
	uint16_t prof_tcam;			/* # profile ID TCAM entries */
	uint16_t prof_id;			/* # profile IDs */
	uint8_t prof_cdid_bits;		/* # CDID one-hot bits used in key */
	uint16_t prof_redir;			/* # profile redirection entries */
	uint16_t es;				/* # extraction sequence entries */
	uint16_t fvw;			/* # field vector words */
	uint8_t overwrite;			/* overwrite existing entries allowed */
	uint8_t reverse;			/* reverse FV order */
};

static const struct ice_blk_size_details blk_sizes[ICE_BLK_COUNT] = {
	/**
	 * Table Definitions
	 * XLT1 - Number of entries in XLT1 table
	 * XLT2 - Number of entries in XLT2 table
	 * TCAM - Number of entries Profile ID TCAM table
	 * CDID - Control Domain ID of the hardware block
	 * PRED - Number of entries in the Profile Redirection Table
	 * FV   - Number of entries in the Field Vector
	 * FVW  - Width (in WORDs) of the Field Vector
	 * OVR  - Overwrite existing table entries
	 * REV  - Reverse FV
	 */
	/*          XLT1        , XLT2        ,TCAM, PID,CDID,PRED,   FV, FVW */
	/*          Overwrite   , Reverse FV */
	/* SW  */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 256,   0,  256, 256,  48,
		    false, false },
	/* ACL */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  32,
		    false, false },
	/* FD  */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  24,
		    false, true  },
	/* RSS */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  24,
		    true,  true  },
	/* PE  */ { ICE_XLT1_CNT, ICE_XLT2_CNT,  64,  32,   0,   32,  32,  24,
		    false, false },
};

enum ice_sid_all {
	ICE_SID_XLT1_OFF = 0,
	ICE_SID_XLT2_OFF,
	ICE_SID_PR_OFF,
	ICE_SID_PR_REDIR_OFF,
	ICE_SID_ES_OFF,
	ICE_SID_OFF_COUNT,
};

/* Block / table section IDs */
static const uint32_t ice_blk_sids[ICE_BLK_COUNT][ICE_SID_OFF_COUNT] = {
	/* SWITCH */
	{	ICE_SID_XLT1_SW,
		ICE_SID_XLT2_SW,
		ICE_SID_PROFID_TCAM_SW,
		ICE_SID_PROFID_REDIR_SW,
		ICE_SID_FLD_VEC_SW
	},

	/* ACL */
	{	ICE_SID_XLT1_ACL,
		ICE_SID_XLT2_ACL,
		ICE_SID_PROFID_TCAM_ACL,
		ICE_SID_PROFID_REDIR_ACL,
		ICE_SID_FLD_VEC_ACL
	},

	/* FD */
	{	ICE_SID_XLT1_FD,
		ICE_SID_XLT2_FD,
		ICE_SID_PROFID_TCAM_FD,
		ICE_SID_PROFID_REDIR_FD,
		ICE_SID_FLD_VEC_FD
	},

	/* RSS */
	{	ICE_SID_XLT1_RSS,
		ICE_SID_XLT2_RSS,
		ICE_SID_PROFID_TCAM_RSS,
		ICE_SID_PROFID_REDIR_RSS,
		ICE_SID_FLD_VEC_RSS
	},

	/* PE */
	{	ICE_SID_XLT1_PE,
		ICE_SID_XLT2_PE,
		ICE_SID_PROFID_TCAM_PE,
		ICE_SID_PROFID_REDIR_PE,
		ICE_SID_FLD_VEC_PE
	}
};

/**
 * ice_init_hw_tbls - init hardware table memory
 * @hw: pointer to the hardware structure
 */
enum ice_status
ice_init_hw_tbls(struct ice_hw *hw)
{
	uint8_t i;
#if 0
	ice_init_lock(&hw->rss_locks);
#endif
	TAILQ_INIT(&hw->rss_list_head);
	for (i = 0; i < ICE_BLK_COUNT; i++) {
		struct ice_prof_redir *prof_redir = &hw->blk[i].prof_redir;
		struct ice_prof_tcam *prof = &hw->blk[i].prof;
		struct ice_xlt1 *xlt1 = &hw->blk[i].xlt1;
		struct ice_xlt2 *xlt2 = &hw->blk[i].xlt2;
		struct ice_es *es = &hw->blk[i].es;
		uint16_t j;

		if (hw->blk[i].is_list_init)
			continue;

		ice_init_flow_profs(hw, i);
		ice_init_lock(&es->prof_map_lock);
		TAILQ_INIT(&es->prof_map);
		hw->blk[i].is_list_init = true;

		hw->blk[i].overwrite = blk_sizes[i].overwrite;
		es->reverse = blk_sizes[i].reverse;

		xlt1->sid = ice_blk_sids[i][ICE_SID_XLT1_OFF];
		xlt1->count = blk_sizes[i].xlt1;

		xlt1->ptypes = (struct ice_ptg_ptype *)
			ice_calloc(hw, xlt1->count, sizeof(*xlt1->ptypes));

		if (!xlt1->ptypes)
			goto err;

		xlt1->ptg_tbl = (struct ice_ptg_entry *)
			ice_calloc(hw, ICE_MAX_PTGS, sizeof(*xlt1->ptg_tbl));

		if (!xlt1->ptg_tbl)
			goto err;

		xlt1->t = (uint8_t *)ice_calloc(hw, xlt1->count,
		    sizeof(*xlt1->t));
		if (!xlt1->t)
			goto err;

		xlt2->sid = ice_blk_sids[i][ICE_SID_XLT2_OFF];
		xlt2->count = blk_sizes[i].xlt2;

		xlt2->vsis = (struct ice_vsig_vsi *)
			ice_calloc(hw, xlt2->count, sizeof(*xlt2->vsis));

		if (!xlt2->vsis)
			goto err;

		xlt2->vsig_tbl = (struct ice_vsig_entry *)
			ice_calloc(hw, xlt2->count, sizeof(*xlt2->vsig_tbl));
		if (!xlt2->vsig_tbl)
			goto err;

		for (j = 0; j < xlt2->count; j++)
			TAILQ_INIT(&xlt2->vsig_tbl[j].prop_lst);

		xlt2->t = (uint16_t *)ice_calloc(hw, xlt2->count,
		    sizeof(*xlt2->t));
		if (!xlt2->t)
			goto err;

		prof->sid = ice_blk_sids[i][ICE_SID_PR_OFF];
		prof->count = blk_sizes[i].prof_tcam;
		prof->max_prof_id = blk_sizes[i].prof_id;
		prof->cdid_bits = blk_sizes[i].prof_cdid_bits;
		prof->t = (struct ice_prof_tcam_entry *)
			ice_calloc(hw, prof->count, sizeof(*prof->t));

		if (!prof->t)
			goto err;

		prof_redir->sid = ice_blk_sids[i][ICE_SID_PR_REDIR_OFF];
		prof_redir->count = blk_sizes[i].prof_redir;
		prof_redir->t = (uint8_t *)ice_calloc(hw, prof_redir->count,
						 sizeof(*prof_redir->t));

		if (!prof_redir->t)
			goto err;

		es->sid = ice_blk_sids[i][ICE_SID_ES_OFF];
		es->count = blk_sizes[i].es;
		es->fvw = blk_sizes[i].fvw;
		es->t = (struct ice_fv_word *)
			ice_calloc(hw, (uint32_t)(es->count * es->fvw),
				   sizeof(*es->t));
		if (!es->t)
			goto err;

		es->ref_count = (uint16_t *)
			ice_calloc(hw, es->count, sizeof(*es->ref_count));

		if (!es->ref_count)
			goto err;

		es->written = (uint8_t *)
			ice_calloc(hw, es->count, sizeof(*es->written));

		if (!es->written)
			goto err;

	}
	return ICE_SUCCESS;

err:
	ice_free_hw_tbls(hw);
	return ICE_ERR_NO_MEMORY;
}

enum ice_status
ice_init_hw(struct ice_hw *hw)
{
	struct ice_softc *sc = hw->hw_sc;
	struct ice_aqc_get_phy_caps_data *pcaps;
	enum ice_status status;
	uint16_t mac_buf_len;
	void *mac_buf;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	/* Set MAC type based on DeviceID */
	ice_set_mac_type(hw);

	hw->pf_id = (uint8_t)(ICE_READ(hw, PF_FUNC_RID) &
			 PF_FUNC_RID_FUNCTION_NUMBER_M) >>
		PF_FUNC_RID_FUNCTION_NUMBER_S;

	status = ice_reset(hw, ICE_RESET_PFR);
	if (status)
		return status;

	ice_get_itr_intrl_gran(hw);

	status = ice_create_all_ctrlq(hw);
	if (status)
		goto err_unroll_cqinit;

	ice_fwlog_set_support_ena(hw);
	status = ice_fwlog_set(hw, &hw->fwlog_cfg);
	if (status) {
		DNPRINTF(ICE_DBG_INIT,
		    "Failed to enable FW logging, status %d.\n", status);
	} else {
		if (hw->fwlog_cfg.options & ICE_FWLOG_OPTION_REGISTER_ON_INIT) {
			status = ice_fwlog_register(hw);
			if (status)
				DNPRINTF(ICE_DBG_INIT,
				    "Failed to register for FW logging "
				    "events, status %d.\n", status);
		} else {
			status = ice_fwlog_unregister(hw);
			if (status)
				DNPRINTF(ICE_DBG_INIT, "Failed to unregister "
				    "for FW logging events, status %d.\n",
				    status);
		}
	}

	status = ice_init_nvm(hw);
	if (status)
		goto err_unroll_cqinit;

	if (ice_get_fw_mode(hw) == ICE_FW_MODE_ROLLBACK)
		ice_print_rollback_msg(hw);

	status = ice_clear_pf_cfg(hw);
	if (status)
		goto err_unroll_cqinit;

	ice_clear_pxe_mode(hw);

	status = ice_get_caps(hw);
	if (status)
		goto err_unroll_cqinit;

	if (!hw->port_info)
		hw->port_info = (struct ice_port_info *)
			ice_malloc(hw, sizeof(*hw->port_info));
	if (!hw->port_info) {
		status = ICE_ERR_NO_MEMORY;
		goto err_unroll_cqinit;
	}

	/* set the back pointer to HW */
	hw->port_info->hw = hw;

	/* Initialize port_info struct with switch configuration data */
	status = ice_get_initial_sw_cfg(hw);
	if (status)
		goto err_unroll_alloc;

	hw->evb_veb = true;
	/* Query the allocated resources for Tx scheduler */
	status = ice_sched_query_res_alloc(hw);
	if (status) {
		DNPRINTF(ICE_DBG_SCHED,
		    "Failed to get scheduler allocated resources\n");
		goto err_unroll_alloc;
	}
	ice_sched_get_psm_clk_freq(hw);

	/* Initialize port_info struct with scheduler data */
	status = ice_sched_init_port(hw->port_info);
	if (status)
		goto err_unroll_sched;
	pcaps = (struct ice_aqc_get_phy_caps_data *)
		ice_malloc(hw, sizeof(*pcaps));
	if (!pcaps) {
		status = ICE_ERR_NO_MEMORY;
		goto err_unroll_sched;
	}
	/* Initialize port_info struct with PHY capabilities */
	status = ice_aq_get_phy_caps(hw->port_info, false,
				     ICE_AQC_REPORT_TOPO_CAP_MEDIA, pcaps, NULL);
	ice_free(hw, pcaps);
	if (status)
		printf("%s: Get PHY capabilities failed status = %d, "
		    "continuing anyway\n", sc->sc_dev.dv_xname, status);

	/* Initialize port_info struct with link information */
	status = ice_aq_get_link_info(hw->port_info, false, NULL, NULL);
	if (status)
		goto err_unroll_sched;
	/* need a valid SW entry point to build a Tx tree */
	if (!hw->sw_entry_point_layer) {
		DNPRINTF(ICE_DBG_SCHED, "invalid sw entry point\n");
		status = ICE_ERR_CFG;
		goto err_unroll_sched;
	}

	TAILQ_INIT(&hw->agg_list);
	/* Initialize max burst size */
	if (!hw->max_burst_size)
		ice_cfg_rl_burst_size(hw, ICE_SCHED_DFLT_BURST_SIZE);

	status = ice_init_fltr_mgmt_struct(hw);
	if (status)
		goto err_unroll_sched;

	/* Get MAC information */

	/* A single port can report up to two (LAN and WoL) addresses */
	mac_buf = ice_calloc(hw, 2,
			     sizeof(struct ice_aqc_manage_mac_read_resp));
	mac_buf_len = 2 * sizeof(struct ice_aqc_manage_mac_read_resp);

	if (!mac_buf) {
		status = ICE_ERR_NO_MEMORY;
		goto err_unroll_fltr_mgmt_struct;
	}

	status = ice_aq_manage_mac_read(hw, mac_buf, mac_buf_len, NULL);
	ice_free(hw, mac_buf);

	if (status)
		goto err_unroll_fltr_mgmt_struct;

	/* enable jumbo frame support at MAC level */
	status = ice_aq_set_mac_cfg(hw, ICE_AQ_SET_MAC_FRAME_SIZE_MAX, false,
				    NULL);
	if (status)
		goto err_unroll_fltr_mgmt_struct;

	status = ice_init_hw_tbls(hw);
	if (status)
		goto err_unroll_fltr_mgmt_struct;
#if 0
	ice_init_lock(&hw->tnl_lock);
#endif
	return ICE_SUCCESS;
err_unroll_fltr_mgmt_struct:
	ice_cleanup_fltr_mgmt_struct(hw);
err_unroll_sched:
	ice_sched_cleanup_all(hw);
err_unroll_alloc:
	ice_free(hw, hw->port_info);
	hw->port_info = NULL;
err_unroll_cqinit:
	ice_destroy_all_ctrlq(hw);
	return status;
}

/**
 * ice_deinit_hw - unroll initialization operations done by ice_init_hw
 * @hw: pointer to the hardware structure
 *
 * This should be called only during nominal operation, not as a result of
 * ice_init_hw() failing since ice_init_hw() will take care of unrolling
 * applicable initializations if it fails for any reason.
 */
void ice_deinit_hw(struct ice_hw *hw)
{
	ice_cleanup_fltr_mgmt_struct(hw);

	ice_sched_cleanup_all(hw);
#if 0
	ice_sched_clear_agg(hw);
	ice_free_seg(hw);
#endif
	ice_free_hw_tbls(hw);
#if 0
	ice_destroy_lock(&hw->tnl_lock);
#endif
	if (hw->port_info) {
		ice_free(hw, hw->port_info);
		hw->port_info = NULL;
	}

	ice_destroy_all_ctrlq(hw);
#if 0
	/* Clear VSI contexts if not already cleared */
	ice_clear_all_vsi_ctx(hw);
#endif
}

void
ice_rxfill(struct ice_softc *sc, struct ice_rx_queue *rxq)
{
	union ice_32b_rx_flex_desc *ring, *rxd;
	struct ice_rx_map *rxm;
	bus_dmamap_t map;
	struct mbuf *m;
	unsigned int prod;
	unsigned int slots;
	unsigned int mask;
	int post = 0;

	slots = if_rxr_get(&rxq->rxq_acct, rxq->desc_count);
	if (slots == 0)
		return;

	prod = rxq->rxq_prod;

	ring = ICE_DMA_KVA(&rxq->rx_desc_mem);
	mask = rxq->desc_count - 1;

	do {
		rxm = &rxq->rx_map[prod];

		m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES + ETHER_ALIGN);
		if (m == NULL)
			break;
		m->m_data += (m->m_ext.ext_size - (MCLBYTES + ETHER_ALIGN));
		m->m_len = m->m_pkthdr.len = MCLBYTES + ETHER_ALIGN;

		map = rxm->rxm_map;

		if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
		    BUS_DMA_NOWAIT) != 0) {
			m_freem(m);
			break;
		}

		rxm->rxm_m = m;

		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_PREREAD);

		rxd = &ring[prod];

		htolem64(&rxd->read.pkt_addr, map->dm_segs[0].ds_addr);
		rxd->read.hdr_addr = htole64(0);

		prod++;
		prod &= mask;

		post = 1;
	} while (--slots);

	if_rxr_put(&rxq->rxq_acct, slots);

	if (if_rxr_inuse(&rxq->rxq_acct) == 0)
		timeout_add(&rxq->rxq_refill, 1);
	else if (post) {
		rxq->rxq_prod = prod;
		ICE_WRITE(&sc->hw, rxq->tail, prod);
	}
}

/**
 * ice_aq_manage_mac_write - manage MAC address write command
 * @hw: pointer to the HW struct
 * @mac_addr: MAC address to be written as LAA/LAA+WoL/Port address
 * @flags: flags to control write behavior
 * @cd: pointer to command details structure or NULL
 *
 * This function is used to write MAC address to the NVM (0x0108).
 */
enum ice_status
ice_aq_manage_mac_write(struct ice_hw *hw, const uint8_t *mac_addr,
    uint8_t flags, struct ice_sq_cd *cd)
{
	struct ice_aqc_manage_mac_write *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.mac_write;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_manage_mac_write);

	cmd->flags = flags;
	memcpy(cmd->mac_addr, mac_addr, ETHER_ADDR_LEN);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_update_laa_mac - Update MAC address if Locally Administered
 * @sc: the device softc
 *
 * Update the device MAC address when a Locally Administered Address is
 * assigned.
 *
 * This function does *not* update the MAC filter list itself. Instead, it
 * should be called after ice_rm_pf_default_mac_filters, so that the previous
 * address filter will be removed, and before ice_cfg_pf_default_mac_filters,
 * so that the new address filter will be assigned.
 */
void
ice_update_laa_mac(struct ice_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint8_t *lladdr = ((struct arpcom *)ifp)->ac_enaddr;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	/* Desired address already set in hardware? */
	if (!memcmp(lladdr, hw->port_info->mac.lan_addr, ETHER_ADDR_LEN))
		return;

	status = ice_aq_manage_mac_write(hw, lladdr,
	    ICE_AQC_MAN_MAC_UPDATE_LAA_WOL, NULL);
	if (status) {
		printf("%s: Failed to write mac %s to firmware, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		    ether_sprintf(lladdr), ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		return;
	}

	/* Cache current hardware address. */
	memcpy(hw->port_info->mac.lan_addr, lladdr, ETHER_ADDR_LEN);
}

/**
 * ice_add_mac_to_list - Add MAC filter to a MAC filter list
 * @vsi: the VSI to forward to
 * @list: list which contains MAC filter entries
 * @addr: the MAC address to be added
 * @action: filter action to perform on match
 *
 * Adds a MAC address filter to the list which will be forwarded to firmware
 * to add a series of MAC address filters.
 *
 * Returns 0 on success, and an error code on failure.
 *
 */
int
ice_add_mac_to_list(struct ice_vsi *vsi, struct ice_fltr_list_head *list,
		    const uint8_t *addr, enum ice_sw_fwd_act_type action)
{
	struct ice_fltr_list_entry *entry;

	entry = malloc(sizeof(*entry), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (!entry)
		return (ENOMEM);

	entry->fltr_info.flag = ICE_FLTR_TX;
	entry->fltr_info.src_id = ICE_SRC_ID_VSI;
	entry->fltr_info.lkup_type = ICE_SW_LKUP_MAC;
	entry->fltr_info.fltr_act = action;
	entry->fltr_info.vsi_handle = vsi->idx;
	memcpy(entry->fltr_info.l_data.mac.mac_addr, addr, ETHER_ADDR_LEN);

	TAILQ_INSERT_HEAD(list, entry, list_entry);

	return 0;
}

/**
 * ice_free_fltr_list - Free memory associated with a MAC address list
 * @list: the list to free
 *
 * Free the memory of each entry associated with the list.
 */
void
ice_free_fltr_list(struct ice_fltr_list_head *list)
{
	struct ice_fltr_list_entry *e, *tmp;

	TAILQ_FOREACH_SAFE(e, list, list_entry, tmp) {
		TAILQ_REMOVE(list, e, list_entry);
		free(e, M_DEVBUF, sizeof(*e));
	}
}

/**
 * ice_find_rule_entry - Search a rule entry
 * @list_head: head of rule list
 * @f_info: rule information
 *
 * Helper function to search for a given rule entry
 * Returns pointer to entry storing the rule if found
 */
struct ice_fltr_mgmt_list_entry *
ice_find_rule_entry(struct ice_fltr_mgmt_list_head *list_head,
		    struct ice_fltr_info *f_info)
{
	struct ice_fltr_mgmt_list_entry *list_itr, *ret = NULL;

	TAILQ_FOREACH(list_itr, list_head, list_entry) {
		if (!memcmp(&f_info->l_data, &list_itr->fltr_info.l_data,
			    sizeof(f_info->l_data)) &&
		    f_info->flag == list_itr->fltr_info.flag) {
			ret = list_itr;
			break;
		}
	}

	return ret;
}

/* Dummy ethernet header needed in the ice_sw_rule_*
 * struct to configure any switch filter rules.
 * {DA (6 bytes), SA(6 bytes),
 * Ether type (2 bytes for header without VLAN tag) OR
 * VLAN tag (4 bytes for header with VLAN tag) }
 *
 * Word on Hardcoded values
 * byte 0 = 0x2: to identify it as locally administered DA MAC
 * byte 6 = 0x2: to identify it as locally administered SA MAC
 * byte 12 = 0x81 & byte 13 = 0x00:
 *	In case of VLAN filter first two bytes defines ether type (0x8100)
 *	and remaining two bytes are placeholder for programming a given VLAN ID
 *	In case of Ether type filter it is treated as header without VLAN tag
 *	and byte 12 and 13 is used to program a given Ether type instead
 */
static const uint8_t dummy_eth_header[ICE_DUMMY_ETH_HDR_LEN] = {
    0x2, 0, 0, 0, 0, 0, 0x2, 0, 0, 0, 0, 0, 0x81, 0, 0, 0
};

#define ICE_ETH_DA_OFFSET		0
#define ICE_ETH_ETHTYPE_OFFSET		12
#define ICE_ETH_VLAN_TCI_OFFSET		14
#define ICE_MAX_VLAN_ID			0xFFF
#define ICE_IPV6_ETHER_ID		0x86DD
#define ICE_PPP_IPV6_PROTO_ID		0x0057
#define ICE_ETH_P_8021Q			0x8100

/**
 * ice_fill_sw_info - Helper function to populate lb_en and lan_en
 * @hw: pointer to the hardware structure
 * @fi: filter info structure to fill/update
 *
 * This helper function populates the lb_en and lan_en elements of the provided
 * ice_fltr_info struct using the switch's type and characteristics of the
 * switch rule being configured.
 */
void
ice_fill_sw_info(struct ice_hw *hw, struct ice_fltr_info *fi)
{
	fi->lb_en = false;
	fi->lan_en = false;
	if ((fi->flag & ICE_FLTR_TX) &&
	    (fi->fltr_act == ICE_FWD_TO_VSI ||
	     fi->fltr_act == ICE_FWD_TO_VSI_LIST ||
	     fi->fltr_act == ICE_FWD_TO_Q ||
	     fi->fltr_act == ICE_FWD_TO_QGRP)) {
		/* Setting LB for prune actions will result in replicated
		 * packets to the internal switch that will be dropped.
		 */
		if (fi->lkup_type != ICE_SW_LKUP_VLAN)
			fi->lb_en = true;

		/* Set lan_en to TRUE if
		 * 1. The switch is a VEB AND
		 * 2
		 * 2.1 The lookup is a directional lookup like ethertype,
		 * promiscuous, ethertype-MAC, promiscuous-VLAN
		 * and default-port OR
		 * 2.2 The lookup is VLAN, OR
		 * 2.3 The lookup is MAC with mcast or bcast addr for MAC, OR
		 * 2.4 The lookup is MAC_VLAN with mcast or bcast addr for MAC.
		 *
		 * OR
		 *
		 * The switch is a VEPA.
		 *
		 * In all other cases, the LAN enable has to be set to false.
		 */

		if (hw->evb_veb) {
			if (fi->lkup_type == ICE_SW_LKUP_ETHERTYPE ||
			    fi->lkup_type == ICE_SW_LKUP_PROMISC ||
			    fi->lkup_type == ICE_SW_LKUP_ETHERTYPE_MAC ||
			    fi->lkup_type == ICE_SW_LKUP_PROMISC_VLAN ||
			    fi->lkup_type == ICE_SW_LKUP_DFLT ||
			    fi->lkup_type == ICE_SW_LKUP_VLAN ||
			    (fi->lkup_type == ICE_SW_LKUP_MAC &&
			     ETHER_IS_MULTICAST(fi->l_data.mac.mac_addr)) ||
			    (fi->lkup_type == ICE_SW_LKUP_MAC_VLAN &&
			     ETHER_IS_MULTICAST(fi->l_data.mac.mac_addr)))
				fi->lan_en = true;
		} else {
			fi->lan_en = true;
		}
	}

	/* To be able to receive packets coming from the VF on the same PF,
	 * unicast filter needs to be added without LB_EN bit
	 */
	if (fi->flag & ICE_FLTR_RX_LB) {
		fi->lb_en = false;
		fi->lan_en = true;
	}
}

/**
 * ice_fill_sw_rule - Helper function to fill switch rule structure
 * @hw: pointer to the hardware structure
 * @f_info: entry containing packet forwarding information
 * @s_rule: switch rule structure to be filled in based on mac_entry
 * @opc: switch rules population command type - pass in the command opcode
 */
void
ice_fill_sw_rule(struct ice_hw *hw, struct ice_fltr_info *f_info,
		 struct ice_sw_rule_lkup_rx_tx *s_rule,
		 enum ice_adminq_opc opc)
{
	uint16_t vlan_id = ICE_MAX_VLAN_ID + 1;
	uint16_t vlan_tpid = ICE_ETH_P_8021Q;
	void *daddr = NULL;
	uint16_t eth_hdr_sz;
	uint8_t *eth_hdr;
	uint32_t act = 0;
	uint16_t *off;
	uint8_t q_rgn;

	if (opc == ice_aqc_opc_remove_sw_rules) {
		s_rule->act = 0;
		s_rule->index = htole16(f_info->fltr_rule_id);
		s_rule->hdr_len = 0;
		return;
	}

	eth_hdr_sz = sizeof(dummy_eth_header);
	eth_hdr = s_rule->hdr_data;

	/* initialize the ether header with a dummy header */
	memcpy(eth_hdr, dummy_eth_header, eth_hdr_sz);
	ice_fill_sw_info(hw, f_info);

	switch (f_info->fltr_act) {
	case ICE_FWD_TO_VSI:
		act |= (f_info->fwd_id.hw_vsi_id << ICE_SINGLE_ACT_VSI_ID_S) &
			ICE_SINGLE_ACT_VSI_ID_M;
		if (f_info->lkup_type != ICE_SW_LKUP_VLAN)
			act |= ICE_SINGLE_ACT_VSI_FORWARDING |
				ICE_SINGLE_ACT_VALID_BIT;
		break;
	case ICE_FWD_TO_VSI_LIST:
		act |= ICE_SINGLE_ACT_VSI_LIST;
		act |= (f_info->fwd_id.vsi_list_id <<
			ICE_SINGLE_ACT_VSI_LIST_ID_S) &
			ICE_SINGLE_ACT_VSI_LIST_ID_M;
		if (f_info->lkup_type != ICE_SW_LKUP_VLAN)
			act |= ICE_SINGLE_ACT_VSI_FORWARDING |
				ICE_SINGLE_ACT_VALID_BIT;
		break;
	case ICE_FWD_TO_Q:
		act |= ICE_SINGLE_ACT_TO_Q;
		act |= (f_info->fwd_id.q_id << ICE_SINGLE_ACT_Q_INDEX_S) &
			ICE_SINGLE_ACT_Q_INDEX_M;
		break;
	case ICE_DROP_PACKET:
		act |= ICE_SINGLE_ACT_VSI_FORWARDING | ICE_SINGLE_ACT_DROP |
			ICE_SINGLE_ACT_VALID_BIT;
		break;
	case ICE_FWD_TO_QGRP:
		q_rgn = f_info->qgrp_size > 0 ?
			(uint8_t)ice_ilog2(f_info->qgrp_size) : 0;
		act |= ICE_SINGLE_ACT_TO_Q;
		act |= (f_info->fwd_id.q_id << ICE_SINGLE_ACT_Q_INDEX_S) &
			ICE_SINGLE_ACT_Q_INDEX_M;
		act |= (q_rgn << ICE_SINGLE_ACT_Q_REGION_S) &
			ICE_SINGLE_ACT_Q_REGION_M;
		break;
	default:
		return;
	}

	if (f_info->lb_en)
		act |= ICE_SINGLE_ACT_LB_ENABLE;
	if (f_info->lan_en)
		act |= ICE_SINGLE_ACT_LAN_ENABLE;

	switch (f_info->lkup_type) {
	case ICE_SW_LKUP_MAC:
		daddr = f_info->l_data.mac.mac_addr;
		break;
	case ICE_SW_LKUP_VLAN:
		vlan_id = f_info->l_data.vlan.vlan_id;
		if (f_info->l_data.vlan.tpid_valid)
			vlan_tpid = f_info->l_data.vlan.tpid;
		if (f_info->fltr_act == ICE_FWD_TO_VSI ||
		    f_info->fltr_act == ICE_FWD_TO_VSI_LIST) {
			act |= ICE_SINGLE_ACT_PRUNE;
			act |= ICE_SINGLE_ACT_EGRESS | ICE_SINGLE_ACT_INGRESS;
		}
		break;
	case ICE_SW_LKUP_ETHERTYPE_MAC:
		daddr = f_info->l_data.ethertype_mac.mac_addr;
		/* fall-through */
	case ICE_SW_LKUP_ETHERTYPE:
		off = (uint16_t *)(eth_hdr + ICE_ETH_ETHTYPE_OFFSET);
		*off = htobe16(f_info->l_data.ethertype_mac.ethertype);
		break;
	case ICE_SW_LKUP_MAC_VLAN:
		daddr = f_info->l_data.mac_vlan.mac_addr;
		vlan_id = f_info->l_data.mac_vlan.vlan_id;
		break;
	case ICE_SW_LKUP_PROMISC_VLAN:
		vlan_id = f_info->l_data.mac_vlan.vlan_id;
		/* fall-through */
	case ICE_SW_LKUP_PROMISC:
		daddr = f_info->l_data.mac_vlan.mac_addr;
		break;
	default:
		break;
	}

	s_rule->hdr.type = (f_info->flag & ICE_FLTR_RX) ?
		htole16(ICE_AQC_SW_RULES_T_LKUP_RX) :
		htole16(ICE_AQC_SW_RULES_T_LKUP_TX);

	/* Recipe set depending on lookup type */
	s_rule->recipe_id = htole16(f_info->lkup_type);
	s_rule->src = htole16(f_info->src);
	s_rule->act = htole32(act);

	if (daddr)
		memcpy(eth_hdr + ICE_ETH_DA_OFFSET, daddr, ETHER_ADDR_LEN);

	if (!(vlan_id > ICE_MAX_VLAN_ID)) {
		off = (uint16_t *)(eth_hdr + ICE_ETH_VLAN_TCI_OFFSET);
		*off = htobe16(vlan_id);
		off = (uint16_t *)(eth_hdr + ICE_ETH_ETHTYPE_OFFSET);
		*off = htobe16(vlan_tpid);
	}

	/* Create the switch rule with the final dummy Ethernet header */
	if (opc != ice_aqc_opc_update_sw_rules)
		s_rule->hdr_len = htole16(eth_hdr_sz);
}

/**
 * ice_aq_sw_rules - add/update/remove switch rules
 * @hw: pointer to the HW struct
 * @rule_list: pointer to switch rule population list
 * @rule_list_sz: total size of the rule list in bytes
 * @num_rules: number of switch rules in the rule_list
 * @opc: switch rules population command type - pass in the command opcode
 * @cd: pointer to command details structure or NULL
 *
 * Add(0x02a0)/Update(0x02a1)/Remove(0x02a2) switch rules commands to firmware
 */
enum ice_status
ice_aq_sw_rules(struct ice_hw *hw, void *rule_list, uint16_t rule_list_sz,
		uint8_t num_rules, enum ice_adminq_opc opc,
		struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	enum ice_status status;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	if (opc != ice_aqc_opc_add_sw_rules &&
	    opc != ice_aqc_opc_update_sw_rules &&
	    opc != ice_aqc_opc_remove_sw_rules)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, opc);

	desc.flags |= htole16(ICE_AQ_FLAG_RD);
	desc.params.sw_rules.num_rules_fltr_entry_index = htole16(num_rules);
	status = ice_aq_send_cmd(hw, &desc, rule_list, rule_list_sz, cd);
	if (opc != ice_aqc_opc_add_sw_rules &&
	    hw->adminq.sq_last_status == ICE_AQ_RC_ENOENT)
		status = ICE_ERR_DOES_NOT_EXIST;

	return status;
}

/**
 * ice_create_pkt_fwd_rule
 * @hw: pointer to the hardware structure
 * @recp_list: corresponding filter management list
 * @f_entry: entry containing packet forwarding information
 *
 * Create switch rule with given filter information and add an entry
 * to the corresponding filter management list to track this switch rule
 * and VSI mapping
 */
enum ice_status
ice_create_pkt_fwd_rule(struct ice_hw *hw, struct ice_sw_recipe *recp_list,
			struct ice_fltr_list_entry *f_entry)
{
	struct ice_fltr_mgmt_list_entry *fm_entry;
	struct ice_sw_rule_lkup_rx_tx *s_rule;
	enum ice_status status;

	s_rule = (struct ice_sw_rule_lkup_rx_tx *)
		ice_malloc(hw, ice_struct_size(s_rule, hdr_data,
					       ICE_DUMMY_ETH_HDR_LEN));
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;
	fm_entry = (struct ice_fltr_mgmt_list_entry *)
		   ice_malloc(hw, sizeof(*fm_entry));
	if (!fm_entry) {
		status = ICE_ERR_NO_MEMORY;
		goto ice_create_pkt_fwd_rule_exit;
	}

	fm_entry->fltr_info = f_entry->fltr_info;

	/* Initialize all the fields for the management entry */
	fm_entry->vsi_count = 1;
	fm_entry->lg_act_idx = ICE_INVAL_LG_ACT_INDEX;
	fm_entry->sw_marker_id = ICE_INVAL_SW_MARKER_ID;
	fm_entry->counter_index = ICE_INVAL_COUNTER_ID;

	ice_fill_sw_rule(hw, &fm_entry->fltr_info, s_rule,
			 ice_aqc_opc_add_sw_rules);

	status = ice_aq_sw_rules(hw, s_rule,
				 ice_struct_size(s_rule, hdr_data,
						 ICE_DUMMY_ETH_HDR_LEN),
				 1, ice_aqc_opc_add_sw_rules, NULL);
	if (status) {
		ice_free(hw, fm_entry);
		goto ice_create_pkt_fwd_rule_exit;
	}

	f_entry->fltr_info.fltr_rule_id = le16toh(s_rule->index);
	fm_entry->fltr_info.fltr_rule_id = le16toh(s_rule->index);

	/* The book keeping entries will get removed when base driver
	 * calls remove filter AQ command
	 */
	TAILQ_INSERT_HEAD(&recp_list->filt_rules, fm_entry, list_entry);

ice_create_pkt_fwd_rule_exit:
	ice_free(hw, s_rule);
	return status;
}

/**
 * ice_aq_alloc_free_res - command to allocate/free resources
 * @hw: pointer to the HW struct
 * @num_entries: number of resource entries in buffer
 * @buf: Indirect buffer to hold data parameters and response
 * @buf_size: size of buffer for indirect commands
 * @opc: pass in the command opcode
 * @cd: pointer to command details structure or NULL
 *
 * Helper function to allocate/free resources using the admin queue commands
 */
enum ice_status
ice_aq_alloc_free_res(struct ice_hw *hw, uint16_t num_entries,
    struct ice_aqc_alloc_free_res_elem *buf, uint16_t buf_size,
    enum ice_adminq_opc opc, struct ice_sq_cd *cd)
{
	struct ice_aqc_alloc_free_res_cmd *cmd;
	struct ice_aq_desc desc;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	cmd = &desc.params.sw_res_ctrl;

	if (!buf)
		return ICE_ERR_PARAM;

	if (buf_size < FLEX_ARRAY_SIZE(buf, elem, num_entries))
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, opc);

	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	cmd->num_entries = htole16(num_entries);

	return ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
}

/**
 * ice_aq_alloc_free_vsi_list
 * @hw: pointer to the HW struct
 * @vsi_list_id: VSI list ID returned or used for lookup
 * @lkup_type: switch rule filter lookup type
 * @opc: switch rules population command type - pass in the command opcode
 *
 * allocates or free a VSI list resource
 */
enum ice_status
ice_aq_alloc_free_vsi_list(struct ice_hw *hw, uint16_t *vsi_list_id,
			   enum ice_sw_lkup_type lkup_type,
			   enum ice_adminq_opc opc)
{
	struct ice_aqc_alloc_free_res_elem *sw_buf;
	struct ice_aqc_res_elem *vsi_ele;
	enum ice_status status;
	uint16_t buf_len;

	buf_len = ice_struct_size(sw_buf, elem, 1);
	sw_buf = (struct ice_aqc_alloc_free_res_elem *)ice_malloc(hw, buf_len);
	if (!sw_buf)
		return ICE_ERR_NO_MEMORY;
	sw_buf->num_elems = htole16(1);

	if (lkup_type == ICE_SW_LKUP_MAC ||
	    lkup_type == ICE_SW_LKUP_MAC_VLAN ||
	    lkup_type == ICE_SW_LKUP_ETHERTYPE ||
	    lkup_type == ICE_SW_LKUP_ETHERTYPE_MAC ||
	    lkup_type == ICE_SW_LKUP_PROMISC ||
	    lkup_type == ICE_SW_LKUP_PROMISC_VLAN ||
	    lkup_type == ICE_SW_LKUP_DFLT ||
	    lkup_type == ICE_SW_LKUP_LAST) {
		sw_buf->res_type = htole16(ICE_AQC_RES_TYPE_VSI_LIST_REP);
	} else if (lkup_type == ICE_SW_LKUP_VLAN) {
		sw_buf->res_type =
			htole16(ICE_AQC_RES_TYPE_VSI_LIST_PRUNE);
	} else {
		status = ICE_ERR_PARAM;
		goto ice_aq_alloc_free_vsi_list_exit;
	}

	if (opc == ice_aqc_opc_free_res)
		sw_buf->elem[0].e.sw_resp = htole16(*vsi_list_id);

	status = ice_aq_alloc_free_res(hw, 1, sw_buf, buf_len, opc, NULL);
	if (status)
		goto ice_aq_alloc_free_vsi_list_exit;

	if (opc == ice_aqc_opc_alloc_res) {
		vsi_ele = &sw_buf->elem[0];
		*vsi_list_id = le16toh(vsi_ele->e.sw_resp);
	}

ice_aq_alloc_free_vsi_list_exit:
	ice_free(hw, sw_buf);
	return status;
}

/**
 * ice_update_vsi_list_rule
 * @hw: pointer to the hardware structure
 * @vsi_handle_arr: array of VSI handles to form a VSI list
 * @num_vsi: number of VSI handles in the array
 * @vsi_list_id: VSI list ID generated as part of allocate resource
 * @remove: Boolean value to indicate if this is a remove action
 * @opc: switch rules population command type - pass in the command opcode
 * @lkup_type: lookup type of the filter
 *
 * Call AQ command to add a new switch rule or update existing switch rule
 * using the given VSI list ID
 */
enum ice_status
ice_update_vsi_list_rule(struct ice_hw *hw, uint16_t *vsi_handle_arr,
    uint16_t num_vsi, uint16_t vsi_list_id, bool remove,
    enum ice_adminq_opc opc, enum ice_sw_lkup_type lkup_type)
{
	struct ice_sw_rule_vsi_list *s_rule;
	enum ice_status status;
	uint16_t s_rule_size;
	uint16_t rule_type;
	int i;

	if (!num_vsi)
		return ICE_ERR_PARAM;

	if (lkup_type == ICE_SW_LKUP_MAC ||
	    lkup_type == ICE_SW_LKUP_MAC_VLAN ||
	    lkup_type == ICE_SW_LKUP_ETHERTYPE ||
	    lkup_type == ICE_SW_LKUP_ETHERTYPE_MAC ||
	    lkup_type == ICE_SW_LKUP_PROMISC ||
	    lkup_type == ICE_SW_LKUP_PROMISC_VLAN ||
	    lkup_type == ICE_SW_LKUP_DFLT ||
	    lkup_type == ICE_SW_LKUP_LAST)
		rule_type = remove ? ICE_AQC_SW_RULES_T_VSI_LIST_CLEAR :
			ICE_AQC_SW_RULES_T_VSI_LIST_SET;
	else if (lkup_type == ICE_SW_LKUP_VLAN)
		rule_type = remove ? ICE_AQC_SW_RULES_T_PRUNE_LIST_CLEAR :
			ICE_AQC_SW_RULES_T_PRUNE_LIST_SET;
	else
		return ICE_ERR_PARAM;

	s_rule_size = (uint16_t)ice_struct_size(s_rule, vsi, num_vsi);
	s_rule = (struct ice_sw_rule_vsi_list *)ice_malloc(hw, s_rule_size);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;
	for (i = 0; i < num_vsi; i++) {
		if (!ice_is_vsi_valid(hw, vsi_handle_arr[i])) {
			status = ICE_ERR_PARAM;
			goto exit;
		}
		/* AQ call requires hw_vsi_id(s) */
		s_rule->vsi[i] =
			htole16(hw->vsi_ctx[vsi_handle_arr[i]]->vsi_num);
	}

	s_rule->hdr.type = htole16(rule_type);
	s_rule->number_vsi = htole16(num_vsi);
	s_rule->index = htole16(vsi_list_id);

	status = ice_aq_sw_rules(hw, s_rule, s_rule_size, 1, opc, NULL);

exit:
	ice_free(hw, s_rule);
	return status;
}

/**
 * ice_create_vsi_list_rule - Creates and populates a VSI list rule
 * @hw: pointer to the HW struct
 * @vsi_handle_arr: array of VSI handles to form a VSI list
 * @num_vsi: number of VSI handles in the array
 * @vsi_list_id: stores the ID of the VSI list to be created
 * @lkup_type: switch rule filter's lookup type
 */
enum ice_status
ice_create_vsi_list_rule(struct ice_hw *hw, uint16_t *vsi_handle_arr,
     uint16_t num_vsi, uint16_t *vsi_list_id, enum ice_sw_lkup_type lkup_type)
{
	enum ice_status status;

	status = ice_aq_alloc_free_vsi_list(hw, vsi_list_id, lkup_type,
					    ice_aqc_opc_alloc_res);
	if (status)
		return status;

	/* Update the newly created VSI list to include the specified VSIs */
	return ice_update_vsi_list_rule(hw, vsi_handle_arr, num_vsi,
					*vsi_list_id, false,
					ice_aqc_opc_add_sw_rules, lkup_type);
}

/**
 * ice_update_pkt_fwd_rule
 * @hw: pointer to the hardware structure
 * @f_info: filter information for switch rule
 *
 * Call AQ command to update a previously created switch rule with a
 * VSI list ID
 */
enum ice_status
ice_update_pkt_fwd_rule(struct ice_hw *hw, struct ice_fltr_info *f_info)
{
	struct ice_sw_rule_lkup_rx_tx *s_rule;
	enum ice_status status;

	s_rule = (struct ice_sw_rule_lkup_rx_tx *)
		ice_malloc(hw, ice_struct_size(s_rule, hdr_data,
					       ICE_DUMMY_ETH_HDR_LEN));
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;

	ice_fill_sw_rule(hw, f_info, s_rule, ice_aqc_opc_update_sw_rules);

	s_rule->index = htole16(f_info->fltr_rule_id);

	/* Update switch rule with new rule set to forward VSI list */
	status = ice_aq_sw_rules(hw, s_rule,
				 ice_struct_size(s_rule, hdr_data,
						 ICE_DUMMY_ETH_HDR_LEN),
				 1, ice_aqc_opc_update_sw_rules, NULL);

	ice_free(hw, s_rule);
	return status;
}

/**
 * ice_create_vsi_list_map
 * @hw: pointer to the hardware structure
 * @vsi_handle_arr: array of VSI handles to set in the VSI mapping
 * @num_vsi: number of VSI handles in the array
 * @vsi_list_id: VSI list ID generated as part of allocate resource
 *
 * Helper function to create a new entry of VSI list ID to VSI mapping
 * using the given VSI list ID
 */
struct ice_vsi_list_map_info *
ice_create_vsi_list_map(struct ice_hw *hw, uint16_t *vsi_handle_arr,
    uint16_t num_vsi, uint16_t vsi_list_id)
{
	struct ice_switch_info *sw = hw->switch_info;
	struct ice_vsi_list_map_info *v_map;
	int i;

	v_map = (struct ice_vsi_list_map_info *)ice_malloc(hw, sizeof(*v_map));
	if (!v_map)
		return NULL;

	v_map->vsi_list_id = vsi_list_id;
	v_map->ref_cnt = 1;
	for (i = 0; i < num_vsi; i++)
		ice_set_bit(vsi_handle_arr[i], v_map->vsi_map);

	TAILQ_INSERT_HEAD(&sw->vsi_list_map_head, v_map, list_entry);
	return v_map;
}

/**
 * ice_add_marker_act
 * @hw: pointer to the hardware structure
 * @m_ent: the management entry for which sw marker needs to be added
 * @sw_marker: sw marker to tag the Rx descriptor with
 * @l_id: large action resource ID
 *
 * Create a large action to hold software marker and update the switch rule
 * entry pointed by m_ent with newly created large action
 */
enum ice_status
ice_add_marker_act(struct ice_hw *hw, struct ice_fltr_mgmt_list_entry *m_ent,
		   uint16_t sw_marker, uint16_t l_id)
{
	struct ice_sw_rule_lkup_rx_tx *rx_tx;
	struct ice_sw_rule_lg_act *lg_act;
	/* For software marker we need 3 large actions
	 * 1. FWD action: FWD TO VSI or VSI LIST
	 * 2. GENERIC VALUE action to hold the profile ID
	 * 3. GENERIC VALUE action to hold the software marker ID
	 */
	const uint16_t num_lg_acts = 3;
	enum ice_status status;
	uint16_t lg_act_size;
	uint16_t rules_size;
	uint32_t act;
	uint16_t id;

	if (m_ent->fltr_info.lkup_type != ICE_SW_LKUP_MAC)
		return ICE_ERR_PARAM;

	/* Create two back-to-back switch rules and submit them to the HW using
	 * one memory buffer:
	 *    1. Large Action
	 *    2. Look up Tx Rx
	 */
	lg_act_size = (uint16_t)ice_struct_size(lg_act, act, num_lg_acts);
	rules_size = lg_act_size +
		     ice_struct_size(rx_tx, hdr_data, ICE_DUMMY_ETH_HDR_LEN);
	lg_act = (struct ice_sw_rule_lg_act *)ice_malloc(hw, rules_size);
	if (!lg_act)
		return ICE_ERR_NO_MEMORY;

	rx_tx = (struct ice_sw_rule_lkup_rx_tx *)((uint8_t *)lg_act +
	    lg_act_size);

	/* Fill in the first switch rule i.e. large action */
	lg_act->hdr.type = htole16(ICE_AQC_SW_RULES_T_LG_ACT);
	lg_act->index = htole16(l_id);
	lg_act->size = htole16(num_lg_acts);

	/* First action VSI forwarding or VSI list forwarding depending on how
	 * many VSIs
	 */
	id = (m_ent->vsi_count > 1) ? m_ent->fltr_info.fwd_id.vsi_list_id :
		m_ent->fltr_info.fwd_id.hw_vsi_id;

	act = ICE_LG_ACT_VSI_FORWARDING | ICE_LG_ACT_VALID_BIT;
	act |= (id << ICE_LG_ACT_VSI_LIST_ID_S) & ICE_LG_ACT_VSI_LIST_ID_M;
	if (m_ent->vsi_count > 1)
		act |= ICE_LG_ACT_VSI_LIST;
	lg_act->act[0] = htole32(act);

	/* Second action descriptor type */
	act = ICE_LG_ACT_GENERIC;

	act |= (1 << ICE_LG_ACT_GENERIC_VALUE_S) & ICE_LG_ACT_GENERIC_VALUE_M;
	lg_act->act[1] = htole32(act);

	act = (ICE_LG_ACT_GENERIC_OFF_RX_DESC_PROF_IDX <<
	       ICE_LG_ACT_GENERIC_OFFSET_S) & ICE_LG_ACT_GENERIC_OFFSET_M;

	/* Third action Marker value */
	act |= ICE_LG_ACT_GENERIC;
	act |= (sw_marker << ICE_LG_ACT_GENERIC_VALUE_S) &
		ICE_LG_ACT_GENERIC_VALUE_M;

	lg_act->act[2] = htole32(act);

	/* call the fill switch rule to fill the lookup Tx Rx structure */
	ice_fill_sw_rule(hw, &m_ent->fltr_info, rx_tx,
			 ice_aqc_opc_update_sw_rules);

	/* Update the action to point to the large action ID */
	rx_tx->act = htole32(ICE_SINGLE_ACT_PTR |
	    ((l_id << ICE_SINGLE_ACT_PTR_VAL_S) & ICE_SINGLE_ACT_PTR_VAL_M));

	/* Use the filter rule ID of the previously created rule with single
	 * act. Once the update happens, hardware will treat this as large
	 * action
	 */
	rx_tx->index = htole16(m_ent->fltr_info.fltr_rule_id);

	status = ice_aq_sw_rules(hw, lg_act, rules_size, 2,
				 ice_aqc_opc_update_sw_rules, NULL);
	if (!status) {
		m_ent->lg_act_idx = l_id;
		m_ent->sw_marker_id = sw_marker;
	}

	ice_free(hw, lg_act);
	return status;
}

/**
 * ice_add_update_vsi_list
 * @hw: pointer to the hardware structure
 * @m_entry: pointer to current filter management list entry
 * @cur_fltr: filter information from the book keeping entry
 * @new_fltr: filter information with the new VSI to be added
 *
 * Call AQ command to add or update previously created VSI list with new VSI.
 *
 * Helper function to do book keeping associated with adding filter information
 * The algorithm to do the book keeping is described below :
 * When a VSI needs to subscribe to a given filter (MAC/VLAN/Ethtype etc.)
 *	if only one VSI has been added till now
 *		Allocate a new VSI list and add two VSIs
 *		to this list using switch rule command
 *		Update the previously created switch rule with the
 *		newly created VSI list ID
 *	if a VSI list was previously created
 *		Add the new VSI to the previously created VSI list set
 *		using the update switch rule command
 */
enum ice_status
ice_add_update_vsi_list(struct ice_hw *hw,
			struct ice_fltr_mgmt_list_entry *m_entry,
			struct ice_fltr_info *cur_fltr,
			struct ice_fltr_info *new_fltr)
{
	enum ice_status status = ICE_SUCCESS;
	uint16_t vsi_list_id = 0;

	if ((cur_fltr->fltr_act == ICE_FWD_TO_Q ||
	     cur_fltr->fltr_act == ICE_FWD_TO_QGRP))
		return ICE_ERR_NOT_IMPL;

	if ((new_fltr->fltr_act == ICE_FWD_TO_Q ||
	     new_fltr->fltr_act == ICE_FWD_TO_QGRP) &&
	    (cur_fltr->fltr_act == ICE_FWD_TO_VSI ||
	     cur_fltr->fltr_act == ICE_FWD_TO_VSI_LIST))
		return ICE_ERR_NOT_IMPL;

	if (m_entry->vsi_count < 2 && !m_entry->vsi_list_info) {
		/* Only one entry existed in the mapping and it was not already
		 * a part of a VSI list. So, create a VSI list with the old and
		 * new VSIs.
		 */
		struct ice_fltr_info tmp_fltr;
		uint16_t vsi_handle_arr[2];

		/* A rule already exists with the new VSI being added */
		if (cur_fltr->fwd_id.hw_vsi_id == new_fltr->fwd_id.hw_vsi_id)
			return ICE_ERR_ALREADY_EXISTS;

		vsi_handle_arr[0] = cur_fltr->vsi_handle;
		vsi_handle_arr[1] = new_fltr->vsi_handle;
		status = ice_create_vsi_list_rule(hw, &vsi_handle_arr[0], 2,
						  &vsi_list_id,
						  new_fltr->lkup_type);
		if (status)
			return status;

		tmp_fltr = *new_fltr;
		tmp_fltr.fltr_rule_id = cur_fltr->fltr_rule_id;
		tmp_fltr.fltr_act = ICE_FWD_TO_VSI_LIST;
		tmp_fltr.fwd_id.vsi_list_id = vsi_list_id;
		/* Update the previous switch rule of "MAC forward to VSI" to
		 * "MAC fwd to VSI list"
		 */
		status = ice_update_pkt_fwd_rule(hw, &tmp_fltr);
		if (status)
			return status;

		cur_fltr->fwd_id.vsi_list_id = vsi_list_id;
		cur_fltr->fltr_act = ICE_FWD_TO_VSI_LIST;
		m_entry->vsi_list_info =
			ice_create_vsi_list_map(hw, &vsi_handle_arr[0], 2,
						vsi_list_id);

		if (!m_entry->vsi_list_info)
			return ICE_ERR_NO_MEMORY;

		/* If this entry was large action then the large action needs
		 * to be updated to point to FWD to VSI list
		 */
		if (m_entry->sw_marker_id != ICE_INVAL_SW_MARKER_ID)
			status =
			    ice_add_marker_act(hw, m_entry,
					       m_entry->sw_marker_id,
					       m_entry->lg_act_idx);
	} else {
		uint16_t vsi_handle = new_fltr->vsi_handle;
		enum ice_adminq_opc opcode;

		if (!m_entry->vsi_list_info)
			return ICE_ERR_CFG;

		/* A rule already exists with the new VSI being added */
		if (ice_is_bit_set(m_entry->vsi_list_info->vsi_map, vsi_handle))
			return ICE_SUCCESS;

		/* Update the previously created VSI list set with
		 * the new VSI ID passed in
		 */
		vsi_list_id = cur_fltr->fwd_id.vsi_list_id;
		opcode = ice_aqc_opc_update_sw_rules;

		status = ice_update_vsi_list_rule(hw, &vsi_handle, 1,
						  vsi_list_id, false, opcode,
						  new_fltr->lkup_type);
		/* update VSI list mapping info with new VSI ID */
		if (!status)
			ice_set_bit(vsi_handle,
				    m_entry->vsi_list_info->vsi_map);
	}
	if (!status)
		m_entry->vsi_count++;
	return status;
}

/**
 * ice_add_rule_internal - add rule for a given lookup type
 * @hw: pointer to the hardware structure
 * @recp_list: recipe list for which rule has to be added
 * @lport: logic port number on which function add rule
 * @f_entry: structure containing MAC forwarding information
 *
 * Adds or updates the rule lists for a given recipe
 */
enum ice_status
ice_add_rule_internal(struct ice_hw *hw, struct ice_sw_recipe *recp_list,
		      uint8_t lport, struct ice_fltr_list_entry *f_entry)
{
	struct ice_fltr_info *new_fltr, *cur_fltr;
	struct ice_fltr_mgmt_list_entry *m_entry;
	struct ice_lock *rule_lock; /* Lock to protect filter rule list */
	enum ice_status status = ICE_SUCCESS;

	if (!ice_is_vsi_valid(hw, f_entry->fltr_info.vsi_handle))
		return ICE_ERR_PARAM;

	/* Load the hw_vsi_id only if the fwd action is fwd to VSI */
	if (f_entry->fltr_info.fltr_act == ICE_FWD_TO_VSI)
		f_entry->fltr_info.fwd_id.hw_vsi_id =
			hw->vsi_ctx[f_entry->fltr_info.vsi_handle]->vsi_num;

	rule_lock = &recp_list->filt_rule_lock;
#if 0
	ice_acquire_lock(rule_lock);
#endif
	new_fltr = &f_entry->fltr_info;
	if (new_fltr->flag & ICE_FLTR_RX)
		new_fltr->src = lport;
	else if (new_fltr->flag & (ICE_FLTR_TX | ICE_FLTR_RX_LB))
		new_fltr->src =
			hw->vsi_ctx[f_entry->fltr_info.vsi_handle]->vsi_num;

	m_entry = ice_find_rule_entry(&recp_list->filt_rules, new_fltr);
	if (!m_entry) {
		status = ice_create_pkt_fwd_rule(hw, recp_list, f_entry);
		goto exit_add_rule_internal;
	}

	cur_fltr = &m_entry->fltr_info;
	status = ice_add_update_vsi_list(hw, m_entry, cur_fltr, new_fltr);

exit_add_rule_internal:
#if 0
	ice_release_lock(rule_lock);
#endif
	return status;
}

/**
 * ice_add_mac_rule - Add a MAC address based filter rule
 * @hw: pointer to the hardware structure
 * @list: list of MAC addresses and forwarding information
 * @sw: pointer to switch info struct for which function add rule
 * @lport: logic port number on which function add rule
 *
 * IMPORTANT: When the umac_shared flag is set to false and 'list' has
 * multiple unicast addresses, the function assumes that all the
 * addresses are unique in a given add_mac call. It doesn't
 * check for duplicates in this case, removing duplicates from a given
 * list should be taken care of in the caller of this function.
 */
enum ice_status
ice_add_mac_rule(struct ice_hw *hw, struct ice_fltr_list_head *list,
		 struct ice_switch_info *sw, uint8_t lport)
{
	struct ice_sw_recipe *recp_list = &sw->recp_list[ICE_SW_LKUP_MAC];
	struct ice_sw_rule_lkup_rx_tx *s_rule, *r_iter;
	struct ice_fltr_list_entry *list_itr;
	struct ice_fltr_mgmt_list_head *rule_head;
	uint16_t total_elem_left, s_rule_size;
#if 0
	struct ice_lock *rule_lock; /* Lock to protect filter rule list */
#endif
	enum ice_status status = ICE_SUCCESS;
	uint16_t num_unicast = 0;
	uint8_t elem_sent;

	s_rule = NULL;
#if 0
	rule_lock = &recp_list->filt_rule_lock;
#endif
	rule_head = &recp_list->filt_rules;

	TAILQ_FOREACH(list_itr, list, list_entry) {
		uint8_t *add = &list_itr->fltr_info.l_data.mac.mac_addr[0];
		uint16_t vsi_handle;
		uint16_t hw_vsi_id;

		list_itr->fltr_info.flag = ICE_FLTR_TX;
		vsi_handle = list_itr->fltr_info.vsi_handle;
		if (!ice_is_vsi_valid(hw, vsi_handle))
			return ICE_ERR_PARAM;
		hw_vsi_id = hw->vsi_ctx[vsi_handle]->vsi_num;
		if (list_itr->fltr_info.fltr_act == ICE_FWD_TO_VSI)
			list_itr->fltr_info.fwd_id.hw_vsi_id = hw_vsi_id;
		/* update the src in case it is VSI num */
		if (list_itr->fltr_info.src_id != ICE_SRC_ID_VSI)
			return ICE_ERR_PARAM;
		list_itr->fltr_info.src = hw_vsi_id;
		if (list_itr->fltr_info.lkup_type != ICE_SW_LKUP_MAC ||
		    ETHER_IS_ANYADDR(add))
			return ICE_ERR_PARAM;
		if (!ETHER_IS_MULTICAST(add) && !hw->umac_shared) {
			/* Don't overwrite the unicast address */
#if 0
			ice_acquire_lock(rule_lock);
#endif
			if (ice_find_rule_entry(rule_head,
						&list_itr->fltr_info)) {
#if 0
				ice_release_lock(rule_lock);
#endif
				continue;
			}
#if 0
			ice_release_lock(rule_lock);
#endif
			num_unicast++;
		} else if (ETHER_IS_MULTICAST(add) || hw->umac_shared) {
			list_itr->status =
				ice_add_rule_internal(hw, recp_list, lport,
						      list_itr);
			if (list_itr->status)
				return list_itr->status;
		}
	}
#if 0
	ice_acquire_lock(rule_lock);
#endif
	/* Exit if no suitable entries were found for adding bulk switch rule */
	if (!num_unicast) {
		status = ICE_SUCCESS;
		goto ice_add_mac_exit;
	}

	/* Allocate switch rule buffer for the bulk update for unicast */
	s_rule_size = ice_struct_size(s_rule, hdr_data, ICE_DUMMY_ETH_HDR_LEN);
	s_rule = (struct ice_sw_rule_lkup_rx_tx *)
		ice_calloc(hw, num_unicast, s_rule_size);
	if (!s_rule) {
		status = ICE_ERR_NO_MEMORY;
		goto ice_add_mac_exit;
	}

	r_iter = s_rule;
	TAILQ_FOREACH(list_itr, list, list_entry) {
		struct ice_fltr_info *f_info = &list_itr->fltr_info;
		uint8_t *mac_addr = &f_info->l_data.mac.mac_addr[0];

		if (!ETHER_IS_MULTICAST(mac_addr)) {
			ice_fill_sw_rule(hw, &list_itr->fltr_info, r_iter,
					 ice_aqc_opc_add_sw_rules);
			r_iter = (struct ice_sw_rule_lkup_rx_tx *)
				((uint8_t *)r_iter + s_rule_size);
		}
	}

	/* Call AQ bulk switch rule update for all unicast addresses */
	r_iter = s_rule;
	/* Call AQ switch rule in AQ_MAX chunk */
	for (total_elem_left = num_unicast; total_elem_left > 0;
	     total_elem_left -= elem_sent) {
		struct ice_sw_rule_lkup_rx_tx *entry = r_iter;

		elem_sent = MIN(total_elem_left,
		    (ICE_AQ_MAX_BUF_LEN / s_rule_size));
		status = ice_aq_sw_rules(hw, entry, elem_sent * s_rule_size,
					 elem_sent, ice_aqc_opc_add_sw_rules,
					 NULL);
		if (status)
			goto ice_add_mac_exit;
		r_iter = (struct ice_sw_rule_lkup_rx_tx *)
			((uint8_t *)r_iter + (elem_sent * s_rule_size));
	}

	/* Fill up rule ID based on the value returned from FW */
	r_iter = s_rule;
	TAILQ_FOREACH(list_itr, list, list_entry) {
		struct ice_fltr_info *f_info = &list_itr->fltr_info;
		uint8_t *mac_addr = &f_info->l_data.mac.mac_addr[0];
		struct ice_fltr_mgmt_list_entry *fm_entry;

		if (!ETHER_IS_MULTICAST(mac_addr)) {
			f_info->fltr_rule_id = le16toh(r_iter->index);
			f_info->fltr_act = ICE_FWD_TO_VSI;
			/* Create an entry to track this MAC address */
			fm_entry = (struct ice_fltr_mgmt_list_entry *)
				ice_malloc(hw, sizeof(*fm_entry));
			if (!fm_entry) {
				status = ICE_ERR_NO_MEMORY;
				goto ice_add_mac_exit;
			}
			fm_entry->fltr_info = *f_info;
			fm_entry->vsi_count = 1;
			/* The book keeping entries will get removed when
			 * base driver calls remove filter AQ command
			 */

			TAILQ_INSERT_HEAD(rule_head, fm_entry, list_entry);
			r_iter = (struct ice_sw_rule_lkup_rx_tx *)
				((uint8_t *)r_iter + s_rule_size);
		}
	}

ice_add_mac_exit:
#if 0
	ice_release_lock(rule_lock);
#endif
	if (s_rule)
		ice_free(hw, s_rule);
	return status;
}

/**
 * ice_add_mac - Add a MAC address based filter rule
 * @hw: pointer to the hardware structure
 * @list: list of MAC addresses and forwarding information
 *
 * Function add MAC rule for logical port from HW struct
 */
enum ice_status
ice_add_mac(struct ice_hw *hw, struct ice_fltr_list_head *list)
{
	if (!list || !hw)
		return ICE_ERR_PARAM;

	return ice_add_mac_rule(hw, list, hw->switch_info,
	    hw->port_info->lport);
}

/**
 * ice_add_vsi_mac_filter - Add a MAC address filter for a VSI
 * @vsi: the VSI to add the filter for
 * @addr: MAC address to add a filter for
 *
 * Add a MAC address filter for a given VSI. This is a wrapper around
 * ice_add_mac to simplify the interface. First, it only accepts a single
 * address, so we don't have to mess around with the list setup in other
 * functions. Second, it ignores the ICE_ERR_ALREADY_EXISTS error, so that
 * callers don't need to worry about attempting to add the same filter twice.
 */
int
ice_add_vsi_mac_filter(struct ice_vsi *vsi, uint8_t *addr)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_fltr_list_head mac_addr_list;
	struct ice_hw *hw = &vsi->sc->hw;
	enum ice_status status;
	int err = 0;

	TAILQ_INIT(&mac_addr_list);

	err = ice_add_mac_to_list(vsi, &mac_addr_list, addr, ICE_FWD_TO_VSI);
	if (err)
		goto free_mac_list;

	status = ice_add_mac(hw, &mac_addr_list);
	if (status != ICE_ERR_ALREADY_EXISTS && status) {
		printf("%s: Failed to add a filter for MAC %s, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		    ether_sprintf(addr), ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		err = (EIO);
	}

free_mac_list:
	ice_free_fltr_list(&mac_addr_list);
	return err;
}

/**
 * ice_cfg_pf_default_mac_filters - Setup default unicast and broadcast addrs
 * @sc: device softc structure
 *
 * Program the default unicast and broadcast filters for the PF VSI.
 */
int
ice_cfg_pf_default_mac_filters(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	struct ice_vsi *vsi = &sc->pf_vsi;
	int err;

	/* Add the LAN MAC address */
	err = ice_add_vsi_mac_filter(vsi, hw->port_info->mac.lan_addr);
	if (err)
		return err;

	/* Add the broadcast address */
	err = ice_add_vsi_mac_filter(vsi, etherbroadcastaddr);
	if (err)
		return err;

	return (0);
}

/**
 * ice_init_tx_tracking - Initialize Tx queue software tracking values
 * @vsi: the VSI to initialize
 *
 * Initialize Tx queue software tracking values, including the Report Status
 * queue, and related software tracking values.
 */
void
ice_init_tx_tracking(struct ice_vsi *vsi)
{
	struct ice_tx_queue *txq;
	size_t j;
	int i;

	for (i = 0, txq = vsi->tx_queues; i < vsi->num_tx_queues; i++, txq++) {

		txq->tx_rs_cidx = txq->tx_rs_pidx = 0;

		/* Initialize the last processed descriptor to be the end of
		 * the ring, rather than the start, so that we avoid an
		 * off-by-one error in ice_ift_txd_credits_update for the
		 * first packet.
		 */
		txq->tx_cidx_processed = txq->desc_count - 1;

		for (j = 0; j < txq->desc_count; j++)
			txq->tx_rsq[j] = ICE_QIDX_INVALID;
	}
}

/**
 * ice_setup_tx_ctx - Setup an ice_tlan_ctx structure for a queue
 * @txq: the Tx queue to configure
 * @tlan_ctx: the Tx LAN queue context structure to initialize
 * @pf_q: real queue number
 */
int
ice_setup_tx_ctx(struct ice_tx_queue *txq, struct ice_tlan_ctx *tlan_ctx,
    uint16_t pf_q)
{
	struct ice_vsi *vsi = txq->vsi;
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;

	tlan_ctx->port_num = hw->port_info->lport;

	/* number of descriptors in the queue */
	tlan_ctx->qlen = txq->desc_count;

	/* set the transmit queue base address, defined in 128 byte units */
	tlan_ctx->base = txq->tx_paddr >> 7;

	tlan_ctx->pf_num = hw->pf_id;

	switch (vsi->type) {
	case ICE_VSI_PF:
		tlan_ctx->vmvf_type = ICE_TLAN_CTX_VMVF_TYPE_PF;
		break;
	case ICE_VSI_VMDQ2:
		tlan_ctx->vmvf_type = ICE_TLAN_CTX_VMVF_TYPE_VMQ;
		break;
	default:
		return (ENODEV);
	}

	tlan_ctx->src_vsi = hw->vsi_ctx[vsi->idx]->vsi_num;

	/* Enable TSO */
	tlan_ctx->tso_ena = 1;
	tlan_ctx->internal_usage_flag = 1;

	tlan_ctx->tso_qnum = pf_q;

	/*
	 * Stick with the older legacy Tx queue interface, instead of the new
	 * advanced queue interface.
	 */
	tlan_ctx->legacy_int = 1;

	/* Descriptor WB mode */
	tlan_ctx->wb_mode = 0;

	return (0);
}

/**
 * ice_get_vsi_ctx - return the VSI context entry for a given VSI handle
 * @hw: pointer to the HW struct
 * @vsi_handle: VSI handle
 *
 * return the VSI context entry for a given VSI handle
 */
struct ice_vsi_ctx *
ice_get_vsi_ctx(struct ice_hw *hw, uint16_t vsi_handle)
{
	return (vsi_handle >= ICE_MAX_VSI) ? NULL : hw->vsi_ctx[vsi_handle];
}

/**
 * ice_get_lan_q_ctx - get the LAN queue context for the given VSI and TC
 * @hw: pointer to the HW struct
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @q_handle: software queue handle
 */
struct ice_q_ctx *
ice_get_lan_q_ctx(struct ice_hw *hw, uint16_t vsi_handle, uint8_t tc,
    uint16_t q_handle)
{
	struct ice_vsi_ctx *vsi;
	struct ice_q_ctx *q_ctx;

	vsi = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi)
		return NULL;
	if (q_handle >= vsi->num_lan_q_entries[tc])
		return NULL;
	if (!vsi->lan_q_ctx[tc])
		return NULL;
	q_ctx = vsi->lan_q_ctx[tc];
	return &q_ctx[q_handle];
}

/**
 * ice_sched_find_node_in_subtree - Find node in part of base node subtree
 * @hw: pointer to the HW struct
 * @base: pointer to the base node
 * @node: pointer to the node to search
 *
 * This function checks whether a given node is part of the base node
 * subtree or not
 */
bool
ice_sched_find_node_in_subtree(struct ice_hw *hw, struct ice_sched_node *base,
			       struct ice_sched_node *node)
{
	uint8_t i;

	for (i = 0; i < base->num_children; i++) {
		struct ice_sched_node *child = base->children[i];

		if (node == child)
			return true;

		if (child->tx_sched_layer > node->tx_sched_layer)
			return false;

		/* this recursion is intentional, and wouldn't
		 * go more than 8 calls
		 */
		if (ice_sched_find_node_in_subtree(hw, child, node))
			return true;
	}
	return false;
}

/**
 * ice_sched_get_free_qgrp - Scan all queue group siblings and find a free node
 * @pi: port information structure
 * @vsi_node: software VSI handle
 * @qgrp_node: first queue group node identified for scanning
 * @owner: LAN or RDMA
 *
 * This function retrieves a free LAN or RDMA queue group node by scanning
 * qgrp_node and its siblings for the queue group with the fewest number
 * of queues currently assigned.
 */
struct ice_sched_node *
ice_sched_get_free_qgrp(struct ice_port_info *pi,
			struct ice_sched_node *vsi_node,
			struct ice_sched_node *qgrp_node, uint8_t owner)
{
	struct ice_sched_node *min_qgrp;
	uint8_t min_children;

	if (!qgrp_node)
		return qgrp_node;
	min_children = qgrp_node->num_children;
	if (!min_children)
		return qgrp_node;
	min_qgrp = qgrp_node;
	/* scan all queue groups until find a node which has less than the
	 * minimum number of children. This way all queue group nodes get
	 * equal number of shares and active. The bandwidth will be equally
	 * distributed across all queues.
	 */
	while (qgrp_node) {
		/* make sure the qgroup node is part of the VSI subtree */
		if (ice_sched_find_node_in_subtree(pi->hw, vsi_node, qgrp_node))
			if (qgrp_node->num_children < min_children &&
			    qgrp_node->owner == owner) {
				/* replace the new min queue group node */
				min_qgrp = qgrp_node;
				min_children = min_qgrp->num_children;
				/* break if it has no children, */
				if (!min_children)
					break;
			}
		qgrp_node = qgrp_node->sibling;
	}
	return min_qgrp;
}

/**
 * ice_sched_get_qgrp_layer - get the current queue group layer number
 * @hw: pointer to the HW struct
 *
 * This function returns the current queue group layer number
 */
uint8_t
ice_sched_get_qgrp_layer(struct ice_hw *hw)
{
	/* It's always total layers - 1, the array is 0 relative so -2 */
	return hw->num_tx_sched_layers - ICE_QGRP_LAYER_OFFSET;
}

/**
 * ice_sched_get_vsi_layer - get the current VSI layer number
 * @hw: pointer to the HW struct
 *
 * This function returns the current VSI layer number
 */
uint8_t
ice_sched_get_vsi_layer(struct ice_hw *hw)
{
	/* Num Layers       VSI layer
	 *     9               6
	 *     7               4
	 *     5 or less       sw_entry_point_layer
	 */
	/* calculate the VSI layer based on number of layers. */
	if (hw->num_tx_sched_layers == ICE_SCHED_9_LAYERS)
		return hw->num_tx_sched_layers - ICE_VSI_LAYER_OFFSET;
	else if (hw->num_tx_sched_layers == ICE_SCHED_5_LAYERS)
		/* qgroup and VSI layers are same */
		return hw->num_tx_sched_layers - ICE_QGRP_LAYER_OFFSET;
	return hw->sw_entry_point_layer;
}

/**
 * ice_sched_get_free_qparent - Get a free LAN or RDMA queue group node
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: branch number
 * @owner: LAN or RDMA
 *
 * This function retrieves a free LAN or RDMA queue group node
 */
struct ice_sched_node *
ice_sched_get_free_qparent(struct ice_port_info *pi, uint16_t vsi_handle,
    uint8_t tc, uint8_t owner)
{
	struct ice_sched_node *vsi_node, *qgrp_node;
	struct ice_vsi_ctx *vsi_ctx;
	uint8_t qgrp_layer, vsi_layer;
	uint16_t max_children;

	qgrp_layer = ice_sched_get_qgrp_layer(pi->hw);
	vsi_layer = ice_sched_get_vsi_layer(pi->hw);
	max_children = pi->hw->max_children[qgrp_layer];

	vsi_ctx = ice_get_vsi_ctx(pi->hw, vsi_handle);
	if (!vsi_ctx)
		return NULL;
	vsi_node = vsi_ctx->sched.vsi_node[tc];
	/* validate invalid VSI ID */
	if (!vsi_node)
		return NULL;

	/* If the queue group and vsi layer are same then queues
	 * are all attached directly to VSI
	 */
	if (qgrp_layer == vsi_layer)
		return vsi_node;

	/* get the first queue group node from VSI sub-tree */
	qgrp_node = ice_sched_get_first_node(pi, vsi_node, qgrp_layer);
	while (qgrp_node) {
		/* make sure the qgroup node is part of the VSI subtree */
		if (ice_sched_find_node_in_subtree(pi->hw, vsi_node, qgrp_node))
			if (qgrp_node->num_children < max_children &&
			    qgrp_node->owner == owner)
				break;
		qgrp_node = qgrp_node->sibling;
	}

	/* Select the best queue group */
	return ice_sched_get_free_qgrp(pi, vsi_node, qgrp_node, owner);
}

/**
 * ice_aq_add_lan_txq
 * @hw: pointer to the hardware structure
 * @num_qgrps: Number of added queue groups
 * @qg_list: list of queue groups to be added
 * @buf_size: size of buffer for indirect command
 * @cd: pointer to command details structure or NULL
 *
 * Add Tx LAN queue (0x0C30)
 *
 * NOTE:
 * Prior to calling add Tx LAN queue:
 * Initialize the following as part of the Tx queue context:
 * Completion queue ID if the queue uses Completion queue, Quanta profile,
 * Cache profile and Packet shaper profile.
 *
 * After add Tx LAN queue AQ command is completed:
 * Interrupts should be associated with specific queues,
 * Association of Tx queue to Doorbell queue is not part of Add LAN Tx queue
 * flow.
 */
enum ice_status
ice_aq_add_lan_txq(struct ice_hw *hw, uint8_t num_qgrps,
		   struct ice_aqc_add_tx_qgrp *qg_list, uint16_t buf_size,
		   struct ice_sq_cd *cd)
{
	struct ice_aqc_add_tx_qgrp *list;
	struct ice_aqc_add_txqs *cmd;
	struct ice_aq_desc desc;
	uint16_t i, sum_size = 0;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	cmd = &desc.params.add_txqs;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_add_txqs);

	if (!qg_list)
		return ICE_ERR_PARAM;

	if (num_qgrps > ICE_LAN_TXQ_MAX_QGRPS)
		return ICE_ERR_PARAM;

	for (i = 0, list = qg_list; i < num_qgrps; i++) {
		sum_size += ice_struct_size(list, txqs, list->num_txqs);
		list = (struct ice_aqc_add_tx_qgrp *)(list->txqs +
						      list->num_txqs);
	}

	if (buf_size != sum_size)
		return ICE_ERR_PARAM;

	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	cmd->num_qgrps = num_qgrps;

	return ice_aq_send_cmd(hw, &desc, qg_list, buf_size, cd);
}

/**
 * ice_aq_add_sched_elems - adds scheduling element
 * @hw: pointer to the HW struct
 * @grps_req: the number of groups that are requested to be added
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @grps_added: returns total number of groups added
 * @cd: pointer to command details structure or NULL
 *
 * Add scheduling elements (0x0401)
 */
enum ice_status
ice_aq_add_sched_elems(struct ice_hw *hw, uint16_t grps_req,
		       struct ice_aqc_add_elem *buf, uint16_t buf_size,
		       uint16_t *grps_added, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_add_sched_elems,
					   grps_req, (void *)buf, buf_size,
					   grps_added, cd);
}

/**
 * ice_aq_cfg_sched_elems - configures scheduler elements
 * @hw: pointer to the HW struct
 * @elems_req: number of elements to configure
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_cfgd: returns total number of elements configured
 * @cd: pointer to command details structure or NULL
 *
 * Configure scheduling elements (0x0403)
 */
enum ice_status
ice_aq_cfg_sched_elems(struct ice_hw *hw, uint16_t elems_req,
		       struct ice_aqc_txsched_elem_data *buf, uint16_t buf_size,
		       uint16_t *elems_cfgd, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_cfg_sched_elems,
					   elems_req, (void *)buf, buf_size,
					   elems_cfgd, cd);
}

/**
 * ice_sched_update_elem - update element
 * @hw: pointer to the HW struct
 * @node: pointer to node
 * @info: node info to update
 *
 * Update the HW DB, and local SW DB of node. Update the scheduling
 * parameters of node from argument info data buffer (Info->data buf) and
 * returns success or error on config sched element failure. The caller
 * needs to hold scheduler lock.
 */
enum ice_status
ice_sched_update_elem(struct ice_hw *hw, struct ice_sched_node *node,
		      struct ice_aqc_txsched_elem_data *info)
{
	struct ice_aqc_txsched_elem_data buf;
	enum ice_status status;
	uint16_t elem_cfgd = 0;
	uint16_t num_elems = 1;

	buf = *info;
	/* For TC nodes, CIR config is not supported */
	if (node->info.data.elem_type == ICE_AQC_ELEM_TYPE_TC)
		buf.data.valid_sections &= ~ICE_AQC_ELEM_VALID_CIR;
	/* Parent TEID is reserved field in this aq call */
	buf.parent_teid = 0;
	/* Element type is reserved field in this aq call */
	buf.data.elem_type = 0;
	/* Flags is reserved field in this aq call */
	buf.data.flags = 0;

	/* Update HW DB */
	/* Configure element node */
	status = ice_aq_cfg_sched_elems(hw, num_elems, &buf, sizeof(buf),
					&elem_cfgd, NULL);
	if (status || elem_cfgd != num_elems) {
		DNPRINTF(ICE_DBG_SCHED, "%s: Config sched elem error\n",
		    __func__);
		return ICE_ERR_CFG;
	}

	/* Config success case */
	/* Now update local SW DB */
	/* Only copy the data portion of info buffer */
	node->info.data = info->data;
	return status;
}

/**
 * ice_sched_replay_node_prio - re-configure node priority
 * @hw: pointer to the HW struct
 * @node: sched node to configure
 * @priority: priority value
 *
 * This function configures node element's priority value. It
 * needs to be called with scheduler lock held.
 */
enum ice_status
ice_sched_replay_node_prio(struct ice_hw *hw, struct ice_sched_node *node,
			   uint8_t priority)
{
	struct ice_aqc_txsched_elem_data buf;
	struct ice_aqc_txsched_elem *data;
	enum ice_status status;

	buf = node->info;
	data = &buf.data;
	data->valid_sections |= ICE_AQC_ELEM_VALID_GENERIC;
	data->generic = priority;

	/* Configure element */
	status = ice_sched_update_elem(hw, node, &buf);
	return status;
}

/**
 * ice_sched_rm_unused_rl_prof - remove unused RL profile
 * @hw: pointer to the hardware structure
 *
 * This function removes unused rate limit profiles from the HW and
 * SW DB. The caller needs to hold scheduler lock.
 */
void
ice_sched_rm_unused_rl_prof(struct ice_hw *hw)
{
	uint16_t ln;

	for (ln = 0; ln < hw->num_tx_sched_layers; ln++) {
		struct ice_aqc_rl_profile_info *rl_prof_elem;
		struct ice_aqc_rl_profile_info *rl_prof_tmp;

		TAILQ_FOREACH_SAFE(rl_prof_elem, &hw->rl_prof_list[ln],
		    list_entry, rl_prof_tmp) {
			if (!ice_sched_del_rl_profile(hw,
			    &hw->rl_prof_list[ln], rl_prof_elem))
				DNPRINTF(ICE_DBG_SCHED,
				    "%s: Removed rl profile\n", __func__);
		}
	}
}

/**
 * ice_sched_get_rl_prof_layer - selects rate limit profile creation layer
 * @pi: port information structure
 * @rl_type: type of rate limit BW - min, max, or shared
 * @layer_index: layer index
 *
 * This function returns requested profile creation layer.
 */
uint8_t
ice_sched_get_rl_prof_layer(struct ice_port_info *pi, enum ice_rl_type rl_type,
			    uint8_t layer_index)
{
	struct ice_hw *hw = pi->hw;

	if (layer_index >= hw->num_tx_sched_layers)
		return ICE_SCHED_INVAL_LAYER_NUM;
	switch (rl_type) {
	case ICE_MIN_BW:
		if (hw->layer_info[layer_index].max_cir_rl_profiles)
			return layer_index;
		break;
	case ICE_MAX_BW:
		if (hw->layer_info[layer_index].max_eir_rl_profiles)
			return layer_index;
		break;
	case ICE_SHARED_BW:
		/* if current layer doesn't support SRL profile creation
		 * then try a layer up or down.
		 */
		if (hw->layer_info[layer_index].max_srl_profiles)
			return layer_index;
		else if (layer_index < hw->num_tx_sched_layers - 1 &&
			 hw->layer_info[layer_index + 1].max_srl_profiles)
			return layer_index + 1;
		else if (layer_index > 0 &&
			 hw->layer_info[layer_index - 1].max_srl_profiles)
			return layer_index - 1;
		break;
	default:
		break;
	}
	return ICE_SCHED_INVAL_LAYER_NUM;
}

/**
 * ice_sched_get_node_rl_prof_id - get node's rate limit profile ID
 * @node: sched node
 * @rl_type: rate limit type
 *
 * If existing profile matches, it returns the corresponding rate
 * limit profile ID, otherwise it returns an invalid ID as error.
 */
uint16_t
ice_sched_get_node_rl_prof_id(struct ice_sched_node *node,
			      enum ice_rl_type rl_type)
{
	uint16_t rl_prof_id = ICE_SCHED_INVAL_PROF_ID;
	struct ice_aqc_txsched_elem *data;

	data = &node->info.data;
	switch (rl_type) {
	case ICE_MIN_BW:
		if (data->valid_sections & ICE_AQC_ELEM_VALID_CIR)
			rl_prof_id = le16toh(data->cir_bw.bw_profile_idx);
		break;
	case ICE_MAX_BW:
		if (data->valid_sections & ICE_AQC_ELEM_VALID_EIR)
			rl_prof_id = le16toh(data->eir_bw.bw_profile_idx);
		break;
	case ICE_SHARED_BW:
		if (data->valid_sections & ICE_AQC_ELEM_VALID_SHARED)
			rl_prof_id = le16toh(data->srl_id);
		break;
	default:
		break;
	}

	return rl_prof_id;
}

/**
 * ice_sched_cfg_node_bw_lmt - configure node sched params
 * @hw: pointer to the HW struct
 * @node: sched node to configure
 * @rl_type: rate limit type CIR, EIR, or shared
 * @rl_prof_id: rate limit profile ID
 *
 * This function configures node element's BW limit.
 */
enum ice_status
ice_sched_cfg_node_bw_lmt(struct ice_hw *hw, struct ice_sched_node *node,
			  enum ice_rl_type rl_type, uint16_t rl_prof_id)
{
	struct ice_aqc_txsched_elem_data buf;
	struct ice_aqc_txsched_elem *data;

	buf = node->info;
	data = &buf.data;
	switch (rl_type) {
	case ICE_MIN_BW:
		data->valid_sections |= ICE_AQC_ELEM_VALID_CIR;
		data->cir_bw.bw_profile_idx = htole16(rl_prof_id);
		break;
	case ICE_MAX_BW:
		data->valid_sections |= ICE_AQC_ELEM_VALID_EIR;
		data->eir_bw.bw_profile_idx = htole16(rl_prof_id);
		break;
	case ICE_SHARED_BW:
		data->valid_sections |= ICE_AQC_ELEM_VALID_SHARED;
		data->srl_id = htole16(rl_prof_id);
		break;
	default:
		/* Unknown rate limit type */
		return ICE_ERR_PARAM;
	}

	/* Configure element */
	return ice_sched_update_elem(hw, node, &buf);
}

/**
 * ice_sched_rm_rl_profile - remove RL profile ID
 * @hw: pointer to the hardware structure
 * @layer_num: layer number where profiles are saved
 * @profile_type: profile type like EIR, CIR, or SRL
 * @profile_id: profile ID to remove
 *
 * This function removes rate limit profile from layer 'layer_num' of type
 * 'profile_type' and profile ID as 'profile_id'. The caller needs to hold
 * scheduler lock.
 */
enum ice_status
ice_sched_rm_rl_profile(struct ice_hw *hw, uint8_t layer_num,
    uint8_t profile_type, uint16_t profile_id)
{
	struct ice_aqc_rl_profile_info *rl_prof_elem;
	enum ice_status status = ICE_SUCCESS;

	if (!hw || layer_num >= hw->num_tx_sched_layers)
		return ICE_ERR_PARAM;
	/* Check the existing list for RL profile */
	TAILQ_FOREACH(rl_prof_elem, &hw->rl_prof_list[layer_num], list_entry) {
		if ((rl_prof_elem->profile.flags & ICE_AQC_RL_PROFILE_TYPE_M) ==
		    profile_type &&
		    le16toh(rl_prof_elem->profile.profile_id) == profile_id) {
			if (rl_prof_elem->prof_id_ref)
				rl_prof_elem->prof_id_ref--;

			/* Remove old profile ID from database */
			status = ice_sched_del_rl_profile(hw,
			    &hw->rl_prof_list[layer_num], rl_prof_elem);
			if (status && status != ICE_ERR_IN_USE)
				DNPRINTF(ICE_DBG_SCHED,
				    "%s: Remove rl profile failed\n", __func__);
			break;
		}
	}
	if (status == ICE_ERR_IN_USE)
		status = ICE_SUCCESS;
	return status;
}

/**
 * ice_sched_set_node_bw_dflt - set node's bandwidth limit to default
 * @pi: port information structure
 * @node: pointer to node structure
 * @rl_type: rate limit type min, max, or shared
 * @layer_num: layer number where RL profiles are saved
 *
 * This function configures node element's BW rate limit profile ID of
 * type CIR, EIR, or SRL to default. This function needs to be called
 * with the scheduler lock held.
 */
enum ice_status
ice_sched_set_node_bw_dflt(struct ice_port_info *pi,
			   struct ice_sched_node *node,
			   enum ice_rl_type rl_type, uint8_t layer_num)
{
	enum ice_status status;
	struct ice_hw *hw;
	uint8_t profile_type;
	uint16_t rl_prof_id;
	uint16_t old_id;

	hw = pi->hw;
	switch (rl_type) {
	case ICE_MIN_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_CIR;
		rl_prof_id = ICE_SCHED_DFLT_RL_PROF_ID;
		break;
	case ICE_MAX_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_EIR;
		rl_prof_id = ICE_SCHED_DFLT_RL_PROF_ID;
		break;
	case ICE_SHARED_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_SRL;
		/* No SRL is configured for default case */
		rl_prof_id = ICE_SCHED_NO_SHARED_RL_PROF_ID;
		break;
	default:
		return ICE_ERR_PARAM;
	}
	/* Save existing RL prof ID for later clean up */
	old_id = ice_sched_get_node_rl_prof_id(node, rl_type);
	/* Configure BW scheduling parameters */
	status = ice_sched_cfg_node_bw_lmt(hw, node, rl_type, rl_prof_id);
	if (status)
		return status;

	/* Remove stale RL profile ID */
	if (old_id == ICE_SCHED_DFLT_RL_PROF_ID ||
	    old_id == ICE_SCHED_INVAL_PROF_ID)
		return ICE_SUCCESS;

	return ice_sched_rm_rl_profile(hw, layer_num, profile_type, old_id);
}

/**
 * ice_aq_add_rl_profile - adds rate limiting profile(s)
 * @hw: pointer to the HW struct
 * @num_profiles: the number of profile(s) to be add
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_profiles_added: total number of profiles added to return
 * @cd: pointer to command details structure
 *
 * Add RL profile (0x0410)
 */
enum ice_status
ice_aq_add_rl_profile(struct ice_hw *hw, uint16_t num_profiles,
		      struct ice_aqc_rl_profile_elem *buf, uint16_t buf_size,
		      uint16_t *num_profiles_added, struct ice_sq_cd *cd)
{
	return ice_aq_rl_profile(hw, ice_aqc_opc_add_rl_profiles, num_profiles,
				 buf, buf_size, num_profiles_added, cd);
}

/**
 * DIV_S64 - Divide signed 64-bit value with signed 64-bit divisor
 * @dividend: value to divide
 * @divisor: value to divide by
 *
 * Use DIV_S64 for any 64-bit divide which operates on signed 64-bit dividends.
 * Do not use this for unsigned 64-bit dividends as it will not produce
 * correct results if the dividend is larger than INT64_MAX.
 */
static inline int64_t DIV_S64(int64_t dividend, int64_t divisor)
{
	return dividend / divisor;
}

/**
 * DIV_U64 - Divide unsigned 64-bit value by unsigned 64-bit divisor
 * @dividend: value to divide
 * @divisor: value to divide by
 *
 * Use DIV_U64 for any 64-bit divide which operates on unsigned 64-bit
 * dividends. Do not use this for signed 64-bit dividends as it will not
 * handle negative values correctly.
 */
static inline uint64_t DIV_U64(uint64_t dividend, uint64_t divisor)
{
	return dividend / divisor;
}

static inline uint64_t round_up_64bit(uint64_t a, uint32_t b)
{
	return DIV_U64(((a) + (b) / 2), (b));
}

/**
 * ice_sched_calc_wakeup - calculate RL profile wakeup parameter
 * @hw: pointer to the HW struct
 * @bw: bandwidth in Kbps
 *
 * This function calculates the wakeup parameter of RL profile.
 */
uint16_t
ice_sched_calc_wakeup(struct ice_hw *hw, int32_t bw)
{
	int64_t bytes_per_sec, wakeup_int, wakeup_a, wakeup_b, wakeup_f;
	int32_t wakeup_f_int;
	uint16_t wakeup = 0;

	/* Get the wakeup integer value */
	bytes_per_sec = DIV_S64((int64_t)bw * 1000, 8);
	wakeup_int = DIV_S64(hw->psm_clk_freq, bytes_per_sec);
	if (wakeup_int > 63) {
		wakeup = (uint16_t)((1 << 15) | wakeup_int);
	} else {
		/* Calculate fraction value up to 4 decimals
		 * Convert Integer value to a constant multiplier
		 */
		wakeup_b = (int64_t)ICE_RL_PROF_MULTIPLIER * wakeup_int;
		wakeup_a = DIV_S64((int64_t)ICE_RL_PROF_MULTIPLIER *
				   hw->psm_clk_freq, bytes_per_sec);

		/* Get Fraction value */
		wakeup_f = wakeup_a - wakeup_b;

		/* Round up the Fractional value via Ceil(Fractional value) */
		if (wakeup_f > DIV_S64(ICE_RL_PROF_MULTIPLIER, 2))
			wakeup_f += 1;

		wakeup_f_int = (int32_t)DIV_S64(wakeup_f * ICE_RL_PROF_FRACTION,
					    ICE_RL_PROF_MULTIPLIER);
		wakeup |= (uint16_t)(wakeup_int << 9);
		wakeup |= (uint16_t)(0x1ff & wakeup_f_int);
	}

	return wakeup;
}

/**
 * ice_sched_bw_to_rl_profile - convert BW to profile parameters
 * @hw: pointer to the HW struct
 * @bw: bandwidth in Kbps
 * @profile: profile parameters to return
 *
 * This function converts the BW to profile structure format.
 */
enum ice_status
ice_sched_bw_to_rl_profile(struct ice_hw *hw, uint32_t bw,
			   struct ice_aqc_rl_profile_elem *profile)
{
	enum ice_status status = ICE_ERR_PARAM;
	int64_t bytes_per_sec, ts_rate, mv_tmp;
	bool found = false;
	int32_t encode = 0;
	int64_t mv = 0;
	int32_t i;

	/* Bw settings range is from 0.5Mb/sec to 100Gb/sec */
	if (bw < ICE_SCHED_MIN_BW || bw > ICE_SCHED_MAX_BW)
		return status;

	/* Bytes per second from Kbps */
	bytes_per_sec = DIV_S64((int64_t)bw * 1000, 8);

	/* encode is 6 bits but really useful are 5 bits */
	for (i = 0; i < 64; i++) {
		uint64_t pow_result = BIT_ULL(i);

		ts_rate = DIV_S64((int64_t)hw->psm_clk_freq,
				  pow_result * ICE_RL_PROF_TS_MULTIPLIER);
		if (ts_rate <= 0)
			continue;

		/* Multiplier value */
		mv_tmp = DIV_S64(bytes_per_sec * ICE_RL_PROF_MULTIPLIER,
				 ts_rate);

		/* Round to the nearest ICE_RL_PROF_MULTIPLIER */
		mv = round_up_64bit(mv_tmp, ICE_RL_PROF_MULTIPLIER);

		/* First multiplier value greater than the given
		 * accuracy bytes
		 */
		if (mv > ICE_RL_PROF_ACCURACY_BYTES) {
			encode = i;
			found = true;
			break;
		}
	}
	if (found) {
		uint16_t wm;

		wm = ice_sched_calc_wakeup(hw, bw);
		profile->rl_multiply = htole16(mv);
		profile->wake_up_calc = htole16(wm);
		profile->rl_encode = htole16(encode);
		status = ICE_SUCCESS;
	} else {
		status = ICE_ERR_DOES_NOT_EXIST;
	}

	return status;
}

/**
 * ice_sched_add_rl_profile - add RL profile
 * @hw: pointer to the hardware structure
 * @rl_type: type of rate limit BW - min, max, or shared
 * @bw: bandwidth in Kbps - Kilo bits per sec
 * @layer_num: specifies in which layer to create profile
 *
 * This function first checks the existing list for corresponding BW
 * parameter. If it exists, it returns the associated profile otherwise
 * it creates a new rate limit profile for requested BW, and adds it to
 * the HW DB and local list. It returns the new profile or null on error.
 * The caller needs to hold the scheduler lock.
 */
struct ice_aqc_rl_profile_info *
ice_sched_add_rl_profile(struct ice_hw *hw, enum ice_rl_type rl_type,
			 uint32_t bw, uint8_t layer_num)
{
	struct ice_aqc_rl_profile_info *rl_prof_elem;
	uint16_t profiles_added = 0, num_profiles = 1;
	struct ice_aqc_rl_profile_elem *buf;
	enum ice_status status;
	uint8_t profile_type;

	if (!hw || layer_num >= hw->num_tx_sched_layers)
		return NULL;
	switch (rl_type) {
	case ICE_MIN_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_CIR;
		break;
	case ICE_MAX_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_EIR;
		break;
	case ICE_SHARED_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_SRL;
		break;
	default:
		return NULL;
	}

	TAILQ_FOREACH(rl_prof_elem, &hw->rl_prof_list[layer_num], list_entry) {
		if ((rl_prof_elem->profile.flags & ICE_AQC_RL_PROFILE_TYPE_M) ==
		    profile_type && rl_prof_elem->bw == bw)
			/* Return existing profile ID info */
			return rl_prof_elem;
	}

	/* Create new profile ID */
	rl_prof_elem = (struct ice_aqc_rl_profile_info *)
		ice_malloc(hw, sizeof(*rl_prof_elem));
	if (!rl_prof_elem)
		return NULL;

	status = ice_sched_bw_to_rl_profile(hw, bw, &rl_prof_elem->profile);
	if (status != ICE_SUCCESS)
		goto exit_add_rl_prof;

	rl_prof_elem->bw = bw;
	/* layer_num is zero relative, and fw expects level from 1 to 9 */
	rl_prof_elem->profile.level = layer_num + 1;
	rl_prof_elem->profile.flags = profile_type;
	rl_prof_elem->profile.max_burst_size = htole16(hw->max_burst_size);

	/* Create new entry in HW DB */
	buf = &rl_prof_elem->profile;
	status = ice_aq_add_rl_profile(hw, num_profiles, buf, sizeof(*buf),
				       &profiles_added, NULL);
	if (status || profiles_added != num_profiles)
		goto exit_add_rl_prof;

	/* Good entry - add in the list */
	rl_prof_elem->prof_id_ref = 0;
	TAILQ_INSERT_HEAD(&hw->rl_prof_list[layer_num], rl_prof_elem,
	    list_entry);
	return rl_prof_elem;

exit_add_rl_prof:
	ice_free(hw, rl_prof_elem);
	return NULL;
}

/**
 * ice_sched_set_node_bw - set node's bandwidth
 * @pi: port information structure
 * @node: tree node
 * @rl_type: rate limit type min, max, or shared
 * @bw: bandwidth in Kbps - Kilo bits per sec
 * @layer_num: layer number
 *
 * This function adds new profile corresponding to requested BW, configures
 * node's RL profile ID of type CIR, EIR, or SRL, and removes old profile
 * ID from local database. The caller needs to hold scheduler lock.
 */
enum ice_status
ice_sched_set_node_bw(struct ice_port_info *pi, struct ice_sched_node *node,
		      enum ice_rl_type rl_type, uint32_t bw, uint8_t layer_num)
{
	struct ice_aqc_rl_profile_info *rl_prof_info;
	enum ice_status status = ICE_ERR_PARAM;
	struct ice_hw *hw = pi->hw;
	uint16_t old_id, rl_prof_id;

	rl_prof_info = ice_sched_add_rl_profile(hw, rl_type, bw, layer_num);
	if (!rl_prof_info)
		return status;

	rl_prof_id = le16toh(rl_prof_info->profile.profile_id);

	/* Save existing RL prof ID for later clean up */
	old_id = ice_sched_get_node_rl_prof_id(node, rl_type);
	/* Configure BW scheduling parameters */
	status = ice_sched_cfg_node_bw_lmt(hw, node, rl_type, rl_prof_id);
	if (status)
		return status;

	/* New changes has been applied */
	/* Increment the profile ID reference count */
	rl_prof_info->prof_id_ref++;

	/* Check for old ID removal */
	if ((old_id == ICE_SCHED_DFLT_RL_PROF_ID && rl_type != ICE_SHARED_BW) ||
	    old_id == ICE_SCHED_INVAL_PROF_ID || old_id == rl_prof_id)
		return ICE_SUCCESS;

	return ice_sched_rm_rl_profile(hw, layer_num,
				       rl_prof_info->profile.flags &
				       ICE_AQC_RL_PROFILE_TYPE_M, old_id);
}

/**
 * ice_sched_set_node_bw_lmt - set node's BW limit
 * @pi: port information structure
 * @node: tree node
 * @rl_type: rate limit type min, max, or shared
 * @bw: bandwidth in Kbps - Kilo bits per sec
 *
 * It updates node's BW limit parameters like BW RL profile ID of type CIR,
 * EIR, or SRL. The caller needs to hold scheduler lock.
 *
 * NOTE: Caller provides the correct SRL node in case of shared profile
 * settings.
 */
enum ice_status
ice_sched_set_node_bw_lmt(struct ice_port_info *pi, struct ice_sched_node *node,
			  enum ice_rl_type rl_type, uint32_t bw)
{
	struct ice_hw *hw;
	uint8_t layer_num;

	if (!pi)
		return ICE_ERR_PARAM;
	hw = pi->hw;
	/* Remove unused RL profile IDs from HW and SW DB */
	ice_sched_rm_unused_rl_prof(hw);

	layer_num = ice_sched_get_rl_prof_layer(pi, rl_type,
						node->tx_sched_layer);
	if (layer_num >= hw->num_tx_sched_layers)
		return ICE_ERR_PARAM;

	if (bw == ICE_SCHED_DFLT_BW)
		return ice_sched_set_node_bw_dflt(pi, node, rl_type, layer_num);

	return ice_sched_set_node_bw(pi, node, rl_type, bw, layer_num);
}

/**
 * ice_sched_cfg_node_bw_alloc - configure node BW weight/alloc params
 * @hw: pointer to the HW struct
 * @node: sched node to configure
 * @rl_type: rate limit type CIR, EIR, or shared
 * @bw_alloc: BW weight/allocation
 *
 * This function configures node element's BW allocation.
 */
enum ice_status
ice_sched_cfg_node_bw_alloc(struct ice_hw *hw, struct ice_sched_node *node,
			    enum ice_rl_type rl_type, uint16_t bw_alloc)
{
	struct ice_aqc_txsched_elem_data buf;
	struct ice_aqc_txsched_elem *data;
	enum ice_status status;

	buf = node->info;
	data = &buf.data;
	if (rl_type == ICE_MIN_BW) {
		data->valid_sections |= ICE_AQC_ELEM_VALID_CIR;
		data->cir_bw.bw_alloc = htole16(bw_alloc);
	} else if (rl_type == ICE_MAX_BW) {
		data->valid_sections |= ICE_AQC_ELEM_VALID_EIR;
		data->eir_bw.bw_alloc = htole16(bw_alloc);
	} else {
		return ICE_ERR_PARAM;
	}

	/* Configure element */
	status = ice_sched_update_elem(hw, node, &buf);
	return status;
}

/**
 * ice_sched_replay_node_bw - replay node(s) BW
 * @hw: pointer to the HW struct
 * @node: sched node to configure
 * @bw_t_info: BW type information
 *
 * This function restores node's BW from bw_t_info. The caller needs
 * to hold the scheduler lock.
 */
enum ice_status
ice_sched_replay_node_bw(struct ice_hw *hw, struct ice_sched_node *node,
			 struct ice_bw_type_info *bw_t_info)
{
	struct ice_port_info *pi = hw->port_info;
	enum ice_status status = ICE_ERR_PARAM;
	uint16_t bw_alloc;

	if (!node)
		return status;
	if (!ice_is_any_bit_set(bw_t_info->bw_t_bitmap, ICE_BW_TYPE_CNT))
		return ICE_SUCCESS;
	if (ice_is_bit_set(bw_t_info->bw_t_bitmap, ICE_BW_TYPE_PRIO)) {
		status = ice_sched_replay_node_prio(hw, node,
						    bw_t_info->generic);
		if (status)
			return status;
	}
	if (ice_is_bit_set(bw_t_info->bw_t_bitmap, ICE_BW_TYPE_CIR)) {
		status = ice_sched_set_node_bw_lmt(pi, node, ICE_MIN_BW,
						   bw_t_info->cir_bw.bw);
		if (status)
			return status;
	}
	if (ice_is_bit_set(bw_t_info->bw_t_bitmap, ICE_BW_TYPE_CIR_WT)) {
		bw_alloc = bw_t_info->cir_bw.bw_alloc;
		status = ice_sched_cfg_node_bw_alloc(hw, node, ICE_MIN_BW,
						     bw_alloc);
		if (status)
			return status;
	}
	if (ice_is_bit_set(bw_t_info->bw_t_bitmap, ICE_BW_TYPE_EIR)) {
		status = ice_sched_set_node_bw_lmt(pi, node, ICE_MAX_BW,
						   bw_t_info->eir_bw.bw);
		if (status)
			return status;
	}
	if (ice_is_bit_set(bw_t_info->bw_t_bitmap, ICE_BW_TYPE_EIR_WT)) {
		bw_alloc = bw_t_info->eir_bw.bw_alloc;
		status = ice_sched_cfg_node_bw_alloc(hw, node, ICE_MAX_BW,
						     bw_alloc);
		if (status)
			return status;
	}
	if (ice_is_bit_set(bw_t_info->bw_t_bitmap, ICE_BW_TYPE_SHARED))
		status = ice_sched_set_node_bw_lmt(pi, node, ICE_SHARED_BW,
						   bw_t_info->shared_bw);
	return status;
}
/**
 * ice_sched_replay_q_bw - replay queue type node BW
 * @pi: port information structure
 * @q_ctx: queue context structure
 *
 * This function replays queue type node bandwidth. This function needs to be
 * called with scheduler lock held.
 */
enum ice_status
ice_sched_replay_q_bw(struct ice_port_info *pi, struct ice_q_ctx *q_ctx)
{
	struct ice_sched_node *q_node;

	/* Following also checks the presence of node in tree */
	q_node = ice_sched_find_node_by_teid(pi->root, q_ctx->q_teid);
	if (!q_node)
		return ICE_ERR_PARAM;
	return ice_sched_replay_node_bw(pi->hw, q_node, &q_ctx->bw_t_info);
}

/**
 * ice_ena_vsi_txq
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @q_handle: software queue handle
 * @num_qgrps: Number of added queue groups
 * @buf: list of queue groups to be added
 * @buf_size: size of buffer for indirect command
 * @cd: pointer to command details structure or NULL
 *
 * This function adds one LAN queue
 */
enum ice_status
ice_ena_vsi_txq(struct ice_port_info *pi, uint16_t vsi_handle, uint8_t tc,
    uint16_t q_handle, uint8_t num_qgrps, struct ice_aqc_add_tx_qgrp *buf,
    uint16_t buf_size, struct ice_sq_cd *cd)
{
	struct ice_aqc_txsched_elem_data node = { 0 };
	struct ice_sched_node *parent;
	struct ice_q_ctx *q_ctx;
	enum ice_status status;
	struct ice_hw *hw;

	if (!pi || pi->port_state != ICE_SCHED_PORT_STATE_READY)
		return ICE_ERR_CFG;

	if (num_qgrps > 1 || buf->num_txqs > 1)
		return ICE_ERR_MAX_LIMIT;

	hw = pi->hw;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;
#if 0
	ice_acquire_lock(&pi->sched_lock);
#endif
	q_ctx = ice_get_lan_q_ctx(hw, vsi_handle, tc, q_handle);
	if (!q_ctx) {
		DNPRINTF(ICE_DBG_SCHED, "%s: Enaq: invalid queue handle %d\n",
		    __func__, q_handle);
		status = ICE_ERR_PARAM;
		goto ena_txq_exit;
	}

	/* find a parent node */
	parent = ice_sched_get_free_qparent(pi, vsi_handle, tc,
					    ICE_SCHED_NODE_OWNER_LAN);
	if (!parent) {
		status = ICE_ERR_PARAM;
		goto ena_txq_exit;
	}

	buf->parent_teid = parent->info.node_teid;
	node.parent_teid = parent->info.node_teid;
	/* Mark that the values in the "generic" section as valid. The default
	 * value in the "generic" section is zero. This means that :
	 * - Scheduling mode is Bytes Per Second (BPS), indicated by Bit 0.
	 * - 0 priority among siblings, indicated by Bit 1-3.
	 * - WFQ, indicated by Bit 4.
	 * - 0 Adjustment value is used in PSM credit update flow, indicated by
	 * Bit 5-6.
	 * - Bit 7 is reserved.
	 * Without setting the generic section as valid in valid_sections, the
	 * Admin queue command will fail with error code ICE_AQ_RC_EINVAL.
	 */
	buf->txqs[0].info.valid_sections =
		ICE_AQC_ELEM_VALID_GENERIC | ICE_AQC_ELEM_VALID_CIR |
		ICE_AQC_ELEM_VALID_EIR;
	buf->txqs[0].info.generic = 0;
	buf->txqs[0].info.cir_bw.bw_profile_idx =
		htole16(ICE_SCHED_DFLT_RL_PROF_ID);
	buf->txqs[0].info.cir_bw.bw_alloc =
		htole16(ICE_SCHED_DFLT_BW_WT);
	buf->txqs[0].info.eir_bw.bw_profile_idx =
		htole16(ICE_SCHED_DFLT_RL_PROF_ID);
	buf->txqs[0].info.eir_bw.bw_alloc =
		htole16(ICE_SCHED_DFLT_BW_WT);

	/* add the LAN queue */
	status = ice_aq_add_lan_txq(hw, num_qgrps, buf, buf_size, cd);
	if (status != ICE_SUCCESS) {
		DNPRINTF(ICE_DBG_SCHED, "%s: enable queue %d failed %d\n",
		    __func__, le16toh(buf->txqs[0].txq_id),
		    hw->adminq.sq_last_status);
		goto ena_txq_exit;
	}

	node.node_teid = buf->txqs[0].q_teid;
	node.data.elem_type = ICE_AQC_ELEM_TYPE_LEAF;
	q_ctx->q_handle = q_handle;
	q_ctx->q_teid = le32toh(node.node_teid);

	/* add a leaf node into scheduler tree queue layer */
	status = ice_sched_add_node(pi, hw->num_tx_sched_layers - 1, &node,
	    NULL);
	if (!status)
		status = ice_sched_replay_q_bw(pi, q_ctx);

ena_txq_exit:
#if 0
	ice_release_lock(&pi->sched_lock);
#endif
	return status;
}

/* LAN Tx Queue Context used for set Tx config by ice_aqc_opc_add_txqs,
 * Bit[0-175] is valid
 */
const struct ice_ctx_ele ice_tlan_ctx_info[] = {
				    /* Field			Width	LSB */
	ICE_CTX_STORE(ice_tlan_ctx, base,			57,	0),
	ICE_CTX_STORE(ice_tlan_ctx, port_num,			3,	57),
	ICE_CTX_STORE(ice_tlan_ctx, cgd_num,			5,	60),
	ICE_CTX_STORE(ice_tlan_ctx, pf_num,			3,	65),
	ICE_CTX_STORE(ice_tlan_ctx, vmvf_num,			10,	68),
	ICE_CTX_STORE(ice_tlan_ctx, vmvf_type,			2,	78),
	ICE_CTX_STORE(ice_tlan_ctx, src_vsi,			10,	80),
	ICE_CTX_STORE(ice_tlan_ctx, tsyn_ena,			1,	90),
	ICE_CTX_STORE(ice_tlan_ctx, internal_usage_flag,	1,	91),
	ICE_CTX_STORE(ice_tlan_ctx, alt_vlan,			1,	92),
	ICE_CTX_STORE(ice_tlan_ctx, cpuid,			8,	93),
	ICE_CTX_STORE(ice_tlan_ctx, wb_mode,			1,	101),
	ICE_CTX_STORE(ice_tlan_ctx, tphrd_desc,			1,	102),
	ICE_CTX_STORE(ice_tlan_ctx, tphrd,			1,	103),
	ICE_CTX_STORE(ice_tlan_ctx, tphwr_desc,			1,	104),
	ICE_CTX_STORE(ice_tlan_ctx, cmpq_id,			9,	105),
	ICE_CTX_STORE(ice_tlan_ctx, qnum_in_func,		14,	114),
	ICE_CTX_STORE(ice_tlan_ctx, itr_notification_mode,	1,	128),
	ICE_CTX_STORE(ice_tlan_ctx, adjust_prof_id,		6,	129),
	ICE_CTX_STORE(ice_tlan_ctx, qlen,			13,	135),
	ICE_CTX_STORE(ice_tlan_ctx, quanta_prof_idx,		4,	148),
	ICE_CTX_STORE(ice_tlan_ctx, tso_ena,			1,	152),
	ICE_CTX_STORE(ice_tlan_ctx, tso_qnum,			11,	153),
	ICE_CTX_STORE(ice_tlan_ctx, legacy_int,			1,	164),
	ICE_CTX_STORE(ice_tlan_ctx, drop_ena,			1,	165),
	ICE_CTX_STORE(ice_tlan_ctx, cache_prof_idx,		2,	166),
	ICE_CTX_STORE(ice_tlan_ctx, pkt_shaper_prof_idx,	3,	168),
	ICE_CTX_STORE(ice_tlan_ctx, int_q_state,		122,	171),
	{ 0 }
};

/**
 * ice_cfg_vsi_for_tx - Configure the hardware for Tx
 * @vsi: the VSI to configure
 *
 * Configure the device Tx queues through firmware AdminQ commands. After
 * this, Tx queues will be ready for transmit.
 */
int
ice_cfg_vsi_for_tx(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_aqc_add_tx_qgrp *qg;
	struct ice_hw *hw = &vsi->sc->hw;
	enum ice_status status;
	int i;
	int err = 0;
	uint16_t qg_size, pf_q;

	qg_size = ice_struct_size(qg, txqs, 1);
	qg = (struct ice_aqc_add_tx_qgrp *)malloc(qg_size, M_DEVBUF,
	    M_NOWAIT|M_ZERO);
	if (!qg)
		return (ENOMEM);

	qg->num_txqs = 1;

	for (i = 0; i < vsi->num_tx_queues; i++) {
		struct ice_tlan_ctx tlan_ctx = { 0 };
		struct ice_tx_queue *txq = &vsi->tx_queues[i];

		pf_q = vsi->tx_qmap[txq->me];
		qg->txqs[0].txq_id = htole16(pf_q);

		err = ice_setup_tx_ctx(txq, &tlan_ctx, pf_q);
		if (err)
			goto free_txqg;

		ice_set_ctx(hw, (uint8_t *)&tlan_ctx, qg->txqs[0].txq_ctx,
			    ice_tlan_ctx_info);

		status = ice_ena_vsi_txq(hw->port_info, vsi->idx, txq->tc,
					 txq->q_handle, 1, qg, qg_size, NULL);
		if (status) {
			printf("%s: Failed to set LAN Tx queue %d "
			    "(TC %d, handle %d) context, err %s aq_err %s\n",
			    sc->sc_dev.dv_xname, i, txq->tc, txq->q_handle,
			    ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			err = ENODEV;
			goto free_txqg;
		}

		/* Keep track of the Tx queue TEID */
		if (pf_q == le16toh(qg->txqs[0].txq_id))
			txq->q_teid = le32toh(qg->txqs[0].q_teid);
	}

free_txqg:
	free(qg, M_DEVBUF, qg_size);

	return (err);
}

/* Different reset sources for which a disable queue AQ call has to be made in
 * order to clean the Tx scheduler as a part of the reset
 */
enum ice_disq_rst_src {
	ICE_NO_RESET = 0,
	ICE_VM_RESET,
	ICE_VF_RESET,
};

/**
 * ice_aq_dis_lan_txq
 * @hw: pointer to the hardware structure
 * @num_qgrps: number of groups in the list
 * @qg_list: the list of groups to disable
 * @buf_size: the total size of the qg_list buffer in bytes
 * @rst_src: if called due to reset, specifies the reset source
 * @vmvf_num: the relative VM or VF number that is undergoing the reset
 * @cd: pointer to command details structure or NULL
 *
 * Disable LAN Tx queue (0x0C31)
 */
enum ice_status
ice_aq_dis_lan_txq(struct ice_hw *hw, uint8_t num_qgrps,
		   struct ice_aqc_dis_txq_item *qg_list, uint16_t buf_size,
		   enum ice_disq_rst_src rst_src, uint16_t vmvf_num,
		   struct ice_sq_cd *cd)
{
	struct ice_aqc_dis_txq_item *item;
	struct ice_aqc_dis_txqs *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;
	uint16_t i, sz = 0;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);
	cmd = &desc.params.dis_txqs;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_dis_txqs);

	/* qg_list can be NULL only in VM/VF reset flow */
	if (!qg_list && !rst_src)
		return ICE_ERR_PARAM;

	if (num_qgrps > ICE_LAN_TXQ_MAX_QGRPS)
		return ICE_ERR_PARAM;

	cmd->num_entries = num_qgrps;

	cmd->vmvf_and_timeout = htole16((5 << ICE_AQC_Q_DIS_TIMEOUT_S) &
	    ICE_AQC_Q_DIS_TIMEOUT_M);

	switch (rst_src) {
	case ICE_VM_RESET:
		cmd->cmd_type = ICE_AQC_Q_DIS_CMD_VM_RESET;
		cmd->vmvf_and_timeout |=
			htole16(vmvf_num & ICE_AQC_Q_DIS_VMVF_NUM_M);
		break;
	case ICE_VF_RESET:
		cmd->cmd_type = ICE_AQC_Q_DIS_CMD_VF_RESET;
		/* In this case, FW expects vmvf_num to be absolute VF ID */
		cmd->vmvf_and_timeout |=
			htole16((vmvf_num + hw->func_caps.vf_base_id) &
			    ICE_AQC_Q_DIS_VMVF_NUM_M);
		break;
	case ICE_NO_RESET:
	default:
		break;
	}

	/* flush pipe on time out */
	cmd->cmd_type |= ICE_AQC_Q_DIS_CMD_FLUSH_PIPE;
	/* If no queue group info, we are in a reset flow. Issue the AQ */
	if (!qg_list)
		goto do_aq;

	/* set RD bit to indicate that command buffer is provided by the driver
	 * and it needs to be read by the firmware
	 */
	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	for (i = 0, item = qg_list; i < num_qgrps; i++) {
		uint16_t item_size = ice_struct_size(item, q_id, item->num_qs);

		/* If the num of queues is even, add 2 bytes of padding */
		if ((item->num_qs % 2) == 0)
			item_size += 2;

		sz += item_size;

		item = (struct ice_aqc_dis_txq_item *)((uint8_t *)item +
		    item_size);
	}

	if (buf_size != sz)
		return ICE_ERR_PARAM;

do_aq:
	status = ice_aq_send_cmd(hw, &desc, qg_list, buf_size, cd);
	if (status) {
		if (!qg_list)
			DNPRINTF(ICE_DBG_SCHED, "%s: VM%d disable failed %d\n", 
			    __func__, vmvf_num, hw->adminq.sq_last_status);
		else
			DNPRINTF(ICE_DBG_SCHED,
			    "%s: disable queue %d failed %d\n", __func__,
			    le16toh(qg_list[0].q_id[0]),
			    hw->adminq.sq_last_status);
	}

	return status;
}

/**
 * ice_dis_vsi_txq
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @num_queues: number of queues
 * @q_handles: pointer to software queue handle array
 * @q_ids: pointer to the q_id array
 * @q_teids: pointer to queue node teids
 * @rst_src: if called due to reset, specifies the reset source
 * @vmvf_num: the relative VM or VF number that is undergoing the reset
 * @cd: pointer to command details structure or NULL
 *
 * This function removes queues and their corresponding nodes in SW DB
 */
enum ice_status
ice_dis_vsi_txq(struct ice_port_info *pi, uint16_t vsi_handle, uint8_t tc,
    uint8_t num_queues, uint16_t *q_handles, uint16_t *q_ids,
    uint32_t *q_teids, enum ice_disq_rst_src rst_src, uint16_t vmvf_num,
    struct ice_sq_cd *cd)
{
	enum ice_status status = ICE_ERR_DOES_NOT_EXIST;
	struct ice_aqc_dis_txq_item *qg_list;
	struct ice_q_ctx *q_ctx;
	struct ice_hw *hw;
	uint16_t i, buf_size;

	if (!pi || pi->port_state != ICE_SCHED_PORT_STATE_READY)
		return ICE_ERR_CFG;

	hw = pi->hw;

	if (!num_queues) {
		/* if queue is disabled already yet the disable queue command
		 * has to be sent to complete the VF reset, then call
		 * ice_aq_dis_lan_txq without any queue information
		 */
		if (rst_src)
			return ice_aq_dis_lan_txq(hw, 0, NULL, 0, rst_src,
						  vmvf_num, NULL);
		return ICE_ERR_CFG;
	}

	buf_size = ice_struct_size(qg_list, q_id, 1);
	qg_list = (struct ice_aqc_dis_txq_item *)ice_malloc(hw, buf_size);
	if (!qg_list)
		return ICE_ERR_NO_MEMORY;
#if 0
	ice_acquire_lock(&pi->sched_lock);
#endif
	for (i = 0; i < num_queues; i++) {
		struct ice_sched_node *node;

		node = ice_sched_find_node_by_teid(pi->root, q_teids[i]);
		if (!node)
			continue;
		q_ctx = ice_get_lan_q_ctx(hw, vsi_handle, tc, q_handles[i]);
		if (!q_ctx) {
			DNPRINTF(ICE_DBG_SCHED, "%s: invalid queue handle%d\n",
			    __func__, q_handles[i]);
			continue;
		}
		if (q_ctx->q_handle != q_handles[i]) {
			DNPRINTF(ICE_DBG_SCHED, "%s: Err:handles %d %d\n",
			    __func__, q_ctx->q_handle, q_handles[i]);
			continue;
		}
		qg_list->parent_teid = node->info.parent_teid;
		qg_list->num_qs = 1;
		qg_list->q_id[0] = htole16(q_ids[i]);
		status = ice_aq_dis_lan_txq(hw, 1, qg_list, buf_size, rst_src,
					    vmvf_num, cd);

		if (status != ICE_SUCCESS)
			break;
		ice_free_sched_node(pi, node);
		q_ctx->q_handle = ICE_INVAL_Q_HANDLE;
	}
#if 0
	ice_release_lock(&pi->sched_lock);
#endif
	ice_free(hw, qg_list);
	return status;
}

/**
 * ice_vsi_disable_tx - Disable (unconfigure) Tx queues for a VSI
 * @vsi: the VSI to disable
 *
 * Disables the Tx queues associated with this VSI. Essentially the opposite
 * of ice_cfg_vsi_for_tx.
 */
int
ice_vsi_disable_tx(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	uint32_t *q_teids;
	uint16_t *q_ids, *q_handles;
	size_t q_teids_size, q_ids_size, q_handles_size;
	int tc, j, buf_idx, err = 0;

	if (vsi->num_tx_queues > 255)
		return (ENOSYS);

	q_teids_size = sizeof(*q_teids) * vsi->num_tx_queues;
	q_teids = (uint32_t *)malloc(q_teids_size, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (!q_teids)
		return (ENOMEM);

	q_ids_size = sizeof(*q_ids) * vsi->num_tx_queues;
	q_ids = (uint16_t *)malloc(q_ids_size, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (!q_ids) {
		err = (ENOMEM);
		goto free_q_teids;
	}

	q_handles_size = sizeof(*q_handles) * vsi->num_tx_queues;
	q_handles = (uint16_t *)malloc(q_handles_size, M_DEVBUF,
	    M_NOWAIT|M_ZERO);
	if (!q_handles) {
		err = (ENOMEM);
		goto free_q_ids;
	}

	ice_for_each_traffic_class(tc) {
		struct ice_tc_info *tc_info = &vsi->tc_info[tc];
		uint16_t start_idx, end_idx;

		/* Skip rest of disabled TCs once the first
		 * disabled TC is found */
		if (!(vsi->tc_map & BIT(tc)))
			break;

		/* Fill out TX queue information for this TC */
		start_idx = tc_info->qoffset;
		end_idx = start_idx + tc_info->qcount_tx;
		buf_idx = 0;
		for (j = start_idx; j < end_idx; j++) {
			struct ice_tx_queue *txq = &vsi->tx_queues[j];

			q_ids[buf_idx] = vsi->tx_qmap[j];
			q_handles[buf_idx] = txq->q_handle;
			q_teids[buf_idx] = txq->q_teid;
			buf_idx++;
		}

		status = ice_dis_vsi_txq(hw->port_info, vsi->idx, tc, buf_idx,
					 q_handles, q_ids, q_teids,
					 ICE_NO_RESET, 0, NULL);
		if (status == ICE_ERR_DOES_NOT_EXIST) {
			; /* Queues have been disabled */
		} else if (status == ICE_ERR_RESET_ONGOING) {
			DPRINTF("%s: Reset in progress. LAN Tx queues already "
			    "disabled\n", __func__);
			break;
		} else if (status) {
			printf("%s: Failed to disable LAN Tx queues: "
			    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
			    ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			err = (ENODEV);
			break;
		}

		/* Clear buffers */
		memset(q_teids, 0, q_teids_size);
		memset(q_ids, 0, q_ids_size);
		memset(q_handles, 0, q_handles_size);
	}

/* free_q_handles: */
	free(q_handles, M_DEVBUF, q_handles_size);
free_q_ids:
	free(q_ids, M_DEVBUF, q_ids_size);
free_q_teids:
	free(q_teids, M_DEVBUF, q_teids_size);

	return err;
}

/**
 * ice_copy_rxq_ctx_to_hw
 * @hw: pointer to the hardware structure
 * @ice_rxq_ctx: pointer to the rxq context
 * @rxq_index: the index of the Rx queue
 *
 * Copies rxq context from dense structure to HW register space
 */
enum ice_status
ice_copy_rxq_ctx_to_hw(struct ice_hw *hw, uint8_t *ice_rxq_ctx,
    uint32_t rxq_index)
{
	uint8_t i;

	if (!ice_rxq_ctx)
		return ICE_ERR_BAD_PTR;

	if (rxq_index > QRX_CTRL_MAX_INDEX)
		return ICE_ERR_PARAM;

	/* Copy each dword separately to HW */
	for (i = 0; i < ICE_RXQ_CTX_SIZE_DWORDS; i++) {
		ICE_WRITE(hw, QRX_CONTEXT(i, rxq_index),
		     *((uint32_t *)(ice_rxq_ctx + (i * sizeof(uint32_t)))));

		DNPRINTF(ICE_DBG_QCTX, "%s: qrxdata[%d]: %08X\n", __func__,
		    i, *((uint32_t *)(ice_rxq_ctx + (i * sizeof(uint32_t)))));
	}

	return ICE_SUCCESS;
}

/* LAN Rx Queue Context */
static const struct ice_ctx_ele ice_rlan_ctx_info[] = {
	/* Field		Width	LSB */
	ICE_CTX_STORE(ice_rlan_ctx, head,		13,	0),
	ICE_CTX_STORE(ice_rlan_ctx, cpuid,		8,	13),
	ICE_CTX_STORE(ice_rlan_ctx, base,		57,	32),
	ICE_CTX_STORE(ice_rlan_ctx, qlen,		13,	89),
	ICE_CTX_STORE(ice_rlan_ctx, dbuf,		7,	102),
	ICE_CTX_STORE(ice_rlan_ctx, hbuf,		5,	109),
	ICE_CTX_STORE(ice_rlan_ctx, dtype,		2,	114),
	ICE_CTX_STORE(ice_rlan_ctx, dsize,		1,	116),
	ICE_CTX_STORE(ice_rlan_ctx, crcstrip,		1,	117),
	ICE_CTX_STORE(ice_rlan_ctx, l2tsel,		1,	119),
	ICE_CTX_STORE(ice_rlan_ctx, hsplit_0,		4,	120),
	ICE_CTX_STORE(ice_rlan_ctx, hsplit_1,		2,	124),
	ICE_CTX_STORE(ice_rlan_ctx, showiv,		1,	127),
	ICE_CTX_STORE(ice_rlan_ctx, rxmax,		14,	174),
	ICE_CTX_STORE(ice_rlan_ctx, tphrdesc_ena,	1,	193),
	ICE_CTX_STORE(ice_rlan_ctx, tphwdesc_ena,	1,	194),
	ICE_CTX_STORE(ice_rlan_ctx, tphdata_ena,	1,	195),
	ICE_CTX_STORE(ice_rlan_ctx, tphhead_ena,	1,	196),
	ICE_CTX_STORE(ice_rlan_ctx, lrxqthresh,		3,	198),
	ICE_CTX_STORE(ice_rlan_ctx, prefena,		1,	201),
	{ 0 }
};

/**
 * ice_write_rxq_ctx
 * @hw: pointer to the hardware structure
 * @rlan_ctx: pointer to the rxq context
 * @rxq_index: the index of the Rx queue
 *
 * Converts rxq context from sparse to dense structure and then writes
 * it to HW register space and enables the hardware to prefetch descriptors
 * instead of only fetching them on demand
 */
enum ice_status
ice_write_rxq_ctx(struct ice_hw *hw, struct ice_rlan_ctx *rlan_ctx,
		  uint32_t rxq_index)
{
	uint8_t ctx_buf[ICE_RXQ_CTX_SZ] = { 0 };

	if (!rlan_ctx)
		return ICE_ERR_BAD_PTR;

	rlan_ctx->prefena = 1;

	ice_set_ctx(hw, (uint8_t *)rlan_ctx, ctx_buf, ice_rlan_ctx_info);
	return ice_copy_rxq_ctx_to_hw(hw, ctx_buf, rxq_index);
}

/**
 * ice_setup_rx_ctx - Setup an Rx context structure for a receive queue
 * @rxq: the receive queue to program
 *
 * Setup an Rx queue context structure and program it into the hardware
 * registers. This is a necessary step for enabling the Rx queue.
 */
int
ice_setup_rx_ctx(struct ice_rx_queue *rxq)
{
	struct ice_rlan_ctx rlan_ctx = {0};
	struct ice_vsi *vsi = rxq->vsi;
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	uint32_t rxdid = ICE_RXDID_FLEX_NIC;
	uint32_t regval;
	uint16_t pf_q;

	pf_q = vsi->rx_qmap[rxq->me];

	/* set the receive queue base address, defined in 128 byte units */
	rlan_ctx.base = rxq->rx_paddr >> 7;

	rlan_ctx.qlen = rxq->desc_count;

	rlan_ctx.dbuf = vsi->mbuf_sz >> ICE_RLAN_CTX_DBUF_S;

	/* use 32 byte descriptors */
	rlan_ctx.dsize = 1;

	/* Strip the Ethernet CRC bytes before the packet is posted to the
	 * host memory.
	 */
	rlan_ctx.crcstrip = 1;

	rlan_ctx.l2tsel = 1;

	/* don't do header splitting */
	rlan_ctx.dtype = ICE_RX_DTYPE_NO_SPLIT;
	rlan_ctx.hsplit_0 = ICE_RLAN_RX_HSPLIT_0_NO_SPLIT;
	rlan_ctx.hsplit_1 = ICE_RLAN_RX_HSPLIT_1_NO_SPLIT;

	/* strip VLAN from inner headers */
	rlan_ctx.showiv = 1;

	rlan_ctx.rxmax = MIN(vsi->max_frame_size,
			     ICE_MAX_RX_SEGS * vsi->mbuf_sz);

	rlan_ctx.lrxqthresh = 1;

	if (vsi->type != ICE_VSI_VF) {
		regval = ICE_READ(hw, QRXFLXP_CNTXT(pf_q));
		regval &= ~QRXFLXP_CNTXT_RXDID_IDX_M;
		regval |= (rxdid << QRXFLXP_CNTXT_RXDID_IDX_S) &
			QRXFLXP_CNTXT_RXDID_IDX_M;

		regval &= ~QRXFLXP_CNTXT_RXDID_PRIO_M;
		regval |= (0x03 << QRXFLXP_CNTXT_RXDID_PRIO_S) &
			QRXFLXP_CNTXT_RXDID_PRIO_M;

		ICE_WRITE(hw, QRXFLXP_CNTXT(pf_q), regval);
	}

	status = ice_write_rxq_ctx(hw, &rlan_ctx, pf_q);
	if (status) {
		printf("%s: Failed to set LAN Rx queue context, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		return (EIO);
	}

	return 0;
}

/**
 * ice_cfg_vsi_for_rx - Configure the hardware for Rx
 * @vsi: the VSI to configure
 *
 * Prepare an Rx context descriptor and configure the device to receive
 * traffic.
 */
int
ice_cfg_vsi_for_rx(struct ice_vsi *vsi)
{
	int i, err;

	for (i = 0; i < vsi->num_rx_queues; i++) {
		err = ice_setup_rx_ctx(&vsi->rx_queues[i]);
		if (err)
			return err;
	}

	return (0);
}

/**
 * ice_is_rxq_ready - Check if an Rx queue is ready
 * @hw: ice hw structure
 * @pf_q: absolute PF queue index to check
 * @reg: on successful return, contains qrx_ctrl contents
 *
 * Reads the QRX_CTRL register and verifies if the queue is in a consistent
 * state. That is, QENA_REQ matches QENA_STAT. Used to check before making
 * a request to change the queue, as well as to verify the request has
 * finished. The queue should change status within a few microseconds, so we
 * use a small delay while polling the register.
 *
 * Returns an error code if the queue does not update after a few retries.
 */
int
ice_is_rxq_ready(struct ice_hw *hw, int pf_q, uint32_t *reg)
{
	uint32_t qrx_ctrl, qena_req, qena_stat;
	int i;

	for (i = 0; i < ICE_Q_WAIT_RETRY_LIMIT; i++) {
		qrx_ctrl = ICE_READ(hw, QRX_CTRL(pf_q));
		qena_req = (qrx_ctrl >> QRX_CTRL_QENA_REQ_S) & 1;
		qena_stat = (qrx_ctrl >> QRX_CTRL_QENA_STAT_S) & 1;

		/* if the request and status bits equal, then the queue is
		 * fully disabled or enabled.
		 */
		if (qena_req == qena_stat) {
			*reg = qrx_ctrl;
			return (0);
		}

		/* wait a few microseconds before we check again */
		DELAY(10);
	}

	return (ETIMEDOUT);
}

/**
 * ice_control_rx_queue - Configure hardware to start or stop an Rx queue
 * @vsi: VSI containing queue to enable/disable
 * @qidx: Queue index in VSI space
 * @enable: true to enable queue, false to disable
 *
 * Control the Rx queue through the QRX_CTRL register, enabling or disabling
 * it. Wait for the appropriate time to ensure that the queue has actually
 * reached the expected state.
 */
int
ice_control_rx_queue(struct ice_vsi *vsi, uint16_t qidx, bool enable)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	uint32_t qrx_ctrl = 0;
	int err;
	struct ice_rx_queue *rxq = &vsi->rx_queues[qidx];
	int pf_q = vsi->rx_qmap[rxq->me];

	err = ice_is_rxq_ready(hw, pf_q, &qrx_ctrl);
	if (err) {
		printf("%s: Rx queue %d is not ready\n",
		    sc->sc_dev.dv_xname, pf_q);
		return err;
	}

	/* Skip if the queue is already in correct state */
	if (enable == !!(qrx_ctrl & QRX_CTRL_QENA_STAT_M))
		return (0);

	if (enable)
		qrx_ctrl |= QRX_CTRL_QENA_REQ_M;
	else
		qrx_ctrl &= ~QRX_CTRL_QENA_REQ_M;
	ICE_WRITE(hw, QRX_CTRL(pf_q), qrx_ctrl);

	/* wait for the queue to finalize the request */
	err = ice_is_rxq_ready(hw, pf_q, &qrx_ctrl);
	if (err) {
		printf("%s: Rx queue %d %sable timeout\n",
		    sc->sc_dev.dv_xname, pf_q, (enable ? "en" : "dis"));
		return err;
	}

	/* this should never happen */
	if (enable != !!(qrx_ctrl & QRX_CTRL_QENA_STAT_M)) {
		printf("%s: Rx queue %d invalid state\n",
		    sc->sc_dev.dv_xname, pf_q);
		return (EINVAL);
	}

	return (0);
}

/**
 * ice_control_all_rx_queues - Configure hardware to start or stop the Rx queues
 * @vsi: VSI to enable/disable queues
 * @enable: true to enable queues, false to disable
 *
 * Control the Rx queues through the QRX_CTRL register, enabling or disabling
 * them. Wait for the appropriate time to ensure that the queues have actually
 * reached the expected state.
 */
int
ice_control_all_rx_queues(struct ice_vsi *vsi, bool enable)
{
	int i, err;

	/* TODO: amortize waits by changing all queues up front and then
	 * checking their status afterwards. This will become more necessary
	 * when we have a large number of queues.
	 */
	for (i = 0; i < vsi->num_rx_queues; i++) {
		err = ice_control_rx_queue(vsi, i, enable);
		if (err)
			break;
	}

	return (0);
}

/**
 * ice_configure_rxq_interrupt - Configure HW Rx queue for an MSI-X interrupt
 * @hw: ice hw structure
 * @rxqid: Rx queue index in PF space
 * @vector: MSI-X vector index in PF/VF space
 * @itr_idx: ITR index to use for interrupt
 *
 * @remark ice_flush() may need to be called after this
 */
void
ice_configure_rxq_interrupt(struct ice_hw *hw, uint16_t rxqid,
    uint16_t vector, uint8_t itr_idx)
{
	uint32_t val;

	KASSERT(itr_idx <= ICE_ITR_NONE);

	val = (QINT_RQCTL_CAUSE_ENA_M |
	       (itr_idx << QINT_RQCTL_ITR_INDX_S) |
	       (vector << QINT_RQCTL_MSIX_INDX_S));
	ICE_WRITE(hw, QINT_RQCTL(rxqid), val);
}

void
ice_configure_all_rxq_interrupts(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->sc->hw;
	int i;

	for (i = 0; i < vsi->num_rx_queues; i++) {
		struct ice_rx_queue *rxq = &vsi->rx_queues[i];
		int v = rxq->irqv->iv_qid + 1;

		ice_configure_rxq_interrupt(hw, vsi->rx_qmap[rxq->me], v,
					    ICE_RX_ITR);

		DNPRINTF(ICE_DBG_INIT,
		    "%s: RXQ(%d) intr enable: me %d rxqid %d vector %d\n",
		    __func__, i, rxq->me, vsi->rx_qmap[rxq->me], v);
	}

	ice_flush(hw);
}

/**
 * ice_configure_txq_interrupt - Configure HW Tx queue for an MSI-X interrupt
 * @hw: ice hw structure
 * @txqid: Tx queue index in PF space
 * @vector: MSI-X vector index in PF/VF space
 * @itr_idx: ITR index to use for interrupt
 *
 * @remark ice_flush() may need to be called after this
 */
void
ice_configure_txq_interrupt(struct ice_hw *hw, uint16_t txqid, uint16_t vector,
    uint8_t itr_idx)
{
	uint32_t val;

	KASSERT(itr_idx <= ICE_ITR_NONE);

	val = (QINT_TQCTL_CAUSE_ENA_M |
	       (itr_idx << QINT_TQCTL_ITR_INDX_S) |
	       (vector << QINT_TQCTL_MSIX_INDX_S));
	ICE_WRITE(hw, QINT_TQCTL(txqid), val);
}

/**
 * ice_configure_all_txq_interrupts - Configure HW Tx queues for MSI-X interrupts
 * @vsi: the VSI to configure
 *
 * Called when setting up MSI-X interrupts to configure the Tx hardware queues.
 */
void
ice_configure_all_txq_interrupts(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->sc->hw;
	int i;

	for (i = 0; i < vsi->num_tx_queues; i++) {
		struct ice_tx_queue *txq = &vsi->tx_queues[i];
		int v = txq->irqv->iv_qid + 1;

		ice_configure_txq_interrupt(hw, vsi->tx_qmap[txq->me], v,
		    ICE_TX_ITR);
	}

	ice_flush(hw);
}

/**
 * ice_itr_to_reg - Convert an ITR setting into its register equivalent
 * @hw: The device HW structure
 * @itr_setting: the ITR setting to convert
 *
 * Based on the hardware ITR granularity, convert an ITR setting into the
 * correct value to prepare programming to the HW.
 */
static inline uint16_t ice_itr_to_reg(struct ice_hw *hw, uint16_t itr_setting)
{
	return itr_setting / hw->itr_gran;
}

/**
 * ice_configure_rx_itr - Configure the Rx ITR settings for this VSI
 * @vsi: the VSI to configure
 *
 * Program the hardware ITR registers with the settings for this VSI.
 */
void
ice_configure_rx_itr(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->sc->hw;
	int i;

	/* TODO: Handle per-queue/per-vector ITR? */

	for (i = 0; i < vsi->num_rx_queues; i++) {
		struct ice_rx_queue *rxq = &vsi->rx_queues[i];
		int v = rxq->irqv->iv_qid + 1;

		ICE_WRITE(hw, GLINT_ITR(ICE_RX_ITR, v),
		     ice_itr_to_reg(hw, vsi->rx_itr));
	}

	ice_flush(hw);
}

/**
 * ice_set_default_promisc_mask - Set default config for promisc settings
 * @promisc_mask: bitmask to setup
 *
 * The ice_(set|clear)_vsi_promisc() function expects a mask of promiscuous
 * modes to operate on. The mask used in here is the default one for the
 * driver, where promiscuous is enabled/disabled for all types of
 * non-VLAN-tagged/VLAN 0 traffic.
 */
void
ice_set_default_promisc_mask(ice_bitmap_t *promisc_mask)
{
	ice_zero_bitmap(promisc_mask, ICE_PROMISC_MAX);
	ice_set_bit(ICE_PROMISC_UCAST_TX, promisc_mask);
	ice_set_bit(ICE_PROMISC_UCAST_RX, promisc_mask);
	ice_set_bit(ICE_PROMISC_MCAST_TX, promisc_mask);
	ice_set_bit(ICE_PROMISC_MCAST_RX, promisc_mask);
}

/**
 * _ice_set_vsi_promisc - set given VSI to given promiscuous mode(s)
 * @hw: pointer to the hardware structure
 * @vsi_handle: VSI handle to configure
 * @promisc_mask: pointer to mask of promiscuous config bits
 * @vid: VLAN ID to set VLAN promiscuous
 * @lport: logical port number to configure promisc mode
 */
enum ice_status
ice_set_vsi_promisc(struct ice_hw *hw, uint16_t vsi_handle,
		    ice_bitmap_t *promisc_mask, uint16_t vid)
{
	struct ice_switch_info *sw = hw->switch_info;
	uint8_t lport = hw->port_info->lport;
	enum { UCAST_FLTR = 1, MCAST_FLTR, BCAST_FLTR };
	ice_declare_bitmap(p_mask, ICE_PROMISC_MAX);
	struct ice_fltr_list_entry f_list_entry;
	struct ice_fltr_info new_fltr;
	enum ice_status status = ICE_SUCCESS;
	bool is_tx_fltr, is_rx_lb_fltr;
	uint16_t hw_vsi_id;
	int pkt_type;
	uint8_t recipe_id;

	DNPRINTF(ICE_DBG_TRACE, "%s\n", __func__);

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;
	hw_vsi_id = hw->vsi_ctx[vsi_handle]->vsi_num;

	memset(&new_fltr, 0, sizeof(new_fltr));

	/* Do not modify original bitmap */
	ice_cp_bitmap(p_mask, promisc_mask, ICE_PROMISC_MAX);

	if (ice_is_bit_set(p_mask, ICE_PROMISC_VLAN_RX) &&
	    ice_is_bit_set(p_mask, ICE_PROMISC_VLAN_TX)) {
		new_fltr.lkup_type = ICE_SW_LKUP_PROMISC_VLAN;
		new_fltr.l_data.mac_vlan.vlan_id = vid;
		recipe_id = ICE_SW_LKUP_PROMISC_VLAN;
	} else {
		new_fltr.lkup_type = ICE_SW_LKUP_PROMISC;
		recipe_id = ICE_SW_LKUP_PROMISC;
	}

	/* Separate filters must be set for each direction/packet type
	 * combination, so we will loop over the mask value, store the
	 * individual type, and clear it out in the input mask as it
	 * is found.
	 */
	while (ice_is_any_bit_set(p_mask, ICE_PROMISC_MAX)) {
		struct ice_sw_recipe *recp_list;
		uint8_t *mac_addr;

		pkt_type = 0;
		is_tx_fltr = false;
		is_rx_lb_fltr = false;

		if (ice_test_and_clear_bit(ICE_PROMISC_UCAST_RX,
					   p_mask)) {
			pkt_type = UCAST_FLTR;
		} else if (ice_test_and_clear_bit(ICE_PROMISC_UCAST_TX,
						  p_mask)) {
			pkt_type = UCAST_FLTR;
			is_tx_fltr = true;
		} else if (ice_test_and_clear_bit(ICE_PROMISC_MCAST_RX,
						  p_mask)) {
			pkt_type = MCAST_FLTR;
		} else if (ice_test_and_clear_bit(ICE_PROMISC_MCAST_TX,
						  p_mask)) {
			pkt_type = MCAST_FLTR;
			is_tx_fltr = true;
		} else if (ice_test_and_clear_bit(ICE_PROMISC_BCAST_RX,
						  p_mask)) {
			pkt_type = BCAST_FLTR;
		} else if (ice_test_and_clear_bit(ICE_PROMISC_BCAST_TX,
						  p_mask)) {
			pkt_type = BCAST_FLTR;
			is_tx_fltr = true;
		} else if (ice_test_and_clear_bit(ICE_PROMISC_UCAST_RX_LB,
						  p_mask)) {
			pkt_type = UCAST_FLTR;
			is_rx_lb_fltr = true;
		}

		/* Check for VLAN promiscuous flag */
		if (ice_is_bit_set(p_mask, ICE_PROMISC_VLAN_RX)) {
			ice_clear_bit(ICE_PROMISC_VLAN_RX, p_mask);
		} else if (ice_test_and_clear_bit(ICE_PROMISC_VLAN_TX,
						  p_mask)) {
			is_tx_fltr = true;
		}
		/* Set filter DA based on packet type */
		mac_addr = new_fltr.l_data.mac.mac_addr;
		if (pkt_type == BCAST_FLTR) {
			memset(mac_addr, 0xff, ETHER_ADDR_LEN);
		} else if (pkt_type == MCAST_FLTR ||
			   pkt_type == UCAST_FLTR) {
			/* Use the dummy ether header DA */
			memcpy(mac_addr, dummy_eth_header, ETHER_ADDR_LEN);
			if (pkt_type == MCAST_FLTR)
				mac_addr[0] |= 0x1;	/* Set multicast bit */
		}

		/* Need to reset this to zero for all iterations */
		new_fltr.flag = 0;
		if (is_tx_fltr) {
			new_fltr.flag |= ICE_FLTR_TX;
			new_fltr.src = hw_vsi_id;
		} else if (is_rx_lb_fltr) {
			new_fltr.flag |= ICE_FLTR_RX_LB;
			new_fltr.src = hw_vsi_id;
		} else {
			new_fltr.flag |= ICE_FLTR_RX;
			new_fltr.src = lport;
		}

		new_fltr.fltr_act = ICE_FWD_TO_VSI;
		new_fltr.vsi_handle = vsi_handle;
		new_fltr.fwd_id.hw_vsi_id = hw_vsi_id;
		f_list_entry.fltr_info = new_fltr;
		recp_list = &sw->recp_list[recipe_id];

		status = ice_add_rule_internal(hw, recp_list, lport,
					       &f_list_entry);
		if (status != ICE_SUCCESS)
			goto set_promisc_exit;
	}

set_promisc_exit:
	return status;
}

/**
 * ice_vsi_uses_fltr - Determine if given VSI uses specified filter
 * @fm_entry: filter entry to inspect
 * @vsi_handle: VSI handle to compare with filter info
 */
bool
ice_vsi_uses_fltr(struct ice_fltr_mgmt_list_entry *fm_entry,
    uint16_t vsi_handle)
{
	return ((fm_entry->fltr_info.fltr_act == ICE_FWD_TO_VSI &&
		 fm_entry->fltr_info.vsi_handle == vsi_handle) ||
		(fm_entry->fltr_info.fltr_act == ICE_FWD_TO_VSI_LIST &&
		 fm_entry->vsi_list_info &&
		 (ice_is_bit_set(fm_entry->vsi_list_info->vsi_map,
				 vsi_handle))));
}

/**
 * ice_add_entry_to_vsi_fltr_list - Add copy of fltr_list_entry to remove list
 * @hw: pointer to the hardware structure
 * @vsi_handle: VSI handle to remove filters from
 * @vsi_list_head: pointer to the list to add entry to
 * @fi: pointer to fltr_info of filter entry to copy & add
 *
 * Helper function, used when creating a list of filters to remove from
 * a specific VSI. The entry added to vsi_list_head is a COPY of the
 * original filter entry, with the exception of fltr_info.fltr_act and
 * fltr_info.fwd_id fields. These are set such that later logic can
 * extract which VSI to remove the fltr from, and pass on that information.
 */
enum ice_status
ice_add_entry_to_vsi_fltr_list(struct ice_hw *hw, uint16_t vsi_handle,
			       struct ice_fltr_list_head *vsi_list_head,
			       struct ice_fltr_info *fi)
{
	struct ice_fltr_list_entry *tmp;

	/* this memory is freed up in the caller function
	 * once filters for this VSI are removed
	 */
	tmp = (struct ice_fltr_list_entry *)ice_malloc(hw, sizeof(*tmp));
	if (!tmp)
		return ICE_ERR_NO_MEMORY;

	tmp->fltr_info = *fi;

	/* Overwrite these fields to indicate which VSI to remove filter from,
	 * so find and remove logic can extract the information from the
	 * list entries. Note that original entries will still have proper
	 * values.
	 */
	tmp->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	tmp->fltr_info.vsi_handle = vsi_handle;
	tmp->fltr_info.fwd_id.hw_vsi_id = hw->vsi_ctx[vsi_handle]->vsi_num;

	TAILQ_INSERT_HEAD(vsi_list_head, tmp, list_entry);

	return ICE_SUCCESS;
}

/**
 * ice_determine_promisc_mask
 * @fi: filter info to parse
 * @promisc_mask: pointer to mask to be filled in
 *
 * Helper function to determine which ICE_PROMISC_ mask corresponds
 * to given filter into.
 */
void ice_determine_promisc_mask(struct ice_fltr_info *fi,
    ice_bitmap_t *promisc_mask)
{
	uint16_t vid = fi->l_data.mac_vlan.vlan_id;
	uint8_t *macaddr = fi->l_data.mac.mac_addr;
	bool is_rx_lb_fltr = false;
	bool is_tx_fltr = false;

	ice_zero_bitmap(promisc_mask, ICE_PROMISC_MAX);

	if (fi->flag == ICE_FLTR_TX)
		is_tx_fltr = true;
	if (fi->flag == ICE_FLTR_RX_LB)
		is_rx_lb_fltr = true;

	if (ETHER_IS_BROADCAST(macaddr)) {
		ice_set_bit(is_tx_fltr ? ICE_PROMISC_BCAST_TX
				       : ICE_PROMISC_BCAST_RX, promisc_mask);
	} else if (ETHER_IS_MULTICAST(macaddr)) {
		ice_set_bit(is_tx_fltr ? ICE_PROMISC_MCAST_TX
				       : ICE_PROMISC_MCAST_RX, promisc_mask);
	} else {
		if (is_tx_fltr)
			ice_set_bit(ICE_PROMISC_UCAST_TX, promisc_mask);
		else if (is_rx_lb_fltr)
			ice_set_bit(ICE_PROMISC_UCAST_RX_LB, promisc_mask);
		else
			ice_set_bit(ICE_PROMISC_UCAST_RX, promisc_mask);
	}

	if (vid) {
		ice_set_bit(is_tx_fltr ? ICE_PROMISC_VLAN_TX
				       : ICE_PROMISC_VLAN_RX, promisc_mask);
	}
}

/**
 * ice_remove_vsi_list_rule
 * @hw: pointer to the hardware structure
 * @vsi_list_id: VSI list ID generated as part of allocate resource
 * @lkup_type: switch rule filter lookup type
 *
 * The VSI list should be emptied before this function is called to remove the
 * VSI list.
 */
enum ice_status
ice_remove_vsi_list_rule(struct ice_hw *hw, uint16_t vsi_list_id,
			 enum ice_sw_lkup_type lkup_type)
{
	/* Free the vsi_list resource that we allocated. It is assumed that the
	 * list is empty at this point.
	 */
	return ice_aq_alloc_free_vsi_list(hw, &vsi_list_id, lkup_type,
	    ice_aqc_opc_free_res);
}

/**
 * ice_rem_update_vsi_list
 * @hw: pointer to the hardware structure
 * @vsi_handle: VSI handle of the VSI to remove
 * @fm_list: filter management entry for which the VSI list management needs to
 *           be done
 */
enum ice_status
ice_rem_update_vsi_list(struct ice_hw *hw, uint16_t vsi_handle,
			struct ice_fltr_mgmt_list_entry *fm_list)
{
	struct ice_switch_info *sw = hw->switch_info;
	enum ice_sw_lkup_type lkup_type;
	enum ice_status status = ICE_SUCCESS;
	uint16_t vsi_list_id;

	if (fm_list->fltr_info.fltr_act != ICE_FWD_TO_VSI_LIST ||
	    fm_list->vsi_count == 0)
		return ICE_ERR_PARAM;

	/* A rule with the VSI being removed does not exist */
	if (!ice_is_bit_set(fm_list->vsi_list_info->vsi_map, vsi_handle))
		return ICE_ERR_DOES_NOT_EXIST;

	lkup_type = fm_list->fltr_info.lkup_type;
	vsi_list_id = fm_list->fltr_info.fwd_id.vsi_list_id;
	status = ice_update_vsi_list_rule(hw, &vsi_handle, 1, vsi_list_id, true,
					  ice_aqc_opc_update_sw_rules,
					  lkup_type);
	if (status)
		return status;

	fm_list->vsi_count--;
	ice_clear_bit(vsi_handle, fm_list->vsi_list_info->vsi_map);

	if (fm_list->vsi_count == 1 && lkup_type != ICE_SW_LKUP_VLAN) {
		struct ice_fltr_info tmp_fltr_info = fm_list->fltr_info;
		struct ice_vsi_list_map_info *vsi_list_info =
			fm_list->vsi_list_info;
		uint16_t rem_vsi_handle;

		rem_vsi_handle = ice_find_first_bit(vsi_list_info->vsi_map,
						    ICE_MAX_VSI);
		if (!ice_is_vsi_valid(hw, rem_vsi_handle))
			return ICE_ERR_OUT_OF_RANGE;

		/* Make sure VSI list is empty before removing it below */
		status = ice_update_vsi_list_rule(hw, &rem_vsi_handle, 1,
						  vsi_list_id, true,
						  ice_aqc_opc_update_sw_rules,
						  lkup_type);
		if (status)
			return status;

		tmp_fltr_info.fltr_act = ICE_FWD_TO_VSI;
		tmp_fltr_info.fwd_id.hw_vsi_id =
			hw->vsi_ctx[rem_vsi_handle]->vsi_num;
		tmp_fltr_info.vsi_handle = rem_vsi_handle;
		status = ice_update_pkt_fwd_rule(hw, &tmp_fltr_info);
		if (status) {
			DNPRINTF(ICE_DBG_SW, "%s: Failed to update pkt fwd "
			    "rule to FWD_TO_VSI on HW VSI %d, error %d\n",
			    __func__,
			    tmp_fltr_info.fwd_id.hw_vsi_id, status);
			return status;
		}

		fm_list->fltr_info = tmp_fltr_info;
	}

	if ((fm_list->vsi_count == 1 && lkup_type != ICE_SW_LKUP_VLAN) ||
	    (fm_list->vsi_count == 0 && lkup_type == ICE_SW_LKUP_VLAN)) {
		struct ice_vsi_list_map_info *vsi_list_info =
			fm_list->vsi_list_info;

		/* Remove the VSI list since it is no longer used */
		status = ice_remove_vsi_list_rule(hw, vsi_list_id, lkup_type);
		if (status) {
			DNPRINTF(ICE_DBG_SW, "%s: Failed to remove "
			    "VSI list %d, error %d\n", __func__,
			    vsi_list_id, status);
			return status;
		}

		TAILQ_REMOVE(&sw->vsi_list_map_head, vsi_list_info, list_entry);
		ice_free(hw, vsi_list_info);
		fm_list->vsi_list_info = NULL;
	}

	return status;
}

/**
 * ice_remove_rule_internal - Remove a filter rule of a given type
 * @hw: pointer to the hardware structure
 * @recp_list: recipe list for which the rule needs to removed
 * @f_entry: rule entry containing filter information
 */
enum ice_status
ice_remove_rule_internal(struct ice_hw *hw, struct ice_sw_recipe *recp_list,
			 struct ice_fltr_list_entry *f_entry)
{
	struct ice_fltr_mgmt_list_entry *list_elem;
#if 0
	struct ice_lock *rule_lock; /* Lock to protect filter rule list */
#endif
	enum ice_status status = ICE_SUCCESS;
	bool remove_rule = false;
	uint16_t vsi_handle;

	if (!ice_is_vsi_valid(hw, f_entry->fltr_info.vsi_handle))
		return ICE_ERR_PARAM;
	f_entry->fltr_info.fwd_id.hw_vsi_id =
		hw->vsi_ctx[f_entry->fltr_info.vsi_handle]->vsi_num;
#if 0
	rule_lock = &recp_list->filt_rule_lock;
	ice_acquire_lock(rule_lock);
#endif
	list_elem = ice_find_rule_entry(&recp_list->filt_rules,
					&f_entry->fltr_info);
	if (!list_elem) {
		status = ICE_ERR_DOES_NOT_EXIST;
		goto exit;
	}

	if (list_elem->fltr_info.fltr_act != ICE_FWD_TO_VSI_LIST) {
		remove_rule = true;
	} else if (!list_elem->vsi_list_info) {
		status = ICE_ERR_DOES_NOT_EXIST;
		goto exit;
	} else if (list_elem->vsi_list_info->ref_cnt > 1) {
		/* a ref_cnt > 1 indicates that the vsi_list is being
		 * shared by multiple rules. Decrement the ref_cnt and
		 * remove this rule, but do not modify the list, as it
		 * is in-use by other rules.
		 */
		list_elem->vsi_list_info->ref_cnt--;
		remove_rule = true;
	} else {
		/* a ref_cnt of 1 indicates the vsi_list is only used
		 * by one rule. However, the original removal request is only
		 * for a single VSI. Update the vsi_list first, and only
		 * remove the rule if there are no further VSIs in this list.
		 */
		vsi_handle = f_entry->fltr_info.vsi_handle;
		status = ice_rem_update_vsi_list(hw, vsi_handle, list_elem);
		if (status)
			goto exit;
		/* if VSI count goes to zero after updating the VSI list */
		if (list_elem->vsi_count == 0)
			remove_rule = true;
	}

	if (remove_rule) {
		/* Remove the lookup rule */
		struct ice_sw_rule_lkup_rx_tx *s_rule;

		s_rule = (struct ice_sw_rule_lkup_rx_tx *)
			ice_malloc(hw, ice_struct_size(s_rule, hdr_data, 0));
		if (!s_rule) {
			status = ICE_ERR_NO_MEMORY;
			goto exit;
		}

		ice_fill_sw_rule(hw, &list_elem->fltr_info, s_rule,
				 ice_aqc_opc_remove_sw_rules);

		status = ice_aq_sw_rules(hw, s_rule,
					 ice_struct_size(s_rule, hdr_data, 0),
					 1, ice_aqc_opc_remove_sw_rules, NULL);

		/* Remove a book keeping from the list */
		ice_free(hw, s_rule);

		if (status)
			goto exit;

		TAILQ_REMOVE(&recp_list->filt_rules, list_elem, list_entry);
		ice_free(hw, list_elem);
	}
exit:
#if 0
	ice_release_lock(rule_lock);
#endif
	return status;
}

/**
 * ice_remove_promisc - Remove promisc based filter rules
 * @hw: pointer to the hardware structure
 * @recp_id: recipe ID for which the rule needs to removed
 * @v_list: list of promisc entries
 */
enum ice_status
ice_remove_promisc(struct ice_hw *hw, uint8_t recp_id,
		   struct ice_fltr_list_head *v_list)
{
	struct ice_fltr_list_entry *v_list_itr, *tmp;
	struct ice_sw_recipe *recp_list;

	recp_list = &hw->switch_info->recp_list[recp_id];
	TAILQ_FOREACH_SAFE(v_list_itr, v_list, list_entry, tmp) {
		v_list_itr->status =
			ice_remove_rule_internal(hw, recp_list, v_list_itr);
		if (v_list_itr->status)
			return v_list_itr->status;
	}
	return ICE_SUCCESS;
}
/**
 * ice_clear_vsi_promisc - clear specified promiscuous mode(s)
 * @hw: pointer to the hardware structure
 * @vsi_handle: VSI handle to clear mode
 * @promisc_mask: pointer to mask of promiscuous config bits to clear
 * @vid: VLAN ID to clear VLAN promiscuous
 */
enum ice_status
ice_clear_vsi_promisc(struct ice_hw *hw, uint16_t vsi_handle,
		       ice_bitmap_t *promisc_mask, uint16_t vid)
{
	struct ice_switch_info *sw = hw->switch_info;
	ice_declare_bitmap(compl_promisc_mask, ICE_PROMISC_MAX);
	ice_declare_bitmap(fltr_promisc_mask, ICE_PROMISC_MAX);
	struct ice_fltr_list_entry *fm_entry, *tmp;
	struct ice_fltr_list_head remove_list_head;
	struct ice_fltr_mgmt_list_entry *itr;
	struct ice_fltr_mgmt_list_head *rule_head;
#if 0
	struct ice_lock *rule_lock;	/* Lock to protect filter rule list */
#endif
	enum ice_status status = ICE_SUCCESS;
	uint8_t recipe_id;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;

	if (ice_is_bit_set(promisc_mask, ICE_PROMISC_VLAN_RX) &&
	    ice_is_bit_set(promisc_mask, ICE_PROMISC_VLAN_TX))
		recipe_id = ICE_SW_LKUP_PROMISC_VLAN;
	else
		recipe_id = ICE_SW_LKUP_PROMISC;

	rule_head = &sw->recp_list[recipe_id].filt_rules;
#if 0
	rule_lock = &sw->recp_list[recipe_id].filt_rule_lock;
#endif
	TAILQ_INIT(&remove_list_head);
#if 0
	ice_acquire_lock(rule_lock);
#endif
	TAILQ_FOREACH(itr, rule_head, list_entry) {
		struct ice_fltr_info *fltr_info;
		ice_zero_bitmap(compl_promisc_mask, ICE_PROMISC_MAX);

		if (!ice_vsi_uses_fltr(itr, vsi_handle))
			continue;
		fltr_info = &itr->fltr_info;

		if (recipe_id == ICE_SW_LKUP_PROMISC_VLAN &&
		    vid != fltr_info->l_data.mac_vlan.vlan_id)
			continue;

		ice_determine_promisc_mask(fltr_info, fltr_promisc_mask);
		ice_andnot_bitmap(compl_promisc_mask, fltr_promisc_mask,
				  promisc_mask, ICE_PROMISC_MAX);

		/* Skip if filter is not completely specified by given mask */
		if (ice_is_any_bit_set(compl_promisc_mask, ICE_PROMISC_MAX))
			continue;

		status = ice_add_entry_to_vsi_fltr_list(hw, vsi_handle,
							&remove_list_head,
							fltr_info);
		if (status) {
#if 0
			ice_release_lock(rule_lock);
#endif
			goto free_fltr_list;
		}
	}
#if 0
	ice_release_lock(rule_lock);
#endif
	status = ice_remove_promisc(hw, recipe_id, &remove_list_head);

free_fltr_list:
	TAILQ_FOREACH_SAFE(fm_entry, &remove_list_head, list_entry, tmp) {
		TAILQ_REMOVE(&remove_list_head, fm_entry, list_entry);
		ice_free(hw, fm_entry);
	}

	return status;
}

/**
 * ice_if_promisc_set - Set device promiscuous mode
 *
 * @remark Calls to this function will always overwrite the previous setting
 */
int
ice_if_promisc_set(struct ice_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	bool promisc_enable = ifp->if_flags & IFF_PROMISC;
	bool multi_enable = ifp->if_flags & IFF_ALLMULTI;
	ice_declare_bitmap(promisc_mask, ICE_PROMISC_MAX);

	/* Do not support configuration when in recovery mode */
	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE))
		return (ENOSYS);
	
	ice_set_default_promisc_mask(promisc_mask);

	if (multi_enable && !promisc_enable) {
		ice_clear_bit(ICE_PROMISC_UCAST_TX, promisc_mask);
		ice_clear_bit(ICE_PROMISC_UCAST_RX, promisc_mask);
	}

	if (promisc_enable || multi_enable) {
		status = ice_set_vsi_promisc(hw, sc->pf_vsi.idx,
					     promisc_mask, 0);
		if (status && status != ICE_ERR_ALREADY_EXISTS) {
			printf("%s: Failed to enable promiscuous mode for "
			    "PF VSI, err %s aq_err %s\n",
			    sc->sc_dev.dv_xname,
			    ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			return (EIO);
		}
	} else {
		status = ice_clear_vsi_promisc(hw, sc->pf_vsi.idx,
					       promisc_mask, 0);
		if (status) {
			printf("%s: Failed to disable promiscuous mode for "
			    "PF VSI, err %s aq_err %s\n",
			    sc->sc_dev.dv_xname,
			    ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			return (EIO);
		}
	}

	return (0);
}

/**
 * ice_enable_intr - Enable interrupts for given vector
 * @hw: the device private HW structure
 * @vector: the interrupt index in PF space
 *
 * In MSI or Legacy interrupt mode, interrupt 0 is the only valid index.
 */
void
ice_enable_intr(struct ice_hw *hw, int vector)
{
	uint32_t dyn_ctl;

	/* Use ITR_NONE so that ITR configuration is not changed. */
	dyn_ctl = GLINT_DYN_CTL_INTENA_M | GLINT_DYN_CTL_CLEARPBA_M |
		  (ICE_ITR_NONE << GLINT_DYN_CTL_ITR_INDX_S);
	ICE_WRITE(hw, GLINT_DYN_CTL(vector), dyn_ctl);
}

/**
 * ice_disable_intr - Disable interrupts for given vector
 * @hw: the device private HW structure
 * @vector: the interrupt index in PF space
 *
 * In MSI or Legacy interrupt mode, interrupt 0 is the only valid index.
 */
void
ice_disable_intr(struct ice_hw *hw, int vector)
{
	uint32_t dyn_ctl;

	/* Use ITR_NONE so that ITR configuration is not changed. */
	dyn_ctl = ICE_ITR_NONE << GLINT_DYN_CTL_ITR_INDX_S;
	ICE_WRITE(hw, GLINT_DYN_CTL(vector), dyn_ctl);
}

/**
 * ice_copy_phy_caps_to_cfg - Copy PHY ability data to configuration data
 * @pi: port information structure
 * @caps: PHY ability structure to copy data from
 * @cfg: PHY configuration structure to copy data to
 *
 * Helper function to copy AQC PHY get ability data to PHY set configuration
 * data structure
 */
void
ice_copy_phy_caps_to_cfg(struct ice_port_info *pi,
			 struct ice_aqc_get_phy_caps_data *caps,
			 struct ice_aqc_set_phy_cfg_data *cfg)
{
	if (!pi || !caps || !cfg)
		return;

	memset(cfg, 0, sizeof(*cfg));
	cfg->phy_type_low = caps->phy_type_low;
	cfg->phy_type_high = caps->phy_type_high;
	cfg->caps = caps->caps;
	cfg->low_power_ctrl_an = caps->low_power_ctrl_an;
	cfg->eee_cap = caps->eee_cap;
	cfg->eeer_value = caps->eeer_value;
	cfg->link_fec_opt = caps->link_fec_options;
	cfg->module_compliance_enforcement =
		caps->module_compliance_enforcement;
}

#define ICE_PHYS_100MB			\
    (ICE_PHY_TYPE_LOW_100BASE_TX |	\
     ICE_PHY_TYPE_LOW_100M_SGMII)
#define ICE_PHYS_1000MB			\
    (ICE_PHY_TYPE_LOW_1000BASE_T |	\
     ICE_PHY_TYPE_LOW_1000BASE_SX |	\
     ICE_PHY_TYPE_LOW_1000BASE_LX |	\
     ICE_PHY_TYPE_LOW_1000BASE_KX |	\
     ICE_PHY_TYPE_LOW_1G_SGMII)
#define ICE_PHYS_2500MB			\
    (ICE_PHY_TYPE_LOW_2500BASE_T |	\
     ICE_PHY_TYPE_LOW_2500BASE_X |	\
     ICE_PHY_TYPE_LOW_2500BASE_KX)
#define ICE_PHYS_5GB			\
    (ICE_PHY_TYPE_LOW_5GBASE_T |	\
     ICE_PHY_TYPE_LOW_5GBASE_KR)
#define ICE_PHYS_10GB			\
    (ICE_PHY_TYPE_LOW_10GBASE_T |	\
     ICE_PHY_TYPE_LOW_10G_SFI_DA |	\
     ICE_PHY_TYPE_LOW_10GBASE_SR |	\
     ICE_PHY_TYPE_LOW_10GBASE_LR |	\
     ICE_PHY_TYPE_LOW_10GBASE_KR_CR1 |	\
     ICE_PHY_TYPE_LOW_10G_SFI_AOC_ACC |	\
     ICE_PHY_TYPE_LOW_10G_SFI_C2C)
#define ICE_PHYS_25GB			\
    (ICE_PHY_TYPE_LOW_25GBASE_T |	\
     ICE_PHY_TYPE_LOW_25GBASE_CR |	\
     ICE_PHY_TYPE_LOW_25GBASE_CR_S |	\
     ICE_PHY_TYPE_LOW_25GBASE_CR1 |	\
     ICE_PHY_TYPE_LOW_25GBASE_SR |	\
     ICE_PHY_TYPE_LOW_25GBASE_LR |	\
     ICE_PHY_TYPE_LOW_25GBASE_KR |	\
     ICE_PHY_TYPE_LOW_25GBASE_KR_S |	\
     ICE_PHY_TYPE_LOW_25GBASE_KR1 |	\
     ICE_PHY_TYPE_LOW_25G_AUI_AOC_ACC |	\
     ICE_PHY_TYPE_LOW_25G_AUI_C2C)
#define ICE_PHYS_40GB			\
    (ICE_PHY_TYPE_LOW_40GBASE_CR4 |	\
     ICE_PHY_TYPE_LOW_40GBASE_SR4 |	\
     ICE_PHY_TYPE_LOW_40GBASE_LR4 |	\
     ICE_PHY_TYPE_LOW_40GBASE_KR4 |	\
     ICE_PHY_TYPE_LOW_40G_XLAUI_AOC_ACC | \
     ICE_PHY_TYPE_LOW_40G_XLAUI)
#define ICE_PHYS_50GB			\
    (ICE_PHY_TYPE_LOW_50GBASE_CR2 |	\
     ICE_PHY_TYPE_LOW_50GBASE_SR2 |	\
     ICE_PHY_TYPE_LOW_50GBASE_LR2 |	\
     ICE_PHY_TYPE_LOW_50GBASE_KR2 |	\
     ICE_PHY_TYPE_LOW_50G_LAUI2_AOC_ACC | \
     ICE_PHY_TYPE_LOW_50G_LAUI2 |	\
     ICE_PHY_TYPE_LOW_50G_AUI2_AOC_ACC | \
     ICE_PHY_TYPE_LOW_50G_AUI2 |	\
     ICE_PHY_TYPE_LOW_50GBASE_CP |	\
     ICE_PHY_TYPE_LOW_50GBASE_SR |	\
     ICE_PHY_TYPE_LOW_50GBASE_FR |	\
     ICE_PHY_TYPE_LOW_50GBASE_LR |	\
     ICE_PHY_TYPE_LOW_50GBASE_KR_PAM4 |	\
     ICE_PHY_TYPE_LOW_50G_AUI1_AOC_ACC | \
     ICE_PHY_TYPE_LOW_50G_AUI1)
#define ICE_PHYS_100GB_LOW		\
    (ICE_PHY_TYPE_LOW_100GBASE_CR4 |	\
     ICE_PHY_TYPE_LOW_100GBASE_SR4 |	\
     ICE_PHY_TYPE_LOW_100GBASE_LR4 |	\
     ICE_PHY_TYPE_LOW_100GBASE_KR4 |	\
     ICE_PHY_TYPE_LOW_100G_CAUI4_AOC_ACC | \
     ICE_PHY_TYPE_LOW_100G_CAUI4 |	\
     ICE_PHY_TYPE_LOW_100G_AUI4_AOC_ACC | \
     ICE_PHY_TYPE_LOW_100G_AUI4 |	\
     ICE_PHY_TYPE_LOW_100GBASE_CR_PAM4 | \
     ICE_PHY_TYPE_LOW_100GBASE_KR_PAM4 | \
     ICE_PHY_TYPE_LOW_100GBASE_CP2 |	\
     ICE_PHY_TYPE_LOW_100GBASE_SR2 |	\
     ICE_PHY_TYPE_LOW_100GBASE_DR)
#define ICE_PHYS_100GB_HIGH		\
    (ICE_PHY_TYPE_HIGH_100GBASE_KR2_PAM4 | \
     ICE_PHY_TYPE_HIGH_100G_CAUI2_AOC_ACC | \
     ICE_PHY_TYPE_HIGH_100G_CAUI2 |	\
     ICE_PHY_TYPE_HIGH_100G_AUI2_AOC_ACC | \
     ICE_PHY_TYPE_HIGH_100G_AUI2)

/**
 * ice_sysctl_speeds_to_aq_phy_types - Convert sysctl speed flags to AQ PHY flags
 * @sysctl_speeds: 16-bit sysctl speeds or AQ_LINK_SPEED flags
 * @phy_type_low: output parameter for lower AQ PHY flags
 * @phy_type_high: output parameter for higher AQ PHY flags
 *
 * Converts the given link speed flags into AQ PHY type flag sets appropriate
 * for use in a Set PHY Config command.
 */
void
ice_sysctl_speeds_to_aq_phy_types(uint16_t sysctl_speeds,
    uint64_t *phy_type_low, uint64_t *phy_type_high)
{
	*phy_type_low = 0, *phy_type_high = 0;

	if (sysctl_speeds & ICE_AQ_LINK_SPEED_100MB)
		*phy_type_low |= ICE_PHYS_100MB;
	if (sysctl_speeds & ICE_AQ_LINK_SPEED_1000MB)
		*phy_type_low |= ICE_PHYS_1000MB;
	if (sysctl_speeds & ICE_AQ_LINK_SPEED_2500MB)
		*phy_type_low |= ICE_PHYS_2500MB;
	if (sysctl_speeds & ICE_AQ_LINK_SPEED_5GB)
		*phy_type_low |= ICE_PHYS_5GB;
	if (sysctl_speeds & ICE_AQ_LINK_SPEED_10GB)
		*phy_type_low |= ICE_PHYS_10GB;
	if (sysctl_speeds & ICE_AQ_LINK_SPEED_25GB)
		*phy_type_low |= ICE_PHYS_25GB;
	if (sysctl_speeds & ICE_AQ_LINK_SPEED_40GB)
		*phy_type_low |= ICE_PHYS_40GB;
	if (sysctl_speeds & ICE_AQ_LINK_SPEED_50GB)
		*phy_type_low |= ICE_PHYS_50GB;
	if (sysctl_speeds & ICE_AQ_LINK_SPEED_100GB) {
		*phy_type_low |= ICE_PHYS_100GB_LOW;
		*phy_type_high |= ICE_PHYS_100GB_HIGH;
	}
}

/**
 * @struct ice_phy_data
 * @brief PHY caps and link speeds
 *
 * Buffer providing report mode and user speeds;
 * returning intersection of PHY types and speeds.
 */
struct ice_phy_data {
	uint64_t phy_low_orig;     /* PHY low quad from report */
	uint64_t phy_high_orig;    /* PHY high quad from report */
	uint64_t phy_low_intr;     /* low quad intersection with user speeds */
	uint64_t phy_high_intr;    /* high quad intersection with user speeds */
	uint16_t user_speeds_orig; /* Input from caller - ICE_AQ_LINK_SPEED_* */
	uint16_t user_speeds_intr; /* Intersect with report speeds */
	uint8_t report_mode;       /* See ICE_AQC_REPORT_* */
};

/**
 * @var phy_link_speeds
 * @brief PHY link speed conversion array
 *
 * Array of link speeds to convert ICE_PHY_TYPE_LOW and ICE_PHY_TYPE_HIGH into
 * link speeds used by the link speed sysctls.
 *
 * @remark these are based on the indices used in the BIT() macros for the
 * ICE_PHY_TYPE_LOW_* and ICE_PHY_TYPE_HIGH_* definitions.
 */
static const uint16_t phy_link_speeds[] = {
    ICE_AQ_LINK_SPEED_100MB,
    ICE_AQ_LINK_SPEED_100MB,
    ICE_AQ_LINK_SPEED_1000MB,
    ICE_AQ_LINK_SPEED_1000MB,
    ICE_AQ_LINK_SPEED_1000MB,
    ICE_AQ_LINK_SPEED_1000MB,
    ICE_AQ_LINK_SPEED_1000MB,
    ICE_AQ_LINK_SPEED_2500MB,
    ICE_AQ_LINK_SPEED_2500MB,
    ICE_AQ_LINK_SPEED_2500MB,
    ICE_AQ_LINK_SPEED_5GB,
    ICE_AQ_LINK_SPEED_5GB,
    ICE_AQ_LINK_SPEED_10GB,
    ICE_AQ_LINK_SPEED_10GB,
    ICE_AQ_LINK_SPEED_10GB,
    ICE_AQ_LINK_SPEED_10GB,
    ICE_AQ_LINK_SPEED_10GB,
    ICE_AQ_LINK_SPEED_10GB,
    ICE_AQ_LINK_SPEED_10GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_25GB,
    ICE_AQ_LINK_SPEED_40GB,
    ICE_AQ_LINK_SPEED_40GB,
    ICE_AQ_LINK_SPEED_40GB,
    ICE_AQ_LINK_SPEED_40GB,
    ICE_AQ_LINK_SPEED_40GB,
    ICE_AQ_LINK_SPEED_40GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_50GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    /* These rates are for ICE_PHY_TYPE_HIGH_* */
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB,
    ICE_AQ_LINK_SPEED_100GB
};

/**
 * ice_aq_phy_types_to_link_speeds - Convert the PHY Types to speeds
 * @phy_type_low: lower 64-bit PHY Type bitmask
 * @phy_type_high: upper 64-bit PHY Type bitmask
 *
 * Convert the PHY Type fields from Get PHY Abilities and Set PHY Config into
 * link speed flags. If phy_type_high has an unknown PHY type, then the return
 * value will include the "ICE_AQ_LINK_SPEED_UNKNOWN" flag as well.
 */
uint16_t
ice_aq_phy_types_to_link_speeds(uint64_t phy_type_low, uint64_t phy_type_high)
{
	uint16_t sysctl_speeds = 0;
	int bit;

	for (bit = 0; bit < 64; bit++) {
		if (phy_type_low & (1ULL << bit))
			sysctl_speeds |= phy_link_speeds[bit];
	}

	for (bit = 0; bit < 64; bit++) {
		if ((phy_type_high & (1ULL << bit)) == 0)
			continue;
		if ((bit + 64) < (int)nitems(phy_link_speeds))
			sysctl_speeds |= phy_link_speeds[bit + 64];
		else
			sysctl_speeds |= ICE_AQ_LINK_SPEED_UNKNOWN;
	}

	return (sysctl_speeds);
}

/**
 * ice_apply_supported_speed_filter - Mask off unsupported speeds
 * @report_speeds: bit-field for the desired link speeds
 * @mod_type: type of module/sgmii connection we have
 *
 * Given a bitmap of the desired lenient mode link speeds,
 * this function will mask off the speeds that are not currently
 * supported by the device.
 */
uint16_t
ice_apply_supported_speed_filter(uint16_t report_speeds, uint8_t mod_type)
{
	uint16_t speed_mask;
	enum { IS_SGMII, IS_SFP, IS_QSFP } module;

	/*
	 * The SFF specification says 0 is unknown, so we'll
	 * treat it like we're connected through SGMII for now.
	 * This may need revisiting if a new type is supported
	 * in the future.
	 */
	switch (mod_type) {
	case 0:
		module = IS_SGMII;
		break;
	case 3:
		module = IS_SFP;
		break;
	default:
		module = IS_QSFP;
		break;
	}

	/* We won't offer anything lower than 100M for any part,
	 * but we'll need to mask off other speeds based on the
	 * device and module type.
	 */
	speed_mask = ~((uint16_t)ICE_AQ_LINK_SPEED_100MB - 1);
	if ((report_speeds & ICE_AQ_LINK_SPEED_10GB) && (module == IS_SFP))
		speed_mask = ~((uint16_t)ICE_AQ_LINK_SPEED_1000MB - 1);
	if (report_speeds & ICE_AQ_LINK_SPEED_25GB)
		speed_mask = ~((uint16_t)ICE_AQ_LINK_SPEED_1000MB - 1);
	if (report_speeds & ICE_AQ_LINK_SPEED_50GB) {
		speed_mask = ~((uint16_t)ICE_AQ_LINK_SPEED_1000MB - 1);
		if (module == IS_QSFP)
			speed_mask = ~((uint16_t)ICE_AQ_LINK_SPEED_10GB - 1);
	}
	if (report_speeds & ICE_AQ_LINK_SPEED_100GB)
		speed_mask = ~((uint16_t)ICE_AQ_LINK_SPEED_25GB - 1);
	return (report_speeds & speed_mask);
}

/**
 * ice_intersect_phy_types_and_speeds - Return intersection of link speeds
 * @sc: device private structure
 * @phy_data: device PHY data
 *
 * On read: Displays the currently supported speeds
 * On write: Sets the device's supported speeds
 * Valid input flags: see ICE_SYSCTL_HELP_ADVERTISE_SPEED
 */
int
ice_intersect_phy_types_and_speeds(struct ice_softc *sc,
				   struct ice_phy_data *phy_data)
{
	struct ice_aqc_get_phy_caps_data pcaps = { 0 };
	const char *report_types[5] = { "w/o MEDIA",
					"w/MEDIA",
					"ACTIVE",
					"EDOOFUS", /* Not used */
					"DFLT" };
	struct ice_hw *hw = &sc->hw;
	struct ice_port_info *pi = hw->port_info;
	enum ice_status status;
	uint16_t report_speeds, temp_speeds;
	uint8_t report_type;
	bool apply_speed_filter = false;

	switch (phy_data->report_mode) {
	case ICE_AQC_REPORT_TOPO_CAP_NO_MEDIA:
	case ICE_AQC_REPORT_TOPO_CAP_MEDIA:
	case ICE_AQC_REPORT_ACTIVE_CFG:
	case ICE_AQC_REPORT_DFLT_CFG:
		report_type = phy_data->report_mode >> 1;
		break;
	default:
		DPRINTF("%s: phy_data.report_mode \"%u\" doesn't exist\n",
		    __func__, phy_data->report_mode);
		return (EINVAL);
	}

	/* 0 is treated as "Auto"; the driver will handle selecting the
	 * correct speeds. Including, in some cases, applying an override
	 * if provided.
	 */
	if (phy_data->user_speeds_orig == 0)
		phy_data->user_speeds_orig = USHRT_MAX;
	else if (ice_is_bit_set(sc->feat_en, ICE_FEATURE_LENIENT_LINK_MODE))
		apply_speed_filter = true;

	status = ice_aq_get_phy_caps(pi, false, phy_data->report_mode, &pcaps, NULL);
	if (status != ICE_SUCCESS) {
		printf("%s: ice_aq_get_phy_caps (%s) failed; status %s, "
		    "aq_err %s\n", sc->sc_dev.dv_xname,
		    report_types[report_type], ice_status_str(status),
		    ice_aq_str(sc->hw.adminq.sq_last_status));
		return (EIO);
	}

	phy_data->phy_low_orig = le64toh(pcaps.phy_type_low);
	phy_data->phy_high_orig = le64toh(pcaps.phy_type_high);
	report_speeds = ice_aq_phy_types_to_link_speeds(phy_data->phy_low_orig,
	    phy_data->phy_high_orig);
	if (apply_speed_filter) {
		temp_speeds = ice_apply_supported_speed_filter(report_speeds,
		    pcaps.module_type[0]);
		if ((phy_data->user_speeds_orig & temp_speeds) == 0) {
			printf("%s: User-specified speeds (\"0x%04X\") not "
			    "supported\n", sc->sc_dev.dv_xname,
			    phy_data->user_speeds_orig);
			return (EINVAL);
		}
		report_speeds = temp_speeds;
	}
	ice_sysctl_speeds_to_aq_phy_types(phy_data->user_speeds_orig,
	    &phy_data->phy_low_intr, &phy_data->phy_high_intr);
	phy_data->user_speeds_intr = phy_data->user_speeds_orig & report_speeds;
	phy_data->phy_low_intr &= phy_data->phy_low_orig;
	phy_data->phy_high_intr &= phy_data->phy_high_orig;

	return (0);
 }

/**
 * ice_apply_saved_phy_req_to_cfg -- Write saved user PHY settings to cfg data
 * @sc: device private structure
 * @cfg: new PHY config data to be modified
 *
 * Applies user settings for advertised speeds to the PHY type fields in the
 * supplied PHY config struct. It uses the data from pcaps to check if the
 * saved settings are invalid and uses the pcaps data instead if they are
 * invalid.
 */
int
ice_apply_saved_phy_req_to_cfg(struct ice_softc *sc,
			       struct ice_aqc_set_phy_cfg_data *cfg)
{
	struct ice_phy_data phy_data = { 0 };
	struct ice_port_info *pi = sc->hw.port_info;
	uint64_t phy_low = 0, phy_high = 0;
	uint16_t link_speeds;
	int ret;

	link_speeds = pi->phy.curr_user_speed_req;
	if (ice_is_bit_set(sc->feat_en, ICE_FEATURE_LINK_MGMT_VER_2)) {
		memset(&phy_data, 0, sizeof(phy_data));
		phy_data.report_mode = ICE_AQC_REPORT_DFLT_CFG;
		phy_data.user_speeds_orig = link_speeds;
		ret = ice_intersect_phy_types_and_speeds(sc, &phy_data);
		if (ret != 0)
			return (ret);
		phy_low = phy_data.phy_low_intr;
		phy_high = phy_data.phy_high_intr;

		if (link_speeds == 0 || phy_data.user_speeds_intr)
			goto finalize_link_speed;
		if (ice_is_bit_set(sc->feat_en,
		    ICE_FEATURE_LENIENT_LINK_MODE)) {
			memset(&phy_data, 0, sizeof(phy_data));
			phy_data.report_mode = ICE_AQC_REPORT_TOPO_CAP_NO_MEDIA;
			phy_data.user_speeds_orig = link_speeds;
			ret = ice_intersect_phy_types_and_speeds(sc, &phy_data);
			if (ret != 0)
				return (ret);
			phy_low = phy_data.phy_low_intr;
			phy_high = phy_data.phy_high_intr;

			if (!phy_data.user_speeds_intr) {
				phy_low = phy_data.phy_low_orig;
				phy_high = phy_data.phy_high_orig;
			}
			goto finalize_link_speed;
		}
		/* If we're here, then it means the benefits of Version 2
		 * link management aren't utilized.  We fall through to
		 * handling Strict Link Mode the same as Version 1 link
		 * management.
		 */
	}

	memset(&phy_data, 0, sizeof(phy_data));
	if ((link_speeds == 0) &&
	    (sc->ldo_tlv.phy_type_low || sc->ldo_tlv.phy_type_high))
		phy_data.report_mode = ICE_AQC_REPORT_TOPO_CAP_NO_MEDIA;
	else
		phy_data.report_mode = ICE_AQC_REPORT_TOPO_CAP_MEDIA;
	phy_data.user_speeds_orig = link_speeds;
	ret = ice_intersect_phy_types_and_speeds(sc, &phy_data);
	if (ret != 0)
		return (ret);
	phy_low = phy_data.phy_low_intr;
	phy_high = phy_data.phy_high_intr;

	if (!ice_is_bit_set(sc->feat_en, ICE_FEATURE_LENIENT_LINK_MODE)) {
		if (phy_low == 0 && phy_high == 0) {
			printf("%s: The selected speed is not supported by "
			    "the current media. Please select a link speed "
			    "that is supported by the current media.\n",
			    sc->sc_dev.dv_xname);
			return (EINVAL);
		}
	} else {
		if (link_speeds == 0) {
			if (sc->ldo_tlv.phy_type_low & phy_low ||
			    sc->ldo_tlv.phy_type_high & phy_high) {
				phy_low &= sc->ldo_tlv.phy_type_low;
				phy_high &= sc->ldo_tlv.phy_type_high;
			}
		} else if (phy_low == 0 && phy_high == 0) {
			memset(&phy_data, 0, sizeof(phy_data));
			phy_data.report_mode = ICE_AQC_REPORT_TOPO_CAP_NO_MEDIA;
			phy_data.user_speeds_orig = link_speeds;
			ret = ice_intersect_phy_types_and_speeds(sc, &phy_data);
			if (ret != 0)
				return (ret);
			phy_low = phy_data.phy_low_intr;
			phy_high = phy_data.phy_high_intr;

			if (!phy_data.user_speeds_intr) {
				phy_low = phy_data.phy_low_orig;
				phy_high = phy_data.phy_high_orig;
			}
		}
	}

finalize_link_speed:
	/* Cache new user settings for speeds */
	pi->phy.curr_user_speed_req = phy_data.user_speeds_intr;
	cfg->phy_type_low = htole64(phy_low);
	cfg->phy_type_high = htole64(phy_high);

	return (ret);
}

/**
 * ice_get_pfa_module_tlv - Reads sub module TLV from NVM PFA
 * @hw: pointer to hardware structure
 * @module_tlv: pointer to module TLV to return
 * @module_tlv_len: pointer to module TLV length to return
 * @module_type: module type requested
 *
 * Finds the requested sub module TLV type from the Preserved Field
 * Area (PFA) and returns the TLV pointer and length. The caller can
 * use these to read the variable length TLV value.
 */
enum ice_status
ice_get_pfa_module_tlv(struct ice_hw *hw, uint16_t *module_tlv,
    uint16_t *module_tlv_len, uint16_t module_type)
{
	enum ice_status status;
	uint16_t pfa_len, pfa_ptr;
	uint16_t next_tlv;

	status = ice_read_sr_word(hw, ICE_SR_PFA_PTR, &pfa_ptr);
	if (status != ICE_SUCCESS) {
		DNPRINTF(ICE_DBG_INIT, "%s: Preserved Field Array pointer.\n",
		    __func__);
		return status;
	}
	status = ice_read_sr_word(hw, pfa_ptr, &pfa_len);
	if (status != ICE_SUCCESS) {
		DNPRINTF(ICE_DBG_INIT, "%s: Failed to read PFA length.\n",
		    __func__);
		return status;
	}
	/* Starting with first TLV after PFA length, iterate through the list
	 * of TLVs to find the requested one.
	 */
	next_tlv = pfa_ptr + 1;
	while (next_tlv < pfa_ptr + pfa_len) {
		uint16_t tlv_sub_module_type;
		uint16_t tlv_len;

		/* Read TLV type */
		status = ice_read_sr_word(hw, next_tlv, &tlv_sub_module_type);
		if (status != ICE_SUCCESS) {
			DNPRINTF(ICE_DBG_INIT, "%s: Failed to read TLV type.\n",
			    __func__);
			break;
		}
		/* Read TLV length */
		status = ice_read_sr_word(hw, next_tlv + 1, &tlv_len);
		if (status != ICE_SUCCESS) {
			DNPRINTF(ICE_DBG_INIT,
			    "%s: Failed to read TLV length.\n", __func__);
			break;
		}
		if (tlv_sub_module_type == module_type) {
			if (tlv_len) {
				*module_tlv = next_tlv;
				*module_tlv_len = tlv_len;
				return ICE_SUCCESS;
			}
			return ICE_ERR_INVAL_SIZE;
		}
		/* Check next TLV, i.e. current TLV pointer + length + 2 words
		 * (for current TLV's type and length)
		 */
		next_tlv = next_tlv + tlv_len + 2;
	}
	/* Module does not exist */
	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_get_link_default_override
 * @ldo: pointer to the link default override struct
 * @pi: pointer to the port info struct
 *
 * Gets the link default override for a port
 */
enum ice_status
ice_get_link_default_override(struct ice_link_default_override_tlv *ldo,
			      struct ice_port_info *pi)
{
	uint16_t i, tlv, tlv_len, tlv_start, buf, offset;
	struct ice_hw *hw = pi->hw;
	enum ice_status status;

	status = ice_get_pfa_module_tlv(hw, &tlv, &tlv_len,
					ICE_SR_LINK_DEFAULT_OVERRIDE_PTR);
	if (status) {
		DNPRINTF(ICE_DBG_INIT,
		     "%s: Failed to read link override TLV.\n", __func__);
		return status;
	}

	/* Each port has its own config; calculate for our port */
	tlv_start = tlv + pi->lport * ICE_SR_PFA_LINK_OVERRIDE_WORDS +
		ICE_SR_PFA_LINK_OVERRIDE_OFFSET;

	/* link options first */
	status = ice_read_sr_word(hw, tlv_start, &buf);
	if (status) {
		DNPRINTF(ICE_DBG_INIT,
		    "%s: Failed to read override link options.\n", __func__);
		return status;
	}
	ldo->options = buf & ICE_LINK_OVERRIDE_OPT_M;
	ldo->phy_config = (buf & ICE_LINK_OVERRIDE_PHY_CFG_M) >>
		ICE_LINK_OVERRIDE_PHY_CFG_S;

	/* link PHY config */
	offset = tlv_start + ICE_SR_PFA_LINK_OVERRIDE_FEC_OFFSET;
	status = ice_read_sr_word(hw, offset, &buf);
	if (status) {
		DNPRINTF(ICE_DBG_INIT,
		    "%s: Failed to read override phy config.\n", __func__);
		return status;
	}
	ldo->fec_options = buf & ICE_LINK_OVERRIDE_FEC_OPT_M;

	/* PHY types low */
	offset = tlv_start + ICE_SR_PFA_LINK_OVERRIDE_PHY_OFFSET;
	for (i = 0; i < ICE_SR_PFA_LINK_OVERRIDE_PHY_WORDS; i++) {
		status = ice_read_sr_word(hw, (offset + i), &buf);
		if (status) {
			DNPRINTF(ICE_DBG_INIT,
			    "%s: Failed to read override link options.\n",
			    __func__);
			return status;
		}
		/* shift 16 bits at a time to fill 64 bits */
		ldo->phy_type_low |= ((uint64_t)buf << (i * 16));
	}

	/* PHY types high */
	offset = tlv_start + ICE_SR_PFA_LINK_OVERRIDE_PHY_OFFSET +
		ICE_SR_PFA_LINK_OVERRIDE_PHY_WORDS;
	for (i = 0; i < ICE_SR_PFA_LINK_OVERRIDE_PHY_WORDS; i++) {
		status = ice_read_sr_word(hw, (offset + i), &buf);
		if (status) {
			DNPRINTF(ICE_DBG_INIT,
			    "%s: Failed to read override link options.\n",
			    __func__);
			return status;
		}
		/* shift 16 bits at a time to fill 64 bits */
		ldo->phy_type_high |= ((uint64_t)buf << (i * 16));
	}

	return status;
}

/**
 * ice_fw_supports_fec_dis_auto
 * @hw: pointer to the hardware structure
 *
 * Checks if the firmware supports FEC disable in Auto FEC mode
 */
bool ice_fw_supports_fec_dis_auto(struct ice_hw *hw)
{
	return ice_is_fw_min_ver(hw, ICE_FW_VER_BRANCH_E810,
				 ICE_FW_FEC_DIS_AUTO_MAJ,
				 ICE_FW_FEC_DIS_AUTO_MIN,
				 ICE_FW_FEC_DIS_AUTO_PATCH) ||
	       ice_is_fw_min_ver(hw, ICE_FW_VER_BRANCH_E82X,
				 ICE_FW_FEC_DIS_AUTO_MAJ_E82X,
				 ICE_FW_FEC_DIS_AUTO_MIN_E82X,
				 ICE_FW_FEC_DIS_AUTO_PATCH_E82X);
}

/**
 * ice_cfg_phy_fec - Configure PHY FEC data based on FEC mode
 * @pi: port information structure
 * @cfg: PHY configuration data to set FEC mode
 * @fec: FEC mode to configure
 */
enum ice_status
ice_cfg_phy_fec(struct ice_port_info *pi, struct ice_aqc_set_phy_cfg_data *cfg,
		enum ice_fec_mode fec)
{
	struct ice_aqc_get_phy_caps_data *pcaps;
	enum ice_status status = ICE_SUCCESS;
	struct ice_hw *hw;

	if (!pi || !cfg)
		return ICE_ERR_BAD_PTR;

	hw = pi->hw;

	pcaps = (struct ice_aqc_get_phy_caps_data *)
		ice_malloc(hw, sizeof(*pcaps));
	if (!pcaps)
		return ICE_ERR_NO_MEMORY;

	status = ice_aq_get_phy_caps(pi, false,
				     (ice_fw_supports_report_dflt_cfg(hw) ?
				      ICE_AQC_REPORT_DFLT_CFG :
				      ICE_AQC_REPORT_TOPO_CAP_MEDIA), pcaps,
				      NULL);

	if (status)
		goto out;

	cfg->caps |= (pcaps->caps & ICE_AQC_PHY_EN_AUTO_FEC);
	cfg->link_fec_opt = pcaps->link_fec_options;

	switch (fec) {
	case ICE_FEC_BASER:
		/* Clear RS bits, and AND BASE-R ability
		 * bits and OR request bits.
		 */
		cfg->link_fec_opt &= ICE_AQC_PHY_FEC_10G_KR_40G_KR4_EN |
			ICE_AQC_PHY_FEC_25G_KR_CLAUSE74_EN;
		cfg->link_fec_opt |= ICE_AQC_PHY_FEC_10G_KR_40G_KR4_REQ |
			ICE_AQC_PHY_FEC_25G_KR_REQ;
		break;
	case ICE_FEC_RS:
		/* Clear BASE-R bits, and AND RS ability
		 * bits and OR request bits.
		 */
		cfg->link_fec_opt &= ICE_AQC_PHY_FEC_25G_RS_CLAUSE91_EN;
		cfg->link_fec_opt |= ICE_AQC_PHY_FEC_25G_RS_528_REQ |
			ICE_AQC_PHY_FEC_25G_RS_544_REQ;
		break;
	case ICE_FEC_NONE:
		/* Clear all FEC option bits. */
		cfg->link_fec_opt &= ~ICE_AQC_PHY_FEC_MASK;
		break;
	case ICE_FEC_DIS_AUTO:
		/* Set No FEC and auto FEC */
		if (!ice_fw_supports_fec_dis_auto(hw)) {
			status = ICE_ERR_NOT_SUPPORTED;
			goto out;
		}
		cfg->link_fec_opt |= ICE_AQC_PHY_FEC_DIS;
		/* fall-through */
	case ICE_FEC_AUTO:
		/* AND auto FEC bit, and all caps bits. */
		cfg->caps &= ICE_AQC_PHY_CAPS_MASK;
		cfg->link_fec_opt |= pcaps->link_fec_options;
		break;
	default:
		status = ICE_ERR_PARAM;
		break;
	}

	if (fec == ICE_FEC_AUTO && ice_fw_supports_link_override(pi->hw) &&
	    !ice_fw_supports_report_dflt_cfg(pi->hw)) {
		struct ice_link_default_override_tlv tlv;

		if (ice_get_link_default_override(&tlv, pi))
			goto out;

		if (!(tlv.options & ICE_LINK_OVERRIDE_STRICT_MODE) &&
		    (tlv.options & ICE_LINK_OVERRIDE_EN))
			cfg->link_fec_opt = tlv.fec_options;
	}

out:
	ice_free(hw, pcaps);

	return status;
}

/**
 * ice_apply_saved_fec_req_to_cfg -- Write saved user FEC mode to cfg data
 * @sc: device private structure
 * @cfg: new PHY config data to be modified
 *
 * Applies user setting for FEC mode to PHY config struct. It uses the data
 * from pcaps to check if the saved settings are invalid and uses the pcaps
 * data instead if they are invalid.
 */
int
ice_apply_saved_fec_req_to_cfg(struct ice_softc *sc,
			       struct ice_aqc_set_phy_cfg_data *cfg)
{
	struct ice_port_info *pi = sc->hw.port_info;
	enum ice_status status;

	cfg->caps &= ~ICE_AQC_PHY_EN_AUTO_FEC;
	status = ice_cfg_phy_fec(pi, cfg, pi->phy.curr_user_fec_req);
	if (status)
		return (EIO);

	return (0);
}

/**
 * ice_apply_saved_fc_req_to_cfg -- Write saved user flow control mode to cfg data
 * @pi: port info struct
 * @cfg: new PHY config data to be modified
 *
 * Applies user setting for flow control mode to PHY config struct. There are
 * no invalid flow control mode settings; if there are, then this function
 * treats them like "ICE_FC_NONE".
 */
void
ice_apply_saved_fc_req_to_cfg(struct ice_port_info *pi,
			      struct ice_aqc_set_phy_cfg_data *cfg)
{
	cfg->caps &= ~(ICE_AQ_PHY_ENA_TX_PAUSE_ABILITY |
		       ICE_AQ_PHY_ENA_RX_PAUSE_ABILITY);

	switch (pi->phy.curr_user_fc_req) {
	case ICE_FC_FULL:
		cfg->caps |= ICE_AQ_PHY_ENA_TX_PAUSE_ABILITY |
			     ICE_AQ_PHY_ENA_RX_PAUSE_ABILITY;
		break;
	case ICE_FC_RX_PAUSE:
		cfg->caps |= ICE_AQ_PHY_ENA_RX_PAUSE_ABILITY;
		break;
	case ICE_FC_TX_PAUSE:
		cfg->caps |= ICE_AQ_PHY_ENA_TX_PAUSE_ABILITY;
		break;
	default:
		/* ICE_FC_NONE */
		break;
	}
}

/**
 * ice_caps_to_fc_mode
 * @caps: PHY capabilities
 *
 * Convert PHY FC capabilities to ice FC mode
 */
enum ice_fc_mode
ice_caps_to_fc_mode(uint8_t caps)
{
	if (caps & ICE_AQC_PHY_EN_TX_LINK_PAUSE &&
	    caps & ICE_AQC_PHY_EN_RX_LINK_PAUSE)
		return ICE_FC_FULL;

	if (caps & ICE_AQC_PHY_EN_TX_LINK_PAUSE)
		return ICE_FC_TX_PAUSE;

	if (caps & ICE_AQC_PHY_EN_RX_LINK_PAUSE)
		return ICE_FC_RX_PAUSE;

	return ICE_FC_NONE;
}

/**
 * ice_caps_to_fec_mode
 * @caps: PHY capabilities
 * @fec_options: Link FEC options
 *
 * Convert PHY FEC capabilities to ice FEC mode
 */
enum ice_fec_mode
ice_caps_to_fec_mode(uint8_t caps, uint8_t fec_options)
{
	if (caps & ICE_AQC_PHY_EN_AUTO_FEC) {
		if (fec_options & ICE_AQC_PHY_FEC_DIS)
			return ICE_FEC_DIS_AUTO;
		else
			return ICE_FEC_AUTO;
	}

	if (fec_options & (ICE_AQC_PHY_FEC_10G_KR_40G_KR4_EN |
			   ICE_AQC_PHY_FEC_10G_KR_40G_KR4_REQ |
			   ICE_AQC_PHY_FEC_25G_KR_CLAUSE74_EN |
			   ICE_AQC_PHY_FEC_25G_KR_REQ))
		return ICE_FEC_BASER;

	if (fec_options & (ICE_AQC_PHY_FEC_25G_RS_528_REQ |
			   ICE_AQC_PHY_FEC_25G_RS_544_REQ |
			   ICE_AQC_PHY_FEC_25G_RS_CLAUSE91_EN))
		return ICE_FEC_RS;

	return ICE_FEC_NONE;
}

/**
 * ice_aq_set_phy_cfg
 * @hw: pointer to the HW struct
 * @pi: port info structure of the interested logical port
 * @cfg: structure with PHY configuration data to be set
 * @cd: pointer to command details structure or NULL
 *
 * Set the various PHY configuration parameters supported on the Port.
 * One or more of the Set PHY config parameters may be ignored in an MFP
 * mode as the PF may not have the privilege to set some of the PHY Config
 * parameters. This status will be indicated by the command response (0x0601).
 */
enum ice_status
ice_aq_set_phy_cfg(struct ice_hw *hw, struct ice_port_info *pi,
		   struct ice_aqc_set_phy_cfg_data *cfg, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;
	enum ice_status status;

	if (!cfg)
		return ICE_ERR_PARAM;

	/* Ensure that only valid bits of cfg->caps can be turned on. */
	if (cfg->caps & ~ICE_AQ_PHY_ENA_VALID_MASK) {
		DNPRINTF(ICE_DBG_PHY, "%s: Invalid bit is set in "
		    "ice_aqc_set_phy_cfg_data->caps : 0x%x\n",
		    __func__, cfg->caps);

		cfg->caps &= ICE_AQ_PHY_ENA_VALID_MASK;
	}

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_phy_cfg);
	desc.params.set_phy.lport_num = pi->lport;
	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	DNPRINTF(ICE_DBG_LINK, "set phy cfg\n");
	DNPRINTF(ICE_DBG_LINK, "	phy_type_low = 0x%llx\n",
	      (unsigned long long)le64toh(cfg->phy_type_low));
	DNPRINTF(ICE_DBG_LINK, "	phy_type_high = 0x%llx\n",
	      (unsigned long long)le64toh(cfg->phy_type_high));
	DNPRINTF(ICE_DBG_LINK, "	caps = 0x%x\n", cfg->caps);
	DNPRINTF(ICE_DBG_LINK, "	low_power_ctrl_an = 0x%x\n",
	      cfg->low_power_ctrl_an);
	DNPRINTF(ICE_DBG_LINK, "	eee_cap = 0x%x\n", cfg->eee_cap);
	DNPRINTF(ICE_DBG_LINK, "	eeer_value = 0x%x\n", cfg->eeer_value);
	DNPRINTF(ICE_DBG_LINK, "	link_fec_opt = 0x%x\n",
		  cfg->link_fec_opt);

	status = ice_aq_send_cmd(hw, &desc, cfg, sizeof(*cfg), cd);

	if (hw->adminq.sq_last_status == ICE_AQ_RC_EMODE)
		status = ICE_SUCCESS;

	if (!status)
		pi->phy.curr_user_phy_cfg = *cfg;

	return status;
}

/**
 * ice_apply_saved_phy_cfg -- Re-apply user PHY config settings
 * @sc: device private structure
 * @settings: which settings to apply
 *
 * Applies user settings for advertised speeds, FEC mode, and flow
 * control mode to a PHY config struct; it uses the data from pcaps
 * to check if the saved settings are invalid and uses the pcaps
 * data instead if they are invalid.
 *
 * For things like sysctls where only one setting needs to be
 * updated, the bitmap allows the caller to specify which setting
 * to update.
 */
int
ice_apply_saved_phy_cfg(struct ice_softc *sc, uint8_t settings)
{
	struct ice_aqc_set_phy_cfg_data cfg = { 0 };
	struct ice_port_info *pi = sc->hw.port_info;
	struct ice_aqc_get_phy_caps_data pcaps = { 0 };
	struct ice_hw *hw = &sc->hw;
	uint64_t phy_low, phy_high;
	enum ice_status status;
	enum ice_fec_mode dflt_fec_mode;
	uint16_t dflt_user_speed;

	if (!settings || settings > ICE_APPLY_LS_FEC_FC) {
		DNPRINTF(ICE_DBG_LINK, "%s: Settings out-of-bounds: %u\n",
		    __func__, settings);
		return EINVAL;
	}

	status = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_ACTIVE_CFG,
				     &pcaps, NULL);
	if (status != ICE_SUCCESS) {
		printf("%s: ice_aq_get_phy_caps (ACTIVE) failed; "
		    "status %s, aq_err %s\n",
		    sc->sc_dev.dv_xname, ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		return (EIO);
	}

	phy_low = le64toh(pcaps.phy_type_low);
	phy_high = le64toh(pcaps.phy_type_high);

	/* Save off initial config parameters */
	dflt_user_speed = ice_aq_phy_types_to_link_speeds(phy_low, phy_high);
	dflt_fec_mode = ice_caps_to_fec_mode(pcaps.caps,
	    pcaps.link_fec_options);

	/* Setup new PHY config */
	ice_copy_phy_caps_to_cfg(pi, &pcaps, &cfg);

	/* On error, restore active configuration values */
	if ((settings & ICE_APPLY_LS) &&
	    ice_apply_saved_phy_req_to_cfg(sc, &cfg)) {
		pi->phy.curr_user_speed_req = dflt_user_speed;
		cfg.phy_type_low = pcaps.phy_type_low;
		cfg.phy_type_high = pcaps.phy_type_high;
	}
	if ((settings & ICE_APPLY_FEC) &&
	    ice_apply_saved_fec_req_to_cfg(sc, &cfg)) {
		pi->phy.curr_user_fec_req = dflt_fec_mode;
	}
	if (settings & ICE_APPLY_FC) {
		/* No real error indicators for this process,
		 * so we'll just have to assume it works. */
		ice_apply_saved_fc_req_to_cfg(pi, &cfg);
	}

	/* Enable link and re-negotiate it */
	cfg.caps |= ICE_AQ_PHY_ENA_AUTO_LINK_UPDT | ICE_AQ_PHY_ENA_LINK;

	status = ice_aq_set_phy_cfg(hw, pi, &cfg, NULL);
	if (status != ICE_SUCCESS) {
		/* Don't indicate failure if there's no media in the port.
		 * The settings have been saved and will apply when media
		 * is inserted.
		 */
		if ((status == ICE_ERR_AQ_ERROR) &&
		    (hw->adminq.sq_last_status == ICE_AQ_RC_EBUSY)) {
			DPRINTF("%s: Setting will be applied when media is "
			    "inserted\n", __func__);
			return (0);
		} else {
			printf("%s: ice_aq_set_phy_cfg failed; status %s, "
			    "aq_err %s\n", sc->sc_dev.dv_xname,
			    ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			return (EIO);
		}
	}

	return (0);
}

/**
 * ice_aq_set_link_restart_an
 * @pi: pointer to the port information structure
 * @ena_link: if true: enable link, if false: disable link
 * @cd: pointer to command details structure or NULL
 *
 * Sets up the link and restarts the Auto-Negotiation over the link.
 */
enum ice_status
ice_aq_set_link_restart_an(struct ice_port_info *pi, bool ena_link,
			   struct ice_sq_cd *cd)
{
	enum ice_status status = ICE_ERR_AQ_ERROR;
	struct ice_aqc_restart_an *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.restart_an;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_restart_an);

	cmd->cmd_flags = ICE_AQC_RESTART_AN_LINK_RESTART;
	cmd->lport_num = pi->lport;
	if (ena_link)
		cmd->cmd_flags |= ICE_AQC_RESTART_AN_LINK_ENABLE;
	else
		cmd->cmd_flags &= ~ICE_AQC_RESTART_AN_LINK_ENABLE;

	status = ice_aq_send_cmd(pi->hw, &desc, NULL, 0, cd);
	if (status)
		return status;

	if (ena_link)
		pi->phy.curr_user_phy_cfg.caps |= ICE_AQC_PHY_EN_LINK;
	else
		pi->phy.curr_user_phy_cfg.caps &= ~ICE_AQC_PHY_EN_LINK;

	return ICE_SUCCESS;
}

/**
 * ice_set_link -- Set up/down link on phy
 * @sc: device private structure
 * @enabled: link status to set up
 *
 * This should be called when change of link status is needed.
 */
void
ice_set_link(struct ice_softc *sc, bool enabled)
{
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	if (ice_driver_is_detaching(sc))
		return;

	if (ice_test_state(&sc->state, ICE_STATE_NO_MEDIA))
		return;

	if (enabled)
		ice_apply_saved_phy_cfg(sc, ICE_APPLY_LS_FEC_FC);
	else {
		status = ice_aq_set_link_restart_an(hw->port_info, false, NULL);
		if (status != ICE_SUCCESS) {
			if (hw->adminq.sq_last_status == ICE_AQ_RC_EMODE)
				printf("%s: Link control not enabled in "
				    "current device mode\n",
				    sc->sc_dev.dv_xname);
			else
				printf("%s: could not restart link: status %s, "
				    "aq_err %s\n", sc->sc_dev.dv_xname,
				    ice_status_str(status),
				    ice_aq_str(hw->adminq.sq_last_status));
		} else
			sc->link_up = false;
	}
}

int
ice_up(struct ice_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_rx_queue *rxq;
	struct ice_tx_queue *txq;
	struct ifqueue *ifq;
	struct ifiqueue *ifiq;
	struct ice_intr_vector *iv;
	int i, err;

	rw_enter_write(&sc->sc_cfg_lock);
	if (sc->sc_dead) {
		rw_exit_write(&sc->sc_cfg_lock);
		return (ENXIO);
	}

	ice_update_laa_mac(sc);

	/* Initialize software Tx tracking values */
	ice_init_tx_tracking(&sc->pf_vsi);

	for (i = 0, rxq = vsi->rx_queues; i < vsi->num_rx_queues; i++, rxq++) {
		ice_rxfill(sc, rxq);

		/* wire everything together */
		iv = &sc->sc_vectors[i];
		iv->iv_rxq = rxq;
		rxq->irqv = iv;

		ifiq = ifp->if_iqs[i];
		ifiq->ifiq_softc = rxq;
		rxq->rxq_ifiq = ifiq;
	}

	for (i = 0, txq = vsi->tx_queues; i < vsi->num_tx_queues; i++, txq++) {
		/* wire everything together */
		iv = &sc->sc_vectors[i];
		iv->iv_txq = txq;
		txq->irqv = iv;

		ifq = ifp->if_ifqs[i];
		ifq->ifq_softc = txq;
		txq->txq_ifq = ifq;
	}

	err = ice_cfg_vsi_for_tx(&sc->pf_vsi);
	if (err) {
		printf("%s: Unable to configure the main VSI for Tx: err %d\n",
		    sc->sc_dev.dv_xname, err);
		rw_exit_write(&sc->sc_cfg_lock);
		return err;
	}

	err = ice_cfg_vsi_for_rx(&sc->pf_vsi);
	if (err) {
		printf("%s: Unable to configure the main VSI for Rx: err %d\n",
		    sc->sc_dev.dv_xname, err);
		goto err_cleanup_tx;
	}

	err = ice_control_all_rx_queues(&sc->pf_vsi, true);
	if (err) {
		printf("%s: Could not enable Rx rings: err %d\n",
		    sc->sc_dev.dv_xname, err);
		goto err_cleanup_tx;
	}

	err = ice_cfg_pf_default_mac_filters(sc);
	if (err) {
		printf("%s: Unable to configure default MAC filters: %d\n",
		    sc->sc_dev.dv_xname, err);
		goto err_stop_rx;
	}

	ice_configure_all_rxq_interrupts(&sc->pf_vsi);
	ice_configure_rx_itr(&sc->pf_vsi);

	ice_configure_all_txq_interrupts(&sc->pf_vsi);

	/* Configure promiscuous mode */
	ice_if_promisc_set(sc);

	if (!ice_testandclear_state(&sc->state, ICE_STATE_FIRST_INIT_LINK) &&
	    (!sc->link_up && ((ifp->if_flags & IFF_UP) ||
	    ice_test_state(&sc->state, ICE_STATE_LINK_ACTIVE_ON_DOWN))))
		ice_set_link(sc, true);

	/* Enable Rx queue interrupts */
	for (i = 0; i < vsi->num_rx_queues; i++) {
		int v = vsi->rx_queues[i].irqv->iv_qid + 1;
		ice_enable_intr(&sc->hw, v);
	}

	timeout_add_nsec(&sc->sc_admin_timer, SEC_TO_NSEC(1));

	ifp->if_flags |= IFF_RUNNING;

	ice_set_state(&sc->state, ICE_STATE_DRIVER_INITIALIZED);
	rw_exit_write(&sc->sc_cfg_lock);
	return 0;

err_stop_rx:
	ice_control_all_rx_queues(&sc->pf_vsi, false);
err_cleanup_tx:
	ice_vsi_disable_tx(&sc->pf_vsi);
	rw_exit_write(&sc->sc_cfg_lock);
	return err;
}

/**
 * ice_find_ucast_rule_entry - Search for a unicast MAC filter rule entry
 * @list_head: head of rule list
 * @f_info: rule information
 *
 * Helper function to search for a unicast rule entry - this is to be used
 * to remove unicast MAC filter that is not shared with other VSIs on the
 * PF switch.
 *
 * Returns pointer to entry storing the rule if found
 */
struct ice_fltr_mgmt_list_entry *
ice_find_ucast_rule_entry(struct ice_fltr_mgmt_list_head *list_head,
			  struct ice_fltr_info *f_info)
{
	struct ice_fltr_mgmt_list_entry *list_itr;

	TAILQ_FOREACH(list_itr, list_head, list_entry) {
		if (!memcmp(&f_info->l_data, &list_itr->fltr_info.l_data,
			    sizeof(f_info->l_data)) &&
		    f_info->fwd_id.hw_vsi_id ==
		    list_itr->fltr_info.fwd_id.hw_vsi_id &&
		    f_info->flag == list_itr->fltr_info.flag)
			return list_itr;
	}
	return NULL;
}

/**
 * ice_remove_mac_rule - remove a MAC based filter rule
 * @hw: pointer to the hardware structure
 * @m_list: list of MAC addresses and forwarding information
 * @recp_list: list from which function remove MAC address
 *
 * This function removes either a MAC filter rule or a specific VSI from a
 * VSI list for a multicast MAC address.
 *
 * Returns ICE_ERR_DOES_NOT_EXIST if a given entry was not added by
 * ice_add_mac. Caller should be aware that this call will only work if all
 * the entries passed into m_list were added previously. It will not attempt to
 * do a partial remove of entries that were found.
 */
enum ice_status
ice_remove_mac_rule(struct ice_hw *hw, struct ice_fltr_list_head *m_list,
		    struct ice_sw_recipe *recp_list)
{
	struct ice_fltr_list_entry *list_itr, *tmp;
	struct ice_lock *rule_lock; /* Lock to protect filter rule list */

	if (!m_list)
		return ICE_ERR_PARAM;

	rule_lock = &recp_list->filt_rule_lock;
	TAILQ_FOREACH_SAFE(list_itr, m_list, list_entry, tmp) {
		enum ice_sw_lkup_type l_type = list_itr->fltr_info.lkup_type;
		uint8_t *add = &list_itr->fltr_info.l_data.mac.mac_addr[0];
		uint16_t vsi_handle;

		if (l_type != ICE_SW_LKUP_MAC)
			return ICE_ERR_PARAM;

		vsi_handle = list_itr->fltr_info.vsi_handle;
		if (!ice_is_vsi_valid(hw, vsi_handle))
			return ICE_ERR_PARAM;

		list_itr->fltr_info.fwd_id.hw_vsi_id =
		    hw->vsi_ctx[vsi_handle]->vsi_num;
		if (!ETHER_IS_MULTICAST(add) && !hw->umac_shared) {
			/* Don't remove the unicast address that belongs to
			 * another VSI on the switch, since it is not being
			 * shared...
			 */
#if 0
			ice_acquire_lock(rule_lock);
#endif
			if (!ice_find_ucast_rule_entry(&recp_list->filt_rules,
						       &list_itr->fltr_info)) {
#if 0
				ice_release_lock(rule_lock);
#endif
				return ICE_ERR_DOES_NOT_EXIST;
			}
#if 0
			ice_release_lock(rule_lock);
#endif
		}
		list_itr->status = ice_remove_rule_internal(hw, recp_list,
							    list_itr);
		if (list_itr->status)
			return list_itr->status;
	}
	return ICE_SUCCESS;
}

/**
 * ice_remove_mac - remove a MAC address based filter rule
 * @hw: pointer to the hardware structure
 * @m_list: list of MAC addresses and forwarding information
 *
 */
enum ice_status
ice_remove_mac(struct ice_hw *hw, struct ice_fltr_list_head *m_list)
{
	struct ice_sw_recipe *recp_list;

	recp_list = &hw->switch_info->recp_list[ICE_SW_LKUP_MAC];
	return ice_remove_mac_rule(hw, m_list, recp_list);
}

/**
 * ice_remove_vsi_mac_filter - Remove a MAC address filter for a VSI
 * @vsi: the VSI to add the filter for
 * @addr: MAC address to remove a filter for
 *
 * Remove a MAC address filter from a given VSI. This is a wrapper around
 * ice_remove_mac to simplify the interface. First, it only accepts a single
 * address, so we don't have to mess around with the list setup in other
 * functions. Second, it ignores the ICE_ERR_DOES_NOT_EXIST error, so that
 * callers don't need to worry about attempting to remove filters which
 * haven't yet been added.
 */
int
ice_remove_vsi_mac_filter(struct ice_vsi *vsi, uint8_t *addr)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_fltr_list_head mac_addr_list;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	int err = 0;

	TAILQ_INIT(&mac_addr_list);

	err = ice_add_mac_to_list(vsi, &mac_addr_list, addr, ICE_FWD_TO_VSI);
	if (err)
		goto free_mac_list;

	status = ice_remove_mac(hw, &mac_addr_list);
	if (status && status != ICE_ERR_DOES_NOT_EXIST) {
		DPRINTF("%s: failed to remove a filter for MAC %s, "
		    "err %s aq_err %s\n", __func__,
		    ether_sprintf(addr), ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		err = EIO;
	}

free_mac_list:
	ice_free_fltr_list(&mac_addr_list);
	return err;
}

/**
 * ice_rm_pf_default_mac_filters - Remove default unicast and broadcast addrs
 * @sc: device softc structure
 *
 * Remove the default unicast and broadcast filters from the PF VSI.
 */
int
ice_rm_pf_default_mac_filters(struct ice_softc *sc)
{
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_hw *hw = &sc->hw;
	int err;

	/* Remove the LAN MAC address */
	err = ice_remove_vsi_mac_filter(vsi, hw->port_info->mac.lan_addr);
	if (err)
		return err;

	/* Remove the broadcast address */
	err = ice_remove_vsi_mac_filter(vsi, etherbroadcastaddr);
	if (err)
		return (EIO);

	return (0);
}

/**
 * ice_flush_rxq_interrupts - Unconfigure Hw Rx queues MSI-X interrupt cause
 * @vsi: the VSI to configure
 *
 * Unset the CAUSE_ENA flag of the TQCTL register for each queue, then trigger
 * a software interrupt on that cause. This is required as part of the Rx
 * queue disable logic to dissociate the Rx queue from the interrupt.
 *
 * This function must be called prior to disabling Rx queues with
 * ice_control_all_rx_queues, otherwise the Rx queue may not be disabled
 * properly.
 */
void
ice_flush_rxq_interrupts(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	int i;

	for (i = 0; i < vsi->num_rx_queues; i++) {
		struct ice_rx_queue *rxq = &vsi->rx_queues[i];
		uint32_t reg, val;
		int v = rxq->irqv->iv_qid + 1;
		int tries = 0;

		/* Clear the CAUSE_ENA flag */
		reg = vsi->rx_qmap[rxq->me];
		val = ICE_READ(hw, QINT_RQCTL(reg));
		val &= ~QINT_RQCTL_CAUSE_ENA_M;
		ICE_WRITE(hw, QINT_RQCTL(reg), val);

		ice_flush(hw);

		/* Trigger a software interrupt to complete interrupt
		 * dissociation.
		 */
		sc->sw_intr[v] = -1;
		ICE_WRITE(hw, GLINT_DYN_CTL(v),
		     GLINT_DYN_CTL_SWINT_TRIG_M | GLINT_DYN_CTL_INTENA_MSK_M);
		do {
			int ret;

			/* Sleep to allow interrupt processing to occur. */
			ret = tsleep_nsec(&sc->sw_intr[v], 0, "iceswi",
			    USEC_TO_NSEC(1));
			if (ret == 0 && sc->sw_intr[v] == 1) {
				sc->sw_intr[v] = 0;
				break;
			}
			tries++;
		} while (tries < 10);
		if (tries == 10)
			DPRINTF("%s: missed software interrupt\n", __func__);
	}
}

/**
 * ice_flush_txq_interrupts - Unconfigure Hw Tx queues MSI-X interrupt cause
 * @vsi: the VSI to configure
 *
 * Unset the CAUSE_ENA flag of the TQCTL register for each queue, then trigger
 * a software interrupt on that cause. This is required as part of the Tx
 * queue disable logic to dissociate the Tx queue from the interrupt.
 *
 * This function must be called prior to ice_vsi_disable_tx, otherwise
 * the Tx queue disable may not complete properly.
 */
void
ice_flush_txq_interrupts(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	int i;

	for (i = 0; i < vsi->num_tx_queues; i++) {
		struct ice_tx_queue *txq = &vsi->tx_queues[i];
		uint32_t reg, val;
		int v = txq->irqv->iv_qid + 1;
		int tries = 0;

		/* Clear the CAUSE_ENA flag */
		reg = vsi->tx_qmap[txq->me];
		val = ICE_READ(hw, QINT_TQCTL(reg));
		val &= ~QINT_TQCTL_CAUSE_ENA_M;
		ICE_WRITE(hw, QINT_TQCTL(reg), val);

		ice_flush(hw);

		/* Trigger a software interrupt to complete interrupt
		 * dissociation.
		 */
		sc->sw_intr[v] = -1;
		ICE_WRITE(hw, GLINT_DYN_CTL(v),
		     GLINT_DYN_CTL_SWINT_TRIG_M | GLINT_DYN_CTL_INTENA_MSK_M);
		do {
			int ret;

			/* Sleep to allow interrupt processing to occur. */
			ret = tsleep_nsec(&sc->sw_intr[v], 0, "iceswi",
			    USEC_TO_NSEC(1));
			if (ret == 0 && sc->sw_intr[v] == 1) {
				sc->sw_intr[v] = 0;
				break;
			}
			tries++;
		} while (tries < 10);
		if (tries == 10)
			DPRINTF("%s: missed software interrupt\n", __func__);
	}
}

void
ice_txq_clean(struct ice_softc *sc, struct ice_tx_queue *txq)
{
	struct ice_tx_map *txm;
	bus_dmamap_t map;
	unsigned int i;

	for (i = 0; i < txq->desc_count; i++) {
		txm = &txq->tx_map[i];

		if (txm->txm_m == NULL)
			continue;
		if (ISSET(txm->txm_m->m_pkthdr.csum_flags, M_TCP_TSO))
			map = txm->txm_map_tso;
		else
			map = txm->txm_map;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, map);

		m_freem(txm->txm_m);
		txm->txm_m = NULL;
		txm->txm_eop = -1;
	}

	txq->txq_cons = txq->txq_prod = 0;

	ifq_clr_oactive(txq->txq_ifq);
}

void
ice_rxq_clean(struct ice_softc *sc, struct ice_rx_queue *rxq)
{
	struct ice_rx_map *rxm;
	bus_dmamap_t map;
	unsigned int i;

	timeout_del_barrier(&rxq->rxq_refill);

	for (i = 0; i < rxq->desc_count; i++) {
		rxm = &rxq->rx_map[i];

		if (rxm->rxm_m == NULL)
			continue;

		map = rxm->rxm_map;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, map);

		m_freem(rxm->rxm_m);
		rxm->rxm_m = NULL;
	}

	if_rxr_init(&rxq->rxq_acct, ICE_MIN_DESC_COUNT, rxq->desc_count - 1);

	m_freem(rxq->rxq_m_head);
	rxq->rxq_m_head = NULL;
	rxq->rxq_m_tail = &rxq->rxq_m_head;

	rxq->rxq_prod = rxq->rxq_cons = 0;
}

int
ice_down(struct ice_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ice_hw *hw = &sc->hw;
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_tx_queue *txq;
	struct ice_rx_queue *rxq;
	int i;

	rw_enter_write(&sc->sc_cfg_lock);

	timeout_del(&sc->sc_admin_timer);
	ifp->if_flags &= ~IFF_RUNNING;
#if 0
	ASSERT_CTX_LOCKED(sc);
#endif
	if (!ice_testandclear_state(&sc->state, ICE_STATE_DRIVER_INITIALIZED)) {
		rw_exit_write(&sc->sc_cfg_lock);
		return 0;
	}

	if (ice_test_state(&sc->state, ICE_STATE_RESET_FAILED)) {
		printf("%s: request to stop interface cannot be completed "
		    "as the device failed to reset\n", sc->sc_dev.dv_xname);
		rw_exit_write(&sc->sc_cfg_lock);
		return ENODEV;
	}

	if (ice_test_state(&sc->state, ICE_STATE_PREPARED_FOR_RESET)) {
		DPRINTF("%s: request to stop interface while device is "
		    "prepared for impending reset\n", __func__);
		rw_exit_write(&sc->sc_cfg_lock);
		return EBUSY;
	}

	NET_UNLOCK();

	/*
	 * Disable all possible interrupts except ITR 0 because this handles
	 * the AdminQ interrupts, and we want to keep processing these even
	 * when the interface is down.
	 */
	for (i = 1; i < hw->func_caps.common_cap.num_msix_vectors; i++)
		ice_disable_intr(hw, i);
#if 0
	ice_rdma_pf_stop(sc);
#endif
	/* Remove the MAC filters, stop Tx, and stop Rx. We don't check the
	 * return of these functions because there's nothing we can really do
	 * if they fail, and the functions already print error messages.
	 * Just try to shut down as much as we can.
	 */
	ice_rm_pf_default_mac_filters(sc);

	/* Dissociate the Tx and Rx queues from the interrupts */
	ice_flush_txq_interrupts(&sc->pf_vsi);
	ice_flush_rxq_interrupts(&sc->pf_vsi);

	/* Disable the Tx and Rx queues */
	ice_vsi_disable_tx(&sc->pf_vsi);
	for (i = 0, txq = vsi->tx_queues; i < vsi->num_tx_queues; i++, txq++) {
		ifq_barrier(txq->txq_ifq);
		intr_barrier(txq->irqv->iv_ihc);
	}
	ice_control_all_rx_queues(&sc->pf_vsi, false);
	for (i = 0, rxq = vsi->rx_queues; i < vsi->num_rx_queues; i++, rxq++)
		timeout_del_barrier(&rxq->rxq_refill);

	if (!ice_test_state(&sc->state, ICE_STATE_LINK_ACTIVE_ON_DOWN) &&
		 !(ifp->if_flags & IFF_UP) && sc->link_up)
		ice_set_link(sc, false);
#if 0
	if (sc->mirr_if && ice_test_state(&mif->state, ICE_STATE_SUBIF_NEEDS_REINIT)) {
		ice_subif_if_stop(sc->mirr_if->subctx);
		device_printf(sc->dev, "The subinterface also comes down and up after reset\n");
	}
#endif

	for (i = 0, txq = vsi->tx_queues; i < vsi->num_tx_queues; i++, txq++)
		ice_txq_clean(sc, txq);
	for (i = 0, rxq = vsi->rx_queues; i < vsi->num_rx_queues; i++, rxq++)
		ice_rxq_clean(sc, rxq);

	rw_exit_write(&sc->sc_cfg_lock);
	NET_LOCK();
	return 0;
}

/* Read SFF EEPROM (0x06EE) */
int
ice_aq_sff_eeprom(struct ice_hw *hw, uint16_t lport, uint8_t bus_addr,
    uint16_t mem_addr, uint8_t page, uint8_t set_page,
    uint8_t *data, uint8_t length, int write, struct ice_sq_cd *cd)
{
	struct ice_aqc_sff_eeprom *cmd;
	struct ice_aq_desc desc;
	int status;

	if (!data || (mem_addr & 0xff00))
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_sff_eeprom);
	cmd = &desc.params.read_write_sff_param;
	desc.flags = htole16(ICE_AQ_FLAG_RD);
	cmd->lport_num = (uint8_t)(lport & 0xff);
	cmd->lport_num_valid = (uint8_t)((lport >> 8) & 0x01);
	cmd->i2c_bus_addr = htole16(
	    ((bus_addr >> 1) & ICE_AQC_SFF_I2CBUS_7BIT_M) |
	    ((set_page << ICE_AQC_SFF_SET_EEPROM_PAGE_S) &
	    ICE_AQC_SFF_SET_EEPROM_PAGE_M));
	cmd->i2c_mem_addr = htole16(mem_addr & 0xff);
	cmd->eeprom_page = htole16((uint16_t)page << ICE_AQC_SFF_EEPROM_PAGE_S);
	if (write)
		cmd->i2c_bus_addr |= htole16(ICE_AQC_SFF_IS_WRITE);

	status = ice_aq_send_cmd(hw, &desc, data, length, cd);
	return status;
}

int
ice_rw_sff_eeprom(struct ice_softc *sc, uint16_t dev_addr, uint16_t offset,
    uint8_t page, uint8_t* data, uint16_t length, uint8_t set_page, int write)
{
	struct ice_hw *hw = &sc->hw;
	int ret = 0, retries = 0;
	int status;

	if (length > 16)
		return (EINVAL);

	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE))
		return (ENOSYS);

	if (ice_test_state(&sc->state, ICE_STATE_NO_MEDIA))
		return (ENXIO);

	do {
		status = ice_aq_sff_eeprom(hw, 0, dev_addr, offset, page,
		    set_page, data, length, write, NULL);
		if (!status) {
			ret = 0;
			break;
		}
		if (status == ICE_ERR_AQ_ERROR &&
		    hw->adminq.sq_last_status == ICE_AQ_RC_EBUSY) {
			ret = EBUSY;
			continue;
		}
		if (status == ICE_ERR_AQ_ERROR &&
		    hw->adminq.sq_last_status == ICE_AQ_RC_EACCES) {
			/* FW says I2C access isn't supported */
			ret = EACCES;
			break;
		}
		if (status == ICE_ERR_AQ_ERROR &&
		    hw->adminq.sq_last_status == ICE_AQ_RC_EPERM) {
			ret = EPERM;
			break;
		} else {
			ret = EIO;
			break;
		}
	} while (retries++ < ICE_I2C_MAX_RETRIES);

	return (ret);
}

/*
 * Read from the SFF eeprom.
 * The I2C device address is typically 0xA0 or 0xA2. For more details on
 * the contents of an SFF eeprom, refer to SFF-8724 (SFP), SFF-8636 (QSFP),
 * and SFF-8024 (both).
 */
int
ice_read_sff_eeprom(struct ice_softc *sc, uint16_t dev_addr, uint16_t offset,
    uint8_t page, uint8_t* data, uint16_t length)
{
	return ice_rw_sff_eeprom(sc, dev_addr, offset, page, data, length,
	    0, 0);
}

/* Write to the SFF eeprom. */
int
ice_write_sff_eeprom(struct ice_softc *sc, uint16_t dev_addr, uint16_t offset,
    uint8_t page, uint8_t* data, uint16_t length, uint8_t set_page)
{
	return ice_rw_sff_eeprom(sc, dev_addr, offset, page, data, length,
	    1, set_page);
}

int
ice_get_sffpage(struct ice_softc *sc, struct if_sffpage *sff)
{
	struct ice_hw *hw = &sc->hw;
	struct ice_port_info *pi = hw->port_info;
	struct ice_link_status *li = &pi->phy.link_info;
	const uint16_t chunksize = 16;
	uint16_t offset = 0;
	uint8_t curpage = 0;
	int error;

	if (sff->sff_addr != IFSFF_ADDR_EEPROM &&
	    sff->sff_addr != IFSFF_ADDR_DDM)
		return (EINVAL);

	if (li->module_type[0] == ICE_SFF8024_ID_NONE)
		return (ENXIO);

	if (sff->sff_addr == IFSFF_ADDR_EEPROM &&
	    li->module_type[0] == ICE_SFF8024_ID_SFP) {
		error = ice_read_sff_eeprom(sc, sff->sff_addr, 127, 0,
		    &curpage, 1);
		if (error)
			return error;

		if (curpage != sff->sff_page) {
			error = ice_write_sff_eeprom(sc, sff->sff_addr, 127, 0,
			    &sff->sff_page, 1, 1);
			if (error)
				return error;
		}
	}

	for (; offset <= IFSFF_DATA_LEN - chunksize; offset += chunksize) {
		error = ice_read_sff_eeprom(sc, sff->sff_addr, offset,
		    sff->sff_page, &sff->sff_data[0] + offset, chunksize);
		if (error)
			return error;
	}

	if (sff->sff_addr == IFSFF_ADDR_EEPROM &&
	    li->module_type[0] == ICE_SFF8024_ID_SFP &&
	    curpage != sff->sff_page) {
		error = ice_write_sff_eeprom(sc, sff->sff_addr, 127, 0,
		    &curpage, 1, 1);
		if (error)
			return error;
	}

	return 0;
}

int
ice_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ice_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	uint8_t addrhi[ETHER_ADDR_LEN], addrlo[ETHER_ADDR_LEN];
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				error = ice_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ice_down(sc);
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
		break;
	case SIOCADDMULTI:
		error = ether_addmulti(ifr, &sc->sc_ac);
		if (error == ENETRESET) {
			struct ice_vsi *vsi = &sc->pf_vsi;

			error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
			if (error)
				break;

			error = ice_add_vsi_mac_filter(vsi, addrlo);
			if (error)
				break;

			if (sc->sc_ac.ac_multirangecnt > 0) {
				SET(ifp->if_flags, IFF_ALLMULTI);
				error = ENETRESET;
			}
		}
		break;
	case SIOCDELMULTI:
		error = ether_delmulti(ifr, &sc->sc_ac);
		if (error == ENETRESET) {
			struct ice_vsi *vsi = &sc->pf_vsi;

			error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
			if (error)
				break;

			error = ice_remove_vsi_mac_filter(vsi, addrlo);
			if (error)
				break;

			if (ISSET(ifp->if_flags, IFF_ALLMULTI) &&
			    sc->sc_ac.ac_multirangecnt == 0) {
				CLR(ifp->if_flags, IFF_ALLMULTI);
				error = ENETRESET;
			}
		}
		break;
	case SIOCGIFSFFPAGE:
		error = rw_enter(&ice_sff_lock, RW_WRITE|RW_INTR);
		if (error)
			break;
		error = ice_get_sffpage(sc, (struct if_sffpage *)data);
		rw_exit(&ice_sff_lock);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		error = 0;
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			ice_down(sc);
			error = ice_up(sc);
		}
	}

	splx(s);
	return error;
}

/**
 * ice_tso_detect_sparse - detect TSO packets with too many segments
 *
 * Hardware only transmits packets with a maximum of 8 descriptors. For TSO
 * packets, hardware needs to be able to build the split packets using 8 or
 * fewer descriptors. Additionally, the header must be contained within at
 * most 3 descriptors.
 *
 * To verify this, we walk the headers to find out how many descriptors the
 * headers require (usually 1). Then we ensure that, for each TSO segment, its
 * data plus the headers are contained within 8 or fewer descriptors.
 */
int
ice_tso_detect_sparse(struct mbuf *m, struct ether_extracted *ext,
    bus_dmamap_t map)
{
	int count, curseg, i, hlen, segsz, seglen, hdrs, maxsegs;
	bus_dma_segment_t *segs;
	uint64_t paylen, outlen, nsegs;

	curseg = hdrs = 0;

	hlen = ETHER_HDR_LEN + ext->iphlen + ext->tcphlen;
	outlen = MIN(9668, MAX(64, m->m_pkthdr.ph_mss));
	paylen = m->m_pkthdr.len - hlen;
	nsegs = (paylen + outlen - 1) / outlen;

	segs = map->dm_segs;

	/* First, count the number of descriptors for the header.
	 * Additionally, make sure it does not span more than 3 segments.
	 */
	i = 0;
	curseg = segs[0].ds_len;
	while (hlen > 0) {
		hdrs++;
		if (hdrs > ICE_MAX_TSO_HDR_SEGS)
			return (1);
		if (curseg == 0) {
			i++;
			if (i == nsegs)
				return (1);

			curseg = segs[i].ds_len;
		}
		seglen = MIN(curseg, hlen);
		curseg -= seglen;
		hlen -= seglen;
	}

	maxsegs = ICE_MAX_TSO_SEGS - hdrs;

	/* We must count the headers, in order to verify that they take up
	 * 128 or fewer descriptors. However, we don't need to check the data
	 * if the total segments is small.
	 */
	if (nsegs <= maxsegs)
		return (0);

	count = 0;

	/* Now check the data to make sure that each TSO segment is made up of
	 * no more than maxsegs descriptors. This ensures that hardware will
	 * be capable of performing TSO offload.
	 */
	while (paylen > 0) {
		segsz = m->m_pkthdr.ph_mss;
		while (segsz > 0 && paylen != 0) {
			count++;
			if (count > maxsegs)
				return (1);
			if (curseg == 0) {
				i++;
				if (i == nsegs)
					return (1);
				curseg = segs[i].ds_len;
			}
			seglen = MIN(curseg, segsz);
			segsz -= seglen;
			curseg -= seglen;
			paylen -= seglen;
		}
		count = 0;
	}

	return (0);
}

uint64_t
ice_tx_setup_offload(struct mbuf *m0, struct ether_extracted *ext)
{
	uint64_t offload = 0, hlen;

#if NVLAN > 0
	if (ISSET(m0->m_flags, M_VLANTAG)) {
		uint64_t vtag = htole16(m0->m_pkthdr.ether_vtag);
		offload |= (ICE_TX_DESC_CMD_IL2TAG1 << ICE_TXD_QW1_CMD_S) |
		    (vtag << ICE_TXD_QW1_L2TAG1_S);
	}
#endif
	if (!ISSET(m0->m_pkthdr.csum_flags,
	    M_IPV4_CSUM_OUT|M_TCP_CSUM_OUT|M_UDP_CSUM_OUT|M_TCP_TSO))
		return offload;

	hlen = ext->iphlen;

	if (ext->ip4) {
		if (ISSET(m0->m_pkthdr.csum_flags, M_IPV4_CSUM_OUT))
			offload |= ICE_TX_DESC_CMD_IIPT_IPV4_CSUM <<
			    ICE_TXD_QW1_CMD_S;
		else
			offload |= ICE_TX_DESC_CMD_IIPT_IPV4 <<
			    ICE_TXD_QW1_CMD_S;
	} else if (ext->ip6)
		offload |= ICE_TX_DESC_CMD_IIPT_IPV6 << ICE_TXD_QW1_CMD_S;
	else
		return offload;

	offload |= ((ETHER_HDR_LEN >> 1) << ICE_TX_DESC_LEN_MACLEN_S) <<
	    ICE_TXD_QW1_OFFSET_S;
	offload |= ((hlen >> 2) << ICE_TX_DESC_LEN_IPLEN_S) <<
	    ICE_TXD_QW1_OFFSET_S;

	if (ext->tcp && ISSET(m0->m_pkthdr.csum_flags, M_TCP_CSUM_OUT)) {
		offload |= ICE_TX_DESC_CMD_L4T_EOFT_TCP << ICE_TXD_QW1_CMD_S;
		offload |= ((uint64_t)(ext->tcphlen >> 2) <<
		    ICE_TX_DESC_LEN_L4_LEN_S) << ICE_TXD_QW1_OFFSET_S;
	} else if (ext->udp && ISSET(m0->m_pkthdr.csum_flags, M_UDP_CSUM_OUT)) {
		offload |= ICE_TX_DESC_CMD_L4T_EOFT_UDP << ICE_TXD_QW1_CMD_S;
		offload |= ((uint64_t)(sizeof(*ext->udp) >> 2) <<
		    ICE_TX_DESC_LEN_L4_LEN_S) << ICE_TXD_QW1_OFFSET_S;
	}

	return offload;
}

static inline int
ice_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map, struct mbuf *m)
{
	int error;

	error = bus_dmamap_load_mbuf(dmat, map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT);
	if (error != EFBIG)
		return (error);

	error = m_defrag(m, M_DONTWAIT);
	if (error != 0)
		return (error);

	return (bus_dmamap_load_mbuf(dmat, map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT));
}

void
ice_set_tso_context(struct mbuf *m0, struct ice_tx_queue *txq,
    unsigned int prod, struct ether_extracted *ext)
{
	struct ice_tx_desc *ring;
	struct ice_tx_ctx_desc *txd;
	uint64_t qword1 = 0, paylen, outlen;

	/*
	 * The MSS should not be set to a lower value than 64.
	 */
	outlen = MAX(64, m0->m_pkthdr.ph_mss);
	paylen = m0->m_pkthdr.len - ETHER_HDR_LEN - ext->iphlen - ext->tcphlen;

	ring = ICE_DMA_KVA(&txq->tx_desc_mem);
	txd = (struct ice_tx_ctx_desc *)&ring[prod];

	qword1 |= ICE_TX_DESC_DTYPE_CTX;
	qword1 |= ICE_TX_CTX_DESC_TSO << ICE_TXD_CTX_QW1_CMD_S;
	qword1 |= paylen << ICE_TXD_CTX_QW1_TSO_LEN_S;
	qword1 |= outlen << ICE_TXD_CTX_QW1_MSS_S;

	htolem32(&txd->tunneling_params, 0);
	htolem16(&txd->l2tag2, 0);
	htolem16(&txd->rsvd, 0);
	htolem64(&txd->qw1, qword1);

	tcpstat_add(tcps_outpkttso, (paylen + outlen - 1) / outlen);
}

void
ice_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct ice_softc *sc = ifp->if_softc;
	struct ice_tx_queue *txq = ifq->ifq_softc;
	struct ice_tx_desc *ring, *txd;
	struct ice_tx_map *txm;
	bus_dmamap_t map;
	struct mbuf *m;
	uint64_t qword1;
	unsigned int prod, free, last, i;
	unsigned int mask;
	int post = 0;
	uint64_t offload;
	uint64_t paddr;
	uint64_t seglen;
	struct ether_extracted ext;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	if (!LINK_STATE_IS_UP(ifp->if_link_state)) {
		ifq_purge(ifq);
		return;
	}

	prod = txq->txq_prod;
	free = txq->txq_cons;
	if (free <= prod)
		free += txq->desc_count;
	free -= prod;

	bus_dmamap_sync(sc->sc_dmat, ICE_DMA_MAP(&txq->tx_desc_mem),
	    0, ICE_DMA_LEN(&txq->tx_desc_mem), BUS_DMASYNC_POSTWRITE);

	ring = ICE_DMA_KVA(&txq->tx_desc_mem);
	mask = txq->desc_count - 1;

	for (;;) {
		/* We need one extra descriptor for TSO packets. */
		if (free <= (ICE_MIN_DESC_COUNT + 1)) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		ether_extract_headers(m, &ext);
		offload = ice_tx_setup_offload(m, &ext);

		if (ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO)) {
			if (ext.tcp == NULL || m->m_pkthdr.ph_mss == 0 ||
			    m->m_pkthdr.ph_mss > ICE_TXD_CTX_MAX_MSS) {
				tcpstat_inc(tcps_outbadtso);
				ifq->ifq_errors++;
				m_freem(m);
				continue;
			}
		}

		txm = &txq->tx_map[prod];

		if (ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO))
			map = txm->txm_map_tso;
		else
			map = txm->txm_map;

		if (ice_load_mbuf(sc->sc_dmat, map, m) != 0) {
			ifq->ifq_errors++;
			m_freem(m);
			continue;
		}

		if (ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO)) {
			if (ice_tso_detect_sparse(m, &ext, map)) {
				bus_dmamap_unload(sc->sc_dmat, map);
				if (m_defrag(m, M_DONTWAIT) != 0 ||
				    bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
				    BUS_DMA_STREAMING | BUS_DMA_NOWAIT) != 0) {
					tcpstat_inc(tcps_outbadtso);
					ifq->ifq_errors++;
					m_freem(m);
					continue;
				}
				if (ice_tso_detect_sparse(m, &ext, map)) {
					bus_dmamap_unload(sc->sc_dmat, map);
					tcpstat_inc(tcps_outbadtso);
					ifq->ifq_errors++;
					m_freem(m);
					continue;
				}
			}

			ice_set_tso_context(m, txq, prod, &ext);
			prod++;
			prod &= mask;
			free--;
		}

		bus_dmamap_sync(sc->sc_dmat, map, 0,
		    map->dm_mapsize, BUS_DMASYNC_PREWRITE);

		for (i = 0; i < map->dm_nsegs; i++) {
			txd = &ring[prod];

			paddr = (uint64_t)map->dm_segs[i].ds_addr;
			seglen = (uint64_t)map->dm_segs[i].ds_len;

			qword1 = ICE_TX_DESC_DTYPE_DATA | offload |
			    (seglen << ICE_TXD_QW1_TX_BUF_SZ_S);

			htolem64(&txd->buf_addr, paddr);
			htolem64(&txd->cmd_type_offset_bsz, qword1);

			last = prod;

			prod++;
			prod &= mask;
		}

		/* Set the last descriptor for report */
		qword1 |= (ICE_TX_DESC_CMD_EOP | ICE_TX_DESC_CMD_RS) <<
		    ICE_TXD_QW1_CMD_S;
		htolem64(&txd->cmd_type_offset_bsz, qword1);

		txm->txm_m = m;
		txm->txm_eop = last;

#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m, BPF_DIRECTION_OUT);
#endif

		free -= i;
		post = 1;
	}

	bus_dmamap_sync(sc->sc_dmat, ICE_DMA_MAP(&txq->tx_desc_mem),
	    0, ICE_DMA_LEN(&txq->tx_desc_mem), BUS_DMASYNC_PREWRITE);

	if (post) {
		txq->txq_prod = prod;
		ICE_WRITE(&sc->hw, txq->tail, prod);
	}
}

void
ice_watchdog(struct ifnet * ifp)
{
	printf("%s\n", __func__);
}

int
ice_media_change(struct ifnet *ifp)
{
	printf("%s\n", __func__);
	return ENXIO;
}

/**
 * ice_get_phy_type_low - Get media associated with phy_type_low
 * @phy_type_low: the low 64bits of phy_type from the AdminQ
 *
 * Given the lower 64bits of the phy_type from the hardware, return the
 * ifm_active bit associated. Return IFM_INST_ANY when phy_type_low is unknown.
 * Note that only one of ice_get_phy_type_low or ice_get_phy_type_high should
 * be called. If phy_type_low is zero, call ice_phy_type_high.
 */
uint64_t
ice_get_phy_type_low(struct ice_softc *sc, uint64_t phy_type_low)
{
	switch (phy_type_low) {
	case ICE_PHY_TYPE_LOW_100BASE_TX:
		return IFM_100_TX;
#if 0
	case ICE_PHY_TYPE_LOW_100M_SGMII:
		return IFM_100_SGMII;
#endif
	case ICE_PHY_TYPE_LOW_1000BASE_T:
		return IFM_1000_T;
	case ICE_PHY_TYPE_LOW_1000BASE_SX:
		return IFM_1000_SX;
	case ICE_PHY_TYPE_LOW_1000BASE_LX:
		return IFM_1000_LX;
	case ICE_PHY_TYPE_LOW_1000BASE_KX:
		return IFM_1000_KX;
#if 0
	case ICE_PHY_TYPE_LOW_1G_SGMII:
		return IFM_1000_SGMII;
#endif
	case ICE_PHY_TYPE_LOW_2500BASE_T:
		return IFM_2500_T;
#if 0
	case ICE_PHY_TYPE_LOW_2500BASE_X:
		return IFM_2500_X;
#endif
	case ICE_PHY_TYPE_LOW_2500BASE_KX:
		return IFM_2500_KX;
	case ICE_PHY_TYPE_LOW_5GBASE_T:
		return IFM_5000_T;
#if 0
	case ICE_PHY_TYPE_LOW_5GBASE_KR:
		return IFM_5000_KR;
#endif
	case ICE_PHY_TYPE_LOW_10GBASE_T:
		return IFM_10G_T;
	case ICE_PHY_TYPE_LOW_10G_SFI_DA:
		return IFM_10G_SFP_CU;
	case ICE_PHY_TYPE_LOW_10GBASE_SR:
		return IFM_10G_SR;
	case ICE_PHY_TYPE_LOW_10GBASE_LR:
		return IFM_10G_LR;
	case ICE_PHY_TYPE_LOW_10GBASE_KR_CR1:
		return IFM_10G_KR;
	case ICE_PHY_TYPE_LOW_10G_SFI_AOC_ACC:
		return IFM_10G_AOC;
	case ICE_PHY_TYPE_LOW_10G_SFI_C2C:
		return IFM_10G_SFI;
#if 0
	case ICE_PHY_TYPE_LOW_25GBASE_T:
		return IFM_25G_T;
#endif
	case ICE_PHY_TYPE_LOW_25GBASE_CR:
		return IFM_25G_CR;
#if 0
	case ICE_PHY_TYPE_LOW_25GBASE_CR_S:
		return IFM_25G_CR_S;
	case ICE_PHY_TYPE_LOW_25GBASE_CR1:
		return IFM_25G_CR1;
#endif
	case ICE_PHY_TYPE_LOW_25GBASE_SR:
		return IFM_25G_SR;
	case ICE_PHY_TYPE_LOW_25GBASE_LR:
		return IFM_25G_LR;
	case ICE_PHY_TYPE_LOW_25GBASE_KR:
		return IFM_25G_KR;
#if 0
	case ICE_PHY_TYPE_LOW_25GBASE_KR_S:
		return IFM_25G_KR_S;
	case ICE_PHY_TYPE_LOW_25GBASE_KR1:
		return IFM_25G_KR1;
#endif
	case ICE_PHY_TYPE_LOW_25G_AUI_AOC_ACC:
		return IFM_25G_AOC;
#if 0
	case ICE_PHY_TYPE_LOW_25G_AUI_C2C:
		return IFM_25G_AUI;
#endif
	case ICE_PHY_TYPE_LOW_40GBASE_CR4:
		return IFM_40G_CR4;
	case ICE_PHY_TYPE_LOW_40GBASE_SR4:
		return IFM_40G_SR4;
	case ICE_PHY_TYPE_LOW_40GBASE_LR4:
		return IFM_40G_LR4;
	case ICE_PHY_TYPE_LOW_40GBASE_KR4:
		return IFM_40G_KR4;
#if 0
	case ICE_PHY_TYPE_LOW_40G_XLAUI_AOC_ACC:
		return IFM_40G_XLAUI_AC;
	case ICE_PHY_TYPE_LOW_40G_XLAUI:
		return IFM_40G_XLAUI;
#endif
	case ICE_PHY_TYPE_LOW_50GBASE_CR2:
		return IFM_50G_CR2;
#if 0
	case ICE_PHY_TYPE_LOW_50GBASE_SR2:
		return IFM_50G_SR2;
	case ICE_PHY_TYPE_LOW_50GBASE_LR2:
		return IFM_50G_LR2;
#endif
	case ICE_PHY_TYPE_LOW_50GBASE_KR2:
		return IFM_50G_KR2;
#if 0
	case ICE_PHY_TYPE_LOW_50G_LAUI2_AOC_ACC:
		return IFM_50G_LAUI2_AC;
	case ICE_PHY_TYPE_LOW_50G_LAUI2:
		return IFM_50G_LAUI2;
	case ICE_PHY_TYPE_LOW_50G_AUI2_AOC_ACC:
		return IFM_50G_AUI2_AC;
	case ICE_PHY_TYPE_LOW_50G_AUI2:
		return IFM_50G_AUI2;
	case ICE_PHY_TYPE_LOW_50GBASE_CP:
		return IFM_50G_CP;
	case ICE_PHY_TYPE_LOW_50GBASE_SR:
		return IFM_50G_SR;
	case ICE_PHY_TYPE_LOW_50GBASE_FR:
		return IFM_50G_FR;
	case ICE_PHY_TYPE_LOW_50GBASE_LR:
		return IFM_50G_LR;
	case ICE_PHY_TYPE_LOW_50GBASE_KR_PAM4:
		return IFM_50G_KR_PAM4;
	case ICE_PHY_TYPE_LOW_50G_AUI1_AOC_ACC:
		return IFM_50G_AUI1_AC;
	case ICE_PHY_TYPE_LOW_50G_AUI1:
		return IFM_50G_AUI1;
#endif
	case ICE_PHY_TYPE_LOW_100GBASE_CR4:
		return IFM_100G_CR4;
	case ICE_PHY_TYPE_LOW_100GBASE_SR4:
		return IFM_100G_SR4;
	case ICE_PHY_TYPE_LOW_100GBASE_LR4:
		return IFM_100G_LR4;
	case ICE_PHY_TYPE_LOW_100GBASE_KR4:
		return IFM_100G_KR4;
#if 0
	case ICE_PHY_TYPE_LOW_100G_CAUI4_AOC_ACC:
		return IFM_100G_CAUI4_AC;
	case ICE_PHY_TYPE_LOW_100G_CAUI4:
		return IFM_100G_CAUI4;
	case ICE_PHY_TYPE_LOW_100G_AUI4_AOC_ACC:
		return IFM_100G_AUI4_AC;
	case ICE_PHY_TYPE_LOW_100G_AUI4:
		return IFM_100G_AUI4;
	case ICE_PHY_TYPE_LOW_100GBASE_CR_PAM4:
		return IFM_100G_CR_PAM4;
	case ICE_PHY_TYPE_LOW_100GBASE_KR_PAM4:
		return IFM_100G_KR_PAM4;
	case ICE_PHY_TYPE_LOW_100GBASE_CP2:
		return IFM_100G_CP2;
	case ICE_PHY_TYPE_LOW_100GBASE_SR2:
		return IFM_100G_SR2;
	case ICE_PHY_TYPE_LOW_100GBASE_DR:
		return IFM_100G_DR;
#endif
	default:
		DPRINTF("%s: unhandled low PHY type 0x%llx\n",
		    sc->sc_dev.dv_xname, phy_type_low);
		return IFM_INST_ANY;
	}
}

/**
 * ice_get_phy_type_high - Get media associated with phy_type_high
 * @phy_type_high: the upper 64bits of phy_type from the AdminQ
 *
 * Given the upper 64bits of the phy_type from the hardware, return the
 * ifm_active bit associated. Return IFM_INST_ANY on an unknown value. Note
 * that only one of ice_get_phy_type_low or ice_get_phy_type_high should be
 * called. If phy_type_high is zero, call ice_get_phy_type_low.
 */
uint64_t
ice_get_phy_type_high(struct ice_softc *sc, uint64_t phy_type_high)
{
	switch (phy_type_high) {
#if 0
	case ICE_PHY_TYPE_HIGH_100GBASE_KR2_PAM4:
		return IFM_100G_KR2_PAM4;
	case ICE_PHY_TYPE_HIGH_100G_CAUI2_AOC_ACC:
		return IFM_100G_CAUI2_AC;
	case ICE_PHY_TYPE_HIGH_100G_CAUI2:
		return IFM_100G_CAUI2;
	case ICE_PHY_TYPE_HIGH_100G_AUI2_AOC_ACC:
		return IFM_100G_AUI2_AC;
	case ICE_PHY_TYPE_HIGH_100G_AUI2:
		return IFM_100G_AUI2;
#endif
	default:
		DPRINTF("%s: unhandled high PHY type 0x%llx\n",
		    sc->sc_dev.dv_xname, phy_type_high);
		return IFM_INST_ANY;
	}
}

void
ice_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ice_softc *sc = ifp->if_softc;
	struct ice_link_status *li = &sc->hw.port_info->phy.link_info;
	uint64_t media;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	/* Never report link up or media types when in recovery mode */
	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE))
		return;

	if (!sc->link_up)
		return;

	ifmr->ifm_status |= IFM_ACTIVE;
	ifmr->ifm_active |= IFM_FDX;

	if (li->phy_type_low) {
		media = ice_get_phy_type_low(sc, li->phy_type_low);
		if (media != IFM_INST_ANY)
			ifmr->ifm_active |= media;
		else
			ifmr->ifm_active |= IFM_ETHER;
	} else if (li->phy_type_high) {
		media = ice_get_phy_type_high(sc, li->phy_type_high);
		if (media != IFM_INST_ANY)
			ifmr->ifm_active |= media;
		else
			ifmr->ifm_active |= IFM_ETHER;
	}

	/* Report flow control status as well */
	if (li->an_info & ICE_AQ_LINK_PAUSE_TX)
		ifmr->ifm_active |= IFM_ETH_TXPAUSE;
	if (li->an_info & ICE_AQ_LINK_PAUSE_RX)
		ifmr->ifm_active |= IFM_ETH_RXPAUSE;
}

/**
 * ice_add_media_types - Add supported media types to the media structure
 * @sc: ice private softc structure
 * @media: ifmedia structure to setup
 *
 * Looks up the supported phy types, and initializes the various media types
 * available.
 *
 * @pre this function must be protected from being called while another thread
 * is accessing the ifmedia types.
 */
enum ice_status
ice_add_media_types(struct ice_softc *sc, struct ifmedia *media)
{
	struct ice_aqc_get_phy_caps_data pcaps = { 0 };
	struct ice_port_info *pi = sc->hw.port_info;
	enum ice_status status;
	uint64_t phy_low, phy_high;
	int bit;
#if 0
	ASSERT_CFG_LOCKED(sc);
#endif
	/* the maximum possible media type index is 511. We probably don't
	 * need most of this space, but this ensures future compatibility when
	 * additional media types are used.
	 */
	ice_declare_bitmap(already_added, 511);

	/* Remove all previous media types */
	ifmedia_delete_instance(media, IFM_INST_ANY);

	status = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_ACTIVE_CFG,
				     &pcaps, NULL);
	if (status != ICE_SUCCESS) {
		printf("%s: ice_aq_get_phy_caps (ACTIVE) failed; status %s, aq_err %s\n",
		    sc->sc_dev.dv_xname, ice_status_str(status),
		    ice_aq_str(sc->hw.adminq.sq_last_status));
		return (status);
	}

	/* make sure the added bitmap is zero'd */
	memset(already_added, 0, sizeof(already_added));

	phy_low = le64toh(pcaps.phy_type_low);
	for (bit = 0; bit < 64; bit++) {
		uint64_t type = BIT_ULL(bit);
		uint64_t ostype;

		if ((phy_low & type) == 0)
			continue;

		/* get the OS media type */
		ostype = ice_get_phy_type_low(sc, type);
	
		/* don't bother adding the unknown type */
		if (ostype == IFM_INST_ANY)
			continue;

		/* only add each media type to the list once */
		if (ice_is_bit_set(already_added, ostype))
			continue;

		ifmedia_add(media, IFM_ETHER | ostype, 0, NULL);
		ice_set_bit(ostype, already_added);
	}

	phy_high = le64toh(pcaps.phy_type_high);
	for (bit = 0; bit < 64; bit++) {
		uint64_t type = BIT_ULL(bit);
		uint64_t ostype;

		if ((phy_high & type) == 0)
			continue;

		/* get the OS media type */
		ostype = ice_get_phy_type_high(sc, type);

		/* don't bother adding the unknown type */
		if (ostype == IFM_INST_ANY)
			continue;

		/* only add each media type to the list once */
		if (ice_is_bit_set(already_added, ostype))
			continue;

		ifmedia_add(media, IFM_ETHER | ostype, 0, NULL);
		ice_set_bit(ostype, already_added);
	}

	/* Use autoselect media by default */
	ifmedia_add(media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(media, IFM_ETHER | IFM_AUTO);

	return (ICE_SUCCESS);
}

/**
 * ice_is_fw_health_report_supported
 * @hw: pointer to the hardware structure
 *
 * Return true if firmware supports health status reports,
 * false otherwise
 */
bool
ice_is_fw_health_report_supported(struct ice_hw *hw)
{
	if (hw->api_maj_ver > ICE_FW_API_HEALTH_REPORT_MAJ)
		return true;

	if (hw->api_maj_ver == ICE_FW_API_HEALTH_REPORT_MAJ) {
		if (hw->api_min_ver > ICE_FW_API_HEALTH_REPORT_MIN)
			return true;
		if (hw->api_min_ver == ICE_FW_API_HEALTH_REPORT_MIN &&
		    hw->api_patch >= ICE_FW_API_HEALTH_REPORT_PATCH)
			return true;
	}

	return false;
}

/**
 * ice_disable_unsupported_features - Disable features not enabled by OS
 * @bitmap: the feature bitmap
 *
 * Check for OS support of various driver features. Clear the feature bit for
 * any feature which is not enabled by the OS. This should be called early
 * during driver attach after setting up the feature bitmap.
 */
static inline void
ice_disable_unsupported_features(ice_bitmap_t *bitmap)
{
	/* Not supported by FreeBSD driver. */
	ice_clear_bit(ICE_FEATURE_SRIOV, bitmap);

	/* Not applicable to OpenBSD. */
	ice_clear_bit(ICE_FEATURE_NETMAP, bitmap);
	ice_clear_bit(ICE_FEATURE_RDMA, bitmap);

	/* Features not (yet?) supported by the OpenBSD driver. */
	ice_clear_bit(ICE_FEATURE_DCB, bitmap);
	ice_clear_bit(ICE_FEATURE_TEMP_SENSOR, bitmap);
	ice_clear_bit(ICE_FEATURE_TX_BALANCE, bitmap);
}

/**
 * ice_init_device_features - Init device driver features
 * @sc: driver softc structure
 *
 * @pre assumes that the function capabilities bits have been set up by
 * ice_init_hw().
 */
void
ice_init_device_features(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;

	/* Set capabilities that all devices support */
	ice_set_bit(ICE_FEATURE_SRIOV, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_RSS, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_RDMA, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_LENIENT_LINK_MODE, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_LINK_MGMT_VER_1, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_LINK_MGMT_VER_2, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_HEALTH_STATUS, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_FW_LOGGING, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_HAS_PBA, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_DCB, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_TX_BALANCE, sc->feat_cap);

	/* Disable features due to hardware limitations... */
	if (!hw->func_caps.common_cap.rss_table_size)
		ice_clear_bit(ICE_FEATURE_RSS, sc->feat_cap);
	if (!hw->func_caps.common_cap.iwarp /* || !ice_enable_irdma */)
		ice_clear_bit(ICE_FEATURE_RDMA, sc->feat_cap);
	if (!hw->func_caps.common_cap.dcb)
		ice_clear_bit(ICE_FEATURE_DCB, sc->feat_cap);
	/* Disable features due to firmware limitations... */
	if (!ice_is_fw_health_report_supported(hw))
		ice_clear_bit(ICE_FEATURE_HEALTH_STATUS, sc->feat_cap);
	if (!ice_fwlog_supported(hw))
		ice_clear_bit(ICE_FEATURE_FW_LOGGING, sc->feat_cap);
	if (hw->fwlog_cfg.options & ICE_FWLOG_OPTION_IS_REGISTERED) {
		if (ice_is_bit_set(sc->feat_cap, ICE_FEATURE_FW_LOGGING))
			ice_set_bit(ICE_FEATURE_FW_LOGGING, sc->feat_en);
		else
			ice_fwlog_unregister(hw);
	}
	/* Disable capabilities not supported by the OS */
	ice_disable_unsupported_features(sc->feat_cap);

	/* RSS is always enabled for iflib */
	if (ice_is_bit_set(sc->feat_cap, ICE_FEATURE_RSS))
		ice_set_bit(ICE_FEATURE_RSS, sc->feat_en);
#if 0
	/* Disable features based on sysctl settings */
	if (!ice_tx_balance_en)
		ice_clear_bit(ICE_FEATURE_TX_BALANCE, sc->feat_cap);
#endif

	if (hw->dev_caps.supported_sensors & ICE_SENSOR_SUPPORT_E810_INT_TEMP) {
		ice_set_bit(ICE_FEATURE_TEMP_SENSOR, sc->feat_cap);
		ice_set_bit(ICE_FEATURE_TEMP_SENSOR, sc->feat_en);
	}
}

/**
 * ice_aq_send_driver_ver
 * @hw: pointer to the HW struct
 * @dv: driver's major, minor version
 * @cd: pointer to command details structure or NULL
 *
 * Send the driver version (0x0002) to the firmware
 */
enum ice_status
ice_aq_send_driver_ver(struct ice_hw *hw, struct ice_driver_ver *dv,
		       struct ice_sq_cd *cd)
{
	struct ice_aqc_driver_ver *cmd;
	struct ice_aq_desc desc;
	uint16_t len;

	cmd = &desc.params.driver_ver;

	if (!dv)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_driver_ver);

	desc.flags |= htole16(ICE_AQ_FLAG_RD);
	cmd->major_ver = dv->major_ver;
	cmd->minor_ver = dv->minor_ver;
	cmd->build_ver = dv->build_ver;
	cmd->subbuild_ver = dv->subbuild_ver;

	len = strlen(dv->driver_string);

	return ice_aq_send_cmd(hw, &desc, dv->driver_string, len, cd);
}

/**
 * ice_send_version - Send driver version to firmware
 * @sc: the device private softc
 *
 * Send the driver version to the firmware. This must be called as early as
 * possible after ice_init_hw().
 */
int
ice_send_version(struct ice_softc *sc)
{
	struct ice_driver_ver driver_version = {0};
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	driver_version.major_ver = ice_major_version;
	driver_version.minor_ver = ice_minor_version;
	driver_version.build_ver = ice_patch_version;
	driver_version.subbuild_ver = ice_rc_version;

	strlcpy((char *)driver_version.driver_string, ice_driver_version,
		sizeof(driver_version.driver_string));

	status = ice_aq_send_driver_ver(hw, &driver_version, NULL);
	if (status) {
		printf("%s: Unable to send driver version to firmware, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		return (EIO);
	}

	return (0);
}

int
ice_reinit_hw(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	enum ice_fw_modes fw_mode;
	enum ice_status status;
	int err;

	fw_mode = ice_get_fw_mode(hw);
	if (fw_mode == ICE_FW_MODE_REC) {
		printf("%s: firmware is in recovery mode\n",
		    sc->sc_dev.dv_xname);
#if 0
		err = ice_attach_pre_recovery_mode(sc);
		if (err)
			goto free_pci_mapping;
#endif
		return ENODEV;
	}

	/* Initialize the hw data structure */
	status = ice_init_hw(hw);
	if (status) {
		if (status == ICE_ERR_FW_API_VER) {
			printf("%s: incompatible firmware API version\n",
			    sc->sc_dev.dv_xname);
#if 0
			/* Enter recovery mode, so that the driver remains
			 * loaded. This way, if the system administrator
			 * cannot update the driver, they may still attempt to
			 * downgrade the NVM.
			 */
			err = ice_attach_pre_recovery_mode(sc);
			if (err)
				goto free_pci_mapping;
#endif
			err = ENOTSUP;
			goto deinit_hw;
		} else {
			printf("%s: could not initialize hardware, "
			    "status %s aq_err %s\n",
			    sc->sc_dev.dv_xname, ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			err = EIO;
			goto deinit_hw;
		}
	}

	ice_init_device_features(sc);

	/* Keep flag set by default */
	ice_set_state(&sc->state, ICE_STATE_LINK_ACTIVE_ON_DOWN);

	/* Notify firmware of the device driver version */
	err = ice_send_version(sc);
deinit_hw:
	if (err)
		ice_deinit_hw(hw);
	return err;	
}

/**
 * ice_get_set_tx_topo - get or set tx topology
 * @hw: pointer to the HW struct
 * @buf: pointer to tx topology buffer
 * @buf_size: buffer size
 * @cd: pointer to command details structure or NULL
 * @flags: pointer to descriptor flags
 * @set: 0-get, 1-set topology
 *
 * The function will get or set tx topology
 */
enum ice_status
ice_get_set_tx_topo(struct ice_hw *hw, uint8_t *buf, uint16_t buf_size,
		    struct ice_sq_cd *cd, uint8_t *flags, bool set)
{
	struct ice_aqc_get_set_tx_topo *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.get_set_tx_topo;
	if (set) {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_tx_topo);
		cmd->set_flags = ICE_AQC_TX_TOPO_FLAGS_ISSUED;
		/* requested to update a new topology, not a default topology */
		if (buf)
			cmd->set_flags |= ICE_AQC_TX_TOPO_FLAGS_SRC_RAM |
					  ICE_AQC_TX_TOPO_FLAGS_LOAD_NEW;
	} else {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_tx_topo);
		cmd->get_flags = ICE_AQC_TX_TOPO_GET_RAM;
	}
	desc.flags |= htole16(ICE_AQ_FLAG_RD);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (status)
		return status;
	/* read the return flag values (first byte) for get operation */
	if (!set && flags)
		*flags = desc.params.get_set_tx_topo.set_flags;

	return ICE_SUCCESS;
}

/**
 * ice_verify_pkg - verify package
 * @pkg: pointer to the package buffer
 * @len: size of the package buffer
 *
 * Verifies various attributes of the package file, including length, format
 * version, and the requirement of at least one segment.
 */
enum ice_ddp_state
ice_verify_pkg(struct ice_pkg_hdr *pkg, uint32_t len)
{
	uint32_t seg_count;
	uint32_t i;

	if (len < ice_struct_size(pkg, seg_offset, 1))
		return ICE_DDP_PKG_INVALID_FILE;

	if (pkg->pkg_format_ver.major != ICE_PKG_FMT_VER_MAJ ||
	    pkg->pkg_format_ver.minor != ICE_PKG_FMT_VER_MNR ||
	    pkg->pkg_format_ver.update != ICE_PKG_FMT_VER_UPD ||
	    pkg->pkg_format_ver.draft != ICE_PKG_FMT_VER_DFT)
		return ICE_DDP_PKG_INVALID_FILE;

	/* pkg must have at least one segment */
	seg_count = le32toh(pkg->seg_count);
	if (seg_count < 1)
		return ICE_DDP_PKG_INVALID_FILE;

	/* make sure segment array fits in package length */
	if (len < ice_struct_size(pkg, seg_offset, seg_count))
		return ICE_DDP_PKG_INVALID_FILE;

	/* all segments must fit within length */
	for (i = 0; i < seg_count; i++) {
		uint32_t off = le32toh(pkg->seg_offset[i]);
		struct ice_generic_seg_hdr *seg;

		/* segment header must fit */
		if (len < off + sizeof(*seg))
			return ICE_DDP_PKG_INVALID_FILE;

		seg = (struct ice_generic_seg_hdr *)((uint8_t *)pkg + off);

		/* segment body must fit */
		if (len < off + le32toh(seg->seg_size))
			return ICE_DDP_PKG_INVALID_FILE;
	}

	return ICE_DDP_PKG_SUCCESS;
}

/**
 * ice_find_seg_in_pkg
 * @hw: pointer to the hardware structure
 * @seg_type: the segment type to search for (i.e., SEGMENT_TYPE_CPK)
 * @pkg_hdr: pointer to the package header to be searched
 *
 * This function searches a package file for a particular segment type. On
 * success it returns a pointer to the segment header, otherwise it will
 * return NULL.
 */
struct ice_generic_seg_hdr *
ice_find_seg_in_pkg(struct ice_hw *hw, uint32_t seg_type,
		    struct ice_pkg_hdr *pkg_hdr)
{
	uint32_t i;

	DNPRINTF(ICE_DBG_PKG, "Package format version: %d.%d.%d.%d\n",
		  pkg_hdr->pkg_format_ver.major, pkg_hdr->pkg_format_ver.minor,
		  pkg_hdr->pkg_format_ver.update,
		  pkg_hdr->pkg_format_ver.draft);

	/* Search all package segments for the requested segment type */
	for (i = 0; i < le32toh(pkg_hdr->seg_count); i++) {
		struct ice_generic_seg_hdr *seg;

		seg = (struct ice_generic_seg_hdr *)
			((uint8_t *)pkg_hdr + le32toh(pkg_hdr->seg_offset[i]));

		if (le32toh(seg->seg_type) == seg_type)
			return seg;
	}

	return NULL;
}

/**
 * ice_pkg_val_buf
 * @buf: pointer to the ice buffer
 *
 * This helper function validates a buffer's header.
 */
struct ice_buf_hdr *
ice_pkg_val_buf(struct ice_buf *buf)
{
	struct ice_buf_hdr *hdr;
	uint16_t section_count;
	uint16_t data_end;

	hdr = (struct ice_buf_hdr *)buf->buf;
	/* verify data */
	section_count = le16toh(hdr->section_count);
	if (section_count < ICE_MIN_S_COUNT || section_count > ICE_MAX_S_COUNT)
		return NULL;

	data_end = le16toh(hdr->data_end);
	if (data_end < ICE_MIN_S_DATA_END || data_end > ICE_MAX_S_DATA_END)
		return NULL;

	return hdr;
}

/**
 * ice_cfg_tx_topo - Initialize new tx topology if available
 * @hw: pointer to the HW struct
 * @buf: pointer to Tx topology buffer
 * @len: buffer size
 *
 * The function will apply the new Tx topology from the package buffer
 * if available.
 */
enum ice_status
ice_cfg_tx_topo(struct ice_softc *sc, uint8_t *buf, uint32_t len)
{
	struct ice_hw *hw = &sc->hw;
	uint8_t *current_topo, *new_topo = NULL;
	struct ice_run_time_cfg_seg *seg;
	struct ice_buf_hdr *section;
	struct ice_pkg_hdr *pkg_hdr;
	enum ice_ddp_state state;
	uint16_t i, size = 0, offset;
	enum ice_status status;
	uint32_t reg = 0;
	uint8_t flags;

	if (!buf || !len)
		return ICE_ERR_PARAM;

	/* Does FW support new Tx topology mode ? */
	if (!hw->func_caps.common_cap.tx_sched_topo_comp_mode_en) {
		DNPRINTF(ICE_DBG_INIT,
		    "FW doesn't support compatibility mode\n");
		return ICE_ERR_NOT_SUPPORTED;
	}

	current_topo = (uint8_t *)ice_malloc(hw, ICE_AQ_MAX_BUF_LEN);
	if (!current_topo)
		return ICE_ERR_NO_MEMORY;

	/* get the current Tx topology */
	status = ice_get_set_tx_topo(hw, current_topo, ICE_AQ_MAX_BUF_LEN, NULL,
				     &flags, false);
	ice_free(hw, current_topo);

	if (status) {
		DNPRINTF(ICE_DBG_INIT, "Get current topology is failed\n");
		return status;
	}

	/* Is default topology already applied ? */
	if (!(flags & ICE_AQC_TX_TOPO_FLAGS_LOAD_NEW) &&
	    hw->num_tx_sched_layers == 9) {
		DNPRINTF(ICE_DBG_INIT, "Loaded default topology\n");
		/* Already default topology is loaded */
		return ICE_ERR_ALREADY_EXISTS;
	}

	/* Is new topology already applied ? */
	if ((flags & ICE_AQC_TX_TOPO_FLAGS_LOAD_NEW) &&
	    hw->num_tx_sched_layers == 5) {
		DNPRINTF(ICE_DBG_INIT, "Loaded new topology\n");
		/* Already new topology is loaded */
		return ICE_ERR_ALREADY_EXISTS;
	}

	/* Is set topology issued already ? */
	if (flags & ICE_AQC_TX_TOPO_FLAGS_ISSUED) {
		DNPRINTF(ICE_DBG_INIT,
		    "Update tx topology was done by another PF\n");
		/* add a small delay before exiting */
		for (i = 0; i < 20; i++)
			ice_msec_delay(100, true);
		return ICE_ERR_ALREADY_EXISTS;
	}

	/* Change the topology from new to default (5 to 9) */
	if (!(flags & ICE_AQC_TX_TOPO_FLAGS_LOAD_NEW) &&
	    hw->num_tx_sched_layers == 5) {
		DNPRINTF(ICE_DBG_INIT, "Change topology from 5 to 9 layers\n");
		goto update_topo;
	}

	pkg_hdr = (struct ice_pkg_hdr *)buf;
	state = ice_verify_pkg(pkg_hdr, len);
	if (state) {
		printf("%s: failed to verify firmware pkg (err: %d)\n",
		    sc->sc_dev.dv_xname, state);
		return ICE_ERR_CFG;
	}

	/* find run time configuration segment */
	seg = (struct ice_run_time_cfg_seg *)
		ice_find_seg_in_pkg(hw, SEGMENT_TYPE_ICE_RUN_TIME_CFG, pkg_hdr);
	if (!seg) {
		DNPRINTF(ICE_DBG_INIT, "5 layer topology segment is missing\n");
		return ICE_ERR_CFG;
	}

	if (le32toh(seg->buf_table.buf_count) < ICE_MIN_S_COUNT) {
		DNPRINTF(ICE_DBG_INIT,
		    "5 layer topology segment count(%d) is wrong\n",
		    seg->buf_table.buf_count);
		return ICE_ERR_CFG;
	}

	section = ice_pkg_val_buf(seg->buf_table.buf_array);

	if (!section || le32toh(section->section_entry[0].type) !=
		ICE_SID_TX_5_LAYER_TOPO) {
		DNPRINTF(ICE_DBG_INIT,
		    "5 layer topology section type is wrong\n");
		return ICE_ERR_CFG;
	}

	size = le16toh(section->section_entry[0].size);
	offset = le16toh(section->section_entry[0].offset);
	if (size < ICE_MIN_S_SZ || size > ICE_MAX_S_SZ) {
		DNPRINTF(ICE_DBG_INIT,
		    "5 layer topology section size is wrong\n");
		return ICE_ERR_CFG;
	}

	/* make sure the section fits in the buffer */
	if (offset + size > ICE_PKG_BUF_SIZE) {
		DNPRINTF(ICE_DBG_INIT, "5 layer topology buffer > 4K\n");
		return ICE_ERR_CFG;
	}

	/* Get the new topology buffer */
	new_topo = ((uint8_t *)section) + offset;

update_topo:
	/* acquire global lock to make sure that set topology issued
	 * by one PF
	 */
	status = ice_acquire_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID, ICE_RES_WRITE,
				 ICE_GLOBAL_CFG_LOCK_TIMEOUT);
	if (status) {
		DNPRINTF(ICE_DBG_INIT, "Failed to acquire global lock\n");
		return status;
	}

	/* check reset was triggered already or not */
	reg = ICE_READ(hw, GLGEN_RSTAT);
	if (reg & GLGEN_RSTAT_DEVSTATE_M) {
		/* Reset is in progress, re-init the hw again */
		DNPRINTF(ICE_DBG_INIT, "Reset is in progress. layer "
		    "topology might be applied already\n");
		ice_check_reset(hw);
		return ICE_SUCCESS;
	}

	/* set new topology */
	status = ice_get_set_tx_topo(hw, new_topo, size, NULL, NULL, true);
	if (status) {
		DNPRINTF(ICE_DBG_INIT, "Set tx topology is failed\n");
		return status;
	}

	/* new topology is updated, delay 1 second before issuing the CORRER */
	for (i = 0; i < 10; i++)
		ice_msec_delay(100, true);
	ice_reset(hw, ICE_RESET_CORER);
	/* CORER will clear the global lock, so no explicit call
	 * required for release
	 */
	return ICE_SUCCESS;
}

/**
 * pkg_ver_empty - Check if a package version is empty
 * @pkg_ver: the package version to check
 * @pkg_name: the package name to check
 *
 * Checks if the package version structure is empty. We consider a package
 * version as empty if none of the versions are non-zero and the name string
 * is null as well.
 *
 * This is used to check if the package version was initialized by the driver,
 * as we do not expect an actual DDP package file to have a zero'd version and
 * name.
 *
 * @returns true if the package version is valid, or false otherwise.
 */
bool
pkg_ver_empty(struct ice_pkg_ver *pkg_ver, uint8_t *pkg_name)
{
	return (pkg_name[0] == '\0' &&
		pkg_ver->major == 0 &&
		pkg_ver->minor == 0 &&
		pkg_ver->update == 0 &&
		pkg_ver->draft == 0);
}

/**
 * ice_active_pkg_version_str - Format active package version info into a buffer
 * @hw: device hw structure
 * @buf: string buffer to store name/version string
 *
 * Formats the name and version of the active DDP package info into a string
 * buffer for use.
 */
void
ice_active_pkg_version_str(struct ice_hw *hw, char *buf, size_t bufsize)
{
	char name_buf[ICE_PKG_NAME_SIZE];

	/* If the active DDP package info is empty, use "None" */
	if (pkg_ver_empty(&hw->active_pkg_ver, hw->active_pkg_name)) {
		snprintf(buf, bufsize, "None");
		return;
	}

	/*
	 * This should already be null-terminated, but since this is a raw
	 * value from an external source, strlcpy() into a new buffer to
	 * make sure.
	 */
	strlcpy(name_buf, (char *)hw->active_pkg_name, bufsize);

	snprintf(buf, bufsize, "%s version %u.%u.%u.%u, track id 0x%08x",
	    name_buf,
	    hw->active_pkg_ver.major,
	    hw->active_pkg_ver.minor,
	    hw->active_pkg_ver.update,
	    hw->active_pkg_ver.draft,
	    hw->active_track_id);
}

/**
 * ice_os_pkg_version_str - Format OS package version info into a buffer
 * @hw: device hw structure
 * @buf: string buffer to store name/version string
 *
 * Formats the name and version of the OS DDP package as found in the ice_ddp
 * module into a string.
 *
 * @remark This will almost always be the same as the active package, but
 * could be different in some cases. Use ice_active_pkg_version_str to get the
 * version of the active DDP package.
 */
void
ice_os_pkg_version_str(struct ice_hw *hw, char *buf, size_t bufsize)
{
	/* If the OS DDP package info is empty, use "" */
	if (pkg_ver_empty(&hw->pkg_ver, hw->pkg_name)) {
		buf[0] = '\0';
		return;
	}

	/*
	 * This should already be null-terminated, but since this is a raw
	 * value from an external source, strlcpy() into a new buffer to
	 * make sure.
	 */
	snprintf(buf, bufsize, "%u.%u.%u.%u",
	    hw->pkg_ver.major,
	    hw->pkg_ver.minor,
	    hw->pkg_ver.update,
	    hw->pkg_ver.draft);
}

/**
 * ice_is_init_pkg_successful - check if DDP init was successful
 * @state: state of the DDP pkg after download
 */
bool
ice_is_init_pkg_successful(enum ice_ddp_state state)
{
	switch (state) {
	case ICE_DDP_PKG_SUCCESS:
	case ICE_DDP_PKG_SAME_VERSION_ALREADY_LOADED:
	case ICE_DDP_PKG_COMPATIBLE_ALREADY_LOADED:
		return true;
	default:
		return false;
	}
}

/**
 * ice_pkg_ver_compatible - Check if the package version is compatible
 * @pkg_ver: the package version to check
 *
 * Compares the package version number to the driver's expected major/minor
 * version. Returns an integer indicating whether the version is older, newer,
 * or compatible with the driver.
 *
 * @returns 0 if the package version is compatible, -1 if the package version
 * is older, and 1 if the package version is newer than the driver version.
 */
int
ice_pkg_ver_compatible(struct ice_pkg_ver *pkg_ver)
{
	if (pkg_ver->major > ICE_PKG_SUPP_VER_MAJ)
		return (1); /* newer */
	else if ((pkg_ver->major == ICE_PKG_SUPP_VER_MAJ) &&
		 (pkg_ver->minor > ICE_PKG_SUPP_VER_MNR))
		return (1); /* newer */
	else if ((pkg_ver->major == ICE_PKG_SUPP_VER_MAJ) &&
		 (pkg_ver->minor == ICE_PKG_SUPP_VER_MNR))
		return (0); /* compatible */
	else
		return (-1); /* older */
}

/**
 * ice_log_pkg_init - Log a message about status of DDP initialization
 * @sc: the device softc pointer
 * @pkg_status: the status result of ice_copy_and_init_pkg
 *
 * Called by ice_load_pkg after an attempt to download the DDP package
 * contents to the device to log an appropriate message for the system
 * administrator about download status.
 *
 * @post ice_is_init_pkg_successful function is used to determine
 * whether the download was successful and DDP package is compatible
 * with this driver. Otherwise driver will transition to Safe Mode.
 */
void
ice_log_pkg_init(struct ice_softc *sc, enum ice_ddp_state pkg_status)
{
	struct ice_hw *hw = &sc->hw;
	char active_pkg[ICE_PKG_NAME_SIZE];
	char os_pkg[ICE_PKG_NAME_SIZE];

	ice_active_pkg_version_str(hw, active_pkg, sizeof(active_pkg));
	ice_os_pkg_version_str(hw, os_pkg, sizeof(os_pkg));

	switch (pkg_status) {
	case ICE_DDP_PKG_SUCCESS:
		DPRINTF("%s: The DDP package was successfully loaded: %s.\n",
		    __func__, active_pkg);
		break;
	case ICE_DDP_PKG_SAME_VERSION_ALREADY_LOADED:
	case ICE_DDP_PKG_ALREADY_LOADED:
		DPRINTF("%s: DDP package already present on device: %s.\n",
		    __func__, active_pkg);
		break;
	case ICE_DDP_PKG_COMPATIBLE_ALREADY_LOADED:
		DPRINTF("%s: The driver could not load the DDP package file "
		    "because a compatible DDP package is already present on "
		    "the device.  The device has package %s.  The ice-ddp "
		    "file has package: %s.\n", __func__, active_pkg, os_pkg);
		break;
	case ICE_DDP_PKG_FILE_VERSION_TOO_HIGH:
		printf("%s: The device has a DDP package that is higher than "
		    "the driver supports.  The device has package %s. The "
		    "driver requires version %d.%d.x.x.  Entering Safe Mode.\n",
		    sc->sc_dev.dv_xname, active_pkg, ICE_PKG_SUPP_VER_MAJ,
		    ICE_PKG_SUPP_VER_MNR);
		break;
	case ICE_DDP_PKG_FILE_VERSION_TOO_LOW:
		printf("%s: The device has a DDP package that is lower than "
		    "the driver supports.  The device has package %s.  The "
		    "driver requires version %d.%d.x.x.  Entering Safe Mode.\n",
		    sc->sc_dev.dv_xname, active_pkg, ICE_PKG_SUPP_VER_MAJ,
		    ICE_PKG_SUPP_VER_MNR);
		break;
	case ICE_DDP_PKG_ALREADY_LOADED_NOT_SUPPORTED:
		/*
		 * This assumes that the active_pkg_ver will not be
		 * initialized if the ice_ddp package version is not
		 * supported.
		 */
		if (pkg_ver_empty(&hw->active_pkg_ver, hw->active_pkg_name)) {
			/* The ice_ddp version is not supported */
			if (ice_pkg_ver_compatible(&hw->pkg_ver) > 0) {
				DPRINTF("%s: The DDP package in the ice-ddp file "
				    "is higher than the driver supports.  The "
				    "ice-ddp file has package %s. The driver "
				    "requires version %d.%d.x.x.  Please use "
				    "an updated driver.  Entering Safe Mode.\n",
				    __func__, os_pkg, ICE_PKG_SUPP_VER_MAJ,
				    ICE_PKG_SUPP_VER_MNR);
			} else if (ice_pkg_ver_compatible(&hw->pkg_ver) < 0) {
				DPRINTF("%s: The DDP package in the "
				    "ice-ddp file is lower than the driver "
				    "supports. The ice_ddp module has package "
				    "%s.  The driver requires version "
				    "%d.%d.x.x. Please use an updated "
				    "ice-ddp file. Entering Safe Mode.\n",
				    __func__, os_pkg, ICE_PKG_SUPP_VER_MAJ,
				    ICE_PKG_SUPP_VER_MNR);
			} else {
				printf("%s: An unknown error occurred when "
				    "loading the DDP package.  The ice-ddp "
				    "file has package %s. The device has "
				    "package %s.  The driver requires version "
				    "%d.%d.x.x.  Entering Safe Mode.\n",
				    sc->sc_dev.dv_xname, os_pkg, active_pkg,
				    ICE_PKG_SUPP_VER_MAJ, ICE_PKG_SUPP_VER_MNR);
			}
		} else {
			if (ice_pkg_ver_compatible(&hw->active_pkg_ver) > 0) {
				DPRINTF("%s: The device has a DDP package "
				    "that is higher than the driver supports. " 
				    "The device has package %s.  The driver "
				    "requires version %d.%d.x.x.  Entering "
				    "Safe Mode.\n", __func__, active_pkg,
				    ICE_PKG_SUPP_VER_MAJ, ICE_PKG_SUPP_VER_MNR);
			} else if (ice_pkg_ver_compatible(&hw->active_pkg_ver)
			    < 0) {
				DPRINTF("%s: The device has a DDP package that "
				    "is lower than the driver supports.  The "
				    "device has package %s. The driver "
				    "requires version %d.%d.x.x.  "
				    "Entering Safe Mode.\n", __func__,
				    active_pkg, ICE_PKG_SUPP_VER_MAJ,
				    ICE_PKG_SUPP_VER_MNR);
			} else {
				printf("%s: An unknown error occurred when "
				    "loading the DDP package.  The ice-ddp "
				    "file has package %s.  The device has "
				    "package %s.  The driver requires "
				    "version %d.%d.x.x.  Entering Safe Mode.\n",
				    sc->sc_dev.dv_xname, os_pkg, active_pkg,
				    ICE_PKG_SUPP_VER_MAJ, ICE_PKG_SUPP_VER_MNR);
			}
		}
		break;
	case ICE_DDP_PKG_INVALID_FILE:
		printf("%s: The DDP package in the ice-ddp file is invalid. "
		    "Entering Safe Mode\n", sc->sc_dev.dv_xname);
		break;
	case ICE_DDP_PKG_FW_MISMATCH:
		printf("%s: The firmware loaded on the device is not "
		    "compatible with the DDP package. "
		    "Please update the device's NVM.  Entering safe mode.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_DDP_PKG_NO_SEC_MANIFEST:
	case ICE_DDP_PKG_FILE_SIGNATURE_INVALID:
		printf("%s: The DDP package in the ice-ddp file cannot be "
		    "loaded because its signature is not valid.  Please "
		    "use a valid ice-ddp file.  Entering Safe Mode.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_DDP_PKG_SECURE_VERSION_NBR_TOO_LOW:
		printf("%s: The DDP package in the ice-ddp file could not "
		    "be loaded because its security revision is too low. "
		    "Please use an updated ice-ddp file.  "
		    "Entering Safe Mode.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_DDP_PKG_MANIFEST_INVALID:
	case ICE_DDP_PKG_BUFFER_INVALID:
		printf("%s: An error occurred on the device while loading "
		    "the DDP package.  Entering Safe Mode.\n",
		    sc->sc_dev.dv_xname);
		break;
	default:
		printf("%s: An unknown error occurred when loading the "
		    "DDP package.  Entering Safe Mode.\n",
		    sc->sc_dev.dv_xname);
		break;
	}
}

/**
 * ice_has_signing_seg - determine if package has a signing segment
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to the driver's package hdr
 */
bool
ice_has_signing_seg(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr)
{
	struct ice_generic_seg_hdr *seg_hdr;

	seg_hdr = (struct ice_generic_seg_hdr *)
		ice_find_seg_in_pkg(hw, SEGMENT_TYPE_SIGNING, pkg_hdr);

	return seg_hdr ? true : false;
}

/**
 * ice_get_pkg_segment_id - get correct package segment id, based on device
 * @mac_type: MAC type of the device
 */
uint32_t
ice_get_pkg_segment_id(enum ice_mac_type mac_type)
{
	uint32_t seg_id;

	switch (mac_type) {
	case ICE_MAC_GENERIC:
	case ICE_MAC_GENERIC_3K:
	case ICE_MAC_GENERIC_3K_E825:
	default:
		seg_id = SEGMENT_TYPE_ICE_E810;
		break;
	}

	return seg_id;
}

/**
 * ice_get_pkg_sign_type - get package segment sign type, based on device
 * @mac_type: MAC type of the device
 */
uint32_t
ice_get_pkg_sign_type(enum ice_mac_type mac_type)
{
	uint32_t sign_type;

	switch (mac_type) {
	case ICE_MAC_GENERIC_3K:
		sign_type = SEGMENT_SIGN_TYPE_RSA3K;
		break;
	case ICE_MAC_GENERIC_3K_E825:
		sign_type = SEGMENT_SIGN_TYPE_RSA3K_E825;
		break;
	case ICE_MAC_GENERIC:
	default:
		sign_type = SEGMENT_SIGN_TYPE_RSA2K;
		break;
	}

	return sign_type;
}

/**
 * ice_get_signing_req - get correct package requirements, based on device
 * @hw: pointer to the hardware structure
 */
void
ice_get_signing_req(struct ice_hw *hw)
{
	hw->pkg_seg_id = ice_get_pkg_segment_id(hw->mac_type);
	hw->pkg_sign_type = ice_get_pkg_sign_type(hw->mac_type);
}

/**
 * ice_get_pkg_seg_by_idx
 * @pkg_hdr: pointer to the package header to be searched
 * @idx: index of segment
 */
struct ice_generic_seg_hdr *
ice_get_pkg_seg_by_idx(struct ice_pkg_hdr *pkg_hdr, uint32_t idx)
{
	struct ice_generic_seg_hdr *seg = NULL;

	if (idx < le32toh(pkg_hdr->seg_count))
		seg = (struct ice_generic_seg_hdr *)
			((uint8_t *)pkg_hdr +
			 le32toh(pkg_hdr->seg_offset[idx]));

	return seg;
}
/**
 * ice_is_signing_seg_at_idx - determine if segment is a signing segment
 * @pkg_hdr: pointer to package header
 * @idx: segment index
 */
bool
ice_is_signing_seg_at_idx(struct ice_pkg_hdr *pkg_hdr, uint32_t idx)
{
	struct ice_generic_seg_hdr *seg;
	bool retval = false;

	seg = ice_get_pkg_seg_by_idx(pkg_hdr, idx);
	if (seg)
		retval = le32toh(seg->seg_type) == SEGMENT_TYPE_SIGNING;

	return retval;
}

/**
 * ice_is_signing_seg_type_at_idx
 * @pkg_hdr: pointer to package header
 * @idx: segment index
 * @seg_id: segment id that is expected
 * @sign_type: signing type
 *
 * Determine if a segment is a signing segment of the correct type
 */
bool
ice_is_signing_seg_type_at_idx(struct ice_pkg_hdr *pkg_hdr, uint32_t idx,
    uint32_t seg_id, uint32_t sign_type)
{
	bool result = false;

	if (ice_is_signing_seg_at_idx(pkg_hdr, idx)) {
		struct ice_sign_seg *seg;

		seg = (struct ice_sign_seg *)ice_get_pkg_seg_by_idx(pkg_hdr,
								    idx);
		if (seg && le32toh(seg->seg_id) == seg_id &&
		    le32toh(seg->sign_type) == sign_type)
			result = true;
	}

	return result;
}

/**
 * ice_match_signing_seg - determine if a matching signing segment exists
 * @pkg_hdr: pointer to package header
 * @seg_id: segment id that is expected
 * @sign_type: signing type
 */
bool
ice_match_signing_seg(struct ice_pkg_hdr *pkg_hdr, uint32_t seg_id,
    uint32_t sign_type)
{
	bool match = false;
	uint32_t i;

	for (i = 0; i < le32toh(pkg_hdr->seg_count); i++) {
		if (ice_is_signing_seg_type_at_idx(pkg_hdr, i, seg_id,
						   sign_type)) {
			match = true;
			break;
		}
	}

	return match;
}

/**
 * ice_find_buf_table
 * @ice_seg: pointer to the ice segment
 *
 * Returns the address of the buffer table within the ice segment.
 */
struct ice_buf_table *
ice_find_buf_table(struct ice_seg *ice_seg)
{
	struct ice_nvm_table *nvms;

	nvms = (struct ice_nvm_table *)
		(ice_seg->device_table +
		 le32toh(ice_seg->device_table_count));

	return (struct ice_buf_table *)
		(nvms->vers + le32toh(nvms->table_count));
}

/**
 * ice_pkg_enum_buf
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 *
 * This function will enumerate all the buffers in the ice segment. The first
 * call is made with the ice_seg parameter non-NULL; on subsequent calls,
 * ice_seg is set to NULL which continues the enumeration. When the function
 * returns a NULL pointer, then the end of the buffers has been reached, or an
 * unexpected value has been detected (for example an invalid section count or
 * an invalid buffer end value).
 */
struct ice_buf_hdr *
ice_pkg_enum_buf(struct ice_seg *ice_seg, struct ice_pkg_enum *state)
{
	if (ice_seg) {
		state->buf_table = ice_find_buf_table(ice_seg);
		if (!state->buf_table)
			return NULL;

		state->buf_idx = 0;
		return ice_pkg_val_buf(state->buf_table->buf_array);
	}

	if (++state->buf_idx < le32toh(state->buf_table->buf_count))
		return ice_pkg_val_buf(state->buf_table->buf_array +
				       state->buf_idx);
	else
		return NULL;
}

/**
 * ice_pkg_advance_sect
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 *
 * This helper function will advance the section within the ice segment,
 * also advancing the buffer if needed.
 */
bool
ice_pkg_advance_sect(struct ice_seg *ice_seg, struct ice_pkg_enum *state)
{
	if (!ice_seg && !state->buf)
		return false;

	if (!ice_seg && state->buf)
		if (++state->sect_idx < le16toh(state->buf->section_count))
			return true;

	state->buf = ice_pkg_enum_buf(ice_seg, state);
	if (!state->buf)
		return false;

	/* start of new buffer, reset section index */
	state->sect_idx = 0;
	return true;
}

/**
 * ice_pkg_enum_section
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 * @sect_type: section type to enumerate
 *
 * This function will enumerate all the sections of a particular type in the
 * ice segment. The first call is made with the ice_seg parameter non-NULL;
 * on subsequent calls, ice_seg is set to NULL which continues the enumeration.
 * When the function returns a NULL pointer, then the end of the matching
 * sections has been reached.
 */
void *
ice_pkg_enum_section(struct ice_seg *ice_seg, struct ice_pkg_enum *state,
		     uint32_t sect_type)
{
	uint16_t offset, size;

	if (ice_seg)
		state->type = sect_type;

	if (!ice_pkg_advance_sect(ice_seg, state))
		return NULL;

	/* scan for next matching section */
	while (state->buf->section_entry[state->sect_idx].type !=
	       htole32(state->type))
		if (!ice_pkg_advance_sect(NULL, state))
			return NULL;

	/* validate section */
	offset = le16toh(state->buf->section_entry[state->sect_idx].offset);
	if (offset < ICE_MIN_S_OFF || offset > ICE_MAX_S_OFF)
		return NULL;

	size = le16toh(state->buf->section_entry[state->sect_idx].size);
	if (size < ICE_MIN_S_SZ || size > ICE_MAX_S_SZ)
		return NULL;

	/* make sure the section fits in the buffer */
	if (offset + size > ICE_PKG_BUF_SIZE)
		return NULL;

	state->sect_type =
		le32toh(state->buf->section_entry[state->sect_idx].type);

	/* calc pointer to this section */
	state->sect = ((uint8_t *)state->buf) +
		le16toh(state->buf->section_entry[state->sect_idx].offset);

	return state->sect;
}

/**
 * ice_init_pkg_info
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to the driver's package hdr
 *
 * Saves off the package details into the HW structure.
 */
enum ice_ddp_state
ice_init_pkg_info(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr)
{
	struct ice_generic_seg_hdr *seg_hdr;

	if (!pkg_hdr)
		return ICE_DDP_PKG_ERR;

	hw->pkg_has_signing_seg = ice_has_signing_seg(hw, pkg_hdr);
	ice_get_signing_req(hw);

	DNPRINTF(ICE_DBG_INIT, "Pkg using segment id: 0x%08X\n",
	    hw->pkg_seg_id);

	seg_hdr = (struct ice_generic_seg_hdr *)
		ice_find_seg_in_pkg(hw, hw->pkg_seg_id, pkg_hdr);
	if (seg_hdr) {
		struct ice_meta_sect *meta;
		struct ice_pkg_enum state;

		memset(&state, 0, sizeof(state));

		/* Get package information from the Metadata Section */
		meta = (struct ice_meta_sect *)
			ice_pkg_enum_section((struct ice_seg *)seg_hdr, &state,
					     ICE_SID_METADATA);
		if (!meta) {
			DNPRINTF(ICE_DBG_INIT,
			    "Did not find ice metadata section in package\n");
			return ICE_DDP_PKG_INVALID_FILE;
		}

		hw->pkg_ver = meta->ver;
		memcpy(hw->pkg_name, meta->name, sizeof(meta->name));

		DNPRINTF(ICE_DBG_PKG, "Pkg: %d.%d.%d.%d, %s\n",
			  meta->ver.major, meta->ver.minor, meta->ver.update,
			  meta->ver.draft, meta->name);

		hw->ice_seg_fmt_ver = seg_hdr->seg_format_ver;
		memcpy(hw->ice_seg_id, seg_hdr->seg_id, sizeof(hw->ice_seg_id));

		DNPRINTF(ICE_DBG_PKG, "Ice Seg: %d.%d.%d.%d, %s\n",
			  seg_hdr->seg_format_ver.major,
			  seg_hdr->seg_format_ver.minor,
			  seg_hdr->seg_format_ver.update,
			  seg_hdr->seg_format_ver.draft,
			  seg_hdr->seg_id);
	} else {
		DNPRINTF(ICE_DBG_INIT,
		    "Did not find ice segment in driver package\n");
		return ICE_DDP_PKG_INVALID_FILE;
	}

	return ICE_DDP_PKG_SUCCESS;
}

/**
 * ice_chk_pkg_version - check package version for compatibility with driver
 * @pkg_ver: pointer to a version structure to check
 *
 * Check to make sure that the package about to be downloaded is compatible with
 * the driver. To be compatible, the major and minor components of the package
 * version must match our ICE_PKG_SUPP_VER_MAJ and ICE_PKG_SUPP_VER_MNR
 * definitions.
 */
enum ice_ddp_state
ice_chk_pkg_version(struct ice_pkg_ver *pkg_ver)
{
	if (pkg_ver->major > ICE_PKG_SUPP_VER_MAJ ||
	    (pkg_ver->major == ICE_PKG_SUPP_VER_MAJ &&
	     pkg_ver->minor > ICE_PKG_SUPP_VER_MNR))
		return ICE_DDP_PKG_FILE_VERSION_TOO_HIGH;
	else if (pkg_ver->major < ICE_PKG_SUPP_VER_MAJ ||
		 (pkg_ver->major == ICE_PKG_SUPP_VER_MAJ &&
		  pkg_ver->minor < ICE_PKG_SUPP_VER_MNR))
		return ICE_DDP_PKG_FILE_VERSION_TOO_LOW;

	return ICE_DDP_PKG_SUCCESS;
}

/**
 * ice_aq_get_pkg_info_list
 * @hw: pointer to the hardware structure
 * @pkg_info: the buffer which will receive the information list
 * @buf_size: the size of the pkg_info information buffer
 * @cd: pointer to command details structure or NULL
 *
 * Get Package Info List (0x0C43)
 */
enum ice_status
ice_aq_get_pkg_info_list(struct ice_hw *hw,
			 struct ice_aqc_get_pkg_info_resp *pkg_info,
			 uint16_t buf_size, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_pkg_info_list);

	return ice_aq_send_cmd(hw, &desc, pkg_info, buf_size, cd);
}

/**
 * ice_chk_pkg_compat
 * @hw: pointer to the hardware structure
 * @ospkg: pointer to the package hdr
 * @seg: pointer to the package segment hdr
 *
 * This function checks the package version compatibility with driver and NVM
 */
enum ice_ddp_state
ice_chk_pkg_compat(struct ice_hw *hw, struct ice_pkg_hdr *ospkg,
		   struct ice_seg **seg)
{
	struct ice_aqc_get_pkg_info_resp *pkg;
	enum ice_ddp_state state;
	uint16_t size;
	uint32_t i;

	/* Check package version compatibility */
	state = ice_chk_pkg_version(&hw->pkg_ver);
	if (state) {
		DNPRINTF(ICE_DBG_INIT, "Package version check failed.\n");
		return state;
	}

	/* find ICE segment in given package */
	*seg = (struct ice_seg *)ice_find_seg_in_pkg(hw, hw->pkg_seg_id,
						     ospkg);
	if (!*seg) {
		DNPRINTF(ICE_DBG_INIT, "no ice segment in package.\n");
		return ICE_DDP_PKG_INVALID_FILE;
	}

	/* Check if FW is compatible with the OS package */
	size = ice_struct_size(pkg, pkg_info, ICE_PKG_CNT);
	pkg = (struct ice_aqc_get_pkg_info_resp *)ice_malloc(hw, size);
	if (!pkg)
		return ICE_DDP_PKG_ERR;

	if (ice_aq_get_pkg_info_list(hw, pkg, size, NULL)) {
		state = ICE_DDP_PKG_ERR;
		goto fw_ddp_compat_free_alloc;
	}

	for (i = 0; i < le32toh(pkg->count); i++) {
		/* loop till we find the NVM package */
		if (!pkg->pkg_info[i].is_in_nvm)
			continue;
		if ((*seg)->hdr.seg_format_ver.major !=
		    pkg->pkg_info[i].ver.major ||
		    (*seg)->hdr.seg_format_ver.minor >
		    pkg->pkg_info[i].ver.minor) {
			state = ICE_DDP_PKG_FW_MISMATCH;
			DNPRINTF(ICE_DBG_INIT,
			    "OS package is not compatible with NVM.\n");
		}
		/* done processing NVM package so break */
		break;
	}
fw_ddp_compat_free_alloc:
	ice_free(hw, pkg);
	return state;
}

/**
 * ice_pkg_enum_entry
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 * @sect_type: section type to enumerate
 * @offset: pointer to variable that receives the offset in the table (optional)
 * @handler: function that handles access to the entries into the section type
 *
 * This function will enumerate all the entries in particular section type in
 * the ice segment. The first call is made with the ice_seg parameter non-NULL;
 * on subsequent calls, ice_seg is set to NULL which continues the enumeration.
 * When the function returns a NULL pointer, then the end of the entries has
 * been reached.
 *
 * Since each section may have a different header and entry size, the handler
 * function is needed to determine the number and location entries in each
 * section.
 *
 * The offset parameter is optional, but should be used for sections that
 * contain an offset for each section table. For such cases, the section handler
 * function must return the appropriate offset + index to give the absolution
 * offset for each entry. For example, if the base for a section's header
 * indicates a base offset of 10, and the index for the entry is 2, then
 * section handler function should set the offset to 10 + 2 = 12.
 */
void *
ice_pkg_enum_entry(struct ice_seg *ice_seg, struct ice_pkg_enum *state,
    uint32_t sect_type, uint32_t *offset, void *(*handler)(uint32_t sect_type,
    void *section, uint32_t index, uint32_t *offset))
{
	void *entry;

	if (ice_seg) {
		if (!handler)
			return NULL;

		if (!ice_pkg_enum_section(ice_seg, state, sect_type))
			return NULL;

		state->entry_idx = 0;
		state->handler = handler;
	} else {
		state->entry_idx++;
	}

	if (!state->handler)
		return NULL;

	/* get entry */
	entry = state->handler(state->sect_type, state->sect, state->entry_idx,
			       offset);
	if (!entry) {
		/* end of a section, look for another section of this type */
		if (!ice_pkg_enum_section(NULL, state, 0))
			return NULL;

		state->entry_idx = 0;
		entry = state->handler(state->sect_type, state->sect,
				       state->entry_idx, offset);
	}

	return entry;
}

/**
 * ice_label_enum_handler
 * @sect_type: section type
 * @section: pointer to section
 * @index: index of the label entry to be returned
 * @offset: pointer to receive absolute offset, always zero for label sections
 *
 * This is a callback function that can be passed to ice_pkg_enum_entry.
 * Handles enumeration of individual label entries.
 */
void *
ice_label_enum_handler(uint32_t sect_type, void *section, uint32_t index,
		       uint32_t *offset)
{
	struct ice_label_section *labels;

	if (!section)
		return NULL;

	if (index > ICE_MAX_LABELS_IN_BUF)
		return NULL;

	if (offset)
		*offset = 0;

	labels = (struct ice_label_section *)section;
	if (index >= le16toh(labels->count))
		return NULL;

	return labels->label + index;
}

/**
 * ice_enum_labels
 * @ice_seg: pointer to the ice segment (NULL on subsequent calls)
 * @type: the section type that will contain the label (0 on subsequent calls)
 * @state: ice_pkg_enum structure that will hold the state of the enumeration
 * @value: pointer to a value that will return the label's value if found
 *
 * Enumerates a list of labels in the package. The caller will call
 * ice_enum_labels(ice_seg, type, ...) to start the enumeration, then call
 * ice_enum_labels(NULL, 0, ...) to continue. When the function returns a NULL
 * the end of the list has been reached.
 */
char *
ice_enum_labels(struct ice_seg *ice_seg, uint32_t type,
    struct ice_pkg_enum *state, uint16_t *value)
{
	struct ice_label *label;

	/* Check for valid label section on first call */
	if (type && !(type >= ICE_SID_LBL_FIRST && type <= ICE_SID_LBL_LAST))
		return NULL;

	label = (struct ice_label *)ice_pkg_enum_entry(ice_seg, state, type,
	    NULL, ice_label_enum_handler);
	if (!label)
		return NULL;

	*value = le16toh(label->value);
	return label->name;
}

/**
 * ice_boost_tcam_handler
 * @sect_type: section type
 * @section: pointer to section
 * @index: index of the boost TCAM entry to be returned
 * @offset: pointer to receive absolute offset, always 0 for boost TCAM sections
 *
 * This is a callback function that can be passed to ice_pkg_enum_entry.
 * Handles enumeration of individual boost TCAM entries.
 */
void *
ice_boost_tcam_handler(uint32_t sect_type, void *section, uint32_t index,
    uint32_t *offset)
{
	struct ice_boost_tcam_section *boost;

	if (!section)
		return NULL;

	if (sect_type != ICE_SID_RXPARSER_BOOST_TCAM)
		return NULL;

	if (index > ICE_MAX_BST_TCAMS_IN_BUF)
		return NULL;

	if (offset)
		*offset = 0;

	boost = (struct ice_boost_tcam_section *)section;
	if (index >= le16toh(boost->count))
		return NULL;

	return boost->tcam + index;
}

/**
 * ice_find_boost_entry
 * @ice_seg: pointer to the ice segment (non-NULL)
 * @addr: Boost TCAM address of entry to search for
 * @entry: returns pointer to the entry
 *
 * Finds a particular Boost TCAM entry and returns a pointer to that entry
 * if it is found. The ice_seg parameter must not be NULL since the first call
 * to ice_pkg_enum_entry requires a pointer to an actual ice_segment structure.
 */
enum ice_status
ice_find_boost_entry(struct ice_seg *ice_seg, uint16_t addr,
		     struct ice_boost_tcam_entry **entry)
{
	struct ice_boost_tcam_entry *tcam;
	struct ice_pkg_enum state;

	memset(&state, 0, sizeof(state));

	if (!ice_seg)
		return ICE_ERR_PARAM;

	do {
		tcam = (struct ice_boost_tcam_entry *)
		       ice_pkg_enum_entry(ice_seg, &state,
					  ICE_SID_RXPARSER_BOOST_TCAM, NULL,
					  ice_boost_tcam_handler);
		if (tcam && le16toh(tcam->addr) == addr) {
			*entry = tcam;
			return ICE_SUCCESS;
		}

		ice_seg = NULL;
	} while (tcam);

	*entry = NULL;
	return ICE_ERR_CFG;
}

static const struct ice_tunnel_type_scan tnls[] = {
	{ TNL_VXLAN,		"TNL_VXLAN_PF" },
	{ TNL_GENEVE,		"TNL_GENEVE_PF" },
	{ TNL_LAST,		"" }
};

/**
 * ice_add_tunnel_hint
 * @hw: pointer to the HW structure
 * @label_name: label text
 * @val: value of the tunnel port boost entry
 */
void
ice_add_tunnel_hint(struct ice_hw *hw, char *label_name, uint16_t val)
{
	if (hw->tnl.count < ICE_TUNNEL_MAX_ENTRIES) {
		uint16_t i;

		for (i = 0; tnls[i].type != TNL_LAST; i++) {
			size_t len = strlen(tnls[i].label_prefix);

			/* Look for matching label start, before continuing */
			if (strncmp(label_name, tnls[i].label_prefix, len))
				continue;

			/* Make sure this label matches our PF. Note that the
			 * PF character ('0' - '7') will be located where our
			 * prefix string's null terminator is located.
			 */
			if ((label_name[len] - '0') == hw->pf_id) {
				hw->tnl.tbl[hw->tnl.count].type = tnls[i].type;
				hw->tnl.tbl[hw->tnl.count].valid = false;
				hw->tnl.tbl[hw->tnl.count].in_use = false;
				hw->tnl.tbl[hw->tnl.count].marked = false;
				hw->tnl.tbl[hw->tnl.count].boost_addr = val;
				hw->tnl.tbl[hw->tnl.count].port = 0;
				hw->tnl.count++;
				break;
			}
		}
	}
}

/**
 * ice_init_pkg_hints
 * @hw: pointer to the HW structure
 * @ice_seg: pointer to the segment of the package scan (non-NULL)
 *
 * This function will scan the package and save off relevant information
 * (hints or metadata) for driver use. The ice_seg parameter must not be NULL
 * since the first call to ice_enum_labels requires a pointer to an actual
 * ice_seg structure.
 */
void
ice_init_pkg_hints(struct ice_hw *hw, struct ice_seg *ice_seg)
{
	struct ice_pkg_enum state;
	char *label_name;
	uint16_t val;
	int i;

	memset(&hw->tnl, 0, sizeof(hw->tnl));
	memset(&state, 0, sizeof(state));

	if (!ice_seg)
		return;

	label_name = ice_enum_labels(ice_seg, ICE_SID_LBL_RXPARSER_TMEM, &state,
				     &val);

	while (label_name) {
		if (!strncmp(label_name, ICE_TNL_PRE, strlen(ICE_TNL_PRE)))
			/* check for a tunnel entry */
			ice_add_tunnel_hint(hw, label_name, val);

		label_name = ice_enum_labels(NULL, 0, &state, &val);
	}

	/* Cache the appropriate boost TCAM entry pointers for tunnels */
	for (i = 0; i < hw->tnl.count; i++) {
		ice_find_boost_entry(ice_seg, hw->tnl.tbl[i].boost_addr,
		    &hw->tnl.tbl[i].boost_entry);
		if (hw->tnl.tbl[i].boost_entry)
			hw->tnl.tbl[i].valid = true;
	}
}

/**
 * ice_acquire_global_cfg_lock
 * @hw: pointer to the HW structure
 * @access: access type (read or write)
 *
 * This function will request ownership of the global config lock for reading
 * or writing of the package. When attempting to obtain write access, the
 * caller must check for the following two return values:
 *
 * ICE_SUCCESS        - Means the caller has acquired the global config lock
 *                      and can perform writing of the package.
 * ICE_ERR_AQ_NO_WORK - Indicates another driver has already written the
 *                      package or has found that no update was necessary; in
 *                      this case, the caller can just skip performing any
 *                      update of the package.
 */
enum ice_status
ice_acquire_global_cfg_lock(struct ice_hw *hw,
			    enum ice_aq_res_access_type access)
{
	enum ice_status status;

	status = ice_acquire_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID, access,
	    ICE_GLOBAL_CFG_LOCK_TIMEOUT);

	if (status == ICE_ERR_AQ_NO_WORK)
		DNPRINTF(ICE_DBG_PKG, "Global config lock: No work to do\n");

	return status;
}

/**
 * ice_release_global_cfg_lock
 * @hw: pointer to the HW structure
 *
 * This function will release the global config lock.
 */
void
ice_release_global_cfg_lock(struct ice_hw *hw)
{
	ice_release_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID);
}

enum ice_ddp_state
ice_map_aq_err_to_ddp_state(enum ice_aq_err aq_err)
{
	switch (aq_err) {
	case ICE_AQ_RC_ENOSEC:
		return ICE_DDP_PKG_NO_SEC_MANIFEST;
	case ICE_AQ_RC_EBADSIG:
		return ICE_DDP_PKG_FILE_SIGNATURE_INVALID;
	case ICE_AQ_RC_ESVN:
		return ICE_DDP_PKG_SECURE_VERSION_NBR_TOO_LOW;
	case ICE_AQ_RC_EBADMAN:
		return ICE_DDP_PKG_MANIFEST_INVALID;
	case ICE_AQ_RC_EBADBUF:
		return ICE_DDP_PKG_BUFFER_INVALID;
	default:
		return ICE_DDP_PKG_ERR;
	}
}

/**
 * ice_is_buffer_metadata - determine if package buffer is a metadata buffer
 * @buf: pointer to buffer header
 */
bool
ice_is_buffer_metadata(struct ice_buf_hdr *buf)
{
	return !!(le32toh(buf->section_entry[0].type) & ICE_METADATA_BUF);
}

/**
 * ice_is_last_download_buffer
 * @buf: pointer to current buffer header
 * @idx: index of the buffer in the current sequence
 * @count: the buffer count in the current sequence
 *
 * Note: this routine should only be called if the buffer is not the last buffer
 */
bool
ice_is_last_download_buffer(struct ice_buf_hdr *buf, uint32_t idx,
    uint32_t count)
{
	bool last = ((idx + 1) == count);

	/* A set metadata flag in the next buffer will signal that the current
	 * buffer will be the last buffer downloaded
	 */
	if (!last) {
		struct ice_buf *next_buf = ((struct ice_buf *)buf) + 1;

		last = ice_is_buffer_metadata((struct ice_buf_hdr *)next_buf);
	}

	return last;
}

/**
 * ice_aq_download_pkg
 * @hw: pointer to the hardware structure
 * @pkg_buf: the package buffer to transfer
 * @buf_size: the size of the package buffer
 * @last_buf: last buffer indicator
 * @error_offset: returns error offset
 * @error_info: returns error information
 * @cd: pointer to command details structure or NULL
 *
 * Download Package (0x0C40)
 */
enum ice_status
ice_aq_download_pkg(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf,
		    uint16_t buf_size, bool last_buf, uint32_t *error_offset,
		    uint32_t *error_info, struct ice_sq_cd *cd)
{
	struct ice_aqc_download_pkg *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	if (error_offset)
		*error_offset = 0;
	if (error_info)
		*error_info = 0;

	cmd = &desc.params.download_pkg;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_download_pkg);
	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	if (last_buf)
		cmd->flags |= ICE_AQC_DOWNLOAD_PKG_LAST_BUF;

	status = ice_aq_send_cmd(hw, &desc, pkg_buf, buf_size, cd);
	if (status == ICE_ERR_AQ_ERROR) {
		/* Read error from buffer only when the FW returned an error */
		struct ice_aqc_download_pkg_resp *resp;

		resp = (struct ice_aqc_download_pkg_resp *)pkg_buf;
		if (error_offset)
			*error_offset = le32toh(resp->error_offset);
		if (error_info)
			*error_info = le32toh(resp->error_info);
	}

	return status;
}

/**
 * ice_dwnld_cfg_bufs_no_lock
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @start: buffer index of first buffer to download
 * @count: the number of buffers to download
 * @indicate_last: if true, then set last buffer flag on last buffer download
 *
 * Downloads package configuration buffers to the firmware. Metadata buffers
 * are skipped, and the first metadata buffer found indicates that the rest
 * of the buffers are all metadata buffers.
 */
enum ice_ddp_state
ice_dwnld_cfg_bufs_no_lock(struct ice_hw *hw, struct ice_buf *bufs,
    uint32_t start, uint32_t count, bool indicate_last)
{
	enum ice_ddp_state state = ICE_DDP_PKG_SUCCESS;
	struct ice_buf_hdr *bh;
	enum ice_aq_err err;
	uint32_t offset, info, i;

	if (!bufs || !count)
		return ICE_DDP_PKG_ERR;

	/* If the first buffer's first section has its metadata bit set
	 * then there are no buffers to be downloaded, and the operation is
	 * considered a success.
	 */
	bh = (struct ice_buf_hdr *)(bufs + start);
	if (le32toh(bh->section_entry[0].type) & ICE_METADATA_BUF)
		return ICE_DDP_PKG_SUCCESS;

	for (i = 0; i < count; i++) {
		enum ice_status status;
		bool last = false;

		bh = (struct ice_buf_hdr *)(bufs + start + i);

		if (indicate_last)
			last = ice_is_last_download_buffer(bh, i, count);

		status = ice_aq_download_pkg(hw, bh, ICE_PKG_BUF_SIZE, last,
					     &offset, &info, NULL);

		/* Save AQ status from download package */
		if (status) {
			DNPRINTF(ICE_DBG_PKG,
			    "Pkg download failed: err %d off %d inf %d\n",
			    status, offset, info);
			err = hw->adminq.sq_last_status;
			state = ice_map_aq_err_to_ddp_state(err);
			break;
		}

		if (last)
			break;
	}

	return state;
}

/**
 * ice_download_pkg_sig_seg - download a signature segment
 * @hw: pointer to the hardware structure
 * @seg: pointer to signature segment
 */
enum ice_ddp_state
ice_download_pkg_sig_seg(struct ice_hw *hw, struct ice_sign_seg *seg)
{
	enum ice_ddp_state state;

	state = ice_dwnld_cfg_bufs_no_lock(hw, seg->buf_tbl.buf_array, 0,
	    le32toh(seg->buf_tbl.buf_count), false);

	return state;
}

/**
 * ice_download_pkg_config_seg - download a config segment
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to package header
 * @idx: segment index
 * @start: starting buffer
 * @count: buffer count
 *
 * Note: idx must reference a ICE segment
 */
enum ice_ddp_state
ice_download_pkg_config_seg(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr,
    uint32_t idx, uint32_t start, uint32_t count)
{
	struct ice_buf_table *bufs;
	struct ice_seg *seg;
	uint32_t buf_count;

	seg = (struct ice_seg *)ice_get_pkg_seg_by_idx(pkg_hdr, idx);
	if (!seg)
		return ICE_DDP_PKG_ERR;

	bufs = ice_find_buf_table(seg);
	buf_count = le32toh(bufs->buf_count);

	if (start >= buf_count || start + count > buf_count)
		return ICE_DDP_PKG_ERR;

	return ice_dwnld_cfg_bufs_no_lock(hw, bufs->buf_array, start, count,
	    true);
}

/**
 * ice_dwnld_sign_and_cfg_segs - download a signing segment and config segment
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to package header
 * @idx: segment index (must be a signature segment)
 *
 * Note: idx must reference a signature segment
 */
enum ice_ddp_state
ice_dwnld_sign_and_cfg_segs(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr,
    uint32_t idx)
{
	enum ice_ddp_state state;
	struct ice_sign_seg *seg;
	uint32_t conf_idx;
	uint32_t start;
	uint32_t count;

	seg = (struct ice_sign_seg *)ice_get_pkg_seg_by_idx(pkg_hdr, idx);
	if (!seg)
		return ICE_DDP_PKG_ERR;

	conf_idx = le32toh(seg->signed_seg_idx);
	start = le32toh(seg->signed_buf_start);
	count = le32toh(seg->signed_buf_count);

	state = ice_download_pkg_sig_seg(hw, seg);
	if (state)
		return state;

	return ice_download_pkg_config_seg(hw, pkg_hdr, conf_idx, start, count);
}

/**
 * ice_aq_set_port_params - set physical port parameters
 * @pi: pointer to the port info struct
 * @bad_frame_vsi: defines the VSI to which bad frames are forwarded
 * @save_bad_pac: if set packets with errors are forwarded to the bad frames VSI
 * @pad_short_pac: if set transmit packets smaller than 60 bytes are padded
 * @double_vlan: if set double VLAN is enabled
 * @cd: pointer to command details structure or NULL
 *
 * Set Physical port parameters (0x0203)
 */
enum ice_status
ice_aq_set_port_params(struct ice_port_info *pi, uint16_t bad_frame_vsi,
		       bool save_bad_pac, bool pad_short_pac, bool double_vlan,
		       struct ice_sq_cd *cd)
{
	struct ice_aqc_set_port_params *cmd;
	struct ice_hw *hw = pi->hw;
	struct ice_aq_desc desc;
	uint16_t cmd_flags = 0;

	cmd = &desc.params.set_port_params;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_port_params);
	cmd->bad_frame_vsi = htole16(bad_frame_vsi);
	if (save_bad_pac)
		cmd_flags |= ICE_AQC_SET_P_PARAMS_SAVE_BAD_PACKETS;
	if (pad_short_pac)
		cmd_flags |= ICE_AQC_SET_P_PARAMS_PAD_SHORT_PACKETS;
	if (double_vlan)
		cmd_flags |= ICE_AQC_SET_P_PARAMS_DOUBLE_VLAN_ENA;
	cmd->cmd_flags = htole16(cmd_flags);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_aq_set_vlan_mode - set the VLAN mode of the device
 * @hw: pointer to the HW structure
 * @set_params: requested VLAN mode configuration
 *
 * Set VLAN Mode Parameters (0x020C)
 */
enum ice_status
ice_aq_set_vlan_mode(struct ice_hw *hw,
		     struct ice_aqc_set_vlan_mode *set_params)
{
	uint8_t rdma_packet, mng_vlan_prot_id;
	struct ice_aq_desc desc;

	if (!set_params)
		return ICE_ERR_PARAM;

	if (set_params->l2tag_prio_tagging > ICE_AQ_VLAN_PRIO_TAG_MAX)
		return ICE_ERR_PARAM;

	rdma_packet = set_params->rdma_packet;
	if (rdma_packet != ICE_AQ_SVM_VLAN_RDMA_PKT_FLAG_SETTING &&
	    rdma_packet != ICE_AQ_DVM_VLAN_RDMA_PKT_FLAG_SETTING)
		return ICE_ERR_PARAM;

	mng_vlan_prot_id = set_params->mng_vlan_prot_id;
	if (mng_vlan_prot_id != ICE_AQ_VLAN_MNG_PROTOCOL_ID_OUTER &&
	    mng_vlan_prot_id != ICE_AQ_VLAN_MNG_PROTOCOL_ID_INNER)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc,
				      ice_aqc_opc_set_vlan_mode_parameters);
	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	return ice_aq_send_cmd(hw, &desc, set_params, sizeof(*set_params),
			       NULL);
}

/**
 * ice_set_svm - set single VLAN mode
 * @hw: pointer to the HW structure
 */
enum ice_status
ice_set_svm(struct ice_hw *hw)
{
	struct ice_aqc_set_vlan_mode *set_params;
	enum ice_status status;

	status = ice_aq_set_port_params(hw->port_info, 0,
	    false, false, false, NULL);
	if (status) {
		DNPRINTF(ICE_DBG_INIT,
		    "Failed to set port parameters for single VLAN mode\n");
		return status;
	}

	set_params = (struct ice_aqc_set_vlan_mode *)
		ice_malloc(hw, sizeof(*set_params));
	if (!set_params)
		return ICE_ERR_NO_MEMORY;

	/* default configuration for SVM configurations */
	set_params->l2tag_prio_tagging = ICE_AQ_VLAN_PRIO_TAG_INNER_CTAG;
	set_params->rdma_packet = ICE_AQ_SVM_VLAN_RDMA_PKT_FLAG_SETTING;
	set_params->mng_vlan_prot_id = ICE_AQ_VLAN_MNG_PROTOCOL_ID_INNER;

	status = ice_aq_set_vlan_mode(hw, set_params);
	if (status)
		DNPRINTF(ICE_DBG_INIT,
		    "Failed to configure port in single VLAN mode\n");

	ice_free(hw, set_params);
	return status;
}

/**
 * ice_pkg_get_supported_vlan_mode - chk if DDP supports Double VLAN mode (DVM)
 * @hw: pointer to the HW struct
 * @dvm: output variable to determine if DDP supports DVM(true) or SVM(false)
 */
enum ice_status
ice_pkg_get_supported_vlan_mode(struct ice_hw *hw, bool *dvm)
{
#if 0
	u16 meta_init_size = sizeof(struct ice_meta_init_section);
	struct ice_meta_init_section *sect;
	struct ice_buf_build *bld;
	enum ice_status status;

	/* if anything fails, we assume there is no DVM support */
	*dvm = false;

	bld = ice_pkg_buf_alloc_single_section(hw,
					       ICE_SID_RXPARSER_METADATA_INIT,
					       meta_init_size, (void **)&sect);
	if (!bld)
		return ICE_ERR_NO_MEMORY;

	/* only need to read a single section */
	sect->count = CPU_TO_LE16(1);
	sect->offset = CPU_TO_LE16(ICE_META_VLAN_MODE_ENTRY);

	status = ice_aq_upload_section(hw,
				       (struct ice_buf_hdr *)ice_pkg_buf(bld),
				       ICE_PKG_BUF_SIZE, NULL);
	if (!status) {
		ice_declare_bitmap(entry, ICE_META_INIT_BITS);
		u32 arr[ICE_META_INIT_DW_CNT];
		u16 i;

		/* convert to host bitmap format */
		for (i = 0; i < ICE_META_INIT_DW_CNT; i++)
			arr[i] = LE32_TO_CPU(sect->entry[0].bm[i]);

		ice_bitmap_from_array32(entry, arr, (u16)ICE_META_INIT_BITS);

		/* check if DVM is supported */
		*dvm = ice_is_bit_set(entry, ICE_META_VLAN_MODE_BIT);
	}

	ice_pkg_buf_free(hw, bld);

	return status;
#else
	return ICE_ERR_NOT_IMPL;
#endif
}

/**
 * ice_pkg_supports_dvm - find out if DDP supports DVM
 * @hw: pointer to the HW structure
 */
bool
ice_pkg_supports_dvm(struct ice_hw *hw)
{
	enum ice_status status;
	bool pkg_supports_dvm;

	status = ice_pkg_get_supported_vlan_mode(hw, &pkg_supports_dvm);
	if (status) {
		DNPRINTF(ICE_DBG_PKG,
		     "Failed to get supported VLAN mode, status %d\n", status);
		return false;
	}

	return pkg_supports_dvm;
}

/**
 * ice_aq_get_vlan_mode - get the VLAN mode of the device
 * @hw: pointer to the HW structure
 * @get_params: structure FW fills in based on the current VLAN mode config
 *
 * Get VLAN Mode Parameters (0x020D)
 */
enum ice_status
ice_aq_get_vlan_mode(struct ice_hw *hw,
		     struct ice_aqc_get_vlan_mode *get_params)
{
	struct ice_aq_desc desc;

	if (!get_params)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc,
				      ice_aqc_opc_get_vlan_mode_parameters);

	return ice_aq_send_cmd(hw, &desc, get_params, sizeof(*get_params),
			       NULL);
}

/**
 * ice_fw_supports_dvm - find out if FW supports DVM
 * @hw: pointer to the HW structure
 */
bool
ice_fw_supports_dvm(struct ice_hw *hw)
{
	struct ice_aqc_get_vlan_mode get_vlan_mode = { 0 };
	enum ice_status status;

	/* If firmware returns success, then it supports DVM, else it only
	 * supports SVM
	 */
	status = ice_aq_get_vlan_mode(hw, &get_vlan_mode);
	if (status) {
		DNPRINTF(ICE_DBG_NVM,
		    "%s: Failed to get VLAN mode, status %d\n",
		    __func__, status);
		return false;
	}

	return true;
}

/**
 * ice_is_dvm_supported - check if Double VLAN Mode is supported
 * @hw: pointer to the hardware structure
 *
 * Returns true if Double VLAN Mode (DVM) is supported and false if only Single
 * VLAN Mode (SVM) is supported. In order for DVM to be supported the DDP and
 * firmware must support it, otherwise only SVM is supported. This function
 * should only be called while the global config lock is held and after the
 * package has been successfully downloaded.
 */
bool
ice_is_dvm_supported(struct ice_hw *hw)
{
	if (!ice_pkg_supports_dvm(hw)) {
		DNPRINTF(ICE_DBG_PKG, "DDP doesn't support DVM\n");
		return false;
	}

	if (!ice_fw_supports_dvm(hw)) {
		DNPRINTF(ICE_DBG_PKG, "FW doesn't support DVM\n");
		return false;
	}

	return true;
}


/**
 * ice_set_vlan_mode
 * @hw: pointer to the HW structure
 */
enum ice_status
ice_set_vlan_mode(struct ice_hw *hw)
{
	if (!ice_is_dvm_supported(hw))
		return ICE_SUCCESS;

	return ice_set_svm(hw);
}

/**
 * ice_post_dwnld_pkg_actions - perform post download package actions
 * @hw: pointer to the hardware structure
 */
enum ice_ddp_state
ice_post_dwnld_pkg_actions(struct ice_hw *hw)
{
	enum ice_ddp_state state = ICE_DDP_PKG_SUCCESS;
	enum ice_status status;

	status = ice_set_vlan_mode(hw);
	if (status) {
		DNPRINTF(ICE_DBG_PKG, "Failed to set VLAN mode: err %d\n",
		    status);
		state = ICE_DDP_PKG_ERR;
	}

	return state;
}

/**
 * ice_download_pkg_with_sig_seg - download package using signature segments
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to package header
 */
enum ice_ddp_state
ice_download_pkg_with_sig_seg(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr)
{
	enum ice_aq_err aq_err = hw->adminq.sq_last_status;
	enum ice_ddp_state state = ICE_DDP_PKG_ERR;
	enum ice_status status;
	uint32_t i;

	DNPRINTF(ICE_DBG_INIT, "Segment ID %d\n", hw->pkg_seg_id);
	DNPRINTF(ICE_DBG_INIT, "Signature type %d\n", hw->pkg_sign_type);

	status = ice_acquire_global_cfg_lock(hw, ICE_RES_WRITE);
	if (status) {
		if (status == ICE_ERR_AQ_NO_WORK)
			state = ICE_DDP_PKG_ALREADY_LOADED;
		else
			state = ice_map_aq_err_to_ddp_state(aq_err);
		return state;
	}

	for (i = 0; i < le32toh(pkg_hdr->seg_count); i++) {
		if (!ice_is_signing_seg_type_at_idx(pkg_hdr, i, hw->pkg_seg_id,
						    hw->pkg_sign_type))
			continue;

		state = ice_dwnld_sign_and_cfg_segs(hw, pkg_hdr, i);
		if (state)
			break;
	}

	if (!state)
		state = ice_post_dwnld_pkg_actions(hw);

	ice_release_global_cfg_lock(hw);

	return state;
}

/**
 * ice_dwnld_cfg_bufs
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @count: the number of buffers in the array
 *
 * Obtains global config lock and downloads the package configuration buffers
 * to the firmware.
 */
enum ice_ddp_state
ice_dwnld_cfg_bufs(struct ice_hw *hw, struct ice_buf *bufs, uint32_t count)
{
	enum ice_ddp_state state = ICE_DDP_PKG_SUCCESS;
	enum ice_status status;
	struct ice_buf_hdr *bh;

	if (!bufs || !count)
		return ICE_DDP_PKG_ERR;

	/* If the first buffer's first section has its metadata bit set
	 * then there are no buffers to be downloaded, and the operation is
	 * considered a success.
	 */
	bh = (struct ice_buf_hdr *)bufs;
	if (le32toh(bh->section_entry[0].type) & ICE_METADATA_BUF)
		return ICE_DDP_PKG_SUCCESS;

	status = ice_acquire_global_cfg_lock(hw, ICE_RES_WRITE);
	if (status) {
		if (status == ICE_ERR_AQ_NO_WORK)
			return ICE_DDP_PKG_ALREADY_LOADED;
		return ice_map_aq_err_to_ddp_state(hw->adminq.sq_last_status);
	}

	state = ice_dwnld_cfg_bufs_no_lock(hw, bufs, 0, count, true);
	if (!state)
		state = ice_post_dwnld_pkg_actions(hw);

	ice_release_global_cfg_lock(hw);

	return state;
}
/**
 * ice_download_pkg_without_sig_seg
 * @hw: pointer to the hardware structure
 * @ice_seg: pointer to the segment of the package to be downloaded
 *
 * Handles the download of a complete package without signature segment.
 */
enum ice_ddp_state
ice_download_pkg_without_sig_seg(struct ice_hw *hw, struct ice_seg *ice_seg)
{
	struct ice_buf_table *ice_buf_tbl;

	DNPRINTF(ICE_DBG_PKG, "Segment format version: %d.%d.%d.%d\n",
	    ice_seg->hdr.seg_format_ver.major,
	    ice_seg->hdr.seg_format_ver.minor,
	    ice_seg->hdr.seg_format_ver.update,
	    ice_seg->hdr.seg_format_ver.draft);

	DNPRINTF(ICE_DBG_PKG, "Seg: type 0x%X, size %d, name %s\n",
	    le32toh(ice_seg->hdr.seg_type),
	    le32toh(ice_seg->hdr.seg_size), ice_seg->hdr.seg_id);

	ice_buf_tbl = ice_find_buf_table(ice_seg);

	DNPRINTF(ICE_DBG_PKG, "Seg buf count: %d\n",
	    le32toh(ice_buf_tbl->buf_count));

	return ice_dwnld_cfg_bufs(hw, ice_buf_tbl->buf_array,
	    le32toh(ice_buf_tbl->buf_count));
}

/**
 * ice_aq_is_dvm_ena - query FW to check if double VLAN mode is enabled
 * @hw: pointer to the HW structure
 *
 * Returns true if the hardware/firmware is configured in double VLAN mode,
 * else return false signaling that the hardware/firmware is configured in
 * single VLAN mode.
 *
 * Also, return false if this call fails for any reason (i.e. firmware doesn't
 * support this AQ call).
 */
bool
ice_aq_is_dvm_ena(struct ice_hw *hw)
{
	struct ice_aqc_get_vlan_mode get_params = { 0 };
	enum ice_status status;

	status = ice_aq_get_vlan_mode(hw, &get_params);
	if (status) {
		DNPRINTF(ICE_DBG_AQ, "Failed to get VLAN mode, status %d\n",
		    status);
		return false;
	}

	return (get_params.vlan_mode & ICE_AQ_VLAN_MODE_DVM_ENA);
}

/**
 * ice_cache_vlan_mode - cache VLAN mode after DDP is downloaded
 * @hw: pointer to the HW structure
 *
 * This is only called after downloading the DDP and after the global
 * configuration lock has been released because all ports on a device need to
 * cache the VLAN mode.
 */
void
ice_cache_vlan_mode(struct ice_hw *hw)
{
	hw->dvm_ena = ice_aq_is_dvm_ena(hw) ? true : false;
}

/**
 * ice_post_pkg_dwnld_vlan_mode_cfg - configure VLAN mode after DDP download
 * @hw: pointer to the HW structure
 *
 * This function is meant to configure any VLAN mode specific functionality
 * after the global configuration lock has been released and the DDP has been
 * downloaded.
 *
 * Since only one PF downloads the DDP and configures the VLAN mode there needs
 * to be a way to configure the other PFs after the DDP has been downloaded and
 * the global configuration lock has been released. All such code should go in
 * this function.
 */
void
ice_post_pkg_dwnld_vlan_mode_cfg(struct ice_hw *hw)
{
	ice_cache_vlan_mode(hw);
}

/**
 * ice_download_pkg
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to package header
 * @ice_seg: pointer to the segment of the package to be downloaded
 *
 * Handles the download of a complete package.
 */
enum ice_ddp_state
ice_download_pkg(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr,
		 struct ice_seg *ice_seg)
{
	enum ice_ddp_state state;

	if (hw->pkg_has_signing_seg)
		state = ice_download_pkg_with_sig_seg(hw, pkg_hdr);
	else
		state = ice_download_pkg_without_sig_seg(hw, ice_seg);

	ice_post_pkg_dwnld_vlan_mode_cfg(hw);

	return state;
}

/**
 * ice_get_pkg_info
 * @hw: pointer to the hardware structure
 *
 * Store details of the package currently loaded in HW into the HW structure.
 */
enum ice_ddp_state
ice_get_pkg_info(struct ice_hw *hw)
{
	enum ice_ddp_state state = ICE_DDP_PKG_SUCCESS;
	struct ice_aqc_get_pkg_info_resp *pkg_info;
	uint16_t size;
	uint32_t i;

	size = ice_struct_size(pkg_info, pkg_info, ICE_PKG_CNT);
	pkg_info = (struct ice_aqc_get_pkg_info_resp *)ice_malloc(hw, size);
	if (!pkg_info)
		return ICE_DDP_PKG_ERR;

	if (ice_aq_get_pkg_info_list(hw, pkg_info, size, NULL)) {
		state = ICE_DDP_PKG_ERR;
		goto init_pkg_free_alloc;
	}

	for (i = 0; i < le32toh(pkg_info->count); i++) {
#define ICE_PKG_FLAG_COUNT	4
		char flags[ICE_PKG_FLAG_COUNT + 1] = { 0 };
		uint8_t place = 0;

		if (pkg_info->pkg_info[i].is_active) {
			flags[place++] = 'A';
			hw->active_pkg_ver = pkg_info->pkg_info[i].ver;
			hw->active_track_id =
				le32toh(pkg_info->pkg_info[i].track_id);
			memcpy(hw->active_pkg_name, pkg_info->pkg_info[i].name,
				   sizeof(pkg_info->pkg_info[i].name));
			hw->active_pkg_in_nvm = pkg_info->pkg_info[i].is_in_nvm;
		}
		if (pkg_info->pkg_info[i].is_active_at_boot)
			flags[place++] = 'B';
		if (pkg_info->pkg_info[i].is_modified)
			flags[place++] = 'M';
		if (pkg_info->pkg_info[i].is_in_nvm)
			flags[place++] = 'N';

		DNPRINTF(ICE_DBG_PKG, "Pkg[%d]: %d.%d.%d.%d,%s,%s\n",
		    i, pkg_info->pkg_info[i].ver.major,
		    pkg_info->pkg_info[i].ver.minor, 
		    pkg_info->pkg_info[i].ver.update,
		    pkg_info->pkg_info[i].ver.draft,
		    pkg_info->pkg_info[i].name, flags);
	}

init_pkg_free_alloc:
	ice_free(hw, pkg_info);

	return state;
}

/**
 * ice_get_ddp_pkg_state - get DDP pkg state after download
 * @hw: pointer to the HW struct
 * @already_loaded: indicates if pkg was already loaded onto the device
 *
 */
enum ice_ddp_state
ice_get_ddp_pkg_state(struct ice_hw *hw, bool already_loaded)
{
	if (hw->pkg_ver.major == hw->active_pkg_ver.major &&
	    hw->pkg_ver.minor == hw->active_pkg_ver.minor &&
	    hw->pkg_ver.update == hw->active_pkg_ver.update &&
	    hw->pkg_ver.draft == hw->active_pkg_ver.draft &&
	    !memcmp(hw->pkg_name, hw->active_pkg_name, sizeof(hw->pkg_name))) {
		if (already_loaded)
			return ICE_DDP_PKG_SAME_VERSION_ALREADY_LOADED;
		else
			return ICE_DDP_PKG_SUCCESS;
	} else if (hw->active_pkg_ver.major != ICE_PKG_SUPP_VER_MAJ ||
		   hw->active_pkg_ver.minor != ICE_PKG_SUPP_VER_MNR) {
		return ICE_DDP_PKG_ALREADY_LOADED_NOT_SUPPORTED;
	} else if (hw->active_pkg_ver.major == ICE_PKG_SUPP_VER_MAJ &&
		   hw->active_pkg_ver.minor == ICE_PKG_SUPP_VER_MNR) {
		return ICE_DDP_PKG_COMPATIBLE_ALREADY_LOADED;
	} else {
		return ICE_DDP_PKG_ERR;
	}
}

/**
 * ice_init_pkg_regs - initialize additional package registers
 * @hw: pointer to the hardware structure
 */
void
ice_init_pkg_regs(struct ice_hw *hw)
{
#define ICE_SW_BLK_INP_MASK_L 0xFFFFFFFF
#define ICE_SW_BLK_INP_MASK_H 0x0000FFFF
#define ICE_SW_BLK_IDX	0

	/* setup Switch block input mask, which is 48-bits in two parts */
	ICE_WRITE(hw, GL_PREEXT_L2_PMASK0(ICE_SW_BLK_IDX),
	    ICE_SW_BLK_INP_MASK_L);
	ICE_WRITE(hw, GL_PREEXT_L2_PMASK1(ICE_SW_BLK_IDX),
	    ICE_SW_BLK_INP_MASK_H);
}

/**
 * ice_ptg_alloc_val - Allocates a new packet type group ID by value
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @ptg: the PTG to allocate
 *
 * This function allocates a given packet type group ID specified by the PTG
 * parameter.
 */
void
ice_ptg_alloc_val(struct ice_hw *hw, enum ice_block blk, uint8_t ptg)
{
	hw->blk[blk].xlt1.ptg_tbl[ptg].in_use = true;
}

/**
 * ice_ptg_find_ptype - Search for packet type group using packet type (ptype)
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @ptype: the ptype to search for
 * @ptg: pointer to variable that receives the PTG
 *
 * This function will search the PTGs for a particular ptype, returning the
 * PTG ID that contains it through the PTG parameter, with the value of
 * ICE_DEFAULT_PTG (0) meaning it is part the default PTG.
 */
enum ice_status
ice_ptg_find_ptype(struct ice_hw *hw, enum ice_block blk, uint16_t ptype,
    uint8_t *ptg)
{
	if (ptype >= ICE_XLT1_CNT || !ptg)
		return ICE_ERR_PARAM;

	*ptg = hw->blk[blk].xlt1.ptypes[ptype].ptg;
	return ICE_SUCCESS;
}

/**
 * ice_ptg_remove_ptype - Removes ptype from a particular packet type group
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @ptype: the ptype to remove
 * @ptg: the PTG to remove the ptype from
 *
 * This function will remove the ptype from the specific PTG, and move it to
 * the default PTG (ICE_DEFAULT_PTG).
 */
enum ice_status
ice_ptg_remove_ptype(struct ice_hw *hw, enum ice_block blk, uint16_t ptype,
    uint8_t ptg)
{
	struct ice_ptg_ptype **ch;
	struct ice_ptg_ptype *p;

	if (ptype > ICE_XLT1_CNT - 1)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	/* Should not happen if .in_use is set, bad config */
	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype)
		return ICE_ERR_CFG;

	/* find the ptype within this PTG, and bypass the link over it */
	p = hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	ch = &hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	while (p) {
		if (ptype == (p - hw->blk[blk].xlt1.ptypes)) {
			*ch = p->next_ptype;
			break;
		}

		ch = &p->next_ptype;
		p = p->next_ptype;
	}

	hw->blk[blk].xlt1.ptypes[ptype].ptg = ICE_DEFAULT_PTG;
	hw->blk[blk].xlt1.ptypes[ptype].next_ptype = NULL;

	return ICE_SUCCESS;
}

/**
 * ice_ptg_add_mv_ptype - Adds/moves ptype to a particular packet type group
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @ptype: the ptype to add or move
 * @ptg: the PTG to add or move the ptype to
 *
 * This function will either add or move a ptype to a particular PTG depending
 * on if the ptype is already part of another group. Note that using a
 * a destination PTG ID of ICE_DEFAULT_PTG (0) will move the ptype to the
 * default PTG.
 */
enum ice_status
ice_ptg_add_mv_ptype(struct ice_hw *hw, enum ice_block blk, uint16_t ptype,
    uint8_t ptg)
{
	enum ice_status status;
	uint8_t original_ptg;

	if (ptype > ICE_XLT1_CNT - 1)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].in_use && ptg != ICE_DEFAULT_PTG)
		return ICE_ERR_DOES_NOT_EXIST;

	status = ice_ptg_find_ptype(hw, blk, ptype, &original_ptg);
	if (status)
		return status;

	/* Is ptype already in the correct PTG? */
	if (original_ptg == ptg)
		return ICE_SUCCESS;

	/* Remove from original PTG and move back to the default PTG */
	if (original_ptg != ICE_DEFAULT_PTG)
		ice_ptg_remove_ptype(hw, blk, ptype, original_ptg);

	/* Moving to default PTG? Then we're done with this request */
	if (ptg == ICE_DEFAULT_PTG)
		return ICE_SUCCESS;

	/* Add ptype to PTG at beginning of list */
	hw->blk[blk].xlt1.ptypes[ptype].next_ptype =
		hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype =
		&hw->blk[blk].xlt1.ptypes[ptype];

	hw->blk[blk].xlt1.ptypes[ptype].ptg = ptg;
	hw->blk[blk].xlt1.t[ptype] = ptg;

	return ICE_SUCCESS;
}

/**
 * ice_init_sw_xlt1_db - init software XLT1 database from HW tables
 * @hw: pointer to the hardware structure
 * @blk: the HW block to initialize
 */
void
ice_init_sw_xlt1_db(struct ice_hw *hw, enum ice_block blk)
{
	uint16_t pt;

	for (pt = 0; pt < hw->blk[blk].xlt1.count; pt++) {
		uint8_t ptg;

		ptg = hw->blk[blk].xlt1.t[pt];
		if (ptg != ICE_DEFAULT_PTG) {
			ice_ptg_alloc_val(hw, blk, ptg);
			ice_ptg_add_mv_ptype(hw, blk, pt, ptg);
		}
	}
}

/**
 * ice_vsig_alloc_val - allocate a new VSIG by value
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsig: the VSIG to allocate
 *
 * This function will allocate a given VSIG specified by the VSIG parameter.
 */
uint16_t
ice_vsig_alloc_val(struct ice_hw *hw, enum ice_block blk, uint16_t vsig)
{
	uint16_t idx = vsig & ICE_VSIG_IDX_M;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use) {
		TAILQ_INIT(&hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst);
		hw->blk[blk].xlt2.vsig_tbl[idx].in_use = true;
	}

	return ICE_VSIG_VALUE(idx, hw->pf_id);
}

/**
 * ice_vsig_find_vsi - find a VSIG that contains a specified VSI
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsi: VSI of interest
 * @vsig: pointer to receive the VSI group
 *
 * This function will lookup the VSI entry in the XLT2 list and return
 * the VSI group its associated with.
 */
enum ice_status
ice_vsig_find_vsi(struct ice_hw *hw, enum ice_block blk, uint16_t vsi,
    uint16_t *vsig)
{
	if (!vsig || vsi >= ICE_MAX_VSI)
		return ICE_ERR_PARAM;

	/* As long as there's a default or valid VSIG associated with the input
	 * VSI, the functions returns a success. Any handling of VSIG will be
	 * done by the following add, update or remove functions.
	 */
	*vsig = hw->blk[blk].xlt2.vsis[vsi].vsig;

	return ICE_SUCCESS;
}

/**
 * ice_vsig_remove_vsi - remove VSI from VSIG
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsi: VSI to remove
 * @vsig: VSI group to remove from
 *
 * The function will remove the input VSI from its VSI group and move it
 * to the DEFAULT_VSIG.
 */
enum ice_status
ice_vsig_remove_vsi(struct ice_hw *hw, enum ice_block blk, uint16_t vsi,
    uint16_t vsig)
{
	struct ice_vsig_vsi **vsi_head, *vsi_cur, *vsi_tgt;
	uint16_t idx;

	idx = vsig & ICE_VSIG_IDX_M;

	if (vsi >= ICE_MAX_VSI || idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	/* entry already in default VSIG, don't have to remove */
	if (idx == ICE_DEFAULT_VSIG)
		return ICE_SUCCESS;

	vsi_head = &hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	if (!(*vsi_head))
		return ICE_ERR_CFG;

	vsi_tgt = &hw->blk[blk].xlt2.vsis[vsi];
	vsi_cur = (*vsi_head);

	/* iterate the VSI list, skip over the entry to be removed */
	while (vsi_cur) {
		if (vsi_tgt == vsi_cur) {
			(*vsi_head) = vsi_cur->next_vsi;
			break;
		}
		vsi_head = &vsi_cur->next_vsi;
		vsi_cur = vsi_cur->next_vsi;
	}

	/* verify if VSI was removed from group list */
	if (!vsi_cur)
		return ICE_ERR_DOES_NOT_EXIST;

	vsi_cur->vsig = ICE_DEFAULT_VSIG;
	vsi_cur->changed = 1;
	vsi_cur->next_vsi = NULL;

	return ICE_SUCCESS;
}

/**
 * ice_vsig_add_mv_vsi - add or move a VSI to a VSI group
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsi: VSI to move
 * @vsig: destination VSI group
 *
 * This function will move or add the input VSI to the target VSIG.
 * The function will find the original VSIG the VSI belongs to and
 * move the entry to the DEFAULT_VSIG, update the original VSIG and
 * then move entry to the new VSIG.
 */
enum ice_status
ice_vsig_add_mv_vsi(struct ice_hw *hw, enum ice_block blk, uint16_t vsi,
    uint16_t vsig)
{
	struct ice_vsig_vsi *tmp;
	enum ice_status status;
	uint16_t orig_vsig, idx;

	idx = vsig & ICE_VSIG_IDX_M;

	if (vsi >= ICE_MAX_VSI || idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	/* if VSIG not in use and VSIG is not default type this VSIG
	 * doesn't exist.
	 */
	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use &&
	    vsig != ICE_DEFAULT_VSIG)
		return ICE_ERR_DOES_NOT_EXIST;

	status = ice_vsig_find_vsi(hw, blk, vsi, &orig_vsig);
	if (status)
		return status;

	/* no update required if vsigs match */
	if (orig_vsig == vsig)
		return ICE_SUCCESS;

	if (orig_vsig != ICE_DEFAULT_VSIG) {
		/* remove entry from orig_vsig and add to default VSIG */
		status = ice_vsig_remove_vsi(hw, blk, vsi, orig_vsig);
		if (status)
			return status;
	}

	if (idx == ICE_DEFAULT_VSIG)
		return ICE_SUCCESS;

	/* Create VSI entry and add VSIG and prop_mask values */
	hw->blk[blk].xlt2.vsis[vsi].vsig = vsig;
	hw->blk[blk].xlt2.vsis[vsi].changed = 1;

	/* Add new entry to the head of the VSIG list */
	tmp = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi =
		&hw->blk[blk].xlt2.vsis[vsi];
	hw->blk[blk].xlt2.vsis[vsi].next_vsi = tmp;
	hw->blk[blk].xlt2.t[vsi] = vsig;

	return ICE_SUCCESS;
}

/**
 * ice_init_sw_xlt2_db - init software XLT2 database from HW tables
 * @hw: pointer to the hardware structure
 * @blk: the HW block to initialize
 */
void
ice_init_sw_xlt2_db(struct ice_hw *hw, enum ice_block blk)
{
	uint16_t vsi;

	for (vsi = 0; vsi < hw->blk[blk].xlt2.count; vsi++) {
		uint16_t vsig;

		vsig = hw->blk[blk].xlt2.t[vsi];
		if (vsig) {
			ice_vsig_alloc_val(hw, blk, vsig);
			ice_vsig_add_mv_vsi(hw, blk, vsi, vsig);
			/* no changes at this time, since this has been
			 * initialized from the original package
			 */
			hw->blk[blk].xlt2.vsis[vsi].changed = 0;
		}
	}
}

/**
 * ice_init_sw_db - init software database from HW tables
 * @hw: pointer to the hardware structure
 */
void
ice_init_sw_db(struct ice_hw *hw)
{
	uint16_t i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		ice_init_sw_xlt1_db(hw, (enum ice_block)i);
		ice_init_sw_xlt2_db(hw, (enum ice_block)i);
	}
}

/**
 * ice_fill_tbl - Reads content of a single table type into database
 * @hw: pointer to the hardware structure
 * @block_id: Block ID of the table to copy
 * @sid: Section ID of the table to copy
 *
 * Will attempt to read the entire content of a given table of a single block
 * into the driver database. We assume that the buffer will always
 * be as large or larger than the data contained in the package. If
 * this condition is not met, there is most likely an error in the package
 * contents.
 */
void
ice_fill_tbl(struct ice_hw *hw, enum ice_block block_id, uint32_t sid)
{
	uint32_t dst_len, sect_len, offset = 0;
	struct ice_prof_redir_section *pr;
	struct ice_prof_id_section *pid;
	struct ice_xlt1_section *xlt1;
	struct ice_xlt2_section *xlt2;
	struct ice_sw_fv_section *es;
	struct ice_pkg_enum state;
	uint8_t *src, *dst;
	void *sect;

	/* if the HW segment pointer is null then the first iteration of
	 * ice_pkg_enum_section() will fail. In this case the HW tables will
	 * not be filled and return success.
	 */
	if (!hw->seg) {
		DNPRINTF(ICE_DBG_PKG,
		    "hw->seg is NULL, tables are not filled\n");
		return;
	}

	memset(&state, 0, sizeof(state));

	sect = ice_pkg_enum_section(hw->seg, &state, sid);
	while (sect) {
		switch (sid) {
		case ICE_SID_XLT1_SW:
		case ICE_SID_XLT1_FD:
		case ICE_SID_XLT1_RSS:
		case ICE_SID_XLT1_ACL:
		case ICE_SID_XLT1_PE:
			xlt1 = (struct ice_xlt1_section *)sect;
			src = xlt1->value;
			sect_len = le16toh(xlt1->count) *
				sizeof(*hw->blk[block_id].xlt1.t);
			dst = hw->blk[block_id].xlt1.t;
			dst_len = hw->blk[block_id].xlt1.count *
				sizeof(*hw->blk[block_id].xlt1.t);
			break;
		case ICE_SID_XLT2_SW:
		case ICE_SID_XLT2_FD:
		case ICE_SID_XLT2_RSS:
		case ICE_SID_XLT2_ACL:
		case ICE_SID_XLT2_PE:
			xlt2 = (struct ice_xlt2_section *)sect;
			src = (uint8_t *)xlt2->value;
			sect_len = le16toh(xlt2->count) *
				sizeof(*hw->blk[block_id].xlt2.t);
			dst = (uint8_t *)hw->blk[block_id].xlt2.t;
			dst_len = hw->blk[block_id].xlt2.count *
				sizeof(*hw->blk[block_id].xlt2.t);
			break;
		case ICE_SID_PROFID_TCAM_SW:
		case ICE_SID_PROFID_TCAM_FD:
		case ICE_SID_PROFID_TCAM_RSS:
		case ICE_SID_PROFID_TCAM_ACL:
		case ICE_SID_PROFID_TCAM_PE:
			pid = (struct ice_prof_id_section *)sect;
			src = (uint8_t *)pid->entry;
			sect_len = le16toh(pid->count) *
				sizeof(*hw->blk[block_id].prof.t);
			dst = (uint8_t *)hw->blk[block_id].prof.t;
			dst_len = hw->blk[block_id].prof.count *
				sizeof(*hw->blk[block_id].prof.t);
			break;
		case ICE_SID_PROFID_REDIR_SW:
		case ICE_SID_PROFID_REDIR_FD:
		case ICE_SID_PROFID_REDIR_RSS:
		case ICE_SID_PROFID_REDIR_ACL:
		case ICE_SID_PROFID_REDIR_PE:
			pr = (struct ice_prof_redir_section *)sect;
			src = pr->redir_value;
			sect_len = le16toh(pr->count) *
				sizeof(*hw->blk[block_id].prof_redir.t);
			dst = hw->blk[block_id].prof_redir.t;
			dst_len = hw->blk[block_id].prof_redir.count *
				sizeof(*hw->blk[block_id].prof_redir.t);
			break;
		case ICE_SID_FLD_VEC_SW:
		case ICE_SID_FLD_VEC_FD:
		case ICE_SID_FLD_VEC_RSS:
		case ICE_SID_FLD_VEC_ACL:
		case ICE_SID_FLD_VEC_PE:
			es = (struct ice_sw_fv_section *)sect;
			src = (uint8_t *)es->fv;
			sect_len = (uint32_t)(le16toh(es->count) *
					 hw->blk[block_id].es.fvw) *
				sizeof(*hw->blk[block_id].es.t);
			dst = (uint8_t *)hw->blk[block_id].es.t;
			dst_len = (uint32_t)(hw->blk[block_id].es.count *
					hw->blk[block_id].es.fvw) *
				sizeof(*hw->blk[block_id].es.t);
			break;
		default:
			return;
		}

		/* if the section offset exceeds destination length, terminate
		 * table fill.
		 */
		if (offset > dst_len)
			return;

		/* if the sum of section size and offset exceed destination size
		 * then we are out of bounds of the HW table size for that PF.
		 * Changing section length to fill the remaining table space
		 * of that PF.
		 */
		if ((offset + sect_len) > dst_len)
			sect_len = dst_len - offset;

		memcpy(dst + offset, src, sect_len);
		offset += sect_len;
		sect = ice_pkg_enum_section(NULL, &state, sid);
	}
}

/**
 * ice_fill_blk_tbls - Read package context for tables
 * @hw: pointer to the hardware structure
 *
 * Reads the current package contents and populates the driver
 * database with the data iteratively for all advanced feature
 * blocks. Assume that the HW tables have been allocated.
 */
void
ice_fill_blk_tbls(struct ice_hw *hw)
{
	uint8_t i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		enum ice_block blk_id = (enum ice_block)i;

		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].xlt1.sid);
		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].xlt2.sid);
		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].prof.sid);
		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].prof_redir.sid);
		ice_fill_tbl(hw, blk_id, hw->blk[blk_id].es.sid);
	}

	ice_init_sw_db(hw);
}

/**
 * ice_sw_fv_handler
 * @sect_type: section type
 * @section: pointer to section
 * @index: index of the field vector entry to be returned
 * @offset: ptr to variable that receives the offset in the field vector table
 *
 * This is a callback function that can be passed to ice_pkg_enum_entry.
 * This function treats the given section as of type ice_sw_fv_section and
 * enumerates offset field. "offset" is an index into the field vector table.
 */
void *
ice_sw_fv_handler(uint32_t sect_type, void *section, uint32_t index,
    uint32_t *offset)
{
	struct ice_sw_fv_section *fv_section =
		(struct ice_sw_fv_section *)section;

	if (!section || sect_type != ICE_SID_FLD_VEC_SW)
		return NULL;
	if (index >= le16toh(fv_section->count))
		return NULL;
	if (offset)
		/* "index" passed in to this function is relative to a given
		 * 4k block. To get to the true index into the field vector
		 * table need to add the relative index to the base_offset
		 * field of this section
		 */
		*offset = le16toh(fv_section->base_offset) + index;
	return fv_section->fv + index;
}

/**
 * ice_get_prof_index_max - get the max profile index for used profile
 * @hw: pointer to the HW struct
 *
 * Calling this function will get the max profile index for used profile
 * and store the index number in struct ice_switch_info *switch_info
 * in hw for following use.
 */
int
ice_get_prof_index_max(struct ice_hw *hw)
{
	uint16_t prof_index = 0, j, max_prof_index = 0;
	struct ice_pkg_enum state;
	struct ice_seg *ice_seg;
	bool flag = false;
	struct ice_fv *fv;
	uint32_t offset;

	memset(&state, 0, sizeof(state));

	if (!hw->seg)
		return ICE_ERR_PARAM;

	ice_seg = hw->seg;

	do {
		fv = (struct ice_fv *)
			ice_pkg_enum_entry(ice_seg, &state, ICE_SID_FLD_VEC_SW,
			    &offset, ice_sw_fv_handler);
		if (!fv)
			break;
		ice_seg = NULL;

		/* in the profile that not be used, the prot_id is set to 0xff
		 * and the off is set to 0x1ff for all the field vectors.
		 */
		for (j = 0; j < hw->blk[ICE_BLK_SW].es.fvw; j++)
			if (fv->ew[j].prot_id != ICE_PROT_INVALID ||
			    fv->ew[j].off != ICE_FV_OFFSET_INVAL)
				flag = true;
		if (flag && prof_index > max_prof_index)
			max_prof_index = prof_index;

		prof_index++;
		flag = false;
	} while (fv);

	hw->switch_info->max_used_prof_index = max_prof_index;

	return ICE_SUCCESS;
}

/**
 * ice_init_pkg - initialize/download package
 * @hw: pointer to the hardware structure
 * @buf: pointer to the package buffer
 * @len: size of the package buffer
 *
 * This function initializes a package. The package contains HW tables
 * required to do packet processing. First, the function extracts package
 * information such as version. Then it finds the ice configuration segment
 * within the package; this function then saves a copy of the segment pointer
 * within the supplied package buffer. Next, the function will cache any hints
 * from the package, followed by downloading the package itself. Note, that if
 * a previous PF driver has already downloaded the package successfully, then
 * the current driver will not have to download the package again.
 *
 * The local package contents will be used to query default behavior and to
 * update specific sections of the HW's version of the package (e.g. to update
 * the parse graph to understand new protocols).
 *
 * This function stores a pointer to the package buffer memory, and it is
 * expected that the supplied buffer will not be freed immediately. If the
 * package buffer needs to be freed, such as when read from a file, use
 * ice_copy_and_init_pkg() instead of directly calling ice_init_pkg() in this
 * case.
 */
enum ice_ddp_state
ice_init_pkg(struct ice_hw *hw, uint8_t *buf, uint32_t len)
{
	bool already_loaded = false;
	enum ice_ddp_state state;
	struct ice_pkg_hdr *pkg;
	struct ice_seg *seg;

	if (!buf || !len)
		return ICE_DDP_PKG_ERR;

	pkg = (struct ice_pkg_hdr *)buf;
	state = ice_verify_pkg(pkg, len);
	if (state) {
		DNPRINTF(ICE_DBG_INIT, "failed to verify pkg (err: %d)\n",
		    state);
		return state;
	}

	/* initialize package info */
	state = ice_init_pkg_info(hw, pkg);
	if (state)
		return state;

	/* For packages with signing segments, must be a matching segment */
	if (hw->pkg_has_signing_seg)
		if (!ice_match_signing_seg(pkg, hw->pkg_seg_id,
					   hw->pkg_sign_type))
			return ICE_DDP_PKG_ERR;

	/* before downloading the package, check package version for
	 * compatibility with driver
	 */
	state = ice_chk_pkg_compat(hw, pkg, &seg);
	if (state)
		return state;

	/* initialize package hints and then download package */
	ice_init_pkg_hints(hw, seg);
	state = ice_download_pkg(hw, pkg, seg);

	if (state == ICE_DDP_PKG_ALREADY_LOADED) {
		DNPRINTF(ICE_DBG_INIT,
		    "package previously loaded - no work.\n");
		already_loaded = true;
	}

	/* Get information on the package currently loaded in HW, then make sure
	 * the driver is compatible with this version.
	 */
	if (!state || state == ICE_DDP_PKG_ALREADY_LOADED) {
		state = ice_get_pkg_info(hw);
		if (!state)
			state = ice_get_ddp_pkg_state(hw, already_loaded);
	}

	if (ice_is_init_pkg_successful(state)) {
		hw->seg = seg;
		/* on successful package download update other required
		 * registers to support the package and fill HW tables
		 * with package content.
		 */
		ice_init_pkg_regs(hw);
		ice_fill_blk_tbls(hw);
		ice_get_prof_index_max(hw);
	} else {
		DNPRINTF(ICE_DBG_INIT, "package load failed, %d\n", state);
	}

	return state;
}

/**
 * ice_copy_and_init_pkg - initialize/download a copy of the package
 * @hw: pointer to the hardware structure
 * @buf: pointer to the package buffer
 * @len: size of the package buffer
 *
 * This function copies the package buffer, and then calls ice_init_pkg() to
 * initialize the copied package contents.
 *
 * The copying is necessary if the package buffer supplied is constant, or if
 * the memory may disappear shortly after calling this function.
 *
 * If the package buffer resides in the data segment and can be modified, the
 * caller is free to use ice_init_pkg() instead of ice_copy_and_init_pkg().
 *
 * However, if the package buffer needs to be copied first, such as when being
 * read from a file, the caller should use ice_copy_and_init_pkg().
 *
 * This function will first copy the package buffer, before calling
 * ice_init_pkg(). The caller is free to immediately destroy the original
 * package buffer, as the new copy will be managed by this function and
 * related routines.
 */
enum ice_ddp_state
ice_copy_and_init_pkg(struct ice_hw *hw, const uint8_t *buf, uint32_t len)
{
	enum ice_ddp_state state;
	uint8_t *buf_copy;

	if (!buf || !len)
		return ICE_DDP_PKG_ERR;

	buf_copy = (uint8_t *)ice_memdup(hw, buf, len);

	state = ice_init_pkg(hw, buf_copy, len);
	if (!ice_is_init_pkg_successful(state)) {
		/* Free the copy, since we failed to initialize the package */
		ice_free(hw, buf_copy);
	} else {
		/* Track the copied pkg so we can free it later */
		hw->pkg_copy = buf_copy;
		hw->pkg_size = len;
	}
	return state;
}

/**
 * ice_load_pkg_file - Load the DDP package file using firmware_get
 * @sc: device private softc
 *
 * Use firmware_get to load the DDP package memory and then request that
 * firmware download the package contents and program the relevant hardware
 * bits.
 *
 * This function makes a copy of the DDP package memory which is tracked in
 * the ice_hw structure. The copy will be managed and released by
 * ice_deinit_hw(). This allows the firmware reference to be immediately
 * released using firmware_put.
 */
enum ice_status
ice_load_pkg_file(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	enum ice_ddp_state state;
	uint8_t *pkg;
	size_t pkg_size;
	enum ice_status status = ICE_SUCCESS;
	uint8_t cached_layer_count;
	const char *fwname = "ice-ddp";
	int err;

	err = loadfirmware(fwname, &pkg, &pkg_size);
	if (err) {
		printf("%s: could not read firmware %s (error %d); "
		    "entering safe mode\n", sc->sc_dev.dv_xname, fwname, err);
		status = ICE_ERR_CFG;
		goto err_load_pkg;
	}

	/* Check for topology change */
	if (ice_is_bit_set(sc->feat_cap, ICE_FEATURE_TX_BALANCE)) {
		cached_layer_count = hw->num_tx_sched_layers;
		status = ice_cfg_tx_topo(sc, pkg, pkg_size);
		/* Success indicates a change was made */
		if (status == ICE_SUCCESS) {
			/* 9 -> 5 */
			if (cached_layer_count == 9)
				DPRINTF("%s: Transmit balancing feature "
				    "enabled\n", __func__);
			else
				DPRINTF("%s: Transmit balancing feature "
				    "disabled\n", __func__);
			ice_set_bit(ICE_FEATURE_TX_BALANCE, sc->feat_en);
			free(pkg, M_DEVBUF, pkg_size);
			return (status);
		} else if (status == ICE_ERR_CFG) {
			/* DDP does not support transmit balancing */
			DPRINTF("%s: DDP package does not support transmit "
			    "balancing feature - please update to the "
			    "latest DDP package and try again\n", __func__);
		}
	}

	/* Copy and download the pkg contents */
	state = ice_copy_and_init_pkg(hw, (const uint8_t *)pkg, pkg_size);

	/* Release the firmware reference */
	free(pkg, M_DEVBUF, pkg_size);

	/* Check the active DDP package version and log a message */
	ice_log_pkg_init(sc, state);

	if (ice_is_init_pkg_successful(state))
		return (ICE_ERR_ALREADY_EXISTS);

err_load_pkg:
	/* Place the driver into safe mode */
	ice_zero_bitmap(sc->feat_cap, ICE_FEATURE_COUNT);
	ice_zero_bitmap(sc->feat_en, ICE_FEATURE_COUNT);
	ice_set_bit(ICE_FEATURE_SAFE_MODE, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_SAFE_MODE, sc->feat_en);

	return (status);
}

/**
 * ice_aq_set_event_mask
 * @hw: pointer to the HW struct
 * @port_num: port number of the physical function
 * @mask: event mask to be set
 * @cd: pointer to command details structure or NULL
 *
 * Set event mask (0x0613)
 */
enum ice_status
ice_aq_set_event_mask(struct ice_hw *hw, uint8_t port_num, uint16_t mask,
		      struct ice_sq_cd *cd)
{
	struct ice_aqc_set_event_mask *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.set_event_mask;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_event_mask);

	cmd->lport_num = port_num;

	cmd->event_mask = htole16(mask);
	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_init_link_events - Initialize Link Status Events mask
 * @sc: the device softc
 *
 * Initialize the Link Status Events mask to disable notification of link
 * events we don't care about in software. Also request that link status
 * events be enabled.
 */
int
ice_init_link_events(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	uint16_t wanted_events;

	/* Set the bits for the events that we want to be notified by */
	wanted_events = (ICE_AQ_LINK_EVENT_UPDOWN |
			 ICE_AQ_LINK_EVENT_MEDIA_NA |
			 ICE_AQ_LINK_EVENT_MODULE_QUAL_FAIL);

	/* request that every event except the wanted events be masked */
	status = ice_aq_set_event_mask(hw, hw->port_info->lport, ~wanted_events, NULL);
	if (status) {
		printf("%s: Failed to set link status event mask, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		return (EIO);
	}

	/* Request link info with the LSE bit set to enable link status events */
	status = ice_aq_get_link_info(hw->port_info, true, NULL, NULL);
	if (status) {
		printf("%s: Failed to enable link status events, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		return (EIO);
	}

	return (0);
}

/**
 * ice_nvm_version_str - Format the NVM version information into a sbuf
 * @hw: device hw structure
 * @buf: string buffer to store version string
 *
 * Formats the NVM information including firmware version, API version, NVM
 * version, the EETRACK id, and OEM specific version information into a string
 * buffer.
 */
const char *
ice_nvm_version_str(struct ice_hw *hw, char *buf, size_t bufsize)
{
	struct ice_nvm_info *nvm = &hw->flash.nvm;
	struct ice_orom_info *orom = &hw->flash.orom;
	struct ice_netlist_info *netlist = &hw->flash.netlist;

	/* Note that the netlist versions are stored in packed Binary Coded
	 * Decimal format. The use of '%x' will correctly display these as
	 * decimal numbers. This works because every 4 bits will be displayed
	 * as a hexadecimal digit, and the BCD format will only use the values
	 * 0-9.
	 */
	snprintf(buf, bufsize,
		    "fw %u.%u.%u api %u.%u nvm %x.%02x etid %08x netlist %x.%x.%x-%x.%x.%x.%04x oem %u.%u.%u",
		    hw->fw_maj_ver, hw->fw_min_ver, hw->fw_patch,
		    hw->api_maj_ver, hw->api_min_ver,
		    nvm->major, nvm->minor, nvm->eetrack,
		    netlist->major, netlist->minor,
		    netlist->type >> 16, netlist->type & 0xFFFF,
		    netlist->rev, netlist->cust_ver, netlist->hash,
		    orom->major, orom->build, orom->patch);

	return buf;
}

/**
 * ice_print_nvm_version - Print the NVM info to the kernel message log
 * @sc: the device softc structure
 *
 * Format and print an NVM version string using ice_nvm_version_str().
 */
void
ice_print_nvm_version(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	char os_pkg[ICE_PKG_NAME_SIZE];
	char buf[512];

	ice_os_pkg_version_str(hw, os_pkg, sizeof(os_pkg));

	printf("%s: %s%s%s, address %s\n", sc->sc_dev.dv_xname,
	    ice_nvm_version_str(hw, buf, sizeof(buf)),
	    os_pkg[0] ? " ddp " : "", os_pkg[0] ? os_pkg : "",
	    ether_sprintf(hw->port_info->mac.perm_addr));
}

/**
 * ice_setup_scctx - Setup the softc context structure; in the FreeBSD
 * driver this function sets up a context used by iflib. Instead, the
 * OpenBSD driver uses it to initialize softc fields which depend on
 * driver mode and hw capabilities.
 */
void
ice_setup_scctx(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	bool safe_mode, recovery_mode, have_rss;
	int i;

	safe_mode = ice_is_bit_set(sc->feat_en, ICE_FEATURE_SAFE_MODE);
	recovery_mode = ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE);
	have_rss = ice_is_bit_set(sc->feat_en, ICE_FEATURE_RSS);

	/*
	 * If the driver loads in Safe mode or Recovery mode, limit iflib to
	 * a single queue pair.
	 */
	if (safe_mode || recovery_mode || !have_rss) {
		sc->isc_ntxqsets_max = 1;
		sc->isc_nrxqsets_max = 1;
	} else {
		sc->isc_ntxqsets_max = hw->func_caps.common_cap.num_txq;
		sc->isc_nrxqsets_max = hw->func_caps.common_cap.num_rxq;
	}

	sc->isc_tx_nsegments = ICE_MAX_TX_SEGS;
	sc->isc_tx_tso_segments_max = ICE_MAX_TSO_SEGS;
	sc->isc_tx_tso_size_max = ICE_TSO_SIZE;
	sc->isc_tx_tso_segsize_max = ICE_MAX_DMA_SEG_SIZE;
#if 0
	/*
	 * If the driver loads in recovery mode, disable Tx/Rx functionality
	 */
	if (recovery_mode)
		scctx->isc_txrx = &ice_recovery_txrx;
	else
		scctx->isc_txrx = &ice_txrx;
#endif
	for (i = 0; i < nitems(sc->isc_ntxd); i++)
		sc->isc_ntxd[i] = ICE_DEFAULT_DESC_COUNT;
	for (i = 0; i < nitems(sc->isc_nrxd); i++)
		sc->isc_nrxd[i] = ICE_DEFAULT_DESC_COUNT;


} /* ice_setup_scctx */

/**
 * ice_resmgr_init - Initialize a resource manager structure
 * @resmgr: structure to track the resource manager state
 * @num_res: the maximum number of resources it can assign
 *
 * Initialize the state of a resource manager structure, allocating space to
 * assign up to the requested number of resources. Uses bit strings to track
 * which resources have been assigned. This type of resmgr is intended to be
 * used for tracking LAN queue assignments between VSIs.
 */
int
ice_resmgr_init(struct ice_resmgr *resmgr, uint16_t num_res)
{
	resmgr->resources = ice_bit_alloc(num_res);
	if (resmgr->resources == NULL)
		return (ENOMEM);

	resmgr->num_res = num_res;
	resmgr->contig_only = false;
	return (0);
}

/**
 * ice_resmgr_init_contig_only - Initialize a resource manager structure
 * @resmgr: structure to track the resource manager state
 * @num_res: the maximum number of resources it can assign
 *
 * Functions similarly to ice_resmgr_init(), but the resulting resmgr structure
 * will only allow contiguous allocations. This type of resmgr is intended to
 * be used with tracking device MSI-X interrupt allocations.
 */
int
ice_resmgr_init_contig_only(struct ice_resmgr *resmgr, uint16_t num_res)
{
	int error;

	error = ice_resmgr_init(resmgr, num_res);
	if (error)
		return (error);

	resmgr->contig_only = true;
	return (0);
}

/**
 * ice_resmgr_destroy - Deallocate memory associated with a resource manager
 * @resmgr: resource manager structure
 *
 * De-allocates the bit string associated with this resource manager. It is
 * expected that this function will not be called until all of the assigned
 * resources have been released.
 */
void
ice_resmgr_destroy(struct ice_resmgr *resmgr)
{
	if (resmgr->resources != NULL) {
		int set;

		set = ice_bit_count(resmgr->resources, 0, resmgr->num_res);
		KASSERT(set == 0);

		free(resmgr->resources, M_DEVBUF,
		    ice_bitstr_size(resmgr->num_res));
		resmgr->resources = NULL;
	}
	resmgr->num_res = 0;
}

/**
 * ice_resmgr_assign_contiguous - Assign contiguous mapping of resources
 * @resmgr: resource manager structure
 * @idx: memory to store mapping, at least num_res wide
 * @num_res: the number of resources to assign
 *
 * Assign num_res number of contiguous resources into the idx mapping. On
 * success, idx will be updated to map each index to a PF resource.
 *
 * This function guarantees that the resource mapping will be contiguous, and
 * will fail if that is not possible.
 */
int
ice_resmgr_assign_contiguous(struct ice_resmgr *resmgr, uint16_t *idx,
    uint16_t num_res)
{
	int start, i;

	ice_bit_ffc_area(resmgr->resources, resmgr->num_res, num_res, &start);
	if (start < 0)
		return (ENOSPC);

	/* Set each bit and update the index array */
	for (i = 0; i < num_res; i++) {
		ice_bit_set(resmgr->resources, start + i);
		idx[i] = start + i;
	}

	return (0);
}

/**
 * ice_alloc_intr_tracking - Setup interrupt tracking structures
 * @sc: device softc structure
 *
 * Sets up the resource manager for keeping track of interrupt allocations,
 * and initializes the tracking maps for the PF's interrupt allocations.
 *
 * Unlike the scheme for queues, this is done in one step since both the
 * manager and the maps both have the same lifetime.
 *
 * @returns 0 on success, or an error code on failure.
 */
int
ice_alloc_intr_tracking(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	size_t nvec;
	int err, i;

	nvec = hw->func_caps.common_cap.num_msix_vectors;

	/* Initialize the interrupt allocation manager */
	err = ice_resmgr_init_contig_only(&sc->dev_imgr, nvec);
	if (err) {
		printf("%s: Unable to initialize PF interrupt manager: "
		    "error %d\n", sc->sc_dev.dv_xname, err);
		return (err);
	}

	/* Allocate PF interrupt mapping storage */
	sc->pf_imap = (uint16_t *)mallocarray(nvec, sizeof(uint16_t),
	    M_DEVBUF, M_NOWAIT);
	if (sc->pf_imap == NULL) {
		printf("%s: Unable to allocate PF imap memory\n",
		    sc->sc_dev.dv_xname);
		err = ENOMEM;
		goto free_imgr;
	}
#if 0
	sc->rdma_imap = (uint16_t *)mallocarray(nvec, sizeof(uint16_t),
	    M_DEVBUF, M_NOWAIT);
	if (sc->sc_rdma_imap == NULL) {
		printf("%s: Unable to allocate RDMA imap memory\n",
		    sc->sc_dev.dv_xname);
		err = ENOMEM;
		goto free_pf_imap;
	}
#endif
	for (i = 0; i < nvec; i++) {
		sc->pf_imap[i] = ICE_INVALID_RES_IDX;
#if 0
		sc->rdma_imap[i] = ICE_INVALID_RES_IDX;
#endif
	}

	return (0);
#if 0
free_pf_imap:
	free(sc->pf_imap, M_DEVBUF, nvec * sizeof(uint16_t));
	sc->pf_imap = NULL;
#endif
free_imgr:
	ice_resmgr_destroy(&sc->dev_imgr);
	return (err);
}

/**
 * ice_resmgr_release_map - Release previously assigned resource mapping
 * @resmgr: the resource manager structure
 * @idx: previously assigned resource mapping
 * @num_res: number of resources in the mapping
 *
 * Clears the assignment of each resource in the provided resource index.
 * Updates the idx to indicate that each of the virtual indexes have
 * invalid resource mappings by assigning them to ICE_INVALID_RES_IDX.
 */
void
ice_resmgr_release_map(struct ice_resmgr *resmgr, uint16_t *idx,
    uint16_t num_res)
{
	int i;

	for (i = 0; i < num_res; i++) {
		if (idx[i] < resmgr->num_res)
			ice_bit_clear(resmgr->resources, idx[i]);
		idx[i] = ICE_INVALID_RES_IDX;
	}
}

/**
 * ice_free_intr_tracking - Free PF interrupt tracking structures
 * @sc: device softc structure
 *
 * Frees the interrupt resource allocation manager and the PF's owned maps.
 *
 * VF maps are released when the owning VF's are destroyed, which should always
 * happen before this function is called.
 */
void
ice_free_intr_tracking(struct ice_softc *sc)
{
	if (sc->pf_imap) {
		ice_resmgr_release_map(&sc->dev_imgr, sc->pf_imap,
		    sc->lan_vectors);
		free(sc->pf_imap, M_DEVBUF,
		    sizeof(uint16_t) * sc->lan_vectors);
		sc->pf_imap = NULL;
	}
#if 0
	if (sc->rdma_imap) {
		ice_resmgr_release_map(&sc->dev_imgr, sc->rdma_imap,
		    sc->lan_vectors);
		free(sc->rdma_imap, M_DEVBUF,
		    sizeof(uint16_t) * sc->lan_vectors);
		sc->rdma_imap = NULL;
	}
#endif
	ice_resmgr_destroy(&sc->dev_imgr);

	ice_resmgr_destroy(&sc->os_imgr);
}

/**
 * ice_setup_vsi_common - Common VSI setup for both dynamic and static VSIs
 * @sc: the device private softc structure
 * @vsi: the VSI to setup
 * @type: the VSI type of the new VSI
 * @idx: the index in the all_vsi array to use
 * @dynamic: whether this VSI memory was dynamically allocated
 *
 * Perform setup for a VSI that is common to both dynamically allocated VSIs
 * and the static PF VSI which is embedded in the softc structure.
 */
void
ice_setup_vsi_common(struct ice_softc *sc, struct ice_vsi *vsi,
		     enum ice_vsi_type type, int idx, bool dynamic)
{
	struct ice_hw *hw = &sc->hw;

	/* Store important values in VSI struct */
	vsi->type = type;
	vsi->sc = sc;
	vsi->idx = idx;
	sc->all_vsi[idx] = vsi;
	vsi->dynamic = dynamic;

	/* Set default mirroring rule information */
	vsi->rule_mir_ingress = ICE_INVAL_MIRROR_RULE_ID;
	vsi->rule_mir_egress = ICE_INVAL_MIRROR_RULE_ID;
#if 0
	/* Setup the VSI tunables now */
	ice_add_vsi_tunables(vsi, sc->vsi_sysctls);
#endif
	vsi->mbuf_sz = MCLBYTES + ETHER_ALIGN;
	vsi->max_frame_size = hw->port_info->phy.link_info.max_frame_size;
}

/**
 * ice_setup_pf_vsi - Setup the PF VSI
 * @sc: the device private softc
 *
 * Setup the PF VSI structure which is embedded as sc->pf_vsi in the device
 * private softc. Unlike other VSIs, the PF VSI memory is allocated as part of
 * the softc memory, instead of being dynamically allocated at creation.
 */
void
ice_setup_pf_vsi(struct ice_softc *sc)
{
	ice_setup_vsi_common(sc, &sc->pf_vsi, ICE_VSI_PF, 0, false);
}

/**
 * ice_alloc_vsi_qmap
 * @vsi: VSI structure
 * @max_tx_queues: Number of transmit queues to identify
 * @max_rx_queues: Number of receive queues to identify
 *
 * Allocates a max_[t|r]x_queues array of words for the VSI where each
 * word contains the index of the queue it represents.  In here, all
 * words are initialized to an index of ICE_INVALID_RES_IDX, indicating
 * all queues for this VSI are not yet assigned an index and thus,
 * not ready for use.
 *
 * Returns an error code on failure.
 */
int
ice_alloc_vsi_qmap(struct ice_vsi *vsi, const int max_tx_queues,
		   const int max_rx_queues)
{
	struct ice_softc *sc = vsi->sc;
	int i;

	KASSERT(max_tx_queues > 0);
	KASSERT(max_rx_queues > 0);

	/* Allocate Tx queue mapping memory */
	vsi->tx_qmap = (uint16_t *)mallocarray(max_tx_queues, sizeof(uint16_t),
	    M_DEVBUF, M_WAITOK);
	if (!vsi->tx_qmap) {
		printf("%s: Unable to allocate Tx qmap memory\n",
		    sc->sc_dev.dv_xname);
		return (ENOMEM);
	}

	/* Allocate Rx queue mapping memory */
	vsi->rx_qmap = (uint16_t *) mallocarray(max_rx_queues, sizeof(uint16_t),
	    M_DEVBUF, M_WAITOK);
	if (!vsi->rx_qmap) {
		printf("%s: Unable to allocate Rx qmap memory\n",
		    sc->sc_dev.dv_xname);
		goto free_tx_qmap;
	}

	/* Mark every queue map as invalid to start with */
	for (i = 0; i < max_tx_queues; i++)
		vsi->tx_qmap[i] = ICE_INVALID_RES_IDX;
	for (i = 0; i < max_rx_queues; i++)
		vsi->rx_qmap[i] = ICE_INVALID_RES_IDX;

	return 0;

free_tx_qmap:
	free(vsi->tx_qmap, M_DEVBUF, max_tx_queues * sizeof(uint16_t));
	vsi->tx_qmap = NULL;

	return (ENOMEM);
}

/**
 * ice_vsig_prof_id_count - count profiles in a VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: VSIG to remove the profile from
 */
uint16_t
ice_vsig_prof_id_count(struct ice_hw *hw, enum ice_block blk,
    uint16_t vsig)
{
	uint16_t idx = vsig & ICE_VSIG_IDX_M, count = 0;
	struct ice_vsig_prof *p;

	TAILQ_FOREACH(p, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst, list)
		count++;

	return count;
}

/**
 * ice_vsig_get_ref - returns number of VSIs belong to a VSIG
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsig: VSIG to query
 * @refs: pointer to variable to receive the reference count
 */
enum ice_status
ice_vsig_get_ref(struct ice_hw *hw, enum ice_block blk, uint16_t vsig,
    uint16_t *refs)
{
	uint16_t idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_vsi *ptr;

	*refs = 0;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	ptr = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	while (ptr) {
		(*refs)++;
		ptr = ptr->next_vsi;
	}

	return ICE_SUCCESS;
}

/* Key creation */

#define ICE_DC_KEY	0x1	/* don't care */
#define ICE_DC_KEYINV	0x1
#define ICE_NM_KEY	0x0	/* never match */
#define ICE_NM_KEYINV	0x0
#define ICE_0_KEY	0x1	/* match 0 */
#define ICE_0_KEYINV	0x0
#define ICE_1_KEY	0x0	/* match 1 */
#define ICE_1_KEYINV	0x1

/**
 * ice_gen_key_word - generate 16-bits of a key/mask word
 * @val: the value
 * @valid: valid bits mask (change only the valid bits)
 * @dont_care: don't care mask
 * @nvr_mtch: never match mask
 * @key: pointer to an array of where the resulting key portion
 * @key_inv: pointer to an array of where the resulting key invert portion
 *
 * This function generates 16-bits from a 8-bit value, an 8-bit don't care mask
 * and an 8-bit never match mask. The 16-bits of output are divided into 8 bits
 * of key and 8 bits of key invert.
 *
 *     '0' =    b01, always match a 0 bit
 *     '1' =    b10, always match a 1 bit
 *     '?' =    b11, don't care bit (always matches)
 *     '~' =    b00, never match bit
 *
 * Input:
 *          val:         b0  1  0  1  0  1
 *          dont_care:   b0  0  1  1  0  0
 *          never_mtch:  b0  0  0  0  1  1
 *          ------------------------------
 * Result:  key:        b01 10 11 11 00 00
 */
enum ice_status
ice_gen_key_word(uint8_t val, uint8_t valid, uint8_t dont_care,
    uint8_t nvr_mtch, uint8_t *key, uint8_t *key_inv)
{
	uint8_t in_key = *key, in_key_inv = *key_inv;
	uint8_t i;

	/* 'dont_care' and 'nvr_mtch' masks cannot overlap */
	if ((dont_care ^ nvr_mtch) != (dont_care | nvr_mtch))
		return ICE_ERR_CFG;

	*key = 0;
	*key_inv = 0;

	/* encode the 8 bits into 8-bit key and 8-bit key invert */
	for (i = 0; i < 8; i++) {
		*key >>= 1;
		*key_inv >>= 1;

		if (!(valid & 0x1)) { /* change only valid bits */
			*key |= (in_key & 0x1) << 7;
			*key_inv |= (in_key_inv & 0x1) << 7;
		} else if (dont_care & 0x1) { /* don't care bit */
			*key |= ICE_DC_KEY << 7;
			*key_inv |= ICE_DC_KEYINV << 7;
		} else if (nvr_mtch & 0x1) { /* never match bit */
			*key |= ICE_NM_KEY << 7;
			*key_inv |= ICE_NM_KEYINV << 7;
		} else if (val & 0x01) { /* exact 1 match */
			*key |= ICE_1_KEY << 7;
			*key_inv |= ICE_1_KEYINV << 7;
		} else { /* exact 0 match */
			*key |= ICE_0_KEY << 7;
			*key_inv |= ICE_0_KEYINV << 7;
		}

		dont_care >>= 1;
		nvr_mtch >>= 1;
		valid >>= 1;
		val >>= 1;
		in_key >>= 1;
		in_key_inv >>= 1;
	}

	return ICE_SUCCESS;
}

/**
 * ice_bits_max_set - determine if the number of bits set is within a maximum
 * @mask: pointer to the byte array which is the mask
 * @size: the number of bytes in the mask
 * @max: the max number of set bits
 *
 * This function determines if there are at most 'max' number of bits set in an
 * array. Returns true if the number for bits set is <= max or will return false
 * otherwise.
 */
bool
ice_bits_max_set(const uint8_t *mask, uint16_t size, uint16_t max)
{
	uint16_t count = 0;
	uint16_t i;

	/* check each byte */
	for (i = 0; i < size; i++) {
		/* if 0, go to next byte */
		if (!mask[i])
			continue;

		/* We know there is at least one set bit in this byte because of
		 * the above check; if we already have found 'max' number of
		 * bits set, then we can return failure now.
		 */
		if (count == max)
			return false;

		/* count the bits in this byte, checking threshold */
		count += ice_popcount16(mask[i]);
		if (count > max)
			return false;
	}

	return true;
}

/**
 * ice_set_key - generate a variable sized key with multiples of 16-bits
 * @key: pointer to where the key will be stored
 * @size: the size of the complete key in bytes (must be even)
 * @val: array of 8-bit values that makes up the value portion of the key
 * @upd: array of 8-bit masks that determine what key portion to update
 * @dc: array of 8-bit masks that make up the don't care mask
 * @nm: array of 8-bit masks that make up the never match mask
 * @off: the offset of the first byte in the key to update
 * @len: the number of bytes in the key update
 *
 * This function generates a key from a value, a don't care mask and a never
 * match mask.
 * upd, dc, and nm are optional parameters, and can be NULL:
 *	upd == NULL --> upd mask is all 1's (update all bits)
 *	dc == NULL --> dc mask is all 0's (no don't care bits)
 *	nm == NULL --> nm mask is all 0's (no never match bits)
 */
enum ice_status
ice_set_key(uint8_t *key, uint16_t size, uint8_t *val, uint8_t *upd,
    uint8_t *dc, uint8_t *nm, uint16_t off, uint16_t len)
{
	uint16_t half_size;
	uint16_t i;

	/* size must be a multiple of 2 bytes. */
	if (size % 2)
		return ICE_ERR_CFG;
	half_size = size / 2;

	if (off + len > half_size)
		return ICE_ERR_CFG;

	/* Make sure at most one bit is set in the never match mask. Having more
	 * than one never match mask bit set will cause HW to consume excessive
	 * power otherwise; this is a power management efficiency check.
	 */
#define ICE_NVR_MTCH_BITS_MAX	1
	if (nm && !ice_bits_max_set(nm, len, ICE_NVR_MTCH_BITS_MAX))
		return ICE_ERR_CFG;

	for (i = 0; i < len; i++)
		if (ice_gen_key_word(val[i], upd ? upd[i] : 0xff,
				     dc ? dc[i] : 0, nm ? nm[i] : 0,
				     key + off + i, key + half_size + off + i))
			return ICE_ERR_CFG;

	return ICE_SUCCESS;
}

/**
 * ice_prof_gen_key - generate profile ID key
 * @hw: pointer to the HW struct
 * @blk: the block in which to write profile ID to
 * @ptg: packet type group (PTG) portion of key
 * @vsig: VSIG portion of key
 * @cdid: CDID portion of key
 * @flags: flag portion of key
 * @vl_msk: valid mask
 * @dc_msk: don't care mask
 * @nm_msk: never match mask
 * @key: output of profile ID key
 */
enum ice_status
ice_prof_gen_key(struct ice_hw *hw, enum ice_block blk, uint8_t ptg,
    uint16_t vsig, uint8_t cdid, uint16_t flags,
    uint8_t vl_msk[ICE_TCAM_KEY_VAL_SZ],
    uint8_t dc_msk[ICE_TCAM_KEY_VAL_SZ],
    uint8_t nm_msk[ICE_TCAM_KEY_VAL_SZ],
    uint8_t key[ICE_TCAM_KEY_SZ])
{
	struct ice_prof_id_key inkey;

	inkey.xlt1 = ptg;
	inkey.xlt2_cdid = htole16(vsig);
	inkey.flags = htole16(flags);

	switch (hw->blk[blk].prof.cdid_bits) {
	case 0:
		break;
	case 2:
#define ICE_CD_2_M 0xC000U
#define ICE_CD_2_S 14
		inkey.xlt2_cdid &= ~htole16(ICE_CD_2_M);
		inkey.xlt2_cdid |= htole16(BIT(cdid) << ICE_CD_2_S);
		break;
	case 4:
#define ICE_CD_4_M 0xF000U
#define ICE_CD_4_S 12
		inkey.xlt2_cdid &= ~htole16(ICE_CD_4_M);
		inkey.xlt2_cdid |= htole16(BIT(cdid) << ICE_CD_4_S);
		break;
	case 8:
#define ICE_CD_8_M 0xFF00U
#define ICE_CD_8_S 16
		inkey.xlt2_cdid &= ~htole16(ICE_CD_8_M);
		inkey.xlt2_cdid |= htole16(BIT(cdid) << ICE_CD_8_S);
		break;
	default:
		DNPRINTF(ICE_DBG_PKG, "%s: Error in profile config\n",
		    __func__);
		break;
	}

	return ice_set_key(key, ICE_TCAM_KEY_SZ, (uint8_t *)&inkey,
	    vl_msk, dc_msk, nm_msk, 0, ICE_TCAM_KEY_SZ / 2);
}

/**
 * ice_tcam_write_entry - write TCAM entry
 * @hw: pointer to the HW struct
 * @blk: the block in which to write profile ID to
 * @idx: the entry index to write to
 * @prof_id: profile ID
 * @ptg: packet type group (PTG) portion of key
 * @vsig: VSIG portion of key
 * @cdid: CDID portion of key
 * @flags: flag portion of key
 * @vl_msk: valid mask
 * @dc_msk: don't care mask
 * @nm_msk: never match mask
 */
enum ice_status
ice_tcam_write_entry(struct ice_hw *hw, enum ice_block blk, uint16_t idx,
		     uint8_t prof_id, uint8_t ptg, uint16_t vsig, uint8_t cdid, uint16_t flags,
		     uint8_t vl_msk[ICE_TCAM_KEY_VAL_SZ],
		     uint8_t dc_msk[ICE_TCAM_KEY_VAL_SZ],
		     uint8_t nm_msk[ICE_TCAM_KEY_VAL_SZ])
{
	struct ice_prof_tcam_entry;
	enum ice_status status;

	status = ice_prof_gen_key(hw, blk, ptg, vsig, cdid, flags, vl_msk,
				  dc_msk, nm_msk, hw->blk[blk].prof.t[idx].key);
	if (!status) {
		hw->blk[blk].prof.t[idx].addr = htole16(idx);
		hw->blk[blk].prof.t[idx].prof_id = prof_id;
	}

	return status;
}

/**
 * ice_tcam_ent_rsrc_type - get TCAM entry resource type for a block type
 * @blk: the block type
 * @rsrc_type: pointer to variable to receive the resource type
 */
bool
ice_tcam_ent_rsrc_type(enum ice_block blk, uint16_t *rsrc_type)
{
	switch (blk) {
	case ICE_BLK_RSS:
		*rsrc_type = ICE_AQC_RES_TYPE_HASH_PROF_BLDR_TCAM;
		break;
	case ICE_BLK_PE:
		*rsrc_type = ICE_AQC_RES_TYPE_QHASH_PROF_BLDR_TCAM;
		break;
	default:
		return false;
	}
	return true;
}

/**
 * ice_alloc_hw_res - allocate resource
 * @hw: pointer to the HW struct
 * @type: type of resource
 * @num: number of resources to allocate
 * @btm: allocate from bottom
 * @res: pointer to array that will receive the resources
 */
enum ice_status
ice_alloc_hw_res(struct ice_hw *hw, uint16_t type, uint16_t num, bool btm,
    uint16_t *res)
{
	struct ice_aqc_alloc_free_res_elem *buf;
	enum ice_status status;
	uint16_t buf_len;

	buf_len = ice_struct_size(buf, elem, num);
	buf = (struct ice_aqc_alloc_free_res_elem *)ice_malloc(hw, buf_len);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	/* Prepare buffer to allocate resource. */
	buf->num_elems = htole16(num);
	buf->res_type = htole16(type | ICE_AQC_RES_TYPE_FLAG_DEDICATED |
				    ICE_AQC_RES_TYPE_FLAG_IGNORE_INDEX);
	if (btm)
		buf->res_type |= htole16(ICE_AQC_RES_TYPE_FLAG_SCAN_BOTTOM);

	status = ice_aq_alloc_free_res(hw, 1, buf, buf_len,
				       ice_aqc_opc_alloc_res, NULL);
	if (status)
		goto ice_alloc_res_exit;

	memcpy(res, buf->elem, sizeof(*buf->elem) * num);

ice_alloc_res_exit:
	ice_free(hw, buf);
	return status;
}

/**
 * ice_free_hw_res - free allocated HW resource
 * @hw: pointer to the HW struct
 * @type: type of resource to free
 * @num: number of resources
 * @res: pointer to array that contains the resources to free
 */
enum ice_status
ice_free_hw_res(struct ice_hw *hw, uint16_t type, uint16_t num, uint16_t *res)
{
	struct ice_aqc_alloc_free_res_elem *buf;
	enum ice_status status;
	uint16_t buf_len;

	/* prevent overflow; all callers currently hard-code num as 1 */
	KASSERT(num == 1);

	buf_len = ice_struct_size(buf, elem, num);
	buf = (struct ice_aqc_alloc_free_res_elem *)ice_malloc(hw, buf_len);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	/* Prepare buffer to free resource. */
	buf->num_elems = htole16(num);
	buf->res_type = htole16(type);
	memcpy(buf->elem, res, sizeof(*buf->elem) * num);

	status = ice_aq_alloc_free_res(hw, num, buf, buf_len,
				       ice_aqc_opc_free_res, NULL);

	ice_free(hw, buf);
	return status;
}

/**
 * ice_free_tcam_ent - free hardware TCAM entry
 * @hw: pointer to the HW struct
 * @blk: the block from which to free the TCAM entry
 * @tcam_idx: the TCAM entry to free
 *
 * This function frees an entry in a Profile ID TCAM for a specific block.
 */
enum ice_status
ice_free_tcam_ent(struct ice_hw *hw, enum ice_block blk, uint16_t tcam_idx)
{
	uint16_t res_type;

	if (!ice_tcam_ent_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_free_hw_res(hw, res_type, 1, &tcam_idx);
}

/**
 * ice_rel_tcam_idx - release a TCAM index
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @idx: the index to release
 */
enum ice_status
ice_rel_tcam_idx(struct ice_hw *hw, enum ice_block blk, uint16_t idx)
{
	/* Masks to invoke a never match entry */
	uint8_t vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFE, 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x01, 0x00, 0x00, 0x00, 0x00 };
	enum ice_status status;

	/* write the TCAM entry */
	status = ice_tcam_write_entry(hw, blk, idx, 0, 0, 0, 0, 0, vl_msk,
				      dc_msk, nm_msk);
	if (status)
		return status;

	/* release the TCAM entry */
	status = ice_free_tcam_ent(hw, blk, idx);

	return status;
}

/**
 * ice_rem_prof_id - remove one profile from a VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @prof: pointer to profile structure to remove
 */
enum ice_status
ice_rem_prof_id(struct ice_hw *hw, enum ice_block blk,
    struct ice_vsig_prof *prof)
{
	enum ice_status status;
	uint16_t i;

	for (i = 0; i < prof->tcam_count; i++)
		if (prof->tcam[i].in_use) {
			prof->tcam[i].in_use = false;
			status = ice_rel_tcam_idx(hw, blk,
						  prof->tcam[i].tcam_idx);
			if (status)
				return ICE_ERR_HW_TABLE;
		}

	return ICE_SUCCESS;
}

/**
 * ice_rem_vsig - remove VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: the VSIG to remove
 * @chg: the change list
 */
enum ice_status
ice_rem_vsig(struct ice_hw *hw, enum ice_block blk, uint16_t vsig,
    struct ice_chs_chg_head *chg)
{
	uint16_t idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_vsi *vsi_cur;
	struct ice_vsig_prof *d, *t;

	/* remove TCAM entries */
	TAILQ_FOREACH_SAFE(d, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
	    list, t) {
		enum ice_status status;

		status = ice_rem_prof_id(hw, blk, d);
		if (status)
			return status;

		TAILQ_REMOVE(&hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
		    d, list);
		ice_free(hw, d);
	}

	/* Move all VSIS associated with this VSIG to the default VSIG */
	vsi_cur = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	/* If the VSIG has at least 1 VSI then iterate through the list
	 * and remove the VSIs before deleting the group.
	 */
	if (vsi_cur) {
		do {
			struct ice_vsig_vsi *tmp = vsi_cur->next_vsi;
			struct ice_chs_chg *p;

			p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
			if (!p)
				return ICE_ERR_NO_MEMORY;

			p->type = ICE_VSIG_REM;
			p->orig_vsig = vsig;
			p->vsig = ICE_DEFAULT_VSIG;
			p->vsi = (uint16_t)(vsi_cur - hw->blk[blk].xlt2.vsis);

			TAILQ_INSERT_HEAD(chg, p, list_entry);

			vsi_cur = tmp;
		} while (vsi_cur);
	}

	return ice_vsig_free(hw, blk, vsig);
}

/**
 * ice_rem_prof_id_vsig - remove a specific profile from a VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: VSIG to remove the profile from
 * @hdl: profile handle indicating which profile to remove
 * @chg: list to receive a record of changes
 */
enum ice_status
ice_rem_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, uint16_t vsig,
    uint64_t hdl, struct ice_chs_chg_head *chg)
{
	uint16_t idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_prof *p, *t;

	TAILQ_FOREACH_SAFE(p, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
	    list, t) {
		if (p->profile_cookie == hdl) {
			enum ice_status status;

			if (ice_vsig_prof_id_count(hw, blk, vsig) == 1)
				/* this is the last profile, remove the VSIG */
				return ice_rem_vsig(hw, blk, vsig, chg);

			status = ice_rem_prof_id(hw, blk, p);
			if (!status) {
				TAILQ_REMOVE(
				    &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
				    p, list);
				ice_free(hw, p);
			}
			return status;
		}
	}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_rem_chg_tcam_ent - remove a specific TCAM entry from change list
 * @hw: pointer to the HW struct
 * @idx: the index of the TCAM entry to remove
 * @chg: the list of change structures to search
 */
void
ice_rem_chg_tcam_ent(struct ice_hw *hw, uint16_t idx,
    struct ice_chs_chg_head *chg)
{
	struct ice_chs_chg *pos, *tmp;

	TAILQ_FOREACH_SAFE(pos, chg, list_entry, tmp) {
		if (pos->type == ICE_TCAM_ADD && pos->tcam_idx == idx) {
			TAILQ_REMOVE(chg, pos, list_entry);
			ice_free(hw, pos);
		}
	}
}

/**
 * ice_alloc_tcam_ent - allocate hardware TCAM entry
 * @hw: pointer to the HW struct
 * @blk: the block to allocate the TCAM for
 * @btm: true to allocate from bottom of table, false to allocate from top
 * @tcam_idx: pointer to variable to receive the TCAM entry
 *
 * This function allocates a new entry in a Profile ID TCAM for a specific
 * block.
 */
enum ice_status
ice_alloc_tcam_ent(struct ice_hw *hw, enum ice_block blk, bool btm,
		   uint16_t *tcam_idx)
{
	uint16_t res_type;

	if (!ice_tcam_ent_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_alloc_hw_res(hw, res_type, 1, btm, tcam_idx);
}

/**
 * ice_prof_tcam_ena_dis - add enable or disable TCAM change
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @enable: true to enable, false to disable
 * @vsig: the VSIG of the TCAM entry
 * @tcam: pointer the TCAM info structure of the TCAM to disable
 * @chg: the change list
 *
 * This function appends an enable or disable TCAM entry in the change log
 */
enum ice_status
ice_prof_tcam_ena_dis(struct ice_hw *hw, enum ice_block blk, bool enable,
		      uint16_t vsig, struct ice_tcam_inf *tcam,
		      struct ice_chs_chg_head *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;
	uint8_t vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0x00, 0x00, 0x00 };
	uint8_t nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x00, 0x00, 0x00, 0x00, 0x00 };

	/* if disabling, free the TCAM */
	if (!enable) {
		status = ice_rel_tcam_idx(hw, blk, tcam->tcam_idx);

		/* if we have already created a change for this TCAM entry, then
		 * we need to remove that entry, in order to prevent writing to
		 * a TCAM entry we no longer will have ownership of.
		 */
		ice_rem_chg_tcam_ent(hw, tcam->tcam_idx, chg);
		tcam->tcam_idx = 0;
		tcam->in_use = 0;
		return status;
	}

	/* for re-enabling, reallocate a TCAM */
	status = ice_alloc_tcam_ent(hw, blk, true, &tcam->tcam_idx);
	if (status)
		return status;

	/* add TCAM to change list */
	p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
	if (!p)
		return ICE_ERR_NO_MEMORY;

	status = ice_tcam_write_entry(hw, blk, tcam->tcam_idx, tcam->prof_id,
				      tcam->ptg, vsig, 0, 0, vl_msk, dc_msk,
				      nm_msk);
	if (status)
		goto err_ice_prof_tcam_ena_dis;

	tcam->in_use = 1;

	p->type = ICE_TCAM_ADD;
	p->add_tcam_idx = true;
	p->prof_id = tcam->prof_id;
	p->ptg = tcam->ptg;
	p->vsig = 0;
	p->tcam_idx = tcam->tcam_idx;

	/* log change */
	TAILQ_INSERT_HEAD(chg, p, list_entry);

	return ICE_SUCCESS;

err_ice_prof_tcam_ena_dis:
	ice_free(hw, p);
	return status;
}

/**
 * ice_adj_prof_priorities - adjust profile based on priorities
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: the VSIG for which to adjust profile priorities
 * @chg: the change list
 */
enum ice_status
ice_adj_prof_priorities(struct ice_hw *hw, enum ice_block blk, uint16_t vsig,
    struct ice_chs_chg_head *chg)
{
	ice_declare_bitmap(ptgs_used, ICE_XLT1_CNT);
	enum ice_status status = ICE_SUCCESS;
	struct ice_vsig_prof *t;
	uint16_t idx;

	ice_zero_bitmap(ptgs_used, ICE_XLT1_CNT);
	idx = vsig & ICE_VSIG_IDX_M;

	/* Priority is based on the order in which the profiles are added. The
	 * newest added profile has highest priority and the oldest added
	 * profile has the lowest priority. Since the profile property list for
	 * a VSIG is sorted from newest to oldest, this code traverses the list
	 * in order and enables the first of each PTG that it finds (that is not
	 * already enabled); it also disables any duplicate PTGs that it finds
	 * in the older profiles (that are currently enabled).
	 */

	TAILQ_FOREACH(t, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst, list) {
		uint16_t i;

		for (i = 0; i < t->tcam_count; i++) {
			bool used;

			/* Scan the priorities from newest to oldest.
			 * Make sure that the newest profiles take priority.
			 */
			used = ice_is_bit_set(ptgs_used, t->tcam[i].ptg);

			if (used && t->tcam[i].in_use) {
				/* need to mark this PTG as never match, as it
				 * was already in use and therefore duplicate
				 * (and lower priority)
				 */
				status = ice_prof_tcam_ena_dis(hw, blk, false,
							       vsig,
							       &t->tcam[i],
							       chg);
				if (status)
					return status;
			} else if (!used && !t->tcam[i].in_use) {
				/* need to enable this PTG, as it in not in use
				 * and not enabled (highest priority)
				 */
				status = ice_prof_tcam_ena_dis(hw, blk, true,
							       vsig,
							       &t->tcam[i],
							       chg);
				if (status)
					return status;
			}

			/* keep track of used ptgs */
			ice_set_bit(t->tcam[i].ptg, ptgs_used);
		}
	}

	return status;
}

/**
 * ice_get_profs_vsig - get a copy of the list of profiles from a VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: VSIG from which to copy the list
 * @lst: output list
 *
 * This routine makes a copy of the list of profiles in the specified VSIG.
 */
enum ice_status
ice_get_profs_vsig(struct ice_hw *hw, enum ice_block blk, uint16_t vsig,
		   struct ice_vsig_prof_head *lst)
{
	struct ice_vsig_prof *ent1, *ent2;
	uint16_t idx = vsig & ICE_VSIG_IDX_M;

	TAILQ_FOREACH(ent1, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst, list) {
		struct ice_vsig_prof *p;

		/* copy to the input list */
		p = (struct ice_vsig_prof *)ice_memdup(hw, ent1, sizeof(*p));
		if (!p)
			goto err_ice_get_profs_vsig;

		TAILQ_INSERT_TAIL(lst, p, list);
	}

	return ICE_SUCCESS;

err_ice_get_profs_vsig:
	TAILQ_FOREACH_SAFE(ent1, lst, list, ent2) {
		TAILQ_REMOVE(lst, ent1, list);
		ice_free(hw, ent1);
	}

	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_rem_prof_from_list - remove a profile from list
 * @hw: pointer to the HW struct
 * @lst: list to remove the profile from
 * @hdl: the profile handle indicating the profile to remove
 */
enum ice_status
ice_rem_prof_from_list(struct ice_hw *hw, struct ice_vsig_prof_head *lst,
    uint64_t hdl)
{
	struct ice_vsig_prof *ent, *tmp;

	TAILQ_FOREACH_SAFE(ent, lst, list, tmp) {
		if (ent->profile_cookie == hdl) {
			TAILQ_REMOVE(lst, ent, list);
			ice_free(hw, ent);
			return ICE_SUCCESS;
		}
	}	

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_move_vsi - move VSI to another VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the VSI to move
 * @vsig: the VSIG to move the VSI to
 * @chg: the change list
 */
enum ice_status
ice_move_vsi(struct ice_hw *hw, enum ice_block blk, uint16_t vsi, uint16_t vsig,
	     struct ice_chs_chg_head *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;
	uint16_t orig_vsig;

	p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
	if (!p)
		return ICE_ERR_NO_MEMORY;

	status = ice_vsig_find_vsi(hw, blk, vsi, &orig_vsig);
	if (!status)
		status = ice_vsig_add_mv_vsi(hw, blk, vsi, vsig);

	if (status) {
		ice_free(hw, p);
		return status;
	}

	p->type = ICE_VSI_MOVE;
	p->vsi = vsi;
	p->orig_vsig = orig_vsig;
	p->vsig = vsig;

	TAILQ_INSERT_HEAD(chg, p, list_entry);

	return ICE_SUCCESS;
}

/**
 * ice_vsig_alloc - Finds a free entry and allocates a new VSIG
 * @hw: pointer to the hardware structure
 * @blk: HW block
 *
 * This function will iterate through the VSIG list and mark the first
 * unused entry for the new VSIG entry as used and return that value.
 */
uint16_t
ice_vsig_alloc(struct ice_hw *hw, enum ice_block blk)
{
	uint16_t i;

	for (i = 1; i < ICE_MAX_VSIGS; i++)
		if (!hw->blk[blk].xlt2.vsig_tbl[i].in_use)
			return ice_vsig_alloc_val(hw, blk, i);

	return ICE_DEFAULT_VSIG;
}

/**
 * ice_has_prof_vsig - check to see if VSIG has a specific profile
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @vsig: VSIG to check against
 * @hdl: profile handle
 */
bool
ice_has_prof_vsig(struct ice_hw *hw, enum ice_block blk, uint16_t vsig,
    uint64_t hdl)
{
	uint16_t idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_prof *ent;

	TAILQ_FOREACH(ent, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst, list) {
		if (ent->profile_cookie == hdl)
			return true;
	}

	DNPRINTF(ICE_DBG_INIT,
	    "%s: Characteristic list for VSI group %d not found\n",
	    __func__, vsig);

	return false;
}

/**
 * ice_search_prof_id - Search for a profile tracking ID
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 *
 * This will search for a profile tracking ID which was previously added.
 * The profile map lock should be held before calling this function.
 */
struct ice_prof_map *
ice_search_prof_id(struct ice_hw *hw, enum ice_block blk, uint64_t id)
{
	struct ice_prof_map *entry = NULL;
	struct ice_prof_map *map;

	TAILQ_FOREACH(map, &hw->blk[blk].es.prof_map, list) {
		if (map->profile_cookie == id) {
			entry = map;
			break;
		}
	}

	return entry;
}

/**
 * ice_add_prof_id_vsig - add profile to VSIG
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsig: the VSIG to which this profile is to be added
 * @hdl: the profile handle indicating the profile to add
 * @rev: true to add entries to the end of the list
 * @chg: the change list
 */
enum ice_status
ice_add_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, uint16_t vsig,
    uint64_t hdl, bool rev, struct ice_chs_chg_head *chg)
{
	/* Masks that ignore flags */
	uint8_t vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0x00, 0x00, 0x00 };
	uint8_t nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
	enum ice_status status = ICE_SUCCESS;
	struct ice_prof_map *map;
	struct ice_vsig_prof *t;
	struct ice_chs_chg *p;
	uint16_t vsig_idx, i;

	/* Error, if this VSIG already has this profile */
	if (ice_has_prof_vsig(hw, blk, vsig, hdl))
		return ICE_ERR_ALREADY_EXISTS;

	/* new VSIG profile structure */
	t = (struct ice_vsig_prof *)ice_malloc(hw, sizeof(*t));
	if (!t)
		return ICE_ERR_NO_MEMORY;
#if 0
	ice_acquire_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	/* Get the details on the profile specified by the handle ID */
	map = ice_search_prof_id(hw, blk, hdl);
	if (!map) {
		status = ICE_ERR_DOES_NOT_EXIST;
		goto err_ice_add_prof_id_vsig;
	}

	t->profile_cookie = map->profile_cookie;
	t->prof_id = map->prof_id;
	t->tcam_count = map->ptg_cnt;

	/* create TCAM entries */
	for (i = 0; i < map->ptg_cnt; i++) {
		uint16_t tcam_idx;

		/* add TCAM to change list */
		p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
		if (!p) {
			status = ICE_ERR_NO_MEMORY;
			goto err_ice_add_prof_id_vsig;
		}

		/* allocate the TCAM entry index */
		status = ice_alloc_tcam_ent(hw, blk, true, &tcam_idx);
		if (status) {
			ice_free(hw, p);
			goto err_ice_add_prof_id_vsig;
		}

		t->tcam[i].ptg = map->ptg[i];
		t->tcam[i].prof_id = map->prof_id;
		t->tcam[i].tcam_idx = tcam_idx;
		t->tcam[i].in_use = true;

		p->type = ICE_TCAM_ADD;
		p->add_tcam_idx = true;
		p->prof_id = t->tcam[i].prof_id;
		p->ptg = t->tcam[i].ptg;
		p->vsig = vsig;
		p->tcam_idx = t->tcam[i].tcam_idx;

		/* write the TCAM entry */
		status = ice_tcam_write_entry(hw, blk, t->tcam[i].tcam_idx,
					      t->tcam[i].prof_id,
					      t->tcam[i].ptg, vsig, 0, 0,
					      vl_msk, dc_msk, nm_msk);
		if (status) {
			ice_free(hw, p);
			goto err_ice_add_prof_id_vsig;
		}

		/* log change */
		TAILQ_INSERT_HEAD(chg, p, list_entry);
	}

	/* add profile to VSIG */
	vsig_idx = vsig & ICE_VSIG_IDX_M;
	if (rev)
		TAILQ_INSERT_TAIL(
		    &hw->blk[blk].xlt2.vsig_tbl[vsig_idx].prop_lst, t, list);
	else
		TAILQ_INSERT_HEAD(
		    &hw->blk[blk].xlt2.vsig_tbl[vsig_idx].prop_lst, t, list);
#if 0
	ice_release_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	return status;

err_ice_add_prof_id_vsig:
#if 0
	ice_release_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	/* let caller clean up the change list */
	ice_free(hw, t);
	return status;
}

/**
 * ice_create_vsig_from_lst - create a new VSIG with a list of profiles
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the initial VSI that will be in VSIG
 * @lst: the list of profile that will be added to the VSIG
 * @new_vsig: return of new VSIG
 * @chg: the change list
 */
enum ice_status
ice_create_vsig_from_lst(struct ice_hw *hw, enum ice_block blk, uint16_t vsi,
			 struct ice_vsig_prof_head *lst, uint16_t *new_vsig,
			 struct ice_chs_chg_head *chg)
{
	struct ice_vsig_prof *t;
	enum ice_status status;
	uint16_t vsig;

	vsig = ice_vsig_alloc(hw, blk);
	if (!vsig)
		return ICE_ERR_HW_TABLE;

	status = ice_move_vsi(hw, blk, vsi, vsig, chg);
	if (status)
		return status;

	TAILQ_FOREACH(t, lst, list) {
		/* Reverse the order here since we are copying the list */
		status = ice_add_prof_id_vsig(hw, blk, vsig, t->profile_cookie,
					      true, chg);
		if (status)
			return status;
	}

	*new_vsig = vsig;

	return ICE_SUCCESS;
}

/**
 * ice_pkg_buf_alloc
 * @hw: pointer to the HW structure
 *
 * Allocates a package buffer and returns a pointer to the buffer header.
 * Note: all package contents must be in Little Endian form.
 */
struct ice_buf_build *
ice_pkg_buf_alloc(struct ice_hw *hw)
{
	struct ice_buf_build *bld;
	struct ice_buf_hdr *buf;

	bld = (struct ice_buf_build *)ice_malloc(hw, sizeof(*bld));
	if (!bld)
		return NULL;

	buf = (struct ice_buf_hdr *)bld;
	buf->data_end = htole16(offsetof(struct ice_buf_hdr, section_entry));
	return bld;
}

/*
 * Define a macro that will align a pointer to point to the next memory address
 * that falls on the given power of 2 (i.e., 2, 4, 8, 16, 32, 64...). For
 * example, given the variable pointer = 0x1006, then after the following call:
 *
 *      pointer = ICE_ALIGN(pointer, 4)
 *
 * ... the value of pointer would equal 0x1008, since 0x1008 is the next
 * address after 0x1006 which is divisible by 4.
 */
#define ICE_ALIGN(ptr, align)	(((ptr) + ((align) - 1)) & ~((align) - 1))

/**
 * ice_pkg_buf_alloc_section
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 * @type: the section type value
 * @size: the size of the section to reserve (in bytes)
 *
 * Reserves memory in the buffer for a section's content and updates the
 * buffers' status accordingly. This routine returns a pointer to the first
 * byte of the section start within the buffer, which is used to fill in the
 * section contents.
 * Note: all package contents must be in Little Endian form.
 */
void *
ice_pkg_buf_alloc_section(struct ice_buf_build *bld, uint32_t type,
    uint16_t size)
{
	struct ice_buf_hdr *buf;
	uint16_t sect_count;
	uint16_t data_end;

	if (!bld || !type || !size)
		return NULL;

	buf = (struct ice_buf_hdr *)&bld->buf;

	/* check for enough space left in buffer */
	data_end = le16toh(buf->data_end);

	/* section start must align on 4 byte boundary */
	data_end = ICE_ALIGN(data_end, 4);

	if ((data_end + size) > ICE_MAX_S_DATA_END)
		return NULL;

	/* check for more available section table entries */
	sect_count = le16toh(buf->section_count);
	if (sect_count < bld->reserved_section_table_entries) {
		void *section_ptr = ((uint8_t *)buf) + data_end;

		buf->section_entry[sect_count].offset = htole16(data_end);
		buf->section_entry[sect_count].size = htole16(size);
		buf->section_entry[sect_count].type = htole32(type);

		data_end += size;
		buf->data_end = htole16(data_end);

		buf->section_count = htole16(sect_count + 1);
		return section_ptr;
	}

	/* no free section table entries */
	return NULL;
}

/**
 * ice_pkg_buf_reserve_section
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 * @count: the number of sections to reserve
 *
 * Reserves one or more section table entries in a package buffer. This routine
 * can be called multiple times as long as they are made before calling
 * ice_pkg_buf_alloc_section(). Once ice_pkg_buf_alloc_section()
 * is called once, the number of sections that can be allocated will not be able
 * to be increased; not using all reserved sections is fine, but this will
 * result in some wasted space in the buffer.
 * Note: all package contents must be in Little Endian form.
 */
int
ice_pkg_buf_reserve_section(struct ice_buf_build *bld, uint16_t count)
{
	struct ice_buf_hdr *buf;
	uint16_t section_count;
	uint16_t data_end;

	if (!bld)
		return ICE_ERR_PARAM;

	buf = (struct ice_buf_hdr *)&bld->buf;

	/* already an active section, can't increase table size */
	section_count = le16toh(buf->section_count);
	if (section_count > 0)
		return ICE_ERR_CFG;

	if (bld->reserved_section_table_entries + count > ICE_MAX_S_COUNT)
		return ICE_ERR_CFG;
	bld->reserved_section_table_entries += count;

	data_end = le16toh(buf->data_end) +
		FLEX_ARRAY_SIZE(buf, section_entry, count);
	buf->data_end = htole16(data_end);

	return 0;
}

/**
 * ice_pkg_buf_get_active_sections
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Returns the number of active sections. Before using the package buffer
 * in an update package command, the caller should make sure that there is at
 * least one active section - otherwise, the buffer is not legal and should
 * not be used.
 * Note: all package contents must be in Little Endian form.
 */
uint16_t
ice_pkg_buf_get_active_sections(struct ice_buf_build *bld)
{
	struct ice_buf_hdr *buf;

	if (!bld)
		return 0;

	buf = (struct ice_buf_hdr *)&bld->buf;
	return le16toh(buf->section_count);
}

/**
 * ice_pkg_buf
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Return a pointer to the buffer's header
 */
struct ice_buf *
ice_pkg_buf(struct ice_buf_build *bld)
{
	if (bld)
		return &bld->buf;

	return NULL;
}

static const uint32_t ice_sect_lkup[ICE_BLK_COUNT][ICE_SECT_COUNT] = {
	/* SWITCH */
	{
		ICE_SID_XLT0_SW,
		ICE_SID_XLT_KEY_BUILDER_SW,
		ICE_SID_XLT1_SW,
		ICE_SID_XLT2_SW,
		ICE_SID_PROFID_TCAM_SW,
		ICE_SID_PROFID_REDIR_SW,
		ICE_SID_FLD_VEC_SW,
		ICE_SID_CDID_KEY_BUILDER_SW,
		ICE_SID_CDID_REDIR_SW
	},

	/* ACL */
	{
		ICE_SID_XLT0_ACL,
		ICE_SID_XLT_KEY_BUILDER_ACL,
		ICE_SID_XLT1_ACL,
		ICE_SID_XLT2_ACL,
		ICE_SID_PROFID_TCAM_ACL,
		ICE_SID_PROFID_REDIR_ACL,
		ICE_SID_FLD_VEC_ACL,
		ICE_SID_CDID_KEY_BUILDER_ACL,
		ICE_SID_CDID_REDIR_ACL
	},

	/* FD */
	{
		ICE_SID_XLT0_FD,
		ICE_SID_XLT_KEY_BUILDER_FD,
		ICE_SID_XLT1_FD,
		ICE_SID_XLT2_FD,
		ICE_SID_PROFID_TCAM_FD,
		ICE_SID_PROFID_REDIR_FD,
		ICE_SID_FLD_VEC_FD,
		ICE_SID_CDID_KEY_BUILDER_FD,
		ICE_SID_CDID_REDIR_FD
	},

	/* RSS */
	{
		ICE_SID_XLT0_RSS,
		ICE_SID_XLT_KEY_BUILDER_RSS,
		ICE_SID_XLT1_RSS,
		ICE_SID_XLT2_RSS,
		ICE_SID_PROFID_TCAM_RSS,
		ICE_SID_PROFID_REDIR_RSS,
		ICE_SID_FLD_VEC_RSS,
		ICE_SID_CDID_KEY_BUILDER_RSS,
		ICE_SID_CDID_REDIR_RSS
	},

	/* PE */
	{
		ICE_SID_XLT0_PE,
		ICE_SID_XLT_KEY_BUILDER_PE,
		ICE_SID_XLT1_PE,
		ICE_SID_XLT2_PE,
		ICE_SID_PROFID_TCAM_PE,
		ICE_SID_PROFID_REDIR_PE,
		ICE_SID_FLD_VEC_PE,
		ICE_SID_CDID_KEY_BUILDER_PE,
		ICE_SID_CDID_REDIR_PE
	}
};

/**
 * ice_sect_id - returns section ID
 * @blk: block type
 * @sect: section type
 *
 * This helper function returns the proper section ID given a block type and a
 * section type.
 */
uint32_t
ice_sect_id(enum ice_block blk, enum ice_sect sect)
{
	return ice_sect_lkup[blk][sect];
}

/**
 * ice_prof_bld_es - build profile ID extraction sequence changes
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
int
ice_prof_bld_es(struct ice_hw *hw, enum ice_block blk,
		struct ice_buf_build *bld, struct ice_chs_chg_head *chgs)
{
	uint16_t vec_size = hw->blk[blk].es.fvw * sizeof(struct ice_fv_word);
	struct ice_chs_chg *tmp;
	uint16_t off;
	struct ice_pkg_es *p;
	uint32_t id;

	TAILQ_FOREACH(tmp, chgs, list_entry) {
		if (tmp->type != ICE_PTG_ES_ADD || !tmp->add_prof)
			continue;

		off = tmp->prof_id * hw->blk[blk].es.fvw;
		id = ice_sect_id(blk, ICE_VEC_TBL);
		p = (struct ice_pkg_es *)ice_pkg_buf_alloc_section(bld, id,
		    ice_struct_size(p, es, 1) + vec_size - sizeof(p->es[0]));
		if (!p)
			return ICE_ERR_MAX_LIMIT;

		p->count = htole16(1);
		p->offset = htole16(tmp->prof_id);
		memcpy(p->es, &hw->blk[blk].es.t[off], vec_size);
	}

	return 0;
}

/**
 * ice_prof_bld_tcam - build profile ID TCAM changes
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
int
ice_prof_bld_tcam(struct ice_hw *hw, enum ice_block blk,
    struct ice_buf_build *bld, struct ice_chs_chg_head *chgs)
{
	struct ice_chs_chg *tmp;
	struct ice_prof_id_section *p;
	uint32_t id;

	TAILQ_FOREACH(tmp, chgs, list_entry) {
		if (tmp->type != ICE_TCAM_ADD || !tmp->add_tcam_idx)
			continue;

		id = ice_sect_id(blk, ICE_PROF_TCAM);
		p = (struct ice_prof_id_section *)ice_pkg_buf_alloc_section(
		    bld, id, ice_struct_size(p, entry, 1));
		if (!p)
			return ICE_ERR_MAX_LIMIT;

		p->count = htole16(1);
		p->entry[0].addr = htole16(tmp->tcam_idx);
		p->entry[0].prof_id = tmp->prof_id;

		memcpy(p->entry[0].key,
		    &hw->blk[blk].prof.t[tmp->tcam_idx].key,
		    sizeof(hw->blk[blk].prof.t->key));
	}

	return 0;
}

/**
 * ice_prof_bld_xlt1 - build XLT1 changes
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
int
ice_prof_bld_xlt1(enum ice_block blk, struct ice_buf_build *bld,
		  struct ice_chs_chg_head *chgs)
{
	struct ice_chs_chg *tmp;
	struct ice_xlt1_section *p;
	uint32_t id;

	TAILQ_FOREACH(tmp, chgs, list_entry) {
		if (tmp->type != ICE_PTG_ES_ADD || !tmp->add_ptg)
			continue;

		id = ice_sect_id(blk, ICE_XLT1);
		p = (struct ice_xlt1_section *)ice_pkg_buf_alloc_section(bld,
		    id, ice_struct_size(p, value, 1));
		if (!p)
			return ICE_ERR_MAX_LIMIT;

		p->count = htole16(1);
		p->offset = htole16(tmp->ptype);
		p->value[0] = tmp->ptg;
	}

	return 0;
}

/**
 * ice_prof_bld_xlt2 - build XLT2 changes
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
int
ice_prof_bld_xlt2(enum ice_block blk, struct ice_buf_build *bld,
		  struct ice_chs_chg_head *chgs)
{
	struct ice_chs_chg *tmp;
	struct ice_xlt2_section *p;
	uint32_t id;

	TAILQ_FOREACH(tmp, chgs, list_entry) {
		if (tmp->type != ICE_VSIG_ADD &&
		    tmp->type != ICE_VSI_MOVE &&
		    tmp->type != ICE_VSIG_REM)
			continue;

		id = ice_sect_id(blk, ICE_XLT2);
		p = (struct ice_xlt2_section *)ice_pkg_buf_alloc_section(bld,
		    id, ice_struct_size(p, value, 1));
		if (!p)
			return ICE_ERR_MAX_LIMIT;

		p->count = htole16(1);
		p->offset = htole16(tmp->vsi);
		p->value[0] = htole16(tmp->vsig);
	}

	return 0;
}

/**
 * ice_aq_update_pkg
 * @hw: pointer to the hardware structure
 * @pkg_buf: the package cmd buffer
 * @buf_size: the size of the package cmd buffer
 * @last_buf: last buffer indicator
 * @error_offset: returns error offset
 * @error_info: returns error information
 * @cd: pointer to command details structure or NULL
 *
 * Update Package (0x0C42)
 */
int
ice_aq_update_pkg(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf,
    uint16_t buf_size, bool last_buf, uint32_t *error_offset,
    uint32_t *error_info, struct ice_sq_cd *cd)
{
	struct ice_aqc_download_pkg *cmd;
	struct ice_aq_desc desc;
	int status;

	if (error_offset)
		*error_offset = 0;
	if (error_info)
		*error_info = 0;

	cmd = &desc.params.download_pkg;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_update_pkg);
	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	if (last_buf)
		cmd->flags |= ICE_AQC_DOWNLOAD_PKG_LAST_BUF;

	status = ice_aq_send_cmd(hw, &desc, pkg_buf, buf_size, cd);
	if (status == ICE_ERR_AQ_ERROR) {
		/* Read error from buffer only when the FW returned an error */
		struct ice_aqc_download_pkg_resp *resp;

		resp = (struct ice_aqc_download_pkg_resp *)pkg_buf;
		if (error_offset)
			*error_offset = le32toh(resp->error_offset);
		if (error_info)
			*error_info = le32toh(resp->error_info);
	}

	return status;
}

/**
 * ice_update_pkg_no_lock
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @count: the number of buffers in the array
 */
int
ice_update_pkg_no_lock(struct ice_hw *hw, struct ice_buf *bufs, uint32_t count)
{
	int status = 0;
	uint32_t i;

	for (i = 0; i < count; i++) {
		struct ice_buf_hdr *bh = (struct ice_buf_hdr *)(bufs + i);
		bool last = ((i + 1) == count);
		uint32_t offset, info;

		status = ice_aq_update_pkg(hw, bh, le16toh(bh->data_end),
		    last, &offset, &info, NULL);
		if (status) {
			DNPRINTF(ICE_DBG_PKG,
			    "Update pkg failed: err %d off %d inf %d\n",
			    status, offset, info);
			break;
		}
	}

	return status;
}

/**
 * ice_acquire_change_lock
 * @hw: pointer to the HW structure
 * @access: access type (read or write)
 *
 * This function will request ownership of the change lock.
 */
int
ice_acquire_change_lock(struct ice_hw *hw, enum ice_aq_res_access_type access)
{
	return ice_acquire_res(hw, ICE_CHANGE_LOCK_RES_ID, access,
	    ICE_CHANGE_LOCK_TIMEOUT);
}

/**
 * ice_release_change_lock
 * @hw: pointer to the HW structure
 *
 * This function will release the change lock using the proper Admin Command.
 */
void
ice_release_change_lock(struct ice_hw *hw)
{
	ice_release_res(hw, ICE_CHANGE_LOCK_RES_ID);
}

/**
 * ice_update_pkg
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @count: the number of buffers in the array
 *
 * Obtains change lock and updates package.
 */
int
ice_update_pkg(struct ice_hw *hw, struct ice_buf *bufs, uint32_t count)
{
	int status;

	status = ice_acquire_change_lock(hw, ICE_RES_WRITE);
	if (status)
		return status;

	status = ice_update_pkg_no_lock(hw, bufs, count);

	ice_release_change_lock(hw);

	return status;
}

/**
 * ice_upd_prof_hw - update hardware using the change list
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @chgs: the list of changes to make in hardware
 */
enum ice_status
ice_upd_prof_hw(struct ice_hw *hw, enum ice_block blk,
		struct ice_chs_chg_head *chgs)
{
	struct ice_buf_build *b;
	struct ice_chs_chg *tmp;
	enum ice_status status;
	uint16_t pkg_sects;
	uint16_t xlt1 = 0;
	uint16_t xlt2 = 0;
	uint16_t tcam = 0;
	uint16_t es = 0;
	uint16_t sects;

	/* count number of sections we need */
	TAILQ_FOREACH(tmp, chgs, list_entry) {
		switch (tmp->type) {
		case ICE_PTG_ES_ADD:
			if (tmp->add_ptg)
				xlt1++;
			if (tmp->add_prof)
				es++;
			break;
		case ICE_TCAM_ADD:
			tcam++;
			break;
		case ICE_VSIG_ADD:
		case ICE_VSI_MOVE:
		case ICE_VSIG_REM:
			xlt2++;
			break;
		default:
			break;
		}
	}
	sects = xlt1 + xlt2 + tcam + es;

	if (!sects)
		return ICE_SUCCESS;

	/* Build update package buffer */
	b = ice_pkg_buf_alloc(hw);
	if (!b)
		return ICE_ERR_NO_MEMORY;

	status = ice_pkg_buf_reserve_section(b, sects);
	if (status)
		goto error_tmp;

	/* Preserve order of table update: ES, TCAM, PTG, VSIG */
	if (es) {
		status = ice_prof_bld_es(hw, blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (tcam) {
		status = ice_prof_bld_tcam(hw, blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (xlt1) {
		status = ice_prof_bld_xlt1(blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (xlt2) {
		status = ice_prof_bld_xlt2(blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	/* After package buffer build check if the section count in buffer is
	 * non-zero and matches the number of sections detected for package
	 * update.
	 */
	pkg_sects = ice_pkg_buf_get_active_sections(b);
	if (!pkg_sects || pkg_sects != sects) {
		status = ICE_ERR_INVAL_SIZE;
		goto error_tmp;
	}

	/* update package */
	status = ice_update_pkg(hw, ice_pkg_buf(b), 1);
	if (status == ICE_ERR_AQ_ERROR)
		DNPRINTF(ICE_DBG_INIT, "Unable to update HW profile\n");

error_tmp:
	ice_free(hw, b);
	return status;
}

/**
 * ice_match_prop_lst - determine if properties of two lists match
 * @list1: first properties list
 * @list2: second properties list
 *
 * Count, cookies and the order must match in order to be considered equivalent.
 */
bool
ice_match_prop_lst(struct ice_vsig_prof_head *list1,
    struct ice_vsig_prof_head *list2)
{
	struct ice_vsig_prof *tmp1, *tmp2;
	uint16_t chk_count = 0;
	uint16_t count = 0;

	/* compare counts */
	TAILQ_FOREACH(tmp1, list1, list)
		count++;
	TAILQ_FOREACH(tmp2, list2, list)
		chk_count++;
	if (!count || count != chk_count)
		return false;

	tmp1 = TAILQ_FIRST(list1);
	tmp2 = TAILQ_FIRST(list2);

	/* profile cookies must compare, and in the exact same order to take
	 * into account priority
	 */
	while (count--) {
		if (tmp2->profile_cookie != tmp1->profile_cookie)
			return false;

		tmp1 = TAILQ_NEXT(tmp1, list);
		tmp2 = TAILQ_NEXT(tmp2, list);
	}

	return true;
}
/**
 * ice_find_dup_props_vsig - find VSI group with a specified set of properties
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @chs: characteristic list
 * @vsig: returns the VSIG with the matching profiles, if found
 *
 * Each VSIG is associated with a characteristic set; i.e. all VSIs under
 * a group have the same characteristic set. To check if there exists a VSIG
 * which has the same characteristics as the input characteristics; this
 * function will iterate through the XLT2 list and return the VSIG that has a
 * matching configuration. In order to make sure that priorities are accounted
 * for, the list must match exactly, including the order in which the
 * characteristics are listed.
 */
enum ice_status
ice_find_dup_props_vsig(struct ice_hw *hw, enum ice_block blk,
			struct ice_vsig_prof_head *lst, uint16_t *vsig)
{
	struct ice_xlt2 *xlt2 = &hw->blk[blk].xlt2;
	uint16_t i;

	for (i = 0; i < xlt2->count; i++) {
		if (xlt2->vsig_tbl[i].in_use &&
		    ice_match_prop_lst(lst, &xlt2->vsig_tbl[i].prop_lst)) {
			*vsig = ICE_VSIG_VALUE(i, hw->pf_id);
			return ICE_SUCCESS;
		}
	}
	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_rem_prof_id_flow - remove flow
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the VSI from which to remove the profile specified by ID
 * @hdl: profile tracking handle
 *
 * Calling this function will update the hardware tables to remove the
 * profile indicated by the ID parameter for the VSIs specified in the VSI
 * array. Once successfully called, the flow will be disabled.
 */
enum ice_status
ice_rem_prof_id_flow(struct ice_hw *hw, enum ice_block blk, uint16_t vsi,
    uint64_t hdl)
{
	struct ice_vsig_prof *tmp1, *del1;
	struct ice_chs_chg *tmp, *del;
	struct ice_chs_chg_head chg;
	struct ice_vsig_prof_head copy;
	enum ice_status status;
	uint16_t vsig;

	TAILQ_INIT(&copy);
	TAILQ_INIT(&chg);

	/* determine if VSI is already part of a VSIG */
	status = ice_vsig_find_vsi(hw, blk, vsi, &vsig);
	if (!status && vsig) {
		bool last_profile;
		bool only_vsi;
		uint16_t ref;

		/* found in VSIG */
		last_profile = ice_vsig_prof_id_count(hw, blk, vsig) == 1;
		status = ice_vsig_get_ref(hw, blk, vsig, &ref);
		if (status)
			goto err_ice_rem_prof_id_flow;
		only_vsi = (ref == 1);

		if (only_vsi) {
			/* If the original VSIG only contains one reference,
			 * which will be the requesting VSI, then the VSI is not
			 * sharing entries and we can simply remove the specific
			 * characteristics from the VSIG.
			 */

			if (last_profile) {
				/* If there are no profiles left for this VSIG,
				 * then simply remove the VSIG.
				 */
				status = ice_rem_vsig(hw, blk, vsig, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			} else {
				status = ice_rem_prof_id_vsig(hw, blk, vsig,
							      hdl, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

				/* Adjust priorities */
				status = ice_adj_prof_priorities(hw, blk, vsig,
								 &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			}

		} else {
			/* Make a copy of the VSIG's list of Profiles */
			status = ice_get_profs_vsig(hw, blk, vsig, &copy);
			if (status)
				goto err_ice_rem_prof_id_flow;

			/* Remove specified profile entry from the list */
			status = ice_rem_prof_from_list(hw, &copy, hdl);
			if (status)
				goto err_ice_rem_prof_id_flow;

			if (TAILQ_EMPTY(&copy)) {
				status = ice_move_vsi(hw, blk, vsi,
						      ICE_DEFAULT_VSIG, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

			} else if (!ice_find_dup_props_vsig(hw, blk, &copy,
							    &vsig)) {
				/* found an exact match */
				/* add or move VSI to the VSIG that matches */
				/* Search for a VSIG with a matching profile
				 * list
				 */

				/* Found match, move VSI to the matching VSIG */
				status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			} else {
				/* since no existing VSIG supports this
				 * characteristic pattern, we need to create a
				 * new VSIG and TCAM entries
				 */
				status = ice_create_vsig_from_lst(hw, blk, vsi,
								  &copy, &vsig,
								  &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

				/* Adjust priorities */
				status = ice_adj_prof_priorities(hw, blk, vsig,
								 &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			}
		}
	} else {
		status = ICE_ERR_DOES_NOT_EXIST;
	}

	/* update hardware tables */
	if (!status)
		status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_rem_prof_id_flow:
	TAILQ_FOREACH_SAFE(del, &chg, list_entry, tmp) {
		TAILQ_REMOVE(&chg, del, list_entry);
		ice_free(hw, del);
	}

	TAILQ_FOREACH_SAFE(del1, &copy, list, tmp1) {
		TAILQ_REMOVE(&copy, del1, list);
		ice_free(hw, del1);
	}

	return status;
}

/**
 * ice_flow_disassoc_prof - disassociate a VSI from a flow profile
 * @hw: pointer to the hardware structure
 * @blk: classification stage
 * @prof: pointer to flow profile
 * @vsi_handle: software VSI handle
 *
 * Assumption: the caller has acquired the lock to the profile list
 * and the software VSI handle has been validated
 */
enum ice_status
ice_flow_disassoc_prof(struct ice_hw *hw, enum ice_block blk,
		       struct ice_flow_prof *prof, uint16_t vsi_handle)
{
	enum ice_status status = ICE_SUCCESS;

	if (ice_is_bit_set(prof->vsis, vsi_handle)) {
		status = ice_rem_prof_id_flow(hw, blk,
		    hw->vsi_ctx[vsi_handle]->vsi_num, prof->id);
		if (!status)
			ice_clear_bit(vsi_handle, prof->vsis);
		else
			DNPRINTF(ICE_DBG_FLOW,
			    "%s: HW profile remove failed, %d\n",
			    __func__, status);
	}

	return status;
}

/**
 * ice_flow_find_prof_id - Look up a profile with given profile ID
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @prof_id: unique ID to identify this flow profile
 */
struct ice_flow_prof *
ice_flow_find_prof_id(struct ice_hw *hw, enum ice_block blk, uint64_t prof_id)
{
	struct ice_flow_prof *p;

	TAILQ_FOREACH(p, &hw->fl_profs[blk], l_entry)
		if (p->id == prof_id)
			return p;

	return NULL;
}

/**
 * ice_rem_flow_all - remove all flows with a particular profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 */
enum ice_status
ice_rem_flow_all(struct ice_hw *hw, enum ice_block blk, uint64_t id)
{
	struct ice_chs_chg *del, *tmp;
	enum ice_status status;
	struct ice_chs_chg_head chg;
	uint16_t i;

	TAILQ_INIT(&chg);

	for (i = 1; i < ICE_MAX_VSIGS; i++)
		if (hw->blk[blk].xlt2.vsig_tbl[i].in_use) {
			if (ice_has_prof_vsig(hw, blk, i, id)) {
				status = ice_rem_prof_id_vsig(hw, blk, i, id,
							      &chg);
				if (status)
					goto err_ice_rem_flow_all;
			}
		}

	status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_rem_flow_all:
	TAILQ_FOREACH_SAFE(del, &chg, list_entry, tmp) {
		TAILQ_REMOVE(&chg, del, list_entry);
		ice_free(hw, del);
	}

	return status;
}

/**
 * ice_prof_inc_ref - increment reference count for profile
 * @hw: pointer to the HW struct
 * @blk: the block from which to free the profile ID
 * @prof_id: the profile ID for which to increment the reference count
 */
enum ice_status
ice_prof_inc_ref(struct ice_hw *hw, enum ice_block blk, uint8_t prof_id)
{
	if (prof_id > hw->blk[blk].es.count)
		return ICE_ERR_PARAM;

	hw->blk[blk].es.ref_count[prof_id]++;

	return ICE_SUCCESS;
}

/**
 * ice_write_es - write an extraction sequence to hardware
 * @hw: pointer to the HW struct
 * @blk: the block in which to write the extraction sequence
 * @prof_id: the profile ID to write
 * @fv: pointer to the extraction sequence to write - NULL to clear extraction
 */
void
ice_write_es(struct ice_hw *hw, enum ice_block blk, uint8_t prof_id,
	     struct ice_fv_word *fv)
{
	uint16_t off;

	off = prof_id * hw->blk[blk].es.fvw;
	if (!fv) {
		memset(&hw->blk[blk].es.t[off], 0, hw->blk[blk].es.fvw *
			   sizeof(*fv));
		hw->blk[blk].es.written[prof_id] = false;
	} else {
		memcpy(&hw->blk[blk].es.t[off], fv, hw->blk[blk].es.fvw *
			   sizeof(*fv));
	}
}

/**
 * ice_prof_id_rsrc_type - get profile ID resource type for a block type
 * @blk: the block type
 * @rsrc_type: pointer to variable to receive the resource type
 */
bool
ice_prof_id_rsrc_type(enum ice_block blk, uint16_t *rsrc_type)
{
	switch (blk) {
	case ICE_BLK_RSS:
		*rsrc_type = ICE_AQC_RES_TYPE_HASH_PROF_BLDR_PROFID;
		break;
	case ICE_BLK_PE:
		*rsrc_type = ICE_AQC_RES_TYPE_QHASH_PROF_BLDR_PROFID;
		break;
	default:
		return false;
	}
	return true;
}

/**
 * ice_free_prof_id - free profile ID
 * @hw: pointer to the HW struct
 * @blk: the block from which to free the profile ID
 * @prof_id: the profile ID to free
 *
 * This function frees a profile ID, which also corresponds to a Field Vector.
 */
enum ice_status
ice_free_prof_id(struct ice_hw *hw, enum ice_block blk, uint8_t prof_id)
{
	uint16_t tmp_prof_id = (uint16_t)prof_id;
	uint16_t res_type;

	if (!ice_prof_id_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_free_hw_res(hw, res_type, 1, &tmp_prof_id);
}

/**
 * ice_prof_dec_ref - decrement reference count for profile
 * @hw: pointer to the HW struct
 * @blk: the block from which to free the profile ID
 * @prof_id: the profile ID for which to decrement the reference count
 */
enum ice_status
ice_prof_dec_ref(struct ice_hw *hw, enum ice_block blk, uint8_t prof_id)
{
	if (prof_id > hw->blk[blk].es.count)
		return ICE_ERR_PARAM;

	if (hw->blk[blk].es.ref_count[prof_id] > 0) {
		if (!--hw->blk[blk].es.ref_count[prof_id]) {
			ice_write_es(hw, blk, prof_id, NULL);
			return ice_free_prof_id(hw, blk, prof_id);
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_rem_prof - remove profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 *
 * This will remove the profile specified by the ID parameter, which was
 * previously created through ice_add_prof. If any existing entries
 * are associated with this profile, they will be removed as well.
 */
enum ice_status
ice_rem_prof(struct ice_hw *hw, enum ice_block blk, uint64_t id)
{
	struct ice_prof_map *pmap;
	enum ice_status status;
#if 0
	ice_acquire_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	pmap = ice_search_prof_id(hw, blk, id);
	if (!pmap) {
		status = ICE_ERR_DOES_NOT_EXIST;
		goto err_ice_rem_prof;
	}

	/* remove all flows with this profile */
	status = ice_rem_flow_all(hw, blk, pmap->profile_cookie);
	if (status)
		goto err_ice_rem_prof;

	/* dereference profile, and possibly remove */
	ice_prof_dec_ref(hw, blk, pmap->prof_id);

	TAILQ_REMOVE(&hw->blk[blk].es.prof_map, pmap, list);
	ice_free(hw, pmap);

err_ice_rem_prof:
#if 0
	ice_release_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	return status;
}

/**
 * ice_flow_rem_prof_sync - remove a flow profile
 * @hw: pointer to the hardware structure
 * @blk: classification stage
 * @prof: pointer to flow profile to remove
 *
 * Assumption: the caller has acquired the lock to the profile list
 */
enum ice_status
ice_flow_rem_prof_sync(struct ice_hw *hw, enum ice_block blk,
		       struct ice_flow_prof *prof)
{
	enum ice_status status;

	/* Remove all hardware profiles associated with this flow profile */
	status = ice_rem_prof(hw, blk, prof->id);
	if (!status) {
		TAILQ_REMOVE(&hw->fl_profs[blk], prof, l_entry);
		ice_free(hw, prof);
	}

	return status;
}

/**
 * ice_flow_rem_prof - Remove a flow profile and all entries associated with it
 * @hw: pointer to the HW struct
 * @blk: the block for which the flow profile is to be removed
 * @prof_id: unique ID of the flow profile to be removed
 */
enum ice_status
ice_flow_rem_prof(struct ice_hw *hw, enum ice_block blk, uint64_t prof_id)
{
	struct ice_flow_prof *prof;
	enum ice_status status;
#if 0
	ice_acquire_lock(&hw->fl_profs_locks[blk]);
#endif
	prof = ice_flow_find_prof_id(hw, blk, prof_id);
	if (!prof) {
		status = ICE_ERR_DOES_NOT_EXIST;
		goto out;
	}

	/* prof becomes invalid after the call */
	status = ice_flow_rem_prof_sync(hw, blk, prof);

out:
#if 0
	ice_release_lock(&hw->fl_profs_locks[blk]);
#endif
	return status;
}

/**
 * ice_rem_vsi_rss_cfg - remove RSS configurations associated with VSI
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 *
 * This function will iterate through all flow profiles and disassociate
 * the VSI from that profile. If the flow profile has no VSIs it will
 * be removed.
 */
enum ice_status
ice_rem_vsi_rss_cfg(struct ice_hw *hw, uint16_t vsi_handle)
{
	const enum ice_block blk = ICE_BLK_RSS;
	struct ice_flow_prof *p, *t;
	enum ice_status status = ICE_SUCCESS;
	uint16_t vsig;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;

	if (TAILQ_EMPTY(&hw->fl_profs[blk]))
		return ICE_SUCCESS;
#if 0
	ice_acquire_lock(&hw->rss_locks);
#endif
	TAILQ_FOREACH_SAFE(p, &hw->fl_profs[blk], l_entry, t) {
		int ret;

		/* check if vsig is already removed */
		ret = ice_vsig_find_vsi(hw, blk,
		    hw->vsi_ctx[vsi_handle]->vsi_num, &vsig);
		if (!ret && !vsig)
			break;

		if (ice_is_bit_set(p->vsis, vsi_handle)) {
			status = ice_flow_disassoc_prof(hw, blk, p, vsi_handle);
			if (status)
				break;

			if (!ice_is_any_bit_set(p->vsis, ICE_MAX_VSI)) {
				status = ice_flow_rem_prof(hw, blk, p->id);
				if (status)
					break;
			}
		}
	}
#if 0
	ice_release_lock(&hw->rss_locks);
#endif
	return status;
}

/**
 * ice_rem_vsi_rss_list - remove VSI from RSS list
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 *
 * Remove the VSI from all RSS configurations in the list.
 */
void
ice_rem_vsi_rss_list(struct ice_hw *hw, uint16_t vsi_handle)
{
	struct ice_rss_cfg *r, *tmp;

	if (TAILQ_EMPTY(&hw->rss_list_head))
		return;
#if 0
	ice_acquire_lock(&hw->rss_locks);
#endif
	TAILQ_FOREACH_SAFE(r, &hw->rss_list_head, l_entry, tmp) {
		if (ice_test_and_clear_bit(vsi_handle, r->vsis) &&
		    !ice_is_any_bit_set(r->vsis, ICE_MAX_VSI)) {
			TAILQ_REMOVE(&hw->rss_list_head, r, l_entry);
			ice_free(hw, r);
		}
	}
#if 0
	ice_release_lock(&hw->rss_locks);
#endif
}

/**
 * ice_clean_vsi_rss_cfg - Cleanup RSS configuration for a given VSI
 * @vsi: pointer to the VSI structure
 *
 * Cleanup the advanced RSS configuration for a given VSI. This is necessary
 * during driver removal to ensure that all RSS resources are properly
 * released.
 *
 * @remark this function doesn't report an error as it is expected to be
 * called during driver reset and unload, and there isn't much the driver can
 * do if freeing RSS resources fails.
 */
void
ice_clean_vsi_rss_cfg(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	status = ice_rem_vsi_rss_cfg(hw, vsi->idx);
	if (status)
		printf("%s: Failed to remove RSS configuration for VSI %d, "
		    "err %s\n", sc->sc_dev.dv_xname, vsi->idx,
		    ice_status_str(status));

	/* Remove this VSI from the RSS list */
	ice_rem_vsi_rss_list(hw, vsi->idx);
}

/**
 * ice_remove_vsi_mirroring -- Teardown any VSI mirroring rules
 * @vsi: VSI to remove mirror rules from
 */
void
ice_remove_vsi_mirroring(struct ice_vsi *vsi)
{
#if 0
	struct ice_hw *hw = &vsi->sc->hw;
	enum ice_status status = ICE_SUCCESS;
	bool keep_alloc = false;

	if (vsi->rule_mir_ingress != ICE_INVAL_MIRROR_RULE_ID)
		status = ice_aq_delete_mir_rule(hw, vsi->rule_mir_ingress, keep_alloc, NULL);

	if (status)
		device_printf(vsi->sc->dev, "Could not remove mirror VSI ingress rule, err %s aq_err %s\n",
			      ice_status_str(status), ice_aq_str(hw->adminq.sq_last_status));

	status = ICE_SUCCESS;

	if (vsi->rule_mir_egress != ICE_INVAL_MIRROR_RULE_ID)
		status = ice_aq_delete_mir_rule(hw, vsi->rule_mir_egress, keep_alloc, NULL);

	if (status)
		device_printf(vsi->sc->dev, "Could not remove mirror VSI egress rule, err %s aq_err %s\n",
			      ice_status_str(status), ice_aq_str(hw->adminq.sq_last_status));
#else
	printf("%s: not implemented\n", __func__);
#endif
}

/**
 * ice_sched_get_vsi_node - Get a VSI node based on VSI ID
 * @pi: pointer to the port information structure
 * @tc_node: pointer to the TC node
 * @vsi_handle: software VSI handle
 *
 * This function retrieves a VSI node for a given VSI ID from a given
 * TC branch
 */
struct ice_sched_node *
ice_sched_get_vsi_node(struct ice_port_info *pi, struct ice_sched_node *tc_node,
		       uint16_t vsi_handle)
{
	struct ice_sched_node *node;
	uint8_t vsi_layer;

	vsi_layer = ice_sched_get_vsi_layer(pi->hw);
	node = ice_sched_get_first_node(pi, tc_node, vsi_layer);

	/* Check whether it already exists */
	while (node) {
		if (node->vsi_handle == vsi_handle)
			return node;
		node = node->sibling;
	}

	return node;
}

/**
 * ice_sched_is_leaf_node_present - check for a leaf node in the sub-tree
 * @node: pointer to the sub-tree node
 *
 * This function checks for a leaf node presence in a given sub-tree node.
 */
bool
ice_sched_is_leaf_node_present(struct ice_sched_node *node)
{
	uint8_t i;

	for (i = 0; i < node->num_children; i++)
		if (ice_sched_is_leaf_node_present(node->children[i]))
			return true;
	/* check for a leaf node */
	return (node->info.data.elem_type == ICE_AQC_ELEM_TYPE_LEAF);
}

/**
 * ice_sched_rm_agg_vsi_info - remove aggregator related VSI info entry
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 *
 * This function removes single aggregator VSI info entry from
 * aggregator list.
 */
void
ice_sched_rm_agg_vsi_info(struct ice_port_info *pi, uint16_t vsi_handle)
{
#if 0
	struct ice_sched_agg_info *agg_info;
	struct ice_sched_agg_info *atmp;

	LIST_FOR_EACH_ENTRY_SAFE(agg_info, atmp, &pi->hw->agg_list,
				 ice_sched_agg_info,
				 list_entry) {
		struct ice_sched_agg_vsi_info *agg_vsi_info;
		struct ice_sched_agg_vsi_info *vtmp;

		LIST_FOR_EACH_ENTRY_SAFE(agg_vsi_info, vtmp,
					 &agg_info->agg_vsi_list,
					 ice_sched_agg_vsi_info, list_entry)
			if (agg_vsi_info->vsi_handle == vsi_handle) {
				LIST_DEL(&agg_vsi_info->list_entry);
				ice_free(pi->hw, agg_vsi_info);
				return;
			}
	}
#else
	printf("%s: not implemented\n", __func__);
#endif
}

/**
 * ice_sched_rm_vsi_cfg - remove the VSI and its children nodes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @owner: LAN or RDMA
 *
 * This function removes the VSI and its LAN or RDMA children nodes from the
 * scheduler tree.
 */
enum ice_status
ice_sched_rm_vsi_cfg(struct ice_port_info *pi, uint16_t vsi_handle,
    uint8_t owner)
{
	enum ice_status status = ICE_ERR_PARAM;
	struct ice_vsi_ctx *vsi_ctx;
	uint8_t i;

	DNPRINTF(ICE_DBG_SCHED, "%s: removing VSI %d\n", __func__, vsi_handle);
	if (!ice_is_vsi_valid(pi->hw, vsi_handle))
		return status;
#if 0
	ice_acquire_lock(&pi->sched_lock);
#endif
	vsi_ctx = ice_get_vsi_ctx(pi->hw, vsi_handle);
	if (!vsi_ctx)
		goto exit_sched_rm_vsi_cfg;

	ice_for_each_traffic_class(i) {
		struct ice_sched_node *vsi_node, *tc_node;
		uint8_t j = 0;

		tc_node = ice_sched_get_tc_node(pi, i);
		if (!tc_node)
			continue;

		vsi_node = ice_sched_get_vsi_node(pi, tc_node, vsi_handle);
		if (!vsi_node)
			continue;

		if (ice_sched_is_leaf_node_present(vsi_node)) {
			DNPRINTF(ICE_DBG_SCHED,
			    "%s: VSI has leaf nodes in TC %d\n", __func__, i);
			status = ICE_ERR_IN_USE;
			goto exit_sched_rm_vsi_cfg;
		}
		while (j < vsi_node->num_children) {
			if (vsi_node->children[j]->owner == owner) {
				ice_free_sched_node(pi, vsi_node->children[j]);

				/* reset the counter again since the num
				 * children will be updated after node removal
				 */
				j = 0;
			} else {
				j++;
			}
		}
		/* remove the VSI if it has no children */
		if (!vsi_node->num_children) {
			ice_free_sched_node(pi, vsi_node);
			vsi_ctx->sched.vsi_node[i] = NULL;

			/* clean up aggregator related VSI info if any */
			ice_sched_rm_agg_vsi_info(pi, vsi_handle);
		}
		if (owner == ICE_SCHED_NODE_OWNER_LAN)
			vsi_ctx->sched.max_lanq[i] = 0;
		else
			vsi_ctx->sched.max_rdmaq[i] = 0;
	}
	status = ICE_SUCCESS;

exit_sched_rm_vsi_cfg:
#if 0
	ice_release_lock(&pi->sched_lock);
#endif
	return status;
}

/**
 * ice_rm_vsi_lan_cfg - remove VSI and its LAN children nodes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 *
 * This function clears the VSI and its LAN children nodes from scheduler tree
 * for all TCs.
 */
enum ice_status
ice_rm_vsi_lan_cfg(struct ice_port_info *pi, uint16_t vsi_handle)
{
	return ice_sched_rm_vsi_cfg(pi, vsi_handle, ICE_SCHED_NODE_OWNER_LAN);
}

/**
 * ice_aq_free_vsi
 * @hw: pointer to the HW struct
 * @vsi_ctx: pointer to a VSI context struct
 * @keep_vsi_alloc: keep VSI allocation as part of this PF's resources
 * @cd: pointer to command details structure or NULL
 *
 * Free VSI context info from hardware (0x0213)
 */
enum ice_status
ice_aq_free_vsi(struct ice_hw *hw, struct ice_vsi_ctx *vsi_ctx,
		bool keep_vsi_alloc, struct ice_sq_cd *cd)
{
	struct ice_aqc_add_update_free_vsi_resp *resp;
	struct ice_aqc_add_get_update_free_vsi *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.vsi_cmd;
	resp = &desc.params.add_update_free_vsi_res;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_free_vsi);

	cmd->vsi_num = htole16(vsi_ctx->vsi_num | ICE_AQ_VSI_IS_VALID);
	if (keep_vsi_alloc)
		cmd->cmd_flags = htole16(ICE_AQ_VSI_KEEP_ALLOC);

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
	if (!status) {
		vsi_ctx->vsis_allocd = le16toh(resp->vsi_used);
		vsi_ctx->vsis_unallocated = le16toh(resp->vsi_free);
	}

	return status;
}

/**
 * ice_clear_vsi_q_ctx - clear VSI queue contexts for all TCs
 * @hw: pointer to the HW struct
 * @vsi_handle: VSI handle
 */
void
ice_clear_vsi_q_ctx(struct ice_hw *hw, uint16_t vsi_handle)
{
	struct ice_vsi_ctx *vsi;
	uint8_t i;

	vsi = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi)
		return;
	ice_for_each_traffic_class(i) {
		if (vsi->lan_q_ctx[i]) {
			ice_free(hw, vsi->lan_q_ctx[i]);
			vsi->lan_q_ctx[i] = NULL;
		}
		if (vsi->rdma_q_ctx[i]) {
			ice_free(hw, vsi->rdma_q_ctx[i]);
			vsi->rdma_q_ctx[i] = NULL;
		}
	}
}

/**
 * ice_clear_vsi_ctx - clear the VSI context entry
 * @hw: pointer to the HW struct
 * @vsi_handle: VSI handle
 *
 * clear the VSI context entry
 */
void
ice_clear_vsi_ctx(struct ice_hw *hw, uint16_t vsi_handle)
{
	struct ice_vsi_ctx *vsi;

	vsi = ice_get_vsi_ctx(hw, vsi_handle);
	if (vsi) {
		ice_clear_vsi_q_ctx(hw, vsi_handle);
		ice_free(hw, vsi);
		hw->vsi_ctx[vsi_handle] = NULL;
	}
}

/**
 * ice_free_vsi- free VSI context from hardware and VSI handle list
 * @hw: pointer to the HW struct
 * @vsi_handle: unique VSI handle
 * @vsi_ctx: pointer to a VSI context struct
 * @keep_vsi_alloc: keep VSI allocation as part of this PF's resources
 * @cd: pointer to command details structure or NULL
 *
 * Free VSI context info from hardware as well as from VSI handle list
 */
enum ice_status
ice_free_vsi(struct ice_hw *hw, uint16_t vsi_handle,
    struct ice_vsi_ctx *vsi_ctx, bool keep_vsi_alloc, struct ice_sq_cd *cd)
{
	enum ice_status status;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;
	vsi_ctx->vsi_num = hw->vsi_ctx[vsi_handle]->vsi_num;
	status = ice_aq_free_vsi(hw, vsi_ctx, keep_vsi_alloc, cd);
	if (!status)
		ice_clear_vsi_ctx(hw, vsi_handle);
	return status;
}

/**
 * ice_deinit_vsi - Tell firmware to release resources for a VSI
 * @vsi: the VSI to release
 *
 * Helper function which requests the firmware to release the hardware
 * resources associated with a given VSI.
 */
void
ice_deinit_vsi(struct ice_vsi *vsi)
{
	struct ice_vsi_ctx ctx = { 0 };
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	/* Assert that the VSI pointer matches in the list */
	KASSERT(vsi == sc->all_vsi[vsi->idx]);

	ctx.info = vsi->info;

	status = ice_rm_vsi_lan_cfg(hw->port_info, vsi->idx);
	if (status) {
		/*
		 * This should only fail if the VSI handle is invalid, or if
		 * any of the nodes have leaf nodes which are still in use.
		 */
		printf("%s: Unable to remove scheduler nodes for VSI %d, "
		    "err %s\n", sc->sc_dev.dv_xname, vsi->idx,
		    ice_status_str(status));
	}

	/* Tell firmware to release the VSI resources */
	status = ice_free_vsi(hw, vsi->idx, &ctx, false, NULL);
	if (status != 0) {
		printf("%s: Free VSI %u AQ call failed, err %s aq_err %s\n",
		    sc->sc_dev.dv_xname, vsi->idx, ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}
}

/**
 * ice_free_vsi_qmaps - Free the PF qmaps associated with a VSI
 * @vsi: the VSI private structure
 *
 * Frees the PF qmaps associated with the given VSI. Generally this will be
 * called by ice_release_vsi, but may need to be called during attach cleanup,
 * depending on when the qmaps were allocated.
 */
void
ice_free_vsi_qmaps(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;

	if (vsi->tx_qmap) {
		ice_resmgr_release_map(&sc->tx_qmgr, vsi->tx_qmap,
		    vsi->num_tx_queues);
		free(vsi->tx_qmap, M_DEVBUF,
		    sc->isc_ntxqsets_max * sizeof(uint16_t));
		vsi->tx_qmap = NULL;
	}

	if (vsi->rx_qmap) {
		ice_resmgr_release_map(&sc->rx_qmgr, vsi->rx_qmap,
		     vsi->num_rx_queues);
		free(vsi->rx_qmap, M_DEVBUF,
		    sc->isc_nrxqsets_max * sizeof(uint16_t));
		vsi->rx_qmap = NULL;
	}
}

/**
 * ice_release_vsi - Release resources associated with a VSI
 * @vsi: the VSI to release
 *
 * Release software and firmware resources associated with a VSI. Release the
 * queue managers associated with this VSI. Also free the VSI structure memory
 * if the VSI was allocated dynamically using ice_alloc_vsi().
 */
void
ice_release_vsi(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	int idx = vsi->idx;

	/* Assert that the VSI pointer matches in the list */
	KASSERT(vsi == sc->all_vsi[idx]);

	/* Cleanup RSS configuration */
	if (ice_is_bit_set(sc->feat_en, ICE_FEATURE_RSS))
		ice_clean_vsi_rss_cfg(vsi);
#if 0
	ice_del_vsi_sysctl_ctx(vsi);
#endif
	/* Remove the configured mirror rule, if it exists */
	ice_remove_vsi_mirroring(vsi);

	/*
	 * If we unload the driver after a reset fails, we do not need to do
	 * this step.
	 */
	if (!ice_test_state(&sc->state, ICE_STATE_RESET_FAILED))
		ice_deinit_vsi(vsi);

	ice_free_vsi_qmaps(vsi);

	if (vsi->dynamic)
		free(sc->all_vsi[idx], M_DEVBUF, sizeof(struct ice_vsi));

	sc->all_vsi[idx] = NULL;
}

/**
 * ice_transition_recovery_mode - Transition to recovery mode
 * @sc: the device private softc
 *
 * Called when the driver detects that the firmware has entered recovery mode
 * at run time.
 */
void
ice_transition_recovery_mode(struct ice_softc *sc)
{
#if 0
	struct ice_vsi *vsi = &sc->pf_vsi;
#endif
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i;

	printf("%s: firmware has switched into recovery mode",
	    sc->sc_dev.dv_xname);

	/* Tell the stack that the link has gone down */
	ifp->if_link_state = LINK_STATE_DOWN;
	if_link_state_change(ifp);
#if 0
	ice_rdma_pf_detach(sc);
#endif
	ice_clear_bit(ICE_FEATURE_RDMA, sc->feat_cap);

	ice_clear_bit(ICE_FEATURE_SRIOV, sc->feat_en);
	ice_clear_bit(ICE_FEATURE_SRIOV, sc->feat_cap);
#if 0
	ice_vsi_del_txqs_ctx(vsi);
	ice_vsi_del_rxqs_ctx(vsi);
#endif
	for (i = 0; i < sc->num_available_vsi; i++) {
		if (sc->all_vsi[i])
			ice_release_vsi(sc->all_vsi[i]);
	}

	if (sc->all_vsi) {
		free(sc->all_vsi, M_DEVBUF,
		    sc->num_available_vsi * sizeof(struct ice_vsi *));
		sc->all_vsi = NULL;
	}
	sc->num_available_vsi = 0;

	/* Destroy the interrupt manager */
	ice_resmgr_destroy(&sc->dev_imgr);
	/* Destroy the queue managers */
	ice_resmgr_destroy(&sc->tx_qmgr);
	ice_resmgr_destroy(&sc->rx_qmgr);

	ice_deinit_hw(&sc->hw);
}

/**
 * ice_clear_hw_tbls - clear HW tables and flow profiles
 * @hw: pointer to the hardware structure
 */
void
ice_clear_hw_tbls(struct ice_hw *hw)
{
	uint8_t i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		struct ice_prof_redir *prof_redir = &hw->blk[i].prof_redir;
		struct ice_prof_tcam *prof = &hw->blk[i].prof;
		struct ice_xlt1 *xlt1 = &hw->blk[i].xlt1;
		struct ice_xlt2 *xlt2 = &hw->blk[i].xlt2;
		struct ice_es *es = &hw->blk[i].es;

		if (hw->blk[i].is_list_init) {
			ice_free_prof_map(hw, i);
			ice_free_flow_profs(hw, i);
		}

		ice_free_vsig_tbl(hw, (enum ice_block)i);

		if (xlt1->ptypes)
			memset(xlt1->ptypes, 0,
			    xlt1->count * sizeof(*xlt1->ptypes));

		if (xlt1->ptg_tbl)
			memset(xlt1->ptg_tbl, 0,
			    ICE_MAX_PTGS * sizeof(*xlt1->ptg_tbl));

		if (xlt1->t)
			memset(xlt1->t, 0, xlt1->count * sizeof(*xlt1->t));

		if (xlt2->vsis)
			memset(xlt2->vsis, 0,
			    xlt2->count * sizeof(*xlt2->vsis));

		if (xlt2->vsig_tbl)
			memset(xlt2->vsig_tbl, 0,
			    xlt2->count * sizeof(*xlt2->vsig_tbl));

		if (xlt2->t)
			memset(xlt2->t, 0, xlt2->count * sizeof(*xlt2->t));

		if (prof->t)
			memset(prof->t, 0, prof->count * sizeof(*prof->t));

		if (prof_redir->t)
			memset(prof_redir->t, 0,
			    prof_redir->count * sizeof(*prof_redir->t));

		if (es->t)
			memset(es->t, 0, es->count * sizeof(*es->t) * es->fvw);

		if (es->ref_count)
			memset(es->ref_count, 0,
			    es->count * sizeof(*es->ref_count));

		if (es->written)
			memset(es->written, 0,
			    es->count * sizeof(*es->written));

	}
}

/**
 * ice_prepare_for_reset - Prepare device for an impending reset
 * @sc: The device private softc
 *
 * Prepare the driver for an impending reset, shutting down VSIs, clearing the
 * scheduler setup, and shutting down controlqs. Uses the
 * ICE_STATE_PREPARED_FOR_RESET to indicate whether we've already prepared the
 * driver for reset or not.
 */
void
ice_prepare_for_reset(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;

	/* If we're already prepared, there's nothing to do */
	if (ice_testandset_state(&sc->state, ICE_STATE_PREPARED_FOR_RESET))
		return;

	DPRINTF("%s: preparing to reset device\n", sc->sc_dev.dv_xname);

	/* In recovery mode, hardware is not initialized */
	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE))
		return;
#if 0
	/* inform the RDMA client */
	ice_rdma_notify_reset(sc);
	/* stop the RDMA client */
	ice_rdma_pf_stop(sc);
#endif
	/* Release the main PF VSI queue mappings */
	ice_resmgr_release_map(&sc->tx_qmgr, sc->pf_vsi.tx_qmap,
				    sc->pf_vsi.num_tx_queues);
	ice_resmgr_release_map(&sc->rx_qmgr, sc->pf_vsi.rx_qmap,
				    sc->pf_vsi.num_rx_queues);
#if 0
	if (sc->mirr_if) {
		ice_resmgr_release_map(&sc->tx_qmgr, sc->mirr_if->vsi->tx_qmap,
		    sc->mirr_if->num_irq_vectors);
		ice_resmgr_release_map(&sc->rx_qmgr, sc->mirr_if->vsi->rx_qmap,
		    sc->mirr_if->num_irq_vectors);
	}
#endif
	ice_clear_hw_tbls(hw);

	if (hw->port_info)
		ice_sched_cleanup_all(hw);

	ice_shutdown_all_ctrlq(hw, false);
}

/**
 * ice_configure_misc_interrupts - enable 'other' interrupt causes
 * @sc: pointer to device private softc
 *
 * Enable various "other" interrupt causes, and associate them to interrupt 0,
 * which is our administrative interrupt.
 */
void
ice_configure_misc_interrupts(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	uint32_t val;

	/* Read the OICR register to clear it */
	ICE_READ(hw, PFINT_OICR);

	/* Enable useful "other" interrupt causes */
	val = (PFINT_OICR_ECC_ERR_M |
	       PFINT_OICR_MAL_DETECT_M |
	       PFINT_OICR_GRST_M |
	       PFINT_OICR_PCI_EXCEPTION_M |
	       PFINT_OICR_VFLR_M |
	       PFINT_OICR_HMC_ERR_M |
	       PFINT_OICR_PE_CRITERR_M);

	ICE_WRITE(hw, PFINT_OICR_ENA, val);

	/* Note that since we're using MSI-X index 0, and ITR index 0, we do
	 * not explicitly program them when writing to the PFINT_*_CTL
	 * registers. Nevertheless, these writes are associating the
	 * interrupts with the ITR 0 vector
	 */

	/* Associate the OICR interrupt with ITR 0, and enable it */
	ICE_WRITE(hw, PFINT_OICR_CTL, PFINT_OICR_CTL_CAUSE_ENA_M);

	/* Associate the Mailbox interrupt with ITR 0, and enable it */
	ICE_WRITE(hw, PFINT_MBX_CTL, PFINT_MBX_CTL_CAUSE_ENA_M);

	/* Associate the AdminQ interrupt with ITR 0, and enable it */
	ICE_WRITE(hw, PFINT_FW_CTL, PFINT_FW_CTL_CAUSE_ENA_M);
}

void
ice_request_stack_reinit(struct ice_softc *sc)
{
	printf("%s: not implemented\n", __func__);
}

/**
 * ice_rebuild_recovery_mode - Rebuild driver state while in recovery mode
 * @sc: The device private softc
 *
 * Handle a driver rebuild while in recovery mode. This will only rebuild the
 * limited functionality supported while in recovery mode.
 */
void
ice_rebuild_recovery_mode(struct ice_softc *sc)
{
#if 0
	/* enable PCIe bus master */
	pci_enable_busmaster(dev);
#endif
	/* Configure interrupt causes for the administrative interrupt */
	ice_configure_misc_interrupts(sc);

	/* Enable ITR 0 right away, so that we can handle admin interrupts */
	ice_enable_intr(&sc->hw, 0);

	/* Rebuild is finished. We're no longer prepared to reset */
	ice_clear_state(&sc->state, ICE_STATE_PREPARED_FOR_RESET);

	printf("%s: device rebuild successful\n", sc->sc_dev.dv_xname);

	/* In order to completely restore device functionality, the iflib core
	 * needs to be reset. We need to request an iflib reset. Additionally,
	 * because the state of IFC_DO_RESET is cached within task_fn_admin in
	 * the iflib core, we also want re-run the admin task so that iflib
	 * resets immediately instead of waiting for the next interrupt.
	 */

	ice_request_stack_reinit(sc);
}

/**
 * ice_clean_all_vsi_rss_cfg - Cleanup RSS configuration for all VSIs
 * @sc: the device softc pointer
 *
 * Cleanup the advanced RSS configuration for all VSIs on a given PF
 * interface.
 *
 * @remark This should be called while preparing for a reset, to cleanup stale
 * RSS configuration for all VSIs.
 */
void
ice_clean_all_vsi_rss_cfg(struct ice_softc *sc)
{
	int i;

	/* No need to cleanup if RSS is not enabled */
	if (!ice_is_bit_set(sc->feat_en, ICE_FEATURE_RSS))
		return;

	for (i = 0; i < sc->num_available_vsi; i++) {
		struct ice_vsi *vsi = sc->all_vsi[i];

		if (vsi)
			ice_clean_vsi_rss_cfg(vsi);
	}
}

/**
 * ice_reset_pf_stats - Reset port stats counters
 * @sc: Device private softc structure
 *
 * Reset software tracking values for statistics to zero, and indicate that
 * offsets haven't been loaded. Intended to be called after a device reset so
 * that statistics count from zero again.
 */
void
ice_reset_pf_stats(struct ice_softc *sc)
{
	memset(&sc->stats.prev, 0, sizeof(sc->stats.prev));
	memset(&sc->stats.cur, 0, sizeof(sc->stats.cur));
	sc->stats.offsets_loaded = false;
}

/**
 * ice_rebuild_pf_vsi_qmap - Rebuild the main PF VSI queue mapping
 * @sc: the device softc pointer
 *
 * Loops over the Tx and Rx queues for the main PF VSI and reassigns the queue
 * mapping after a reset occurred.
 */
int
ice_rebuild_pf_vsi_qmap(struct ice_softc *sc)
{
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_tx_queue *txq;
	struct ice_rx_queue *rxq;
	int err, i;

	/* Re-assign Tx queues from PF space to the main VSI */
	err = ice_resmgr_assign_contiguous(&sc->tx_qmgr, vsi->tx_qmap,
					    vsi->num_tx_queues);
	if (err) {
		printf("%s: Unable to re-assign PF Tx queues: %d\n",
		    sc->sc_dev.dv_xname, err);
		return (err);
	}

	/* Re-assign Rx queues from PF space to this VSI */
	err = ice_resmgr_assign_contiguous(&sc->rx_qmgr, vsi->rx_qmap,
					    vsi->num_rx_queues);
	if (err) {
		printf("%s: Unable to re-assign PF Rx queues: %d\n",
		    sc->sc_dev.dv_xname, err);
		goto err_release_tx_queues;
	}

	vsi->qmap_type = ICE_RESMGR_ALLOC_CONTIGUOUS;

	/* Re-assign Tx queue tail pointers */
	for (i = 0, txq = vsi->tx_queues; i < vsi->num_tx_queues; i++, txq++)
		txq->tail = QTX_COMM_DBELL(vsi->tx_qmap[i]);

	/* Re-assign Rx queue tail pointers */
	for (i = 0, rxq = vsi->rx_queues; i < vsi->num_rx_queues; i++, rxq++)
		rxq->tail = QRX_TAIL(vsi->rx_qmap[i]);

	return (0);

err_release_tx_queues:
	ice_resmgr_release_map(&sc->tx_qmgr, sc->pf_vsi.tx_qmap,
				   sc->pf_vsi.num_tx_queues);

	return (err);
}

#define ICE_UP_TABLE_TRANSLATE(val, i) \
		(((val) << ICE_AQ_VSI_UP_TABLE_UP##i##_S) & \
		ICE_AQ_VSI_UP_TABLE_UP##i##_M)

/**
 * ice_set_default_vsi_ctx - Setup default VSI context parameters
 * @ctx: the VSI context to initialize
 *
 * Initialize and prepare a default VSI context for configuring a new VSI.
 */
void
ice_set_default_vsi_ctx(struct ice_vsi_ctx *ctx)
{
	uint32_t table = 0;

	memset(&ctx->info, 0, sizeof(ctx->info));
	/* VSI will be allocated from shared pool */
	ctx->alloc_from_pool = true;
	/* Enable source pruning by default */
	ctx->info.sw_flags = ICE_AQ_VSI_SW_FLAG_SRC_PRUNE;
	/* Traffic from VSI can be sent to LAN */
	ctx->info.sw_flags2 = ICE_AQ_VSI_SW_FLAG_LAN_ENA;
	/* Allow all packets untagged/tagged */
	ctx->info.inner_vlan_flags = ((ICE_AQ_VSI_INNER_VLAN_TX_MODE_ALL &
				       ICE_AQ_VSI_INNER_VLAN_TX_MODE_M) >>
				       ICE_AQ_VSI_INNER_VLAN_TX_MODE_S);
	/* Show VLAN/UP from packets in Rx descriptors */
	ctx->info.inner_vlan_flags |= ((ICE_AQ_VSI_INNER_VLAN_EMODE_STR_BOTH &
					ICE_AQ_VSI_INNER_VLAN_EMODE_M) >>
					ICE_AQ_VSI_INNER_VLAN_EMODE_S);
	/* Have 1:1 UP mapping for both ingress/egress tables */
	table |= ICE_UP_TABLE_TRANSLATE(0, 0);
	table |= ICE_UP_TABLE_TRANSLATE(1, 1);
	table |= ICE_UP_TABLE_TRANSLATE(2, 2);
	table |= ICE_UP_TABLE_TRANSLATE(3, 3);
	table |= ICE_UP_TABLE_TRANSLATE(4, 4);
	table |= ICE_UP_TABLE_TRANSLATE(5, 5);
	table |= ICE_UP_TABLE_TRANSLATE(6, 6);
	table |= ICE_UP_TABLE_TRANSLATE(7, 7);
	ctx->info.ingress_table = htole32(table);
	ctx->info.egress_table = htole32(table);
	/* Have 1:1 UP mapping for outer to inner UP table */
	ctx->info.outer_up_table = htole32(table);
	/* No Outer tag support, so outer_vlan_flags remains zero */
}

/**
 * ice_set_rss_vsi_ctx - Setup VSI context parameters for RSS
 * @ctx: the VSI context to configure
 * @type: the VSI type
 *
 * Configures the VSI context for RSS, based on the VSI type.
 */
void
ice_set_rss_vsi_ctx(struct ice_vsi_ctx *ctx, enum ice_vsi_type type)
{
	uint8_t lut_type, hash_type;

	switch (type) {
	case ICE_VSI_PF:
		lut_type = ICE_AQ_VSI_Q_OPT_RSS_LUT_PF;
		hash_type = ICE_AQ_VSI_Q_OPT_RSS_TPLZ;
		break;
	case ICE_VSI_VF:
	case ICE_VSI_VMDQ2:
		lut_type = ICE_AQ_VSI_Q_OPT_RSS_LUT_VSI;
		hash_type = ICE_AQ_VSI_Q_OPT_RSS_TPLZ;
		break;
	default:
		/* Other VSI types do not support RSS */
		return;
	}

	ctx->info.q_opt_rss = (((lut_type << ICE_AQ_VSI_Q_OPT_RSS_LUT_S) &
				 ICE_AQ_VSI_Q_OPT_RSS_LUT_M) |
				((hash_type << ICE_AQ_VSI_Q_OPT_RSS_HASH_S) &
				 ICE_AQ_VSI_Q_OPT_RSS_HASH_M));
}

/**
 * ice_vsi_set_rss_params - Set the RSS parameters for the VSI
 * @vsi: the VSI to configure
 *
 * Sets the RSS table size and lookup table type for the VSI based on its
 * VSI type.
 */
void
ice_vsi_set_rss_params(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_hw_common_caps *cap;

	cap = &sc->hw.func_caps.common_cap;

	switch (vsi->type) {
	case ICE_VSI_PF:
		/* The PF VSI inherits RSS instance of the PF */
		vsi->rss_table_size = cap->rss_table_size;
		vsi->rss_lut_type = ICE_LUT_PF;
		break;
	case ICE_VSI_VF:
	case ICE_VSI_VMDQ2:
		vsi->rss_table_size = ICE_VSIQF_HLUT_ARRAY_SIZE;
		vsi->rss_lut_type = ICE_LUT_VSI;
		break;
	default:
		DPRINTF("%s: VSI %d: RSS not supported for VSI type %d\n",
		    __func__, vsi->idx, vsi->type);
		break;
	}
}

/**
 * ice_setup_vsi_qmap - Setup the queue mapping for a VSI
 * @vsi: the VSI to configure
 * @ctx: the VSI context to configure
 *
 * Configures the context for the given VSI, setting up how the firmware
 * should map the queues for this VSI.
 *
 * @pre vsi->qmap_type is set to a valid type
 */
int
ice_setup_vsi_qmap(struct ice_vsi *vsi, struct ice_vsi_ctx *ctx)
{
	int pow = 0;
	uint16_t qmap;

	KASSERT(vsi->rx_qmap != NULL);

	switch (vsi->qmap_type) {
	case ICE_RESMGR_ALLOC_CONTIGUOUS:
		ctx->info.mapping_flags |= htole16(ICE_AQ_VSI_Q_MAP_CONTIG);

		ctx->info.q_mapping[0] = htole16(vsi->rx_qmap[0]);
		ctx->info.q_mapping[1] = htole16(vsi->num_rx_queues);

		break;
	case ICE_RESMGR_ALLOC_SCATTERED:
		ctx->info.mapping_flags |= htole16(ICE_AQ_VSI_Q_MAP_NONCONTIG);

		for (int i = 0; i < vsi->num_rx_queues; i++)
			ctx->info.q_mapping[i] = htole16(vsi->rx_qmap[i]);
		break;
	default:
		return (EOPNOTSUPP);
	}

	/* Calculate the next power-of-2 of number of queues */
	if (vsi->num_rx_queues)
		pow = flsl(vsi->num_rx_queues - 1);

	/* Assign all the queues to traffic class zero */
	qmap = (pow << ICE_AQ_VSI_TC_Q_NUM_S) & ICE_AQ_VSI_TC_Q_NUM_M;
	ctx->info.tc_mapping[0] = htole16(qmap);

	/* Fill out default driver TC queue info for VSI */
	vsi->tc_info[0].qoffset = 0;
	vsi->tc_info[0].qcount_rx = vsi->num_rx_queues;
	vsi->tc_info[0].qcount_tx = vsi->num_tx_queues;
	for (int i = 1; i < ICE_MAX_TRAFFIC_CLASS; i++) {
		vsi->tc_info[i].qoffset = 0;
		vsi->tc_info[i].qcount_rx = 1;
		vsi->tc_info[i].qcount_tx = 1;
	}
	vsi->tc_map = 0x1;

	return 0;
}

/**
 * ice_aq_add_vsi
 * @hw: pointer to the HW struct
 * @vsi_ctx: pointer to a VSI context struct
 * @cd: pointer to command details structure or NULL
 *
 * Add a VSI context to the hardware (0x0210)
 */
enum ice_status
ice_aq_add_vsi(struct ice_hw *hw, struct ice_vsi_ctx *vsi_ctx,
	       struct ice_sq_cd *cd)
{
	struct ice_aqc_add_update_free_vsi_resp *res;
	struct ice_aqc_add_get_update_free_vsi *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.vsi_cmd;
	res = &desc.params.add_update_free_vsi_res;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_add_vsi);

	if (!vsi_ctx->alloc_from_pool)
		cmd->vsi_num = htole16(vsi_ctx->vsi_num |
					   ICE_AQ_VSI_IS_VALID);
	cmd->vf_id = vsi_ctx->vf_num;

	cmd->vsi_flags = htole16(vsi_ctx->flags);

	desc.flags |= htole16(ICE_AQ_FLAG_RD);

	status = ice_aq_send_cmd(hw, &desc, &vsi_ctx->info,
				 sizeof(vsi_ctx->info), cd);

	if (!status) {
		vsi_ctx->vsi_num = le16toh(res->vsi_num) & ICE_AQ_VSI_NUM_M;
		vsi_ctx->vsis_allocd = le16toh(res->vsi_used);
		vsi_ctx->vsis_unallocated = le16toh(res->vsi_free);
	}

	return status;
}

/**
 * ice_add_vsi - add VSI context to the hardware and VSI handle list
 * @hw: pointer to the HW struct
 * @vsi_handle: unique VSI handle provided by drivers
 * @vsi_ctx: pointer to a VSI context struct
 * @cd: pointer to command details structure or NULL
 *
 * Add a VSI context to the hardware also add it into the VSI handle list.
 * If this function gets called after reset for existing VSIs then update
 * with the new HW VSI number in the corresponding VSI handle list entry.
 */
enum ice_status
ice_add_vsi(struct ice_hw *hw, uint16_t vsi_handle, struct ice_vsi_ctx *vsi_ctx,
	    struct ice_sq_cd *cd)
{
	struct ice_vsi_ctx *tmp_vsi_ctx;
	enum ice_status status;

	if (vsi_handle >= ICE_MAX_VSI)
		return ICE_ERR_PARAM;
	status = ice_aq_add_vsi(hw, vsi_ctx, cd);
	if (status)
		return status;
	tmp_vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!tmp_vsi_ctx) {
		/* Create a new VSI context */
		tmp_vsi_ctx = (struct ice_vsi_ctx *)
			ice_malloc(hw, sizeof(*tmp_vsi_ctx));
		if (!tmp_vsi_ctx) {
			ice_aq_free_vsi(hw, vsi_ctx, false, cd);
			return ICE_ERR_NO_MEMORY;
		}
		*tmp_vsi_ctx = *vsi_ctx;

		hw->vsi_ctx[vsi_handle] = tmp_vsi_ctx;
	} else {
		/* update with new HW VSI num */
		tmp_vsi_ctx->vsi_num = vsi_ctx->vsi_num;
	}

	return ICE_SUCCESS;
}

/**
 * ice_aq_suspend_sched_elems - suspend scheduler elements
 * @hw: pointer to the HW struct
 * @elems_req: number of elements to suspend
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements suspended
 * @cd: pointer to command details structure or NULL
 *
 * Suspend scheduling elements (0x0409)
 */
enum ice_status
ice_aq_suspend_sched_elems(struct ice_hw *hw, uint16_t elems_req, uint32_t *buf,
    uint16_t buf_size, uint16_t *elems_ret, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_suspend_sched_elems,
					   elems_req, (void *)buf, buf_size,
					   elems_ret, cd);
}

/**
 * ice_aq_resume_sched_elems - resume scheduler elements
 * @hw: pointer to the HW struct
 * @elems_req: number of elements to resume
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements resumed
 * @cd: pointer to command details structure or NULL
 *
 * resume scheduling elements (0x040A)
 */
enum ice_status
ice_aq_resume_sched_elems(struct ice_hw *hw, uint16_t elems_req, uint32_t *buf,
    uint16_t buf_size, uint16_t *elems_ret, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_resume_sched_elems,
					   elems_req, (void *)buf, buf_size,
					   elems_ret, cd);
}

/**
 * ice_sched_suspend_resume_elems - suspend or resume HW nodes
 * @hw: pointer to the HW struct
 * @num_nodes: number of nodes
 * @node_teids: array of node teids to be suspended or resumed
 * @suspend: true means suspend / false means resume
 *
 * This function suspends or resumes HW nodes
 */
enum ice_status
ice_sched_suspend_resume_elems(struct ice_hw *hw, uint8_t num_nodes,
    uint32_t *node_teids, bool suspend)
{
	uint16_t i, buf_size, num_elem_ret = 0;
	enum ice_status status;
	uint32_t *buf;

	buf_size = sizeof(*buf) * num_nodes;
	buf = (uint32_t *)ice_malloc(hw, buf_size);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	for (i = 0; i < num_nodes; i++)
		buf[i] = htole32(node_teids[i]);

	if (suspend)
		status = ice_aq_suspend_sched_elems(hw, num_nodes, buf,
						    buf_size, &num_elem_ret,
						    NULL);
	else
		status = ice_aq_resume_sched_elems(hw, num_nodes, buf,
						   buf_size, &num_elem_ret,
						   NULL);
	if (status != ICE_SUCCESS || num_elem_ret != num_nodes)
		DNPRINTF(ICE_DBG_SCHED, "%s: suspend/resume failed\n",
		    __func__);

	ice_free(hw, buf);
	return status;
}

/**
 * ice_sched_calc_vsi_support_nodes - calculate number of VSI support nodes
 * @pi: pointer to the port info structure
 * @tc_node: pointer to TC node
 * @num_nodes: pointer to num nodes array
 *
 * This function calculates the number of supported nodes needed to add this
 * VSI into Tx tree including the VSI, parent and intermediate nodes in below
 * layers
 */
void
ice_sched_calc_vsi_support_nodes(struct ice_port_info *pi,
    struct ice_sched_node *tc_node, uint16_t *num_nodes)
{
	struct ice_sched_node *node;
	uint8_t vsil;
	int i;

	vsil = ice_sched_get_vsi_layer(pi->hw);
	for (i = vsil; i >= pi->hw->sw_entry_point_layer; i--) {
		/* Add intermediate nodes if TC has no children and
		 * need at least one node for VSI
		 */
		if (!tc_node->num_children || i == vsil) {
			num_nodes[i]++;
		} else {
			/* If intermediate nodes are reached max children
			 * then add a new one.
			 */
			node = ice_sched_get_first_node(pi, tc_node, (uint8_t)i);
			/* scan all the siblings */
			while (node) {
				if (node->num_children <
				    pi->hw->max_children[i])
					break;
				node = node->sibling;
			}

			/* tree has one intermediate node to add this new VSI.
			 * So no need to calculate supported nodes for below
			 * layers.
			 */
			if (node)
				break;
			/* all the nodes are full, allocate a new one */
			num_nodes[i]++;
		}
	}
}

/**
 * ice_sched_add_elems - add nodes to HW and SW DB
 * @pi: port information structure
 * @tc_node: pointer to the branch node
 * @parent: pointer to the parent node
 * @layer: layer number to add nodes
 * @num_nodes: number of nodes
 * @num_nodes_added: pointer to num nodes added
 * @first_node_teid: if new nodes are added then return the TEID of first node
 * @prealloc_nodes: preallocated nodes struct for software DB
 *
 * This function add nodes to HW as well as to SW DB for a given layer
 */
enum ice_status
ice_sched_add_elems(struct ice_port_info *pi, struct ice_sched_node *tc_node,
    struct ice_sched_node *parent, uint8_t layer, uint16_t num_nodes,
    uint16_t *num_nodes_added, uint32_t *first_node_teid,
    struct ice_sched_node **prealloc_nodes)
{
	struct ice_sched_node *prev, *new_node;
	struct ice_aqc_add_elem *buf;
	uint16_t i, num_groups_added = 0;
	enum ice_status status = ICE_SUCCESS;
	struct ice_hw *hw = pi->hw;
	uint16_t buf_size;
	uint32_t teid;

	buf_size = ice_struct_size(buf, generic, num_nodes);
	buf = (struct ice_aqc_add_elem *)ice_malloc(hw, buf_size);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	buf->hdr.parent_teid = parent->info.node_teid;
	buf->hdr.num_elems = htole16(num_nodes);
	for (i = 0; i < num_nodes; i++) {
		buf->generic[i].parent_teid = parent->info.node_teid;
		buf->generic[i].data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
		buf->generic[i].data.valid_sections =
			ICE_AQC_ELEM_VALID_GENERIC | ICE_AQC_ELEM_VALID_CIR |
			ICE_AQC_ELEM_VALID_EIR;
		buf->generic[i].data.generic = 0;
		buf->generic[i].data.cir_bw.bw_profile_idx =
			htole16(ICE_SCHED_DFLT_RL_PROF_ID);
		buf->generic[i].data.cir_bw.bw_alloc =
			htole16(ICE_SCHED_DFLT_BW_WT);
		buf->generic[i].data.eir_bw.bw_profile_idx =
			htole16(ICE_SCHED_DFLT_RL_PROF_ID);
		buf->generic[i].data.eir_bw.bw_alloc =
			htole16(ICE_SCHED_DFLT_BW_WT);
	}

	status = ice_aq_add_sched_elems(hw, 1, buf, buf_size,
					&num_groups_added, NULL);
	if (status != ICE_SUCCESS || num_groups_added != 1) {
		DNPRINTF(ICE_DBG_SCHED, "%s: add node failed FW Error %d\n",
			  __func__, hw->adminq.sq_last_status);
		ice_free(hw, buf);
		return ICE_ERR_CFG;
	}

	*num_nodes_added = num_nodes;
	/* add nodes to the SW DB */
	for (i = 0; i < num_nodes; i++) {
		if (prealloc_nodes) {
			status = ice_sched_add_node(pi, layer,
			    &buf->generic[i], prealloc_nodes[i]);
		} else {
			status = ice_sched_add_node(pi, layer,
			    &buf->generic[i], NULL);
		}

		if (status != ICE_SUCCESS) {
			DNPRINTF(ICE_DBG_SCHED,
			    "%s: add nodes in SW DB failed status =%d\n",
			    __func__, status);
			break;
		}

		teid = le32toh(buf->generic[i].node_teid);
		new_node = ice_sched_find_node_by_teid(parent, teid);
		if (!new_node) {
			DNPRINTF(ICE_DBG_SCHED,
			    "%s: Node is missing for teid =%d\n",
			    __func__, teid);
			break;
		}

		new_node->sibling = NULL;
		new_node->tc_num = tc_node->tc_num;

		/* add it to previous node sibling pointer */
		/* Note: siblings are not linked across branches */
		prev = ice_sched_get_first_node(pi, tc_node, layer);
		if (prev && prev != new_node) {
			while (prev->sibling)
				prev = prev->sibling;
			prev->sibling = new_node;
		}

		/* initialize the sibling head */
		if (!pi->sib_head[tc_node->tc_num][layer])
			pi->sib_head[tc_node->tc_num][layer] = new_node;

		if (i == 0)
			*first_node_teid = teid;
	}

	ice_free(hw, buf);
	return status;
}

/**
 * ice_sched_add_nodes_to_hw_layer - Add nodes to hw layer
 * @pi: port information structure
 * @tc_node: pointer to TC node
 * @parent: pointer to parent node
 * @layer: layer number to add nodes
 * @num_nodes: number of nodes to be added
 * @first_node_teid: pointer to the first node TEID
 * @num_nodes_added: pointer to number of nodes added
 *
 * Add nodes into specific hw layer.
 */
enum ice_status
ice_sched_add_nodes_to_hw_layer(struct ice_port_info *pi,
				struct ice_sched_node *tc_node,
				struct ice_sched_node *parent, uint8_t layer,
				uint16_t num_nodes, uint32_t *first_node_teid,
				uint16_t *num_nodes_added)
{
	uint16_t max_child_nodes;

	*num_nodes_added = 0;

	if (!num_nodes)
		return ICE_SUCCESS;

	if (!parent || layer < pi->hw->sw_entry_point_layer)
		return ICE_ERR_PARAM;

	/* max children per node per layer */
	max_child_nodes = pi->hw->max_children[parent->tx_sched_layer];

	/* current number of children + required nodes exceed max children */
	if ((parent->num_children + num_nodes) > max_child_nodes) {
		/* Fail if the parent is a TC node */
		if (parent == tc_node)
			return ICE_ERR_CFG;
		return ICE_ERR_MAX_LIMIT;
	}

	return ice_sched_add_elems(pi, tc_node, parent, layer, num_nodes,
				   num_nodes_added, first_node_teid, NULL);
}

/**
 * ice_sched_add_nodes_to_layer - Add nodes to a given layer
 * @pi: port information structure
 * @tc_node: pointer to TC node
 * @parent: pointer to parent node
 * @layer: layer number to add nodes
 * @num_nodes: number of nodes to be added
 * @first_node_teid: pointer to the first node TEID
 * @num_nodes_added: pointer to number of nodes added
 *
 * This function add nodes to a given layer.
 */
enum ice_status
ice_sched_add_nodes_to_layer(struct ice_port_info *pi,
			     struct ice_sched_node *tc_node,
			     struct ice_sched_node *parent, uint8_t layer,
			     uint16_t num_nodes, uint32_t *first_node_teid,
			     uint16_t *num_nodes_added)
{
	uint32_t *first_teid_ptr = first_node_teid;
	uint16_t new_num_nodes = num_nodes;
	enum ice_status status = ICE_SUCCESS;
	uint32_t temp;

	*num_nodes_added = 0;
	while (*num_nodes_added < num_nodes) {
		uint16_t max_child_nodes, num_added = 0;

		status = ice_sched_add_nodes_to_hw_layer(pi, tc_node, parent,
							 layer,	new_num_nodes,
							 first_teid_ptr,
							 &num_added);
		if (status == ICE_SUCCESS)
			*num_nodes_added += num_added;
		/* added more nodes than requested ? */
		if (*num_nodes_added > num_nodes) {
			DNPRINTF(ICE_DBG_SCHED, "%s: added extra nodes %d %d\n",
			    __func__, num_nodes, *num_nodes_added);
			status = ICE_ERR_CFG;
			break;
		}
		/* break if all the nodes are added successfully */
		if (status == ICE_SUCCESS && (*num_nodes_added == num_nodes))
			break;
		/* break if the error is not max limit */
		if (status != ICE_SUCCESS && status != ICE_ERR_MAX_LIMIT)
			break;
		/* Exceeded the max children */
		max_child_nodes = pi->hw->max_children[parent->tx_sched_layer];
		/* utilize all the spaces if the parent is not full */
		if (parent->num_children < max_child_nodes) {
			new_num_nodes = max_child_nodes - parent->num_children;
		} else {
			/* This parent is full, try the next sibling */
			parent = parent->sibling;
			/* Don't modify the first node TEID memory if the
			 * first node was added already in the above call.
			 * Instead send some temp memory for all other
			 * recursive calls.
			 */
			if (num_added)
				first_teid_ptr = &temp;

			new_num_nodes = num_nodes - *num_nodes_added;
		}
	}

	return status;
}

/**
 * ice_sched_add_vsi_support_nodes - add VSI supported nodes into Tx tree
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc_node: pointer to TC node
 * @num_nodes: pointer to num nodes array
 *
 * This function adds the VSI supported nodes into Tx tree including the
 * VSI, its parent and intermediate nodes in below layers
 */
enum ice_status
ice_sched_add_vsi_support_nodes(struct ice_port_info *pi, uint16_t vsi_handle,
    struct ice_sched_node *tc_node, uint16_t *num_nodes)
{
	struct ice_sched_node *parent = tc_node;
	uint32_t first_node_teid;
	uint16_t num_added = 0;
	uint8_t i, vsil;

	if (!pi)
		return ICE_ERR_PARAM;

	vsil = ice_sched_get_vsi_layer(pi->hw);
	for (i = pi->hw->sw_entry_point_layer; i <= vsil; i++) {
		enum ice_status status;

		status = ice_sched_add_nodes_to_layer(pi, tc_node, parent,
						      i, num_nodes[i],
						      &first_node_teid,
						      &num_added);
		if (status != ICE_SUCCESS || num_nodes[i] != num_added)
			return ICE_ERR_CFG;

		/* The newly added node can be a new parent for the next
		 * layer nodes
		 */
		if (num_added)
			parent = ice_sched_find_node_by_teid(tc_node,
							     first_node_teid);
		else
			parent = parent->children[0];

		if (!parent)
			return ICE_ERR_CFG;

		if (i == vsil)
			parent->vsi_handle = vsi_handle;
	}

	return ICE_SUCCESS;
}

/**
 * ice_sched_add_vsi_to_topo - add a new VSI into tree
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 *
 * This function adds a new VSI into scheduler tree
 */
enum ice_status
ice_sched_add_vsi_to_topo(struct ice_port_info *pi, uint16_t vsi_handle,
    uint8_t tc)
{
	uint16_t num_nodes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	struct ice_sched_node *tc_node;

	tc_node = ice_sched_get_tc_node(pi, tc);
	if (!tc_node)
		return ICE_ERR_PARAM;

	/* calculate number of supported nodes needed for this VSI */
	ice_sched_calc_vsi_support_nodes(pi, tc_node, num_nodes);

	/* add VSI supported nodes to TC subtree */
	return ice_sched_add_vsi_support_nodes(pi, vsi_handle, tc_node,
					       num_nodes);
}

/**
 * ice_alloc_lan_q_ctx - allocate LAN queue contexts for the given VSI and TC
 * @hw: pointer to the HW struct
 * @vsi_handle: VSI handle
 * @tc: TC number
 * @new_numqs: number of queues
 */
enum ice_status
ice_alloc_lan_q_ctx(struct ice_hw *hw, uint16_t vsi_handle, uint8_t tc,
    uint16_t new_numqs)
{
	struct ice_vsi_ctx *vsi_ctx;
	struct ice_q_ctx *q_ctx;

	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return ICE_ERR_PARAM;

	/* allocate LAN queue contexts */
	if (!vsi_ctx->lan_q_ctx[tc]) {
		vsi_ctx->lan_q_ctx[tc] = (struct ice_q_ctx *)
			ice_calloc(hw, new_numqs, sizeof(*q_ctx));
		if (!vsi_ctx->lan_q_ctx[tc])
			return ICE_ERR_NO_MEMORY;
		vsi_ctx->num_lan_q_entries[tc] = new_numqs;
		return ICE_SUCCESS;
	}

	/* num queues are increased, update the queue contexts */
	if (new_numqs > vsi_ctx->num_lan_q_entries[tc]) {
		uint16_t prev_num = vsi_ctx->num_lan_q_entries[tc];

		q_ctx = (struct ice_q_ctx *)
			ice_calloc(hw, new_numqs, sizeof(*q_ctx));
		if (!q_ctx)
			return ICE_ERR_NO_MEMORY;
		memcpy(q_ctx, vsi_ctx->lan_q_ctx[tc],
		    prev_num * sizeof(*q_ctx));
		ice_free(hw, vsi_ctx->lan_q_ctx[tc]);
		vsi_ctx->lan_q_ctx[tc] = q_ctx;
		vsi_ctx->num_lan_q_entries[tc] = new_numqs;
	}

	return ICE_SUCCESS;
}

/**
 * ice_sched_calc_vsi_child_nodes - calculate number of VSI child nodes
 * @hw: pointer to the HW struct
 * @num_qs: number of queues
 * @num_nodes: num nodes array
 *
 * This function calculates the number of VSI child nodes based on the
 * number of queues.
 */
void
ice_sched_calc_vsi_child_nodes(struct ice_hw *hw, uint16_t num_qs, uint16_t *num_nodes)
{
	uint16_t num = num_qs;
	uint8_t i, qgl, vsil;

	qgl = ice_sched_get_qgrp_layer(hw);
	vsil = ice_sched_get_vsi_layer(hw);

	/* calculate num nodes from queue group to VSI layer */
	for (i = qgl; i > vsil; i--) {
		/* round to the next integer if there is a remainder */
		num = howmany(num, hw->max_children[i]);

		/* need at least one node */
		num_nodes[i] = num ? num : 1;
	}
}

/**
 * ice_sched_add_vsi_child_nodes - add VSI child nodes to tree
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc_node: pointer to the TC node
 * @num_nodes: pointer to the num nodes that needs to be added per layer
 * @owner: node owner (LAN or RDMA)
 *
 * This function adds the VSI child nodes to tree. It gets called for
 * LAN and RDMA separately.
 */
enum ice_status
ice_sched_add_vsi_child_nodes(struct ice_port_info *pi, uint16_t vsi_handle,
    struct ice_sched_node *tc_node, uint16_t *num_nodes, uint8_t owner)
{
	struct ice_sched_node *parent, *node;
	struct ice_hw *hw = pi->hw;
	uint32_t first_node_teid;
	uint16_t num_added = 0;
	uint8_t i, qgl, vsil;

	qgl = ice_sched_get_qgrp_layer(hw);
	vsil = ice_sched_get_vsi_layer(hw);
	parent = ice_sched_get_vsi_node(pi, tc_node, vsi_handle);
	for (i = vsil + 1; i <= qgl; i++) {
		enum ice_status status;

		if (!parent)
			return ICE_ERR_CFG;

		status = ice_sched_add_nodes_to_layer(pi, tc_node, parent, i,
						      num_nodes[i],
						      &first_node_teid,
						      &num_added);
		if (status != ICE_SUCCESS || num_nodes[i] != num_added)
			return ICE_ERR_CFG;

		/* The newly added node can be a new parent for the next
		 * layer nodes
		 */
		if (num_added) {
			parent = ice_sched_find_node_by_teid(tc_node,
							     first_node_teid);
			node = parent;
			while (node) {
				node->owner = owner;
				node = node->sibling;
			}
		} else {
			parent = parent->children[0];
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_sched_update_vsi_child_nodes - update VSI child nodes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @new_numqs: new number of max queues
 * @owner: owner of this subtree
 *
 * This function updates the VSI child nodes based on the number of queues
 */
enum ice_status
ice_sched_update_vsi_child_nodes(struct ice_port_info *pi, uint16_t vsi_handle,
				 uint8_t tc, uint16_t new_numqs, uint8_t owner)
{
	uint16_t new_num_nodes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	struct ice_sched_node *vsi_node;
	struct ice_sched_node *tc_node;
	struct ice_vsi_ctx *vsi_ctx;
	enum ice_status status = ICE_SUCCESS;
	struct ice_hw *hw = pi->hw;
	uint16_t prev_numqs;

	tc_node = ice_sched_get_tc_node(pi, tc);
	if (!tc_node)
		return ICE_ERR_CFG;

	vsi_node = ice_sched_get_vsi_node(pi, tc_node, vsi_handle);
	if (!vsi_node)
		return ICE_ERR_CFG;

	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return ICE_ERR_PARAM;

	if (owner == ICE_SCHED_NODE_OWNER_LAN)
		prev_numqs = vsi_ctx->sched.max_lanq[tc];
	else
		prev_numqs = vsi_ctx->sched.max_rdmaq[tc];
	/* num queues are not changed or less than the previous number */
	if (new_numqs <= prev_numqs)
		return status;
	if (owner == ICE_SCHED_NODE_OWNER_LAN) {
		status = ice_alloc_lan_q_ctx(hw, vsi_handle, tc, new_numqs);
		if (status)
			return status;
	} else {
#if 0
		status = ice_alloc_rdma_q_ctx(hw, vsi_handle, tc, new_numqs);
		if (status)
			return status;
#else
		return ICE_ERR_NOT_IMPL;
#endif
	}

	if (new_numqs)
		ice_sched_calc_vsi_child_nodes(hw, new_numqs, new_num_nodes);
	/* Keep the max number of queue configuration all the time. Update the
	 * tree only if number of queues > previous number of queues. This may
	 * leave some extra nodes in the tree if number of queues < previous
	 * number but that wouldn't harm anything. Removing those extra nodes
	 * may complicate the code if those nodes are part of SRL or
	 * individually rate limited.
	 */
	status = ice_sched_add_vsi_child_nodes(pi, vsi_handle, tc_node,
					       new_num_nodes, owner);
	if (status)
		return status;
	if (owner == ICE_SCHED_NODE_OWNER_LAN)
		vsi_ctx->sched.max_lanq[tc] = new_numqs;
	else
		vsi_ctx->sched.max_rdmaq[tc] = new_numqs;

	return ICE_SUCCESS;
}

/**
 * ice_sched_cfg_vsi - configure the new/existing VSI
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @maxqs: max number of queues
 * @owner: LAN or RDMA
 * @enable: TC enabled or disabled
 *
 * This function adds/updates VSI nodes based on the number of queues. If TC is
 * enabled and VSI is in suspended state then resume the VSI back. If TC is
 * disabled then suspend the VSI if it is not already.
 */
enum ice_status
ice_sched_cfg_vsi(struct ice_port_info *pi, uint16_t vsi_handle, uint8_t tc,
    uint16_t maxqs, uint8_t owner, bool enable)
{
	struct ice_sched_node *vsi_node, *tc_node;
	struct ice_vsi_ctx *vsi_ctx;
	enum ice_status status = ICE_SUCCESS;
	struct ice_hw *hw = pi->hw;

	DNPRINTF(ICE_DBG_SCHED, "%s: add/config VSI %d\n",
	    __func__, vsi_handle);
	tc_node = ice_sched_get_tc_node(pi, tc);
	if (!tc_node)
		return ICE_ERR_PARAM;
	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return ICE_ERR_PARAM;
	vsi_node = ice_sched_get_vsi_node(pi, tc_node, vsi_handle);

	/* suspend the VSI if TC is not enabled */
	if (!enable) {
		if (vsi_node && vsi_node->in_use) {
			uint32_t teid = le32toh(vsi_node->info.node_teid);

			status = ice_sched_suspend_resume_elems(hw, 1, &teid,
								true);
			if (!status)
				vsi_node->in_use = false;
		}
		return status;
	}

	/* TC is enabled, if it is a new VSI then add it to the tree */
	if (!vsi_node) {
		status = ice_sched_add_vsi_to_topo(pi, vsi_handle, tc);
		if (status)
			return status;

		vsi_node = ice_sched_get_vsi_node(pi, tc_node, vsi_handle);
		if (!vsi_node)
			return ICE_ERR_CFG;

		vsi_ctx->sched.vsi_node[tc] = vsi_node;
		vsi_node->in_use = true;
		/* invalidate the max queues whenever VSI gets added first time
		 * into the scheduler tree (boot or after reset). We need to
		 * recreate the child nodes all the time in these cases.
		 */
		vsi_ctx->sched.max_lanq[tc] = 0;
		vsi_ctx->sched.max_rdmaq[tc] = 0;
	}

	/* update the VSI child nodes */
	status = ice_sched_update_vsi_child_nodes(pi, vsi_handle, tc, maxqs,
						  owner);
	if (status)
		return status;

	/* TC is enabled, resume the VSI if it is in the suspend state */
	if (!vsi_node->in_use) {
		uint32_t teid = le32toh(vsi_node->info.node_teid);

		status = ice_sched_suspend_resume_elems(hw, 1, &teid, false);
		if (!status)
			vsi_node->in_use = true;
	}

	return status;
}

static inline bool ice_is_tc_ena(ice_bitmap_t bitmap, uint8_t tc)
{
	return !!(bitmap & BIT(tc));
}

/**
 * ice_cfg_vsi_qs - configure the new/existing VSI queues
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc_bitmap: TC bitmap
 * @maxqs: max queues array per TC
 * @owner: LAN or RDMA
 *
 * This function adds/updates the VSI queues per TC.
 */
enum ice_status
ice_cfg_vsi_qs(struct ice_port_info *pi, uint16_t vsi_handle,
    uint16_t tc_bitmap, uint16_t *maxqs, uint8_t owner)
{
	enum ice_status status = ICE_SUCCESS;
	uint8_t i;

	if (!pi || pi->port_state != ICE_SCHED_PORT_STATE_READY)
		return ICE_ERR_CFG;

	if (!ice_is_vsi_valid(pi->hw, vsi_handle))
		return ICE_ERR_PARAM;
#if 0
	ice_acquire_lock(&pi->sched_lock);
#endif
	ice_for_each_traffic_class(i) {
		/* configuration is possible only if TC node is present */
		if (!ice_sched_get_tc_node(pi, i))
			continue;

		status = ice_sched_cfg_vsi(pi, vsi_handle, i, maxqs[i], owner,
					   ice_is_tc_ena(tc_bitmap, i));
		if (status)
			break;
	}
#if 0
	ice_release_lock(&pi->sched_lock);
#endif
	return status;
}

/**
 * ice_cfg_vsi_lan - configure VSI LAN queues
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc_bitmap: TC bitmap
 * @max_lanqs: max LAN queues array per TC
 *
 * This function adds/updates the VSI LAN queues per TC.
 */
enum ice_status
ice_cfg_vsi_lan(struct ice_port_info *pi, uint16_t vsi_handle,
    uint16_t tc_bitmap, uint16_t *max_lanqs)
{
	return ice_cfg_vsi_qs(pi, vsi_handle, tc_bitmap, max_lanqs,
			      ICE_SCHED_NODE_OWNER_LAN);
}

/**
 * ice_reset_vsi_stats - Reset VSI statistics counters
 * @vsi: VSI structure
 *
 * Resets the software tracking counters for the VSI statistics, and indicate
 * that the offsets haven't been loaded. This is intended to be called
 * post-reset so that VSI statistics count from zero again.
 */
void
ice_reset_vsi_stats(struct ice_vsi *vsi)
{
	/* Reset HW stats */
	memset(&vsi->hw_stats.prev, 0, sizeof(vsi->hw_stats.prev));
	memset(&vsi->hw_stats.cur, 0, sizeof(vsi->hw_stats.cur));
	vsi->hw_stats.offsets_loaded = false;
}

/**
 * ice_initialize_vsi - Initialize a VSI for use
 * @vsi: the vsi to initialize
 *
 * Initialize a VSI over the adminq and prepare it for operation.
 *
 * @pre vsi->num_tx_queues is set
 * @pre vsi->num_rx_queues is set
 */
int
ice_initialize_vsi(struct ice_vsi *vsi)
{
	struct ice_vsi_ctx ctx = { 0 };
	struct ice_hw *hw = &vsi->sc->hw;
	uint16_t max_txqs[ICE_MAX_TRAFFIC_CLASS] = { 0 };
	enum ice_status status;
	int err;

	/* For now, we only have code supporting PF VSIs */
	switch (vsi->type) {
	case ICE_VSI_PF:
		ctx.flags = ICE_AQ_VSI_TYPE_PF;
		break;
	case ICE_VSI_VMDQ2:
		ctx.flags = ICE_AQ_VSI_TYPE_VMDQ2;
		break;
	default:
		return (ENODEV);
	}

	ice_set_default_vsi_ctx(&ctx);
	ice_set_rss_vsi_ctx(&ctx, vsi->type);

	/* XXX: VSIs of other types may need different port info? */
	ctx.info.sw_id = hw->port_info->sw_id;

	/* Set some RSS parameters based on the VSI type */
	ice_vsi_set_rss_params(vsi);

	/* Initialize the Rx queue mapping for this VSI */
	err = ice_setup_vsi_qmap(vsi, &ctx);
	if (err)
		return err;

	/* (Re-)add VSI to HW VSI handle list */
	status = ice_add_vsi(hw, vsi->idx, &ctx, NULL);
	if (status != 0) {
		DPRINTF("%s: Add VSI AQ call failed, err %s aq_err %s\n",
		    __func__, ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		return (EIO);
	}
	vsi->info = ctx.info;

	/* Initialize VSI with just 1 TC to start */
	max_txqs[0] = vsi->num_tx_queues;

	status = ice_cfg_vsi_lan(hw->port_info, vsi->idx,
	    ICE_DFLT_TRAFFIC_CLASS, max_txqs);
	if (status) {
		DPRINTF("%s: Failed VSI lan queue config, err %s aq_err %s\n",
		    __func__, ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		ice_deinit_vsi(vsi);
		return (ENODEV);
	}

	/* Reset VSI stats */
	ice_reset_vsi_stats(vsi);

	return 0;
}

/**
 * ice_sched_replay_agg - recreate aggregator node(s)
 * @hw: pointer to the HW struct
 *
 * This function recreate aggregator type nodes which are not replayed earlier.
 * It also replay aggregator BW information. These aggregator nodes are not
 * associated with VSI type node yet.
 */
void
ice_sched_replay_agg(struct ice_hw *hw)
{
#if 0
	struct ice_port_info *pi = hw->port_info;
	struct ice_sched_agg_info *agg_info;

	ice_acquire_lock(&pi->sched_lock);
	LIST_FOR_EACH_ENTRY(agg_info, &hw->agg_list, ice_sched_agg_info,
			    list_entry)
		/* replay aggregator (re-create aggregator node) */
		if (!ice_cmp_bitmap(agg_info->tc_bitmap,
				    agg_info->replay_tc_bitmap,
				    ICE_MAX_TRAFFIC_CLASS)) {
			ice_declare_bitmap(replay_bitmap,
					   ICE_MAX_TRAFFIC_CLASS);
			enum ice_status status;

			ice_zero_bitmap(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
			ice_sched_get_ena_tc_bitmap(pi,
						    agg_info->replay_tc_bitmap,
						    replay_bitmap);
			status = ice_sched_cfg_agg(hw->port_info,
						   agg_info->agg_id,
						   ICE_AGG_TYPE_AGG,
						   replay_bitmap);
			if (status) {
				ice_info(hw, "Replay agg id[%d] failed\n",
					 agg_info->agg_id);
				/* Move on to next one */
				continue;
			}
			/* Replay aggregator node BW (restore aggregator BW) */
			status = ice_sched_replay_agg_bw(hw, agg_info);
			if (status)
				ice_info(hw, "Replay agg bw [id=%d] failed\n",
					 agg_info->agg_id);
		}
	ice_release_lock(&pi->sched_lock);
#endif
}

/**
 * ice_replay_post - post replay configuration cleanup
 * @hw: pointer to the HW struct
 *
 * Post replay cleanup.
 */
void ice_replay_post(struct ice_hw *hw)
{
	/* Delete old entries from replay filter list head */
	ice_rm_sw_replay_rule_info(hw, hw->switch_info);
	ice_sched_replay_agg(hw);
}

/**
 * ice_is_main_vsi - checks whether the VSI is main VSI
 * @hw: pointer to the HW struct
 * @vsi_handle: VSI handle
 *
 * Checks whether the VSI is the main VSI (the first PF VSI created on
 * given PF).
 */
bool
ice_is_main_vsi(struct ice_hw *hw, uint16_t vsi_handle)
{
	return vsi_handle == ICE_MAIN_VSI_HANDLE && hw->vsi_ctx[vsi_handle];
}

/**
 * ice_replay_pre_init - replay pre initialization
 * @hw: pointer to the HW struct
 * @sw: pointer to switch info struct for which function initializes filters
 *
 * Initializes required config data for VSI, FD, ACL, and RSS before replay.
 */
enum ice_status
ice_replay_pre_init(struct ice_hw *hw, struct ice_switch_info *sw)
{
#if 0
	enum ice_status status;
	uint8_t i;

	/* Delete old entries from replay filter list head if there is any */
	ice_rm_sw_replay_rule_info(hw, sw);
	/* In start of replay, move entries into replay_rules list, it
	 * will allow adding rules entries back to filt_rules list,
	 * which is operational list.
	 */
	for (i = 0; i < ICE_MAX_NUM_RECIPES; i++)
		LIST_REPLACE_INIT(&sw->recp_list[i].filt_rules,
				  &sw->recp_list[i].filt_replay_rules);
	ice_sched_replay_agg_vsi_preinit(hw);

	status = ice_sched_replay_root_node_bw(hw->port_info);
	if (status)
		return status;

	return ice_sched_replay_tc_node_bw(hw->port_info);
#else
	return ICE_ERR_NOT_IMPL;
#endif
}

/**
 * ice_replay_rss_cfg - replay RSS configurations associated with VSI
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 */
enum ice_status
ice_replay_rss_cfg(struct ice_hw *hw, uint16_t vsi_handle)
{
#if 0
	enum ice_status status = ICE_SUCCESS;
	struct ice_rss_cfg *r;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;

	ice_acquire_lock(&hw->rss_locks);
	LIST_FOR_EACH_ENTRY(r, &hw->rss_list_head,
			    ice_rss_cfg, l_entry) {
		if (ice_is_bit_set(r->vsis, vsi_handle)) {
			status = ice_add_rss_cfg_sync(hw, vsi_handle, &r->hash);
			if (status)
				break;
		}
	}
	ice_release_lock(&hw->rss_locks);

	return status;
#else
	return ICE_ERR_NOT_IMPL;
#endif
}

/**
 * ice_find_vsi_list_entry - Search VSI list map with VSI count 1
 * @recp_list: VSI lists needs to be searched
 * @vsi_handle: VSI handle to be found in VSI list
 * @vsi_list_id: VSI list ID found containing vsi_handle
 *
 * Helper function to search a VSI list with single entry containing given VSI
 * handle element. This can be extended further to search VSI list with more
 * than 1 vsi_count. Returns pointer to VSI list entry if found.
 */
struct ice_vsi_list_map_info *
ice_find_vsi_list_entry(struct ice_sw_recipe *recp_list, uint16_t vsi_handle,
			uint16_t *vsi_list_id)
{
	struct ice_vsi_list_map_info *map_info = NULL;

	if (recp_list->adv_rule) {
		struct ice_adv_fltr_mgmt_list_head *adv_list_head;
		struct ice_adv_fltr_mgmt_list_entry *list_itr;

		adv_list_head = &recp_list->adv_filt_rules;
		TAILQ_FOREACH(list_itr, adv_list_head, list_entry) {
			if (list_itr->vsi_list_info) {
				map_info = list_itr->vsi_list_info;
				if (ice_is_bit_set(map_info->vsi_map,
						   vsi_handle)) {
					*vsi_list_id = map_info->vsi_list_id;
					return map_info;
				}
			}
		}
	} else {
		struct ice_fltr_mgmt_list_head *list_head;
		struct ice_fltr_mgmt_list_entry *list_itr;

		list_head = &recp_list->filt_rules;
		TAILQ_FOREACH(list_itr, list_head, list_entry) {
			if (list_itr->vsi_count == 1 &&
			    list_itr->vsi_list_info) {
				map_info = list_itr->vsi_list_info;
				if (ice_is_bit_set(map_info->vsi_map,
						   vsi_handle)) {
					*vsi_list_id = map_info->vsi_list_id;
					return map_info;
				}
			}
		}
	}

	return NULL;
}

/**
 * ice_add_vlan_internal - Add one VLAN based filter rule
 * @hw: pointer to the hardware structure
 * @recp_list: recipe list for which rule has to be added
 * @f_entry: filter entry containing one VLAN information
 */
enum ice_status
ice_add_vlan_internal(struct ice_hw *hw, struct ice_sw_recipe *recp_list,
		      struct ice_fltr_list_entry *f_entry)
{
	struct ice_fltr_mgmt_list_entry *v_list_itr;
	struct ice_fltr_info *new_fltr, *cur_fltr;
	enum ice_sw_lkup_type lkup_type;
	uint16_t vsi_list_id = 0, vsi_handle;
#if 0
	struct ice_lock *rule_lock; /* Lock to protect filter rule list */
#endif
	enum ice_status status = ICE_SUCCESS;

	if (!ice_is_vsi_valid(hw, f_entry->fltr_info.vsi_handle))
		return ICE_ERR_PARAM;

	f_entry->fltr_info.fwd_id.hw_vsi_id =
		hw->vsi_ctx[f_entry->fltr_info.vsi_handle]->vsi_num;
	new_fltr = &f_entry->fltr_info;

	/* VLAN ID should only be 12 bits */
	if (new_fltr->l_data.vlan.vlan_id > ICE_MAX_VLAN_ID)
		return ICE_ERR_PARAM;

	if (new_fltr->src_id != ICE_SRC_ID_VSI)
		return ICE_ERR_PARAM;

	new_fltr->src = new_fltr->fwd_id.hw_vsi_id;
	lkup_type = new_fltr->lkup_type;
	vsi_handle = new_fltr->vsi_handle;
#if 0
	rule_lock = &recp_list->filt_rule_lock;
	ice_acquire_lock(rule_lock);
#endif
	v_list_itr = ice_find_rule_entry(&recp_list->filt_rules, new_fltr);
	if (!v_list_itr) {
		struct ice_vsi_list_map_info *map_info = NULL;

		if (new_fltr->fltr_act == ICE_FWD_TO_VSI) {
			/* All VLAN pruning rules use a VSI list. Check if
			 * there is already a VSI list containing VSI that we
			 * want to add. If found, use the same vsi_list_id for
			 * this new VLAN rule or else create a new list.
			 */
			map_info = ice_find_vsi_list_entry(recp_list,
							   vsi_handle,
							   &vsi_list_id);
			if (!map_info) {
				status = ice_create_vsi_list_rule(hw,
								  &vsi_handle,
								  1,
								  &vsi_list_id,
								  lkup_type);
				if (status)
					goto exit;
			}
			/* Convert the action to forwarding to a VSI list. */
			new_fltr->fltr_act = ICE_FWD_TO_VSI_LIST;
			new_fltr->fwd_id.vsi_list_id = vsi_list_id;
		}

		status = ice_create_pkt_fwd_rule(hw, recp_list, f_entry);
		if (!status) {
			v_list_itr = ice_find_rule_entry(&recp_list->filt_rules,
							 new_fltr);
			if (!v_list_itr) {
				status = ICE_ERR_DOES_NOT_EXIST;
				goto exit;
			}
			/* reuse VSI list for new rule and increment ref_cnt */
			if (map_info) {
				v_list_itr->vsi_list_info = map_info;
				map_info->ref_cnt++;
			} else {
				v_list_itr->vsi_list_info =
					ice_create_vsi_list_map(hw, &vsi_handle,
								1, vsi_list_id);
			}
		}
	} else if (v_list_itr->vsi_list_info->ref_cnt == 1) {
		/* Update existing VSI list to add new VSI ID only if it used
		 * by one VLAN rule.
		 */
		cur_fltr = &v_list_itr->fltr_info;
		status = ice_add_update_vsi_list(hw, v_list_itr, cur_fltr,
						 new_fltr);
	} else {
		/* If VLAN rule exists and VSI list being used by this rule is
		 * referenced by more than 1 VLAN rule. Then create a new VSI
		 * list appending previous VSI with new VSI and update existing
		 * VLAN rule to point to new VSI list ID
		 */
		struct ice_fltr_info tmp_fltr;
		uint16_t vsi_handle_arr[2];
		uint16_t cur_handle;

		/* Current implementation only supports reusing VSI list with
		 * one VSI count. We should never hit below condition
		 */
		if (v_list_itr->vsi_count > 1 &&
		    v_list_itr->vsi_list_info->ref_cnt > 1) {
			DNPRINTF(ICE_DBG_SW, "%s: Invalid configuration: "
			    "Optimization to reuse VSI list with more than "
			    "one VSI is not being done yet\n", __func__);
			status = ICE_ERR_CFG;
			goto exit;
		}

		cur_handle =
			ice_find_first_bit(v_list_itr->vsi_list_info->vsi_map,
					   ICE_MAX_VSI);

		/* A rule already exists with the new VSI being added */
		if (cur_handle == vsi_handle) {
			status = ICE_ERR_ALREADY_EXISTS;
			goto exit;
		}

		vsi_handle_arr[0] = cur_handle;
		vsi_handle_arr[1] = vsi_handle;
		status = ice_create_vsi_list_rule(hw, &vsi_handle_arr[0], 2,
						  &vsi_list_id, lkup_type);
		if (status)
			goto exit;

		tmp_fltr = v_list_itr->fltr_info;
		tmp_fltr.fltr_rule_id = v_list_itr->fltr_info.fltr_rule_id;
		tmp_fltr.fwd_id.vsi_list_id = vsi_list_id;
		tmp_fltr.fltr_act = ICE_FWD_TO_VSI_LIST;
		/* Update the previous switch rule to a new VSI list which
		 * includes current VSI that is requested
		 */
		status = ice_update_pkt_fwd_rule(hw, &tmp_fltr);
		if (status)
			goto exit;

		/* before overriding VSI list map info. decrement ref_cnt of
		 * previous VSI list
		 */
		v_list_itr->vsi_list_info->ref_cnt--;

		/* now update to newly created list */
		v_list_itr->fltr_info.fwd_id.vsi_list_id = vsi_list_id;
		v_list_itr->vsi_list_info =
			ice_create_vsi_list_map(hw, &vsi_handle_arr[0], 2,
						vsi_list_id);
		v_list_itr->vsi_count++;
	}

exit:
#if 0
	ice_release_lock(rule_lock);
#endif
	return status;
}

/**
 * ice_replay_vsi_fltr - Replay filters for requested VSI
 * @hw: pointer to the hardware structure
 * @pi: pointer to port information structure
 * @sw: pointer to switch info struct for which function replays filters
 * @vsi_handle: driver VSI handle
 * @recp_id: Recipe ID for which rules need to be replayed
 * @list_head: list for which filters need to be replayed
 *
 * Replays the filter of recipe recp_id for a VSI represented via vsi_handle.
 * It is required to pass valid VSI handle.
 */
enum ice_status
ice_replay_vsi_fltr(struct ice_hw *hw, struct ice_port_info *pi,
		    struct ice_switch_info *sw, uint16_t vsi_handle,
		    uint8_t recp_id,
		    struct ice_fltr_mgmt_list_head *list_head)
{
	struct ice_fltr_mgmt_list_entry *itr;
	enum ice_status status = ICE_SUCCESS;
	struct ice_sw_recipe *recp_list;
	uint16_t hw_vsi_id;

	if (TAILQ_EMPTY(list_head))
		return status;
	recp_list = &sw->recp_list[recp_id];
	hw_vsi_id = hw->vsi_ctx[vsi_handle]->vsi_num;

	TAILQ_FOREACH(itr, list_head, list_entry) {
		struct ice_fltr_list_entry f_entry;

		f_entry.fltr_info = itr->fltr_info;
		if (itr->vsi_count < 2 && recp_id != ICE_SW_LKUP_VLAN &&
		    itr->fltr_info.vsi_handle == vsi_handle) {
			/* update the src in case it is VSI num */
			if (f_entry.fltr_info.src_id == ICE_SRC_ID_VSI)
				f_entry.fltr_info.src = hw_vsi_id;
			status = ice_add_rule_internal(hw, recp_list,
						       pi->lport,
						       &f_entry);
			if (status != ICE_SUCCESS)
				goto end;
			continue;
		}
		if (!itr->vsi_list_info ||
		    !ice_is_bit_set(itr->vsi_list_info->vsi_map, vsi_handle))
			continue;
		/* Clearing it so that the logic can add it back */
		ice_clear_bit(vsi_handle, itr->vsi_list_info->vsi_map);
		f_entry.fltr_info.vsi_handle = vsi_handle;
		f_entry.fltr_info.fltr_act = ICE_FWD_TO_VSI;
		/* update the src in case it is VSI num */
		if (f_entry.fltr_info.src_id == ICE_SRC_ID_VSI)
			f_entry.fltr_info.src = hw_vsi_id;
		if (recp_id == ICE_SW_LKUP_VLAN)
			status = ice_add_vlan_internal(hw, recp_list, &f_entry);
		else
			status = ice_add_rule_internal(hw, recp_list,
						       pi->lport,
						       &f_entry);
		if (status != ICE_SUCCESS)
			goto end;
	}
end:
	return status;
}

/**
 * ice_replay_vsi_all_fltr - replay all filters stored in bookkeeping lists
 * @hw: pointer to the hardware structure
 * @pi: pointer to port information structure
 * @vsi_handle: driver VSI handle
 *
 * Replays filters for requested VSI via vsi_handle.
 */
enum ice_status
ice_replay_vsi_all_fltr(struct ice_hw *hw, struct ice_port_info *pi,
			uint16_t vsi_handle)
{
	struct ice_switch_info *sw = NULL;
	enum ice_status status = ICE_SUCCESS;
	uint8_t i;

	sw = hw->switch_info;

	/* Update the recipes that were created */
	for (i = 0; i < ICE_MAX_NUM_RECIPES; i++) {
		struct ice_fltr_mgmt_list_head *head;

		head = &sw->recp_list[i].filt_replay_rules;
		if (!sw->recp_list[i].adv_rule)
			status = ice_replay_vsi_fltr(hw, pi, sw, vsi_handle, i,
						     head);
		if (status != ICE_SUCCESS)
			return status;
	}

	return ICE_SUCCESS;
}

/**
 * ice_sched_replay_vsi_agg - replay aggregator & VSI to aggregator node(s)
 * @hw: pointer to the HW struct
 * @vsi_handle: software VSI handle
 *
 * This function replays aggregator node, VSI to aggregator type nodes, and
 * their node bandwidth information. This function needs to be called with
 * scheduler lock held.
 */
enum ice_status
ice_sched_replay_vsi_agg(struct ice_hw *hw, uint16_t vsi_handle)
{
#if 0
	ice_declare_bitmap(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
	struct ice_sched_agg_vsi_info *agg_vsi_info;
	struct ice_port_info *pi = hw->port_info;
	struct ice_sched_agg_info *agg_info;
	enum ice_status status;

	ice_zero_bitmap(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;
	agg_info = ice_get_vsi_agg_info(hw, vsi_handle);
	if (!agg_info)
		return ICE_SUCCESS; /* Not present in list - default Agg case */
	agg_vsi_info = ice_get_agg_vsi_info(agg_info, vsi_handle);
	if (!agg_vsi_info)
		return ICE_SUCCESS; /* Not present in list - default Agg case */
	ice_sched_get_ena_tc_bitmap(pi, agg_info->replay_tc_bitmap,
				    replay_bitmap);
	/* Replay aggregator node associated to vsi_handle */
	status = ice_sched_cfg_agg(hw->port_info, agg_info->agg_id,
				   ICE_AGG_TYPE_AGG, replay_bitmap);
	if (status)
		return status;
	/* Replay aggregator node BW (restore aggregator BW) */
	status = ice_sched_replay_agg_bw(hw, agg_info);
	if (status)
		return status;

	ice_zero_bitmap(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
	ice_sched_get_ena_tc_bitmap(pi, agg_vsi_info->replay_tc_bitmap,
				    replay_bitmap);
	/* Move this VSI (vsi_handle) to above aggregator */
	status = ice_sched_assoc_vsi_to_agg(pi, agg_info->agg_id, vsi_handle,
					    replay_bitmap);
	if (status)
		return status;
	/* Replay VSI BW (restore VSI BW) */
	return ice_sched_replay_vsi_bw(hw, vsi_handle,
				       agg_vsi_info->tc_bitmap);
#else
	return ICE_ERR_NOT_IMPL;
#endif
}

/**
 * ice_replay_vsi_agg - replay VSI to aggregator node
 * @hw: pointer to the HW struct
 * @vsi_handle: software VSI handle
 *
 * This function replays association of VSI to aggregator type nodes, and
 * node bandwidth information.
 */
enum ice_status
ice_replay_vsi_agg(struct ice_hw *hw, uint16_t vsi_handle)
{
#if 0
	struct ice_port_info *pi = hw->port_info;
#endif
	enum ice_status status;
#if 0
	ice_acquire_lock(&pi->sched_lock);
#endif
	status = ice_sched_replay_vsi_agg(hw, vsi_handle);
#if 0
	ice_release_lock(&pi->sched_lock);
#endif
	return status;
}
/**
 * ice_replay_vsi - replay VSI configuration
 * @hw: pointer to the HW struct
 * @vsi_handle: driver VSI handle
 *
 * Restore all VSI configuration after reset. It is required to call this
 * function with main VSI first.
 */
enum ice_status
ice_replay_vsi(struct ice_hw *hw, uint16_t vsi_handle)
{
	struct ice_switch_info *sw = hw->switch_info;
	struct ice_port_info *pi = hw->port_info;
	enum ice_status status;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;

	/* Replay pre-initialization if there is any */
	if (ice_is_main_vsi(hw, vsi_handle)) {
		status = ice_replay_pre_init(hw, sw);
		if (status)
			return status;
	}
	/* Replay per VSI all RSS configurations */
	status = ice_replay_rss_cfg(hw, vsi_handle);
	if (status)
		return status;
	/* Replay per VSI all filters */
	status = ice_replay_vsi_all_fltr(hw, pi, vsi_handle);
	if (!status)
		status = ice_replay_vsi_agg(hw, vsi_handle);
	return status;
}

/**
 * ice_replay_all_vsi_cfg - Replace configuration for all VSIs after reset
 * @sc: the device softc
 *
 * Replace the configuration for each VSI, and then cleanup replay
 * information. Called after a hardware reset in order to reconfigure the
 * active VSIs.
 */
int
ice_replay_all_vsi_cfg(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	int i;

	for (i = 0 ; i < sc->num_available_vsi; i++) {
		struct ice_vsi *vsi = sc->all_vsi[i];

		if (!vsi)
			continue;

		status = ice_replay_vsi(hw, vsi->idx);
		if (status) {
			printf("%s: Failed to replay VSI %d, err %s "
			    "aq_err %s\n", sc->sc_dev.dv_xname,
			    vsi->idx, ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			return (EIO);
		}
	}

	/* Cleanup replay filters after successful reconfiguration */
	ice_replay_post(hw);
	return (0);
}

/**
 * ice_aq_set_health_status_config - Configure FW health events
 * @hw: pointer to the HW struct
 * @event_source: type of diagnostic events to enable
 * @cd: pointer to command details structure or NULL
 *
 * Configure the health status event types that the firmware will send to this
 * PF. The supported event types are: PF-specific, all PFs, and global
 */
enum ice_status
ice_aq_set_health_status_config(struct ice_hw *hw, uint8_t event_source,
				struct ice_sq_cd *cd)
{
	struct ice_aqc_set_health_status_config *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.set_health_status_config;

	ice_fill_dflt_direct_cmd_desc(&desc,
				      ice_aqc_opc_set_health_status_config);

	cmd->event_source = event_source;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_init_health_events - Enable FW health event reporting
 * @sc: device softc
 *
 * Will try to enable firmware health event reporting, but shouldn't
 * cause any grief (to the caller) if this fails.
 */
void
ice_init_health_events(struct ice_softc *sc)
{
	enum ice_status status;
	uint8_t health_mask;

	if (!ice_is_bit_set(sc->feat_cap, ICE_FEATURE_HEALTH_STATUS))
		return;

	health_mask = ICE_AQC_HEALTH_STATUS_SET_PF_SPECIFIC_MASK |
		      ICE_AQC_HEALTH_STATUS_SET_GLOBAL_MASK;

	status = ice_aq_set_health_status_config(&sc->hw, health_mask, NULL);
	if (status) {
		printf("%s: Failed to enable firmware health events, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		    ice_status_str(status),
		    ice_aq_str(sc->hw.adminq.sq_last_status));
	} else
		ice_set_bit(ICE_FEATURE_HEALTH_STATUS, sc->feat_en);
}

/**
 * ice_fw_supports_lldp_fltr_ctrl - check NVM version supports lldp_fltr_ctrl
 * @hw: pointer to HW struct
 */
bool
ice_fw_supports_lldp_fltr_ctrl(struct ice_hw *hw)
{
	if (hw->mac_type != ICE_MAC_E810 && hw->mac_type != ICE_MAC_GENERIC)
		return false;

	return ice_is_fw_api_min_ver(hw, ICE_FW_API_LLDP_FLTR_MAJ,
				     ICE_FW_API_LLDP_FLTR_MIN,
				     ICE_FW_API_LLDP_FLTR_PATCH);
}

/**
 * ice_add_ethertype_to_list - Add an Ethertype filter to a filter list
 * @vsi: the VSI to target packets to
 * @list: the list to add the filter to
 * @ethertype: the Ethertype to filter on
 * @direction: The direction of the filter (Tx or Rx)
 * @action: the action to take
 *
 * Add an Ethertype filter to a filter list. Used to forward a series of
 * filters to the firmware for configuring the switch.
 *
 * Returns 0 on success, and an error code on failure.
 */
int
ice_add_ethertype_to_list(struct ice_vsi *vsi, struct ice_fltr_list_head *list,
    uint16_t ethertype, uint16_t direction, enum ice_sw_fwd_act_type action)
{
	struct ice_fltr_list_entry *entry;

	KASSERT((direction == ICE_FLTR_TX) || (direction == ICE_FLTR_RX));

	entry = malloc(sizeof(*entry), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (!entry)
		return (ENOMEM);

	entry->fltr_info.flag = direction;
	entry->fltr_info.src_id = ICE_SRC_ID_VSI;
	entry->fltr_info.lkup_type = ICE_SW_LKUP_ETHERTYPE;
	entry->fltr_info.fltr_act = action;
	entry->fltr_info.vsi_handle = vsi->idx;
	entry->fltr_info.l_data.ethertype_mac.ethertype = ethertype;

	TAILQ_INSERT_HEAD(list, entry, list_entry);

	return 0;
}

/**
 * ice_lldp_fltr_add_remove - add or remove a LLDP Rx switch filter
 * @hw: pointer to HW struct
 * @vsi_num: absolute HW index for VSI
 * @add: boolean for if adding or removing a filter
 */
enum ice_status
ice_lldp_fltr_add_remove(struct ice_hw *hw, uint16_t vsi_num, bool add)
{
	struct ice_aqc_lldp_filter_ctrl *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.lldp_filter_ctrl;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_lldp_filter_ctrl);

	if (add)
		cmd->cmd_flags = ICE_AQC_LLDP_FILTER_ACTION_ADD;
	else
		cmd->cmd_flags = ICE_AQC_LLDP_FILTER_ACTION_DELETE;

	cmd->vsi_num = htole16(vsi_num);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_add_eth_mac_rule - Add ethertype and MAC based filter rule
 * @hw: pointer to the hardware structure
 * @em_list: list of ether type MAC filter, MAC is optional
 * @sw: pointer to switch info struct for which function add rule
 * @lport: logic port number on which function add rule
 *
 * This function requires the caller to populate the entries in
 * the filter list with the necessary fields (including flags to
 * indicate Tx or Rx rules).
 */
enum ice_status
ice_add_eth_mac_rule(struct ice_hw *hw, struct ice_fltr_list_head *em_list,
		     struct ice_switch_info *sw, uint8_t lport)
{
	struct ice_fltr_list_entry *em_list_itr;

	TAILQ_FOREACH(em_list_itr, em_list, list_entry) {
		struct ice_sw_recipe *recp_list;
		enum ice_sw_lkup_type l_type;

		l_type = em_list_itr->fltr_info.lkup_type;
		recp_list = &sw->recp_list[l_type];

		if (l_type != ICE_SW_LKUP_ETHERTYPE_MAC &&
		    l_type != ICE_SW_LKUP_ETHERTYPE)
			return ICE_ERR_PARAM;

		em_list_itr->status = ice_add_rule_internal(hw, recp_list,
							    lport,
							    em_list_itr);
		if (em_list_itr->status)
			return em_list_itr->status;
	}
	return ICE_SUCCESS;
}

/**
 * ice_add_eth_mac - Add a ethertype based filter rule
 * @hw: pointer to the hardware structure
 * @em_list: list of ethertype and forwarding information
 *
 * Function add ethertype rule for logical port from HW struct
 */
enum ice_status
ice_add_eth_mac(struct ice_hw *hw, struct ice_fltr_list_head *em_list)
{
	if (!em_list || !hw)
		return ICE_ERR_PARAM;

	return ice_add_eth_mac_rule(hw, em_list, hw->switch_info,
				    hw->port_info->lport);
}

/**
 * ice_add_rx_lldp_filter - add ethertype filter for Rx LLDP frames
 * @sc: the device private structure
 *
 * Add a switch ethertype filter which forwards the LLDP frames to the main PF
 * VSI. Called when the fw_lldp_agent is disabled, to allow the LLDP frames to
 * be forwarded to the stack.
 */
void
ice_add_rx_lldp_filter(struct ice_softc *sc)
{
	struct ice_fltr_list_head ethertype_list;
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	int err;
	uint16_t vsi_num;

	/*
	 * If FW is new enough, use a direct AQ command to perform the filter
	 * addition.
	 */
	if (ice_fw_supports_lldp_fltr_ctrl(hw)) {
		vsi_num = hw->vsi_ctx[vsi->idx]->vsi_num;
		status = ice_lldp_fltr_add_remove(hw, vsi_num, true);
		if (status) {
			DPRINTF("%s: failed to add Rx LLDP filter, "
			    "err %s aq_err %s\n", __func__,
			    ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
		} else
			ice_set_state(&sc->state,
			    ICE_STATE_LLDP_RX_FLTR_FROM_DRIVER);
		return;
	}

	TAILQ_INIT(&ethertype_list);

	/* Forward Rx LLDP frames to the stack */
	err = ice_add_ethertype_to_list(vsi, &ethertype_list, ETHERTYPE_LLDP,
					ICE_FLTR_RX, ICE_FWD_TO_VSI);
	if (err) {
		DPRINTF("%s: failed to add Rx LLDP filter, err %d\n",
		    __func__, err);
		goto free_ethertype_list;
	}

	status = ice_add_eth_mac(hw, &ethertype_list);
	if (status && status != ICE_ERR_ALREADY_EXISTS) {
		DPRINTF("%s: failed to add Rx LLDP filter, err %s aq_err %s\n",
		    __func__, ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	} else {
		/*
		 * If status == ICE_ERR_ALREADY_EXISTS, we won't treat an
		 * already existing filter as an error case.
		 */
		ice_set_state(&sc->state, ICE_STATE_LLDP_RX_FLTR_FROM_DRIVER);
	}

free_ethertype_list:
	ice_free_fltr_list(&ethertype_list);
}

/**
 * ice_update_link_info - update status of the HW network link
 * @pi: port info structure of the interested logical port
 */
enum ice_status
ice_update_link_info(struct ice_port_info *pi)
{
	struct ice_link_status *li;
	enum ice_status status;

	if (!pi)
		return ICE_ERR_PARAM;

	li = &pi->phy.link_info;

	status = ice_aq_get_link_info(pi, true, NULL, NULL);
	if (status)
		return status;

	if (li->link_info & ICE_AQ_MEDIA_AVAILABLE) {
		struct ice_aqc_get_phy_caps_data *pcaps;
		struct ice_hw *hw;

		hw = pi->hw;
		pcaps = (struct ice_aqc_get_phy_caps_data *)
			ice_malloc(hw, sizeof(*pcaps));
		if (!pcaps)
			return ICE_ERR_NO_MEMORY;

		status = ice_aq_get_phy_caps(pi, false,
		    ICE_AQC_REPORT_TOPO_CAP_MEDIA, pcaps, NULL);

		if (status == ICE_SUCCESS)
			memcpy(li->module_type, &pcaps->module_type,
				   sizeof(li->module_type));

		ice_free(hw, pcaps);
	}

	return status;
}

/**
 * ice_get_link_status - get status of the HW network link
 * @pi: port information structure
 * @link_up: pointer to bool (true/false = linkup/linkdown)
 *
 * Variable link_up is true if link is up, false if link is down.
 * The variable link_up is invalid if status is non zero. As a
 * result of this call, link status reporting becomes enabled
 */
enum ice_status
ice_get_link_status(struct ice_port_info *pi, bool *link_up)
{
	struct ice_phy_info *phy_info;
	enum ice_status status = ICE_SUCCESS;

	if (!pi || !link_up)
		return ICE_ERR_PARAM;

	phy_info = &pi->phy;

	if (phy_info->get_link_info) {
		status = ice_update_link_info(pi);
		if (status) {
			DNPRINTF(ICE_DBG_LINK,
			    "%s: get link status error, status = %d\n",
			    __func__, status);
		}
	}

	*link_up = phy_info->link_info.link_info & ICE_AQ_LINK_UP;

	return status;
}

/**
 * ice_set_default_local_mib_settings - Set Local LLDP MIB to default settings
 * @sc: device softc structure
 *
 * Overwrites the driver's SW local LLDP MIB with default settings. This
 * ensures the driver has a valid MIB when it next uses the Set Local LLDP MIB
 * admin queue command.
 */
void
ice_set_default_local_mib_settings(struct ice_softc *sc)
{
	struct ice_dcbx_cfg *dcbcfg;
	struct ice_hw *hw = &sc->hw;
	struct ice_port_info *pi;
	uint8_t maxtcs, maxtcs_ets, old_pfc_mode;

	pi = hw->port_info;

	dcbcfg = &pi->qos_cfg.local_dcbx_cfg;

	maxtcs = hw->func_caps.common_cap.maxtc;
	/* This value is only 3 bits; 8 TCs maps to 0 */
	maxtcs_ets = maxtcs & ICE_IEEE_ETS_MAXTC_M;

	/* VLAN vs DSCP mode needs to be preserved */
	old_pfc_mode = dcbcfg->pfc_mode;

	/**
	 * Setup the default settings used by the driver for the Set Local
	 * LLDP MIB Admin Queue command (0x0A08). (1TC w/ 100% BW, ETS, no
	 * PFC, TSA=2).
	 */
	memset(dcbcfg, 0, sizeof(*dcbcfg));

	dcbcfg->etscfg.willing = 1;
	dcbcfg->etscfg.tcbwtable[0] = 100;
	dcbcfg->etscfg.maxtcs = maxtcs_ets;
	dcbcfg->etscfg.tsatable[0] = 2;

	dcbcfg->etsrec = dcbcfg->etscfg;
	dcbcfg->etsrec.willing = 0;

	dcbcfg->pfc.willing = 1;
	dcbcfg->pfc.pfccap = maxtcs;

	dcbcfg->pfc_mode = old_pfc_mode;
}

/**
 * ice_add_ieee_ets_common_tlv
 * @buf: Data buffer to be populated with ice_dcb_ets_cfg data
 * @ets_cfg: Container for ice_dcb_ets_cfg data
 *
 * Populate the TLV buffer with ice_dcb_ets_cfg data
 */
void
ice_add_ieee_ets_common_tlv(uint8_t *buf, struct ice_dcb_ets_cfg *ets_cfg)
{
	uint8_t priority0, priority1;
	uint8_t offset = 0;
	int i;

	/* Priority Assignment Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < ICE_MAX_TRAFFIC_CLASS / 2; i++) {
		priority0 = ets_cfg->prio_table[i * 2] & 0xF;
		priority1 = ets_cfg->prio_table[i * 2 + 1] & 0xF;
		buf[offset] = (priority0 << ICE_IEEE_ETS_PRIO_1_S) | priority1;
		offset++;
	}

	/* TC Bandwidth Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 *
	 * TSA Assignment Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	ice_for_each_traffic_class(i) {
		buf[offset] = ets_cfg->tcbwtable[i];
		buf[ICE_MAX_TRAFFIC_CLASS + offset] = ets_cfg->tsatable[i];
		offset++;
	}
}

/**
 * ice_add_ieee_ets_tlv - Prepare ETS TLV in IEEE format
 * @tlv: Fill the ETS config data in IEEE format
 * @dcbcfg: Local store which holds the DCB Config
 *
 * Prepare IEEE 802.1Qaz ETS CFG TLV
 */
void
ice_add_ieee_ets_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_dcb_ets_cfg *etscfg;
	uint8_t *buf = tlv->tlvinfo;
	uint8_t maxtcwilling = 0;
	uint32_t ouisubtype;
	uint16_t typelen;

	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_IEEE_ETS_TLV_LEN);
	tlv->typelen = HTONS(typelen);

	ouisubtype = ((ICE_IEEE_8021QAZ_OUI << ICE_LLDP_TLV_OUI_S) |
		      ICE_IEEE_SUBTYPE_ETS_CFG);
	tlv->ouisubtype = htonl(ouisubtype);

	/* First Octet post subtype
	 * --------------------------
	 * |will-|CBS  | Re-  | Max |
	 * |ing  |     |served| TCs |
	 * --------------------------
	 * |1bit | 1bit|3 bits|3bits|
	 */
	etscfg = &dcbcfg->etscfg;
	if (etscfg->willing)
		maxtcwilling = BIT(ICE_IEEE_ETS_WILLING_S);
	maxtcwilling |= etscfg->maxtcs & ICE_IEEE_ETS_MAXTC_M;
	buf[0] = maxtcwilling;

	/* Begin adding at Priority Assignment Table (offset 1 in buf) */
	ice_add_ieee_ets_common_tlv(&buf[1], etscfg);
}

/**
 * ice_add_ieee_etsrec_tlv - Prepare ETS Recommended TLV in IEEE format
 * @tlv: Fill ETS Recommended TLV in IEEE format
 * @dcbcfg: Local store which holds the DCB Config
 *
 * Prepare IEEE 802.1Qaz ETS REC TLV
 */
void
ice_add_ieee_etsrec_tlv(struct ice_lldp_org_tlv *tlv,
			struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_dcb_ets_cfg *etsrec;
	uint8_t *buf = tlv->tlvinfo;
	uint32_t ouisubtype;
	uint16_t typelen;

	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_IEEE_ETS_TLV_LEN);
	tlv->typelen = htons(typelen);

	ouisubtype = ((ICE_IEEE_8021QAZ_OUI << ICE_LLDP_TLV_OUI_S) |
		      ICE_IEEE_SUBTYPE_ETS_REC);
	tlv->ouisubtype = HTONL(ouisubtype);

	etsrec = &dcbcfg->etsrec;

	/* First Octet is reserved */
	/* Begin adding at Priority Assignment Table (offset 1 in buf) */
	ice_add_ieee_ets_common_tlv(&buf[1], etsrec);
}

/**
 * ice_add_ieee_pfc_tlv - Prepare PFC TLV in IEEE format
 * @tlv: Fill PFC TLV in IEEE format
 * @dcbcfg: Local store which holds the PFC CFG data
 *
 * Prepare IEEE 802.1Qaz PFC CFG TLV
 */
void
ice_add_ieee_pfc_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	uint8_t *buf = tlv->tlvinfo;
	uint32_t ouisubtype;
	uint16_t typelen;

	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_IEEE_PFC_TLV_LEN);
	tlv->typelen = htons(typelen);

	ouisubtype = ((ICE_IEEE_8021QAZ_OUI << ICE_LLDP_TLV_OUI_S) |
		      ICE_IEEE_SUBTYPE_PFC_CFG);
	tlv->ouisubtype = htonl(ouisubtype);

	/* ----------------------------------------
	 * |will-|MBC  | Re-  | PFC |  PFC Enable  |
	 * |ing  |     |served| cap |              |
	 * -----------------------------------------
	 * |1bit | 1bit|2 bits|4bits| 1 octet      |
	 */
	if (dcbcfg->pfc.willing)
		buf[0] = BIT(ICE_IEEE_PFC_WILLING_S);

	if (dcbcfg->pfc.mbc)
		buf[0] |= BIT(ICE_IEEE_PFC_MBC_S);

	buf[0] |= dcbcfg->pfc.pfccap & 0xF;
	buf[1] = dcbcfg->pfc.pfcena;
}

/**
 * ice_add_ieee_app_pri_tlv -  Prepare APP TLV in IEEE format
 * @tlv: Fill APP TLV in IEEE format
 * @dcbcfg: Local store which holds the APP CFG data
 *
 * Prepare IEEE 802.1Qaz APP CFG TLV
 */
void
ice_add_ieee_app_pri_tlv(struct ice_lldp_org_tlv *tlv,
			 struct ice_dcbx_cfg *dcbcfg)
{
	uint16_t typelen, len, offset = 0;
	uint8_t priority, selector, i = 0;
	uint8_t *buf = tlv->tlvinfo;
	uint32_t ouisubtype;

	/* No APP TLVs then just return */
	if (dcbcfg->numapps == 0)
		return;
	ouisubtype = ((ICE_IEEE_8021QAZ_OUI << ICE_LLDP_TLV_OUI_S) |
		      ICE_IEEE_SUBTYPE_APP_PRI);
	tlv->ouisubtype = HTONL(ouisubtype);

	/* Move offset to App Priority Table */
	offset++;
	/* Application Priority Table (3 octets)
	 * Octets:|         1          |    2    |    3    |
	 *        -----------------------------------------
	 *        |Priority|Rsrvd| Sel |    Protocol ID    |
	 *        -----------------------------------------
	 *   Bits:|23    21|20 19|18 16|15                0|
	 *        -----------------------------------------
	 */
	while (i < dcbcfg->numapps) {
		priority = dcbcfg->app[i].priority & 0x7;
		selector = dcbcfg->app[i].selector & 0x7;
		buf[offset] = (priority << ICE_IEEE_APP_PRIO_S) | selector;
		buf[offset + 1] = (dcbcfg->app[i].prot_id >> 0x8) & 0xFF;
		buf[offset + 2] = dcbcfg->app[i].prot_id & 0xFF;
		/* Move to next app */
		offset += 3;
		i++;
		if (i >= ICE_DCBX_MAX_APPS)
			break;
	}
	/* len includes size of ouisubtype + 1 reserved + 3*numapps */
	len = sizeof(tlv->ouisubtype) + 1 + (i * 3);
	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) | (len & 0x1FF));
	tlv->typelen = HTONS(typelen);
}

/**
 * ice_add_dscp_up_tlv - Prepare DSCP to UP TLV
 * @tlv: location to build the TLV data
 * @dcbcfg: location of data to convert to TLV
 */
void
ice_add_dscp_up_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	uint8_t *buf = tlv->tlvinfo;
	uint32_t ouisubtype;
	uint16_t typelen;
	int i;

	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_DSCP_UP_TLV_LEN);
	tlv->typelen = htons(typelen);

	ouisubtype = (uint32_t)((ICE_DSCP_OUI << ICE_LLDP_TLV_OUI_S) |
			   ICE_DSCP_SUBTYPE_DSCP2UP);
	tlv->ouisubtype = htonl(ouisubtype);

	/* bytes 0 - 63 - IPv4 DSCP2UP LUT */
	for (i = 0; i < ICE_DSCP_NUM_VAL; i++) {
		/* IPv4 mapping */
		buf[i] = dcbcfg->dscp_map[i];
		/* IPv6 mapping */
		buf[i + ICE_DSCP_IPV6_OFFSET] = dcbcfg->dscp_map[i];
	}

	/* byte 64 - IPv4 untagged traffic */
	buf[i] = 0;

	/* byte 144 - IPv6 untagged traffic */
	buf[i + ICE_DSCP_IPV6_OFFSET] = 0;
}

#define ICE_BYTES_PER_TC	8

/**
 * ice_add_dscp_enf_tlv - Prepare DSCP Enforcement TLV
 * @tlv: location to build the TLV data
 */
void
ice_add_dscp_enf_tlv(struct ice_lldp_org_tlv *tlv)
{
	uint8_t *buf = tlv->tlvinfo;
	uint32_t ouisubtype;
	uint16_t typelen;

	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_DSCP_ENF_TLV_LEN);
	tlv->typelen = htons(typelen);

	ouisubtype = (uint32_t)((ICE_DSCP_OUI << ICE_LLDP_TLV_OUI_S) |
			   ICE_DSCP_SUBTYPE_ENFORCE);
	tlv->ouisubtype = htonl(ouisubtype);

	/* Allow all DSCP values to be valid for all TC's (IPv4 and IPv6) */
	memset(buf, 0, 2 * (ICE_MAX_TRAFFIC_CLASS * ICE_BYTES_PER_TC));
}

/**
 * ice_add_dscp_tc_bw_tlv - Prepare DSCP BW for TC TLV
 * @tlv: location to build the TLV data
 * @dcbcfg: location of the data to convert to TLV
 */
void
ice_add_dscp_tc_bw_tlv(struct ice_lldp_org_tlv *tlv,
		       struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_dcb_ets_cfg *etscfg;
	uint8_t *buf = tlv->tlvinfo;
	uint32_t ouisubtype;
	uint8_t offset = 0;
	uint16_t typelen;
	int i;

	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_DSCP_TC_BW_TLV_LEN);
	tlv->typelen = htons(typelen);

	ouisubtype = (uint32_t)((ICE_DSCP_OUI << ICE_LLDP_TLV_OUI_S) |
			   ICE_DSCP_SUBTYPE_TCBW);
	tlv->ouisubtype = htonl(ouisubtype);

	/* First Octet after subtype
	 * ----------------------------
	 * | RSV | CBS | RSV | Max TCs |
	 * | 1b  | 1b  | 3b  | 3b      |
	 * ----------------------------
	 */
	etscfg = &dcbcfg->etscfg;
	buf[0] = etscfg->maxtcs & ICE_IEEE_ETS_MAXTC_M;

	/* bytes 1 - 4 reserved */
	offset = 5;

	/* TC BW table
	 * bytes 0 - 7 for TC 0 - 7
	 *
	 * TSA Assignment table
	 * bytes 8 - 15 for TC 0 - 7
	 */
	for (i = 0; i < ICE_MAX_TRAFFIC_CLASS; i++) {
		buf[offset] = etscfg->tcbwtable[i];
		buf[offset + ICE_MAX_TRAFFIC_CLASS] = etscfg->tsatable[i];
		offset++;
	}
}

/**
 * ice_add_dscp_pfc_tlv - Prepare DSCP PFC TLV
 * @tlv: Fill PFC TLV in IEEE format
 * @dcbcfg: Local store which holds the PFC CFG data
 */
void
ice_add_dscp_pfc_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	uint8_t *buf = tlv->tlvinfo;
	uint32_t ouisubtype;
	uint16_t typelen;

	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_DSCP_PFC_TLV_LEN);
	tlv->typelen = htons(typelen);

	ouisubtype = (uint32_t)((ICE_DSCP_OUI << ICE_LLDP_TLV_OUI_S) |
			   ICE_DSCP_SUBTYPE_PFC);
	tlv->ouisubtype = HTONL(ouisubtype);

	buf[0] = dcbcfg->pfc.pfccap & 0xF;
	buf[1] = dcbcfg->pfc.pfcena;
}

/**
 * ice_add_dcb_tlv - Add all IEEE or DSCP TLVs
 * @tlv: Fill TLV data in IEEE format
 * @dcbcfg: Local store which holds the DCB Config
 * @tlvid: Type of IEEE TLV
 *
 * Add tlv information
 */
void
ice_add_dcb_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg,
		uint16_t tlvid)
{
	if (dcbcfg->pfc_mode == ICE_QOS_MODE_VLAN) {
		switch (tlvid) {
		case ICE_IEEE_TLV_ID_ETS_CFG:
			ice_add_ieee_ets_tlv(tlv, dcbcfg);
			break;
		case ICE_IEEE_TLV_ID_ETS_REC:
			ice_add_ieee_etsrec_tlv(tlv, dcbcfg);
			break;
		case ICE_IEEE_TLV_ID_PFC_CFG:
			ice_add_ieee_pfc_tlv(tlv, dcbcfg);
			break;
		case ICE_IEEE_TLV_ID_APP_PRI:
			ice_add_ieee_app_pri_tlv(tlv, dcbcfg);
			break;
		default:
			break;
		}
	} else {
		/* pfc_mode == ICE_QOS_MODE_DSCP */
		switch (tlvid) {
		case ICE_TLV_ID_DSCP_UP:
			ice_add_dscp_up_tlv(tlv, dcbcfg);
			break;
		case ICE_TLV_ID_DSCP_ENF:
			ice_add_dscp_enf_tlv(tlv);
			break;
		case ICE_TLV_ID_DSCP_TC_BW:
			ice_add_dscp_tc_bw_tlv(tlv, dcbcfg);
			break;
		case ICE_TLV_ID_DSCP_TO_PFC:
			ice_add_dscp_pfc_tlv(tlv, dcbcfg);
			break;
		default:
			break;
		}
	}
}
/**
 * ice_dcb_cfg_to_lldp - Convert DCB configuration to MIB format
 * @lldpmib: pointer to the HW struct
 * @miblen: length of LLDP MIB
 * @dcbcfg: Local store which holds the DCB Config
 *
 * Convert the DCB configuration to MIB format
 */
void
ice_dcb_cfg_to_lldp(uint8_t *lldpmib, uint16_t *miblen,
    struct ice_dcbx_cfg *dcbcfg)
{
	uint16_t len, offset = 0, tlvid = ICE_TLV_ID_START;
	struct ice_lldp_org_tlv *tlv;
	uint16_t typelen;

	tlv = (struct ice_lldp_org_tlv *)lldpmib;
	while (1) {
		ice_add_dcb_tlv(tlv, dcbcfg, tlvid++);
		typelen = ntohs(tlv->typelen);
		len = (typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S;
		if (len)
			offset += len + 2;
		/* END TLV or beyond LLDPDU size */
		if (tlvid >= ICE_TLV_ID_END_OF_LLDPPDU ||
		    offset > ICE_LLDPDU_SIZE)
			break;
		/* Move to next TLV */
		if (len)
			tlv = (struct ice_lldp_org_tlv *)
				((char *)tlv + sizeof(tlv->typelen) + len);
	}
	*miblen = offset;
}

/**
 * ice_aq_set_lldp_mib - Set the LLDP MIB
 * @hw: pointer to the HW struct
 * @mib_type: Local, Remote or both Local and Remote MIBs
 * @buf: pointer to the caller-supplied buffer to store the MIB block
 * @buf_size: size of the buffer (in bytes)
 * @cd: pointer to command details structure or NULL
 *
 * Set the LLDP MIB. (0x0A08)
 */
enum ice_status
ice_aq_set_lldp_mib(struct ice_hw *hw, uint8_t mib_type, void *buf,
    uint16_t buf_size, struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_set_local_mib *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.lldp_set_mib;

	if (buf_size == 0 || !buf)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_lldp_set_local_mib);

	desc.flags |= htole16((uint16_t)ICE_AQ_FLAG_RD);
	desc.datalen = htole16(buf_size);

	cmd->type = mib_type;
	cmd->length = htole16(buf_size);

	return ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
}

/**
 * ice_set_dcb_cfg - Set the local LLDP MIB to FW
 * @pi: port information structure
 *
 * Set DCB configuration to the Firmware
 */
enum ice_status
ice_set_dcb_cfg(struct ice_port_info *pi)
{
	uint8_t mib_type, *lldpmib = NULL;
	struct ice_dcbx_cfg *dcbcfg;
	enum ice_status ret;
	struct ice_hw *hw;
	uint16_t miblen;

	if (!pi)
		return ICE_ERR_PARAM;

	hw = pi->hw;

	/* update the HW local config */
	dcbcfg = &pi->qos_cfg.local_dcbx_cfg;
	/* Allocate the LLDPDU */
	lldpmib = (uint8_t *)ice_malloc(hw, ICE_LLDPDU_SIZE);
	if (!lldpmib)
		return ICE_ERR_NO_MEMORY;

	mib_type = SET_LOCAL_MIB_TYPE_LOCAL_MIB;
	if (dcbcfg->app_mode == ICE_DCBX_APPS_NON_WILLING)
		mib_type |= SET_LOCAL_MIB_TYPE_CEE_NON_WILLING;

	ice_dcb_cfg_to_lldp(lldpmib, &miblen, dcbcfg);
	ret = ice_aq_set_lldp_mib(hw, mib_type, (void *)lldpmib, miblen,
				  NULL);

	ice_free(hw, lldpmib);

	return ret;
}

/**
 * ice_set_default_local_lldp_mib - Possibly apply local LLDP MIB to FW
 * @sc: device softc structure
 *
 * This function needs to be called after link up; it makes sure the FW has
 * certain PFC/DCB settings. In certain configurations this will re-apply a
 * default local LLDP MIB configuration; this is intended to workaround a FW
 * behavior where these settings seem to be cleared on link up.
 */
void
ice_set_default_local_lldp_mib(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	struct ice_port_info *pi;
	enum ice_status status;

	/* Set Local MIB can disrupt flow control settings for
	 * non-DCB-supported devices.
	 */
	if (!ice_is_bit_set(sc->feat_en, ICE_FEATURE_DCB))
		return;

	pi = hw->port_info;

	/* Don't overwrite a custom SW configuration */
	if (!pi->qos_cfg.is_sw_lldp &&
	    !ice_test_state(&sc->state, ICE_STATE_MULTIPLE_TCS))
		ice_set_default_local_mib_settings(sc);

	status = ice_set_dcb_cfg(pi);
	if (status) {
		printf("%s: Error setting Local LLDP MIB: %s aq_err %s\n",
		    sc->sc_dev.dv_xname,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}
}

/**
 * ice_aq_speed_to_rate - Convert AdminQ speed enum to baudrate
 * @pi: port info data
 *
 * Returns the baudrate value for the current link speed of a given port.
 */
uint64_t
ice_aq_speed_to_rate(struct ice_port_info *pi)
{
	switch (pi->phy.link_info.link_speed) {
	case ICE_AQ_LINK_SPEED_100GB:
		return IF_Gbps(100);
	case ICE_AQ_LINK_SPEED_50GB:
		return IF_Gbps(50);
	case ICE_AQ_LINK_SPEED_40GB:
		return IF_Gbps(40);
	case ICE_AQ_LINK_SPEED_25GB:
		return IF_Gbps(25);
	case ICE_AQ_LINK_SPEED_10GB:
		return IF_Gbps(10);
	case ICE_AQ_LINK_SPEED_5GB:
		return IF_Gbps(5);
	case ICE_AQ_LINK_SPEED_2500MB:
		return IF_Mbps(2500);
	case ICE_AQ_LINK_SPEED_1000MB:
		return IF_Mbps(1000);
	case ICE_AQ_LINK_SPEED_100MB:
		return IF_Mbps(100);
	case ICE_AQ_LINK_SPEED_10MB:
		return IF_Mbps(10);
	case ICE_AQ_LINK_SPEED_UNKNOWN:
	default:
		/* return 0 if we don't know the link speed */
		return 0;
	}
}

/**
 * ice_update_link_status - notify OS of link state change
 * @sc: device private softc structure
 * @update_media: true if we should update media even if link didn't change
 *
 * Called to notify iflib core of link status changes. Should be called once
 * during attach_post, and whenever link status changes during runtime.
 *
 * This call only updates the currently supported media types if the link
 * status changed, or if update_media is set to true.
 */
void
ice_update_link_status(struct ice_softc *sc, bool update_media)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	/* Never report link up when in recovery mode */
	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE))
		return;

	/* Report link status to iflib only once each time it changes */
	if (!ice_testandset_state(&sc->state, ICE_STATE_LINK_STATUS_REPORTED)) {
		if (sc->link_up) { /* link is up */
			uint64_t baudrate;

			baudrate = ice_aq_speed_to_rate(sc->hw.port_info);
			if (!(hw->port_info->phy.link_info_old.link_info &
			    ICE_AQ_LINK_UP))
				ice_set_default_local_lldp_mib(sc);

			ifp->if_baudrate = baudrate;
			ifp->if_link_state = LINK_STATE_UP;
			if_link_state_change(ifp);
#if 0
			ice_rdma_link_change(sc, LINK_STATE_UP, baudrate);
			ice_link_up_msg(sc);
#endif
		} else { /* link is down */
			ifp->if_baudrate = 0;
			ifp->if_link_state = LINK_STATE_DOWN;
			if_link_state_change(ifp);
#if 0
			ice_rdma_link_change(sc, LINK_STATE_DOWN, 0);
#endif
		}
		update_media = true;
	}

	/* Update the supported media types */
	if (update_media &&
	    !ice_test_state(&sc->state, ICE_STATE_PREPARED_FOR_RESET)) {
		status = ice_add_media_types(sc, &sc->media);
		if (status)
			printf("%s: Error adding device media types: "
			    "%s aq_err %s\n", sc->sc_dev.dv_xname,
			    ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
	}
}

/**
 * __ice_aq_get_set_rss_key
 * @hw: pointer to the HW struct
 * @vsi_id: VSI FW index
 * @key: pointer to key info struct
 * @set: set true to set the key, false to get the key
 *
 * get (0x0B04) or set (0x0B02) the RSS key per VSI
 */
enum ice_status
ice_aq_get_set_rss_key(struct ice_hw *hw, uint16_t vsi_id,
    struct ice_aqc_get_set_rss_keys *key, bool set)
{
	struct ice_aqc_get_set_rss_key *cmd_resp;
	uint16_t key_size = sizeof(*key);
	struct ice_aq_desc desc;

	cmd_resp = &desc.params.get_set_rss_key;

	if (set) {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_rss_key);
		desc.flags |= htole16(ICE_AQ_FLAG_RD);
	} else {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_rss_key);
	}

	cmd_resp->vsi_id = htole16(((vsi_id <<
					 ICE_AQC_GSET_RSS_KEY_VSI_ID_S) &
					ICE_AQC_GSET_RSS_KEY_VSI_ID_M) |
				       ICE_AQC_GSET_RSS_KEY_VSI_VALID);

	return ice_aq_send_cmd(hw, &desc, key, key_size, NULL);
}

/**
 * ice_aq_set_rss_key
 * @hw: pointer to the HW struct
 * @vsi_handle: software VSI handle
 * @keys: pointer to key info struct
 *
 * set the RSS key per VSI
 */
enum ice_status
ice_aq_set_rss_key(struct ice_hw *hw, uint16_t vsi_handle,
		   struct ice_aqc_get_set_rss_keys *keys)
{
	if (!ice_is_vsi_valid(hw, vsi_handle) || !keys)
		return ICE_ERR_PARAM;

	return ice_aq_get_set_rss_key(hw, hw->vsi_ctx[vsi_handle]->vsi_num,
	    keys, true);
}

/**
 * ice_set_rss_key - Configure a given VSI with the default RSS key
 * @vsi: the VSI to configure
 *
 * Program the hardware RSS key. We use rss_getkey to grab the kernel RSS key.
 * If the kernel RSS interface is not available, this will fall back to our
 * pre-generated hash seed from ice_get_default_rss_key().
 */
int
ice_set_rss_key(struct ice_vsi *vsi)
{
	struct ice_aqc_get_set_rss_keys keydata = { .standard_rss_key = {0} };
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	stoeplitz_to_key(keydata.standard_rss_key,
	    sizeof(keydata.standard_rss_key));

	status = ice_aq_set_rss_key(hw, vsi->idx, &keydata);
	if (status) {
		DPRINTF("%s: ice_aq_set_rss_key: status %s, error %s\n",
		     __func__, ice_status_str(status),
		     ice_aq_str(hw->adminq.sq_last_status));
		return (EIO);
	}

	return (0);
}

/**
 * ice_get_rss_hdr_type - get a RSS profile's header type
 * @prof: RSS flow profile
 */
enum ice_rss_cfg_hdr_type
ice_get_rss_hdr_type(struct ice_flow_prof *prof)
{
	enum ice_rss_cfg_hdr_type hdr_type = ICE_RSS_ANY_HEADERS;

	if (prof->segs_cnt == ICE_FLOW_SEG_SINGLE) {
		hdr_type = ICE_RSS_OUTER_HEADERS;
	} else if (prof->segs_cnt == ICE_FLOW_SEG_MAX) {
		const struct ice_flow_seg_info *s;

		s = &prof->segs[ICE_RSS_OUTER_HEADERS];
		if (s->hdrs == ICE_FLOW_SEG_HDR_NONE)
			hdr_type = ICE_RSS_INNER_HEADERS;
		if (s->hdrs & ICE_FLOW_SEG_HDR_IPV4)
			hdr_type = ICE_RSS_INNER_HEADERS_W_OUTER_IPV4;
		if (s->hdrs & ICE_FLOW_SEG_HDR_IPV6)
			hdr_type = ICE_RSS_INNER_HEADERS_W_OUTER_IPV6;
	}

	return hdr_type;
}

/**
 * ice_add_rss_list - add RSS configuration to list
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @prof: pointer to flow profile
 *
 * Assumption: lock has already been acquired for RSS list
 */
enum ice_status
ice_add_rss_list(struct ice_hw *hw, uint16_t vsi_handle,
    struct ice_flow_prof *prof)
{
	enum ice_rss_cfg_hdr_type hdr_type;
	struct ice_rss_cfg *r, *rss_cfg;
	uint64_t seg_match = 0;
	uint16_t i;

	ice_for_each_set_bit(i, prof->segs[prof->segs_cnt - 1].match,
			     ICE_FLOW_FIELD_IDX_MAX) {
		seg_match |= 1ULL << i;
	}

	hdr_type = ice_get_rss_hdr_type(prof);
	TAILQ_FOREACH(r, &hw->rss_list_head, l_entry)
		if (r->hash.hash_flds == seg_match &&
		    r->hash.addl_hdrs == prof->segs[prof->segs_cnt - 1].hdrs &&
		    r->hash.hdr_type == hdr_type) {
			ice_set_bit(vsi_handle, r->vsis);
			return ICE_SUCCESS;
		}

	rss_cfg = (struct ice_rss_cfg *)ice_malloc(hw, sizeof(*rss_cfg));
	if (!rss_cfg)
		return ICE_ERR_NO_MEMORY;

	rss_cfg->hash.hash_flds = seg_match;
	rss_cfg->hash.addl_hdrs = prof->segs[prof->segs_cnt - 1].hdrs;
	rss_cfg->hash.hdr_type = hdr_type;
	rss_cfg->hash.symm = prof->cfg.symm;
	ice_set_bit(vsi_handle, rss_cfg->vsis);

	TAILQ_INSERT_TAIL(&hw->rss_list_head, rss_cfg, l_entry);

	return ICE_SUCCESS;
}

#define ICE_FLOW_PROF_HASH_S	0
#define ICE_FLOW_PROF_HASH_M	(0xFFFFFFFFULL << ICE_FLOW_PROF_HASH_S)
#define ICE_FLOW_PROF_HDR_S	32
#define ICE_FLOW_PROF_HDR_M	(0x3FFFFFFFULL << ICE_FLOW_PROF_HDR_S)
#define ICE_FLOW_PROF_ENCAP_S	62
#define ICE_FLOW_PROF_ENCAP_M	(0x3ULL << ICE_FLOW_PROF_ENCAP_S)

/* Flow profile ID format:
 * [0:31] - Packet match fields
 * [32:61] - Protocol header
 * [62:63] - Encapsulation flag:
 *	     0 if non-tunneled
 *	     1 if tunneled
 *	     2 for tunneled with outer IPv4
 *	     3 for tunneled with outer IPv6
 */
#define ICE_FLOW_GEN_PROFID(hash, hdr, encap)				\
	((uint64_t)(((uint64_t)(hash) & ICE_FLOW_PROF_HASH_M) |		\
	       (((uint64_t)(hdr) << ICE_FLOW_PROF_HDR_S) &		\
	            ICE_FLOW_PROF_HDR_M) |				\
	       (((uint64_t)(encap) << ICE_FLOW_PROF_ENCAP_S) &		\
		ICE_FLOW_PROF_ENCAP_M)))

#define ICE_FLOW_SEG_HDRS_L3_MASK	\
	(ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_ARP)
#define ICE_FLOW_SEG_HDRS_L4_MASK	\
	(ICE_FLOW_SEG_HDR_ICMP | ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP | \
	 ICE_FLOW_SEG_HDR_SCTP)
/* mask for L4 protocols that are NOT part of IPv4/6 OTHER PTYPE groups */
#define ICE_FLOW_SEG_HDRS_L4_MASK_NO_OTHER	\
	(ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP | ICE_FLOW_SEG_HDR_SCTP)

/**
 * ice_is_pow2 - Check if the value is a power of 2
 * @n: 64bit number
 *
 * Check if the given value is a power of 2.
 *
 * @remark OpenBSD's powerof2 function treats zero as a power of 2, while this
 * function does not.
 *
 * @returns true or false
 */
static inline bool ice_is_pow2(uint64_t n) {
	if (n == 0)
		return false;
	return powerof2(n);
}

/**
 * ice_flow_val_hdrs - validates packet segments for valid protocol headers
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 */
enum ice_status
ice_flow_val_hdrs(struct ice_flow_seg_info *segs, uint8_t segs_cnt)
{
	uint8_t i;

	for (i = 0; i < segs_cnt; i++) {
		/* Multiple L3 headers */
		if (segs[i].hdrs & ICE_FLOW_SEG_HDRS_L3_MASK &&
		    !ice_is_pow2(segs[i].hdrs & ICE_FLOW_SEG_HDRS_L3_MASK))
			return ICE_ERR_PARAM;

		/* Multiple L4 headers */
		if (segs[i].hdrs & ICE_FLOW_SEG_HDRS_L4_MASK &&
		    !ice_is_pow2(segs[i].hdrs & ICE_FLOW_SEG_HDRS_L4_MASK))
			return ICE_ERR_PARAM;
	}

	return ICE_SUCCESS;
}

/* Size of known protocol header fields */
#define ICE_FLOW_FLD_SZ_ETH_TYPE	2
#define ICE_FLOW_FLD_SZ_VLAN		2
#define ICE_FLOW_FLD_SZ_IPV4_ADDR	4
#define ICE_FLOW_FLD_SZ_IPV6_ADDR	16
#define ICE_FLOW_FLD_SZ_IP_DSCP		1
#define ICE_FLOW_FLD_SZ_IP_TTL		1
#define ICE_FLOW_FLD_SZ_IP_PROT		1
#define ICE_FLOW_FLD_SZ_PORT		2
#define ICE_FLOW_FLD_SZ_TCP_FLAGS	1
#define ICE_FLOW_FLD_SZ_ICMP_TYPE	1
#define ICE_FLOW_FLD_SZ_ICMP_CODE	1
#define ICE_FLOW_FLD_SZ_ARP_OPER	2
#define ICE_FLOW_FLD_SZ_GRE_KEYID	4

/* Describe properties of a protocol header field */
struct ice_flow_field_info {
	enum ice_flow_seg_hdr hdr;
	int16_t off;	/* Offset from start of a protocol header, in bits */
	uint16_t size;	/* Size of fields in bits */
};

#define ICE_FLOW_FLD_INFO(_hdr, _offset_bytes, _size_bytes) { \
	.hdr = _hdr, \
	.off = (_offset_bytes) * 8, \
	.size = (_size_bytes) * 8, \
}

/* Table containing properties of supported protocol header fields */
static const
struct ice_flow_field_info ice_flds_info[ICE_FLOW_FIELD_IDX_MAX] = {
	/* Ether */
	/* ICE_FLOW_FIELD_IDX_ETH_DA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ETH, 0, ETHER_ADDR_LEN),
	/* ICE_FLOW_FIELD_IDX_ETH_SA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ETH, ETHER_ADDR_LEN, ETHER_ADDR_LEN),
	/* ICE_FLOW_FIELD_IDX_S_VLAN */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_VLAN, 12, ICE_FLOW_FLD_SZ_VLAN),
	/* ICE_FLOW_FIELD_IDX_C_VLAN */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_VLAN, 14, ICE_FLOW_FLD_SZ_VLAN),
	/* ICE_FLOW_FIELD_IDX_ETH_TYPE */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ETH, 0, ICE_FLOW_FLD_SZ_ETH_TYPE),
	/* IPv4 / IPv6 */
	/* ICE_FLOW_FIELD_IDX_IPV4_DSCP */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV4, 1, ICE_FLOW_FLD_SZ_IP_DSCP),
	/* ICE_FLOW_FIELD_IDX_IPV6_DSCP */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV6, 0, ICE_FLOW_FLD_SZ_IP_DSCP),
	/* ICE_FLOW_FIELD_IDX_IPV4_TTL */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_NONE, 8, ICE_FLOW_FLD_SZ_IP_TTL),
	/* ICE_FLOW_FIELD_IDX_IPV4_PROT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_NONE, 9, ICE_FLOW_FLD_SZ_IP_PROT),
	/* ICE_FLOW_FIELD_IDX_IPV6_TTL */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_NONE, 7, ICE_FLOW_FLD_SZ_IP_TTL),
	/* ICE_FLOW_FIELD_IDX_IPV4_PROT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_NONE, 6, ICE_FLOW_FLD_SZ_IP_PROT),
	/* ICE_FLOW_FIELD_IDX_IPV4_SA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV4, 12, ICE_FLOW_FLD_SZ_IPV4_ADDR),
	/* ICE_FLOW_FIELD_IDX_IPV4_DA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV4, 16, ICE_FLOW_FLD_SZ_IPV4_ADDR),
	/* ICE_FLOW_FIELD_IDX_IPV6_SA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV6, 8, ICE_FLOW_FLD_SZ_IPV6_ADDR),
	/* ICE_FLOW_FIELD_IDX_IPV6_DA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV6, 24, ICE_FLOW_FLD_SZ_IPV6_ADDR),
	/* Transport */
	/* ICE_FLOW_FIELD_IDX_TCP_SRC_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_TCP, 0, ICE_FLOW_FLD_SZ_PORT),
	/* ICE_FLOW_FIELD_IDX_TCP_DST_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_TCP, 2, ICE_FLOW_FLD_SZ_PORT),
	/* ICE_FLOW_FIELD_IDX_UDP_SRC_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_UDP, 0, ICE_FLOW_FLD_SZ_PORT),
	/* ICE_FLOW_FIELD_IDX_UDP_DST_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_UDP, 2, ICE_FLOW_FLD_SZ_PORT),
	/* ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_SCTP, 0, ICE_FLOW_FLD_SZ_PORT),
	/* ICE_FLOW_FIELD_IDX_SCTP_DST_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_SCTP, 2, ICE_FLOW_FLD_SZ_PORT),
	/* ICE_FLOW_FIELD_IDX_TCP_FLAGS */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_TCP, 13, ICE_FLOW_FLD_SZ_TCP_FLAGS),
	/* ARP */
	/* ICE_FLOW_FIELD_IDX_ARP_SIP */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 14, ICE_FLOW_FLD_SZ_IPV4_ADDR),
	/* ICE_FLOW_FIELD_IDX_ARP_DIP */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 24, ICE_FLOW_FLD_SZ_IPV4_ADDR),
	/* ICE_FLOW_FIELD_IDX_ARP_SHA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 8, ETHER_ADDR_LEN),
	/* ICE_FLOW_FIELD_IDX_ARP_DHA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 18, ETHER_ADDR_LEN),
	/* ICE_FLOW_FIELD_IDX_ARP_OP */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 6, ICE_FLOW_FLD_SZ_ARP_OPER),
	/* ICMP */
	/* ICE_FLOW_FIELD_IDX_ICMP_TYPE */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ICMP, 0, ICE_FLOW_FLD_SZ_ICMP_TYPE),
	/* ICE_FLOW_FIELD_IDX_ICMP_CODE */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ICMP, 1, ICE_FLOW_FLD_SZ_ICMP_CODE),
	/* GRE */
	/* ICE_FLOW_FIELD_IDX_GRE_KEYID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_GRE, 12, ICE_FLOW_FLD_SZ_GRE_KEYID),
};

/* Bitmaps indicating relevant packet types for a particular protocol header
 *
 * Packet types for packets with an Outer/First/Single MAC header
 */
static const uint32_t ice_ptypes_mac_ofos[] = {
	0xFDC00846, 0xBFBF7F7E, 0xF70001DF, 0xFEFDFDFB,
	0x0000077E, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last MAC VLAN header */
static const uint32_t ice_ptypes_macvlan_il[] = {
	0x00000000, 0xBC000000, 0x000001DF, 0xF0000000,
	0x0000077E, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single non-frag IPv4 header,
 * does NOT include IPV4 other PTYPEs
 */
static const uint32_t ice_ptypes_ipv4_ofos[] = {
	0x1D800000, 0x04000800, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single non-frag IPv4 header,
 * includes IPV4 other PTYPEs
 */
static const uint32_t ice_ptypes_ipv4_ofos_all[] = {
	0x1D800000, 0x04000800, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv4 header */
static const uint32_t ice_ptypes_ipv4_il[] = {
	0xE0000000, 0xB807700E, 0x80000003, 0xE01DC03B,
	0x0000000E, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single non-frag IPv6 header,
 * does NOT include IVP6 other PTYPEs
 */
static const uint32_t ice_ptypes_ipv6_ofos[] = {
	0x00000000, 0x00000000, 0x76000000, 0x10002000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single non-frag IPv6 header,
 * includes IPV6 other PTYPEs
 */
static const uint32_t ice_ptypes_ipv6_ofos_all[] = {
	0x00000000, 0x00000000, 0x76000000, 0x10002000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv6 header */
static const uint32_t ice_ptypes_ipv6_il[] = {
	0x00000000, 0x03B80770, 0x000001DC, 0x0EE00000,
	0x00000770, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single
 * non-frag IPv4 header - no L4
 */
static const uint32_t ice_ptypes_ipv4_ofos_no_l4[] = {
	0x10800000, 0x04000800, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv4 header - no L4 */
static const uint32_t ice_ptypes_ipv4_il_no_l4[] = {
	0x60000000, 0x18043008, 0x80000002, 0x6010c021,
	0x00000008, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single
 * non-frag IPv6 header - no L4
 */
static const uint32_t ice_ptypes_ipv6_ofos_no_l4[] = {
	0x00000000, 0x00000000, 0x42000000, 0x10002000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv6 header - no L4 */
static const uint32_t ice_ptypes_ipv6_il_no_l4[] = {
	0x00000000, 0x02180430, 0x0000010c, 0x086010c0,
	0x00000430, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outermost/First ARP header */
static const uint32_t ice_ptypes_arp_of[] = {
	0x00000800, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* UDP Packet types for non-tunneled packets or tunneled
 * packets with inner UDP.
 */
static const uint32_t ice_ptypes_udp_il[] = {
	0x81000000, 0x20204040, 0x04000010, 0x80810102,
	0x00000040, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last TCP header */
static const uint32_t ice_ptypes_tcp_il[] = {
	0x04000000, 0x80810102, 0x10000040, 0x02040408,
	0x00000102, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last SCTP header */
static const uint32_t ice_ptypes_sctp_il[] = {
	0x08000000, 0x01020204, 0x20000081, 0x04080810,
	0x00000204, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outermost/First ICMP header */
static const uint32_t ice_ptypes_icmp_of[] = {
	0x10000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last ICMP header */
static const uint32_t ice_ptypes_icmp_il[] = {
	0x00000000, 0x02040408, 0x40000102, 0x08101020,
	0x00000408, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outermost/First GRE header */
static const uint32_t ice_ptypes_gre_of[] = {
	0x00000000, 0xBFBF7800, 0x000001DF, 0xFEFDE000,
	0x0000017E, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last MAC header */
static const uint32_t ice_ptypes_mac_il[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Manage parameters and info. used during the creation of a flow profile */
struct ice_flow_prof_params {
	enum ice_block blk;
	uint16_t entry_length; /* # of bytes formatted entry will require */
	uint8_t es_cnt;
	struct ice_flow_prof *prof;

	/* For ACL, the es[0] will have the data of ICE_RX_MDID_PKT_FLAGS_15_0
	 * This will give us the direction flags.
	 */
	struct ice_fv_word es[ICE_MAX_FV_WORDS];

	ice_declare_bitmap(ptypes, ICE_FLOW_PTYPE_MAX);
};

/**
 * ice_flow_proc_seg_hdrs - process protocol headers present in pkt segments
 * @params: information about the flow to be processed
 *
 * This function identifies the packet types associated with the protocol
 * headers being present in packet segments of the specified flow profile.
 */
enum ice_status
ice_flow_proc_seg_hdrs(struct ice_flow_prof_params *params)
{
	struct ice_flow_prof *prof;
	uint8_t i;

	memset(params->ptypes, 0xff, sizeof(params->ptypes));

	prof = params->prof;

	for (i = 0; i < params->prof->segs_cnt; i++) {
		const ice_bitmap_t *src;
		uint32_t hdrs;

		hdrs = prof->segs[i].hdrs;

		if (hdrs & ICE_FLOW_SEG_HDR_ETH) {
			src = !i ? (const ice_bitmap_t *)ice_ptypes_mac_ofos :
				(const ice_bitmap_t *)ice_ptypes_mac_il;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		}

		if (i && hdrs & ICE_FLOW_SEG_HDR_VLAN) {
			src = (const ice_bitmap_t *)ice_ptypes_macvlan_il;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		}

		if (!i && hdrs & ICE_FLOW_SEG_HDR_ARP) {
			ice_and_bitmap(params->ptypes, params->ptypes,
				       (const ice_bitmap_t *)ice_ptypes_arp_of,
				       ICE_FLOW_PTYPE_MAX);
		}

		if ((hdrs & ICE_FLOW_SEG_HDR_IPV4) &&
		    (hdrs & ICE_FLOW_SEG_HDR_IPV_OTHER)) {
			src = i ? (const ice_bitmap_t *)ice_ptypes_ipv4_il :
				(const ice_bitmap_t *)ice_ptypes_ipv4_ofos_all;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		} else if ((hdrs & ICE_FLOW_SEG_HDR_IPV6) &&
			   (hdrs & ICE_FLOW_SEG_HDR_IPV_OTHER)) {
			src = i ? (const ice_bitmap_t *)ice_ptypes_ipv6_il :
				(const ice_bitmap_t *)ice_ptypes_ipv6_ofos_all;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		} else if ((hdrs & ICE_FLOW_SEG_HDR_IPV4) &&
			   !(hdrs & ICE_FLOW_SEG_HDRS_L4_MASK_NO_OTHER)) {
			src = !i ? (const ice_bitmap_t *)ice_ptypes_ipv4_ofos_no_l4 :
				(const ice_bitmap_t *)ice_ptypes_ipv4_il_no_l4;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_IPV4) {
			src = !i ? (const ice_bitmap_t *)ice_ptypes_ipv4_ofos :
				(const ice_bitmap_t *)ice_ptypes_ipv4_il;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		} else if ((hdrs & ICE_FLOW_SEG_HDR_IPV6) &&
			   !(hdrs & ICE_FLOW_SEG_HDRS_L4_MASK_NO_OTHER)) {
			src = !i ? (const ice_bitmap_t *)ice_ptypes_ipv6_ofos_no_l4 :
				(const ice_bitmap_t *)ice_ptypes_ipv6_il_no_l4;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_IPV6) {
			src = !i ? (const ice_bitmap_t *)ice_ptypes_ipv6_ofos :
				(const ice_bitmap_t *)ice_ptypes_ipv6_il;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		}

		if (hdrs & ICE_FLOW_SEG_HDR_UDP) {
			src = (const ice_bitmap_t *)ice_ptypes_udp_il;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_TCP) {
			ice_and_bitmap(params->ptypes, params->ptypes,
				       (const ice_bitmap_t *)ice_ptypes_tcp_il,
				       ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_SCTP) {
			src = (const ice_bitmap_t *)ice_ptypes_sctp_il;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		}

		if (hdrs & ICE_FLOW_SEG_HDR_ICMP) {
			src = !i ? (const ice_bitmap_t *)ice_ptypes_icmp_of :
				(const ice_bitmap_t *)ice_ptypes_icmp_il;
			ice_and_bitmap(params->ptypes, params->ptypes, src,
				       ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_GRE) {
			if (!i) {
				src = (const ice_bitmap_t *)ice_ptypes_gre_of;
				ice_and_bitmap(params->ptypes, params->ptypes,
					       src, ICE_FLOW_PTYPE_MAX);
			}
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_flow_xtract_fld - Create an extraction sequence entry for the given field
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 * @seg: packet segment index of the field to be extracted
 * @fld: ID of field to be extracted
 *
 * This function determines the protocol ID, offset, and size of the given
 * field. It then allocates one or more extraction sequence entries for the
 * given field, and fill the entries with protocol ID and offset information.
 */
enum ice_status
ice_flow_xtract_fld(struct ice_hw *hw, struct ice_flow_prof_params *params,
		    uint8_t seg, enum ice_flow_field fld)
{
	enum ice_flow_field sib = ICE_FLOW_FIELD_IDX_MAX;
	uint8_t fv_words = (uint8_t)hw->blk[params->blk].es.fvw;
	enum ice_prot_id prot_id = ICE_PROT_ID_INVAL;
	struct ice_flow_fld_info *flds;
	uint16_t cnt, ese_bits, i;
	uint16_t off;

	flds = params->prof->segs[seg].fields;

	switch (fld) {
	case ICE_FLOW_FIELD_IDX_ETH_DA:
	case ICE_FLOW_FIELD_IDX_ETH_SA:
	case ICE_FLOW_FIELD_IDX_S_VLAN:
	case ICE_FLOW_FIELD_IDX_C_VLAN:
		prot_id = seg == 0 ? ICE_PROT_MAC_OF_OR_S : ICE_PROT_MAC_IL;
		break;
	case ICE_FLOW_FIELD_IDX_ETH_TYPE:
		prot_id = seg == 0 ? ICE_PROT_ETYPE_OL : ICE_PROT_ETYPE_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV4_DSCP:
		prot_id = seg == 0 ? ICE_PROT_IPV4_OF_OR_S : ICE_PROT_IPV4_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV6_DSCP:
		prot_id = seg == 0 ? ICE_PROT_IPV6_OF_OR_S : ICE_PROT_IPV6_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV4_TTL:
	case ICE_FLOW_FIELD_IDX_IPV4_PROT:
		prot_id = seg == 0 ? ICE_PROT_IPV4_OF_OR_S : ICE_PROT_IPV4_IL;
		/* TTL and PROT share the same extraction seq. entry.
		 * Each is considered a sibling to the other in terms of sharing
		 * the same extraction sequence entry.
		 */
		if (fld == ICE_FLOW_FIELD_IDX_IPV4_TTL)
			sib = ICE_FLOW_FIELD_IDX_IPV4_PROT;
		else
			sib = ICE_FLOW_FIELD_IDX_IPV4_TTL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV6_TTL:
	case ICE_FLOW_FIELD_IDX_IPV6_PROT:
		prot_id = seg == 0 ? ICE_PROT_IPV6_OF_OR_S : ICE_PROT_IPV6_IL;
		/* TTL and PROT share the same extraction seq. entry.
		 * Each is considered a sibling to the other in terms of sharing
		 * the same extraction sequence entry.
		 */
		if (fld == ICE_FLOW_FIELD_IDX_IPV6_TTL)
			sib = ICE_FLOW_FIELD_IDX_IPV6_PROT;
		else
			sib = ICE_FLOW_FIELD_IDX_IPV6_TTL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV4_SA:
	case ICE_FLOW_FIELD_IDX_IPV4_DA:
		prot_id = seg == 0 ? ICE_PROT_IPV4_OF_OR_S : ICE_PROT_IPV4_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV6_SA:
	case ICE_FLOW_FIELD_IDX_IPV6_DA:
		prot_id = seg == 0 ? ICE_PROT_IPV6_OF_OR_S : ICE_PROT_IPV6_IL;
		break;
	case ICE_FLOW_FIELD_IDX_TCP_SRC_PORT:
	case ICE_FLOW_FIELD_IDX_TCP_DST_PORT:
	case ICE_FLOW_FIELD_IDX_TCP_FLAGS:
		prot_id = ICE_PROT_TCP_IL;
		break;
	case ICE_FLOW_FIELD_IDX_UDP_SRC_PORT:
	case ICE_FLOW_FIELD_IDX_UDP_DST_PORT:
		prot_id = ICE_PROT_UDP_IL_OR_S;
		break;
	case ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT:
	case ICE_FLOW_FIELD_IDX_SCTP_DST_PORT:
		prot_id = ICE_PROT_SCTP_IL;
		break;
	case ICE_FLOW_FIELD_IDX_ARP_SIP:
	case ICE_FLOW_FIELD_IDX_ARP_DIP:
	case ICE_FLOW_FIELD_IDX_ARP_SHA:
	case ICE_FLOW_FIELD_IDX_ARP_DHA:
	case ICE_FLOW_FIELD_IDX_ARP_OP:
		prot_id = ICE_PROT_ARP_OF;
		break;
	case ICE_FLOW_FIELD_IDX_ICMP_TYPE:
	case ICE_FLOW_FIELD_IDX_ICMP_CODE:
		/* ICMP type and code share the same extraction seq. entry */
		prot_id = (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_IPV4) ?
			ICE_PROT_ICMP_IL : ICE_PROT_ICMPV6_IL;
		sib = fld == ICE_FLOW_FIELD_IDX_ICMP_TYPE ?
			ICE_FLOW_FIELD_IDX_ICMP_CODE :
			ICE_FLOW_FIELD_IDX_ICMP_TYPE;
		break;
	case ICE_FLOW_FIELD_IDX_GRE_KEYID:
		prot_id = ICE_PROT_GRE_OF;
		break;
	default:
		return ICE_ERR_NOT_IMPL;
	}

	/* Each extraction sequence entry is a word in size, and extracts a
	 * word-aligned offset from a protocol header.
	 */
	ese_bits = ICE_FLOW_FV_EXTRACT_SZ * 8;

	flds[fld].xtrct.prot_id = (uint8_t)prot_id;
	flds[fld].xtrct.off = (ice_flds_info[fld].off / ese_bits) *
		ICE_FLOW_FV_EXTRACT_SZ;
	flds[fld].xtrct.disp = (uint8_t)(ice_flds_info[fld].off % ese_bits);
	flds[fld].xtrct.idx = params->es_cnt;

	/* Adjust the next field-entry index after accommodating the number of
	 * entries this field consumes
	 */
	cnt = howmany(flds[fld].xtrct.disp + ice_flds_info[fld].size, ese_bits);

	/* Fill in the extraction sequence entries needed for this field */
	off = flds[fld].xtrct.off;
	for (i = 0; i < cnt; i++) {
		/* Only consume an extraction sequence entry if there is no
		 * sibling field associated with this field or the sibling entry
		 * already extracts the word shared with this field.
		 */
		if (sib == ICE_FLOW_FIELD_IDX_MAX ||
		    flds[sib].xtrct.prot_id == ICE_PROT_ID_INVAL ||
		    flds[sib].xtrct.off != off) {
			uint8_t idx;

			/* Make sure the number of extraction sequence required
			 * does not exceed the block's capability
			 */
			if (params->es_cnt >= fv_words)
				return ICE_ERR_MAX_LIMIT;

			/* some blocks require a reversed field vector layout */
			if (hw->blk[params->blk].es.reverse)
				idx = fv_words - params->es_cnt - 1;
			else
				idx = params->es_cnt;

			params->es[idx].prot_id = (uint8_t)prot_id;
			params->es[idx].off = off;
			params->es_cnt++;
		}

		off += ICE_FLOW_FV_EXTRACT_SZ;
	}

	return ICE_SUCCESS;
}

/**
 * ice_flow_create_xtrct_seq - Create an extraction sequence for given segments
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 *
 * This function iterates through all matched fields in the given segments, and
 * creates an extraction sequence for the fields.
 */
enum ice_status
ice_flow_create_xtrct_seq(struct ice_hw *hw,
			  struct ice_flow_prof_params *params)
{
	enum ice_status status = ICE_SUCCESS;
	uint8_t i;

	for (i = 0; i < params->prof->segs_cnt; i++) {
		ice_declare_bitmap(match, ICE_FLOW_FIELD_IDX_MAX);
		enum ice_flow_field j;

		ice_cp_bitmap(match, params->prof->segs[i].match,
			      ICE_FLOW_FIELD_IDX_MAX);
		ice_for_each_set_bit(j, match, ICE_FLOW_FIELD_IDX_MAX) {
			status = ice_flow_xtract_fld(hw, params, i, j);
			if (status)
				return status;
			ice_clear_bit(j, match);
		}
	}

	return status;
}

/**
 * ice_flow_proc_segs - process all packet segments associated with a profile
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 */
enum ice_status
ice_flow_proc_segs(struct ice_hw *hw, struct ice_flow_prof_params *params)
{
	enum ice_status status;

	status = ice_flow_proc_seg_hdrs(params);
	if (status)
		return status;

	status = ice_flow_create_xtrct_seq(hw, params);
	if (status)
		return status;

	switch (params->blk) {
	case ICE_BLK_RSS:
		status = ICE_SUCCESS;
		break;
	default:
		return ICE_ERR_NOT_IMPL;
	}

	return status;
}

/**
 * ice_find_prof_id - find profile ID for a given field vector
 * @hw: pointer to the hardware structure
 * @blk: HW block
 * @fv: field vector to search for
 * @prof_id: receives the profile ID
 */
enum ice_status
ice_find_prof_id(struct ice_hw *hw, enum ice_block blk,
		 struct ice_fv_word *fv, uint8_t *prof_id)
{
	struct ice_es *es = &hw->blk[blk].es;
	uint16_t off;
	uint8_t i;

	for (i = 0; i < (uint8_t)es->count; i++) {
		off = i * es->fvw;

		if (memcmp(&es->t[off], fv, es->fvw * sizeof(*fv)))
			continue;

		*prof_id = i;
		return ICE_SUCCESS;
	}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_alloc_prof_id - allocate profile ID
 * @hw: pointer to the HW struct
 * @blk: the block to allocate the profile ID for
 * @prof_id: pointer to variable to receive the profile ID
 *
 * This function allocates a new profile ID, which also corresponds to a Field
 * Vector (Extraction Sequence) entry.
 */
enum ice_status
ice_alloc_prof_id(struct ice_hw *hw, enum ice_block blk, uint8_t *prof_id)
{
	enum ice_status status;
	uint16_t res_type;
	uint16_t get_prof;

	if (!ice_prof_id_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	status = ice_alloc_hw_res(hw, res_type, 1, false, &get_prof);
	if (!status)
		*prof_id = (uint8_t)get_prof;

	return status;
}

/**
 * ice_add_prof - add profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @id: profile tracking ID
 * @ptypes: bitmap indicating ptypes (ICE_FLOW_PTYPE_MAX bits)
 * @es: extraction sequence (length of array is determined by the block)
 *
 * This function registers a profile, which matches a set of PTGs with a
 * particular extraction sequence. While the hardware profile is allocated
 * it will not be written until the first call to ice_add_flow that specifies
 * the ID value used here.
 */
enum ice_status
ice_add_prof(struct ice_hw *hw, enum ice_block blk, uint64_t id,
	     ice_bitmap_t *ptypes, struct ice_fv_word *es)
{
	ice_declare_bitmap(ptgs_used, ICE_XLT1_CNT);
	struct ice_prof_map *prof;
	enum ice_status status;
	uint8_t prof_id;
	uint16_t ptype;

	ice_zero_bitmap(ptgs_used, ICE_XLT1_CNT);
#if 0
	ice_acquire_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	/* search for existing profile */
	status = ice_find_prof_id(hw, blk, es, &prof_id);
	if (status) {
		/* allocate profile ID */
		status = ice_alloc_prof_id(hw, blk, &prof_id);
		if (status)
			goto err_ice_add_prof;

		/* and write new es */
		ice_write_es(hw, blk, prof_id, es);
	}

	ice_prof_inc_ref(hw, blk, prof_id);

	/* add profile info */

	prof = (struct ice_prof_map *)ice_malloc(hw, sizeof(*prof));
	if (!prof)
		goto err_ice_add_prof;

	prof->profile_cookie = id;
	prof->prof_id = prof_id;
	prof->ptg_cnt = 0;
	prof->context = 0;

	/* build list of ptgs */
	ice_for_each_set_bit(ptype, ptypes, ICE_FLOW_PTYPE_MAX) {
		uint8_t ptg;

		/* The package should place all ptypes in a non-zero
		 * PTG, so the following call should never fail.
		 */
		if (ice_ptg_find_ptype(hw, blk, ptype, &ptg))
			continue;

		/* If PTG is already added, skip and continue */
		if (ice_is_bit_set(ptgs_used, ptg))
			continue;

		ice_set_bit(ptg, ptgs_used);
		prof->ptg[prof->ptg_cnt] = ptg;

		if (++prof->ptg_cnt >= ICE_MAX_PTG_PER_PROFILE)
			break;
	}

	TAILQ_INSERT_HEAD(&hw->blk[blk].es.prof_map, prof, list);
	status = ICE_SUCCESS;

err_ice_add_prof:
#if 0
	ice_release_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	return status;
}

#define ICE_FV_OFFSET_INVAL	0x1FF

/**
 * ice_flow_add_prof_sync - Add a flow profile for packet segments and fields
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @dir: flow direction
 * @prof_id: unique ID to identify this flow profile
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 * @acts: array of default actions
 * @acts_cnt: number of default actions
 * @prof: stores the returned flow profile added
 *
 * Assumption: the caller has acquired the lock to the profile list
 */
enum ice_status
ice_flow_add_prof_sync(struct ice_hw *hw, enum ice_block blk,
		       enum ice_flow_dir dir, uint64_t prof_id,
		       struct ice_flow_seg_info *segs, uint8_t segs_cnt,
		       struct ice_flow_action *acts, uint8_t acts_cnt,
		       struct ice_flow_prof **prof)
{
	struct ice_flow_prof_params *params;
	enum ice_status status;
	uint8_t i;

	if (!prof || (acts_cnt && !acts))
		return ICE_ERR_BAD_PTR;

	params = (struct ice_flow_prof_params *)ice_malloc(hw, sizeof(*params));
	if (!params)
		return ICE_ERR_NO_MEMORY;

	params->prof = (struct ice_flow_prof *)
		ice_malloc(hw, sizeof(*params->prof));
	if (!params->prof) {
		status = ICE_ERR_NO_MEMORY;
		goto free_params;
	}

	/* initialize extraction sequence to all invalid (0xff) */
	for (i = 0; i < ICE_MAX_FV_WORDS; i++) {
		params->es[i].prot_id = ICE_PROT_INVALID;
		params->es[i].off = ICE_FV_OFFSET_INVAL;
	}

	params->blk = blk;
	params->prof->id = prof_id;
	params->prof->dir = dir;
	params->prof->segs_cnt = segs_cnt;

	/* Make a copy of the segments that need to be persistent in the flow
	 * profile instance
	 */
	for (i = 0; i < segs_cnt; i++)
		memcpy(&params->prof->segs[i], &segs[i], sizeof(*segs));

	status = ice_flow_proc_segs(hw, params);
	if (status) {
		DNPRINTF(ICE_DBG_FLOW,
		    "%s: Error processing a flow's packet segments\n",
		    __func__);
		goto out;
	}

	/* Add a HW profile for this flow profile */
	status = ice_add_prof(hw, blk, prof_id, params->ptypes, params->es);
	if (status) {
		DNPRINTF(ICE_DBG_FLOW, "%s: Error adding a HW flow profile\n",
		    __func__);
		goto out;
	}

	*prof = params->prof;

out:
	if (status) {
		ice_free(hw, params->prof);
	}
free_params:
	ice_free(hw, params);

	return status;
}

/**
 * ice_flow_add_prof - Add a flow profile for packet segments and matched fields
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @dir: flow direction
 * @prof_id: unique ID to identify this flow profile
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 * @acts: array of default actions
 * @acts_cnt: number of default actions
 * @prof: stores the returned flow profile added
 */
static enum ice_status
ice_flow_add_prof(struct ice_hw *hw, enum ice_block blk, enum ice_flow_dir dir,
    uint64_t prof_id, struct ice_flow_seg_info *segs, uint8_t segs_cnt,
    struct ice_flow_action *acts, uint8_t acts_cnt,
    struct ice_flow_prof **prof)
{
	enum ice_status status;

	if (segs_cnt > ICE_FLOW_SEG_MAX)
		return ICE_ERR_MAX_LIMIT;

	if (!segs_cnt)
		return ICE_ERR_PARAM;

	if (!segs)
		return ICE_ERR_BAD_PTR;

	status = ice_flow_val_hdrs(segs, segs_cnt);
	if (status)
		return status;
#if 0
	ice_acquire_lock(&hw->fl_profs_locks[blk]);
#endif
	status = ice_flow_add_prof_sync(hw, blk, dir, prof_id, segs, segs_cnt,
					acts, acts_cnt, prof);
	if (!status)
		TAILQ_INSERT_HEAD(&hw->fl_profs[blk], *prof, l_entry);
#if 0
	ice_release_lock(&hw->fl_profs_locks[blk]);
#endif
	return status;
}

/**
 * ice_get_prof - get profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @hdl: profile handle
 * @chg: change list
 */
enum ice_status
ice_get_prof(struct ice_hw *hw, enum ice_block blk, uint64_t hdl,
    struct ice_chs_chg_head *chg)
{
	enum ice_status status = ICE_SUCCESS;
	struct ice_prof_map *map;
	struct ice_chs_chg *p;
	uint16_t i;
#if 0
	ice_acquire_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	/* Get the details on the profile specified by the handle ID */
	map = ice_search_prof_id(hw, blk, hdl);
	if (!map) {
		status = ICE_ERR_DOES_NOT_EXIST;
		goto err_ice_get_prof;
	}

	for (i = 0; i < map->ptg_cnt; i++)
		if (!hw->blk[blk].es.written[map->prof_id]) {
			/* add ES to change list */
			p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
			if (!p) {
				status = ICE_ERR_NO_MEMORY;
				goto err_ice_get_prof;
			}

			p->type = ICE_PTG_ES_ADD;
			p->ptype = 0;
			p->ptg = map->ptg[i];
			p->add_ptg = 0;

			p->add_prof = 1;
			p->prof_id = map->prof_id;

			hw->blk[blk].es.written[map->prof_id] = true;

			TAILQ_INSERT_HEAD(chg, p, list_entry);
		}

err_ice_get_prof:
#if 0
	ice_release_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	/* let caller clean up the change list */
	return status;
}

/**
 * ice_add_prof_to_lst - add profile entry to a list
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @lst: the list to be added to
 * @hdl: profile handle of entry to add
 */
enum ice_status
ice_add_prof_to_lst(struct ice_hw *hw, enum ice_block blk,
	struct ice_vsig_prof_head *lst, uint64_t hdl)
{
	enum ice_status status = ICE_SUCCESS;
	struct ice_prof_map *map;
	struct ice_vsig_prof *p;
	uint16_t i;
#if 0
	ice_acquire_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	map = ice_search_prof_id(hw, blk, hdl);
	if (!map) {
		status = ICE_ERR_DOES_NOT_EXIST;
		goto err_ice_add_prof_to_lst;
	}

	p = (struct ice_vsig_prof *)ice_malloc(hw, sizeof(*p));
	if (!p) {
		status = ICE_ERR_NO_MEMORY;
		goto err_ice_add_prof_to_lst;
	}

	p->profile_cookie = map->profile_cookie;
	p->prof_id = map->prof_id;
	p->tcam_count = map->ptg_cnt;

	for (i = 0; i < map->ptg_cnt; i++) {
		p->tcam[i].prof_id = map->prof_id;
		p->tcam[i].tcam_idx = ICE_INVALID_TCAM;
		p->tcam[i].ptg = map->ptg[i];
	}

	TAILQ_INSERT_HEAD(lst, p, list);

err_ice_add_prof_to_lst:
#if 0
	ice_release_lock(&hw->blk[blk].es.prof_map_lock);
#endif
	return status;
}

/**
 * ice_find_prof_vsig - find a VSIG with a specific profile handle
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @hdl: the profile handle of the profile to search for
 * @vsig: returns the VSIG with the matching profile
 */
bool
ice_find_prof_vsig(struct ice_hw *hw, enum ice_block blk, uint64_t hdl,
    uint16_t *vsig)
{
	struct ice_vsig_prof *t;
	enum ice_status status;
	struct ice_vsig_prof_head lst;

	TAILQ_INIT(&lst);

	t = (struct ice_vsig_prof *)ice_malloc(hw, sizeof(*t));
	if (!t)
		return false;

	t->profile_cookie = hdl;
	TAILQ_INSERT_HEAD(&lst, t, list);

	status = ice_find_dup_props_vsig(hw, blk, &lst, vsig);

	TAILQ_REMOVE(&lst, t, list);
	ice_free(hw, t);

	return status == ICE_SUCCESS;
}

/**
 * ice_create_prof_id_vsig - add a new VSIG with a single profile
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the initial VSI that will be in VSIG
 * @hdl: the profile handle of the profile that will be added to the VSIG
 * @chg: the change list
 */
enum ice_status
ice_create_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, uint16_t vsi,
    uint64_t hdl, struct ice_chs_chg_head *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;
	uint16_t new_vsig;

	p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
	if (!p)
		return ICE_ERR_NO_MEMORY;

	new_vsig = ice_vsig_alloc(hw, blk);
	if (!new_vsig) {
		status = ICE_ERR_HW_TABLE;
		goto err_ice_create_prof_id_vsig;
	}

	status = ice_move_vsi(hw, blk, vsi, new_vsig, chg);
	if (status)
		goto err_ice_create_prof_id_vsig;

	status = ice_add_prof_id_vsig(hw, blk, new_vsig, hdl, false, chg);
	if (status)
		goto err_ice_create_prof_id_vsig;

	p->type = ICE_VSIG_ADD;
	p->vsi = vsi;
	p->orig_vsig = ICE_DEFAULT_VSIG;
	p->vsig = new_vsig;

	TAILQ_INSERT_HEAD(chg, p, list_entry);

	return ICE_SUCCESS;

err_ice_create_prof_id_vsig:
	/* let caller clean up the change list */
	ice_free(hw, p);
	return status;
}

/**
 * ice_add_prof_id_flow - add profile flow
 * @hw: pointer to the HW struct
 * @blk: hardware block
 * @vsi: the VSI to enable with the profile specified by ID
 * @hdl: profile handle
 *
 * Calling this function will update the hardware tables to enable the
 * profile indicated by the ID parameter for the VSIs specified in the VSI
 * array. Once successfully called, the flow will be enabled.
 */
enum ice_status
ice_add_prof_id_flow(struct ice_hw *hw, enum ice_block blk, uint16_t vsi,
    uint64_t hdl)
{
	struct ice_vsig_prof *tmp1, *del1;
	struct ice_chs_chg *tmp, *del;
	struct ice_vsig_prof_head union_lst;
	enum ice_status status;
	struct ice_chs_chg_head chg;
	uint16_t vsig;

	TAILQ_INIT(&union_lst);
	TAILQ_INIT(&chg);

	/* Get profile */
	status = ice_get_prof(hw, blk, hdl, &chg);
	if (status)
		return status;

	/* determine if VSI is already part of a VSIG */
	status = ice_vsig_find_vsi(hw, blk, vsi, &vsig);
	if (!status && vsig) {
		bool only_vsi;
		uint16_t or_vsig;
		uint16_t ref;

		/* found in VSIG */
		or_vsig = vsig;

		/* make sure that there is no overlap/conflict between the new
		 * characteristics and the existing ones; we don't support that
		 * scenario
		 */
		if (ice_has_prof_vsig(hw, blk, vsig, hdl)) {
			status = ICE_ERR_ALREADY_EXISTS;
			goto err_ice_add_prof_id_flow;
		}

		/* last VSI in the VSIG? */
		status = ice_vsig_get_ref(hw, blk, vsig, &ref);
		if (status)
			goto err_ice_add_prof_id_flow;
		only_vsi = (ref == 1);

		/* create a union of the current profiles and the one being
		 * added
		 */
		status = ice_get_profs_vsig(hw, blk, vsig, &union_lst);
		if (status)
			goto err_ice_add_prof_id_flow;

		status = ice_add_prof_to_lst(hw, blk, &union_lst, hdl);
		if (status)
			goto err_ice_add_prof_id_flow;

		/* search for an existing VSIG with an exact charc match */
		status = ice_find_dup_props_vsig(hw, blk, &union_lst, &vsig);
		if (!status) {
			/* move VSI to the VSIG that matches */
			status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* VSI has been moved out of or_vsig. If the or_vsig had
			 * only that VSI it is now empty and can be removed.
			 */
			if (only_vsi) {
				status = ice_rem_vsig(hw, blk, or_vsig, &chg);
				if (status)
					goto err_ice_add_prof_id_flow;
			}
		} else if (only_vsi) {
			/* If the original VSIG only contains one VSI, then it
			 * will be the requesting VSI. In this case the VSI is
			 * not sharing entries and we can simply add the new
			 * profile to the VSIG.
			 */
			status = ice_add_prof_id_vsig(hw, blk, vsig, hdl, false,
						      &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* Adjust priorities */
			status = ice_adj_prof_priorities(hw, blk, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		} else {
			/* No match, so we need a new VSIG */
			status = ice_create_vsig_from_lst(hw, blk, vsi,
							  &union_lst, &vsig,
							  &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* Adjust priorities */
			status = ice_adj_prof_priorities(hw, blk, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		}
	} else {
		/* need to find or add a VSIG */
		/* search for an existing VSIG with an exact charc match */
		if (ice_find_prof_vsig(hw, blk, hdl, &vsig)) {
			/* found an exact match */
			/* add or move VSI to the VSIG that matches */
			status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		} else {
			/* we did not find an exact match */
			/* we need to add a VSIG */
			status = ice_create_prof_id_vsig(hw, blk, vsi, hdl,
							 &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		}
	}

	/* update hardware */
	if (!status)
		status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_add_prof_id_flow:
	TAILQ_FOREACH_SAFE(del, &chg, list_entry, tmp) {
		TAILQ_REMOVE(&chg, del, list_entry);
		ice_free(hw, del);
	}

	TAILQ_FOREACH_SAFE(del1, &union_lst, list, tmp1) {
		TAILQ_REMOVE(&union_lst, del1, list);
		ice_free(hw, del1);
	}

	return status;
}

/**
 * ice_flow_assoc_prof - associate a VSI with a flow profile
 * @hw: pointer to the hardware structure
 * @blk: classification stage
 * @prof: pointer to flow profile
 * @vsi_handle: software VSI handle
 *
 * Assumption: the caller has acquired the lock to the profile list
 * and the software VSI handle has been validated
 */
enum ice_status
ice_flow_assoc_prof(struct ice_hw *hw, enum ice_block blk,
		    struct ice_flow_prof *prof, uint16_t vsi_handle)
{
	enum ice_status status = ICE_SUCCESS;

	if (!ice_is_bit_set(prof->vsis, vsi_handle)) {
		status = ice_add_prof_id_flow(hw, blk,
					      hw->vsi_ctx[vsi_handle]->vsi_num,
					      prof->id);
		if (!status)
			ice_set_bit(vsi_handle, prof->vsis);
		else
			DNPRINTF(ICE_DBG_FLOW,
			     "%s: HW profile add failed, %d\n",
			     __func__, status);
	}

	return status;
}

#define ICE_FLOW_RSS_SEG_HDR_L3_MASKS \
	(ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV6)

#define ICE_FLOW_RSS_SEG_HDR_L4_MASKS \
	(ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP | ICE_FLOW_SEG_HDR_SCTP)

#define ICE_FLOW_RSS_SEG_HDR_VAL_MASKS \
	(ICE_FLOW_RSS_SEG_HDR_L3_MASKS | \
	 ICE_FLOW_RSS_SEG_HDR_L4_MASKS)

#define ICE_FLOW_SET_HDRS(seg, val)	((seg)->hdrs |= (uint32_t)(val))

/**
 * ice_flow_set_fld_ext - specifies locations of field from entry's input buffer
 * @seg: packet segment the field being set belongs to
 * @fld: field to be set
 * @field_type: type of the field
 * @val_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of the value to match from
 *           entry's input buffer
 * @mask_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of mask value from entry's
 *            input buffer
 * @last_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of last/upper value from
 *            entry's input buffer
 *
 * This helper function stores information of a field being matched, including
 * the type of the field and the locations of the value to match, the mask, and
 * the upper-bound value in the start of the input buffer for a flow entry.
 * This function should only be used for fixed-size data structures.
 *
 * This function also opportunistically determines the protocol headers to be
 * present based on the fields being set. Some fields cannot be used alone to
 * determine the protocol headers present. Sometimes, fields for particular
 * protocol headers are not matched. In those cases, the protocol headers
 * must be explicitly set.
 */
void
ice_flow_set_fld_ext(struct ice_flow_seg_info *seg, enum ice_flow_field fld,
		     enum ice_flow_fld_match_type field_type, uint16_t val_loc,
		     uint16_t mask_loc, uint16_t last_loc)
{
	ice_set_bit(fld, seg->match);
	if (field_type == ICE_FLOW_FLD_TYPE_RANGE)
		ice_set_bit(fld, seg->range);

	seg->fields[fld].type = field_type;
	seg->fields[fld].src.val = val_loc;
	seg->fields[fld].src.mask = mask_loc;
	seg->fields[fld].src.last = last_loc;

	ICE_FLOW_SET_HDRS(seg, ice_flds_info[fld].hdr);
}

/**
 * ice_flow_set_fld - specifies locations of field from entry's input buffer
 * @seg: packet segment the field being set belongs to
 * @fld: field to be set
 * @val_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of the value to match from
 *           entry's input buffer
 * @mask_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of mask value from entry's
 *            input buffer
 * @last_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of last/upper value from
 *            entry's input buffer
 * @range: indicate if field being matched is to be in a range
 *
 * This function specifies the locations, in the form of byte offsets from the
 * start of the input buffer for a flow entry, from where the value to match,
 * the mask value, and upper value can be extracted. These locations are then
 * stored in the flow profile. When adding a flow entry associated with the
 * flow profile, these locations will be used to quickly extract the values and
 * create the content of a match entry. This function should only be used for
 * fixed-size data structures.
 */
void
ice_flow_set_fld(struct ice_flow_seg_info *seg, enum ice_flow_field fld,
		 uint16_t val_loc, uint16_t mask_loc, uint16_t last_loc,
		 bool range)
{
	enum ice_flow_fld_match_type t = range ?
		ICE_FLOW_FLD_TYPE_RANGE : ICE_FLOW_FLD_TYPE_REG;

	ice_flow_set_fld_ext(seg, fld, t, val_loc, mask_loc, last_loc);
}

/**
 * ice_flow_set_rss_seg_info - setup packet segments for RSS
 * @segs: pointer to the flow field segment(s)
 * @seg_cnt: segment count
 * @cfg: configure parameters
 *
 * Helper function to extract fields from hash bitmap and use flow
 * header value to set flow field segment for further use in flow
 * profile entry or removal.
 */
enum ice_status
ice_flow_set_rss_seg_info(struct ice_flow_seg_info *segs, uint8_t seg_cnt,
			  const struct ice_rss_hash_cfg *cfg)
{
	struct ice_flow_seg_info *seg;
	uint64_t val;
	uint16_t i;

	/* set inner most segment */
	seg = &segs[seg_cnt - 1];

	ice_for_each_set_bit(i, (const ice_bitmap_t *)&cfg->hash_flds,
			     (uint16_t)ICE_FLOW_FIELD_IDX_MAX)
		ice_flow_set_fld(seg, (enum ice_flow_field)i,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);

	ICE_FLOW_SET_HDRS(seg, cfg->addl_hdrs);

	/* set outer most header */
	if (cfg->hdr_type == ICE_RSS_INNER_HEADERS_W_OUTER_IPV4)
		segs[ICE_RSS_OUTER_HEADERS].hdrs |= ICE_FLOW_SEG_HDR_IPV4 |
						    ICE_FLOW_SEG_HDR_IPV_FRAG |
						    ICE_FLOW_SEG_HDR_IPV_OTHER;
	else if (cfg->hdr_type == ICE_RSS_INNER_HEADERS_W_OUTER_IPV6)
		segs[ICE_RSS_OUTER_HEADERS].hdrs |= ICE_FLOW_SEG_HDR_IPV6 |
						    ICE_FLOW_SEG_HDR_IPV_FRAG |
						    ICE_FLOW_SEG_HDR_IPV_OTHER;
	else if (cfg->hdr_type == ICE_RSS_INNER_HEADERS_W_OUTER_IPV4_GRE)
		segs[ICE_RSS_OUTER_HEADERS].hdrs |= ICE_FLOW_SEG_HDR_IPV4 |
						    ICE_FLOW_SEG_HDR_GRE |
						    ICE_FLOW_SEG_HDR_IPV_OTHER;
	else if (cfg->hdr_type == ICE_RSS_INNER_HEADERS_W_OUTER_IPV6_GRE)
		segs[ICE_RSS_OUTER_HEADERS].hdrs |= ICE_FLOW_SEG_HDR_IPV6 |
						    ICE_FLOW_SEG_HDR_GRE |
						    ICE_FLOW_SEG_HDR_IPV_OTHER;

	if (seg->hdrs & ~ICE_FLOW_RSS_SEG_HDR_VAL_MASKS)
		return ICE_ERR_PARAM;

	val = (uint64_t)(seg->hdrs & ICE_FLOW_RSS_SEG_HDR_L3_MASKS);
	if (val && !ice_is_pow2(val))
		return ICE_ERR_CFG;

	val = (uint64_t)(seg->hdrs & ICE_FLOW_RSS_SEG_HDR_L4_MASKS);
	if (val && !ice_is_pow2(val))
		return ICE_ERR_CFG;

	return ICE_SUCCESS;
}

#define ICE_FLOW_FIND_PROF_CHK_FLDS	0x00000001
#define ICE_FLOW_FIND_PROF_CHK_VSI	0x00000002
#define ICE_FLOW_FIND_PROF_NOT_CHK_DIR	0x00000004

/**
 * ice_flow_find_prof_conds - Find a profile matching headers and conditions
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @dir: flow direction
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 * @vsi_handle: software VSI handle to check VSI (ICE_FLOW_FIND_PROF_CHK_VSI)
 * @conds: additional conditions to be checked (ICE_FLOW_FIND_PROF_CHK_*)
 */
struct ice_flow_prof *
ice_flow_find_prof_conds(struct ice_hw *hw, enum ice_block blk,
			 enum ice_flow_dir dir, struct ice_flow_seg_info *segs,
			 uint8_t segs_cnt, uint16_t vsi_handle, uint32_t conds)
{
	struct ice_flow_prof *p, *prof = NULL;
#if 0
	ice_acquire_lock(&hw->fl_profs_locks[blk]);
#endif
	TAILQ_FOREACH(p, &hw->fl_profs[blk], l_entry) {
		if ((p->dir == dir || conds & ICE_FLOW_FIND_PROF_NOT_CHK_DIR) &&
		    segs_cnt && segs_cnt == p->segs_cnt) {
			uint8_t i;

			/* Check for profile-VSI association if specified */
			if ((conds & ICE_FLOW_FIND_PROF_CHK_VSI) &&
			    ice_is_vsi_valid(hw, vsi_handle) &&
			    !ice_is_bit_set(p->vsis, vsi_handle))
				continue;

			/* Protocol headers must be checked. Matched fields are
			 * checked if specified.
			 */
			for (i = 0; i < segs_cnt; i++)
				if (segs[i].hdrs != p->segs[i].hdrs ||
				    ((conds & ICE_FLOW_FIND_PROF_CHK_FLDS) &&
				     (ice_cmp_bitmap(segs[i].match,
						     p->segs[i].match,
						     ICE_FLOW_FIELD_IDX_MAX) ==
				       false)))
					break;

			/* A match is found if all segments are matched */
			if (i == segs_cnt) {
				prof = p;
				break;
			}
		}
	}
#if 0
	ice_release_lock(&hw->fl_profs_locks[blk]);
#endif
	return prof;
}

/**
 * ice_rem_rss_list - remove RSS configuration from list
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @prof: pointer to flow profile
 *
 * Assumption: lock has already been acquired for RSS list
 */
void
ice_rem_rss_list(struct ice_hw *hw, uint16_t vsi_handle,
    struct ice_flow_prof *prof)
{
	enum ice_rss_cfg_hdr_type hdr_type;
	struct ice_rss_cfg *r, *tmp;
	uint64_t seg_match = 0;
	uint16_t i;

	/* convert match bitmap to u64 for hash field comparison */
	ice_for_each_set_bit(i, prof->segs[prof->segs_cnt - 1].match,
			     ICE_FLOW_FIELD_IDX_MAX) {
		seg_match |= 1ULL << i;
	}

	/* Search for RSS hash fields associated to the VSI that match the
	 * hash configurations associated to the flow profile. If found
	 * remove from the RSS entry list of the VSI context and delete entry.
	 */
	hdr_type = ice_get_rss_hdr_type(prof);
	TAILQ_FOREACH_SAFE(r, &hw->rss_list_head, l_entry, tmp) {
		if (r->hash.hash_flds == seg_match &&
		    r->hash.addl_hdrs == prof->segs[prof->segs_cnt - 1].hdrs &&
		    r->hash.hdr_type == hdr_type) {
			ice_clear_bit(vsi_handle, r->vsis);
			if (!ice_is_any_bit_set(r->vsis, ICE_MAX_VSI)) {
				TAILQ_REMOVE(&hw->rss_list_head, r, l_entry);
				ice_free(hw, r);
			}
			return;
		}
	}
}

/**
 * ice_add_rss_cfg_sync - add an RSS configuration
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @cfg: configure parameters
 *
 * Assumption: lock has already been acquired for RSS list
 */
enum ice_status
ice_add_rss_cfg_sync(struct ice_hw *hw, uint16_t vsi_handle,
		     const struct ice_rss_hash_cfg *cfg)
{
	const enum ice_block blk = ICE_BLK_RSS;
	struct ice_flow_prof *prof = NULL;
	struct ice_flow_seg_info *segs;
	enum ice_status status;
	uint8_t segs_cnt;

	if (cfg->symm)
		return ICE_ERR_PARAM;

	segs_cnt = (cfg->hdr_type == ICE_RSS_OUTER_HEADERS) ?
			   ICE_FLOW_SEG_SINGLE :
			   ICE_FLOW_SEG_MAX;

	segs = (struct ice_flow_seg_info *)ice_calloc(hw, segs_cnt,
						      sizeof(*segs));
	if (!segs)
		return ICE_ERR_NO_MEMORY;

	/* Construct the packet segment info from the hashed fields */
	status = ice_flow_set_rss_seg_info(segs, segs_cnt, cfg);
	if (status)
		goto exit;

	/* Search for a flow profile that has matching headers, hash fields
	 * and has the input VSI associated to it. If found, no further
	 * operations required and exit.
	 */
	prof = ice_flow_find_prof_conds(hw, blk, ICE_FLOW_RX, segs, segs_cnt,
					vsi_handle,
					ICE_FLOW_FIND_PROF_CHK_FLDS |
					ICE_FLOW_FIND_PROF_CHK_VSI);
	if (prof)
		goto exit;

	/* Check if a flow profile exists with the same protocol headers and
	 * associated with the input VSI. If so disassociate the VSI from
	 * this profile. The VSI will be added to a new profile created with
	 * the protocol header and new hash field configuration.
	 */
	prof = ice_flow_find_prof_conds(hw, blk, ICE_FLOW_RX, segs, segs_cnt,
					vsi_handle, ICE_FLOW_FIND_PROF_CHK_VSI);
	if (prof) {
		status = ice_flow_disassoc_prof(hw, blk, prof, vsi_handle);
		if (!status)
			ice_rem_rss_list(hw, vsi_handle, prof);
		else
			goto exit;

		/* Remove profile if it has no VSIs associated */
		if (!ice_is_any_bit_set(prof->vsis, ICE_MAX_VSI)) {
			status = ice_flow_rem_prof(hw, blk, prof->id);
			if (status)
				goto exit;
		}
	}

	/* Search for a profile that has same match fields only. If this
	 * exists then associate the VSI to this profile.
	 */
	prof = ice_flow_find_prof_conds(hw, blk, ICE_FLOW_RX, segs, segs_cnt,
					vsi_handle,
					ICE_FLOW_FIND_PROF_CHK_FLDS);
	if (prof) {
		status = ice_flow_assoc_prof(hw, blk, prof, vsi_handle);
		if (!status)
			status = ice_add_rss_list(hw, vsi_handle, prof);
		goto exit;
	}

	/* Create a new flow profile with generated profile and packet
	 * segment information.
	 */
	status = ice_flow_add_prof(hw, blk, ICE_FLOW_RX,
				   ICE_FLOW_GEN_PROFID(cfg->hash_flds,
						       segs[segs_cnt - 1].hdrs,
						       cfg->hdr_type),
				   segs, segs_cnt, NULL, 0, &prof);
	if (status)
		goto exit;

	status = ice_flow_assoc_prof(hw, blk, prof, vsi_handle);
	/* If association to a new flow profile failed then this profile can
	 * be removed.
	 */
	if (status) {
		ice_flow_rem_prof(hw, blk, prof->id);
		goto exit;
	}

	status = ice_add_rss_list(hw, vsi_handle, prof);

	prof->cfg.symm = cfg->symm;

exit:
	ice_free(hw, segs);
	return status;
}

/**
 * ice_add_rss_cfg - add an RSS configuration with specified hashed fields
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @cfg: configure parameters
 *
 * This function will generate a flow profile based on fields associated with
 * the input fields to hash on, the flow type and use the VSI number to add
 * a flow entry to the profile.
 */
enum ice_status
ice_add_rss_cfg(struct ice_hw *hw, uint16_t vsi_handle,
		const struct ice_rss_hash_cfg *cfg)
{
	struct ice_rss_hash_cfg local_cfg;
	enum ice_status status;

	if (!ice_is_vsi_valid(hw, vsi_handle) || !cfg ||
	    cfg->hdr_type > ICE_RSS_ANY_HEADERS ||
	    cfg->hash_flds == ICE_HASH_INVALID)
		return ICE_ERR_PARAM;
#if 0
	ice_acquire_lock(&hw->rss_locks);
#endif
	local_cfg = *cfg;
	if (cfg->hdr_type < ICE_RSS_ANY_HEADERS) {
		status = ice_add_rss_cfg_sync(hw, vsi_handle, &local_cfg);
	} else {
		local_cfg.hdr_type = ICE_RSS_OUTER_HEADERS;
		status = ice_add_rss_cfg_sync(hw, vsi_handle, &local_cfg);
		if (!status) {
			local_cfg.hdr_type = ICE_RSS_INNER_HEADERS;
			status = ice_add_rss_cfg_sync(hw, vsi_handle,
						      &local_cfg);
		}
	}
#if 0
	ice_release_lock(&hw->rss_locks);
#endif
	return status;
}
/**
 * ice_set_rss_flow_flds - Program the RSS hash flows after package init
 * @vsi: the VSI to configure
 *
 * If the package file is initialized, the default RSS flows are reset. We
 * need to reprogram the expected hash configuration.
 */
void
ice_set_rss_flow_flds(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	struct ice_rss_hash_cfg rss_cfg = { 0, 0, ICE_RSS_ANY_HEADERS, false };
	enum ice_status status;

	rss_cfg.addl_hdrs = ICE_FLOW_SEG_HDR_IPV4;
	rss_cfg.hash_flds = ICE_FLOW_HASH_IPV4;
	status = ice_add_rss_cfg(hw, vsi->idx, &rss_cfg);
	if (status) {
		DPRINTF("%s: ice_add_rss_cfg on VSI %d failed for ipv4 flow, "
		    "err %s aq_err %s\n", __func__, vsi->idx,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}

	rss_cfg.addl_hdrs = ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_TCP;
	rss_cfg.hash_flds = ICE_HASH_TCP_IPV4;
	status = ice_add_rss_cfg(hw, vsi->idx, &rss_cfg);
	if (status) {
		DPRINTF("ice_add_rss_cfg on VSI %d failed for tcp4 flow, "
		    "err %s aq_err %s\n", vsi->idx, ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}

	rss_cfg.addl_hdrs = ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_UDP;
	rss_cfg.hash_flds = ICE_HASH_UDP_IPV4;
	status = ice_add_rss_cfg(hw, vsi->idx, &rss_cfg);
	if (status) {
		DPRINTF("%s: ice_add_rss_cfg on VSI %d failed for udp4 flow, "
		    "err %s aq_err %s\n", __func__, vsi->idx,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}

	rss_cfg.addl_hdrs = ICE_FLOW_SEG_HDR_IPV6;
	rss_cfg.hash_flds = ICE_FLOW_HASH_IPV6;
	status = ice_add_rss_cfg(hw, vsi->idx, &rss_cfg);
	if (status) {
		DPRINTF("%s: ice_add_rss_cfg on VSI %d failed for ipv6 flow, "
		    "err %s aq_err %s\n", __func__, vsi->idx,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}

	rss_cfg.addl_hdrs = ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_TCP;
	rss_cfg.hash_flds = ICE_HASH_TCP_IPV6;
	status = ice_add_rss_cfg(hw, vsi->idx, &rss_cfg);
	if (status) {
		DPRINTF("%s: ice_add_rss_cfg on VSI %d failed for tcp6 flow, "
		    "err %s aq_err %s\n", __func__, vsi->idx,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}

	rss_cfg.addl_hdrs = ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_UDP;
	rss_cfg.hash_flds = ICE_HASH_UDP_IPV6;
	status = ice_add_rss_cfg(hw, vsi->idx, &rss_cfg);
	if (status) {
		DPRINTF("%s: ice_add_rss_cfg on VSI %d failed for udp6 flow, "
		    "err %s aq_err %s\n", __func__, vsi->idx,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}
}

uint16_t
ice_lut_type_to_size(uint16_t lut_type)
{
	switch (lut_type) {
	case ICE_LUT_VSI:
		return ICE_LUT_VSI_SIZE;
	case ICE_LUT_GLOBAL:
		return ICE_LUT_GLOBAL_SIZE;
	case ICE_LUT_PF:
		return ICE_LUT_PF_SIZE;
	case ICE_LUT_PF_SMALL:
		return ICE_LUT_PF_SMALL_SIZE;
	default:
		return 0;
	}
}

uint16_t
ice_lut_size_to_flag(uint16_t lut_size)
{
	uint16_t f = 0;

	switch (lut_size) {
	case ICE_LUT_GLOBAL_SIZE:
		f = ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_512_FLAG;
		break;
	case ICE_LUT_PF_SIZE:
		f = ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_2K_FLAG;
		break;
	default:
		break;
	}
	return f << ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_S;
}

/**
 * ice_aq_get_set_rss_lut
 * @hw: pointer to the hardware structure
 * @params: RSS LUT parameters
 * @set: set true to set the table, false to get the table
 *
 * Internal function to get (0x0B05) or set (0x0B03) RSS look up table
 */
enum ice_status
ice_aq_get_set_rss_lut(struct ice_hw *hw,
    struct ice_aq_get_set_rss_lut_params *params, bool set)
{
	uint16_t flags, vsi_id, lut_type, lut_size, glob_lut_idx = 0;
	uint16_t vsi_handle;
	struct ice_aqc_get_set_rss_lut *cmd_resp;
	struct ice_aq_desc desc;
	enum ice_status status;
	uint8_t *lut;

	if (!params)
		return ICE_ERR_PARAM;

	vsi_handle = params->vsi_handle;
	lut = params->lut;
	lut_size = ice_lut_type_to_size(params->lut_type);
	lut_type = params->lut_type & ICE_LUT_TYPE_MASK;
	cmd_resp = &desc.params.get_set_rss_lut;
	if (lut_type == ICE_LUT_GLOBAL)
		glob_lut_idx = params->global_lut_id;

	if (!lut || !lut_size || !ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;

	if (lut_size > params->lut_size)
		return ICE_ERR_INVAL_SIZE;

	if (set && lut_size != params->lut_size)
		return ICE_ERR_PARAM;

	vsi_id = hw->vsi_ctx[vsi_handle]->vsi_num;

	if (set) {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_rss_lut);
		desc.flags |= htole16(ICE_AQ_FLAG_RD);
	} else {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_rss_lut);
	}

	cmd_resp->vsi_id = htole16(((vsi_id <<
					 ICE_AQC_GSET_RSS_LUT_VSI_ID_S) &
					ICE_AQC_GSET_RSS_LUT_VSI_ID_M) |
				       ICE_AQC_GSET_RSS_LUT_VSI_VALID);

	flags = ice_lut_size_to_flag(lut_size) |
		 ((lut_type << ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_S) &
		  ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_M) |
		 ((glob_lut_idx << ICE_AQC_GSET_RSS_LUT_GLOBAL_IDX_S) &
		  ICE_AQC_GSET_RSS_LUT_GLOBAL_IDX_M);

	cmd_resp->flags = htole16(flags);
	status = ice_aq_send_cmd(hw, &desc, lut, lut_size, NULL);
	params->lut_size = le16toh(desc.datalen);
	return status;
}

/**
 * ice_aq_set_rss_lut
 * @hw: pointer to the hardware structure
 * @set_params: RSS LUT parameters used to specify how to set the RSS LUT
 *
 * set the RSS lookup table, PF or VSI type
 */
enum ice_status
ice_aq_set_rss_lut(struct ice_hw *hw,
    struct ice_aq_get_set_rss_lut_params *set_params)
{
	return ice_aq_get_set_rss_lut(hw, set_params, true);
}

/**
 * ice_set_rss_lut - Program the RSS lookup table for a VSI
 * @vsi: the VSI to configure
 *
 * Programs the RSS lookup table for a given VSI.
 */
int
ice_set_rss_lut(struct ice_vsi *vsi)
{
	struct ice_softc *sc = vsi->sc;
	struct ice_hw *hw = &sc->hw;
	struct ice_aq_get_set_rss_lut_params lut_params;
	enum ice_status status;
	int i, err = 0;
	uint8_t *lut;

	lut = (uint8_t *)malloc(vsi->rss_table_size, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (!lut) {
		DPRINTF("%s: Failed to allocate RSS lut memory\n", __func__);
		return (ENOMEM);
	}

	/*
	 * Populate the LUT with max no. of queues. This will assign the
	 * lookup table in a simple round robin fashion.
	 */
	for (i = 0; i < vsi->rss_table_size; i++) {
		/* XXX: this needs to be changed if num_rx_queues ever counts
		 * more than just the RSS queues */
		lut[i] = i % vsi->num_rx_queues;
	}

	lut_params.vsi_handle = vsi->idx;
	lut_params.lut_size = vsi->rss_table_size;
	lut_params.lut_type = vsi->rss_lut_type;
	lut_params.lut = lut;
	lut_params.global_lut_id = 0;
	status = ice_aq_set_rss_lut(hw, &lut_params);
	if (status) {
		DPRINTF("%s: Cannot set RSS lut, err %s aq_err %s\n", __func__,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		err = EIO;
	}

	free(lut, M_DEVBUF, vsi->rss_table_size);
	return err;
}

/**
 * ice_config_rss - Configure RSS for a VSI
 * @vsi: the VSI to configure
 *
 * If FEATURE_RSS is enabled, configures the RSS lookup table and hash key for
 * a given VSI.
 */
int
ice_config_rss(struct ice_vsi *vsi)
{
	int err;

	/* Nothing to do, if RSS is not enabled */
	if (!ice_is_bit_set(vsi->sc->feat_en, ICE_FEATURE_RSS))
		return 0;

	err = ice_set_rss_key(vsi);
	if (err)
		return err;

	ice_set_rss_flow_flds(vsi);

	return ice_set_rss_lut(vsi);
}

/**
 * ice_transition_safe_mode - Transition to safe mode
 * @sc: the device private softc
 *
 * Called when the driver attempts to reload the DDP package during a device
 * reset, and the new download fails. If so, we must transition to safe mode
 * at run time.
 *
 * @remark although safe mode normally allocates only a single queue, we can't
 * change the number of queues dynamically when using iflib. Due to this, we
 * do not attempt to reduce the number of queues.
 */
void
ice_transition_safe_mode(struct ice_softc *sc)
{
	/* Indicate that we are in Safe mode */
	ice_set_bit(ICE_FEATURE_SAFE_MODE, sc->feat_cap);
	ice_set_bit(ICE_FEATURE_SAFE_MODE, sc->feat_en);
#if 0
	ice_rdma_pf_detach(sc);
#endif
	ice_clear_bit(ICE_FEATURE_RDMA, sc->feat_cap);

	ice_clear_bit(ICE_FEATURE_SRIOV, sc->feat_en);
	ice_clear_bit(ICE_FEATURE_SRIOV, sc->feat_cap);

	ice_clear_bit(ICE_FEATURE_RSS, sc->feat_cap);
	ice_clear_bit(ICE_FEATURE_RSS, sc->feat_en);
}

/**
 * ice_rebuild - Rebuild driver state post reset
 * @sc: The device private softc
 *
 * Restore driver state after a reset occurred. Restart the controlqs, setup
 * the hardware port, and re-enable the VSIs.
 */
void
ice_rebuild(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	int err;

	sc->rebuild_ticks = ticks;

	/* If we're rebuilding, then a reset has succeeded. */
	ice_clear_state(&sc->state, ICE_STATE_RESET_FAILED);

	/*
	 * If the firmware is in recovery mode, only restore the limited
	 * functionality supported by recovery mode.
	 */
	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE)) {
		ice_rebuild_recovery_mode(sc);
		return;
	}
#if 0
	/* enable PCIe bus master */
	pci_enable_busmaster(dev);
#endif
	status = ice_init_all_ctrlq(hw);
	if (status) {
		printf("%s: failed to re-init control queues, err %s\n",
		    sc->sc_dev.dv_xname, ice_status_str(status));
		goto err_shutdown_ctrlq;
	}

	/* Query the allocated resources for Tx scheduler */
	status = ice_sched_query_res_alloc(hw);
	if (status) {
		printf("%s: Failed to query scheduler resources, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		goto err_shutdown_ctrlq;
	}

	/* Re-enable FW logging. Keep going even if this fails */
	status = ice_fwlog_set(hw, &hw->fwlog_cfg);
	if (!status) {
		/*
		 * We should have the most updated cached copy of the
		 * configuration, regardless of whether we're rebuilding
		 * or not.  So we'll simply check to see if logging was
		 * enabled pre-rebuild.
		 */
		if (hw->fwlog_cfg.options & ICE_FWLOG_OPTION_IS_REGISTERED) {
			status = ice_fwlog_register(hw);
			if (status)
				printf("%s: failed to re-register fw logging, "
				    "err %s aq_err %s\n",
				   sc->sc_dev.dv_xname,
				   ice_status_str(status),
				   ice_aq_str(hw->adminq.sq_last_status));
		}
	} else {
		printf("%s: failed to rebuild fw logging configuration, "
		    "err %s aq_err %s\n", sc->sc_dev.dv_xname,
		   ice_status_str(status),
		   ice_aq_str(hw->adminq.sq_last_status));
	}
	err = ice_send_version(sc);
	if (err)
		goto err_shutdown_ctrlq;

	err = ice_init_link_events(sc);
	if (err) {
		printf("%s: ice_init_link_events failed: %d\n",
		    sc->sc_dev.dv_xname, err);
		goto err_shutdown_ctrlq;
	}

	status = ice_clear_pf_cfg(hw);
	if (status) {
		printf("%s: failed to clear PF configuration, err %s\n",
		    sc->sc_dev.dv_xname, ice_status_str(status));
		goto err_shutdown_ctrlq;
	}

	ice_clean_all_vsi_rss_cfg(sc);

	ice_clear_pxe_mode(hw);

	status = ice_get_caps(hw);
	if (status) {
		printf("%s: failed to get capabilities, err %s\n",
		    sc->sc_dev.dv_xname, ice_status_str(status));
		goto err_shutdown_ctrlq;
	}

	status = ice_sched_init_port(hw->port_info);
	if (status) {
		printf("%s: failed to initialize port, err %s\n",
		     sc->sc_dev.dv_xname, ice_status_str(status));
		goto err_sched_cleanup;
	}

	/* If we previously loaded the package, it needs to be reloaded now */
	if (!ice_is_bit_set(sc->feat_en, ICE_FEATURE_SAFE_MODE)) {
		enum ice_ddp_state pkg_state;

		pkg_state = ice_init_pkg(hw, hw->pkg_copy, hw->pkg_size);
		if (!ice_is_init_pkg_successful(pkg_state)) {
			ice_log_pkg_init(sc, pkg_state);
			ice_transition_safe_mode(sc);
		}
	}

	ice_reset_pf_stats(sc);

	err = ice_rebuild_pf_vsi_qmap(sc);
	if (err) {
		printf("%s: Unable to re-assign main VSI queues, err %d\n",
		    sc->sc_dev.dv_xname, err);
		goto err_sched_cleanup;
	}
	err = ice_initialize_vsi(&sc->pf_vsi);
	if (err) {
		printf("%s: Unable to re-initialize Main VSI, err %d\n",
		    sc->sc_dev.dv_xname, err);
		goto err_release_queue_allocations;
	}

	/* Replay all VSI configuration */
	err = ice_replay_all_vsi_cfg(sc);
	if (err)
		goto err_deinit_pf_vsi;

	/* Re-enable FW health event reporting */
	ice_init_health_events(sc);

	/* Reconfigure the main PF VSI for RSS */
	err = ice_config_rss(&sc->pf_vsi);
	if (err) {
		printf("%s: Unable to reconfigure RSS for the main VSI, "
		    "err %d\n", sc->sc_dev.dv_xname, err);
		goto err_deinit_pf_vsi;
	}

	if (hw->port_info->qos_cfg.is_sw_lldp)
		ice_add_rx_lldp_filter(sc);

	/* Refresh link status */
	ice_clear_state(&sc->state, ICE_STATE_LINK_STATUS_REPORTED);
	sc->hw.port_info->phy.get_link_info = true;
	ice_get_link_status(sc->hw.port_info, &sc->link_up);
	ice_update_link_status(sc, true);

	/* RDMA interface will be restarted by the stack re-init */

	/* Configure interrupt causes for the administrative interrupt */
	ice_configure_misc_interrupts(sc);

	/* Enable ITR 0 right away, so that we can handle admin interrupts */
	ice_enable_intr(&sc->hw, 0);

	/* Rebuild is finished. We're no longer prepared to reset */
	ice_clear_state(&sc->state, ICE_STATE_PREPARED_FOR_RESET);
#if 0
	/* Reconfigure the subinterface */
	if (sc->mirr_if) {
		err = ice_subif_rebuild(sc);
		if (err)
			goto err_deinit_pf_vsi;
	}
#endif
	printf("%s: device rebuild successful\n", sc->sc_dev.dv_xname);

	/* In order to completely restore device functionality, the iflib core
	 * needs to be reset. We need to request an iflib reset. Additionally,
	 * because the state of IFC_DO_RESET is cached within task_fn_admin in
	 * the iflib core, we also want re-run the admin task so that iflib
	 * resets immediately instead of waiting for the next interrupt.
	 * If LLDP is enabled we need to reconfig DCB to properly reinit all TC
	 * queues, not only 0. It contains ice_request_stack_reinit as well.
	 */
#if 0
	if (hw->port_info->qos_cfg.is_sw_lldp)
		ice_request_stack_reinit(sc);
	else
		ice_do_dcb_reconfig(sc, false);
#endif
	return;

err_deinit_pf_vsi:
	ice_deinit_vsi(&sc->pf_vsi);
err_release_queue_allocations:
	ice_resmgr_release_map(&sc->tx_qmgr, sc->pf_vsi.tx_qmap,
				    sc->pf_vsi.num_tx_queues);
	ice_resmgr_release_map(&sc->rx_qmgr, sc->pf_vsi.rx_qmap,
				    sc->pf_vsi.num_rx_queues);
err_sched_cleanup:
	ice_sched_cleanup_all(hw);
err_shutdown_ctrlq:
	ice_shutdown_all_ctrlq(hw, false);
	ice_clear_state(&sc->state, ICE_STATE_PREPARED_FOR_RESET);
	ice_set_state(&sc->state, ICE_STATE_RESET_FAILED);
	printf("%s: driver reset failed\n", sc->sc_dev.dv_xname);
}

/**
 * ice_handle_reset_event - Handle reset events triggered by OICR
 * @sc: The device private softc
 *
 * Handle reset events triggered by an OICR notification. This includes CORER,
 * GLOBR, and EMPR resets triggered by software on this or any other PF or by
 * firmware.
 *
 * @pre assumes the iflib context lock is held, and will unlock it while
 * waiting for the hardware to finish reset.
 */
void
ice_handle_reset_event(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	/* When a CORER, GLOBR, or EMPR is about to happen, the hardware will
	 * trigger an OICR interrupt. Our OICR handler will determine when
	 * this occurs and set the ICE_STATE_RESET_OICR_RECV bit as
	 * appropriate.
	 */
	if (!ice_testandclear_state(&sc->state, ICE_STATE_RESET_OICR_RECV))
		return;

	ice_prepare_for_reset(sc);

	/*
	 * Release the iflib context lock and wait for the device to finish
	 * resetting.
	 */
#if 0
	IFLIB_CTX_UNLOCK(sc);
#endif
	status = ice_check_reset(hw);
#if 0
	IFLIB_CTX_LOCK(sc);
#endif
	if (status) {
		printf("%s: Device never came out of reset, err %s\n",
		    sc->sc_dev.dv_xname, ice_status_str(status));
		ice_set_state(&sc->state, ICE_STATE_RESET_FAILED);
		return;
	}

	/* We're done with the reset, so we can rebuild driver state */
	sc->hw.reset_ongoing = false;
	ice_rebuild(sc);

	/* In the unlikely event that a PF reset request occurs at the same
	 * time as a global reset, clear the request now. This avoids
	 * resetting a second time right after we reset due to a global event.
	 */
	if (ice_testandclear_state(&sc->state, ICE_STATE_RESET_PFR_REQ))
		printf("%s: Ignoring PFR request that occurred while a "
		    "reset was ongoing\n", sc->sc_dev.dv_xname);
}

/**
 * ice_handle_pf_reset_request - Initiate PF reset requested by software
 * @sc: The device private softc
 *
 * Initiate a PF reset requested by software. We handle this in the admin task
 * so that only one thread actually handles driver preparation and cleanup,
 * rather than having multiple threads possibly attempt to run this code
 * simultaneously.
 *
 * @pre assumes the iflib context lock is held and will unlock it while
 * waiting for the PF reset to complete.
 */
void
ice_handle_pf_reset_request(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	/* Check for PF reset requests */
	if (!ice_testandclear_state(&sc->state, ICE_STATE_RESET_PFR_REQ))
		return;

	/* Make sure we're prepared for reset */
	ice_prepare_for_reset(sc);

	/*
	 * Release the iflib context lock and wait for the device to finish
	 * resetting.
	 */
#if 0
	IFLIB_CTX_UNLOCK(sc);
#endif
	status = ice_reset(hw, ICE_RESET_PFR);
#if 0
	IFLIB_CTX_LOCK(sc);
#endif
	if (status) {
		printf("%s: device PF reset failed, err %s\n",
		    sc->sc_dev.dv_xname, ice_status_str(status));
		ice_set_state(&sc->state, ICE_STATE_RESET_FAILED);
		return;
	}
#if 0
	sc->soft_stats.pfr_count++;
#endif
	ice_rebuild(sc);
}

/**
 * ice_handle_mdd_event - Handle possibly malicious events
 * @sc: the device softc
 *
 * Called by the admin task if an MDD detection interrupt is triggered.
 * Identifies possibly malicious events coming from VFs. Also triggers for
 * similar incorrect behavior from the PF as well.
 */
void
ice_handle_mdd_event(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	bool mdd_detected = false, request_reinit = false;
	uint32_t reg;

	if (!ice_testandclear_state(&sc->state, ICE_STATE_MDD_PENDING))
		return;

	reg = ICE_READ(hw, GL_MDET_TX_TCLAN);
	if (reg & GL_MDET_TX_TCLAN_VALID_M) {
		uint8_t pf_num  = (reg & GL_MDET_TX_TCLAN_PF_NUM_M) >>
		    GL_MDET_TX_TCLAN_PF_NUM_S;
		uint16_t vf_num = (reg & GL_MDET_TX_TCLAN_VF_NUM_M) >>
		    GL_MDET_TX_TCLAN_VF_NUM_S;
		uint8_t event   = (reg & GL_MDET_TX_TCLAN_MAL_TYPE_M) >>
		    GL_MDET_TX_TCLAN_MAL_TYPE_S;
		uint16_t queue  = (reg & GL_MDET_TX_TCLAN_QNUM_M) >>
		    GL_MDET_TX_TCLAN_QNUM_S;

		printf("%s: Malicious Driver Detection Tx Descriptor "
		    "check event '%s' on Tx queue %u PF# %u VF# %u\n",
		    sc->sc_dev.dv_xname, ice_mdd_tx_tclan_str(event),
		    queue, pf_num, vf_num);

		/* Only clear this event if it matches this PF, that way other
		 * PFs can read the event and determine VF and queue number.
		 */
		if (pf_num == hw->pf_id)
			ICE_WRITE(hw, GL_MDET_TX_TCLAN, 0xffffffff);

		mdd_detected = true;
	}

	/* Determine what triggered the MDD event */
	reg = ICE_READ(hw, GL_MDET_TX_PQM);
	if (reg & GL_MDET_TX_PQM_VALID_M) {
		uint8_t pf_num  = (reg & GL_MDET_TX_PQM_PF_NUM_M) >>
		    GL_MDET_TX_PQM_PF_NUM_S;
		uint16_t vf_num = (reg & GL_MDET_TX_PQM_VF_NUM_M) >>
		    GL_MDET_TX_PQM_VF_NUM_S;
		uint8_t event   = (reg & GL_MDET_TX_PQM_MAL_TYPE_M) >>
		    GL_MDET_TX_PQM_MAL_TYPE_S;
		uint16_t queue  = (reg & GL_MDET_TX_PQM_QNUM_M) >>
		    GL_MDET_TX_PQM_QNUM_S;

		printf("%s: Malicious Driver Detection Tx Quanta check "
		    "event '%s' on Tx queue %u PF# %u VF# %u\n",
		    sc->sc_dev.dv_xname, ice_mdd_tx_pqm_str(event), queue,
		    pf_num, vf_num);

		/* Only clear this event if it matches this PF, that way other
		 * PFs can read the event and determine VF and queue number.
		 */
		if (pf_num == hw->pf_id)
			ICE_WRITE(hw, GL_MDET_TX_PQM, 0xffffffff);

		mdd_detected = true;
	}

	reg = ICE_READ(hw, GL_MDET_RX);
	if (reg & GL_MDET_RX_VALID_M) {
		uint8_t pf_num  = (reg & GL_MDET_RX_PF_NUM_M) >>
		    GL_MDET_RX_PF_NUM_S;
		uint16_t vf_num = (reg & GL_MDET_RX_VF_NUM_M) >>
		    GL_MDET_RX_VF_NUM_S;
		uint8_t event   = (reg & GL_MDET_RX_MAL_TYPE_M) >>
		    GL_MDET_RX_MAL_TYPE_S;
		uint16_t queue  = (reg & GL_MDET_RX_QNUM_M) >>
		    GL_MDET_RX_QNUM_S;

		printf("%s: Malicious Driver Detection Rx event '%s' "
		    "on Rx queue %u PF# %u VF# %u\n", sc->sc_dev.dv_xname,
		    ice_mdd_rx_str(event), queue, pf_num, vf_num);

		/* Only clear this event if it matches this PF, that way other
		 * PFs can read the event and determine VF and queue number.
		 */
		if (pf_num == hw->pf_id)
			ICE_WRITE(hw, GL_MDET_RX, 0xffffffff);

		mdd_detected = true;
	}

	/* Now, confirm that this event actually affects this PF, by checking
	 * the PF registers.
	 */
	if (mdd_detected) {
		reg = ICE_READ(hw, PF_MDET_TX_TCLAN);
		if (reg & PF_MDET_TX_TCLAN_VALID_M) {
			ICE_WRITE(hw, PF_MDET_TX_TCLAN, 0xffff);
#if 0
			sc->soft_stats.tx_mdd_count++;
#endif
			request_reinit = true;
		}

		reg = ICE_READ(hw, PF_MDET_TX_PQM);
		if (reg & PF_MDET_TX_PQM_VALID_M) {
			ICE_WRITE(hw, PF_MDET_TX_PQM, 0xffff);
#if 0
			sc->soft_stats.tx_mdd_count++;
#endif
			request_reinit = true;
		}

		reg = ICE_READ(hw, PF_MDET_RX);
		if (reg & PF_MDET_RX_VALID_M) {
			ICE_WRITE(hw, PF_MDET_RX, 0xffff);
#if 0
			sc->soft_stats.rx_mdd_count++;
#endif
			request_reinit = true;
		}
	}

	/* TODO: Implement logic to detect and handle events caused by VFs. */
	/* request that the upper stack re-initialize the Tx/Rx queues */
	if (request_reinit)
		ice_request_stack_reinit(sc);
	ice_flush(hw);
}

/**
 * ice_check_ctrlq_errors - Check for and report controlq errors
 * @sc: device private structure
 * @qname: name of the controlq
 * @cq: the controlq to check
 *
 * Check and report controlq errors. Currently all we do is report them to the
 * kernel message log, but we might want to improve this in the future, such
 * as to keep track of statistics.
 */
void
ice_check_ctrlq_errors(struct ice_softc *sc, const char *qname,
		       struct ice_ctl_q_info *cq)
{
	struct ice_hw *hw = &sc->hw;
	uint32_t val;

	/* Check for error indications. Note that all the controlqs use the
	 * same register layout, so we use the PF_FW_AxQLEN defines only.
	 */
	val = ICE_READ(hw, cq->rq.len);
	if (val & (PF_FW_ARQLEN_ARQVFE_M | PF_FW_ARQLEN_ARQOVFL_M |
		   PF_FW_ARQLEN_ARQCRIT_M)) {
		if (val & PF_FW_ARQLEN_ARQVFE_M)
			printf("%s: %s Receive Queue VF Error detected\n",
			    sc->sc_dev.dv_xname, qname);
		if (val & PF_FW_ARQLEN_ARQOVFL_M)
			printf("%s: %s Receive Queue Overflow Error detected\n",
			    sc->sc_dev.dv_xname, qname);
		if (val & PF_FW_ARQLEN_ARQCRIT_M)
			printf("%s: %s Receive Queue Critical Error detected\n",
			    sc->sc_dev.dv_xname, qname);
		val &= ~(PF_FW_ARQLEN_ARQVFE_M | PF_FW_ARQLEN_ARQOVFL_M |
			 PF_FW_ARQLEN_ARQCRIT_M);
		ICE_WRITE(hw, cq->rq.len, val);
	}

	val = ICE_READ(hw, cq->sq.len);
	if (val & (PF_FW_ATQLEN_ATQVFE_M | PF_FW_ATQLEN_ATQOVFL_M |
		   PF_FW_ATQLEN_ATQCRIT_M)) {
		if (val & PF_FW_ATQLEN_ATQVFE_M)
			printf("%s: %s Send Queue VF Error detected\n",
			    sc->sc_dev.dv_xname, qname);
		if (val & PF_FW_ATQLEN_ATQOVFL_M)
			printf("%s: %s Send Queue Overflow Error detected\n",
			    sc->sc_dev.dv_xname, qname);
		if (val & PF_FW_ATQLEN_ATQCRIT_M)
			printf("%s: %s Send Queue Critical Error detected\n",
			    sc->sc_dev.dv_xname, qname);
		val &= ~(PF_FW_ATQLEN_ATQVFE_M | PF_FW_ATQLEN_ATQOVFL_M |
			 PF_FW_ATQLEN_ATQCRIT_M);
		ICE_WRITE(hw, cq->sq.len, val);
	}
}

/**
 * ice_process_link_event - Process a link event indication from firmware
 * @sc: device softc structure
 * @e: the received event data
 *
 * Gets the current link status from hardware, and may print a message if an
 * unqualified is detected.
 */
void
ice_process_link_event(struct ice_softc *sc, struct ice_rq_event_info *e)
{
	struct ice_port_info *pi = sc->hw.port_info;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	/* Sanity check that the data length isn't too small */
	KASSERT(le16toh(e->desc.datalen) >= ICE_GET_LINK_STATUS_DATALEN_V1);

	/*
	 * Even though the adapter gets link status information inside the
	 * event, it needs to send a Get Link Status AQ command in order
	 * to re-enable link events.
	 */
	pi->phy.get_link_info = true;
	ice_get_link_status(pi, &sc->link_up);

	if (pi->phy.link_info.topo_media_conflict &
	   (ICE_AQ_LINK_TOPO_CONFLICT | ICE_AQ_LINK_MEDIA_CONFLICT |
	    ICE_AQ_LINK_TOPO_CORRUPT))
		printf("%s: Possible mis-configuration of the Ethernet port "
		    "detected: topology conflict, or link media conflict, "
		    "or link topology corrupt\n", sc->sc_dev.dv_xname);

	if ((pi->phy.link_info.link_info & ICE_AQ_MEDIA_AVAILABLE) &&
	    !(pi->phy.link_info.link_info & ICE_AQ_LINK_UP)) {
		if (!(pi->phy.link_info.an_info & ICE_AQ_QUALIFIED_MODULE))
			printf("%s: Link is disabled on this device because "
			    "an unsupported module type was detected!",
			    sc->sc_dev.dv_xname);

		if (pi->phy.link_info.link_cfg_err &
		    ICE_AQ_LINK_MODULE_POWER_UNSUPPORTED)
			printf("%s: The module's power requirements exceed "
			    "the device's power supply. Cannot start link.\n",
			    sc->sc_dev.dv_xname);
		if (pi->phy.link_info.link_cfg_err &
		    ICE_AQ_LINK_INVAL_MAX_POWER_LIMIT)
			printf("%s: The installed module is incompatible with "
			    "the device's NVM image. Cannot start link.\n",
			    sc->sc_dev.dv_xname);
	}

	if (!(pi->phy.link_info.link_info & ICE_AQ_MEDIA_AVAILABLE)) {
		if (!ice_testandset_state(&sc->state, ICE_STATE_NO_MEDIA)) {
			status = ice_aq_set_link_restart_an(pi, false, NULL);
			if (status != ICE_SUCCESS &&
			    hw->adminq.sq_last_status != ICE_AQ_RC_EMODE)
				DPRINTF("%s: ice_aq_set_link_restart_an: "
				    "status %s, aq_err %s\n", __func__,
				    ice_status_str(status),
				    ice_aq_str(hw->adminq.sq_last_status));
		}
	}
	/* ICE_STATE_NO_MEDIA is cleared when polling task detects media */

	/* Indicate that link status must be reported again */
	ice_clear_state(&sc->state, ICE_STATE_LINK_STATUS_REPORTED);

	/* OS link info is updated elsewhere */
}

/**
 * ice_info_fwlog - Format and print an array of values to the console
 * @hw: private hardware structure
 * @rowsize: preferred number of rows to use
 * @groupsize: preferred size in bytes to print each chunk
 * @buf: the array buffer to print
 * @len: size of the array buffer
 *
 * Format the given array as a series of uint8_t values with hexadecimal
 * notation and log the contents to the console log.  This variation is
 * specific to firmware logging.
 *
 * TODO: Currently only supports a group size of 1, due to the way hexdump is
 * implemented.
 */
void
ice_info_fwlog(struct ice_hw *hw, uint32_t rowsize, uint32_t __unused groupsize,
	       uint8_t *buf, size_t len)
{
	struct ice_softc *sc = hw->hw_sc;

	if (!ice_fwlog_supported(hw))
		return;

	/* Format the device header to a string */
	printf("%s: FWLOG: ", sc->sc_dev.dv_xname);

	ice_hexdump(buf, len);
}

/**
 * ice_fwlog_event_dump - Dump the event received over the Admin Receive Queue
 * @hw: pointer to the HW structure
 * @desc: Admin Receive Queue descriptor
 * @buf: buffer that contains the FW log event data
 *
 * If the driver receives the ice_aqc_opc_fw_logs_event on the Admin Receive
 * Queue, then it should call this function to dump the FW log data.
 */
void
ice_fwlog_event_dump(struct ice_hw *hw, struct ice_aq_desc *desc, void *buf)
{
	if (!ice_fwlog_supported(hw))
		return;

	ice_info_fwlog(hw, 32, 1, (uint8_t *)buf, le16toh(desc->datalen));
}

/**
 * ice_handle_fw_log_event - Handle a firmware logging event from the AdminQ
 * @sc: pointer to private softc structure
 * @desc: the AdminQ descriptor for this firmware event
 * @buf: pointer to the buffer accompanying the AQ message
 */
void
ice_handle_fw_log_event(struct ice_softc *sc, struct ice_aq_desc *desc,
			void *buf)
{
#if 0
	/* Trigger a DTrace probe event for this firmware message */
	SDT_PROBE2(ice_fwlog, , , message, (const u8 *)buf, desc->datalen);
#endif
	/* Possibly dump the firmware message to the console, if enabled */
	ice_fwlog_event_dump(&sc->hw, desc, buf);
}

/**
 * ice_debug_print_mib_change_event - helper function to log LLDP MIB change events
 * @sc: the device private softc
 * @event: event received on a control queue
 *
 * Prints out the type and contents of an LLDP MIB change event in a DCB debug message.
 */
void
ice_debug_print_mib_change_event(struct ice_softc *sc,
    struct ice_rq_event_info *event)
{
#ifdef ICE_DEBUG
	struct ice_aqc_lldp_get_mib *params =
	    (struct ice_aqc_lldp_get_mib *)&event->desc.params.lldp_get_mib;
	uint8_t mib_type, bridge_type, tx_status;

	static const char* mib_type_strings[] = {
	    "Local MIB",
	    "Remote MIB",
	    "Reserved",
	    "Reserved"
	};
	static const char* bridge_type_strings[] = {
	    "Nearest Bridge",
	    "Non-TPMR Bridge",
	    "Reserved",
	    "Reserved"
	};
	static const char* tx_status_strings[] = {
	    "Port's TX active",
	    "Port's TX suspended and drained",
	    "Reserved",
	    "Port's TX suspended and drained; blocked TC pipe flushed"
	};

	mib_type = (params->type & ICE_AQ_LLDP_MIB_TYPE_M) >>
	    ICE_AQ_LLDP_MIB_TYPE_S;
	bridge_type = (params->type & ICE_AQ_LLDP_BRID_TYPE_M) >>
	    ICE_AQ_LLDP_BRID_TYPE_S;
	tx_status = (params->type & ICE_AQ_LLDP_TX_M) >>
	    ICE_AQ_LLDP_TX_S;

	DNPRINTF(ICE_DBG_DCB, "%s: LLDP MIB Change Event (%s, %s, %s)\n",
	    sc->sc_dev.dv_xname,
	    mib_type_strings[mib_type], bridge_type_strings[bridge_type],
	    tx_status_strings[tx_status]);

	/* Nothing else to report */
	if (!event->msg_buf)
		return;

	DNPRINTF(ICE_DBG_DCB, "- %s contents:\n", mib_type_strings[mib_type]);
	ice_debug_array(&sc->hw, ICE_DBG_DCB, 16, 1, event->msg_buf,
			event->msg_len);
#endif
}

/**
 * ice_aq_get_lldp_mib
 * @hw: pointer to the HW struct
 * @bridge_type: type of bridge requested
 * @mib_type: Local, Remote or both Local and Remote MIBs
 * @buf: pointer to the caller-supplied buffer to store the MIB block
 * @buf_size: size of the buffer (in bytes)
 * @local_len: length of the returned Local LLDP MIB
 * @remote_len: length of the returned Remote LLDP MIB
 * @cd: pointer to command details structure or NULL
 *
 * Requests the complete LLDP MIB (entire packet). (0x0A00)
 */
enum ice_status
ice_aq_get_lldp_mib(struct ice_hw *hw, uint8_t bridge_type, uint8_t mib_type,
    void *buf, uint16_t buf_size, uint16_t *local_len, uint16_t *remote_len,
    struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_get_mib *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.lldp_get_mib;

	if (buf_size == 0 || !buf)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_lldp_get_mib);

	cmd->type = mib_type & ICE_AQ_LLDP_MIB_TYPE_M;
	cmd->type |= (bridge_type << ICE_AQ_LLDP_BRID_TYPE_S) &
		ICE_AQ_LLDP_BRID_TYPE_M;

	desc.datalen = htole16(buf_size);

	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status) {
		if (local_len)
			*local_len = le16toh(cmd->local_len);
		if (remote_len)
			*remote_len = le16toh(cmd->remote_len);
	}

	return status;
}

/**
 * ice_parse_ieee_ets_common_tlv
 * @buf: Data buffer to be parsed for ETS CFG/REC data
 * @ets_cfg: Container to store parsed data
 *
 * Parses the common data of IEEE 802.1Qaz ETS CFG/REC TLV
 */
void
ice_parse_ieee_ets_common_tlv(uint8_t *buf, struct ice_dcb_ets_cfg *ets_cfg)
{
	uint8_t offset = 0;
	int i;

	/* Priority Assignment Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		ets_cfg->prio_table[i * 2] =
			((buf[offset] & ICE_IEEE_ETS_PRIO_1_M) >>
			 ICE_IEEE_ETS_PRIO_1_S);
		ets_cfg->prio_table[i * 2 + 1] =
			((buf[offset] & ICE_IEEE_ETS_PRIO_0_M) >>
			 ICE_IEEE_ETS_PRIO_0_S);
		offset++;
	}

	/* TC Bandwidth Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 *
	 * TSA Assignment Table (8 octets)
	 * Octets:| 9 | 10| 11| 12| 13| 14| 15| 16|
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	ice_for_each_traffic_class(i) {
		ets_cfg->tcbwtable[i] = buf[offset];
		ets_cfg->tsatable[i] = buf[ICE_MAX_TRAFFIC_CLASS + offset++];
	}
}

/**
 * ice_parse_ieee_etscfg_tlv
 * @tlv: IEEE 802.1Qaz ETS CFG TLV
 * @dcbcfg: Local store to update ETS CFG data
 *
 * Parses IEEE 802.1Qaz ETS CFG TLV
 */
void
ice_parse_ieee_etscfg_tlv(struct ice_lldp_org_tlv *tlv,
			  struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_dcb_ets_cfg *etscfg;
	uint8_t *buf = tlv->tlvinfo;

	/* First Octet post subtype
	 * --------------------------
	 * |will-|CBS  | Re-  | Max |
	 * |ing  |     |served| TCs |
	 * --------------------------
	 * |1bit | 1bit|3 bits|3bits|
	 */
	etscfg = &dcbcfg->etscfg;
	etscfg->willing = ((buf[0] & ICE_IEEE_ETS_WILLING_M) >>
			   ICE_IEEE_ETS_WILLING_S);
	etscfg->cbs = ((buf[0] & ICE_IEEE_ETS_CBS_M) >> ICE_IEEE_ETS_CBS_S);
	etscfg->maxtcs = ((buf[0] & ICE_IEEE_ETS_MAXTC_M) >>
			  ICE_IEEE_ETS_MAXTC_S);

	/* Begin parsing at Priority Assignment Table (offset 1 in buf) */
	ice_parse_ieee_ets_common_tlv(&buf[1], etscfg);
}

/**
 * ice_parse_ieee_etsrec_tlv
 * @tlv: IEEE 802.1Qaz ETS REC TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Parses IEEE 802.1Qaz ETS REC TLV
 */
void
ice_parse_ieee_etsrec_tlv(struct ice_lldp_org_tlv *tlv,
			  struct ice_dcbx_cfg *dcbcfg)
{
	uint8_t *buf = tlv->tlvinfo;

	/* Begin parsing at Priority Assignment Table (offset 1 in buf) */
	ice_parse_ieee_ets_common_tlv(&buf[1], &dcbcfg->etsrec);
}

/**
 * ice_parse_ieee_pfccfg_tlv
 * @tlv: IEEE 802.1Qaz PFC CFG TLV
 * @dcbcfg: Local store to update PFC CFG data
 *
 * Parses IEEE 802.1Qaz PFC CFG TLV
 */
void
ice_parse_ieee_pfccfg_tlv(struct ice_lldp_org_tlv *tlv,
			  struct ice_dcbx_cfg *dcbcfg)
{
	uint8_t *buf = tlv->tlvinfo;

	/* ----------------------------------------
	 * |will-|MBC  | Re-  | PFC |  PFC Enable  |
	 * |ing  |     |served| cap |              |
	 * -----------------------------------------
	 * |1bit | 1bit|2 bits|4bits| 1 octet      |
	 */
	dcbcfg->pfc.willing = ((buf[0] & ICE_IEEE_PFC_WILLING_M) >>
			       ICE_IEEE_PFC_WILLING_S);
	dcbcfg->pfc.mbc = ((buf[0] & ICE_IEEE_PFC_MBC_M) >> ICE_IEEE_PFC_MBC_S);
	dcbcfg->pfc.pfccap = ((buf[0] & ICE_IEEE_PFC_CAP_M) >>
			      ICE_IEEE_PFC_CAP_S);
	dcbcfg->pfc.pfcena = buf[1];
}

/**
 * ice_parse_ieee_app_tlv
 * @tlv: IEEE 802.1Qaz APP TLV
 * @dcbcfg: Local store to update APP PRIO data
 *
 * Parses IEEE 802.1Qaz APP PRIO TLV
 */
void
ice_parse_ieee_app_tlv(struct ice_lldp_org_tlv *tlv,
		       struct ice_dcbx_cfg *dcbcfg)
{
	uint16_t offset = 0;
	uint16_t typelen;
	int i = 0;
	uint16_t len;
	uint8_t *buf;

	typelen = ntohs(tlv->typelen);
	len = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);
	buf = tlv->tlvinfo;

	/* Removing sizeof(ouisubtype) and reserved byte from len.
	 * Remaining len div 3 is number of APP TLVs.
	 */
	len -= (sizeof(tlv->ouisubtype) + 1);

	/* Move offset to App Priority Table */
	offset++;

	/* Application Priority Table (3 octets)
	 * Octets:|         1          |    2    |    3    |
	 *        -----------------------------------------
	 *        |Priority|Rsrvd| Sel |    Protocol ID    |
	 *        -----------------------------------------
	 *   Bits:|23    21|20 19|18 16|15                0|
	 *        -----------------------------------------
	 */
	while (offset < len) {
		dcbcfg->app[i].priority = ((buf[offset] &
					    ICE_IEEE_APP_PRIO_M) >>
					   ICE_IEEE_APP_PRIO_S);
		dcbcfg->app[i].selector = ((buf[offset] &
					    ICE_IEEE_APP_SEL_M) >>
					   ICE_IEEE_APP_SEL_S);
		dcbcfg->app[i].prot_id = (buf[offset + 1] << 0x8) |
			buf[offset + 2];
		/* Move to next app */
		offset += 3;
		i++;
		if (i >= ICE_DCBX_MAX_APPS)
			break;
	}

	dcbcfg->numapps = i;
}

/**
 * ice_parse_ieee_tlv
 * @tlv: IEEE 802.1Qaz TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Get the TLV subtype and send it to parsing function
 * based on the subtype value
 */
void
ice_parse_ieee_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	uint32_t ouisubtype;
	uint8_t subtype;

	ouisubtype = ntohl(tlv->ouisubtype);
	subtype = (uint8_t)((ouisubtype & ICE_LLDP_TLV_SUBTYPE_M) >>
		       ICE_LLDP_TLV_SUBTYPE_S);
	switch (subtype) {
	case ICE_IEEE_SUBTYPE_ETS_CFG:
		ice_parse_ieee_etscfg_tlv(tlv, dcbcfg);
		break;
	case ICE_IEEE_SUBTYPE_ETS_REC:
		ice_parse_ieee_etsrec_tlv(tlv, dcbcfg);
		break;
	case ICE_IEEE_SUBTYPE_PFC_CFG:
		ice_parse_ieee_pfccfg_tlv(tlv, dcbcfg);
		break;
	case ICE_IEEE_SUBTYPE_APP_PRI:
		ice_parse_ieee_app_tlv(tlv, dcbcfg);
		break;
	default:
		break;
	}
}

/**
 * ice_parse_cee_pgcfg_tlv
 * @tlv: CEE DCBX PG CFG TLV
 * @dcbcfg: Local store to update ETS CFG data
 *
 * Parses CEE DCBX PG CFG TLV
 */
void
ice_parse_cee_pgcfg_tlv(struct ice_cee_feat_tlv *tlv,
			struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_dcb_ets_cfg *etscfg;
	uint8_t *buf = tlv->tlvinfo;
	uint16_t offset = 0;
	int i;

	etscfg = &dcbcfg->etscfg;

	if (tlv->en_will_err & ICE_CEE_FEAT_TLV_WILLING_M)
		etscfg->willing = 1;

	etscfg->cbs = 0;
	/* Priority Group Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		etscfg->prio_table[i * 2] =
			((buf[offset] & ICE_CEE_PGID_PRIO_1_M) >>
			 ICE_CEE_PGID_PRIO_1_S);
		etscfg->prio_table[i * 2 + 1] =
			((buf[offset] & ICE_CEE_PGID_PRIO_0_M) >>
			 ICE_CEE_PGID_PRIO_0_S);
		offset++;
	}

	/* PG Percentage Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |pg0|pg1|pg2|pg3|pg4|pg5|pg6|pg7|
	 *        ---------------------------------
	 */
	ice_for_each_traffic_class(i) {
		etscfg->tcbwtable[i] = buf[offset++];

		if (etscfg->prio_table[i] == ICE_CEE_PGID_STRICT)
			dcbcfg->etscfg.tsatable[i] = ICE_IEEE_TSA_STRICT;
		else
			dcbcfg->etscfg.tsatable[i] = ICE_IEEE_TSA_ETS;
	}

	/* Number of TCs supported (1 octet) */
	etscfg->maxtcs = buf[offset];
}

/**
 * ice_parse_cee_pfccfg_tlv
 * @tlv: CEE DCBX PFC CFG TLV
 * @dcbcfg: Local store to update PFC CFG data
 *
 * Parses CEE DCBX PFC CFG TLV
 */
void
ice_parse_cee_pfccfg_tlv(struct ice_cee_feat_tlv *tlv,
			 struct ice_dcbx_cfg *dcbcfg)
{
	uint8_t *buf = tlv->tlvinfo;

	if (tlv->en_will_err & ICE_CEE_FEAT_TLV_WILLING_M)
		dcbcfg->pfc.willing = 1;

	/* ------------------------
	 * | PFC Enable | PFC TCs |
	 * ------------------------
	 * | 1 octet    | 1 octet |
	 */
	dcbcfg->pfc.pfcena = buf[0];
	dcbcfg->pfc.pfccap = buf[1];
}

/**
 * ice_parse_cee_app_tlv
 * @tlv: CEE DCBX APP TLV
 * @dcbcfg: Local store to update APP PRIO data
 *
 * Parses CEE DCBX APP PRIO TLV
 */
void
ice_parse_cee_app_tlv(struct ice_cee_feat_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	uint16_t len, typelen, offset = 0;
	struct ice_cee_app_prio *app;
	uint8_t i;

	typelen = NTOHS(tlv->hdr.typelen);
	len = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);

	dcbcfg->numapps = len / sizeof(*app);
	if (!dcbcfg->numapps)
		return;
	if (dcbcfg->numapps > ICE_DCBX_MAX_APPS)
		dcbcfg->numapps = ICE_DCBX_MAX_APPS;

	for (i = 0; i < dcbcfg->numapps; i++) {
		uint8_t up, selector;

		app = (struct ice_cee_app_prio *)(tlv->tlvinfo + offset);
		for (up = 0; up < ICE_MAX_USER_PRIORITY; up++)
			if (app->prio_map & BIT(up))
				break;

		dcbcfg->app[i].priority = up;

		/* Get Selector from lower 2 bits, and convert to IEEE */
		selector = (app->upper_oui_sel & ICE_CEE_APP_SELECTOR_M);
		switch (selector) {
		case ICE_CEE_APP_SEL_ETHTYPE:
			dcbcfg->app[i].selector = ICE_APP_SEL_ETHTYPE;
			break;
		case ICE_CEE_APP_SEL_TCPIP:
			dcbcfg->app[i].selector = ICE_APP_SEL_TCPIP;
			break;
		default:
			/* Keep selector as it is for unknown types */
			dcbcfg->app[i].selector = selector;
		}

		dcbcfg->app[i].prot_id = NTOHS(app->protocol);
		/* Move to next app */
		offset += sizeof(*app);
	}
}

/**
 * ice_parse_cee_tlv
 * @tlv: CEE DCBX TLV
 * @dcbcfg: Local store to update DCBX config data
 *
 * Get the TLV subtype and send it to parsing function
 * based on the subtype value
 */
void
ice_parse_cee_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_cee_feat_tlv *sub_tlv;
	uint8_t subtype, feat_tlv_count = 0;
	uint16_t len, tlvlen, typelen;
	uint32_t ouisubtype;

	ouisubtype = ntohl(tlv->ouisubtype);
	subtype = (uint8_t)((ouisubtype & ICE_LLDP_TLV_SUBTYPE_M) >>
		       ICE_LLDP_TLV_SUBTYPE_S);
	/* Return if not CEE DCBX */
	if (subtype != ICE_CEE_DCBX_TYPE)
		return;

	typelen = ntohs(tlv->typelen);
	tlvlen = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);
	len = sizeof(tlv->typelen) + sizeof(ouisubtype) +
		sizeof(struct ice_cee_ctrl_tlv);
	/* Return if no CEE DCBX Feature TLVs */
	if (tlvlen <= len)
		return;

	sub_tlv = (struct ice_cee_feat_tlv *)((char *)tlv + len);
	while (feat_tlv_count < ICE_CEE_MAX_FEAT_TYPE) {
		uint16_t sublen;

		typelen = ntohs(sub_tlv->hdr.typelen);
		sublen = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);
		subtype = (uint8_t)((typelen & ICE_LLDP_TLV_TYPE_M) >>
			       ICE_LLDP_TLV_TYPE_S);
		switch (subtype) {
		case ICE_CEE_SUBTYPE_PG_CFG:
			ice_parse_cee_pgcfg_tlv(sub_tlv, dcbcfg);
			break;
		case ICE_CEE_SUBTYPE_PFC_CFG:
			ice_parse_cee_pfccfg_tlv(sub_tlv, dcbcfg);
			break;
		case ICE_CEE_SUBTYPE_APP_PRI:
			ice_parse_cee_app_tlv(sub_tlv, dcbcfg);
			break;
		default:
			return;	/* Invalid Sub-type return */
		}
		feat_tlv_count++;
		/* Move to next sub TLV */
		sub_tlv = (struct ice_cee_feat_tlv *)
			  ((char *)sub_tlv + sizeof(sub_tlv->hdr.typelen) +
			   sublen);
	}
}
/**
 * ice_parse_org_tlv
 * @tlv: Organization specific TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Currently only IEEE 802.1Qaz TLV is supported, all others
 * will be returned
 */
void
ice_parse_org_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	uint32_t ouisubtype;
	uint32_t oui;

	ouisubtype = ntohl(tlv->ouisubtype);
	oui = ((ouisubtype & ICE_LLDP_TLV_OUI_M) >> ICE_LLDP_TLV_OUI_S);
	switch (oui) {
	case ICE_IEEE_8021QAZ_OUI:
		ice_parse_ieee_tlv(tlv, dcbcfg);
		break;
	case ICE_CEE_DCBX_OUI:
		ice_parse_cee_tlv(tlv, dcbcfg);
		break;
	default:
		break;
	}
}
/**
 * ice_lldp_to_dcb_cfg
 * @lldpmib: LLDPDU to be parsed
 * @dcbcfg: store for LLDPDU data
 *
 * Parse DCB configuration from the LLDPDU
 */
enum ice_status
ice_lldp_to_dcb_cfg(uint8_t *lldpmib, struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_lldp_org_tlv *tlv;
	enum ice_status ret = ICE_SUCCESS;
	uint16_t offset = 0;
	uint16_t typelen;
	uint16_t type;
	uint16_t len;

	if (!lldpmib || !dcbcfg)
		return ICE_ERR_PARAM;

	/* set to the start of LLDPDU */
	lldpmib += ETHER_HDR_LEN;
	tlv = (struct ice_lldp_org_tlv *)lldpmib;
	while (1) {
		typelen = ntohs(tlv->typelen);
		type = ((typelen & ICE_LLDP_TLV_TYPE_M) >> ICE_LLDP_TLV_TYPE_S);
		len = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);
		offset += sizeof(typelen) + len;

		/* END TLV or beyond LLDPDU size */
		if (type == ICE_TLV_TYPE_END || offset > ICE_LLDPDU_SIZE)
			break;

		switch (type) {
		case ICE_TLV_TYPE_ORG:
			ice_parse_org_tlv(tlv, dcbcfg);
			break;
		default:
			break;
		}

		/* Move to next TLV */
		tlv = (struct ice_lldp_org_tlv *)
		      ((char *)tlv + sizeof(tlv->typelen) + len);
	}

	return ret;
}
/**
 * ice_aq_get_dcb_cfg
 * @hw: pointer to the HW struct
 * @mib_type: MIB type for the query
 * @bridgetype: bridge type for the query (remote)
 * @dcbcfg: store for LLDPDU data
 *
 * Query DCB configuration from the firmware
 */
enum ice_status
ice_aq_get_dcb_cfg(struct ice_hw *hw, uint8_t mib_type, uint8_t bridgetype,
		   struct ice_dcbx_cfg *dcbcfg)
{
	enum ice_status ret;
	uint8_t *lldpmib;

	/* Allocate the LLDPDU */
	lldpmib = (uint8_t *)ice_malloc(hw, ICE_LLDPDU_SIZE);
	if (!lldpmib)
		return ICE_ERR_NO_MEMORY;

	ret = ice_aq_get_lldp_mib(hw, bridgetype, mib_type, (void *)lldpmib,
				  ICE_LLDPDU_SIZE, NULL, NULL, NULL);

	if (ret == ICE_SUCCESS)
		/* Parse LLDP MIB to get DCB configuration */
		ret = ice_lldp_to_dcb_cfg(lldpmib, dcbcfg);

	ice_free(hw, lldpmib);

	return ret;
}

/**
 * ice_cee_to_dcb_cfg
 * @cee_cfg: pointer to CEE configuration struct
 * @pi: port information structure
 *
 * Convert CEE configuration from firmware to DCB configuration
 */
void
ice_cee_to_dcb_cfg(struct ice_aqc_get_cee_dcb_cfg_resp *cee_cfg,
		   struct ice_port_info *pi)
{
	uint32_t status, tlv_status = le32toh(cee_cfg->tlv_status);
	uint32_t ice_aqc_cee_status_mask, ice_aqc_cee_status_shift;
	uint8_t i, j, err, sync, oper, app_index, ice_app_sel_type;
	uint16_t app_prio = le16toh(cee_cfg->oper_app_prio);
	uint16_t ice_aqc_cee_app_mask, ice_aqc_cee_app_shift;
	struct ice_dcbx_cfg *cmp_dcbcfg, *dcbcfg;
	uint16_t ice_app_prot_id_type;

	dcbcfg = &pi->qos_cfg.local_dcbx_cfg;
	dcbcfg->dcbx_mode = ICE_DCBX_MODE_CEE;
	dcbcfg->tlv_status = tlv_status;

	/* CEE PG data */
	dcbcfg->etscfg.maxtcs = cee_cfg->oper_num_tc;

	/* Note that the FW creates the oper_prio_tc nibbles reversed
	 * from those in the CEE Priority Group sub-TLV.
	 */
	for (i = 0; i < ICE_MAX_TRAFFIC_CLASS / 2; i++) {
		dcbcfg->etscfg.prio_table[i * 2] =
			((cee_cfg->oper_prio_tc[i] & ICE_CEE_PGID_PRIO_0_M) >>
			 ICE_CEE_PGID_PRIO_0_S);
		dcbcfg->etscfg.prio_table[i * 2 + 1] =
			((cee_cfg->oper_prio_tc[i] & ICE_CEE_PGID_PRIO_1_M) >>
			 ICE_CEE_PGID_PRIO_1_S);
	}

	ice_for_each_traffic_class(i) {
		dcbcfg->etscfg.tcbwtable[i] = cee_cfg->oper_tc_bw[i];

		if (dcbcfg->etscfg.prio_table[i] == ICE_CEE_PGID_STRICT) {
			/* Map it to next empty TC */
			dcbcfg->etscfg.prio_table[i] = cee_cfg->oper_num_tc - 1;
			dcbcfg->etscfg.tsatable[i] = ICE_IEEE_TSA_STRICT;
		} else {
			dcbcfg->etscfg.tsatable[i] = ICE_IEEE_TSA_ETS;
		}
	}

	/* CEE PFC data */
	dcbcfg->pfc.pfcena = cee_cfg->oper_pfc_en;
	dcbcfg->pfc.pfccap = ICE_MAX_TRAFFIC_CLASS;

	/* CEE APP TLV data */
	if (dcbcfg->app_mode == ICE_DCBX_APPS_NON_WILLING)
		cmp_dcbcfg = &pi->qos_cfg.desired_dcbx_cfg;
	else
		cmp_dcbcfg = &pi->qos_cfg.remote_dcbx_cfg;

	app_index = 0;
	for (i = 0; i < 3; i++) {
		if (i == 0) {
			/* FCoE APP */
			ice_aqc_cee_status_mask = ICE_AQC_CEE_FCOE_STATUS_M;
			ice_aqc_cee_status_shift = ICE_AQC_CEE_FCOE_STATUS_S;
			ice_aqc_cee_app_mask = ICE_AQC_CEE_APP_FCOE_M;
			ice_aqc_cee_app_shift = ICE_AQC_CEE_APP_FCOE_S;
			ice_app_sel_type = ICE_APP_SEL_ETHTYPE;
			ice_app_prot_id_type = ICE_APP_PROT_ID_FCOE;
		} else if (i == 1) {
			/* iSCSI APP */
			ice_aqc_cee_status_mask = ICE_AQC_CEE_ISCSI_STATUS_M;
			ice_aqc_cee_status_shift = ICE_AQC_CEE_ISCSI_STATUS_S;
			ice_aqc_cee_app_mask = ICE_AQC_CEE_APP_ISCSI_M;
			ice_aqc_cee_app_shift = ICE_AQC_CEE_APP_ISCSI_S;
			ice_app_sel_type = ICE_APP_SEL_TCPIP;
			ice_app_prot_id_type = ICE_APP_PROT_ID_ISCSI;

			for (j = 0; j < cmp_dcbcfg->numapps; j++) {
				uint16_t prot_id = cmp_dcbcfg->app[j].prot_id;
				uint8_t sel = cmp_dcbcfg->app[j].selector;

				if  (sel == ICE_APP_SEL_TCPIP &&
				     (prot_id == ICE_APP_PROT_ID_ISCSI ||
				      prot_id == ICE_APP_PROT_ID_ISCSI_860)) {
					ice_app_prot_id_type = prot_id;
					break;
				}
			}
		} else {
			/* FIP APP */
			ice_aqc_cee_status_mask = ICE_AQC_CEE_FIP_STATUS_M;
			ice_aqc_cee_status_shift = ICE_AQC_CEE_FIP_STATUS_S;
			ice_aqc_cee_app_mask = ICE_AQC_CEE_APP_FIP_M;
			ice_aqc_cee_app_shift = ICE_AQC_CEE_APP_FIP_S;
			ice_app_sel_type = ICE_APP_SEL_ETHTYPE;
			ice_app_prot_id_type = ICE_APP_PROT_ID_FIP;
		}

		status = (tlv_status & ice_aqc_cee_status_mask) >>
			 ice_aqc_cee_status_shift;
		err = (status & ICE_TLV_STATUS_ERR) ? 1 : 0;
		sync = (status & ICE_TLV_STATUS_SYNC) ? 1 : 0;
		oper = (status & ICE_TLV_STATUS_OPER) ? 1 : 0;
		/* Add FCoE/iSCSI/FIP APP if Error is False and
		 * Oper/Sync is True
		 */
		if (!err && sync && oper) {
			dcbcfg->app[app_index].priority =
				(uint8_t)((app_prio & ice_aqc_cee_app_mask) >>
				     ice_aqc_cee_app_shift);
			dcbcfg->app[app_index].selector = ice_app_sel_type;
			dcbcfg->app[app_index].prot_id = ice_app_prot_id_type;
			app_index++;
		}
	}

	dcbcfg->numapps = app_index;
}

/**
 * ice_get_dcb_cfg_from_mib_change
 * @pi: port information structure
 * @event: pointer to the admin queue receive event
 *
 * Set DCB configuration from received MIB Change event
 */
void
ice_get_dcb_cfg_from_mib_change(struct ice_port_info *pi,
    struct ice_rq_event_info *event)
{
	struct ice_dcbx_cfg *dcbx_cfg = &pi->qos_cfg.local_dcbx_cfg;
	struct ice_aqc_lldp_get_mib *mib;
	uint8_t change_type, dcbx_mode;

	mib = (struct ice_aqc_lldp_get_mib *)&event->desc.params.raw;

	change_type = mib->type & ICE_AQ_LLDP_MIB_TYPE_M;
	if (change_type == ICE_AQ_LLDP_MIB_REMOTE)
		dcbx_cfg = &pi->qos_cfg.remote_dcbx_cfg;

	dcbx_mode = ((mib->type & ICE_AQ_LLDP_DCBX_M) >>
		     ICE_AQ_LLDP_DCBX_S);

	switch (dcbx_mode) {
	case ICE_AQ_LLDP_DCBX_IEEE:
		dcbx_cfg->dcbx_mode = ICE_DCBX_MODE_IEEE;
		ice_lldp_to_dcb_cfg(event->msg_buf, dcbx_cfg);
		break;

	case ICE_AQ_LLDP_DCBX_CEE:
		pi->qos_cfg.desired_dcbx_cfg = pi->qos_cfg.local_dcbx_cfg;
		ice_cee_to_dcb_cfg((struct ice_aqc_get_cee_dcb_cfg_resp *)
				   event->msg_buf, pi);
		break;
	}
}

/**
 * ice_aq_get_cee_dcb_cfg
 * @hw: pointer to the HW struct
 * @buff: response buffer that stores CEE operational configuration
 * @cd: pointer to command details structure or NULL
 *
 * Get CEE DCBX mode operational configuration from firmware (0x0A07)
 */
enum ice_status
ice_aq_get_cee_dcb_cfg(struct ice_hw *hw,
		       struct ice_aqc_get_cee_dcb_cfg_resp *buff,
		       struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_cee_dcb_cfg);

	return ice_aq_send_cmd(hw, &desc, (void *)buff, sizeof(*buff), cd);
}

/**
 * ice_get_ieee_or_cee_dcb_cfg
 * @pi: port information structure
 * @dcbx_mode: mode of DCBX (IEEE or CEE)
 *
 * Get IEEE or CEE mode DCB configuration from the Firmware
 */
enum ice_status
ice_get_ieee_or_cee_dcb_cfg(struct ice_port_info *pi, uint8_t dcbx_mode)
{
	struct ice_dcbx_cfg *dcbx_cfg = NULL;
	enum ice_status ret;

	if (!pi)
		return ICE_ERR_PARAM;

	if (dcbx_mode == ICE_DCBX_MODE_IEEE)
		dcbx_cfg = &pi->qos_cfg.local_dcbx_cfg;
	else if (dcbx_mode == ICE_DCBX_MODE_CEE)
		dcbx_cfg = &pi->qos_cfg.desired_dcbx_cfg;

	/* Get Local DCB Config in case of ICE_DCBX_MODE_IEEE
	 * or get CEE DCB Desired Config in case of ICE_DCBX_MODE_CEE
	 */
	ret = ice_aq_get_dcb_cfg(pi->hw, ICE_AQ_LLDP_MIB_LOCAL,
				 ICE_AQ_LLDP_BRID_TYPE_NEAREST_BRID, dcbx_cfg);
	if (ret)
		goto out;

	/* Get Remote DCB Config */
	dcbx_cfg = &pi->qos_cfg.remote_dcbx_cfg;
	ret = ice_aq_get_dcb_cfg(pi->hw, ICE_AQ_LLDP_MIB_REMOTE,
				 ICE_AQ_LLDP_BRID_TYPE_NEAREST_BRID, dcbx_cfg);
	/* Don't treat ENOENT as an error for Remote MIBs */
	if (pi->hw->adminq.sq_last_status == ICE_AQ_RC_ENOENT)
		ret = ICE_SUCCESS;

out:
	return ret;
}

/**
 * ice_get_dcb_cfg
 * @pi: port information structure
 *
 * Get DCB configuration from the Firmware
 */
enum ice_status
ice_get_dcb_cfg(struct ice_port_info *pi)
{
	struct ice_aqc_get_cee_dcb_cfg_resp cee_cfg;
	struct ice_dcbx_cfg *dcbx_cfg;
	enum ice_status ret;

	if (!pi)
		return ICE_ERR_PARAM;

	ret = ice_aq_get_cee_dcb_cfg(pi->hw, &cee_cfg, NULL);
	if (ret == ICE_SUCCESS) {
		/* CEE mode */
		ret = ice_get_ieee_or_cee_dcb_cfg(pi, ICE_DCBX_MODE_CEE);
		ice_cee_to_dcb_cfg(&cee_cfg, pi);
	} else if (pi->hw->adminq.sq_last_status == ICE_AQ_RC_ENOENT) {
		/* CEE mode not enabled try querying IEEE data */
		dcbx_cfg = &pi->qos_cfg.local_dcbx_cfg;
		dcbx_cfg->dcbx_mode = ICE_DCBX_MODE_IEEE;
		ret = ice_get_ieee_or_cee_dcb_cfg(pi, ICE_DCBX_MODE_IEEE);
	}

	return ret;
}

/**
 * ice_dcb_needs_reconfig - Returns true if driver needs to reconfigure
 * @sc: the device private softc
 * @old_cfg: Old DCBX configuration to compare against
 * @new_cfg: New DCBX configuration to check
 *
 * @return true if something changed in new_cfg that requires the driver
 * to do some reconfiguration.
 */
bool
ice_dcb_needs_reconfig(struct ice_softc *sc, struct ice_dcbx_cfg *old_cfg,
    struct ice_dcbx_cfg *new_cfg)
{
	bool needs_reconfig = false;

	/* No change detected in DCBX config */
	if (!memcmp(old_cfg, new_cfg, sizeof(*old_cfg))) {
		DNPRINTF(ICE_DBG_DCB,
		    "%s: No change detected in local DCBX configuration\n",
		    sc->sc_dev.dv_xname);
		return (false);
	}

	/* Check if ETS config has changed */
	if (memcmp(&new_cfg->etscfg, &old_cfg->etscfg,
		   sizeof(new_cfg->etscfg))) {
		/* If Priority Table has changed, driver reconfig is needed */
		if (memcmp(&new_cfg->etscfg.prio_table,
			   &old_cfg->etscfg.prio_table,
			   sizeof(new_cfg->etscfg.prio_table))) {
			DNPRINTF(ICE_DBG_DCB, "%s: ETS UP2TC changed\n",
			    __func__);
			needs_reconfig = true;
		}

		/* These are just informational */
		if (memcmp(&new_cfg->etscfg.tcbwtable,
			   &old_cfg->etscfg.tcbwtable,
			   sizeof(new_cfg->etscfg.tcbwtable))) {
			DNPRINTF(ICE_DBG_DCB, "%s: ETS TCBW table changed\n",
			    __func__);
			needs_reconfig = true;
		}

		if (memcmp(&new_cfg->etscfg.tsatable,
			   &old_cfg->etscfg.tsatable,
			   sizeof(new_cfg->etscfg.tsatable))) {
			DNPRINTF(ICE_DBG_DCB, "%s: ETS TSA table changed\n",
			    __func__);
			needs_reconfig = true;
		}
	}

	/* Check if PFC config has changed */
	if (memcmp(&new_cfg->pfc, &old_cfg->pfc, sizeof(new_cfg->pfc))) {
		DNPRINTF(ICE_DBG_DCB, "%s: PFC config changed\n", __func__);
		needs_reconfig = true;
	}

	/* Check if APP table has changed */
	if (memcmp(&new_cfg->app, &old_cfg->app, sizeof(new_cfg->app)))
		DNPRINTF(ICE_DBG_DCB, "%s: APP Table changed\n", __func__);

	DNPRINTF(ICE_DBG_DCB, "%s result: %d\n", __func__, needs_reconfig);

	return (needs_reconfig);
}

/**
 * ice_do_dcb_reconfig - notify RDMA and reconfigure PF LAN VSI
 * @sc: the device private softc
 * @pending_mib: FW has a pending MIB change to execute
 * 
 * @pre Determined that the DCB configuration requires a change
 *
 * Reconfigures the PF LAN VSI based on updated DCB configuration
 * found in the hw struct's/port_info's/ local dcbx configuration.
 */
void
ice_do_dcb_reconfig(struct ice_softc *sc, bool pending_mib)
{
#if 0
	struct ice_aqc_port_ets_elem port_ets = { 0 };
	struct ice_dcbx_cfg *local_dcbx_cfg;
	struct ice_hw *hw = &sc->hw;
	struct ice_port_info *pi;
	device_t dev = sc->dev;
	enum ice_status status;

	pi = sc->hw.port_info;
	local_dcbx_cfg = &pi->qos_cfg.local_dcbx_cfg;

	ice_rdma_notify_dcb_qos_change(sc);
	/* If there's a pending MIB, tell the FW to execute the MIB change
	 * now.
	 */
	if (pending_mib) {
		status = ice_lldp_execute_pending_mib(hw);
		if ((status == ICE_ERR_AQ_ERROR) &&
		    (hw->adminq.sq_last_status == ICE_AQ_RC_ENOENT)) {
			device_printf(dev,
			    "Execute Pending LLDP MIB AQ call failed, no pending MIB\n");
		} else if (status) {
			device_printf(dev,
			    "Execute Pending LLDP MIB AQ call failed, err %s aq_err %s\n",
			    ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			/* This won't break traffic, but QoS will not work as expected */
		}
	}

	/* Set state when there's more than one TC */
	if (ice_dcb_get_num_tc(local_dcbx_cfg) > 1) {
		device_printf(dev, "Multiple traffic classes enabled\n");
		ice_set_state(&sc->state, ICE_STATE_MULTIPLE_TCS);
	} else {
		device_printf(dev, "Multiple traffic classes disabled\n");
		ice_clear_state(&sc->state, ICE_STATE_MULTIPLE_TCS);
	}

	/* Disable PF VSI since it's going to be reconfigured */
	ice_stop_pf_vsi(sc);

	/* Query ETS configuration and update SW Tx scheduler info */
	status = ice_query_port_ets(pi, &port_ets, sizeof(port_ets), NULL);
	if (status != ICE_SUCCESS) {
		device_printf(dev,
		    "Query Port ETS AQ call failed, err %s aq_err %s\n",
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		/* This won't break traffic, but QoS will not work as expected */
	}

	/* Change PF VSI configuration */
	ice_dcb_recfg(sc);

	/* Send new configuration to RDMA client driver */
	ice_rdma_dcb_qos_update(sc, pi);

	ice_request_stack_reinit(sc);
#else
	printf("%s: not implemented", __func__);
#endif
}

/**
 * ice_handle_mib_change_event - handle LLDP MIB change events
 * @sc: the device private softc
 * @event: event received on a control queue
 *
 * Checks the updated MIB it receives and possibly reconfigures the PF LAN
 * VSI depending on what has changed. This will also print out some debug
 * information about the MIB event if ICE_DBG_DCB is enabled in the debug_mask.
 */
void
ice_handle_mib_change_event(struct ice_softc *sc,
    struct ice_rq_event_info *event)
{
	struct ice_aqc_lldp_get_mib *params =
	    (struct ice_aqc_lldp_get_mib *)&event->desc.params.lldp_get_mib;
	struct ice_dcbx_cfg tmp_dcbx_cfg, *local_dcbx_cfg;
	struct ice_port_info *pi;
	struct ice_hw *hw = &sc->hw;
	bool needs_reconfig, mib_is_pending;
	enum ice_status status;
	uint8_t mib_type, bridge_type;
#if 0
	ASSERT_CFG_LOCKED(sc);
#endif
	ice_debug_print_mib_change_event(sc, event);

	pi = sc->hw.port_info;

	mib_type = (params->type & ICE_AQ_LLDP_MIB_TYPE_M) >>
	    ICE_AQ_LLDP_MIB_TYPE_S;
	bridge_type = (params->type & ICE_AQ_LLDP_BRID_TYPE_M) >>
	    ICE_AQ_LLDP_BRID_TYPE_S;
	mib_is_pending = (params->state & ICE_AQ_LLDP_MIB_CHANGE_STATE_M) >>
	    ICE_AQ_LLDP_MIB_CHANGE_STATE_S;

	/* Ignore if event is not for Nearest Bridge */
	if (bridge_type != ICE_AQ_LLDP_BRID_TYPE_NEAREST_BRID)
		return;

	/* Check MIB Type and return if event for Remote MIB update */
	if (mib_type == ICE_AQ_LLDP_MIB_REMOTE) {
		/* Update the cached remote MIB and return */
		status = ice_aq_get_dcb_cfg(pi->hw, ICE_AQ_LLDP_MIB_REMOTE,
					 ICE_AQ_LLDP_BRID_TYPE_NEAREST_BRID,
					 &pi->qos_cfg.remote_dcbx_cfg);
		if (status)
			printf("%s: Failed to get Remote DCB config; "
			    "status %s, aq_err %s\n",
			    sc->sc_dev.dv_xname, ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
		/* Not fatal if this fails */
		return;
	}

	/* Save line length by aliasing the local dcbx cfg */
	local_dcbx_cfg = &pi->qos_cfg.local_dcbx_cfg;
	/* Save off the old configuration and clear current config */
	tmp_dcbx_cfg = *local_dcbx_cfg;
	memset(local_dcbx_cfg, 0, sizeof(*local_dcbx_cfg));

	/* Update the current local_dcbx_cfg with new data */
	if (mib_is_pending) {
		ice_get_dcb_cfg_from_mib_change(pi, event);
	} else {
		/* Get updated DCBX data from firmware */
		status = ice_get_dcb_cfg(pi);
		if (status) {
			printf("%s: Failed to get Local DCB config; "
			    "status %s, aq_err %s\n",
			    sc->sc_dev.dv_xname, ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			return;
		}
	}

	/* Check to see if DCB needs reconfiguring */
	needs_reconfig = ice_dcb_needs_reconfig(sc, &tmp_dcbx_cfg,
	    local_dcbx_cfg);

	if (!needs_reconfig && !mib_is_pending)
		return;

	/* Reconfigure -- this will also notify FW that configuration is done,
	 * if the FW MIB change is only pending instead of executed.
	 */
	ice_do_dcb_reconfig(sc, mib_is_pending);
}

/**
 * ice_handle_lan_overflow_event - helper function to log LAN overflow events
 * @sc: device softc
 * @event: event received on a control queue
 *
 * Prints out a message when a LAN overflow event is detected on a receive
 * queue.
 */
void
ice_handle_lan_overflow_event(struct ice_softc *sc,
    struct ice_rq_event_info *event)
{
#ifdef ICE_DEBUG
	struct ice_aqc_event_lan_overflow *params =
	    (struct ice_aqc_event_lan_overflow *)&event->desc.params.lan_overflow;

	DNPRINTF(ICE_DBG_DCB, "%s: LAN overflow event detected, "
	    "prtdcb_ruptq=0x%08x, qtx_ctl=0x%08x\n",
	    sc->sc_dev.dv_xname, le32toh(params->prtdcb_ruptq),
	    le32toh(params->qtx_ctl));
#endif
}

/**
 * ice_print_health_status_string - Print message for given FW health event
 * @dev: the PCIe device
 * @elem: health status element containing status code
 *
 * A rather large list of possible health status codes and their associated
 * messages.
 */
void
ice_print_health_status_string(struct ice_softc *sc,
    struct ice_aqc_health_status_elem *elem)
{
	uint16_t status_code = le16toh(elem->health_status_code);

	switch (status_code) {
	case ICE_AQC_HEALTH_STATUS_INFO_RECOVERY:
		DPRINTF("%s: The device is in firmware recovery mode.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_FLASH_ACCESS:
		DPRINTF("%s: The flash chip cannot be accessed.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_NVM_AUTH:
		DPRINTF("%s: NVM authentication failed.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_OROM_AUTH:
		DPRINTF("%s: Option ROM authentication failed.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_DDP_AUTH:
		DPRINTF("%s: DDP package failed.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_NVM_COMPAT:
		DPRINTF("%s: NVM image is incompatible.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_OROM_COMPAT:
		DPRINTF("%s: Option ROM is incompatible.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_DCB_MIB:
		DPRINTF("%s: Supplied MIB file is invalid. "
		    "DCB reverted to default configuration.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_UNKNOWN_MOD_STRICT:
		DPRINTF("%s: An unsupported module was detected.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_MOD_TYPE:
		DPRINTF("%s: Module type is not supported.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_MOD_QUAL:
		DPRINTF("%s: Module is not qualified.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_MOD_COMM:
		DPRINTF("%s: Device cannot communicate with the module.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_MOD_CONFLICT:
		DPRINTF("%s: Unresolved module conflict.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_MOD_NOT_PRESENT:
		DPRINTF("%s: Module is not present.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_INFO_MOD_UNDERUTILIZED:
		DPRINTF("%s: Underutilized module.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_UNKNOWN_MOD_LENIENT:
		DPRINTF("%s: An unsupported module was detected.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_INVALID_LINK_CFG:
		DPRINTF("%s: Invalid link configuration.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_PORT_ACCESS:
		DPRINTF("%s: Port hardware access error.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_PORT_UNREACHABLE:
		DPRINTF("%s: A port is unreachable.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_INFO_PORT_SPEED_MOD_LIMITED:
		DPRINTF("%s: Port speed is limited due to module.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_PARALLEL_FAULT:
		DPRINTF("%s: A parallel fault was detected.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_INFO_PORT_SPEED_PHY_LIMITED:
		DPRINTF("%s: Port speed is limited by PHY capabilities.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_NETLIST_TOPO:
		DPRINTF("%s: LOM topology netlist is corrupted.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_NETLIST:
		DPRINTF("%s: Unrecoverable netlist error.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_TOPO_CONFLICT:
		DPRINTF("%s: Port topology conflict.\n", sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_LINK_HW_ACCESS:
		DPRINTF("%s: Unrecoverable hardware access error.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_LINK_RUNTIME:
		DPRINTF("%s: Unrecoverable runtime error.\n",
		    sc->sc_dev.dv_xname);
		break;
	case ICE_AQC_HEALTH_STATUS_ERR_DNL_INIT:
		DPRINTF("%s: Link management engine failed to initialize.\n",
		    sc->sc_dev.dv_xname);
		break;
	default:
		break;
	}
}

/**
 * ice_handle_health_status_event - helper function to output health status
 * @sc: device softc structure
 * @event: event received on a control queue
 *
 * Prints out the appropriate string based on the given Health Status Event
 * code.
 */
void
ice_handle_health_status_event(struct ice_softc *sc,
			       struct ice_rq_event_info *event)
{
	struct ice_aqc_health_status_elem *health_info;
	uint16_t status_count;
	int i;

	if (!ice_is_bit_set(sc->feat_en, ICE_FEATURE_HEALTH_STATUS))
		return;

	health_info = (struct ice_aqc_health_status_elem *)event->msg_buf;
	status_count = le16toh(
	    event->desc.params.get_health_status.health_status_count);

	if (status_count > (event->buf_len / sizeof(*health_info))) {
		DPRINTF("%s: Received a health status event with invalid "
		    "event count\n", sc->sc_dev.dv_xname);
		return;
	}

	for (i = 0; i < status_count; i++) {
		ice_print_health_status_string(sc, health_info);
		health_info++;
	}
}
/**
 * ice_process_ctrlq_event - Respond to a controlq event
 * @sc: device private structure
 * @qname: the name for this controlq
 * @event: the event to process
 *
 * Perform actions in response to various controlq event notifications.
 */
void
ice_process_ctrlq_event(struct ice_softc *sc, const char *qname,
			struct ice_rq_event_info *event)
{
	uint16_t opcode;

	opcode = le16toh(event->desc.opcode);

	switch (opcode) {
	case ice_aqc_opc_get_link_status:
		ice_process_link_event(sc, event);
		break;
	case ice_aqc_opc_fw_logs_event:
		ice_handle_fw_log_event(sc, &event->desc, event->msg_buf);
		break;
	case ice_aqc_opc_lldp_set_mib_change:
		ice_handle_mib_change_event(sc, event);
		break;
	case ice_aqc_opc_event_lan_overflow:
		ice_handle_lan_overflow_event(sc, event);
		break;
	case ice_aqc_opc_get_health_status:
		ice_handle_health_status_event(sc, event);
		break;
	default:
		printf("%s: %s Receive Queue unhandled event 0x%04x ignored\n",
		    sc->sc_dev.dv_xname, qname, opcode);
		break;
	}
}

/**
 * ice_clean_rq_elem
 * @hw: pointer to the HW struct
 * @cq: pointer to the specific Control queue
 * @e: event info from the receive descriptor, includes any buffers
 * @pending: number of events that could be left to process
 *
 * Clean one element from the receive side of a control queue. On return 'e'
 * contains contents of the message, and 'pending' contains the number of
 * events left to process.
 */
enum ice_status
ice_clean_rq_elem(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		  struct ice_rq_event_info *e, uint16_t *pending)
{
	uint16_t ntc = cq->rq.next_to_clean;
	enum ice_aq_err rq_last_status;
	enum ice_status ret_code = ICE_SUCCESS;
	struct ice_aq_desc *desc;
	struct ice_dma_mem *bi;
	uint16_t desc_idx;
	uint16_t datalen;
	uint16_t flags;
	uint16_t ntu;

	/* pre-clean the event info */
	memset(&e->desc, 0, sizeof(e->desc));
#if 0
	/* take the lock before we start messing with the ring */
	ice_acquire_lock(&cq->rq_lock);
#endif
	if (!cq->rq.count) {
		DNPRINTF(ICE_DBG_AQ_MSG,
		    "%s: Control Receive queue not initialized.\n", __func__);
		ret_code = ICE_ERR_AQ_EMPTY;
		goto clean_rq_elem_err;
	}

	/* set next_to_use to head */
	ntu = (uint16_t)(ICE_READ(hw, cq->rq.head) & cq->rq.head_mask);

	if (ntu == ntc) {
		/* nothing to do - shouldn't need to update ring's values */
		ret_code = ICE_ERR_AQ_NO_WORK;
		goto clean_rq_elem_out;
	}

	/* now clean the next descriptor */
	desc = ICE_CTL_Q_DESC(cq->rq, ntc);
	desc_idx = ntc;

	rq_last_status = (enum ice_aq_err)le16toh(desc->retval);
	flags = le16toh(desc->flags);
	if (flags & ICE_AQ_FLAG_ERR) {
		ret_code = ICE_ERR_AQ_ERROR;
		DNPRINTF(ICE_DBG_AQ_MSG, "%s: Control Receive Queue "
		    "Event 0x%04X received with error 0x%X\n",
		    __func__, le16toh(desc->opcode), rq_last_status);
	}
	memcpy(&e->desc, desc, sizeof(e->desc));
	datalen = le16toh(desc->datalen);
	e->msg_len = MIN(datalen, e->buf_len);
	if (e->msg_buf && e->msg_len)
		memcpy(e->msg_buf, cq->rq.r.rq_bi[desc_idx].va, e->msg_len);

	DNPRINTF(ICE_DBG_AQ_DESC, "%s: ARQ: desc and buffer:\n", __func__);
	ice_debug_cq(hw, cq, (void *)desc, e->msg_buf, cq->rq_buf_size, true);

	/* Restore the original datalen and buffer address in the desc,
	 * FW updates datalen to indicate the event message size
	 */
	bi = &cq->rq.r.rq_bi[ntc];
	memset(desc, 0, sizeof(*desc));

	desc->flags = htole16(ICE_AQ_FLAG_BUF);
	if (cq->rq_buf_size > ICE_AQ_LG_BUF)
		desc->flags |= htole16(ICE_AQ_FLAG_LB);
	desc->datalen = htole16(bi->size);
	desc->params.generic.addr_high = htole32(ICE_HI_DWORD(bi->pa));
	desc->params.generic.addr_low = htole32(ICE_LO_DWORD(bi->pa));

	/* set tail = the last cleaned desc index. */
	ICE_WRITE(hw, cq->rq.tail, ntc);
	/* ntc is updated to tail + 1 */
	ntc++;
	if (ntc == cq->num_rq_entries)
		ntc = 0;
	cq->rq.next_to_clean = ntc;
	cq->rq.next_to_use = ntu;

clean_rq_elem_out:
	/* Set pending if needed, unlock and return */
	if (pending) {
		/* re-read HW head to calculate actual pending messages */
		ntu = (uint16_t)(ICE_READ(hw, cq->rq.head) & cq->rq.head_mask);
		*pending = (uint16_t)((ntc > ntu ? cq->rq.count : 0) +
		    (ntu - ntc));
	}
clean_rq_elem_err:
#if 0
	ice_release_lock(&cq->rq_lock);
#endif
	return ret_code;
}

/**
 * ice_process_ctrlq - helper function to process controlq rings
 * @sc: device private structure
 * @q_type: specific control queue type
 * @pending: return parameter to track remaining events
 *
 * Process controlq events for a given control queue type. Returns zero on
 * success, and an error code on failure. If successful, pending is the number
 * of remaining events left in the queue.
 */
int
ice_process_ctrlq(struct ice_softc *sc, enum ice_ctl_q q_type,
    uint16_t *pending)
{
	struct ice_rq_event_info event = { { 0 } };
	struct ice_hw *hw = &sc->hw;
	struct ice_ctl_q_info *cq;
	enum ice_status status;
	const char *qname;
	int loop = 0;

	switch (q_type) {
	case ICE_CTL_Q_ADMIN:
		cq = &hw->adminq;
		qname = "Admin";
		break;
	case ICE_CTL_Q_MAILBOX:
		cq = &hw->mailboxq;
		qname = "Mailbox";
		break;
	default:
		DPRINTF("%s: Unknown control queue type 0x%x\n",
		    __func__, q_type);
		return 0;
	}

	ice_check_ctrlq_errors(sc, qname, cq);

	/*
	 * Control queue processing happens during the admin task which may be
	 * holding a non-sleepable lock, so we *must* use M_NOWAIT here.
	 */
	event.buf_len = cq->rq_buf_size;
	event.msg_buf = (uint8_t *)malloc(event.buf_len, M_DEVBUF,
	    M_ZERO | M_NOWAIT);
	if (!event.msg_buf) {
		printf("%s: Unable to allocate memory for %s Receive Queue "
		    "event\n", sc->sc_dev.dv_xname, qname);
		return (ENOMEM);
	}

	do {
		status = ice_clean_rq_elem(hw, cq, &event, pending);
		if (status == ICE_ERR_AQ_NO_WORK)
			break;
		if (status) {
			if (q_type == ICE_CTL_Q_ADMIN) {
				printf("%s: %s Receive Queue event error %s\n",
				    sc->sc_dev.dv_xname, qname,
				    ice_status_str(status));
			} else {
				printf("%s: %s Receive Queue event error %s\n",
				    sc->sc_dev.dv_xname, qname,
				    ice_status_str(status));
			}
			free(event.msg_buf, M_DEVBUF, event.buf_len);
			return (EIO);
		}
		/* XXX should we separate this handler by controlq type? */
		ice_process_ctrlq_event(sc, qname, &event);
	} while (*pending && (++loop < ICE_CTRLQ_WORK_LIMIT));

	free(event.msg_buf, M_DEVBUF, event.buf_len);

	return 0;
}

/**
 * ice_poll_for_media_avail - Re-enable link if media is detected
 * @sc: device private structure
 *
 * Intended to be called from the driver's timer function, this function
 * sends the Get Link Status AQ command and re-enables HW link if the
 * command says that media is available.
 *
 * If the driver doesn't have the "NO_MEDIA" state set, then this does nothing,
 * since media removal events are supposed to be sent to the driver through
 * a link status event.
 */
void
ice_poll_for_media_avail(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ice_port_info *pi = hw->port_info;

	if (ice_test_state(&sc->state, ICE_STATE_NO_MEDIA)) {
		pi->phy.get_link_info = true;
		ice_get_link_status(pi, &sc->link_up);

		if (pi->phy.link_info.link_info & ICE_AQ_MEDIA_AVAILABLE) {
			enum ice_status status;

			/* Re-enable link and re-apply user link settings */
			if (ice_test_state(&sc->state,
			    ICE_STATE_LINK_ACTIVE_ON_DOWN) ||
			    (ifp->if_flags & IFF_UP)) {
				ice_apply_saved_phy_cfg(sc,
				    ICE_APPLY_LS_FEC_FC);

				/* Update OS with changes in media capability */
				status = ice_add_media_types(sc, &sc->media);
				if (status) {
					printf("%s: Error adding device "
					    "media types: %s aq_err %s\n",
					    sc->sc_dev.dv_xname,
					    ice_status_str(status),
					    ice_aq_str(
					    hw->adminq.sq_last_status));
				}
			}

			ice_clear_state(&sc->state, ICE_STATE_NO_MEDIA);
		}
	}
}

/**
 * ice_stat_update40 - read 40 bit stat from the chip and update stat values
 * @hw: ptr to the hardware info
 * @reg: offset of 64 bit HW register to read from
 * @prev_stat_loaded: bool to specify if previous stats are loaded
 * @prev_stat: ptr to previous loaded stat value
 * @cur_stat: ptr to current stat value
 */
void
ice_stat_update40(struct ice_hw *hw, uint32_t reg, bool prev_stat_loaded,
		  uint64_t *prev_stat, uint64_t *cur_stat)
{
	uint64_t new_data = ICE_READ_8(hw, reg) & (BIT_ULL(40) - 1);

	/*
	 * Device stats are not reset at PFR, they likely will not be zeroed
	 * when the driver starts. Thus, save the value from the first read
	 * without adding to the statistic value so that we report stats which
	 * count up from zero.
	 */
	if (!prev_stat_loaded) {
		*prev_stat = new_data;
		return;
	}

	/*
	 * Calculate the difference between the new and old values, and then
	 * add it to the software stat value.
	 */
	if (new_data >= *prev_stat)
		*cur_stat += new_data - *prev_stat;
	else
		/* to manage the potential roll-over */
		*cur_stat += (new_data + BIT_ULL(40)) - *prev_stat;

	/* Update the previously stored value to prepare for next read */
	*prev_stat = new_data;
}

/**
 * ice_stat_update32 - read 32 bit stat from the chip and update stat values
 * @hw: ptr to the hardware info
 * @reg: offset of HW register to read from
 * @prev_stat_loaded: bool to specify if previous stats are loaded
 * @prev_stat: ptr to previous loaded stat value
 * @cur_stat: ptr to current stat value
 */
void
ice_stat_update32(struct ice_hw *hw, uint32_t reg, bool prev_stat_loaded,
		  uint64_t *prev_stat, uint64_t *cur_stat)
{
	uint32_t new_data;

	new_data = ICE_READ(hw, reg);

	/*
	 * Device stats are not reset at PFR, they likely will not be zeroed
	 * when the driver starts. Thus, save the value from the first read
	 * without adding to the statistic value so that we report stats which
	 * count up from zero.
	 */
	if (!prev_stat_loaded) {
		*prev_stat = new_data;
		return;
	}

	/*
	 * Calculate the difference between the new and old values, and then
	 * add it to the software stat value.
	 */
	if (new_data >= *prev_stat)
		*cur_stat += new_data - *prev_stat;
	else
		/* to manage the potential roll-over */
		*cur_stat += (new_data + BIT_ULL(32)) - *prev_stat;

	/* Update the previously stored value to prepare for next read */
	*prev_stat = new_data;
}

/**
 * ice_stat_update_repc - read GLV_REPC stats from chip and update stat values
 * @hw: ptr to the hardware info
 * @vsi_handle: VSI handle
 * @prev_stat_loaded: bool to specify if the previous stat values are loaded
 * @cur_stats: ptr to current stats structure
 *
 * The GLV_REPC statistic register actually tracks two 16bit statistics, and
 * thus cannot be read using the normal ice_stat_update32 function.
 *
 * Read the GLV_REPC register associated with the given VSI, and update the
 * rx_no_desc and rx_error values in the ice_eth_stats structure.
 *
 * Because the statistics in GLV_REPC stick at 0xFFFF, the register must be
 * cleared each time it's read.
 *
 * Note that the GLV_RDPC register also counts the causes that would trigger
 * GLV_REPC. However, it does not give the finer grained detail about why the
 * packets are being dropped. The GLV_REPC values can be used to distinguish
 * whether Rx packets are dropped due to errors or due to no available
 * descriptors.
 */
void
ice_stat_update_repc(struct ice_hw *hw, uint16_t vsi_handle,
    bool prev_stat_loaded, struct ice_eth_stats *cur_stats)
{
	uint16_t vsi_num, no_desc, error_cnt;
	uint32_t repc;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return;

	vsi_num = hw->vsi_ctx[vsi_handle]->vsi_num;

	/* If we haven't loaded stats yet, just clear the current value */
	if (!prev_stat_loaded) {
		ICE_WRITE(hw, GLV_REPC(vsi_num), 0);
		return;
	}

	repc = ICE_READ(hw, GLV_REPC(vsi_num));
	no_desc = (repc & GLV_REPC_NO_DESC_CNT_M) >> GLV_REPC_NO_DESC_CNT_S;
	error_cnt = (repc & GLV_REPC_ERROR_CNT_M) >> GLV_REPC_ERROR_CNT_S;

	/* Clear the count by writing to the stats register */
	ICE_WRITE(hw, GLV_REPC(vsi_num), 0);

	cur_stats->rx_no_desc += no_desc;
	cur_stats->rx_errors += error_cnt;
}

/**
 * ice_update_pf_stats - Update port stats counters
 * @sc: device private softc structure
 *
 * Reads hardware statistics registers and updates the software tracking
 * structure with new values.
 */
void
ice_update_pf_stats(struct ice_softc *sc)
{
	struct ice_hw_port_stats *prev_ps, *cur_ps;
	struct ice_hw *hw = &sc->hw;
	uint8_t lport;

	KASSERT(hw->port_info);

	prev_ps = &sc->stats.prev;
	cur_ps = &sc->stats.cur;
	lport = hw->port_info->lport;

#define ICE_PF_STAT_PFC(name, location, index) \
	ice_stat_update40(hw, name(lport, index), \
			  sc->stats.offsets_loaded, \
			  &prev_ps->location[index], &cur_ps->location[index])

#define ICE_PF_STAT40(name, location) \
	ice_stat_update40(hw, name ## L(lport), \
			  sc->stats.offsets_loaded, \
			  &prev_ps->location, &cur_ps->location)

#define ICE_PF_STAT32(name, location) \
	ice_stat_update32(hw, name(lport), \
			  sc->stats.offsets_loaded, \
			  &prev_ps->location, &cur_ps->location)

	ICE_PF_STAT40(GLPRT_GORC, eth.rx_bytes);
	ICE_PF_STAT40(GLPRT_UPRC, eth.rx_unicast);
	ICE_PF_STAT40(GLPRT_MPRC, eth.rx_multicast);
	ICE_PF_STAT40(GLPRT_BPRC, eth.rx_broadcast);
	ICE_PF_STAT40(GLPRT_GOTC, eth.tx_bytes);
	ICE_PF_STAT40(GLPRT_UPTC, eth.tx_unicast);
	ICE_PF_STAT40(GLPRT_MPTC, eth.tx_multicast);
	ICE_PF_STAT40(GLPRT_BPTC, eth.tx_broadcast);
	/* This stat register doesn't have an lport */
	ice_stat_update32(hw, PRTRPB_RDPC,
			  sc->stats.offsets_loaded,
			  &prev_ps->eth.rx_discards, &cur_ps->eth.rx_discards);

	ICE_PF_STAT32(GLPRT_TDOLD, tx_dropped_link_down);
	ICE_PF_STAT40(GLPRT_PRC64, rx_size_64);
	ICE_PF_STAT40(GLPRT_PRC127, rx_size_127);
	ICE_PF_STAT40(GLPRT_PRC255, rx_size_255);
	ICE_PF_STAT40(GLPRT_PRC511, rx_size_511);
	ICE_PF_STAT40(GLPRT_PRC1023, rx_size_1023);
	ICE_PF_STAT40(GLPRT_PRC1522, rx_size_1522);
	ICE_PF_STAT40(GLPRT_PRC9522, rx_size_big);
	ICE_PF_STAT40(GLPRT_PTC64, tx_size_64);
	ICE_PF_STAT40(GLPRT_PTC127, tx_size_127);
	ICE_PF_STAT40(GLPRT_PTC255, tx_size_255);
	ICE_PF_STAT40(GLPRT_PTC511, tx_size_511);
	ICE_PF_STAT40(GLPRT_PTC1023, tx_size_1023);
	ICE_PF_STAT40(GLPRT_PTC1522, tx_size_1522);
	ICE_PF_STAT40(GLPRT_PTC9522, tx_size_big);

	/* Update Priority Flow Control Stats */
	for (int i = 0; i <= GLPRT_PXOFFRXC_MAX_INDEX; i++) {
		ICE_PF_STAT_PFC(GLPRT_PXONRXC, priority_xon_rx, i);
		ICE_PF_STAT_PFC(GLPRT_PXOFFRXC, priority_xoff_rx, i);
		ICE_PF_STAT_PFC(GLPRT_PXONTXC, priority_xon_tx, i);
		ICE_PF_STAT_PFC(GLPRT_PXOFFTXC, priority_xoff_tx, i);
		ICE_PF_STAT_PFC(GLPRT_RXON2OFFCNT, priority_xon_2_xoff, i);
	}

	ICE_PF_STAT32(GLPRT_LXONRXC, link_xon_rx);
	ICE_PF_STAT32(GLPRT_LXOFFRXC, link_xoff_rx);
	ICE_PF_STAT32(GLPRT_LXONTXC, link_xon_tx);
	ICE_PF_STAT32(GLPRT_LXOFFTXC, link_xoff_tx);
	ICE_PF_STAT32(GLPRT_CRCERRS, crc_errors);
	ICE_PF_STAT32(GLPRT_ILLERRC, illegal_bytes);
	ICE_PF_STAT32(GLPRT_MLFC, mac_local_faults);
	ICE_PF_STAT32(GLPRT_MRFC, mac_remote_faults);
	ICE_PF_STAT32(GLPRT_RLEC, rx_len_errors);
	ICE_PF_STAT32(GLPRT_RUC, rx_undersize);
	ICE_PF_STAT32(GLPRT_RFC, rx_fragments);
	ICE_PF_STAT32(GLPRT_ROC, rx_oversize);
	ICE_PF_STAT32(GLPRT_RJC, rx_jabber);

#undef ICE_PF_STAT40
#undef ICE_PF_STAT32
#undef ICE_PF_STAT_PFC

	sc->stats.offsets_loaded = true;
}

/**
 * ice_update_vsi_hw_stats - Update VSI-specific ethernet statistics counters
 * @vsi: the VSI to be updated
 *
 * Reads hardware stats and updates the ice_vsi_hw_stats tracking structure with
 * the updated values.
 */
void
ice_update_vsi_hw_stats(struct ice_vsi *vsi)
{
	struct ice_eth_stats *prev_es, *cur_es;
	struct ice_hw *hw = &vsi->sc->hw;
	uint16_t vsi_num;

	if (!ice_is_vsi_valid(hw, vsi->idx))
		return;

	/* HW absolute index of a VSI */
	vsi_num = hw->vsi_ctx[vsi->idx]->vsi_num;
	prev_es = &vsi->hw_stats.prev;
	cur_es = &vsi->hw_stats.cur;

#define ICE_VSI_STAT40(name, location) \
	ice_stat_update40(hw, name ## L(vsi_num), \
			  vsi->hw_stats.offsets_loaded, \
			  &prev_es->location, &cur_es->location)

#define ICE_VSI_STAT32(name, location) \
	ice_stat_update32(hw, name(vsi_num), \
			  vsi->hw_stats.offsets_loaded, \
			  &prev_es->location, &cur_es->location)

	ICE_VSI_STAT40(GLV_GORC, rx_bytes);
	ICE_VSI_STAT40(GLV_UPRC, rx_unicast);
	ICE_VSI_STAT40(GLV_MPRC, rx_multicast);
	ICE_VSI_STAT40(GLV_BPRC, rx_broadcast);
	ICE_VSI_STAT32(GLV_RDPC, rx_discards);
	ICE_VSI_STAT40(GLV_GOTC, tx_bytes);
	ICE_VSI_STAT40(GLV_UPTC, tx_unicast);
	ICE_VSI_STAT40(GLV_MPTC, tx_multicast);
	ICE_VSI_STAT40(GLV_BPTC, tx_broadcast);
	ICE_VSI_STAT32(GLV_TEPC, tx_errors);

	ice_stat_update_repc(hw, vsi->idx, vsi->hw_stats.offsets_loaded,
			     cur_es);

#undef ICE_VSI_STAT40
#undef ICE_VSI_STAT32

	vsi->hw_stats.offsets_loaded = true;
}

void
ice_update_stats(struct ice_softc *sc)
{
	/* Do not attempt to update stats when in recovery mode */
	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE))
		return;

	/* Update device statistics */
	ice_update_pf_stats(sc);

	/* Update the primary VSI stats */
	ice_update_vsi_hw_stats(&sc->pf_vsi);
#if 0
	/* Update mirror VSI stats */
	if (sc->mirr_if && sc->mirr_if->if_attached)
		ice_update_vsi_hw_stats(sc->mirr_if->vsi);
#endif
}

/**
 * ice_if_update_admin_status - update admin status
 * @ctx: iflib ctx structure
 *
 * Called by iflib to update the admin status. For our purposes, this means
 * check the adminq, and update the link status. It's ultimately triggered by
 * our admin interrupt, or by the ice_if_timer periodically.
 *
 * @pre assumes the caller holds the iflib CTX lock
 */
void
ice_if_update_admin_status(void *arg)
{
	struct ice_softc *sc = (struct ice_softc *)arg;
	enum ice_fw_modes fw_mode;
	bool reschedule = false;
	uint16_t pending = 0;
	int s;
#if 0
	ASSERT_CTX_LOCKED(sc);
#endif
	s = splnet();

	if (ice_driver_is_detaching(sc)) {
		splx(s);
		return;
	}

	/* Check if the firmware entered recovery mode at run time */
	fw_mode = ice_get_fw_mode(&sc->hw);
	if (fw_mode == ICE_FW_MODE_REC) {
		if (!ice_testandset_state(&sc->state,
		    ICE_STATE_RECOVERY_MODE)) {
			/* If we just entered recovery mode, log a warning to
			 * the system administrator and deinit driver state
			 * that is no longer functional.
			 */
			ice_transition_recovery_mode(sc);
		}
	} else if (fw_mode == ICE_FW_MODE_ROLLBACK) {
		if (!ice_testandset_state(&sc->state,
		    ICE_STATE_ROLLBACK_MODE)) {
			/* Rollback mode isn't fatal, but we don't want to
			 * repeatedly post a message about it.
			 */
			ice_print_rollback_msg(&sc->hw);
		}
	}

	/* Handle global reset events */
	ice_handle_reset_event(sc);

	/* Handle PF reset requests */
	ice_handle_pf_reset_request(sc);

	/* Handle MDD events */
	ice_handle_mdd_event(sc);

	if (ice_test_state(&sc->state, ICE_STATE_RESET_FAILED) ||
	    ice_test_state(&sc->state, ICE_STATE_PREPARED_FOR_RESET) ||
	    ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE)) {
		/*
		 * If we know the control queues are disabled, skip processing
		 * the control queues entirely.
		 */
		;
	} else if (ice_testandclear_state(&sc->state,
	    ICE_STATE_CONTROLQ_EVENT_PENDING)) {
		ice_process_ctrlq(sc, ICE_CTL_Q_ADMIN, &pending);
		if (pending > 0)
			reschedule = true;

		ice_process_ctrlq(sc, ICE_CTL_Q_MAILBOX, &pending);
		if (pending > 0)
			reschedule = true;
	}

	/* Poll for link up */
	ice_poll_for_media_avail(sc);

	/* Check and update link status */
	ice_update_link_status(sc, false);

	/* Update statistics. */
	ice_update_stats(sc);

	/*
	 * If there are still messages to process, we need to reschedule
	 * ourselves. Otherwise, we can just re-enable the interrupt. We'll be
	 * woken up at the next interrupt or timer event.
	 */
	if (reschedule) {
		ice_set_state(&sc->state, ICE_STATE_CONTROLQ_EVENT_PENDING);
		task_add(systq, &sc->sc_admin_task);
	} else {
		ice_enable_intr(&sc->hw, 0);
	}

	splx(s);
}


/**
 * ice_admin_timer - called periodically to trigger the admin task
 * @arg: timeout(9) argument pointing to the device private softc structure
 *
 * Timer function used as part of a timeout(9) timer that will periodically
 * trigger the admin task, even when the interface is down.
 *
 * @remark because this is a timeout function, it cannot sleep and should not
 * attempt taking the iflib CTX lock.
 */
void
ice_admin_timer(void *arg)
{
	struct ice_softc *sc = (struct ice_softc *)arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int s;

	s = splnet();

	if (ice_driver_is_detaching(sc) ||
	    (ifp->if_flags & IFF_RUNNING) == 0) {
		splx(s);
		return;
	}

	/* Fire off the admin task */
	task_add(systq, &sc->sc_admin_task);

	/* Reschedule the admin timer */
	timeout_add_nsec(&sc->sc_admin_timer, SEC_TO_NSEC(1));

	splx(s);
}

/**
 * @enum hmc_error_type
 * @brief enumeration of HMC errors
 *
 * Enumeration defining the possible HMC errors that might occur.
 */
enum hmc_error_type {
	HMC_ERR_PMF_INVALID = 0,
	HMC_ERR_VF_IDX_INVALID = 1,
	HMC_ERR_VF_PARENT_PF_INVALID = 2,
	/* 3 is reserved */
	HMC_ERR_INDEX_TOO_BIG = 4,
	HMC_ERR_ADDRESS_TOO_LARGE = 5,
	HMC_ERR_SEGMENT_DESC_INVALID = 6,
	HMC_ERR_SEGMENT_DESC_TOO_SMALL = 7,
	HMC_ERR_PAGE_DESC_INVALID = 8,
	HMC_ERR_UNSUPPORTED_REQUEST_COMPLETION = 9,
	/* 10 is reserved */
	HMC_ERR_INVALID_OBJECT_TYPE = 11,
	/* 12 is reserved */
};

/**
 * ice_log_hmc_error - Log an HMC error message
 * @hw: device hw structure
 * @dev: the device to pass to device_printf()
 *
 * Log a message when an HMC error interrupt is triggered.
 */
void
ice_log_hmc_error(struct ice_hw *hw)
{
	struct ice_softc *sc = hw->hw_sc;
	uint32_t info, data;
	uint8_t index, errtype, objtype;
	bool isvf;

	info = ICE_READ(hw, PFHMC_ERRORINFO);
	data = ICE_READ(hw, PFHMC_ERRORDATA);

	index = (uint8_t)(info & PFHMC_ERRORINFO_PMF_INDEX_M);
	errtype = (uint8_t)((info & PFHMC_ERRORINFO_HMC_ERROR_TYPE_M) >>
		       PFHMC_ERRORINFO_HMC_ERROR_TYPE_S);
	objtype = (uint8_t)((info & PFHMC_ERRORINFO_HMC_OBJECT_TYPE_M) >>
		       PFHMC_ERRORINFO_HMC_OBJECT_TYPE_S);

	isvf = info & PFHMC_ERRORINFO_PMF_ISVF_M;

	printf("%s: %s HMC Error detected on PMF index %d: "
	    "error type %d, object type %d, data 0x%08x\n",
	    sc->sc_dev.dv_xname, isvf ? "VF" : "PF", index,
	    errtype, objtype, data);

	switch (errtype) {
	case HMC_ERR_PMF_INVALID:
		DPRINTF("Private Memory Function is not valid\n");
		break;
	case HMC_ERR_VF_IDX_INVALID:
		DPRINTF("Invalid Private Memory Function index for PE enabled VF\n");
		break;
	case HMC_ERR_VF_PARENT_PF_INVALID:
		DPRINTF("Invalid parent PF for PE enabled VF\n");
		break;
	case HMC_ERR_INDEX_TOO_BIG:
		DPRINTF("Object index too big\n");
		break;
	case HMC_ERR_ADDRESS_TOO_LARGE:
		DPRINTF("Address extends beyond segment descriptor limit\n");
		break;
	case HMC_ERR_SEGMENT_DESC_INVALID:
		DPRINTF("Segment descriptor is invalid\n");
		break;
	case HMC_ERR_SEGMENT_DESC_TOO_SMALL:
		DPRINTF("Segment descriptor is too small\n");
		break;
	case HMC_ERR_PAGE_DESC_INVALID:
		DPRINTF("Page descriptor is invalid\n");
		break;
	case HMC_ERR_UNSUPPORTED_REQUEST_COMPLETION:
		DPRINTF("Unsupported Request completion received from PCIe\n");
		break;
	case HMC_ERR_INVALID_OBJECT_TYPE:
		DPRINTF("Invalid object type\n");
		break;
	default:
		DPRINTF("Unknown HMC error\n");
	}

	/* Clear the error indication */
	ICE_WRITE(hw, PFHMC_ERRORINFO, 0);
}

/**
 * Interrupt handler for MSI-X admin interrupt
 */
int
ice_intr0(void *xsc)
{
	struct ice_softc *sc = (struct ice_softc *)xsc;
	struct ice_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t oicr;

	/* There is no safe way to modify the enabled miscellaneous causes of
	 * the OICR vector at runtime, as doing so would be prone to race
	 * conditions. Reading PFINT_OICR will unmask the associated interrupt
	 * causes and allow future interrupts to occur. The admin interrupt
	 * vector will not be re-enabled until after we exit this function,
	 * but any delayed tasks must be resilient against possible "late
	 * arrival" interrupts that occur while we're already handling the
	 * task. This is done by using state bits and serializing these
	 * delayed tasks via the admin status task function.
	 */
	oicr = ICE_READ(hw, PFINT_OICR);

	/* Processing multiple controlq interrupts on a single vector does not
	 * provide an indication of which controlq triggered the interrupt.
	 * We might try reading the INTEVENT bit of the respective PFINT_*_CTL
	 * registers. However, the INTEVENT bit is not guaranteed to be set as
	 * it gets automatically cleared when the hardware acknowledges the
	 * interrupt.
	 *
	 * This means we don't really have a good indication of whether or
	 * which controlq triggered this interrupt. We'll just notify the
	 * admin task that it should check all the controlqs.
	 */
	ice_set_state(&sc->state, ICE_STATE_CONTROLQ_EVENT_PENDING);

	if (oicr & PFINT_OICR_VFLR_M) {
		ice_set_state(&sc->state, ICE_STATE_VFLR_PENDING);
	}

	if (oicr & PFINT_OICR_MAL_DETECT_M) {
		ice_set_state(&sc->state, ICE_STATE_MDD_PENDING);
	}

	if (oicr & PFINT_OICR_GRST_M) {
		uint32_t reset;

		reset = (ICE_READ(hw, GLGEN_RSTAT) &
		    GLGEN_RSTAT_RESET_TYPE_M) >> GLGEN_RSTAT_RESET_TYPE_S;
#if 0
		if (reset == ICE_RESET_CORER)
			sc->soft_stats.corer_count++;
		else if (reset == ICE_RESET_GLOBR)
			sc->soft_stats.globr_count++;
		else
			sc->soft_stats.empr_count++;
#endif
		/* There are a couple of bits at play for handling resets.
		 * First, the ICE_STATE_RESET_OICR_RECV bit is used to
		 * indicate that the driver has received an OICR with a reset
		 * bit active, indicating that a CORER/GLOBR/EMPR is about to
		 * happen. Second, we set hw->reset_ongoing to indicate that
		 * the hardware is in reset. We will set this back to false as
		 * soon as the driver has determined that the hardware is out
		 * of reset.
		 *
		 * If the driver wishes to trigger a request, it can set one of
		 * the ICE_STATE_RESET_*_REQ bits, which will trigger the
		 * correct type of reset.
		 */
		if (!ice_testandset_state(&sc->state,
		    ICE_STATE_RESET_OICR_RECV)) {
			hw->reset_ongoing = true;
			/*
			 * During the NVM update process, there is a driver
			 * reset and link goes down and then up. The below
			 * if-statement prevents a second link flap from
			 * occurring in ice_up().
			 */
			if (ifp->if_flags & IFF_UP) {
				ice_set_state(&sc->state,
				    ICE_STATE_FIRST_INIT_LINK);
			}
		}
	}

	if (oicr & PFINT_OICR_ECC_ERR_M) {
		DPRINTF("%s: ECC Error detected!\n", sc->sc_dev.dv_xname);
		ice_set_state(&sc->state, ICE_STATE_RESET_PFR_REQ);
	}

	if (oicr & (PFINT_OICR_PE_CRITERR_M | PFINT_OICR_HMC_ERR_M)) {
		if (oicr & PFINT_OICR_HMC_ERR_M)
			/* Log the HMC errors */
			ice_log_hmc_error(hw);
#if 0
		ice_rdma_notify_pe_intr(sc, oicr);
#endif
	}

	if (oicr & PFINT_OICR_PCI_EXCEPTION_M) {
		DPRINTF("%s: PCI Exception detected!\n", sc->sc_dev.dv_xname);
		ice_set_state(&sc->state, ICE_STATE_RESET_PFR_REQ);
	}

	task_add(systq, &sc->sc_admin_task);
	return 1;
}

/*
 * Macro to help extract the NIC mode flexible Rx descriptor fields from the
 * advanced 32byte Rx descriptors.
 */
#define ICE_RX_FLEX_NIC(desc, field) \
	(((struct ice_32b_rx_flex_desc_nic *)desc)->field)

/**
 * ice_rx_checksum - verify hardware checksum is valid or not
 * @status0: descriptor status data
 * @ptype: packet type
 *
 * Determine whether the hardware indicated that the Rx checksum is valid. If
 * so, update the checksum flags and data, informing the stack of the status
 * of the checksum so that it does not spend time verifying it manually.
 */
void
ice_rx_checksum(struct mbuf *m, uint16_t status0, uint16_t ptype)
{
	const uint16_t l3_error = (BIT(ICE_RX_FLEX_DESC_STATUS0_XSUM_IPE_S) |
	    BIT(ICE_RX_FLEX_DESC_STATUS0_XSUM_EIPE_S));
	const uint16_t l4_error = (BIT(ICE_RX_FLEX_DESC_STATUS0_XSUM_L4E_S) |
	    BIT(ICE_RX_FLEX_DESC_STATUS0_XSUM_EUDPE_S));
	const uint16_t xsum_errors = (l3_error | l4_error |
	    BIT(ICE_RX_FLEX_DESC_STATUS0_IPV6EXADD_S));
	struct ice_rx_ptype_decoded decoded;
	int is_ipv4, is_ipv6;

	/* No L3 or L4 checksum was calculated */
	if (!(status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_L3L4P_S)))
		return;

	decoded = ice_decode_rx_desc_ptype(ptype);

	if (!(decoded.known && decoded.outer_ip))
		return;

	is_ipv4 = (decoded.outer_ip == ICE_RX_PTYPE_OUTER_IP) &&
	    (decoded.outer_ip_ver == ICE_RX_PTYPE_OUTER_IPV4);
	is_ipv6 = (decoded.outer_ip == ICE_RX_PTYPE_OUTER_IP) &&
	    (decoded.outer_ip_ver == ICE_RX_PTYPE_OUTER_IPV6);

	/* No checksum errors were reported */
	if (!(status0 & xsum_errors)) {
		if (is_ipv4)
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

		switch (decoded.inner_prot) {
		case ICE_RX_PTYPE_INNER_PROT_TCP:
			m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
			break;
		case ICE_RX_PTYPE_INNER_PROT_UDP:
			m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_OK;
			break;
		default:
			break;
		}

		return;
	}

	/*
	 * Certain IPv6 extension headers impact the validity of L4 checksums.
	 * If one of these headers exist, hardware will set the IPV6EXADD bit
	 * in the descriptor. If the bit is set then pretend like hardware
	 * didn't checksum this packet.
	 */
	if (is_ipv6 && (status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_IPV6EXADD_S)))
		return;

	/*
	 * At this point, status0 must have at least one of the l3_error or
	 * l4_error bits set.
	 */
	if (status0 & l3_error) {
		if (is_ipv4)
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_BAD;

		/* don't bother reporting L4 errors if we got an L3 error */
		return;
	} else if (is_ipv4)
		m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

	if (status0 & l4_error) {
		switch (decoded.inner_prot) {
		case ICE_RX_PTYPE_INNER_PROT_TCP:
			m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_BAD;
			break;
		case ICE_RX_PTYPE_INNER_PROT_UDP:
			m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_BAD;
			break;
		default:
			break;
		}
	}
}

int
ice_rxeof(struct ice_softc *sc, struct ice_rx_queue *rxq)
{
	struct ifiqueue *ifiq = rxq->rxq_ifiq;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	union ice_32b_rx_flex_desc *ring, *cur;
	struct ice_rx_map *rxm;
	bus_dmamap_t map;
	unsigned int cons, prod;
	struct mbuf_list mltcp = MBUF_LIST_INITIALIZER();
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	uint16_t status0, ptype;
	unsigned int eop;
	unsigned int len;
	unsigned int mask;
	int done = 0;

	prod = rxq->rxq_prod;
	cons = rxq->rxq_cons;

	if (cons == prod)
		return (0);

	rxm = &rxq->rx_map[cons];
	map = rxm->rxm_map;
	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	ring = ICE_DMA_KVA(&rxq->rx_desc_mem);
	mask = rxq->desc_count - 1;

	do {
		cur = &ring[cons];

		status0 = le16toh(cur->wb.status_error0);
		if ((status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_DD_S)) == 0)
			break;

		if_rxr_put(&rxq->rxq_acct, 1);

		rxm = &rxq->rx_map[cons];

		map = rxm->rxm_map;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, map);
		
		m = rxm->rxm_m;
		rxm->rxm_m = NULL;

		len = le16toh(cur->wb.pkt_len) & ICE_RX_FLX_DESC_PKT_LEN_M;
		m->m_len = len;
		m->m_pkthdr.len = 0;

		m->m_next = NULL;
		*rxq->rxq_m_tail = m;
		rxq->rxq_m_tail = &m->m_next;

		m = rxq->rxq_m_head;
		m->m_pkthdr.len += len;

		eop = !!(status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_EOF_S));
		if (eop && (status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_RXE_S))) {
			/*
			 * Make sure packets with bad L2 values are discarded.
			 * This bit is only valid in the last descriptor.
			 */
			ifp->if_ierrors++;
			m_freem(m);
			m = NULL;
			rxq->rxq_m_head = NULL;
			rxq->rxq_m_tail = &rxq->rxq_m_head;
		} else if (eop) {
#if NVLAN > 0
			if (status0 & BIT(ICE_RX_FLEX_DESC_STATUS0_L2TAG1P_S)) {
				m->m_pkthdr.ether_vtag =
				    le16toh(cur->wb.l2tag1);
				SET(m->m_flags, M_VLANTAG);
			}
#endif
			if (status0 &
			    BIT(ICE_RX_FLEX_DESC_STATUS0_RSS_VALID_S)) {
				m->m_pkthdr.ph_flowid = le32toh(
				    ICE_RX_FLEX_NIC(&cur->wb, rss_hash));
				m->m_pkthdr.csum_flags |= M_FLOWID;
			}

			/* Get packet type and set checksum flags */
			ptype = le16toh(cur->wb.ptype_flex_flags0) &
				ICE_RX_FLEX_DESC_PTYPE_M;
			ice_rx_checksum(m, status0, ptype);

#ifndef SMALL_KERNEL
			if (ISSET(ifp->if_xflags, IFXF_LRO) &&
			    (ptype == ICE_RX_FLEX_DECS_PTYPE_MAC_IPV4_TCP ||
			     ptype == ICE_RX_FLEX_DECS_PTYPE_MAC_IPV6_TCP))
				tcp_softlro_glue(&mltcp, m, ifp);
			else
#endif
				ml_enqueue(&ml, m);

			rxq->rxq_m_head = NULL;
			rxq->rxq_m_tail = &rxq->rxq_m_head;
		}

		cons++;
		cons &= mask;

		done = 1;
	} while (cons != prod);

	if (done) {
		int livelocked = 0;

		rxq->rxq_cons = cons;
		if (ifiq_input(ifiq, &mltcp))
			livelocked = 1;
		if (ifiq_input(ifiq, &ml))
			livelocked = 1;

		if (livelocked)
			if_rxr_livelocked(&rxq->rxq_acct);
		ice_rxfill(sc, rxq);
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	return (done);
}

int
ice_txeof(struct ice_softc *sc, struct ice_tx_queue *txq)
{
	struct ifqueue *ifq = txq->txq_ifq;
	struct ice_tx_desc *ring, *txd;
	struct ice_tx_map *txm;
	bus_dmamap_t map;
	unsigned int cons, prod, last;
	unsigned int mask;
	uint64_t dtype;
	int done = 0;

	prod = txq->txq_prod;
	cons = txq->txq_cons;

	if (cons == prod)
		return (0);

	bus_dmamap_sync(sc->sc_dmat, ICE_DMA_MAP(&txq->tx_desc_mem),
	    0, ICE_DMA_LEN(&txq->tx_desc_mem), BUS_DMASYNC_POSTREAD);

	ring = ICE_DMA_KVA(&txq->tx_desc_mem);
	mask = txq->desc_count - 1;

	do {
		txm = &txq->tx_map[cons];
		last = txm->txm_eop;
		txd = &ring[last];

		dtype = htole64((txd->cmd_type_offset_bsz &
		    ICE_TXD_QW1_DTYPE_M) >> ICE_TXD_QW1_DTYPE_S);
		if (dtype != htole64(ICE_TX_DESC_DTYPE_DESC_DONE))
			break;

		if (ISSET(txm->txm_m->m_pkthdr.csum_flags, M_TCP_TSO))
			map = txm->txm_map_tso;
		else
			map = txm->txm_map;

		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, map);
		m_freem(txm->txm_m);

		txm->txm_m = NULL;
		txm->txm_eop = -1;

		cons = last + 1;
		cons &= mask;

		done = 1;
	} while (cons != prod);

	bus_dmamap_sync(sc->sc_dmat, ICE_DMA_MAP(&txq->tx_desc_mem),
	    0, ICE_DMA_LEN(&txq->tx_desc_mem), BUS_DMASYNC_PREREAD);

	txq->txq_cons = cons;

	//ixl_enable(sc, txr->txr_msix);

	if (done && ifq_is_oactive(ifq))
		ifq_restart(ifq);

	return (done);
}

int
ice_intr_vector(void *ivp)
{
	struct ice_intr_vector *iv = ivp;
	struct ice_softc *sc = iv->iv_sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int rv = 0, v = iv->iv_qid + 1;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		rv |= ice_rxeof(sc, iv->iv_rxq);
		rv |= ice_txeof(sc, iv->iv_txq);
	}

	/* Wake threads waiting for software interrupt confirmation. */
	if (sc->sw_intr[v] == -1) {
		sc->sw_intr[v] = 1;
		wakeup(&sc->sw_intr[v]);
	}
		
	ice_enable_intr(&sc->hw, v);
	return rv;
}

/**
 * ice_allocate_msix - Allocate MSI-X vectors for the interface
 * @sc: the device private softc
 *
 * @returns zero on success or an error code on failure.
 */
int
ice_allocate_msix(struct ice_softc *sc)
{
	int err, i;

	sc->sc_ihc = pci_intr_establish(sc->sc_pct, sc->sc_ih,
	    IPL_NET | IPL_MPSAFE, ice_intr0, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ihc == NULL) {
		printf("%s: unable to establish interrupt handler\n",
		    sc->sc_dev.dv_xname);
		return ENOTRECOVERABLE;
	}

	if (sc->sc_intrmap) {
		for (i = 0; i < sc->sc_nqueues; i++) {
			struct ice_intr_vector *iv = &sc->sc_vectors[i];
			int v = i + 1; /* 0 is used for adminq */

			iv->iv_sc = sc;
			iv->iv_qid = i;
			iv->iv_ihc = pci_intr_establish_cpu(sc->sc_pct, iv->ih,
			    IPL_NET | IPL_MPSAFE,
			    intrmap_cpu(sc->sc_intrmap, i),
			    ice_intr_vector, iv, iv->iv_name);
			if (iv->iv_ihc == NULL) {
				printf("%s: unable to establish interrupt %d\n",
				    sc->sc_dev.dv_xname, v);
				err = ENOTRECOVERABLE;
				goto disestablish;
			}
		}
	}

	return 0;

disestablish:
	if (sc->sc_intrmap != NULL) {
		for (i = 0; i < sc->sc_nqueues; i++) {
			struct ice_intr_vector *iv = &sc->sc_vectors[i];
			if (iv->iv_ihc == NULL)
				continue;
			pci_intr_disestablish(sc->sc_pct, iv->iv_ihc);
		}
	}
	pci_intr_disestablish(sc->sc_pct, sc->sc_ihc);
	sc->sc_ihc = NULL;
	return err;
}

void
ice_free_tx_queues(struct ice_softc *sc)
{
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_tx_queue *txq;
	struct ice_tx_map *map;
	int i, j;

	for (i = 0, txq = vsi->tx_queues; i < sc->sc_nqueues; i++, txq++) {
		ice_free_dma_mem(&sc->hw, &txq->tx_desc_mem);
		for (j = 0; j < txq->desc_count; j++) {
			map = &txq->tx_map[j];
			if (map->txm_map != NULL) {
				bus_dmamap_destroy(sc->sc_dmat, map->txm_map);
				map->txm_map = NULL;
			}
			if (map->txm_map_tso != NULL) {
				bus_dmamap_destroy(sc->sc_dmat,
				    map->txm_map_tso);
				map->txm_map_tso = NULL;
			}
		}
		free(txq->tx_map, M_DEVBUF, txq->desc_count * sizeof(*map));
		txq->tx_map = NULL;
		if (txq->tx_rsq != NULL) {
			free(txq->tx_rsq, M_DEVBUF,
			    sc->isc_ntxd[0] * sizeof(uint16_t));
			txq->tx_rsq = NULL;
		}
	}

	free(vsi->tx_queues, M_DEVBUF,
	    sc->sc_nqueues * sizeof(struct ice_tx_queue));
	vsi->tx_queues = NULL;
}

/* ice_tx_queues_alloc - Allocate Tx queue memory */
int
ice_tx_queues_alloc(struct ice_softc *sc)
{
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_tx_queue *txq;
	int err, i, j;

	KASSERT(sc->isc_ntxd[0] <= ICE_MAX_DESC_COUNT);
#if 0
	ASSERT_CTX_LOCKED(sc);
#endif
	/* Do not bother allocating queues if we're in recovery mode */
	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE))
		return (0);

	/* Allocate queue structure memory */
	if (!(vsi->tx_queues =
	      (struct ice_tx_queue *) mallocarray(sc->sc_nqueues,
	          sizeof(struct ice_tx_queue), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate Tx queue memory\n",
		    sc->sc_dev.dv_xname);
		return (ENOMEM);
	}

	/* Allocate Tx descriptor memory */
	for (i = 0, txq = vsi->tx_queues; i < sc->sc_nqueues; i++, txq++) {
		txq->tx_base = ice_alloc_dma_mem(&sc->hw, &txq->tx_desc_mem,
		    sc->isc_ntxd[i] * sizeof(struct ice_tx_desc));
		if (txq->tx_base == NULL) {
			printf("%s: Unable to allocate Tx descriptor memory\n",
			    sc->sc_dev.dv_xname);
			err = ENOMEM;
			goto free_tx_queues;
		}
		txq->tx_paddr = txq->tx_desc_mem.pa;
	}

	/* Create Tx queue DMA maps. */
	for (i = 0, txq = vsi->tx_queues; i < sc->sc_nqueues; i++, txq++) {
		struct ice_tx_map *map;
		int j;

		txq->tx_map = mallocarray(sc->isc_ntxd[i], sizeof(*map),
		    M_DEVBUF, M_NOWAIT| M_ZERO);
		if (txq->tx_map == NULL) {
			printf("%s: could not allocate Tx DMA map\n",
			    sc->sc_dev.dv_xname);
			err = ENOMEM;
			goto free_tx_queues;
		}

		for (j = 0; j < sc->isc_ntxd[i]; j++) {
			map = &txq->tx_map[j];
			if (bus_dmamap_create(sc->sc_dmat, MAXMCLBYTES,
			    ICE_MAX_TX_SEGS, ICE_MAX_DMA_SEG_SIZE, 0,
			    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
			    &map->txm_map) != 0) {
				printf("%s: could not allocate Tx DMA map\n",
				    sc->sc_dev.dv_xname);
				err = ENOMEM;
				goto free_tx_queues;
			}

			if (bus_dmamap_create(sc->sc_dmat, MAXMCLBYTES,
			    ICE_MAX_TSO_SEGS, ICE_MAX_DMA_SEG_SIZE, 0,
			    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
			    &map->txm_map_tso) != 0) {
				printf("%s: could not allocate TSO DMA map\n",
				    sc->sc_dev.dv_xname);
				err = ENOMEM;
				goto free_tx_queues;
			}
		}
	}

	/* Allocate report status arrays */
	for (i = 0, txq = vsi->tx_queues; i < sc->sc_nqueues; i++, txq++) {
		if (!(txq->tx_rsq =
		      (uint16_t *) mallocarray(sc->isc_ntxd[0],
		          sizeof(uint16_t), M_DEVBUF, M_NOWAIT))) {
			printf("%s: Unable to allocate tx_rsq memory\n",
			    sc->sc_dev.dv_xname);
			err = ENOMEM;
			goto free_tx_queues;
		}
		/* Initialize report status array */
		for (j = 0; j < sc->isc_ntxd[i]; j++)
			txq->tx_rsq[j] = ICE_QIDX_INVALID;
	}

	/* Assign queues from PF space to the main VSI */
	err = ice_resmgr_assign_contiguous(&sc->tx_qmgr, vsi->tx_qmap,
	    sc->sc_nqueues);
	if (err) {
		printf("%s: Unable to assign PF queues: error %d\n",
		    sc->sc_dev.dv_xname, err);
		goto free_tx_queues;
	}
	vsi->qmap_type = ICE_RESMGR_ALLOC_CONTIGUOUS;
#if 0
	/* Add Tx queue sysctls context */
	ice_vsi_add_txqs_ctx(vsi);
#endif
	for (i = 0, txq = vsi->tx_queues; i < sc->sc_nqueues; i++, txq++) {
		/* q_handle == me when only one TC */
		txq->me = txq->q_handle = i;
		txq->vsi = vsi;

		/* store the queue size for easier access */
		txq->desc_count = sc->isc_ntxd[i];

		/* set doorbell address */
		txq->tail = QTX_COMM_DBELL(vsi->tx_qmap[i]);
#if 0
		ice_add_txq_sysctls(txq);
#endif

		txq->txq_cons = txq->txq_prod = 0;
	}

	vsi->num_tx_queues = sc->sc_nqueues;

	return (0);

free_tx_queues:
	ice_free_tx_queues(sc);
	return err;
}

uint32_t
ice_hardmtu(struct ice_hw *hw)
{
	return hw->port_info->phy.link_info.max_frame_size -
	    ETHER_HDR_LEN - ETHER_CRC_LEN;
}

void
ice_free_rx_queues(struct ice_softc *sc)
{
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_rx_queue *rxq;
	struct ice_rx_map *map;
	int i, j;

	for (i = 0, rxq = vsi->rx_queues; i < sc->sc_nqueues; i++, rxq++) {
		ice_free_dma_mem(&sc->hw, &rxq->rx_desc_mem);
		for (j = 0; j < rxq->desc_count; j++) {
			map = &rxq->rx_map[j];
			if (map->rxm_map != NULL) {
				bus_dmamap_destroy(sc->sc_dmat, map->rxm_map);
				map->rxm_map = NULL;
			}
		}
		free(rxq->rx_map, M_DEVBUF, rxq->desc_count * sizeof(*map));
		rxq->rx_map = NULL;
	}

	free(vsi->rx_queues, M_DEVBUF,
	    sc->sc_nqueues * sizeof(struct ice_rx_queue));
	vsi->rx_queues = NULL;
}

void
ice_rxrefill(void *arg)
{
	struct ice_rx_queue *rxq = arg;
	struct ice_softc *sc = rxq->vsi->sc;

	ice_rxfill(sc, rxq);
}

/* ice_rx_queues_alloc - Allocate Rx queue memory */
int
ice_rx_queues_alloc(struct ice_softc *sc)
{
	struct ice_vsi *vsi = &sc->pf_vsi;
	struct ice_rx_queue *rxq;
	int err, i;

	KASSERT(sc->isc_nrxd[0] <= ICE_MAX_DESC_COUNT);
#if 0
	ASSERT_CTX_LOCKED(sc);
#endif
	/* Do not bother allocating queues if we're in recovery mode */
	if (ice_test_state(&sc->state, ICE_STATE_RECOVERY_MODE))
		return (0);

	/* Allocate queue structure memory */
	if (!(vsi->rx_queues =
	      (struct ice_rx_queue *) mallocarray(sc->sc_nqueues,
	          sizeof(struct ice_rx_queue), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate Rx queue memory\n",
		    sc->sc_dev.dv_xname);
		return (ENOMEM);
	}

	/* Allocate Rx descriptor memory */
	for (i = 0, rxq = vsi->rx_queues; i < sc->sc_nqueues; i++, rxq++) {
		rxq->rx_base = ice_alloc_dma_mem(&sc->hw, &rxq->rx_desc_mem,
		    sc->isc_nrxd[i] * sizeof(union ice_32b_rx_flex_desc));
		if (rxq->rx_base == NULL) {
			printf("%s: Unable to allocate Rx descriptor memory\n",
			    sc->sc_dev.dv_xname);
			err = ENOMEM;
			goto free_rx_queues;
		}
		rxq->rx_paddr = rxq->rx_desc_mem.pa;
	}

	/* Create Rx queue DMA maps. */
	for (i = 0, rxq = vsi->rx_queues; i < sc->sc_nqueues; i++, rxq++) {
		struct ice_rx_map *map;
		int j;

		rxq->rx_map = mallocarray(sc->isc_nrxd[i], sizeof(*map),
		    M_DEVBUF, M_NOWAIT| M_ZERO);
		if (rxq->rx_map == NULL) {
			printf("%s: could not allocate Rx DMA map\n",
			    sc->sc_dev.dv_xname);
			err = ENOMEM;
			goto free_rx_queues;
		}

		for (j = 0; j < sc->isc_nrxd[i]; j++) {
			map = &rxq->rx_map[j];
			if (bus_dmamap_create(sc->sc_dmat, vsi->mbuf_sz, 1,
			    vsi->mbuf_sz, 0,
			    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
			    &map->rxm_map) != 0) {
				printf("%s: could not allocate Rx DMA map\n",
				    sc->sc_dev.dv_xname);
				err = ENOMEM;
				goto free_rx_queues;
			}
		}
	}

	/* Assign queues from PF space to the main VSI */
	err = ice_resmgr_assign_contiguous(&sc->rx_qmgr, vsi->rx_qmap,
	    sc->sc_nqueues);
	if (err) {
		printf("%s: Unable to assign PF queues: error %d\n",
		    sc->sc_dev.dv_xname, err);
		goto free_rx_queues;
	}
	vsi->qmap_type = ICE_RESMGR_ALLOC_CONTIGUOUS;
#if 0
	/* Add Rx queue sysctls context */
	ice_vsi_add_rxqs_ctx(vsi);
#endif
	for (i = 0, rxq = vsi->rx_queues; i < sc->sc_nqueues; i++, rxq++) {
		rxq->me = i;
		rxq->vsi = vsi;

		/* store the queue size for easier access */
		rxq->desc_count = sc->isc_nrxd[i];

		/* set tail address */
		rxq->tail = QRX_TAIL(vsi->rx_qmap[i]);
#if 0
		ice_add_rxq_sysctls(rxq);
#endif
		if_rxr_init(&rxq->rxq_acct, ICE_MIN_DESC_COUNT,
		    rxq->desc_count - 1);
		timeout_set(&rxq->rxq_refill, ice_rxrefill, rxq);

		rxq->rxq_cons = rxq->rxq_prod = 0;
		rxq->rxq_m_head = NULL;
		rxq->rxq_m_tail = &rxq->rxq_m_head;
	}

	vsi->num_rx_queues = sc->sc_nqueues;

	return (0);

free_rx_queues:
	ice_free_rx_queues(sc);
	return err;
}

/**
 * ice_aq_start_stop_dcbx - Start/Stop DCBX service in FW
 * @hw: pointer to the HW struct
 * @start_dcbx_agent: True if DCBX Agent needs to be started
 *		      False if DCBX Agent needs to be stopped
 * @dcbx_agent_status: FW indicates back the DCBX agent status
 *		       True if DCBX Agent is active
 *		       False if DCBX Agent is stopped
 * @cd: pointer to command details structure or NULL
 *
 * Start/Stop the embedded dcbx Agent. In case that this wrapper function
 * returns ICE_SUCCESS, caller will need to check if FW returns back the same
 * value as stated in dcbx_agent_status, and react accordingly. (0x0A09)
 */
enum ice_status
ice_aq_start_stop_dcbx(struct ice_hw *hw, bool start_dcbx_agent,
		       bool *dcbx_agent_status, struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_stop_start_specific_agent *cmd;
	enum ice_adminq_opc opcode;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.lldp_agent_ctrl;

	opcode = ice_aqc_opc_lldp_stop_start_specific_agent;

	ice_fill_dflt_direct_cmd_desc(&desc, opcode);

	if (start_dcbx_agent)
		cmd->command = ICE_AQC_START_STOP_AGENT_START_DCBX;

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	*dcbx_agent_status = false;

	if (status == ICE_SUCCESS &&
	    cmd->command == ICE_AQC_START_STOP_AGENT_START_DCBX)
		*dcbx_agent_status = true;

	return status;
}

/**
 * ice_get_dcbx_status
 * @hw: pointer to the HW struct
 *
 * Get the DCBX status from the Firmware
 */
uint8_t
ice_get_dcbx_status(struct ice_hw *hw)
{
	uint32_t reg;

	reg = ICE_READ(hw, PRTDCB_GENS);
	return (uint8_t)((reg & PRTDCB_GENS_DCBX_STATUS_M) >>
		    PRTDCB_GENS_DCBX_STATUS_S);
}

/**
 * ice_start_dcbx_agent - Start DCBX agent in FW via AQ command
 * @sc: the device softc
 *
 * @pre device is DCB capable and the FW LLDP agent has started
 *
 * Checks DCBX status and starts the DCBX agent if it is not in
 * a valid state via an AQ command.
 */
void
ice_start_dcbx_agent(struct ice_softc *sc)
{
	struct ice_hw *hw = &sc->hw;
	bool dcbx_agent_status;
	enum ice_status status;

	hw->port_info->qos_cfg.dcbx_status = ice_get_dcbx_status(hw);

	if (hw->port_info->qos_cfg.dcbx_status != ICE_DCBX_STATUS_DONE &&
	    hw->port_info->qos_cfg.dcbx_status != ICE_DCBX_STATUS_IN_PROGRESS) {
		/*
		 * Start DCBX agent, but not LLDP. The return value isn't
		 * checked here because a more detailed dcbx agent status is
		 * retrieved and checked in ice_init_dcb() and elsewhere.
		 */
		status = ice_aq_start_stop_dcbx(hw, true, &dcbx_agent_status,
		    NULL);
		if (status && hw->adminq.sq_last_status != ICE_AQ_RC_EPERM)
			printf("%s: start_stop_dcbx failed, err %s aq_err %s\n",
			    sc->sc_dev.dv_xname, ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
	}
}

/**
 * ice_aq_cfg_lldp_mib_change
 * @hw: pointer to the HW struct
 * @ena_update: Enable or Disable event posting
 * @cd: pointer to command details structure or NULL
 *
 * Enable or Disable posting of an event on ARQ when LLDP MIB
 * associated with the interface changes (0x0A01)
 */
enum ice_status
ice_aq_cfg_lldp_mib_change(struct ice_hw *hw, bool ena_update,
			   struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_set_mib_change *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.lldp_set_event;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_lldp_set_mib_change);

	if (!ena_update)
		cmd->command |= ICE_AQ_LLDP_MIB_UPDATE_DIS;
	else
		cmd->command |= ICE_AQ_LLDP_MIB_PENDING_ENABLE <<
				ICE_AQ_LLDP_MIB_PENDING_S;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_init_dcb
 * @hw: pointer to the HW struct
 * @enable_mib_change: enable MIB change event
 *
 * Update DCB configuration from the Firmware
 */
enum ice_status
ice_init_dcb(struct ice_hw *hw, bool enable_mib_change)
{
	struct ice_qos_cfg *qos_cfg = &hw->port_info->qos_cfg;
	enum ice_status ret = ICE_SUCCESS;

	if (!hw->func_caps.common_cap.dcb)
		return ICE_ERR_NOT_SUPPORTED;

	qos_cfg->is_sw_lldp = true;

	/* Get DCBX status */
	qos_cfg->dcbx_status = ice_get_dcbx_status(hw);

	if (qos_cfg->dcbx_status == ICE_DCBX_STATUS_DONE ||
	    qos_cfg->dcbx_status == ICE_DCBX_STATUS_IN_PROGRESS ||
	    qos_cfg->dcbx_status == ICE_DCBX_STATUS_NOT_STARTED) {
		/* Get current DCBX configuration */
		ret = ice_get_dcb_cfg(hw->port_info);
		if (ret)
			return ret;
		qos_cfg->is_sw_lldp = false;
	} else if (qos_cfg->dcbx_status == ICE_DCBX_STATUS_DIS) {
		return ICE_ERR_NOT_READY;
	}

	/* Configure the LLDP MIB change event */
	if (enable_mib_change) {
		ret = ice_aq_cfg_lldp_mib_change(hw, true, NULL);
		if (ret)
			qos_cfg->is_sw_lldp = true;
	}

	return ret;
}

/**
 * ice_aq_query_pfc_mode - Query PFC mode
 * @hw: pointer to the HW struct
 * @pfcmode_ret: Return PFC mode
 * @cd: pointer to command details structure or NULL
 *
 * This will return an indication if DSCP-based PFC or VLAN-based PFC
 * is enabled. (0x0302)
 */
enum ice_status
ice_aq_query_pfc_mode(struct ice_hw *hw, uint8_t *pfcmode_ret,
    struct ice_sq_cd *cd)
{
	struct ice_aqc_set_query_pfc_mode *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.set_query_pfc_mode;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_query_pfc_mode);

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	if (!status)
		*pfcmode_ret = cmd->pfc_mode;

	return status;
}

/**
 * ice_init_dcb_setup - Initialize DCB settings for HW
 * @sc: the device softc
 *
 * This needs to be called after the fw_lldp_agent sysctl is added, since that
 * can update the device's LLDP agent status if a tunable value is set.
 *
 * Get and store the initial state of DCB settings on driver load. Print out
 * informational messages as well.
 */
void
ice_init_dcb_setup(struct ice_softc *sc)
{
	struct ice_dcbx_cfg *local_dcbx_cfg;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;
	uint8_t pfcmode_ret;

	/* Don't do anything if DCB isn't supported */
	if (!ice_is_bit_set(sc->feat_cap, ICE_FEATURE_DCB)) {
		DPRINTF("%s: No DCB support\n", __func__);
		return;
	}

	/* Starts DCBX agent if it needs starting */
	ice_start_dcbx_agent(sc);

	/* This sets hw->port_info->qos_cfg.is_sw_lldp */
	status = ice_init_dcb(hw, true);

	/* If there is an error, then FW LLDP is not in a usable state */
	if (status != 0 && status != ICE_ERR_NOT_READY) {
		/* Don't print an error message if the return code from the AQ
		 * cmd performed in ice_init_dcb() is EPERM; that means the
		 * FW LLDP engine is disabled, and that is a valid state.
		 */
		if (!(status == ICE_ERR_AQ_ERROR &&
		      hw->adminq.sq_last_status == ICE_AQ_RC_EPERM)) {
			printf("%s: DCB init failed, err %s aq_err %s\n",
			    sc->sc_dev.dv_xname, ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
		}
		hw->port_info->qos_cfg.dcbx_status = ICE_DCBX_STATUS_NOT_STARTED;
	}

	switch (hw->port_info->qos_cfg.dcbx_status) {
	case ICE_DCBX_STATUS_DIS:
		DNPRINTF(ICE_DBG_DCB, "%s: DCBX disabled\n", __func__);
		break;
	case ICE_DCBX_STATUS_NOT_STARTED:
		DNPRINTF(ICE_DBG_DCB, "%s: DCBX not started\n", __func__);
		break;
	case ICE_DCBX_STATUS_MULTIPLE_PEERS:
		DNPRINTF(ICE_DBG_DCB, "%s: DCBX detected multiple peers\n",
		    __func__);
		break;
	default:
		break;
	}

	/* LLDP disabled in FW */
	if (hw->port_info->qos_cfg.is_sw_lldp) {
		ice_add_rx_lldp_filter(sc);
		DPRINTF("%s: Firmware LLDP agent disabled\n", __func__);
	}

	/* Query and cache PFC mode */
	status = ice_aq_query_pfc_mode(hw, &pfcmode_ret, NULL);
	if (status) {
		printf("%s: PFC mode query failed, err %s aq_err %s\n",
		    sc->sc_dev.dv_xname, ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
	}
	local_dcbx_cfg = &hw->port_info->qos_cfg.local_dcbx_cfg;
	switch (pfcmode_ret) {
	case ICE_AQC_PFC_VLAN_BASED_PFC:
		local_dcbx_cfg->pfc_mode = ICE_QOS_MODE_VLAN;
		break;
	case ICE_AQC_PFC_DSCP_BASED_PFC:
		local_dcbx_cfg->pfc_mode = ICE_QOS_MODE_DSCP;
		break;
	default:
		/* DCB is disabled, but we shouldn't get here */
		break;
	}

	/* Set default SW MIB for init */
	ice_set_default_local_mib_settings(sc);

	ice_set_bit(ICE_FEATURE_DCB, sc->feat_en);
}

/**
 * ice_set_link_management_mode -- Strict or lenient link management
 * @sc: device private structure
 *
 * Some NVMs give the adapter the option to advertise a superset of link
 * configurations.  This checks to see if that option is enabled.
 * Further, the NVM could also provide a specific set of configurations
 * to try; these are cached in the driver's private structure if they
 * are available.
 */
void
ice_set_link_management_mode(struct ice_softc *sc)
{
	struct ice_port_info *pi = sc->hw.port_info;
	struct ice_link_default_override_tlv tlv = { 0 };
	enum ice_status status;

	/* Port must be in strict mode if FW version is below a certain
	 * version. (i.e. Don't set lenient mode features)
	 */
	if (!(ice_fw_supports_link_override(&sc->hw)))
		return;

	status = ice_get_link_default_override(&tlv, pi);
	if (status != ICE_SUCCESS) {
		DPRINTF("%s: ice_get_link_default_override failed; "
		    "status %s, aq_err %s\n", __func__, ice_status_str(status),
		    ice_aq_str(sc->hw.adminq.sq_last_status));
		return;
	}
#if 0
	if (ice_debug & ICE_DBG_LINK)
		ice_print_ldo_tlv(sc, &tlv);
#endif
	/* Set lenient link mode */
	if (ice_is_bit_set(sc->feat_cap, ICE_FEATURE_LENIENT_LINK_MODE) &&
	    (!(tlv.options & ICE_LINK_OVERRIDE_STRICT_MODE)))
		ice_set_bit(ICE_FEATURE_LENIENT_LINK_MODE, sc->feat_en);

	/* FW supports reporting a default configuration */
	if (ice_is_bit_set(sc->feat_cap, ICE_FEATURE_LINK_MGMT_VER_2) &&
	    ice_fw_supports_report_dflt_cfg(&sc->hw)) {
		ice_set_bit(ICE_FEATURE_LINK_MGMT_VER_2, sc->feat_en);
		/* Knowing we're at a high enough firmware revision to
		 * support this link management configuration, we don't
		 * need to check/support earlier versions.
		 */
		return;
	}

	/* Default overrides only work if in lenient link mode */
	if (ice_is_bit_set(sc->feat_cap, ICE_FEATURE_LINK_MGMT_VER_1) &&
	    ice_is_bit_set(sc->feat_en, ICE_FEATURE_LENIENT_LINK_MODE) &&
	    (tlv.options & ICE_LINK_OVERRIDE_EN))
		ice_set_bit(ICE_FEATURE_LINK_MGMT_VER_1, sc->feat_en);

	/* Cache the LDO TLV structure in the driver, since it
	 * won't change during the driver's lifetime.
	 */
	sc->ldo_tlv = tlv;
}

/**
 * ice_init_saved_phy_cfg -- Set cached user PHY cfg settings with NVM defaults
 * @sc: device private structure
 *
 * This should be called before the tunables for these link settings
 * (e.g. advertise_speed) are added -- so that these defaults don't overwrite
 * the cached values that the sysctl handlers will write.
 *
 * This also needs to be called before ice_init_link_configuration, to ensure
 * that there are sane values that can be written if there is media available
 * in the port.
 */
void
ice_init_saved_phy_cfg(struct ice_softc *sc)
{
	struct ice_port_info *pi = sc->hw.port_info;
	struct ice_aqc_get_phy_caps_data pcaps = { 0 };
	enum ice_status status;
	uint64_t phy_low, phy_high;
	uint8_t report_mode = ICE_AQC_REPORT_TOPO_CAP_MEDIA;

	if (ice_is_bit_set(sc->feat_en, ICE_FEATURE_LINK_MGMT_VER_2))
		report_mode = ICE_AQC_REPORT_DFLT_CFG;
	status = ice_aq_get_phy_caps(pi, false, report_mode, &pcaps, NULL);
	if (status != ICE_SUCCESS) {
		DPRINTF("%s: ice_aq_get_phy_caps (%s) failed; status %s, "
		    "aq_err %s\n", __func__,
		    report_mode == ICE_AQC_REPORT_DFLT_CFG ? "DFLT" : "w/MEDIA",
		    ice_status_str(status),
		    ice_aq_str(sc->hw.adminq.sq_last_status));
		return;
	}

	phy_low = le64toh(pcaps.phy_type_low);
	phy_high = le64toh(pcaps.phy_type_high);

	/* Save off initial config parameters */
	pi->phy.curr_user_speed_req =
	   ice_aq_phy_types_to_link_speeds(phy_low, phy_high);
	pi->phy.curr_user_fec_req = ice_caps_to_fec_mode(pcaps.caps,
	    pcaps.link_fec_options);
	pi->phy.curr_user_fc_req = ice_caps_to_fc_mode(pcaps.caps);
}

/**
 * ice_read_pba_string - Reads part number string from NVM
 * @hw: pointer to hardware structure
 * @pba_num: stores the part number string from the NVM
 * @pba_num_size: part number string buffer length
 *
 * Reads the part number string from the NVM.
 */
enum ice_status
ice_read_pba_string(struct ice_hw *hw, uint8_t *pba_num, uint32_t pba_num_size)
{
	uint16_t pba_tlv, pba_tlv_len;
	enum ice_status status;
	uint16_t pba_word, pba_size;
	uint16_t i;

	status = ice_get_pfa_module_tlv(hw, &pba_tlv, &pba_tlv_len,
					ICE_SR_PBA_BLOCK_PTR);
	if (status != ICE_SUCCESS) {
		DNPRINTF(ICE_DBG_INIT, "%s: Failed to read PBA Block TLV.\n",
		    __func__);
		return status;
	}

	/* pba_size is the next word */
	status = ice_read_sr_word(hw, (pba_tlv + 2), &pba_size);
	if (status != ICE_SUCCESS) {
		DNPRINTF(ICE_DBG_INIT, "%s: Failed to read PBA Section size.\n",
		    __func__);
		return status;
	}

	if (pba_tlv_len < pba_size) {
		DNPRINTF(ICE_DBG_INIT, "%s: Invalid PBA Block TLV size.\n",
		    __func__);
		return ICE_ERR_INVAL_SIZE;
	}

	/* Subtract one to get PBA word count (PBA Size word is included in
	 * total size)
	 */
	pba_size--;
	if (pba_num_size < (((uint32_t)pba_size * 2) + 1)) {
		DNPRINTF(ICE_DBG_INIT, "%s: Buffer too small for PBA data.\n",
		    __func__);
		return ICE_ERR_PARAM;
	}

	for (i = 0; i < pba_size; i++) {
		status = ice_read_sr_word(hw, (pba_tlv + 2 + 1) + i, &pba_word);
		if (status != ICE_SUCCESS) {
			DNPRINTF(ICE_DBG_INIT,
			    "%s: Failed to read PBA Block word %d.\n",
			    __func__, i);
			return status;
		}

		pba_num[(i * 2)] = (pba_word >> 8) & 0xFF;
		pba_num[(i * 2) + 1] = pba_word & 0xFF;
	}
	pba_num[(pba_size * 2)] = '\0';

	return status;
}

/**
 * ice_cfg_pba_num - Determine if PBA Number is retrievable
 * @sc: the device private softc structure
 *
 * Sets the feature flag for the existence of a PBA number
 * based on the success of the read command.  This does not
 * cache the result.
 */
void
ice_cfg_pba_num(struct ice_softc *sc)
{
	uint8_t pba_string[32] = "";

	if ((ice_is_bit_set(sc->feat_cap, ICE_FEATURE_HAS_PBA)) &&
	    (ice_read_pba_string(&sc->hw, pba_string, sizeof(pba_string)) == 0))
		ice_set_bit(ICE_FEATURE_HAS_PBA, sc->feat_en);
}

/**
 * ice_init_link_configuration -- Setup link in different ways depending
 * on whether media is available or not.
 * @sc: device private structure
 *
 * Called at the end of the attach process to either set default link
 * parameters if there is media available, or force HW link down and
 * set a state bit if there is no media.
 */
void
ice_init_link_configuration(struct ice_softc *sc)
{
	struct ice_port_info *pi = sc->hw.port_info;
	struct ice_hw *hw = &sc->hw;
	enum ice_status status;

	pi->phy.get_link_info = true;
	status = ice_get_link_status(pi, &sc->link_up);
	if (status != ICE_SUCCESS) {
		DPRINTF("%s: ice_get_link_status failed; status %s, "
		    "aq_err %s\n", __func__, ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		return;
	}

	if (pi->phy.link_info.link_info & ICE_AQ_MEDIA_AVAILABLE) {
		ice_clear_state(&sc->state, ICE_STATE_NO_MEDIA);
		/* Apply default link settings */
		if (!ice_test_state(&sc->state,
		    ICE_STATE_LINK_ACTIVE_ON_DOWN)) {
			ice_set_link(sc, false);
			ice_set_state(&sc->state,
			    ICE_STATE_LINK_STATUS_REPORTED);
		} else
			ice_apply_saved_phy_cfg(sc, ICE_APPLY_LS_FEC_FC);
	} else {
		 /*
		  * Set link down, and poll for media available in timer.
		  * This prevents the driver from receiving spurious
		  * link-related events.
		  */
		ice_set_state(&sc->state, ICE_STATE_NO_MEDIA);
		status = ice_aq_set_link_restart_an(pi, false, NULL);
		if (status != ICE_SUCCESS &&
		    hw->adminq.sq_last_status != ICE_AQ_RC_EMODE) {
			DPRINTF("%s: ice_aq_set_link_restart_an: status %s, "
			    "aq_err %s\n", __func__, ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
		}
	}
}

void
ice_attach_hook(struct device *self)
{
	struct ice_softc *sc = (void *)self;
	struct ice_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	enum ice_status status;
	unsigned nmsix, nqueues_max, nqueues;
	int err;

	KASSERT(!cold);

	/*
	 * Attempt to load a firmware package.
	 * Success indicates a change was made that requires a reinitialization
	 * of the hardware
	 */
	status = ice_load_pkg_file(sc);
	if (status == ICE_SUCCESS) {
		ice_deinit_hw(hw);
		err = ice_reinit_hw(sc);
		if (err)
			return;
	}

	err = ice_init_link_events(sc);
	if (err)
		goto deinit_hw;

	/* Initialize VLAN mode in FW; if dual VLAN mode is supported by the package
	 * and firmware, this will force them to use single VLAN mode.
	 */
	status = ice_set_vlan_mode(hw);
	if (status) {
		err = EIO;
		DPRINTF("%s: Unable to initialize VLAN mode, "
		    "err %s aq_err %s\n", __func__,
		    ice_status_str(status),
		    ice_aq_str(hw->adminq.sq_last_status));
		goto deinit_hw;
	}

	ice_print_nvm_version(sc);

	ice_setup_scctx(sc);
	if (ice_is_bit_set(sc->feat_en, ICE_FEATURE_SAFE_MODE))
		ice_set_safe_mode_caps(hw);

	/*
	 * Figure out how many queues we can use.
	 * Require at least two MSIX vectors, one for traffic and
	 * one for misc causes.
	 */
	nmsix = MIN(sc->sc_nmsix_max,
	    hw->func_caps.common_cap.num_msix_vectors);
	if (nmsix < 2) {
		printf("%s: insufficient amount of MSIx vectors available\n",
		    sc->sc_dev.dv_xname);
		goto deinit_hw;
	}
	sc->sc_nmsix = nmsix;
	nqueues_max = MIN(sc->isc_nrxqsets_max, sc->isc_ntxqsets_max);
	sc->sc_intrmap = intrmap_create(&sc->sc_dev, sc->sc_nmsix - 1,
	    nqueues_max, INTRMAP_POWEROF2);
	nqueues = intrmap_count(sc->sc_intrmap);
	KASSERT(nqueues > 0);
	KASSERT(powerof2(nqueues));
	sc->sc_nqueues = MIN(nqueues, sc->sc_nvectors);
	DPRINTF("%s: %d MSIx vector%s available, using %d queue%s\n", __func__,
	    sc->sc_nmsix, sc->sc_nmsix > 1 ? "s" : "",
	    sc->sc_nqueues, sc->sc_nqueues > 1 ? "s" : "");

	/* Initialize the Tx queue manager */
	err = ice_resmgr_init(&sc->tx_qmgr, sc->sc_nqueues);
	if (err) {
		printf("%s: Unable to initialize Tx queue manager: err %d\n",
		    sc->sc_dev.dv_xname, err);
		goto deinit_hw;
	}

	/* Initialize the Rx queue manager */
	err = ice_resmgr_init(&sc->rx_qmgr, sc->sc_nqueues);
	if (err) {
		printf("%s: Unable to initialize Rx queue manager: %d\n",
		    sc->sc_dev.dv_xname, err);
		goto free_tx_qmgr;
	}

	/* Initialize the PF device interrupt resource manager */
	err = ice_alloc_intr_tracking(sc);
	if (err)
		/* Errors are already printed */
		goto free_rx_qmgr;

	/* Determine maximum number of VSIs we'll prepare for */
	sc->num_available_vsi = MIN(ICE_MAX_VSI_AVAILABLE,
				    hw->func_caps.guar_num_vsi);
	if (!sc->num_available_vsi) {
		err = EIO;
		printf("%s: No VSIs allocated to host\n",
		    sc->sc_dev.dv_xname);
		goto free_intr_tracking;
	}

	/* Allocate storage for the VSI pointers */
	sc->all_vsi = (struct ice_vsi **)
		mallocarray(sc->num_available_vsi, sizeof(struct ice_vsi *),
		       M_DEVBUF, M_WAITOK | M_ZERO);
	if (sc->all_vsi == NULL) {
		err = ENOMEM;
		printf("%s: Unable to allocate VSI array\n",
		    sc->sc_dev.dv_xname);
		goto free_intr_tracking;
	}

	/*
	 * Prepare the statically allocated primary PF VSI in the softc
	 * structure. Other VSIs will be dynamically allocated as needed.
	 */
	ice_setup_pf_vsi(sc);

	err = ice_alloc_vsi_qmap(&sc->pf_vsi, sc->isc_ntxqsets_max,
	    sc->isc_nrxqsets_max);
	if (err) {
		printf("%s: Unable to allocate VSI Queue maps\n",
		    sc->sc_dev.dv_xname);
		goto free_main_vsi;
	}

	/* Allocate MSI-X vectors. */
	err = ice_allocate_msix(sc);
	if (err)
		goto free_main_vsi;

	err = ice_tx_queues_alloc(sc);
	if (err)
		goto free_main_vsi;

	err = ice_rx_queues_alloc(sc);
	if (err)
		goto free_queues;

	err = ice_initialize_vsi(&sc->pf_vsi);
	if (err)
		goto free_queues;

	/* Enable FW health event reporting */
	ice_init_health_events(sc);

	/* Configure the main PF VSI for RSS */
	err = ice_config_rss(&sc->pf_vsi);
	if (err) {
		printf("%s: Unable to configure RSS for the main VSI, "
		    "err %d\n", sc->sc_dev.dv_xname, err);
		goto free_queues;
	}
#if 0
	/* Configure switch to drop transmitted LLDP and PAUSE frames */
	err = ice_cfg_pf_ethertype_filters(sc);
	if (err)
		return err;

	ice_get_and_print_bus_info(sc);
#endif

	/*
	 * At this point we are committed to attaching the driver.
	 * Network stack needs to be wired up before ice_update_link_status()
	 * calls if_link_state_change().
	 */
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = ice_ioctl;
	ifp->if_qstart = ice_start;
	ifp->if_watchdog = ice_watchdog;
	ifp->if_hardmtu = ice_hardmtu(hw);

	ifq_init_maxlen(&ifp->if_snd, ICE_DEFAULT_DESC_COUNT);

	ifp->if_capabilities = IFCAP_VLAN_MTU;
#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif
	ifp->if_capabilities |= IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4 |
	    IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6 |
	    IFCAP_TSOv4 | IFCAP_TSOv6;
	ifp->if_capabilities |= IFCAP_LRO;
#if notyet
	/* for now tcplro at ice(4) is default off */
	ifp->if_xflags |= IFXF_LRO;
#endif

	if_attach(ifp);
	ether_ifattach(ifp);

	if_attach_queues(ifp, sc->sc_nqueues);
	if_attach_iqueues(ifp, sc->sc_nqueues);

	ice_set_link_management_mode(sc);

	ice_init_saved_phy_cfg(sc);

	ice_cfg_pba_num(sc);
#if 0
	/* Set a default value for PFC mode on attach since the FW state is unknown
	 * before sysctl tunables are executed and it can't be queried. This fixes an
	 * issue when loading the driver with the FW LLDP agent enabled but the FW
	 * was previously in DSCP PFC mode.
	 */
	status = ice_aq_set_pfc_mode(&sc->hw, ICE_AQC_PFC_VLAN_BASED_PFC, NULL);
	if (status != ICE_SUCCESS)
		device_printf(sc->dev, "Setting pfc mode failed, status %s\n", ice_status_str(status));

	ice_add_device_sysctls(sc);
#endif
	/* Get DCBX/LLDP state and start DCBX agent */
	ice_init_dcb_setup(sc);

	/* Setup link configuration parameters */
	ice_init_link_configuration(sc);
	ice_update_link_status(sc, true);

	/* Configure interrupt causes for the administrative interrupt */
	ice_configure_misc_interrupts(sc);

	/* Enable ITR 0 right away, so that we can handle admin interrupts */
	ice_enable_intr(&sc->hw, 0);
#if 0
	err = ice_rdma_pf_attach(sc);
	if (err)
		return (err);
#endif
	if (ice_test_state(&sc->state, ICE_STATE_LINK_ACTIVE_ON_DOWN) &&
		 !ice_test_state(&sc->state, ICE_STATE_NO_MEDIA))
		ice_set_state(&sc->state, ICE_STATE_FIRST_INIT_LINK);

	/* Setup the MAC address */
	err = if_setlladdr(ifp, hw->port_info->mac.perm_addr);
	if (err)
		printf("%s: could not set MAC address (error %d)\n",
		    sc->sc_dev.dv_xname, err);

	ice_clear_state(&sc->state, ICE_STATE_ATTACHING);
	return;

free_queues:
	ice_free_tx_queues(sc);	
	ice_free_rx_queues(sc);	
free_main_vsi:
	/* ice_release_vsi will free the queue maps if they were allocated */
	ice_release_vsi(&sc->pf_vsi);
	free(sc->all_vsi, M_DEVBUF,
	    sc->num_available_vsi * sizeof(struct ice_vis *));
	sc->all_vsi = NULL;
free_intr_tracking:
	ice_free_intr_tracking(sc);
free_rx_qmgr:
	ice_resmgr_destroy(&sc->rx_qmgr);
free_tx_qmgr:
	ice_resmgr_destroy(&sc->tx_qmgr);
deinit_hw:
	ice_deinit_hw(hw);
}

void
ice_attach(struct device *parent, struct device *self, void *aux)
{
	struct ice_softc *sc = (void *)self;
	struct ice_hw *hw = &sc->hw;
	struct pci_attach_args *pa = aux;
	enum ice_fw_modes fw_mode;
	pcireg_t memtype;
	enum ice_status status;
	int err, i;

	rw_init(&sc->sc_cfg_lock, "icecfg");

	ice_set_state(&sc->state, ICE_STATE_ATTACHING);

	sc->sc_pid = PCI_PRODUCT(pa->pa_id);
	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	hw->hw_sc = sc;

	task_set(&sc->sc_admin_task, ice_if_update_admin_status, sc);
	timeout_set(&sc->sc_admin_timer, ice_admin_timer, sc);

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	err = pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0,
	    &sc->sc_st, &sc->sc_sh, NULL, &sc->sc_sz, 0);
	if (err) {
		printf("%s: can't map mem space\n", sc->sc_dev.dv_xname);
		return;
	}

	ice_set_ctrlq_len(hw);

	fw_mode = ice_get_fw_mode(hw);
	if (fw_mode == ICE_FW_MODE_REC) {
		printf("%s: firmware is in recovery mode\n",
		    sc->sc_dev.dv_xname);
#if 0
		err = ice_attach_pre_recovery_mode(sc);
		if (err)
			goto free_pci_mapping;
#endif
		return;
	}

	/* Initialize the hw data structure */
	status = ice_init_hw(hw);
	if (status) {
		if (status == ICE_ERR_FW_API_VER) {
			printf("%s: incompatible firmware API version\n",
			    sc->sc_dev.dv_xname);
#if 0
			/* Enter recovery mode, so that the driver remains
			 * loaded. This way, if the system administrator
			 * cannot update the driver, they may still attempt to
			 * downgrade the NVM.
			 */
			err = ice_attach_pre_recovery_mode(sc);
			if (err)
				goto free_pci_mapping;
#endif
			return;
		} else {
			printf("%s: could not initialize hardware, "
			    "status %s aq_err %s\n",
			    sc->sc_dev.dv_xname, ice_status_str(status),
			    ice_aq_str(hw->adminq.sq_last_status));
			return;
		}
	}

	if (pci_intr_map_msix(pa, 0, &sc->sc_ih) == 0) {
		unsigned int nmsix = pci_intr_msix_count(pa);

		/*
		 * Require at least two MSIX vectors, one for traffic and
		 * one for misc causes.
		 */
		if (nmsix < 2) {
			printf(": can't map interrupt\n");
			return;
		}
		sc->sc_nmsix_max = nmsix;
	} else {
		printf(": can't map interrupt\n");
		return;
	}

	/*
	 * Map an extra MSI-X vector per CPU, up to a hard-coded limit.
	 * We may not need them all but we can only figure out the supported
	 * number of queues once firmware is loaded.
	 */
	sc->sc_nvectors = MIN(sc->sc_nmsix_max, ncpus);
	sc->sc_nvectors = MIN(sc->sc_nvectors, ICE_MAX_VECTORS);
	sc->sc_vectors = mallocarray(sizeof(*sc->sc_vectors), sc->sc_nvectors,
	    M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (sc->sc_vectors == NULL) {
		printf(": unable to allocate MSIx interrupt vector array\n");
		return;
	}
	for (i = 0; i < sc->sc_nvectors; i++) {
		struct ice_intr_vector *iv = &sc->sc_vectors[i];
		int v = i + 1; /* 0 is used for adminq */

		iv->iv_sc = sc;
		iv->iv_qid = i;
		snprintf(iv->iv_name, sizeof(iv->iv_name),
		    "%s:%u", sc->sc_dev.dv_xname, i);
		if (pci_intr_map_msix(pa, v, &iv->ih)) {
			printf(": unable to map MSI-X vector %d\n", v);
			goto free_vectors;
		}
	}
	
	printf(": %s\n", pci_intr_string(sc->sc_pct, sc->sc_ih));

	ice_init_device_features(sc);

	/* Keep flag set by default */
	ice_set_state(&sc->state, ICE_STATE_LINK_ACTIVE_ON_DOWN);

	/* Notify firmware of the device driver version */
	err = ice_send_version(sc);
	if (err)
		goto deinit_hw;

	/* Initialize ifmedia structures. */
	ifmedia_init(&sc->media, IFM_IMASK, ice_media_change, ice_media_status);
	ice_add_media_types(sc, &sc->media);

	config_mountroot(self, ice_attach_hook);
	return;

deinit_hw:
	ice_deinit_hw(hw);
free_vectors:
	free(sc->sc_vectors, M_DEVBUF,
	    sc->sc_nvectors * sizeof(*sc->sc_vectors));
	sc->sc_vectors = NULL;
	sc->sc_nvectors = 0;
}

struct cfdriver ice_cd = {
	NULL, "ice", DV_IFNET
};

const struct cfattach ice_ca = {
	sizeof(struct ice_softc), ice_match, ice_attach,
	NULL, NULL,
};
