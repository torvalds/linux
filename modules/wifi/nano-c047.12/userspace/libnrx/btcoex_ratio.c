/* Copyright (C) 2007 Nanoradio AB */
/* $Id:  $ */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include <unistd.h>
#include "nrx_lib.h"
#include "nrx_priv.h"

const char * btcoex_antenna_distribution_mib = "5.16.4";   /* Might be changed later. */

typedef struct {
      uint32_t bt_time;
      uint32_t wlan_time;
}bt_coex_antenna_distribution_t;

void show_help(void)
{
   fprintf(stderr, 
           "Usage: btcoex_ratio [-i ifname][-z]\n"
           "Options:\n"
           "   -i, change interface\n"
           "   -z, reset counters to zero.\n");
}

int
main(int argc, char **argv)
{
   int ch;
   nrx_context ctx;
   size_t len;
   bt_coex_antenna_distribution_t ant_dist;
   char *ifname = "eth1";
   int zero_flag = 0;
   int err_no;

   /* Options. */
   while((ch = getopt(argc, argv, "i:z")) != -1) {
      switch(ch) {
         case 'i':
            ifname = optarg;
            break;
         case 'z':
            zero_flag = 1;
            break;
         default:
            printf ("ERROR: Unknown option: -%c", ch);
            show_help();
            exit(1);
      }
   }
   if (optind < argc) {
      printf ("ERROR: Unknown option: ");
      while (optind < argc)
         printf ("%s ", argv[optind++]);
      printf ("\n");
      show_help();
      exit(1);
   }


   /* Init. */
   err_no = nrx_init_context(&ctx, ifname);
   if (err_no != 0)
   {
      fprintf(stderr, "ERROR: nrx_init_context() failed: %s\n", strerror(err_no));
      exit(2);
   }

   /* Get mib value. */
   len = sizeof(ant_dist);
   err_no = nrx_get_mib_val(ctx, 
                            btcoex_antenna_distribution_mib,
                            &ant_dist,
                            &len);
   if (err_no != 0)
   {
      fprintf(stderr, "ERROR: nrx_get_mib_val() failed: %s\n", strerror(err_no));
      exit(3);
   }

   /* Calculate and print percentage. */
   if (ant_dist.bt_time == 0 && ant_dist.wlan_time == 0)
      printf("WLAN media time: NaN\n");
   else
      printf("WLAN media time: %.1f%%\n", 
             100.0 * ant_dist.wlan_time / (0.0 + ant_dist.bt_time + ant_dist.wlan_time) );

   /* Reset mib to zero. */
   if (zero_flag)
   {
      ant_dist.bt_time = 0;
      ant_dist.wlan_time = 0;
      err_no = nrx_set_mib_val(ctx, 
                               btcoex_antenna_distribution_mib,
                               &ant_dist,
                               sizeof(ant_dist));
      if (err_no != 0)
      {
         fprintf(stderr, "ERROR: nrx_set_mib_val() failed: %s\n", strerror(err_no));
         exit(4);
      }
   }

   /* We're done. */
   nrx_free_context(ctx);
   return 0;
}
