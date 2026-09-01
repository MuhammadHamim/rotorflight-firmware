#include "dfu_ch32h41x.h"
#include "debug.h"

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0200, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    DFU_DESCRIPTOR_INIT()
};

static const uint8_t device_quality_descriptor[] = {
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },                   /* Langid */
    "WCH",                                          /* Manufacturer */
#if defined(DFU_FOR_BETAFLIGHT)    
    "WCH BetaFlight DFU",                           /* Product */
#else 
    "WCH RotorFlight DFU",                           /* Product */
#endif
    "2022123456",                                   /* Serial Number */
    FLASH_DESC_STR,                                 /* flash */
    "@Option Bytes /0x1FFFF800/01*016 e",           /* 用户字 */
    "@OTP Memory /0x1FFF0000/01*512 e,01*016 e",    /* 暂时先映射到boot区域 */
    "@Device Feature/0x1FFFF7E0/01*004 e"           /* flash容量指示 */ 
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    return device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    if (index > 7) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor dfu_flash_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};


static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET:
            break;
        case USBD_EVENT_CONNECTED:
            break;
        case USBD_EVENT_DISCONNECTED:
            break;
        case USBD_EVENT_RESUME:
            break;
        case USBD_EVENT_SUSPEND:
            break;
        case USBD_EVENT_CONFIGURED:
            break;
        case USBD_EVENT_SET_REMOTE_WAKEUP:
            break;
        case USBD_EVENT_CLR_REMOTE_WAKEUP:
            break;

        default:
            break;
    }
}

struct usbd_interface intf0;

void dfu_flash_init(uint8_t busid, uintptr_t reg_base)
{
    usbd_desc_register(busid, &dfu_flash_descriptor);

    usbd_add_interface(busid, usbd_dfu_init_intf(&intf0));
    
    usbd_initialize(busid, reg_base, usbd_event_handler);
}





uint8_t *dfu_read_flash(uint8_t *src, uint8_t *dest, uint32_t len)
{
    // printf("read start src %08x  %08x  %08x \r\n", src, dest,len);

    uint32_t i = 0;
   __IO uint8_t *psrc = (__IO uint8_t *)src;
    for (i = 0; i < len; i++)
    {
        dest[i] = *psrc++;
    }
    return (uint8_t *)dest;
}



uint32_t page_buffer[64];           // 256字节缓冲区
uint32_t flash_sector[1024*2];      //8KB
uint16_t dfu_write_flash(uint8_t *src, uint8_t *dest, uint32_t len)
{
  
    // len = ((len+255) & 0xFFFFFF00);
    // printf("write start src %08x  %08x  %08x \r\n",src, dest,len);
    
    // FLASH_ROM_WRITE((uint32_t)dest, (uint32_t *)src, len);
    __disable_irq( );

    FLASH_Unlock_Fast( );

    uint8_t *src_ptr = (uint8_t *)src;
    uint8_t *dest_ptr = (uint8_t *)dest;
    uint32_t total_written = 0;
    
    // 处理起始地址不对齐的情况
    uint32_t page_offset = (uint32_t)dest_ptr & 0xFF;  // 页内偏移量
    if (page_offset != 0)  //非256对齐，需要保存8K，擦除后编程
    {
        uint32_t page_start = (uint32_t)dest_ptr & ~0xFF;        // 当前页起始地址
        uint32_t sector_start = (uint32_t)dest_ptr & ~0x1FFF;    // 当前sector起始地址   
        uint32_t bytes_in_first_page = 256 - page_offset;  
        uint32_t write_size = (len < bytes_in_first_page) ? len : bytes_in_first_page;
        
        uint32_t prepage_cnt = (page_start-sector_start) / 256;

        // printf("addr:%08x %08x %d\r\n",sector_start,page_start, prepage_cnt);

        memset(flash_sector, 0x00, sizeof(flash_sector));

        memcpy((uint8_t *)flash_sector,(uint8_t *)sector_start, page_start-sector_start);

        memset(page_buffer, 0x00, sizeof(page_buffer));
        memcpy((uint8_t *)page_buffer, (uint8_t *)page_start, page_offset);
        // 复制数据到缓冲区
        memcpy((uint8_t *)page_buffer + page_offset, src_ptr, write_size);

        FLASH_ROM_ERASE(sector_start,0x2000);
       
        uint32_t *p32 = (uint32_t *)flash_sector;
        FLASH_Unlock_Fast( );
        for(uint32_t i=0; i<prepage_cnt; i++) //补齐cur_page-1
        {
            // printf("addr:%d %08x\r\n",i,sector_start);
            FLASH_ProgramPage_Fast(sector_start, p32);
            p32 += 64;
            sector_start +=256;
        }

        // 写入整页
        FLASH_ProgramPage_Fast(page_start, page_buffer);

        
        // 更新指针和计数器
        src_ptr += write_size;
        dest_ptr += write_size;
        len -= write_size;
        total_written += write_size;
    }
    
    // 处理完整的256字节页
    while (len >= 256) {
        // 直接写入整页数据
        FLASH_ProgramPage_Fast((uint32_t)dest_ptr, (uint32_t *)src_ptr);
        
        // 更新指针和计数器
        src_ptr += 256;
        dest_ptr += 256;
        len -= 256;
        total_written += 256;
    }
    
    // 处理剩余不足一页的数据，不要用页编程，会破坏之前的擦除结果
    if (len > 0) 
    {
        uint32_t page_start = (uint32_t)dest_ptr & ~0xFF;  // 当前页起始地址

#if 1        
        // 创建页缓冲区并初始化为0xFF（擦除状态）
        // uint32_t page_buffer[64];
        memset(page_buffer, 0x00, sizeof(page_buffer));
        // 复制剩余数据到缓冲区
        memcpy(page_buffer, src_ptr, len);
        // 写入整页
        FLASH_ProgramPage_Fast(page_start, page_buffer);
#else 
        memcpy(page_buffer, src_ptr, len);
        uint16_t *p16 = (uint16_t *)page_buffer;
        for(uint32_t halfwordcnt=0; halfwordcnt<len/2; halfwordcnt++)
        {
            FLASH_ProgramHalfWord((uint32_t)dest_ptr+halfwordcnt*2, *p16++);
        }
#endif

        total_written += len;
    }
    __enable_irq( );
    return 0;
}

uint16_t dfu_erase_flash(uint32_t add)
{
    // printf("Erase start add %08x \r\n",add);

    FLASH_ROM_ERASE(add,0x2000);
    
    return 0;
}

void dfu_leave(void)
{
    // printf("dfu leave\r\n");
    usbd_event_reset_handler( 0 );
    NVIC_SystemReset( );
}



