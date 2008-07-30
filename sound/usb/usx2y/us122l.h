#ifndef US122L_H
#define US122L_H


struct us122l {
	struct snd_usb_audio 	chip;
	int			stride;
	struct usb_stream_kernel sk;

	struct mutex		mutex;
	struct file		*first;
	unsigned		second_periods_polled;
	struct file		*master;
	struct file		*slave;

	atomic_t		mmap_count;
};


#define US122L(c) ((struct us122l *)(c)->private_data)

#define NAME_ALLCAPS "US-122L"

#define USB_ID_US122L 0x800E
#define USB_ID_US144 0x800F

#endif
