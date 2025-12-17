/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_H
#define SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_H

#include <libvfio/assert.h>
#include <libvfio/iommu.h>
#include <libvfio/iova_allocator.h>
#include <libvfio/vfio_pci_device.h>
#include <libvfio/vfio_pci_driver.h>

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
char **vfio_selftests_get_bdfs(int *argc, char *argv[], int *nr_bdfs);

#endif /* SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_H */
