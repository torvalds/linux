// Copyright (c) 2010 Atheros Communications Inc.
// All rights reserved.
// $ATH_LICENSE_TARGET_C$

#ifndef __CLIENT_H_
#define __CLIENT_H_

#include "dk.h"
#include "dk_ioctl.h"
#include "dk_event.h"

#define MAX_CLIENTS_SUPPORTED 8
#define INVALID_CLIENT -1
#define DK_MAJOR_NUMBER 63
#define DK_UART_MAJOR_NUMBER 64
#define WMAC_FN_DEV_START_NUM      0
#define UART_FN_DEV_START_NUM      4
#define NETWORK_CLASS   0x2
#define SIMPLE_COMM_CLASS       0x7



/* atheros devices info */
struct atheros_dev_ {
    void *bus_dev;
	UINT32 reg_phy_addr;
	UINT32 reg_ker_vir_addr;
	UINT32 reg_range;
	UINT32 mem_phy_addr;
	UINT32 mem_ker_vir_addr;
	UINT32 mem_size;
	UINT32 irq;
    UINT32 dev_busy;
	UINT32 cli_id;		// cli_id is same as the array index in the dev_table
	event_queue isr_event_q;
	event_queue trigered_event_q;
	UINT32 areg_phy_addr[MAX_BARS];
    UINT32 areg_ker_vir_addr[MAX_BARS];
	UINT32 areg_range[MAX_BARS];
    UINT32 numBars;
    UINT32 device_class;
};

typedef struct atheros_dev_ atheros_dev;
typedef struct atheros_dev_ *p_atheros_dev;

VOID init_client
(
 	VOID
);

INT32 add_client
(
 	VOID *bus_dev,
	UINT32 baseaddr[MAX_BARS],
	UINT32 len[MAX_BARS],
	UINT32 irq,
    UINT32 numBars,
    UINT32 sIndex
);

VOID remove_client
(
 	VOID *bus_dev
);

VOID cleanup_client
(
     VOID
);

INT32 register_client
(
     INT32 major,
     INT32 minor
);


VOID unregister_client
(
     INT32 cli_id
);

INT32 cli_cfg_read
(
 	INT32 cli_id,
	INT32 offset,
	INT32 size,
	INT32 *ret_val
);


INT32 cli_cfg_write
(
 	INT32 cli_id,
	INT32 offset,
	INT32 size,
	INT32 ret_val
);

INT32 cli_reg_read
(
 	INT32 cli_id,
	INT32 offset,
	UINT32 *ret_val
);


INT32 cli_reg_write
(
 	INT32 cli_id,
	INT32 offset,
	UINT32 ret_val
);

INT32 get_cli_info
(
     INT32 cli_id,
     struct client_info *ci
);

p_atheros_dev get_client
(
     INT32 cli_id
);

#define VALID_CLIENT(x) ((x)->cli_id != INVALID_CLIENT)
#define BUSY_CLIENT(x) ((x)->dev_busy != 0)

#endif // __CLIENT_H_
