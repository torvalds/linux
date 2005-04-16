/*
 *   fs/cifs/cifsencrypt.h
 *
 *   Copyright (c) International Business Machines  Corp., 2005
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Externs for misc. small encryption routines
 *   so we do not have to put them in cifsproto.h
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* md4.c */
extern void mdfour(unsigned char *out, unsigned char *in, int n);
/* smbdes.c */
extern void E_P16(unsigned char *p14, unsigned char *p16);
extern void E_P24(unsigned char *p21, unsigned char *c8, unsigned char *p24);
extern void D_P16(unsigned char *p14, unsigned char *in, unsigned char *out);
extern void E_old_pw_hash(unsigned char *, unsigned char *, unsigned char *);



