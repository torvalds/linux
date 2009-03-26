#ifndef __LINUX_TC_GACT_H
#define __LINUX_TC_GACT_H

#include <linux/types.h>
#include <linux/pkt_cls.h>

#define TCA_ACT_GACT 5
struct tc_gact
{
	tc_gen;

};

struct tc_gact_p
{
#define PGACT_NONE              0
#define PGACT_NETRAND           1
#define PGACT_DETERM            2
#define MAX_RAND                (PGACT_DETERM + 1 )
	__u16                 ptype;
	__u16                 pval;
	int                   paction;
};
 
enum
{
	TCA_GACT_UNSPEC,
	TCA_GACT_TM,
	TCA_GACT_PARMS,
	TCA_GACT_PROB,
	__TCA_GACT_MAX
};
#define TCA_GACT_MAX (__TCA_GACT_MAX - 1)
 
#endif
