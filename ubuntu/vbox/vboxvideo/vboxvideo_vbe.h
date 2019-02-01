/*
 * Copyright (C) 2006-2019 Oracle Corporation
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

#ifndef VBOX_INCLUDED_Graphics_VBoxVideoVBE_h
#define VBOX_INCLUDED_Graphics_VBoxVideoVBE_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUEST <-> HOST Communication API */

/** @todo FIXME: Either dynamicly ask host for this or put somewhere high in
 *               physical memory like 0xE0000000. */

#define VBE_DISPI_BANK_ADDRESS          0xA0000
#define VBE_DISPI_BANK_SIZE_KB          64

#define VBE_DISPI_MAX_XRES              16384
#define VBE_DISPI_MAX_YRES              16384
#define VBE_DISPI_MAX_BPP               32

#define VBE_DISPI_IOPORT_INDEX          0x01CE
#define VBE_DISPI_IOPORT_DATA           0x01CF

#define VBE_DISPI_IOPORT_DAC_WRITE_INDEX  0x03C8
#define VBE_DISPI_IOPORT_DAC_DATA         0x03C9

/* Cross reference with src/VBox/Devices/Graphics/DevVGA.h */
#define VBE_DISPI_INDEX_ID              0x0
#define VBE_DISPI_INDEX_XRES            0x1
#define VBE_DISPI_INDEX_YRES            0x2
#define VBE_DISPI_INDEX_BPP             0x3
#define VBE_DISPI_INDEX_ENABLE          0x4
#define VBE_DISPI_INDEX_BANK            0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH      0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT     0x7
#define VBE_DISPI_INDEX_X_OFFSET        0x8
#define VBE_DISPI_INDEX_Y_OFFSET        0x9
#define VBE_DISPI_INDEX_VBOX_VIDEO      0xa
#define VBE_DISPI_INDEX_FB_BASE_HI      0xb
#define VBE_DISPI_INDEX_CFG             0xc

#define VBE_DISPI_ID0                   0xB0C0
#define VBE_DISPI_ID1                   0xB0C1
#define VBE_DISPI_ID2                   0xB0C2
#define VBE_DISPI_ID3                   0xB0C3
#define VBE_DISPI_ID4                   0xB0C4

#define VBE_DISPI_ID_VBOX_VIDEO         0xBE00
/* The VBOX interface id. Indicates support for VBVA shared memory interface. */
#define VBE_DISPI_ID_HGSMI              0xBE01
#define VBE_DISPI_ID_ANYX               0xBE02
#define VBE_DISPI_ID_CFG                0xBE03 /* VBE_DISPI_INDEX_CFG is available. */

#define VBE_DISPI_DISABLED              0x00
#define VBE_DISPI_ENABLED               0x01
#define VBE_DISPI_GETCAPS               0x02
#define VBE_DISPI_8BIT_DAC              0x20
/** @note this definition is a BOCHS legacy, used only in the video BIOS
 *        code and ignored by the emulated hardware. */
#define VBE_DISPI_LFB_ENABLED           0x40
#define VBE_DISPI_NOCLEARMEM            0x80

/* VBE_DISPI_INDEX_CFG content. */
#define VBE_DISPI_CFG_MASK_ID           0x0FFF /* Identifier of a configuration value. */
#define VBE_DISPI_CFG_MASK_SUPPORT      0x1000 /* Query whether the identifier is supported. */
#define VBE_DISPI_CFG_MASK_RESERVED     0xE000 /* For future extensions. Must be 0. */

/* VBE_DISPI_INDEX_CFG values. */
#define VBE_DISPI_CFG_ID_VERSION        0x0000 /* Version of the configuration interface. */
#define VBE_DISPI_CFG_ID_VRAM_SIZE      0x0001 /* VRAM size. */
#define VBE_DISPI_CFG_ID_3D             0x0002 /* 3D support. */
#define VBE_DISPI_CFG_ID_VMSVGA         0x0003 /* VMSVGA FIFO and ports are available. */

#define VGA_PORT_HGSMI_HOST             0x3b0
#define VGA_PORT_HGSMI_GUEST            0x3d0

#endif /* !VBOX_INCLUDED_Graphics_VBoxVideoVBE_h */

