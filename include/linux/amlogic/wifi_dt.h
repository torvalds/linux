#ifndef _wifi_dt_h_
#define _wifi_dt_h_

int wifi_setup_dt(void);
void wifi_teardown_dt(void);

void wifi_request_32k_clk(int is_on, const char *requestor);
void extern_wifi_set_enable(int is_on);

#endif /* _wifi_dt_h_ */
