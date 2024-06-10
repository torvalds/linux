/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Virtual PTP 1588 clock for use with KVM guests
 *
 * Copyright (C) 2017 Red Hat Inc.
 */

#ifndef _PTP_KVM_H_
#define _PTP_KVM_H_

#include <linux/clocksource_ids.h>
#include <linux/types.h>

struct timespec64;

int kvm_arch_ptp_init(void);
void kvm_arch_ptp_exit(void);
int kvm_arch_ptp_get_clock(struct timespec64 *ts);
int kvm_arch_ptp_get_crosststamp(u64 *cycle,
		struct timespec64 *tspec, enum clocksource_ids *cs_id);

#endif /* _PTP_KVM_H_ */
