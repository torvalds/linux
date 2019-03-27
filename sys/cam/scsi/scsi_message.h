/*-
 * This file is in the public domain.
 * $FreeBSD$
 */

/* Messages (1 byte) */		     /* I/T (M)andatory or (O)ptional */
#define MSG_CMDCOMPLETE		0x00 /* M/M */
#define MSG_TASK_COMPLETE	0x00 /* M/M */ /* SPI3 Terminology */
#define MSG_EXTENDED		0x01 /* O/O */
#define MSG_SAVEDATAPOINTER	0x02 /* O/O */
#define MSG_RESTOREPOINTERS	0x03 /* O/O */
#define MSG_DISCONNECT		0x04 /* O/O */
#define MSG_INITIATOR_DET_ERR	0x05 /* M/M */
#define MSG_ABORT		0x06 /* O/M */
#define MSG_ABORT_TASK_SET	0x06 /* O/M */ /* SPI3 Terminology */
#define MSG_MESSAGE_REJECT	0x07 /* M/M */
#define MSG_NOOP		0x08 /* M/M */
#define MSG_PARITY_ERROR	0x09 /* M/M */
#define MSG_LINK_CMD_COMPLETE	0x0a /* O/O */
#define MSG_LINK_CMD_COMPLETEF	0x0b /* O/O */ /* Obsolete */
#define MSG_BUS_DEV_RESET	0x0c /* O/M */
#define MSG_TARGET_RESET	0x0c /* O/M */ /* SPI3 Terminology */
#define MSG_ABORT_TAG		0x0d /* O/O */
#define MSG_ABORT_TASK		0x0d /* O/O */ /* SPI3 Terminology */
#define MSG_CLEAR_QUEUE		0x0e /* O/O */
#define MSG_CLEAR_TASK_SET	0x0e /* O/O */ /* SPI3 Terminology */
#define MSG_INIT_RECOVERY	0x0f /* O/O */ /* Deprecated in SPI3 */
#define MSG_REL_RECOVERY	0x10 /* O/O */ /* Deprecated in SPI3 */
#define MSG_TERM_IO_PROC	0x11 /* O/O */ /* Deprecated in SPI3 */
#define MSG_CLEAR_ACA		0x16 /* O/O */ /* SPI3 */
#define MSG_LOGICAL_UNIT_RESET	0x17 /* O/O */ /* SPI3 */
#define MSG_QAS_REQUEST		0x55 /* O/O */ /* SPI3 */

/* Messages (2 byte) */
#define MSG_SIMPLE_Q_TAG	0x20 /* O/O */
#define MSG_SIMPLE_TASK		0x20 /* O/O */ /* SPI3 Terminology */
#define MSG_HEAD_OF_Q_TAG	0x21 /* O/O */
#define MSG_HEAD_OF_QUEUE_TASK	0x21 /* O/O */ /* SPI3 Terminology */
#define MSG_ORDERED_Q_TAG	0x22 /* O/O */
#define MSG_ORDERED_TASK	0x22 /* O/O */ /* SPI3 Terminology */
#define MSG_IGN_WIDE_RESIDUE	0x23 /* O/O */
#define MSG_ACA_TASK		0x24 /* 0/0 */ /* SPI3 */

/* Identify message */		     /* M/M */	
#define MSG_IDENTIFYFLAG	0x80 
#define MSG_IDENTIFY_DISCFLAG	0x40 
#define MSG_IDENTIFY(lun, disc)	(((disc) ? 0xc0 : MSG_IDENTIFYFLAG) | (lun))
#define MSG_ISIDENTIFY(m)	((m) & MSG_IDENTIFYFLAG)
#define MSG_IDENTIFY_LUNMASK	0x3F 

/* Extended messages (opcode and length) */
#define MSG_EXT_SDTR		0x01
#define MSG_EXT_SDTR_LEN	0x03

#define MSG_EXT_WDTR		0x03
#define MSG_EXT_WDTR_LEN	0x02
#define MSG_EXT_WDTR_BUS_8_BIT	0x00
#define MSG_EXT_WDTR_BUS_16_BIT	0x01
#define MSG_EXT_WDTR_BUS_32_BIT	0x02 /* Deprecated in SPI3 */

#define	MSG_EXT_PPR		0x04 /* SPI3/SPI4 */
#define	MSG_EXT_PPR_LEN		0x06
#define	MSG_EXT_PPR_PCOMP_EN	0x80
#define	MSG_EXT_PPR_RTI		0x40
#define	MSG_EXT_PPR_RD_STRM	0x20
#define	MSG_EXT_PPR_WR_FLOW	0x10
#define	MSG_EXT_PPR_HOLD_MCS	0x08
#define	MSG_EXT_PPR_QAS_REQ	0x04
#define	MSG_EXT_PPR_DT_REQ	0x02
#define	MSG_EXT_PPR_IU_REQ	0x01

/* Fake messages not defined for SPI, but needed for other transports */
#define	MSG_QUERY_TASK		0x100
#define	MSG_QUERY_TASK_SET	0x101
#define	MSG_QUERY_ASYNC_EVENT	0x102

