/***************************************************************************

      Eeprom��дģ��-ʹ���ڲ���Flash˫ҳС�����ռ�ģ��-ȫRAM����ʱ��ʵ��

��ģ��ΪEeprom_FlashD.c�ļ򻯰棬ʹ����ȫRAM�����Թ���������⣬ʹ������Ϊ��
   1. �û�����Ҫ��EEPROM�ռ���һ������(��ҳʱһ����������)���ܸ㶨��
   2. ���������Flash����(��ҳʱһ����������)��ΪС������RAM���࣬�Ա�֤ȫ������ռ�õ�RAM��С

ע��Ĭ�ϰ�64k Flash������512byte��������, EEPROM����󣬵�ҳģʽ����EEPROM��СΪ512byte
****************************************************************************/

#include "Eeprom.h"
#include "Flash.h"              //Flash��׼�����ӿ�
#include <string.h>
//#include "IoCtrl.h"             //������WDT_Week();

//--------------------------��Ĭ��ֵ-------------------------------------
#define _HEADER_SIZE        8      //��������ͷ��С,�̶�Ϊ8�Ա�֤8�ֽڶ����У��

#ifndef EEPROM_WR_BACK_OV   //��дʱ��Ĭ�ϵ�λ256ms
  #define EEPROM_WR_BACK_OV  (15 * 4)
#endif

#ifndef EEPROM_BASE1                 //��һ��EEPROM��ҳ��ʼ��ַ
  #define EEPROM_BASE1  (0x10000 - 0x400)  
#endif

#ifndef EEPROM_BASE2                 //�ڶ���EEPROM��ҳ��ʼ��ַ
  //��ֵӦ����(EEPROM_BASE2 - EEPROM_BASE1) >= (EEPROM_SIZE + _HEADER_SIZE)
  #define EEPROM_BASE2  (0x10000 - 0x200)  
#endif

//FLASH��ҳ����,���ö������ҳ���һ��EEPROM_SIZE,��ֵ���㣺
//((EEPROM_SIZE + _HEADER_SIZE) / EEPROM_PAGE_COUNT) Ӧ���ö�ӦFLASH��һҳ
#ifndef EEPROM_PAGE_COUNT  
  #define EEPROM_PAGE_COUNT     1     //Ĭ��ֻ��һҳ
#endif

//ʵ��EEPROM��ҳ��С,EEPROM����,��Ϊ������С-_HEADER_SIZE(�ⲿ������-8)
#ifndef EEPROM_SIZE
  #define EEPROM_SIZE  (512 - _HEADER_SIZE)       
#endif

struct _Eeprom{ //������
  //ҳ����
  unsigned long Counter[2];   //ҳ������(�ڻ�����ǰ�Ա�֤8���ֶ���,�׸���Ч���ڶ���ȡ��)
  unsigned char Buf[EEPROM_SIZE];   //�û�����ȫ����,����ҳ������

  //�ڶ�ҳΪ�����ݣ�����Ϊ��һҳ
  unsigned char Page2New;
  //��д���ݶ�ʱ��,��ֵʱ��ʾ��������Ҫ����д��
  unsigned char WrBackTimer; 
  //Eeprom_pGEtWr()���ü����������ڷ�ֹ�û�Ƶ�����û�ÿ�дָ��ʱ����λ��д��ʱ���Ӷ����²��Զ���д
  unsigned char BufWrCount;
  //����ͷ�����־
  signed char HeaderErr;  
};

struct _Eeprom  Eeprom;

#define  _PAGE_SIZE   ((EEPROM_SIZE + _HEADER_SIZE) / EEPROM_PAGE_COUNT) //Ӧ����ΪFLASH��һҳ
/*******************************************************************************
                           ��ʼ�����ʽ������ع�������
********************************************************************************/
//----------------------��Flash�и��¼���������ҳ��־----------------------------
static void _UpdateCounterAndNew(void)
{
  unsigned char Page2New = 0;
  unsigned long Counter = *(unsigned long*)EEPROM_BASE1;
  unsigned long Counter2 = *(unsigned long*)EEPROM_BASE2;
  if(Counter == 0xffffffff){
    if(Counter2 == 0xffffffff)//оƬͷһ�α�ʹ��
      Counter = 0;   
    else{//�ڶ�ҳ������
      Counter = Counter2;
      Page2New = 1;//�ڶ�ҳ��
    }
  }
  else{//��ҳ��������,�Ƚϴ�С(��ûУ���Ƿ��ۼ���������)
    if((Counter2 != 0xffffffff) && (Counter < Counter2)){ //�ڵڶ�ҳ
      Counter = Counter2;
      Page2New = 1;
    }
  }
  //У������ͷ
  if(Page2New) Counter2 = *(unsigned long*)(EEPROM_BASE2 + 4);
  else Counter2 = *(unsigned long*)(EEPROM_BASE1 + 4);
  if(Counter2 != (0 - Counter)) //У�����
    Eeprom.HeaderErr = -1;
  else Eeprom.HeaderErr = 0; //��ȷ��
  
  Eeprom.Counter[0] = Counter;
  Eeprom.Counter[1] = 0 - Eeprom.Counter[0];//ȡ����д��
  Eeprom.Page2New = Page2New;
  
  //��󻺳�ȫ������������
  if(Page2New) 
    memcpy(Eeprom.Buf, (const char*)(EEPROM_BASE2 + _HEADER_SIZE), EEPROM_SIZE);
  else
    memcpy(Eeprom.Buf, (const char*)(EEPROM_BASE1 + _HEADER_SIZE), EEPROM_SIZE);
}

//--------------------������д���ӦFlash��----------------------------
static void _WrFlash(unsigned char IsPage2,
                     unsigned long OffFlashAdr, //��Ϊҳ�ڵ�flash��ַ
                     const void *pVoid,
                     EepromLen_t Len)
{
  //��þ���λ��
  if(IsPage2) OffFlashAdr += EEPROM_BASE2;
  else OffFlashAdr += EEPROM_BASE1;  
  #if EEPROM_PAGE_COUNT <= 1 //һҳ�����
    Flash_Write(OffFlashAdr, pVoid, Len);
  #else //���FLASHҳ���ʱ���ֱ���
    unsigned short CurLen = OffFlashAdr % _PAGE_SIZE; //ҳ��ƫ��
    if((CurLen + Len) <= _PAGE_SIZE){//��һҳ��д����
      Flash_Write(OffFlashAdr, pVoid, Len);
      return;
    }
    CurLen = _PAGE_SIZE - CurLen; //��һҳ��д������
    //��һҳ����д
    const unsigned char *pPos = (const unsigned char *)pVoid;
    Flash_Write(OffFlashAdr, pPos, CurLen);
    Len -= CurLen;
    pPos += CurLen;
    OffFlashAdr += CurLen;
    //�ڶ�ҳ����ҳд��
    for(; Len > _PAGE_SIZE; Len -= _PAGE_SIZE, 
                             pPos += _PAGE_SIZE, OffFlashAdr += _PAGE_SIZE){   
      Flash_Write(OffFlashAdr, pPos, _PAGE_SIZE);
    }
    //���һҳ����д
    Flash_Write(OffFlashAdr, pPos, Len);
  #endif
}

//-----------------------��ʽ����Ӧҳ----------------------------
static void _Format(unsigned char IsPage2) 
{
  unsigned long FlashBase;
  if(IsPage2) FlashBase = EEPROM_BASE2;
  else FlashBase = EEPROM_BASE1;
  
  #if EEPROM_PAGE_COUNT <= 1 //һҳ�����
    Flash_ErasePage(FlashBase);
  #else //���FLASHҳ���ʱ���ֱ��ʽ��
    for(unsigned char Count = EEPROM_PAGE_COUNT; Count > 0; Count--){
      Flash_ErasePage(FlashBase);
      FlashBase += _PAGE_SIZE;
    }
  #endif
}

//---------------------------��ʼ������---------------------------------
void Eeprom_Init(void)
{
  Eeprom.WrBackTimer = 0;                    //����Ҫ��д
  Eeprom.BufWrCount = 0;             //����������
  _UpdateCounterAndNew();               //���¼���������ҳ��־
}

//----------------------��ʽ��Eeprom����---------------------------------
void Eeprom_Format(void)
{

}

//-----------------------------���渴λ---------------------------------
//ȫ���ָ�Ϊ0xff
void Eeprom_BufReset(void)
{
	memset(Eeprom.Buf,0xff, EEPROM_SIZE);
}



/*******************************************************************************
                                д�������
********************************************************************************/
//------------------------------��д����������----------------------------------
static void _WrBack(void)
{
  //��λ��д���
  Eeprom.WrBackTimer = 0; 
  Eeprom.BufWrCount = 0;  
  
  //׼��1�����ҵ���д�����ҳ�����ϻ�ַ
  unsigned char CurNew;
  unsigned long OldFlashBase; 
  if(Eeprom.Page2New){
    OldFlashBase = EEPROM_BASE2;    
    CurNew = 0;//�����෴
  }
  else{
    OldFlashBase = EEPROM_BASE1;      
    CurNew = 1;
  }
  //׼��2��������ȷ�����������ֵ���µ�
  if(!memcmp((void*)(OldFlashBase + _HEADER_SIZE), Eeprom.Buf, EEPROM_SIZE))
    return; //û�и���
  
  //׼��3���ȸ�ʽ����д��ҳ��  
  _Format(CurNew);  
  
  //1д������
  unsigned long OffAdr = _HEADER_SIZE; //��ǰ��ַƫ��
  //�������������д��(_FlashToBuf()ʱ��ȷ��������)
  _WrFlash(CurNew, OffAdr, Eeprom.Buf, EEPROM_SIZE);
  OffAdr += EEPROM_SIZE;  //ƫ�Ƹ������´�
  
  //2.���¶�ʱ���ۼӲ�д��(δ���Ǽ������׻ػ���0���)
  Eeprom.Counter[0]++;
  Eeprom.Counter[1] = 0 - Eeprom.Counter[0];//ȡ����ΪУ����д��
  _WrFlash(CurNew, 0, Eeprom.Counter, _HEADER_SIZE);//д����ͷ
  //ע�������˶�����ͷд���У�飡
  
  //3.����л�����ҳ�����(ע: ��ʱ���������������Ȼ���µĲ���Ч)
  Eeprom.Page2New = CurNew;
}

//---------------------------дEeprom����---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
  //�����������ȫ��
  // (��)
  
  //��д��������
  memcpy(&Eeprom.Buf[Adr], pVoid, Len);
  
  //�����Զ���д
  Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; 
  
}
/*******************************************************************************
                                ���������
********************************************************************************/

//---------------------------��ȡEeprom����---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
  //�����������ȫ��
  // (��)
  
  memcpy(pVoid, &Eeprom.Buf[Adr], Len); //ֱ���ڻ���������
}

//-------------------------��Eeprom��ַת��Ϊֻ��ָ��------------------
//������ֻ�����ݣ������Խṹ��ʽ���������Ч��
const unsigned char *Eeprom_pGetRd(EepromAdr_t Adr, EepromLen_t Len)
{
  return  &Eeprom.Buf[Adr];   //ֱ���ڻ���������
}

//-------------------------��Eeprom��ַת��Ϊ��дָ��------------------
//���������ݵĶ�д����ֻ�������ģ�Ӧʹ��Eeprom_pGetRd()�Խ�ʡд�������Ч��
//�����Խṹ��ʽ���������Ч��
const unsigned char *Eeprom_pGetWr(EepromAdr_t Adr, EepromLen_t Len)
{
  return  &Eeprom.Buf[Adr];   //ֱ���ڻ���������
}

//-------------------------������---------------------------------
//����250ms������
void Eeprom_Task(void)
{
  //��ֹ�û�Ƶ������ʱ��λ��д���˻�������ǿ�ƻ�д
  if(Eeprom.BufWrCount == 255){
    _WrBack();
    return;
  }
  //�Զ���д����
  if(!Eeprom.WrBackTimer) return; //û�л�д����
  Eeprom.WrBackTimer--;
  if(Eeprom.WrBackTimer) return;  //�ȴ�������
  //ʱ�䵽ִ�л�д
  _WrBack();
}

//-----------------------�õ�д���������---------------------------------
//�д˹���ʱ�����أ����򷵻�Ϊ0
unsigned long Eeprom_GetWrCount(void)
{
  return Eeprom.Counter[0];
}

//--------------------------ǿ�ƻ�д����������---------------------------------
//��Ҫ���ݱ���ʱ(��ϵͳʵʼ����־��)���ɵ��ô˺���ǿ�ƻ�д
void Eeprom_ForceWrBuf(void)
{
  _WrBack();
}

//--------------------------------ǿ�ƻ�д������-------------------------------
//�ڼ�⵽�ػ�������Ҫ��λʱ����ô˺�����ֹ����δ�������ʧ
void Eeprom_ForceWrBufAndRestart(void)
{
  _WrBack();  
  while(1);
}

  


