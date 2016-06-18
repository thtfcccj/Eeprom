/***************************************************************************

         EERPOM存取位置标准化接口-在PIC内部EEPROM中的实现

此接口为具体项目: 所有存取位置的,提供统一接口。实现了调用层与移动应用的分离。
***************************************************************************/

#include <pic.h>
#include "PicBit.h"

#include "Eeprom.h"

//---------------------------读取Eeprom数据---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
    unsigned char *pData;
    pData = (unsigned char*)pVoid;
    for(;Len > 0; Len-- ,Adr++, pData++)
    {
        _CLI();                  //保证此次读完成
        EEADRL = Adr;
        EECON1 &= ~PICB_CFGS;       //Deselect Config space and 
        EECON1 &= ~PICB_EEPGD;      //Point to DATA memory
        EECON1 |= PICB_RD;          //EE Read
        *pData = EEDATL;
        _SEI();
    }    
}

//---------------------------写入Eeprom数据---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
    unsigned char *pData;
    pData = (unsigned char*)(pVoid);
    for(;Len > 0; Len--, Adr++, pData++)
    {
        EEADRL = Adr;
        EEDATL = *pData;
        _CLI();                  //保证此次读完成
        EECON1 &= ~PICB_CFGS;       //Deselect Config space and 
        EECON1 &= ~PICB_EEPGD;      //Point to DATA memory
        EECON1 |= PICB_WREN; //开始写
        EECON2 = 0x55;
        EECON2 = 0xaa;
        EECON1 |= PICB_WR;   //启动写序列
        EECON1 &= ~PICB_WREN; //结束写,但写序列仍在继续
        _SEI();
        while(EECON1 & PICB_WR){}; //等待写结束
    }    
}




