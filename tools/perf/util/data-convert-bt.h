#ifndef __DATA_CONVERT_BT_H
#define __DATA_CONVERT_BT_H
#ifdef HAVE_LIBBABELTRACE_SUPPORT

int bt_convert__perf2ctf(const char *input_name, const char *to_ctf, bool force);

#endif /* HAVE_LIBBABELTRACE_SUPPORT */
#endif /* __DATA_CONVERT_BT_H */
