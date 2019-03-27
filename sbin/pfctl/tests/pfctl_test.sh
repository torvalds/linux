# $FreeBSD$
# Make will add a #! line at the top of this file.

# Tests 0001-0999 are copied from OpenBSD's regress/sbin/pfctl.
# Tests 1001-1999 are ours (FreeBSD's own).

# pf: Run pfctl -nv on pfNNNN.in and check that the output matches pfNNNN.ok.
#     Copied from OpenBSD.  Main differences are some things not working
#     in FreeBSD:
#         * The action 'match'
#         * The command 'set reassemble'
#         * The 'from'/'to' options together with 'route-to'
#         * The option 'scrub' (it is an action in FreeBSD)
#         * Accepting undefined routing tables in actions (??: see pf0093.in)
#         * The 'route' option
#         * The 'set queue def' option
# selfpf: Feed pfctl output through pfctl again and verify it stays the same.
#         Copied from OpenBSD.

pftests="0001 0002 0003 0004 0005 0006 0007 0008 0009 0010 0011 0012
0013 0014 0016 0018 0019 0020 0022 0023 0024 0025 0026 0028 0030 0031
0032 0034 0035 0038 0039 0040 0041 0047 0048 0049 0050 0052 0053 0055
0056 0057 0060 0061 0065 0067 0069 0070 0071 0072 0074 0075 0077 0078
0079 0081 0082 0084 0085 0087 0088 0089 0090 0091 0092 0094 0095 0096
0097 0098 0100 0101 0102 0104 1001 1002 1003 1004 1005"

. $(atf_get_srcdir)/files/pfctl_test_descr.sh

for i in ${pftests} ; do
    atf_test_case "pf${i}"
    eval "pf${i}_head () { atf_set descr \"$(pf${i}_descr)\" ; }"
    eval "pf${i}_body () { \
              kldstat -m pf || atf_skip \"pf(4) is not loaded\" && \
              cd $(atf_get_srcdir)/files && \
              atf_check -o file:pf${i}.ok \
                  pfctl -o none -nvf - < pf${i}.in ; }"

    atf_test_case "selfpf${i}"
    eval "selfpf${i}_head () { atf_set descr \"self$(pf${i}_descr)\" ; }"
    eval "selfpf${i}_body () { \
              cd $(atf_get_srcdir)/files && \
              atf_check -o file:pf${i}.ok \
                  pfctl -o none -nvf - < pf${i}.ok ; }"
done

atf_init_test_cases () {
    for i in ${pftests} ; do atf_add_test_case "pf${i}"
			     atf_add_test_case "selfpf${i}" ; done ; }
