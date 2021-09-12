/***************************************************************************

                          Eeprom��дģ���׼���ӿ�

****************************************************************************/
#ifndef _EEPROM_H
#define _EEPROM_H
#ifdef SUPPORT_EX_PREINCLUDE//��֧��Preinlude�r
  #include "Preinclude.h"
#endif

//��д��ʵ��(���ַ,E2��ַ,��д����),uiaddr_e2�����Ϳɸ������ȷ��
#ifndef EepromAdr_t
  #define EepromAdr_t  unsigned char
#endif

#ifndef EepromLen_t
  #define EepromLen_t  unsigned char
#endif

/***************************************************************************
                          ����ӿڲ���
***************************************************************************/

//-----------------------------��ʼ������---------------------------------
void Eeprom_Init(void);

//---------------------------��ȡEeprom����---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len);

//---------------------------дEeprom����---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len);

//--------------------------Eepromд��������--------------------------
//�˺���Ϊ���ӹ���, ��������ʵ��
void Eeprom_WrConst(EepromAdr_t Adr,   //Eerpom�еĴ�ȡλ��
                  unsigned char Data,  //��������
                  EepromLen_t Len);    //д�볤��

#endif
