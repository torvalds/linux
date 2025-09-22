/*	$OpenBSD: utiltest.c,v 1.2 2016/09/02 16:54:28 mikeb Exp $	*/

/*
 * Copyright (c) 2001 Niklas Hallqvist.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

#include "util.h"

int test_1 (char *, char *, int);

int
main (int argc, char *argv[])
{
  test_1 ("10.0.0.1", "10", 0);
  test_1 ("10.0.0.1", "isakmp", 0);
  test_1 ("10::1", "10", 0);
  test_1 ("10::1", "isakmp", 0);
  test_1 ("10.0x0.1", "10", -1);
  test_1 ("10.0.0.1", "telnet", -1);
  test_1 ("10::x:1", "10", -1);
  test_1 ("10::1", "telnet", -1);
  return 0;
}

int test_1 (char *address, char *port, int ok)
{
  struct sockaddr *sa;
#ifdef DEBUG
  struct sockaddr_in *sai;
  struct sockaddr_in6 *sai6;
  int i;
#endif
  int rv;

  printf ("test_1 (\"%s\", \"%s\") ", address, port);
  rv = text2sockaddr (address, port, &sa, 0, 0) == ok;
  printf (rv ? "OK" : "FAIL");
  printf ("\n");

#ifdef DEBUG
  printf ("af %d len %d ", sa->sa_family, sa->sa_len);
  if (sa->sa_family == AF_INET)
    {
      sai = (struct sockaddr_in *)sa;
      printf ("addr %08x port %d\n", ntohl (sai->sin_addr.s_addr),
	      ntohs (sai->sin_port));
    }
  else
    {
      sai6 = (struct sockaddr_in6 *)sa;
      printf ("addr ");
      for (i = 0; i < sizeof sai6->sin6_addr; i++)
	printf ("%02x", sai6->sin6_addr.s6_addr[i]);
      printf (" port %d\n", ntohs (sai6->sin6_port));
    }
#endif
  return rv;
}
