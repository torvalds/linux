/*
 * asm-s390/kvm_para.h - definition for paravirtual devices on s390
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 */

#ifndef __S390_KVM_PARA_H
#define __S390_KVM_PARA_H

/*
 * No hypercalls for KVM on s390
 */

static inline int kvm_para_available(void)
{
	return 0;
}

static inline unsigned int kvm_arch_para_features(void)
{
	return 0;
}

#endif /* __S390_KVM_PARA_H */
