# $FreeBSD$
# See https://wiki.freebsd.org/LocaleNewApproach
# Taken from FreeBSD svn [base]/user/edwin/locale/cldr
#
# needs:
# devel/p5-Tie-IxHash
#
# Modified by John Marino to suit DragonFly needs
#

.OBJDIR:	.

.if !defined(UNIDIR)
.error UNIDIR is not set
.endif
PASSON=		UNIDIR="${UNIDIR}"

ETCDIR=		${.CURDIR}/etc

KNOWN=		monetdef numericdef msgdef colldef ctypedef # timedef
TYPES?=		${KNOWN}
LOCALE_DESTDIR?=	/tmp/generated-locales/

COLLATION_SPECIAL?= \
	cs_CZ ISO8859-2 \
	da_DK ISO8859-1 \
	da_DK ISO8859-15 \
	hr_HR ISO8859-2 \
	hu_HU ISO8859-2 \
	nb_NO ISO8859-1 \
	nb_NO ISO8859-15 \
	sk_SK ISO8859-2 \
	sr_Latn_RS ISO8859-2 \
	sr_Cyrl_RS ISO8859-5 \
	zh_Hans_CN GB2312 \
	zh_Hans_CN eucCN \
	zh_Hant_TW Big5 \
	zh_Hans_CN GB18030 \
	zh_Hans_CN GBK \
	ja_JP eucJP \
	nn_NO ISO8859-15 \
	nn_NO ISO8859-1

.for area enc in ${COLLATION_SPECIAL}
COLLATIONS_SPECIAL_ENV+=	${area}.${enc}
.endfor
PASSON+=	COLLATIONS_SPECIAL="${COLLATIONS_SPECIAL_ENV}"

all:
.for t in ${TYPES}
.  if ${KNOWN:M${t}}
	test -d ${t} || mkdir ${t}
	make build-${t}
.  endif
.endfor
	@echo ""
	@find . -name *failed

.for t in ${TYPES}
install: install-${t}
install-${t}:
.  if ${KNOWN:M${t}}
	rm -rf ${.CURDIR}/${t}.draft
	rm -f ${.CURDIR}/../../../share/${t}/Makefile
	rm -f ${.CURDIR}/../../../share/${t}/*.src
	mv ${.CURDIR}/${t}/* ${.CURDIR}/../../../share/${t}/
.  endif
.endfor

post-install:
.for t in ${TYPES}
.  if ${KNOWN:M${t}}
	(cd ${.CURDIR}/../../../share/${t} && \
		make && make install && make clean)
.  endif
.endfor

.for t in ${TYPES}
gen-${t}:
	mkdir -p ${t} ${t}.draft
	perl -I tools tools/cldr2def.pl \
		--unidir=$$(realpath ${UNIDIR}) \
		--etc=$$(realpath ${ETCDIR}) \
		--type=${t}

build-${t}: gen-${t}
	env ${PASSON} tools/finalize ${t}
.endfor

gen-ctypedef:	ctype-rollup
static-colldef: gen-colldef
build-colldef:	static-colldef

static-colldef:
.for area enc in ${COLLATION_SPECIAL}
	awk -f tools/extract-colldef.awk ${UNIDIR}/posix/${area}.${enc}.src > \
	    colldef.draft/${area}.${enc}.src
.endfor

ctype-rollup:
	perl -I tools tools/utf8-rollup.pl --unidir=$$(realpath ${UNIDIR})

clean:
.for t in ${TYPES}
	rm -rf ${t} ${t}.draft
.endfor

BASE_LOCALES_OF_INTEREST?= \
	af_ZA am_ET ar_AE ar_EG ar_JO ar_MA ar_QA ar_SA \
	be_BY bg_BG ca_AD ca_ES ca_FR ca_IT \
	cs_CZ da_DK de_AT de_CH de_DE el_GR en_AU en_CA \
	en_GB en_HK en_IE en_NZ en_PH en_SG en_US en_ZA \
	es_AR es_CR es_ES es_MX et_EE eu_ES fi_FI fr_BE \
	fr_CA fr_CH fr_FR ga_IE he_IL hi_IN hr_HR hu_HU hy_AM \
	is_IS it_CH it_IT ja_JP ko_KR lt_LT lv_LV \
	nb_NO nl_BE nl_NL nn_NO pl_PL pt_BR pt_PT ro_RO \
	ru_RU se_FI se_NO sk_SK sl_SI sv_FI sv_SE tr_TR \
	uk_UA \
	kk_KZ mn_MN sr_Cyrl_RS sr_Latn_RS \
	zh_Hans_CN zh_Hant_HK zh_Hant_TW \
	bn_IN gu_IN or_IN ta_IN te_IN kn_IN ml_IN si_LK \
	th_TH lo_LA bo_IN my_MM pa_Guru_IN ka_GE chr_US \
	km_KH shi_Tfng_MA ii_CN vai_Vaii_LR vi_VN

ENCODINGS=	Big5 \
		CP1251 \
		CP866 \
		CP949 \
		eucCN \
		eucJP \
		eucKR \
		GB18030 \
		GB2312 \
		GBK \
		ISO8859-1 \
		ISO8859-13 \
		ISO8859-15 \
		ISO8859-2 \
		ISO8859-5 \
		ISO8859-7 \
		ISO8859-9 \
		KOI8-R \
		KOI8-U \
		SJIS \
		US-ASCII \
		UTF-8 \


POSIX:
.if exists (${UNIDIR}/tools/java/cldr.jar)
	mkdir -p ${UNIDIR}/posix
.  for area in ${BASE_LOCALES_OF_INTEREST}
.    if !exists(${UNIDIR}/posix/${area}.UTF-8.src)
	java -DCLDR_DIR=${UNIDIR:Q} -jar ${UNIDIR}/tools/java/cldr.jar \
		org.unicode.cldr.posix.GeneratePOSIX \
		-d ${UNIDIR}/posix -m ${area} -c UTF-8
.    endif
.  endfor
.  for area encoding in ${COLLATION_SPECIAL}
.    if !exists(${UNIDIR}/posix/${area}.${encoding}.src)
	java -DCLDR_DIR=${UNIDIR:Q} -jar ${UNIDIR}/tools/java/cldr.jar \
		org.unicode.cldr.posix.GeneratePOSIX \
		-d ${UNIDIR}/posix -m ${area} -c ${encoding}
.    endif
.  endfor
.  for enc in ${ENCODINGS}
.  if !exists(${UNIDIR}/posix/${enc}.cm)
	java -DCLDR_DIR=${UNIDIR:Q} -jar ${UNIDIR}/tools/java/cldr.jar \
		org.unicode.cldr.posix.GenerateCharmap \
		-d ${UNIDIR}/posix -c ${enc}
.  endif
.  endfor
.else
	@echo "Please install CLDR toolset for the desired release"
	@echo "It should go at ${UNIDIR}/tools"
.endif

clean-POSIX:
	rm -f ${UNIDIR}/posix/*
