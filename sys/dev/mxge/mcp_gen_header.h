/*******************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2006-2007, Myricom Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Myricom Inc, nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

$FreeBSD$
***************************************************************************/

#ifndef _mcp_gen_header_h
#define _mcp_gen_header_h

/* this file define a standard header used as a first entry point to
   exchange information between firmware/driver and driver.  The
   header structure can be anywhere in the mcp. It will usually be in
   the .data section, because some fields needs to be initialized at
   compile time.
   The 32bit word at offset MX_HEADER_PTR_OFFSET in the mcp must
   contains the location of the header. 

   Typically a MCP will start with the following:
   .text
     .space 52    ! to help catch MEMORY_INT errors
     bt start     ! jump to real code
     nop
     .long _gen_mcp_header
   
   The source will have a definition like:

   mcp_gen_header_t gen_mcp_header = {
      .header_length = sizeof(mcp_gen_header_t),
      .mcp_type = MCP_TYPE_XXX,
      .version = "something $Id: mcp_gen_header.h,v 1.1 2005/12/23 02:10:44 gallatin Exp $",
      .mcp_globals = (unsigned)&Globals
   };
*/


#define MCP_HEADER_PTR_OFFSET  0x3c

#define MCP_TYPE_MX 0x4d582020 /* "MX  " */
#define MCP_TYPE_PCIE 0x70636965 /* "PCIE" pcie-only MCP */
#define MCP_TYPE_ETH 0x45544820 /* "ETH " */
#define MCP_TYPE_MCP0 0x4d435030 /* "MCP0" */


typedef struct mcp_gen_header {
  /* the first 4 fields are filled at compile time */
  unsigned header_length;
  unsigned mcp_type;
  char version[128];
  unsigned mcp_globals; /* pointer to mcp-type specific structure */

  /* filled by the MCP at run-time */
  unsigned sram_size;
  unsigned string_specs;  /* either the original STRING_SPECS or a superset */
  unsigned string_specs_len;

  /* Fields above this comment are guaranteed to be present.

     Fields below this comment are extensions added in later versions
     of this struct, drivers should compare the header_length against
     offsetof(field) to check wether a given MCP implements them.

     Never remove any field.  Keep everything naturally align.
  */
} mcp_gen_header_t;

/* Macro to create a simple mcp header */
#define MCP_GEN_HEADER_DECL(type, version_str, global_ptr)	\
  struct mcp_gen_header mcp_gen_header = {			\
    sizeof (struct mcp_gen_header),				\
    (type),							\
    version_str,						\
    (global_ptr),						\
    SRAM_SIZE,							\
    (unsigned int) STRING_SPECS,				\
    256								\
  }


#endif /* _mcp_gen_header_h */
