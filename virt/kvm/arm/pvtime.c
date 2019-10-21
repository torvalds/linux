// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Arm Ltd.

#include <linux/arm-smccc.h>

#include <kvm/arm_hypercalls.h>

long kvm_hypercall_pv_features(struct kvm_vcpu *vcpu)
{
	u32 feature = smccc_get_arg1(vcpu);
	long val = SMCCC_RET_NOT_SUPPORTED;

	switch (feature) {
	case ARM_SMCCC_HV_PV_TIME_FEATURES:
		val = SMCCC_RET_SUCCESS;
		break;
	}

	return val;
}
