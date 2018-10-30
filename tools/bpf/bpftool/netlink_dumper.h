// SPDX-License-Identifier: GPL-2.0+
// Copyright (C) 2018 Facebook

#ifndef _NETLINK_DUMPER_H_
#define _NETLINK_DUMPER_H_

#define NET_START_OBJECT				\
{							\
	if (json_output)				\
		jsonw_start_object(json_wtr);		\
}

#define NET_START_OBJECT_NESTED(name)			\
{							\
	if (json_output) {				\
		jsonw_name(json_wtr, name);		\
		jsonw_start_object(json_wtr);		\
	} else {					\
		fprintf(stdout, "%s {", name);		\
	}						\
}

#define NET_START_OBJECT_NESTED2			\
{							\
	if (json_output)				\
		jsonw_start_object(json_wtr);		\
	else						\
		fprintf(stdout, "{");			\
}

#define NET_END_OBJECT_NESTED				\
{							\
	if (json_output)				\
		jsonw_end_object(json_wtr);		\
	else						\
		fprintf(stdout, "}");			\
}

#define NET_END_OBJECT					\
{							\
	if (json_output)				\
		jsonw_end_object(json_wtr);		\
}

#define NET_END_OBJECT_FINAL				\
{							\
	if (json_output)				\
		jsonw_end_object(json_wtr);		\
	else						\
		fprintf(stdout, "\n");			\
}

#define NET_START_ARRAY(name, fmt_str)			\
{							\
	if (json_output) {				\
		jsonw_name(json_wtr, name);		\
		jsonw_start_array(json_wtr);		\
	} else {					\
		fprintf(stdout, fmt_str, name);		\
	}						\
}

#define NET_END_ARRAY(endstr)				\
{							\
	if (json_output)				\
		jsonw_end_array(json_wtr);		\
	else						\
		fprintf(stdout, "%s", endstr);		\
}

#define NET_DUMP_UINT(name, fmt_str, val)		\
{							\
	if (json_output)				\
		jsonw_uint_field(json_wtr, name, val);	\
	else						\
		fprintf(stdout, fmt_str, val);		\
}

#define NET_DUMP_STR(name, fmt_str, str)		\
{							\
	if (json_output)				\
		jsonw_string_field(json_wtr, name, str);\
	else						\
		fprintf(stdout, fmt_str, str);		\
}

#define NET_DUMP_STR_ONLY(str)				\
{							\
	if (json_output)				\
		jsonw_string(json_wtr, str);		\
	else						\
		fprintf(stdout, "%s ", str);		\
}

#endif
