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
 | $Id: auth_subr.c,v 2.2 2007/06/01 08:09:37 danny Exp $
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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include <md5.h>
#include <sha.h>

#include <dev/iscsi_initiator/iscsi.h>
#include "iscontrol.h"

static int
chapMD5(char id, char *cp, char *chapSecret, unsigned char *digest)
{
     MD5_CTX	ctx;
     char	*tmp;
     int	len;

     debug_called(3);

     MD5Init(&ctx);

     MD5Update(&ctx, &id, 1);

     if((len = str2bin(chapSecret, &tmp)) == 0) {
	  // print error
	  return -1;
     }
     MD5Update(&ctx, tmp, len);
     free(tmp);

     if((len = str2bin(cp, &tmp)) == 0) {
	  // print error
	  return -1;
     }
     MD5Update(&ctx, tmp, len);
     free(tmp);

     MD5Final(digest, &ctx);
     

     return 0;
}

static int
chapSHA1(char id, char *cp, char *chapSecret, unsigned char *digest)
{
     SHA1_CTX	ctx;
     char	*tmp;
     int	len;

     debug_called(3);

     SHA1_Init(&ctx);
     
     SHA1_Update(&ctx, &id, 1);

     if((len = str2bin(chapSecret, &tmp)) == 0) {
	  // print error
	  return -1;
     }
     SHA1_Update(&ctx, tmp, len);
     free(tmp);

     if((len = str2bin(cp, &tmp)) == 0) {
	  // print error
	  return -1;
     }
     SHA1_Update(&ctx, tmp, len);
     free(tmp);

     SHA1_Final(digest, &ctx);

     return 0;
    
}
/*
 | the input text format can be anything that the rfc3270 defines
 | (see section 5.1 and str2bin)
 | digest length for md5 is 128bits, and for sha1 is 160bits.
 | digest is an ASCII string which represents the bits in 
 | hexadecimal or base64 according to the challenge(cp) format
 */
char *
chapDigest(char *ap, char id, char *cp, char *chapSecret)
{
     int	len;
     unsigned	char digest[20];
     char	encoding[3];

     debug_called(3);

     len = 0;
     if(strcmp(ap, "5") == 0 && chapMD5(id, cp, chapSecret, digest) == 0)
	  len = 16;
     else
     if(strcmp(ap, "7") == 0 && chapSHA1(id, cp, chapSecret, digest) == 0)
	  len = 20;

     if(len) {
	  sprintf(encoding, "%.2s", cp);
	  return bin2str(encoding, digest, len);
     }

     return NULL;
}

char *
genChapChallenge(char *encoding, uint len)
{
     int	fd;
     unsigned	char tmp[1024];

     if(len > sizeof(tmp))
	  return NULL;

     if((fd = open("/dev/random", O_RDONLY)) != -1) {
	  read(fd, tmp, len);
	  close(fd);
	  return bin2str(encoding, tmp, len);
     }
     perror("/dev/random");
     // make up something ...
     return NULL;
}

#ifdef TEST_AUTH
static void
puke(char *str, unsigned char *dg, int len)
{
     printf("%3d] %s\n     0x", len, str);
     while(len-- > 0)
	  printf("%02x", *dg++);
     printf("\n");
}

main(int cc, char **vv)
{
     char *p, *ap, *ip, *cp, *chapSecret, *digest;
     int len;

#if 0
     ap = "5";
     chapSecret = "0xa5aff013dd839b1edd31ee73a1df0b1b";
//     chapSecret = "abcdefghijklmnop";
     len = str2bin(chapSecret, &cp);
     puke(chapSecret, cp, len);

     ip = "238";
     cp = "0xbd456029";

     
     if((digest = chapDigest(ap, ip, cp, chapSecret)) != NULL) {
	  len = str2bin(digest, &cp);
	  puke(digest, cp, len);
     }
#else
     printf("%d] %s\n", 24, genChallenge("0X", 24));
#endif
}
#endif
