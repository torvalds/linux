#ifndef __NET_TC_PED_H
#define __NET_TC_PED_H

#include <net/act_api.h>

struct tcf_pedit_key_ex {
	enum pedit_header_type htype;
	enum pedit_cmd cmd;
};

struct tcf_pedit {
	struct tc_action	common;
	unsigned char		tcfp_nkeys;
	unsigned char		tcfp_flags;
	struct tc_pedit_key	*tcfp_keys;
	struct tcf_pedit_key_ex	*tcfp_keys_ex;
};
#define to_pedit(a) ((struct tcf_pedit *)a)

#endif /* __NET_TC_PED_H */
