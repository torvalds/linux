/*!
\file
\brief Набор данных FSM Аудиоустроство
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/
#ifndef FCM_SWITCHDEVICECLASS_H
#define FCM_SWITCHDEVICECLASS_H
/*!
\brief Вид устроства
*/
enum FSMSW_VidDevice {
    SkyNet = 1 ///< Устроства связи
};
/*!
\brief ПодВид устроства
*/
enum FSMSW_PodVidDevice {
    K1986BE1T = 1 ///< СЦК
};
/*!
\brief Род устроства
*/
enum FSMSW_RodDevice {
    BLE_nRFC_RS485_Ethernet = 1, ///< BLE_nRFC_RS485_Ethernet
};

enum FSMSWCommand {
    SetSettingSwitch = 1,
    AnsSetSettingSwitch = 2,
    GetSettingSwitch = 3,
    AnsGetSettingSwitch = 4,
};

struct FSM_SkyNetDevice
{
    char reg;
    unsigned short iddev;
    struct fsm_ethernet_dev* ethdev;
    // struct fsm_po06_setting po06set;
};

#endif /* FCM_SWITCHDEVICECLASS_H */
