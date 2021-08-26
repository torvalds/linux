/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2017-2021  NXP
 *
 ******************************************************************************
 * Communication stack of audio with rpmsg
 ******************************************************************************
 * Packet structure:
 *   A SRTM message consists of a 10 bytes header followed by 0~N bytes of data
 *
 *   +---------------+-------------------------------+
 *   |               |            Content            |
 *   +---------------+-------------------------------+
 *   |  Byte Offset  | 7   6   5   4   3   2   1   0 |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       0       |           Category            |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |     1 ~ 2     |           Version             |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       3       |             Type              |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       4       |           Command             |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       5       |           Reserved0           |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       6       |           Reserved1           |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       7       |           Reserved2           |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       8       |           Reserved3           |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       9       |           Reserved4           |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |       10      |            DATA 0             |
 *   +---------------+---+---+---+---+---+---+---+---+
 *   :   :   :   :   :   :   :   :   :   :   :   :   :
 *   +---------------+---+---+---+---+---+---+---+---+
 *   |   N + 10 - 1  |            DATA N-1           |
 *   +---------------+---+---+---+---+---+---+---+---+
 *
 *   +----------+------------+------------------------------------------------+
 *   |  Field   |    Byte    |                                                |
 *   +----------+------------+------------------------------------------------+
 *   | Category |     0      | The destination category.                      |
 *   +----------+------------+------------------------------------------------+
 *   | Version  |   1 ~ 2    | The category version of the sender of the      |
 *   |          |            | packet.                                        |
 *   |          |            | The first byte represent the major version of  |
 *   |          |            | the packet.The second byte represent the minor |
 *   |          |            | version of the packet.                         |
 *   +----------+------------+------------------------------------------------+
 *   |  Type    |     3      | The message type of current message packet.    |
 *   +----------+------------+------------------------------------------------+
 *   | Command  |     4      | The command byte sent to remote processor/SoC. |
 *   +----------+------------+------------------------------------------------+
 *   | Reserved |   5 ~ 9    | Reserved field for future extension.           |
 *   +----------+------------+------------------------------------------------+
 *   | Data     |     N      | The data payload of the message packet.        |
 *   +----------+------------+------------------------------------------------+
 *
 * Audio control:
 *   SRTM Audio Control Category Request Command Table:
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   | Category | Version | Type | Command | Data                          | Function              |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x00   | Data[0]: Audio Device Index   | Open a TX Instance.   |
 *   |          |         |      |         | Data[1]:     format           |                       |
 *   |          |         |      |         | Data[2]:     channels         |                       |
 *   |          |         |      |         | Data[3-6]:   samplerate       |                       |
 *   |          |         |      |         | Data[7-10]:  buffer_addr      |                       |
 *   |          |         |      |         | Data[11-14]: buffer_size      |                       |
 *   |          |         |      |         | Data[15-18]: period_size      |                       |
 *   |          |         |      |         | Data[19-22]: buffer_tail      |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x01   | Data[0]: Audio Device Index   | Start a TX Instance.  |
 *   |          |         |      |         | Same as above command         |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x02   | Data[0]: Audio Device Index   | Pause a TX Instance.  |
 *   |          |         |      |         | Same as above command         |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x03   | Data[0]: Audio Device Index   | Resume a TX Instance. |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x04   | Data[0]: Audio Device Index   | Stop a TX Instance.   |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x05   | Data[0]: Audio Device Index   | Close a TX Instance.  |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x06   | Data[0]: Audio Device Index   | Set Parameters for    |
 *   |          |         |      |         | Data[1]:     format           | a TX Instance.        |
 *   |          |         |      |         | Data[2]:     channels         |                       |
 *   |          |         |      |         | Data[3-6]:   samplerate       |                       |
 *   |          |         |      |         | Data[7-22]:  reserved         |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x07   | Data[0]: Audio Device Index   | Set TX Buffer.        |
 *   |          |         |      |         | Data[1-6]:   reserved         |                       |
 *   |          |         |      |         | Data[7-10]:  buffer_addr      |                       |
 *   |          |         |      |         | Data[11-14]: buffer_size      |                       |
 *   |          |         |      |         | Data[15-18]: period_size      |                       |
 *   |          |         |      |         | Data[19-22]: buffer_tail      |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x08   | Data[0]: Audio Device Index   | Suspend a TX Instance |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x09   | Data[0]: Audio Device Index   | Resume a TX Instance. |
 *   |          |         |      |         | Data[1]:     format           |                       |
 *   |          |         |      |         | Data[2]:     channels         |                       |
 *   |          |         |      |         | Data[3-6]:   samplerate       |                       |
 *   |          |         |      |         | Data[7-10]:  buffer_addr      |                       |
 *   |          |         |      |         | Data[11-14]: buffer_size      |                       |
 *   |          |         |      |         | Data[15-18]: period_size      |                       |
 *   |          |         |      |         | Data[19-22]: buffer_tail      |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x0A   | Data[0]: Audio Device Index   | Open a RX Instance.   |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x0B   | Data[0]: Audio Device Index   | Start a RX Instance.  |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x0C   | Data[0]: Audio Device Index   | Pause a RX Instance.  |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x0D   | Data[0]: Audio Device Index   | Resume a RX Instance. |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x0E   | Data[0]: Audio Device Index   | Stop a RX Instance.   |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x0F   | Data[0]: Audio Device Index   | Close a RX Instance.  |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x10   | Data[0]: Audio Device Index   | Set Parameters for    |
 *   |          |         |      |         | Data[1]:     format           | a RX Instance.        |
 *   |          |         |      |         | Data[2]:     channels         |                       |
 *   |          |         |      |         | Data[3-6]:   samplerate       |                       |
 *   |          |         |      |         | Data[7-22]:  reserved         |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x11   | Data[0]: Audio Device Index   | Set RX Buffer.        |
 *   |          |         |      |         | Data[1-6]:   reserved         |                       |
 *   |          |         |      |         | Data[7-10]:  buffer_addr      |                       |
 *   |          |         |      |         | Data[11-14]: buffer_size      |                       |
 *   |          |         |      |         | Data[15-18]: period_size      |                       |
 *   |          |         |      |         | Data[19-22]: buffer_tail      |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x12   | Data[0]: Audio Device Index   | Suspend a RX Instance.|
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x13   | Data[0]: Audio Device Index   | Resume a RX Instance. |
 *   |          |         |      |         | Data[1]:     format           |                       |
 *   |          |         |      |         | Data[2]:     channels         |                       |
 *   |          |         |      |         | Data[3-6]:   samplerate       |                       |
 *   |          |         |      |         | Data[7-10]:  buffer_addr      |                       |
 *   |          |         |      |         | Data[11-14]: buffer_size      |                       |
 *   |          |         |      |         | Data[15-18]: period_size      |                       |
 *   |          |         |      |         | Data[19-22]: buffer_tail      |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x14   | Data[0]: Audio Device Index   | Set register value    |
 *   |          |         |      |         | Data[1-6]:   reserved         | to codec              |
 *   |          |         |      |         | Data[7-10]:  register         |                       |
 *   |          |         |      |         | Data[11-14]: value            |                       |
 *   |          |         |      |         | Data[15-22]: reserved         |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x00 |  0x15   | Data[0]: Audio Device Index   | Get register value    |
 *   |          |         |      |         | Data[1-6]:   reserved         | from codec            |
 *   |          |         |      |         | Data[7-10]:  register         |                       |
 *   |          |         |      |         | Data[11-22]: reserved         |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   Note 1: See <List of Sample Format> for available value of
 *           Sample Format;
 *   Note 2: See <List of Audio Channels> for available value of Channels;
 *   Note 3: Sample Rate of Set Parameters for an Audio TX Instance
 *           Command and Set Parameters for an Audio RX Instance Command is
 *           in little-endian format.
 *
 *   SRTM Audio Control Category Response Command Table:
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   | Category | Version | Type | Command | Data                          | Function              |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x00   | Data[0]: Audio Device Index   | Reply for Open        |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x01   | Data[0]: Audio Device Index   | Reply for Start       |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x02   | Data[0]: Audio Device Index   | Reply for Pause       |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x03   | Data[0]: Audio Device Index   | Reply for Resume      |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x04   | Data[0]: Audio Device Index   | Reply for Stop        |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x05   | Data[0]: Audio Device Index   | Reply for Close       |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x06   | Data[0]: Audio Device Index   | Reply for Set Param   |
 *   |          |         |      |         | Data[1]: Return code          | for a TX Instance.    |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x07   | Data[0]: Audio Device Index   | Reply for Set         |
 *   |          |         |      |         | Data[1]: Return code          | TX Buffer             |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x08   | Data[0]: Audio Device Index   | Reply for Suspend     |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x09   | Data[0]: Audio Device Index   | Reply for Resume      |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x0A   | Data[0]: Audio Device Index   | Reply for Open        |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x0B   | Data[0]: Audio Device Index   | Reply for Start       |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x0C   | Data[0]: Audio Device Index   | Reply for Pause       |
 *   |          |         |      |         | Data[1]: Return code          | a TX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x0D   | Data[0]: Audio Device Index   | Reply for Resume      |
 *   |          |         |      |         | Data[1]: Return code          | a RX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x0E   | Data[0]: Audio Device Index   | Reply for Stop        |
 *   |          |         |      |         | Data[1]: Return code          | a RX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x0F   | Data[0]: Audio Device Index   | Reply for Close       |
 *   |          |         |      |         | Data[1]: Return code          | a RX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x10   | Data[0]: Audio Device Index   | Reply for Set Param   |
 *   |          |         |      |         | Data[1]: Return code          | for a RX Instance.    |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x11   | Data[0]: Audio Device Index   | Reply for Set         |
 *   |          |         |      |         | Data[1]: Return code          | RX Buffer             |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x12   | Data[0]: Audio Device Index   | Reply for Suspend     |
 *   |          |         |      |         | Data[1]: Return code          | a RX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x13   | Data[0]: Audio Device Index   | Reply for Resume      |
 *   |          |         |      |         | Data[1]: Return code          | a RX Instance         |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x14   | Data[0]: Audio Device Index   | Reply for Set codec   |
 *   |          |         |      |         | Data[1]: Return code          | register value        |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x01 |  0x15   | Data[0]: Audio Device Index   | Reply for Get codec   |
 *   |          |         |      |         | Data[1]: Return code          | register value        |
 *   |          |         |      |         | Data[2-6]:   reserved         |                       |
 *   |          |         |      |         | Data[7-10]:  register         |                       |
 *   |          |         |      |         | Data[11-14]: value            |                       |
 *   |          |         |      |         | Data[15-22]: reserved         |                       |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *
 *   SRTM Audio Control Category Notification Command Table:
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   | Category | Version | Type | Command | Data                          | Function              |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x02 |  0x00   | Data[0]: Audio Device Index   | Notify one TX period  |
 *   |          |         |      |         |                               | is finished           |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *   |  0x03    | 0x0100  | 0x02 |  0x01   | Data[0]: Audio Device Index   | Notify one RX period  |
 *   |          |         |      |         |                               | is finished           |
 *   +----------+---------+------+---------+-------------------------------+-----------------------+
 *
 *   List of Sample Format:
 *   +------------------+-----------------------+
 *   | Sample Format    |   Description         |
 *   +------------------+-----------------------+
 *   |       0x0        | S16_LE                |
 *   +------------------+-----------------------+
 *   |       0x1        | S24_LE                |
 *   +------------------+-----------------------+
 *
 *   List of Audio Channels
 *   +------------------+-----------------------+
 *   |  Audio Channel   |   Description         |
 *   +------------------+-----------------------+
 *   |       0x0        | Left Channel          |
 *   +------------------+-----------------------+
 *   |       0x1        | Right Channel         |
 *   +------------------+---------------- ------+
 *   |       0x2        | Left & Right Channel  |
 *   +------------------+-----------------------+
 *
 */

#ifndef _IMX_PCM_RPMSG_H
#define _IMX_PCM_RPMSG_H

#include <linux/pm_qos.h>
#include <linux/interrupt.h>
#include <sound/dmaengine_pcm.h>

#define RPMSG_TIMEOUT 1000

/* RPMSG Command (TYPE A)*/
#define TX_OPEN		0x0
#define	TX_START	0x1
#define	TX_PAUSE	0x2
#define	TX_RESTART	0x3
#define	TX_TERMINATE	0x4
#define	TX_CLOSE	0x5
#define TX_HW_PARAM	0x6
#define	TX_BUFFER	0x7
#define	TX_SUSPEND	0x8
#define	TX_RESUME	0x9

#define	RX_OPEN		0xA
#define	RX_START	0xB
#define	RX_PAUSE	0xC
#define	RX_RESTART	0xD
#define	RX_TERMINATE	0xE
#define	RX_CLOSE	0xF
#define	RX_HW_PARAM	0x10
#define	RX_BUFFER	0x11
#define	RX_SUSPEND	0x12
#define	RX_RESUME	0x13
#define SET_CODEC_VALUE 0x14
#define GET_CODEC_VALUE 0x15
#define	TX_POINTER	0x16
#define	RX_POINTER	0x17
/* Total msg numver for type A */
#define MSG_TYPE_A_NUM  0x18

/* RPMSG Command (TYPE C)*/
#define	TX_PERIOD_DONE	0x0
#define	RX_PERIOD_DONE	0x1
/* Total msg numver for type C */
#define MSG_TYPE_C_NUM  0x2

#define MSG_MAX_NUM     (MSG_TYPE_A_NUM + MSG_TYPE_C_NUM)

#define MSG_TYPE_A	0x0
#define MSG_TYPE_B	0x1
#define MSG_TYPE_C	0x2

#define RESP_NONE		0x0
#define RESP_NOT_ALLOWED	0x1
#define	RESP_SUCCESS		0x2
#define	RESP_FAILED		0x3

#define	RPMSG_S16_LE		0x0
#define	RPMSG_S24_LE		0x1
#define	RPMSG_S32_LE		0x2
#define	RPMSG_DSD_U16_LE	49  /* SNDRV_PCM_FORMAT_DSD_U16_LE */
#define	RPMSG_DSD_U24_LE	0x4
#define	RPMSG_DSD_U32_LE	50  /* SNDRV_PCM_FORMAT_DSD_U32_LE */

#define	RPMSG_CH_LEFT		0x0
#define	RPMSG_CH_RIGHT		0x1
#define	RPMSG_CH_STEREO		0x2

#define WORK_MAX_NUM    0x30

/* Category define */
#define IMX_RMPSG_LIFECYCLE     1
#define IMX_RPMSG_PMIC          2
#define IMX_RPMSG_AUDIO         3
#define IMX_RPMSG_KEY           4
#define IMX_RPMSG_GPIO          5
#define IMX_RPMSG_RTC           6
#define IMX_RPMSG_SENSOR        7

/* rpmsg version */
#define IMX_RMPSG_MAJOR         1
#define IMX_RMPSG_MINOR         0

#define TX SNDRV_PCM_STREAM_PLAYBACK
#define RX SNDRV_PCM_STREAM_CAPTURE

/**
 * struct rpmsg_head: rpmsg header structure
 *
 * @cate: category
 * @major: major version
 * @minor: minor version
 * @type: message type (A/B/C)
 * @cmd: message command
 * @reserved: reserved space
 */
struct rpmsg_head {
	u8 cate;
	u8 major;
	u8 minor;
	u8 type;
	u8 cmd;
	u8 reserved[5];
} __packed;

/**
 * struct param_s: sent rpmsg parameter
 *
 * @audioindex: audio instance index
 * @format: audio format
 * @channels: audio channel number
 * @rate: sample rate
 * @buffer_addr: dma buffer physical address or register for SET_CODEC_VALUE
 * @buffer_size: dma buffer size or register value for SET_CODEC_VALUE
 * @period_size: period size
 * @buffer_tail: current period index
 */
struct param_s {
	unsigned char audioindex;
	unsigned char format;
	unsigned char channels;
	unsigned int  rate;
	unsigned int  buffer_addr;
	unsigned int  buffer_size;
	unsigned int  period_size;
	unsigned int  buffer_tail;
} __packed;

/**
 * struct param_s: send rpmsg parameter
 *
 * @audioindex: audio instance index
 * @resp: response value
 * @reserved1: reserved space
 * @buffer_offset: the consumed offset of buffer
 * @reg_addr: register addr of codec
 * @reg_data: register value of codec
 * @reserved2: reserved space
 * @buffer_tail: current period index
 */
struct param_r {
	unsigned char audioindex;
	unsigned char resp;
	unsigned char reserved1[1];
	unsigned int  buffer_offset;
	unsigned int  reg_addr;
	unsigned int  reg_data;
	unsigned char reserved2[4];
	unsigned int  buffer_tail;
} __packed;

/* Struct of sent message */
struct rpmsg_s_msg {
	struct rpmsg_head header;
	struct param_s    param;
};

/* Struct of received message */
struct rpmsg_r_msg {
	struct rpmsg_head header;
	struct param_r    param;
};

/* Struct of rpmsg */
struct rpmsg_msg {
	struct rpmsg_s_msg  s_msg;
	struct rpmsg_r_msg  r_msg;
};

/* Struct of rpmsg for workqueue */
struct work_of_rpmsg {
	struct rpmsg_info   *info;
	/* Sent msg for each work */
	struct rpmsg_msg    msg;
	struct work_struct  work;
};

/* Struct of timer */
struct stream_timer {
	struct timer_list   timer;
	struct rpmsg_info   *info;
	struct snd_pcm_substream *substream;
};

typedef void (*dma_callback)(void *arg);

/**
 * struct rpmsg_info: rpmsg audio information
 *
 * @rpdev: pointer of rpmsg_device
 * @dev: pointer for imx_pcm_rpmsg device
 * @cmd_complete: command is finished
 * @pm_qos_req: request of pm qos
 * @r_msg: received rpmsg
 * @msg: array of rpmsg
 * @notify: notification msg (type C) for TX & RX
 * @notify_updated: notification flag for TX & RX
 * @rpmsg_wq: rpmsg workqueue
 * @work_list: array of work list for workqueue
 * @work_write_index: write index of work list
 * @work_read_index: read index of work list
 * @msg_drop_count: counter of dropped msg for TX & RX
 * @num_period: period number for TX & RX
 * @callback_param: parameter for period elapse callback for TX & RX
 * @callback: period elapse callback for TX & RX
 * @send_message: function pointer for send message
 * @lock: spin lock for TX & RX
 * @wq_lock: lock for work queue
 * @msg_lock: lock for send message
 * @stream_timer: timer for tigger workqueue
 */
struct rpmsg_info {
	struct rpmsg_device      *rpdev;
	struct device            *dev;
	struct completion        cmd_complete;
	struct pm_qos_request    pm_qos_req;

	/* Received msg (global) */
	struct rpmsg_r_msg       r_msg;
	struct rpmsg_msg         msg[MSG_MAX_NUM];
	/* period done */
	struct rpmsg_msg         notify[2];
	bool                     notify_updated[2];

	struct workqueue_struct  *rpmsg_wq;
	struct work_of_rpmsg	 work_list[WORK_MAX_NUM];
	int                      work_write_index;
	int                      work_read_index;
	int                      msg_drop_count[2];
	int                      num_period[2];
	void                     *callback_param[2];
	dma_callback             callback[2];
	int (*send_message)(struct rpmsg_msg *msg, struct rpmsg_info *info);
	spinlock_t               lock[2]; /* spin lock for resource protection */
	spinlock_t               wq_lock; /* spin lock for resource protection */
	struct mutex             msg_lock; /* mutex for resource protection */
	struct stream_timer      stream_timer[2];
};

#define IMX_PCM_DRV_NAME "imx_pcm_rpmsg"

#endif /* IMX_PCM_RPMSG_H */
