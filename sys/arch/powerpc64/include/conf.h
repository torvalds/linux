#include <sys/conf.h>

#define mmread	mmrw
#define mmwrite	mmrw
cdev_decl(mm);

cdev_decl(opalcons);

/* open, close, ioctl */
#define cdev_kexec_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) nullop, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, ioctl */
#define cdev_openprom_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) nullop, 0, \
	(dev_type_mmap((*))) enodev }

cdev_decl(kexec);
cdev_decl(openprom);
