#ifndef __linux_video_edid_h__
#define __linux_video_edid_h__

#ifdef __KERNEL__

#include <linux/config.h>
#ifdef CONFIG_PPC_OF
#include <linux/pci.h>
#endif

#ifdef CONFIG_X86
struct edid_info {
	unsigned char dummy[128];
};

extern struct edid_info edid_info;
extern char *get_EDID_from_BIOS(void *);

#endif /* CONFIG_X86 */

#ifdef CONFIG_PPC_OF
extern char *get_EDID_from_OF(struct pci_dev *pdev);
#endif

#endif /* __KERNEL__ */

#endif /* __linux_video_edid_h__ */
