/*	$OpenBSD: pic.h,v 1.2 2014/12/16 21:40:05 tedu Exp $	*/
/*	$NetBSD: pic.h,v 1.1 2003/02/26 21:26:11 fvdl Exp $	*/

#ifndef _X86_PIC_H
#define _X86_PIC_H

#include <sys/device.h>
#include <sys/mutex.h>

struct cpu_info;

/* 
 * Structure common to all PIC softcs
 */
struct pic {
	struct device pic_dev;
        int pic_type;
#ifdef MULTIPROCESSOR
	struct mutex pic_mutex;
#endif
        void (*pic_hwmask)(struct pic *, int);
        void (*pic_hwunmask)(struct pic *, int);
	void (*pic_addroute)(struct pic *, struct cpu_info *, int, int, int);
	void (*pic_delroute)(struct pic *, struct cpu_info *, int, int, int);
	struct intrstub *pic_level_stubs;
	struct intrstub *pic_edge_stubs;
};

#define pic_name pic_dev.dv_xname

/*
 * PIC types.
 */
#define PIC_I8259	0
#define PIC_IOAPIC	1
#define PIC_LAPIC	2
#define PIC_SOFT	3

extern struct pic i8259_pic;
extern struct pic local_pic;
extern struct pic softintr_pic;
#endif
