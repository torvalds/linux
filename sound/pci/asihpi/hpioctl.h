/*******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License as
    published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Linux HPI ioctl, and shared module init functions
*******************************************************************************/

int __devinit asihpi_adapter_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *pci_id);
void __devexit asihpi_adapter_remove(struct pci_dev *pci_dev);
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
