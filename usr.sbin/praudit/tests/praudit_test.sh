#
# Copyright (c) 2018 Aniket Pandey
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#


atf_test_case praudit_delim_comma
praudit_delim_comma_head()
{
	atf_set "descr" "Verify that comma delimiter is present with -d ',' cmd"
}

praudit_delim_comma_body()
{
	atf_check -o file:$(atf_get_srcdir)/del_comma \
		praudit -d "," $(atf_get_srcdir)/trail
}


atf_test_case praudit_delim_underscore
praudit_delim_underscore_head()
{
	atf_set "descr" "Verify that underscore delimiter is present with -d _"
}

praudit_delim_underscore_body()
{
	atf_check -o file:$(atf_get_srcdir)/del_underscore \
		praudit -d "_" $(atf_get_srcdir)/trail
}


atf_test_case praudit_no_args
praudit_no_args_head()
{
	atf_set "descr" "Verify that praudit outputs default form without " \
			"any arguments"
}

praudit_no_args_body()
{
	atf_check -o file:$(atf_get_srcdir)/no_args \
		praudit $(atf_get_srcdir)/trail
}


atf_test_case praudit_numeric_form
praudit_numeric_form_head()
{
	atf_set "descr" "Verify that praudit outputs the numeric form " \
			"with -n flag"
}

praudit_numeric_form_body()
{
	atf_check -o file:$(atf_get_srcdir)/numeric_form \
		praudit -n $(atf_get_srcdir)/trail
}


atf_test_case praudit_raw_form
praudit_raw_form_head()
{
	atf_set "descr" "Verify that praudit outputs the raw form with -r flag"
}

praudit_raw_form_body()
{
	atf_check -o file:$(atf_get_srcdir)/raw_form \
		praudit -r $(atf_get_srcdir)/trail
}


atf_test_case praudit_same_line
praudit_same_line_head()
{
	atf_set "descr" "Verify that praudit outputs the trail in the same " \
			"line  with -l flag"
}

praudit_same_line_body()
{
	atf_check -o file:$(atf_get_srcdir)/same_line \
		praudit -l $(atf_get_srcdir)/trail
}


atf_test_case praudit_short_form
praudit_short_form_head()
{
	atf_set "descr" "Verify that praudit outputs the short form " \
			"with -s flag"
}

praudit_short_form_body()
{
	atf_check -o file:$(atf_get_srcdir)/short_form \
		praudit -s $(atf_get_srcdir)/trail
}


atf_test_case praudit_xml_form
praudit_xml_form_head()
{
	atf_set "descr" "Verify that praudit outputs the XML file with -x flag"
}

praudit_xml_form_body()
{
	atf_check -o file:$(atf_get_srcdir)/xml_form \
		praudit -x $(atf_get_srcdir)/trail
}


atf_test_case praudit_sync_to_next_record
praudit_sync_to_next_record_head()
{
	atf_set "descr" "Verify that praudit(1) outputs the last few audit " \
			"records when the initial part of the trail is " \
			"corrputed."
}

praudit_sync_to_next_record_body()
{
	# The 'corrupted' binary file contains some redundant
	# binary symbols before the actual audit record.
	# Since 'praudit -p' syncs to the next legitimate record,
	# it would skip the corrupted part and print the desired
	# audit record to STDOUT.
	atf_check -o file:$(atf_get_srcdir)/no_args \
		praudit -p $(atf_get_srcdir)/corrupted
}


atf_test_case praudit_raw_short_exclusive
praudit_raw_short_exclusive_head()
{
	atf_set "descr" "Verify that praudit outputs usage message on stderr " \
			"when both raw and short options are specified"
}

praudit_raw_short_exclusive_body()
{
	atf_check -s exit:1 -e match:"usage: praudit" \
		praudit -rs $(atf_get_srcdir)/trail
}


atf_init_test_cases()
{
	atf_add_test_case praudit_delim_comma
	atf_add_test_case praudit_delim_underscore
	atf_add_test_case praudit_no_args
	atf_add_test_case praudit_numeric_form
	atf_add_test_case praudit_raw_form
	atf_add_test_case praudit_same_line
	atf_add_test_case praudit_short_form
	atf_add_test_case praudit_xml_form
	atf_add_test_case praudit_sync_to_next_record
	atf_add_test_case praudit_raw_short_exclusive
}
