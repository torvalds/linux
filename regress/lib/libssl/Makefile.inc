# Use this variable when the test needs internal symbols from libcrypto
CRYPTO_INT=  -Wl,-Bstatic -lcrypto -Wl,-Bdynamic
# Use this variable when the test needs internal symbols from libssl
SSL_INT=  -Wl,-Bstatic -lssl -Wl,-Bdynamic
