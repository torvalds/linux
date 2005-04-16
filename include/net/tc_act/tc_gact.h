#ifndef __NET_TC_GACT_H
#define __NET_TC_GACT_H

#include <net/act_api.h>

struct tcf_gact
{
        tca_gen(gact);
#ifdef CONFIG_GACT_PROB
        u16                 ptype;
        u16                 pval;
        int                 paction;
#endif
                                                                                
};
                                                                                
#endif
