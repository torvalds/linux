#ifndef _XFS_CKSUM_H
#define _XFS_CKSUM_H 1

#define XFS_CRC_SEED	(~(__uint32_t)0)

/*
 * Calculate the intermediate checksum for a buffer that has the CRC field
 * inside it.  The offset of the 32bit crc fields is passed as the
 * cksum_offset parameter.
 */
static inline __uint32_t
xfs_start_cksum(char *buffer, size_t length, unsigned long cksum_offset)
{
	__uint32_t zero = 0;
	__uint32_t crc;

	/* Calculate CRC up to the checksum. */
	crc = crc32c(XFS_CRC_SEED, buffer, cksum_offset);

	/* Skip checksum field */
	crc = crc32c(crc, &zero, sizeof(__u32));

	/* Calculate the rest of the CRC. */
	return crc32c(crc, &buffer[cksum_offset + sizeof(__be32)],
		      length - (cksum_offset + sizeof(__be32)));
}

/*
 * Convert the intermediate checksum to the final ondisk format.
 *
 * The CRC32c calculation uses LE format even on BE machines, but returns the
 * result in host endian format. Hence we need to byte swap it back to LE format
 * so that it is consistent on disk.
 */
static inline __le32
xfs_end_cksum(__uint32_t crc)
{
	return ~cpu_to_le32(crc);
}

/*
 * Helper to generate the checksum for a buffer.
 */
static inline void
xfs_update_cksum(char *buffer, size_t length, unsigned long cksum_offset)
{
	__uint32_t crc = xfs_start_cksum(buffer, length, cksum_offset);

	*(__le32 *)(buffer + cksum_offset) = xfs_end_cksum(crc);
}

/*
 * Helper to verify the checksum for a buffer.
 */
static inline int
xfs_verify_cksum(char *buffer, size_t length, unsigned long cksum_offset)
{
	__uint32_t crc = xfs_start_cksum(buffer, length, cksum_offset);

	return *(__le32 *)(buffer + cksum_offset) == xfs_end_cksum(crc);
}

#endif /* _XFS_CKSUM_H */
