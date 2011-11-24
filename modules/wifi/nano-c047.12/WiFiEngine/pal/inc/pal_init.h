//#define PAL
int pal_Init (void);
void pal_Exit (void);
void nano_To_Pal(int msg);
void eval_hci_cmd(char* HCI_Packet);


#define PAL_FAILURE_NOT_SUPPORTED          -8 /**< The operation is not supported */
#define PAL_FAILURE_ABORT                  -7 /**< The operation was aborted */
#define PAL_FAILURE_NOT_IMPLEMENTED        -6 /**< This function is not yet implemented */
#define PAL_FAILURE_DEFER                  -5 /**< Try again later */
#define PAL_FAILURE_INVALID_DATA           -4 /**< Data buffer contents was bad */
#define PAL_FAILURE_RESOURCES              -3 /**< Not enough resources */     
#define PAL_FAILURE_NOT_ACCEPTED           -2 /**< NIC op was tried but failed */
#define PAL_FAILURE_INVALID_LENGTH         -1 /**< Argument buffer size wrong */
#define PAL_FAILURE                         0
#define PAL_SUCCESS            		    1

void palTty_handle_tty_output(unsigned char* str, int len);
//void palTty_handle_tty_output(struct palTty_dev *priv, unsigned char* str, int len);






