#	From: @(#)Makefile	8.1 (Berkeley) 6/8/93
# $FreeBSD$

.include <src.opts.mk>

.PATH:	${.CURDIR}/../../sys/dev/usb

FILES=	ascii birthtoken bsd-family-tree committers-doc.dot committers-ports.dot \
	committers-src.dot flowers init.ee \
	iso3166 iso639 latin1 mdoc.template operator pci_vendors \
	scsi_modes \
	organization.dot

FILESDIR=	${BINDIR}/misc

.if ${MK_USB} != "no"
FILES+=	usb_hid_usages
FILES+=	usbdevs
.endif

.include <bsd.prog.mk>
