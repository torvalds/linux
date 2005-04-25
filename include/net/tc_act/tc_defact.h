#ifndef __NET_TC_DEF_H
#define __NET_TC_DEF_H

#include <net/act_api.h>

struct tcf_defact
{
	tca_gen(defact);
	u32     datalen;
	void    *defdata;
};

#endif
