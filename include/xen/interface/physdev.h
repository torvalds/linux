/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __XEN_PUBLIC_PHYSDEV_H__
#define __XEN_PUBLIC_PHYSDEV_H__

/*
 * Prototype for this hypercall is:
 *  int physdev_op(int cmd, void *args)
 * @cmd	 == PHYSDEVOP_??? (physdev operation).
 * @args == Operation-specific extra arguments (NULL if none).
 */

/*
 * Notify end-of-interrupt (EOI) for the specified IRQ.
 * @arg == pointer to physdev_eoi structure.
 */
#define PHYSDEVOP_eoi			12
struct physdev_eoi {
	/* IN */
	uint32_t irq;
};

/*
 * Register a shared page for the hypervisor to indicate whether the guest
 * must issue PHYSDEVOP_eoi. The semantics of PHYSDEVOP_eoi change slightly
 * once the guest used this function in that the associated event channel
 * will automatically get unmasked. The page registered is used as a bit
 * array indexed by Xen's PIRQ value.
 */
#define PHYSDEVOP_pirq_eoi_gmfn_v1       17
/*
 * Register a shared page for the hypervisor to indicate whether the
 * guest must issue PHYSDEVOP_eoi. This hypercall is very similar to
 * PHYSDEVOP_pirq_eoi_gmfn_v1 but it doesn't change the semantics of
 * PHYSDEVOP_eoi. The page registered is used as a bit array indexed by
 * Xen's PIRQ value.
 */
#define PHYSDEVOP_pirq_eoi_gmfn_v2       28
struct physdev_pirq_eoi_gmfn {
    /* IN */
    xen_ulong_t gmfn;
};

/*
 * Query the status of an IRQ line.
 * @arg == pointer to physdev_irq_status_query structure.
 */
#define PHYSDEVOP_irq_status_query	 5
struct physdev_irq_status_query {
	/* IN */
	uint32_t irq;
	/* OUT */
	uint32_t flags; /* XENIRQSTAT_* */
};

/* Need to call PHYSDEVOP_eoi when the IRQ has been serviced? */
#define _XENIRQSTAT_needs_eoi	(0)
#define	 XENIRQSTAT_needs_eoi	(1U<<_XENIRQSTAT_needs_eoi)

/* IRQ shared by multiple guests? */
#define _XENIRQSTAT_shared	(1)
#define	 XENIRQSTAT_shared	(1U<<_XENIRQSTAT_shared)

/*
 * Set the current VCPU's I/O privilege level.
 * @arg == pointer to physdev_set_iopl structure.
 */
#define PHYSDEVOP_set_iopl		 6
struct physdev_set_iopl {
	/* IN */
	uint32_t iopl;
};

/*
 * Set the current VCPU's I/O-port permissions bitmap.
 * @arg == pointer to physdev_set_iobitmap structure.
 */
#define PHYSDEVOP_set_iobitmap		 7
struct physdev_set_iobitmap {
	/* IN */
	uint8_t * bitmap;
	uint32_t nr_ports;
};

/*
 * Read or write an IO-APIC register.
 * @arg == pointer to physdev_apic structure.
 */
#define PHYSDEVOP_apic_read		 8
#define PHYSDEVOP_apic_write		 9
struct physdev_apic {
	/* IN */
	unsigned long apic_physbase;
	uint32_t reg;
	/* IN or OUT */
	uint32_t value;
};

/*
 * Allocate or free a physical upcall vector for the specified IRQ line.
 * @arg == pointer to physdev_irq structure.
 */
#define PHYSDEVOP_alloc_irq_vector	10
#define PHYSDEVOP_free_irq_vector	11
struct physdev_irq {
	/* IN */
	uint32_t irq;
	/* IN or OUT */
	uint32_t vector;
};

#define MAP_PIRQ_TYPE_MSI		0x0
#define MAP_PIRQ_TYPE_GSI		0x1
#define MAP_PIRQ_TYPE_UNKNOWN		0x2
#define MAP_PIRQ_TYPE_MSI_SEG		0x3

#define PHYSDEVOP_map_pirq		13
struct physdev_map_pirq {
    domid_t domid;
    /* IN */
    int type;
    /* IN */
    int index;
    /* IN or OUT */
    int pirq;
    /* IN - high 16 bits hold segment for MAP_PIRQ_TYPE_MSI_SEG */
    int bus;
    /* IN */
    int devfn;
    /* IN */
    int entry_nr;
    /* IN */
    uint64_t table_base;
};

#define PHYSDEVOP_unmap_pirq		14
struct physdev_unmap_pirq {
    domid_t domid;
    /* IN */
    int pirq;
};

#define PHYSDEVOP_manage_pci_add	15
#define PHYSDEVOP_manage_pci_remove	16
struct physdev_manage_pci {
	/* IN */
	uint8_t bus;
	uint8_t devfn;
};

#define PHYSDEVOP_restore_msi            19
struct physdev_restore_msi {
	/* IN */
	uint8_t bus;
	uint8_t devfn;
};

#define PHYSDEVOP_manage_pci_add_ext	20
struct physdev_manage_pci_ext {
	/* IN */
	uint8_t bus;
	uint8_t devfn;
	unsigned is_extfn;
	unsigned is_virtfn;
	struct {
		uint8_t bus;
		uint8_t devfn;
	} physfn;
};

/*
 * Argument to physdev_op_compat() hypercall. Superceded by new physdev_op()
 * hypercall since 0x00030202.
 */
struct physdev_op {
	uint32_t cmd;
	union {
		struct physdev_irq_status_query	     irq_status_query;
		struct physdev_set_iopl		     set_iopl;
		struct physdev_set_iobitmap	     set_iobitmap;
		struct physdev_apic		     apic_op;
		struct physdev_irq		     irq_op;
	} u;
};

#define PHYSDEVOP_setup_gsi    21
struct physdev_setup_gsi {
    int gsi;
    /* IN */
    uint8_t triggering;
    /* IN */
    uint8_t polarity;
    /* IN */
};

#define PHYSDEVOP_get_nr_pirqs    22
struct physdev_nr_pirqs {
    /* OUT */
    uint32_t nr_pirqs;
};

/* type is MAP_PIRQ_TYPE_GSI or MAP_PIRQ_TYPE_MSI
 * the hypercall returns a free pirq */
#define PHYSDEVOP_get_free_pirq    23
struct physdev_get_free_pirq {
    /* IN */ 
    int type;
    /* OUT */
    uint32_t pirq;
};

#define XEN_PCI_DEV_EXTFN              0x1
#define XEN_PCI_DEV_VIRTFN             0x2
#define XEN_PCI_DEV_PXM                0x4

#define PHYSDEVOP_pci_device_add        25
struct physdev_pci_device_add {
    /* IN */
    uint16_t seg;
    uint8_t bus;
    uint8_t devfn;
    uint32_t flags;
    struct {
        uint8_t bus;
        uint8_t devfn;
    } physfn;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    uint32_t optarr[];
#elif defined(__GNUC__)
    uint32_t optarr[0];
#endif
};

#define PHYSDEVOP_pci_device_remove     26
#define PHYSDEVOP_restore_msi_ext       27
struct physdev_pci_device {
    /* IN */
    uint16_t seg;
    uint8_t bus;
    uint8_t devfn;
};

#define PHYSDEVOP_DBGP_RESET_PREPARE    1
#define PHYSDEVOP_DBGP_RESET_DONE       2

#define PHYSDEVOP_DBGP_BUS_UNKNOWN      0
#define PHYSDEVOP_DBGP_BUS_PCI          1

#define PHYSDEVOP_dbgp_op               29
struct physdev_dbgp_op {
    /* IN */
    uint8_t op;
    uint8_t bus;
    union {
        struct physdev_pci_device pci;
    } u;
};

/*
 * Notify that some PIRQ-bound event channels have been unmasked.
 * ** This command is obsolete since interface version 0x00030202 and is **
 * ** unsupported by newer versions of Xen.				 **
 */
#define PHYSDEVOP_IRQ_UNMASK_NOTIFY	 4

/*
 * These all-capitals physdev operation names are superceded by the new names
 * (defined above) since interface version 0x00030202.
 */
#define PHYSDEVOP_IRQ_STATUS_QUERY	 PHYSDEVOP_irq_status_query
#define PHYSDEVOP_SET_IOPL		 PHYSDEVOP_set_iopl
#define PHYSDEVOP_SET_IOBITMAP		 PHYSDEVOP_set_iobitmap
#define PHYSDEVOP_APIC_READ		 PHYSDEVOP_apic_read
#define PHYSDEVOP_APIC_WRITE		 PHYSDEVOP_apic_write
#define PHYSDEVOP_ASSIGN_VECTOR		 PHYSDEVOP_alloc_irq_vector
#define PHYSDEVOP_FREE_VECTOR		 PHYSDEVOP_free_irq_vector
#define PHYSDEVOP_IRQ_NEEDS_UNMASK_NOTIFY XENIRQSTAT_needs_eoi
#define PHYSDEVOP_IRQ_SHARED		 XENIRQSTAT_shared

#endif /* __XEN_PUBLIC_PHYSDEV_H__ */
