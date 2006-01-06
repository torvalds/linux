/*
 * linux/include/asm/qdio.h
 *
 * Linux for S/390 QDIO base support, Hipersocket base support
 * version 2
 *
 * Copyright 2000,2002 IBM Corporation
 * Author(s): Utz Bacher <utz.bacher@de.ibm.com>
 *
 */
#ifndef __QDIO_H__
#define __QDIO_H__

#define VERSION_QDIO_H "$Revision: 1.57 $"

/* note, that most of the typedef's are from ingo. */

#include <linux/interrupt.h>
#include <asm/cio.h>
#include <asm/ccwdev.h>

#define QDIO_NAME "qdio "

#ifndef __s390x__
#define QDIO_32_BIT
#endif /* __s390x__ */

/**** CONSTANTS, that are relied on without using these symbols *****/
#define QDIO_MAX_QUEUES_PER_IRQ 32 /* used in width of unsigned int */
/************************ END of CONSTANTS **************************/
#define QDIO_MAX_BUFFERS_PER_Q 128 /* must be a power of 2 (%x=&(x-1)*/
#define QDIO_BUF_ORDER 7 /* 2**this == number of pages used for sbals in 1 q */
#define QDIO_MAX_ELEMENTS_PER_BUFFER 16
#define SBAL_SIZE 256

#define QDIO_QETH_QFMT 0
#define QDIO_ZFCP_QFMT 1
#define QDIO_IQDIO_QFMT 2

struct qdio_buffer_element{
	unsigned int flags;
	unsigned int length;
#ifdef QDIO_32_BIT
	void *reserved;
#endif /* QDIO_32_BIT */
	void *addr;
} __attribute__ ((packed,aligned(16)));

struct qdio_buffer{
	volatile struct qdio_buffer_element element[16];
} __attribute__ ((packed,aligned(256)));


/* params are: ccw_device, status, qdio_error, siga_error,
   queue_number, first element processed, number of elements processed,
   int_parm */
typedef void qdio_handler_t(struct ccw_device *,unsigned int,unsigned int,
			    unsigned int,unsigned int,int,int,unsigned long);


#define QDIO_STATUS_INBOUND_INT 0x01
#define QDIO_STATUS_OUTBOUND_INT 0x02
#define QDIO_STATUS_LOOK_FOR_ERROR 0x04
#define QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR 0x08
#define QDIO_STATUS_MORE_THAN_ONE_SIGA_ERROR 0x10
#define QDIO_STATUS_ACTIVATE_CHECK_CONDITION 0x20

#define QDIO_SIGA_ERROR_ACCESS_EXCEPTION 0x10
#define QDIO_SIGA_ERROR_B_BIT_SET 0x20

/* for qdio_initialize */
#define QDIO_INBOUND_0COPY_SBALS 0x01
#define QDIO_OUTBOUND_0COPY_SBALS 0x02
#define QDIO_USE_OUTBOUND_PCIS 0x04

/* for qdio_cleanup */
#define QDIO_FLAG_CLEANUP_USING_CLEAR 0x01
#define QDIO_FLAG_CLEANUP_USING_HALT 0x02

struct qdio_initialize {
	struct ccw_device *cdev;
	unsigned char q_format;
	unsigned char adapter_name[8];
       	unsigned int qib_param_field_format; /*adapter dependent*/
	/* pointer to 128 bytes or NULL, if no param field */
	unsigned char *qib_param_field; /* adapter dependent */
	/* pointer to no_queues*128 words of data or NULL */
	unsigned long *input_slib_elements;
	unsigned long *output_slib_elements;
	unsigned int min_input_threshold;
	unsigned int max_input_threshold;
	unsigned int min_output_threshold;
	unsigned int max_output_threshold;
	unsigned int no_input_qs;
	unsigned int no_output_qs;
	qdio_handler_t *input_handler;
	qdio_handler_t *output_handler;
	unsigned long int_parm;
	unsigned long flags;
	void **input_sbal_addr_array; /* addr of n*128 void ptrs */
	void **output_sbal_addr_array; /* addr of n*128 void ptrs */
};

extern int qdio_initialize(struct qdio_initialize *init_data);
extern int qdio_allocate(struct qdio_initialize *init_data);
extern int qdio_establish(struct qdio_initialize *init_data);

extern int qdio_activate(struct ccw_device *,int flags);

#define QDIO_STATE_MUST_USE_OUTB_PCI	0x00000001
#define QDIO_STATE_INACTIVE 		0x00000002 /* after qdio_cleanup */
#define QDIO_STATE_ESTABLISHED 		0x00000004 /* after qdio_initialize */
#define QDIO_STATE_ACTIVE 		0x00000008 /* after qdio_activate */
#define QDIO_STATE_STOPPED 		0x00000010 /* after queues went down */
extern unsigned long qdio_get_status(int irq);


#define QDIO_FLAG_SYNC_INPUT     0x01
#define QDIO_FLAG_SYNC_OUTPUT    0x02
#define QDIO_FLAG_UNDER_INTERRUPT 0x04
#define QDIO_FLAG_NO_INPUT_INTERRUPT_CONTEXT 0x08 /* no effect on
						     adapter interrupts */
#define QDIO_FLAG_DONT_SIGA 0x10

extern int do_QDIO(struct ccw_device*, unsigned int flags, 
		   unsigned int queue_number,
		   unsigned int qidx,unsigned int count,
		   struct qdio_buffer *buffers);

extern int qdio_synchronize(struct ccw_device*, unsigned int flags,
			    unsigned int queue_number);

extern int qdio_cleanup(struct ccw_device*, int how);
extern int qdio_shutdown(struct ccw_device*, int how);
extern int qdio_free(struct ccw_device*);

unsigned char qdio_get_slsb_state(struct ccw_device*, unsigned int flag,
				  unsigned int queue_number,
				  unsigned int qidx);

extern void qdio_init_scrubber(void);

struct qdesfmt0 {
#ifdef QDIO_32_BIT
	unsigned long res1;             /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long sliba;            /* storage-list-information-block
					   address */
#ifdef QDIO_32_BIT
	unsigned long res2;             /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long sla;              /* storage-list address */
#ifdef QDIO_32_BIT
	unsigned long res3;             /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long slsba;            /* storage-list-state-block address */
	unsigned int  res4;		/* reserved */
	unsigned int  akey  :  4;       /* access key for DLIB */
	unsigned int  bkey  :  4;       /* access key for SL */
	unsigned int  ckey  :  4;       /* access key for SBALs */
	unsigned int  dkey  :  4;       /* access key for SLSB */
	unsigned int  res5  : 16;       /* reserved */
} __attribute__ ((packed));

/*
 * Queue-Description record (QDR)
 */
struct qdr {
	unsigned int  qfmt    :  8;     /* queue format */
	unsigned int  pfmt    :  8;     /* impl. dep. parameter format */
	unsigned int  res1    :  8;     /* reserved */
	unsigned int  ac      :  8;     /* adapter characteristics */
	unsigned int  res2    :  8;     /* reserved */
	unsigned int  iqdcnt  :  8;     /* input-queue-descriptor count */
	unsigned int  res3    :  8;     /* reserved */
	unsigned int  oqdcnt  :  8;     /* output-queue-descriptor count */
	unsigned int  res4    :  8;     /* reserved */
	unsigned int  iqdsz   :  8;     /* input-queue-descriptor size */
	unsigned int  res5    :  8;     /* reserved */
	unsigned int  oqdsz   :  8;     /* output-queue-descriptor size */
	unsigned int  res6[9];          /* reserved */
#ifdef QDIO_32_BIT
	unsigned long res7;		/* reserved */
#endif /* QDIO_32_BIT */
	unsigned long qiba;             /* queue-information-block address */
	unsigned int  res8;             /* reserved */
	unsigned int  qkey    :  4;     /* queue-informatio-block key */
	unsigned int  res9    : 28;     /* reserved */
/*	union _qd {*/ /* why this? */
		struct qdesfmt0 qdf0[126];
/*	} qd;*/
} __attribute__ ((packed,aligned(4096)));


/*
 * queue information block (QIB)
 */
#define QIB_AC_INBOUND_PCI_SUPPORTED 	0x80
#define QIB_AC_OUTBOUND_PCI_SUPPORTED 	0x40
#define QIB_RFLAGS_ENABLE_QEBSM		0x80

struct qib {
	unsigned int  qfmt    :  8;     /* queue format */
	unsigned int  pfmt    :  8;     /* impl. dep. parameter format */
	unsigned int  rflags  :  8;	/* QEBSM */
	unsigned int  ac      :  8;     /* adapter characteristics */
	unsigned int  res2;             /* reserved */
#ifdef QDIO_32_BIT
	unsigned long res3;             /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long isliba;           /* absolute address of 1st
					   input SLIB */
#ifdef QDIO_32_BIT
	unsigned long res4;             /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long osliba;           /* absolute address of 1st
					   output SLIB */
	unsigned int  res5;             /* reserved */
	unsigned int  res6;             /* reserved */
	unsigned char ebcnam[8];        /* adapter identifier in EBCDIC */
	unsigned char res7[88];         /* reserved */
	unsigned char parm[QDIO_MAX_BUFFERS_PER_Q];
					/* implementation dependent
					   parameters */
} __attribute__ ((packed,aligned(256)));


/*
 * storage-list-information block element (SLIBE)
 */
struct slibe {
#ifdef QDIO_32_BIT
	unsigned long res;              /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long parms;            /* implementation dependent
					   parameters */
};

/*
 * storage-list-information block (SLIB)
 */
struct slib {
#ifdef QDIO_32_BIT
	unsigned long res1;             /* reserved */
#endif /* QDIO_32_BIT */
        unsigned long nsliba;           /* next SLIB address (if any) */
#ifdef QDIO_32_BIT
	unsigned long res2;             /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long sla;              /* SL address */
#ifdef QDIO_32_BIT
	unsigned long res3;             /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long slsba;            /* SLSB address */
	unsigned char res4[1000];       /* reserved */
	struct slibe slibe[QDIO_MAX_BUFFERS_PER_Q];    /* SLIB elements */
} __attribute__ ((packed,aligned(2048)));

struct sbal_flags {
	unsigned char res1  : 1;   /* reserved */
	unsigned char last  : 1;   /* last entry */
	unsigned char cont  : 1;   /* contiguous storage */
	unsigned char res2  : 1;   /* reserved */
	unsigned char frag  : 2;   /* fragmentation (s.below) */
	unsigned char res3  : 2;   /* reserved */
} __attribute__ ((packed));

#define SBAL_FLAGS_FIRST_FRAG	     0x04000000UL
#define SBAL_FLAGS_MIDDLE_FRAG	     0x08000000UL
#define SBAL_FLAGS_LAST_FRAG	     0x0c000000UL
#define SBAL_FLAGS_LAST_ENTRY	     0x40000000UL
#define SBAL_FLAGS_CONTIGUOUS	     0x20000000UL

#define SBAL_FLAGS0_DATA_CONTINUATION 0x20UL

/* Awesome OpenFCP extensions */
#define SBAL_FLAGS0_TYPE_STATUS       0x00UL
#define SBAL_FLAGS0_TYPE_WRITE        0x08UL
#define SBAL_FLAGS0_TYPE_READ         0x10UL
#define SBAL_FLAGS0_TYPE_WRITE_READ   0x18UL
#define SBAL_FLAGS0_MORE_SBALS	      0x04UL
#define SBAL_FLAGS0_COMMAND           0x02UL
#define SBAL_FLAGS0_LAST_SBAL         0x00UL
#define SBAL_FLAGS0_ONLY_SBAL         SBAL_FLAGS0_COMMAND
#define SBAL_FLAGS0_MIDDLE_SBAL       SBAL_FLAGS0_MORE_SBALS
#define SBAL_FLAGS0_FIRST_SBAL        SBAL_FLAGS0_MORE_SBALS | SBAL_FLAGS0_COMMAND
/* Naught of interest beyond this point */

#define SBAL_FLAGS0_PCI		0x40
struct sbal_sbalf_0 {
	unsigned char res1  : 1;   /* reserved */
	unsigned char pci   : 1;   /* PCI indicator */
	unsigned char cont  : 1;   /* data continuation */
	unsigned char sbtype: 2;   /* storage-block type (OpenFCP) */
	unsigned char res2  : 3;   /* reserved */
} __attribute__ ((packed));

struct sbal_sbalf_1 {
	unsigned char res1  : 4;   /* reserved */
	unsigned char key   : 4;   /* storage key */
} __attribute__ ((packed));

struct sbal_sbalf_14 {
	unsigned char res1   : 4;  /* reserved */
	unsigned char erridx : 4;  /* error index */
} __attribute__ ((packed));

struct sbal_sbalf_15 {
	unsigned char reason;      /* reserved */
} __attribute__ ((packed));

union sbal_sbalf {
	struct sbal_sbalf_0  i0;
	struct sbal_sbalf_1  i1;
	struct sbal_sbalf_14 i14;
	struct sbal_sbalf_15 i15;
	unsigned char value;
};

struct sbal_element {
	union {
		struct sbal_flags  bits;       /* flags */
		unsigned char value;
	} flags;
	unsigned int  res1  : 16;   /* reserved */
	union sbal_sbalf  sbalf;       /* SBAL flags */
	unsigned int  res2  : 16;  /* reserved */
	unsigned int  count : 16;  /* data count */
#ifdef QDIO_32_BIT
	unsigned long res3;        /* reserved */
#endif /* QDIO_32_BIT */
	unsigned long addr;        /* absolute data address */
} __attribute__ ((packed,aligned(16)));

/*
 * strorage-block access-list (SBAL)
 */
struct sbal {
	struct sbal_element element[QDIO_MAX_ELEMENTS_PER_BUFFER];
} __attribute__ ((packed,aligned(256)));

/*
 * storage-list (SL)
 */
struct sl_element {
#ifdef QDIO_32_BIT
        unsigned long res;     /* reserved */
#endif /* QDIO_32_BIT */
        unsigned long sbal;    /* absolute SBAL address */
} __attribute__ ((packed));

struct sl {
	struct sl_element element[QDIO_MAX_BUFFERS_PER_Q];
} __attribute__ ((packed,aligned(1024)));

/*
 * storage-list-state block (SLSB)
 */
struct slsb_flags {
	unsigned char owner  : 2;   /* SBAL owner */
	unsigned char type   : 1;   /* buffer type */
	unsigned char state  : 5;   /* processing state */
} __attribute__ ((packed));


struct slsb {
	union {
		unsigned char val[QDIO_MAX_BUFFERS_PER_Q];
		struct slsb_flags flags[QDIO_MAX_BUFFERS_PER_Q];
	} acc;
} __attribute__ ((packed,aligned(256)));

/*
 * SLSB values
 */
#define SLSB_OWNER_PROG              1
#define SLSB_OWNER_CU                2

#define SLSB_TYPE_INPUT              0
#define SLSB_TYPE_OUTPUT             1

#define SLSB_STATE_NOT_INIT          0
#define SLSB_STATE_EMPTY             1
#define SLSB_STATE_PRIMED            2
#define SLSB_STATE_HALTED          0xe
#define SLSB_STATE_ERROR           0xf

#define SLSB_P_INPUT_NOT_INIT     0x80
#define SLSB_P_INPUT_PROCESSING	  0x81
#define SLSB_CU_INPUT_EMPTY       0x41
#define SLSB_P_INPUT_PRIMED       0x82
#define SLSB_P_INPUT_HALTED       0x8E
#define SLSB_P_INPUT_ERROR        0x8F

#define SLSB_P_OUTPUT_NOT_INIT    0xA0
#define SLSB_P_OUTPUT_EMPTY       0xA1
#define SLSB_CU_OUTPUT_PRIMED     0x62
#define SLSB_P_OUTPUT_HALTED      0xAE
#define SLSB_P_OUTPUT_ERROR       0xAF

#define SLSB_ERROR_DURING_LOOKUP  0xFF

#endif /* __QDIO_H__ */
