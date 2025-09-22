/*	$OpenBSD: conf.h,v 1.5 2003/06/17 00:36:40 pjanzen Exp $	*/
/*	David Leonard <d@openbsd.org>, 1999.  Public domain.	*/

/* Configuration option variables for the server: */

extern int conf_random;
extern int conf_reflect;
extern int conf_monitor;
extern int conf_ooze;
extern int conf_fly;
extern int conf_volcano;
extern int conf_drone;
extern int conf_boots;
extern int conf_scan;
extern int conf_cloak;
extern int conf_logerr;
extern int conf_syslog;

extern int conf_scoredecay;
extern int conf_maxremove;
extern int conf_linger;

extern int conf_flytime;
extern int conf_flystep;
extern int conf_volcano_max;
extern int conf_ptrip_face;
extern int conf_ptrip_back;
extern int conf_ptrip_side;
extern int conf_prandom;
extern int conf_preflect;
extern int conf_pshot_coll;
extern int conf_pgren_coll;
extern int conf_pgren_catch;
extern int conf_pmiss;
extern int conf_pdroneabsorb;
extern int conf_fall_frac;

extern int conf_bulspd;
extern int conf_ishots;
extern int conf_nshots;
extern int conf_maxncshot;
extern int conf_maxdam;
extern int conf_mindam;
extern int conf_stabdam;
extern int conf_killgain;
extern int conf_slimefactor;
extern int conf_slimespeed;
extern int conf_lavaspeed;
extern int conf_cloaklen;
extern int conf_scanlen;
extern int conf_mindshot;
extern int conf_simstep;

void config(void);
void config_arg(char *);
