#ifndef __LINUX_USB_IOWARRIOR_H
#define __LINUX_USB_IOWARRIOR_H

#define CODEMERCS_MAGIC_NUMBER	0xC0	/* like COde Mercenaries */

/* Define the ioctl commands for reading and writing data */
#define IOW_WRITE	_IOW(CODEMERCS_MAGIC_NUMBER, 1, __u8 *)
#define IOW_READ	_IOW(CODEMERCS_MAGIC_NUMBER, 2, __u8 *)

/*
   A struct for available device info which is read
   with the ioctl IOW_GETINFO.
   To be compatible with 2.4 userspace which didn't have an easy way to get
   this information.
*/
struct iowarrior_info {
	/* vendor id : supposed to be USB_VENDOR_ID_CODEMERCS in all cases */
	__u32 vendor;
	/* product id : depends on type of chip (USB_DEVICE_ID_CODEMERCS_X) */
	__u32 product;
	/* the serial number of our chip (if a serial-number is not available
	 * this is empty string) */
	__u8 serial[9];
	/* revision number of the chip */
	__u32 revision;
	/* USB-speed of the device (0=UNKNOWN, 1=LOW, 2=FULL 3=HIGH) */
	__u32 speed;
	/* power consumption of the device in mA */
	__u32 power;
	/* the number of the endpoint */
	__u32 if_num;
	/* size of the data-packets on this interface */
	__u32 report_size;
};

/*
  Get some device-information (product-id , serial-number etc.)
  in order to identify a chip.
*/
#define IOW_GETINFO _IOR(CODEMERCS_MAGIC_NUMBER, 3, struct iowarrior_info)

#endif /* __LINUX_USB_IOWARRIOR_H */
