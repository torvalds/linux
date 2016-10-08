#ifndef FSM_SA_H
#define FSM_SA_H

enum FSMSA_VidDevice {
    FSMSA_Analiz = 1, ///<  Модуль Конфигурации
};
/*!
\brief ПодВид устроства
*/
enum FSMSA_PodVidDevice {
    FSMSA_AnalizData = 1 ///< ComputerStatistic
};
/*!
\brief Род устроства
*/
enum FSMSA_RodDevice {
    FSMSA_AnalizDataServer = 1, ///< PCx86
};

struct fsm_sa_setting
{
    int dt;
};
struct FSM_SADevice
{
    char reg;
    unsigned short iddev;
    int idstream;
    int idcon;
    struct fsm_ethernet_dev* ethdev;
    struct fsm_sa_setting saset;
};
enum FSMSACommand /*0*****125*/
{ FSMSA_IDK = 1,
  FSMSA_ODK = 2,
};
#endif