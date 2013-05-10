/*
 *  Serial Port driver for a NWP uart device
 *
 *    Copyright (C) 2008 IBM Corp., Benjamin Krill <ben@codiert.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#ifndef _NWPSERIAL_H
#define _NWPSERIAL_H

int nwpserial_register_port(struct uart_port *port);
void nwpserial_unregister_port(int line);

#endif /* _NWPSERIAL_H */
