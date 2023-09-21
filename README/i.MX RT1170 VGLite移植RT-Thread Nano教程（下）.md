# i.MX RT1170：VGLite 移植 RT-Thread Nano 教程（下）

上篇介绍了如何移植 RT-Thread Nano 内核与 Finsh 控制台到 RT1170。本篇继续介绍如何将 NXP 官方的 VGLite API 移植到 RT-Thread Nano 上。

## RT-Thread 配置

rtconfig.h 可对 RT-Thread 配置，因 VGLite 会使用互斥量、消息队列等，故取消以下注释：

``` C
#define RT_USING_MUTEX
#define RT_USING_MESSAGEQUEUE
#define RT_USING_HEAP
```

邮箱机制在本工程并不使用，可以注释掉：

``` C
// #define RT_USING_MAILBOX
```

默认最大名称长度为 8，可以更改为 16 以容纳更长名称。

``` C
#define RT_NAME_MAX    16
```

原 FreeRTOS 工程 tick 频率为 200，RT-Thread Nano 默认 tick 频率为 1000，可与原工程保持一致。

``` C
#define RT_TICK_PER_SECOND  200
```

## FreeRTOS 与 RT-Thread 对比

首先分析 FreeRTOS 与 RT-Thread 的一些区别，以加深读者理解，帮助后续用 RT-Thread API 改写 FreeRTOS API 。

### 任务与线程

FreeRTOS 称线程为 “任务” （ task ），而 RT-Thread 直接称为 “线程” （ thread ），这一术语尚未达成共识，两者只是同一事物的不同表述。

### 任务（线程）优先级

FreeRTOS 中，优先级范围为 0 到 `configMAX_PRIORITIES - 1`，该宏在 FreeRTOSConfig.h 中定义，数字越低则该任务优先级越低。

RT-Thread 中，优先级范围为 0 到 `RT_THREAD_PRIORITY_MAX - 1`，该宏在 rtconfig.h 中定义，数字越低线程优先级却越高，这点与 FreeRTOS 相反。

### 任务（线程）调度

FreeRTOS 中，需手动调用 `vTaskStartScheduler()` 开启任务调度器。任务一旦创建便直接参与调度运行。时间片轮转调度时，各相同优先级线程的单次运行时间片统一为 1，即 1 个 tick 便调度一次。

RT-Thread 中，系统初始化时就已调用了 `rt_system_scheduler_start()` ，无需再手动开启。但创建后的线程尚位于初始状态，初始状态的线程均需调用 `rt_thread_startup()` 才会参与调度运行。各线程的单次运行时间片在创建时可指定为不同 tick。

### 中断适用函数

FreeRTOS 中，涉及上下文切换的函数存在两个版本：一种是在任务中的常规版本；另一种则用于中断内调用，通常以 `FromISR()` 结尾。若中断调用的 API 唤醒了更高优先级的线程，需手动调用 `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)` 以在中断退出时唤醒高优先级线程。

RT-Thread 中，线程与中断可使用同一 API。但中断内不能使用挂起当前线程的操作，若使用则会打印 "Function [xxx_func] shall not used in ISR" 的提示信息。若中断内的函数唤醒了更高优先级的线程，则中断退出时会自动切换到高优先级线程，无需手动切换。

### 线程本地数据

FreeRTOS 中，线程的本地数据为一个数组，长度由 FreeRTOSConfig.h 中的 `configNUM_THREAD_LOCAL_STORAGE_POINTERS` 宏设置。

RT-Thread 中，线程的本地数据为一个 `uint32` 格式的 `user_data` 变量，而非数组。若要在线程本地存储数组、结构体等数据，可手动创建后将地址存入该变量。

### 信号量

FreeRTOS 中，分为二值信号量与计数信号量。二值信号量最大值为 1 ，初值为 0 。计数信号量的最大值和初值均可在创建时分别指定。

RT-Thread 中，未区分二值或计数信号量，且仅能指定信号量初值，最大值无法指定，统一为 65535 。此外，信号量在创建时可选先入先出模式（ `RT_IPC_FLAG_FIFO` ）或优先级模式（ `RT_IPC_FLAG_PRIO` ），通常选用优先级模式以保证线程实时性。

### 互斥量

FreeRTOS 中，除创建的 API 以外，互斥量的结构体与持有、释放与删除所使用的 API ，与信号量的相同。

RT-Thread 中，互斥量拥有一套独立的 API ，而非与信号量共用 API 。

### 头文件

FreeRTOS 中，当使用信号量、互斥量、队列等，除 FreeRTOS.h 外，需额外包含其他对应的头文件。

RT-Thread 中，通常仅需包含 rtthread.h 即可使用信号量、互斥量、队列等。

## VGLite 代码改写

首先，可将上篇工程中排除编译的组与文件恢复回去，并恢复之前备份的 /source/clock_rtthread.c 代码。

### 头文件更改

工程在以下文件中会使用到 RT-Thread ，需包含头文件： `#include "rtthread.h"` ：

* /source/clock_rtthread.c 
* /vglite/VGLite/rtos/vg_lite_os.c （较多改动）
* /vglite/VGLiteKernel/rtos/vg_lite_hal.c
* /vglite/font/vft_draw.c
* /elementary/src/elm_os.c 
* /elementary/src/velm.h
* /video/fsl_fbdev.h
* /video/fsl_dc_fb_lcdifv2.c
* /video/fsl_video_common.c
* /utilities/fsl_debug_console.c

### 线程 API 改写

FreeRTOS 中的 `xTaskCreate()` 可由 RT-Thread 中的 `rt_thread_create()` 替换。注意两者优先级数字代表的高低相反，需转换。`rt_thread_create()` 中可指定线程单次运行的时间片，若 RT-Thread 已设置 tick 频率与原 FreeRTOS 相等，则时间片可全部设置为 1 。 `vTaskDelete(NULL)` 为删除当前线程，RT-Thread 可用 `rt_thread_delete(rt_thread_self())` 代替。 `TaskHandle_t` 结构体由 `rt_thread_t` 代替。

而原有的 `vTaskStartScheduler()` 应删除，改用 `rt_thread_startup()` 启动指定线程。此外，命名中的 "task" 可替换为 "thread" 以符合 RT-Thread 规范。

/source/clock_rtthread.c 的线程相关代码对比主要如下：（篇幅所限，仅以有代表性的文件与函数为例，其他未列出的文件和函数可根据例子参考，对编译错误信息位置改写，下同）

``` C
xTaskCreate(vglite_task, "vglite_task", configMINIMAL_STACK_SIZE + 200, NULL, configMAX_PRIORITIES - 1, NULL)
^^^^^^
rt_thread_t vglite_thread_handle = rt_thread_create("vglite_thread", vglite_thread, RT_NULL, 1024, 0, 1);
```
``` C
vTaskStartScheduler();
^^^^^^
if (vglite_thread_handle != RT_NULL)
    rt_thread_startup(vglite_thread_handle);
```

/vglite/VGLite/rtos/vg_lite_os.c 更改的线程相关代码主要如下：

``` C
#define QUEUE_TASK_PRIO  (configMAX_PRIORITIES - 1)
^^^^^^^
#define QUEUE_THREAD_PRIO  0
```
``` C
vTaskDelete(NULL);
^^^^^^^
rt_thread_delete(rt_thread_self());
```
``` C
ret = xTaskCreate(command_queue, QUEUE_TASK_NAME, QUEUE_TASK_SIZE, NULL, QUEUE_TASK_PRIO, &os_obj.task_hanlde);
^^^^^^^
os_obj.task_hanlde = rt_thread_create(QUEUE_THREAD_NAME, command_queue, NULL, QUEUE_THREAD_SIZE, QUEUE_THREAD_PRIO, 1);
if (os_obj.task_hanlde != RT_NULL)
    rt_thread_startup(os_obj.task_hanlde);
```

### 信号量 API 改写

原 `xSemaphoreCreateCounting()` 与 `xSemaphoreCreateBinary()` 均可使用 `rt_sem_create()` 代替，`rt_sem_create()` 需设置名称，无需设置最大值，初值通常为 0，排队方式一般采用 `RT_IPC_FLAG_PRIO` ，下同。 `SemaphoreHandle_t` 结构体换为 `rt_sem_t` 。

`xSemaphoreTake()` 可替换为 `rt_sem_take()`，FreeRTOS 中 `portMAX_DELAY` 代表无限等待，可换为 RT-Thread 的 `RT_WAITING_FOREVER` 。若原 FreeRTOS 中指定过期 tick 形如 `timeout / portTICK_PERIOD_MS`，应使用 `(rt_int32)((rt_int64)timeout * RT_TICK_PER_SECOND / 1000)` 替换。

判断返回值由 `pdTRUE` 替换为 `RT_EOK` 。 `xSemaphoreGive()`换为 `rt_sem_release()` 。 `vSemaphoreDelete()` 使用 `rt_sem_delete()` 替换。有关信号量的中断内函数 `xSemaphoreGiveFromISR()` 在下文再详细讲解。

以 /vglite/VGLite/rtos/vg_lite_os.c 为例，信号量代码对比主要如下：

``` C
command_semaphore = xSemaphoreCreateCounting(30,0);
^^^^^^^
command_semaphore = rt_sem_create("cs", 0, RT_IPC_FLAG_PRIO);
```
``` C
int_queue = xSemaphoreCreateBinary();
^^^^^^^
int_queue = rt_sem_create("iq", 0, RT_IPC_FLAG_PRIO);
```
``` C
if (xSemaphoreTake(int_queue, timeout / portTICK_PERIOD_MS) == pdTRUE)
^^^^^^^
if (rt_sem_take(int_queue, (rt_int32_t) ((rt_int64_t)timeout * RT_TICK_PER_SECOND / 1000)) == RT_EOK)
```

<!--
以下文件同样需改写信号量相关函数与结构体，均可参考以上例子：

* /video/fsl_fbdev.h
* /video/fsl_fbdev.c
* /vglite/VGLiteKernel/rtos/vg_lite_hal.c
-->

### 互斥量 API 改写

`xSemaphoreCreateMutex()` 替换为 `rt_mutex_create()` ，需指定名称与排队方式。`SemaphoreHandle_t` 结构体替换为 `rt_mutex_t` 。

其他互斥量 API 的改写与信号量基本一致。`xSemaphoreTake()` 、`xSemaphoreGive()` 、`vSemaphoreDelete()` 替换为 `rt_mutex_take()` 、`rt_mutex_release()` 、`rt_mutex_delete()` 。

/vglite/VGLite/rtos/vg_lite_os.c 中，互斥量代码对比主要如下：

``` C
mutex = xSemaphoreCreateMutex();
^^^^^^^
mutex = rt_mutex_create("mut", RT_IPC_FLAG_PRIO);
```
``` C
if(xSemaphoreTake(mutex, TASK_WAIT_TIME/portTICK_PERIOD_MS) == pdTRUE)
^^^^^^^
if(rt_mutex_take(mutex, (rt_int32_t) ((rt_int64_t)MAX_MUTEX_TIME * RT_TICK_PER_SECOND / 1000)) != RT_EOK)
```

### 消息队列 API 改写

`xQueueCreate()` 替换为 `rt_mq_create()` ，需指定名称与排队方式。 `QueueHandle_t` 结构体替换为 `rt_mq_t` 。

RT-Thread 无类似 `uxQueueMessagesWaiting()` 的函数用于确认队列是否不为空，但 `rt_mq_t` 结构体中的 `entry` 变量表示队列的消息数，故可用 `if (xxx->entry)`代替。

`xQueueReceive()` 、 `xQueueSend()` 更换为 `rt_mq_recv()` 、 `rt_mq_send_wait()` ，需额外指定发送与接收消息的大小，同时也需注意过期 tick 的转换。

/vglite/VGLite/rtos/vg_lite_os.c 中，消息队列代码对比主要如下：

``` C
os_obj.queue_handle = xQueueCreate(QUEUE_LENGTH, sizeof(vg_lite_queue_t * ));
^^^^^^^
os_obj.queue_handle = rt_mq_create("queue_vglite", sizeof(vg_lite_queue_t * ), QUEUE_LENGTH, RT_IPC_FLAG_PRIO);
```
``` C
if(uxQueueMessagesWaiting(os_obj.queue_handle))
^^^^^^^
if(os_obj.queue_handle->entry)
```
``` C
ret = xQueueReceive(os_obj.queue_handle, (void*) &peek_queue, TASK_WAIT_TIME/portTICK_PERIOD_MS);
^^^^^^^
ret = rt_mq_recv(os_obj.queue_handle, (void*) &peek_queue, os_obj.queue_handle->msg_size, (rt_int32_t) ((rt_int64_t)TASK_WAIT_TIME * RT_TICK_PER_SECOND / 1000));
```
``` C
if(xQueueSend(os_obj.queue_handle, (void *) &queue_node, ISR_WAIT_TIME/portTICK_PERIOD_MS) != pdTRUE)
^^^^^^^
if(rt_mq_send_wait(os_obj.queue_handle, (void *) &queue_node, os_obj.queue_handle->msg_size, (rt_int32_t) ((rt_int64_t)ISR_WAIT_TIME * RT_TICK_PER_SECOND / 1000)) != RT_EOK)
```

### 中断内 API 改写

FreeRTOS 中断内采用 `xSemaphoreGiveFromISR()` 信号量释放函数保证以中断安全，且根据 `xHigherPriorityTaskWoken` 变量，需使用 `portYIELD_FROM_ISR()` 手动切换上下文；而 RT-Thread 仍使用通用的 `rt_sem_release()` ，且可自动切换上下文。

/vglite/VGLite/rtos/vg_lite_os.c 中，中断内代码对比主要如下：

``` C
portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
xSemaphoreGiveFromISR(int_queue, &xHigherPriorityTaskWoken);
if(xHigherPriorityTaskWoken != pdFALSE )
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
^^^^^^^
rt_sem_release(int_queue);
```

<!--
/video/fsl_fbdev.c 文件同样需改写信号量在中断中的函数与结构体，可参考以上例子。
-->

### 内存管理 API 改写

`pvPortMalloc()` 代替为 `rt_malloc()` ，`vPortFree()` 代替为 `rt_free()` 即可。

<!--
以下文件需改写内存管理相关函数：
* /vglite/VGLite/rtos/vg_lite_os.c
* /vglite/font/vft_draw.c
* /elementary/src/velm.h
-->

### 临界区资源 API 改写

`portENTER_CRITICAL()` 与 `portEXIT_CRITICAL()` 需替换为 `rt_enter_critical()` 与 `rt_exit_critical()` 。 

<!--
/video/fsl_fbdev.c 文件需对临界区资源相关函数进行改写。
-->

### 时间相关 API 改写

`vTaskDelay()` 可替换为 `rt_thread_delay()` 。若用于延时指定毫秒，也可直接使用 `rt_thread_mdelay()` 代替，无需再计算毫秒对应的 tick。`xTaskGetTickCount()` 用于得到 tick 的计数值，可用 `rt_tick_get()` 代替。

/source/clock_rtthread.c 的时间相关代码对比如下：

``` C
return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
^^^^^^^
return (rt_tick_t)((rt_uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
```

/vglite/VGLite/rtos/vg_lite_os.c 中，延时代码对比如下，无需再计算毫秒对应的 tick：

``` C
vTaskDelay((configTICK_RATE_HZ * msec + 999)/ 1000);
^^^^^^^
rt_thread_mdelay(msec);
```

<!--
以下文件也需对延时相关函数进行改写，均可参考以上例子：
* /video/fsl_dc_fb_lcdifv2.c
* /video/fsl_video_common.c
* /utilities/fsl_debug_console.c
-->

### 线程本地数据 API 改写

FreeRTOS 中各线程的本地数据为一个数组，而 RT-Thread 中线程本地数据仅有一个名为 `user_data` 的变量。

原 VGLite 仅使用了 FreeRTOS 本地数组中第一个元素，若用 RT-Thread 仅改用 VGLite API，则可直接用 `user_data` 变量保存原数组中第一个元素。但若也使用了 Elementary （即本工程），Elementary 也需要存放一个线程本地变量，此时本地数据便需要存放两个变量。这是使用 RT-Thread Nano 需要先开辟一个数组空间以存放两个变量，再将数组地址存放于本地的 `user_data` 变量中。

在工程 /vglite/VGLite/rtos/vg_lite_os.c 中新建数组 `tls_array` 并添加 `vg_lite_os_init_tls_array()` 、 `vg_lite_os_deinit_tls_array()` 函数，再在对应的 .h 文件中声明。`vg_lite_os_init_tls_array()` 将数组地址存入当前线程的 `user_data` 变量；`vg_lite_os_deinit_tls_array()` 则用于删除当前线程 `user_data` 中的数组地址。

``` C
#define TLS_ARRAY_LENGTH    2
rt_uint32_t tls_array[TLS_ARRAY_LENGTH] = {NULL};

int32_t vg_lite_os_init_tls_array(void) {
    rt_thread_t rt_TCB = rt_thread_self();
    RT_ASSERT( rt_TCB != NULL );
    rt_TCB->user_data = (rt_uint32_t) tls_array;

    return VG_LITE_SUCCESS;
}
void vg_lite_os_deinit_tls_array(void) {
    rt_thread_t rt_TCB = rt_thread_self();
    RT_ASSERT( rt_TCB != NULL );
    rt_TCB->user_data = NULL;
}
```

再对 /vglite/VGLite/rtos/vg_lite_os.c 中 `vg_lite_os_get_tls()` 、`vg_lite_os_set_tls()` 、`vg_lite_os_reset_tls()` 修改。原例程直接调用了 FreeRTOS 函数获取与修改线程本地数据，而新代码需手动实现，以获取当前线程本地数据并读写数组第一位元素：

``` C
void * vg_lite_os_get_tls() {
    rt_thread_t rt_TCB = rt_thread_self();
    rt_uint32_t * tls_ptr = (rt_uint32_t *) rt_TCB->user_data;
    void * pvReturn = (void *) (*tls_ptr);
    return pvReturn;
}
```
``` C
int32_t vg_lite_os_set_tls(void* tls) {
    rt_thread_t rt_TCB;
    rt_TCB = rt_thread_self();
    RT_ASSERT( rt_TCB != NULL );
    rt_uint32_t * tls_ptr = (rt_uint32_t *) rt_TCB->user_data;
    *tls_ptr = (rt_uint32_t) tls;
}
```
``` C
void vg_lite_os_reset_tls() {
    rt_thread_t rt_TCB = rt_thread_self();
    RT_ASSERT( rt_TCB != NULL );
    rt_uint32_t * tls_ptr = (rt_uint32_t *) rt_TCB->user_data;
    *tls_ptr = NULL;
}
```

随后更改工程 /vglite/VGLite/vg_lite.c 中 `vg_lite_init()` ，在调用 `vg_lite_os_get_tls()` 、`vg_lite_os_malloc()` 、`vg_lite_os_set_tls()` 等函数之前，添加上文定义的 `vg_lite_os_init_tls_array()` 进行线程本地数据初始化。同样，该文件有 `vg_lite_close()` ，在其调用 `vg_lite_os_reset_tls()` 等函数的最后，也需添加上文定义的 `vg_lite_os_deinit_tls_array()` 。

### Elementary API 改写

使用 Elementary 时，同样也需修改工程 /elementary/src/elm_os.c 中的 `elm_os_get_tls()` 、 `elm_os_set_tls()` 、`elm_os_reset_tls()` ，以读写线程本地数据中数组的第二个元素，与上文 VGLite 的三个线程本地数据 API 改写方法基本一致，主要区别为将使用 `*(tls_ptr + 1)` 而非 `*tls_ptr` 。

### 数据类型改写

FreeRTOS 定义了 `TickType_t` 与 `BaseType_t` 类型，在 RT1170 中可分别用 `rt_uint32_t` 与 `rt_err_t` 代替。同时，可用 `RT_NULL` 代替 `NULL` 判断 RT-Thread 对象是否为空。

<!--
以下文件需对数据类型进行改写：
* /vglite/VGLite/rtos/vg_lite_os.c
* /video/fsl_fbdev.c （在中断函数中因不再需要手动切换上下文，也可直接删除 `BaseType_t` 变量）
* /video/fsl_video_common.c （若用于延时指定毫秒，也可直接删除转换毫秒为 tick 的 `TickType_t` 变量与计算过程，直接使用 `rt_thread_mdelay()` 代替）
-->

### 输出 API 改写

若在上篇移植了 Finsh 控制台组件，则可用 `rt_kprintf()` 代替 `PRINTF` 宏。 `rt_kprintf()` 已自动在字符串末尾添加 "\r\n" ，无需再手动添加。

## 结果验证

编译并运行，若与上篇的原工程结果相同，即屏幕出现指针不断旋转的时钟，且串口打印帧数信息。恭喜， VGLite 与 Elementary 已成功移植到 RT-Thread Nano 上！

## 总结

VGLite 移植到 RT-Thread Nano 的过程还是有些繁琐的，需要更改的 FreeRTOS API 较多，但两个 RTOS 的大部分特性相似度高，难度不大。经过两篇文章的学习，相信您对于 FreeRTOS、RT-Thread 以及 VGLite 的细节有了更深入的了解。

此外，若追求移植效率，并不关心 RTOS 细节，也可以使用 RT-Thread 的 [FreeRTOS-Wrapper](https://github.com/RT-Thread-packages/FreeRTOS-Wrapper) 兼容层替换 FreeRTOS，读者可以自行进行研究。

## 参考教程

* https://www.rt-thread.org/document/api/index.html
* https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/basic/basic
* https://freertos.org/zh-cn-cmn-s/FreeRTOS-quick-start-guide.html
* https://freertos.org/fr-content-src/uploads/2018/07/161204_Mastering_the_FreeRTOS_Real_Time_Kernel-A_Hands-On_Tutorial_Guide.pdf