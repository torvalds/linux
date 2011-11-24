/* $Id: nano_pci_var.h 11215 2009-02-27 14:25:03Z joda $ */

#ifndef __nano_pci_var_h__
#define __nano_pci_var_h__

#ifndef __KERNEL__
#include <sys/ioctl.h>
#endif

struct nanopci_status {
   unsigned int irq_count;
   unsigned int irq_handled;

   unsigned int tx_queue;

   unsigned int booting;
};

#define NANOPCI_IOCGSTATUS     _IOR('n', 1, struct nanopci_status)
//#define NANOPCI_IOCBOOT        _IO('n', 2)
#define NANOPCI_IOCSLEEP       _IO('n', 3)
#define NANOPCI_IOCWAKEUP      _IO('n', 4)
#define NANOPCI_IOCGATTN       _IOR('n', 5, int)

#define NANOPCI_IOCRESET       _IO('n', 6)
#define NANOPCI_IOCSDIO        _IO('n', 7)
#define NANOPCI_IOCBOOTENABLE  _IO('n', 8)
#define NANOPCI_IOCBOOTDISABLE _IO('n', 9)
#define NANOPCI_IOCFLUSH       _IO('n', 10)

#endif /* __nano_pci_var_h__ */
