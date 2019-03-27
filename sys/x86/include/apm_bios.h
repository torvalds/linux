/*-
 * APM (Advanced Power Management) BIOS Device Driver
 *
 * Copyright (c) 1994-1995 by HOSOKAWA, Tatsumi <hosokawa@mt.cs.keio.ac.jp>
 *
 * This software may be used, modified, copied, and distributed, in
 * both source and binary form provided that the above copyright and
 * these terms are retained. Under no circumstances is the author
 * responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its
 * use.
 *
 * Aug, 1994	Implemented on FreeBSD 1.1.5.1R (Toshiba AVS001WD)
 *
 * $FreeBSD$
 */

#ifndef _X86_APM_BIOS_H_
#define	_X86_APM_BIOS_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

/* BIOS id */
#define APM_BIOS		0x53
#define APM_INT			0x15

/* APM flags */
#define APM_16BIT_SUPPORT	0x01
#define APM_32BIT_SUPPORT	0x02
#define APM_CPUIDLE_SLOW	0x04
#define APM_DISABLED		0x08
#define APM_DISENGAGED		0x10

/* APM initializer physical address */
#define APM_OURADDR		0x00080000

/* APM functions */
#define APM_INSTCHECK		0x00
#define APM_REALCONNECT		0x01
#define APM_PROT16CONNECT	0x02
#define APM_PROT32CONNECT	0x03
#define APM_DISCONNECT		0x04
#define APM_CPUIDLE		0x05
#define APM_CPUBUSY		0x06
#define APM_SETPWSTATE		0x07
#define APM_ENABLEDISABLEPM	0x08
#define APM_RESTOREDEFAULT	0x09
#define	APM_GETPWSTATUS		0x0a
#define APM_GETPMEVENT		0x0b
#define APM_GETPWSTATE		0x0c
#define APM_ENABLEDISABLEDPM	0x0d
#define APM_DRVVERSION		0x0e
#define APM_ENGAGEDISENGAGEPM	0x0f
#define APM_GETCAPABILITIES	0x10
#define APM_RESUMETIMER		0x11
#define APM_RESUMEONRING	0x12
#define APM_TIMERREQUESTS	0x13
#define APM_OEMFUNC		0x80

/* error code */
#define APME_OK			0x00
#define APME_PMDISABLED		0x01
#define APME_REALESTABLISHED	0x02
#define APME_NOTCONNECTED	0x03
#define APME_PROT16ESTABLISHED	0x05
#define APME_PROT16NOTSUPPORTED	0x06
#define APME_PROT32ESTABLISHED	0x07
#define APME_PROT32NOTDUPPORTED	0x08
#define APME_UNKNOWNDEVICEID	0x09
#define APME_OUTOFRANGE		0x0a
#define APME_NOTENGAGED		0x0b
#define APME_CANTENTERSTATE	0x60
#define APME_NOPMEVENT		0x80
#define APME_NOAPMPRESENT	0x86


/* device code */
#define PMDV_APMBIOS		0x0000
#define PMDV_ALLDEV		0x0001
#define PMDV_DISP0		0x0100
#define PMDV_DISP1		0x0101
#define PMDV_DISPALL		0x01ff
#define PMDV_2NDSTORAGE0	0x0200
#define PMDV_2NDSTORAGE1	0x0201
#define PMDV_2NDSTORAGE2	0x0202
#define PMDV_2NDSTORAGE3	0x0203
#define PMDV_PARALLEL0		0x0300
#define PMDV_PARALLEL1		0x0301
#define PMDV_SERIAL0		0x0400
#define PMDV_SERIAL1		0x0401
#define PMDV_SERIAL2		0x0402
#define PMDV_SERIAL3		0x0403
#define PMDV_SERIAL4		0x0404
#define PMDV_SERIAL5		0x0405
#define PMDV_SERIAL6		0x0406
#define PMDV_SERIAL7		0x0407
#define PMDV_NET0		0x0500
#define PMDV_NET1		0x0501
#define PMDV_NET2		0x0502
#define PMDV_NET3		0x0503
#define PMDV_PCMCIA0		0x0600
#define PMDV_PCMCIA1		0x0601
#define PMDV_PCMCIA2		0x0602
#define PMDV_PCMCIA3		0x0603
/* 0x0700 - 0x7fff	Reserved			*/
#define PMDV_BATT_BASE		0x8000
#define PMDV_BATT0		0x8001
#define PMDV_BATT1		0x8002
#define PMDV_BATT_ALL		0x80ff
/* 0x8100 - 0xdfff	Reserved			*/
/* 0xe000 - 0xefff	OEM-defined power device IDs	*/
/* 0xf000 - 0xffff	Reserved			*/

/* Power state */
#define PMST_APMENABLED		0x0000
#define PMST_STANDBY		0x0001
#define PMST_SUSPEND		0x0002
#define PMST_OFF		0x0003
#define PMST_LASTREQNOTIFY	0x0004
#define PMST_LASTREQREJECT	0x0005
/* 0x0006 - 0x001f	Reserved system states		*/
/* 0x0020 - 0x003f	OEM-defined system states	*/
/* 0x0040 - 0x007f	OEM-defined device states	*/
/* 0x0080 - 0xffff	Reserved device states		*/

#if !defined(ASSEMBLER) && !defined(INITIALIZER)

/* C definitions */
struct apmhook {
	struct apmhook	*ah_next;
	int		(*ah_fun)(void *ah_arg);
	void		*ah_arg;
	const char	*ah_name;
	int		ah_order;
};
#define APM_HOOK_NONE		(-1)
#define APM_HOOK_SUSPEND        0
#define APM_HOOK_RESUME         1
#define NAPM_HOOK               2

#ifdef _KERNEL

void apm_suspend(int state);
struct apmhook *apm_hook_establish (int apmh, struct apmhook *);
void apm_hook_disestablish (int apmh, struct apmhook *);
void apm_cpu_idle(void);
void apm_cpu_busy(void);

#endif

#endif /* !ASSEMBLER && !INITIALIZER */

#define APM_MIN_ORDER		0x00
#define APM_MID_ORDER		0x80
#define APM_MAX_ORDER		0xff

/* power management event code */
#define PMEV_NOEVENT		0x0000
#define PMEV_STANDBYREQ		0x0001
#define PMEV_SUSPENDREQ		0x0002
#define PMEV_NORMRESUME		0x0003
#define PMEV_CRITRESUME		0x0004
#define PMEV_BATTERYLOW		0x0005
#define PMEV_POWERSTATECHANGE	0x0006
#define PMEV_UPDATETIME		0x0007
#define PMEV_CRITSUSPEND	0x0008
#define PMEV_USERSTANDBYREQ	0x0009
#define PMEV_USERSUSPENDREQ	0x000a
#define PMEV_STANDBYRESUME	0x000b
#define PMEV_CAPABILITIESCHANGE 0x000c
/* 0x000d - 0x00ff	Reserved system events	*/
/* 0x0100 - 0x01ff	Reserved device events	*/
/* 0x0200 - 0x02ff	OEM-defined APM events	*/
/* 0x0300 - 0xffff	Reserved		*/
#define PMEV_DEFAULT		0xffffffff	/* used for customization */

#if !defined(ASSEMBLER) && !defined(INITIALIZER)

/*
 * Old apm_info structure, returned by the APMIO_GETINFO_OLD ioctl.  This
 * is for backward compatibility with old executables.
 */
typedef struct apm_info_old {
	u_int	ai_major;	/* APM major version */
	u_int	ai_minor;	/* APM minor version */
	u_int	ai_acline;	/* AC line status */
	u_int	ai_batt_stat;	/* Battery status */
	u_int	ai_batt_life;	/* Remaining battery life */
	u_int	ai_status;	/* Status of APM support (enabled/disabled) */
} *apm_info_old_t;

/*
 * Structure returned by the APMIO_GETINFO ioctl.
 *
 * In the comments below, the parenthesized numbers indicate the minimum
 * value of ai_infoversion for which each field is valid.
 */
typedef struct apm_info {
	u_int	ai_infoversion;	/* Indicates which fields are valid */
	u_int	ai_major;	/* APM major version (0) */
	u_int	ai_minor;	/* APM minor version (0) */
	u_int	ai_acline;	/* AC line status (0) */
	u_int	ai_batt_stat;	/* Battery status (0) */
	u_int	ai_batt_life;	/* Remaining battery life in percent (0) */
	int	ai_batt_time;	/* Remaining battery time in seconds (0) */
	u_int	ai_status;	/* True if enabled (0) */
	u_int	ai_batteries;	/* Number of batteries (1) */
	u_int	ai_capabilities;/* APM Capabilities (1) */
	u_int	ai_spare[6];	/* For future expansion */
} *apm_info_t;

/* Battery flag */
#define APM_BATT_HIGH		0x01
#define APM_BATT_LOW		0x02
#define APM_BATT_CRITICAL	0x04
#define APM_BATT_CHARGING	0x08
#define APM_BATT_NOT_PRESENT	0x10
#define APM_BATT_NO_SYSTEM	0x80

typedef struct apm_pwstatus {
	u_int	ap_device;	/* Device code of battery */
	u_int	ap_acline;	/* AC line status (0) */
	u_int	ap_batt_stat;	/* Battery status (0) */
	u_int	ap_batt_flag;	/* Battery flag (0) */
	u_int	ap_batt_life;	/* Remaining battery life in percent (0) */
	int	ap_batt_time;	/* Remaining battery time in seconds (0) */
} *apm_pwstatus_t;

struct apm_bios_arg {
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;
        uint32_t esi;
        uint32_t edi;
};

struct apm_event_info {
	u_int type;
	u_int index;
	u_int spare[8];
};

#define APMIO_SUSPEND		_IO('P', 1)
#define APMIO_GETINFO_OLD	_IOR('P', 2, struct apm_info_old)
#define APMIO_ENABLE		_IO('P', 5)
#define APMIO_DISABLE		_IO('P', 6)
#define APMIO_HALTCPU		_IO('P', 7)
#define APMIO_NOTHALTCPU	_IO('P', 8)
#define APMIO_DISPLAY		_IOW('P', 9, int)
#define APMIO_BIOS		_IOWR('P', 10, struct apm_bios_arg)
#define APMIO_GETINFO		_IOR('P', 11, struct apm_info)
#define APMIO_STANDBY		_IO('P', 12)
#define APMIO_GETPWSTATUS	_IOWR('P', 13, struct apm_pwstatus)
/* for /dev/apmctl */
#define APMIO_NEXTEVENT		_IOR('A', 100, struct apm_event_info)
#define APMIO_REJECTLASTREQ	_IO('P', 101)

#endif /* !ASSEMBLER && !INITIALIZER */

#endif /* !_X86_APM_BIOS_H_ */
