/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * kvm asynchronous fault support
 *
 * Copyright 2010 Red Hat, Inc.
 *
 * Author:
 *      Gleb Natapov <gleb@redhat.com>
 */

#ifndef __KVM_ASYNC_PF_H__
#define __KVM_ASYNC_PF_H__

#ifdef CONFIG_KVM_ASYNC_PF
int kvm_async_pf_init(void);
void kvm_async_pf_deinit(void);
void kvm_async_pf_vcpu_init(struct kvm_vcpu *vcpu);
#else
#define kvm_async_pf_init() (0)
#define kvm_async_pf_deinit() do {} while (0)
#define kvm_async_pf_vcpu_init(C) do {} while (0)
#endif

#endif
