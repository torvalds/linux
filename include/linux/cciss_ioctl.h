/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CCISS_IOCTLH
#define CCISS_IOCTLH

#include <uapi/linux/cciss_ioctl.h>

#ifdef CONFIG_COMPAT

/* 32 bit compatible ioctl structs */
typedef struct _IOCTL32_Command_struct {
  LUNAddr_struct	   LUN_info;
  RequestBlock_struct      Request;
  ErrorInfo_struct  	   error_info;
  WORD			   buf_size;  /* size in bytes of the buf */
  __u32			   buf; /* 32 bit pointer to data buffer */
} IOCTL32_Command_struct;

typedef struct _BIG_IOCTL32_Command_struct {
  LUNAddr_struct	   LUN_info;
  RequestBlock_struct      Request;
  ErrorInfo_struct  	   error_info;
  DWORD			   malloc_size; /* < MAX_KMALLOC_SIZE in cciss.c */
  DWORD			   buf_size;    /* size in bytes of the buf */
  				        /* < malloc_size * MAXSGENTRIES */
  __u32 		buf;	/* 32 bit pointer to data buffer */
} BIG_IOCTL32_Command_struct;

#define CCISS_PASSTHRU32   _IOWR(CCISS_IOC_MAGIC, 11, IOCTL32_Command_struct)
#define CCISS_BIG_PASSTHRU32 _IOWR(CCISS_IOC_MAGIC, 18, BIG_IOCTL32_Command_struct)

#endif /* CONFIG_COMPAT */
#endif  
