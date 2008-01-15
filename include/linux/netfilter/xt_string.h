#ifndef _XT_STRING_H
#define _XT_STRING_H

#define XT_STRING_MAX_PATTERN_SIZE 128
#define XT_STRING_MAX_ALGO_NAME_SIZE 16

struct xt_string_info
{
	u_int16_t from_offset;
	u_int16_t to_offset;
	char	  algo[XT_STRING_MAX_ALGO_NAME_SIZE];
	char 	  pattern[XT_STRING_MAX_PATTERN_SIZE];
	u_int8_t  patlen;
	u_int8_t  invert;

	/* Used internally by the kernel */
	struct ts_config __attribute__((aligned(8))) *config;
};

#endif /*_XT_STRING_H*/
