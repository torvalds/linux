// Copyright (c) 2010 Atheros Communications Inc.
// All rights reserved.
// $ATH_LICENSE_TARGET_C$

#ifndef __DK_IOCTL_H_
#define __DK_IOCTL_H_

#define DK_IOCTL_GET_VERSION 1
#define DK_IOCTL_GET_CLIENT_INFO 2
#define DK_IOCTL_CFG_READ 3
#define DK_IOCTL_CFG_WRITE 4
#define DK_IOCTL_CREATE_EVENT 5
#define DK_IOCTL_GET_NEXT_EVENT 6
#define DK_IOCTL_SYS_REG_READ_32 7
#define DK_IOCTL_SYS_REG_WRITE_32 8
#define DK_IOCTL_FLASH_READ 9
#define DK_IOCTL_FLASH_WRITE 10
#define DK_IOCTL_MAC_WRITE 11
#define DK_IOCTL_GET_CHIP_ID 12
#define DK_IOCTL_RTC_REG_READ 13
#define DK_IOCTL_RTC_REG_WRITE 14

#undef MAX_BARS
#define MAX_BARS    6


struct cfg_op {
	int offset;
	int size;
	int value;
};

struct client_info {
	int reg_phy_addr;
	int reg_range;
	int mem_phy_addr;
	int mem_size;
	int irq;
	int areg_phy_addr[MAX_BARS];
	int areg_range[MAX_BARS];
    int numBars;
    int device_class;
};

struct event_op {
	unsigned int valid;
	unsigned int param[16];
};

struct flash_op{
	int fcl;
	int offset;
	int len;
	int retlen;
	A_UINT8 value;
};

struct flash_op_wr{
	int fcl;
	int offset;
	int len;
	int retlen;
	A_UINT8 *pvalue;
};

struct flash_mac_addr{
	int len;
	A_UINT8 *pAddr0;
	A_UINT8 *pAddr1;
};


struct ath_mac_cfg{
	int freq_setting;
	unsigned char macAddr0[6];
	unsigned char macAddr1[6];
	unsigned char dummy[240];
};

#endif
