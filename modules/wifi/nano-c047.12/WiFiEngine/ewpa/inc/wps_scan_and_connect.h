
void wps_abort(void* wps_priv, int reason);

typedef void (*wps_complete_cb_t)(int status);

void* wps_scan_and_connect(
      const char *ssid,
      char *pin,
      size_t pin_len,
      wps_complete_cb_t complete_cb,
      int timeout);

#define WPS_CODE_USER_ABORT        1
#define WPS_CODE_TIMEOUT           2
#define WPS_CODE_INTERNAL_ERROR    3
#define WPS_CODE_INTERNAL_FAILURE  4
#define WPS_CODE_MULTIPLE_NETS     5
#define WPS_CODE_FAILURE           9
#define WPS_CODE_SUCCESS          10

void wps_set_credentials_tlvs(unsigned char *buf,size_t len);
void wps_reset_credentials(void);
int wps_get_auth_mode(int *auth_type);
int wps_get_encr_type(int *encr_type);
int wps_get_ssid(char *dst, size_t size);
int wps_get_ssid_as_string(char *dst, size_t size);
int wps_get_key(char *dst, size_t size);
int wps_get_wep_key_index(int *index);

