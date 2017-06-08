#!/bin/sh

PARAM="-npro -kr -i8 -ts8 -sob -l80 -ss -ncs -cp1"

RES=`indent --version | cut -d' ' -f3`
if [ "$RES" = "" ]; then
	exit 1
fi
V1=`echo $RES | cut -d'.' -f1`
V2=`echo $RES | cut -d'.' -f2`
V3=`echo $RES | cut -d'.' -f3`

if [ $V1 -gt 2 ]; then
  PARAM="$PARAM -il0"
elif [ $V1 -eq 2 ]; then
  if [ $V2 -gt 2 ]; then
    PARAM="$PARAM -il0"
  elif [ $V2 -eq 2 ]; then
    if [ $V3 -ge 10 ]; then
      PARAM="$PARAM -il0"
    fi
  fi
fi

indent $PARAM "$@"
