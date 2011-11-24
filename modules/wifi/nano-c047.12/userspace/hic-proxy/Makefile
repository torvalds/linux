all: hic-proxy

hic-proxy: hic_proxy.o host_flash.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/hic-proxy host_flash.o hic_proxy.o
clean:
	rm -f *.o hic-proxy

