# MIMXRT1170-EVKB-VGLite-RT-Thread-Nano

## Introduction

VGLite examples from RT1170 EVKB SDK, in which FreeRTOS is replaced with RT-Thread Nano.

## Environment

* i.MX RT1170 EVKB SDK v2.14.0
* RT-Thread Nano v3.1.3
* IAR 9.40.1

## How to Use

Open /board/vglite_examples/xxx/iar/xxx.eww with IAR, make sure that the macro `DEMO_PANEL` in /board/display_support.h matches the displayer, then compile and run.