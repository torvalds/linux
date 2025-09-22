/* $OpenBSD: vmm.h,v 1.11 2025/07/15 13:40:02 jsg Exp $ */
/*
 * Copyright (c) 2014-2023 Mike Larkin <mlarkin@openbsd.org>
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

#include <sys/rwlock.h>
#include <sys/refcnt.h>

#include <uvm/uvm_extern.h>

#include <machine/vmmvar.h>

#ifndef DEV_VMM_H
#define DEV_VMM_H

#define VMM_MAX_MEM_RANGES	16
#define VMM_MAX_DISKS_PER_VM	4
#define VMM_MAX_NAME_LEN	64
#define VMM_MAX_VCPUS		512
#define VMM_MAX_VCPUS_PER_VM	64
#define VMM_MAX_VM_MEM_SIZE	128L * 1024 * 1024 * 1024
#define VMM_MAX_NICS_PER_VM	4

struct vm_mem_range {
	paddr_t vmr_gpa;
	vaddr_t vmr_va;
	size_t  vmr_size;
	int     vmr_type;
#define VM_MEM_RAM		0	/* Presented as usable system memory. */
#define VM_MEM_RESERVED		1	/* Reserved for BIOS, etc. */
#define VM_MEM_MMIO		2	/* Special region for device mmio. */
};

struct vm_create_params {
/* Input parameters to VMM_IOC_CREATE */
	size_t			vcp_nmemranges;
	size_t			vcp_ncpus;
	struct vm_mem_range	vcp_memranges[VMM_MAX_MEM_RANGES];
	char			vcp_name[VMM_MAX_NAME_LEN];
	int			vcp_sev;
	int			vcp_seves;

        /* Output parameter from VMM_IOC_CREATE */
        uint32_t		vcp_id;
        uint32_t		vcp_poscbit;
        uint32_t		vcp_asid[VMM_MAX_VCPUS];
};

struct vm_info_result {
	/* Output parameters from VMM_IOC_INFO */
	size_t		vir_memory_size;
	size_t		vir_used_size;
	size_t		vir_ncpus;
	uint8_t		vir_vcpu_state[VMM_MAX_VCPUS_PER_VM];
	pid_t		vir_creator_pid;
	uint32_t	vir_id;
	char		vir_name[VMM_MAX_NAME_LEN];
};

struct vm_info_params {
	/* Input parameters to VMM_IOC_INFO */
	size_t			 vip_size;	/* Output buffer size */

	/* Output Parameters from VMM_IOC_INFO */
	size_t			 vip_info_ct;	/* # of entries returned */
	struct vm_info_result	*vip_info;	/* Output buffer */
};

struct vm_terminate_params {
	/* Input parameters to VMM_IOC_TERM */
	uint32_t		vtp_vm_id;
};

struct vm_resetcpu_params {
	/* Input parameters to VMM_IOC_RESETCPU */
	uint32_t		vrp_vm_id;
	uint32_t		vrp_vcpu_id;
	struct vcpu_reg_state	vrp_init_state;
};

struct vm_sharemem_params {
	/* Input parameters to VMM_IOC_SHAREMEM */
	uint32_t		vsp_vm_id;
	size_t			vsp_nmemranges;
	struct vm_mem_range	vsp_memranges[VMM_MAX_MEM_RANGES];

	/* Output parameters from VMM_IOC_SHAREMEM */
	vaddr_t			vsp_va[VMM_MAX_MEM_RANGES];
};

struct vm_run_params {
	/* Input parameters to VMM_IOC_RUN */
	uint32_t	vrp_vm_id;
	uint32_t	vrp_vcpu_id;
	struct vcpu_inject_event	vrp_inject;
	uint8_t		vrp_intr_pending;	/* Additional intrs pending? */

	/* Input/output parameter to VMM_IOC_RUN */
	struct vm_exit	*vrp_exit;		/* updated exit data */

	/* Output parameter from VMM_IOC_RUN */
	uint16_t	vrp_exit_reason;	/* exit reason */
	uint8_t		vrp_irqready;		/* ready for IRQ on entry */
};

#define VM_RWVMPARAMS_PVCLOCK_SYSTEM_GPA 0x1	/* read/write pvclock gpa */
#define VM_RWVMPARAMS_PVCLOCK_VERSION	 0x2	/* read/write pvclock version */
#define VM_RWVMPARAMS_ALL	(VM_RWVMPARAMS_PVCLOCK_SYSTEM_GPA | \
    VM_RWVMPARAMS_PVCLOCK_VERSION)

struct vm_rwvmparams_params {
	/* Input parameters to VMM_IOC_READVMPARAMS/VMM_IOC_WRITEVMPARAMS */
	uint32_t		vpp_vm_id;
	uint32_t		vpp_vcpu_id;
	uint32_t		vpp_mask;
	paddr_t			vpp_pvclock_system_gpa;
	uint32_t		vpp_pvclock_version;
};

/* IOCTL definitions */
#define VMM_IOC_CREATE _IOWR('V', 1, struct vm_create_params) /* Create VM */
#define VMM_IOC_RUN _IOWR('V', 2, struct vm_run_params) /* Run VCPU */
#define VMM_IOC_INFO _IOWR('V', 3, struct vm_info_params) /* Get VM Info */
#define VMM_IOC_TERM _IOW('V', 4, struct vm_terminate_params) /* Terminate VM */
#define VMM_IOC_RESETCPU _IOW('V', 5, struct vm_resetcpu_params) /* Reset */
#define VMM_IOC_READREGS _IOWR('V', 7, struct vm_rwregs_params) /* Get regs */
#define VMM_IOC_WRITEREGS _IOW('V', 8, struct vm_rwregs_params) /* Set regs */
/* Get VM params */
#define VMM_IOC_READVMPARAMS _IOWR('V', 9, struct vm_rwvmparams_params)
/* Set VM params */
#define VMM_IOC_WRITEVMPARAMS _IOW('V', 10, struct vm_rwvmparams_params)
#define VMM_IOC_SHAREMEM _IOWR('V', 11, struct vm_sharemem_params)

#ifdef _KERNEL

/* #define VMM_DEBUG */

#ifdef VMM_DEBUG
#define DPRINTF(x...)   do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif /* VMM_DEBUG */
enum {
	VCPU_STATE_STOPPED,
	VCPU_STATE_RUNNING,
	VCPU_STATE_REQTERM,
	VCPU_STATE_TERMINATED,
	VCPU_STATE_UNKNOWN,
};

/*
 * Virtual Machine
 *
 * Methods used to protect vm struct members:
 *	a	atomic operations
 *	I	immutable after create
 *	K	kernel lock
 *	r	reference count
 *	v	vcpu list rwlock (vm_vcpu_list)
 *	V	vmm_softc's vm_lock
 */
struct vm {
	pmap_t			 vm_pmap;		/* [r] */

	uint32_t		 vm_id;			/* [I] */
	pid_t			 vm_creator_pid;	/* [I] */

	size_t			 vm_nmemranges;		/* [I] */
	size_t			 vm_memory_size;	/* [I] */
	struct vm_mem_range	 vm_memranges[VMM_MAX_MEM_RANGES];
	struct uvm_object	*vm_memory_slot[VMM_MAX_MEM_RANGES]; /* [I] */

	char			 vm_name[VMM_MAX_NAME_LEN];
	struct refcnt		 vm_refcnt;		/* [a] */

	struct vcpu_head	 vm_vcpu_list;		/* [v] */
	uint32_t		 vm_vcpu_ct;		/* [v] */
	struct rwlock		 vm_vcpu_lock;

	SLIST_ENTRY(vm)		 vm_link;		/* [V] */
};

SLIST_HEAD(vmlist_head, vm);

/*
 * Virtual Machine Monitor
 *
 * Methods used to protect struct members in the global vmm device:
 *	a	atomic operations
 *	I	immutable operations
 *	K	kernel lock
 *	p	virtual process id (vpid/asid) rwlock
 *	r	reference count
 *	v	vm list rwlock (vm_lock)
 */
struct vmm_softc {
	struct device		sc_dev;		/* [r] */

	/* Suspend/Resume Synchronization */
	struct rwlock		sc_slock;
	struct refcnt		sc_refcnt;
	volatile unsigned int	sc_status;	/* [a] */
#define VMM_SUSPENDED		(unsigned int) 0
#define VMM_ACTIVE		(unsigned int) 1

	struct vmm_softc_md	sc_md;

	/* Managed VMs */
	struct vmlist_head	vm_list;	/* [v] */

	int			mode;		/* [I] */

	size_t			vcpu_ct;	/* [v] */
	size_t			vcpu_max;	/* [I] */

	struct rwlock		vm_lock;
	size_t			vm_ct;		/* [v] no. of in-memory VMs */
	size_t			vm_idx;		/* [a] next unique VM index */

	struct rwlock		vpid_lock;
	uint16_t		max_vpid;	/* [I] */
	uint8_t			vpids[512];	/* [p] bitmap of VPID/ASIDs */
};

extern struct vmm_softc *vmm_softc;
extern struct pool vm_pool;
extern struct pool vcpu_pool;
extern struct cfdriver vmm_cd;
extern const struct cfattach vmm_ca;

int vmm_probe(struct device *, void *, void *);
int vmm_activate(struct device *, int);
void vmm_attach(struct device *, struct device *,  void *);
int vmmopen(dev_t, int, int, struct proc *);
int vmmclose(dev_t, int, int, struct proc *);
int vm_find(uint32_t, struct vm **);
int vmmioctl_machdep(dev_t, u_long, caddr_t, int, struct proc *);
int pledge_ioctl_vmm(struct proc *, long);
struct vcpu *vm_find_vcpu(struct vm *, uint32_t);
int vm_create(struct vm_create_params *, struct proc *);
size_t vm_create_check_mem_ranges(struct vm_create_params *);
void vm_teardown(struct vm **);
int vm_get_info(struct vm_info_params *);
int vm_terminate(struct vm_terminate_params *);
int vm_resetcpu(struct vm_resetcpu_params *);
int vm_rwvmparams(struct vm_rwvmparams_params *, int);
int vcpu_must_stop(struct vcpu *);
int vm_share_mem(struct vm_sharemem_params *, struct proc *);
int vm_run(struct vm_run_params *);

#ifdef VMM_DEBUG
void dump_vcpu(struct vcpu *);
#endif

#endif /* _KERNEL */
#endif /* DEV_VMM_H */
