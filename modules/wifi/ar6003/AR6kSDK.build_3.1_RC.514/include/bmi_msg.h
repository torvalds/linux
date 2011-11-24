//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef __BMI_MSG_H__
#define __BMI_MSG_H__

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

/*
 * Bootloader Messaging Interface (BMI)
 *
 * BMI is a very simple messaging interface used during initialization
 * to read memory, write memory, execute code, and to define an
 * application entry PC.
 *
 * It is used to download an application to AR6K, to provide
 * patches to code that is already resident on AR6K, and generally
 * to examine and modify state.  The Host has an opportunity to use
 * BMI only once during bootup.  Once the Host issues a BMI_DONE
 * command, this opportunity ends.
 *
 * The Host writes BMI requests to mailbox0, and reads BMI responses
 * from mailbox0.   BMI requests all begin with a command
 * (see below for specific commands), and are followed by
 * command-specific data.
 *
 * Flow control:
 * The Host can only issue a command once the Target gives it a
 * "BMI Command Credit", using AR6K Counter #4.  As soon as the
 * Target has completed a command, it issues another BMI Command
 * Credit (so the Host can issue the next command).
 *
 * BMI handles all required Target-side cache flushing.
 */


/* Maximum data size used for BMI transfers */
#define BMI_DATASZ_MAX                      256

/* BMI Commands */

#define BMI_NO_COMMAND                      0

#define BMI_DONE                            1
        /*
         * Semantics: Host is done using BMI
         * Request format:
         *    A_UINT32      command (BMI_DONE)
         * Response format: none
         */

#define BMI_READ_MEMORY                     2
        /*
         * Semantics: Host reads AR6K memory
         * Request format:
         *    A_UINT32      command (BMI_READ_MEMORY)
         *    A_UINT32      address
         *    A_UINT32      length, at most BMI_DATASZ_MAX
         * Response format:
         *    A_UINT8       data[length]
         */

#define BMI_WRITE_MEMORY                    3
        /*
         * Semantics: Host writes AR6K memory
         * Request format:
         *    A_UINT32      command (BMI_WRITE_MEMORY)
         *    A_UINT32      address
         *    A_UINT32      length, at most BMI_DATASZ_MAX
         *    A_UINT8       data[length]
         * Response format: none
         */
/* 
 * Capbility to write "segmented files" is provided for two reasons
 * 1) backwards compatibility for certain situations where Hosts
 *    have limited flexibility
 * 2) because it's darn convenient.
 *
 * A segmented file consists of a file header followed by an arbitrary number
 * of segments.  Each segment contains segment metadata -- a Target address and
 * a length -- followed by "length" bytes of data. A segmented file ends with
 * a segment that specifies length=BMI_SGMTFILE_DONE. When a segmented file
 * is sent to the Target, firmware writes each segment to the specified address.
 *
 * Special cases:
 * 1) If a segment's metadata indicates length=BMI_SGMTFILE_EXEC, then the
 * specified address is used as a function entry point for a brief function
 * with prototype "(void *)(void)". That function is called immediately.
 * After execution of the function completes, firmware continues with the
 * next segment. No data is expected when length=BMI_SGMTFILE_EXEC.
 *
 * 2) If a segment's metadata indicates length=BMI_SGMTFILE_BEGINADDR, then
 * the specified address is established as the application start address
 * so that a subsequent BMI_DONE jumps there.
 *
 * 3) If a segment's metadata indicates length=BMI_SGMTFILE_BDDATA, then
 * the specified address is used as the (possibly compressed) length of board
 * data, which is loaded into the proper Target address as specified by
 * hi_board_data. In addition, the hi_board_data_initialized flag is set.
 *
 * A segmented file is sent to the Target using a sequence of 1 or more
 * BMI_WRITE_MEMORY commands.  The first such command must have
 * address=BMI_SEGMENTED_WRITE_ADDR.  Subsequent BMI_WRITE_MEMORY commands
 * can use an arbitrary address.  In each BMI_WRITE_MEMORY command, the
 * length specifies the number of data bytes transmitted (except for the
 * special cases listed above).
 * 
 * Alternatively, a segmented file may be sent to the Target using a
 * BMI_LZ_STREAM_START command with address=BMI_SEGMENTED_WRITE_ADDR
 * followed by a series of BMI_LZ_DATA commands that each send the next portion
 * of the segmented file.
 *
 * The data segments may be lz77 compressed.  In this case, the segmented file
 * header flag, BMI_SGMTFILE_FLAG_COMPRESS, must be set.  Note that segmented
 * file METAdata is never compressed; only the data segments themselves are
 * compressed. There is no way to mix compressed and uncompressed data segments
 * in a single segmented file. Compressed (or uncompressed) segments are handled
 * by both BMI_WRITE_MEMORY and by BMI_LZ_DATA commands.  (Compression is an
 * attribute of the segmented file rather than of the command used to transmit
 * it.)
 */
#define BMI_SEGMENTED_WRITE_ADDR 0x1234

/* File header for a segmented file */
struct bmi_segmented_file_header {
    A_UINT32 magic_num;
    A_UINT32 file_flags;
};
#define BMI_SGMTFILE_MAGIC_NUM          0x544d4753 /* "SGMT" */
#define BMI_SGMTFILE_FLAG_COMPRESS      1

/* Metadata for a segmented file segment */
struct bmi_segmented_metadata {
    A_UINT32 addr;
    A_UINT32 length;
};
/* Special values for bmi_segmented_metadata.length (all have high bit set) */
#define BMI_SGMTFILE_DONE               0xffffffff      /* end of segmented data */
#define BMI_SGMTFILE_BDDATA             0xfffffffe      /* Board Data segment */
#define BMI_SGMTFILE_BEGINADDR          0xfffffffd      /* set beginning address */
#define BMI_SGMTFILE_EXEC               0xfffffffc      /* immediate function execution */

#define BMI_EXECUTE                         4
        /*
         * Semantics: Causes AR6K to execute code
         * Request format:
         *    A_UINT32      command (BMI_EXECUTE)
         *    A_UINT32      address
         *    A_UINT32      parameter
         * Response format:
         *    A_UINT32      return value
         */
/*
 * Note: In order to support the segmented file feature
 * (see BMI_WRITE_MEMORY), when the address specified in a
 * BMI_EXECUTE command matches (same physical address)
 * BMI_SEGMENTED_WRITE_ADDR, it is ignored. Instead, execution
 * begins at the address specified by hi_app_start.
 */

#define BMI_SET_APP_START                   5
        /*
         * Semantics: Set Target application starting address
         * Request format:
         *    A_UINT32      command (BMI_SET_APP_START)
         *    A_UINT32      address
         * Response format: none
         */

#define BMI_READ_SOC_REGISTER               6
        /*
         * Semantics: Read a 32-bit Target SOC register.
         * Request format:
         *    A_UINT32      command (BMI_READ_REGISTER)
         *    A_UINT32      address
         * Response format: 
         *    A_UINT32      value
         */

#define BMI_WRITE_SOC_REGISTER              7
        /*
         * Semantics: Write a 32-bit Target SOC register.
         * Request format:
         *    A_UINT32      command (BMI_WRITE_REGISTER)
         *    A_UINT32      address
         *    A_UINT32      value
         *
         * Response format: none
         */

#define BMI_GET_TARGET_ID                  8
#define BMI_GET_TARGET_INFO                8
        /*
         * Semantics: Fetch the 4-byte Target information
         * Request format:
         *    A_UINT32      command (BMI_GET_TARGET_ID/INFO)
         * Response format1 (old firmware):
         *    A_UINT32      TargetVersionID
         * Response format2 (newer firmware):
         *    A_UINT32      TARGET_VERSION_SENTINAL
         *    struct bmi_target_info;
         */

PREPACK struct bmi_target_info {
    A_UINT32 target_info_byte_count; /* size of this structure */
    A_UINT32 target_ver;             /* Target Version ID */
    A_UINT32 target_type;            /* Target type */
} POSTPACK;
#define TARGET_VERSION_SENTINAL 0xffffffff
#define TARGET_TYPE_AR6001 1
#define TARGET_TYPE_AR6002 2
#define TARGET_TYPE_AR6003 3
#define TARGET_TYPE_MCKINLEY 5


#define BMI_ROMPATCH_INSTALL               9
        /*
         * Semantics: Install a ROM Patch.
         * Request format:
         *    A_UINT32      command (BMI_ROMPATCH_INSTALL)
         *    A_UINT32      Target ROM Address
         *    A_UINT32      Target RAM Address or Value (depending on Target Type)
         *    A_UINT32      Size, in bytes
         *    A_UINT32      Activate? 1-->activate;
         *                            0-->install but do not activate
         * Response format:
         *    A_UINT32      PatchID
         */

#define BMI_ROMPATCH_UNINSTALL             10
        /*
         * Semantics: Uninstall a previously-installed ROM Patch,
         * automatically deactivating, if necessary.
         * Request format:
         *    A_UINT32      command (BMI_ROMPATCH_UNINSTALL)
         *    A_UINT32      PatchID
         *
         * Response format: none
         */

#define BMI_ROMPATCH_ACTIVATE              11
        /*
         * Semantics: Activate a list of previously-installed ROM Patches.
         * Request format:
         *    A_UINT32      command (BMI_ROMPATCH_ACTIVATE)
         *    A_UINT32      rompatch_count
         *    A_UINT32      PatchID[rompatch_count]
         *
         * Response format: none
         */

#define BMI_ROMPATCH_DEACTIVATE            12
        /*
         * Semantics: Deactivate a list of active ROM Patches.
         * Request format:
         *    A_UINT32      command (BMI_ROMPATCH_DEACTIVATE)
         *    A_UINT32      rompatch_count
         *    A_UINT32      PatchID[rompatch_count]
         *
         * Response format: none
         */


#define BMI_LZ_STREAM_START                13
        /*
         * Semantics: Begin an LZ-compressed stream of input
         * which is to be uncompressed by the Target to an
         * output buffer at address.  The output buffer must
         * be sufficiently large to hold the uncompressed
         * output from the compressed input stream.  This BMI
         * command should be followed by a series of 1 or more
         * BMI_LZ_DATA commands.
         *    A_UINT32      command (BMI_LZ_STREAM_START)
         *    A_UINT32      address
         * Note: Not supported on all versions of ROM firmware.
         */

#define BMI_LZ_DATA                        14
        /*
         * Semantics: Host writes AR6K memory with LZ-compressed
         * data which is uncompressed by the Target.  This command
         * must be preceded by a BMI_LZ_STREAM_START command. A series
         * of BMI_LZ_DATA commands are considered part of a single
         * input stream until another BMI_LZ_STREAM_START is issued.
         * Request format:
         *    A_UINT32      command (BMI_LZ_DATA)
         *    A_UINT32      length (of compressed data),
         *                  at most BMI_DATASZ_MAX
         *    A_UINT8       CompressedData[length]
         * Response format: none
         * Note: Not supported on all versions of ROM firmware.
         */

#define BMI_NVRAM_PROCESS                  15
#define BMI_NVRAM_SEG_NAME_SZ 16
        /*
         * Semantics: Cause Target to search NVRAM (if any) for a
         * segment with the specified name and process it according
         * to NVRAM metadata.
         * Request format:
         *    A_UINT32      command (BMI_NVRAM_PROCESS)
         *    A_UCHAR       name[BMI_NVRAM_SEG_NAME_SZ] name (LE format)
         * Response format:
         *    A_UINT32      0, if nothing was executed;
         *                  otherwise the value returned from the
         *                  last NVRAM segment that was executed
         */

#ifndef ATH_TARGET
#include "athendpack.h"
#endif

#endif /* __BMI_MSG_H__ */
