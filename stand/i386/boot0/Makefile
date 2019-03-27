# $FreeBSD$

PROG?=	boot0
STRIP=
BINMODE=${NOBINMODE}
SRCS=	${PROG}.S

# Additional options that you can specify with make OPTS="..."
# (these only apply to boot0.S)
#
#	-DVOLUME_SERIAL	support volume serial number (NT, XP, Vista)
#	-DSIO		do I/O using COM1:
#	-DPXE		fallback to INT18/PXE with F6
#	-DCHECK_DRIVE	enable checking drive number
#	-DONLY_F_KEYS	accept only Fx keys in console
#	-DTEST		print drive number on entry
#
OPTS ?= -DVOLUME_SERIAL -DPXE
CFLAGS += ${OPTS}

# Flags used in the boot0.S code:
#   0x0f	all valid partitions enabled.
#   0x80	'packet', use BIOS EDD (LBA) extensions instead of CHS
#		to read from disk. boot0.S does not check that the extensions
#		are supported, but all modern BIOSes should have them.
#   0x40	'noupdate', disable writing boot0 back to disk so that
#		the current selection is not preserved across reboots.
#   0x20	'setdrv', override the drive number supplied by the bios
#		with the one in the boot sector.

# Default boot flags:
BOOT_BOOT0_FLAGS?=	0x8f

# The number of timer ticks to wait for a keypress before assuming the default
# selection.  Since there are 18.2 ticks per second, the default value of
# 0xb6 (182d) corresponds to 10 seconds.
BOOT_BOOT0_TICKS?=	0xb6

# The base address that we the boot0 code to to run it.  Don't change this
# unless you are glutton for punishment.
BOOT_BOOT0_ORG?=	0x600
ORG=${BOOT_BOOT0_ORG}

# Comm settings for boot0sio.
# Bit(s) Description
# 7-5    data rate (110,150,300,600,1200,2400,4800,9600 bps)
# 4-3    parity (00 or 10 = none, 01 = odd, 11 = even)
# 2      stop bits (set = 2, clear = 1)
# 1-0    data bits (00 = 5, 01 = 6, 10 = 7, 11 = 8)
.if !defined(BOOT_BOOT0_COMCONSOLE_SPEED)
BOOT_COMCONSOLE_SPEED?=	9600
.if ${BOOT_COMCONSOLE_SPEED} == 9600
BOOT_BOOT0_COMCONSOLE_SPEED=	"7 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 4800
BOOT_BOOT0_COMCONSOLE_SPEED=	"6 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 2400
BOOT_BOOT0_COMCONSOLE_SPEED=	"5 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 1200
BOOT_BOOT0_COMCONSOLE_SPEED=	"4 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 600
BOOT_BOOT0_COMCONSOLE_SPEED=	"3 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 300
BOOT_BOOT0_COMCONSOLE_SPEED=	"2 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 150
BOOT_BOOT0_COMCONSOLE_SPEED=	"1 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 110
BOOT_BOOT0_COMCONSOLE_SPEED=	"0 << 5 + 3"
.else
BOOT_BOOT0_COMCONSOLE_SPEED=	"7 << 5 + 3"
.endif
.endif

CFLAGS+=-DFLAGS=${BOOT_BOOT0_FLAGS} \
	-DTICKS=${BOOT_BOOT0_TICKS} \
	-DCOMSPEED=${BOOT_BOOT0_COMCONSOLE_SPEED}

LDFLAGS+=${LDFLAGS_BIN}

.include <bsd.prog.mk>
