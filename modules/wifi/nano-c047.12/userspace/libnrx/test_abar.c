
/* 
 * Nota Bene: This code is for testing only and should not be used in
 * an application as trigger events might be missed. This happens as
 * registring triggers and receiving them are done in separate
 * processes. Hence, the trigger may be received inbetween a trigger
 * is enabled and its callback is registered.
 */

#include <stdio.h>
#include <err.h>
#include <pthread.h>
#include "nrx_lib.h"
#include "nrx_priv.h"

#define IWEVCUSTOM	0x8C02		/* Driver specific ascii string */

int
nrx_event_dispatch(nrx_context ctx, 
                   int operation, 
                   void *event_data, 
                   size_t event_data_size,
                   void *user_data)
{
   struct nrx_we_callback_data *d;
   
   if(operation == NRX_CB_CANCEL)
      return 0;

   d = event_data;

   nrx_printbuf(d->data, d->len, "RX-->");
   return 0;
}


int levels[] = {-150, -145, -140, -135, -130, -125, -120, -115, -110, -105, -100, -95, -90, -85, -80, -75, -70, -65, -60, -55, -50};
const int hyst = 0;             /* hysteresis */

char levelstr[] = "                                      ";

#define CB(level, rising)                                                                                       \
        int cb_##level##rising(nrx_context ctx,                                                                 \
                               int operation,                                                                   \
                               void *event_data,                                                                \
                               size_t event_data_size,                                                          \
                               void *user_data)                                                                 \
        {                                                                                                       \
           nrx_event_mibtrigger_t *ed = (nrx_event_mibtrigger_t *)event_data;                                   \
           int id, value;                                                                                       \
           if (operation == NRX_CB_CANCEL) {                                                                    \
              printf("Callback: Canceled level %d %s\n", levels[level], rising ? "rising":"falling");           \
              if (levelstr[level] == 'c')                                                                       \
                 levelstr[level] = 'C';                                                                         \
              else                                                                                              \
                 levelstr[level] = 'c';                                                                         \
              printf("%s\n", levelstr);                                                                         \
              return 0;                                                                                         \
           }                                                                                                    \
           id = (int)ed->id;                                                                                    \
           value = (int)ed->value;                                                                              \
           printf("Callback: Trig id %d. Passed level %d %s (%d)\n",                                            \
                  id,                                                                                           \
                  levels[level],                                                                                \
                  rising ? "rising":"falling",                                                                  \
                  value);                                                                                       \
           levelstr[level] = rising ? '^':'v';                                                                  \
           printf("%s\n", levelstr);                                                                            \
           return 0;                                                                                            \
        }

CB(0,1)                         /* rising */
CB(1,1)
CB(2,1)
CB(3,1)
CB(4,1)
CB(5,1)
CB(6,1)
CB(7,1)
CB(8,1)
CB(9,1)
CB(10,1)
CB(11,1)
CB(12,1)
CB(13,1)
CB(14,1)
CB(15,1)
CB(16,1)
CB(17,1)
CB(18,1)
CB(19,1)
CB(20,1)

CB(0,0)                         /* falling */
CB(1,0)
CB(2,0)
CB(3,0)
CB(4,0)
CB(5,0)
CB(6,0)
CB(7,0)
CB(8,0)
CB(9,0)
CB(10,0)
CB(11,0)
CB(12,0)
CB(13,0)
CB(14,0)
CB(15,0)
CB(16,0)
CB(17,0)
CB(18,0)
CB(19,0)
CB(20,0)

#define REGISTER(level, rising)                                                                                 \
{                                                                                                               \
   int thr_id, ret;                                                                                             \
   ret = nrx_enable_rssi_threshold(ctx,                                                                         \
                                   &thr_id,                                                                     \
                                   rising ? levels[level] + hyst : levels[level] - hyst,                        \
                                   1000,                                                                        \
                                   rising?NRX_THR_RISING:NRX_THR_FALLING,                                       \
                                   NRX_DT_BEACON);                                                              \
   if (ret != 0) {                                                                                              \
      printf("Registration for level %d %s FAILED\n", levels[level], rising ? "rising":"falling");              \
      levelstr[level] = 'f';                                                                                    \
   }                                                                                                            \
   else {                                                                                                       \
      ret = nrx_register_mib_trigger_event_handler(ctx, thr_id, cb_##level##rising, NULL);                      \
      if (ret == 0)                                                                                             \
         printf("No event handler: level %d %s, id %d\n", levels[level], rising ? "rising":"falling", thr_id);  \
      else                                                                                                      \
         printf("Registred: level %d %s, id %d\n", levels[level], rising ? "rising":"falling", thr_id);         \
      if (levels[level] == ' ')                                                                                 \
         levelstr[level] = '-';                                                                                 \
}  }

void *dispatch(void* data)
{
   nrx_context ctx = (nrx_context) data;
   while (nrx_wait_event(ctx, -1) == 0) {
         nrx_next_event(ctx);
   } 
   return NULL;
}


int
main(int argc, char **argv)
{
   nrx_context ctx;
   const char *ifname = NULL;
   pthread_t thread;
   void *status;

   if (argc == 2 && !strcmp(argv[1], "--help")) {
      printf("Usage: test [interface]\n");
      return 1;
   }
   
   if (argc == 2) 
      ifname = argv[1];

   if(nrx_init_context(&ctx, ifname) != 0)
      err(1, "nrx_init_context");

   pthread_create(&thread, NULL, dispatch, ctx);

   nrx_register_we_event_handler(ctx, IWEVCUSTOM, nrx_event_dispatch, NULL);
 
   /* Registred in "incorrect" direction to get harder test case */

   /* Rising: value is 1 */
   REGISTER(0,1);
   REGISTER(1,1);
   REGISTER(2,1);
   REGISTER(3,1);
   REGISTER(4,1);
   REGISTER(5,1);
   REGISTER(6,1);
   REGISTER(7,1);
   printf("Sleep a few sec\n");
   sleep(2);
   REGISTER(8,1);
   REGISTER(9,1);
   REGISTER(10,1);
   REGISTER(11,1);
   REGISTER(12,1);
   REGISTER(13,1);
   REGISTER(14,1);
   REGISTER(15,1);
   REGISTER(16,1);
   REGISTER(17,1);
   REGISTER(18,1);
   REGISTER(19,1);
   REGISTER(20,1);

   /* Falling: value is 0 */
   REGISTER(20,0);
   REGISTER(19,0);
   REGISTER(18,0);
   REGISTER(17,0);
   REGISTER(16,0);
   REGISTER(15,0);
   REGISTER(14,0);
   REGISTER(13,0);
   REGISTER(12,0);
   REGISTER(11,0);
   REGISTER(10,0);
   REGISTER(9,0);
   REGISTER(8,0);
   REGISTER(7,0);
   REGISTER(6,0);
   REGISTER(5,0);
   REGISTER(4,0);
   REGISTER(3,0);
   REGISTER(2,0);
   REGISTER(1,0);
   REGISTER(0,0);
   
   pthread_join(thread, (void**) &status);
   nrx_free_context(ctx);

   return 0;
}

