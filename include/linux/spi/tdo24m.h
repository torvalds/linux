#ifndef __TDO24M_H__
#define __TDO24M_H__

enum tdo24m_model {
	TDO24M,
	TDO35S,
};

struct tdo24m_platform_data {
	enum tdo24m_model model;
};

#endif /* __TDO24M_H__ */
