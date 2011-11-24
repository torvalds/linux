// Copyright (c) 2007 Atheros Communications Inc.
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

PCI Standard Host Controller Driver

General Notes:
-  SDIO Standard Host 2.0 compliant.
-  SD 1 and 4 bit modes, up to 50 Mhz Clock rates and SD High Speed Mode.
-  SDIO IRQ detection for 1,4 bit modes.
-  Programmed I/O mode (non-DMA).
-  Simple DMA Support
-  Advanced DMA Support (scatter-gather)
-  Supports Tokyo Electron Ellen II MMC8 bus widths.
-  Card detect via slot mechanical switch
-  Configurable idle bus clock rate
    
Linux Notes:

Module Parameters:
   
    "debug" =  set module debug level (default = 4).
    Module Debug Level:      Description of Kernel Prints:
           7                   Setup/Initialization
           8                   Card Insertion (if mechanical card switch implemented)
           9                   Processing bus requests w/ Data
          10                   Processing for all bus requests.
          11                   Configuration Requests.
          12                   SDIO controller IRQ processing
          13                   Clock Control
          14                   SDIO Card Interrupt  


    "IdleBusClockRate" - idle bus clock rate in Hz (active when 4-bit interrupt detection is required) 
                         Lower values will reduce power at the cost of higher interrupt detection
                         latency.  Typical values are 2000000 (2Mhz), 4000000 (4 Mhz) 
    "CommonBufferDMASize" - common buffer size if host controller is DMA capable. Default is set to
                            0 (common buffer DMA disabled).
