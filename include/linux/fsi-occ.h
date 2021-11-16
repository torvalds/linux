// SPDX-License-Identifier: GPL-2.0

#ifndef LINUX_FSI_OCC_H
#define LINUX_FSI_OCC_H

struct device;

#define OCC_RESP_CMD_IN_PRG		0xFF
#define OCC_RESP_SUCCESS		0
#define OCC_RESP_CMD_INVAL		0x11
#define OCC_RESP_CMD_LEN_INVAL		0x12
#define OCC_RESP_DATA_INVAL		0x13
#define OCC_RESP_CHKSUM_ERR		0x14
#define OCC_RESP_INT_ERR		0x15
#define OCC_RESP_BAD_STATE		0x16
#define OCC_RESP_CRIT_EXCEPT		0xE0
#define OCC_RESP_CRIT_INIT		0xE1
#define OCC_RESP_CRIT_WATCHDOG		0xE2
#define OCC_RESP_CRIT_OCB		0xE3
#define OCC_RESP_CRIT_HW		0xE4

#define OCC_MAX_RESP_WORDS		2048

int fsi_occ_submit(struct device *dev, const void *request, size_t req_len,
		   void *response, size_t *resp_len);

#endif /* LINUX_FSI_OCC_H */
