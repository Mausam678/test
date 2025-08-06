/*
 * xmodem.h
 *
 *  Created on: 09-Jan-2021
 *      Author: RnD02
 */

#ifndef SRC_XMODEM_H_
#define SRC_XMODEM_H_

int _inbyte(unsigned short timeout) ;
void flushinput(void);
int xmodemReceive(unsigned char *dest, int destsz);
int xmodemTransmit(unsigned char *src, int srcsz);

/* Transmit File From SD Card via XModem */
int xmodemTransmitFilefrmSD(const char *fname, int srcsz);

#endif /* SRC_XMODEM_H_ */
