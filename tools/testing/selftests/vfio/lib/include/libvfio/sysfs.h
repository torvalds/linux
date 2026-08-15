/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_SYSFS_H
#define SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_SYSFS_H

int sysfs_sriov_totalvfs_get(const char *bdf);
int sysfs_sriov_numvfs_get(const char *bdf);
void sysfs_sriov_numvfs_set(const char *bdf, int numvfs);
char *sysfs_sriov_vf_bdf_get(const char *pf_bdf, int i);
int sysfs_iommu_group_get(const char *bdf);
char *sysfs_driver_get(const char *bdf);

#endif /* SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_SYSFS_H */
