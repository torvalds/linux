#ifndef __EZUSB_H
#define __EZUSB_H


extern int ezusb_writememory(struct usb_device *dev, int address,
			     unsigned char *data, int length, __u8 bRequest);

extern int ezusb_fx1_set_reset(struct usb_device *dev, unsigned char reset_bit);
extern int ezusb_fx2_set_reset(struct usb_device *dev, unsigned char reset_bit);

extern int ezusb_fx1_ihex_firmware_download(struct usb_device *dev,
					    const char *firmware_path);
extern int ezusb_fx2_ihex_firmware_download(struct usb_device *dev,
					    const char *firmware_path);

#endif /* __EZUSB_H */
