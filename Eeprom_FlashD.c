/***************************************************************************

         Eeprom��дģ��-ʹ���ڲ���Flash˫ҳ������ռ�ģ��-����ҳ��ʱʵ��
��ģ�������Ӳ��,ʹ�ñ�׼Flash�ӿ�ʵ�֣��������ڲ�ͬMCU��FLASH(���ⲿFLASH)
ʹ��˵��:
1. ��ȫ�ֺ궨����ʼ��ַEEPROM_BASE1/2,��ΪEEPROM��ҳ��СEEPROM_SIZE,�Լ�
   �����СEEPROM_BUF_SIZE
2. ���ֳ�ʼ����־Ϊ�״�ʹ��FLASH�����ʱ����ͨ��Eeprom_Format()��EEPROM����
3. ��ģ��ϵͳ����:
   (1. Eeprom_init()�������EEPROMǰ�ĳ�ʼ����
   (2. Eeprom_Task()����ϵͳ256ms������
   (3. ʵ��WDT_Week()
���ڲ�����˵��:
����˫Flash��д����+�������򽻲�д��,����Ϊ:
//  ��ÿ��������ʼ����һ��>=4Byte����Ϣͷ�洢�ۼ�����û��������У�飩
//  ������һ���������ݻ��ƣ��Լ���д�����,����ʱ��ͨ����ѯ���п�ȷ�����ݵ���ȷ��
//  ����������Ҫд��ʱ,��������Ϊ:
//    ��1. ���ۼ���С��Flash������
//    ��2. ���ۼ������Flash������+��ϱ��޸ĵ�����һ�𣬷���д����һ����Flash����
//    ��3. д����ɣ�У�������д���µ���Ϣͷ(�ۼ���+1)��ʾ�ɹ���
****************************************************************************/

#include "Eeprom.h"
#include "Flash.h"              //Flash��׼�����ӿ�
#include <string.h>
//#include "IoCtrl.h"             //������WDT_Week();

//--------------------------��Ĭ��ֵ-------------------------------------
//Ĭ�ϰ�STM32F4x 1M������128K��������
#ifndef EEPROM_WR_BACK_OV   //��дʱ��Ĭ�ϵ�λ256ms
  #define EEPROM_WR_BACK_OV  (10 * 4)
#endif

#ifndef EEPROM_BASE1                 //��һ��EEPROM��ҳ��ʼ��ַ
  #define EEPROM_BASE1  0x08020000  
#endif

#ifndef EEPROM_BASE2                 //�ڶ���EEPROM��ҳ��ʼ��ַ
  #define EEPROM_BASE2  0x08040000  
#endif

#ifndef EEPROM_SIZE
  #define EEPROM_SIZE  0x00020000  //ʵ��EEPROM��ҳ��С,��Ϊ8�ı���
#endif

#ifndef EEPROM_BUF_SIZE
  #define EEPROM_BUF_SIZE  10240    //��������ݴ�С,<=EEPROM_SIZE
#endif

#define _HEADER_SIZE        8      //��������ͷ��С,�̶�Ϊ8�Ա�֤8�ֽڶ����У��

struct _Eeprom{ //������
  //ҳ����
  EepromAdr_t BufBase;   //�������ݵ�EEPROM��ַ(����_HEADER_SIZE,-1��ʾ�޻���)
  unsigned long Counter[2];   //ҳ������(�ڻ�����ǰ�Ա�֤8���ֶ���,�׸���Ч���ڶ���ȡ��)
  unsigned char Buf[EEPROM_BUF_SIZE];   //�û����ݻ���

  //�ڶ�ҳΪ�����ݣ�����Ϊ��һҳ
  unsigned char Page2New;
  //��д���ݶ�ʱ��,��ֵʱ��ʾ��������Ҫ����д��
  unsigned char WrBackTimer; 
  //Eeprom_pGEtWr()���ü����������ڷ�ֹ�û�Ƶ�����û�ÿ�дָ��ʱ����λ��д��ʱ���Ӷ����²��Զ���д
  unsigned char BufWrCount;
  //����ͷ�����־
  signed char HeaderErr;  
};

struct _Eeprom  _Eeprom;

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
    if(Counter < Counter2){ //�ڵڶ�ҳ
      Counter = Counter2;
      Page2New = 1;
    }
  }
  //У������ͷ
  if(Page2New) Counter2 = *(unsigned long*)(EEPROM_BASE2 + 4);
  else Counter2 = *(unsigned long*)(EEPROM_BASE1 + 4);
  if(Counter2 != (0 - Counter)) //У�����
    _Eeprom.HeaderErr = -1;
  else _Eeprom.HeaderErr = 0; //��ȷ��
  
  _Eeprom.Counter[0] = Counter;
  _Eeprom.Counter[1] = 0 - _Eeprom.Counter[0];//ȡ����д��
  _Eeprom.Page2New = Page2New;
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
  
  Flash_Write(OffFlashAdr, pVoid, Len);
}

//-----------------------��ʽ����Ӧҳ----------------------------
static void _Format(unsigned char IsPage2) 
{
  unsigned long FlashBase;
  if(IsPage2) FlashBase = EEPROM_BASE2;
  else FlashBase = EEPROM_BASE1;
  Flash_ErasePage(FlashBase);
}

//---------------------------��ʼ������---------------------------------
void Eeprom_Init(void)
{
  _Eeprom.BufBase = (unsigned long)-1;//û�л���
  _Eeprom.WrBackTimer = 0;                    //����Ҫ��д
  _Eeprom.BufWrCount = 0;             //����������
  _UpdateCounterAndNew();               //���¼���������ҳ��־
}

//----------------------��ʽ��Eeprom����---------------------------------
void Eeprom_Format(void)
{
  /*_Format(0);
  _Format(1); 
  _Eeprom.BufBase = (unsigned long)-1;//�û�����ʧЧ
  _Eeprom.WrBackTimer = 0;//����Ҫ��д
  _Eeprom.BufWrCount = 0; //���¿�ʼ  
  
  //����ǰ����ֵд���Ӧҳ��ͳ��Flash��д�����
  _WrFlash(_Eeprom.Page2New, 0, _Eeprom.Counter, 8);*/
}

/*******************************************************************************
                                д�������
********************************************************************************/
//------------------------------��д����������----------------------------------
static void _WrBack(void)
{
  //��λ��д���
  _Eeprom.WrBackTimer = 0; 
  _Eeprom.BufWrCount = 0;  
  
  if(_Eeprom.BufBase == (unsigned long)-1) return; //�쳣����������û������
  
  //׼��1�����ҵ���д�����ҳ�����ϻ�ַ
  unsigned char CurNew;
  unsigned long OldFlashBase; 
  if(_Eeprom.Page2New){
    OldFlashBase = EEPROM_BASE2;    
    CurNew = 0;//�����෴
  }
  else{
    OldFlashBase = EEPROM_BASE1;      
    CurNew = 1;
  }
  //׼��2��������ȷ�����������ֵ���µ�
  if(!memcmp((void*)(OldFlashBase + _HEADER_SIZE + _Eeprom.BufBase), 
             _Eeprom.Buf, EEPROM_BUF_SIZE))
    return; //û�и���
  
  //׼��3���ȸ�ʽ����д��ҳ��  
  _Format(CurNew);  
  
  unsigned long OffAdr = _HEADER_SIZE; //��ǰ��ַƫ��
  //1. ����������ַǰ���Flashԭ������д����ҳ��
  if(_Eeprom.BufBase){
    _WrFlash(CurNew,
             OffAdr, 
             (unsigned char*)(OldFlashBase + OffAdr),
             _Eeprom.BufBase);
    OffAdr += _Eeprom.BufBase;  //ƫ�Ƹ������´�
  }
  //2. �������������д��(_FlashToBuf()ʱ��ȷ��������)
  _WrFlash(CurNew, OffAdr, _Eeprom.Buf, EEPROM_BUF_SIZE);
  OffAdr += EEPROM_BUF_SIZE;  //ƫ�Ƹ������´�
  
  //3. ����������ַ�����Flashԭ������д����ҳ��
  if(OffAdr < EEPROM_SIZE){
    _WrFlash(CurNew,
             OffAdr, 
             (unsigned char*)(OldFlashBase + OffAdr),
             EEPROM_SIZE - OffAdr);
  }
  //4.���¶�ʱ���ۼӲ�д��(δ���Ǽ������׻ػ���0���)
  _Eeprom.Counter[0]++;
  _Eeprom.Counter[1] = 0 - _Eeprom.Counter[0];//ȡ����ΪУ����д��
  _WrFlash(CurNew, 0, _Eeprom.Counter, 8);//д����ͷ
  //ע�������˶�����ͷд���У�飡
  
  //5.����л�����ҳ�����(ע: ��ʱ���������������Ȼ���µĲ���Ч)
  _Eeprom.Page2New = CurNew;
}

//-----------------------���ָ��λ�������ݵ�������-------------------------
static void _FlashToBuf(EepromAdr_t Adr)
{
  Adr &= ~0x07; //��֤8�ֶ��뷽��дFlashʱ���Ч��
  if((Adr + EEPROM_BUF_SIZE) > (EEPROM_SIZE - _HEADER_SIZE)){//������ˣ�ֱ�ӻ����������
    Adr = (EEPROM_SIZE - _HEADER_SIZE) - EEPROM_BUF_SIZE;
  }
  //�ҵ���ַ
  unsigned long FlashBase; 
  if(_Eeprom.Page2New) FlashBase = EEPROM_BASE2 + _HEADER_SIZE;
  else FlashBase = EEPROM_BASE1 + _HEADER_SIZE;
  //copy����
  memcpy(_Eeprom.Buf, 
         (const unsigned char*)(FlashBase + Adr), 
         EEPROM_BUF_SIZE);
  //������ɣ������ܸ��µ�ַ
  _Eeprom.BufBase = Adr; 
}

//---------------------------д�û����ݵ�������---------------------------------
//ֻ������ʱ����д���Ƚ����ݣ�����1��ʾ�ɹ�(��������ͬ)������ʧ��
static signed char  _EerpomToBuf(EepromAdr_t Adr,
                                 const void *pVoid,
                                 EepromLen_t Len)

{
  if(Adr < _Eeprom.BufBase) return 0;//��ʼλ���ڻ���������,���ܺ�벿������Ҳ��Ϊ������
  
  EepromAdr_t OffBufBase = Adr - _Eeprom.BufBase;//�õ���������ƫ��
  
  if(OffBufBase >= EEPROM_BUF_SIZE) return 0; //��ʼλ���ڻ���������
  
  if((OffBufBase + Len) >  EEPROM_BUF_SIZE) return 0; //�������У�Ҳ��Ϊû����
  
  //��������Ƿ�һ�£���û�����򲻴���->RAM��RAM������ͬ���
  //if(!memcmp(&_Eeprom.Buf[OffBufBase], pVoid, Len))
  //  return 1; //һ��Ҳ�൱��д�ˣ�Ҳ�ɹ���  
  
  //�ڻ��������ˣ�д�뻺���� 
  memcpy(&_Eeprom.Buf[OffBufBase], pVoid, Len);
  _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //�����Զ���д
  return 1; //�ɹ�������
}

//---------------------------дEeprom����---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
  //1. ����������ʱд��������
  if(_EerpomToBuf(Adr, pVoid, Len)) return; 
  //2. û�����У����д��������Ƿ�û���Ĺ�
  unsigned long FlashBase; 
  if(_Eeprom.Page2New) FlashBase = EEPROM_BASE2 + _HEADER_SIZE;
  else FlashBase = EEPROM_BASE1 + _HEADER_SIZE;
  if(!memcmp((void*)(FlashBase + Adr), pVoid, Len)) return; //����û�б����Ĺ�!  
  //3. ��дԭ�л���������
  _WrBack();
  //4. ��дԭ�����ݺ󣬶�ȡ������������ݵ�������
  _FlashToBuf(Adr);
  //5. �ٴλ���������д��������(һ����,�������������)
  _EerpomToBuf(Adr, pVoid, Len);
}

/*******************************************************************************
                                ���������
********************************************************************************/
//---------------------------�õ��û����ݴӻ�����-------------------------------
//���ǵ�����һ���ṹ�У�д�������ݣ�����һ������������ʲ�ȡ���ֶ���ʽ
//����0ȫ�����꣬Len: δ��; 0x80...ǰ�벿��δ���꣬0x40...��벿��δ���꣬
static unsigned long _EerpomFromBuf(EepromAdr_t Adr,
                                    void *pVoid,
                                    EepromLen_t Len)

{
  if(Adr < _Eeprom.BufBase){//��ʼ����
    EepromAdr_t EndPos = Adr + Len;
    if(EndPos <= _Eeprom.BufBase) //����λ��Ҳ����
      return Len; //��ʼλ���ڻ�����ǰ����
    //���ݵ�ǰ�沿���ڻ���������
    EepromLen_t RdCount = EndPos - _Eeprom.BufBase;
    memcpy(pVoid, &_Eeprom.Buf, RdCount);
    return 0x80000000 | (Len - RdCount); //ǰ�벿��δ��
  }
  EepromAdr_t OffBufBase = Adr - _Eeprom.BufBase;//�õ���������ƫ��
  
  if(OffBufBase >= EEPROM_BUF_SIZE) return Len; //��ʼλ���ڻ�����������
  
  EepromLen_t RdCount;
  //ȫ�������ݵĺ��沿���ڻ���������
  if((OffBufBase + Len) >  EEPROM_BUF_SIZE){
    RdCount = EEPROM_BUF_SIZE - OffBufBase; //�ض�
  }
  else RdCount = Len;
  //�ӻ�����ȫ�����벿�ֶ����� 
  memcpy(pVoid, &_Eeprom.Buf[OffBufBase], RdCount);
  if(RdCount == Len) return 0;//������
  
  return 0x40000000 | (Len - RdCount); //��벿��δ��
}

//---------------------------��ȡEeprom����---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
  //1. �ȴӻ������������
  unsigned long  NextLen = _EerpomFromBuf(Adr, pVoid, Len);
  if(NextLen == 0) return; //ȫ��������

  //2.��Flash���ȡ
  unsigned long FlashBase; 
  if(_Eeprom.Page2New) FlashBase = EEPROM_BASE2 + _HEADER_SIZE;
  else FlashBase = EEPROM_BASE1 + _HEADER_SIZE;
  FlashBase += Adr;
  
  if(NextLen != Len){//���ֻ�����
    NextLen &= 0x3fffffff;
    if(NextLen & 0x80000000)//������ǰ�벿��
      FlashBase -= (Len - NextLen);
    else FlashBase += (Len - NextLen);  //�ں�벿��
    Len = NextLen;
  }
  //��д��FLASHʱ���ⲿ������˶�����RAM�ĸ��£��ʲ���黺�����Ƿ�д��
  #ifdef Flash_Read
    Flash_Read(FlashBase, pVoid, Len);
  #else //��ͨ���ڴ�ķ���
    memcpy(pVoid, (unsigned char*)(FlashBase), Len);
  #endif
}

//---------------------------�õ��û�ָ��ӻ�����-------------------------------
//���ǵ�����һ���ṹ�У�д�������ݣ�����һ������������ʲ�ȡ���ֶ���ʽ
//����NULL: ���ڻ�����,-1: �������У����򷵻�����ָ��
static const unsigned char *_pEerpomFromBuf(EepromAdr_t Adr,
                                            EepromLen_t Len)

{
  if(Adr < _Eeprom.BufBase){//��ʼ����
    EepromAdr_t EndPos = Adr + Len;
    if(EndPos <= _Eeprom.BufBase) //����λ��Ҳ����
      return NULL;
    //���ݵ�ǰ�沿���ڻ���������
    return (unsigned char *)-1;
  }
  EepromAdr_t OffBufBase = Adr - _Eeprom.BufBase;//�õ���������ƫ��
  
  if(OffBufBase >= EEPROM_BUF_SIZE) return NULL; //��ʼλ���ڻ�����������
  
  //ȫ�������ݵĺ��沿���ڻ���������
  if((OffBufBase + Len) >  EEPROM_BUF_SIZE) return (unsigned char *)-1; //������
  //ȫ�����ˣ����ػ�����ָ��
  return &_Eeprom.Buf[OffBufBase];
}

//-------------------------��Eeprom��ַת��Ϊֻ��ָ��------------------
//������ֻ�����ݣ������Խṹ��ʽ���������Ч��
const unsigned char *Eeprom_pGetRd(EepromAdr_t Adr, EepromLen_t Len)
{
  //1. �ȴӻ������������
  const unsigned char *pData = _pEerpomFromBuf(Adr, Len);
  if(pData == (unsigned char *)-1){ //��������ʱ
    if(_Eeprom.WrBackTimer){//��������ʱ�����дԭ�л��������ݷ�ֹ����Ϊһ��
      _WrBack();
    }
    pData = NULL;//���ڴ����
  }
  if(pData != NULL) return pData; //������
  //ֱ��ָ����Ч�ڴ�����
  unsigned long FlashBase; 
  if(_Eeprom.Page2New) FlashBase = EEPROM_BASE2 + _HEADER_SIZE;
  else FlashBase = EEPROM_BASE1 + _HEADER_SIZE;
  return (unsigned char *)(FlashBase + Adr);
}

//-------------------------��Eeprom��ַת��Ϊ��дָ��------------------
//���������ݵĶ�д����ֻ�������ģ�Ӧʹ��Eeprom_pGetRd()�Խ�ʡд�������Ч��
//�����Խṹ��ʽ���������Ч��
const unsigned char *Eeprom_pGetWr(EepromAdr_t Adr, EepromLen_t Len)
{
  //1. �ȴӻ������������
  const unsigned char *pData = _pEerpomFromBuf(Adr, Len);
  if((pData != NULL) && (pData != (unsigned char *)-1)){ //������
    if(_Eeprom.BufWrCount != 255) _Eeprom.BufWrCount++; //��������д�ۼ�
    _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //�Զ���д
    return pData; 
  }
  //2. �������л�û����ʱ
  if(_Eeprom.WrBackTimer) _WrBack();//��������ʱ�����дԭ�л���������

  //3.��������
  _FlashToBuf(Adr);

  //��д�û�������
  _Eeprom.BufWrCount = 0; //������Ϊ��������
  _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //�Զ���д
  //4.���ػ��������  
  return _pEerpomFromBuf(Adr, Len); //�˴�������
}

//-------------------------������---------------------------------
//����250ms������
void Eeprom_Task(void)
{
  //��ֹ�û�Ƶ������ʱ��λ��д���˻�������ǿ�ƻ�д
  if(_Eeprom.BufWrCount == 255){
    _WrBack();
    return;
  }
  //�Զ���д����
  if(!_Eeprom.WrBackTimer) return; //û�л�д����
  _Eeprom.WrBackTimer--;
  if(_Eeprom.WrBackTimer) return;  //�ȴ�������
  //ʱ�䵽ִ�л�д
  _WrBack();
}

//-----------------------�õ�д���������---------------------------------
//�д˹���ʱ�����أ����򷵻�Ϊ0
unsigned long Eeprom_GetWrCount(void)
{
  return _Eeprom.Counter[0];
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

  


