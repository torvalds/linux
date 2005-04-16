#ifndef _ACI_H_
#define _ACI_H_

extern int aci_port;
extern int aci_version;		/* ACI firmware version	*/
extern int aci_rw_cmd(int write1, int write2, int write3);

#define aci_indexed_cmd(a, b) aci_rw_cmd(a, b, -1)
#define aci_write_cmd(a, b)   aci_rw_cmd(a, b, -1)
#define aci_read_cmd(a)       aci_rw_cmd(a,-1, -1)

#define COMMAND_REGISTER    (aci_port)      /* write register */
#define STATUS_REGISTER     (aci_port + 1)  /* read register */
#define BUSY_REGISTER       (aci_port + 2)  /* also used for rds */

#define RDS_REGISTER        BUSY_REGISTER

#define ACI_SET_MUTE          0x0d
#define ACI_SET_POWERAMP      0x0f
#define ACI_SET_TUNERMUTE     0xa3
#define ACI_SET_TUNERMONO     0xa4
#define ACI_SET_IDE           0xd0
#define ACI_SET_WSS           0xd1
#define ACI_SET_SOLOMODE      0xd2
#define ACI_WRITE_IGAIN       0x03
#define ACI_WRITE_TUNE        0xa7
#define ACI_READ_TUNERSTEREO  0xa8
#define ACI_READ_TUNERSTATION 0xa9
#define ACI_READ_VERSION      0xf1
#define ACI_READ_IDCODE       0xf2
#define ACI_INIT              0xff
#define ACI_STATUS            0xf0
#define     ACI_S_GENERAL     0x00
#define     ACI_S_READ_IGAIN  0x21
#define ACI_ERROR_OP          0xdf

/*
 * The following macro SCALE can be used to scale one integer volume
 * value into another one using only integer arithmetic. If the input
 * value x is in the range 0 <= x <= xmax, then the result will be in
 * the range 0 <= SCALE(xmax,ymax,x) <= ymax.
 *
 * This macro has for all xmax, ymax > 0 and all 0 <= x <= xmax the
 * following nice properties:
 *
 * - SCALE(xmax,ymax,xmax) = ymax
 * - SCALE(xmax,ymax,0) = 0
 * - SCALE(xmax,ymax,SCALE(ymax,xmax,SCALE(xmax,ymax,x))) = SCALE(xmax,ymax,x)
 *
 * In addition, the rounding error is minimal and nicely distributed.
 * The proofs are left as an exercise to the reader.
 */

#define SCALE(xmax,ymax,x) (((x)*(ymax)+(xmax)/2)/(xmax))


#endif  /* _ACI_H_ */
