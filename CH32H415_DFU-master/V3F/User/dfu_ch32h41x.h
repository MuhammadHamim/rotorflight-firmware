#ifndef __DFU_CH32H41X_H
#define __DFU_CH32H41X_H

#include "usbd_core.h"
#include "usbd_dfu.h"


// #define DFU_FOR_BETAFLIGHT

//betaflight configurator filters xIDs,Support for WCH xIDs needs to be added.
#define USBD_VID           0x0483
#define USBD_PID           0xDF11
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033


#define FLASH_DESC_STR "@Internal Flash   /0x08016000/109*08Kg"                     //V5F从88KB启动
// #define FLASH_DESC_STR "@Internal Flash   /0x08010000/112*08Kg"                  //V5F从64KB启动 

#define USB_CONFIG_SIZE (54)   //54   27







#ifdef CONFIG_USB_HS
#define CDC_MAX_MPS 512
#else
#define CDC_MAX_MPS 64
#endif



void dfu_flash_init(uint8_t busid, uintptr_t reg_base);



#endif


