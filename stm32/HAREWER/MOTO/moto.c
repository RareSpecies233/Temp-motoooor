#include "moto.h"


/**************************************************************************
函数功能：电机的正反转
入口参数：mode   mode=0时为正转  mode=1时反转
返回  值：无
**************************************************************************/


void moto1(int mode)
{ 
	if(mode==1)
	{
	 GPIO_SetBits(GPIOB, GPIO_Pin_7);	 // 高电平
	 GPIO_ResetBits(GPIOB, GPIO_Pin_8);	 // 低电平}
		
	}
	 if(mode==0)
	{
	 GPIO_SetBits(GPIOB, GPIO_Pin_8);	 // 高电平
	 GPIO_ResetBits(GPIOB, GPIO_Pin_7);	 // 低电平}
		
	 }
 
}
void moto2(int mode)
{ 
	if(mode==1)
	{
	
		 GPIO_SetBits(GPIOA, GPIO_Pin_2);	 // 高电平
	     GPIO_ResetBits(GPIOA, GPIO_Pin_1);	 // 低电平}
	}
	 if(mode==0)
	{
	 
		GPIO_SetBits(GPIOA, GPIO_Pin_1);	 // 高电平
	     GPIO_ResetBits(GPIOA, GPIO_Pin_2);	 // 低电平}
	 }
 
}


