#!/bin/sh
# $OpenBSD: t3.sh,v 1.1 2005/04/30 17:36:58 otto Exp $
#
# Test filled prefix and empty name, plus various other edgae cases.
#
CUR=$1
OBJ=$2
uudecode -p << '_EOF' | tar ztf - 2> /dev/null | cmp -s $CUR/t3.out /dev/stdin
begin 644 t3.tar.gz
M'XL("$&5:D("`W0S+G1A<@#MF\%NPC`,AO,HO`&.'<?;XP`35Z:-O?^:!"38
M8:,2M>S.'X<>RJ'5US_Y$[4I+0],"',[9F&X/5Y)&9"R2*Y%$B`6Q+1AA6M+
M7Y_GW<=T*:?S^?3;__XZ?[V1'\=%><+M[Y3\UU(>\T^U^6?.:0/A?W'_^[TQ
M_P6:?P$)_QK^#X>#,?_3/($$-?*OXO]MPI;_-O_3]`O_&OXUF-7_*K;QOSTN
MOOM?UF"%_6_X%V'G^7?BWUS_Z_XI3S4P_"OX-]?_AG^"R+^*?W/];_@O7,*_
MAG]S_4]R7_\).>]_J,+Z^M_PC^@]_T[\F^M_PS]5"?\:_LWUO^&?*?*OXM]<
M_QO^!2C\:_A_69SMS/Y7N__JO?\Y0<'_;E[^F_^2L_?QWPGV^E_WC^!]_V_Y
M8#WE]NWUO^Z?"H=_#?_V^E_WS^['?R?^7Y=G.Z?_"?`8_R7ZGP;6]O^N_MV_
M_Z,0K#7VOXO_XG[_UXE_:_WOXI\E\J_BWUK_N_@7]^]_./%_!!T>](\9<__^
MH\;^CPI'A<=TGO^Q_R<4_M?"H^L_1.(BDH"@N%__O9-1[*[_;ORS^_G?B7]K
M^><,S;_XSW^QROWZWUC^A_\,Q?OZSXE_<_EO_9\RHOO\LUGN]G^LY7_XI^K]
9^S\G_H,@"((@"((@"(+_P3=C<CD]`%``````
`
end
_EOF
