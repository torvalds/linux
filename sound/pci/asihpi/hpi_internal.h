/******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License as
    published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

HPI internal definitions

(C) Copyright AudioScience Inc. 1996-2009
******************************************************************************/

#ifndef _HPI_INTERNAL_H_
#define _HPI_INTERNAL_H_

#include "hpi.h"

/** maximum number of memory regions mapped to an adapter */
#define HPI_MAX_ADAPTER_MEM_SPACES (2)

/* Each OS needs its own hpios.h */
#include "hpios.h"

/* physical memory allocation */

/** Allocate and map an area of locked memory for bus master DMA operations.

On success, *pLockedMemeHandle is a valid handle, and 0 is returned
On error *pLockedMemHandle marked invalid, non-zero returned.

If this function succeeds, then HpiOs_LockedMem_GetVirtAddr() and
HpiOs_LockedMem_GetPyhsAddr() will always succed on the returned handle.
*/
int hpios_locked_mem_alloc(struct consistent_dma_area *p_locked_mem_handle,
							   /**< memory handle */
	u32 size, /**< Size in bytes to allocate */
	struct pci_dev *p_os_reference
	/**< OS specific data required for memory allocation */
	);

/** Free mapping and memory represented by LockedMemHandle

Frees any resources, then invalidates the handle.
Returns 0 on success, 1 if handle is invalid.

*/
u16 hpios_locked_mem_free(struct consistent_dma_area *locked_mem_handle);

/** Get the physical PCI address of memory represented by LockedMemHandle.

If handle is invalid *pPhysicalAddr is set to zero and return 1
*/
u16 hpios_locked_mem_get_phys_addr(struct consistent_dma_area
	*locked_mem_handle, u32 *p_physical_addr);

/** Get the CPU address of of memory represented by LockedMemHandle.

If handle is NULL *ppvVirtualAddr is set to NULL and return 1
*/
u16 hpios_locked_mem_get_virt_addr(struct consistent_dma_area
	*locked_mem_handle, void **ppv_virtual_addr);

/** Check that handle is valid
i.e it represents a valid memory area
*/
u16 hpios_locked_mem_valid(struct consistent_dma_area *locked_mem_handle);

/* timing/delay */
void hpios_delay_micro_seconds(u32 num_micro_sec);

struct hpi_message;
struct hpi_response;

typedef void hpi_handler_func(struct hpi_message *, struct hpi_response *);

/* If the assert fails, compiler complains
   something like size of array `msg' is negative.
   Unlike linux BUILD_BUG_ON, this works outside function scope.
*/
#define compile_time_assert(cond, msg) \
    typedef char ASSERT_##msg[(cond) ? 1 : -1]

/******************************************* bus types */
enum HPI_BUSES {
	HPI_BUS_ISAPNP = 1,
	HPI_BUS_PCI = 2,
	HPI_BUS_USB = 3,
	HPI_BUS_NET = 4
};

enum HPI_SUBSYS_OPTIONS {
	/* 0, 256 are invalid, 1..255 reserved for global options */
	HPI_SUBSYS_OPT_NET_ENABLE = 257,
	HPI_SUBSYS_OPT_NET_BROADCAST = 258,
	HPI_SUBSYS_OPT_NET_UNICAST = 259,
	HPI_SUBSYS_OPT_NET_ADDR = 260,
	HPI_SUBSYS_OPT_NET_MASK = 261,
	HPI_SUBSYS_OPT_NET_ADAPTER_ADDRESS_ADD = 262
};

/** Volume flags
*/
enum HPI_VOLUME_FLAGS {
	/** Set if the volume control is muted */
	HPI_VOLUME_FLAG_MUTED = (1 << 0),
	/** Set if the volume control has a mute function */
	HPI_VOLUME_FLAG_HAS_MUTE = (1 << 1),
	/** Set if volume control can do autofading */
	HPI_VOLUME_FLAG_HAS_AUTOFADE = (1 << 2)
		/* Note Flags >= (1<<8) are for DSP internal use only */
};

/******************************************* CONTROL ATTRIBUTES ****/
/* (in order of control type ID */

/* This allows for 255 control types, 256 unique attributes each */
#define HPI_CTL_ATTR(ctl, ai) ((HPI_CONTROL_##ctl << 8) + ai)

/* Get the sub-index of the attribute for a control type */
#define HPI_CTL_ATTR_INDEX(i) (i & 0xff)

/* Extract the control from the control attribute */
#define HPI_CTL_ATTR_CONTROL(i) (i >> 8)

/** Enable event generation for a control.
0=disable, 1=enable
\note generic to all controls that can generate events
*/

/** Unique identifiers for every control attribute
*/
enum HPI_CONTROL_ATTRIBUTES {
	HPI_GENERIC_ENABLE = HPI_CTL_ATTR(GENERIC, 1),
	HPI_GENERIC_EVENT_ENABLE = HPI_CTL_ATTR(GENERIC, 2),

	HPI_VOLUME_GAIN = HPI_CTL_ATTR(VOLUME, 1),
	HPI_VOLUME_AUTOFADE = HPI_CTL_ATTR(VOLUME, 2),
	HPI_VOLUME_MUTE = HPI_CTL_ATTR(VOLUME, 3),
	HPI_VOLUME_GAIN_AND_FLAGS = HPI_CTL_ATTR(VOLUME, 4),
	HPI_VOLUME_NUM_CHANNELS = HPI_CTL_ATTR(VOLUME, 6),
	HPI_VOLUME_RANGE = HPI_CTL_ATTR(VOLUME, 10),

	HPI_METER_RMS = HPI_CTL_ATTR(METER, 1),
	HPI_METER_PEAK = HPI_CTL_ATTR(METER, 2),
	HPI_METER_RMS_BALLISTICS = HPI_CTL_ATTR(METER, 3),
	HPI_METER_PEAK_BALLISTICS = HPI_CTL_ATTR(METER, 4),
	HPI_METER_NUM_CHANNELS = HPI_CTL_ATTR(METER, 5),

	HPI_MULTIPLEXER_SOURCE = HPI_CTL_ATTR(MULTIPLEXER, 1),
	HPI_MULTIPLEXER_QUERYSOURCE = HPI_CTL_ATTR(MULTIPLEXER, 2),

	HPI_AESEBUTX_FORMAT = HPI_CTL_ATTR(AESEBUTX, 1),
	HPI_AESEBUTX_SAMPLERATE = HPI_CTL_ATTR(AESEBUTX, 3),
	HPI_AESEBUTX_CHANNELSTATUS = HPI_CTL_ATTR(AESEBUTX, 4),
	HPI_AESEBUTX_USERDATA = HPI_CTL_ATTR(AESEBUTX, 5),

	HPI_AESEBURX_FORMAT = HPI_CTL_ATTR(AESEBURX, 1),
	HPI_AESEBURX_ERRORSTATUS = HPI_CTL_ATTR(AESEBURX, 2),
	HPI_AESEBURX_SAMPLERATE = HPI_CTL_ATTR(AESEBURX, 3),
	HPI_AESEBURX_CHANNELSTATUS = HPI_CTL_ATTR(AESEBURX, 4),
	HPI_AESEBURX_USERDATA = HPI_CTL_ATTR(AESEBURX, 5),

	HPI_LEVEL_GAIN = HPI_CTL_ATTR(LEVEL, 1),
	HPI_LEVEL_RANGE = HPI_CTL_ATTR(LEVEL, 10),

	HPI_TUNER_BAND = HPI_CTL_ATTR(TUNER, 1),
	HPI_TUNER_FREQ = HPI_CTL_ATTR(TUNER, 2),
	HPI_TUNER_LEVEL_AVG = HPI_CTL_ATTR(TUNER, 3),
	HPI_TUNER_LEVEL_RAW = HPI_CTL_ATTR(TUNER, 4),
	HPI_TUNER_SNR = HPI_CTL_ATTR(TUNER, 5),
	HPI_TUNER_GAIN = HPI_CTL_ATTR(TUNER, 6),
	HPI_TUNER_STATUS = HPI_CTL_ATTR(TUNER, 7),
	HPI_TUNER_MODE = HPI_CTL_ATTR(TUNER, 8),
	HPI_TUNER_RDS = HPI_CTL_ATTR(TUNER, 9),
	HPI_TUNER_DEEMPHASIS = HPI_CTL_ATTR(TUNER, 10),
	HPI_TUNER_PROGRAM = HPI_CTL_ATTR(TUNER, 11),
	HPI_TUNER_HDRADIO_SIGNAL_QUALITY = HPI_CTL_ATTR(TUNER, 12),
	HPI_TUNER_HDRADIO_SDK_VERSION = HPI_CTL_ATTR(TUNER, 13),
	HPI_TUNER_HDRADIO_DSP_VERSION = HPI_CTL_ATTR(TUNER, 14),
	HPI_TUNER_HDRADIO_BLEND = HPI_CTL_ATTR(TUNER, 15),

	HPI_VOX_THRESHOLD = HPI_CTL_ATTR(VOX, 1),

	HPI_CHANNEL_MODE_MODE = HPI_CTL_ATTR(CHANNEL_MODE, 1),

	HPI_BITSTREAM_DATA_POLARITY = HPI_CTL_ATTR(BITSTREAM, 1),
	HPI_BITSTREAM_CLOCK_EDGE = HPI_CTL_ATTR(BITSTREAM, 2),
	HPI_BITSTREAM_CLOCK_SOURCE = HPI_CTL_ATTR(BITSTREAM, 3),
	HPI_BITSTREAM_ACTIVITY = HPI_CTL_ATTR(BITSTREAM, 4),

	HPI_SAMPLECLOCK_SOURCE = HPI_CTL_ATTR(SAMPLECLOCK, 1),
	HPI_SAMPLECLOCK_SAMPLERATE = HPI_CTL_ATTR(SAMPLECLOCK, 2),
	HPI_SAMPLECLOCK_SOURCE_INDEX = HPI_CTL_ATTR(SAMPLECLOCK, 3),
	HPI_SAMPLECLOCK_LOCAL_SAMPLERATE = HPI_CTL_ATTR(SAMPLECLOCK, 4),
	HPI_SAMPLECLOCK_AUTO = HPI_CTL_ATTR(SAMPLECLOCK, 5),
	HPI_SAMPLECLOCK_LOCAL_LOCK = HPI_CTL_ATTR(SAMPLECLOCK, 6),

	HPI_MICROPHONE_PHANTOM_POWER = HPI_CTL_ATTR(MICROPHONE, 1),

	HPI_EQUALIZER_NUM_FILTERS = HPI_CTL_ATTR(EQUALIZER, 1),
	HPI_EQUALIZER_FILTER = HPI_CTL_ATTR(EQUALIZER, 2),
	HPI_EQUALIZER_COEFFICIENTS = HPI_CTL_ATTR(EQUALIZER, 3),

	HPI_COMPANDER_PARAMS = HPI_CTL_ATTR(COMPANDER, 1),
	HPI_COMPANDER_MAKEUPGAIN = HPI_CTL_ATTR(COMPANDER, 2),
	HPI_COMPANDER_THRESHOLD = HPI_CTL_ATTR(COMPANDER, 3),
	HPI_COMPANDER_RATIO = HPI_CTL_ATTR(COMPANDER, 4),
	HPI_COMPANDER_ATTACK = HPI_CTL_ATTR(COMPANDER, 5),
	HPI_COMPANDER_DECAY = HPI_CTL_ATTR(COMPANDER, 6),

	HPI_COBRANET_SET = HPI_CTL_ATTR(COBRANET, 1),
	HPI_COBRANET_GET = HPI_CTL_ATTR(COBRANET, 2),
	HPI_COBRANET_GET_STATUS = HPI_CTL_ATTR(COBRANET, 5),
	HPI_COBRANET_SEND_PACKET = HPI_CTL_ATTR(COBRANET, 6),
	HPI_COBRANET_GET_PACKET = HPI_CTL_ATTR(COBRANET, 7),

	HPI_TONEDETECTOR_THRESHOLD = HPI_CTL_ATTR(TONEDETECTOR, 1),
	HPI_TONEDETECTOR_STATE = HPI_CTL_ATTR(TONEDETECTOR, 2),
	HPI_TONEDETECTOR_FREQUENCY = HPI_CTL_ATTR(TONEDETECTOR, 3),

	HPI_SILENCEDETECTOR_THRESHOLD = HPI_CTL_ATTR(SILENCEDETECTOR, 1),
	HPI_SILENCEDETECTOR_STATE = HPI_CTL_ATTR(SILENCEDETECTOR, 2),
	HPI_SILENCEDETECTOR_DELAY = HPI_CTL_ATTR(SILENCEDETECTOR, 3),

	HPI_PAD_CHANNEL_NAME = HPI_CTL_ATTR(PAD, 1),
	HPI_PAD_ARTIST = HPI_CTL_ATTR(PAD, 2),
	HPI_PAD_TITLE = HPI_CTL_ATTR(PAD, 3),
	HPI_PAD_COMMENT = HPI_CTL_ATTR(PAD, 4),
	HPI_PAD_PROGRAM_TYPE = HPI_CTL_ATTR(PAD, 5),
	HPI_PAD_PROGRAM_ID = HPI_CTL_ATTR(PAD, 6),
	HPI_PAD_TA_SUPPORT = HPI_CTL_ATTR(PAD, 7),
	HPI_PAD_TA_ACTIVE = HPI_CTL_ATTR(PAD, 8),

	HPI_UNIVERSAL_ENTITY = HPI_CTL_ATTR(UNIVERSAL, 1)
};

#define HPI_POLARITY_POSITIVE           0
#define HPI_POLARITY_NEGATIVE           1

/*------------------------------------------------------------
 Cobranet Chip Bridge - copied from HMI.H
------------------------------------------------------------*/
#define  HPI_COBRANET_HMI_cobra_bridge           0x20000
#define  HPI_COBRANET_HMI_cobra_bridge_tx_pkt_buf \
	(HPI_COBRANET_HMI_cobra_bridge + 0x1000)
#define  HPI_COBRANET_HMI_cobra_bridge_rx_pkt_buf \
	(HPI_COBRANET_HMI_cobra_bridge + 0x2000)
#define  HPI_COBRANET_HMI_cobra_if_table1         0x110000
#define  HPI_COBRANET_HMI_cobra_if_phy_address \
	(HPI_COBRANET_HMI_cobra_if_table1 + 0xd)
#define  HPI_COBRANET_HMI_cobra_protocolIP       0x72000
#define  HPI_COBRANET_HMI_cobra_ip_mon_currentIP \
	(HPI_COBRANET_HMI_cobra_protocolIP + 0x0)
#define  HPI_COBRANET_HMI_cobra_ip_mon_staticIP \
	(HPI_COBRANET_HMI_cobra_protocolIP + 0x2)
#define  HPI_COBRANET_HMI_cobra_sys              0x100000
#define  HPI_COBRANET_HMI_cobra_sys_desc \
		(HPI_COBRANET_HMI_cobra_sys + 0x0)
#define  HPI_COBRANET_HMI_cobra_sys_objectID \
	(HPI_COBRANET_HMI_cobra_sys + 0x100)
#define  HPI_COBRANET_HMI_cobra_sys_contact \
	(HPI_COBRANET_HMI_cobra_sys + 0x200)
#define  HPI_COBRANET_HMI_cobra_sys_name \
		(HPI_COBRANET_HMI_cobra_sys + 0x300)
#define  HPI_COBRANET_HMI_cobra_sys_location \
	(HPI_COBRANET_HMI_cobra_sys + 0x400)

/*------------------------------------------------------------
 Cobranet Chip Status bits
------------------------------------------------------------*/
#define HPI_COBRANET_HMI_STATUS_RXPACKET 2
#define HPI_COBRANET_HMI_STATUS_TXPACKET 3

/*------------------------------------------------------------
 Ethernet header size
------------------------------------------------------------*/
#define HPI_ETHERNET_HEADER_SIZE (16)

/* These defines are used to fill in protocol information for an Ethernet packet
    sent using HMI on CS18102 */
/** ID supplied by Cirrus for ASI packets. */
#define HPI_ETHERNET_PACKET_ID                  0x85
/** Simple packet - no special routing required */
#define HPI_ETHERNET_PACKET_V1                  0x01
/** This packet must make its way to the host across the HPI interface */
#define HPI_ETHERNET_PACKET_HOSTED_VIA_HMI      0x20
/** This packet must make its way to the host across the HPI interface */
#define HPI_ETHERNET_PACKET_HOSTED_VIA_HMI_V1   0x21
/** This packet must make its way to the host across the HPI interface */
#define HPI_ETHERNET_PACKET_HOSTED_VIA_HPI      0x40
/** This packet must make its way to the host across the HPI interface */
#define HPI_ETHERNET_PACKET_HOSTED_VIA_HPI_V1   0x41

#define HPI_ETHERNET_UDP_PORT 44600 /**< HPI UDP service */

/** Default network timeout in milli-seconds. */
#define HPI_ETHERNET_TIMEOUT_MS 500

/** Locked memory buffer alloc/free phases */
enum HPI_BUFFER_CMDS {
	/** use one message to allocate or free physical memory */
	HPI_BUFFER_CMD_EXTERNAL = 0,
	/** alloc physical memory */
	HPI_BUFFER_CMD_INTERNAL_ALLOC = 1,
	/** send physical memory address to adapter */
	HPI_BUFFER_CMD_INTERNAL_GRANTADAPTER = 2,
	/** notify adapter to stop using physical buffer */
	HPI_BUFFER_CMD_INTERNAL_REVOKEADAPTER = 3,
	/** free physical buffer */
	HPI_BUFFER_CMD_INTERNAL_FREE = 4
};

/*****************************************************************************/
/*****************************************************************************/
/********               HPI LOW LEVEL MESSAGES                  *******/
/*****************************************************************************/
/*****************************************************************************/
/** Pnp ids */
/** "ASI"  - actual is "ASX" - need to change */
#define HPI_ID_ISAPNP_AUDIOSCIENCE      0x0669
/** PCI vendor ID that AudioScience uses */
#define HPI_PCI_VENDOR_ID_AUDIOSCIENCE  0x175C
/** PCI vendor ID that the DSP56301 has */
#define HPI_PCI_VENDOR_ID_MOTOROLA      0x1057
/** PCI vendor ID that TI uses */
#define HPI_PCI_VENDOR_ID_TI            0x104C

#define HPI_PCI_DEV_ID_PCI2040          0xAC60
/** TI's C6205 PCI interface has this ID */
#define HPI_PCI_DEV_ID_DSP6205          0xA106

#define HPI_USB_VENDOR_ID_AUDIOSCIENCE  0x1257
#define HPI_USB_W2K_TAG                 0x57495341	/* "ASIW"       */
#define HPI_USB_LINUX_TAG               0x4C495341	/* "ASIL"       */

/** Invalid Adapter index
Used in HPI messages that are not addressed to a specific adapter
Used in DLL to indicate device not present
*/
#define HPI_ADAPTER_INDEX_INVALID 0xFFFF

/** First 2 hex digits define the adapter family */
#define HPI_ADAPTER_FAMILY_MASK         0xff00
#define HPI_MODULE_FAMILY_MASK          0xfff0

#define HPI_ADAPTER_FAMILY_ASI(f)   (f & HPI_ADAPTER_FAMILY_MASK)
#define HPI_MODULE_FAMILY_ASI(f)   (f & HPI_MODULE_FAMILY_MASK)
#define HPI_ADAPTER_ASI(f)   (f)

enum HPI_MESSAGE_TYPES {
	HPI_TYPE_REQUEST = 1,
	HPI_TYPE_RESPONSE = 2,
	HPI_TYPE_DATA = 3,
	HPI_TYPE_SSX2BYPASS_MESSAGE = 4,
	HPI_TYPE_COMMAND = 5,
	HPI_TYPE_NOTIFICATION = 6
};

enum HPI_OBJECT_TYPES {
	HPI_OBJ_SUBSYSTEM = 1,
	HPI_OBJ_ADAPTER = 2,
	HPI_OBJ_OSTREAM = 3,
	HPI_OBJ_ISTREAM = 4,
	HPI_OBJ_MIXER = 5,
	HPI_OBJ_NODE = 6,
	HPI_OBJ_CONTROL = 7,
	HPI_OBJ_NVMEMORY = 8,
	HPI_OBJ_GPIO = 9,
	HPI_OBJ_WATCHDOG = 10,
	HPI_OBJ_CLOCK = 11,
	HPI_OBJ_PROFILE = 12,
	/* HPI_ OBJ_ CONTROLEX  = 13, */
	HPI_OBJ_ASYNCEVENT = 14
#define HPI_OBJ_MAXINDEX 14
};

#define HPI_OBJ_FUNCTION_SPACING 0x100
#define HPI_FUNC_ID(obj, i) (HPI_OBJ_##obj * HPI_OBJ_FUNCTION_SPACING + i)

#define HPI_EXTRACT_INDEX(fn) (fn & 0xff)

enum HPI_FUNCTION_IDS {
	HPI_SUBSYS_OPEN = HPI_FUNC_ID(SUBSYSTEM, 1),
	HPI_SUBSYS_GET_VERSION = HPI_FUNC_ID(SUBSYSTEM, 2),
	HPI_SUBSYS_GET_INFO = HPI_FUNC_ID(SUBSYSTEM, 3),
	HPI_SUBSYS_CREATE_ADAPTER = HPI_FUNC_ID(SUBSYSTEM, 5),
	HPI_SUBSYS_CLOSE = HPI_FUNC_ID(SUBSYSTEM, 6),
	HPI_SUBSYS_DRIVER_LOAD = HPI_FUNC_ID(SUBSYSTEM, 8),
	HPI_SUBSYS_DRIVER_UNLOAD = HPI_FUNC_ID(SUBSYSTEM, 9),
	HPI_SUBSYS_GET_NUM_ADAPTERS = HPI_FUNC_ID(SUBSYSTEM, 12),
	HPI_SUBSYS_GET_ADAPTER = HPI_FUNC_ID(SUBSYSTEM, 13),
	HPI_SUBSYS_SET_NETWORK_INTERFACE = HPI_FUNC_ID(SUBSYSTEM, 14),
	HPI_SUBSYS_OPTION_INFO = HPI_FUNC_ID(SUBSYSTEM, 15),
	HPI_SUBSYS_OPTION_GET = HPI_FUNC_ID(SUBSYSTEM, 16),
	HPI_SUBSYS_OPTION_SET = HPI_FUNC_ID(SUBSYSTEM, 17),
#define HPI_SUBSYS_FUNCTION_COUNT 17

	HPI_ADAPTER_OPEN = HPI_FUNC_ID(ADAPTER, 1),
	HPI_ADAPTER_CLOSE = HPI_FUNC_ID(ADAPTER, 2),
	HPI_ADAPTER_GET_INFO = HPI_FUNC_ID(ADAPTER, 3),
	HPI_ADAPTER_GET_ASSERT = HPI_FUNC_ID(ADAPTER, 4),
	HPI_ADAPTER_TEST_ASSERT = HPI_FUNC_ID(ADAPTER, 5),
	HPI_ADAPTER_SET_MODE = HPI_FUNC_ID(ADAPTER, 6),
	HPI_ADAPTER_GET_MODE = HPI_FUNC_ID(ADAPTER, 7),
	HPI_ADAPTER_ENABLE_CAPABILITY = HPI_FUNC_ID(ADAPTER, 8),
	HPI_ADAPTER_SELFTEST = HPI_FUNC_ID(ADAPTER, 9),
	HPI_ADAPTER_FIND_OBJECT = HPI_FUNC_ID(ADAPTER, 10),
	HPI_ADAPTER_QUERY_FLASH = HPI_FUNC_ID(ADAPTER, 11),
	HPI_ADAPTER_START_FLASH = HPI_FUNC_ID(ADAPTER, 12),
	HPI_ADAPTER_PROGRAM_FLASH = HPI_FUNC_ID(ADAPTER, 13),
	HPI_ADAPTER_SET_PROPERTY = HPI_FUNC_ID(ADAPTER, 14),
	HPI_ADAPTER_GET_PROPERTY = HPI_FUNC_ID(ADAPTER, 15),
	HPI_ADAPTER_ENUM_PROPERTY = HPI_FUNC_ID(ADAPTER, 16),
	HPI_ADAPTER_MODULE_INFO = HPI_FUNC_ID(ADAPTER, 17),
	HPI_ADAPTER_DEBUG_READ = HPI_FUNC_ID(ADAPTER, 18),
	HPI_ADAPTER_IRQ_QUERY_AND_CLEAR = HPI_FUNC_ID(ADAPTER, 19),
	HPI_ADAPTER_IRQ_CALLBACK = HPI_FUNC_ID(ADAPTER, 20),
	HPI_ADAPTER_DELETE = HPI_FUNC_ID(ADAPTER, 21),
	HPI_ADAPTER_READ_FLASH = HPI_FUNC_ID(ADAPTER, 22),
	HPI_ADAPTER_END_FLASH = HPI_FUNC_ID(ADAPTER, 23),
	HPI_ADAPTER_FILESTORE_DELETE_ALL = HPI_FUNC_ID(ADAPTER, 24),
#define HPI_ADAPTER_FUNCTION_COUNT 24

	HPI_OSTREAM_OPEN = HPI_FUNC_ID(OSTREAM, 1),
	HPI_OSTREAM_CLOSE = HPI_FUNC_ID(OSTREAM, 2),
	HPI_OSTREAM_WRITE = HPI_FUNC_ID(OSTREAM, 3),
	HPI_OSTREAM_START = HPI_FUNC_ID(OSTREAM, 4),
	HPI_OSTREAM_STOP = HPI_FUNC_ID(OSTREAM, 5),
	HPI_OSTREAM_RESET = HPI_FUNC_ID(OSTREAM, 6),
	HPI_OSTREAM_GET_INFO = HPI_FUNC_ID(OSTREAM, 7),
	HPI_OSTREAM_QUERY_FORMAT = HPI_FUNC_ID(OSTREAM, 8),
	HPI_OSTREAM_DATA = HPI_FUNC_ID(OSTREAM, 9),
	HPI_OSTREAM_SET_VELOCITY = HPI_FUNC_ID(OSTREAM, 10),
	HPI_OSTREAM_SET_PUNCHINOUT = HPI_FUNC_ID(OSTREAM, 11),
	HPI_OSTREAM_SINEGEN = HPI_FUNC_ID(OSTREAM, 12),
	HPI_OSTREAM_ANC_RESET = HPI_FUNC_ID(OSTREAM, 13),
	HPI_OSTREAM_ANC_GET_INFO = HPI_FUNC_ID(OSTREAM, 14),
	HPI_OSTREAM_ANC_READ = HPI_FUNC_ID(OSTREAM, 15),
	HPI_OSTREAM_SET_TIMESCALE = HPI_FUNC_ID(OSTREAM, 16),
	HPI_OSTREAM_SET_FORMAT = HPI_FUNC_ID(OSTREAM, 17),
	HPI_OSTREAM_HOSTBUFFER_ALLOC = HPI_FUNC_ID(OSTREAM, 18),
	HPI_OSTREAM_HOSTBUFFER_FREE = HPI_FUNC_ID(OSTREAM, 19),
	HPI_OSTREAM_GROUP_ADD = HPI_FUNC_ID(OSTREAM, 20),
	HPI_OSTREAM_GROUP_GETMAP = HPI_FUNC_ID(OSTREAM, 21),
	HPI_OSTREAM_GROUP_RESET = HPI_FUNC_ID(OSTREAM, 22),
	HPI_OSTREAM_HOSTBUFFER_GET_INFO = HPI_FUNC_ID(OSTREAM, 23),
	HPI_OSTREAM_WAIT_START = HPI_FUNC_ID(OSTREAM, 24),
	HPI_OSTREAM_WAIT = HPI_FUNC_ID(OSTREAM, 25),
#define HPI_OSTREAM_FUNCTION_COUNT 25

	HPI_ISTREAM_OPEN = HPI_FUNC_ID(ISTREAM, 1),
	HPI_ISTREAM_CLOSE = HPI_FUNC_ID(ISTREAM, 2),
	HPI_ISTREAM_SET_FORMAT = HPI_FUNC_ID(ISTREAM, 3),
	HPI_ISTREAM_READ = HPI_FUNC_ID(ISTREAM, 4),
	HPI_ISTREAM_START = HPI_FUNC_ID(ISTREAM, 5),
	HPI_ISTREAM_STOP = HPI_FUNC_ID(ISTREAM, 6),
	HPI_ISTREAM_RESET = HPI_FUNC_ID(ISTREAM, 7),
	HPI_ISTREAM_GET_INFO = HPI_FUNC_ID(ISTREAM, 8),
	HPI_ISTREAM_QUERY_FORMAT = HPI_FUNC_ID(ISTREAM, 9),
	HPI_ISTREAM_ANC_RESET = HPI_FUNC_ID(ISTREAM, 10),
	HPI_ISTREAM_ANC_GET_INFO = HPI_FUNC_ID(ISTREAM, 11),
	HPI_ISTREAM_ANC_WRITE = HPI_FUNC_ID(ISTREAM, 12),
	HPI_ISTREAM_HOSTBUFFER_ALLOC = HPI_FUNC_ID(ISTREAM, 13),
	HPI_ISTREAM_HOSTBUFFER_FREE = HPI_FUNC_ID(ISTREAM, 14),
	HPI_ISTREAM_GROUP_ADD = HPI_FUNC_ID(ISTREAM, 15),
	HPI_ISTREAM_GROUP_GETMAP = HPI_FUNC_ID(ISTREAM, 16),
	HPI_ISTREAM_GROUP_RESET = HPI_FUNC_ID(ISTREAM, 17),
	HPI_ISTREAM_HOSTBUFFER_GET_INFO = HPI_FUNC_ID(ISTREAM, 18),
	HPI_ISTREAM_WAIT_START = HPI_FUNC_ID(ISTREAM, 19),
	HPI_ISTREAM_WAIT = HPI_FUNC_ID(ISTREAM, 20),
#define HPI_ISTREAM_FUNCTION_COUNT 20

/* NOTE:
   GET_NODE_INFO, SET_CONNECTION, GET_CONNECTIONS are not currently used */
	HPI_MIXER_OPEN = HPI_FUNC_ID(MIXER, 1),
	HPI_MIXER_CLOSE = HPI_FUNC_ID(MIXER, 2),
	HPI_MIXER_GET_INFO = HPI_FUNC_ID(MIXER, 3),
	HPI_MIXER_GET_NODE_INFO = HPI_FUNC_ID(MIXER, 4),
	HPI_MIXER_GET_CONTROL = HPI_FUNC_ID(MIXER, 5),
	HPI_MIXER_SET_CONNECTION = HPI_FUNC_ID(MIXER, 6),
	HPI_MIXER_GET_CONNECTIONS = HPI_FUNC_ID(MIXER, 7),
	HPI_MIXER_GET_CONTROL_BY_INDEX = HPI_FUNC_ID(MIXER, 8),
	HPI_MIXER_GET_CONTROL_ARRAY_BY_INDEX = HPI_FUNC_ID(MIXER, 9),
	HPI_MIXER_GET_CONTROL_MULTIPLE_VALUES = HPI_FUNC_ID(MIXER, 10),
	HPI_MIXER_STORE = HPI_FUNC_ID(MIXER, 11),
	HPI_MIXER_GET_CACHE_INFO = HPI_FUNC_ID(MIXER, 12),
	HPI_MIXER_GET_BLOCK_HANDLE = HPI_FUNC_ID(MIXER, 13),
	HPI_MIXER_GET_PARAMETER_HANDLE = HPI_FUNC_ID(MIXER, 14),
#define HPI_MIXER_FUNCTION_COUNT 14

	HPI_CONTROL_GET_INFO = HPI_FUNC_ID(CONTROL, 1),
	HPI_CONTROL_GET_STATE = HPI_FUNC_ID(CONTROL, 2),
	HPI_CONTROL_SET_STATE = HPI_FUNC_ID(CONTROL, 3),
#define HPI_CONTROL_FUNCTION_COUNT 3

	HPI_NVMEMORY_OPEN = HPI_FUNC_ID(NVMEMORY, 1),
	HPI_NVMEMORY_READ_BYTE = HPI_FUNC_ID(NVMEMORY, 2),
	HPI_NVMEMORY_WRITE_BYTE = HPI_FUNC_ID(NVMEMORY, 3),
#define HPI_NVMEMORY_FUNCTION_COUNT 3

	HPI_GPIO_OPEN = HPI_FUNC_ID(GPIO, 1),
	HPI_GPIO_READ_BIT = HPI_FUNC_ID(GPIO, 2),
	HPI_GPIO_WRITE_BIT = HPI_FUNC_ID(GPIO, 3),
	HPI_GPIO_READ_ALL = HPI_FUNC_ID(GPIO, 4),
	HPI_GPIO_WRITE_STATUS = HPI_FUNC_ID(GPIO, 5),
#define HPI_GPIO_FUNCTION_COUNT 5

	HPI_ASYNCEVENT_OPEN = HPI_FUNC_ID(ASYNCEVENT, 1),
	HPI_ASYNCEVENT_CLOSE = HPI_FUNC_ID(ASYNCEVENT, 2),
	HPI_ASYNCEVENT_WAIT = HPI_FUNC_ID(ASYNCEVENT, 3),
	HPI_ASYNCEVENT_GETCOUNT = HPI_FUNC_ID(ASYNCEVENT, 4),
	HPI_ASYNCEVENT_GET = HPI_FUNC_ID(ASYNCEVENT, 5),
	HPI_ASYNCEVENT_SENDEVENTS = HPI_FUNC_ID(ASYNCEVENT, 6),
#define HPI_ASYNCEVENT_FUNCTION_COUNT 6

	HPI_WATCHDOG_OPEN = HPI_FUNC_ID(WATCHDOG, 1),
	HPI_WATCHDOG_SET_TIME = HPI_FUNC_ID(WATCHDOG, 2),
	HPI_WATCHDOG_PING = HPI_FUNC_ID(WATCHDOG, 3),

	HPI_CLOCK_OPEN = HPI_FUNC_ID(CLOCK, 1),
	HPI_CLOCK_SET_TIME = HPI_FUNC_ID(CLOCK, 2),
	HPI_CLOCK_GET_TIME = HPI_FUNC_ID(CLOCK, 3),

	HPI_PROFILE_OPEN_ALL = HPI_FUNC_ID(PROFILE, 1),
	HPI_PROFILE_START_ALL = HPI_FUNC_ID(PROFILE, 2),
	HPI_PROFILE_STOP_ALL = HPI_FUNC_ID(PROFILE, 3),
	HPI_PROFILE_GET = HPI_FUNC_ID(PROFILE, 4),
	HPI_PROFILE_GET_IDLECOUNT = HPI_FUNC_ID(PROFILE, 5),
	HPI_PROFILE_GET_NAME = HPI_FUNC_ID(PROFILE, 6),
	HPI_PROFILE_GET_UTILIZATION = HPI_FUNC_ID(PROFILE, 7)
#define HPI_PROFILE_FUNCTION_COUNT 7
};

/* ////////////////////////////////////////////////////////////////////// */
/* STRUCTURES */
#ifndef DISABLE_PRAGMA_PACK1
#pragma pack(push, 1)
#endif

/** PCI bus resource */
struct hpi_pci {
	u32 __iomem *ap_mem_base[HPI_MAX_ADAPTER_MEM_SPACES];
	struct pci_dev *pci_dev;
};

struct hpi_resource {
	union {
		const struct hpi_pci *pci;
		const char *net_if;
	} r;
#ifndef HPI64BIT		/* keep structure size constant */
	u32 pad_to64;
#endif
	u16 bus_type;		/* HPI_BUS_PNPISA, _PCI, _USB etc */
	u16 padding;

};

/** Format info used inside struct hpi_message
    Not the same as public API struct hpi_format */
struct hpi_msg_format {
	u32 sample_rate; /**< 11025, 32000, 44100 etc. */
	u32 bit_rate; /**< for MPEG */
	u32 attributes;	/**< stereo/joint_stereo/mono */
	u16 channels; /**< 1,2..., (or ancillary mode or idle bit */
	u16 format; /**< HPI_FORMAT_PCM16, _MPEG etc. see \ref HPI_FORMATS. */
};

/**  Buffer+format structure.
	 Must be kept 7 * 32 bits to match public struct hpi_datastruct */
struct hpi_msg_data {
	struct hpi_msg_format format;
	u8 *pb_data;
#ifndef HPI64BIT
	u32 padding;
#endif
	u32 data_size;
};

/** struct hpi_datastructure used up to 3.04 driver */
struct hpi_data_legacy32 {
	struct hpi_format format;
	u32 pb_data;
	u32 data_size;
};

#ifdef HPI64BIT
/* Compatibility version of struct hpi_data*/
struct hpi_data_compat32 {
	struct hpi_msg_format format;
	u32 pb_data;
	u32 padding;
	u32 data_size;
};
#endif

struct hpi_buffer {
  /** placeholder for backward compatibility (see dwBufferSize) */
	struct hpi_msg_format reserved;
	u32 command; /**< HPI_BUFFER_CMD_xxx*/
	u32 pci_address; /**< PCI physical address of buffer for DSP DMA */
	u32 buffer_size; /**< must line up with data_size of HPI_DATA*/
};

/*/////////////////////////////////////////////////////////////////////////// */
/* This is used for background buffer bus mastering stream buffers.           */
struct hpi_hostbuffer_status {
	u32 samples_processed;
	u32 auxiliary_data_available;
	u32 stream_state;
	/* DSP index in to the host bus master buffer. */
	u32 dsp_index;
	/* Host index in to the host bus master buffer. */
	u32 host_index;
	u32 size_in_bytes;
};

struct hpi_streamid {
	u16 object_type;
		    /**< Type of object, HPI_OBJ_OSTREAM or HPI_OBJ_ISTREAM. */
	u16 stream_index; /**< outstream or instream index. */
};

struct hpi_punchinout {
	u32 punch_in_sample;
	u32 punch_out_sample;
};

struct hpi_subsys_msg {
	struct hpi_resource resource;
};

struct hpi_subsys_res {
	u32 version;
	u32 data;		/* extended version */
	u16 num_adapters;
	u16 adapter_index;
	u16 adapter_type;
	u16 pad16;
};

union hpi_adapterx_msg {
	struct {
		u32 dsp_address;
		u32 count_bytes;
	} debug_read;
	struct {
		u32 adapter_mode;
		u16 query_or_set;
	} mode;
	struct {
		u16 index;
	} module_info;
	struct {
		u16 index;
		u16 what;
		u16 property_index;
	} property_enum;
	struct {
		u16 property;
		u16 parameter1;
		u16 parameter2;
	} property_set;
	struct {
		u32 pad32;
		u16 key1;
		u16 key2;
	} restart;
	struct {
		u32 pad32;
		u16 value;
	} test_assert;
	struct {
		u32 yes;
	} irq_query;
	u32 pad[3];
};

struct hpi_adapter_res {
	u32 serial_number;
	u16 adapter_type;
	u16 adapter_index;
	u16 num_instreams;
	u16 num_outstreams;
	u16 num_mixers;
	u16 version;
	u8 sz_adapter_assert[HPI_STRING_LEN];
};

union hpi_adapterx_res {
	struct hpi_adapter_res info;
	struct {
		u32 p1;
		u16 count;
		u16 dsp_index;
		u32 p2;
		u32 dsp_msg_addr;
		char sz_message[HPI_STRING_LEN];
	} assert;
	struct {
		u32 adapter_mode;
	} mode;
	struct {
		u16 parameter1;
		u16 parameter2;
	} property_get;
	struct {
		u32 yes;
	} irq_query;
};

struct hpi_stream_msg {
	union {
		struct hpi_msg_data data;
		struct hpi_data_legacy32 data32;
		u16 velocity;
		struct hpi_punchinout pio;
		u32 time_scale;
		struct hpi_buffer buffer;
		struct hpi_streamid stream;
		u32 threshold_bytes;
	} u;
};

struct hpi_stream_res {
	union {
		struct {
			/* size of hardware buffer */
			u32 buffer_size;
			/* OutStream - data to play,
			   InStream - data recorded */
			u32 data_available;
			/* OutStream - samples played,
			   InStream - samples recorded */
			u32 samples_transferred;
			/* Adapter - OutStream - data to play,
			   InStream - data recorded */
			u32 auxiliary_data_available;
			u16 state;	/* HPI_STATE_PLAYING, _STATE_STOPPED */
			u16 padding;
		} stream_info;
		struct {
			u32 buffer_size;
			u32 data_available;
			u32 samples_transfered;
			u16 state;
			u16 outstream_index;
			u16 instream_index;
			u16 padding;
			u32 auxiliary_data_available;
		} legacy_stream_info;
		struct {
			/* bitmap of grouped OutStreams */
			u32 outstream_group_map;
			/* bitmap of grouped InStreams */
			u32 instream_group_map;
		} group_info;
		struct {
			/* pointer to the buffer */
			u8 *p_buffer;
			/* pointer to the hostbuffer status */
			struct hpi_hostbuffer_status *p_status;
		} hostbuffer_info;
	} u;
};

struct hpi_mixer_msg {
	u16 control_index;
	u16 control_type;	/* = HPI_CONTROL_METER _VOLUME etc */
	u16 padding1;		/* Maintain alignment of subsequent fields */
	u16 node_type1;		/* = HPI_SOURCENODE_LINEIN etc */
	u16 node_index1;	/* = 0..N */
	u16 node_type2;
	u16 node_index2;
	u16 padding2;		/* round to 4 bytes */
};

struct hpi_mixer_res {
	u16 src_node_type;	/* = HPI_SOURCENODE_LINEIN etc */
	u16 src_node_index;	/* = 0..N */
	u16 dst_node_type;
	u16 dst_node_index;
	/* Also controlType for MixerGetControlByIndex */
	u16 control_index;
	/* may indicate which DSP the control is located on */
	u16 dsp_index;
};

union hpi_mixerx_msg {
	struct {
		u16 starting_index;
		u16 flags;
		u32 length_in_bytes;	/* length in bytes of p_data */
		u32 p_data;	/* pointer to a data array */
	} gcabi;
	struct {
		u16 command;
		u16 index;
	} store;		/* for HPI_MIXER_STORE message */
};

union hpi_mixerx_res {
	struct {
		u32 bytes_returned;	/* size of items returned */
		u32 p_data;	/* pointer to data array */
		u16 more_to_do;	/* indicates if there is more to do */
	} gcabi;
	struct {
		u32 total_controls;	/* count of controls in the mixer */
		u32 cache_controls;	/* count of controls in the cac */
		u32 cache_bytes;	/* size of cache */
	} cache_info;
};

struct hpi_control_msg {
	u16 attribute;		/* control attribute or property */
	u16 saved_index;
	u32 param1;		/* generic parameter 1 */
	u32 param2;		/* generic parameter 2 */
	short an_log_value[HPI_MAX_CHANNELS];
};

struct hpi_control_union_msg {
	u16 attribute;		/* control attribute or property */
	u16 saved_index;	/* only used in ctrl save/restore */
	union {
		struct {
			u32 param1;	/* generic parameter 1 */
			u32 param2;	/* generic parameter 2 */
			short an_log_value[HPI_MAX_CHANNELS];
		} old;
		union {
			u32 frequency;
			u32 gain;
			u32 band;
			u32 deemphasis;
			u32 program;
			struct {
				u32 mode;
				u32 value;
			} mode;
			u32 blend;
		} tuner;
	} u;
};

struct hpi_control_res {
	/* Could make union. dwParam, anLogValue never used in same response */
	u32 param1;
	u32 param2;
	short an_log_value[HPI_MAX_CHANNELS];
};

union hpi_control_union_res {
	struct {
		u32 param1;
		u32 param2;
		short an_log_value[HPI_MAX_CHANNELS];
	} old;
	union {
		u32 band;
		u32 frequency;
		u32 gain;
		u32 deemphasis;
		struct {
			u32 data[2];
			u32 bLER;
		} rds;
		short s_level;
		struct {
			u16 value;
			u16 mask;
		} status;
	} tuner;
	struct {
		char sz_data[8];
		u32 remaining_chars;
	} chars8;
	char c_data12[12];
	union {
		struct {
			u32 status;
			u32 readable_size;
			u32 writeable_size;
		} status;
	} cobranet;
};

struct hpi_nvmemory_msg {
	u16 address;
	u16 data;
};

struct hpi_nvmemory_res {
	u16 size_in_bytes;
	u16 data;
};

struct hpi_gpio_msg {
	u16 bit_index;
	u16 bit_data;
};

struct hpi_gpio_res {
	u16 number_input_bits;
	u16 number_output_bits;
	u16 bit_data[4];
};

struct hpi_async_msg {
	u32 events;
	u16 maximum_events;
	u16 padding;
};

struct hpi_async_res {
	union {
		struct {
			u16 count;
		} count;
		struct {
			u32 events;
			u16 number_returned;
			u16 padding;
		} get;
		struct hpi_async_event event;
	} u;
};

struct hpi_watchdog_msg {
	u32 time_ms;
};

struct hpi_watchdog_res {
	u32 time_ms;
};

struct hpi_clock_msg {
	u16 hours;
	u16 minutes;
	u16 seconds;
	u16 milli_seconds;
};

struct hpi_clock_res {
	u16 size_in_bytes;
	u16 hours;
	u16 minutes;
	u16 seconds;
	u16 milli_seconds;
	u16 padding;
};

struct hpi_profile_msg {
	u16 bin_index;
	u16 padding;
};

struct hpi_profile_res_open {
	u16 max_profiles;
};

struct hpi_profile_res_time {
	u32 total_tick_count;
	u32 call_count;
	u32 max_tick_count;
	u32 ticks_per_millisecond;
	u16 profile_interval;
};

struct hpi_profile_res_name {
	u8 sz_name[32];
};

struct hpi_profile_res {
	union {
		struct hpi_profile_res_open o;
		struct hpi_profile_res_time t;
		struct hpi_profile_res_name n;
	} u;
};

struct hpi_message_header {
	u16 size;		/* total size in bytes */
	u8 type;		/* HPI_TYPE_MESSAGE  */
	u8 version;		/* message version */
	u16 object;		/* HPI_OBJ_* */
	u16 function;		/* HPI_SUBSYS_xxx, HPI_ADAPTER_xxx */
	u16 adapter_index;	/* the adapter index */
	u16 obj_index;		/* */
};

struct hpi_message {
	/* following fields must match HPI_MESSAGE_HEADER */
	u16 size;		/* total size in bytes */
	u8 type;		/* HPI_TYPE_MESSAGE  */
	u8 version;		/* message version */
	u16 object;		/* HPI_OBJ_* */
	u16 function;		/* HPI_SUBSYS_xxx, HPI_ADAPTER_xxx */
	u16 adapter_index;	/* the adapter index */
	u16 obj_index;		/*  */
	union {
		struct hpi_subsys_msg s;
		union hpi_adapterx_msg ax;
		struct hpi_stream_msg d;
		struct hpi_mixer_msg m;
		union hpi_mixerx_msg mx;	/* extended mixer; */
		struct hpi_control_msg c;	/* mixer control; */
		/* identical to struct hpi_control_msg,
		   but field naming is improved */
		struct hpi_control_union_msg cu;
		struct hpi_nvmemory_msg n;
		struct hpi_gpio_msg l;	/* digital i/o */
		struct hpi_watchdog_msg w;
		struct hpi_clock_msg t;	/* dsp time */
		struct hpi_profile_msg p;
		struct hpi_async_msg as;
		char fixed_size[32];
	} u;
};

#define HPI_MESSAGE_SIZE_BY_OBJECT { \
	sizeof(struct hpi_message_header) ,   /* Default, no object type 0 */ \
	sizeof(struct hpi_message_header) + sizeof(struct hpi_subsys_msg),\
	sizeof(struct hpi_message_header) + sizeof(union hpi_adapterx_msg),\
	sizeof(struct hpi_message_header) + sizeof(struct hpi_stream_msg),\
	sizeof(struct hpi_message_header) + sizeof(struct hpi_stream_msg),\
	sizeof(struct hpi_message_header) + sizeof(struct hpi_mixer_msg),\
	sizeof(struct hpi_message_header) ,   /* no node message */ \
	sizeof(struct hpi_message_header) + sizeof(struct hpi_control_msg),\
	sizeof(struct hpi_message_header) + sizeof(struct hpi_nvmemory_msg),\
	sizeof(struct hpi_message_header) + sizeof(struct hpi_gpio_msg),\
	sizeof(struct hpi_message_header) + sizeof(struct hpi_watchdog_msg),\
	sizeof(struct hpi_message_header) + sizeof(struct hpi_clock_msg),\
	sizeof(struct hpi_message_header) + sizeof(struct hpi_profile_msg),\
	sizeof(struct hpi_message_header), /* controlx obj removed */ \
	sizeof(struct hpi_message_header) + sizeof(struct hpi_async_msg) \
}

/*
Note that the wSpecificError error field should be inspected and potentially
reported whenever HPI_ERROR_DSP_COMMUNICATION or HPI_ERROR_DSP_BOOTLOAD is
returned in wError.
*/
struct hpi_response_header {
	u16 size;
	u8 type;		/* HPI_TYPE_RESPONSE  */
	u8 version;		/* response version */
	u16 object;		/* HPI_OBJ_* */
	u16 function;		/* HPI_SUBSYS_xxx, HPI_ADAPTER_xxx */
	u16 error;		/* HPI_ERROR_xxx */
	u16 specific_error;	/* adapter specific error */
};

struct hpi_response {
/* following fields must match HPI_RESPONSE_HEADER */
	u16 size;
	u8 type;		/* HPI_TYPE_RESPONSE  */
	u8 version;		/* response version */
	u16 object;		/* HPI_OBJ_* */
	u16 function;		/* HPI_SUBSYS_xxx, HPI_ADAPTER_xxx */
	u16 error;		/* HPI_ERROR_xxx */
	u16 specific_error;	/* adapter specific error */
	union {
		struct hpi_subsys_res s;
		union hpi_adapterx_res ax;
		struct hpi_stream_res d;
		struct hpi_mixer_res m;
		union hpi_mixerx_res mx;	/* extended mixer; */
		struct hpi_control_res c;	/* mixer control; */
		/* identical to hpi_control_res, but field naming is improved */
		union hpi_control_union_res cu;
		struct hpi_nvmemory_res n;
		struct hpi_gpio_res l;	/* digital i/o */
		struct hpi_watchdog_res w;
		struct hpi_clock_res t;	/* dsp time */
		struct hpi_profile_res p;
		struct hpi_async_res as;
		u8 bytes[52];
	} u;
};

#define HPI_RESPONSE_SIZE_BY_OBJECT { \
	sizeof(struct hpi_response_header) ,/* Default, no object type 0 */ \
	sizeof(struct hpi_response_header) + sizeof(struct hpi_subsys_res),\
	sizeof(struct hpi_response_header) + sizeof(union  hpi_adapterx_res),\
	sizeof(struct hpi_response_header) + sizeof(struct hpi_stream_res),\
	sizeof(struct hpi_response_header) + sizeof(struct hpi_stream_res),\
	sizeof(struct hpi_response_header) + sizeof(struct hpi_mixer_res),\
	sizeof(struct hpi_response_header) , /* no node response */ \
	sizeof(struct hpi_response_header) + sizeof(struct hpi_control_res),\
	sizeof(struct hpi_response_header) + sizeof(struct hpi_nvmemory_res),\
	sizeof(struct hpi_response_header) + sizeof(struct hpi_gpio_res),\
	sizeof(struct hpi_response_header) + sizeof(struct hpi_watchdog_res),\
	sizeof(struct hpi_response_header) + sizeof(struct hpi_clock_res),\
	sizeof(struct hpi_response_header) + sizeof(struct hpi_profile_res),\
	sizeof(struct hpi_response_header), /* controlx obj removed */ \
	sizeof(struct hpi_response_header) + sizeof(struct hpi_async_res) \
}

/*********************** version 1 message/response **************************/
#define HPINET_ETHERNET_DATA_SIZE (1500)
#define HPINET_IP_HDR_SIZE (20)
#define HPINET_IP_DATA_SIZE (HPINET_ETHERNET_DATA_SIZE - HPINET_IP_HDR_SIZE)
#define HPINET_UDP_HDR_SIZE (8)
#define HPINET_UDP_DATA_SIZE (HPINET_IP_DATA_SIZE - HPINET_UDP_HDR_SIZE)
#define HPINET_ASI_HDR_SIZE (2)
#define HPINET_ASI_DATA_SIZE (HPINET_UDP_DATA_SIZE - HPINET_ASI_HDR_SIZE)

#define HPI_MAX_PAYLOAD_SIZE (HPINET_ASI_DATA_SIZE - 2)

/* New style message/response, but still V0 compatible */
struct hpi_msg_adapter_get_info {
	struct hpi_message_header h;
};

struct hpi_res_adapter_get_info {
	struct hpi_response_header h;	/*v0 */
	struct hpi_adapter_res p;
};

struct hpi_res_adapter_debug_read {
	struct hpi_response_header h;
	u8 bytes[1024];
};

struct hpi_msg_cobranet_hmi {
	u16 attribute;
	u16 padding;
	u32 hmi_address;
	u32 byte_count;
};

struct hpi_msg_cobranet_hmiwrite {
	struct hpi_message_header h;
	struct hpi_msg_cobranet_hmi p;
	u8 bytes[256];
};

struct hpi_msg_cobranet_hmiread {
	struct hpi_message_header h;
	struct hpi_msg_cobranet_hmi p;
};

struct hpi_res_cobranet_hmiread {
	struct hpi_response_header h;
	u32 byte_count;
	u8 bytes[256];
};

#if 1
#define hpi_message_header_v1 hpi_message_header
#define hpi_response_header_v1 hpi_response_header
#else
/* V1 headers in Addition to v0 headers */
struct hpi_message_header_v1 {
	struct hpi_message_header h0;
/* struct {
} h1; */
};

struct hpi_response_header_v1 {
	struct hpi_response_header h0;
	struct {
		u16 adapter_index;	/* the adapter index */
		u16 obj_index;	/* object index */
	} h1;
};
#endif

struct hpi_msg_payload_v0 {
	struct hpi_message_header h;
	union {
		struct hpi_subsys_msg s;
		union hpi_adapterx_msg ax;
		struct hpi_stream_msg d;
		struct hpi_mixer_msg m;
		union hpi_mixerx_msg mx;
		struct hpi_control_msg c;
		struct hpi_control_union_msg cu;
		struct hpi_nvmemory_msg n;
		struct hpi_gpio_msg l;
		struct hpi_watchdog_msg w;
		struct hpi_clock_msg t;
		struct hpi_profile_msg p;
		struct hpi_async_msg as;
	} u;
};

struct hpi_res_payload_v0 {
	struct hpi_response_header h;
	union {
		struct hpi_subsys_res s;
		union hpi_adapterx_res ax;
		struct hpi_stream_res d;
		struct hpi_mixer_res m;
		union hpi_mixerx_res mx;
		struct hpi_control_res c;
		union hpi_control_union_res cu;
		struct hpi_nvmemory_res n;
		struct hpi_gpio_res l;
		struct hpi_watchdog_res w;
		struct hpi_clock_res t;
		struct hpi_profile_res p;
		struct hpi_async_res as;
	} u;
};

union hpi_message_buffer_v1 {
	struct hpi_message m0;	/* version 0 */
	struct hpi_message_header_v1 h;
	u8 buf[HPI_MAX_PAYLOAD_SIZE];
};

union hpi_response_buffer_v1 {
	struct hpi_response r0;	/* version 0 */
	struct hpi_response_header_v1 h;
	u8 buf[HPI_MAX_PAYLOAD_SIZE];
};

compile_time_assert((sizeof(union hpi_message_buffer_v1) <=
		HPI_MAX_PAYLOAD_SIZE), message_buffer_ok);
compile_time_assert((sizeof(union hpi_response_buffer_v1) <=
		HPI_MAX_PAYLOAD_SIZE), response_buffer_ok);

/*////////////////////////////////////////////////////////////////////////// */
/* declarations for compact control calls  */
struct hpi_control_defn {
	u8 type;
	u8 channels;
	u8 src_node_type;
	u8 src_node_index;
	u8 dest_node_type;
	u8 dest_node_index;
};

/*////////////////////////////////////////////////////////////////////////// */
/* declarations for control caching (internal to HPI<->DSP interaction)      */

/** indicates a cached u16 value is invalid. */
#define HPI_CACHE_INVALID_UINT16 0xFFFF
/** indicates a cached short value is invalid. */
#define HPI_CACHE_INVALID_SHORT -32768

/** A compact representation of (part of) a controls state.
Used for efficient transfer of the control state
between DSP and host or across a network
*/
struct hpi_control_cache_info {
	/** one of HPI_CONTROL_* */
	u8 control_type;
	/** The total size of cached information in 32-bit words. */
	u8 size_in32bit_words;
	/** The original index of the control on the DSP */
	u16 control_index;
};

struct hpi_control_cache_vol {
	struct hpi_control_cache_info i;
	short an_log[2];
	unsigned short flags;
	char padding[2];
};

struct hpi_control_cache_meter {
	struct hpi_control_cache_info i;
	short an_log_peak[2];
	short an_logRMS[2];
};

struct hpi_control_cache_channelmode {
	struct hpi_control_cache_info i;
	u16 mode;
	char temp_padding[6];
};

struct hpi_control_cache_mux {
	struct hpi_control_cache_info i;
	u16 source_node_type;
	u16 source_node_index;
	char temp_padding[4];
};

struct hpi_control_cache_level {
	struct hpi_control_cache_info i;
	short an_log[2];
	char temp_padding[4];
};

struct hpi_control_cache_tuner {
	struct hpi_control_cache_info i;
	u32 freq_ink_hz;
	u16 band;
	short s_level_avg;
};

struct hpi_control_cache_aes3rx {
	struct hpi_control_cache_info i;
	u32 error_status;
	u32 format;
};

struct hpi_control_cache_aes3tx {
	struct hpi_control_cache_info i;
	u32 format;
	char temp_padding[4];
};

struct hpi_control_cache_tonedetector {
	struct hpi_control_cache_info i;
	u16 state;
	char temp_padding[6];
};

struct hpi_control_cache_silencedetector {
	struct hpi_control_cache_info i;
	u32 state;
	char temp_padding[4];
};

struct hpi_control_cache_sampleclock {
	struct hpi_control_cache_info i;
	u16 source;
	u16 source_index;
	u32 sample_rate;
};

struct hpi_control_cache_microphone {
	struct hpi_control_cache_info i;
	u16 phantom_state;
	char temp_padding[6];
};

struct hpi_control_cache_single {
	union {
		struct hpi_control_cache_info i;
		struct hpi_control_cache_vol vol;
		struct hpi_control_cache_meter meter;
		struct hpi_control_cache_channelmode mode;
		struct hpi_control_cache_mux mux;
		struct hpi_control_cache_level level;
		struct hpi_control_cache_tuner tuner;
		struct hpi_control_cache_aes3rx aes3rx;
		struct hpi_control_cache_aes3tx aes3tx;
		struct hpi_control_cache_tonedetector tone;
		struct hpi_control_cache_silencedetector silence;
		struct hpi_control_cache_sampleclock clk;
		struct hpi_control_cache_microphone microphone;
	} u;
};

struct hpi_control_cache_pad {
	struct hpi_control_cache_info i;
	u32 field_valid_flags;
	u8 c_channel[8];
	u8 c_artist[40];
	u8 c_title[40];
	u8 c_comment[200];
	u32 pTY;
	u32 pI;
	u32 traffic_supported;
	u32 traffic_anouncement;
};

/* 2^N sized FIFO buffer (internal to HPI<->DSP interaction) */
struct hpi_fifo_buffer {
	u32 size;
	u32 dsp_index;
	u32 host_index;
};

#ifndef DISABLE_PRAGMA_PACK1
#pragma pack(pop)
#endif

/* skip host side function declarations for DSP
   compile and documentation extraction */

char hpi_handle_object(const u32 handle);

void hpi_handle_to_indexes(const u32 handle, u16 *pw_adapter_index,
	u16 *pw_object_index);

u32 hpi_indexes_to_handle(const char c_object, const u16 adapter_index,
	const u16 object_index);

/*////////////////////////////////////////////////////////////////////////// */

/* main HPI entry point */
void hpi_send_recv(struct hpi_message *phm, struct hpi_response *phr);

/* used in PnP OS/driver */
u16 hpi_subsys_create_adapter(const struct hpi_resource *p_resource,
	u16 *pw_adapter_index);

u16 hpi_outstream_host_buffer_get_info(u32 h_outstream, u8 **pp_buffer,
	struct hpi_hostbuffer_status **pp_status);

u16 hpi_instream_host_buffer_get_info(u32 h_instream, u8 **pp_buffer,
	struct hpi_hostbuffer_status **pp_status);

u16 hpi_adapter_restart(u16 adapter_index);

/*
The following 3 functions were last declared in header files for
driver 3.10. HPI_ControlQuery() used to be the recommended way
of getting a volume range. Declared here for binary asihpi32.dll
compatibility.
*/

void hpi_format_to_msg(struct hpi_msg_format *pMF,
	const struct hpi_format *pF);
void hpi_stream_response_to_legacy(struct hpi_stream_res *pSR);

/*////////////////////////////////////////////////////////////////////////// */
/* declarations for individual HPI entry points */
hpi_handler_func HPI_6000;
hpi_handler_func HPI_6205;

#endif				/* _HPI_INTERNAL_H_ */
