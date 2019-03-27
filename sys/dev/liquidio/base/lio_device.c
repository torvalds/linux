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

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_main.h"
#include "lio_network.h"
#include "cn23xx_pf_device.h"
#include "lio_image.h"
#include "lio_mem_ops.h"

static struct lio_config default_cn23xx_conf = {
	.card_type			= LIO_23XX,
	.card_name			= LIO_23XX_NAME,
	/* IQ attributes */
	.iq = {
		.max_iqs		= LIO_CN23XX_CFG_IO_QUEUES,
		.pending_list_size	= (LIO_CN23XX_DEFAULT_IQ_DESCRIPTORS *
					   LIO_CN23XX_CFG_IO_QUEUES),
		.instr_type		= LIO_64BYTE_INSTR,
		.db_min			= LIO_CN23XX_DB_MIN,
		.db_timeout		= LIO_CN23XX_DB_TIMEOUT,
		.iq_intr_pkt		= LIO_CN23XX_DEF_IQ_INTR_THRESHOLD,
	},

	/* OQ attributes */
	.oq = {
		.max_oqs		= LIO_CN23XX_CFG_IO_QUEUES,
		.pkts_per_intr		= LIO_CN23XX_OQ_PKTS_PER_INTR,
		.refill_threshold	= LIO_CN23XX_OQ_REFIL_THRESHOLD,
		.oq_intr_pkt		= LIO_CN23XX_OQ_INTR_PKT,
		.oq_intr_time		= LIO_CN23XX_OQ_INTR_TIME,
	},

	.num_nic_ports			= LIO_CN23XX_DEFAULT_NUM_PORTS,
	.num_def_rx_descs		= LIO_CN23XX_DEFAULT_OQ_DESCRIPTORS,
	.num_def_tx_descs		= LIO_CN23XX_DEFAULT_IQ_DESCRIPTORS,
	.def_rx_buf_size		= LIO_CN23XX_OQ_BUF_SIZE,

	/* For ethernet interface 0:  Port cfg Attributes */
	.nic_if_cfg[0] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs		= LIO_MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs		= LIO_DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs		= LIO_MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs		= LIO_DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs		= LIO_CN23XX_DEFAULT_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs		= LIO_CN23XX_DEFAULT_IQ_DESCRIPTORS,

		/*
		 * Mbuf size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= LIO_CN23XX_OQ_BUF_SIZE,

		.base_queue			= LIO_BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 0,
	},

	.nic_if_cfg[1] = {
		/* Max Txqs: Half for each of the two ports :max_iq/2 */
		.max_txqs		= LIO_MAX_TXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_txqs */
		.num_txqs		= LIO_DEF_TXQS_PER_INTF,

		/* Max Rxqs: Half for each of the two ports :max_oq/2  */
		.max_rxqs		= LIO_MAX_RXQS_PER_INTF,

		/* Actual configured value. Range could be: 1...max_rxqs */
		.num_rxqs		= LIO_DEF_RXQS_PER_INTF,

		/* Num of desc for rx rings */
		.num_rx_descs		= LIO_CN23XX_DEFAULT_OQ_DESCRIPTORS,

		/* Num of desc for tx rings */
		.num_tx_descs		= LIO_CN23XX_DEFAULT_IQ_DESCRIPTORS,

		/*
		 * Mbuf size, We need not change buf size even for Jumbo frames.
		 * Octeon can send jumbo frames in 4 consecutive descriptors,
		 */
		.rx_buf_size			= LIO_CN23XX_OQ_BUF_SIZE,

		.base_queue			= LIO_BASE_QUEUE_NOT_REQUESTED,

		.gmx_port_id			= 1,
	},

	.misc					= {
		/* Host driver link query interval */
		.oct_link_query_interval	= 100,

		/* Octeon link query interval */
		.host_link_query_interval	= 500,

		.enable_sli_oq_bp		= 0,

		/* Control queue group */
		.ctrlq_grp			= 1,
	}
};

static struct lio_config_ptr {
	uint32_t	conf_type;
}	oct_conf_info[LIO_MAX_DEVICES] = {

	{
		LIO_CFG_TYPE_DEFAULT,
	}, {
		LIO_CFG_TYPE_DEFAULT,
	}, {
		LIO_CFG_TYPE_DEFAULT,
	}, {
		LIO_CFG_TYPE_DEFAULT,
	},
};

static char lio_state_str[LIO_DEV_STATES + 1][32] = {
	"BEGIN", "PCI-ENABLE-DONE", "PCI-MAP-DONE", "DISPATCH-INIT-DONE",
	"IQ-INIT-DONE", "SCBUFF-POOL-INIT-DONE", "RESPLIST-INIT-DONE",
	"DROQ-INIT-DONE", "MBOX-SETUP-DONE", "MSIX-ALLOC-VECTOR-DONE",
	"INTR-SET-DONE", "IO-QUEUES-INIT-DONE", "CONSOLE-INIT-DONE",
	"HOST-READY", "CORE-READY", "RUNNING", "IN-RESET",
	"INVALID"
};

static char	lio_app_str[LIO_DRV_APP_COUNT + 1][32] = {"BASE", "NIC", "UNKNOWN"};

static struct octeon_device	*octeon_device[LIO_MAX_DEVICES];
static volatile int		lio_adapter_refcounts[LIO_MAX_DEVICES];

static uint32_t	octeon_device_count;
/* locks device array (i.e. octeon_device[]) */
struct mtx	octeon_devices_lock;

static struct lio_core_setup	core_setup[LIO_MAX_DEVICES];

static void
oct_set_config_info(int oct_id, int conf_type)
{

	if (conf_type < 0 || conf_type > (LIO_NUM_CFGS - 1))
		conf_type = LIO_CFG_TYPE_DEFAULT;
	oct_conf_info[oct_id].conf_type = conf_type;
}

void
lio_init_device_list(int conf_type)
{
	int	i;

	bzero(octeon_device, (sizeof(void *) * LIO_MAX_DEVICES));
	for (i = 0; i < LIO_MAX_DEVICES; i++)
		oct_set_config_info(i, conf_type);
	mtx_init(&octeon_devices_lock, "octeon_devices_lock", NULL, MTX_DEF);
}

static void *
__lio_retrieve_config_info(struct octeon_device *oct, uint16_t card_type)
{
	void		*ret = NULL;
	uint32_t	oct_id = oct->octeon_id;

	switch (oct_conf_info[oct_id].conf_type) {
	case LIO_CFG_TYPE_DEFAULT:
		if (oct->chip_id == LIO_CN23XX_PF_VID) {
			ret = &default_cn23xx_conf;
		}

		break;
	default:
		break;
	}
	return (ret);
}

void   *
lio_get_config_info(struct octeon_device *oct, uint16_t card_type)
{
	void	*conf = NULL;

	conf = __lio_retrieve_config_info(oct, card_type);
	if (conf == NULL)
		return (NULL);

	return (conf);
}

char   *
lio_get_state_string(volatile int *state_ptr)
{
	int32_t	istate = (int32_t)atomic_load_acq_int(state_ptr);

	if (istate > LIO_DEV_STATES || istate < 0)
		return (lio_state_str[LIO_DEV_STATE_INVALID]);

	return (lio_state_str[istate]);
}

static char *
lio_get_app_string(uint32_t app_mode)
{

	if (app_mode <= LIO_DRV_APP_END)
		return (lio_app_str[app_mode - LIO_DRV_APP_START]);

	return (lio_app_str[LIO_DRV_INVALID_APP - LIO_DRV_APP_START]);
}

void
lio_free_device_mem(struct octeon_device *oct)
{
	int	i;

	for (i = 0; i < LIO_MAX_OUTPUT_QUEUES(oct); i++) {
		if ((oct->io_qmask.oq & BIT_ULL(i)) && (oct->droq[i]))
			free(oct->droq[i], M_DEVBUF);
	}

	for (i = 0; i < LIO_MAX_INSTR_QUEUES(oct); i++) {
		if ((oct->io_qmask.iq & BIT_ULL(i)) && (oct->instr_queue[i]))
			free(oct->instr_queue[i], M_DEVBUF);
	}

	i = oct->octeon_id;
	free(oct->chip, M_DEVBUF);

	octeon_device[i] = NULL;
	octeon_device_count--;
}

static struct octeon_device *
lio_allocate_device_mem(device_t device)
{
	struct octeon_device	*oct;
	uint32_t	configsize = 0, pci_id = 0, size;
	uint8_t		*buf = NULL;

	pci_id = pci_get_device(device);
	switch (pci_id) {
	case LIO_CN23XX_PF_VID:
		configsize = sizeof(struct lio_cn23xx_pf);
		break;
	default:
		device_printf(device, "Error: Unknown PCI Device: 0x%x\n",
			      pci_id);
		return (NULL);
	}

	if (configsize & 0x7)
		configsize += (8 - (configsize & 0x7));

	size = configsize +
		(sizeof(struct lio_dispatch) * LIO_DISPATCH_LIST_SIZE);

	buf = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (buf == NULL)
		return (NULL);

	oct = (struct octeon_device *)device_get_softc(device);
	oct->chip = (void *)(buf);
	oct->dispatch.dlist = (struct lio_dispatch *)(buf + configsize);

	return (oct);
}

struct octeon_device *
lio_allocate_device(device_t device)
{
	struct octeon_device	*oct = NULL;
	uint32_t	oct_idx = 0;

	mtx_lock(&octeon_devices_lock);

	for (oct_idx = 0; oct_idx < LIO_MAX_DEVICES; oct_idx++)
		if (!octeon_device[oct_idx])
			break;

	if (oct_idx < LIO_MAX_DEVICES) {
		oct = lio_allocate_device_mem(device);
		if (oct != NULL) {
			octeon_device_count++;
			octeon_device[oct_idx] = oct;
		}
	}

	mtx_unlock(&octeon_devices_lock);

	if (oct == NULL)
		return (NULL);

	mtx_init(&oct->pci_win_lock, "pci_win_lock", NULL, MTX_DEF);
	mtx_init(&oct->mem_access_lock, "mem_access_lock", NULL, MTX_DEF);

	oct->octeon_id = oct_idx;
	snprintf(oct->device_name, sizeof(oct->device_name), "%s%d",
		 LIO_DRV_NAME, oct->octeon_id);

	return (oct);
}

/*
 *  Register a device's bus location at initialization time.
 *  @param oct        - pointer to the octeon device structure.
 *  @param bus        - PCIe bus #
 *  @param dev        - PCIe device #
 *  @param func       - PCIe function #
 *  @param is_pf      - TRUE for PF, FALSE for VF
 *  @return reference count of device's adapter
 */
int
lio_register_device(struct octeon_device *oct, int bus, int dev, int func,
		    int is_pf)
{
	int	idx, refcount;

	oct->loc.bus = bus;
	oct->loc.dev = dev;
	oct->loc.func = func;

	oct->adapter_refcount = &lio_adapter_refcounts[oct->octeon_id];
	atomic_store_rel_int(oct->adapter_refcount, 0);

	mtx_lock(&octeon_devices_lock);
	for (idx = (int)oct->octeon_id - 1; idx >= 0; idx--) {
		if (octeon_device[idx] == NULL) {
			lio_dev_err(oct, "%s: Internal driver error, missing dev\n",
				    __func__);
			mtx_unlock(&octeon_devices_lock);
			atomic_add_int(oct->adapter_refcount, 1);
			return (1);	/* here, refcount is guaranteed to be 1 */
		}

		/* if another device is at same bus/dev, use its refcounter */
		if ((octeon_device[idx]->loc.bus == bus) &&
		    (octeon_device[idx]->loc.dev == dev)) {
			oct->adapter_refcount =
				octeon_device[idx]->adapter_refcount;
			break;
		}
	}

	mtx_unlock(&octeon_devices_lock);

	atomic_add_int(oct->adapter_refcount, 1);
	refcount = atomic_load_acq_int(oct->adapter_refcount);

	lio_dev_dbg(oct, "%s: %02x:%02x:%d refcount %u\n", __func__,
		    oct->loc.bus, oct->loc.dev, oct->loc.func, refcount);

	return (refcount);
}

/*
 *  Deregister a device at de-initialization time.
 *  @param oct - pointer to the octeon device structure.
 *  @return reference count of device's adapter
 */
int
lio_deregister_device(struct octeon_device *oct)
{
	int	refcount;

	atomic_subtract_int(oct->adapter_refcount, 1);
	refcount = atomic_load_acq_int(oct->adapter_refcount);

	lio_dev_dbg(oct, "%s: %04d:%02d:%d refcount %u\n", __func__,
		    oct->loc.bus, oct->loc.dev, oct->loc.func, refcount);

	return (refcount);
}

int
lio_allocate_ioq_vector(struct octeon_device *oct)
{
	struct lio_ioq_vector	*ioq_vector;
	int	i, cpu_num, num_ioqs = 0, size;

	if (LIO_CN23XX_PF(oct))
		num_ioqs = oct->sriov_info.num_pf_rings;

	size = sizeof(struct lio_ioq_vector) * num_ioqs;

	oct->ioq_vector = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (oct->ioq_vector == NULL)
		return (1);

	for (i = 0; i < num_ioqs; i++) {
		ioq_vector = &oct->ioq_vector[i];
		ioq_vector->oct_dev = oct;
		ioq_vector->droq_index = i;
		cpu_num = i % mp_ncpus;
		CPU_SETOF(cpu_num, &ioq_vector->affinity_mask);

		if (oct->chip_id == LIO_CN23XX_PF_VID)
			ioq_vector->ioq_num = i + oct->sriov_info.pf_srn;
		else
			ioq_vector->ioq_num = i;
	}
	return (0);
}

void
lio_free_ioq_vector(struct octeon_device *oct)
{

	free(oct->ioq_vector, M_DEVBUF);
	oct->ioq_vector = NULL;
}

/* this function is only for setting up the first queue */
int
lio_setup_instr_queue0(struct octeon_device *oct)
{
	union octeon_txpciq	txpciq;
	uint32_t	iq_no = 0;
	uint32_t	num_descs = 0;

	if (LIO_CN23XX_PF(oct))
		num_descs =
			LIO_GET_NUM_DEF_TX_DESCS_CFG(LIO_CHIP_CONF(oct,
								   cn23xx_pf));

	oct->num_iqs = 0;

	oct->instr_queue[0]->q_index = 0;
	oct->instr_queue[0]->app_ctx = (void *)(size_t)0;
	oct->instr_queue[0]->ifidx = 0;
	txpciq.txpciq64 = 0;
	txpciq.s.q_no = iq_no;
	txpciq.s.pkind = oct->pfvf_hsword.pkind;
	txpciq.s.use_qpg = 0;
	txpciq.s.qpg = 0;
	if (lio_init_instr_queue(oct, txpciq, num_descs)) {
		/* prevent memory leak */
		lio_delete_instr_queue(oct, 0);
		return (1);
	}

	oct->num_iqs++;
	return (0);
}

int
lio_setup_output_queue0(struct octeon_device *oct)
{
	uint32_t	desc_size = 0, num_descs = 0, oq_no = 0;

	if (LIO_CN23XX_PF(oct)) {
		num_descs =
			LIO_GET_NUM_DEF_RX_DESCS_CFG(LIO_CHIP_CONF(oct,
								   cn23xx_pf));
		desc_size =
			LIO_GET_DEF_RX_BUF_SIZE_CFG(LIO_CHIP_CONF(oct,
								  cn23xx_pf));
	}

	oct->num_oqs = 0;

	if (lio_init_droq(oct, oq_no, num_descs, desc_size, NULL)) {
		return (1);
	}

	oct->num_oqs++;

	return (0);
}

int
lio_init_dispatch_list(struct octeon_device *oct)
{
	uint32_t	i;

	oct->dispatch.count = 0;

	for (i = 0; i < LIO_DISPATCH_LIST_SIZE; i++) {
		oct->dispatch.dlist[i].opcode = 0;
		STAILQ_INIT(&oct->dispatch.dlist[i].head);
	}

	mtx_init(&oct->dispatch.lock, "dispatch_lock", NULL, MTX_DEF);

	return (0);
}

void
lio_delete_dispatch_list(struct octeon_device *oct)
{
	struct lio_stailq_head	freelist;
	struct lio_stailq_node	*temp, *tmp2;
	uint32_t		i;

	STAILQ_INIT(&freelist);

	mtx_lock(&oct->dispatch.lock);

	for (i = 0; i < LIO_DISPATCH_LIST_SIZE; i++) {
		struct lio_stailq_head *dispatch;

		dispatch = &oct->dispatch.dlist[i].head;
		while (!STAILQ_EMPTY(dispatch)) {
			temp = STAILQ_FIRST(dispatch);
			STAILQ_REMOVE_HEAD(&oct->dispatch.dlist[i].head,
					   entries);
			STAILQ_INSERT_TAIL(&freelist, temp, entries);
		}

		oct->dispatch.dlist[i].opcode = 0;
	}

	oct->dispatch.count = 0;

	mtx_unlock(&oct->dispatch.lock);

	STAILQ_FOREACH_SAFE(temp, &freelist, entries, tmp2) {
		STAILQ_REMOVE_HEAD(&freelist, entries);
		free(temp, M_DEVBUF);
	}
}

lio_dispatch_fn_t
lio_get_dispatch(struct octeon_device *octeon_dev, uint16_t opcode,
		 uint16_t subcode)
{
	struct lio_stailq_node	*dispatch;
	lio_dispatch_fn_t	fn = NULL;
	uint32_t		idx;
	uint16_t	combined_opcode = LIO_OPCODE_SUBCODE(opcode, subcode);

	idx = combined_opcode & LIO_OPCODE_MASK;

	mtx_lock(&octeon_dev->dispatch.lock);

	if (octeon_dev->dispatch.count == 0) {
		mtx_unlock(&octeon_dev->dispatch.lock);
		return (NULL);
	}

	if (!(octeon_dev->dispatch.dlist[idx].opcode)) {
		mtx_unlock(&octeon_dev->dispatch.lock);
		return (NULL);
	}

	if (octeon_dev->dispatch.dlist[idx].opcode == combined_opcode) {
		fn = octeon_dev->dispatch.dlist[idx].dispatch_fn;
	} else {
		STAILQ_FOREACH(dispatch, &octeon_dev->dispatch.dlist[idx].head,
			       entries) {
			if (((struct lio_dispatch *)dispatch)->opcode ==
			    combined_opcode) {
				fn = ((struct lio_dispatch *)
				      dispatch)->dispatch_fn;
				break;
			}
		}
	}

	mtx_unlock(&octeon_dev->dispatch.lock);
	return (fn);
}

/*
 * lio_register_dispatch_fn
 * Parameters:
 *   octeon_id - id of the octeon device.
 *   opcode    - opcode for which driver should call the registered function
 *   subcode   - subcode for which driver should call the registered function
 *   fn        - The function to call when a packet with "opcode" arrives in
 *               octeon output queues.
 *   fn_arg    - The argument to be passed when calling function "fn".
 * Description:
 *   Registers a function and its argument to be called when a packet
 *   arrives in Octeon output queues with "opcode".
 * Returns:
 *   Success: 0
 *   Failure: 1
 * Locks:
 *   No locks are held.
 */
int
lio_register_dispatch_fn(struct octeon_device *oct, uint16_t opcode,
			 uint16_t subcode, lio_dispatch_fn_t fn, void *fn_arg)
{
	lio_dispatch_fn_t	pfn;
	uint32_t	idx;
	uint16_t	combined_opcode = LIO_OPCODE_SUBCODE(opcode, subcode);

	idx = combined_opcode & LIO_OPCODE_MASK;

	mtx_lock(&oct->dispatch.lock);
	/* Add dispatch function to first level of lookup table */
	if (oct->dispatch.dlist[idx].opcode == 0) {
		oct->dispatch.dlist[idx].opcode = combined_opcode;
		oct->dispatch.dlist[idx].dispatch_fn = fn;
		oct->dispatch.dlist[idx].arg = fn_arg;
		oct->dispatch.count++;
		mtx_unlock(&oct->dispatch.lock);
		return (0);
	}

	mtx_unlock(&oct->dispatch.lock);

	/*
	 * Check if there was a function already registered for this
	 * opcode/subcode.
	 */
	pfn = lio_get_dispatch(oct, opcode, subcode);
	if (!pfn) {
		struct lio_dispatch *dispatch;

		lio_dev_dbg(oct,
			    "Adding opcode to dispatch list linked list\n");
		dispatch = (struct lio_dispatch *)
			malloc(sizeof(struct lio_dispatch),
			       M_DEVBUF, M_NOWAIT | M_ZERO);
		if (dispatch == NULL) {
			lio_dev_err(oct,
				    "No memory to add dispatch function\n");
			return (1);
		}

		dispatch->opcode = combined_opcode;
		dispatch->dispatch_fn = fn;
		dispatch->arg = fn_arg;

		/*
		 * Add dispatch function to linked list of fn ptrs
		 * at the hashed index.
		 */
		mtx_lock(&oct->dispatch.lock);
		STAILQ_INSERT_HEAD(&oct->dispatch.dlist[idx].head,
				   &dispatch->node, entries);
		oct->dispatch.count++;
		mtx_unlock(&oct->dispatch.lock);

	} else {
		lio_dev_err(oct, "Found previously registered dispatch fn for opcode/subcode: %x/%x\n",
			    opcode, subcode);
		return (1);
	}

	return (0);
}

/*
 * lio_unregister_dispatch_fn
 * Parameters:
 *   oct       - octeon device
 *   opcode    - driver should unregister the function for this opcode
 *   subcode   - driver should unregister the function for this subcode
 * Description:
 *   Unregister the function set for this opcode+subcode.
 * Returns:
 *   Success: 0
 *   Failure: 1
 * Locks:
 *   No locks are held.
 */
int
lio_unregister_dispatch_fn(struct octeon_device *oct, uint16_t opcode,
			   uint16_t subcode)
{
	struct lio_stailq_head	*dispatch_head;
	struct lio_stailq_node	*dispatch, *dfree = NULL, *tmp2;
	int		retval = 0;
	uint32_t	idx;
	uint16_t	combined_opcode = LIO_OPCODE_SUBCODE(opcode, subcode);

	idx = combined_opcode & LIO_OPCODE_MASK;

	mtx_lock(&oct->dispatch.lock);

	if (oct->dispatch.count == 0) {
		mtx_unlock(&oct->dispatch.lock);
		lio_dev_err(oct, "No dispatch functions registered for this device\n");
		return (1);
	}
	if (oct->dispatch.dlist[idx].opcode == combined_opcode) {
		dispatch_head = &oct->dispatch.dlist[idx].head;
		if (!STAILQ_EMPTY(dispatch_head)) {
			dispatch = STAILQ_FIRST(dispatch_head);
			oct->dispatch.dlist[idx].opcode =
			    ((struct lio_dispatch *)dispatch)->opcode;
			oct->dispatch.dlist[idx].dispatch_fn =
			    ((struct lio_dispatch *)dispatch)->dispatch_fn;
			oct->dispatch.dlist[idx].arg =
			    ((struct lio_dispatch *)dispatch)->arg;
			STAILQ_REMOVE_HEAD(dispatch_head, entries);
			dfree = dispatch;
		} else {
			oct->dispatch.dlist[idx].opcode = 0;
			oct->dispatch.dlist[idx].dispatch_fn = NULL;
			oct->dispatch.dlist[idx].arg = NULL;
		}
	} else {
		retval = 1;
		STAILQ_FOREACH_SAFE(dispatch,
				    &oct->dispatch.dlist[idx].head,
				    entries, tmp2) {
			if (((struct lio_dispatch *)dispatch)->opcode ==
			    combined_opcode) {
				STAILQ_REMOVE(&oct->dispatch.dlist[idx].head,
					      dispatch,
					      lio_stailq_node, entries);
				dfree = dispatch;
				retval = 0;
			}
		}
	}

	if (!retval)
		oct->dispatch.count--;

	mtx_unlock(&oct->dispatch.lock);
	free(dfree, M_DEVBUF);

	return (retval);
}

int
lio_core_drv_init(struct lio_recv_info *recv_info, void *buf)
{
	struct octeon_device	*oct = (struct octeon_device *)buf;
	struct lio_recv_pkt	*recv_pkt = recv_info->recv_pkt;
	struct lio_core_setup	*cs = NULL;
	uint32_t	i;
	uint32_t	num_nic_ports = 0;
	char		app_name[16];

	if (LIO_CN23XX_PF(oct))
		num_nic_ports = LIO_GET_NUM_NIC_PORTS_CFG(
					       LIO_CHIP_CONF(oct, cn23xx_pf));

	if (atomic_load_acq_int(&oct->status) >= LIO_DEV_RUNNING) {
		lio_dev_err(oct, "Received CORE OK when device state is 0x%x\n",
			    atomic_load_acq_int(&oct->status));
		goto core_drv_init_err;
	}

	strncpy(app_name,
		lio_get_app_string((uint32_t)
				   recv_pkt->rh.r_core_drv_init.app_mode),
		sizeof(app_name) - 1);
	oct->app_mode = (uint32_t)recv_pkt->rh.r_core_drv_init.app_mode;
	if (recv_pkt->rh.r_core_drv_init.app_mode == LIO_DRV_NIC_APP) {
		oct->fw_info.max_nic_ports =
		    (uint32_t)recv_pkt->rh.r_core_drv_init.max_nic_ports;
		oct->fw_info.num_gmx_ports =
		    (uint32_t)recv_pkt->rh.r_core_drv_init.num_gmx_ports;
	}

	if (oct->fw_info.max_nic_ports < num_nic_ports) {
		lio_dev_err(oct, "Config has more ports than firmware allows (%d > %d).\n",
			    num_nic_ports, oct->fw_info.max_nic_ports);
		goto core_drv_init_err;
	}

	oct->fw_info.app_cap_flags = recv_pkt->rh.r_core_drv_init.app_cap_flags;
	oct->fw_info.app_mode = (uint32_t)recv_pkt->rh.r_core_drv_init.app_mode;
	oct->pfvf_hsword.app_mode =
	    (uint32_t)recv_pkt->rh.r_core_drv_init.app_mode;

	oct->pfvf_hsword.pkind = recv_pkt->rh.r_core_drv_init.pkind;

	for (i = 0; i < oct->num_iqs; i++)
		oct->instr_queue[i]->txpciq.s.pkind = oct->pfvf_hsword.pkind;

	atomic_store_rel_int(&oct->status, LIO_DEV_CORE_OK);

	cs = &core_setup[oct->octeon_id];

	if (recv_pkt->buffer_size[0] != (sizeof(*cs) + LIO_DROQ_INFO_SIZE)) {
		lio_dev_dbg(oct, "Core setup bytes expected %llu found %d\n",
			    LIO_CAST64(sizeof(*cs) + LIO_DROQ_INFO_SIZE),
			    recv_pkt->buffer_size[0]);
	}

	memcpy(cs, recv_pkt->buffer_ptr[0]->m_data + LIO_DROQ_INFO_SIZE,
	       sizeof(*cs));
	strncpy(oct->boardinfo.name, cs->boardname, LIO_BOARD_NAME);
	strncpy(oct->boardinfo.serial_number, cs->board_serial_number,
		LIO_SERIAL_NUM_LEN);

	lio_swap_8B_data((uint64_t *)cs, (sizeof(*cs) >> 3));

	oct->boardinfo.major = cs->board_rev_major;
	oct->boardinfo.minor = cs->board_rev_minor;

	lio_dev_info(oct, "Running %s (%llu Hz)\n", app_name,
		     LIO_CAST64(cs->corefreq));

core_drv_init_err:
	for (i = 0; i < recv_pkt->buffer_count; i++)
		lio_recv_buffer_free(recv_pkt->buffer_ptr[i]);

	lio_free_recv_info(recv_info);
	return (0);
}

int
lio_get_tx_qsize(struct octeon_device *oct, uint32_t q_no)
{

	if ((oct != NULL) && (q_no < (uint32_t)LIO_MAX_INSTR_QUEUES(oct)) &&
	    (oct->io_qmask.iq & BIT_ULL(q_no)))
		return (oct->instr_queue[q_no]->max_count);


	return (-1);
}

int
lio_get_rx_qsize(struct octeon_device *oct, uint32_t q_no)
{

	if ((oct != NULL) && (q_no < (uint32_t)LIO_MAX_OUTPUT_QUEUES(oct)) &&
	    (oct->io_qmask.oq & BIT_ULL(q_no)))
		return (oct->droq[q_no]->max_count);

	return (-1);
}

/* Returns the host firmware handshake OCTEON specific configuration */
struct lio_config *
lio_get_conf(struct octeon_device *oct)
{
	struct lio_config	*default_oct_conf = NULL;

	/*
	 * check the OCTEON Device model & return the corresponding octeon
	 * configuration
	 */
	if (LIO_CN23XX_PF(oct)) {
		default_oct_conf = (struct lio_config *)(
					       LIO_CHIP_CONF(oct, cn23xx_pf));
	}

	return (default_oct_conf);
}

/*
 *  Get the octeon device pointer.
 *  @param octeon_id  - The id for which the octeon device pointer is required.
 *  @return Success: Octeon device pointer.
 *  @return Failure: NULL.
 */
struct octeon_device *
lio_get_device(uint32_t octeon_id)
{

	if (octeon_id >= LIO_MAX_DEVICES)
		return (NULL);
	else
		return (octeon_device[octeon_id]);
}

uint64_t
lio_pci_readq(struct octeon_device *oct, uint64_t addr)
{
	uint64_t		val64;
	volatile uint32_t	val32, addrhi;

	mtx_lock(&oct->pci_win_lock);

	/*
	 * The windowed read happens when the LSB of the addr is written.
	 * So write MSB first
	 */
	addrhi = (addr >> 32);
	if (oct->chip_id == LIO_CN23XX_PF_VID)
		addrhi |= 0x00060000;
	lio_write_csr32(oct, oct->reg_list.pci_win_rd_addr_hi, addrhi);

	/* Read back to preserve ordering of writes */
	val32 = lio_read_csr32(oct, oct->reg_list.pci_win_rd_addr_hi);

	lio_write_csr32(oct, oct->reg_list.pci_win_rd_addr_lo,
			addr & 0xffffffff);
	val32 = lio_read_csr32(oct, oct->reg_list.pci_win_rd_addr_lo);

	val64 = lio_read_csr64(oct, oct->reg_list.pci_win_rd_data);

	mtx_unlock(&oct->pci_win_lock);

	return (val64);
}

void
lio_pci_writeq(struct octeon_device *oct, uint64_t val, uint64_t addr)
{
	volatile uint32_t	val32;

	mtx_lock(&oct->pci_win_lock);

	lio_write_csr64(oct, oct->reg_list.pci_win_wr_addr, addr);

	/* The write happens when the LSB is written. So write MSB first. */
	lio_write_csr32(oct, oct->reg_list.pci_win_wr_data_hi, val >> 32);
	/* Read the MSB to ensure ordering of writes. */
	val32 = lio_read_csr32(oct, oct->reg_list.pci_win_wr_data_hi);

	lio_write_csr32(oct, oct->reg_list.pci_win_wr_data_lo,
			val & 0xffffffff);

	mtx_unlock(&oct->pci_win_lock);
}

int
lio_mem_access_ok(struct octeon_device *oct)
{
	uint64_t	access_okay = 0;
	uint64_t	lmc0_reset_ctl;

	/* Check to make sure a DDR interface is enabled */
	if (LIO_CN23XX_PF(oct)) {
		lmc0_reset_ctl = lio_pci_readq(oct, LIO_CN23XX_LMC0_RESET_CTL);
		access_okay =
		    (lmc0_reset_ctl & LIO_CN23XX_LMC0_RESET_CTL_DDR3RST_MASK);
	}

	return (access_okay ? 0 : 1);
}

int
lio_wait_for_ddr_init(struct octeon_device *oct, unsigned long *timeout)
{
	int		ret = 1;
	uint32_t	ms;

	if (timeout == NULL)
		return (ret);

	for (ms = 0; ret && ((*timeout == 0) || (ms <= *timeout)); ms += 100) {
		ret = lio_mem_access_ok(oct);

		/* wait 100 ms */
		if (ret)
			lio_sleep_timeout(100);
	}

	return (ret);
}

/*
 *  Get the octeon id assigned to the octeon device passed as argument.
 *  This function is exported to other modules.
 *  @param dev - octeon device pointer passed as a void *.
 *  @return octeon device id
 */
int
lio_get_device_id(void *dev)
{
	struct octeon_device	*octeon_dev = (struct octeon_device *)dev;
	uint32_t		i;

	for (i = 0; i < LIO_MAX_DEVICES; i++)
		if (octeon_device[i] == octeon_dev)
			return (octeon_dev->octeon_id);

	return (-1);
}

void
lio_enable_irq(struct lio_droq *droq, struct lio_instr_queue *iq)
{
	struct octeon_device *oct = NULL;
	uint64_t	instr_cnt;
	uint32_t	pkts_pend;

	/* the whole thing needs to be atomic, ideally */
	if (droq != NULL) {
		oct = droq->oct_dev;
		pkts_pend = atomic_load_acq_int(&droq->pkts_pending);
		mtx_lock(&droq->lock);
		lio_write_csr32(oct, droq->pkts_sent_reg,
				droq->pkt_count - pkts_pend);
		droq->pkt_count = pkts_pend;
		/* this write needs to be flushed before we release the lock */
		__compiler_membar();
		mtx_unlock(&droq->lock);
	}

	if (iq != NULL) {
		oct = iq->oct_dev;
		mtx_lock(&iq->lock);
		lio_write_csr32(oct, iq->inst_cnt_reg, iq->pkt_in_done);
		iq->pkt_in_done = 0;
		/* this write needs to be flushed before we release the lock */
		__compiler_membar();
		mtx_unlock(&iq->lock);
	}

	/*
	 * Implementation note:
	 *
	 * SLI_PKT(x)_CNTS[RESEND] is written separately so that if an interrupt
	 * DOES occur as a result of RESEND, the DROQ lock will NOT be held.
	 *
	 * Write resend. Writing RESEND in SLI_PKTX_CNTS should be enough
	 * to trigger tx interrupts as well, if they are pending.
	 */
	if ((oct != NULL) && (LIO_CN23XX_PF(oct))) {
		if (droq != NULL)
			lio_write_csr64(oct, droq->pkts_sent_reg,
					LIO_CN23XX_INTR_RESEND);
		/* we race with firmrware here. */
		/* read and write the IN_DONE_CNTS */
		else if (iq != NULL) {
			instr_cnt = lio_read_csr64(oct, iq->inst_cnt_reg);
			lio_write_csr64(oct, iq->inst_cnt_reg,
					((instr_cnt & 0xFFFFFFFF00000000ULL) |
					 LIO_CN23XX_INTR_RESEND));
		}
	}
}
