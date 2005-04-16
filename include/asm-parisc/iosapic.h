/*
** This file is private to iosapic driver.
** If stuff needs to be used by another driver, move it to a common file.
**
** WARNING: fields most data structures here are ordered to make sure
**          they pack nicely for 64-bit compilation. (ie sizeof(long) == 8)
*/


/*
** I/O SAPIC init function
** Caller knows where an I/O SAPIC is. LBA has an integrated I/O SAPIC.
** Call setup as part of per instance initialization.
** (ie *not* init_module() function unless only one is present.)
** fixup_irq is to initialize PCI IRQ line support and
** virtualize pcidev->irq value. To be called by pci_fixup_bus().
*/
extern void *iosapic_register(unsigned long hpa);
extern int iosapic_fixup_irq(void *obj, struct pci_dev *pcidev);


#ifdef __IA64__
/*
** PA: PIB (Processor Interrupt Block) is handled by Runway bus adapter.
**     and is hardcoded to 0xfeeNNNN0 where NNNN is id_eid field.
**
** IA64: PIB is handled by "Local SAPIC" (integrated in the processor).
*/
struct local_sapic_info {
	struct local_sapic_info *lsi_next;      /* point to next CPU info */
	int                     *lsi_cpu_id;    /* point to logical CPU id */
	unsigned long           *lsi_id_eid;    /* point to IA-64 CPU id */
	int                     *lsi_status;    /* point to CPU status   */
	void                    *lsi_private;   /* point to special info */
};

/*
** "root" data structure which ties everything together.
** Should always be able to start with sapic_root and locate
** the desired information.
*/
struct sapic_info {
	struct sapic_info	*si_next;	/* info is per cell */
	int                     si_cellid;      /* cell id */
	unsigned int            si_status;       /* status  */
	char                    *si_pib_base;   /* intr blk base address */
	local_sapic_info_t      *si_local_info;
	io_sapic_info_t         *si_io_info;
	extint_info_t           *si_extint_info;/* External Intr info      */
};

#endif /* IA64 */

