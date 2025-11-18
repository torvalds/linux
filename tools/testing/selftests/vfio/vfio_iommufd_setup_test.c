// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/types.h>
#include <linux/limits.h>
#include <linux/sizes.h>
#include <linux/vfio.h>
#include <linux/iommufd.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <vfio_util.h>
#include "../kselftest_harness.h"

static const char iommu_dev_path[] = "/dev/iommu";
static const char *cdev_path;

static int vfio_device_bind_iommufd_ioctl(int cdev_fd, int iommufd)
{
	struct vfio_device_bind_iommufd bind_args = {
		.argsz = sizeof(bind_args),
		.iommufd = iommufd,
	};

	return ioctl(cdev_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind_args);
}

static int vfio_device_get_info_ioctl(int cdev_fd)
{
	struct vfio_device_info info_args = { .argsz = sizeof(info_args) };

	return ioctl(cdev_fd, VFIO_DEVICE_GET_INFO, &info_args);
}

static int vfio_device_ioas_alloc_ioctl(int iommufd, struct iommu_ioas_alloc *alloc_args)
{
	*alloc_args = (struct iommu_ioas_alloc){
		.size = sizeof(struct iommu_ioas_alloc),
	};

	return ioctl(iommufd, IOMMU_IOAS_ALLOC, alloc_args);
}

static int vfio_device_attach_iommufd_pt_ioctl(int cdev_fd, u32 pt_id)
{
	struct vfio_device_attach_iommufd_pt attach_args = {
		.argsz = sizeof(attach_args),
		.pt_id = pt_id,
	};

	return ioctl(cdev_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach_args);
}

static int vfio_device_detach_iommufd_pt_ioctl(int cdev_fd)
{
	struct vfio_device_detach_iommufd_pt detach_args = {
		.argsz = sizeof(detach_args),
	};

	return ioctl(cdev_fd, VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach_args);
}

FIXTURE(vfio_cdev) {
	int cdev_fd;
	int iommufd;
};

FIXTURE_SETUP(vfio_cdev)
{
	ASSERT_LE(0, (self->cdev_fd = open(cdev_path, O_RDWR, 0)));
	ASSERT_LE(0, (self->iommufd = open(iommu_dev_path, O_RDWR, 0)));
}

FIXTURE_TEARDOWN(vfio_cdev)
{
	ASSERT_EQ(0, close(self->cdev_fd));
	ASSERT_EQ(0, close(self->iommufd));
}

TEST_F(vfio_cdev, bind)
{
	ASSERT_EQ(0, vfio_device_bind_iommufd_ioctl(self->cdev_fd, self->iommufd));
	ASSERT_EQ(0, vfio_device_get_info_ioctl(self->cdev_fd));
}

TEST_F(vfio_cdev, get_info_without_bind_fails)
{
	ASSERT_NE(0, vfio_device_get_info_ioctl(self->cdev_fd));
}

TEST_F(vfio_cdev, bind_bad_iommufd_fails)
{
	ASSERT_NE(0, vfio_device_bind_iommufd_ioctl(self->cdev_fd, -2));
}

TEST_F(vfio_cdev, repeated_bind_fails)
{
	ASSERT_EQ(0, vfio_device_bind_iommufd_ioctl(self->cdev_fd, self->iommufd));
	ASSERT_NE(0, vfio_device_bind_iommufd_ioctl(self->cdev_fd, self->iommufd));
}

TEST_F(vfio_cdev, attach_detatch_pt)
{
	struct iommu_ioas_alloc alloc_args;

	ASSERT_EQ(0, vfio_device_bind_iommufd_ioctl(self->cdev_fd, self->iommufd));
	ASSERT_EQ(0, vfio_device_ioas_alloc_ioctl(self->iommufd, &alloc_args));
	ASSERT_EQ(0, vfio_device_attach_iommufd_pt_ioctl(self->cdev_fd, alloc_args.out_ioas_id));
	ASSERT_EQ(0, vfio_device_detach_iommufd_pt_ioctl(self->cdev_fd));
}

TEST_F(vfio_cdev, attach_invalid_pt_fails)
{
	ASSERT_EQ(0, vfio_device_bind_iommufd_ioctl(self->cdev_fd, self->iommufd));
	ASSERT_NE(0, vfio_device_attach_iommufd_pt_ioctl(self->cdev_fd, UINT32_MAX));
}

int main(int argc, char *argv[])
{
	const char *device_bdf = vfio_selftests_get_bdf(&argc, argv);

	cdev_path = vfio_pci_get_cdev_path(device_bdf);
	printf("Using cdev device %s\n", cdev_path);

	return test_harness_run(argc, argv);
}
