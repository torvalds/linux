/* net/atm/common.h - ATM sockets (common part for PVC and SVC) */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#ifndef NET_ATM_COMMON_H
#define NET_ATM_COMMON_H

#include <linux/net.h>
#include <linux/poll.h> /* for poll_table */


int vcc_create(struct net *net, struct socket *sock, int protocol, int family);
int vcc_release(struct socket *sock);
int vcc_connect(struct socket *sock, int itf, short vpi, int vci);
int vcc_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
		size_t size, int flags);
int vcc_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *m,
		size_t total_len);
unsigned int vcc_poll(struct file *file, struct socket *sock, poll_table *wait);
int vcc_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
int vcc_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
int vcc_setsockopt(struct socket *sock, int level, int optname,
		   char __user *optval, int optlen);
int vcc_getsockopt(struct socket *sock, int level, int optname,
		   char __user *optval, int __user *optlen);

int atmpvc_init(void);
void atmpvc_exit(void);
int atmsvc_init(void);
void atmsvc_exit(void);
int atm_sysfs_init(void);
void atm_sysfs_exit(void);

#ifdef CONFIG_PROC_FS
int atm_proc_init(void);
void atm_proc_exit(void);
#else
static inline int atm_proc_init(void)
{
	return 0;
}

static inline void atm_proc_exit(void)
{
	/* nothing */
}
#endif /* CONFIG_PROC_FS */

/* SVC */
int svc_change_qos(struct atm_vcc *vcc,struct atm_qos *qos);

void atm_dev_release_vccs(struct atm_dev *dev);

#endif
