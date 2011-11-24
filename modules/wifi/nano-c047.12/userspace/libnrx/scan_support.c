/* Copyright (C) 2007 Nanoradio AB */
/* $Id: scan_support.c 14772 2010-03-23 15:02:17Z joda $ */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include "nrx_lib.h"
#include "nrx_priv.h"

/* horrible mess of stuff form wireless.h */
#define SIOCGIWFREQ	0x8B05
#define SIOCGIWMODE	0x8B07
#define SIOCGIWAP	0x8B15
#define SIOCGIWESSID	0x8B1B
#define SIOCGIWRATE	0x8B21
#define SIOCGIWENCODE	0x8B2B
#define IW_ENCODE_DISABLED	0x8000
#define IWEVQUAL	0x8C01
#define IW_QUAL_QUAL_INVALID	0x10
#define IW_QUAL_LEVEL_INVALID	0x20
#define IW_QUAL_NOISE_INVALID	0x40
#define IWEVCUSTOM	0x8C02
#define IWEVGENIE	0x8C05

struct iw_event {
   uint16_t len;
   uint16_t cmd;
   union {
      struct sockaddr ap;
      uint32_t number;
      struct {
         void *pointer;
         uint16_t length;
         uint16_t flags;
         char data;
      } point;
      struct {
         uint16_t length;
         uint16_t flags;
         char data;
      } point19;
      struct iw_param {
         int32_t value;
         uint8_t fixed;
         uint8_t disabled;
         uint16_t flags;
      } param;
      struct iw_quality {
         uint8_t qual;
         uint8_t level;
         uint8_t noise;
         uint8_t updated;
      } qual;
      struct {
         int32_t mantissa;
         int16_t exponent;
         uint8_t index;
         uint8_t flags;
      } freq;
   } u;
};

static int xsnprintf(char *s, size_t size, size_t offset, const char *fmt, ...)
{
   int n;
   /* make sure offset doesn't pass end of s */
   if(offset > size)
      offset = size;
   va_list ap;
   va_start(ap, fmt);
   n = vsnprintf(s + offset, size - offset, fmt, ap);
   va_end(ap);
   return n;
}

static void
format_rate(char *rates, size_t size, char *s, size_t len)
{
   struct iw_param param;
   size_t offset = 0;
   int i;
   if(len > 0)
      *s = '\0';
   for(i = 0; i < size / sizeof(param); i++) {
      memcpy(&param, rates + i * sizeof(param), sizeof(param));
      if(i > 0)
         offset += xsnprintf(s, len, offset, " ");
      switch(param.value % 1000000) {
         case 0:
            offset += xsnprintf(s, len, offset, "%u", param.value / 1000000);
            break;
         case 500000:
            offset += xsnprintf(s, len, offset, "%u.5", param.value / 1000000);
            break;
         default:
            offset += xsnprintf(s, len, offset, "%.2f", param.value / 1e6);
            break;
      }
   }
}

static void
format_data(char *data, size_t size, char *s, size_t len)
{
   size_t offset = 0;
   int i;
   if(len > 0)
      *s = '\0';
   for(i = 0; i < size; i++)
      offset += xsnprintf(s, len, offset, "%02x", (unsigned char)data[i]);
   if(offset > len) {
      /* indicate overflowed data */
      if(len >= 4)
         s[len - 4] = '.';
      if(len >= 3)
         s[len - 3] = '.';
      if(len >= 2)
         s[len - 2] = '.';
   }
}

static void
output(int netno, const char *var, const char *val)
{
   const char *p;
   int f = 0;
   for(p = val; *p != '\0'; p++)
      if(!isprint(*p) ||
         isspace(*p) || 
         *p == '\'' ||
         *p == '"' ||
         *p == '$' ||
         *p == '*' ||
         *p == '?') {
         f = 1;
         break;
      }
   if(!f) {
      printf("NET_%d_%s=%s\n", netno, var, val);
      return;
   }
   printf("NET_%d_%s=", netno, var);
   fputc('\'', stdout);
   for(p = val; *p != '\0'; p++) {
      switch(*p) {
         case '\'':
            fputs("'\\''", stdout);
            break;
         default:
            if(isprint(*p) || *p == ' ')
               fputc(*p, stdout);
            else
               printf("%%%02x", (unsigned char)*p);
            break;
      }
   }
   fputc('\'', stdout);
   fputc('\n', stdout);
}

int
nrx_show_scan(nrx_context ctx, char *nets, size_t len)
{
   struct iw_event ev;
   void *p = NULL;
   size_t l;
   int netno = -1;
   char tmp[64];
   
   while(nrx_scan_get_event(ctx, nets, len, &p, &l) == 0) {
      if(l > sizeof(ev))
	 memcpy(&ev, p, sizeof(ev));
      else
	 memcpy(&ev, p, l);
      switch(ev.cmd) {
         case SIOCGIWAP:
            netno++;
            snprintf(tmp, sizeof(tmp), "%02x:%02x:%02x:%02x:%02x:%02x", 
                     (unsigned char)ev.u.ap.sa_data[0],
                     (unsigned char)ev.u.ap.sa_data[1],
                     (unsigned char)ev.u.ap.sa_data[2],
                     (unsigned char)ev.u.ap.sa_data[3],
                     (unsigned char)ev.u.ap.sa_data[4],
                     (unsigned char)ev.u.ap.sa_data[5]);
            output(netno, "BSSID", tmp);
            break;
         case SIOCGIWESSID: {
            size_t offset, size;
            if(nrx_check_wx_version(ctx, 19) >= 0) {
               offset = &ev.u.point19.data - (char*)&ev;
               size = ev.u.point19.length;
            } else {
               offset = &ev.u.point.data - (char*)&ev;
               size = ev.u.point.length;
            }
            strncpy(tmp, p + offset, size);
            tmp[size] = '\0';
            output(netno, "SSID", tmp);
            break;
         }
         case SIOCGIWMODE:
            switch(ev.u.number) {
               case 1:
                  strlcpy(tmp, "IBSS", sizeof(tmp));
                  break;
               case 3:
                  strlcpy(tmp, "BSS", sizeof(tmp));
                  break;
               default:
                  snprintf(tmp, sizeof(tmp), "unknown_%u", ev.u.number);
                  break;
            }
            output(netno, "MODE", tmp);
            break;
         case SIOCGIWENCODE: {
            uint16_t flags;
            if(nrx_check_wx_version(ctx, 19) >= 0)
               flags = ev.u.point19.flags;
            else 
               flags = ev.u.point.flags;
            if(!(flags & IW_ENCODE_DISABLED))
               strlcpy(tmp, "WEP", sizeof(tmp));
            else
               *tmp = '\0';
            output(netno, "ENCODE", tmp);
            break;
         }
         case IWEVQUAL:
            if((ev.u.qual.updated & IW_QUAL_QUAL_INVALID) == 0 &&
               ev.u.qual.qual != 0)
               printf("NET_%d_QUAL=%d\n", netno, 
                      (int)ev.u.qual.qual);
            if((ev.u.qual.updated & IW_QUAL_LEVEL_INVALID) == 0 &&
               ev.u.qual.level != 0)
               printf("NET_%d_SIGNAL=%d\n", netno, 
                      (int)ev.u.qual.level - 256);
            if((ev.u.qual.updated & IW_QUAL_NOISE_INVALID) == 0 &&
               ev.u.qual.noise != 0)
               printf("NET_%d_NOISE=%d\n", netno, 
                      (int)ev.u.qual.noise - 256);
            break;
         case SIOCGIWFREQ: {
            nrx_channel_t channel;
            nrx_convert_frequency_to_channel(ctx, 
                                             ev.u.freq.mantissa,
                                             &channel);
            printf("NET_%d_CHANNEL=%u\n", netno, channel);
            break; 
         }
         case SIOCGIWRATE: 
            format_rate(p + 4, l - 4, tmp, sizeof(tmp));
            output(netno, "RATE", tmp);
            break;
         case IWEVCUSTOM: {
            size_t offset, size;
            if(nrx_check_wx_version(ctx, 19) >= 0) {
               offset = &ev.u.point19.data - (char*)&ev;
               size = ev.u.point19.length;
            } else {
               offset = &ev.u.point.data - (char*)&ev;
               size = ev.u.point.length;
            }
            strncpy(tmp, p + offset, size);
            tmp[size] = '\0';
            output(netno, "EXTRA", tmp);
            break;
         }
         case IWEVGENIE: {
            size_t offset, size;
            if(nrx_check_wx_version(ctx, 19) >= 0) {
               offset = &ev.u.point19.data - (char*)&ev;
               size = ev.u.point19.length;
            } else {
               offset = &ev.u.point.data - (char*)&ev;
               size = ev.u.point.length;
            }
            format_data(p + offset, size, tmp, sizeof(tmp));
            output(netno, "GENIE", tmp);
            break;
         }
         default:
            format_data(p + 4, l - 4, tmp, sizeof(tmp));
            printf("NET_%d_%x=%s\n", netno, ev.cmd, tmp);
            break;
      }
   }
   printf("NET_NUMNETS=%d\n", netno + 1);
   return 0;
}

