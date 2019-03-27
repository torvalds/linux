#
# Copyright (c) 2015 EMC / Isilon Storage Division
# All rights reserved.
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

mqtest1_head()
{
	:
}
mqtest1_body()
{
	atf_check -s exit:0 -x $(atf_get_srcdir)/mqtest1
}

mqtest2_head()
{
	:
}
mqtest2_body()
{
	atf_check -s exit:0 -x $(atf_get_srcdir)/mqtest2
}

mqtest3_head()
{
	:
}
mqtest3_body()
{
	atf_check -s exit:0 -x $(atf_get_srcdir)/mqtest3
}

mqtest4_head()
{
	:
}
mqtest4_body()
{
	atf_check -s exit:0 -x $(atf_get_srcdir)/mqtest4
}

mqtest5_head()
{
	:
}
mqtest5_body()
{
	atf_check -s exit:0 -x $(atf_get_srcdir)/mqtest5
}

atf_init_test_cases()
{
	atf_add_test_case mqtest1
	atf_add_test_case mqtest2
	#atf_add_test_case mqtest3
	#atf_add_test_case mqtest4
	atf_add_test_case mqtest5
}
