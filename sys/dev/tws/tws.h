/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2010, LSI Corp.
 * All rights reserved.
 * Author : Manjunath Ranganathaiah
 * Support: freebsdraid@lsi.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the <ORGANIZATION> nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>        /* defines used in kernel.h */
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/kernel.h>       /* types used in module initialization */
#include <sys/conf.h>         /* cdevsw struct */
#include <sys/uio.h>          /* uio struct */
#include <sys/malloc.h>
#include <sys/bus.h>          /* structs, prototypes for pci bus stuff */


#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>   /* For pci_get macros! */
#include <dev/pci/pcireg.h>

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/stat.h>


#define TWS_PULL_MODE_ENABLE 1

MALLOC_DECLARE(M_TWS);
/* externs */
extern int tws_queue_depth;


#define TWS_DRIVER_VERSION_STRING "10.80.00.005"
#define TWS_MAX_NUM_UNITS             65 
#define TWS_MAX_NUM_LUNS              32
#define TWS_MAX_IRQS                  2
#define TWS_SCSI_INITIATOR_ID         66
#define TWS_MAX_IO_SIZE               0x20000 /* 128kB */
#define TWS_SECTOR_SIZE               0x200
#define TWS_POLL_TIMEOUT              60
#define TWS_IO_TIMEOUT                60
#define TWS_IOCTL_TIMEOUT             60
#define TWS_RESET_TIMEOUT             60

#define TWS_PCI_BAR0                  0x10
#define TWS_PCI_BAR1                  0x14
#define TWS_PCI_BAR2                  0x1C

#define TWS_VENDOR_ID                 0x13C1 
#define TWS_DEVICE_ID                 0x1010

#define TWS_INVALID_REQID             0xFFFF

/* bus tag related */
#define TWS_ALIGNMENT                 4
#define TWS_IN_MF_ALIGNMENT           16
#define TWS_OUT_MF_ALIGNMENT          4

#define TWS_MAX_32BIT_SG_ELEMENTS     93     /* max 32-bit sg elements */
#define TWS_MAX_64BIT_SG_ELEMENTS     46     /* max 64-bit sg elements */

#define TWS_MAX_QS                    4
#define TWS_MAX_REQS                  256
#define TWS_RESERVED_REQS             4

/* Request states */
#define TWS_REQ_STATE_FREE            0
#define TWS_REQ_STATE_BUSY            1
#define TWS_REQ_STATE_TRAN            2
#define TWS_REQ_STATE_COMPLETE        3

/* Request types */
#define TWS_REQ_TYPE_INTERNAL_CMD     0x0
#define TWS_REQ_TYPE_AEN_FETCH        0x1
#define TWS_REQ_TYPE_PASSTHRU         0x2
#define TWS_REQ_TYPE_GETSET_PARAM     0x3
#define TWS_REQ_TYPE_SCSI_IO          0x4

/* Driver states */

enum tws_states {
    TWS_INIT=50,
    TWS_UNINIT,
    TWS_OFFLINE,
    TWS_ONLINE,
    TWS_RESET,
};

/* events */

enum tws_events {
    TWS_INIT_START=100,
    TWS_INIT_COMPLETE,
    TWS_UNINIT_START,
    TWS_RESET_START,
    TWS_RESET_COMPLETE,
    TWS_SCAN_FAILURE,
};

enum tws_req_flags {
    TWS_DIR_UNKNOWN = 0x1,
    TWS_DIR_IN = 0x2,
    TWS_DIR_OUT = 0x4,
    TWS_DIR_NONE = 0x8,
    TWS_DATA_CCB = 0x10,
};
 
enum tws_intrs {
     TWS_INTx,
     TWS_MSI,
     TWS_MSIX,
};

struct tws_msix_info {
    int tbl_res_id;
    bus_space_tag_t tbl_tag;
    bus_space_handle_t tbl_handle;
    struct resource *tbl_res;  
};

struct tws_ioctl_lock {
    u_int32_t       lock;
    time_t          timeout;
};
 

#define TWS_TRACE_FNAME_LEN  10
#define TWS_TRACE_FUNC_LEN   15
#define TWS_TRACE_DESC_LEN   10
struct tws_trace_rec {
    struct timespec ts;
    char fname[TWS_TRACE_FNAME_LEN];
    char func[TWS_TRACE_FUNC_LEN];
    int linenum;
    char desc[TWS_TRACE_DESC_LEN];
    u_int64_t val1;
    u_int64_t val2;
};

struct tws_circular_q {
    volatile int16_t head;
    volatile int16_t tail;
    u_int16_t depth;
    u_int8_t  overflow;
    void *    q;
};
 


struct tws_stats {
    u_int64_t reqs_in;
    u_int64_t reqs_out;
    u_int64_t reqs_errored;
    u_int64_t spurios_intrs;
    u_int64_t num_intrs;    
    u_int64_t num_aens;    
    u_int64_t ioctls;       
    u_int64_t scsi_ios;
};

struct tws_init_connect_info {
    u_int16_t     working_srl;
    u_int16_t     working_branch;
    u_int16_t     working_build;
    u_int16_t     fw_on_ctlr_srl;
    u_int16_t     fw_on_ctlr_branch;
    u_int16_t     fw_on_ctlr_build;

};


/* ------------ boolean types ------------------- */

#ifndef __bool_true_false_are_defined
typedef enum _boolean { false, true } boolean;
#else
#define	boolean		bool
#endif
enum err { SUCCESS, FAILURE };

/* ----------- per instance data ---------------- */

/* The softc holds our per-instance data. */
struct tws_softc {
    device_t    tws_dev;                  /* bus device */
    struct cdev *tws_cdev;                /* controller device */
    u_int32_t   device_id;                /* device id */
    u_int32_t   subvendor_id;             /* device id */
    u_int32_t   subdevice_id;             /* device id */
    u_int8_t    tws_state;                /* driver state */
    u_int8_t    tws_prev_state;           /* driver prev state */
    struct sysctl_ctx_list tws_clist;     /* sysctl context */
    struct sysctl_oid *tws_oidp;          /* sysctl context */
    struct resource *reg_res;             /* register interface window */
    struct resource *mfa_res;             /* mfa interface window */
    int reg_res_id;                       /* register resource id */
    int mfa_res_id;                       /* register resource id */
    bus_space_handle_t bus_handle;        /* bus space handle */
    bus_space_handle_t bus_mfa_handle;     /* bus space handle */
    bus_space_tag_t bus_tag;              /* bus space tag */
    bus_space_tag_t bus_mfa_tag;          /* bus space tag for mfa's */
    u_int64_t mfa_base;                   /* mfa base address */
    struct resource *irq_res[TWS_MAX_IRQS];/* interrupt resource */
    int irq_res_id[TWS_MAX_IRQS];         /* intr resource id */
    void *intr_handle[TWS_MAX_IRQS];      /* interrupt handle */
    int irqs;                             /* intrs used */
    struct tws_msix_info msix;            /* msix info */
    struct cam_sim *sim;                  /* sim for this controller */
    struct cam_path *path;                /* Ctlr path to CAM */
    struct mtx q_lock;                    /* queue lock */
    struct mtx sim_lock;                  /* sim lock */
    struct mtx gen_lock;                  /* general driver  lock */
    struct mtx io_lock;                   /* IO  lock */
    struct tws_ioctl_lock ioctl_lock;     /* ioctl lock */ 
    u_int32_t seq_id;                     /* Sequence id */
    struct tws_circular_q aen_q;          /* aen q */
    struct tws_circular_q trace_q;        /* trace q */
    struct tws_stats stats;               /* I/O stats */
    struct tws_init_connect_info cinfo;   /* compatibility info */
    boolean is64bit;                      /* True - 64bit else 32bit */
    u_int8_t intr_type;                   /* Interrupt type used */
    bus_dma_tag_t parent_tag;             /* parent DMA tag */
    bus_dma_tag_t cmd_tag;                /* command DMA tag */
    bus_dmamap_t cmd_map;                 /* command map */
    void *dma_mem;                        /* pointer to dmable memory */
    u_int64_t dma_mem_phys;               /* phy addr */
    bus_dma_tag_t data_tag;               /* data DMA tag */
    void *ioctl_data_mem;                 /* ioctl dmable memory */
    bus_dmamap_t ioctl_data_map;          /* ioctl data map */
    struct tws_request *reqs;             /* pointer to requests */
    struct tws_sense *sense_bufs;         /* pointer to sense buffers */
    boolean obfl_q_overrun;               /* OBFL overrun flag  */
    union ccb *scan_ccb;                  /* pointer to a ccb */
    struct tws_request *q_head[TWS_MAX_QS]; /* head pointers to q's */
    struct tws_request *q_tail[TWS_MAX_QS]; /* tail pointers to q's */
    struct callout stats_timer;
};
