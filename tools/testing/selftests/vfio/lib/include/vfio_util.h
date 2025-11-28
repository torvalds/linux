/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_VFIO_LIB_INCLUDE_VFIO_UTIL_H
#define SELFTESTS_VFIO_LIB_INCLUDE_VFIO_UTIL_H

#include <fcntl.h>
#include <string.h>

#include <uapi/linux/types.h>
#include <linux/iommufd.h>
#include <linux/list.h>
#include <linux/pci_regs.h>
#include <linux/vfio.h>

#include "../../../kselftest.h"

#define VFIO_LOG_AND_EXIT(...) do {		\
	fprintf(stderr, "  " __VA_ARGS__);	\
	fprintf(stderr, "\n");			\
	exit(KSFT_FAIL);			\
} while (0)

#define VFIO_ASSERT_OP(_lhs, _rhs, _op, ...) do {				\
	typeof(_lhs) __lhs = (_lhs);						\
	typeof(_rhs) __rhs = (_rhs);						\
										\
	if (__lhs _op __rhs)							\
		break;								\
										\
	fprintf(stderr, "%s:%u: Assertion Failure\n\n", __FILE__, __LINE__);	\
	fprintf(stderr, "  Expression: " #_lhs " " #_op " " #_rhs "\n");	\
	fprintf(stderr, "  Observed: %#lx %s %#lx\n",				\
			(u64)__lhs, #_op, (u64)__rhs);				\
	fprintf(stderr, "  [errno: %d - %s]\n", errno, strerror(errno));	\
	VFIO_LOG_AND_EXIT(__VA_ARGS__);						\
} while (0)

#define VFIO_ASSERT_EQ(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, ==, ##__VA_ARGS__)
#define VFIO_ASSERT_NE(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, !=, ##__VA_ARGS__)
#define VFIO_ASSERT_LT(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, <, ##__VA_ARGS__)
#define VFIO_ASSERT_LE(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, <=, ##__VA_ARGS__)
#define VFIO_ASSERT_GT(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, >, ##__VA_ARGS__)
#define VFIO_ASSERT_GE(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, >=, ##__VA_ARGS__)
#define VFIO_ASSERT_TRUE(_a, ...) VFIO_ASSERT_NE(false, (_a), ##__VA_ARGS__)
#define VFIO_ASSERT_FALSE(_a, ...) VFIO_ASSERT_EQ(false, (_a), ##__VA_ARGS__)
#define VFIO_ASSERT_NULL(_a, ...) VFIO_ASSERT_EQ(NULL, _a, ##__VA_ARGS__)
#define VFIO_ASSERT_NOT_NULL(_a, ...) VFIO_ASSERT_NE(NULL, _a, ##__VA_ARGS__)

#define VFIO_FAIL(_fmt, ...) do {				\
	fprintf(stderr, "%s:%u: FAIL\n\n", __FILE__, __LINE__);	\
	VFIO_LOG_AND_EXIT(_fmt, ##__VA_ARGS__);			\
} while (0)

struct vfio_iommu_mode {
	const char *name;
	const char *container_path;
	unsigned long iommu_type;
};

/*
 * Generator for VFIO selftests fixture variants that replicate across all
 * possible IOMMU modes. Tests must define FIXTURE_VARIANT_ADD_IOMMU_MODE()
 * which should then use FIXTURE_VARIANT_ADD() to create the variant.
 */
#define FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(...) \
FIXTURE_VARIANT_ADD_IOMMU_MODE(vfio_type1_iommu, ##__VA_ARGS__); \
FIXTURE_VARIANT_ADD_IOMMU_MODE(vfio_type1v2_iommu, ##__VA_ARGS__); \
FIXTURE_VARIANT_ADD_IOMMU_MODE(iommufd_compat_type1, ##__VA_ARGS__); \
FIXTURE_VARIANT_ADD_IOMMU_MODE(iommufd_compat_type1v2, ##__VA_ARGS__); \
FIXTURE_VARIANT_ADD_IOMMU_MODE(iommufd, ##__VA_ARGS__)

struct vfio_pci_bar {
	struct vfio_region_info info;
	void *vaddr;
};

typedef u64 iova_t;

#define INVALID_IOVA UINT64_MAX

struct vfio_dma_region {
	struct list_head link;
	void *vaddr;
	iova_t iova;
	u64 size;
};

struct vfio_pci_device;

struct vfio_pci_driver_ops {
	const char *name;

	/**
	 * @probe() - Check if the driver supports the given device.
	 *
	 * Return: 0 on success, non-0 on failure.
	 */
	int (*probe)(struct vfio_pci_device *device);

	/**
	 * @init() - Initialize the driver for @device.
	 *
	 * Must be called after device->driver.region has been initialized.
	 */
	void (*init)(struct vfio_pci_device *device);

	/**
	 * remove() - Deinitialize the driver for @device.
	 */
	void (*remove)(struct vfio_pci_device *device);

	/**
	 * memcpy_start() - Kick off @count repeated memcpy operations from
	 * [@src, @src + @size) to [@dst, @dst + @size).
	 *
	 * Guarantees:
	 *  - The device will attempt DMA reads on [src, src + size).
	 *  - The device will attempt DMA writes on [dst, dst + size).
	 *  - The device will not generate any interrupts.
	 *
	 * memcpy_start() returns immediately, it does not wait for the
	 * copies to complete.
	 */
	void (*memcpy_start)(struct vfio_pci_device *device,
			     iova_t src, iova_t dst, u64 size, u64 count);

	/**
	 * memcpy_wait() - Wait until the memcpy operations started by
	 * memcpy_start() have finished.
	 *
	 * Guarantees:
	 *  - All in-flight DMAs initiated by memcpy_start() are fully complete
	 *    before memcpy_wait() returns.
	 *
	 * Returns non-0 if the driver detects that an error occurred during the
	 * memcpy, 0 otherwise.
	 */
	int (*memcpy_wait)(struct vfio_pci_device *device);

	/**
	 * send_msi() - Make the device send the MSI device->driver.msi.
	 *
	 * Guarantees:
	 *  - The device will send the MSI once.
	 */
	void (*send_msi)(struct vfio_pci_device *device);
};

struct vfio_pci_driver {
	const struct vfio_pci_driver_ops *ops;
	bool initialized;
	bool memcpy_in_progress;

	/* Region to be used by the driver (e.g. for in-memory descriptors) */
	struct vfio_dma_region region;

	/* The maximum size that can be passed to memcpy_start(). */
	u64 max_memcpy_size;

	/* The maximum count that can be passed to memcpy_start(). */
	u64 max_memcpy_count;

	/* The MSI vector the device will signal in ops->send_msi(). */
	int msi;
};

struct vfio_pci_device {
	int fd;

	const struct vfio_iommu_mode *iommu_mode;
	int group_fd;
	int container_fd;

	int iommufd;
	u32 ioas_id;

	struct vfio_device_info info;
	struct vfio_region_info config_space;
	struct vfio_pci_bar bars[PCI_STD_NUM_BARS];

	struct vfio_irq_info msi_info;
	struct vfio_irq_info msix_info;

	struct list_head dma_regions;

	/* eventfds for MSI and MSI-x interrupts */
	int msi_eventfds[PCI_MSIX_FLAGS_QSIZE + 1];

	struct vfio_pci_driver driver;
};

struct iova_allocator {
	struct iommu_iova_range *ranges;
	u32 nranges;
	u32 range_idx;
	u64 range_offset;
};

/*
 * Return the BDF string of the device that the test should use.
 *
 * If a BDF string is provided by the user on the command line (as the last
 * element of argv[]), then this function will return that and decrement argc
 * by 1.
 *
 * Otherwise this function will attempt to use the environment variable
 * $VFIO_SELFTESTS_BDF.
 *
 * If BDF cannot be determined then the test will exit with KSFT_SKIP.
 */
const char *vfio_selftests_get_bdf(int *argc, char *argv[]);
const char *vfio_pci_get_cdev_path(const char *bdf);

extern const char *default_iommu_mode;

struct vfio_pci_device *vfio_pci_device_init(const char *bdf, const char *iommu_mode);
void vfio_pci_device_cleanup(struct vfio_pci_device *device);
void vfio_pci_device_reset(struct vfio_pci_device *device);

struct iommu_iova_range *vfio_pci_iova_ranges(struct vfio_pci_device *device,
					      u32 *nranges);

struct iova_allocator *iova_allocator_init(struct vfio_pci_device *device);
void iova_allocator_cleanup(struct iova_allocator *allocator);
iova_t iova_allocator_alloc(struct iova_allocator *allocator, size_t size);

int __vfio_pci_dma_map(struct vfio_pci_device *device,
		       struct vfio_dma_region *region);
int __vfio_pci_dma_unmap(struct vfio_pci_device *device,
			 struct vfio_dma_region *region,
			 u64 *unmapped);
int __vfio_pci_dma_unmap_all(struct vfio_pci_device *device, u64 *unmapped);

static inline void vfio_pci_dma_map(struct vfio_pci_device *device,
				    struct vfio_dma_region *region)
{
	VFIO_ASSERT_EQ(__vfio_pci_dma_map(device, region), 0);
}

static inline void vfio_pci_dma_unmap(struct vfio_pci_device *device,
				      struct vfio_dma_region *region)
{
	VFIO_ASSERT_EQ(__vfio_pci_dma_unmap(device, region, NULL), 0);
}

static inline void vfio_pci_dma_unmap_all(struct vfio_pci_device *device)
{
	VFIO_ASSERT_EQ(__vfio_pci_dma_unmap_all(device, NULL), 0);
}

void vfio_pci_config_access(struct vfio_pci_device *device, bool write,
			    size_t config, size_t size, void *data);

#define vfio_pci_config_read(_device, _offset, _type) ({			    \
	_type __data;								    \
	vfio_pci_config_access((_device), false, _offset, sizeof(__data), &__data); \
	__data;									    \
})

#define vfio_pci_config_readb(_d, _o) vfio_pci_config_read(_d, _o, u8)
#define vfio_pci_config_readw(_d, _o) vfio_pci_config_read(_d, _o, u16)
#define vfio_pci_config_readl(_d, _o) vfio_pci_config_read(_d, _o, u32)

#define vfio_pci_config_write(_device, _offset, _value, _type) do {		  \
	_type __data = (_value);						  \
	vfio_pci_config_access((_device), true, _offset, sizeof(_type), &__data); \
} while (0)

#define vfio_pci_config_writeb(_d, _o, _v) vfio_pci_config_write(_d, _o, _v, u8)
#define vfio_pci_config_writew(_d, _o, _v) vfio_pci_config_write(_d, _o, _v, u16)
#define vfio_pci_config_writel(_d, _o, _v) vfio_pci_config_write(_d, _o, _v, u32)

void vfio_pci_irq_enable(struct vfio_pci_device *device, u32 index,
			 u32 vector, int count);
void vfio_pci_irq_disable(struct vfio_pci_device *device, u32 index);
void vfio_pci_irq_trigger(struct vfio_pci_device *device, u32 index, u32 vector);

static inline void fcntl_set_nonblock(int fd)
{
	int r;

	r = fcntl(fd, F_GETFL, 0);
	VFIO_ASSERT_NE(r, -1, "F_GETFL failed for fd %d\n", fd);

	r = fcntl(fd, F_SETFL, r | O_NONBLOCK);
	VFIO_ASSERT_NE(r, -1, "F_SETFL O_NONBLOCK failed for fd %d\n", fd);
}

static inline void vfio_pci_msi_enable(struct vfio_pci_device *device,
				       u32 vector, int count)
{
	vfio_pci_irq_enable(device, VFIO_PCI_MSI_IRQ_INDEX, vector, count);
}

static inline void vfio_pci_msi_disable(struct vfio_pci_device *device)
{
	vfio_pci_irq_disable(device, VFIO_PCI_MSI_IRQ_INDEX);
}

static inline void vfio_pci_msix_enable(struct vfio_pci_device *device,
					u32 vector, int count)
{
	vfio_pci_irq_enable(device, VFIO_PCI_MSIX_IRQ_INDEX, vector, count);
}

static inline void vfio_pci_msix_disable(struct vfio_pci_device *device)
{
	vfio_pci_irq_disable(device, VFIO_PCI_MSIX_IRQ_INDEX);
}

iova_t __to_iova(struct vfio_pci_device *device, void *vaddr);
iova_t to_iova(struct vfio_pci_device *device, void *vaddr);

static inline bool vfio_pci_device_match(struct vfio_pci_device *device,
					 u16 vendor_id, u16 device_id)
{
	return (vendor_id == vfio_pci_config_readw(device, PCI_VENDOR_ID)) &&
		(device_id == vfio_pci_config_readw(device, PCI_DEVICE_ID));
}

void vfio_pci_driver_probe(struct vfio_pci_device *device);
void vfio_pci_driver_init(struct vfio_pci_device *device);
void vfio_pci_driver_remove(struct vfio_pci_device *device);
int vfio_pci_driver_memcpy(struct vfio_pci_device *device,
			   iova_t src, iova_t dst, u64 size);
void vfio_pci_driver_memcpy_start(struct vfio_pci_device *device,
				  iova_t src, iova_t dst, u64 size,
				  u64 count);
int vfio_pci_driver_memcpy_wait(struct vfio_pci_device *device);
void vfio_pci_driver_send_msi(struct vfio_pci_device *device);

#endif /* SELFTESTS_VFIO_LIB_INCLUDE_VFIO_UTIL_H */
