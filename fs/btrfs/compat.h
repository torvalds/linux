#ifndef _COMPAT_H_
#define _COMPAT_H_

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26)
#define trylock_page(page) (!TestSetPageLocked(page))
#endif

/*
 * Even if AppArmor isn't enabled, it still has different prototypes.
 * Add more distro/version pairs here to declare which has AppArmor applied.
 */
#if defined(CONFIG_SUSE_KERNEL)
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
# define REMOVE_SUID_PATH 1
# endif
#endif

/*
 * catch any other distros that have patched in apparmor.  This isn't
 * 100% reliable because it won't catch people that hand compile their
 * own distro kernels without apparmor compiled in.  But, it is better
 * than nothing.
 */
#ifdef CONFIG_SECURITY_APPARMOR
# define REMOVE_SUID_PATH 1
#endif

#endif /* _COMPAT_H_ */
