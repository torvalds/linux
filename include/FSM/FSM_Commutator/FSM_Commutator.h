#ifndef FSM_Commutator
#define FSM_Commutator

struct FSM_P2P_abonent
{
    unsigned char reg;
    unsigned short idstream1;
    unsigned short idstream2;
    unsigned short idcon;
};
struct FSM_Conferenc_abonent
{
    unsigned char reg;
    unsigned short idstream[FSM_Conferenc_num];
    unsigned short idcon;
};
struct FSM_Circular_abonent
{
    unsigned char reg;
    unsigned short idorg;
    unsigned short idstream[FSM_Circular_num];
    unsigned short idcon;
};
enum FSMComType { p2p, Conferenc, Circular };
unsigned short FSM_P2P_Connect(unsigned short id1, unsigned short id2);
void FSM_P2P_Disconnect(unsigned short idcon);
void FSM_Commutator_Process(char* data, short len);

#endif