dnl $FreeBSD$
dnl A regression test for m4 C operators		(ksb,petef)
dnl If you think you have a short-circuiting m4, run us m4 -DSHORCIRCUIT=yes
dnl
dnl first level of precedence
ifelse(expr(-7),-7,,`failed -
')dnl
ifelse(expr(- -2),2,,`failed -
')dnl
ifelse(expr(!0),1,,`failed !
')dnl
ifelse(expr(!7),0,,`failed !
')dnl
ifelse(expr(~-1),0,,`failed ~
')dnl
dnl next level of precedence
ifelse(expr(3*5),15,,`failed *
')dnl
ifelse(expr(3*0),0,,`failed *
')dnl
ifelse(expr(11/2),5,,`failed /
')dnl
ifelse(expr(1/700),0,,`failed /
')dnl
ifelse(expr(10%5),0,,`failed %
')dnl
ifelse(expr(2%5),2,,`failed %
')dnl
ifelse(expr(2%-1),0,,`failed %
')dnl
dnl next level of precedence
ifelse(expr(2+2),4,,`failed +
')dnl
ifelse(expr(2+-2),0,,`failed +
')dnl
ifelse(expr(2- -2),4,,`failed -
')dnl
ifelse(expr(2-2),0,,`failed -
')dnl
dnl next level of precedence
ifelse(expr(1<<4),16,,`failed <<
')dnl
ifelse(expr(16>>4),1,,`failed >>
')dnl
dnl next level of precedence
ifelse(expr(4<4),0,,`failed <
')dnl
ifelse(expr(4<5),1,,`failed <
')dnl
ifelse(expr(4<3),0,,`failed <
')dnl
ifelse(expr(4>4),0,,`failed >
')dnl
ifelse(expr(4>5),0,,`failed >
')dnl
ifelse(expr(4>3),1,,`failed >
')dnl
ifelse(expr(4<=4),1,,`failed <=
')dnl
ifelse(expr(4<=5),1,,`failed <=
')dnl
ifelse(expr(4<=3),0,,`failed <=
')dnl
ifelse(expr(4>=4),1,,`failed >=
')dnl
ifelse(expr(4>=5),0,,`failed >=
')dnl
ifelse(expr(4>=3),1,,`failed >=
')dnl
dnl next level of precedence
ifelse(expr(1==1),1,,`failed ==
')dnl
ifelse(expr(1==-1),0,,`failed ==
')dnl
ifelse(expr(1!=1),0,,`failed !=
')dnl
ifelse(expr(1!=2),1,,`failed !=
')dnl
dnl next level of precedence
ifelse(expr(3&5),1,,`failed &
')dnl
ifelse(expr(8&7),0,,`failed &
')dnl
dnl next level of precedence
ifelse(expr(1^1),0,,`failed ^
')dnl
ifelse(expr(21^5),16,,`failed ^
')dnl
dnl next level of precedence
ifelse(expr(1|1),1,,`failed |
')dnl
ifelse(expr(21|5),21,,`failed |
')dnl
ifelse(expr(100|1),101,,`failed |
')dnl
dnl next level of precedence
ifelse(expr(1&&1),1,,`failed &&
')dnl
ifelse(expr(0&&1),0,,`failed &&
')dnl
ifelse(expr(1&&0),0,,`failed &&
')dnl
ifelse(SHORTCIRCUIT,`yes',`ifelse(expr(0&&10/0),0,,`failed && shortcircuit
')')dnl
dnl next level of precedence
ifelse(expr(1||1),1,,`failed ||
')dnl
ifelse(expr(1||0),1,,`failed ||
')dnl
ifelse(expr(0||0),0,,`failed ||
')dnl
ifelse(SHORTCIRCUIT,`yes',`ifelse(expr(1||10/0),1,,`failed || shortcircuit
')')dnl
dnl next level of precedence
ifelse(expr(0 ? 2 : 5),5,,`failed ?:
')dnl
ifelse(expr(1 ? 2 : 5),2,,`failed ?:
')dnl
ifelse(SHORTCIRCUIT,`yes',`ifelse(expr(0 ? 10/0 : 7),7,,`failed ?: shortcircuit
')')dnl
ifelse(SHORTCIRCUIT,`yes',`ifelse(expr(1 ? 7 : 10/0),7,,`failed ?: shortcircuit
')')dnl
dnl operator precedence
ifelse(expr(!0*-2),-2,,`precedence wrong, ! *
')dnl
ifelse(expr(~8/~2),3,,`precedence wrong ~ /
')dnl
ifelse(expr(~-20%7),5,,`precedence wrong ~ %
')dnl
ifelse(expr(3*2+100),106,,`precedence wrong * +
')dnl
ifelse(expr(3+2*100),203,,`precedence wrong + *
')dnl
ifelse(expr(2%5-6/3),0,,`precedence wrong % -
')dnl
ifelse(expr(2/5-5%3),-2,,`precedence wrong / -
')dnl
ifelse(expr(2+5%5+1),3,,`precedence wrong % +
')dnl
ifelse(expr(7+9<<1),32,,`precedence wrong + <<
')dnl
ifelse(expr(35-3>>2),8,,`precedence wrong - >>
')dnl
ifelse(expr(9<10<<5),1,,`precedence wrong << <
')dnl
ifelse(expr(9>10<<5),0,,`precedence wrong << >
')dnl
ifelse(expr(32>>2<32),1,,`precedence wrong >> <
')dnl
ifelse(expr(9<=10<<5),1,,`precedence wrong << <
')dnl
ifelse(expr(5<<1<=20>>1),1,,`precedence wrong << <=
')dnl
ifelse(expr(5<<1>=20>>1),1,,`precedence wrong << >=
')dnl
ifelse(expr(0<7==5>=5),1,,`precedence wrong < ==
')dnl
ifelse(expr(0<7!=5>=5),0,,`precedence wrong < !=
')dnl
ifelse(expr(0>7==5>=5),0,,`precedence wrong > ==
')dnl
ifelse(expr(0>7!=5>=5),1,,`precedence wrong > !=
')dnl
ifelse(expr(1&7==7),1,,`precedence wrong & ==
')dnl
ifelse(expr(0&7!=6),0,,`precedence wrong & !=
')dnl
ifelse(expr(9&1|5),5,,`precedence wrong & |
')dnl
ifelse(expr(9&1^5),4,,`precedence wrong & ^
')dnl
ifelse(expr(9^1|5),13,,`precedence wrong ^ |
')dnl
ifelse(expr(5|0&&1),1,,`precedence wrong | &&
')dnl
ifelse(expr(5&&0||0&&5||5),1,,`precedence wrong && ||
')dnl
ifelse(expr(0 || 1 ? 0 : 1),0,,`precedence wrong || ?:
')dnl
ifelse(expr(5&&(0||0)&&(5||5)),0,,`precedence wrong || parens
')dnl
