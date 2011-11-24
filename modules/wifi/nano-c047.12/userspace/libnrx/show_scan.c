/* Copyright (C) 2007 Nanoradio AB */
/* $Id: show_scan.c 11045 2009-02-10 22:07:21Z kath $ */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include <unistd.h>
#include "nrx_lib.h"
#include "nrx_priv.h"

int
nrx_show_scan(nrx_context ctx, char *nets, size_t len);

int
main(int argc, char **argv)
{
   nrx_context ctx;
   char nets[4096];
   size_t len = sizeof(nets);
   
   char *from_file = NULL;
   char *to_file = NULL;
   char *ifname = "eth1";
   int ch;
   int ignore;

   while((ch = getopt(argc, argv, "o:i:f:")) != -1) {
      switch(ch) {
         case 'f':
            from_file = optarg;
            break;
         case 'i':
            ifname = optarg;
            break;
         case 'o':
            to_file = optarg;
            break;
         default:
            fprintf(stderr, "Usage: show_scan [-i ifname][-f infile][-o outfile]\n");
            exit(1);
      }
   }
   
   if(nrx_init_context(&ctx, ifname) != 0)
      err(1, "nrx_init_context");

   if(from_file != NULL) {
      FILE *f = fopen(from_file, "r");
      if(f == NULL)
         err(1, "%s", from_file);
      len = fread(nets, 1, sizeof(nets), f);
      fclose(f);
   } else {
      nrx_get_scan_list(ctx, nets, &len);
   }
   if(to_file != NULL) {
      FILE *f = fopen(to_file, "w");
      if(f == NULL)
         err(1, "%s", to_file);
      ignore = fwrite(nets, 1, len, f);
      fclose(f);
   } else {
      nrx_show_scan(ctx, nets, len);
   }

   nrx_free_context(ctx);
   return 0;
}
