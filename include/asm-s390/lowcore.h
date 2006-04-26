/*
 *  include/asm-s390/lowcore.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */

#ifndef _ASM_S390_LOWCORE_H
#define _ASM_S390_LOWCORE_H

#ifndef __s390x__
#define __LC_EXT_OLD_PSW                0x018
#define __LC_SVC_OLD_PSW                0x020
#define __LC_PGM_OLD_PSW                0x028
#define __LC_MCK_OLD_PSW                0x030
#define __LC_IO_OLD_PSW                 0x038
#define __LC_EXT_NEW_PSW                0x058
#define __LC_SVC_NEW_PSW                0x060
#define __LC_PGM_NEW_PSW                0x068
#define __LC_MCK_NEW_PSW                0x070
#define __LC_IO_NEW_PSW                 0x078
#else /* !__s390x__ */
#define __LC_EXT_OLD_PSW                0x0130
#define __LC_SVC_OLD_PSW                0x0140
#define __LC_PGM_OLD_PSW                0x0150
#define __LC_MCK_OLD_PSW                0x0160
#define __LC_IO_OLD_PSW                 0x0170
#define __LC_EXT_NEW_PSW                0x01b0
#define __LC_SVC_NEW_PSW                0x01c0
#define __LC_PGM_NEW_PSW                0x01d0
#define __LC_MCK_NEW_PSW                0x01e0
#define __LC_IO_NEW_PSW                 0x01f0
#endif /* !__s390x__ */

#define __LC_EXT_PARAMS                 0x080
#define __LC_CPU_ADDRESS                0x084
#define __LC_EXT_INT_CODE               0x086

#define __LC_SVC_ILC                    0x088
#define __LC_SVC_INT_CODE               0x08A
#define __LC_PGM_ILC                    0x08C
#define __LC_PGM_INT_CODE               0x08E

#define __LC_PER_ATMID			0x096
#define __LC_PER_ADDRESS		0x098
#define __LC_PER_ACCESS_ID		0x0A1

#define __LC_SUBCHANNEL_ID              0x0B8
#define __LC_SUBCHANNEL_NR              0x0BA
#define __LC_IO_INT_PARM                0x0BC
#define __LC_IO_INT_WORD                0x0C0
#define __LC_MCCK_CODE                  0x0E8

#define __LC_RETURN_PSW                 0x200

#define __LC_SAVE_AREA                  0xC00

#ifndef __s390x__
#define __LC_IRB			0x208
#define __LC_SYNC_ENTER_TIMER		0x248
#define __LC_ASYNC_ENTER_TIMER		0x250
#define __LC_EXIT_TIMER			0x258
#define __LC_LAST_UPDATE_TIMER		0x260
#define __LC_USER_TIMER			0x268
#define __LC_SYSTEM_TIMER		0x270
#define __LC_LAST_UPDATE_CLOCK		0x278
#define __LC_STEAL_CLOCK		0x280
#define __LC_RETURN_MCCK_PSW            0x288
#define __LC_KERNEL_STACK               0xC40
#define __LC_THREAD_INFO		0xC44
#define __LC_ASYNC_STACK                0xC48
#define __LC_KERNEL_ASCE		0xC4C
#define __LC_USER_ASCE			0xC50
#define __LC_PANIC_STACK                0xC54
#define __LC_CPUID                      0xC60
#define __LC_CPUADDR                    0xC68
#define __LC_IPLDEV                     0xC7C
#define __LC_JIFFY_TIMER		0xC80
#define __LC_CURRENT			0xC90
#define __LC_INT_CLOCK			0xC98
#else /* __s390x__ */
#define __LC_IRB			0x210
#define __LC_SYNC_ENTER_TIMER		0x250
#define __LC_ASYNC_ENTER_TIMER		0x258
#define __LC_EXIT_TIMER			0x260
#define __LC_LAST_UPDATE_TIMER		0x268
#define __LC_USER_TIMER			0x270
#define __LC_SYSTEM_TIMER		0x278
#define __LC_LAST_UPDATE_CLOCK		0x280
#define __LC_STEAL_CLOCK		0x288
#define __LC_RETURN_MCCK_PSW            0x290
#define __LC_KERNEL_STACK               0xD40
#define __LC_THREAD_INFO		0xD48
#define __LC_ASYNC_STACK                0xD50
#define __LC_KERNEL_ASCE		0xD58
#define __LC_USER_ASCE			0xD60
#define __LC_PANIC_STACK                0xD68
#define __LC_CPUID                      0xD90
#define __LC_CPUADDR                    0xD98
#define __LC_IPLDEV                     0xDB8
#define __LC_JIFFY_TIMER		0xDC0
#define __LC_CURRENT			0xDD8
#define __LC_INT_CLOCK			0xDE8
#endif /* __s390x__ */

#define __LC_PANIC_MAGIC                0xE00

#ifndef __s390x__
#define __LC_PFAULT_INTPARM             0x080
#define __LC_CPU_TIMER_SAVE_AREA        0x0D8
#define __LC_AREGS_SAVE_AREA            0x120
#define __LC_GPREGS_SAVE_AREA           0x180
#define __LC_CREGS_SAVE_AREA            0x1C0
#else /* __s390x__ */
#define __LC_PFAULT_INTPARM             0x11B8
#define __LC_GPREGS_SAVE_AREA           0x1280
#define __LC_CPU_TIMER_SAVE_AREA        0x1328
#define __LC_AREGS_SAVE_AREA            0x1340
#define __LC_CREGS_SAVE_AREA            0x1380
#endif /* __s390x__ */

#ifndef __ASSEMBLY__

#include <asm/processor.h>
#include <linux/types.h>
#include <asm/sigp.h>

void restart_int_handler(void);
void ext_int_handler(void);
void system_call(void);
void pgm_check_handler(void);
void mcck_int_handler(void);
void io_int_handler(void);

struct _lowcore
{
#ifndef __s390x__
        /* prefix area: defined by architecture */
	psw_t        restart_psw;              /* 0x000 */
	__u32        ccw2[4];                  /* 0x008 */
	psw_t        external_old_psw;         /* 0x018 */
	psw_t        svc_old_psw;              /* 0x020 */
	psw_t        program_old_psw;          /* 0x028 */
	psw_t        mcck_old_psw;             /* 0x030 */
	psw_t        io_old_psw;               /* 0x038 */
	__u8         pad1[0x58-0x40];          /* 0x040 */
	psw_t        external_new_psw;         /* 0x058 */
	psw_t        svc_new_psw;              /* 0x060 */
	psw_t        program_new_psw;          /* 0x068 */
	psw_t        mcck_new_psw;             /* 0x070 */
	psw_t        io_new_psw;               /* 0x078 */
	__u32        ext_params;               /* 0x080 */
	__u16        cpu_addr;                 /* 0x084 */
	__u16        ext_int_code;             /* 0x086 */
        __u16        svc_ilc;                  /* 0x088 */
        __u16        svc_code;                 /* 0x08a */
        __u16        pgm_ilc;                  /* 0x08c */
        __u16        pgm_code;                 /* 0x08e */
	__u32        trans_exc_code;           /* 0x090 */
	__u16        mon_class_num;            /* 0x094 */
	__u16        per_perc_atmid;           /* 0x096 */
	__u32        per_address;              /* 0x098 */
	__u32        monitor_code;             /* 0x09c */
	__u8         exc_access_id;            /* 0x0a0 */
	__u8         per_access_id;            /* 0x0a1 */
	__u8         pad2[0xB8-0xA2];          /* 0x0a2 */
	__u16        subchannel_id;            /* 0x0b8 */
	__u16        subchannel_nr;            /* 0x0ba */
	__u32        io_int_parm;              /* 0x0bc */
	__u32        io_int_word;              /* 0x0c0 */
        __u8         pad3[0xD4-0xC4];          /* 0x0c4 */
	__u32        extended_save_area_addr;  /* 0x0d4 */
	__u32        cpu_timer_save_area[2];   /* 0x0d8 */
	__u32        clock_comp_save_area[2];  /* 0x0e0 */
	__u32        mcck_interruption_code[2]; /* 0x0e8 */
	__u8         pad4[0xf4-0xf0];          /* 0x0f0 */
	__u32        external_damage_code;     /* 0x0f4 */
	__u32        failing_storage_address;  /* 0x0f8 */
	__u8         pad5[0x100-0xfc];         /* 0x0fc */
	__u32        st_status_fixed_logout[4];/* 0x100 */
	__u8         pad6[0x120-0x110];        /* 0x110 */
	__u32        access_regs_save_area[16];/* 0x120 */
	__u32        floating_pt_save_area[8]; /* 0x160 */
	__u32        gpregs_save_area[16];     /* 0x180 */
	__u32        cregs_save_area[16];      /* 0x1c0 */	

        psw_t        return_psw;               /* 0x200 */
	__u8	     irb[64];		       /* 0x208 */
	__u64        sync_enter_timer;         /* 0x248 */
	__u64        async_enter_timer;        /* 0x250 */
	__u64        exit_timer;               /* 0x258 */
	__u64        last_update_timer;        /* 0x260 */
	__u64        user_timer;               /* 0x268 */
	__u64        system_timer;             /* 0x270 */
	__u64        last_update_clock;        /* 0x278 */
	__u64        steal_clock;              /* 0x280 */
        psw_t        return_mcck_psw;          /* 0x288 */
	__u8         pad8[0xc00-0x290];        /* 0x290 */

        /* System info area */
	__u32        save_area[16];            /* 0xc00 */
	__u32        kernel_stack;             /* 0xc40 */
	__u32        thread_info;              /* 0xc44 */
	__u32        async_stack;              /* 0xc48 */
	__u32        kernel_asce;              /* 0xc4c */
	__u32        user_asce;                /* 0xc50 */
	__u32        panic_stack;              /* 0xc54 */
	__u8         pad10[0xc60-0xc58];       /* 0xc58 */
	/* entry.S sensitive area start */
	struct       cpuinfo_S390 cpu_data;    /* 0xc60 */
	__u32        ipl_device;               /* 0xc7c */
	/* entry.S sensitive area end */

        /* SMP info area: defined by DJB */
        __u64        jiffy_timer;              /* 0xc80 */
	__u32        ext_call_fast;            /* 0xc88 */
	__u32        percpu_offset;            /* 0xc8c */
	__u32        current_task;	       /* 0xc90 */
	__u32        softirq_pending;	       /* 0xc94 */
	__u64        int_clock;                /* 0xc98 */
        __u8         pad11[0xe00-0xca0];       /* 0xca0 */

        /* 0xe00 is used as indicator for dump tools */
        /* whether the kernel died with panic() or not */
        __u32        panic_magic;              /* 0xe00 */

        /* Align to the top 1k of prefix area */
	__u8         pad12[0x1000-0xe04];      /* 0xe04 */
#else /* !__s390x__ */
        /* prefix area: defined by architecture */
	__u32        ccw1[2];                  /* 0x000 */
	__u32        ccw2[4];                  /* 0x008 */
	__u8         pad1[0x80-0x18];          /* 0x018 */
	__u32        ext_params;               /* 0x080 */
	__u16        cpu_addr;                 /* 0x084 */
	__u16        ext_int_code;             /* 0x086 */
        __u16        svc_ilc;                  /* 0x088 */
        __u16        svc_code;                 /* 0x08a */
        __u16        pgm_ilc;                  /* 0x08c */
        __u16        pgm_code;                 /* 0x08e */
	__u32        data_exc_code;            /* 0x090 */
	__u16        mon_class_num;            /* 0x094 */
	__u16        per_perc_atmid;           /* 0x096 */
	addr_t       per_address;              /* 0x098 */
	__u8         exc_access_id;            /* 0x0a0 */
	__u8         per_access_id;            /* 0x0a1 */
	__u8         op_access_id;             /* 0x0a2 */
	__u8         ar_access_id;             /* 0x0a3 */
	__u8         pad2[0xA8-0xA4];          /* 0x0a4 */
	addr_t       trans_exc_code;           /* 0x0A0 */
	addr_t       monitor_code;             /* 0x09c */
	__u16        subchannel_id;            /* 0x0b8 */
	__u16        subchannel_nr;            /* 0x0ba */
	__u32        io_int_parm;              /* 0x0bc */
	__u32        io_int_word;              /* 0x0c0 */
	__u8         pad3[0xc8-0xc4];          /* 0x0c4 */
	__u32        stfl_fac_list;            /* 0x0c8 */
	__u8         pad4[0xe8-0xcc];          /* 0x0cc */
	__u32        mcck_interruption_code[2]; /* 0x0e8 */
	__u8         pad5[0xf4-0xf0];          /* 0x0f0 */
	__u32        external_damage_code;     /* 0x0f4 */
	addr_t       failing_storage_address;  /* 0x0f8 */
	__u8         pad6[0x120-0x100];        /* 0x100 */
	psw_t        restart_old_psw;          /* 0x120 */
	psw_t        external_old_psw;         /* 0x130 */
	psw_t        svc_old_psw;              /* 0x140 */
	psw_t        program_old_psw;          /* 0x150 */
	psw_t        mcck_old_psw;             /* 0x160 */
	psw_t        io_old_psw;               /* 0x170 */
	__u8         pad7[0x1a0-0x180];        /* 0x180 */
	psw_t        restart_psw;              /* 0x1a0 */
	psw_t        external_new_psw;         /* 0x1b0 */
	psw_t        svc_new_psw;              /* 0x1c0 */
	psw_t        program_new_psw;          /* 0x1d0 */
	psw_t        mcck_new_psw;             /* 0x1e0 */
	psw_t        io_new_psw;               /* 0x1f0 */
        psw_t        return_psw;               /* 0x200 */
	__u8	     irb[64];		       /* 0x210 */
	__u64        sync_enter_timer;         /* 0x250 */
	__u64        async_enter_timer;        /* 0x258 */
	__u64        exit_timer;               /* 0x260 */
	__u64        last_update_timer;        /* 0x268 */
	__u64        user_timer;               /* 0x270 */
	__u64        system_timer;             /* 0x278 */
	__u64        last_update_clock;        /* 0x280 */
	__u64        steal_clock;              /* 0x288 */
        psw_t        return_mcck_psw;          /* 0x290 */
        __u8         pad8[0xc00-0x2a0];        /* 0x2a0 */
        /* System info area */
	__u64        save_area[16];            /* 0xc00 */
        __u8         pad9[0xd40-0xc80];        /* 0xc80 */
 	__u64        kernel_stack;             /* 0xd40 */
	__u64        thread_info;              /* 0xd48 */
	__u64        async_stack;              /* 0xd50 */
	__u64        kernel_asce;              /* 0xd58 */
	__u64        user_asce;                /* 0xd60 */
	__u64        panic_stack;              /* 0xd68 */
	__u8         pad10[0xd80-0xd70];       /* 0xd70 */
	/* entry.S sensitive area start */
	struct       cpuinfo_S390 cpu_data;    /* 0xd80 */
	__u32        ipl_device;               /* 0xdb8 */
	__u32        pad11;                    /* 0xdbc */
	/* entry.S sensitive area end */

        /* SMP info area: defined by DJB */
        __u64        jiffy_timer;              /* 0xdc0 */
	__u64        ext_call_fast;            /* 0xdc8 */
	__u64        percpu_offset;            /* 0xdd0 */
	__u64        current_task;	       /* 0xdd8 */
	__u64        softirq_pending;	       /* 0xde0 */
	__u64        int_clock;                /* 0xde8 */
        __u8         pad12[0xe00-0xdf0];       /* 0xdf0 */

        /* 0xe00 is used as indicator for dump tools */
        /* whether the kernel died with panic() or not */
        __u32        panic_magic;              /* 0xe00 */

	__u8         pad13[0x1200-0xe04];      /* 0xe04 */

        /* System info area */ 

	__u64        floating_pt_save_area[16]; /* 0x1200 */
	__u64        gpregs_save_area[16];      /* 0x1280 */
	__u32        st_status_fixed_logout[4]; /* 0x1300 */
	__u8         pad14[0x1318-0x1310];      /* 0x1310 */
	__u32        prefixreg_save_area;       /* 0x1318 */
	__u32        fpt_creg_save_area;        /* 0x131c */
	__u8         pad15[0x1324-0x1320];      /* 0x1320 */
	__u32        tod_progreg_save_area;     /* 0x1324 */
	__u32        cpu_timer_save_area[2];    /* 0x1328 */
	__u32        clock_comp_save_area[2];   /* 0x1330 */
	__u8         pad16[0x1340-0x1338];      /* 0x1338 */ 
	__u32        access_regs_save_area[16]; /* 0x1340 */ 
	__u64        cregs_save_area[16];       /* 0x1380 */

	/* align to the top of the prefix area */

	__u8         pad17[0x2000-0x1400];      /* 0x1400 */
#endif /* !__s390x__ */
} __attribute__((packed)); /* End structure*/

#define S390_lowcore (*((struct _lowcore *) 0))
extern struct _lowcore *lowcore_ptr[];

static inline void set_prefix(__u32 address)
{
        __asm__ __volatile__ ("spx %0" : : "m" (address) : "memory" );
}

#define __PANIC_MAGIC           0xDEADC0DE

#endif

#endif
