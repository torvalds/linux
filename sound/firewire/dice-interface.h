#ifndef SOUND_FIREWIRE_DICE_INTERFACE_H_INCLUDED
#define SOUND_FIREWIRE_DICE_INTERFACE_H_INCLUDED

/*
 * DICE device interface definitions
 */

/*
 * Generally, all registers can be read like memory, i.e., with quadlet read or
 * block read transactions with at least quadlet-aligned offset and length.
 * Writes are not allowed except where noted; quadlet-sized registers must be
 * written with a quadlet write transaction.
 *
 * All values are in big endian.  The DICE firmware runs on a little-endian CPU
 * and just byte-swaps _all_ quadlets on the bus, so values without endianness
 * (e.g. strings) get scrambled and must be byte-swapped again by the driver.
 */

/*
 * Streaming is handled by the "DICE driver" interface.  Its registers are
 * located in this private address space.
 */
#define DICE_PRIVATE_SPACE		0xffffe0000000uLL

/*
 * The registers are organized in several sections, which are organized
 * separately to allow them to be extended individually.  Whether a register is
 * supported can be detected by checking its offset against its section's size.
 *
 * The section offset values are relative to DICE_PRIVATE_SPACE; the offset/
 * size values are measured in quadlets.  Read-only.
 */
#define DICE_GLOBAL_OFFSET		0x00
#define DICE_GLOBAL_SIZE		0x04
#define DICE_TX_OFFSET			0x08
#define DICE_TX_SIZE			0x0c
#define DICE_RX_OFFSET			0x10
#define DICE_RX_SIZE			0x14
#define DICE_EXT_SYNC_OFFSET		0x18
#define DICE_EXT_SYNC_SIZE		0x1c
#define DICE_UNUSED2_OFFSET		0x20
#define DICE_UNUSED2_SIZE		0x24

/*
 * Global settings.
 */

/*
 * Stores the full 64-bit address (node ID and offset in the node's address
 * space) where the device will send notifications.  Must be changed with
 * a compare/swap transaction by the owner.  This register is automatically
 * cleared on a bus reset.
 */
#define GLOBAL_OWNER			0x000
#define  OWNER_NO_OWNER			0xffff000000000000uLL
#define  OWNER_NODE_SHIFT		48

/*
 * A bitmask with asynchronous events; read-only.  When any event(s) happen,
 * the bits of previous events are cleared, and the value of this register is
 * also written to the address stored in the owner register.
 */
#define GLOBAL_NOTIFICATION		0x008
/* Some registers in the Rx/Tx sections may have changed. */
#define  NOTIFY_RX_CFG_CHG		0x00000001
#define  NOTIFY_TX_CFG_CHG		0x00000002
/* Lock status of the current clock source may have changed. */
#define  NOTIFY_LOCK_CHG		0x00000010
/* Write to the clock select register has been finished. */
#define  NOTIFY_CLOCK_ACCEPTED		0x00000020
/* Lock status of some clock source has changed. */
#define  NOTIFY_EXT_STATUS		0x00000040
/* Other bits may be used for device-specific events. */

/*
 * A name that can be customized for each device; read/write.  Padded with zero
 * bytes.  Quadlets are byte-swapped.  The encoding is whatever the host driver
 * happens to be using.
 */
#define GLOBAL_NICK_NAME		0x00c
#define  NICK_NAME_SIZE			64

/*
 * The current sample rate and clock source; read/write.  Whether a clock
 * source or sample rate is supported is device-specific; the internal clock
 * source is always available.  Low/mid/high = up to 48/96/192 kHz.  This
 * register can be changed even while streams are running.
 */
#define GLOBAL_CLOCK_SELECT		0x04c
#define  CLOCK_SOURCE_MASK		0x000000ff
#define  CLOCK_SOURCE_AES1		0x00000000
#define  CLOCK_SOURCE_AES2		0x00000001
#define  CLOCK_SOURCE_AES3		0x00000002
#define  CLOCK_SOURCE_AES4		0x00000003
#define  CLOCK_SOURCE_AES_ANY		0x00000004
#define  CLOCK_SOURCE_ADAT		0x00000005
#define  CLOCK_SOURCE_TDIF		0x00000006
#define  CLOCK_SOURCE_WC		0x00000007
#define  CLOCK_SOURCE_ARX1		0x00000008
#define  CLOCK_SOURCE_ARX2		0x00000009
#define  CLOCK_SOURCE_ARX3		0x0000000a
#define  CLOCK_SOURCE_ARX4		0x0000000b
#define  CLOCK_SOURCE_INTERNAL		0x0000000c
#define  CLOCK_RATE_MASK		0x0000ff00
#define  CLOCK_RATE_32000		0x00000000
#define  CLOCK_RATE_44100		0x00000100
#define  CLOCK_RATE_48000		0x00000200
#define  CLOCK_RATE_88200		0x00000300
#define  CLOCK_RATE_96000		0x00000400
#define  CLOCK_RATE_176400		0x00000500
#define  CLOCK_RATE_192000		0x00000600
#define  CLOCK_RATE_ANY_LOW		0x00000700
#define  CLOCK_RATE_ANY_MID		0x00000800
#define  CLOCK_RATE_ANY_HIGH		0x00000900
#define  CLOCK_RATE_NONE		0x00000a00
#define  CLOCK_RATE_SHIFT		8

/*
 * Enable streaming; read/write.  Writing a non-zero value (re)starts all
 * streams that have a valid iso channel set; zero stops all streams.  The
 * streams' parameters must be configured before starting.  This register is
 * automatically cleared on a bus reset.
 */
#define GLOBAL_ENABLE			0x050

/*
 * Status of the sample clock; read-only.
 */
#define GLOBAL_STATUS			0x054
/* The current clock source is locked. */
#define  STATUS_SOURCE_LOCKED		0x00000001
/* The actual sample rate; CLOCK_RATE_32000-_192000 or _NONE. */
#define  STATUS_NOMINAL_RATE_MASK	0x0000ff00

/*
 * Status of all clock sources; read-only.
 */
#define GLOBAL_EXTENDED_STATUS		0x058
/*
 * The _LOCKED bits always show the current status; any change generates
 * a notification.
 */
#define  EXT_STATUS_AES1_LOCKED		0x00000001
#define  EXT_STATUS_AES2_LOCKED		0x00000002
#define  EXT_STATUS_AES3_LOCKED		0x00000004
#define  EXT_STATUS_AES4_LOCKED		0x00000008
#define  EXT_STATUS_ADAT_LOCKED		0x00000010
#define  EXT_STATUS_TDIF_LOCKED		0x00000020
#define  EXT_STATUS_ARX1_LOCKED		0x00000040
#define  EXT_STATUS_ARX2_LOCKED		0x00000080
#define  EXT_STATUS_ARX3_LOCKED		0x00000100
#define  EXT_STATUS_ARX4_LOCKED		0x00000200
#define  EXT_STATUS_WC_LOCKED		0x00000400
/*
 * The _SLIP bits do not generate notifications; a set bit indicates that an
 * error occurred since the last time when this register was read with
 * a quadlet read transaction.
 */
#define  EXT_STATUS_AES1_SLIP		0x00010000
#define  EXT_STATUS_AES2_SLIP		0x00020000
#define  EXT_STATUS_AES3_SLIP		0x00040000
#define  EXT_STATUS_AES4_SLIP		0x00080000
#define  EXT_STATUS_ADAT_SLIP		0x00100000
#define  EXT_STATUS_TDIF_SLIP		0x00200000
#define  EXT_STATUS_ARX1_SLIP		0x00400000
#define  EXT_STATUS_ARX2_SLIP		0x00800000
#define  EXT_STATUS_ARX3_SLIP		0x01000000
#define  EXT_STATUS_ARX4_SLIP		0x02000000
#define  EXT_STATUS_WC_SLIP		0x04000000

/*
 * The measured rate of the current clock source, in Hz; read-only.
 */
#define GLOBAL_SAMPLE_RATE		0x05c

/*
 * The version of the DICE driver specification that this device conforms to;
 * read-only.
 */
#define GLOBAL_VERSION			0x060

/* Some old firmware versions do not have the following global registers: */

/*
 * Supported sample rates and clock sources; read-only.
 */
#define GLOBAL_CLOCK_CAPABILITIES	0x064
#define  CLOCK_CAP_RATE_32000		0x00000001
#define  CLOCK_CAP_RATE_44100		0x00000002
#define  CLOCK_CAP_RATE_48000		0x00000004
#define  CLOCK_CAP_RATE_88200		0x00000008
#define  CLOCK_CAP_RATE_96000		0x00000010
#define  CLOCK_CAP_RATE_176400		0x00000020
#define  CLOCK_CAP_RATE_192000		0x00000040
#define  CLOCK_CAP_SOURCE_AES1		0x00010000
#define  CLOCK_CAP_SOURCE_AES2		0x00020000
#define  CLOCK_CAP_SOURCE_AES3		0x00040000
#define  CLOCK_CAP_SOURCE_AES4		0x00080000
#define  CLOCK_CAP_SOURCE_AES_ANY	0x00100000
#define  CLOCK_CAP_SOURCE_ADAT		0x00200000
#define  CLOCK_CAP_SOURCE_TDIF		0x00400000
#define  CLOCK_CAP_SOURCE_WC		0x00800000
#define  CLOCK_CAP_SOURCE_ARX1		0x01000000
#define  CLOCK_CAP_SOURCE_ARX2		0x02000000
#define  CLOCK_CAP_SOURCE_ARX3		0x04000000
#define  CLOCK_CAP_SOURCE_ARX4		0x08000000
#define  CLOCK_CAP_SOURCE_INTERNAL	0x10000000

/*
 * Names of all clock sources; read-only.  Quadlets are byte-swapped.  Names
 * are separated with one backslash, the list is terminated with two
 * backslashes.  Unused clock sources are included.
 */
#define GLOBAL_CLOCK_SOURCE_NAMES	0x068
#define  CLOCK_SOURCE_NAMES_SIZE	256

/*
 * Capture stream settings.  This section includes the number/size registers
 * and the registers of all streams.
 */

/*
 * The number of supported capture streams; read-only.
 */
#define TX_NUMBER			0x000

/*
 * The size of one stream's register block, in quadlets; read-only.  The
 * registers of the first stream follow immediately afterwards; the registers
 * of the following streams are offset by this register's value.
 */
#define TX_SIZE				0x004

/*
 * The isochronous channel number on which packets are sent, or -1 if the
 * stream is not to be used; read/write.
 */
#define TX_ISOCHRONOUS			0x008

/*
 * The number of audio channels; read-only.  There will be one quadlet per
 * channel; the first channel is the first quadlet in a data block.
 */
#define TX_NUMBER_AUDIO			0x00c

/*
 * The number of MIDI ports, 0-8; read-only.  If > 0, there will be one
 * additional quadlet in each data block, following the audio quadlets.
 */
#define TX_NUMBER_MIDI			0x010

/*
 * The speed at which the packets are sent, SCODE_100-_400; read/write.
 */
#define TX_SPEED			0x014

/*
 * Names of all audio channels; read-only.  Quadlets are byte-swapped.  Names
 * are separated with one backslash, the list is terminated with two
 * backslashes.
 */
#define TX_NAMES			0x018
#define  TX_NAMES_SIZE			256

/*
 * Audio IEC60958 capabilities; read-only.  Bitmask with one bit per audio
 * channel.
 */
#define TX_AC3_CAPABILITIES		0x118

/*
 * Send audio data with IEC60958 label; read/write.  Bitmask with one bit per
 * audio channel.  This register can be changed even while the stream is
 * running.
 */
#define TX_AC3_ENABLE			0x11c

/*
 * Playback stream settings.  This section includes the number/size registers
 * and the registers of all streams.
 */

/*
 * The number of supported playback streams; read-only.
 */
#define RX_NUMBER			0x000

/*
 * The size of one stream's register block, in quadlets; read-only.  The
 * registers of the first stream follow immediately afterwards; the registers
 * of the following streams are offset by this register's value.
 */
#define RX_SIZE				0x004

/*
 * The isochronous channel number on which packets are received, or -1 if the
 * stream is not to be used; read/write.
 */
#define RX_ISOCHRONOUS			0x008

/*
 * Index of first quadlet to be interpreted; read/write.  If > 0, that many
 * quadlets at the beginning of each data block will be ignored, and all the
 * audio and MIDI quadlets will follow.
 */
#define RX_SEQ_START			0x00c

/*
 * The number of audio channels; read-only.  There will be one quadlet per
 * channel.
 */
#define RX_NUMBER_AUDIO			0x010

/*
 * The number of MIDI ports, 0-8; read-only.  If > 0, there will be one
 * additional quadlet in each data block, following the audio quadlets.
 */
#define RX_NUMBER_MIDI			0x014

/*
 * Names of all audio channels; read-only.  Quadlets are byte-swapped.  Names
 * are separated with one backslash, the list is terminated with two
 * backslashes.
 */
#define RX_NAMES			0x018
#define  RX_NAMES_SIZE			256

/*
 * Audio IEC60958 capabilities; read-only.  Bitmask with one bit per audio
 * channel.
 */
#define RX_AC3_CAPABILITIES		0x118

/*
 * Receive audio data with IEC60958 label; read/write.  Bitmask with one bit
 * per audio channel.  This register can be changed even while the stream is
 * running.
 */
#define RX_AC3_ENABLE			0x11c

/*
 * Extended synchronization information.
 * This section can be read completely with a block read request.
 */

/*
 * Current clock source; read-only.
 */
#define EXT_SYNC_CLOCK_SOURCE		0x000

/*
 * Clock source is locked (boolean); read-only.
 */
#define EXT_SYNC_LOCKED			0x004

/*
 * Current sample rate (CLOCK_RATE_* >> CLOCK_RATE_SHIFT), _32000-_192000 or
 * _NONE; read-only.
 */
#define EXT_SYNC_RATE			0x008

/*
 * ADAT user data bits; read-only.
 */
#define EXT_SYNC_ADAT_USER_DATA		0x00c
/* The data bits, if available. */
#define  ADAT_USER_DATA_MASK		0x0f
/* The data bits are not available. */
#define  ADAT_USER_DATA_NO_DATA		0x10

#endif
