/*
 * pvdrivers.h: Register of PV drivers product numbers.
 * Copyright (c) 2012, Citrix Systems Inc.
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
 */

#ifndef _XEN_PUBLIC_PVDRIVERS_H_
#define _XEN_PUBLIC_PVDRIVERS_H_

/*
 * This is the master registry of product numbers for
 * PV drivers. 
 * If you need a new product number allocating, please
 * post to xen-devel@lists.xensource.com.  You should NOT use
 * a product number without allocating one.
 * If you maintain a separate versioning and distribution path
 * for PV drivers you should have a separate product number so
 * that your drivers can be separated from others.
 *
 * During development, you may use the product ID to
 * indicate a driver which is yet to be released.
 */

#define PVDRIVERS_PRODUCT_LIST(EACH)                               \
        EACH("xensource-windows",       0x0001) /* Citrix */       \
        EACH("gplpv-windows",           0x0002) /* James Harper */ \
        EACH("linux",                   0x0003)                    \
        EACH("xenserver-windows-v7.0+", 0x0004) /* Citrix */       \
        EACH("xenserver-windows-v7.2+", 0x0005) /* Citrix */       \
        EACH("experimental",            0xffff)

#endif /* _XEN_PUBLIC_PVDRIVERS_H_ */
