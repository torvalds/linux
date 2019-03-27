/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	A.R. Gordon (andrew.gordon@net-tel.co.uk).  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the FreeBSD project
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANDREW GORDON AND CONTRIBUTORS ``AS IS'' AND
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
 *	$FreeBSD$
 */



#include "sm_inter.h"

/* ------------------------------------------------------------------------- */
/*
  Data structures for recording monitored hosts

  The information held by the status monitor comprises a list of hosts
  that we have been asked to monitor, and, associated with each monitored
  host, one or more clients to be called back if the monitored host crashes.

  The list of monitored hosts must be retained over a crash, so that upon
  re-boot we can call the SM_NOTIFY procedure in all those hosts so as to
  cause them to start recovery processing.  On the other hand, the client
  call-backs are not required to be preserved: they are assumed (in the
  protocol design) to be local processes which will have crashed when
  we did, and so are discarded on restart.

  We handle this by keeping the list of monitored hosts in a file
  (/var/statd.state) which is mmap()ed and whose format is described
  by the typedef FileLayout.  The lists of client callbacks are chained
  off this structure, but are held in normal memory and so will be
  lost after a re-boot.  Hence the actual values of MonList * pointers
  in the copy on disc have no significance, but their NULL/non-NULL
  status indicates whether this host is actually being monitored or if it
  is an empty slot in the file.
*/

typedef struct MonList_s
{
  struct MonList_s *next;	/* Next in list or NULL			*/
  char notifyHost[SM_MAXSTRLEN + 1];	/* Host to notify		*/
  int notifyProg;		/* RPC program number to call		*/
  int notifyVers;		/* version number			*/
  int notifyProc;		/* procedure number			*/
  unsigned char notifyData[16];	/* Opaque data from caller		*/
} MonList;

typedef struct
{
  char hostname[SM_MAXSTRLEN + 1];	/* Name of monitored host	*/
  int notifyReqd;		/* TRUE if we've crashed and not yet	*/
				/* informed the monitored host		*/
  MonList *monList;		/* List of clients to inform if we	*/
				/* hear that the monitored host has	*/
				/* crashed, NULL if no longer monitored	*/
} HostInfo;


/* Overall file layout.  						*/

typedef struct
{
  int ourState;		/* State number as defined in statd protocol	*/
  int noOfHosts;	/* Number of elements in hosts[]		*/
  char reserved[248];	/* Reserved for future use			*/
  HostInfo hosts[1];	/* vector of monitored hosts			*/
} FileLayout;

#define	HEADER_LEN (sizeof(FileLayout) - sizeof(HostInfo))

/* ------------------------------------------------------------------------- */

/* Global variables		*/

extern FileLayout *status_info;	/* The mmap()ed status file		*/

extern int debug;		/* =1 to enable diagnostics to syslog	*/

/* Function prototypes		*/

extern HostInfo *find_host(char * /*hostname*/, int /*create*/);
extern void init_file(const char * /*filename*/);
extern void notify_hosts(void);
extern void sync_file(void);
extern int sm_check_hostname(struct svc_req *req, char *arg);
