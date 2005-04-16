#ifndef __LINUX_TC_PED_H
#define __LINUX_TC_PED_H

#include <linux/pkt_cls.h>

#define TCA_ACT_PEDIT 7

enum
{
	TCA_PEDIT_UNSPEC,
	TCA_PEDIT_TM,
	TCA_PEDIT_PARMS,
	__TCA_PEDIT_MAX
};
#define TCA_PEDIT_MAX (__TCA_PEDIT_MAX - 1)
                                                                                
struct tc_pedit_key
{
	__u32           mask;  /* AND */
	__u32           val;   /*XOR */
	__u32           off;  /*offset */
	__u32           at;
	__u32           offmask;
	__u32           shift;
};
                                                                                
struct tc_pedit_sel
{
	tc_gen;
	unsigned char           nkeys;
	unsigned char           flags;
	struct tc_pedit_key     keys[0];
};
#define tc_pedit tc_pedit_sel

#endif
