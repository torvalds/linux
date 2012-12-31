#ifndef __HSIC_USB3503_H__
#define __HSIC_USB3503_H__

#define		USB3503_VIDL		0x00
#define		USB3503_VIDM		0x01
#define		USB3503_PIDL		0x02
#define		USB3503_PIDM		0x03
#define		USB3503_DIDL		0x04
#define		USB3503_DIDM		0x05
#define		USB3503_CFG1		0x06
#define		USB3503_CFG2		0x07
#define		USB3503_CFG3		0x08
#define		USB3503_NRD			0x09
#define		USB3503_PDS			0x0A
#define		USB3503_PDB			0x0B
#define		USB3503_MAXPS		0x0C
#define		USB3503_MAXPB		0x0D
#define		USB3503_HCMCS		0x0E
#define		USB3503_HCMCB		0x0F

#define		USB3503_PWRT		0x10
#define		USB3503_LANGIDH		0x11
#define		USB3503_LANGIDL		0x12
#define		USB3503_MFRSL		0x13
#define		USB3503_PRDSL		0x14
#define		USB3503_SERSL		0x15

#define		USB3503_MANSTR		0x16	// 0x16 ~ 0x53 Manufacture string
#define		USB3503_PRDSTR		0x54	// 0x54 ~ 0x91 Product string
#define		USB3503_SERSTR		0x92	// 0x92 ~ 0xCF Serial string

#define		USB3503_BCEN		0xD0

#define		USB3503_PRTPWR		0xE5
#define		USB3503_OCS			0xE6
#define		USB3503_SP_ILOCK	0xE7
#define		USB3503_INT_STATUS	0xE8
#define		USB3503_INT_MASK	0xE9

#define		USB3503_CFGP		0xEE

#define		USB3503_VSNSUP3		0xF4
#define		USB3503_VSNS21		0xF5
#define		USB3503_BSTUP3		0xF6
#define		USB3503_BST21		0xF8

#define		USB3503_PRTSP		0xFA
#define		USB3503_PRTR12		0xFB
#define		USB3503_PRTR34		0xFC

#define		USB3503_STCD		0xFF


struct usb3503_platform_data {
	int gpio_reset;
	int gpio_hub_con;

	int sys_irq;
	int irq_gpio;
};

#endif //__HSIC_USB3503_H__
