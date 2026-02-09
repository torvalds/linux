/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PCI_TSM_H
#define __PCI_TSM_H
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/sockptr.h>

struct pci_tsm;
struct tsm_dev;
struct kvm;
enum pci_tsm_req_scope;

/*
 * struct pci_tsm_ops - manage confidential links and security state
 * @link_ops: Coordinate PCIe SPDM and IDE establishment via a platform TSM.
 *	      Provide a secure session transport for TDISP state management
 *	      (typically bare metal physical function operations).
 * @devsec_ops: Lock, unlock, and interrogate the security state of the
 *		function via the platform TSM (typically virtual function
 *		operations).
 *
 * This operations are mutually exclusive either a tsm_dev instance
 * manages physical link properties or it manages function security
 * states like TDISP lock/unlock.
 */
struct pci_tsm_ops {
	/*
	 * struct pci_tsm_link_ops - Manage physical link and the TSM/DSM session
	 * @probe: establish context with the TSM (allocate / wrap 'struct
	 *	   pci_tsm') for follow-on link operations
	 * @remove: destroy link operations context
	 * @connect: establish / validate a secure connection (e.g. IDE)
	 *	     with the device
	 * @disconnect: teardown the secure link
	 * @bind: bind a TDI in preparation for it to be accepted by a TVM
	 * @unbind: remove a TDI from secure operation with a TVM
	 * @guest_req: marshal TVM information and state change requests
	 *
	 * Context: @probe, @remove, @connect, and @disconnect run under
	 * pci_tsm_rwsem held for write to sync with TSM unregistration and
	 * mutual exclusion of @connect and @disconnect. @connect and
	 * @disconnect additionally run under the DSM lock (struct
	 * pci_tsm_pf0::lock) as well as @probe and @remove of the subfunctions.
	 * @bind, @unbind, and @guest_req run under pci_tsm_rwsem held for read
	 * and the DSM lock.
	 */
	struct_group_tagged(pci_tsm_link_ops, link_ops,
		struct pci_tsm *(*probe)(struct tsm_dev *tsm_dev,
					 struct pci_dev *pdev);
		void (*remove)(struct pci_tsm *tsm);
		int (*connect)(struct pci_dev *pdev);
		void (*disconnect)(struct pci_dev *pdev);
		struct pci_tdi *(*bind)(struct pci_dev *pdev,
					struct kvm *kvm, u32 tdi_id);
		void (*unbind)(struct pci_tdi *tdi);
		ssize_t (*guest_req)(struct pci_tdi *tdi,
				     enum pci_tsm_req_scope scope,
				     sockptr_t req_in, size_t in_len,
				     sockptr_t req_out, size_t out_len,
				     u64 *tsm_code);
	);

	/*
	 * struct pci_tsm_devsec_ops - Manage the security state of the function
	 * @lock: establish context with the TSM (allocate / wrap 'struct
	 *	  pci_tsm') for follow-on security state transitions from the
	 *	  LOCKED state
	 * @unlock: destroy TSM context and return device to UNLOCKED state
	 *
	 * Context: @lock and @unlock run under pci_tsm_rwsem held for write to
	 * sync with TSM unregistration and each other
	 */
	struct_group_tagged(pci_tsm_devsec_ops, devsec_ops,
		struct pci_tsm *(*lock)(struct tsm_dev *tsm_dev,
					struct pci_dev *pdev);
		void (*unlock)(struct pci_tsm *tsm);
	);
};

/**
 * struct pci_tdi - Core TEE I/O Device Interface (TDI) context
 * @pdev: host side representation of guest-side TDI
 * @kvm: TEE VM context of bound TDI
 * @tdi_id: Identifier (virtual BDF) for the TDI as referenced by the TSM and DSM
 */
struct pci_tdi {
	struct pci_dev *pdev;
	struct kvm *kvm;
	u32 tdi_id;
};

/**
 * struct pci_tsm - Core TSM context for a given PCIe endpoint
 * @pdev: Back ref to device function, distinguishes type of pci_tsm context
 * @dsm_dev: PCI Device Security Manager for link operations on @pdev
 * @tsm_dev: PCI TEE Security Manager device for Link Confidentiality or Device
 *	     Function Security operations
 * @tdi: TDI context established by the @bind link operation
 *
 * This structure is wrapped by low level TSM driver data and returned by
 * probe()/lock(), it is freed by the corresponding remove()/unlock().
 *
 * For link operations it serves to cache the association between a Device
 * Security Manager (DSM) and the functions that manager can assign to a TVM.
 * That can be "self", for assigning function0 of a TEE I/O device, a
 * sub-function (SR-IOV virtual function, or non-function0
 * multifunction-device), or a downstream endpoint (PCIe upstream switch-port as
 * DSM).
 */
struct pci_tsm {
	struct pci_dev *pdev;
	struct pci_dev *dsm_dev;
	struct tsm_dev *tsm_dev;
	struct pci_tdi *tdi;
};

/**
 * struct pci_tsm_pf0 - Physical Function 0 TDISP link context
 * @base_tsm: generic core "tsm" context
 * @lock: mutual exclustion for pci_tsm_ops invocation
 * @doe_mb: PCIe Data Object Exchange mailbox
 */
struct pci_tsm_pf0 {
	struct pci_tsm base_tsm;
	struct mutex lock;
	struct pci_doe_mb *doe_mb;
};

/* physical function0 and capable of 'connect' */
static inline bool is_pci_tsm_pf0(struct pci_dev *pdev)
{
	if (!pdev)
		return false;

	if (!pci_is_pcie(pdev))
		return false;

	if (pdev->is_virtfn)
		return false;

	/*
	 * Allow for a Device Security Manager (DSM) associated with function0
	 * of an Endpoint to coordinate TDISP requests for other functions
	 * (physical or virtual) of the device, or allow for an Upstream Port
	 * DSM to accept TDISP requests for the Endpoints downstream of the
	 * switch.
	 */
	switch (pci_pcie_type(pdev)) {
	case PCI_EXP_TYPE_ENDPOINT:
	case PCI_EXP_TYPE_UPSTREAM:
	case PCI_EXP_TYPE_RC_END:
		if (pdev->ide_cap || (pdev->devcap & PCI_EXP_DEVCAP_TEE))
			break;
		fallthrough;
	default:
		return false;
	}

	return PCI_FUNC(pdev->devfn) == 0;
}

/**
 * enum pci_tsm_req_scope - Scope of guest requests to be validated by TSM
 *
 * Guest requests are a transport for a TVM to communicate with a TSM + DSM for
 * a given TDI. A TSM driver is responsible for maintaining the kernel security
 * model and limit commands that may affect the host, or are otherwise outside
 * the typical TDISP operational model.
 */
enum pci_tsm_req_scope {
	/**
	 * @PCI_TSM_REQ_INFO: Read-only, without side effects, request for
	 * typical TDISP collateral information like Device Interface Reports.
	 * No device secrets are permitted, and no device state is changed.
	 */
	PCI_TSM_REQ_INFO = 0,
	/**
	 * @PCI_TSM_REQ_STATE_CHANGE: Request to change the TDISP state from
	 * UNLOCKED->LOCKED, LOCKED->RUN, or other architecture specific state
	 * changes to support those transitions for a TDI. No other (unrelated
	 * to TDISP) device / host state, configuration, or data change is
	 * permitted.
	 */
	PCI_TSM_REQ_STATE_CHANGE = 1,
	/**
	 * @PCI_TSM_REQ_DEBUG_READ: Read-only request for debug information
	 *
	 * A method to facilitate TVM information retrieval outside of typical
	 * TDISP operational requirements. No device secrets are permitted.
	 */
	PCI_TSM_REQ_DEBUG_READ = 2,
	/**
	 * @PCI_TSM_REQ_DEBUG_WRITE: Device state changes for debug purposes
	 *
	 * The request may affect the operational state of the device outside of
	 * the TDISP operational model. If allowed, requires CAP_SYS_RAW_IO, and
	 * will taint the kernel.
	 */
	PCI_TSM_REQ_DEBUG_WRITE = 3,
};

#ifdef CONFIG_PCI_TSM
int pci_tsm_register(struct tsm_dev *tsm_dev);
void pci_tsm_unregister(struct tsm_dev *tsm_dev);
int pci_tsm_link_constructor(struct pci_dev *pdev, struct pci_tsm *tsm,
			     struct tsm_dev *tsm_dev);
int pci_tsm_pf0_constructor(struct pci_dev *pdev, struct pci_tsm_pf0 *tsm,
			    struct tsm_dev *tsm_dev);
void pci_tsm_pf0_destructor(struct pci_tsm_pf0 *tsm);
int pci_tsm_doe_transfer(struct pci_dev *pdev, u8 type, const void *req,
			 size_t req_sz, void *resp, size_t resp_sz);
int pci_tsm_bind(struct pci_dev *pdev, struct kvm *kvm, u32 tdi_id);
void pci_tsm_unbind(struct pci_dev *pdev);
void pci_tsm_tdi_constructor(struct pci_dev *pdev, struct pci_tdi *tdi,
			     struct kvm *kvm, u32 tdi_id);
ssize_t pci_tsm_guest_req(struct pci_dev *pdev, enum pci_tsm_req_scope scope,
			  sockptr_t req_in, size_t in_len, sockptr_t req_out,
			  size_t out_len, u64 *tsm_code);
#else
static inline int pci_tsm_register(struct tsm_dev *tsm_dev)
{
	return 0;
}
static inline void pci_tsm_unregister(struct tsm_dev *tsm_dev)
{
}
static inline int pci_tsm_bind(struct pci_dev *pdev, struct kvm *kvm, u64 tdi_id)
{
	return -ENXIO;
}
static inline void pci_tsm_unbind(struct pci_dev *pdev)
{
}
static inline ssize_t pci_tsm_guest_req(struct pci_dev *pdev,
					enum pci_tsm_req_scope scope,
					sockptr_t req_in, size_t in_len,
					sockptr_t req_out, size_t out_len,
					u64 *tsm_code)
{
	return -ENXIO;
}
#endif
#endif /*__PCI_TSM_H */
