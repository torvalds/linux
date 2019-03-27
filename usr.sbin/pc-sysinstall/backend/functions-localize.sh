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


# Function which localizes a FreeBSD install
localize_freebsd()
{
  sed -i.bak "s/lang=en_US/lang=${LOCALE}/g" ${FSMNT}/etc/login.conf
  rm ${FSMNT}/etc/login.conf.bak
};

localize_x_desktops() {

  # Check for and customize KDE lang
  ##########################################################################

  # Check if we can localize KDE via skel
  if [ -e "${FSMNT}/usr/share/skel/.kde4/share/config/kdeglobals" ] ; then
    sed -i '' "s/Country=us/Country=${COUNTRY}/g" ${FSMNT}/usr/share/skel/.kde4/share/config/kdeglobals
    sed -i '' "s/Country=us/Country=${COUNTRY}/g" ${FSMNT}/root/.kde4/share/config/kdeglobals
    sed -i '' "s/Language=en_US/Language=${SETLANG}:${LOCALE}/g" ${FSMNT}/usr/share/skel/.kde4/share/config/kdeglobals
  fi

  # Check if we have a KDE root config
  if [ -e "${FSMNT}/root/.kde4/share/config/kdeglobals" ] ; then
    sed -i '' "s/Language=en_US/Language=${SETLANG}:${LOCALE}/g" ${FSMNT}/root/.kde4/share/config/kdeglobals
  fi

  # Check for KDM
  if [ -e "${FSMNT}/usr/local/kde4/share/config/kdm/kdmrc" ] ; then
    sed -i '' "s/Language=en_US/Language=${LOCALE}.UTF-8/g" ${FSMNT}/usr/local/kde4/share/config/kdm/kdmrc
  fi

  # Check for and customize GNOME / GDM lang
  ##########################################################################

  # See if GDM is enabled and customize its lang
  cat ${FSMNT}/etc/rc.conf 2>/dev/null | grep -q "gdm_enable=\"YES\"" 2>/dev/null
  if [ "$?" = "0" ] ; then
    echo "gdm_lang=\"${LOCALE}.UTF-8\"" >> ${FSMNT}/etc/rc.conf
  fi

};

# Function which localizes a PC-BSD install
localize_pcbsd()
{
  # Check if we have a localized splash screen and copy it
  if [ -e "${FSMNT}/usr/local/share/pcbsd/splash-screens/loading-screen-${SETLANG}.pcx" ]
  then
    cp ${FSMNT}/usr/local/share/pcbsd/splash-screens/loading-screen-${SETLANG}.pcx ${FSMNT}/boot/loading-screen.pcx    
  fi

};

localize_x_keyboard()
{
  KEYMOD="$1"
  KEYLAY="$2"
  KEYVAR="$3"
  COUNTRY="$4"
  OPTION="grp:alt_shift_toggle"
  SETXKBMAP=""

  if [ "${COUNTRY}" = "NONE" -o "${COUNTRY}" = "us" -o "${COUNTRY}" = "C" ] ; then
    #In this case we don't need any additional language
    COUNTRY=""
    OPTION=""
  else
    COUNTRY=",${COUNTRY}"
  fi

  if [ "${KEYMOD}" != "NONE" ]
  then
    SETXKBMAP="-model ${KEYMOD}"
    KXMODEL="${KEYMOD}"
  else
    KXMODEL="pc104"
  fi

  if [ "${KEYLAY}" != "NONE" ]
  then
    localize_key_layout "$KEYLAY"
    SETXKBMAP="${SETXKBMAP} -layout ${KEYLAY}"
    KXLAYOUT="${KEYLAY}"
  else
    KXLAYOUT="us"
  fi

  if [ "${KEYVAR}" != "NONE" ]
  then
    SETXKBMAP="${SETXKBMAP} -variant ${KEYVAR}"
    KXVAR="(${KEYVAR})"
  else
    KXVAR=""
  fi

  # Setup .xprofile with our setxkbmap call now
  if [ ! -z "${SETXKBMAP}" ]
  then
    if [ ! -e "${FSMNT}/usr/share/skel/.xprofile" ]
    then
      echo "#!/bin/sh" >${FSMNT}/usr/share/skel/.xprofile
    fi

    # Save the keyboard layout for user / root X logins
    echo "setxkbmap ${SETXKBMAP}" >>${FSMNT}/usr/share/skel/.xprofile
    chmod 755 ${FSMNT}/usr/share/skel/.xprofile
    cp ${FSMNT}/usr/share/skel/.xprofile ${FSMNT}/root/.xprofile

    # Save it for KDM
    if [ -e "${FSMNT}/usr/local/kde4/share/config/kdm/Xsetup" ] ; then
      echo "setxkbmap ${SETXKBMAP}" >>${FSMNT}/usr/local/kde4/share/config/kdm/Xsetup
    fi
  fi
 
  # Create the kxkbrc configuration using these options
  if [ -d "${FSMNT}/usr/share/skel/.kde4/share/config" ] ; then
    echo "[Layout]
DisplayNames=${KXLAYOUT}${COUNTRY}
IndicatorOnly=false
LayoutList=${KXLAYOUT}${KXVAR}${COUNTRY}
Model=${KXMODEL}
Options=${OPTION}
ResetOldOptions=true
ShowFlag=true
ShowSingle=false
SwitchMode=WinClass
Use=true " >${FSMNT}/usr/share/skel/.kde4/share/config/kxkbrc
  fi

};

localize_key_layout()
{

  KEYLAYOUT="$1"

  # Set the keylayout in rc.conf
  case ${KEYLAYOUT} in
    am) KEYLAYOUT_CONSOLE="hy.armscii-8" ;;
    ca) KEYLAYOUT_CONSOLE="fr_CA.acc.iso" ;;
    ch) KEYLAYOUT_CONSOLE="swissgerman.iso" ;;
    cz) KEYLAYOUT_CONSOLE="cz.iso2" ;;
    de) KEYLAYOUT_CONSOLE="german.iso" ;;
    dk) KEYLAYOUT_CONSOLE="danish.iso" ;;
    ee) KEYLAYOUT_CONSOLE="estonian.iso" ;;
    es) KEYLAYOUT_CONSOLE="spanish.iso" ;;
    fi) KEYLAYOUT_CONSOLE="finnish.iso" ;;
    is) KEYLAYOUT_CONSOLE="icelandic.iso" ;;
    jp) KEYLAYOUT_CONSOLE="jp.106" ;;
    nl) KEYLAYOUT_CONSOLE="dutch.iso.acc" ;;
    no) KEYLAYOUT_CONSOLE="norwegian.iso" ;;
    pl) KEYLAYOUT_CONSOLE="pl_PL.ISO8859-2" ;;
    ru) KEYLAYOUT_CONSOLE="ru.koi8-r" ;;
    sk) KEYLAYOUT_CONSOLE="sk.iso2" ;;
    se) KEYLAYOUT_CONSOLE="swedish.iso" ;;
    tr) KEYLAYOUT_CONSOLE="tr.iso9.q" ;;
    gb) KEYLAYOUT_CONSOLE="uk.iso" ;;
     *)  if [ ! -z "${KEYLAYOUT}" ]
         then
           KEYLAYOUT_CONSOLE="${KEYLAYOUT}.iso"
         fi
        ;;
  esac

  if [ -n "${KEYLAYOUT_CONSOLE}" ]
  then
    echo "keymap=\"${KEYLAYOUT_CONSOLE}\"" >>${FSMNT}/etc/rc.conf
  fi

};

#  Function which prunes other l10n files from the KDE install
localize_prune_langs()
{
  get_value_from_cfg localizeLang
  KEEPLANG="$VAL"
  if [ -z "$KEEPLANG" ] ; then
    KEEPLANG="en"
  fi
  export KEEPLANG 

  echo_log "Pruning other l10n files, keeping ${KEEPLANG}"

  # Create the script to do uninstalls
  echo '#!/bin/sh

  for i in `pkg_info -xEI kde-l10n`
  do
    echo "$i" | grep "${KEEPLANG}-kde"
    if [ $? -ne 0 ] ; then
      pkg_delete ${i}
    fi
  done
  ' > ${FSMNT}/.pruneLangs.sh

  chmod 755 ${FSMNT}/.pruneLangs.sh
  chroot ${FSMNT} /.pruneLangs.sh >/dev/null 2>/dev/null
  rm ${FSMNT}/.pruneLangs.sh

};

# Function which sets COUNTRY SETLANG and LOCALE based upon $1
localize_get_codes()
{ 
  TARGETLANG="${1}"
  # Setup the presets for the specific lang
  case $TARGETLANG in
    af)
      COUNTRY="C"
      SETLANG="af"
      LOCALE="af_ZA"
      ;;
    ar)
	  COUNTRY="C"
      SETLANG="ar"
      LOCALE="en_US"
      ;;
    az)
	  COUNTRY="C"
      SETLANG="az"
      LOCALE="en_US"
      ;;
    ca)
	  COUNTRY="es"
      SETLANG="es:ca"
      LOCALE="ca_ES"
      ;;
    be)
	  COUNTRY="be"
      SETLANG="be"
      LOCALE="be_BY"
      ;;
    bn)
	  COUNTRY="bn"
      SETLANG="bn"
      LOCALE="en_US"
      ;;
    bg)
	  COUNTRY="bg"
      SETLANG="bg"
      LOCALE="bg_BG"
      ;;
    cs)
	  COUNTRY="cz"
      SETLANG="cs"
      LOCALE="cs_CZ"
      ;;
    da)
	  COUNTRY="dk"
      SETLANG="da"
      LOCALE="da_DK"
      ;;
    de)
	  COUNTRY="de"
      SETLANG="de"
      LOCALE="de_DE"
      ;;
    en_GB)
	  COUNTRY="gb"
      SETLANG="en_GB:cy"
      LOCALE="en_GB"
      ;;
    el)
	  COUNTRY="gr"
      SETLANG="el:gr"
      LOCALE="el_GR"
      ;;
    es)
	  COUNTRY="es"
      SETLANG="es"
      LOCALE="es_ES"
      ;;
    es_LA)
	  COUNTRY="us"
      SETLANG="es:en_US"
      LOCALE="es_ES"
      ;;
    et)
	  COUNTRY="ee"
      SETLANG="et"
      LOCALE="et_EE"
      ;;
    fr)
	  COUNTRY="fr"
      SETLANG="fr"
      LOCALE="fr_FR"
      ;;
    he)
	  COUNTRY="il"
      SETLANG="he:ar"
      LOCALE="he_IL"
      ;;
    hr)
	  COUNTRY="hr"
      SETLANG="hr"
      LOCALE="hr_HR"
      ;;
    hu)
	  COUNTRY="hu"
      SETLANG="hu"
      LOCALE="hu_HU"
      ;;
    it)
	  COUNTRY="it"
      SETLANG="it"
      LOCALE="it_IT"
      ;;
    ja)
	  COUNTRY="jp"
      SETLANG="ja"
      LOCALE="ja_JP"
      ;;
    ko)
	  COUNTRY="kr"
      SETLANG="ko"
      LOCALE="ko_KR"
      ;;
    nl)
	  COUNTRY="nl"
      SETLANG="nl"
      LOCALE="nl_NL"
      ;;
    nn)
	  COUNTRY="no"
      SETLANG="nn"
      LOCALE="en_US"
      ;;
    pa)
	  COUNTRY="pa"
      SETLANG="pa"
      LOCALE="en_US"
      ;;
    pl)
	  COUNTRY="pl"
      SETLANG="pl"
      LOCALE="pl_PL"
      ;;
    pt)
	  COUNTRY="pt"
      SETLANG="pt"
      LOCALE="pt_PT"
      ;;
    pt_BR)
	  COUNTRY="br"
      SETLANG="pt_BR"
      LOCALE="pt_BR"
      ;;
    ru)
	  COUNTRY="ru"
      SETLANG="ru"
      LOCALE="ru_RU"
      ;;
    sl)
	  COUNTRY="si"
      SETLANG="sl"
      LOCALE="sl_SI"
      ;;
    sk)
	  COUNTRY="sk"
      SETLANG="sk"
      LOCALE="sk_SK"
      ;;
    sv)
	  COUNTRY="se"
      SETLANG="sv"
      LOCALE="sv_SE"
      ;;
    uk)
	  COUNTRY="ua"
      SETLANG="uk"
      LOCALE="uk_UA"
      ;;
    vi)
	  COUNTRY="vn"
      SETLANG="vi"
      LOCALE="en_US"
      ;;
    zh_CN)
	  COUNTRY="cn"
      SETLANG="zh_CN"
      LOCALE="zh_CN"
      ;;
    zh_TW)
	  COUNTRY="tw"
      SETLANG="zh_TW"
      LOCALE="zh_TW"
      ;;
    *)
	  COUNTRY="C"
      SETLANG="${TARGETLANG}"
      LOCALE="en_US"
      ;;
  esac

  export COUNTRY SETLANG LOCALE

};

# Function which sets the timezone on the system
set_timezone()
{
  TZONE="$1"
  cp ${FSMNT}/usr/share/zoneinfo/${TZONE} ${FSMNT}/etc/localtime
};

# Function which enables / disables NTP
set_ntp()
{
  ENABLED="$1"
  if [ "$ENABLED" = "yes" -o "${ENABLED}" = "YES" ]
  then
    cat ${FSMNT}/etc/rc.conf 2>/dev/null | grep -q 'ntpd_enable="YES"' 2>/dev/null
    if [ $? -ne 0 ]
    then
      echo 'ntpd_enable="YES"' >>${FSMNT}/etc/rc.conf
      echo 'ntpd_sync_on_start="YES"' >>${FSMNT}/etc/rc.conf
    fi
  else
    cat ${FSMNT}/etc/rc.conf 2>/dev/null | grep -q 'ntpd_enable="YES"' 2>/dev/null
    if [ $? -ne 0 ]
    then
      sed -i.bak 's|ntpd_enable="YES"||g' ${FSMNT}/etc/rc.conf
    fi
  fi
};

# Starts checking for localization directives
run_localize()
{
  KEYLAYOUT="NONE"
  KEYMOD="NONE"
  KEYVAR="NONE"

  while read line
  do
    # Check if we need to do any localization
    echo $line | grep -q "^localizeLang=" 2>/dev/null
    if [ $? -eq 0 ]
    then

      # Set our country / lang / locale variables
      get_value_from_string "$line"
      localize_get_codes ${VAL}

      get_value_from_string "$line"
      # If we are doing PC-BSD install, localize it as well as FreeBSD base
      if [ "${INSTALLTYPE}" != "FreeBSD" ]
      then
        localize_pcbsd "$VAL"
      fi

      # Localize FreeBSD
      localize_freebsd "$VAL"

      # Localize any X pkgs
      localize_x_desktops "$VAL"
    fi

    # Check if we need to do any keylayouts
    echo $line | grep -q "^localizeKeyLayout=" 2>/dev/null
    if [ $? -eq 0 ] ; then
      get_value_from_string "$line"
      KEYLAYOUT="$VAL"
    fi

    # Check if we need to do any key models
    echo $line | grep -q "^localizeKeyModel=" 2>/dev/null
    if [ $? -eq 0 ] ; then
      get_value_from_string "$line"
      KEYMOD="$VAL"
    fi

    # Check if we need to do any key variant
    echo $line | grep -q "^localizeKeyVariant=" 2>/dev/null
    if [ $? -eq 0 ] ; then
      get_value_from_string "$line"
      KEYVAR="$VAL"
    fi


    # Check if we need to set a timezone
    echo $line | grep -q "^timeZone=" 2>/dev/null
    if [ $? -eq 0 ] ; then
      get_value_from_string "$line"
      set_timezone "$VAL"
    fi

    # Check if we need to set a timezone
    echo $line | grep -q "^enableNTP=" 2>/dev/null
    if [ $? -eq 0 ] ; then
      get_value_from_string "$line"
      set_ntp "$VAL"
    fi
  done <${CFGF}

  if [ "${INSTALLTYPE}" != "FreeBSD" ] ; then
    # Do our X keyboard localization
    localize_x_keyboard "${KEYMOD}" "${KEYLAYOUT}" "${KEYVAR}" "${COUNTRY}"
  fi

  # Check if we want to prunt any other KDE lang files to save some disk space
  get_value_from_cfg localizePrune
  if [ "${VAL}" = "yes" -o "${VAL}" = "YES" ] ; then
    localize_prune_langs
  fi

  # Update the login.conf db, even if we didn't localize, its a good idea to make sure its up2date
  run_chroot_cmd "/usr/bin/cap_mkdb /etc/login.conf" >/dev/null 2>/dev/null

};
