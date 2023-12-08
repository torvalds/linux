// SPDX-License-Identifier: GPL-2.0-only
/// do_div() does a 64-by-32 division.
/// When the divisor is long, unsigned long, u64, or s64,
/// do_div() truncates it to 32 bits, this means it can test
/// non-zero and be truncated to 0 for division on 64bit platforms.
///
//# This makes an effort to find those inappropriate do_div() calls.
//
// Confidence: Moderate
// Copyright: (C) 2020 Wen Yang, Alibaba.
// Comments:
// Options: --no-includes --include-headers

virtual context
virtual org
virtual report

@initialize:python@
@@

def get_digit_type_and_value(str):
    is_digit = False
    value = 0

    try:
        if (str.isdigit()):
           is_digit = True
           value =  int(str, 0)
        elif (str.upper().endswith('ULL')):
           is_digit = True
           value = int(str[:-3], 0)
        elif (str.upper().endswith('LL')):
           is_digit = True
           value = int(str[:-2], 0)
        elif (str.upper().endswith('UL')):
           is_digit = True
           value = int(str[:-2], 0)
        elif (str.upper().endswith('L')):
           is_digit = True
           value = int(str[:-1], 0)
        elif (str.upper().endswith('U')):
           is_digit = True
           value = int(str[:-1], 0)
    except Exception as e:
          print('Error:',e)
          is_digit = False
          value = 0
    finally:
        return is_digit, value

def filter_out_safe_constants(str):
    is_digit, value = get_digit_type_and_value(str)
    if (is_digit):
        if (value >= 0x100000000):
            return True
        else:
            return False
    else:
        return True

def construct_warnings(suggested_fun):
    msg="WARNING: do_div() does a 64-by-32 division, please consider using %s instead."
    return  msg % suggested_fun

@depends on context@
expression f;
long l: script:python() { filter_out_safe_constants(l) };
unsigned long ul : script:python() { filter_out_safe_constants(ul) };
u64 ul64 : script:python() { filter_out_safe_constants(ul64) };
s64 sl64 : script:python() { filter_out_safe_constants(sl64) };

@@
(
* do_div(f, l);
|
* do_div(f, ul);
|
* do_div(f, ul64);
|
* do_div(f, sl64);
)

@r depends on (org || report)@
expression f;
position p;
long l: script:python() { filter_out_safe_constants(l) };
unsigned long ul : script:python() { filter_out_safe_constants(ul) };
u64 ul64 : script:python() { filter_out_safe_constants(ul64) };
s64 sl64 : script:python() { filter_out_safe_constants(sl64) };
@@
(
do_div@p(f, l);
|
do_div@p(f, ul);
|
do_div@p(f, ul64);
|
do_div@p(f, sl64);
)

@script:python depends on org@
p << r.p;
ul << r.ul;
@@

coccilib.org.print_todo(p[0], construct_warnings("div64_ul"))

@script:python depends on org@
p << r.p;
l << r.l;
@@

coccilib.org.print_todo(p[0], construct_warnings("div64_long"))

@script:python depends on org@
p << r.p;
ul64 << r.ul64;
@@

coccilib.org.print_todo(p[0], construct_warnings("div64_u64"))

@script:python depends on org@
p << r.p;
sl64 << r.sl64;
@@

coccilib.org.print_todo(p[0], construct_warnings("div64_s64"))

@script:python depends on report@
p << r.p;
ul << r.ul;
@@

coccilib.report.print_report(p[0], construct_warnings("div64_ul"))

@script:python depends on report@
p << r.p;
l << r.l;
@@

coccilib.report.print_report(p[0], construct_warnings("div64_long"))

@script:python depends on report@
p << r.p;
sl64 << r.sl64;
@@

coccilib.report.print_report(p[0], construct_warnings("div64_s64"))

@script:python depends on report@
p << r.p;
ul64 << r.ul64;
@@

coccilib.report.print_report(p[0], construct_warnings("div64_u64"))
