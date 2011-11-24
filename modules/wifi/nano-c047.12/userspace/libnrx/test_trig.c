/* Copyright (C) 2007 Nanoradio AB */
/* $Id: test_trig.c 10372 2008-11-13 09:22:12Z miwi $ */

/*
 * Testprogram for mib triggers in NRXAPI.
 *
 * This program will randomly choose between the following actions:
 * - Create a new mib trigger.
 * - Cancel a mib trigger.
 * - Change value of the mib (a special version of x_mac is needed for this).
 * For each of these actions an expected result is anticipated.
 *
 * The trigger APIs uses callbacks to forward information. Hence, a
 * callback is registered. When trigger events happens or when a
 * trigger has been canceled, these should agree with the anticipated
 * result. Statistics is gathered regarding successes and failures.
 *
 */

#include <stdio.h>
#include <err.h>
#include <pthread.h>
#include "nrx_lib.h"
#include "nrx_priv.h"
#include "mac_mib_defs.h"

/*******************************************/
/*                                         */
/*                SETTINGS                 */
/*                                         */
/*******************************************/

typedef struct {
   int help;
   int debuglevel;
   const char *ifname;
   int seed;
   int num_trig;
   int prob_level;
   int prob_new;
   int prob_cancel;
   int level_min;
   int level_max;
   int sw_speed;
   int fw_speed;
   int timeout;
   const char *logfile;
}settings_t;

settings_t settings = {
   .help        = 0,
   .debuglevel  = 4,
   .ifname      = NULL,
   .seed        = 1,
   .num_trig    = 10,
   .prob_level  = 50,
   .prob_new    = 10,
   .prob_cancel = 10,
   .level_min   = -120,
   .level_max  = -40,
   .sw_speed    = 350,          /* large enough to miss a few beacons */
   .fw_speed    = 100,
   .timeout     = 0,
   .logfile     = NULL,
};


/*******************************************/
/*                                         */
/*               DEBUG STUFF               */
/*                                         */
/*******************************************/

#define ESC           "\033"
#define BLACK         ESC"[0;30m"
#define BLACK_EMPH    ESC"[47m"ESC"[30m"
#define GRAY_BOLD     ESC"[1;30m"
#define RED           ESC"[0;31m"
#define RED_BOLD      ESC"[1;31m"
#define GREEN         ESC"[0;32m"
#define GREEN_BOLD    ESC"[1;32m"
#define YELLOW        ESC"[0;33m"
#define YELLOW_BOLD   ESC"[1;33m"
#define BLUE          ESC"[0;34m"
#define BLUE_BOLD     ESC"[1;34m"
#define MAGENTA       ESC"[0;35m"
#define MAGENTA_BOLD  ESC"[1;35m"
#define CYAN          ESC"[0;36m"
#define CYAN_BOLD     ESC"[1;36m"
#define WHITE         ESC"[0;37m"
#define WHITE_BOLD    ESC"[1;37m"

#define DEBUG_PRINTF(level, fmt, args...) do {  \
   if(level<=settings.debuglevel)               \
      fprintf(stderr, fmt , ##args);            \
   if (settings.logfile != NULL) {              \
      FILE *f = fopen(settings.logfile, "a");   \
      if (f)                                    \
         fprintf(f, fmt , ##args);              \
      fclose(f);                                \
   }  } while(0)

#define DEBUG(level, fmt, args...)              \
   DEBUG_PRINTF(level, fmt "\n" , ##args)


#define DEBUG_HIGH(fmt, args...)   DEBUG(3, fmt , ##args)
#define DEBUG_MED(fmt, args...)    DEBUG(4, fmt , ##args)
#define DEBUG_LOW(fmt, args...)    DEBUG(5, fmt , ##args)

#ifdef ERROR
#undef ERROR
#endif
#define ERROR(fmt, args...)                                     \
   DEBUG(1, RED_BOLD "ERROR[%d]: " RED fmt BLACK, __LINE__ , ##args)

#ifdef WARNING
#undef WARNING
#endif
#define WARNING(fmt, args...)                                   \
   DEBUG(2, RED_BOLD "WARNING[%d]: " RED fmt BLACK, __LINE__ , ##args)

#define FATAL(fmt, args...) do {                                \
   DEBUG(0, RED_BOLD "FATAL[%d]: " fmt BLACK, __LINE__ , ##args);      \
   exit(1);                                                     \
}while(0)

#define ASSERT(x) do {                          \
   if (!(x))                                    \
      FATAL("Assertion failed on \"%s\".", #x); \
}while(0)

#define FAILED(fmt, args...) do {                       \
   failures++;                                          \
   DEBUG(1, RED "FAILURE[%d]: " fmt BLACK, __LINE__ , ##args); \
}while(0)


/* Handle debug info from NRXAPI */
static int debug_callback(int prio, const char *file, int line, const char *message)
{
   switch (prio) {
      case NRX_PRIO_ERR: DEBUG(1, RED_BOLD "ERROR[%s:%d]: " RED "%s" BLACK, file, line, message); break;
      case NRX_PRIO_WRN: DEBUG(2, RED "WARNING[%s:%d]: %s" BLACK, file, line, message); break;
      case NRX_PRIO_LOG: DEBUG(4, "INFO[%s:%d]: " RED "%s" BLACK, file, line, message); break;
      default: DEBUG(4, "????[%s:%d]: %s", file, line, message); break;
   }
   return 0;
}


#define ELEMENTS_IN_VECTOR(x) (sizeof(x)/sizeof(*x))

/*******************************************/
/*                                         */
/*               CONFIGURATION             */
/*                                         */
/*******************************************/

enum opt_type_t {FLAG, INT, STR};
typedef struct {
   char *arg_name;
   char *arg_short;
   char *help_text;
   enum opt_type_t opt_type;
   void  *variable;
}option_t;

option_t options[] = {
   {"--help",           "-h",   "Show this help and exit.",                     FLAG,   &settings.help},
   {"--debug",          "-d",   "Set debug level, range 0-5.",                  INT,    &settings.debuglevel},
   {"--ifname",         "-i",   "Set interface, e.g. \"eth1\".",                STR,    &settings.ifname},
   {"--rand_seed",      "-r",   "Set random seed.",                             INT,    &settings.seed},
   {"--num_trig",       "-n",   "Number of triggers to create at start up.",    INT,    &settings.num_trig},
   {"--prob_level",     "-l",   "Set probability for changing level.",          INT,    &settings.prob_level},
   {"--prob_new",       "-n",   "Set prob in per cent for adding a trigger.",   INT,    &settings.prob_new},
   {"--prob_cancel",    "-c",   "Set prob in per cent for removing a trigger.", INT,    &settings.prob_cancel},
   {"--level_min",      "-m",   "Min level for RSSI interval.",                 INT,    &settings.level_min},
   {"--level_max",      "-M",   "Min level for RSSI interval.",                 INT,    &settings.level_max},
   {"--sw_speed",       "-s",   "Time between tests (in ms).",                  INT,    &settings.sw_speed},
   {"--fw_speed",       "-f",   "Time for checks in fw. (less than sw_speed)",  INT,    &settings.fw_speed},
   {"--timeout",        "-t",   "Timeout to stop test. Deactivate with 0.",     INT,    &settings.timeout},
   {"--output",         "-o",   "Name of log file.",                            STR,    &settings.logfile},
};


int configure(int argc, char *argv[])
{
   int i, j;
   for (i = 1; i < argc; i++) {
      for (j = 0; j < ELEMENTS_IN_VECTOR(options); j++) {
         if (!strcmp(argv[i], options[j].arg_name) || !strcmp(argv[i], options[j].arg_short)) {
            if (options[j].opt_type == FLAG) { /* A flag */
               *((int *)options[j].variable) = 1;
            }
            else if (options[j].opt_type == INT) { /* A number */
               if (argv[i+1] != NULL) {
                  *((int *)options[j].variable) = strtol(argv[i+1], NULL, 0);
                  i++;
               }
               else {
                  ERROR("Option \"%s\" requires a numeric value.", argv[i]);
                  return 1;
               }
            }
            else if (options[j].opt_type == STR) { /* A string */
               if (argv[i+1] != NULL) {
                  *((char **)options[j].variable) = argv[i+1];
                  i++;
               }
               else {
                  ERROR("Option \"%s\" requires string.", argv[i]);
                  return 1;
               }
            }
            else {              /* unknown type */
               FATAL("Internal error. Unknown type.");
            }
            break;
         }
      }
      if (j == ELEMENTS_IN_VECTOR(options)) { // Not found
         ERROR("Unknown option: %s", argv[i]);
         return 1;
      }
   }
   return 0;
}

void show_help()
{
   int j;
   printf("This progam's options:\n");
   for (j = 0; j < ELEMENTS_IN_VECTOR(options); j++) {
      printf("    %s,  %-14s %-5s %s ", 
             options[j].arg_short, 
             options[j].arg_name, 
             options[j].opt_type == INT ? "<n>" : options[j].opt_type == STR ? "<str>" : "",
             options[j].help_text);
      if (options[j].opt_type == INT)
         printf("(default %d)\n", *((int *)options[j].variable));
      else if (options[j].opt_type == STR) {
         if (*((char **)options[j].variable) == NULL)
            printf("(no default)\n");
         else
            printf("(default %s)\n", *((char **)options[j].variable));
      }
      else
         printf("\n");
   }
}




/*******************************************/
/*                                         */
/*              HANDLE TRIGGERS            */
/*                                         */
/*******************************************/

int failures = 0;
int prev_level;
int curr_level;
int last_known;
int triggers = 0;

struct trig_entry {
   int thr_id;
   int thr;
   int prev_level;
   int rising;
   int ready_to_trig;
   int actual_trig;             /* number of triggers received for this trig_id */
   int expected_trig;           /* number of expected trig events */
   int incomming;               /* -1 no trigger expected, 0 may come, 1 should come */
   int cancel;
   TAILQ_ENTRY(trig_entry) next;
};

TAILQ_HEAD(, trig_entry) trig_list;

char *rising2str(int rising) {
   if (rising == NRX_THR_RISING)
      return "rising";
   if (rising == NRX_THR_FALLING)
      return "falling";
   if (rising == (NRX_THR_RISING | NRX_THR_FALLING))
      return "both dir";
   FATAL("Invalid dir");
}


int trigger_callback(nrx_context ctx,
                     int operation,
                     void *event_data,
                     size_t event_data_size,
                     void *user_data)
{
   nrx_event_mibtrigger_t *ed = (nrx_event_mibtrigger_t *)event_data;
   int id, level;
   struct trig_entry *p, *t = (struct trig_entry *)user_data;

   /* Check trigger exists */
   TAILQ_FOREACH(p, &trig_list, next) 
      if (p == t)
         break;
   if (p == NULL) {
      FAILED("Trigger not in list");
      return 0;
   }

   /* Trigger canceled */
   if (operation == NRX_CB_CANCEL) {
      DEBUG_HIGH("Callback: Canceled thr_id %d, thr %d", t->thr_id, t->thr);
      if (t->cancel == 0)
         FAILED("CANCEL: Unexpected cancel");

      if (t->expected_trig != t->actual_trig)
         DEBUG_HIGH("CANCEL: Expected != Actual triggers (diff %d of %d)", 
                t->expected_trig - t->actual_trig, 
                t->expected_trig);
      TAILQ_REMOVE(&trig_list, t, next);
      free(t);
      triggers--;
      return 0;
   }
   if (operation != NRX_CB_TRIGGER) {
      FATAL("Unknown operation");
   }

   id = (int)ed->id;
   level = (int)ed->value;

   DEBUG_LOW("Callback: Trig id %d. Passed level %d (to %d)",
           id,
           t->thr,
           level);

   if (level != curr_level) {
      ERROR("Level does not agree, level %d, curr_level %d", level, curr_level);
   }

   t->actual_trig++;

   /* Sanity checks */
   if (t->actual_trig == 1 && curr_level != last_known && level == last_known) {
      ERROR("Known limitation: Did not expect this thr to expire, thr_id %d, thr %d (now %d, prev %d), dir %s",
            t->thr_id, t->thr, level, t->prev_level, rising2str(t->rising));
      t->incomming = 0;         /* may get one more */
      return 0;
   }
   if (t->incomming < 0)
      FAILED("Did not expect this thr to expire, thr_id %d, thr %d (now %d, prev %d), dir %s",
             t->thr_id, t->thr, level, t->prev_level, rising2str(t->rising));

   if (level == t->thr)
      FAILED("Level same as thr");
   else if (t->rising == NRX_THR_RISING) {
      if (level <= t->thr)
         FAILED("Rising: Trigger level not above thr, mib_id %d, thr %d %s (now %d)", 
                t->thr_id, t->thr, rising2str(t->rising), level);
      if (t->actual_trig > 1 && prev_level > t->thr)
         FAILED("Rising: Level has not been below thr, mib_id %d, thr %d %s (now %d)", 
                t->thr_id, t->thr, rising2str(t->rising), level);
   }
   else if (t->rising == NRX_THR_FALLING) {
      if (level >= t->thr)
         FAILED("Falling: Trigger level not below thr, mib_id %d, thr %d %s (now %d)", 
                t->thr_id, t->thr, rising2str(t->rising), level);
      if (t->actual_trig > 1 && prev_level < t->thr)
         FAILED("Falling: Level has not been above thr, mib_id %d, thr %d %s (now %d)", 
                t->thr_id, t->thr, rising2str(t->rising), level);
   }
   else if (t->rising == (NRX_THR_RISING | NRX_THR_FALLING) && t->actual_trig > 1) {
      if (prev_level < t->thr && level <= t->thr) 
         FAILED("Both dir: Did not rise above thr, thr_id %d, thr %d, prev %d %s (now %d)",
                t->thr_id, t->thr, prev_level, rising2str(t->rising), curr_level);
      if (prev_level > t->thr && level >= t->thr) 
         FAILED("Both dir: Did not fall below thr , thr_id %d, thr %d, prev %d %s (now %d)",
                t->thr_id, t->thr, prev_level, rising2str(t->rising), curr_level);
   }

   t->prev_level = level;
   t->incomming = -1;

   return 0;
}



int main(int argc, char *argv[])
{
   nrx_context ctx;
   int ret;
   time_t start_time = time(0);
   int prob;
   int loop;
   int err;
   int num_trig_reached = 0;
   struct trig_entry *t;

   err = configure(argc, argv);
   if (err) {
      printf("For info, use --help.\n");
      return 1;
   }

   if (settings.help) {
      printf("Overview:\n");
      printf("    This program will test mib triggers. To use it, you must first load\n");
      printf("    the driver and connect to an AP. Triggers will only be checked in\n");
      printf("    firmware (fw) when a beacon is received. Hence, either ensure an\n");
      printf("    excellent connection to the AP whitout any loss of beacons, or reduce\n");
      printf("    the SW speed (see options).\n\n");
      show_help();
      return 1;
   }

   ASSERT(settings.level_min < settings.level_max);
   ASSERT(settings.prob_level + settings.prob_new + settings.prob_cancel <= 100);
   ASSERT(settings.fw_speed <= settings.sw_speed);

   /* Init */
   nrx_set_log_cb(debug_callback);
   if(nrx_init_context(&ctx, settings.ifname) != 0)
      FATAL("nrx_init_context failed");
   TAILQ_INIT(&trig_list);
   
   /* Settings */
   srand(settings.seed);
   curr_level = settings.level_min + rand() % (settings.level_max - settings.level_min + 1);
   last_known = curr_level;
   DEBUG_MED("Starting level %d", curr_level);
   nrx_set_mib_val(ctx, MIB_dot11rssiDataFrame, &curr_level, sizeof(curr_level));

   DEBUG_HIGH(BLUE "Test started" BLACK);

   for (loop = 1; ; loop++ ) {

      if (settings.timeout && time(0) > start_time + settings.timeout)
         break;

      while (1) {
         ret = nrx_wait_event(ctx, settings.sw_speed);
         if (ret == 0) 
            nrx_next_event(ctx);
         else if (ret == EWOULDBLOCK) /* about a second should have passed */
            break;
         else if (ret < 0)
            FATAL("nrx_next_event failed");
         else 
            FATAL("Unknown return code (%d).", ret);
      }
      
      /* Check that all expected events have been reset to 0 */
      TAILQ_FOREACH(t, &trig_list, next) {
         if (t->incomming > 0) {
            if (t->thr == prev_level)
               ERROR("Known limitation: Expected trigger event did not happen, thr_id %d, thr %d, dir %s, level %d",
                     t->thr_id,
                     t->thr,
                     rising2str(t->rising),
                     curr_level);
            else
               FAILED("Expected trigger event did not happen, thr_id %d, thr %d, dir %s, level %d", 
                      t->thr_id, 
                      t->thr, 
                      rising2str(t->rising),
                      curr_level);
            t->incomming = -1;
         }
         if (t->cancel != 0) {
            FAILED("Trigger should have been removed, thr_id %d", t->thr_id);
            TAILQ_REMOVE(&trig_list, t, next);
            free(t);
            break;  /* t destroyed => can't loop (doesn't matter as this should be only event) */
         }
      }

      DEBUG_MED("%d: triggers %d, failures %d", loop, triggers, failures);
      {
         int ignore;
         char buf[100];
         sprintf(buf, "echo \"### Trigger test: loop %d ###\" >> /var/log/messages", loop);
         ignore = system(buf);
      }

      /* Change level */
      prob = rand() % 100;
      if (prob < settings.prob_level) {
         prev_level = curr_level; /* Remember prev level */
         curr_level = settings.level_min + rand() % (settings.level_max - settings.level_min + 1);
         nrx_set_mib_val(ctx, MIB_dot11rssiDataFrame, &curr_level, sizeof(curr_level));

         DEBUG_MED("%d: Changed level to " GREEN "%d" BLACK, loop, curr_level);

         /* Write in expected / incomming in list */
         TAILQ_FOREACH(t, &trig_list, next) {
            if (t->ready_to_trig)
               if ((t->rising == NRX_THR_RISING && curr_level > t->thr) 
                   || ((t->rising == NRX_THR_FALLING) && curr_level < t->thr)
                   || ((t->rising == (NRX_THR_RISING | NRX_THR_FALLING))
                       && ((prev_level <= t->thr && curr_level > t->thr) 
                           || (prev_level >= t->thr && curr_level < t->thr)))) 
               {
                  DEBUG_LOW("Incomming, thr_id %d, thr %d %s (now %d)", t->thr_id, t->thr, rising2str(t->rising), curr_level);
                  t->ready_to_trig = 0;
                  t->incomming = 1;
                  t->expected_trig++;
               }

            if ((t->rising == NRX_THR_RISING && curr_level <= t->thr)
                || (t->rising == NRX_THR_FALLING && curr_level >= t->thr)
                || (t->rising == (NRX_THR_RISING | NRX_THR_FALLING)))
            {
               if (!t->ready_to_trig)
                  DEBUG_LOW("Ready to trig, thr_id %d, thr %d %s (now %d)", t->thr_id, t->thr, rising2str(t->rising), curr_level);
               t->ready_to_trig = 1;
            }
            if ((last_known <= t->thr && curr_level > t->thr) || (last_known >= t->thr && curr_level < t->thr))
               last_known = curr_level; /* this is what the driver will know about fw state */
         }
      }
      prob -= settings.prob_level;

      /* Create new trigger */
      if (prob >= 0 && prob < settings.prob_new) { 
         struct trig_entry *trig = malloc(sizeof(struct trig_entry));
         if (trig == NULL)
            FATAL("No mem");
         memset(trig, 0, sizeof(*trig));
         trig->prev_level = curr_level;

         trig->thr = settings.level_min + rand() % (settings.level_max - settings.level_min + 1);
         trig->prev_level = curr_level;
         trig->rising = 1 + rand() % 3;
         trig->ready_to_trig = ((trig->rising & NRX_THR_RISING) && curr_level <= trig->thr) 
            || ((trig->rising & NRX_THR_FALLING) && curr_level >= trig->thr);
         trig->actual_trig = 0;
         trig->expected_trig = ((trig->rising & NRX_THR_RISING) && curr_level > trig->thr) 
            || ((trig->rising & NRX_THR_FALLING) && curr_level < trig->thr);
         trig->incomming = 2 * trig->expected_trig - 1;
         trig->cancel = 0;

         ret = nrx_enable_rssi_threshold(ctx,
                                         &trig->thr_id,
                                         trig->thr,
                                         settings.fw_speed, /* lower than a second (used in timeout) */
                                         trig->rising,
                                         NRX_DT_BEACON);
         if (ret) {
            FAILED("Could not create trigger (err %d, %s)", ret, strerror(ret));
            free(trig);
            continue;
         }
         if (!nrx_register_rssi_threshold_callback(ctx,
                                                   trig->thr_id,
                                                   trigger_callback,
                                                   trig)) {
            FAILED("Could not register callback");
            free(trig);
            continue;
         }

         TAILQ_INSERT_TAIL(&trig_list, trig, next);
         triggers++;
         if (triggers == settings.num_trig) {
            DEBUG_HIGH("Number of triggers reached (%d)", triggers);
            num_trig_reached = 1;
         }

         DEBUG_MED("%d: Created new trigger, thr_id %d, thr %d, dir %s. Total %d trig", 
                   loop, 
                   trig->thr_id, 
                   trig->thr, 
                   rising2str(trig->rising),
                   triggers);
      }
      prob -= settings.prob_new;

      /* Remove trigger */
      if (prob >= 0 && prob < settings.prob_cancel) {
         int i;

         /* Make sure that it does not happen so often until volume reached */
         if (!num_trig_reached)
            if (prob > settings.prob_new/2) {
               DEBUG_MED("Skip cancel as ramp up phase");
               continue;
            }

         DEBUG_LOW("%d: Cancel a trig", loop);

         if (triggers == 0) {
            DEBUG_MED("No triggers to remove");
            continue;
         }

         i = rand() % triggers; /* Randomize which one is removed */
         TAILQ_FOREACH(t, &trig_list, next) {
            if (i-- == 0) {
               DEBUG_MED("Remove trig_id %d", t->thr_id);
               if (nrx_disable_rssi_threshold(ctx, t->thr_id)) {
                  FAILED("Could not cancel thr_id %d, %s", t->thr_id, strerror(errno));
                  break;
               }
               /* Write in cancel list */
               t->cancel = 1;
               break;
            }
         }
         if (t == NULL) {
            FAILED("Unaware about the number of triggers. Assumed %d", triggers);
            triggers = 0;
            TAILQ_FOREACH(t, &trig_list, next) 
               triggers++;
            ERROR("Have %d triggers", triggers);
         }
      }
   }

   DEBUG_HIGH(BLUE "Timeout. Clean up." BLACK);

   /* Remove all triggers */
   TAILQ_FOREACH(t, &trig_list, next) {
      t->cancel = 1;
      nrx_disable_rssi_threshold(ctx, t->thr_id);
   }
   
   /* Take care of events */
   while ((ret = nrx_wait_event(ctx, settings.sw_speed)) != EWOULDBLOCK) {
      if (ret == 0) 
         nrx_next_event(ctx);
      else if (ret < 0) {
         FATAL("nrx_next_event failed");
         break;
      }
   }

   /* List should be cleaned out */
   while ((t = TAILQ_FIRST(&trig_list))) {
      FAILED("Trigger %d not removed", t->thr_id);
      TAILQ_REMOVE(&trig_list, t, next);
      free(t);
   }

   nrx_free_context(ctx);

   DEBUG_HIGH(BLUE "End of program, %d failures" BLACK, failures);

   return !failures;
}

