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

#define TWS_AEN_NOT_RETRIEVED        0x1
#define TWS_AEN_RETRIEVED            0x2

#define TWS_AEN_NO_EVENTS            0x1003  /* No more events */
#define TWS_AEN_OVERFLOW             0x1004  /* AEN overflow occurred */

#define TWS_IOCTL_LOCK_NOT_HELD      0x1001   /* Not locked */
#define TWS_IOCTL_LOCK_ALREADY_HELD  0x1002   /* Already locked */

#define TWS_IOCTL_LOCK_HELD          0x1
#define TWS_IOCTL_LOCK_FREE          0x0

#pragma pack(1)

/* Structure used to handle GET/RELEASE LOCK ioctls. */
struct tws_lock_packet {
    u_int32_t       timeout_msec;
    u_int32_t       time_remaining_msec;
    u_int32_t       force_flag;
};

/* Structure used to handle GET COMPATIBILITY INFO ioctl. */
struct tws_compatibility_packet {
    u_int8_t    driver_version[32];/* driver version */
    u_int16_t   working_srl;    /* driver & firmware negotiated srl */
    u_int16_t   working_branch; /* branch # of the firmware that the
                                    driver is compatible with */
    u_int16_t   working_build;  /* build # of the firmware that the
                                        driver is compatible with */
    u_int16_t   driver_srl_high;/* highest driver supported srl */
    u_int16_t   driver_branch_high;/* highest driver supported branch */
    u_int16_t   driver_build_high;/* highest driver supported build */
    u_int16_t   driver_srl_low;/* lowest driver supported srl */
    u_int16_t   driver_branch_low;/* lowest driver supported branch */
    u_int16_t   driver_build_low;/* lowest driver supported build */
    u_int16_t   fw_on_ctlr_srl; /* srl of running firmware */
    u_int16_t   fw_on_ctlr_branch;/* branch # of running firmware */
    u_int16_t   fw_on_ctlr_build;/* build # of running firmware */
};


/* Driver understandable part of the ioctl packet built by the API. */
struct tws_driver_packet {
    u_int32_t       control_code;
    u_int32_t       status;
    u_int32_t       unique_id;
    u_int32_t       sequence_id;
    u_int32_t       os_status;
    u_int32_t       buffer_length;
};

/* ioctl packet built by the API. */
struct tws_ioctl_packet {
    struct tws_driver_packet      driver_pkt;
    char                          padding[488];
    struct tws_command_packet     cmd_pkt;
    char                          data_buf[1];
};

#pragma pack()


#pragma pack(1)
/*
 * We need the structure below to ensure that the first byte of
 * data_buf is not overwritten by the kernel, after we return
 * from the ioctl call.  Note that cmd_pkt has been reduced
 * to an array of 1024 bytes even though it's actually 2048 bytes
 * in size.  This is because, we don't expect requests from user
 * land requiring 2048 (273 sg elements) byte cmd pkts.
 */
struct tws_ioctl_no_data_buf {
    struct tws_driver_packet     driver_pkt;
    void                         *pdata; /* points to data_buf */
    char                         padding[488 - sizeof(void *)];
    struct tws_command_packet    cmd_pkt;
};

#pragma pack()


#include <sys/ioccom.h> 

#pragma pack(1)

struct tws_ioctl_with_payload {
    struct tws_driver_packet     driver_pkt;
    char                         padding[488];
    struct tws_command_packet    cmd_pkt;
    union {
        struct tws_event_packet               event_pkt;
        struct tws_lock_packet                lock_pkt;
        struct tws_compatibility_packet       compat_pkt;
        char                                  data_buf[1];
    } payload;
};

#pragma pack()

/* ioctl cmds */

#define TWS_IOCTL_SCAN_BUS                            \
        _IO('T', 200)
#define TWS_IOCTL_FIRMWARE_PASS_THROUGH               \
        _IOWR('T', 202, struct tws_ioctl_no_data_buf)
#define TWS_IOCTL_GET_FIRST_EVENT                     \
        _IOWR('T', 203, struct tws_ioctl_with_payload)
#define TWS_IOCTL_GET_LAST_EVENT                      \
        _IOWR('T', 204, struct tws_ioctl_with_payload)
#define TWS_IOCTL_GET_NEXT_EVENT                      \
        _IOWR('T', 205, struct tws_ioctl_with_payload)
#define TWS_IOCTL_GET_PREVIOUS_EVENT                  \
        _IOWR('T', 206, struct tws_ioctl_with_payload)
#define TWS_IOCTL_GET_LOCK                            \
        _IOWR('T', 207, struct tws_ioctl_with_payload)
#define TWS_IOCTL_RELEASE_LOCK                        \
        _IOWR('T', 208, struct tws_ioctl_with_payload)
#define TWS_IOCTL_GET_COMPATIBILITY_INFO              \
        _IOWR('T', 209, struct tws_ioctl_with_payload)

