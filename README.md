# MIMXRT1170-EVKB-VGLite-RT-Thread-Nano

## Introduction

VGLite examples from RT1170 EVKB SDK, in which FreeRTOS is replaced by RT-Thread Nano.

The work includes two parts: porting kernel and finsh console of RT-Thread Nano to RT1170, and replacing FreeRTOS API of VGLite and Elementary with RT-Thread API.

## Environment

* i.MX RT1170 EVKB SDK v2.14.0
* RT-Thread Nano v3.1.3
* IAR 9.40.1

## How to Use

If using the MIPI interface, connect the LCD displayer to *J48* on the MIMXRT1170-EVK board. Connect 5V power to *J43*, set *J38* to 1-2, and turn on the power switch *SW5*.

Open */board/vglite_examples/xxx/iar/xxx.eww* with IAR, make sure that the macro `DEMO_PANEL` in */board/display_support.h* matches the displayer, then compile and run.

## Tutorial

* [i.MX RT1170 VGLite移植RT-Thread Nano教程（上）.md](./README/i.MX%20RT1170%20VGLite移植RT-Thread%20Nano教程（上）.md)
* [i.MX RT1170 VGLite移植RT-Thread Nano教程（下）.md](./README/i.MX%20RT1170%20VGLite移植RT-Thread%20Nano教程（下）.md)
