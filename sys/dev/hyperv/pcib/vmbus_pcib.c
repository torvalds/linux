/*-
 * Copyright (c) 2016-2017 Microsoft Corp.
 * All rights reserved.
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

#ifdef NEW_PCIB

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/mutex.h>
#include <sys/errno.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/frame.h>
#include <machine/pci_cfgreg.h>
#include <machine/resource.h>

#include <sys/pciio.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>
#include <dev/pci/pcib_private.h>
#include "pcib_if.h"

#include <machine/intr_machdep.h>
#include <x86/apicreg.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/hyperv_busdma.h>
#include <dev/hyperv/include/vmbus_xact.h>
#include <dev/hyperv/vmbus/vmbus_reg.h>
#include <dev/hyperv/vmbus/vmbus_chanvar.h>

#include "vmbus_if.h"

#if __FreeBSD_version < 1100000
typedef u_long rman_res_t;
#define RM_MAX_END	(~(rman_res_t)0)
#endif

struct completion {
	unsigned int done;
	struct mtx lock;
};

static void
init_completion(struct completion *c)
{
	memset(c, 0, sizeof(*c));
	mtx_init(&c->lock, "hvcmpl", NULL, MTX_DEF);
	c->done = 0;
}

static void
free_completion(struct completion *c)
{
	mtx_destroy(&c->lock);
}

static void
complete(struct completion *c)
{
	mtx_lock(&c->lock);
	c->done++;
	mtx_unlock(&c->lock);
	wakeup(c);
}

static void
wait_for_completion(struct completion *c)
{
	mtx_lock(&c->lock);
	while (c->done == 0)
		mtx_sleep(c, &c->lock, 0, "hvwfc", 0);
	c->done--;
	mtx_unlock(&c->lock);
}

#define PCI_MAKE_VERSION(major, minor) ((uint32_t)(((major) << 16) | (major)))

enum {
	PCI_PROTOCOL_VERSION_1_1 = PCI_MAKE_VERSION(1, 1),
	PCI_PROTOCOL_VERSION_CURRENT = PCI_PROTOCOL_VERSION_1_1
};

#define PCI_CONFIG_MMIO_LENGTH	0x2000
#define CFG_PAGE_OFFSET 0x1000
#define CFG_PAGE_SIZE (PCI_CONFIG_MMIO_LENGTH - CFG_PAGE_OFFSET)

/*
 * Message Types
 */

enum pci_message_type {
	/*
	 * Version 1.1
	 */
	PCI_MESSAGE_BASE                = 0x42490000,
	PCI_BUS_RELATIONS               = PCI_MESSAGE_BASE + 0,
	PCI_QUERY_BUS_RELATIONS         = PCI_MESSAGE_BASE + 1,
	PCI_POWER_STATE_CHANGE          = PCI_MESSAGE_BASE + 4,
	PCI_QUERY_RESOURCE_REQUIREMENTS = PCI_MESSAGE_BASE + 5,
	PCI_QUERY_RESOURCE_RESOURCES    = PCI_MESSAGE_BASE + 6,
	PCI_BUS_D0ENTRY                 = PCI_MESSAGE_BASE + 7,
	PCI_BUS_D0EXIT                  = PCI_MESSAGE_BASE + 8,
	PCI_READ_BLOCK                  = PCI_MESSAGE_BASE + 9,
	PCI_WRITE_BLOCK                 = PCI_MESSAGE_BASE + 0xA,
	PCI_EJECT                       = PCI_MESSAGE_BASE + 0xB,
	PCI_QUERY_STOP                  = PCI_MESSAGE_BASE + 0xC,
	PCI_REENABLE                    = PCI_MESSAGE_BASE + 0xD,
	PCI_QUERY_STOP_FAILED           = PCI_MESSAGE_BASE + 0xE,
	PCI_EJECTION_COMPLETE           = PCI_MESSAGE_BASE + 0xF,
	PCI_RESOURCES_ASSIGNED          = PCI_MESSAGE_BASE + 0x10,
	PCI_RESOURCES_RELEASED          = PCI_MESSAGE_BASE + 0x11,
	PCI_INVALIDATE_BLOCK            = PCI_MESSAGE_BASE + 0x12,
	PCI_QUERY_PROTOCOL_VERSION      = PCI_MESSAGE_BASE + 0x13,
	PCI_CREATE_INTERRUPT_MESSAGE    = PCI_MESSAGE_BASE + 0x14,
	PCI_DELETE_INTERRUPT_MESSAGE    = PCI_MESSAGE_BASE + 0x15,
	PCI_MESSAGE_MAXIMUM
};

/*
 * Structures defining the virtual PCI Express protocol.
 */

union pci_version {
	struct {
		uint16_t minor_version;
		uint16_t major_version;
	} parts;
	uint32_t version;
} __packed;

/*
 * This representation is the one used in Windows, which is
 * what is expected when sending this back and forth with
 * the Hyper-V parent partition.
 */
union win_slot_encoding {
	struct {
		uint32_t	slot:5;
		uint32_t	func:3;
		uint32_t	reserved:24;
	} bits;
	uint32_t val;
} __packed;

struct pci_func_desc {
	uint16_t	v_id;	/* vendor ID */
	uint16_t	d_id;	/* device ID */
	uint8_t		rev;
	uint8_t		prog_intf;
	uint8_t		subclass;
	uint8_t		base_class;
	uint32_t	subsystem_id;
	union win_slot_encoding wslot;
	uint32_t	ser;	/* serial number */
} __packed;

struct hv_msi_desc {
	uint8_t		vector;
	uint8_t		delivery_mode;
	uint16_t	vector_count;
	uint32_t	reserved;
	uint64_t	cpu_mask;
} __packed;

struct tran_int_desc {
	uint16_t	reserved;
	uint16_t	vector_count;
	uint32_t	data;
	uint64_t	address;
} __packed;

struct pci_message {
	uint32_t type;
} __packed;

struct pci_child_message {
	struct pci_message message_type;
	union win_slot_encoding wslot;
} __packed;

struct pci_incoming_message {
	struct vmbus_chanpkt_hdr hdr;
	struct pci_message message_type;
} __packed;

struct pci_response {
	struct vmbus_chanpkt_hdr hdr;
	int32_t status;	/* negative values are failures */
} __packed;

struct pci_packet {
	void (*completion_func)(void *context, struct pci_response *resp,
	    int resp_packet_size);
	void *compl_ctxt;

	struct pci_message message[0];
};

/*
 * Specific message types supporting the PCI protocol.
 */

struct pci_version_request {
	struct pci_message message_type;
	uint32_t protocol_version;
	uint32_t is_last_attempt:1;
	uint32_t reservedz:31;
} __packed;

struct pci_bus_d0_entry {
	struct pci_message message_type;
	uint32_t reserved;
	uint64_t mmio_base;
} __packed;

struct pci_bus_relations {
	struct pci_incoming_message incoming;
	uint32_t device_count;
	struct pci_func_desc func[0];
} __packed;

#define MAX_NUM_BARS	(PCIR_MAX_BAR_0 + 1)
struct pci_q_res_req_response {
	struct vmbus_chanpkt_hdr hdr;
	int32_t status; /* negative values are failures */
	uint32_t probed_bar[MAX_NUM_BARS];
} __packed;

struct pci_resources_assigned {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	uint8_t memory_range[0x14][MAX_NUM_BARS]; /* unused here */
	uint32_t msi_descriptors;
	uint32_t reserved[4];
} __packed;

struct pci_create_interrupt {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	struct hv_msi_desc int_desc;
} __packed;

struct pci_create_int_response {
	struct pci_response response;
	uint32_t reserved;
	struct tran_int_desc int_desc;
} __packed;

struct pci_delete_interrupt {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	struct tran_int_desc int_desc;
} __packed;

struct pci_dev_incoming {
	struct pci_incoming_message incoming;
	union win_slot_encoding wslot;
} __packed;

struct pci_eject_response {
	struct pci_message message_type;
	union win_slot_encoding wslot;
	uint32_t status;
} __packed;

/*
 * Driver specific state.
 */

enum hv_pcibus_state {
	hv_pcibus_init = 0,
	hv_pcibus_installed,
};

struct hv_pcibus {
	device_t pcib;
	device_t pci_bus;
	struct vmbus_pcib_softc *sc;

	uint16_t pci_domain;

	enum hv_pcibus_state state;

	struct resource *cfg_res;

	struct completion query_completion, *query_comp;

	struct mtx config_lock; /* Avoid two threads writing index page */
	struct mtx device_list_lock;    /* Protect lists below */
	TAILQ_HEAD(, hv_pci_dev) children;
	TAILQ_HEAD(, hv_dr_state) dr_list;

	volatile int detaching;
};

struct hv_pci_dev {
	TAILQ_ENTRY(hv_pci_dev) link;

	struct pci_func_desc desc;

	bool reported_missing;

	struct hv_pcibus *hbus;
	struct task eject_task;

	TAILQ_HEAD(, hv_irq_desc) irq_desc_list;

	/*
	 * What would be observed if one wrote 0xFFFFFFFF to a BAR and then
	 * read it back, for each of the BAR offsets within config space.
	 */
	uint32_t probed_bar[MAX_NUM_BARS];
};

/*
 * Tracks "Device Relations" messages from the host, which must be both
 * processed in order.
 */
struct hv_dr_work {
	struct task task;
	struct hv_pcibus *bus;
};

struct hv_dr_state {
	TAILQ_ENTRY(hv_dr_state) link;
	uint32_t device_count;
	struct pci_func_desc func[0];
};

struct hv_irq_desc {
	TAILQ_ENTRY(hv_irq_desc) link;
	struct tran_int_desc desc;
	int irq;
};

#define PCI_DEVFN(slot, func)   ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)         ((devfn) & 0x07)

static uint32_t
devfn_to_wslot(unsigned int devfn)
{
	union win_slot_encoding wslot;

	wslot.val = 0;
	wslot.bits.slot = PCI_SLOT(devfn);
	wslot.bits.func = PCI_FUNC(devfn);

	return (wslot.val);
}

static unsigned int
wslot_to_devfn(uint32_t wslot)
{
	union win_slot_encoding encoding;
	unsigned int slot;
	unsigned int func;

	encoding.val = wslot;

	slot = encoding.bits.slot;
	func = encoding.bits.func;

	return (PCI_DEVFN(slot, func));
}

struct vmbus_pcib_softc {
	struct vmbus_channel	*chan;
	void *rx_buf;

	struct taskqueue	*taskq;

	struct hv_pcibus	*hbus;
};

/* {44C4F61D-4444-4400-9D52-802E27EDE19F} */
static const struct hyperv_guid g_pass_through_dev_type = {
	.hv_guid = {0x1D, 0xF6, 0xC4, 0x44, 0x44, 0x44, 0x00, 0x44,
	    0x9D, 0x52, 0x80, 0x2E, 0x27, 0xED, 0xE1, 0x9F}
};

struct hv_pci_compl {
	struct completion host_event;
	int32_t completion_status;
};

struct q_res_req_compl {
	struct completion host_event;
	struct hv_pci_dev *hpdev;
};

struct compose_comp_ctxt {
	struct hv_pci_compl comp_pkt;
	struct tran_int_desc int_desc;
};

static void
hv_pci_generic_compl(void *context, struct pci_response *resp,
    int resp_packet_size)
{
	struct hv_pci_compl *comp_pkt = context;

	if (resp_packet_size >= sizeof(struct pci_response))
		comp_pkt->completion_status = resp->status;
	else
		comp_pkt->completion_status = -1;

	complete(&comp_pkt->host_event);
}

static void
q_resource_requirements(void *context, struct pci_response *resp,
    int resp_packet_size)
{
	struct q_res_req_compl *completion = context;
	struct pci_q_res_req_response *q_res_req =
	    (struct pci_q_res_req_response *)resp;
	int i;

	if (resp->status < 0) {
		printf("vmbus_pcib: failed to query resource requirements\n");
	} else {
		for (i = 0; i < MAX_NUM_BARS; i++)
			completion->hpdev->probed_bar[i] =
			    q_res_req->probed_bar[i];
	}

	complete(&completion->host_event);
}

static void
hv_pci_compose_compl(void *context, struct pci_response *resp,
    int resp_packet_size)
{
	struct compose_comp_ctxt *comp_pkt = context;
	struct pci_create_int_response *int_resp =
	    (struct pci_create_int_response *)resp;

	comp_pkt->comp_pkt.completion_status = resp->status;
	comp_pkt->int_desc = int_resp->int_desc;
	complete(&comp_pkt->comp_pkt.host_event);
}

static void
hv_int_desc_free(struct hv_pci_dev *hpdev, struct hv_irq_desc *hid)
{
	struct pci_delete_interrupt *int_pkt;
	struct {
		struct pci_packet pkt;
		uint8_t buffer[sizeof(struct pci_delete_interrupt)];
	} ctxt;

	memset(&ctxt, 0, sizeof(ctxt));
	int_pkt = (struct pci_delete_interrupt *)&ctxt.pkt.message;
	int_pkt->message_type.type = PCI_DELETE_INTERRUPT_MESSAGE;
	int_pkt->wslot.val = hpdev->desc.wslot.val;
	int_pkt->int_desc = hid->desc;

	vmbus_chan_send(hpdev->hbus->sc->chan, VMBUS_CHANPKT_TYPE_INBAND, 0,
	    int_pkt, sizeof(*int_pkt), 0);

	free(hid, M_DEVBUF);
}

static void
hv_pci_delete_device(struct hv_pci_dev *hpdev)
{
	struct hv_pcibus *hbus = hpdev->hbus;
	struct hv_irq_desc *hid, *tmp_hid;
	device_t pci_dev;
	int devfn;

	devfn = wslot_to_devfn(hpdev->desc.wslot.val);

	mtx_lock(&Giant);

	pci_dev = pci_find_dbsf(hbus->pci_domain,
	    0, PCI_SLOT(devfn), PCI_FUNC(devfn));
	if (pci_dev)
		device_delete_child(hbus->pci_bus, pci_dev);

	mtx_unlock(&Giant);

	mtx_lock(&hbus->device_list_lock);
	TAILQ_REMOVE(&hbus->children, hpdev, link);
	mtx_unlock(&hbus->device_list_lock);

	TAILQ_FOREACH_SAFE(hid, &hpdev->irq_desc_list, link, tmp_hid)
		hv_int_desc_free(hpdev, hid);

	free(hpdev, M_DEVBUF);
}

static struct hv_pci_dev *
new_pcichild_device(struct hv_pcibus *hbus, struct pci_func_desc *desc)
{
	struct hv_pci_dev *hpdev;
	struct pci_child_message *res_req;
	struct q_res_req_compl comp_pkt;
	struct {
		struct pci_packet pkt;
		uint8_t buffer[sizeof(struct pci_child_message)];
	} ctxt;
	int ret;

	hpdev = malloc(sizeof(*hpdev), M_DEVBUF, M_WAITOK | M_ZERO);
	hpdev->hbus = hbus;

	TAILQ_INIT(&hpdev->irq_desc_list);

	init_completion(&comp_pkt.host_event);
	comp_pkt.hpdev = hpdev;

	ctxt.pkt.compl_ctxt = &comp_pkt;
	ctxt.pkt.completion_func = q_resource_requirements;

	res_req = (struct pci_child_message *)&ctxt.pkt.message;
	res_req->message_type.type = PCI_QUERY_RESOURCE_REQUIREMENTS;
	res_req->wslot.val = desc->wslot.val;

	ret = vmbus_chan_send(hbus->sc->chan,
	    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
	    res_req, sizeof(*res_req), (uint64_t)(uintptr_t)&ctxt.pkt);
	if (ret)
		goto err;

	wait_for_completion(&comp_pkt.host_event);
	free_completion(&comp_pkt.host_event);

	hpdev->desc = *desc;

	mtx_lock(&hbus->device_list_lock);
	if (TAILQ_EMPTY(&hbus->children))
		hbus->pci_domain = desc->ser & 0xFFFF;
	TAILQ_INSERT_TAIL(&hbus->children, hpdev, link);
	mtx_unlock(&hbus->device_list_lock);
	return (hpdev);
err:
	free_completion(&comp_pkt.host_event);
	free(hpdev, M_DEVBUF);
	return (NULL);
}

#if __FreeBSD_version < 1100000

/* Old versions don't have BUS_RESCAN(). Let's copy it from FreeBSD 11. */

static struct pci_devinfo *
pci_identify_function(device_t pcib, device_t dev, int domain, int busno,
    int slot, int func, size_t dinfo_size)
{
	struct pci_devinfo *dinfo;

	dinfo = pci_read_device(pcib, domain, busno, slot, func, dinfo_size);
	if (dinfo != NULL)
		pci_add_child(dev, dinfo);

	return (dinfo);
}

static int
pci_rescan(device_t dev)
{
#define	REG(n, w)	PCIB_READ_CONFIG(pcib, busno, s, f, n, w)
	device_t pcib = device_get_parent(dev);
	struct pci_softc *sc;
	device_t child, *devlist, *unchanged;
	int devcount, error, i, j, maxslots, oldcount;
	int busno, domain, s, f, pcifunchigh;
	uint8_t hdrtype;

	/* No need to check for ARI on a rescan. */
	error = device_get_children(dev, &devlist, &devcount);
	if (error)
		return (error);
	if (devcount != 0) {
		unchanged = malloc(devcount * sizeof(device_t), M_TEMP,
		    M_NOWAIT | M_ZERO);
		if (unchanged == NULL) {
			free(devlist, M_TEMP);
			return (ENOMEM);
		}
	} else
		unchanged = NULL;

	sc = device_get_softc(dev);
	domain = pcib_get_domain(dev);
	busno = pcib_get_bus(dev);
	maxslots = PCIB_MAXSLOTS(pcib);
	for (s = 0; s <= maxslots; s++) {
		/* If function 0 is not present, skip to the next slot. */
		f = 0;
		if (REG(PCIR_VENDOR, 2) == 0xffff)
			continue;
		pcifunchigh = 0;
		hdrtype = REG(PCIR_HDRTYPE, 1);
		if ((hdrtype & PCIM_HDRTYPE) > PCI_MAXHDRTYPE)
			continue;
		if (hdrtype & PCIM_MFDEV)
			pcifunchigh = PCIB_MAXFUNCS(pcib);
		for (f = 0; f <= pcifunchigh; f++) {
			if (REG(PCIR_VENDOR, 2) == 0xffff)
				continue;

			/*
			 * Found a valid function.  Check if a
			 * device_t for this device already exists.
			 */
			for (i = 0; i < devcount; i++) {
				child = devlist[i];
				if (child == NULL)
					continue;
				if (pci_get_slot(child) == s &&
				    pci_get_function(child) == f) {
					unchanged[i] = child;
					goto next_func;
				}
			}

			pci_identify_function(pcib, dev, domain, busno, s, f,
			    sizeof(struct pci_devinfo));
		next_func:;
		}
	}

	/* Remove devices that are no longer present. */
	for (i = 0; i < devcount; i++) {
		if (unchanged[i] != NULL)
			continue;
		device_delete_child(dev, devlist[i]);
	}

	free(devlist, M_TEMP);
	oldcount = devcount;

	/* Try to attach the devices just added. */
	error = device_get_children(dev, &devlist, &devcount);
	if (error) {
		free(unchanged, M_TEMP);
		return (error);
	}

	for (i = 0; i < devcount; i++) {
		for (j = 0; j < oldcount; j++) {
			if (devlist[i] == unchanged[j])
				goto next_device;
		}

		device_probe_and_attach(devlist[i]);
	next_device:;
	}

	free(unchanged, M_TEMP);
	free(devlist, M_TEMP);
	return (0);
#undef REG
}

#else

static int
pci_rescan(device_t dev)
{
	return (BUS_RESCAN(dev));
}

#endif

static void
pci_devices_present_work(void *arg, int pending __unused)
{
	struct hv_dr_work *dr_wrk = arg;
	struct hv_dr_state *dr = NULL;
	struct hv_pcibus *hbus;
	uint32_t child_no;
	bool found;
	struct pci_func_desc *new_desc;
	struct hv_pci_dev *hpdev, *tmp_hpdev;
	struct completion *query_comp;
	bool need_rescan = false;

	hbus = dr_wrk->bus;
	free(dr_wrk, M_DEVBUF);

	/* Pull this off the queue and process it if it was the last one. */
	mtx_lock(&hbus->device_list_lock);
	while (!TAILQ_EMPTY(&hbus->dr_list)) {
		dr = TAILQ_FIRST(&hbus->dr_list);
		TAILQ_REMOVE(&hbus->dr_list, dr, link);

		/* Throw this away if the list still has stuff in it. */
		if (!TAILQ_EMPTY(&hbus->dr_list)) {
			free(dr, M_DEVBUF);
			continue;
		}
	}
	mtx_unlock(&hbus->device_list_lock);

	if (!dr)
		return;

	/* First, mark all existing children as reported missing. */
	mtx_lock(&hbus->device_list_lock);
	TAILQ_FOREACH(hpdev, &hbus->children, link)
		hpdev->reported_missing = true;
	mtx_unlock(&hbus->device_list_lock);

	/* Next, add back any reported devices. */
	for (child_no = 0; child_no < dr->device_count; child_no++) {
		found = false;
		new_desc = &dr->func[child_no];

		mtx_lock(&hbus->device_list_lock);
		TAILQ_FOREACH(hpdev, &hbus->children, link) {
			if ((hpdev->desc.wslot.val ==
			    new_desc->wslot.val) &&
			    (hpdev->desc.v_id == new_desc->v_id) &&
			    (hpdev->desc.d_id == new_desc->d_id) &&
			    (hpdev->desc.ser == new_desc->ser)) {
				hpdev->reported_missing = false;
				found = true;
				break;
			}
		}
		mtx_unlock(&hbus->device_list_lock);

		if (!found) {
			if (!need_rescan)
				need_rescan = true;

			hpdev = new_pcichild_device(hbus, new_desc);
			if (!hpdev)
				printf("vmbus_pcib: failed to add a child\n");
		}
	}

	/* Remove missing device(s), if any */
	TAILQ_FOREACH_SAFE(hpdev, &hbus->children, link, tmp_hpdev) {
		if (hpdev->reported_missing)
			hv_pci_delete_device(hpdev);
	}

	/* Rescan the bus to find any new device, if necessary. */
	if (hbus->state == hv_pcibus_installed && need_rescan)
		pci_rescan(hbus->pci_bus);

	/* Wake up hv_pci_query_relations(), if it's waiting. */
	query_comp = hbus->query_comp;
	if (query_comp) {
		hbus->query_comp = NULL;
		complete(query_comp);
	}

	free(dr, M_DEVBUF);
}

static struct hv_pci_dev *
get_pcichild_wslot(struct hv_pcibus *hbus, uint32_t wslot)
{
	struct hv_pci_dev *hpdev, *ret = NULL;

	mtx_lock(&hbus->device_list_lock);
	TAILQ_FOREACH(hpdev, &hbus->children, link) {
		if (hpdev->desc.wslot.val == wslot) {
			ret = hpdev;
			break;
		}
	}
	mtx_unlock(&hbus->device_list_lock);

	return (ret);
}

static void
hv_pci_devices_present(struct hv_pcibus *hbus,
    struct pci_bus_relations *relations)
{
	struct hv_dr_state *dr;
	struct hv_dr_work *dr_wrk;
	unsigned long dr_size;

	if (hbus->detaching && relations->device_count > 0)
		return;

	dr_size = offsetof(struct hv_dr_state, func) +
	    (sizeof(struct pci_func_desc) * relations->device_count);
	dr = malloc(dr_size, M_DEVBUF, M_WAITOK | M_ZERO);

	dr->device_count = relations->device_count;
	if (dr->device_count != 0)
		memcpy(dr->func, relations->func,
		    sizeof(struct pci_func_desc) * dr->device_count);

	mtx_lock(&hbus->device_list_lock);
	TAILQ_INSERT_TAIL(&hbus->dr_list, dr, link);
	mtx_unlock(&hbus->device_list_lock);

	dr_wrk = malloc(sizeof(*dr_wrk), M_DEVBUF, M_WAITOK | M_ZERO);
	dr_wrk->bus = hbus;
	TASK_INIT(&dr_wrk->task, 0, pci_devices_present_work, dr_wrk);
	taskqueue_enqueue(hbus->sc->taskq, &dr_wrk->task);
}

static void
hv_eject_device_work(void *arg, int pending __unused)
{
	struct hv_pci_dev *hpdev = arg;
	union win_slot_encoding wslot = hpdev->desc.wslot;
	struct hv_pcibus *hbus = hpdev->hbus;
	struct pci_eject_response *eject_pkt;
	struct {
		struct pci_packet pkt;
		uint8_t buffer[sizeof(struct pci_eject_response)];
	} ctxt;

	hv_pci_delete_device(hpdev);

	memset(&ctxt, 0, sizeof(ctxt));
	eject_pkt = (struct pci_eject_response *)&ctxt.pkt.message;
	eject_pkt->message_type.type = PCI_EJECTION_COMPLETE;
	eject_pkt->wslot.val = wslot.val;
	vmbus_chan_send(hbus->sc->chan, VMBUS_CHANPKT_TYPE_INBAND, 0,
	    eject_pkt, sizeof(*eject_pkt), 0);
}

static void
hv_pci_eject_device(struct hv_pci_dev *hpdev)
{
	struct hv_pcibus *hbus = hpdev->hbus;
	struct taskqueue *taskq;

	if (hbus->detaching)
		return;

	/*
	 * Push this task into the same taskqueue on which
	 * vmbus_pcib_attach() runs, so we're sure this task can't run
	 * concurrently with vmbus_pcib_attach().
	 */
	TASK_INIT(&hpdev->eject_task, 0, hv_eject_device_work, hpdev);
	taskq = vmbus_chan_mgmt_tq(hbus->sc->chan);
	taskqueue_enqueue(taskq, &hpdev->eject_task);
}

#define PCIB_PACKET_SIZE	0x100

static void
vmbus_pcib_on_channel_callback(struct vmbus_channel *chan, void *arg)
{
	struct vmbus_pcib_softc *sc = arg;
	struct hv_pcibus *hbus = sc->hbus;

	void *buffer;
	int bufferlen = PCIB_PACKET_SIZE;

	struct pci_packet *comp_packet;
	struct pci_response *response;
	struct pci_incoming_message *new_msg;
	struct pci_bus_relations *bus_rel;
	struct pci_dev_incoming *dev_msg;
	struct hv_pci_dev *hpdev;

	buffer = sc->rx_buf;
	do {
		struct vmbus_chanpkt_hdr *pkt = buffer;
		uint32_t bytes_rxed;
		int ret;

		bytes_rxed = bufferlen;
		ret = vmbus_chan_recv_pkt(chan, pkt, &bytes_rxed);

		if (ret == ENOBUFS) {
			/* Handle large packet */
			if (bufferlen > PCIB_PACKET_SIZE) {
				free(buffer, M_DEVBUF);
				buffer = NULL;
			}

			/* alloc new buffer */
			buffer = malloc(bytes_rxed, M_DEVBUF, M_WAITOK | M_ZERO);
			bufferlen = bytes_rxed;

			continue;
		}

		if (ret != 0) {
			/* ignore EIO or EAGAIN */
			break;
		}

		if (bytes_rxed <= sizeof(struct pci_response))
			continue;

		switch (pkt->cph_type) {
		case VMBUS_CHANPKT_TYPE_COMP:
			comp_packet =
			    (struct pci_packet *)(uintptr_t)pkt->cph_xactid;
			response = (struct pci_response *)pkt;
			comp_packet->completion_func(comp_packet->compl_ctxt,
			    response, bytes_rxed);
			break;
		case VMBUS_CHANPKT_TYPE_INBAND:
			new_msg = (struct pci_incoming_message *)buffer;

			switch (new_msg->message_type.type) {
			case PCI_BUS_RELATIONS:
				bus_rel = (struct pci_bus_relations *)buffer;

				if (bus_rel->device_count == 0)
					break;

				if (bytes_rxed <
				    offsetof(struct pci_bus_relations, func) +
				        (sizeof(struct pci_func_desc) *
				            (bus_rel->device_count)))
					break;

				hv_pci_devices_present(hbus, bus_rel);
				break;

			case PCI_EJECT:
				dev_msg = (struct pci_dev_incoming *)buffer;
				hpdev = get_pcichild_wslot(hbus,
				    dev_msg->wslot.val);

				if (hpdev)
					hv_pci_eject_device(hpdev);

				break;
			default:
				printf("vmbus_pcib: Unknown msg type 0x%x\n",
				    new_msg->message_type.type);
				break;
			}
			break;
		default:
			printf("vmbus_pcib: Unknown VMBus msg type %hd\n",
			    pkt->cph_type);
			break;
		}
	} while (1);

	if (bufferlen > PCIB_PACKET_SIZE)
		free(buffer, M_DEVBUF);
}

static int
hv_pci_protocol_negotiation(struct hv_pcibus *hbus)
{
	struct pci_version_request *version_req;
	struct hv_pci_compl comp_pkt;
	struct {
		struct pci_packet pkt;
		uint8_t buffer[sizeof(struct pci_version_request)];
	} ctxt;
	int ret;

	init_completion(&comp_pkt.host_event);

	ctxt.pkt.completion_func = hv_pci_generic_compl;
	ctxt.pkt.compl_ctxt = &comp_pkt;
	version_req = (struct pci_version_request *)&ctxt.pkt.message;
	version_req->message_type.type = PCI_QUERY_PROTOCOL_VERSION;
	version_req->protocol_version = PCI_PROTOCOL_VERSION_CURRENT;
	version_req->is_last_attempt = 1;

	ret = vmbus_chan_send(hbus->sc->chan, VMBUS_CHANPKT_TYPE_INBAND,
	    VMBUS_CHANPKT_FLAG_RC, version_req, sizeof(*version_req),
	    (uint64_t)(uintptr_t)&ctxt.pkt);
	if (ret)
		goto out;

	wait_for_completion(&comp_pkt.host_event);

	if (comp_pkt.completion_status < 0) {
		device_printf(hbus->pcib,
		    "vmbus_pcib version negotiation failed: %x\n",
		    comp_pkt.completion_status);
		ret = EPROTO;
	} else {
		ret = 0;
	}
out:
	free_completion(&comp_pkt.host_event);
	return (ret);
}

/* Ask the host to send along the list of child devices */
static int
hv_pci_query_relations(struct hv_pcibus *hbus)
{
	struct pci_message message;
	int ret;

	message.type = PCI_QUERY_BUS_RELATIONS;
	ret = vmbus_chan_send(hbus->sc->chan, VMBUS_CHANPKT_TYPE_INBAND, 0,
	    &message, sizeof(message), 0);
	return (ret);
}

static int
hv_pci_enter_d0(struct hv_pcibus *hbus)
{
	struct pci_bus_d0_entry *d0_entry;
	struct hv_pci_compl comp_pkt;
	struct {
		struct pci_packet pkt;
		uint8_t buffer[sizeof(struct pci_bus_d0_entry)];
	} ctxt;
	int ret;

	/*
	 * Tell the host that the bus is ready to use, and moved into the
	 * powered-on state.  This includes telling the host which region
	 * of memory-mapped I/O space has been chosen for configuration space
	 * access.
	 */
	init_completion(&comp_pkt.host_event);

	ctxt.pkt.completion_func = hv_pci_generic_compl;
	ctxt.pkt.compl_ctxt = &comp_pkt;

	d0_entry = (struct pci_bus_d0_entry *)&ctxt.pkt.message;
	memset(d0_entry, 0, sizeof(*d0_entry));
	d0_entry->message_type.type = PCI_BUS_D0ENTRY;
	d0_entry->mmio_base = rman_get_start(hbus->cfg_res);

	ret = vmbus_chan_send(hbus->sc->chan, VMBUS_CHANPKT_TYPE_INBAND,
	    VMBUS_CHANPKT_FLAG_RC, d0_entry, sizeof(*d0_entry),
	    (uint64_t)(uintptr_t)&ctxt.pkt);
	if (ret)
		goto out;

	wait_for_completion(&comp_pkt.host_event);

	if (comp_pkt.completion_status < 0) {
		device_printf(hbus->pcib, "vmbus_pcib failed to enable D0\n");
		ret = EPROTO;
	} else {
		ret = 0;
	}

out:
	free_completion(&comp_pkt.host_event);
	return (ret);
}

/*
 * It looks this is only needed by Windows VM, but let's send the message too
 * just to make the host happy.
 */
static int
hv_send_resources_allocated(struct hv_pcibus *hbus)
{
	struct pci_resources_assigned *res_assigned;
	struct hv_pci_compl comp_pkt;
	struct hv_pci_dev *hpdev;
	struct pci_packet *pkt;
	uint32_t wslot;
	int ret = 0;

	pkt = malloc(sizeof(*pkt) + sizeof(*res_assigned),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	for (wslot = 0; wslot < 256; wslot++) {
		hpdev = get_pcichild_wslot(hbus, wslot);
		if (!hpdev)
			continue;

		init_completion(&comp_pkt.host_event);

		memset(pkt, 0, sizeof(*pkt) + sizeof(*res_assigned));
		pkt->completion_func = hv_pci_generic_compl;
		pkt->compl_ctxt = &comp_pkt;

		res_assigned = (struct pci_resources_assigned *)&pkt->message;
		res_assigned->message_type.type = PCI_RESOURCES_ASSIGNED;
		res_assigned->wslot.val = hpdev->desc.wslot.val;

		ret = vmbus_chan_send(hbus->sc->chan,
		    VMBUS_CHANPKT_TYPE_INBAND, VMBUS_CHANPKT_FLAG_RC,
		    &pkt->message, sizeof(*res_assigned),
		    (uint64_t)(uintptr_t)pkt);
		if (ret) {
			free_completion(&comp_pkt.host_event);
			break;
		}

		wait_for_completion(&comp_pkt.host_event);
		free_completion(&comp_pkt.host_event);

		if (comp_pkt.completion_status < 0) {
			ret = EPROTO;
			device_printf(hbus->pcib,
			    "failed to send PCI_RESOURCES_ASSIGNED\n");
			break;
		}
	}

	free(pkt, M_DEVBUF);
	return (ret);
}

static int
hv_send_resources_released(struct hv_pcibus *hbus)
{
	struct pci_child_message pkt;
	struct hv_pci_dev *hpdev;
	uint32_t wslot;
	int ret;

	for (wslot = 0; wslot < 256; wslot++) {
		hpdev = get_pcichild_wslot(hbus, wslot);
		if (!hpdev)
			continue;

		pkt.message_type.type = PCI_RESOURCES_RELEASED;
		pkt.wslot.val = hpdev->desc.wslot.val;

		ret = vmbus_chan_send(hbus->sc->chan,
		    VMBUS_CHANPKT_TYPE_INBAND, 0, &pkt, sizeof(pkt), 0);
		if (ret)
			return (ret);
	}

	return (0);
}

#define hv_cfg_read(x, s)						\
static inline uint##x##_t hv_cfg_read_##s(struct hv_pcibus *bus,	\
    bus_size_t offset)							\
{									\
	return (bus_read_##s(bus->cfg_res, offset));			\
}

#define hv_cfg_write(x, s)						\
static inline void hv_cfg_write_##s(struct hv_pcibus *bus,		\
    bus_size_t offset, uint##x##_t val)					\
{									\
	return (bus_write_##s(bus->cfg_res, offset, val));		\
}

hv_cfg_read(8, 1)
hv_cfg_read(16, 2)
hv_cfg_read(32, 4)

hv_cfg_write(8, 1)
hv_cfg_write(16, 2)
hv_cfg_write(32, 4)

static void
_hv_pcifront_read_config(struct hv_pci_dev *hpdev, int where, int size,
    uint32_t *val)
{
	struct hv_pcibus *hbus = hpdev->hbus;
	bus_size_t addr = CFG_PAGE_OFFSET + where;

	/*
	 * If the attempt is to read the IDs or the ROM BAR, simulate that.
	 */
	if (where + size <= PCIR_COMMAND) {
		memcpy(val, ((uint8_t *)&hpdev->desc.v_id) + where, size);
	} else if (where >= PCIR_REVID && where + size <=
		   PCIR_CACHELNSZ) {
		memcpy(val, ((uint8_t *)&hpdev->desc.rev) + where -
		       PCIR_REVID, size);
	} else if (where >= PCIR_SUBVEND_0 && where + size <=
		   PCIR_BIOS) {
		memcpy(val, (uint8_t *)&hpdev->desc.subsystem_id + where -
		       PCIR_SUBVEND_0, size);
	} else if (where >= PCIR_BIOS && where + size <=
		   PCIR_CAP_PTR) {
		/* ROM BARs are unimplemented */
		*val = 0;
	} else if ((where >= PCIR_INTLINE && where + size <=
		   PCIR_INTPIN) ||(where == PCIR_INTPIN && size == 1)) {
		/*
		 * Interrupt Line and Interrupt PIN are hard-wired to zero
		 * because this front-end only supports message-signaled
		 * interrupts.
		 */
		*val = 0;
	} else if (where + size <= CFG_PAGE_SIZE) {
		mtx_lock(&hbus->config_lock);

		/* Choose the function to be read. */
		hv_cfg_write_4(hbus, 0, hpdev->desc.wslot.val);

		/* Make sure the function was chosen before we start reading.*/
		mb();

		/* Read from that function's config space. */
		switch (size) {
		case 1:
			*((uint8_t *)val) = hv_cfg_read_1(hbus, addr);
			break;
		case 2:
			*((uint16_t *)val) = hv_cfg_read_2(hbus, addr);
			break;
		default:
			*((uint32_t *)val) = hv_cfg_read_4(hbus, addr);
			break;
		}
		/*
		 * Make sure the write was done before we release the lock,
		 * allowing consecutive reads/writes.
		 */
		mb();

		mtx_unlock(&hbus->config_lock);
	} else {
		/* Invalid config read: it's unlikely to reach here. */
		memset(val, 0, size);
	}
}

static void
_hv_pcifront_write_config(struct hv_pci_dev *hpdev, int where, int size,
    uint32_t val)
{
	struct hv_pcibus *hbus = hpdev->hbus;
	bus_size_t addr = CFG_PAGE_OFFSET + where;

	/* SSIDs and ROM BARs are read-only */
	if (where >= PCIR_SUBVEND_0 && where + size <= PCIR_CAP_PTR)
		return;

	if (where >= PCIR_COMMAND && where + size <= CFG_PAGE_SIZE) {
		mtx_lock(&hbus->config_lock);

		/* Choose the function to be written. */
		hv_cfg_write_4(hbus, 0, hpdev->desc.wslot.val);

		/* Make sure the function was chosen before we start writing.*/
		wmb();

		/* Write to that function's config space. */
		switch (size) {
		case 1:
			hv_cfg_write_1(hbus, addr, (uint8_t)val);
			break;
		case 2:
			hv_cfg_write_2(hbus, addr, (uint16_t)val);
			break;
		default:
			hv_cfg_write_4(hbus, addr, (uint32_t)val);
			break;
		}

		/*
		 * Make sure the write was done before we release the lock,
		 * allowing consecutive reads/writes.
		 */
		mb();

		mtx_unlock(&hbus->config_lock);
	} else {
		/* Invalid config write: it's unlikely to reach here. */
		return;
	}
}

static void
vmbus_pcib_set_detaching(void *arg, int pending __unused)
{
	struct hv_pcibus *hbus = arg;

	atomic_set_int(&hbus->detaching, 1);
}

static void
vmbus_pcib_pre_detach(struct hv_pcibus *hbus)
{
	struct task task;

	TASK_INIT(&task, 0, vmbus_pcib_set_detaching, hbus);

	/*
	 * Make sure the channel callback won't push any possible new
	 * PCI_BUS_RELATIONS and PCI_EJECT tasks to sc->taskq.
	 */
	vmbus_chan_run_task(hbus->sc->chan, &task);

	taskqueue_drain_all(hbus->sc->taskq);
}


/*
 * Standard probe entry point.
 *
 */
static int
vmbus_pcib_probe(device_t dev)
{
	if (VMBUS_PROBE_GUID(device_get_parent(dev), dev,
	    &g_pass_through_dev_type) == 0) {
		device_set_desc(dev, "Hyper-V PCI Express Pass Through");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

/*
 * Standard attach entry point.
 *
 */
static int
vmbus_pcib_attach(device_t dev)
{
	const int pci_ring_size = (4 * PAGE_SIZE);
	const struct hyperv_guid *inst_guid;
	struct vmbus_channel *channel;
	struct vmbus_pcib_softc *sc;
	struct hv_pcibus *hbus;
	int rid = 0;
	int ret;

	hbus = malloc(sizeof(*hbus), M_DEVBUF, M_WAITOK | M_ZERO);
	hbus->pcib = dev;

	channel = vmbus_get_channel(dev);
	inst_guid = vmbus_chan_guid_inst(channel);
	hbus->pci_domain = inst_guid->hv_guid[9] |
			  (inst_guid->hv_guid[8] << 8);

	mtx_init(&hbus->config_lock, "hbcfg", NULL, MTX_DEF);
	mtx_init(&hbus->device_list_lock, "hbdl", NULL, MTX_DEF);
	TAILQ_INIT(&hbus->children);
	TAILQ_INIT(&hbus->dr_list);

	hbus->cfg_res = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid,
	    0, RM_MAX_END, PCI_CONFIG_MMIO_LENGTH,
	    RF_ACTIVE | rman_make_alignment_flags(PAGE_SIZE));

	if (!hbus->cfg_res) {
		device_printf(dev, "failed to get resource for cfg window\n");
		ret = ENXIO;
		goto free_bus;
	}

	sc = device_get_softc(dev);
	sc->chan = channel;
	sc->rx_buf = malloc(PCIB_PACKET_SIZE, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->hbus = hbus;

	/*
	 * The taskq is used to handle PCI_BUS_RELATIONS and PCI_EJECT
	 * messages. NB: we can't handle the messages in the channel callback
	 * directly, because the message handlers need to send new messages
	 * to the host and waits for the host's completion messages, which
	 * must also be handled by the channel callback.
	 */
	sc->taskq = taskqueue_create("vmbus_pcib_tq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->taskq);
	taskqueue_start_threads(&sc->taskq, 1, PI_NET, "vmbus_pcib_tq");

	hbus->sc = sc;

	init_completion(&hbus->query_completion);
	hbus->query_comp = &hbus->query_completion;

	ret = vmbus_chan_open(sc->chan, pci_ring_size, pci_ring_size,
		NULL, 0, vmbus_pcib_on_channel_callback, sc);
	if (ret)
		goto free_res;

	ret = hv_pci_protocol_negotiation(hbus);
	if (ret)
		goto vmbus_close;

	ret = hv_pci_query_relations(hbus);
	if (ret)
		goto vmbus_close;
	wait_for_completion(hbus->query_comp);

	ret = hv_pci_enter_d0(hbus);
	if (ret)
		goto vmbus_close;

	ret = hv_send_resources_allocated(hbus);
	if (ret)
		goto vmbus_close;

	hbus->pci_bus = device_add_child(dev, "pci", -1);
	if (!hbus->pci_bus) {
		device_printf(dev, "failed to create pci bus\n");
		ret = ENXIO;
		goto vmbus_close;
	}

	bus_generic_attach(dev);

	hbus->state = hv_pcibus_installed;

	return (0);

vmbus_close:
	vmbus_pcib_pre_detach(hbus);
	vmbus_chan_close(sc->chan);
free_res:
	taskqueue_free(sc->taskq);
	free_completion(&hbus->query_completion);
	free(sc->rx_buf, M_DEVBUF);
	bus_release_resource(dev, SYS_RES_MEMORY, 0, hbus->cfg_res);
free_bus:
	mtx_destroy(&hbus->device_list_lock);
	mtx_destroy(&hbus->config_lock);
	free(hbus, M_DEVBUF);
	return (ret);
}

/*
 * Standard detach entry point
 */
static int
vmbus_pcib_detach(device_t dev)
{
	struct vmbus_pcib_softc *sc = device_get_softc(dev);
	struct hv_pcibus *hbus = sc->hbus;
	struct pci_message teardown_packet;
	struct pci_bus_relations relations;
	int ret;

	vmbus_pcib_pre_detach(hbus);

	if (hbus->state == hv_pcibus_installed)
		bus_generic_detach(dev);

	/* Delete any children which might still exist. */
	memset(&relations, 0, sizeof(relations));
	hv_pci_devices_present(hbus, &relations);

	ret = hv_send_resources_released(hbus);
	if (ret)
		device_printf(dev, "failed to send PCI_RESOURCES_RELEASED\n");

	teardown_packet.type = PCI_BUS_D0EXIT;
	ret = vmbus_chan_send(sc->chan, VMBUS_CHANPKT_TYPE_INBAND, 0,
	    &teardown_packet, sizeof(struct pci_message), 0);
	if (ret)
		device_printf(dev, "failed to send PCI_BUS_D0EXIT\n");

	taskqueue_drain_all(hbus->sc->taskq);
	vmbus_chan_close(sc->chan);
	taskqueue_free(sc->taskq);

	free_completion(&hbus->query_completion);
	free(sc->rx_buf, M_DEVBUF);
	bus_release_resource(dev, SYS_RES_MEMORY, 0, hbus->cfg_res);

	mtx_destroy(&hbus->device_list_lock);
	mtx_destroy(&hbus->config_lock);
	free(hbus, M_DEVBUF);

	return (0);
}

static int
vmbus_pcib_read_ivar(device_t dev, device_t child, int which, uintptr_t *val)
{
	struct vmbus_pcib_softc *sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*val = sc->hbus->pci_domain;
		return (0);

	case PCIB_IVAR_BUS:
		/* There is only bus 0. */
		*val = 0;
		return (0);
	}
	return (ENOENT);
}

static int
vmbus_pcib_write_ivar(device_t dev, device_t child, int which, uintptr_t val)
{
	return (ENOENT);
}

static struct resource *
vmbus_pcib_alloc_resource(device_t dev, device_t child, int type, int *rid,
	rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	unsigned int bar_no;
	struct hv_pci_dev *hpdev;
	struct vmbus_pcib_softc *sc = device_get_softc(dev);
	struct resource *res;
	unsigned int devfn;

	if (type == PCI_RES_BUS)
		return (pci_domain_alloc_bus(sc->hbus->pci_domain, child, rid,
		    start, end, count, flags));

	/* Devices with port I/O BAR are not supported. */
	if (type == SYS_RES_IOPORT)
		return (NULL);

	if (type == SYS_RES_MEMORY) {
		devfn = PCI_DEVFN(pci_get_slot(child),
		    pci_get_function(child));
		hpdev = get_pcichild_wslot(sc->hbus, devfn_to_wslot(devfn));
		if (!hpdev)
			return (NULL);

		bar_no = PCI_RID2BAR(*rid);
		if (bar_no >= MAX_NUM_BARS)
			return (NULL);

		/* Make sure a 32-bit BAR gets a 32-bit address */
		if (!(hpdev->probed_bar[bar_no] & PCIM_BAR_MEM_64))
			end = ulmin(end, 0xFFFFFFFF);
	}

	res = bus_generic_alloc_resource(dev, child, type, rid,
		start, end, count, flags);
	/*
	 * If this is a request for a specific range, assume it is
	 * correct and pass it up to the parent.
	 */
	if (res == NULL && start + count - 1 == end)
		res = bus_generic_alloc_resource(dev, child, type, rid,
		    start, end, count, flags);
	return (res);
}

static int
vmbus_pcib_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	struct vmbus_pcib_softc *sc = device_get_softc(dev);

	if (type == PCI_RES_BUS)
		return (pci_domain_release_bus(sc->hbus->pci_domain, child,
		    rid, r));

	if (type == SYS_RES_IOPORT)
		return (EINVAL);

	return (bus_generic_release_resource(dev, child, type, rid, r));
}

#if __FreeBSD_version >= 1100000
static int
vmbus_pcib_get_cpus(device_t pcib, device_t dev, enum cpu_sets op,
    size_t setsize, cpuset_t *cpuset)
{
	return (bus_get_cpus(pcib, op, setsize, cpuset));
}
#endif

static uint32_t
vmbus_pcib_read_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, int bytes)
{
	struct vmbus_pcib_softc *sc = device_get_softc(dev);
	struct hv_pci_dev *hpdev;
	unsigned int devfn = PCI_DEVFN(slot, func);
	uint32_t data = 0;

	KASSERT(bus == 0, ("bus should be 0, but is %u", bus));

	hpdev = get_pcichild_wslot(sc->hbus, devfn_to_wslot(devfn));
	if (!hpdev)
		return (~0);

	_hv_pcifront_read_config(hpdev, reg, bytes, &data);

	return (data);
}

static void
vmbus_pcib_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t data, int bytes)
{
	struct vmbus_pcib_softc *sc = device_get_softc(dev);
	struct hv_pci_dev *hpdev;
	unsigned int devfn = PCI_DEVFN(slot, func);

	KASSERT(bus == 0, ("bus should be 0, but is %u", bus));

	hpdev = get_pcichild_wslot(sc->hbus, devfn_to_wslot(devfn));
	if (!hpdev)
		return;

	_hv_pcifront_write_config(hpdev, reg, bytes, data);
}

static int
vmbus_pcib_route_intr(device_t pcib, device_t dev, int pin)
{
	/* We only support MSI/MSI-X and don't support INTx interrupt. */
	return (PCI_INVALID_IRQ);
}

static int
vmbus_pcib_alloc_msi(device_t pcib, device_t dev, int count,
    int maxcount, int *irqs)
{
	return (PCIB_ALLOC_MSI(device_get_parent(pcib), dev, count, maxcount,
	    irqs));
}

static int
vmbus_pcib_release_msi(device_t pcib, device_t dev, int count, int *irqs)
{
	return (PCIB_RELEASE_MSI(device_get_parent(pcib), dev, count, irqs));
}

static int
vmbus_pcib_alloc_msix(device_t pcib, device_t dev, int *irq)
{
	return (PCIB_ALLOC_MSIX(device_get_parent(pcib), dev, irq));
}

static int
vmbus_pcib_release_msix(device_t pcib, device_t dev, int irq)
{
	return (PCIB_RELEASE_MSIX(device_get_parent(pcib), dev, irq));
}

#define	MSI_INTEL_ADDR_DEST	0x000ff000
#define	MSI_INTEL_DATA_INTVEC	IOART_INTVEC	/* Interrupt vector. */
#define	MSI_INTEL_DATA_DELFIXED	IOART_DELFIXED

static int
vmbus_pcib_map_msi(device_t pcib, device_t child, int irq,
    uint64_t *addr, uint32_t *data)
{
	unsigned int devfn;
	struct hv_pci_dev *hpdev;

	uint64_t v_addr;
	uint32_t v_data;
	struct hv_irq_desc *hid, *tmp_hid;
	unsigned int cpu, vcpu_id;
	unsigned int vector;

	struct vmbus_pcib_softc *sc = device_get_softc(pcib);
	struct pci_create_interrupt *int_pkt;
	struct compose_comp_ctxt comp;
	struct {
		struct pci_packet pkt;
		uint8_t buffer[sizeof(struct pci_create_interrupt)];
	} ctxt;

	int ret;

	devfn = PCI_DEVFN(pci_get_slot(child), pci_get_function(child));
	hpdev = get_pcichild_wslot(sc->hbus, devfn_to_wslot(devfn));
	if (!hpdev)
		return (ENOENT);

	ret = PCIB_MAP_MSI(device_get_parent(pcib), child, irq,
	    &v_addr, &v_data);
	if (ret)
		return (ret);

	TAILQ_FOREACH_SAFE(hid, &hpdev->irq_desc_list, link, tmp_hid) {
		if (hid->irq == irq) {
			TAILQ_REMOVE(&hpdev->irq_desc_list, hid, link);
			hv_int_desc_free(hpdev, hid);
			break;
		}
	}

	cpu = (v_addr & MSI_INTEL_ADDR_DEST) >> 12;
	vcpu_id = VMBUS_GET_VCPU_ID(device_get_parent(pcib), pcib, cpu);
	vector = v_data & MSI_INTEL_DATA_INTVEC;

	init_completion(&comp.comp_pkt.host_event);

	memset(&ctxt, 0, sizeof(ctxt));
	ctxt.pkt.completion_func = hv_pci_compose_compl;
	ctxt.pkt.compl_ctxt = &comp;

	int_pkt = (struct pci_create_interrupt *)&ctxt.pkt.message;
	int_pkt->message_type.type = PCI_CREATE_INTERRUPT_MESSAGE;
	int_pkt->wslot.val = hpdev->desc.wslot.val;
	int_pkt->int_desc.vector = vector;
	int_pkt->int_desc.vector_count = 1;
	int_pkt->int_desc.delivery_mode = MSI_INTEL_DATA_DELFIXED;
	int_pkt->int_desc.cpu_mask = 1ULL << vcpu_id;

	ret = vmbus_chan_send(sc->chan,	VMBUS_CHANPKT_TYPE_INBAND,
	    VMBUS_CHANPKT_FLAG_RC, int_pkt, sizeof(*int_pkt),
	    (uint64_t)(uintptr_t)&ctxt.pkt);
	if (ret) {
		free_completion(&comp.comp_pkt.host_event);
		return (ret);
	}

	wait_for_completion(&comp.comp_pkt.host_event);
	free_completion(&comp.comp_pkt.host_event);

	if (comp.comp_pkt.completion_status < 0)
		return (EPROTO);

	*addr = comp.int_desc.address;
	*data = comp.int_desc.data;

	hid = malloc(sizeof(struct hv_irq_desc), M_DEVBUF, M_WAITOK | M_ZERO);
	hid->irq = irq;
	hid->desc = comp.int_desc;
	TAILQ_INSERT_TAIL(&hpdev->irq_desc_list, hid, link);

	return (0);
}

static device_method_t vmbus_pcib_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         vmbus_pcib_probe),
	DEVMETHOD(device_attach,        vmbus_pcib_attach),
	DEVMETHOD(device_detach,        vmbus_pcib_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,		vmbus_pcib_read_ivar),
	DEVMETHOD(bus_write_ivar,		vmbus_pcib_write_ivar),
	DEVMETHOD(bus_alloc_resource,		vmbus_pcib_alloc_resource),
	DEVMETHOD(bus_release_resource,		vmbus_pcib_release_resource),
	DEVMETHOD(bus_activate_resource,   bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	   bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	   bus_generic_teardown_intr),
#if __FreeBSD_version >= 1100000
	DEVMETHOD(bus_get_cpus,			vmbus_pcib_get_cpus),
#endif

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,		pcib_maxslots),
	DEVMETHOD(pcib_read_config,		vmbus_pcib_read_config),
	DEVMETHOD(pcib_write_config,		vmbus_pcib_write_config),
	DEVMETHOD(pcib_route_interrupt,		vmbus_pcib_route_intr),
	DEVMETHOD(pcib_alloc_msi,		vmbus_pcib_alloc_msi),
	DEVMETHOD(pcib_release_msi,		vmbus_pcib_release_msi),
	DEVMETHOD(pcib_alloc_msix,		vmbus_pcib_alloc_msix),
	DEVMETHOD(pcib_release_msix,		vmbus_pcib_release_msix),
	DEVMETHOD(pcib_map_msi,			vmbus_pcib_map_msi),
	DEVMETHOD(pcib_request_feature,		pcib_request_feature_allow),

	DEVMETHOD_END
};

static devclass_t pcib_devclass;

DEFINE_CLASS_0(pcib, vmbus_pcib_driver, vmbus_pcib_methods,
		sizeof(struct vmbus_pcib_softc));
DRIVER_MODULE(vmbus_pcib, vmbus, vmbus_pcib_driver, pcib_devclass, 0, 0);
MODULE_DEPEND(vmbus_pcib, vmbus, 1, 1, 1);
MODULE_DEPEND(vmbus_pcib, pci, 1, 1, 1);

#endif /* NEW_PCIB */
