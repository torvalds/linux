#ifndef ASM_X86__DEVICE_H
#define ASM_X86__DEVICE_H

struct dev_archdata {
#ifdef CONFIG_ACPI
	void	*acpi_handle;
#endif
#ifdef CONFIG_X86_64
struct dma_mapping_ops *dma_ops;
#endif
#ifdef CONFIG_DMAR
	void *iommu; /* hook for IOMMU specific extension */
#endif
};

#endif /* ASM_X86__DEVICE_H */
