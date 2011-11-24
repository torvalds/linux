//------------------------------------------------------------------------------
// <copyright file="flash.h" company="Atheros">
//    Copyright (c) 2004-2007 Atheros Corporation.  All rights reserved.
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
#ifndef __APP_FLASH_H__
#define __APP_FLASH_H__

/* 
 * Flash Application Message Interface
 *
 * This is a very simple messaging interface that the Host and Target use
 * in order to allow the Host to read, write, and erase flash memory on
 * the Target.
 *
 * The Host writes requests to mailbox0, and reads responses
 * from mailbox0.   Flash requests all begin with a command
 * (see below for specific commands), and are followed by
 * command-specific data.
 *
 * All flash messages -- both commands and responses -- begin
 * with FLASH_PADDING_SZ bytes of padding.  This may enable
 * Host software to leverage portions of an existing network
 * stack during flash messaging.
 *
 * Flow control:
 * The Host can only issue a command once the Target issues a
 * "Flash Command Credit", using AR6k Counter #4.  As soon as the
 * Target has completed a command, it issues another Flash Command
 * Credit (so the Host can issue the next Flash command).
 */

#define FLASH_DATASZ_MAX 1024
#define FLASH_PADDING_SZ 64

#define FLASH_READ                      1
        /*
         * Semantics: Host reads Target flash
         * Request format:
         *    A_UINT32      command (FLASH_READ)
         *    A_UINT32      address
         *    A_UINT32      length, at most FLASH_DATASZ_MAX
         * Response format:
         *    A_UINT8       data[length]
         */

#define FLASH_WRITE                     2
        /*
         * Semantics: Host writes Target flash
         * Request format:
         *    A_UINT32      command (FLASH_WRITE)
         *    A_UINT32      address
         *    A_UINT32      length, at most FLASH_DATASZ_MAX
         *    A_UINT8       data[length]
         * Response format: none
         */

#define FLASH_ERASE                     3
#define FLASH_ERASE_COOKIE 0x00a112ff
        /*
         * Semantics: Host erases ENTIRE Target flash, including any
         * board-specific calibration data that is stored into flash
         * at manufacture time and which is needed for proper operation!
         * Request format:
         *    A_UINT32      command      (FLASH_ERASE)
         *    A_UINT32      magic cookie (FLASH_ERASE_COOKIE)
         * Response format: none
         */

#define FLASH_PARTIAL_ERASE             4
        /*
         * Semantics: Host partially erases Target flash
         * Request format:
         *    A_UINT32      command (FLASH_PARTIAL_ERASE)
         *    A_UINT32      address
         *    A_UINT32      length
         * Response format: none
         */

#define FLASH_PART_INIT               5
        /*
         * Semantics: Target initializes the flashpart
         * module (previously loaded into RAM) which contains
         * code that is specific to a particular flash
         * command set (e.g. AMD16).
         *
         * Note: This command should be issued before all
         * other flash commands.  Otherwise, the default
         * flash command set, AMD16, is used.   A request
         * to initialize the flashpart module that is made
         * after other flash commands is ignored.
         *
         * Request format:
         *    A_UINT32      command (FLASH_PART_INIT)
         *    A_UINT32      address of flashpart init function
         * Response format: none
         */

#define FLASH_DONE                      99
        /*
         * Semantics: Host indicates that it is done.
         * Request format:
         *    A_UINT32      command (FLASH_DONE)
         * Response format: none
         */

#endif /* __APP_FLASH_H__ */
