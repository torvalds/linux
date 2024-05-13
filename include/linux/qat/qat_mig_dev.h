/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024 Intel Corporation */
#ifndef QAT_MIG_DEV_H_
#define QAT_MIG_DEV_H_

struct pci_dev;

struct qat_mig_dev {
	void *parent_accel_dev;
	u8 *state;
	u32 setup_size;
	u32 remote_setup_size;
	u32 state_size;
	s32 vf_id;
};

struct qat_mig_dev *qat_vfmig_create(struct pci_dev *pdev, int vf_id);
int qat_vfmig_init(struct qat_mig_dev *mdev);
void qat_vfmig_cleanup(struct qat_mig_dev *mdev);
void qat_vfmig_reset(struct qat_mig_dev *mdev);
int qat_vfmig_open(struct qat_mig_dev *mdev);
void qat_vfmig_close(struct qat_mig_dev *mdev);
int qat_vfmig_suspend(struct qat_mig_dev *mdev);
int qat_vfmig_resume(struct qat_mig_dev *mdev);
int qat_vfmig_save_state(struct qat_mig_dev *mdev);
int qat_vfmig_save_setup(struct qat_mig_dev *mdev);
int qat_vfmig_load_state(struct qat_mig_dev *mdev);
int qat_vfmig_load_setup(struct qat_mig_dev *mdev, int size);
void qat_vfmig_destroy(struct qat_mig_dev *mdev);

#endif /*QAT_MIG_DEV_H_*/
