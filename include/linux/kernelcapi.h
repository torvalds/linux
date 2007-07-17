/*
 * $Id: kernelcapi.h,v 1.8.6.2 2001/02/07 11:31:31 kai Exp $
 * 
 * Kernel CAPI 2.0 Interface for Linux
 * 
 * (c) Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 */

#ifndef __KERNELCAPI_H__
#define __KERNELCAPI_H__

#define CAPI_MAXAPPL	240	/* maximum number of applications  */
#define CAPI_MAXCONTR	32	/* maximum number of controller    */
#define CAPI_MAXDATAWINDOW	8


typedef struct kcapi_flagdef {
	int contr;
	int flag;
} kcapi_flagdef;

typedef struct kcapi_carddef {
	char		driver[32];
	unsigned int	port;
	unsigned	irq;
	unsigned int	membase;
	int		cardnr;
} kcapi_carddef;

/* new ioctls >= 10 */
#define KCAPI_CMD_TRACE		10
#define KCAPI_CMD_ADDCARD	11	/* OBSOLETE */

/* 
 * flag > 2 => trace also data
 * flag & 1 => show trace
 */
#define KCAPI_TRACE_OFF			0
#define KCAPI_TRACE_SHORT_NO_DATA	1
#define KCAPI_TRACE_FULL_NO_DATA	2
#define KCAPI_TRACE_SHORT		3
#define KCAPI_TRACE_FULL		4


#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <asm/semaphore.h>

#define	KCI_CONTRUP	0	/* arg: struct capi_profile */
#define	KCI_CONTRDOWN	1	/* arg: NULL */

struct capi20_appl {
	u16 applid;
	capi_register_params rparam;
	void (*recv_message)(struct capi20_appl *ap, struct sk_buff *skb);
	void *private;

	/* internal to kernelcapi.o */
	unsigned long nrecvctlpkt;
	unsigned long nrecvdatapkt;
	unsigned long nsentctlpkt;
	unsigned long nsentdatapkt;
	struct mutex recv_mtx;
	struct sk_buff_head recv_queue;
	struct work_struct recv_work;
	int release_in_progress;

	/* ugly hack to allow for notification of added/removed
	 * controllers. The Right Way (tm) is known. XXX
	 */
	void (*callback) (unsigned int cmd, __u32 contr, void *data);
};

u16 capi20_isinstalled(void);
u16 capi20_register(struct capi20_appl *ap);
u16 capi20_release(struct capi20_appl *ap);
u16 capi20_put_message(struct capi20_appl *ap, struct sk_buff *skb);
u16 capi20_get_manufacturer(u32 contr, u8 buf[CAPI_MANUFACTURER_LEN]);
u16 capi20_get_version(u32 contr, struct capi_version *verp);
u16 capi20_get_serial(u32 contr, u8 serial[CAPI_SERIAL_LEN]);
u16 capi20_get_profile(u32 contr, struct capi_profile *profp);
int capi20_manufacturer(unsigned int cmd, void __user *data);

/* temporary hack XXX */
void capi20_set_callback(struct capi20_appl *ap, 
			 void (*callback) (unsigned int cmd, __u32 contr, void *data));



#define CAPI_NOERROR                      0x0000

#define CAPI_TOOMANYAPPLS		  0x1001
#define CAPI_LOGBLKSIZETOSMALL	          0x1002
#define CAPI_BUFFEXECEEDS64K 	          0x1003
#define CAPI_MSGBUFSIZETOOSMALL	          0x1004
#define CAPI_ANZLOGCONNNOTSUPPORTED	  0x1005
#define CAPI_REGRESERVED		  0x1006
#define CAPI_REGBUSY 		          0x1007
#define CAPI_REGOSRESOURCEERR	          0x1008
#define CAPI_REGNOTINSTALLED 	          0x1009
#define CAPI_REGCTRLERNOTSUPPORTEXTEQUIP  0x100a
#define CAPI_REGCTRLERONLYSUPPORTEXTEQUIP 0x100b

#define CAPI_ILLAPPNR		          0x1101
#define CAPI_ILLCMDORSUBCMDORMSGTOSMALL   0x1102
#define CAPI_SENDQUEUEFULL		  0x1103
#define CAPI_RECEIVEQUEUEEMPTY	          0x1104
#define CAPI_RECEIVEOVERFLOW 	          0x1105
#define CAPI_UNKNOWNNOTPAR		  0x1106
#define CAPI_MSGBUSY 		          0x1107
#define CAPI_MSGOSRESOURCEERR	          0x1108
#define CAPI_MSGNOTINSTALLED 	          0x1109
#define CAPI_MSGCTRLERNOTSUPPORTEXTEQUIP  0x110a
#define CAPI_MSGCTRLERONLYSUPPORTEXTEQUIP 0x110b

typedef enum {
        CapiMessageNotSupportedInCurrentState = 0x2001,
        CapiIllContrPlciNcci                  = 0x2002,
        CapiNoPlciAvailable                   = 0x2003,
        CapiNoNcciAvailable                   = 0x2004,
        CapiNoListenResourcesAvailable        = 0x2005,
        CapiNoFaxResourcesAvailable           = 0x2006,
        CapiIllMessageParmCoding              = 0x2007,
} RESOURCE_CODING_PROBLEM;

typedef enum {
        CapiB1ProtocolNotSupported                      = 0x3001,
        CapiB2ProtocolNotSupported                      = 0x3002,
        CapiB3ProtocolNotSupported                      = 0x3003,
        CapiB1ProtocolParameterNotSupported             = 0x3004,
        CapiB2ProtocolParameterNotSupported             = 0x3005,
        CapiB3ProtocolParameterNotSupported             = 0x3006,
        CapiBProtocolCombinationNotSupported            = 0x3007,
        CapiNcpiNotSupported                            = 0x3008,
        CapiCipValueUnknown                             = 0x3009,
        CapiFlagsNotSupported                           = 0x300a,
        CapiFacilityNotSupported                        = 0x300b,
        CapiDataLengthNotSupportedByCurrentProtocol     = 0x300c,
        CapiResetProcedureNotSupportedByCurrentProtocol = 0x300d,
        CapiTeiAssignmentFailed                         = 0x300e,
} REQUESTED_SERVICES_PROBLEM;

typedef enum {
	CapiSuccess                                     = 0x0000,
	CapiSupplementaryServiceNotSupported            = 0x300e,
	CapiRequestNotAllowedInThisState                = 0x3010,
} SUPPLEMENTARY_SERVICE_INFO;

typedef enum {
	CapiProtocolErrorLayer1                         = 0x3301,
	CapiProtocolErrorLayer2                         = 0x3302,
	CapiProtocolErrorLayer3                         = 0x3303,
	CapiTimeOut                                     = 0x3303, // SuppServiceReason
	CapiCallGivenToOtherApplication                 = 0x3304,
} CAPI_REASON;

#endif				/* __KERNEL__ */

#endif				/* __KERNELCAPI_H__ */
