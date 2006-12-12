#ifndef __ASM_SH_LANDISK_GIO_H
#define __ASM_SH_LANDISK_GIO_H

#include <linux/ioctl.h>

/* version */
#define VERSION_STR	"1.00"

/* Driver name */
#define GIO_DRIVER_NAME		"/dev/giodrv"

/* Use 'k' as magic number */
#define GIODRV_IOC_MAGIC  'k'

#define GIODRV_IOCRESET    _IO(GIODRV_IOC_MAGIC, 0)
/*
 * S means "Set" through a ptr,
 * T means "Tell" directly
 * G means "Get" (to a pointed var)
 * Q means "Query", response is on the return value
 * X means "eXchange": G and S atomically
 * H means "sHift": T and Q atomically
 */
#define GIODRV_IOCSGIODATA1   _IOW(GIODRV_IOC_MAGIC,  1, unsigned char *)
#define GIODRV_IOCGGIODATA1   _IOR(GIODRV_IOC_MAGIC,  2, unsigned char *)
#define GIODRV_IOCSGIODATA2   _IOW(GIODRV_IOC_MAGIC,  3, unsigned short *)
#define GIODRV_IOCGGIODATA2   _IOR(GIODRV_IOC_MAGIC,  4, unsigned short *)
#define GIODRV_IOCSGIODATA4   _IOW(GIODRV_IOC_MAGIC,  5, unsigned long *)
#define GIODRV_IOCGGIODATA4   _IOR(GIODRV_IOC_MAGIC,  6, unsigned long *)
#define GIODRV_IOCSGIOSETADDR _IOW(GIODRV_IOC_MAGIC,  7, unsigned long *)
#define GIODRV_IOCHARDRESET   _IO(GIODRV_IOC_MAGIC, 8) /* debugging tool */

#define GIODRV_IOCSGIO_LED    _IOW(GIODRV_IOC_MAGIC,  9, unsigned long *)
#define GIODRV_IOCGGIO_LED    _IOR(GIODRV_IOC_MAGIC,  10, unsigned long *)
#define GIODRV_IOCSGIO_BUZZER _IOW(GIODRV_IOC_MAGIC,  11, unsigned long *)
#define GIODRV_IOCGGIO_LANDISK _IOR(GIODRV_IOC_MAGIC,  14, unsigned long *)
#define GIODRV_IOCGGIO_BTN _IOR(GIODRV_IOC_MAGIC,  22, unsigned long *)
#define GIODRV_IOCSGIO_BTNPID _IOW(GIODRV_IOC_MAGIC,  23, unsigned long *)
#define GIODRV_IOCGGIO_BTNPID _IOR(GIODRV_IOC_MAGIC,  24, unsigned long *)

#define GIODRV_IOC_MAXNR 8
#define GIO_READ 0x00000000
#define GIO_WRITE 0x00000001

#endif /* __ASM_SH_LANDISK_GIO_H  */
