#ifndef __NET_WEXT_H
#define __NET_WEXT_H

/*
 * wireless extensions interface to the core code
 */

#ifdef CONFIG_WIRELESS_EXT
extern int wext_proc_init(void);
extern int wext_handle_ioctl(struct ifreq *ifr, unsigned int cmd,
			     void __user *arg);
#else
static inline int wext_proc_init(void)
{
	return 0;
}
static inline int wext_handle_ioctl(struct ifreq *ifr, unsigned int cmd,
				    void __user *arg)
{
	return -EINVAL;
}
#endif

#endif /* __NET_WEXT_H */
