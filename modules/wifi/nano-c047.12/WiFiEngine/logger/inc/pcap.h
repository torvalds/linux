#ifndef __pcap_h__
#define __pcap_h__

#if (DE_ENABLE_PCAPLOG > CFG_OFF)
void nrx_pcap_q(void* data, size_t len, uint16_t flags);
int nrx_pcap_write(void* data, size_t len, uint16_t flags);
int nrx_pcap_open(const char* path);
int nrx_pcap_close(void);
#else
#define nrx_pcap_q(_data, _len, _flags)
#define nrx_pcap_open(_path)
#define nrx_pcap_write(_data, _len, _flags)
#endif


#endif /* __pcap_h__ */

