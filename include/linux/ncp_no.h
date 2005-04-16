#ifndef _NCP_NO
#define _NCP_NO

/* these define the attribute byte as seen by NCP */
#define aRONLY			(__constant_cpu_to_le32(1))
#define aHIDDEN			(__constant_cpu_to_le32(2))
#define aSYSTEM			(__constant_cpu_to_le32(4))
#define aEXECUTE		(__constant_cpu_to_le32(8))
#define aDIR			(__constant_cpu_to_le32(0x10))
#define aARCH			(__constant_cpu_to_le32(0x20))
#define aSHARED			(__constant_cpu_to_le32(0x80))
#define aDONTSUBALLOCATE	(__constant_cpu_to_le32(1L<<11))
#define aTRANSACTIONAL		(__constant_cpu_to_le32(1L<<12))
#define aPURGE			(__constant_cpu_to_le32(1L<<16))
#define aRENAMEINHIBIT		(__constant_cpu_to_le32(1L<<17))
#define aDELETEINHIBIT		(__constant_cpu_to_le32(1L<<18))
#define aDONTCOMPRESS		(__constant_cpu_to_le32(1L<<27))

#endif /* _NCP_NO */
