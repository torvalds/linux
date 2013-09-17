/*
* ALSA SoC CX20709 Channel codec driver
*
* Copyright:   (C) 2009/2010 Conexant Systems
*
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* 
*      
*************************************************************************
*  Modified Date:  11/02/11
*  File Version:   2.26.35.11 
*************************************************************************
*/
#ifndef CXDEBUG_H
#define CXDEBUG_H

#define CXDBG_MAJOR	168		/* Device major number		*/


/* Use 'k' as magic number */
#define CXDBG_IOC_MAGIC 'S'

#define MAX_DATA_LEN      64
struct CXDBG_IODATA{
    unsigned short len;
    unsigned char  data[MAX_DATA_LEN];
};

#define CXDBG_IOCTL_REG_SET             _IOWR(CXDBG_IOC_MAGIC, 1, struct CXDBG_IODATA)
#define CXDBG_IOCTL_REG_GET 	        _IOWR(CXDBG_IOC_MAGIC, 2, struct CXDBG_IODATA)
#define CXDBG_IOCTL_PDRIVER_VERSION     _IOR( CXDBG_IOC_MAGIC, 3, int)

#define CXDBG_DEVICE_NAME "cxdbg"

#ifdef __KERNEL__
int cxdbg_dev_init(struct snd_soc_codec * codec);
void cxdbg_dev_exit(void);
#endif 

#endif //#ifndef CXDEBUG_H


