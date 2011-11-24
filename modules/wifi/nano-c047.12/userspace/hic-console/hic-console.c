/* Simplistic HIC console interface. */

/* $Id: hic-console.c,v 1.1 2006-12-07 16:03:42 joda Exp $ */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <inttypes.h>
#include <linux/if.h>
#include <errno.h>
#include <err.h>

#include "nanoioctl.h"

int debuglevel;
int loop_mode;

void
printbuf(const void *data, size_t len, const char *prefix)
{
    int i, j;
    const unsigned char *p = data;
    for(i = 0; i < len; i += 16) {
	printf("%s %04x: ", prefix, i);
	for(j = 0; j < 16; j++) {
	    if(i + j < len)
		printf("%02x ", p[i+j]);
	    else
		printf("   ");
	}
	printf(" : ");
	for(j = 0; j < 16; j++) {
	    if(i + j < len) {
#define isprint(c) ((c) >= 32 && (c) <= 126)
		if(isprint(p[i+j]))
		    printf("%c", p[i+j]);
		else
		    printf(".");
	    } else
		printf(" ");
	}
	printf("\n");
    }
}

struct hic_header {
   uint16_t len;
   uint16_t type;
   uint8_t pad;
   uint8_t epad;
};

void
print_console(unsigned char *buf, size_t len)
{
   struct hic_header *h = (struct hic_header*)buf;
   unsigned char *s;
   size_t l;

   s = buf + 5 + h->pad;
   l = h->len + 2 - 5 - h->pad - h->epad;

   if(debuglevel)
      printbuf(s, l, "<-");
   if(loop_mode == 0 && l == 9 && memcmp(s, "\n\rAddams>", 9) == 0)
      return;

   if(h->type == 0x104) {
      if(*s != 0) {
         fprintf(stderr, "command had non-zero exit status\n");
      }
   } else {
      printf("%.*s", (int)l, s);
   }
}

int get_reply(const char *ifname, int count, void *reply, size_t *len)
{
   int s;
   struct ifreq ifr;
   struct nanoioctl nr;

   s = socket(AF_INET, SOCK_DGRAM, 0);
   if(s < 0)
      err(1, "socket");
   
   strcpy(ifr.ifr_name, ifname);
   ifr.ifr_data = (void*)&nr;
   memset(&nr, 0, sizeof(nr));
   nr.magic = NR_MAGIC;
   nr.tid = 0x00020000;
   nr.length = sizeof(nr.data);

   while(count--) {
      if(ioctl(s, SIOCNRGETREPLY, &ifr) < 0) {
         if(errno == EAGAIN) {
            usleep(100000);
            continue;
         }
         err(1, "ioctl");
      }
      memcpy(reply, nr.data, nr.length);
      *len = nr.length;
      break;
   }
   close(s);

   if(count < 0)
      return -1;

   return 0;
}

void
print_reply(const char *ifname, int count)
{
   unsigned char buf[512];
   size_t len;

   while(1) {
      len = sizeof(buf);
      if(get_reply(ifname, 5, buf, &len) == -1)
         break;
      print_console(buf, len);
   }
}

int send_command(const char *ifname, char *command)
{
   int s;
   struct ifreq ifr;
   struct nanoioctl nr;

   char cmd[512] = {
      0x2a, 0x00, /* len */
      0x04, /* HIC_MESSAGE_TYPE_CONSOLE */
      0x00, /* HIC_MAC_CONSOLE_REQ */
      0x03, 0x00, 0x00, 0x00, /* pad */
   };

   s = socket(AF_INET, SOCK_DGRAM, 0);
   if(s < 0)
      err(1, "socket");
   
   strcpy(ifr.ifr_name, ifname);
   ifr.ifr_data = (void*)&nr;
   memset(&nr, 0, sizeof(nr));
   nr.magic = NR_MAGIC;
   nr.length = 8;

   if(strchr(command, '\n') == NULL)
      nr.length += snprintf(cmd + nr.length, sizeof(cmd) - nr.length, "%s\n", command);
   else
      nr.length += snprintf(cmd + nr.length, sizeof(cmd) - nr.length, "%s", command);
   
   *(uint16_t*)cmd = nr.length - 2;
   memcpy(nr.data, cmd, nr.length);
   if(debuglevel)
      printbuf(nr.data, nr.length, "->");
   if(ioctl(s, SIOCNRSENDCOMMAND, &ifr) < 0)
      err(1, "ioctl");

   close(s);

   return 0;
}

void
usage(void)
{
   fprintf(stderr, "hic-console [-d][-i ifname][-l] [command]\n");
   exit(1);
}

int
main(int argc, char **argv)
{
   int ch;
   char *ifname = "eth1";

   while((ch = getopt(argc, argv, "di:l")) != -1) {
      switch(ch) {
         case 'i':
            ifname = optarg;
            break;
         case 'd':
            debuglevel++;
            break;
         case 'l':
            loop_mode++;
            break;
         case '?':
         default:
            usage();
      }
   }

   if(loop_mode) {
      print_reply(ifname, 5);
      while(1) {
         char buf[256];
         if(fgets(buf, sizeof(buf), stdin) == NULL)
            break;
         send_command(ifname, buf);
         print_reply(ifname, 10);
      }
   } else {
      print_reply(ifname, 5);
      send_command(ifname, argv[optind]);
      print_reply(ifname, 10);
      printf("\n");
   }

   return 0;
}
