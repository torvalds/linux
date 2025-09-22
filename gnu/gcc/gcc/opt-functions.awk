#  Copyright (C) 2003,2004 Free Software Foundation, Inc.
#  Contributed by Kelley Cook, June 2004.
#  Original code from Neil Booth, May 2003.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

# Some common subroutines for use by opt[ch]-gen.awk.

# Return nonzero if FLAGS contains a flag matching REGEX.
function flag_set_p(regex, flags)
{
	return (" " flags " ") ~ (" " regex " ")
}

# Return STRING if FLAGS contains a flag matching regexp REGEX,
# otherwise return the empty string.
function test_flag(regex, flags, string)
{
	if (flag_set_p(regex, flags))
		return string
	return ""
}

# If FLAGS contains a "NAME(...argument...)" flag, return the value
# of the argument.  Return the empty string otherwise.
function opt_args(name, flags)
{
	flags = " " flags
	if (flags !~ " " name "\\(")
		return ""
	sub(".* " name "\\(", "", flags)
	sub("\\).*", "", flags)

	return flags
}

# Return the Nth comma-separated element of S.  Return the empty string
# if S does not contain N elements.
function nth_arg(n, s)
{
	while (n-- > 0) {
		if (s !~ ",")
			return ""
		sub("[^,]*, *", "", s)
	}
	sub(",.*", "", s)
	return s
}

# Return a bitmask of CL_* values for option flags FLAGS.
function switch_flags (flags)
{
	result = "0"
	for (j = 0; j < n_langs; j++) {
		regex = langs[j]
		gsub ( "\\+", "\\+", regex )
		result = result test_flag(regex, flags, " | " macros[j])
	}
	result = result \
	  test_flag("Common", flags, " | CL_COMMON") \
	  test_flag("Target", flags, " | CL_TARGET") \
	  test_flag("Joined", flags, " | CL_JOINED") \
	  test_flag("JoinedOrMissing", flags, " | CL_JOINED | CL_MISSING_OK") \
	  test_flag("Separate", flags, " | CL_SEPARATE") \
	  test_flag("RejectNegative", flags, " | CL_REJECT_NEGATIVE") \
	  test_flag("UInteger", flags, " | CL_UINTEGER") \
	  test_flag("Undocumented", flags,  " | CL_UNDOCUMENTED") \
	  test_flag("Report", flags, " | CL_REPORT")
	sub( "^0 \\| ", "", result )
	return result
}

# If FLAGS includes a Var flag, return the name of the variable it specifies.
# Return the empty string otherwise.
function var_name(flags)
{
	return nth_arg(0, opt_args("Var", flags))
}

# Return true if the option described by FLAGS has a globally-visible state.
function global_state_p(flags)
{
	return (var_name(flags) != "" \
		|| opt_args("Mask", flags) != "" \
		|| opt_args("InverseMask", flags) != "")
}

# Return true if the option described by FLAGS must have some state
# associated with it.
function needs_state_p(flags)
{
	return flag_set_p("Target", flags)
}

# If FLAGS describes an option that needs a static state variable,
# return the name of that variable, otherwise return "".  NAME is
# the name of the option.
function static_var(name, flags)
{
	if (global_state_p(flags) || !needs_state_p(flags))
		return ""
	gsub ("[^A-Za-z0-9]", "_", name)
	return "VAR_" name
}

# Return the type of variable that should be associated with the given flags.
function var_type(flags)
{
	if (!flag_set_p("Joined.*", flags))
		return "int "
	else if (flag_set_p("UInteger", flags))
		return "int "
	else
		return "const char *"
}

# Given that an option has flags FLAGS, return an initializer for the
# "var_cond" and "var_value" fields of its cl_options[] entry.
function var_set(flags)
{
	s = nth_arg(1, opt_args("Var", flags))
	if (s != "")
		return "CLVC_EQUAL, " s
	s = opt_args("Mask", flags);
	if (s != "") {
		vn = var_name(flags);
		if (vn)
			return "CLVC_BIT_SET, OPTION_MASK_" s
		else
			return "CLVC_BIT_SET, MASK_" s
	}
	s = nth_arg(0, opt_args("InverseMask", flags));
	if (s != "") {
		vn = var_name(flags);
		if (vn)
			return "CLVC_BIT_CLEAR, OPTION_MASK_" s
		else
			return "CLVC_BIT_CLEAR, MASK_" s
	}
	if (var_type(flags) == "const char *")
		return "CLVC_STRING, 0"
	return "CLVC_BOOLEAN, 0"
}

# Given that an option called NAME has flags FLAGS, return an initializer
# for the "flag_var" field of its cl_options[] entry.
function var_ref(name, flags)
{
	name = var_name(flags) static_var(name, flags)
	if (name != "")
		return "&" name
	if (opt_args("Mask", flags) != "")
		return "&target_flags"
	if (opt_args("InverseMask", flags) != "")
		return "&target_flags"
	return "0"
}
