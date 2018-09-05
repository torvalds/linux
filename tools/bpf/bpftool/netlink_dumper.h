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
		fprintf(stderr, "%s {", name);		\
	}						\
}

#define NET_START_OBJECT_NESTED2			\
{							\
	if (json_output)				\
		jsonw_start_object(json_wtr);		\
	else						\
		fprintf(stderr, "{");			\
}

#define NET_END_OBJECT_NESTED				\
{							\
	if (json_output)				\
		jsonw_end_object(json_wtr);		\
	else						\
		fprintf(stderr, "}");			\
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
		fprintf(stderr, "\n");			\
}

#define NET_START_ARRAY(name, newline)			\
{							\
	if (json_output) {				\
		jsonw_name(json_wtr, name);		\
		jsonw_start_array(json_wtr);		\
	} else {					\
		fprintf(stderr, "%s [%s", name, newline);\
	}						\
}

#define NET_END_ARRAY(endstr)				\
{							\
	if (json_output)				\
		jsonw_end_array(json_wtr);		\
	else						\
		fprintf(stderr, "]%s", endstr);		\
}

#define NET_DUMP_UINT(name, val)			\
{							\
	if (json_output)				\
		jsonw_uint_field(json_wtr, name, val);	\
	else						\
		fprintf(stderr, "%s %d ", name, val);	\
}

#define NET_DUMP_LLUINT(name, val)			\
{							\
	if (json_output)				\
		jsonw_lluint_field(json_wtr, name, val);\
	else						\
		fprintf(stderr, "%s %lld ", name, val);	\
}

#define NET_DUMP_STR(name, str)				\
{							\
	if (json_output)				\
		jsonw_string_field(json_wtr, name, str);\
	else						\
		fprintf(stderr, "%s %s ", name, str);	\
}

#define NET_DUMP_STR_ONLY(str)				\
{							\
	if (json_output)				\
		jsonw_string(json_wtr, str);		\
	else						\
		fprintf(stderr, "%s ", str);		\
}

#endif
