/*
 * Copyright (C) 2013-2017 Intel Corporation
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _TVINPUT_HAL_UTILS_H_
#define _TVINPUT_HAL_UTILS_H_

#include "hardware/hardware.h"
#if defined(ANDROID_VERSION_ABOVE_12_X)
#include <hardware/hardware_rockchip.h>
#endif
#include <linux/videodev2.h>
#include <utils/Log.h>
#include <cutils/properties.h>
#include <hardware/hardware_rockchip.h>
#include <hardware/gralloc_rockchip.h>

#define HIN_DEV_NODE_MAIN "/dev/video0"
#define HIN_DEV_NODE_OTHERS "/dev/video1"

// 04201000 = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_EXT_PIX_FORMAT | V4L2_CAP_STREAMING
//typedef V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE TVHAL_V4L2_BUF_TYPE
//#define TVHAL_V4L2_BUF_TYPE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
#define TVHAL_V4L2_BUF_MEMORY_TYPE V4L2_MEMORY_DMABUF //V4L2_MEMORY_MMAP

#define SIDEBAND_RECORD_BUFF_CNT 4
#define SIDEBAND_WINDOW_BUFF_CNT 3 //pq/enc/nv24trans need >= 3
#define APP_PREVIEW_BUFF_CNT SIDEBAND_WINDOW_BUFF_CNT
#define SIDEBAND_PQ_BUFF_CNT SIDEBAND_WINDOW_BUFF_CNT
#define PLANES_NUM 1

#define DEFAULT_V4L2_STREAM_WIDTH 1920
#define DEFAULT_V4L2_STREAM_HEIGHT 1080
#define DEFAULT_V4L2_STREAM_FORMAT 859981650//V4L2_PIX_FMT_NV12

//HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
#define DEFAULT_TVHAL_STREAM_FORMAT HAL_PIXEL_FORMAT_YCrCb_NV12    // 0x15 = 21
//#define DEFAULT_TVHAL_STREAM_FORMAT HAL_PIXEL_FORMAT_RGB_888
//#define DEFAULT_TVHAL_STREAM_FORMAT HAL_PIXEL_FORMAT_YCbCr_422_SP_10    // 0x18 = 24
//#define DEFAULT_TVHAL_STREAM_FORMAT HAL_PIXEL_FORMAT_sRGB_A_8888    // 0x18 = 24
//#define DEFAULT_TVHAL_STREAM_FORMAT HAL_PIXEL_FORMAT_FLEX_RGBA_8888    // 0x18 = 24

//#define DUMP_YUV_IMG

enum FrameType{
    TYPF_SIDEBAND_WINDOW = 0x1,
    TYPE_STREAM_BUFFER_PRODUCER = 0x2,
};

enum DisplayRatio{
    FULL_SCREEN = 0x0,
    SCREEN_16_9 = 0x1,
    SCREEN_4_3  = 0x2,
};

#define PQ_OFF           0

static const int64_t STREAM_BUFFER_GRALLOC_USAGE = (
    GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN |
    RK_GRALLOC_USAGE_WITHIN_4G | RK_GRALLOC_USAGE_PHY_CONTIG_BUFFER
);

#define TV_INPUT_USER_FORMAT "vendor.tvinput.format"
#define TV_INPUT_SKIP_FRAME "vendor.tvinput.skipframe"
#define TV_INPUT_DUMP_TYPE "vendor.tvinput.dumptype"
#define TV_INPUT_SHOW_FPS "vendor.tvinput.showfps"
#define TV_INPUT_HAS_ENCODE "vendor.tvinput.encode"
#define TV_INPUT_DISPLAY_RATIO "vendor.tvinput.displayratio"
#define TV_INPUT_PQ_ENABLE "persist.vendor.rkpq.enable"
#define TV_INPUT_PQ_MODE "persist.vendor.pq.mode"
#define TV_INPUT_PQ_RANGE "persist.vendor.rkpq.range"
#define TV_INPUT_PQ_LUMA "persist.vendor.rkpq.luma"
#define TV_INPUT_HDMIIN "vendor.rk.hdmiin"
#define TV_INPUT_RESOLUTION_MAIN "persist.vendor.resolution.main"
#define DEBUG_LEVEL_PROPNAME "vendor.tvinput.level"

#define DEBUG_PRINT(level, fmt, arg...)                       \
    do {                                                      \
        if (3 == level)                                       \
            ALOGE("%s:line %d | " fmt, __func__, __LINE__,## arg);              \
        else if (2 == level)                                  \
            ALOGD("%s:line %d | " fmt, __func__, __LINE__, ## arg);              \
        else if (1 == level)                                  \
            ALOGI("%s:line %d | " fmt, __func__, __LINE__, ## arg);              \
        else                                                  \
            ALOGV("%s:line %d | " fmt, __func__, __LINE__, ## arg);              \
    } while (0)


#endif // _TVINPUT_HAL_UTILS_H_
