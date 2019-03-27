/******************************************************************************
 * arch-x86_64.h
 * 
 * Guest OS interface to x86 64-bit Xen.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2004-2006, K A Fraser
 */

#include "arch-x86/xen.h"

/*
 * ` enum neg_errnoval
 * ` HYPERVISOR_set_callbacks(unsigned long event_selector,
 * `                          unsigned long event_address,
 * `                          unsigned long failsafe_selector,
 * `                          unsigned long failsafe_address);
 * `
 * Register for callbacks on events.  When an event (from an event
 * channel) occurs, event_address is used as the value of eip.
 *
 * A similar callback occurs if the segment selectors are invalid.
 * failsafe_address is used as the value of eip.
 *
 * On x86_64, event_selector and failsafe_selector are ignored (???).
 */
