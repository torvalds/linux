#ifndef FSMEthernetHeader
#define FSMEthernetHeader

#define FSM_PROTO_ID 0x1996
#define FSM_PROTO_ID_R 0x9619

enum FSMNetwork_VidDevice
{
    Ethernet=1 ///< Ethernet
};
/*!
\brief ПодВид устроства
*/
enum FSMNetwork_PodVidDevice
{
   WireEthernet=1 ///< WireEthernet
};
/*!
\brief Род устроства
*/
enum FSMNetwork_RodDevice
{
    StandartEthernet=1///< StandartEthernet
};

struct fsm_ethernet_dev
{
    char reg;
    unsigned short id;
    unsigned short numdev;
    char destmac[6];
    struct net_device *dev;
};
unsigned int FSM_Send_Ethernet_Package(void * data, int len, struct fsm_ethernet_dev *fsmdev);
unsigned int FSM_Send_Ethernet_Package2(void * data, int len, int id);
void FSM_RegisterAudioStreamCallback(FSM_StreamProcessSend FSM_ASC);
FSM_ADSendEthPack FSM_GetAudioStreamCallback(void);
struct fsm_ethernet_dev*  FSM_FindEthernetDevice(unsigned short id);
#endif