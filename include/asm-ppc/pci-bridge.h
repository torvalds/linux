#ifdef __KERNEL__
#ifndef _ASM_PCI_BRIDGE_H
#define _ASM_PCI_BRIDGE_H

#include <linux/ioport.h>
#include <linux/pci.h>

struct device_node;
struct pci_controller;

/*
 * pci_io_base returns the memory address at which you can access
 * the I/O space for PCI bus number `bus' (or NULL on error).
 */
extern void __iomem *pci_bus_io_base(unsigned int bus);
extern unsigned long pci_bus_io_base_phys(unsigned int bus);
extern unsigned long pci_bus_mem_base_phys(unsigned int bus);

/* Allocate a new PCI host bridge structure */
extern struct pci_controller* pcibios_alloc_controller(void);

/* Helper function for setting up resources */
extern void pci_init_resource(struct resource *res, unsigned long start,
			      unsigned long end, int flags, char *name);

/* Get the PCI host controller for a bus */
extern struct pci_controller* pci_bus_to_hose(int bus);

/* Get the PCI host controller for an OF device */
extern struct pci_controller*
pci_find_hose_for_OF_device(struct device_node* node);

/* Fill up host controller resources from the OF node */
extern void
pci_process_bridge_OF_ranges(struct pci_controller *hose,
			   struct device_node *dev, int primary);

/*
 * Structure of a PCI controller (host bridge)
 */
struct pci_controller {
	int index;			/* PCI domain number */
	struct pci_controller *next;
        struct pci_bus *bus;
	void *arch_data;
	struct device *parent;

	int first_busno;
	int last_busno;
	int bus_offset;

	void __iomem *io_base_virt;
	unsigned long io_base_phys;

	/* Some machines (PReP) have a non 1:1 mapping of
	 * the PCI memory space in the CPU bus space
	 */
	unsigned long pci_mem_offset;

	struct pci_ops *ops;
	volatile unsigned int __iomem *cfg_addr;
	volatile void __iomem *cfg_data;
	/*
	 * If set, indirect method will set the cfg_type bit as
	 * needed to generate type 1 configuration transactions.
	 */
	int set_cfg_type;

	/* Currently, we limit ourselves to 1 IO range and 3 mem
	 * ranges since the common pci_bus structure can't handle more
	 */
	struct resource	io_resource;
	struct resource mem_resources[3];
	int mem_resource_count;

	/* Host bridge I/O and Memory space
	 * Used for BAR placement algorithms
	 */
	struct resource io_space;
	struct resource mem_space;
};

static inline struct pci_controller *pci_bus_to_host(struct pci_bus *bus)
{
	return bus->sysdata;
}

/* These are used for config access before all the PCI probing
   has been done. */
int early_read_config_byte(struct pci_controller *hose, int bus, int dev_fn,
			   int where, u8 *val);
int early_read_config_word(struct pci_controller *hose, int bus, int dev_fn,
			   int where, u16 *val);
int early_read_config_dword(struct pci_controller *hose, int bus, int dev_fn,
			    int where, u32 *val);
int early_write_config_byte(struct pci_controller *hose, int bus, int dev_fn,
			    int where, u8 val);
int early_write_config_word(struct pci_controller *hose, int bus, int dev_fn,
			    int where, u16 val);
int early_write_config_dword(struct pci_controller *hose, int bus, int dev_fn,
			     int where, u32 val);

extern void setup_indirect_pci_nomap(struct pci_controller* hose,
			       void __iomem *cfg_addr, void __iomem *cfg_data);
extern void setup_indirect_pci(struct pci_controller* hose,
			       u32 cfg_addr, u32 cfg_data);
extern void setup_grackle(struct pci_controller *hose);

extern unsigned char common_swizzle(struct pci_dev *, unsigned char *);

/*
 *   The following code swizzles for exactly one bridge.  The routine
 *   common_swizzle below handles multiple bridges.  But there are a
 *   some boards that don't follow the PCI spec's suggestion so we
 *   break this piece out separately.
 */
static inline unsigned char bridge_swizzle(unsigned char pin,
		unsigned char idsel)
{
	return (((pin-1) + idsel) % 4) + 1;
}

/*
 * The following macro is used to lookup irqs in a standard table
 * format for those PPC systems that do not already have PCI
 * interrupts properly routed.
 */
/* FIXME - double check this */
#define PCI_IRQ_TABLE_LOOKUP						    \
({ long _ctl_ = -1; 							    \
   if (idsel >= min_idsel && idsel <= max_idsel && pin <= irqs_per_slot)    \
     _ctl_ = pci_irq_table[idsel - min_idsel][pin-1];			    \
   _ctl_; })

/*
 * Scan the buses below a given PCI host bridge and assign suitable
 * resources to all devices found.
 */
extern int pciauto_bus_scan(struct pci_controller *, int);

#ifdef CONFIG_PCI
extern unsigned long pci_address_to_pio(phys_addr_t address);
#else
static inline unsigned long pci_address_to_pio(phys_addr_t address)
{
	return (unsigned long)-1;
}
#endif

#endif
#endif /* __KERNEL__ */
