#ifndef _XT_DCCP_H_
#define _XT_DCCP_H_

#define XT_DCCP_SRC_PORTS	        0x01
#define XT_DCCP_DEST_PORTS	        0x02
#define XT_DCCP_TYPE			0x04
#define XT_DCCP_OPTION			0x08

#define XT_DCCP_VALID_FLAGS		0x0f

struct xt_dccp_info {
	u_int16_t dpts[2];  /* Min, Max */
	u_int16_t spts[2];  /* Min, Max */

	u_int16_t flags;
	u_int16_t invflags;

	u_int16_t typemask;
	u_int8_t option;
};

#endif /* _XT_DCCP_H_ */

