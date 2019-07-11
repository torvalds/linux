/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>


Linux HPI ioctl, and shared module init functions
*******************************************************************************/

int asihpi_adapter_probe(struct pci_dev *pci_dev,
			 const struct pci_device_id *pci_id);
void asihpi_adapter_remove(struct pci_dev *pci_dev);
void __init asihpi_init(void);
void __exit asihpi_exit(void);

int asihpi_hpi_release(struct file *file);

long asihpi_hpi_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* This is called from hpifunc.c functions, called by ALSA
 * (or other kernel process) In this case there is no file descriptor
 * available for the message cache code
 */
void hpi_send_recv(struct hpi_message *phm, struct hpi_response *phr);

#define HOWNER_KERNEL ((void *)-1)
