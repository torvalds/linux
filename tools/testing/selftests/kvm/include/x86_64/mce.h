/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/x86_64/mce.h
 *
 * Copyright (C) 2022, Google LLC.
 */

#ifndef SELFTEST_KVM_MCE_H
#define SELFTEST_KVM_MCE_H

#define MCG_CTL_P		BIT_ULL(8)   /* MCG_CTL register available */
#define MCG_SER_P		BIT_ULL(24)  /* MCA recovery/new status bits */
#define MCG_LMCE_P		BIT_ULL(27)  /* Local machine check supported */
#define MCG_CMCI_P		BIT_ULL(10)  /* CMCI supported */
#define KVM_MAX_MCE_BANKS 32
#define MCG_CAP_BANKS_MASK 0xff       /* Bit 0-7 of the MCG_CAP register are #banks */
#define MCI_STATUS_VAL (1ULL << 63)   /* valid error */
#define MCI_STATUS_UC (1ULL << 61)    /* uncorrected error */
#define MCI_STATUS_EN (1ULL << 60)    /* error enabled */
#define MCI_STATUS_MISCV (1ULL << 59) /* misc error reg. valid */
#define MCI_STATUS_ADDRV (1ULL << 58) /* addr reg. valid */
#define MCM_ADDR_PHYS 2    /* physical address */
#define MCI_CTL2_CMCI_EN		BIT_ULL(30)

#endif /* SELFTEST_KVM_MCE_H */
