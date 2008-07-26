#ifndef ASM_X86__DEVICE_H
#define ASM_X86__DEVICE_H

struct dev_archdata {
#ifdef CONFIG_ACPI
	void	*acpi_handle;
#endif
#ifdef CONFIG_DMAR
	void *iommu; /* hook for IOMMU specific extension */
#endif
};

#endif /* ASM_X86__DEVICE_H */
