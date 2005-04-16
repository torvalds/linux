#ifndef _IPT_STATE_H
#define _IPT_STATE_H

#define IPT_STATE_BIT(ctinfo) (1 << ((ctinfo)%IP_CT_IS_REPLY+1))
#define IPT_STATE_INVALID (1 << 0)

#define IPT_STATE_UNTRACKED (1 << (IP_CT_NUMBER + 1))

struct ipt_state_info
{
	unsigned int statemask;
};
#endif /*_IPT_STATE_H*/
