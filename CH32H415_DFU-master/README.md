# CH32H415 Betaflight & INAV DFU

由于CH32内置的ISP程序没有DFU功能，这个程序模拟一个DFU设备，用于升级引导betaflight和INAV的飞控主程序。该程序请使用
MRS2.0打开，且仅编译下载V3F部分代码。

## Features related to flight control

* CH32H415为RISC—V双核设计，大小核V3F/V5F指令集相同，均为RV32IMACBF；
* V5F最高频率400MHz，V3F最高频率150MHz；
* 896KB SRAM，960KB Flash；
* GPTIMx8, ADVTIMx2, ADCx2, DACx2, SDIOx1, SPIx4, I2Cx4, USARTx8, QSPIx2, PDUSB(USBSS,USBHS,USBFS,PDType-C)

## Extended 

本程序仅是一个DFU功能的引导程序，大小核之间没有通讯，理想运用场景可以是小核处理一些简单运用，大核跑飞控算法。
例如小核除DFU外支持OSD的随屏显示字符叠加，大小核之间通过核间通信和硬件信号量进行通讯。


## Flight control

目前已经移植BF4.6.0/BF4.5.3，以及INAV，详见以下仓库：<br>
* [BF4.6.0](https://github.com/TianpeiLee/betaflight/tree/master)
* [BF4.5.3](https://github.com/TianpeiLee/betaflight/tree/h415_bf45x)
* [INAV](https://github.com/TianpeiLee/inav/tree/master)



