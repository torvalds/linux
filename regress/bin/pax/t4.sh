# $OpenBSD: t4.sh,v 1.1 2005/06/13 19:22:02 otto Exp $
# Test extraction and building of old style tar archives
#
CUR=$1
mkdir -p t4 && cd t4
uudecode -p << 'EOF' | tar -zx -f -
begin 644 t4.tar.gz
M'XL("&2>@T(``W0T9VYU+G1A<@#MULT-@S`,AN&,P@;8^2%TG%X8HM,W`7$G
MB+A*]3X7"P4).5\,K&MOLY,BIU2KYB3[M:@>]>!4?(AY"2%Y)UZRBIN2&\C9
MR%D'T3W^=7[7_5ABO)1_N7//OQR7Z==[TV30_%_]S4WSG^O\J\;$_%O8Q$;3
M_&L.,3#_%K;^\^^:\_<JD?>_B8_!,V[D'WSY_U.+P_EDDR/F;W``VO,ORWO^
>U[\OMSW9Y(#Y`P``````````X+]\`:H?&NT`*```
`
end
EOF
tar -cO -f - *  | tar -tf - | cmp -s $CUR/t4.out -
ret=$?
cd .. && rm -rf t4
exit $ret
