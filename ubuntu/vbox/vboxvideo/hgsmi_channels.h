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

#ifndef VBOX_INCLUDED_Graphics_HGSMIChannels_h
#define VBOX_INCLUDED_Graphics_HGSMIChannels_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*
 * Each channel has an 8 bit identifier. There are a number of predefined
 * (hardcoded) channels.
 *
 * HGSMI_CH_HGSMI channel can be used to map a string channel identifier
 * to a free 16 bit numerical value. values are allocated in range
 * [HGSMI_CH_STRING_FIRST;HGSMI_CH_STRING_LAST].
 */


/* Predefined channel identifiers. Used internally by VBOX to simplify the channel setup. */
/* A reserved channel value */
#define HGSMI_CH_RESERVED     0x00
/* HGCMI: setup and configuration */
#define HGSMI_CH_HGSMI        0x01
/* Graphics: VBVA */
#define HGSMI_CH_VBVA         0x02
/* Graphics: Seamless with a single guest region */
#define HGSMI_CH_SEAMLESS     0x03
/* Graphics: Seamless with separate host windows */
#define HGSMI_CH_SEAMLESS2    0x04
/* Graphics: OpenGL HW acceleration */
#define HGSMI_CH_OPENGL       0x05


/* Dynamically allocated channel identifiers. */
/* The first channel index to be used for string mappings (inclusive) */
#define HGSMI_CH_STRING_FIRST 0x20
/* The last channel index for string mappings (inclusive) */
#define HGSMI_CH_STRING_LAST  0xff


/* Check whether the channel identifier is allocated for a dynamic channel */
#define HGSMI_IS_DYNAMIC_CHANNEL(_channel) (((u8)(_channel) & 0xE0) != 0)


#endif /* !VBOX_INCLUDED_Graphics_HGSMIChannels_h */
