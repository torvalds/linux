#ifndef UBLK_DEP_H
#define UBLK_DEP_H

#ifndef UBLK_U_IO_REGISTER_IO_BUF
#define	UBLK_U_IO_REGISTER_IO_BUF	\
	_IOWR('u', 0x23, struct ublksrv_io_cmd)
#define	UBLK_U_IO_UNREGISTER_IO_BUF	\
	_IOWR('u', 0x24, struct ublksrv_io_cmd)
#endif

#ifndef UBLK_F_USER_RECOVERY_FAIL_IO
#define UBLK_F_USER_RECOVERY_FAIL_IO (1ULL << 9)
#endif

#ifndef UBLK_F_ZONED
#define UBLK_F_ZONED (1ULL << 8)
#endif
#endif
