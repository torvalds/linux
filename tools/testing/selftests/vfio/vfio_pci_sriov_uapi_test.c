// SPDX-License-Identifier: GPL-2.0-only
#include "lib/include/libvfio/assert.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/limits.h>

#include <libvfio.h>

#include "../kselftest_harness.h"

#define UUID_1 "52ac9bff-3a88-4fbd-901a-0d767c3b6c97"
#define UUID_2 "88594674-90a0-47a9-aea8-9d9b352ac08a"

static const char *pf_bdf;
static char *vf_bdf;

static pid_t main_pid;

static int container_setup(struct vfio_pci_device *device, const char *bdf,
			   const char *vf_token)
{
	vfio_pci_group_setup(device, bdf);
	vfio_container_set_iommu(device);
	__vfio_pci_group_get_device_fd(device, bdf, vf_token);

	/* The device fd will be -1 in case of mismatched tokens */
	return (device->fd < 0);
}

static int iommufd_setup(struct vfio_pci_device *device, const char *bdf,
			 const char *vf_token)
{
	vfio_pci_cdev_open(device, bdf);
	return __vfio_device_bind_iommufd(device->fd,
					  device->iommu->iommufd, vf_token);
}

static int device_init(const char *bdf, struct iommu *iommu,
		       const char *vf_token, struct vfio_pci_device **out_dev)
{
	struct vfio_pci_device *device = vfio_pci_device_alloc(bdf, iommu);
	int ret;

	if (iommu->mode->container_path)
		ret = container_setup(device, bdf, vf_token);
	else
		ret = iommufd_setup(device, bdf, vf_token);

	*out_dev = device;
	return ret;
}

static void device_cleanup(struct vfio_pci_device *device)
{
	if (!device)
		return;

	if (device->fd > 0)
		VFIO_ASSERT_EQ(close(device->fd), 0);

	if (device->group_fd)
		VFIO_ASSERT_EQ(close(device->group_fd), 0);

	vfio_pci_device_free(device);
}

FIXTURE(vfio_pci_sriov_uapi_test) {
	struct vfio_pci_device *pf;
	struct vfio_pci_device *vf;
	struct iommu *iommu;
	char *pf_token;
};

FIXTURE_VARIANT(vfio_pci_sriov_uapi_test) {
	const char *iommu_mode;
	char *vf_token;
};

#define FIXTURE_VARIANT_ADD_IOMMU_MODE(_iommu_mode, _name, _vf_token)		\
FIXTURE_VARIANT_ADD(vfio_pci_sriov_uapi_test, _iommu_mode ## _ ## _name) {	\
	.iommu_mode = #_iommu_mode,						\
	.vf_token = (_vf_token),						\
}

FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(same_uuid, UUID_1);
FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(diff_uuid, UUID_2);
FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(null_uuid, NULL);

FIXTURE_SETUP(vfio_pci_sriov_uapi_test)
{
	self->iommu = iommu_init(variant->iommu_mode);

	self->pf_token = UUID_1;
	ASSERT_EQ(device_init(pf_bdf, self->iommu, self->pf_token, &self->pf), 0);
}

FIXTURE_TEARDOWN(vfio_pci_sriov_uapi_test)
{
	device_cleanup(self->vf);
	device_cleanup(self->pf);
	iommu_cleanup(self->iommu);
}

/*
 * This asserts if the VF device is successfully created if its token matches
 * with the token used to create/override the PF or fails during a mismatch.
 */
#define ASSERT_COND_VF_CREATION(_ret) do {					\
	if (!variant->vf_token || strcmp(self->pf_token, variant->vf_token)) {	\
		ASSERT_NE((_ret), 0);						\
	} else {								\
		ASSERT_EQ((_ret), 0);						\
	}									\
} while (0)

/*
 * Validate if the UAPI handles correctly and incorrectly set token on the VF.
 */
TEST_F(vfio_pci_sriov_uapi_test, init_token_match)
{
	int ret;

	ret = device_init(vf_bdf, self->iommu, variant->vf_token, &self->vf);
	ASSERT_COND_VF_CREATION(ret);
}

/*
 * After closing the PF, validate if the VF access still needs the right token.
 */
TEST_F(vfio_pci_sriov_uapi_test, pf_early_close)
{
	int ret;

	device_cleanup(self->pf);

	/* Clean the 'pf' to avoid calling device_cleanup() again. */
	self->pf = NULL;

	ret = device_init(vf_bdf, self->iommu, variant->vf_token, &self->vf);
	ASSERT_COND_VF_CREATION(ret);
}

/*
 * After PF device init, override the existing token and validate if the newly
 * set token is the one that's active.
 */
TEST_F(vfio_pci_sriov_uapi_test, override_token)
{
	int ret;

	self->pf_token = UUID_2;
	vfio_device_set_vf_token(self->pf->fd, self->pf_token);

	ret = device_init(vf_bdf, self->iommu, variant->vf_token, &self->vf);
	ASSERT_COND_VF_CREATION(ret);
}

static void vf_teardown(void)
{
	/*
	 * The child processes, created by TEST_F()s, inherits this atexit()
	 * handler. Hence, check and destroy the VF only when the main/parent
	 * process exits.
	 */
	if (getpid() != main_pid)
		return;

	free(vf_bdf);
	sysfs_sriov_numvfs_set(pf_bdf, 0);
}

static void vf_setup(void)
{
	char *vf_driver;
	int nr_vfs;

	nr_vfs = sysfs_sriov_totalvfs_get(pf_bdf);
	if (nr_vfs <= 0)
		ksft_exit_skip("SR-IOV may not be supported by the PF: %s\n", pf_bdf);

	nr_vfs = sysfs_sriov_numvfs_get(pf_bdf);
	if (nr_vfs != 0)
		ksft_exit_skip("SR-IOV already configured for the PF: %s\n", pf_bdf);

	/* Create only one VF for testing */
	sysfs_sriov_numvfs_set(pf_bdf, 1);

	/*
	 * Setup an exit handler to destroy the VF in case of failures
	 * during further setup at the end of the test run.
	 */
	main_pid = getpid();
	VFIO_ASSERT_EQ(atexit(vf_teardown), 0);

	vf_bdf = sysfs_sriov_vf_bdf_get(pf_bdf, 0);

	/*
	 * The VF inherits the driver from the PF.
	 * Ensure this is 'vfio-pci' before proceeding.
	 */
	vf_driver = sysfs_driver_get(vf_bdf);
	VFIO_ASSERT_NE(vf_driver, NULL);
	VFIO_ASSERT_EQ(strcmp(vf_driver, "vfio-pci"), 0);
	free(vf_driver);

	printf("Created 1 VF (%s) under the PF: %s\n", vf_bdf, pf_bdf);
}

int main(int argc, char *argv[])
{
	pf_bdf = vfio_selftests_get_bdf(&argc, argv);
	vf_setup();

	return test_harness_run(argc, argv);
}
