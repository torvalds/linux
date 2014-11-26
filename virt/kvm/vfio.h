#ifndef __KVM_VFIO_H
#define __KVM_VFIO_H

#ifdef CONFIG_KVM_VFIO
int kvm_vfio_ops_init(void);
void kvm_vfio_ops_exit(void);
#else
static inline int kvm_vfio_ops_init(void)
{
	return 0;
}
static inline void kvm_vfio_ops_exit(void)
{
}
#endif

#endif
