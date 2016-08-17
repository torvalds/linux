# Remove default preprocessor define's from config.h
#   PACKAGE
#   PACKAGE_BUGREPORT
#   PACKAGE_NAME
#   PACKAGE_STRING
#   PACKAGE_TARNAME
#   PACKAGE_VERSION
#   STDC_HEADERS
#   VERSION

BEGIN { RS = "" ; FS = "\n" }     \
	!/.#define PACKAGE./ &&   \
	!/.#define VERSION./ &&   \
	!/.#define STDC_HEADERS./ \
	{ print $0"\n" }
