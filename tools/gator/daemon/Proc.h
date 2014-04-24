/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PROC_H
#define PROC_H

class Buffer;
class DynBuf;

bool readProc(Buffer *const buffer, DynBuf *const printb, DynBuf *const b1, DynBuf *const b2, DynBuf *const b3);

#endif // PROC_H
