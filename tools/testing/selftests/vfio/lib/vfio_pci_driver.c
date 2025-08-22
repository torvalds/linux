// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include "../../../kselftest.h"
#include <vfio_util.h>

#ifdef __x86_64__
extern struct vfio_pci_driver_ops dsa_ops;
extern struct vfio_pci_driver_ops ioat_ops;
#endif

static struct vfio_pci_driver_ops *driver_ops[] = {
#ifdef __x86_64__
	&dsa_ops,
	&ioat_ops,
#endif
};

void vfio_pci_driver_probe(struct vfio_pci_device *device)
{
	struct vfio_pci_driver_ops *ops;
	int i;

	VFIO_ASSERT_NULL(device->driver.ops);

	for (i = 0; i < ARRAY_SIZE(driver_ops); i++) {
		ops = driver_ops[i];

		if (ops->probe(device))
			continue;

		printf("Driver found: %s\n", ops->name);
		device->driver.ops = ops;
	}
}

static void vfio_check_driver_op(struct vfio_pci_driver *driver, void *op,
				 const char *op_name)
{
	VFIO_ASSERT_NOT_NULL(driver->ops);
	VFIO_ASSERT_NOT_NULL(op, "Driver has no %s()\n", op_name);
	VFIO_ASSERT_EQ(driver->initialized, op != driver->ops->init);
	VFIO_ASSERT_EQ(driver->memcpy_in_progress, op == driver->ops->memcpy_wait);
}

#define VFIO_CHECK_DRIVER_OP(_driver, _op) do {				\
	struct vfio_pci_driver *__driver = (_driver);			\
	vfio_check_driver_op(__driver, __driver->ops->_op, #_op);	\
} while (0)

void vfio_pci_driver_init(struct vfio_pci_device *device)
{
	struct vfio_pci_driver *driver = &device->driver;

	VFIO_ASSERT_NOT_NULL(driver->region.vaddr);
	VFIO_CHECK_DRIVER_OP(driver, init);

	driver->ops->init(device);

	driver->initialized = true;

	printf("%s: region: vaddr %p, iova 0x%lx, size 0x%lx\n",
	       driver->ops->name,
	       driver->region.vaddr,
	       driver->region.iova,
	       driver->region.size);

	printf("%s: max_memcpy_size 0x%lx, max_memcpy_count 0x%lx\n",
	       driver->ops->name,
	       driver->max_memcpy_size,
	       driver->max_memcpy_count);
}

void vfio_pci_driver_remove(struct vfio_pci_device *device)
{
	struct vfio_pci_driver *driver = &device->driver;

	VFIO_CHECK_DRIVER_OP(driver, remove);

	driver->ops->remove(device);
	driver->initialized = false;
}

void vfio_pci_driver_send_msi(struct vfio_pci_device *device)
{
	struct vfio_pci_driver *driver = &device->driver;

	VFIO_CHECK_DRIVER_OP(driver, send_msi);

	driver->ops->send_msi(device);
}

void vfio_pci_driver_memcpy_start(struct vfio_pci_device *device,
				  iova_t src, iova_t dst, u64 size,
				  u64 count)
{
	struct vfio_pci_driver *driver = &device->driver;

	VFIO_ASSERT_LE(size, driver->max_memcpy_size);
	VFIO_ASSERT_LE(count, driver->max_memcpy_count);
	VFIO_CHECK_DRIVER_OP(driver, memcpy_start);

	driver->ops->memcpy_start(device, src, dst, size, count);
	driver->memcpy_in_progress = true;
}

int vfio_pci_driver_memcpy_wait(struct vfio_pci_device *device)
{
	struct vfio_pci_driver *driver = &device->driver;
	int r;

	VFIO_CHECK_DRIVER_OP(driver, memcpy_wait);

	r = driver->ops->memcpy_wait(device);
	driver->memcpy_in_progress = false;

	return r;
}

int vfio_pci_driver_memcpy(struct vfio_pci_device *device,
			   iova_t src, iova_t dst, u64 size)
{
	vfio_pci_driver_memcpy_start(device, src, dst, size, 1);

	return vfio_pci_driver_memcpy_wait(device);
}
