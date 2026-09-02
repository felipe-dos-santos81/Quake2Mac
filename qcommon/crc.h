/* crc.h */

void CRC_Init(unsigned short *crcvalue);
unsigned short CRC_Block (byte *start, int count);
