#!/bin/sh
#
# Canon BJC-240 Setup script
#
# Settings are:
# Epson LQ emulation, A4, Code Page 866, 66 lines, Roman font, Smoothing,
# HQ mode, no CR translation, power off in 10min, auto power on
#

printf "\033[K\00200\037" | tr 0 "\0"

cat << EOF1
BJLSTART
ControlMode=BJ
Font=Roman
PageLength=12
CodePage=866
AutoLF=Off
TextScaleMode=On
AutoCR=Off
CharacterSet=Set2
AGM=Off
BJLEND
EOF1

printf "\033[K\00200\037" | tr 0 "\0"

cat << EOF2
BJLSTART
ControlMode=LQ
Font=Roman
PageLength=12
CodePage=866
AutoLF=Off
TextScaleMode=On
CharacterSet=Graphics
International=USA
BJLEND
EOF2

printf "\033[K\00200\037" | tr 0 "\0"

cat << EOF3
BJLSTART
@SetControlMode=LQ
BJLEND
EOF3

printf "\033[K\00200\037" | tr 0 "\0"

cat << EOF4
BJLSTART
ControlMode=Common
PrintMode=HQ
Reduction=Off
Smoothing=On
PaperSelect=A4
I/D-Buffer=Input
AutoPowerOff=10
AutoPowerOn=Enable
BJLEND
EOF4

exec /usr/libexec/lpr/ru/koi2alt $*
