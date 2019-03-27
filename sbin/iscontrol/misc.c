/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2010 Daniel Braniss <danny@cs.huji.ac.il>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 | $Id: misc.c,v 2.1 2006/11/12 08:06:51 danny Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dev/iscsi_initiator/iscsi.h>
#include "iscontrol.h"

static inline char
c2b(unsigned char c)
{
     switch(c) {
     case '0' ... '9':
	  return c - '0';
     case 'a' ... 'f':
	  return c - 'a' + 10;
     case 'A' ... 'F':
	  return c - 'A' + 10;
     }
     return 0;
}

static char 	base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                   "abcdefghijklmnopqrstuvwxyz"
	                   "0123456789+/";

static __inline unsigned char
c64tobin(unsigned char c64)
{
     int	i;
     for(i = 0; i < 64; i++)
	  if(base64[i] == c64)
	       break;
     return i;
}
/*
 | according to rfc3720, the binary string
 | cannot be larger than 1024 - but i can't find it :-) XXX
 | not enforced yet.
 */
int
str2bin(char *str, char **rsp)
{
     char	*src, *dst, *tmp;
     int	i, len = 0;

     src = str;
     tmp = NULL;
     if(strncasecmp("0x", src, 2) == 0) {
	  src += 2;
	  len = strlen(src);
	  
	  if((tmp = malloc((len+1)/2)) == NULL) {
	       // XXX: print some error?
	       return 0;
	  }
	  dst = tmp;
	  if(len & 1)
	       *dst++ = c2b(*src++);
	  while(*src) {
	       *dst = c2b(*src++) << 4;
	       *dst++ |= c2b(*src++);
	  }
	  len = dst - tmp;
     } else
     if(strncasecmp("0b", src , 2) == 0) {
	  // base64
	  unsigned char b6;

	  src += 2;
	  len = strlen(src) / 4 * 3;
	  if((tmp = malloc(len)) == NULL) {
	       // XXX: print some error?
	       return 0;
	  }
	  dst = tmp; 
	  i = 0;
	  while(*src && ((b6 = c64tobin(*src++)) != 64)) {
	       switch(i % 4) {
	       case 0:
		    *dst = b6 << 2;
		    break;
	       case 1:
		    *dst++ |= b6 >> 4;
		    *dst = b6 << 4;
		    break;
	       case 2:
		    *dst++ |= b6 >> 2;
		    *dst = b6 << 6;
		    break;
	       case 3:
		    *dst++ |= b6;
		    break;
	       }
	       i++;
	  }
	  len = dst - tmp;
     }
     else {
	  /*
	   | assume it to be an ascii string, so just copy it
	   */
	  len = strlen(str);
	  if((tmp = malloc(len)) == NULL)
	       return 0;
	  dst = tmp;
	  src = str;
	  while(*src)
	       *dst++ = *src++;
     }

     *rsp = tmp;
     return len;
}

char *
bin2str(char *encoding, unsigned char *md, int blen)
{
     int	len;
     char	*dst, *ds;
     unsigned char *cp;

     if(strncasecmp(encoding, "0x", 2) == 0) {
	  char	ofmt[5];

	  len = blen * 2;
	  dst = malloc(len + 3);
	  strcpy(dst, encoding);
	  ds = dst + 2;
	  cp = md;
	  sprintf(ofmt, "%%02%c", encoding[1]);
	  while(blen-- > 0) {
	       sprintf(ds, ofmt, *cp++);
	       ds += 2;
	  }
	  *ds = 0;
	  return dst;
     }
     if(strncasecmp(encoding, "0b", 2) == 0) {
	  int i, b6;

	  len = (blen + 2) * 4 / 3;
	  dst = malloc(len + 3);
	  strcpy(dst, encoding);
	  ds = dst + 2;
	  cp = md;
	  b6 = 0; // to keep compiler happy.
	  for(i = 0; i < blen; i++) {
	       switch(i % 3) {
	       case 0:
		    *ds++ = base64[*cp >> 2];
		    b6 = (*cp & 0x3) << 4;
		    break;
	       case 1:
		    b6 += (*cp >> 4);
		    *ds++ = base64[b6];
		    b6 = (*cp & 0xf) << 2;
		    break;
	       case 2:
		    b6 += (*cp >> 6);
		    *ds++ = base64[b6];
		    *ds++ = base64[*cp & 0x3f];
	       }
	       cp++;
	  }
	  switch(blen % 3) {
	  case 0:
	       break;
	  case 1:
	       *ds++ = base64[b6];
	       *ds++ = '=';
	       *ds++ = '=';
	       break;
	  case 2:
	       *ds++ = base64[b6];
	       *ds++ = '=';
	       break;
	  }

	  *ds = 0;
	  return dst;
     }

     return NULL;
}
