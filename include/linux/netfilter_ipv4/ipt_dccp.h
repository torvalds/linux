#ifndef _IPT_DCCP_H_
#define _IPT_DCCP_H_

#define IPT_DCCP_SRC_PORTS	        0x01
#define IPT_DCCP_DEST_PORTS	        0x02
#define IPT_DCCP_TYPE			0x04
#define IPT_DCCP_OPTION			0x08

#define IPT_DCCP_VALID_FLAGS		0x0f

struct ipt_dccp_info {
	u_int16_t dpts[2];  /* Min, Max */
	u_int16_t spts[2];  /* Min, Max */

	u_int16_t flags;
	u_int16_t invflags;

	u_int16_t typemask;
	u_int8_t option;
};

#endif /* _IPT_DCCP_H_ */

