/*
 * Copyright (C) 2006-2017 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef ___VBox_Graphics_HGSMIDefs_h
#define ___VBox_Graphics_HGSMIDefs_h

#include "vbox_err.h"

/* HGSMI uses 32 bit offsets and sizes. */

#define HGSMIOFFSET_VOID ((u32)~0)

/**
 * Describes a shared memory area buffer.
 *
 * Used for calculations with offsets and for buffers verification.
 */
typedef struct HGSMIAREA {
	u8     *pu8Base; /**< The starting address of the area. Corresponds to offset 'offBase'. */
	u32  offBase; /**< The starting offset of the area. */
	u32  offLast; /**< The last valid offset:  offBase + cbArea - 1 - (sizeof(header) + sizeof(tail)). */
	u32    cbArea;  /**< Size of the area. */
} HGSMIAREA;


/* The buffer description flags. */
#define HGSMI_BUFFER_HEADER_F_SEQ_MASK     0x03 /* Buffer sequence type mask. */
#define HGSMI_BUFFER_HEADER_F_SEQ_SINGLE   0x00 /* Single buffer, not a part of a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_START    0x01 /* The first buffer in a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_CONTINUE 0x02 /* A middle buffer in a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_END      0x03 /* The last buffer in a sequence. */


#pragma pack(1) /** @todo not necessary. use assert_compile_size instead. */
/* 16 bytes buffer header. */
typedef struct HGSMIBUFFERHEADER {
	u32    u32DataSize;            /* Size of data that follows the header. */

	u8     u8Flags;                /* The buffer description: HGSMI_BUFFER_HEADER_F_* */

	u8     u8Channel;              /* The channel the data must be routed to. */
	u16    u16ChannelInfo;         /* Opaque to the HGSMI, used by the channel. */

	union {
		u8 au8Union[8];            /* Opaque placeholder to make the union 8 bytes. */

		struct {                               /* HGSMI_BUFFER_HEADER_F_SEQ_SINGLE */
			u32 u32Reserved1;      /* A reserved field, initialize to 0. */
			u32 u32Reserved2;      /* A reserved field, initialize to 0. */
		} Buffer;

		struct {                               /* HGSMI_BUFFER_HEADER_F_SEQ_START */
			u32 u32SequenceNumber; /* The sequence number, the same for all buffers in the sequence. */
			u32 u32SequenceSize;   /* The total size of the sequence. */
		} SequenceStart;

		struct {                               /* HGSMI_BUFFER_HEADER_F_SEQ_CONTINUE and HGSMI_BUFFER_HEADER_F_SEQ_END */
			u32 u32SequenceNumber; /* The sequence number, the same for all buffers in the sequence. */
			u32 u32SequenceOffset; /* Data offset in the entire sequence. */
		} SequenceContinue;
	} u;
} HGSMIBUFFERHEADER;

/* 8 bytes buffer tail. */
typedef struct HGSMIBUFFERTAIL {
	u32    reserved;        /* Reserved, must be initialized to 0. */
	u32    u32Checksum;        /* Verifyer for the buffer header and offset and for first 4 bytes of the tail. */
} HGSMIBUFFERTAIL;
#pragma pack()

assert_compile_size(HGSMIBUFFERHEADER, 16);
assert_compile_size(HGSMIBUFFERTAIL, 8);

/* The size of the array of channels. Array indexes are u8. Note: the value must not be changed. */
#define HGSMI_NUMBER_OF_CHANNELS 0x100

typedef struct HGSMIENV {
	/* Environment context pointer. */
	void *pvEnv;

	/* Allocate system memory. */
	void * (*pfnAlloc)(void *pvEnv, u32 len);

	/* Free system memory. */
	void (*pfnFree)(void *pvEnv, void *pv);
} HGSMIENV;

#endif /* !___VBox_Graphics_HGSMIDefs_h */

