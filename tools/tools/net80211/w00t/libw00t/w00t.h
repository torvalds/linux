/*-
 * Copyright (c) 2006, Andrea Bittau <a.bittau@cs.ucl.ac.uk>
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
 * $FreeBSD$
 */
#ifndef __W00T_H__
#define __W00T_H__

#include <net80211/ieee80211.h>
#include <net80211/ieee80211_freebsd.h>

int str2mac(char *mac, char *str);
void mac2str(char *str, char *mac);
int open_tx(char *iface);
int open_rx(char *iface);
int open_rxtx(char *iface, int *rx, int *tx);
int inject(int fd, void *buf, int len);
int inject_params(int fd, void *buf, int len,
		  struct ieee80211_bpf_params *params);
int sniff(int fd, void *buf, int len);
void *get_wifi(void *buf, int *len);
short seqfn(unsigned short seq, unsigned short fn);
int send_ack(int fd, char *mac);
unsigned short seqno(struct ieee80211_frame *wh);
int open_tap(char *iface);
int set_iface_mac(char *iface, char *mac);
int str2wep(char *wep, int *len, char *str);
int wep_decrypt(struct ieee80211_frame *wh, int len, char *key, int klen);
void wep_encrypt(struct ieee80211_frame *wh, int len, char *key, int klen);
int frame_type(struct ieee80211_frame *wh, int type, int stype);
void hexdump(void *b, int len);
int elapsed(struct timeval *past, struct timeval *now);
char *known_pt(struct ieee80211_frame *wh, int *len);

#endif /* __W00T_H__ */
