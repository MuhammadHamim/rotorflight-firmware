/*
	1.使用高速USB口，模拟DFU设备，对H415进行升级
	2.上电按住PC8口，进入升级模式，否则跳转唤醒V5内核执行其代码
	3.唤醒V5核后，V3若没有任务，可以进入睡眠
*/


#include "debug.h"
#include "hardware.h"
#include "dfu_ch32h41x.h"

#define USE_HSI
/* Global define */
void usb_dc_low_level_init(void)
{

	Delay_Ms(100);
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

	if((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS)
	{
		/* Initialize USBHS 480M PLL */
		RCC_USBHS_PLLCmd(DISABLE);
#if defined USE_HSI
		RCC_USBHSPLLCLKConfig(RCC_USBHSPLLSource_HSI); 
#else		
		RCC_USBHSPLLCLKConfig(RCC_USBHSPLLSource_HSE); 
#endif
		RCC_USBHSPLLReferConfig(RCC_USBHSPLLRefer_25M);
		RCC_USBHSPLLClockSourceDivConfig(RCC_USBHSPLL_IN_Div1);
		RCC_USBHS_PLLCmd(ENABLE);
	}
	/* Enable UTMI Clock */
	RCC_UTMIcmd(ENABLE);
	/* Enable USBHS Clock */
	RCC_HBPeriphClockCmd(RCC_HBPeriph_USBHS, ENABLE);

    NVIC_EnableIRQ(USBHS_IRQn);

    Delay_Us(100);
}

void bootpin_init( void )
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;  //下拉输入
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
}

static uint32_t boot_pin_low_cnt = 0;

void checkboot(void)
{
	if(!(GPIOC->INDR & GPIO_Pin_8)) //boot脚未按下
	{
		for(uint32_t i=0;i<500;i++)
		{
			if(!(GPIOC->INDR & GPIO_Pin_8)) boot_pin_low_cnt++;
			else boot_pin_low_cnt = 0;
		}
	}

	if(boot_pin_low_cnt > 400) //跳转APP
	{
		NVIC_WakeUp_V5F(Core_V5F_StartAddr);

		Delay_Us(100);
		while(1)
		{
			;
		}
	}
	else 
	{
		dfu_flash_init(0, 0);

		// FLASH_Unlock_Fast( );
		// FLASH->ACTLR |= (0x3<<0);
		// FLASH->ACTLR &= ~(0x1<<0);  
		// FLASH_Lock_Fast( );

		while(1)
		{
			;
		}
	}
}




void TIM9_11_INIT(void)
{
	GPIO_InitTypeDef 				GPIO_InitStructure={0};
	TIM9_12_OCInitTypeDef 			TIM_OCInitStructure={0};
	TIM9_12_TimeBaseInitTypeDef 	TIM_TimeBaseInitStructure={0};

	RCC_HB2PeriphClockCmd( RCC_HB2Periph_GPIOB|RCC_HB2Periph_GPIOD|RCC_HB2Periph_TIM9|RCC_HB2Periph_TIM11|RCC_HB2Periph_AFIO, ENABLE );

	GPIO_PinAFConfig(GPIOB,GPIO_PinSource11,GPIO_AF9); //TIM9_CH4
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource3,GPIO_AF2); //TIM11_CH1
	

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;       //TIM9_CH4
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
	GPIO_Init( GPIOB, &GPIO_InitStructure );

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;         //TIM11_CH1
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
	GPIO_Init( GPIOD, &GPIO_InitStructure );



	TIM_TimeBaseInitStructure.TIM_Period = 175000000/10000 - 1;
	TIM_TimeBaseInitStructure.TIM_Prescaler = 0;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM9_12_TimeBaseInit( TIM9, &TIM_TimeBaseInitStructure);

	TIM_TimeBaseInitStructure.TIM_Period = 175000000/20000-1;
	TIM_TimeBaseInitStructure.TIM_Prescaler = 0;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM9_12_TimeBaseInit( TIM11, &TIM_TimeBaseInitStructure);


	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OCInitStructure.TIM_Pulse = 175000000/10000/2;
	TIM9_12_OC4Init( TIM9, &TIM_OCInitStructure );

	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OCInitStructure.TIM_Pulse = 175000000/20000/2;
	TIM9_12_OC1Init( TIM11, &TIM_OCInitStructure );

	TIM_Cmd( TIM9, ENABLE );
	TIM_Cmd( TIM11, ENABLE );
}




/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
	SystemInit();
	SystemAndCoreClockUpdate();
	Delay_Init();
	// USART_Printf_Init(2000000);
	// Delay_Ms(100);

	// printf("SystemClk:%d\r\n", SystemClock);
	// printf("V3F SystemCoreClk:%d\r\n", SystemCoreClock);
	



	// TIM9_11_INIT();
	// printf("TIM11-ATRLR_32:%d\r\n", TIM11->ATRLR_32);			
	// printf("TIM9-ATRLR_32:%d\r\n", TIM9->ATRLR_32);			
	// while(1)
	// {
	// printf("TIM11-CNT_32:%d\r\n", TIM11->CNT_32);			
	// printf("TIM9-CNT_32:%d\r\n", TIM9->CNT_32);	
	// Delay_Ms(100);	
	// }

 	bootpin_init(  );
	checkboot(  ); 






	
#if (Run_Core == Run_Core_V3FandV5F)
	NVIC_WakeUp_V5F(Core_V5F_StartAddr);//wake up V5
	HSEM_ITConfig(HSEM_ID0, ENABLE);
	NVIC->SCTLR |= 1<<4;
	RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
	PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
	HSEM_ClearFlag(HSEM_ID0);
	printf("V3F wake up\r\n");

	Hardware();

#elif (Run_Core == Run_Core_V3F)

	while(1)
	{
		// PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE); //如果没有任务需要执行，直接进入低功耗模式
		// asm("wfi");
	}


#elif (Run_Core == Run_Core_V5F)
	NVIC_WakeUp_V5F(Core_V5F_StartAddr);//wake up V5
	PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
	printf("V3F wake up\r\n");
#endif

	
	while(1)
	{
		
	}
}
