/*-
 * Copyright (c) 2010-2011 Monthadar Al Jaberi, TerraNet AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "if_wtapioctl.h"

static int dev = -1;

static void create(int id)
{
    if(ioctl(dev, WTAPIOCTLCRT, &id) < 0){
	printf("error creating wtap with id=%d\n", id);
    }
}

static void delete(int id)
{
    if(ioctl(dev, WTAPIOCTLDEL, &id) < 0){
	printf("error deleting wtap with id=%d\n", id);
    }
}

int main( int argc, const char* argv[])
{
    if(argc != 3){
      printf("usage: %s [c | d] wtap_id\n", argv[0]);
      return -1;
    }
    int id = atoi(argv[2]);
    if(!(id >= 0 && id < 64)){
	printf("wtap_id must be between 0 and 7\n");
	return -1;
    }
    dev = open("/dev/wtapctl", O_RDONLY);
    if(dev < 0){
      printf("error opening wtapctl cdev\n");
      return -1;
    }
    switch((char)*argv[1]){
      case 'c':
	create(id);
	break;
      case 'd':
	delete(id);
	break;
      default:
	printf("wtap ioctl: unknown command '%c'\n", *argv[1]);
	return -1;
    }
    return 0;
}
