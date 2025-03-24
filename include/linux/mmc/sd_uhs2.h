/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Header file for UHS-II packets, Host Controller registers and I/O
 * accessors.
 *
 *  Copyright (C) 2014 Intel Corp, All Rights Reserved.
 */
#ifndef LINUX_MMC_UHS2_H
#define LINUX_MMC_UHS2_H

/* LINK Layer definition */
/*
 * UHS2 Header:
 * Refer to UHS-II Addendum Version 1.02 Figure 5-2, the format of CCMD Header is described below:
 *      bit [3:0]  : DID(Destination ID = Node ID of UHS2 card)
 *      bit [6:4]  : TYP(Packet Type)
 *                   000b: CCMD(Control command packet)
 *                   001b: DCMD(Data command packet)
 *                   010b: RES(Response packet)
 *                   011b: DATA(Data payload packet)
 *                   111b: MSG(Message packet)
 *                   Others: Reserved
 *      bit [7]    : NP(Native Packet)
 *      bit [10:8] : TID(Transaction ID)
 *      bit [11]   : Reserved
 *      bit [15:12]: SID(Source ID 0: Node ID of Host)
 *
 * Broadcast CCMD issued by Host is represented as DID=SID=0.
 */
/*
 * UHS2 Argument:
 * Refer to UHS-II Addendum Version 1.02 Figure 6-5, the format of CCMD Argument is described below:
 *      bit [3:0]  : MSB of IOADR
 *      bit [5:4]  : PLEN(Payload Length)
 *                   00b: 0 byte
 *                   01b: 4 bytes
 *                   10b: 8 bytes
 *                   11b: 16 bytes
 *      bit [6]    : Reserved
 *      bit [7]    : R/W(Read/Write)
 *                   0: Control read command
 *                   1: Control write command
 *      bit [15:8] : LSB of IOADR
 *
 * I/O Address specifies the address of register in UHS-II I/O space accessed by CCMD.
 * The unit of I/O Address is 4 Bytes. It is transmitted in MSB first, LSB last.
 */
#define UHS2_NATIVE_PACKET_POS	7
#define UHS2_NATIVE_PACKET	(1 << UHS2_NATIVE_PACKET_POS)

#define UHS2_PACKET_TYPE_POS	4
#define UHS2_PACKET_TYPE_CCMD	(0 << UHS2_PACKET_TYPE_POS)
#define UHS2_PACKET_TYPE_DCMD	(1 << UHS2_PACKET_TYPE_POS)
#define UHS2_PACKET_TYPE_RES	(2 << UHS2_PACKET_TYPE_POS)
#define UHS2_PACKET_TYPE_DATA	(3 << UHS2_PACKET_TYPE_POS)
#define UHS2_PACKET_TYPE_MSG	(7 << UHS2_PACKET_TYPE_POS)

#define UHS2_DEST_ID_MASK	0x0F
#define UHS2_DEST_ID		0x1

#define UHS2_SRC_ID_POS		12
#define UHS2_SRC_ID_MASK	0xF000

#define UHS2_TRANS_ID_POS	8
#define UHS2_TRANS_ID_MASK	0x0700

/* UHS2 MSG */
#define UHS2_MSG_CTG_POS	5
#define UHS2_MSG_CTG_LMSG	0x00
#define UHS2_MSG_CTG_INT	0x60
#define UHS2_MSG_CTG_AMSG	0x80

#define UHS2_MSG_CTG_FCREQ	0x00
#define UHS2_MSG_CTG_FCRDY	0x01
#define UHS2_MSG_CTG_STAT	0x02

#define UHS2_MSG_CODE_POS			8
#define UHS2_MSG_CODE_FC_UNRECOVER_ERR		0x8
#define UHS2_MSG_CODE_STAT_UNRECOVER_ERR	0x8
#define UHS2_MSG_CODE_STAT_RECOVER_ERR		0x1

/* TRANS Layer definition */

/* Native packets*/
#define UHS2_NATIVE_CMD_RW_POS	7
#define UHS2_NATIVE_CMD_WRITE	(1 << UHS2_NATIVE_CMD_RW_POS)
#define UHS2_NATIVE_CMD_READ	(0 << UHS2_NATIVE_CMD_RW_POS)

#define UHS2_NATIVE_CMD_PLEN_POS	4
#define UHS2_NATIVE_CMD_PLEN_4B		(1 << UHS2_NATIVE_CMD_PLEN_POS)
#define UHS2_NATIVE_CMD_PLEN_8B		(2 << UHS2_NATIVE_CMD_PLEN_POS)
#define UHS2_NATIVE_CMD_PLEN_16B	(3 << UHS2_NATIVE_CMD_PLEN_POS)

#define UHS2_NATIVE_CCMD_GET_MIOADR_MASK	0xF00
#define UHS2_NATIVE_CCMD_MIOADR_MASK		0x0F

#define UHS2_NATIVE_CCMD_LIOADR_POS		8
#define UHS2_NATIVE_CCMD_GET_LIOADR_MASK	0x0FF

#define UHS2_CCMD_DEV_INIT_COMPLETE_FLAG	BIT(11)
#define UHS2_DEV_INIT_PAYLOAD_LEN		1
#define UHS2_DEV_INIT_RESP_LEN			6
#define UHS2_DEV_ENUM_PAYLOAD_LEN		1
#define UHS2_DEV_ENUM_RESP_LEN			8
#define UHS2_CFG_WRITE_PAYLOAD_LEN		2
#define UHS2_CFG_WRITE_PHY_SET_RESP_LEN		4
#define UHS2_CFG_WRITE_GENERIC_SET_RESP_LEN	5
#define UHS2_GO_DORMANT_PAYLOAD_LEN		1

/*
 * UHS2 Argument:
 * Refer to UHS-II Addendum Version 1.02 Figure 6-8, the format of DCMD Argument is described below:
 *      bit [3:0]  : Reserved
 *      bit [6:3]  : TMODE(Transfer Mode)
 *                   bit 3: DAM(Data Access Mode)
 *                   bit 4: TLUM(TLEN Unit Mode)
 *                   bit 5: LM(Length Mode)
 *                   bit 6: DM(Duplex Mode)
 *      bit [7]    : R/W(Read/Write)
 *                   0: Control read command
 *                   1: Control write command
 *      bit [15:8] : Reserved
 *
 * I/O Address specifies the address of register in UHS-II I/O space accessed by CCMD.
 * The unit of I/O Address is 4 Bytes. It is transmitted in MSB first, LSB last.
 */
#define UHS2_DCMD_DM_POS		6
#define UHS2_DCMD_2L_HD_MODE		(1 << UHS2_DCMD_DM_POS)
#define UHS2_DCMD_LM_POS		5
#define UHS2_DCMD_LM_TLEN_EXIST		(1 << UHS2_DCMD_LM_POS)
#define UHS2_DCMD_TLUM_POS		4
#define UHS2_DCMD_TLUM_BYTE_MODE	(1 << UHS2_DCMD_TLUM_POS)
#define UHS2_NATIVE_DCMD_DAM_POS	3
#define UHS2_NATIVE_DCMD_DAM_IO		(1 << UHS2_NATIVE_DCMD_DAM_POS)

#define UHS2_RES_NACK_POS	7
#define UHS2_RES_NACK_MASK	(0x1 << UHS2_RES_NACK_POS)

#define UHS2_RES_ECODE_POS	4
#define UHS2_RES_ECODE_MASK	0x7
#define UHS2_RES_ECODE_COND	1
#define UHS2_RES_ECODE_ARG	2
#define UHS2_RES_ECODE_GEN	3

/* IOADR of device registers */
#define UHS2_IOADR_GENERIC_CAPS		0x00
#define UHS2_IOADR_PHY_CAPS		0x02
#define UHS2_IOADR_LINK_CAPS		0x04
#define UHS2_IOADR_RSV_CAPS		0x06
#define UHS2_IOADR_GENERIC_SETTINGS	0x08
#define UHS2_IOADR_PHY_SETTINGS		0x0A
#define UHS2_IOADR_LINK_SETTINGS	0x0C
#define UHS2_IOADR_PRESET		0x40

/* SD application packets */
#define UHS2_SD_CMD_INDEX_POS	8

#define UHS2_SD_CMD_APP_POS	14
#define UHS2_SD_CMD_APP		(1 << UHS2_SD_CMD_APP_POS)

/* UHS-II Device Registers */
#define UHS2_DEV_CONFIG_REG	0x000

/* General Caps and Settings registers */
#define UHS2_DEV_CONFIG_GEN_CAPS	(UHS2_DEV_CONFIG_REG + 0x000)
#define UHS2_DEV_CONFIG_N_LANES_POS	8
#define UHS2_DEV_CONFIG_N_LANES_MASK	0x3F
#define UHS2_DEV_CONFIG_2L_HD_FD	0x1
#define UHS2_DEV_CONFIG_2D1U_FD		0x2
#define UHS2_DEV_CONFIG_1D2U_FD		0x4
#define UHS2_DEV_CONFIG_2D2U_FD		0x8
#define UHS2_DEV_CONFIG_DADR_POS	14
#define UHS2_DEV_CONFIG_DADR_MASK	0x1
#define UHS2_DEV_CONFIG_APP_POS		16
#define UHS2_DEV_CONFIG_APP_MASK	0xFF
#define UHS2_DEV_CONFIG_APP_SD_MEM	0x1

#define UHS2_DEV_CONFIG_GEN_SET			(UHS2_DEV_CONFIG_REG + 0x008)
#define UHS2_DEV_CONFIG_GEN_SET_N_LANES_POS	8
#define UHS2_DEV_CONFIG_GEN_SET_2L_FD_HD	0x0
#define UHS2_DEV_CONFIG_GEN_SET_2D1U_FD		0x2
#define UHS2_DEV_CONFIG_GEN_SET_1D2U_FD		0x3
#define UHS2_DEV_CONFIG_GEN_SET_2D2U_FD		0x4
#define UHS2_DEV_CONFIG_GEN_SET_CFG_COMPLETE	BIT(31)

/* PHY Caps and Settings registers */
#define UHS2_DEV_CONFIG_PHY_CAPS	(UHS2_DEV_CONFIG_REG + 0x002)
#define UHS2_DEV_CONFIG_PHY_MINOR_MASK	0xF
#define UHS2_DEV_CONFIG_PHY_MAJOR_POS	4
#define UHS2_DEV_CONFIG_PHY_MAJOR_MASK	0x3
#define UHS2_DEV_CONFIG_CAN_HIBER_POS	15
#define UHS2_DEV_CONFIG_CAN_HIBER_MASK	0x1
#define UHS2_DEV_CONFIG_PHY_CAPS1	(UHS2_DEV_CONFIG_REG + 0x003)
#define UHS2_DEV_CONFIG_N_LSS_SYN_MASK	0xF
#define UHS2_DEV_CONFIG_N_LSS_DIR_POS	4
#define UHS2_DEV_CONFIG_N_LSS_DIR_MASK	0xF

#define UHS2_DEV_CONFIG_PHY_SET			(UHS2_DEV_CONFIG_REG + 0x00A)
#define UHS2_DEV_CONFIG_PHY_SET_SPEED_POS	6
#define UHS2_DEV_CONFIG_PHY_SET_SPEED_A		0x0
#define UHS2_DEV_CONFIG_PHY_SET_SPEED_B		0x1

/* LINK-TRAN Caps and Settings registers */
#define UHS2_DEV_CONFIG_LINK_TRAN_CAPS		(UHS2_DEV_CONFIG_REG + 0x004)
#define UHS2_DEV_CONFIG_LT_MINOR_MASK		0xF
#define UHS2_DEV_CONFIG_LT_MAJOR_POS		4
#define UHS2_DEV_CONFIG_LT_MAJOR_MASK		0x3
#define UHS2_DEV_CONFIG_N_FCU_POS		8
#define UHS2_DEV_CONFIG_N_FCU_MASK		0xFF
#define UHS2_DEV_CONFIG_DEV_TYPE_POS		16
#define UHS2_DEV_CONFIG_DEV_TYPE_MASK		0x7
#define UHS2_DEV_CONFIG_MAX_BLK_LEN_POS		20
#define UHS2_DEV_CONFIG_MAX_BLK_LEN_MASK	0xFFF
#define UHS2_DEV_CONFIG_LINK_TRAN_CAPS1		(UHS2_DEV_CONFIG_REG + 0x005)
#define UHS2_DEV_CONFIG_N_DATA_GAP_MASK		0xFF

#define UHS2_DEV_CONFIG_LINK_TRAN_SET		(UHS2_DEV_CONFIG_REG + 0x00C)
#define UHS2_DEV_CONFIG_LT_SET_MAX_BLK_LEN	0x200
#define UHS2_DEV_CONFIG_LT_SET_MAX_RETRY_POS	16

/* Preset register */
#define UHS2_DEV_CONFIG_PRESET	(UHS2_DEV_CONFIG_REG + 0x040)

#define UHS2_DEV_INT_REG	0x100

#define UHS2_DEV_STATUS_REG	0x180

#define UHS2_DEV_CMD_REG		0x200
#define UHS2_DEV_CMD_FULL_RESET		(UHS2_DEV_CMD_REG + 0x000)
#define UHS2_DEV_CMD_GO_DORMANT_STATE	(UHS2_DEV_CMD_REG + 0x001)
#define UHS2_DEV_CMD_DORMANT_HIBER	BIT(7)
#define UHS2_DEV_CMD_DEVICE_INIT	(UHS2_DEV_CMD_REG + 0x002)
#define UHS2_DEV_INIT_COMPLETE_FLAG	BIT(11)
#define UHS2_DEV_CMD_ENUMERATE		(UHS2_DEV_CMD_REG + 0x003)
#define UHS2_DEV_CMD_TRANS_ABORT	(UHS2_DEV_CMD_REG + 0x004)

#define UHS2_RCLK_MAX	52000000
#define UHS2_RCLK_MIN	26000000

#endif /* LINUX_MMC_UHS2_H */
