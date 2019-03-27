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


/* bit's defination */

#define TWS_BIT0                              0x00000001
#define TWS_BIT1                              0x00000002
#define TWS_BIT2                              0x00000004
#define TWS_BIT3                              0x00000008
#define TWS_BIT4                              0x00000010
#define TWS_BIT5                              0x00000020
#define TWS_BIT6                              0x00000040
#define TWS_BIT7                              0x00000080
#define TWS_BIT8                              0x00000100
#define TWS_BIT9                              0x00000200
#define TWS_BIT10                             0x00000400
#define TWS_BIT11                             0x00000800
#define TWS_BIT12                             0x00001000
#define TWS_BIT13                             0x00002000
#define TWS_BIT14                             0x00004000
#define TWS_BIT15                             0x00008000
#define TWS_BIT16                             0x00010000
#define TWS_BIT17                             0x00020000
#define TWS_BIT18                             0x00040000
#define TWS_BIT19                             0x00080000
#define TWS_BIT20                             0x00100000
#define TWS_BIT21                             0x00200000
#define TWS_BIT22                             0x00400000
#define TWS_BIT23                             0x00800000
#define TWS_BIT24                             0x01000000
#define TWS_BIT25                             0x02000000
#define TWS_BIT26                             0x04000000
#define TWS_BIT27                             0x08000000
#define TWS_BIT28                             0x10000000
#define TWS_BIT29                             0x20000000
#define TWS_BIT30                             0x40000000
#define TWS_BIT31                             0x80000000

#define TWS_SENSE_DATA_LENGTH                 18
#define TWS_ERROR_SPECIFIC_DESC_LEN           98

/* response codes */
#define TWS_SENSE_SCSI_CURRENT_ERROR          0x70
#define TWS_SENSE_SCSI_DEFERRED_ERROR         0x71

#define TWS_SRC_CTRL_ERROR                    3
#define TWS_SRC_CTRL_EVENT                    4
#define TWS_SRC_FREEBSD_DRIVER                5
#define TWS_SRC_FREEBSD_OS                    8


enum tws_sense_severity {
    error = 1,
    warning ,
    info,
    debug,
};

/*
 * Some errors of interest (in cmd_hdr->status_block.error) when a command
 * is completed by the firmware with an error.
 */
#define TWS_ERROR_LOGICAL_UNIT_NOT_SUPPORTED    0x010a
#define TWS_ERROR_NOT_SUPPORTED                 0x010D
#define TWS_ERROR_UNIT_OFFLINE                  0x0128
#define TWS_ERROR_MORE_DATA                     0x0231


/* AEN codes of interest. */
#define TWS_AEN_QUEUE_EMPTY                     0x00
#define TWS_AEN_SOFT_RESET                      0x01
#define TWS_AEN_SYNC_TIME_WITH_HOST             0x31


/* AEN severity */
#define TWS_SEVERITY_ERROR                      0x1
#define TWS_SEVERITY_WARNING                    0x2
#define TWS_SEVERITY_INFO                       0x3
#define TWS_SEVERITY_DEBUG                      0x4

#define TWS_64BIT_SG_ADDRESSES                  0x00000001
#define TWS_BIT_EXTEND                          0x00000002

#define TWS_BASE_FW_SRL                         24
#define TWS_BASE_FW_BRANCH                      0
#define TWS_BASE_FW_BUILD                       1
#define TWS_CURRENT_FW_SRL                      41

#define TWS_CURRENT_FW_BRANCH                   8
#define TWS_CURRENT_FW_BUILD                    4
#define TWS_CURRENT_ARCH_ID                     0x000A


#define TWS_FIFO_EMPTY                          0xFFFFFFFFFFFFFFFFull
#define TWS_FIFO_EMPTY32                        0xFFFFFFFFull


/* Register offsets from base address. */
#define TWS_CONTROL_REGISTER_OFFSET             0x0
#define TWS_STATUS_REGISTER_OFFSET              0x4
#define TWS_COMMAND_QUEUE_OFFSET                0x8
#define TWS_RESPONSE_QUEUE_OFFSET               0xC
#define TWS_COMMAND_QUEUE_OFFSET_LOW            0x20
#define TWS_COMMAND_QUEUE_OFFSET_HIGH           0x24
#define TWS_LARGE_RESPONSE_QUEUE_OFFSET         0x30

/* I2O offsets */
#define TWS_I2O0_STATUS                         0x0

#define TWS_I2O0_HIBDB                          0x20

#define TWS_I2O0_HISTAT                         0x30
#define TWS_I2O0_HIMASK                         0x34

#define TWS_I2O0_HIBQP                          0x40
#define TWS_I2O0_HOBQP                          0x44

#define TWS_I2O0_CTL                            0x74

#define TWS_I2O0_IOBDB                          0x9C
#define TWS_I2O0_HOBDBC                         0xA0

#define TWS_I2O0_SCRPD3                         0xBC

#define TWS_I2O0_HIBQPL                         0xC0 /* 64bit inb port low */
#define TWS_I2O0_HIBQPH                         0xC4 /* 64bit inb port high */
#define TWS_I2O0_HOBQPL                         0xC8 /* 64bit out port low */
#define TWS_I2O0_HOBQPH                         0xCC /* 64bit out port high */

/* IOP related */
#define TWS_I2O0_IOPOBQPL                       0xD8 /* OBFL */
#define TWS_I2O0_IOPOBQPH                       0xDC /* OBFH */
#define TWS_I2O0_SRC_ADDRH                      0xF8 /* Msg ASA */

#define TWS_MSG_ACC_MASK                        0x20000000
#define TWS_32BIT_MASK                          0xFFFFFFFF

/* revisit */
#define TWS_FW_CMD_NOP                     0x0
#define TWS_FW_CMD_INIT_CONNECTION         0x01
#define TWS_FW_CMD_EXECUTE_SCSI            0x10

#define TWS_FW_CMD_ATA_PASSTHROUGH         0x11 // This is really a PASSTHROUGH for both ATA and SCSI commands.
#define TWS_FW_CMD_GET_PARAM               0x12
#define TWS_FW_CMD_SET_PARAM               0x13


#define BUILD_SGL_OFF__OPCODE(sgl_off, opcode)  \
        ((sgl_off << 5) & 0xE0) | (opcode & 0x1F)       /* 3:5 */

#define BUILD_RES__OPCODE(res, opcode)          \
        ((res << 5) & 0xE0) | (opcode & 0x1F)           /* 3:5 */

#define GET_OPCODE(sgl_off__opcode)     \
        (sgl_off__opcode & 0x1F)                        /* 3:5 */



/* end revisit */


/* Table #'s and id's of parameters of interest in firmware's param table. */
#define TWS_PARAM_VERSION_TABLE         0x0402
#define TWS_PARAM_VERSION_FW            3       /* firmware version [16] */
#define TWS_PARAM_VERSION_BIOS          4       /* BIOSs version [16] */
#define TWS_PARAM_CTLR_MODEL            8       /* Controller model [16] */

#define TWS_PARAM_CONTROLLER_TABLE      0x0403
#define TWS_PARAM_CONTROLLER_PORT_COUNT 3       /* number of ports [1] */

#define TWS_PARAM_TIME_TABLE            0x40A
#define TWS_PARAM_TIME_SCHED_TIME       0x3

#define TWS_PARAM_PHYS_TABLE            0x0001 
#define TWS_PARAM_CONTROLLER_PHYS_COUNT 2       /* number of phys */

#define TWS_9K_PARAM_DESCRIPTOR         0x8000


/* ----------- request  ------------- */


#pragma pack(1)

struct tws_cmd_init_connect {
    u_int8_t        res1__opcode;   /* 3:5 */
    u_int8_t        size;
    u_int8_t        request_id;
    u_int8_t        res2;
    u_int8_t        status;
    u_int8_t        flags;
    u_int16_t       message_credits;
    u_int32_t       features;
    u_int16_t       fw_srl;
    u_int16_t       fw_arch_id;
    u_int16_t       fw_branch;
    u_int16_t       fw_build;
    u_int32_t       result;
};

/* Structure for downloading firmware onto the controller. */
struct tws_cmd_download_firmware {
    u_int8_t        sgl_off__opcode;/* 3:5 */
    u_int8_t        size;
    u_int8_t        request_id;
    u_int8_t        unit;
    u_int8_t        status;
    u_int8_t        flags;
    u_int16_t       param;
    u_int8_t        sgl[1];
};

/* Structure for hard resetting the controller. */
struct tws_cmd_reset_firmware {
    u_int8_t        res1__opcode;   /* 3:5 */
    u_int8_t        size;
    u_int8_t        request_id;
    u_int8_t        unit;
    u_int8_t        status;
    u_int8_t        flags;
    u_int8_t        res2;
    u_int8_t        param;
};


/* Structure for sending get/set param commands. */
struct tws_cmd_param {
    u_int8_t        sgl_off__opcode;/* 3:5 */
    u_int8_t        size;
    u_int8_t        request_id;
    u_int8_t        host_id__unit;  /* 4:4 */
    u_int8_t        status;
    u_int8_t        flags;
    u_int16_t       param_count;
    u_int8_t        sgl[1];
};

/* Generic command packet. */
struct tws_cmd_generic {
    u_int8_t        sgl_off__opcode;/* 3:5 */
    u_int8_t        size;
    u_int8_t        request_id;
    u_int8_t        host_id__unit;  /* 4:4 */
    u_int8_t        status;
    u_int8_t        flags;
    u_int16_t       count;  /* block cnt, parameter cnt, message credits */
};




/* Command packet header. */
struct tws_command_header {
    u_int8_t        sense_data[TWS_SENSE_DATA_LENGTH];
    struct { /* status block - additional sense data */
        u_int16_t       srcnum;
        u_int8_t        reserved;
        u_int8_t        status;
        u_int16_t       error;
        u_int8_t        res__srcid;     /* 4:4 */
        u_int8_t        res__severity;  /* 5:3 */
    } status_block;
    u_int8_t        err_specific_desc[TWS_ERROR_SPECIFIC_DESC_LEN];
    struct { /* sense buffer descriptor */
        u_int8_t        size_header;
        u_int16_t       request_id;
        u_int8_t        size_sense;
    } header_desc;
};

/* Command - 1024 byte size including header (128+24+896)*/
union tws_command_giga {
    struct tws_cmd_init_connect       init_connect;
    struct tws_cmd_download_firmware  download_fw;
    struct tws_cmd_reset_firmware     reset_fw;
    struct tws_cmd_param              param;
    struct tws_cmd_generic            generic;
    u_int8_t        padding[1024 - sizeof(struct tws_command_header)];
};
    
/* driver command pkt - 1024 byte size including header(128+24+744+128) */
/* h/w & f/w supported command size excluding header 768 */
struct tws_command_apache {
    u_int8_t        res__opcode;    /* 3:5 */
    u_int8_t        unit;
    u_int16_t       lun_l4__req_id; /* 4:12 */
    u_int8_t        status;
    u_int8_t        sgl_offset;     /* offset (in bytes) to sg_list, 
                                     from the end of sgl_entries */
    u_int16_t       lun_h4__sgl_entries;
    u_int8_t        cdb[16];
    u_int8_t        sg_list[744];   /* 768 - 24 */
    u_int8_t        padding[128];   /* make it 1024 bytes */
};

struct tws_command_packet {
    struct tws_command_header hdr;
    union {
        union tws_command_giga pkt_g;
        struct tws_command_apache pkt_a;
    } cmd;
};

/* Structure describing payload for get/set param commands. */
struct tws_getset_param {
    u_int16_t       table_id;
    u_int8_t        parameter_id;
    u_int8_t        reserved;
    u_int16_t       parameter_size_bytes;
    u_int16_t       parameter_actual_size_bytes;
    u_int8_t        data[1];
};

struct tws_outbound_response {
    u_int32_t     not_mfa   :1;   /* 1 if the structure is valid else MFA */
    u_int32_t     reserved  :7;   /* reserved bits */
    u_int32_t     status    :8;   /* should be 0 */
    u_int32_t     request_id:16;  /* request id */
};


/* Scatter/Gather list entry with 32 bit addresses. */
struct tws_sg_desc32 {
    u_int32_t     address;
    u_int32_t     length  :24;
    u_int32_t     flag    :8;
};

/* Scatter/Gather list entry with 64 bit addresses. */
struct tws_sg_desc64 {
    u_int64_t     address;
    u_int64_t     length   :32;
    u_int64_t     reserved :24;
    u_int64_t     flag     :8;
};

/*
 * Packet that describes an AEN/error generated by the controller,
 * shared with user
 */
struct tws_event_packet {
    u_int32_t       sequence_id;
    u_int32_t       time_stamp_sec;
    u_int16_t       aen_code;
    u_int8_t        severity;
    u_int8_t        retrieved;
    u_int8_t        repeat_count;
    u_int8_t        parameter_len;
    u_int8_t        parameter_data[TWS_ERROR_SPECIFIC_DESC_LEN];
    u_int32_t       event_src;
    u_int8_t        severity_str[20];
};



#pragma pack()

struct tws_sense {
    struct tws_command_header *hdr;
    u_int64_t  hdr_pkt_phy;
};

struct tws_request {
    struct tws_command_packet *cmd_pkt; /* command pkt */  
    u_int64_t    cmd_pkt_phy;    /* cmd pkt physical address */       
    void         *data;          /* ptr to data being passed to fw */
    u_int32_t    length;         /* length of data being passed to fw */

    u_int32_t    state;          /* request state */
    u_int32_t    type;           /* request type */
    u_int32_t    flags;          /* request flags */

    u_int32_t    error_code;     /* error during request processing */

    u_int32_t    request_id;     /* request id for tracking with fw */
    void         (*cb)(struct tws_request *);      /* callback func */
    bus_dmamap_t dma_map;        /* dma map */
    union ccb    *ccb_ptr;       /* pointer to ccb */
    struct callout timeout;	 /* request timeout timer */
    struct tws_softc *sc;        /* pointer back to ctlr softc */

    struct tws_request *next;    /* pointer to next request */
    struct tws_request *prev;    /* pointer to prev request */
};


