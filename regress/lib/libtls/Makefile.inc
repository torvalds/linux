# Use this variable when the test needs internal symbols from libtls
TLS_INT=  -Wl,-Bstatic -ltls -Wl,-Bdynamic
