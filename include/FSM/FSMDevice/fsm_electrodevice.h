#ifndef FSM_ELECTRODEVICE
#define FSM_ELECTRODEVICE
/*!
\brief Вид устроства
*/
enum FSMELS_VidDevice {
    C220V_12VEl = 1 ///< 220в электрощиток
};
/*!
\brief ПодВид устроства
*/
enum FSMELS_PodVidDevice {
    C220V_12VEl_rev1 = 1 ///< 220в электрощиток
};
/*!
\brief Род устроства
*/
enum FSMELS_RodDevice {
    C220V_12VEl_rev1v1 = 1, ///< 220в электрощиток
};
#endif // FSM_ELECTRODEVICE