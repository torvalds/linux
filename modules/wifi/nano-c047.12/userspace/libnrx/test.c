/* Copyright (C) 2007 Nanoradio AB */
/* $Id: test.c 9954 2008-09-15 09:41:38Z joda $ */

#include <stdio.h>
#include <err.h>
#include "nrx_lib.h"


#include "nrx_priv.h"

#if 0

int main() {
   int x, ret;
   nrx_context ctx;
   size_t x_size = sizeof(x);

   if(nrx_init_context(&ctx, "eth1") != 0)
      err(1, "nrx_init_context");

      ret = nrx_get_mib_val(ctx, "1.1.1.22", &x, &x_size);
      if (!ret)
         printf("Value MIB_dot11MultiDomainCapabilityEnabled %d\n", x);
      else
         printf("Failed (err %d, %s)\n", x, strerror(x));
   return 0;
}

#endif 


#include <time.h>
int main() {
   int x, ret;
   nrx_context ctx;
   size_t x_size = sizeof(x);

   if(nrx_init_context(&ctx, "eth1") != 0)
      err(1, "nrx_init_context");

   do {
      struct timespec t = {1, 0}; // {sec, nanosec}
      ret = nrx_get_mib_val(ctx, "5.2.9", &x, &x_size);
      if (!ret)
         printf("Value MIB_dot11beaconLossRate %d\n", x);
      else 
         printf("Failed (err %d, %s)\n", x, strerror(x));
      nanosleep(&t, NULL);
   } while (!ret);
   return 0;
}


