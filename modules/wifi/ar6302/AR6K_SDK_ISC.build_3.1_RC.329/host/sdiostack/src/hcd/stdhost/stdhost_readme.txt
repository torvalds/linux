// Copyright (c) 2006 Atheros Communications Inc.
// 
"//"

"//"
 Permission to use, copy, modify, and/or distribute this software for any
"//"
 purpose with or without fee is hereby granted, provided that the above
"//"
 copyright notice and this permission notice appear in all copies.
"//"

"//"
 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
"//"
 WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
"//"
 MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
"//"
 ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
"//"
 WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
"//"
 ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
"//"
 OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
"//"

"//"
 Portions of this code were developed with information supplied from the 
"//"
 SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
"//"

"//"
  The following conditions apply to the release of the SD simplified specification (“Simplified
"//"
  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
"//"
  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
"//"
  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
"//"
  Specification may require a license from the SD Card Association or other third parties.
"//"
  Disclaimers:
"//"
  The information contained in the Simplified Specification is presented only as a standard 
"//"
  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
"//"
  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
"//"
  any damages, any infringements of patents or other right of the SD Card Association or any third 
"//"
  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
"//"
  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
"//"
  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
"//"
  information, know-how or other confidential information to any third party.
"//"

"//"

"//"
 The initial developers of the original code are Seung Yi and Paul Lever
"//"

"//"
 sdio@atheros.com
"//"

"//"


Codetelligence Embedded SDIO Stack Host Controller Driver Read Me.

Standard Host Controller Driver Core

This sample driver source (stdhost) provides reference code for a
SDIO Standard Host 2.0 compliant controller.  The source code is designed to be compiled 
within a bus-specific implementation (i.e PCI).  The bus implementation is responsible for
allocating I/O, interrupt and DMA resources.  The reference code may contain OS-specific helper
functions that may handle common resource allocations (i.e. DMA, I/O Work items).
Refer to the pci_std sample to view how the reference code is used on some OS implementations. 

The core provides the following features:

   1. Host Controller Driver interface to the SDIO stack (request and configuration processing).
   2. Abstraction of interrupt handling, deferred work and card insertion/removal.
   3. OS-specific implementation of Direct Memory Access (DMA) (see pci_std reference).
   4. OS-specific management of multiple slot instances (see pci_std reference).



          

