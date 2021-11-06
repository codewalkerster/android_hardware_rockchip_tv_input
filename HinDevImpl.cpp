/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 */

#include <utils/Log.h>
#include <utils/String8.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>
#include <sys/time.h>
#include <utils/Timers.h>

#include <cutils/properties.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "HinDev.h"
#include <ui/GraphicBufferMapper.h>
#include <ui/GraphicBuffer.h>
#include <linux/videodev2.h>

#define V4L2_ROTATE_ID 0x980922

#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof(((type *) 0)->member) *__mptr = (ptr);     \
        (type *) ((char *) __mptr - (char *)(&((type *)0)->member)); })
#endif

#define BOUNDRY 32
#define ALIGN_32(x) ((x + (BOUNDRY) - 1)& ~((BOUNDRY) - 1))
#define ALIGN(b,w) (((b)+((w)-1))/(w)*(w))

static int sNewFrameWidth = DEFAULT_V4L2_STREAM_WIDTH;
static int sNewFrameHeight = DEFAULT_V4L2_STREAM_HEIGHT;

static size_t getBufSize(int format, int width, int height)
{
    size_t buf_size = 0;
    switch (format) {
        case V4L2_PIX_FMT_YVU420:
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_NV12:
        {
            buf_size = width * height * 3 / 2;
            break;
        }
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_RGB565X:
            buf_size = width * height * 2;
            break;
        case V4L2_PIX_FMT_RGB24:
            buf_size = width * height * 3;
            break;
        case V4L2_PIX_FMT_RGB32:
            buf_size = width * height * 4;
            break;
        default:
            DEBUG_PRINT(3, "Invalid format");
            buf_size = width * height * 3 / 2;
    }
    return buf_size;
}

static void two_bytes_per_pixel_memcpy_align32(unsigned char *dst, unsigned char *src, int width, int height)
{
        int stride = (width + 31) & ( ~31);
        int h;
        for (h=0; h<height; h++)
        {
                memcpy( dst, src, width*2);
                dst += width*2;
                src += stride*2;
        }
}

static void nv21_memcpy_align32(unsigned char *dst, unsigned char *src, int width, int height)
{
        int stride = (width + 31) & ( ~31);
        int h;
        for (h=0; h<height*3/2; h++)
        {
                memcpy( dst, src, width);
                dst += width;
                src += stride;
        }
}

static void yv12_memcpy_align32(unsigned char *dst, unsigned char *src, int width, int height)
{
        int new_width = (width + 63) & ( ~63);
        int stride;
        int h;
        for (h=0; h<height; h++)
        {
                memcpy( dst, src, width);
                dst += width;
                src += new_width;
        }

        stride = ALIGN(width/2, 16);
        for (h=0; h<height; h++)
        {
                memcpy( dst, src, width/2);
                dst += stride;
                src += new_width/2;
        }
}

static void rgb24_memcpy_align32(unsigned char *dst, unsigned char *src, int width, int height)
{
        int stride = (width + 31) & ( ~31);
        int  h;
        for (h=0; h<height; h++)
        {
                memcpy( dst, src, width*3);
                dst += width*3;
                src += stride*3;
        }
}

static void rgb32_memcpy_align32(unsigned char *dst, unsigned char *src, int width, int height)
{
        int stride = (width + 31) & ( ~31);
        int h;
        for (h=0; h<height; h++)
        {
                memcpy( dst, src, width*4);
                dst += width*4;
                src += stride*4;
        }
}

static int  getNativeWindowFormat(int format)
{
    int nativeFormat = HAL_PIXEL_FORMAT_YCbCr_422_I;

    switch(format){
        case V4L2_PIX_FMT_YVU420:
            nativeFormat = HAL_PIXEL_FORMAT_YV12;
            break;
        case V4L2_PIX_FMT_NV21:
            nativeFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP;
            break;
        case V4L2_PIX_FMT_YUYV:
            nativeFormat = HAL_PIXEL_FORMAT_YCbCr_422_I;
            break;
        case V4L2_PIX_FMT_RGB565:
            nativeFormat = HAL_PIXEL_FORMAT_RGB_565;
            break;
        case V4L2_PIX_FMT_RGB24:
            nativeFormat = HAL_PIXEL_FORMAT_RGB_888;
            break;
        case V4L2_PIX_FMT_RGB32:
            nativeFormat = HAL_PIXEL_FORMAT_RGBA_8888;
            break;
        default:
            DEBUG_PRINT(3, "Invalid format,Use default format");
    }
    return nativeFormat;
}

HinDevImpl::HinDevImpl()
                  : mHinDevHandle(-1),
                    mHinNodeInfo(NULL),
                    mSidebandHandle(NULL),
                    mDumpFrameCount(1)
{
    char prop_value[PROPERTY_VALUE_MAX] = {0};
    property_get(DEBUG_LEVEL_PROPNAME, prop_value, "0");
    mDebugLevel = (int)atoi(prop_value);

    property_get(TV_INPUT_SKIP_FRAME, prop_value, "0");
    mSkipFrame = (int)atoi(prop_value);

    property_get(TV_INPUT_DUMP_TYPE, prop_value, "0");
    mDumpType = (int)atoi(prop_value);
    if (mDumpType == 1)
        mDumpFrameCount = 3;

    property_get(TV_INPUT_SHOW_FPS, prop_value, "0");
    mShowFps = (int)atoi(prop_value);

    DEBUG_PRINT(1, "prop value : mDebugLevel=%d, mSkipFrame=%d, mDumpType=%d", mDebugLevel, mSkipFrame, mDumpType);
    mV4l2Event = new V4L2DeviceEvent();
}

int HinDevImpl::init(int id, int initWidth, int initHeight, int initType) {
    if (!access(HIN_DEV_NODE_MAIN, F_OK|R_OK)) {
        mHinDevHandle = open(HIN_DEV_NODE_MAIN, O_RDWR);
        if (mHinDevHandle < 0)
        {
            DEBUG_PRINT(3, "[%s %d] mHinDevHandle:%x [%s]", __FUNCTION__, __LINE__, mHinDevHandle,strerror(errno));
            return -1;
        } else {
            DEBUG_PRINT(1, "%s open device %s successful.", __FUNCTION__, HIN_DEV_NODE_MAIN);
        }
    } else {
        if (access(HIN_DEV_NODE_OTHERS, F_OK|R_OK) != 0) {
            DEBUG_PRINT(3, "%s access failed!", HIN_DEV_NODE_OTHERS);
            return -1;
        }
        mHinDevHandle = open(HIN_DEV_NODE_OTHERS, O_RDWR);
        if (mHinDevHandle < 0)
        {
            DEBUG_PRINT(3, "[%s %d] mHinDevHandle:%x [%s]", __FUNCTION__, __LINE__, mHinDevHandle,strerror(errno));
            return -1;
        } else {
            DEBUG_PRINT(1, "%s open device %s successful.", __FUNCTION__, HIN_DEV_NODE_OTHERS);
        }
    }

    mV4l2Event->initialize(mHinDevHandle);

    mHinNodeInfo = (struct HinNodeInfo *) calloc (1, sizeof (struct HinNodeInfo));
    if (mHinNodeInfo == NULL)
    {
        DEBUG_PRINT(3, "[%s %d] no memory for mHinNodeInfo", __FUNCTION__, __LINE__);
        close(mHinDevHandle);
        return NO_MEMORY;
    }
    memset(mHinNodeInfo, 0, sizeof(struct HinNodeInfo));
    mHinNodeInfo->currBufferHandleIndex = 0;
    mHinNodeInfo->currBufferHandleFd = 0;

    if (initType == TV_STREAM_TYPE_INDEPENDENT_VIDEO_SOURCE) {
        mFrameType |= TYPF_SIDEBAND_WINDOW;
    } else {
        mFrameType |= TYPE_NATIVE_WINDOW_DATA;
    }

    mFramecount = 0;
    mBufferCount = SIDEBAND_WINDOW_BUFF_CNT;
    mPixelFormat = DEFAULT_TVHAL_STREAM_FORMAT;
    mFrameWidth = initWidth;
    mFrameHeight = initHeight;
    mBufferSize = mFrameWidth * mFrameHeight * 3/2;
    mSetStateCB = NULL;
    mState = STOPED;
    mANativeWindow = NULL;
    mWorkThread = NULL;
    mTvInputCB = NULL;
    mOpen = false;

    mSidebandWindow = new RTSidebandWindow();
    /**
     *  init RTSidebandWindow
     */
    RTSidebandInfo info;
    memset(&info, 0, sizeof(RTSidebandInfo));
    info.structSize = sizeof(RTSidebandInfo);
    info.top = 0;
    info.left = 0;
    info.width = initWidth;
    info.height = initHeight;
    info.usage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE
        |  GRALLOC_USAGE_HW_VIDEO_ENCODER;
    info.format = DEFAULT_TVHAL_STREAM_FORMAT; //0x15

    if(-1 == mSidebandWindow->init(info)) {
        DEBUG_PRINT(3, "mSidebandWindow->init failed !!!");
        return -1;
    }

	return NO_ERROR;
}

int HinDevImpl::makeHwcSidebandHandle() {
    buffer_handle_t buffer = NULL;

    mSidebandWindow->allocateSidebandHandle(&buffer);
    if (!buffer) {
        DEBUG_PRINT(3, "allocate buffer from sideband window failed!");
        return -1;
    }
    mSidebandHandle = buffer;
    return 0;
}

buffer_handle_t HinDevImpl::getSindebandBufferHandle() {
    if (mSidebandHandle == NULL)
        makeHwcSidebandHandle();
    return mSidebandHandle;
}

HinDevImpl::~HinDevImpl()
{
    DEBUG_PRINT(3, "%s %d", __FUNCTION__, __LINE__);
    if (mSidebandWindow) {
        mSidebandWindow->stop();
    }

    if (mHinNodeInfo)
        free (mHinNodeInfo);
    if (mHinDevHandle >= 0)
        close(mHinDevHandle);
}

int HinDevImpl::start_device()
{
    int ret = -1;

    DEBUG_PRINT(1, "[%s %d] mHinDevHandle:%x", __FUNCTION__, __LINE__, mHinDevHandle);

    ret = ioctl(mHinDevHandle, VIDIOC_QUERYCAP, &mHinNodeInfo->cap);
    if (ret < 0) {
        DEBUG_PRINT(3, "VIDIOC_QUERYCAP Failed, error: %s", strerror(errno));
        return ret;
    }
    DEBUG_PRINT(1, "VIDIOC_QUERYCAP driver=%s", mHinNodeInfo->cap.driver);
    DEBUG_PRINT(1, "VIDIOC_QUERYCAP card=%s", mHinNodeInfo->cap.card);
    DEBUG_PRINT(1, "VIDIOC_QUERYCAP version=%d", mHinNodeInfo->cap.version);
    DEBUG_PRINT(1, "VIDIOC_QUERYCAP capabilities=0x%08x", mHinNodeInfo->cap.capabilities);
    DEBUG_PRINT(1, "VIDIOC_QUERYCAP device_caps=0x%08x", mHinNodeInfo->cap.device_caps);

    mHinNodeInfo->reqBuf.type = TVHAL_V4L2_BUF_TYPE;
    mHinNodeInfo->reqBuf.memory = TVHAL_V4L2_BUF_MEMORY;
    mHinNodeInfo->reqBuf.count = mBufferCount;

    ret = ioctl(mHinDevHandle, VIDIOC_REQBUFS, &mHinNodeInfo->reqBuf);
    if (ret < 0) {
        DEBUG_PRINT(3, "VIDIOC_REQBUFS Failed, error: %s", strerror(errno));
        return ret;
    } else {
        ALOGD("VIDIOC_REQBUFS successful.");
    }

    aquire_buffer();

    for (int i = 0; i < mBufferCount; i++) {
        DEBUG_PRINT(mDebugLevel, "bufferArray index = %d", mHinNodeInfo->bufferArray[i].index);
        DEBUG_PRINT(mDebugLevel, "bufferArray type = %d", mHinNodeInfo->bufferArray[i].type);
        DEBUG_PRINT(mDebugLevel, "bufferArray memory = %d", mHinNodeInfo->bufferArray[i].memory);
        DEBUG_PRINT(mDebugLevel, "bufferArray m.fd = %d", mHinNodeInfo->bufferArray[i].m.planes[0].m.fd);
        DEBUG_PRINT(mDebugLevel, "bufferArray length = %d", mHinNodeInfo->bufferArray[i].length);
        DEBUG_PRINT(mDebugLevel, "buffer length = %d", mSidebandWindow->getBufferLength(mHinNodeInfo->buffer_handle_poll[i]));

        ret = ioctl(mHinDevHandle, VIDIOC_QBUF, &mHinNodeInfo->bufferArray[i]);
        if (ret < 0) {
            DEBUG_PRINT(3, "VIDIOC_QBUF Failed, error: %s", strerror(errno));
            return -1;
        }
    }
    ALOGD("[%s %d] VIDIOC_QBUF successful", __FUNCTION__, __LINE__);

    v4l2_buf_type bufType;
    bufType = TVHAL_V4L2_BUF_TYPE;
    ret = ioctl(mHinDevHandle, VIDIOC_STREAMON, &bufType);
    if (ret < 0) {
        DEBUG_PRINT(3, "VIDIOC_STREAMON Failed, error: %s", strerror(errno));
        return -1;
    }

    ALOGD("[%s %d] VIDIOC_STREAMON return=:%d", __FUNCTION__, __LINE__, ret);
    return ret;
}

int HinDevImpl::stop_device()
{
    DEBUG_PRINT(3, "%s %d", __FUNCTION__, __LINE__);
    int ret;
    enum v4l2_buf_type bufType = TVHAL_V4L2_BUF_TYPE;

    ret = ioctl (mHinDevHandle, VIDIOC_STREAMOFF, &bufType);
    if (ret < 0) {
        DEBUG_PRINT(3, "StopStreaming: Unable to stop capture: %s", strerror(errno));
    }
    return ret;
}

int HinDevImpl::start()
{
    ALOGD("%s %d", __FUNCTION__, __LINE__);
    int ret;
    if(mOpen == true){
        ALOGI("already open");
        return NO_ERROR;
    }

    ret = start_device();
    if(ret != NO_ERROR) {
        DEBUG_PRINT(3, "Start v4l2 device failed:%d",ret);
        return ret;
    }

    if (mFrameType & TYPF_SIDEBAND_WINDOW) {
        ALOGD("Create Work Thread");
        mWorkThread = new WorkThread(this);
    }

    mState = START;
    mOpen = true;
    ALOGD("%s %d ret:%d", __FUNCTION__, __LINE__, ret);
    return NO_ERROR;
}


int HinDevImpl::stop()
{
    ALOGD("%s %d", __FUNCTION__, __LINE__);
    int ret;

    enum v4l2_buf_type bufType = TVHAL_V4L2_BUF_TYPE;

    ret = ioctl (mHinDevHandle, VIDIOC_STREAMOFF, &bufType);
    if (ret < 0) {
        DEBUG_PRINT(3, "StopStreaming: Unable to stop capture: %s", strerror(errno));
    } else {
        DEBUG_PRINT(3, "StopStreaming: successful.");
    }

    if(mWorkThread != NULL){
        mWorkThread->requestExit();
        mWorkThread.clear();
        mWorkThread = NULL;
    }
    mState = STOPED;
    release_buffer();

    mSidebandWindow->clearVopArea();
    mDumpFrameCount = 3;

    mOpen = false;
    mFrameType = 0;

    if (mHinNodeInfo)
        free (mHinNodeInfo);

    if (mV4l2Event)
        mV4l2Event->closeEventThread();

    if (mHinDevHandle >= 0)
        close(mHinDevHandle);

    DEBUG_PRINT(3, "============================= %s end ================================", __FUNCTION__);
    return ret;
}

int HinDevImpl::set_state_callback(olStateCB callback)
{
    if (!callback){
        DEBUG_PRINT(3, "NULL state callback pointer");
        return BAD_VALUE;
    }
    mSetStateCB = callback;
    return NO_ERROR;
}

int HinDevImpl::set_data_callback(V4L2EventCallBack callback)
{
    ALOGD("%s %d", __FUNCTION__, __LINE__);
    if (callback == NULL){
        DEBUG_PRINT(3, "NULL data callback pointer");
        return BAD_VALUE;
    }
    mV4l2Event->RegisterEventvCallBack(callback);
    return NO_ERROR;
}

int HinDevImpl::get_format()
{
    return mPixelFormat;
}

int HinDevImpl::set_mode(int displayMode)
{
    DEBUG_PRINT(3, "run into set_mode,displaymode = %d\n", displayMode);
    mHinNodeInfo->displaymode = displayMode;
    m_displaymode = displayMode;
    return 0;
}

int HinDevImpl::set_format(int width, int height, int color_format)
{
    ALOGD("[%s %d] width=%d, height=%d, color_format=%d", __FUNCTION__, __LINE__, width, height, color_format);
    Mutex::Autolock autoLock(mLock);
    if (mOpen == true)
        return NO_ERROR;
    int ret;

    mFrameWidth = width;
    mFrameHeight = height;
    mHinNodeInfo->width = mFrameWidth;
    mHinNodeInfo->height = mFrameHeight;
    mHinNodeInfo->formatIn = color_format;
    mHinNodeInfo->format.type = TVHAL_V4L2_BUF_TYPE;
    mHinNodeInfo->format.fmt.pix.width = mFrameWidth;
    mHinNodeInfo->format.fmt.pix.height = mFrameHeight;
    mHinNodeInfo->format.fmt.pix.pixelformat = color_format;

    ret = ioctl(mHinDevHandle, VIDIOC_S_FMT, &mHinNodeInfo->format);
    if (ret < 0) {
        DEBUG_PRINT(3, "[%s %d] failed, set VIDIOC_S_FMT %d, %s", __FUNCTION__, __LINE__, ret, strerror(ret));
        return ret;
    } else {
        ALOGD("%s VIDIOC_S_FMT success. ", __FUNCTION__);
    }

    v4l2_format format;
    ret = ioctl(mHinDevHandle, VIDIOC_G_FMT, &format);
    if (ret < 0) {
        DEBUG_PRINT(3, "[%s %d] failed, VIDIOC_G_FMT %d, %s", __FUNCTION__, __LINE__, ret, strerror(ret));
    } else {
        DEBUG_PRINT(mDebugLevel, "after %s get from v4l2 format.type = %d ", __FUNCTION__, format.type);
        DEBUG_PRINT(mDebugLevel, "after %s get from v4l2 format.fmt.pix.width =%d", __FUNCTION__, format.fmt.pix.width);
        DEBUG_PRINT(mDebugLevel, "after %s get from v4l2 format.fmt.pix.height =%d", __FUNCTION__, format.fmt.pix.height);
        DEBUG_PRINT(mDebugLevel, "after %s get from v4l2 format.fmt.pix.pixelformat =%d", __FUNCTION__, format.fmt.pix.pixelformat);
    }
    
    mSidebandWindow->setBufferGeometry(mFrameWidth, mFrameHeight, DEFAULT_TVHAL_STREAM_FORMAT);
    return ret;
}

int HinDevImpl::set_rotation(int degree)
{
    ALOGD("[%s %d]", __FUNCTION__, __LINE__);
    int ret = 0;
    struct v4l2_control ctl;
    if(mHinDevHandle<0)
        return -1;
    if((degree!=0)&&(degree!=90)&&(degree!=180)&&(degree!=270)){
        DEBUG_PRINT(3, "Set rotate value invalid: %d.", degree);
        return -1;
    }

    memset( &ctl, 0, sizeof(ctl));
    ctl.value=degree;
    ctl.id = V4L2_ROTATE_ID;
    ret = ioctl(mHinDevHandle, VIDIOC_S_CTRL, &ctl);

    if(ret<0){
        DEBUG_PRINT(3, "Set rotate value fail: %s. ret=%d", strerror(errno),ret);
    }
    return ret ;
}

int HinDevImpl::set_crop(int x, int y, int width, int height)
{
    ALOGD("[%s %d] crop [%d - %d -%d - %d]", __FUNCTION__, __LINE__, x, y, width, height);
    mSidebandWindow->setCrop(x, y, width, height);
    return NO_ERROR;
}

int HinDevImpl::set_frame_rate(int frameRate)
{
    ALOGD("[%s %d]", __FUNCTION__, __LINE__);
    int ret = 0;

    if(mHinDevHandle<0)
        return -1;

    struct v4l2_streamparm sparm;
    memset(&sparm, 0, sizeof( sparm ));
    sparm.type = TVHAL_V4L2_BUF_TYPE;//stream_flag;
    sparm.parm.output.timeperframe.denominator = frameRate;
    sparm.parm.output.timeperframe.numerator = 1;

    ret = ioctl(mHinDevHandle, VIDIOC_S_PARM, &sparm);
    if(ret < 0){
        DEBUG_PRINT(3, "Set frame rate fail: %s. ret=%d", strerror(errno),ret);
    }
    return ret ;
}

int HinDevImpl::get_hin_crop(int *x, int *y, int *width, int *height)
{
    ALOGD("[%s %d]", __FUNCTION__, __LINE__);
    int ret = 0;

    struct v4l2_crop crop;
    memset(&crop, 0, sizeof(struct v4l2_crop));
    crop.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = ioctl(mHinDevHandle, VIDIOC_S_CROP, &crop);
    if (ret) {
        DEBUG_PRINT(3, "get amlvideo2 crop  fail: %s. ret=%d", strerror(errno),ret);
    }
    *x = crop.c.left;
    *y = crop.c.top;
    *width = crop.c.width;
    *height = crop.c.height;
     return ret ;
}

int HinDevImpl::set_hin_crop(int x, int y, int width, int height)
{
    ALOGD("[%s %d]", __FUNCTION__, __LINE__);
    int ret = 0;

    struct v4l2_crop crop;
    memset(&crop, 0, sizeof(struct v4l2_crop));

    crop.type = TVHAL_V4L2_BUF_TYPE;
    crop.c.left = x;
    crop.c.top = y;
    crop.c.width = width;
    crop.c.height = height;
    ret = ioctl(mHinDevHandle, VIDIOC_S_CROP, &crop);
    if (ret) {
        DEBUG_PRINT(3, "Set amlvideo2 crop  fail: %s. ret=%d", strerror(errno),ret);
    }

    return ret ;
}

int HinDevImpl::set_preview_window(ANativeWindow* window) {
    ALOGD("%s %d", __FUNCTION__, __LINE__);
    if(mOpen == true) {
        ALOGE("%s thread has opened, can't set_preview_window.", __FUNCTION__);
        return UNKNOWN_ERROR;
    }
    //can work without a valid window object ?
    if (window == NULL){
        ALOGE("%s param window is NULL, please check it.", __FUNCTION__);
        return UNKNOWN_ERROR;
    }
    mFrameType |= TYPE_NATIVE_WINDOW_DATA;
    mANativeWindow = window;
    return NO_ERROR;
}


int HinDevImpl::get_current_sourcesize(int *width,  int *height)
{
    int ret = NO_ERROR;
    struct v4l2_format format;
    memset(&format, 0,sizeof(struct v4l2_format));

    format.type = TVHAL_V4L2_BUF_TYPE;
    ret = ioctl(mHinDevHandle, VIDIOC_G_FMT, &format);
    if (ret < 0) {
        DEBUG_PRINT(3, "Open: VIDIOC_G_FMT Failed: %s", strerror(errno));
        return ret;
    }
    *width = format.fmt.pix.width;
    *height = format.fmt.pix.height;
    ALOGD("VIDIOC_G_FMT, w * h: %5d x %5d", *width,  *height);
    return ret;
}

int HinDevImpl::set_screen_mode(int mode)
{
    int ret = NO_ERROR;
    ret = ioctl(mHinDevHandle, VIDIOC_S_OUTPUT, &mode);
    if (ret < 0) {
        DEBUG_PRINT(3, "VIDIOC_S_OUTPUT Failed: %s", strerror(errno));
        return ret;
    }
    return ret;
}

int HinDevImpl::aquire_buffer()
{
    int ret = UNKNOWN_ERROR;
    DEBUG_PRINT(mDebugLevel, "%s %d", __FUNCTION__, __LINE__);
    for (int i = 0; i < mBufferCount; i++) {
        memset(&mHinNodeInfo->planes[i], 0, sizeof(struct v4l2_plane));
        memset(&mHinNodeInfo->bufferArray[i], 0, sizeof(struct v4l2_buffer));

        mHinNodeInfo->bufferArray[i].index = i;
        mHinNodeInfo->bufferArray[i].type = TVHAL_V4L2_BUF_TYPE;
        mHinNodeInfo->bufferArray[i].memory = TVHAL_V4L2_BUF_MEMORY;
        if (mHinNodeInfo->cap.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
            mHinNodeInfo->bufferArray[i].m.planes = &mHinNodeInfo->planes[i];
            mHinNodeInfo->bufferArray[i].length = PLANES_NUM;
        }

        ret = ioctl(mHinDevHandle, VIDIOC_QUERYBUF, &mHinNodeInfo->bufferArray[i]);
        if (ret < 0) {
            DEBUG_PRINT(3, "VIDIOC_QUERYBUF Failed, error: %s", strerror(errno));
            return ret;
        }

        ret = mSidebandWindow->allocateBuffer(&mHinNodeInfo->buffer_handle_poll[i]);
        if (ret != 0) {
            DEBUG_PRINT(3, "mSidebandWindow->allocateBuffer failed !!!");
            return ret;
        }
 
        if (mHinNodeInfo->cap.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
            for (int j=0; j<PLANES_NUM; j++) {
                mHinNodeInfo->bufferArray[i].m.planes[j].m.fd = mSidebandWindow->getBufferHandleFd(mHinNodeInfo->buffer_handle_poll[i]);
                mHinNodeInfo->bufferArray[i].m.planes[j].length = 0;                
            }
        }
    }
    ALOGD("[%s %d] VIDIOC_QUERYBUF successful", __FUNCTION__, __LINE__);
    return -1;
}

int HinDevImpl::release_buffer()
{
    DEBUG_PRINT(mDebugLevel, "%s %d", __FUNCTION__, __LINE__);
    for (int i=0; i<mBufferCount; i++) {
        mSidebandWindow->freeBuffer(&mHinNodeInfo->buffer_handle_poll[i]);
    }

    if (mSidebandHandle) {
        mSidebandWindow->freeBuffer(&mSidebandHandle);
        mSidebandHandle = NULL;
    }
    return -1;
}
bool writeData = false, writeOutBuffData = false;
static int nNums = 0;
int HinDevImpl::requestOneGrahicsBufferData(buffer_handle_t rawHandle) {
    int ret;
    int bufferIndex = -1;

    if (mHinNodeInfo->currBufferHandleIndex == SIDEBAND_WINDOW_BUFF_CNT)
        mHinNodeInfo->currBufferHandleIndex = mHinNodeInfo->currBufferHandleIndex % SIDEBAND_WINDOW_BUFF_CNT;

    ret = ioctl(mHinDevHandle, VIDIOC_DQBUF, &mHinNodeInfo->bufferArray[mHinNodeInfo->currBufferHandleIndex]);
    if (ret < 0) {
        DEBUG_PRINT(3, "VIDIOC_DQBUF Failed, error: %s", strerror(errno));
        return -1;
    } else {
        DEBUG_PRINT(mDebugLevel, "VIDIOC_DQBUF successful.");
    }

//     unsigned char *dest = NULL;
//     void *dstDataPtr = NULL;
//     int dstDataSize = -1;
//     buffer_handle_t outBufferHandlePtr = NULL;
//     // int handle_fd = mSidebandWindow->importHidlHandleBuffer(rawHandle, &outBufferHandlePtr);
//     int handle_fd = mSidebandWindow->registerHidlHandleBuffer(rawHandle, &outBufferHandlePtr);
//     ALOGD("%s outBufferHandlePtr = %p, handle_fd=%d", __FUNCTION__, outBufferHandlePtr, handle_fd);
//     if (!outBufferHandlePtr || handle_fd == -1) {
//         ALOGD("importHandleBuffer FAILED!!!");
//         return -1;
//     }
//     mSidebandWindow->getBufferDataLocked(mHinNodeInfo->buffer_handle_poll[mHinNodeInfo->currBufferHandleIndex], &dstDataPtr, &dstDataSize);
//     if (dstDataPtr == NULL) {
//         ALOGE("%s failed", __FUNCTION__);
//         return -1;
//     }
//     unsigned long vir_addr =  reinterpret_cast<unsigned long>(dstDataPtr);
//     tvinput::RgaCropScale::rga_nv12_scale_crop(
//         1920, 1080, vir_addr, handle_fd,
//         1920, 1080, 100, false, true,
//         false, true,
//         true);

// if (!writeOutBuffData) {
//     writeOutBuffData = true;
//     char fileName[128] = {0};
//     sprintf(fileName, "/data/system/tv_input_result_dump_%dx%d_%d.yuv", mFrameWidth, mFrameHeight, nNums);
//     mSidebandWindow->dumpImage(outBufferHandlePtr, fileName, 0);
//     nNums++;
// }

    mSidebandWindow->buffCopy(mHinNodeInfo->buffer_handle_poll[mHinNodeInfo->currBufferHandleIndex], rawHandle);
    ALOGV("%s end.", __FUNCTION__);
    return mHinNodeInfo->currBufferHandleIndex;
}

void HinDevImpl::releaseOneGraphicsBuffer(int bufferHandleIndex) {
    int ret;
    if (bufferHandleIndex == -1)
        bufferHandleIndex = mHinNodeInfo->currBufferHandleIndex;
    // mSidebandWindow->unLockBufferHandle(mHinNodeInfo->buffer_handle_poll[bufferHandleIndex]);
    ret = ioctl(mHinDevHandle, VIDIOC_QBUF, &mHinNodeInfo->bufferArray[bufferHandleIndex]);
    if (ret != 0) {
        DEBUG_PRINT(3, "VIDIOC_QBUF Buffer failed %s", strerror(errno));
    } else {
        DEBUG_PRINT(mDebugLevel, "VIDIOC_QBUF successful.");
    }
    mHinNodeInfo->currBufferHandleIndex++;
}

int HinDevImpl::workThread()
{
    DEBUG_PRINT(mDebugLevel, "HinDevImpl::workThread()");
    int ret;

    if (mState == START) {
        if (mHinNodeInfo->currBufferHandleIndex == SIDEBAND_WINDOW_BUFF_CNT)
             mHinNodeInfo->currBufferHandleIndex = mHinNodeInfo->currBufferHandleIndex % SIDEBAND_WINDOW_BUFF_CNT;

        DEBUG_PRINT(mDebugLevel, "%s %d currBufferHandleIndex = %d", __FUNCTION__, __LINE__, mHinNodeInfo->currBufferHandleIndex);
 
        ret = ioctl(mHinDevHandle, VIDIOC_DQBUF, &mHinNodeInfo->bufferArray[mHinNodeInfo->currBufferHandleIndex]);
        if (ret < 0) {
            DEBUG_PRINT(3, "VIDIOC_DQBUF Failed, error: %s", strerror(errno));
            return -1;
        } else {
            DEBUG_PRINT(mDebugLevel, "VIDIOC_DQBUF successful.");
        }

        if (mSkipFrame <= 0) {
#ifdef DUMP_YUV_IMG
            if (mDumpType == 0 && mDumpFrameCount > 0) {
                char fileName[128] = {0};
                sprintf(fileName, "/data/system/tv_input_dump_%dx%d_%d.yuv", mFrameWidth, mFrameHeight, mDumpFrameCount);
                mSidebandWindow->dumpImage(mHinNodeInfo->buffer_handle_poll[mHinNodeInfo->currBufferHandleIndex], fileName, 0);
                mDumpFrameCount--;
            } else if (mDumpType == 1 && mDumpFrameCount > 0) {
                char fileName[128] = {0};
                sprintf(fileName, "/data/system/tv_input_dump_%dx%d.h264", mFrameWidth, mFrameHeight);
                mSidebandWindow->dumpImage(mHinNodeInfo->buffer_handle_poll[mHinNodeInfo->currBufferHandleIndex], fileName, 0);
                mDumpFrameCount--;
            }
#endif
            mSidebandWindow->goDisplay(mHinNodeInfo->buffer_handle_poll[mHinNodeInfo->currBufferHandleIndex]);
        } else if (mSkipFrame > 0) {
            mSkipFrame--;
        }
        debugShowFPS();
        ret = ioctl(mHinDevHandle, VIDIOC_QBUF, &mHinNodeInfo->bufferArray[mHinNodeInfo->currBufferHandleIndex]);
        if (ret != 0) {
            DEBUG_PRINT(3, "VIDIOC_QBUF Buffer failed %s", strerror(errno));
        } else {
            DEBUG_PRINT(mDebugLevel, "VIDIOC_QBUF successful.");
        }
        mHinNodeInfo->currBufferHandleIndex++;
    }
    return NO_ERROR;
}

void HinDevImpl::debugShowFPS() {
    if (mShowFps == 0)
        return;
    static int mFrameCount = 0;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    if (!(mFrameCount & 0x1F)) {
        nsecs_t now = systemTime();
        nsecs_t diff = now - mLastFpsTime;
        mFps = ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
        DEBUG_PRINT(3, "tvinput: %d Frames, %2.3f FPS", mFrameCount, mFps);
    }
}

