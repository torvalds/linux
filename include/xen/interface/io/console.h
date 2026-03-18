/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * console.h
 *
 * Console I/O interface for Xen guest OSes.
 *
 * Copyright (c) 2005, Keir Fraser
 */

#ifndef __XEN_PUBLIC_IO_CONSOLE_H__
#define __XEN_PUBLIC_IO_CONSOLE_H__

typedef uint32_t XENCONS_RING_IDX;

#define MASK_XENCONS_IDX(idx, ring) ((idx) & (sizeof(ring)-1))

struct xencons_interface {
    char in[1024];
    char out[2048];
    XENCONS_RING_IDX in_cons, in_prod;
    XENCONS_RING_IDX out_cons, out_prod;
/*
 * Flag values signaling from backend to frontend whether the console is
 * connected.  i.e. Whether it will be serviced and emptied.
 *
 * The flag starts as disconnected.
 */
#define XENCONSOLE_DISCONNECTED 1
/*
 * The flag is set to connected when the backend connects and the console
 * will be serviced.
 */
#define XENCONSOLE_CONNECTED    0
    uint8_t connection;
};

#endif /* __XEN_PUBLIC_IO_CONSOLE_H__ */
