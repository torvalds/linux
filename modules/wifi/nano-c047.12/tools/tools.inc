
iw: obj
	(cd $(OBJ_DIR); \
	cp -fv $(OBJ_DIR)/../tools/wireless_tools.28.tar.gz .;)
	(cd $(OBJ_DIR); tar xvzf wireless_tools.28.tar.gz)
	(cd $(OBJ_DIR)/wireless_tools.28; \
	sed -i 's%CC = gcc%CC = $(CC)%' Makefile; \
	sed -i 's%AR = ar%AR = $(AR)%' Makefile; \
	make HEADERS=-I$(KERNEL_DIR)/include BUILD_STATIC=y; \
	cp iwconfig iwlist iwpriv iwevent ifrename ../)
	rm $(OBJ_DIR)/wireless_tools.28.tar.gz
	rm -Rf $(OBJ_DIR)/wireless_tools.28

openssl: obj
	(cd $(OBJ_DIR); \
	cp -fv $(OBJ_DIR)/../tools/openssl-0.9.8e.tar.gz .;)
	(cd $(OBJ_DIR); tar xvzf openssl-0.9.8e.tar.gz)
	(cd $(OBJ_DIR)/openssl-0.9.8e; \
	./Configure no-asm linux-elf; \
	sed -i 's%CC= gcc%CC = $(CC)%' Makefile; \
	make; \
	cp libssl.a ../; \
	cp libcrypto.a ../)

wpa-supplicant-ssl: obj
	(cd $(OBJ_DIR); \
	cp -fv $(OBJ_DIR)/../tools/wpa_supplicant-0.5.7.tar.gz .;)
	(cd $(OBJ_DIR); tar xvzf wpa_supplicant-0.5.7.tar.gz)
	(cd $(OBJ_DIR)/wpa_supplicant-0.5.7; \
	echo CONFIG_DRIVER_HOSTAP=y > .config; \
        echo CONFIG_DRIVER_WEXT=y >> .config; \
        echo CONFIG_WIRELESS_EXTENSION=y >> .config; \
	echo CONFIG_IEEE8021X_EAPOL=y >> .config; \
        echo CONFIG_EAP_MD5=y >> .config; \
        echo CONFIG_EAP_MSCHAPV2=y >> .config; \
        echo CONFIG_EAP_TLS=y >> .config; \
        echo CONFIG_EAP_PEAP=y >> .config; \
        echo CONFIG_EAP_TTLS=y >> .config; \
        echo CONFIG_EAP_GTC=y >> .config; \
        echo CONFIG_EAP_OTP=y >> .config; \
        echo CONFIG_EAP_SIM=y >> .config; \
        echo CONFIG_EAP_AKA=y >> .config; \
        echo CONFIG_EAP_PSK=y >> .config; \
        echo CONFIG_EAP_PAX=y >> .config; \
        echo CONFIG_EAP_LEAP=y >> .config; \
        echo CONFIG_CTRL_IFACE=y >> .config; \
	echo CONFIG_DRIVER_WEXT=y >> .config; \
	echo CONFIG_WIRELESS_EXTENSION=y >> .config; \
	echo CONFIG_CTRL_IFACE=y >> .config; \
	echo CONFIG_BACKEND=file >> .config; \
	echo CONFIG_PKCS12=y >> .config; \
	echo CFLAGS += -I$(HOSTDRIVER_DIR)/linux/obj/openssl-0.9.8e/include >> .config; \
	echo LIBS += -L$(HOSTDRIVER_DIR)/linux/obj >> .config; \
	echo LDFLAGS += -L$(HOSTDRIVER_DIR)/linux/obj -ldl >> .config; \
	make; \
	cp wpa_cli ../wpa_cli-ssl; \
	cp wpa_supplicant ../wpa_supplicant-ssl)
	rm $(OBJ_DIR)/wpa_supplicant-0.5.7.tar.gz
	rm -Rf $(OBJ_DIR)/wpa_supplicant-0.5.7

wpa-supplicant: obj
	(cd $(OBJ_DIR); \
	cp -fv $(OBJ_DIR)/../tools/wpa_supplicant-0.5.7.tar.gz .;)
	(cd $(OBJ_DIR); tar xvzf wpa_supplicant-0.5.7.tar.gz)
	(cd $(OBJ_DIR)/wpa_supplicant-0.5.7; \
	echo CONFIG_DRIVER_HOSTAP=y > .config; \
	echo CONFIG_DRIVER_WEXT=y >> .config; \
	echo CONFIG_WIRELESS_EXTENSION=y >> .config; \
	echo CONFIG_CTRL_IFACE=y >> .config; \
	make; \
	cp wpa_cli wpa_supplicant ../)
	rm $(OBJ_DIR)/wpa_supplicant-0.5.7.tar.gz
	rm -Rf $(OBJ_DIR)/wpa_supplicant-0.5.7

nuttcp: obj
	(cd $(OBJ_DIR); \
	cp -fv $(OBJ_DIR)/../tools/nuttcp-5.5.5.c .; \
	$(CC) nuttcp-5.5.5.c -o nuttcp; \
	rm nuttcp-5.5.5.c)


ettcp: obj
	(cd $(OBJ_DIR); \
	cp -fv $(OBJ_DIR)/../tools/ettcp-1.0.tar.gz .; \
	tar xvzf ettcp-1.0.tar.gz)
	(cd $(OBJ_DIR)/ettcp-1.0; \
	sed -i 's%gcc%$(CC)%' Makefile; \
	make; \
	cp ettcp ../)
	rm $(OBJ_DIR)/ettcp-1.0.tar.gz
	rm -Rf $(OBJ_DIR)/ettcp-1.0

iperf: obj
	(cd $(OBJ_DIR); \
	cp -fv $(OBJ_DIR)/../tools/iperf-1.7.0-source.tar.gz .; \
	tar xvzf iperf-1.7.0-source.tar.gz)
	(cd $(OBJ_DIR)/iperf-1.7.0/cfg; \
	./configure --host=$(HOST);)
	(cd $(OBJ_DIR)/iperf-1.7.0; \
	make; \
	cp iperf ../)
	rm $(OBJ_DIR)/iperf-1.7.0-source.tar.gz
	rm -Rf $(OBJ_DIR)/iperf-1.7.0

socat: obj
	(cd $(OBJ_DIR); \
	cp -fv $(OBJ_DIR)/../tools/socat-1.5.0.0.tar.gz .; \
	tar xvzf socat-1.5.0.0.tar.gz)
	(cd $(OBJ_DIR)/socat-1.5.0.0; \
	./configure --host=$(HOST) --disable-termios LDFLAGS=-static; \
	make; \
	cp socat ../)
	rm $(OBJ_DIR)/socat-1.5.0.0.tar.gz
	rm -Rf $(OBJ_DIR)/socat-1.5.0.0

