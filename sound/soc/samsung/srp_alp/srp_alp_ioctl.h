#ifndef __SRP_ALP_IOCTL_H
#define __SRP_ALP_IOCTL_H

#define SRP_INIT				(0x10000)
#define SRP_DEINIT				(0x10001)
#define SRP_GET_MMAP_SIZE			(0x10002)
#define SRP_FLUSH				(0x20002)
#define SRP_SEND_EOS				(0x20005)
#define SRP_GET_IBUF_INFO                       (0x20007)
#define SRP_GET_OBUF_INFO                       (0x20008)
#define SRP_STOP_EOS_STATE			(0x30007)
#define SRP_GET_DEC_INFO			(0x30008)

#endif /* __SRP_ALP_IOCTL_H */
