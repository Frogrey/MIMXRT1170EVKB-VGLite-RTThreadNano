/*
 * Copyright 2019, 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* RT-Thread kernel includes. */
#include "rtthread.h"

/*-----------------------------------------------------------*/
#include "vg_lite.h"
#include "vglite_support.h"
#include "vglite_window.h"
#include "Elm.h"

#include "clock_analog.h"
#include "hour_needle.h"
#include "minute_needle.h"

#include "fsl_soc_src.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define APP_BUFFER_COUNT 2
#define DEFAULT_SIZE     256.0f;

typedef struct elm_render_buffer
{
    ElmBuffer handle;
    vg_lite_buffer_t *buffer;
} ElmRenderBuffer;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void vglite_thread(void *parameter);

/*******************************************************************************
 * Variables
 ******************************************************************************/
static vg_lite_display_t display;
static vg_lite_window_t window;

static vg_lite_matrix_t matrix;

static ElmHandle analogClockHandle  = ELM_NULL_HANDLE;
static ElmHandle hourNeedleHandle   = ELM_NULL_HANDLE;
static ElmHandle minuteNeedleHandle = ELM_NULL_HANDLE;
static ElmRenderBuffer elmFB[APP_BUFFER_COUNT];

extern unsigned int ClockAnalog_evo_len;
extern unsigned char ClockAnalog_evo[];

extern unsigned int HourNeedle_evo_len;
extern unsigned char HourNeedle_evo[];

extern unsigned int MinuteNeedle_evo_len;
extern unsigned char MinuteNeedle_evo[];
/*******************************************************************************
 * Code
 ******************************************************************************/
static void BOARD_ResetDisplayMix(void)
{
    /*
     * Reset the displaymix, otherwise during debugging, the
     * debugger may not reset the display, then the behavior
     * is not right.
     */
    SRC_AssertSliceSoftwareReset(SRC, kSRC_DisplaySlice);
    while (kSRC_SliceResetInProcess == SRC_GetSliceResetState(SRC, kSRC_DisplaySlice))
    {
    }
}


int main(void)
{
    BOARD_ResetDisplayMix();

    rt_thread_t vglite_thread_handle = rt_thread_create("vglite_thread", vglite_thread, RT_NULL, 1024, 0, 1);
    if (vglite_thread_handle != RT_NULL)
        rt_thread_startup(vglite_thread_handle);
}

static ELM_BUFFER_FORMAT _buffer_format_to_Elm(vg_lite_buffer_format_t format)
{
    switch (format)
    {
        case VG_LITE_RGB565:
            return ELM_BUFFER_FORMAT_RGB565;
            break;
        case VG_LITE_BGR565:
            return ELM_BUFFER_FORMAT_BGR565;
            break;
        case VG_LITE_BGRX8888:
            return ELM_BUFFER_FORMAT_BGRX8888;
            break;
        default:
            return ELM_BUFFER_FORMAT_RGBA8888;
            break;
    }
}

static void cleanup(void)
{
    vg_lite_close();
}

static int load_clock()
{
    if (ClockAnalog_evo_len != 0)
    {
        analogClockHandle = ElmCreateObjectFromData(ELM_OBJECT_TYPE_EGO, (void *)ClockAnalog_evo, ClockAnalog_evo_len);
    }

    return (analogClockHandle != ELM_NULL_HANDLE);
}

static int load_hour()
{
    if (HourNeedle_evo_len != 0)
    {
        hourNeedleHandle = ElmCreateObjectFromData(ELM_OBJECT_TYPE_EGO, (void *)HourNeedle_evo, HourNeedle_evo_len);
    }

    return (hourNeedleHandle != ELM_NULL_HANDLE);
}

static int load_minute()
{
    if (MinuteNeedle_evo_len != 0)
    {
        minuteNeedleHandle =
            ElmCreateObjectFromData(ELM_OBJECT_TYPE_EGO, (void *)MinuteNeedle_evo, MinuteNeedle_evo_len);
    }

    return (minuteNeedleHandle != ELM_NULL_HANDLE);
}

static int load_texture()
{
    int ret = 0;
    ret     = load_clock();
    if (ret < 0)
    {
        rt_kprintf("load_clock");
        return ret;
    }
    ret = load_hour();
    if (ret < 0)
    {
        rt_kprintf("load_hour");
        return ret;
    }
    ret = load_minute();
    if (ret < 0)
    {
        rt_kprintf("load_minute");
    }
    return ret;
}

static vg_lite_error_t init_vg_lite(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int ret               = 0;

    error = VGLITE_CreateDisplay(&display);
    if (error)
    {
        rt_kprintf("VGLITE_CreateDisplay failed: VGLITE_CreateDisplay() returned error %d\n", error);
        return error;
    }
    // Initialize the window.
    error = VGLITE_CreateWindow(&display, &window);
    if (error)
    {
        rt_kprintf("VGLITE_CreateWindow failed: VGLITE_CreateWindow() returned error %d\n", error);
        return error;
    }
    // Initialize the draw.
    ret = ElmInitialize(DEFAULT_VG_LITE_TW_WIDTH, DEFAULT_VG_LITE_TW_HEIGHT);
    if (!ret)
    {
        rt_kprintf("ElmInitialize failed\n");
        cleanup();
        return VG_LITE_OUT_OF_MEMORY;
    }
    // Set GPU command buffer size for this drawing task.
    error = vg_lite_set_command_buffer_size(VG_LITE_COMMAND_BUFFER_SIZE);
    if (error)
    {
        rt_kprintf("vg_lite_set_command_buffer_size() returned error %d\n", error);
        cleanup();
        return error;
    }

    // Setup a scale at center of buffer.
    vg_lite_identity(&matrix);
    vg_lite_translate(window.width / 2.0f, window.height / 2.0f, &matrix);
    // load the texture;
    ret = load_texture();
    if (ret < 0)
    {
        rt_kprintf("load_texture error");
        return VG_LITE_OUT_OF_MEMORY;
    }

    return error;
}

static ElmBuffer get_elm_buffer(vg_lite_buffer_t *buffer)
{
    for (int i = 0; i < APP_BUFFER_COUNT; i++)
    {
        if (elmFB[i].buffer == NULL)
        {
            elmFB[i].buffer = buffer;
            elmFB[i].handle = ElmWrapBuffer(buffer->width, buffer->height, buffer->stride, buffer->memory,
                                            buffer->address, _buffer_format_to_Elm(buffer->format));
            vg_lite_clear(buffer, NULL, 0x0);
            return elmFB[i].handle;
        }
        if (elmFB[i].buffer == buffer)
            return elmFB[i].handle;
    }
    return 0;
}
static int render(vg_lite_buffer_t *buffer, ElmHandle object)
{
    int status                = 0;
    ElmBuffer elmRenderBuffer = get_elm_buffer(buffer);
    status                    = ElmDraw(elmRenderBuffer, object);
    if (!status)
    {
        status = -1;
        return status;
    }
    ElmFinish();
    return status;
}

static void redraw()
{
    int status = 0;

    vg_lite_buffer_t *rt = VGLITE_GetRenderTarget(&window);
    if (rt == NULL)
    {
        rt_kprintf("vg_lite_get_renderTarget error");
        while (1)
            ;
    }
    static float angle = 0;

    // Draw the path using the matrix.
    status = render(rt, analogClockHandle);
    if (status == -1)
    {
        rt_kprintf("ELM Render analogClockHandle Failed");
        return;
    }

    ElmReset(hourNeedleHandle, ELM_PROP_TRANSFER_BIT);
    ElmTransfer(hourNeedleHandle, 200.0, 200.0);
    ElmRotate(hourNeedleHandle, angle);

    status = render(rt, hourNeedleHandle);
    if (status == -1)
    {
        rt_kprintf("ELM Render hourNeedleHandle Failed");
    }

    ElmReset(minuteNeedleHandle, ELM_PROP_TRANSFER_BIT);
    ElmTransfer(minuteNeedleHandle, 200.0, 200.0);
    ElmRotate(minuteNeedleHandle, -angle);

    status = render(rt, minuteNeedleHandle);
    if (status == -1)
    {
        rt_kprintf("ELM Render minuteNeedleHandle Failed");
    }

    angle += 0.5;

    VGLITE_SwapBuffers(&window);

    return;
}

uint32_t getTime()
{
    return (rt_tick_t)(1000 * rt_tick_get() / RT_TICK_PER_SECOND);
}

static void vglite_thread(void *parameter)
{
    status_t status;
    vg_lite_error_t error;

    status = BOARD_PrepareVGLiteController();
    if (status != kStatus_Success)
    {
        rt_kprintf("Prepare VGlite contolor error");
        while (1)
            ;
    }

    error = init_vg_lite();
    if (error)
    {
        rt_kprintf("init_vg_lite failed: init_vg_lite() returned error %d\n", error);
        while (1)
            ;
    }

    uint32_t startTime, time, n = 0;
    startTime = getTime();
    while (1)
    {
        redraw();
        n++;
        if (n > 60)
        {
            time = getTime() - startTime;
            rt_kprintf("%d frames in %d seconds: %d fps", n, time / 1000, n * 1000 / time);
            n         = 0;
            startTime = getTime();
        }
    }
}
