/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_VFIO_H_
#define _XE_SRIOV_VFIO_H_

#include <linux/types.h>

struct pci_dev;
struct xe_device;

/**
 * xe_sriov_vfio_get_pf() - Get PF &xe_device.
 * @pdev: the VF &pci_dev device
 *
 * Return: pointer to PF &xe_device, NULL otherwise.
 */
struct xe_device *xe_sriov_vfio_get_pf(struct pci_dev *pdev);

/**
 * xe_sriov_vfio_migration_supported() - Check if migration is supported.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 *
 * Return: true if migration is supported, false otherwise.
 */
bool xe_sriov_vfio_migration_supported(struct xe_device *xe);

/**
 * xe_sriov_vfio_wait_flr_done() - Wait for VF FLR completion.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * This function will wait until VF FLR is processed by PF on all tiles (or
 * until timeout occurs).
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_vfio_wait_flr_done(struct xe_device *xe, unsigned int vfid);

/**
 * xe_sriov_vfio_suspend_device() - Suspend VF.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * This function will pause VF on all tiles/GTs.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_vfio_suspend_device(struct xe_device *xe, unsigned int vfid);

/**
 * xe_sriov_vfio_resume_device() - Resume VF.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * This function will resume VF on all tiles.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_vfio_resume_device(struct xe_device *xe, unsigned int vfid);

/**
 * xe_sriov_vfio_stop_copy_enter() - Initiate a VF device migration data save.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_vfio_stop_copy_enter(struct xe_device *xe, unsigned int vfid);

/**
 * xe_sriov_vfio_stop_copy_exit() - Finish a VF device migration data save.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_vfio_stop_copy_exit(struct xe_device *xe, unsigned int vfid);

/**
 * xe_sriov_vfio_resume_data_enter() - Initiate a VF device migration data restore.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_vfio_resume_data_enter(struct xe_device *xe, unsigned int vfid);

/**
 * xe_sriov_vfio_resume_data_exit() - Finish a VF device migration data restore.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_vfio_resume_data_exit(struct xe_device *xe, unsigned int vfid);

/**
 * xe_sriov_vfio_error() - Move VF device to error state.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * Reset is needed to move it out of error state.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_vfio_error(struct xe_device *xe, unsigned int vfid);

/**
 * xe_sriov_vfio_data_read() - Read migration data from the VF device.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 * @buf: start address of userspace buffer
 * @len: requested read size from userspace
 *
 * Return: number of bytes that has been successfully read,
 *	   0 if no more migration data is available, -errno on failure.
 */
ssize_t xe_sriov_vfio_data_read(struct xe_device *xe, unsigned int vfid,
				char __user *buf, size_t len);
/**
 * xe_sriov_vfio_data_write() - Write migration data to the VF device.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 * @buf: start address of userspace buffer
 * @len: requested write size from userspace
 *
 * Return: number of bytes that has been successfully written, -errno on failure.
 */
ssize_t xe_sriov_vfio_data_write(struct xe_device *xe, unsigned int vfid,
				 const char __user *buf, size_t len);
/**
 * xe_sriov_vfio_stop_copy_size() - Get a size estimate of VF device migration data.
 * @xe: the PF &xe_device obtained by calling xe_sriov_vfio_get_pf()
 * @vfid: the VF identifier (can't be 0)
 *
 * Return: migration data size in bytes or a negative error code on failure.
 */
ssize_t xe_sriov_vfio_stop_copy_size(struct xe_device *xe, unsigned int vfid);

#endif
