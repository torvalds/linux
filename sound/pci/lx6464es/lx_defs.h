/* -*- linux-c -*- *
 *
 * ALSA driver for the digigram lx6464es interface
 * adapted upstream headers
 *
 * Copyright (c) 2009 Tim Blechmann <tim@klingt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef LX_DEFS_H
#define LX_DEFS_H

/* code adapted from ethersound.h */
#define	XES_FREQ_COUNT8_MASK    0x00001FFF /* compteur 25MHz entre 8 ech. */
#define	XES_FREQ_COUNT8_44_MIN  0x00001288 /* 25M /
					    * [ 44k - ( 44.1k + 48k ) / 2 ]
					    * * 8 */
#define	XES_FREQ_COUNT8_44_MAX	0x000010F0 /* 25M / [ ( 44.1k + 48k ) / 2 ]
					    * * 8 */
#define	XES_FREQ_COUNT8_48_MAX	0x00000F08 /* 25M /
					    * [ 48k + ( 44.1k + 48k ) / 2 ]
					    * * 8 */

/* code adapted from LXES_registers.h */

#define IOCR_OUTPUTS_OFFSET 0	/* (rw) offset for the number of OUTs in the
				 * ConfES register. */
#define IOCR_INPUTS_OFFSET  8	/* (rw) offset for the number of INs in the
				 * ConfES register. */
#define FREQ_RATIO_OFFSET  19	/* (rw) offset for frequency ratio in the
				 * ConfES register. */
#define	FREQ_RATIO_SINGLE_MODE 0x01 /* value for single mode frequency ratio:
				     * sample rate = frequency rate. */

#define CONFES_READ_PART_MASK	0x00070000
#define CONFES_WRITE_PART_MASK	0x00F80000

/* code adapted from if_drv_mb.h */

#define MASK_SYS_STATUS_ERROR	(1L << 31) /* events that lead to a PCI irq if
					    * not yet pending */
#define MASK_SYS_STATUS_URUN	(1L << 30)
#define MASK_SYS_STATUS_ORUN	(1L << 29)
#define MASK_SYS_STATUS_EOBO	(1L << 28)
#define MASK_SYS_STATUS_EOBI	(1L << 27)
#define MASK_SYS_STATUS_FREQ	(1L << 26)
#define MASK_SYS_STATUS_ESA	(1L << 25) /* reserved, this is set by the
					    * XES */
#define MASK_SYS_STATUS_TIMER	(1L << 24)

#define MASK_SYS_ASYNC_EVENTS	(MASK_SYS_STATUS_ERROR |		\
				 MASK_SYS_STATUS_URUN  |		\
				 MASK_SYS_STATUS_ORUN  |		\
				 MASK_SYS_STATUS_EOBO  |		\
				 MASK_SYS_STATUS_EOBI  |		\
				 MASK_SYS_STATUS_FREQ  |		\
				 MASK_SYS_STATUS_ESA)

#define MASK_SYS_PCI_EVENTS		(MASK_SYS_ASYNC_EVENTS |	\
					 MASK_SYS_STATUS_TIMER)

#define MASK_SYS_TIMER_COUNT	0x0000FFFF

#define MASK_SYS_STATUS_EOT_PLX		(1L << 22) /* event that remains
						    * internal: reserved fo end
						    * of plx dma */
#define MASK_SYS_STATUS_XES		(1L << 21) /* event that remains
						    * internal: pending XES
						    * IRQ */
#define MASK_SYS_STATUS_CMD_DONE	(1L << 20) /* alternate command
						    * management: notify driver
						    * instead of polling */


#define MAX_STREAM_BUFFER 5	/* max amount of stream buffers. */

#define MICROBLAZE_IBL_MIN		 32
#define MICROBLAZE_IBL_DEFAULT	        128
#define MICROBLAZE_IBL_MAX		512
/* #define MASK_GRANULARITY		(2*MICROBLAZE_IBL_MAX-1) */



/* command opcodes, see reference for details */

/*
 the capture bit position in the object_id field in driver commands
 depends upon the number of managed channels. For now, 64 IN + 64 OUT are
 supported. HOwever, the communication protocol forsees 1024 channels, hence
 bit 10 indicates a capture (input) object).
*/
#define ID_IS_CAPTURE (1L << 10)
#define ID_OFFSET	13	/* object ID is at the 13th bit in the
				 * 1st command word.*/
#define ID_CH_MASK    0x3F
#define OPCODE_OFFSET	24	/* offset of the command opcode in the first
				 * command word.*/

enum cmd_mb_opcodes {
	CMD_00_INFO_DEBUG	        = 0x00,
	CMD_01_GET_SYS_CFG		= 0x01,
	CMD_02_SET_GRANULARITY		= 0x02,
	CMD_03_SET_TIMER_IRQ		= 0x03,
	CMD_04_GET_EVENT		= 0x04,
	CMD_05_GET_PIPES		= 0x05,

	CMD_06_ALLOCATE_PIPE            = 0x06,
	CMD_07_RELEASE_PIPE		= 0x07,
	CMD_08_ASK_BUFFERS		= 0x08,
	CMD_09_STOP_PIPE		= 0x09,
	CMD_0A_GET_PIPE_SPL_COUNT	= 0x0a,
	CMD_0B_TOGGLE_PIPE_STATE	= 0x0b,

	CMD_0C_DEF_STREAM		= 0x0c,
	CMD_0D_SET_MUTE			= 0x0d,
	CMD_0E_GET_STREAM_SPL_COUNT     = 0x0e,
	CMD_0F_UPDATE_BUFFER		= 0x0f,
	CMD_10_GET_BUFFER		= 0x10,
	CMD_11_CANCEL_BUFFER		= 0x11,
	CMD_12_GET_PEAK			= 0x12,
	CMD_13_SET_STREAM_STATE		= 0x13,
	CMD_14_INVALID			= 0x14,
};

/* pipe states */
enum pipe_state_t {
	PSTATE_IDLE	= 0,	/* the pipe is not processed in the XES_IRQ
				 * (free or stopped, or paused). */
	PSTATE_RUN	= 1,	/* sustained play/record state. */
	PSTATE_PURGE	= 2,	/* the ES channels are now off, render pipes do
				 * not DMA, record pipe do a last DMA. */
	PSTATE_ACQUIRE	= 3,	/* the ES channels are now on, render pipes do
				 * not yet increase their sample count, record
				 * pipes do not DMA. */
	PSTATE_CLOSING	= 4,	/* the pipe is releasing, and may not yet
				 * receive an "alloc" command. */
};

/* stream states */
enum stream_state_t {
	SSTATE_STOP	=  0x00,       /* setting to stop resets the stream spl
					* count.*/
	SSTATE_RUN	= (0x01 << 0), /* start DMA and spl count handling. */
	SSTATE_PAUSE	= (0x01 << 1), /* pause DMA and spl count handling. */
};

/* buffer flags */
enum buffer_flags {
	BF_VALID	= 0x80,	/* set if the buffer is valid, clear if free.*/
	BF_CURRENT	= 0x40,	/* set if this is the current buffer (there is
				 * always a current buffer).*/
	BF_NOTIFY_EOB	= 0x20,	/* set if this buffer must cause a PCI event
				 * when finished.*/
	BF_CIRCULAR	= 0x10,	/* set if buffer[1] must be copied to buffer[0]
				 * by the end of this buffer.*/
	BF_64BITS_ADR	= 0x08,	/* set if the hi part of the address is valid.*/
	BF_xx		= 0x04,	/* future extension.*/
	BF_EOB		= 0x02,	/* set if finished, but not yet free.*/
	BF_PAUSE	= 0x01,	/* pause stream at buffer end.*/
	BF_ZERO		= 0x00,	/* no flags (init).*/
};

/**
*	Stream Flags definitions
*/
enum stream_flags {
	SF_ZERO		= 0x00000000, /* no flags (stream invalid). */
	SF_VALID	= 0x10000000, /* the stream has a valid DMA_conf
				       * info (setstreamformat). */
	SF_XRUN		= 0x20000000, /* the stream is un x-run state. */
	SF_START	= 0x40000000, /* the DMA is running.*/
	SF_ASIO		= 0x80000000, /* ASIO.*/
};


#define MASK_SPL_COUNT_HI 0x00FFFFFF /* 4 MSBits are status bits */
#define PSTATE_OFFSET             28 /* 4 MSBits are status bits */


#define MASK_STREAM_HAS_MAPPING	(1L << 12)
#define MASK_STREAM_IS_ASIO	(1L <<  9)
#define STREAM_FMT_OFFSET	10   /* the stream fmt bits start at the 10th
				      * bit in the command word. */

#define STREAM_FMT_16b          0x02
#define STREAM_FMT_intel        0x01

#define FREQ_FIELD_OFFSET	15  /* offset of the freq field in the response
				     * word */

#define BUFF_FLAGS_OFFSET	  24 /*  offset of the buffer flags in the
				      *  response word. */
#define MASK_DATA_SIZE	  0x00FFFFFF /* this must match the field size of
				      * datasize in the buffer_t structure. */

#define MASK_BUFFER_ID	        0xFF /* the cancel command awaits a buffer ID,
				      * may be 0xFF for "current". */


/* code adapted from PcxErr_e.h */

/* Bits masks */

#define ERROR_MASK              0x8000

#define SOURCE_MASK             0x7800

#define E_SOURCE_BOARD          0x4000 /* 8 >> 1 */
#define E_SOURCE_DRV            0x2000 /* 4 >> 1 */
#define E_SOURCE_API            0x1000 /* 2 >> 1 */
/* Error tools */
#define E_SOURCE_TOOLS          0x0800 /* 1 >> 1 */
/* Error pcxaudio */
#define E_SOURCE_AUDIO          0x1800 /* 3 >> 1 */
/* Error virtual pcx */
#define E_SOURCE_VPCX           0x2800 /* 5 >> 1 */
/* Error dispatcher */
#define E_SOURCE_DISPATCHER     0x3000 /* 6 >> 1 */
/* Error from CobraNet firmware */
#define E_SOURCE_COBRANET       0x3800 /* 7 >> 1 */

#define E_SOURCE_USER           0x7800

#define CLASS_MASK              0x0700

#define CODE_MASK               0x00FF

/* Bits values */

/* Values for the error/warning bit */
#define ERROR_VALUE             0x8000
#define WARNING_VALUE           0x0000

/* Class values */
#define E_CLASS_GENERAL                  0x0000
#define E_CLASS_INVALID_CMD              0x0100
#define E_CLASS_INVALID_STD_OBJECT       0x0200
#define E_CLASS_RSRC_IMPOSSIBLE          0x0300
#define E_CLASS_WRONG_CONTEXT            0x0400
#define E_CLASS_BAD_SPECIFIC_PARAMETER   0x0500
#define E_CLASS_REAL_TIME_ERROR          0x0600
#define E_CLASS_DIRECTSHOW               0x0700
#define E_CLASS_FREE                     0x0700


/* Complete DRV error code for the general class */
#define ED_GN           (ERROR_VALUE | E_SOURCE_DRV | E_CLASS_GENERAL)
#define ED_CONCURRENCY                  (ED_GN | 0x01)
#define ED_DSP_CRASHED                  (ED_GN | 0x02)
#define ED_UNKNOWN_BOARD                (ED_GN | 0x03)
#define ED_NOT_INSTALLED                (ED_GN | 0x04)
#define ED_CANNOT_OPEN_SVC_MANAGER      (ED_GN | 0x05)
#define ED_CANNOT_READ_REGISTRY         (ED_GN | 0x06)
#define ED_DSP_VERSION_MISMATCH         (ED_GN | 0x07)
#define ED_UNAVAILABLE_FEATURE          (ED_GN | 0x08)
#define ED_CANCELLED                    (ED_GN | 0x09)
#define ED_NO_RESPONSE_AT_IRQA          (ED_GN | 0x10)
#define ED_INVALID_ADDRESS              (ED_GN | 0x11)
#define ED_DSP_CORRUPTED                (ED_GN | 0x12)
#define ED_PENDING_OPERATION            (ED_GN | 0x13)
#define ED_NET_ALLOCATE_MEMORY_IMPOSSIBLE   (ED_GN | 0x14)
#define ED_NET_REGISTER_ERROR               (ED_GN | 0x15)
#define ED_NET_THREAD_ERROR                 (ED_GN | 0x16)
#define ED_NET_OPEN_ERROR                   (ED_GN | 0x17)
#define ED_NET_CLOSE_ERROR                  (ED_GN | 0x18)
#define ED_NET_NO_MORE_PACKET               (ED_GN | 0x19)
#define ED_NET_NO_MORE_BUFFER               (ED_GN | 0x1A)
#define ED_NET_SEND_ERROR                   (ED_GN | 0x1B)
#define ED_NET_RECEIVE_ERROR                (ED_GN | 0x1C)
#define ED_NET_WRONG_MSG_SIZE               (ED_GN | 0x1D)
#define ED_NET_WAIT_ERROR                   (ED_GN | 0x1E)
#define ED_NET_EEPROM_ERROR                 (ED_GN | 0x1F)
#define ED_INVALID_RS232_COM_NUMBER         (ED_GN | 0x20)
#define ED_INVALID_RS232_INIT               (ED_GN | 0x21)
#define ED_FILE_ERROR                       (ED_GN | 0x22)
#define ED_INVALID_GPIO_CMD                 (ED_GN | 0x23)
#define ED_RS232_ALREADY_OPENED             (ED_GN | 0x24)
#define ED_RS232_NOT_OPENED                 (ED_GN | 0x25)
#define ED_GPIO_ALREADY_OPENED              (ED_GN | 0x26)
#define ED_GPIO_NOT_OPENED                  (ED_GN | 0x27)
#define ED_REGISTRY_ERROR                   (ED_GN | 0x28) /* <- NCX */
#define ED_INVALID_SERVICE                  (ED_GN | 0x29) /* <- NCX */

#define ED_READ_FILE_ALREADY_OPENED	    (ED_GN | 0x2a) /* <- Decalage
							    * pour RCX
							    * (old 0x28)
							    * */
#define ED_READ_FILE_INVALID_COMMAND	    (ED_GN | 0x2b) /* ~ */
#define ED_READ_FILE_INVALID_PARAMETER	    (ED_GN | 0x2c) /* ~ */
#define ED_READ_FILE_ALREADY_CLOSED	    (ED_GN | 0x2d) /* ~ */
#define ED_READ_FILE_NO_INFORMATION	    (ED_GN | 0x2e) /* ~ */
#define ED_READ_FILE_INVALID_HANDLE	    (ED_GN | 0x2f) /* ~ */
#define ED_READ_FILE_END_OF_FILE	    (ED_GN | 0x30) /* ~ */
#define ED_READ_FILE_ERROR	            (ED_GN | 0x31) /* ~ */

#define ED_DSP_CRASHED_EXC_DSPSTACK_OVERFLOW (ED_GN | 0x32) /* <- Decalage pour
							     * PCX (old 0x14) */
#define ED_DSP_CRASHED_EXC_SYSSTACK_OVERFLOW (ED_GN | 0x33) /* ~ */
#define ED_DSP_CRASHED_EXC_ILLEGAL           (ED_GN | 0x34) /* ~ */
#define ED_DSP_CRASHED_EXC_TIMER_REENTRY     (ED_GN | 0x35) /* ~ */
#define ED_DSP_CRASHED_EXC_FATAL_ERROR       (ED_GN | 0x36) /* ~ */

#define ED_FLASH_PCCARD_NOT_PRESENT          (ED_GN | 0x37)

#define ED_NO_CURRENT_CLOCK                  (ED_GN | 0x38)

/* Complete DRV error code for real time class */
#define ED_RT           (ERROR_VALUE | E_SOURCE_DRV | E_CLASS_REAL_TIME_ERROR)
#define ED_DSP_TIMED_OUT                (ED_RT | 0x01)
#define ED_DSP_CHK_TIMED_OUT            (ED_RT | 0x02)
#define ED_STREAM_OVERRUN               (ED_RT | 0x03)
#define ED_DSP_BUSY                     (ED_RT | 0x04)
#define ED_DSP_SEMAPHORE_TIME_OUT       (ED_RT | 0x05)
#define ED_BOARD_TIME_OUT               (ED_RT | 0x06)
#define ED_XILINX_ERROR                 (ED_RT | 0x07)
#define ED_COBRANET_ITF_NOT_RESPONDING  (ED_RT | 0x08)

/* Complete BOARD error code for the invaid standard object class */
#define EB_ISO          (ERROR_VALUE | E_SOURCE_BOARD | \
			 E_CLASS_INVALID_STD_OBJECT)
#define EB_INVALID_EFFECT               (EB_ISO | 0x00)
#define EB_INVALID_PIPE                 (EB_ISO | 0x40)
#define EB_INVALID_STREAM               (EB_ISO | 0x80)
#define EB_INVALID_AUDIO                (EB_ISO | 0xC0)

/* Complete BOARD error code for impossible resource allocation class */
#define EB_RI           (ERROR_VALUE | E_SOURCE_BOARD | E_CLASS_RSRC_IMPOSSIBLE)
#define EB_ALLOCATE_ALL_STREAM_TRANSFERT_BUFFERS_IMPOSSIBLE (EB_RI | 0x01)
#define EB_ALLOCATE_PIPE_SAMPLE_BUFFER_IMPOSSIBLE           (EB_RI | 0x02)

#define EB_ALLOCATE_MEM_STREAM_IMPOSSIBLE		\
	EB_ALLOCATE_ALL_STREAM_TRANSFERT_BUFFERS_IMPOSSIBLE
#define EB_ALLOCATE_MEM_PIPE_IMPOSSIBLE			\
	EB_ALLOCATE_PIPE_SAMPLE_BUFFER_IMPOSSIBLE

#define EB_ALLOCATE_DIFFERED_CMD_IMPOSSIBLE     (EB_RI | 0x03)
#define EB_TOO_MANY_DIFFERED_CMD                (EB_RI | 0x04)
#define EB_RBUFFERS_TABLE_OVERFLOW              (EB_RI | 0x05)
#define EB_ALLOCATE_EFFECTS_IMPOSSIBLE          (EB_RI | 0x08)
#define EB_ALLOCATE_EFFECT_POS_IMPOSSIBLE       (EB_RI | 0x09)
#define EB_RBUFFER_NOT_AVAILABLE                (EB_RI | 0x0A)
#define EB_ALLOCATE_CONTEXT_LIII_IMPOSSIBLE     (EB_RI | 0x0B)
#define EB_STATUS_DIALOG_IMPOSSIBLE             (EB_RI | 0x1D)
#define EB_CONTROL_CMD_IMPOSSIBLE               (EB_RI | 0x1E)
#define EB_STATUS_SEND_IMPOSSIBLE               (EB_RI | 0x1F)
#define EB_ALLOCATE_PIPE_IMPOSSIBLE             (EB_RI | 0x40)
#define EB_ALLOCATE_STREAM_IMPOSSIBLE           (EB_RI | 0x80)
#define EB_ALLOCATE_AUDIO_IMPOSSIBLE            (EB_RI | 0xC0)

/* Complete BOARD error code for wrong call context class */
#define EB_WCC          (ERROR_VALUE | E_SOURCE_BOARD | E_CLASS_WRONG_CONTEXT)
#define EB_CMD_REFUSED                  (EB_WCC | 0x00)
#define EB_START_STREAM_REFUSED         (EB_WCC | 0xFC)
#define EB_SPC_REFUSED                  (EB_WCC | 0xFD)
#define EB_CSN_REFUSED                  (EB_WCC | 0xFE)
#define EB_CSE_REFUSED                  (EB_WCC | 0xFF)




#endif /* LX_DEFS_H */
