/*
 *	PCI sound skeleton example
 *
 *	(c) 1998 Red Hat Software
 *
 *	This software may be used and distributed according to the 
 *	terms of the GNU General Public License, incorporated herein by 
 *	reference.
 *
 *	This example is designed to be built in the linux/drivers/sound
 *	directory as part of a kernel build. The example is modular only
 *	drop me a note once you have a working modular driver and want
 *	to integrate it with the main code.
 *		-- Alan <alan@redhat.com>
 *
 *	This is a first draft. Please report any errors, corrections or
 *	improvements to me.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include <asm/io.h>

#include "sound_config.h"

/*
 *	Define our PCI vendor ID here
 */
 
#ifndef PCI_VENDOR_MYIDENT
#define PCI_VENDOR_MYIDENT			0x125D

/*
 *	PCI identity for the card.
 */
 
#define PCI_DEVICE_ID_MYIDENT_MYCARD1		0x1969
#endif

#define CARD_NAME	"ExampleWave 3D Pro Ultra ThingyWotsit"

#define MAX_CARDS	8

/*
 *	Each address_info object holds the information about one of
 *	our card resources. In this case the MSS emulation of our
 *	ficticious card. Its used to manage and attach things.
 */
 
static struct address_info	mss_data[MAX_CARDS];
static int 			cards;

/*
 *	Install the actual card. This is an example
 */

static int mycard_install(struct pci_dev *pcidev)
{
	int iobase;
	int mssbase;
	int mpubase;
	u8 x;
	u16 w;
	u32 v;
	int i;
	int dma;

	/*
	 *	Our imaginary code has its I/O on PCI address 0, a
	 *	MSS on PCI address 1 and an MPU on address 2
	 *
	 *	For the example we will only initialise the MSS
	 */
	 	
	iobase = pci_resource_start(pcidev, 0);
	mssbase = pci_resource_start(pcidev, 1);
	mpubase = pci_resource_start(pcidev, 2);
	
	/*
	 *	Reset the board
	 */
	 
	/*
	 *	Wait for completion. udelay() waits in microseconds
	 */
	 
	udelay(100);
	
	/*
	 *	Ok card ready. Begin setup proper. You might for example
	 *	load the firmware here
	 */
	
	dma = card_specific_magic(ioaddr);
	
	/*
	 *	Turn on legacy mode (example), There are also byte and
	 *	dword (32bit) PCI configuration function calls
	 */

	pci_read_config_word(pcidev, 0x40, &w);
	w&=~(1<<15);			/* legacy decode on */
	w|=(1<<14);			/* Reserved write as 1 in this case */
	w|=(1<<3)|(1<<1)|(1<<0);	/* SB on , FM on, MPU on */
	pci_write_config_word(pcidev, 0x40, w);
	
	/*
	 *	Let the user know we found his toy.
	 */
	 
	printk(KERN_INFO "Programmed "CARD_NAME" at 0x%X to legacy mode.\n",
		iobase);
		
	/*
	 *	Now set it up the description of the card
	 */
	 
	mss_data[cards].io_base = mssbase;
	mss_data[cards].irq = pcidev->irq;
	mss_data[cards].dma = dma;
	
	/*
	 *	Check there is an MSS present
	 */

	if(ad1848_detect(mssbase, NULL, mss_data[cards].osp)==0)
		return 0;
		
	/*
	 *	Initialize it
	 */
	 
	mss_data[cards].slots[3] = ad1848_init("MyCard MSS 16bit", 
			mssbase,
			mss_data[cards].irq,
			mss_data[cards].dma,
			mss_data[cards].dma,
			0,
			0,
			THIS_MODULE);

	cards++;	
	return 1;
}


/*
 * 	This loop walks the PCI configuration database and finds where
 *	the sound cards are.
 */
 
int init_mycard(void)
{
	struct pci_dev *pcidev=NULL;
	int count=0;
		
	while((pcidev = pci_find_device(PCI_VENDOR_MYIDENT, PCI_DEVICE_ID_MYIDENT_MYCARD1, pcidev))!=NULL)
	{
		if (pci_enable_device(pcidev))
			continue;
		count+=mycard_install(pcidev);
		if(count)
			return 0;
		if(count==MAX_CARDS)
			break;
	}
	
	if(count==0)
		return -ENODEV;
	return 0;
}

/*
 *	This function is called when the user or kernel loads the 
 *	module into memory.
 */


int init_module(void)
{
	if(init_mycard()<0)
	{
		printk(KERN_ERR "No "CARD_NAME" cards found.\n");
		return -ENODEV;
	}

	return 0;
}

/*
 *	This is called when it is removed. It will only be removed 
 *	when its use count is 0.
 */
 
void cleanup_module(void)
{
	for(i=0;i< cards; i++)
	{
		/*
		 *	Free attached resources
		 */
		 
		ad1848_unload(mss_data[i].io_base,
			      mss_data[i].irq,
			      mss_data[i].dma,
			      mss_data[i].dma,
			      0);
		/*
		 *	And disconnect the device from the kernel
		 */
		sound_unload_audiodevice(mss_data[i].slots[3]);
	}
}

