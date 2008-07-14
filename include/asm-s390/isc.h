#ifndef _ASM_S390_ISC_H
#define _ASM_S390_ISC_H

/*
 * I/O interruption subclasses used by drivers.
 * Please add all used iscs here so that it is possible to distribute
 * isc usage between drivers.
 * Reminder: 0 is highest priority, 7 lowest.
 */
#define MAX_ISC 7

/* Regular I/O interrupts. */
#define IO_SCH_ISC 3			/* regular I/O subchannels */
#define CONSOLE_ISC 1			/* console I/O subchannel */
/* Adapter interrupts. */
#define QDIO_AIRQ_ISC IO_SCH_ISC	/* I/O subchannel in qdio mode */

#endif /* _ASM_S390_ISC_H */
