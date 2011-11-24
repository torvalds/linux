/**** AUTO GENERATED ****/

#ifndef REGISTRY_ACCESS_H
#define REGISTRY_ACCESS_H

/*****************************************************************************
C O N S T A N T S / M A C R O S
*****************************************************************************/
#define Registry_VERSION_ID 0xFA220C46L

typedef enum            /* Chache actions. */
{
   CACHE_ACTION_READ,   /* Read data from persistent storage. */
   CACHE_ACTION_WRITE   /* Write data to persistent storage. */
} Action_t;

typedef enum            /* Enumeration used as identification for a property. */
{
   ID_version /*[rRegistry]*/,
   ID_general /*[rRegistry]*/,
   ID_network /*[rRegistry]*/,
   ID_basic /*[rNetworkProperties]*/,
   ID_connectionPolicy /*[rBasicWiFiProperties]*/,
   ID_passiveScanTimeouts /*[rConnectionPolicy]*/,
   ID_activeScanTimeouts /*[rConnectionPolicy]*/,
   ID_connectedScanTimeouts /*[rConnectionPolicy]*/,
   ID_QoSInfoElements /*[rBasicWiFiProperties]*/,
   ID_linkSupervision /*[rBasicWiFiProperties]*/,
   ID_scanPolicy /*[rBasicWiFiProperties]*/,
   ID_ibssBeacon /*[rNetworkProperties]*/,
   ID_powerManagement /*[rRegistry]*/,
   ID_hostDriver /*[rRegistry]*/
} PropertyId_t;

/*****************************************************************************
G L O B A L   D A T A T Y P E S
*****************************************************************************/
typedef struct 
{
   char* buffer;
   char* ptr;
} PersistentStorage_t;


/*****************************************************************************
G L O B A L   C O N S T A N T S / V A R I A B L E S
*****************************************************************************/
extern rRegistry registry;


/*****************************************************************************
G L O B A L   F U N C T I O N S
*****************************************************************************/
bool_t   Registry_VerifyVersion(rVersionId version);
void*    Registry_GetProperty(PropertyId_t id);


/*****************************************************************************
G L O B A L   A C C E S S   F U N C T I O N S
*****************************************************************************/
void Cache_int(PersistentStorage_t* storage, Action_t action, int* object_p, char* name);
void Cache_uint(PersistentStorage_t* storage, Action_t action, unsigned int* object_p, char* name);
void Cache_rBool(PersistentStorage_t* storage, Action_t action, rBool* object_p, char* name);
void Cache_rVersionId(PersistentStorage_t* storage, Action_t action, rVersionId* object_p, char* name);
void Cache_rTimeout(PersistentStorage_t* storage, Action_t action, rTimeout* object_p, char* name);
void Cache_rInfo(PersistentStorage_t* storage, Action_t action, rInfo* object_p, char* name);
void Cache_rSSID(PersistentStorage_t* storage, Action_t action, rSSID* object_p, char* name);
void Cache_rBSSID(PersistentStorage_t* storage, Action_t action, rBSSID* object_p, char* name);
void Cache_rChannelList(PersistentStorage_t* storage, Action_t action, rChannelList* object_p, char* name);
void Cache_rSupportedRates(PersistentStorage_t* storage, Action_t action, rSupportedRates* object_p, char* name);
void Cache_rExtSupportedRates(PersistentStorage_t* storage, Action_t action, rExtSupportedRates* object_p, char* name);
void Cache_rInterval(PersistentStorage_t* storage, Action_t action, rInterval* object_p, char* name);
void Cache_rATIMSet(PersistentStorage_t* storage, Action_t action, rATIMSet* object_p, char* name);
void Cache_rChannelSet(PersistentStorage_t* storage, Action_t action, rChannelSet* object_p, char* name);
void Cache_rPowerSaveMode(PersistentStorage_t* storage, Action_t action, rPowerSaveMode* object_p, char* name);
void Cache_rBSS_Type(PersistentStorage_t* storage, Action_t action, rBSS_Type* object_p, char* name);
void Cache_rSTA_WMMSupport(PersistentStorage_t* storage, Action_t action, rSTA_WMMSupport* object_p, char* name);
void Cache_rScanTimeouts(PersistentStorage_t* storage, Action_t action, rScanTimeouts* object_p, char* name);
void Cache_rScanPolicy(PersistentStorage_t* storage, Action_t action, rScanPolicy* object_p, char* name);
void Cache_rQoSInfoElements(PersistentStorage_t* storage, Action_t action, rQoSInfoElements* object_p, char* name);
void Cache_rConnectionPolicy(PersistentStorage_t* storage, Action_t action, rConnectionPolicy* object_p, char* name);
void Cache_rGeneralWiFiProperties(PersistentStorage_t* storage, Action_t action, rGeneralWiFiProperties* object_p, char* name);
void Cache_rBasicWiFiProperties(PersistentStorage_t* storage, Action_t action, rBasicWiFiProperties* object_p, char* name);
void Cache_rIBSSBeaconProperties(PersistentStorage_t* storage, Action_t action, rIBSSBeaconProperties* object_p, char* name);
void Cache_rNetworkProperties(PersistentStorage_t* storage, Action_t action, rNetworkProperties* object_p, char* name);
void Cache_rPowerManagementProperties(PersistentStorage_t* storage, Action_t action, rPowerManagementProperties* object_p, char* name);
void Cache_rHostDriverProperties(PersistentStorage_t* storage, Action_t action, rHostDriverProperties* object_p, char* name);
void Cache_rRegistry(PersistentStorage_t* storage, Action_t action, rRegistry* object_p, char* name);

#endif /* REGISTRY_ACCESS_H */
