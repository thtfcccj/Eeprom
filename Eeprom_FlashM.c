/***************************************************************************

         Eeprom��дģ��-ʹ���ڲ���Flash�ռ���ģ��-����ҳ��ʱʵ��
��ģ�������Ӳ��,ʹ�ñ�׼Flash�ӿ�ʵ�֣��������ڲ�ͬMCU��FLASH(���ⲿFLASH)
ʹ��˵��:
1. ��ȫ�ֺ궨����ʼ��ַEEPROM_BASE,����FLASH_CAPACITY,ҳ��СFLASH_PAGE_SIZE
2. ���ֳ�ʼ����־Ϊ�״�ʹ��FLASH�����ʱ����ͨ��Eeprom_Format()��EEPROM����
3. ��ģ��ϵͳ����:
   (1. Eeprom_init()�������EEPROMǰ�ĳ�ʼ����
   (2. Eeprom_Task()����ϵͳ256ms������
   (3. ʵ��WDT_Week()
�ڲ�����˵��:
   Ϊ��С����������ģ�齫�����Ҫд���ҳ������RAM�У����û��л�ҳ�����û�
û��д���������ڲ��жϳ�ʱʱ(2s)�����Ὣ������ֵ�Զ�д��FLASH��

ע����ģ���ֹ�洢>1ҳ������
****************************************************************************/

#include "Eeprom.h"
#include "EepromInner.h"
#include "Flash.h"              //Flash��׼�����ӿ�
#include <string.h>
//#include "IoCtrl.h"             //������WDT_Week();


/******************************************************************************
                               ������ṹ
******************************************************************************/

//--------------------------ȫ�ֶ���-------------------------------------
//֧����EEPROMд����ʱ�� ͬʱ����һ�����д�����ݴ�С
//#define SUPPORT_EEPROM_WR_BUF  512   

//--------------------------��Ĭ��ֵ-------------------------------------
#ifndef EEPROM_WR_BACK_OV   //��дʱ��Ĭ��256ms*8
  #define EEPROM_WR_BACK_OV  8
#endif

#ifndef EEPROM_BASE
  #define EEPROM_BASE  0x08008000  //EEPROM��ҳ��ʼ��ַ��ҳ32��
#endif

#ifndef FLASH_CAPACITY    //Flash�Ŀռ�������,����ΪFLASH_PAGE_SIZE������
  #define FLASH_CAPACITY  0x08000  //Ĭ��32768
#endif

#ifndef FLASH_PAGE_SIZE 	//��ֵΪʵ��оƬ�����ϵ�ҳ��С
  #define FLASH_PAGE_SIZE  512   //û�ж���FLASHҳʱʹ��Ĭ��ֵ
#endif

struct _Eeprom{ //ҳ����
  unsigned long Buf[FLASH_PAGE_SIZE / 4];  //ҳ���壬��֤4���ֶ���
  unsigned char WrBackTimer; //��ʱ��,��ֵʱ��ʾ�ࣨ��ʾ�������ӦFlash��ֵ�Բ��ϣ� 
  unsigned char BufWrCount;//���ü����������ڷ�ֹ�û�Ƶ�����û�ÿ�дָ��ʱ����λ��д��ʱ���Ӷ����²��Զ���д
  unsigned char CurPage;  //��ǰ���е�ҳ
};
struct _Eeprom  _Eeprom;

#ifdef SUPPORT_EEPROM_WR_BUF
struct _WrBuf{ //ҳ����
  unsigned long Buf[SUPPORT_EEPROM_WR_BUF / 4];  //д��
  unsigned long EepromAdr;          //д�����ݻ�ַ
  unsigned short Len;                //д�����ݳ��ȣ�0��ʾû����д�������
};
struct _WrBuf _WrBuf;

#endif


/******************************************************************************
                               ��غ���ʵ��
******************************************************************************/

//---------------------------��ʼ������---------------------------------
void Eeprom_Init(void)
{
  _Eeprom.CurPage = 255; //��λû������������ҳ��������ֵ��Ч
  _Eeprom.WrBackTimer = 0;     //û����
  _Eeprom.BufWrCount = 0;             //����������
  #ifdef SUPPORT_EEPROM_WR_BUF
    _WrBuf.Len = 0;      //û����Ҫд�������
  #endif
}

//----------------------��ʽ��Eeprom����---------------------------------
void Eeprom_Format(void)
{
  unsigned long Base = EEPROM_BASE;
  for(; Base < (EEPROM_BASE + FLASH_CAPACITY); Base+= FLASH_PAGE_SIZE){
    Flash_Unlock();//����
    Flash_ErasePage(Base);//����ҳ
    Flash_Lock();//����
    //WDT_Week();//ι��һ��
  }
  _Eeprom.BufWrCount = 0; //���¿�ʼ 
}

//--------------------------��д������(һҳ)����---------------------------------
static void _WrPageBack(void)
{
  unsigned long Base = EEPROM_BASE + _Eeprom.CurPage * FLASH_PAGE_SIZE;
  if((Base >= (EEPROM_BASE + FLASH_CAPACITY)) || //�쳣��û�л���
     (Base < EEPROM_BASE)) return; 
  //��λ���
   _Eeprom.WrBackTimer = 0; 
   _Eeprom.BufWrCount = 0;
   
  //ȷ�����������ֵ���µ�
  if(!memcmp((void*)Base, _Eeprom.Buf, FLASH_PAGE_SIZE)) return; //û�и���
  
  Flash_Unlock();//����
  Flash_ErasePage(Base);//д����ǰ������ҳ
  Flash_Write(Base, _Eeprom.Buf, FLASH_PAGE_SIZE);
  Flash_Lock();//����
}

//--------------------------��д����---------------------------------
static void _WrBack(void)
{
  #ifdef SUPPORT_EEPROM_WR_BUF
    if(_WrBuf.Len){//����Ҫд��ʱ
      if(!_Eeprom.WrBackTimer) _Eeprom.WrBackTimer = 1;//�������ʱ���ڻ�д
      Eeprom_Wr(_WrBuf.EepromAdr, _WrBuf.Buf, _WrBuf.Len);//�����ڻ�����
      _WrBuf.Len = 0;
      //������ǿ��д��
    }
  #endif
  _WrPageBack();
}

//---------------------------дEepromҳ������---------------------------------
static void _WrInPage(EepromAdr_t Adr,
                      const void *pVoid,
                      EepromLen_t Len)
{
  unsigned char NextPage = Adr / FLASH_PAGE_SIZE;
  //�ȼ���������
  if(NextPage != _Eeprom.CurPage){
    if(_Eeprom.WrBackTimer) _WrPageBack();//���˵�û���Զ�������FLASHʱ���Ȼ�д     
      
    //������һҳ����
    unsigned long Base = EEPROM_BASE + NextPage * FLASH_PAGE_SIZE;
    memcpy(_Eeprom.Buf,(unsigned char*)Base, FLASH_PAGE_SIZE);
    _Eeprom.CurPage = NextPage;
  }
  //�ڴ�Ƚ��з������(��ʱ��ΪֻҪ���ü�����)
  //if(��ͬʱ) reutrn ���ز���
  //�ڻ�������,����Ҫд���������
  memcpy(((unsigned char*)_Eeprom.Buf) + (Adr % FLASH_PAGE_SIZE), pVoid,Len);//����
  _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //�����Զ���д
  if(_Eeprom.BufWrCount != 255) _Eeprom.BufWrCount++; //��������д�ۼ�
}

//---------------------------дEeprom����---------------------------------
void Eeprom_Wr(EepromAdr_t Adr,
               const void *pVoid,
               EepromLen_t Len)
{
  //�ȼ���Ƿ��ҳ,��ҳ�����Ϊ�����ִ���
  unsigned short InPageBase = Adr % FLASH_PAGE_SIZE;
  if((InPageBase + Len) > FLASH_PAGE_SIZE){//��ҳ��
    //�ֳ���ҳ���� (�ݽ�ֹ�洢<2ҳ������)
    EepromLen_t CurLen = FLASH_PAGE_SIZE - InPageBase;
    _WrInPage(Adr, pVoid, CurLen);
    _WrInPage(Adr + CurLen, (const char *)pVoid + CurLen, Len - CurLen);    
  }
  else _WrInPage(Adr, pVoid, Len);
}

//---------------------------��ȡEeprom����---------------------------------
void Eeprom_Rd(EepromAdr_t Adr,
               void *pVoid,
               EepromLen_t Len)
{
  //��д��FLASHʱ���ⲿ������˶�����RAM�ĸ��£��ʲ���黺�����Ƿ�д��
  #ifdef Flash_Read
    Flash_Read(EEPROM_BASE + Adr, pVoid, Len);
  #else //��ͨ���ڴ�ķ���
    memcpy(pVoid, (unsigned char*)(EEPROM_BASE + Adr), Len);
  #endif
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
  if(!_Eeprom.WrBackTimer) return; //û�л�д����
  _Eeprom.WrBackTimer--;
  if(_Eeprom.WrBackTimer) return;  //�ȴ�������
  //ʱ�䵽ִ�л�д
  _WrBack();
}

//--------------------------ǿ�ƻ�д����������---------------------------------
void Eeprom_ForceWrBuf(void)
{
  _WrBack();
  _Eeprom.WrBackTimer = 0;//���ٵȴ�
}

//--------------------------ǿ�ƻ�д������---------------------------
void Eeprom_ForceWrBufAndRestart(void)
{
  _WrBack();  
  while(1);
}

/******************************************************************************
                               ��дָ�뺯��ʵ��
******************************************************************************/

//-------------------------��Eeprom��ַת��Ϊֻ��ָ��------------------
//������ֻ�����ݣ������Խṹ��ʽ���������Ч��
const unsigned char *Eeprom_pGetRd(EepromAdr_t Adr, EepromLen_t Len)
{
  //�ȴӵ�ǰEEPROM����������
  #ifdef SUPPORT_EEPROM_WR_BUF
    if(_WrBuf.Len){//�����ݻ���ʱ
      EepromAdr_t EndAdr = _WrBuf.EepromAdr + SUPPORT_EEPROM_WR_BUF;
      if((Adr >= _WrBuf.EepromAdr) && (Adr < EndAdr)){//��ʼ�ڷ�Χ
        if((Adr + Len) <= EndAdr){//�����ڷ�Χ�ˣ�������ֱ�ӷ��ػ�����
         return ((unsigned char *)_WrBuf.Buf) + (Adr - _WrBuf.EepromAdr); 
        }
        else{//��������ʱ���Ƚ�����������EEPROM��������
          Eeprom_Wr(_WrBuf.EepromAdr, _WrBuf.Buf, _WrBuf.Len);//�����ڻ�����
          _WrBuf.Len = 0;          
        }
      }
    }
  #endif
  //���EEPOM������������ʱ���ػ���������������ʱ���д�ٶ�
  if(_Eeprom.WrBackTimer && (_Eeprom.CurPage != 0xFF)){//
    EepromAdr_t BufBase = _Eeprom.CurPage * FLASH_PAGE_SIZE;
    EepromAdr_t BufEnd = BufBase + FLASH_PAGE_SIZE;
    if((Adr >= BufBase) && (Adr < BufEnd)){//��ʼ����
      if((Adr + Len) <= BufEnd){//�����ڷ�Χ��
        return ((unsigned char *)_Eeprom.Buf) + (Adr % FLASH_PAGE_SIZE); 
      }
      else _WrBack();//��������ʱ,�Ȼ�д,Ȼ���ֻ������
    }
  }
  
  return (const unsigned char *)(EEPROM_BASE +Adr);
}

//-------------------------��Eeprom��ַת��Ϊ��дָ��------------------
//���������ݵĶ�д����ֻ�������ģ�Ӧʹ��Eeprom_pGetRd()�Խ�ʡд�������Ч��
//�����Խṹ��ʽ���������Ч��
unsigned char *Eeprom_pGetWr(EepromAdr_t Adr, EepromLen_t Len)
{
  #ifdef SUPPORT_EEPROM_WR_BUF
    if(_WrBuf.Len){//�����ݻ���ʱ
      EepromAdr_t EndAdr = _WrBuf.EepromAdr + SUPPORT_EEPROM_WR_BUF;
      if((Adr >= _WrBuf.EepromAdr) && ((Adr + Len) <= EndAdr)){//ȫ�ڷ�Χ��������
         if(_Eeprom.BufWrCount != 255) _Eeprom.BufWrCount++; //��������д�ۼ�  
         //ֱ�ӷ��ػ�����
         _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //�����Զ�ǿ�ƻ�д
         return ((unsigned char *)_WrBuf.Buf) + (Adr - _WrBuf.EepromAdr);   
      }
      //δ���л򲿷����У���д�����ͷ�EEPROM����
     _WrBack();
    }
    //�ӵ�ǰλ�û����µ�����
    const unsigned char *pRd = Eeprom_pGetRd(Adr, SUPPORT_EEPROM_WR_BUF);//ȷ���������µ�
    memcpy(_WrBuf.Buf, pRd, SUPPORT_EEPROM_WR_BUF);
    _WrBuf.EepromAdr = Adr;
    _WrBuf.Len = SUPPORT_EEPROM_WR_BUF;
    _Eeprom.WrBackTimer = EEPROM_WR_BACK_OV; //�����Զ�ǿ�ƻ�д
    return (unsigned char *)_WrBuf.Buf; 
  #else
    return NULL;  //��֧��
  #endif
}


