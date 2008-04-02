/*
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
 * Copyright (C) IBM Corp. 2006
 */

#ifndef _XEN_XENCOMM_H_
#define _XEN_XENCOMM_H_

/* A xencomm descriptor is a scatter/gather list containing physical
 * addresses corresponding to a virtually contiguous memory area. The
 * hypervisor translates these physical addresses to machine addresses to copy
 * to and from the virtually contiguous area.
 */

#define XENCOMM_MAGIC 0x58434F4D /* 'XCOM' */
#define XENCOMM_INVALID (~0UL)

struct xencomm_desc {
    uint32_t magic;
    uint32_t nr_addrs; /* the number of entries in address[] */
    uint64_t address[0];
};

#endif /* _XEN_XENCOMM_H_ */
