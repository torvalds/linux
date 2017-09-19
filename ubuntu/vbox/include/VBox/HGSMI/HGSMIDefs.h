/** @file
 *
 * VBox Host Guest Shared Memory Interface (HGSMI).
 * Host/Guest shared part: types and defines.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


#ifndef ___VBox_HGSMI_HGSMIDefs_h
#define ___VBox_HGSMI_HGSMIDefs_h

#include <iprt/assert.h>
#include <iprt/types.h>

/* HGSMI uses 32 bit offsets and sizes. */
typedef uint32_t HGSMISIZE;
typedef uint32_t HGSMIOFFSET;

#define HGSMIOFFSET_VOID ((HGSMIOFFSET)~0)

/* Describes a shared memory area buffer.
 * Used for calculations with offsets and for buffers verification.
 */
typedef struct HGSMIAREA
{
    uint8_t     *pu8Base; /* The starting address of the area. Corresponds to offset 'offBase'. */
    HGSMIOFFSET  offBase; /* The starting offset of the area. */
    HGSMIOFFSET  offLast; /* The last valid offset:
                           * offBase + cbArea - 1 - (sizeof(header) + sizeof(tail)).
                           */
    HGSMISIZE    cbArea;  /* Size of the area. */
} HGSMIAREA;


/* The buffer description flags. */
#define HGSMI_BUFFER_HEADER_F_SEQ_MASK     0x03 /* Buffer sequence type mask. */
#define HGSMI_BUFFER_HEADER_F_SEQ_SINGLE   0x00 /* Single buffer, not a part of a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_START    0x01 /* The first buffer in a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_CONTINUE 0x02 /* A middle buffer in a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_END      0x03 /* The last buffer in a sequence. */


#pragma pack(1)
/* 16 bytes buffer header. */
typedef struct HGSMIBUFFERHEADER
{
    uint32_t    u32DataSize;            /* Size of data that follows the header. */

    uint8_t     u8Flags;                /* The buffer description: HGSMI_BUFFER_HEADER_F_* */

    uint8_t     u8Channel;              /* The channel the data must be routed to. */
    uint16_t    u16ChannelInfo;         /* Opaque to the HGSMI, used by the channel. */

    union {
        uint8_t au8Union[8];            /* Opaque placeholder to make the union 8 bytes. */

        struct
        {                               /* HGSMI_BUFFER_HEADER_F_SEQ_SINGLE */
            uint32_t u32Reserved1;      /* A reserved field, initialize to 0. */
            uint32_t u32Reserved2;      /* A reserved field, initialize to 0. */
        } Buffer;

        struct
        {                               /* HGSMI_BUFFER_HEADER_F_SEQ_START */
            uint32_t u32SequenceNumber; /* The sequence number, the same for all buffers in the sequence. */
            uint32_t u32SequenceSize;   /* The total size of the sequence. */
        } SequenceStart;

        struct
        {                               /* HGSMI_BUFFER_HEADER_F_SEQ_CONTINUE and HGSMI_BUFFER_HEADER_F_SEQ_END */
            uint32_t u32SequenceNumber; /* The sequence number, the same for all buffers in the sequence. */
            uint32_t u32SequenceOffset; /* Data offset in the entire sequence. */
        } SequenceContinue;
    } u;
} HGSMIBUFFERHEADER;

/* 8 bytes buffer tail. */
typedef struct HGSMIBUFFERTAIL
{
    uint32_t    u32Reserved;        /* Reserved, must be initialized to 0. */
    uint32_t    u32Checksum;        /* Verifyer for the buffer header and offset and for first 4 bytes of the tail. */
} HGSMIBUFFERTAIL;
#pragma pack()

AssertCompileSize(HGSMIBUFFERHEADER, 16);
AssertCompileSize(HGSMIBUFFERTAIL, 8);

/* The size of the array of channels. Array indexes are uint8_t. Note: the value must not be changed. */
#define HGSMI_NUMBER_OF_CHANNELS 0x100

typedef struct HGSMIENV
{
    /* Environment context pointer. */
    void *pvEnv;

    /* Allocate system memory. */
    DECLCALLBACKMEMBER(void *, pfnAlloc)(void *pvEnv, HGSMISIZE cb);

    /* Free system memory. */
    DECLCALLBACKMEMBER(void, pfnFree)(void *pvEnv, void *pv);
} HGSMIENV;

#endif /* !___VBox_HGSMI_HGSMIDefs_h */
