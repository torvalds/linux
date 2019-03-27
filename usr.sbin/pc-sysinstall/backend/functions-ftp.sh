#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2010 iXsystems, Inc.  All rights reserved.
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

# Functions which runs commands on the system

. ${BACKEND}/functions.sh
. ${BACKEND}/functions-parse.sh

DEFAULT_FTP_SERVER="ftp.freebsd.org"

MAIN_FTP_SERVERS="\
Main Site: ftp.freebsd.org"

IPV6_FTP_SERVERS="\
IPv6 Main Site: ftp.freebsd.org|\
IPv6 Ireland: ftp3.ie.freebsd.org|\
IPv6 Israel: ftp.il.freebsd.org|\
IPv6 Japan: ftp2.jp.freebsd.org|\
IPv6 Sweden: ftp4.se.freebsd.org|\
IPv6 USA: ftp4.us.freebsd.org|\
IPv6 Turkey: ftp2.tr.freebsd.org"

PRIMARY_FTP_SERVERS="\
Primary: ftp1.freebsd.org|\
Primary #2: ftp2.freebsd.org|\
Primary #3: ftp3.freebsd.org|\
Primary #4: ftp4.freebsd.org|\
Primary #5: ftp5.freebsd.org|\
Primary #6: ftp6.freebsd.org|\
Primary #7: ftp7.freebsd.org|\
Primary #8: ftp8.freebsd.org|\
Primary #9: ftp9.freebsd.org|\
Primary #10: ftp10.freebsd.org|\
Primary #11: ftp11.freebsd.org|\
Primary #12: ftp12.freebsd.org|\
Primary #13: ftp13.freebsd.org|\
Primary #14: ftp14.freebsd.org"

ARGENTINA_FTP_SERVERS="\
Argentina: ftp.ar.freebsd.org"

AUSTRALIA_FTP_SERVERS="\
Australia: ftp.au.freebsd.org|\
Australia #2: ftp2.au.freebsd.org|\
Australia #3: ftp3.au.freebsd.org"

AUSTRIA_FTP_SERVERS="\
Austria: ftp.at.freebsd.org|\
Austria #2: ftp2.at.freebsd.org"

BRAZIL_FTP_SERVERS="\
Brazil: ftp.br.freebsd.org|\
Brazil #2: ftp2.br.freebsd.org|\
Brazil #3: ftp3.br.freebsd.org|\
Brazil #4: ftp4.br.freebsd.org|\
Brazil #5: ftp5.br.freebsd.org|\
Brazil #6: ftp6.br.freebsd.org|\
Brazil #7: ftp7.br.freebsd.org"

CANADA_FTP_SERVERS="\
Canada: ftp.ca.freebsd.org"

CHINA_FTP_SERVERS="\
China: ftp.cn.freebsd.org|\
China #2: ftp2.cn.freebsd.org"

CROATIA_FTP_SERVERS="\
Croatia: ftp.hr.freebsd.org"

CZECH_REPUBLIC_FTP_SERVERS="\
Czech Republic: ftp.cz.freebsd.org"

DENMARK_FTP_SERVERS="\
Denmark: ftp.dk.freebsd.org|\
Denmark #2: ftp2.dk.freebsd.org"

ESTONIA_FTP_SERVERS="\
Estonia: ftp.ee.freebsd.org"

FINLAND_FTP_SERVERS="\
Finland: ftp.fi.freebsd.org"

FRANCE_FTP_SERVERS="\
France: ftp.fr.freebsd.org|\
France #2: ftp2.fr.freebsd.org|\
France #3: ftp3.fr.freebsd.org|\
France #5: ftp5.fr.freebsd.org|\
France #6: ftp6.fr.freebsd.org|\
France #8: ftp8.fr.freebsd.org"

GERMANY_FTP_SERVERS="\
Germany: ftp.de.freebsd.org|\
Germany #2: ftp2.de.freebsd.org|\
Germany #3: ftp3.de.freebsd.org|\
Germany #4: ftp4.de.freebsd.org|\
Germany #5: ftp5.de.freebsd.org|\
Germany #6: ftp6.de.freebsd.org|\
Germany #7: ftp7.de.freebsd.org|\
Germany #8: ftp8.de.freebsd.org"

GREECE_FTP_SERVERS="\
Greece: ftp.gr.freebsd.org|\
Greece #2: ftp2.gr.freebsd.org"

HUNGARY_FTP_SERVERS="\
Hungary: ftp.hu.freebsd.org"

ICELAND_FTP_SERVERS="\
Iceland: ftp.is.freebsd.org"

IRELAND_FTP_SERVERS="\
Ireland: ftp.ie.freebsd.org|\
Ireland #2: ftp2.ie.freebsd.org|\
Ireland #3: ftp3.ie.freebsd.org"

ISRAEL_FTP_SERVERS="\
Israel: ftp.il.freebsd.org"

ITALY_FTP_SERVERS="\
Italy: ftp.it.freebsd.org"

JAPAN_FTP_SERVERS="\
Japan: ftp.jp.freebsd.org|\
Japan #2: ftp2.jp.freebsd.org|\
Japan #3: ftp3.jp.freebsd.org|\
Japan #4: ftp4.jp.freebsd.org|\
Japan #5: ftp5.jp.freebsd.org|\
Japan #6: ftp6.jp.freebsd.org|\
Japan #7: ftp7.jp.freebsd.org|\
Japan #8: ftp8.jp.freebsd.org|\
Japan #9: ftp9.jp.freebsd.org"

KOREA_FTP_SERVERS="\
Korea: ftp.kr.freebsd.org|\
Korea #2: ftp2.kr.freebsd.org"

LITHUANIA_FTP_SERVERS="\
Lithuania: ftp.lt.freebsd.org"

NETHERLANDS_FTP_SERVERS="\
Netherlands: ftp.nl.freebsd.org|\
Netherlands #2: ftp2.nl.freebsd.org"

NORWAY_FTP_SERVERS="\
Norway: ftp.no.freebsd.org|\
Norway #3: ftp3.no.freebsd.org"

POLAND_FTP_SERVERS="\
Poland: ftp.pl.freebsd.org|\
Poland #2: ftp2.pl.freebsd.org|\
Poland #5: ftp5.pl.freebsd.org"

PORTUGAL_FTP_SERVERS="\
Portugal: ftp.pt.freebsd.org|\
Portugal #2: ftp2.pt.freebsd.org|\
Portugal #4: ftp4.pt.freebsd.org"

ROMANIA_FTP_SERVERS="\
Romania: ftp.ro.freebsd.org"

RUSSIA_FTP_SERVERS="\
Russia: ftp.ru.freebsd.org|\
Russia #2: ftp2.ru.freebsd.org|\
Russia #3: ftp3.ru.freebsd.org|\
Russia #4: ftp4.ru.freebsd.org"

SINGAPORE_FTP_SERVERS="\
Singapore: ftp.sg.freebsd.org"

SLOVAK_REPUBLIC_FTP_SERVERS="\
Slovak Republic: ftp.sk.freebsd.org"

SLOVENIA_FTP_SERVERS="\
Slovenia: ftp.si.freebsd.org|\
Slovenia #2: ftp2.si.freebsd.org"

SOUTH_AFRICA_FTP_SERVERS="\
South Africa: ftp.za.freebsd.org|\
South Africa #2: ftp2.za.freebsd.org|\
South Africa #3: ftp3.za.freebsd.org|\
South Africa #4: ftp4.za.freebsd.org"

SPAIN_FTP_SERVERS="\
Spain: ftp.es.freebsd.org|\
Spain #2: ftp2.es.freebsd.org|\
Spain #3: ftp3.es.freebsd.org"

SWEDEN_FTP_SERVERS="\
Sweden: ftp.se.freebsd.org|\
Sweden #2: ftp2.se.freebsd.org|\
Sweden #3: ftp3.se.freebsd.org|\
Sweden #4: ftp4.se.freebsd.org|\
Sweden #5: ftp5.se.freebsd.org"

SWITZERLAND_FTP_SERVERS="\
Switzerland: ftp.ch.freebsd.org|\
Switzerland #2: ftp2.ch.freebsd.org"

TAIWAN_FTP_SERVERS="\
Taiwan: ftp.tw.freebsd.org|\
Taiwan #2: ftp2.tw.freebsd.org|\
Taiwan #3: ftp3.tw.freebsd.org|\
Taiwan #4: ftp4.tw.freebsd.org|\
Taiwan #6: ftp6.tw.freebsd.org|\
Taiwan #11: ftp11.tw.freebsd.org"

TURKEY_FTP_SERVERS="\
Turkey: ftp.tr.freebsd.org|\
Turkey #2: ftp2.tr.freebsd.org"

UK_FTP_SERVERS="\
UK: ftp.uk.freebsd.org|\
UK #2: ftp2.uk.freebsd.org|\
UK #3: ftp3.uk.freebsd.org|\
UK #4: ftp4.uk.freebsd.org|\
UK #5: ftp5.uk.freebsd.org|\
UK #6: ftp6.uk.freebsd.org"

UKRAINE_FTP_SERVERS="\
Ukraine: ftp.ua.freebsd.org|\
Ukraine #2: ftp2.ua.freebsd.org|\
Ukraine #5: ftp5.ua.freebsd.org|\
Ukraine #6: ftp6.ua.freebsd.org|\
Ukraine #7: ftp7.ua.freebsd.org|\
Ukraine #8: ftp8.ua.freebsd.org"

USA_FTP_SERVERS="\
USA #1: ftp1.us.freebsd.org|\
USA #2: ftp2.us.freebsd.org|\
USA #3: ftp3.us.freebsd.org|\
USA #4: ftp4.us.freebsd.org|\
USA #5: ftp5.us.freebsd.org|\
USA #6: ftp6.us.freebsd.org|\
USA #7: ftp7.us.freebsd.org|\
USA #8: ftp8.us.freebsd.org|\
USA #9: ftp9.us.freebsd.org|\
USA #10: ftp10.us.freebsd.org|\
USA #11: ftp11.us.freebsd.org|\
USA #12: ftp12.us.freebsd.org|\
USA #13: ftp13.us.freebsd.org|\
USA #14: ftp14.us.freebsd.org|\
USA #15: ftp15.us.freebsd.org"

show_mirrors()
{
  MIRRORS="${1}"
  if [ -n "${MIRRORS}" ]
  then
    SAVE_IFS="${IFS}"
    IFS="|"
    for m in ${MIRRORS}
    do
      echo "$m"
    done
    IFS="${SAVE_IFS}"
  fi
};

set_ftp_mirror()
{
  MIRROR="${1}"
  echo "${MIRROR}" > "${CONFDIR}/mirrors.conf"
};

get_ftp_mirror()
{
  MIRROR="${DEFAULT_FTP_SERVER}"
  if [ -f "${CONFDIR}/mirrors.conf" ]
  then
    MIRROR=`cat "${CONFDIR}/mirrors.conf"`
  fi

  export VAL="${MIRROR}"
};


get_ftpHost()
{
  get_value_from_cfg ftpPath
  ftpPath="$VAL"

  ftpHost=`echo "${ftpPath}" | sed -E 's|^(ftp://)([^/]*)(.*)|\2|'`
  export VAL="${ftpHost}"
};

get_ftpDir()
{
  get_value_from_cfg ftpPath
  ftpPath="$VAL"

  ftpDir=`echo "${ftpPath}" | sed -E 's|^(ftp://)([^/]*)(.*)|\3|'`
  export VAL="${ftpDir}"
};

get_ftp_mirrors()
{
  COUNTRY="${1}"
  if [ -n "$COUNTRY" ]
  then
    COUNTRY=`echo $COUNTRY|tr A-Z a-z`
    case "${COUNTRY}" in
      argentina*) VAL="${ARGENTINA_FTP_SERVERS}" ;;
      australia*) VAL="${AUSTRALIA_FTP_SERVERS}" ;;
      austria*) VAL="${AUSTRIA_FTP_SERVERS}" ;;
      brazil*) VAL="${BRAZIL_FTP_SERVERS}" ;;
      canada*) VAL="${CANADA_FTP_SERVERS}" ;;
      china*) VAL="${CHINA_FTP_SERVERS}" ;;
      croatia*) VAL="${CROATIA_FTP_SERVERS}" ;;
      czech*) VAL="${CZECH_REPUBLIC_FTP_SERVERS}" ;;
      denmark*) VAL="${DENMARK_FTP_SERVERS}" ;;
      estonia*) VAL="${ESTONIA_FTP_SERVERS}" ;;
      finland*) VAL="${FINLAND_FTP_SERVERS}" ;;
      france*) VAL="${FRANCE_FTP_SERVERS}" ;;
      germany*) VAL="${GERMANY_FTP_SERVERS}" ;;
      greece*) VAL="${GREECE_FTP_SERVERS}" ;;
      hungary*) VAL="${HUNGARY_FTP_SERVERS}" ;;
      iceland*) VAL="${ICELAND_FTP_SERVERS}" ;;
      ireland*) VAL="${IRELAND_FTP_SERVERS}" ;;
      israel*) VAL="${ISRAEL_FTP_SERVERS}" ;;
      italy*) VAL="${ITALY_FTP_SERVERS}" ;;
      japan*) VAL="${JAPAN_FTP_SERVERS}" ;;
      korea*) VAL="${KOREA_FTP_SERVERS}" ;;
      lithuania*) VAL="${LITHUANIA_FTP_SERVERS}" ;;
      netherlands*) VAL="${NETHERLANDS_FTP_SERVERS}" ;;
      norway*) VAL="${NORWAY_FTP_SERVERS}" ;;
      poland*) VAL="${POLAND_FTP_SERVERS}" ;;
      portugal*) VAL="${PORTUGAL_FTP_SERVERS}" ;;
      romania*) VAL="${ROMAINIA_FTP_SERVERS}" ;;
      russia*) VAL="${RUSSIA_FTP_SERVERS}" ;;
      singapore*) VAL="${SINGAPORE_FTP_SERVERS}" ;;
      slovak*) VAL="${SLOVAK_REPUBLIC_FTP_SERVERS}" ;;
      slovenia*) VAL="${SLOVENIA_FTP_SERVERS}" ;;
      *africa*) VAL="${SOUTH_AFRICA_FTP_SERVERS}" ;;
      spain*) VAL="${SPAIN_FTP_SERVERS}" ;;
      sweden*) VAL="${SWEDEN_FTP_SERVERS}" ;;
      switzerland*) VAL="${SWITZERLAND_FTP_SERVERS}" ;;
      taiwan*) VAL="${TAIWAN_FTP_SERVERS}" ;;
      turkey*) VAL="${TURKEY_FTP_SERVERS}" ;;
      ukraine*) VAL="${UKRAINE_FTP_SERVERS}" ;;
      uk*) VAL="${UK_FTP_SERVERS}" ;;
      usa*) VAL="${USA_FTP_SERVERS}" ;;
    esac
  else
    VAL="${MAIN_FTP_SERVERS}"
    VAL="${VAL}|${IPV6_FTP_SERVERS}"
    VAL="${VAL}|${PRIMARY_FTP_SERVERS}"
    VAL="${VAL}|${ARGENTINA_FTP_SERVERS}"
    VAL="${VAL}|${AUSTRALIA_FTP_SERVERS}"
    VAL="${VAL}|${AUSTRIA_FTP_SERVERS}"
    VAL="${VAL}|${BRAZIL_FTP_SERVERS}"
    VAL="${VAL}|${CANADA_FTP_SERVERS}"
    VAL="${VAL}|${CHINA_FTP_SERVERS}"
    VAL="${VAL}|${CROATIA_FTP_SERVERS}"
    VAL="${VAL}|${CZECH_REPUBLIC_FTP_SERVERS}"
    VAL="${VAL}|${DENMARK_FTP_SERVERS}"
    VAL="${VAL}|${ESTONIA_FTP_SERVERS}"
    VAL="${VAL}|${FINLAND_FTP_SERVERS}"
    VAL="${VAL}|${FRANCE_FTP_SERVERS}"
    VAL="${VAL}|${GERMANY_FTP_SERVERS}"
    VAL="${VAL}|${GREECE_FTP_SERVERS}"
    VAL="${VAL}|${HUNGARY_FTP_SERVERS}"
    VAL="${VAL}|${ICELAND_FTP_SERVERS}"
    VAL="${VAL}|${IRELAND_FTP_SERVERS}"
    VAL="${VAL}|${ISRAEL_FTP_SERVERS}"
    VAL="${VAL}|${ITALY_FTP_SERVERS}"
    VAL="${VAL}|${JAPAN_FTP_SERVERS}"
    VAL="${VAL}|${KOREA_FTP_SERVERS}"
    VAL="${VAL}|${LITHUANIA_FTP_SERVERS}"
    VAL="${VAL}|${NETHERLANDS_FTP_SERVERS}"
    VAL="${VAL}|${NORWAY_FTP_SERVERS}"
    VAL="${VAL}|${POLAND_FTP_SERVERS}"
    VAL="${VAL}|${PORTUGAL_FTP_SERVERS}"
    VAL="${VAL}|${ROMANIA_FTP_SERVERS}"
    VAL="${VAL}|${RUSSIA_FTP_SERVERS}"
    VAL="${VAL}|${SINGAPORE_FTP_SERVERS}"
    VAL="${VAL}|${SLOVAK_REPUBLIC_FTP_SERVERS}"
    VAL="${VAL}|${SLOVENIA_FTP_SERVERS}"
    VAL="${VAL}|${SOUTH_AFRICA_FTP_SERVERS}"
    VAL="${VAL}|${SPAIN_FTP_SERVERS}"
    VAL="${VAL}|${SWEDEN_FTP_SERVERS}"
    VAL="${VAL}|${SWITZERLAND_FTP_SERVERS}"
    VAL="${VAL}|${TAIWAN_FTP_SERVERS}"
    VAL="${VAL}|${TURKEY_FTP_SERVERS}"
    VAL="${VAL}|${UKRAINE_FTP_SERVERS}"
    VAL="${VAL}|${UK_FTP_SERVERS}"
    VAL="${VAL}|${USA_FTP_SERVERS}"
  fi

  export VAL
};
