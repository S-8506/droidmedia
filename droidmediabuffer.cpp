/*
 * Copyright (C) 2014-2015 Jolla Ltd.
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
 *
 * Authored by: Mohammed Hassan <mohammed.hassan@jolla.com>
 */

#include "droidmediabuffer.h"
#include "private.h"

#undef LOG_TAG
#define LOG_TAG "DroidMediaBuffer"

#if ANDROID_MAJOR < 5
static const int staleBuffer = android::BufferQueue::STALE_BUFFER_SLOT;
#else
static const int staleBuffer = android::IGraphicBufferConsumer::STALE_BUFFER_SLOT;
#endif

#if ANDROID_MAJOR < 6
_DroidMediaBuffer::_DroidMediaBuffer(android::BufferQueue::BufferItem& buffer,
#else
_DroidMediaBuffer::_DroidMediaBuffer(android::BufferItem& buffer,
#endif
				     android::sp<DroidMediaBufferQueue> queue,
				     void *data,
				     DroidMediaCallback ref,
				     DroidMediaCallback unref) :
    m_buffer(buffer.mGraphicBuffer),
    m_queue(queue),
    m_transform(buffer.mTransform),
    m_scalingMode(buffer.mScalingMode),
    m_timestamp(buffer.mTimestamp),
    m_frameNumber(buffer.mFrameNumber),
    m_crop(buffer.mCrop),
#if ANDROID_MAJOR >= 6
    m_slot(buffer.mSlot),
#else
    m_slot(buffer.mBuf),
#endif
    m_data(data),
    m_ref(ref),
    m_unref(unref)
{
    width  = buffer.mGraphicBuffer->width;
    height = buffer.mGraphicBuffer->height;
    stride = buffer.mGraphicBuffer->stride;
    format = buffer.mGraphicBuffer->format;
    usage  = buffer.mGraphicBuffer->usage;
    handle = buffer.mGraphicBuffer->handle;

    common.incRef = incRef;
    common.decRef = decRef;
}

_DroidMediaBuffer::_DroidMediaBuffer(android::sp<android::GraphicBuffer>& buffer,
				     void *data,
				     DroidMediaCallback ref,
				     DroidMediaCallback unref) :
    m_buffer(buffer),
    m_transform(-1),
    m_scalingMode(-1),
    m_timestamp(-1),
    m_frameNumber(-1),
    m_slot(-1),
    m_data(data),
    m_ref(ref),
    m_unref(unref)
{
    width  = m_buffer->width;
    height = m_buffer->height;
    stride = m_buffer->stride;
    format = m_buffer->format;
    usage  = m_buffer->usage;
    handle = m_buffer->handle;

    common.incRef = incRef;
    common.decRef = decRef;
}

_DroidMediaBuffer::~_DroidMediaBuffer()
{

}

void _DroidMediaBuffer::incRef(struct android_native_base_t* base)
{
    DroidMediaBuffer *self = reinterpret_cast<DroidMediaBuffer *>(base);
    self->m_ref(self->m_data);
}

void _DroidMediaBuffer::decRef(struct android_native_base_t* base)
{
    DroidMediaBuffer *self = reinterpret_cast<DroidMediaBuffer *>(base);
    self->m_unref(self->m_data);
}

extern "C" {
DroidMediaBuffer *droid_media_buffer_create_from_raw_data(uint32_t w, uint32_t h,
							  uint32_t strideY, uint32_t strideUV,
							  uint32_t format,
							  DroidMediaData *data,
							  DroidMediaBufferCallbacks *cb)
{
  void *addr = NULL;
  uint32_t linesY, linesUV,
           lineWidthY, lineWidthUV,
           dstStrideY, dstStrideUV;
  uint8_t *dst;
  uint8_t *src;

  // Make sure the format is supported before continue
  switch (format) {
  case HAL_PIXEL_FORMAT_RGBA_8888:
  case HAL_PIXEL_FORMAT_RGBX_8888:
  case HAL_PIXEL_FORMAT_BGRA_8888:
  case HAL_PIXEL_FORMAT_RGB_888:
  case HAL_PIXEL_FORMAT_RGB_565:
  case HAL_PIXEL_FORMAT_YCbCr_422_I:
  case HAL_PIXEL_FORMAT_YV12:
  case HAL_PIXEL_FORMAT_YCbCr_422_SP:
  case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    break;
  default:
    ALOGE("Unsupported format 0x%x", format);
    return NULL;
  }

  android::sp<android::GraphicBuffer>
    buffer(new android::GraphicBuffer(w, h, format,
				      android::GraphicBuffer::USAGE_HW_TEXTURE));

  android::status_t err = buffer->initCheck();

  if (err != android::NO_ERROR) {
    ALOGE("Error 0x%x allocating buffer", -err);
    buffer.clear();
    return NULL;
  }

  err = buffer->lock(android::GraphicBuffer::USAGE_SW_READ_RARELY
		     | android::GraphicBuffer::USAGE_SW_WRITE_RARELY, &addr);
  if (err != android::NO_ERROR) {
    ALOGE("Error 0x%x locking buffer", -err);
    buffer.clear();
    return NULL;
  }

  switch (format) {
  case HAL_PIXEL_FORMAT_RGBA_8888:
  case HAL_PIXEL_FORMAT_RGBX_8888:
  case HAL_PIXEL_FORMAT_BGRA_8888:
    linesY = h;
    lineWidthY = 4 * w;
    dstStrideY = 4 * buffer->getStride();
    break;
  case HAL_PIXEL_FORMAT_RGB_888:
    linesY = h;
    lineWidthY = 3 * w;
    dstStrideY = 3 * buffer->getStride();
    break;
  case HAL_PIXEL_FORMAT_RGB_565:
  case HAL_PIXEL_FORMAT_YCbCr_422_I:
    linesY = h;
    lineWidthY = 2 * w;
    dstStrideY = 2 * buffer->getStride();
    break;
  case HAL_PIXEL_FORMAT_YV12:
  case HAL_PIXEL_FORMAT_YCbCr_422_SP:
  case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    linesY = h;
    lineWidthY = w;
    dstStrideY = buffer->getStride();
    break;
  }

  switch (format) {
  case HAL_PIXEL_FORMAT_YV12:
    // U plane is followed by V plane. So let's do 2-in-1 by not halving h.
    linesUV = h;
    lineWidthUV = w / 2;
    dstStrideUV = dstStrideY / 2;
    break;
  case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    linesUV = h; // Twice the lines as 4:2:0.
    lineWidthUV = w;
    dstStrideUV = dstStrideY;
    break;
  case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    linesUV = h / 2;
    lineWidthUV = w;
    dstStrideUV = dstStrideY;
    break;
  default: // RGB, and YCbCr_422_I
    if (strideUV != 0) {
      ALOGW("Get unexpected 2nd plane for the format that shouldn't have one.");
      // In this case, the global memcpy below should still happen if the caller
      // erroneously passed a strideUV for an RGB format. Setting strideUV to 0
      // allow this to happen.
      strideUV = 0;
    }

    linesUV = 0;
    lineWidthUV = 0;
    dstStrideUV = 0;
    break;
  }

  if (abs((int32_t)dstStrideUV - (int32_t)strideUV) > 128) {
    ALOGW("dstStride and (src) strideUV is too much apart. Program might get SIGSEGV."
          "(format = %d, dstStrideUV = %d, strideUV = %d)", format, dstStrideUV, strideUV);
  }

  if (strideY == dstStrideY && strideUV == dstStrideUV) {
    memcpy(addr, data->data, data->size);
    goto out;
  }

  dst = (uint8_t *)addr;
  src = (uint8_t *)data->data;

  // First plane
  while (linesY-- > 0) {
    memcpy(dst, src, lineWidthY);
    dst += dstStrideY;
    src += strideY;
  }

  // Second (and third) plane(s)
  while (linesUV-- > 0) {
    memcpy(dst, src, lineWidthUV);
    dst += dstStrideUV;
    src += strideUV;
  }

out:
  err = buffer->unlock();
  if (err != android::NO_ERROR) {
    ALOGE("Error 0x%x unlocking buffer", -err);
    buffer.clear();
    return NULL;
  }

  return new DroidMediaBuffer(buffer, cb->data, cb->ref, cb->unref);
}

DroidMediaBuffer *droid_media_buffer_create(uint32_t w, uint32_t h,
					    uint32_t format,
					    DroidMediaBufferCallbacks *cb)
{
  android::sp<android::GraphicBuffer>
    buffer(new android::GraphicBuffer(w, h, format,
				      android::GraphicBuffer::USAGE_HW_TEXTURE));

  android::status_t err = buffer->initCheck();

  if (err != android::NO_ERROR) {
    ALOGE("Error 0x%x allocating buffer", -err);
    buffer.clear();
    return NULL;
  }

  return new DroidMediaBuffer(buffer, cb->data, cb->ref, cb->unref);
}


void droid_media_buffer_release(DroidMediaBuffer *buffer,
                                EGLDisplay display, EGLSyncKHR fence)
{
    if (buffer->m_queue == NULL) {
      // TODO: what should we do with fence?
      delete buffer;
      return;
    }

    int err = buffer->m_queue->releaseMediaBuffer(buffer, display, fence);

    switch (err) {
    case android::NO_ERROR:
        break;

    case staleBuffer:
        ALOGW("Released stale buffer %d", buffer->m_slot);
        break;

    default:
        ALOGE("Error 0x%x releasing buffer %d", -err, buffer->m_slot);
        break;
    }

    delete buffer;
}

void *droid_media_buffer_lock(DroidMediaBuffer *buffer, uint32_t flags)
{
  int usage = 0;
  void *addr = NULL;
  android::status_t err;

  if (flags & DROID_MEDIA_BUFFER_LOCK_READ) {
    usage |= android::GraphicBuffer::USAGE_SW_READ_RARELY;
  }
  if (flags & DROID_MEDIA_BUFFER_LOCK_WRITE) {
    usage |= android::GraphicBuffer::USAGE_SW_WRITE_RARELY;
  }

  err = buffer->m_buffer->lock(usage, &addr);

  if (err != android::NO_ERROR) {
    ALOGE("Error 0x%x locking buffer", -err);
    return NULL;
  } else {
    return addr;
  }
}

void droid_media_buffer_unlock(DroidMediaBuffer *buffer)
{
  android::status_t err = buffer->m_buffer->unlock();

  if (err != android::NO_ERROR) {
    ALOGE("Error 0x%x unlocking buffer", -err);
  }
}

uint32_t droid_media_buffer_get_transform(DroidMediaBuffer * buffer)
{
    return buffer->m_transform;
}

uint32_t droid_media_buffer_get_scaling_mode(DroidMediaBuffer * buffer)
{
    return buffer->m_scalingMode;
}

int64_t droid_media_buffer_get_timestamp(DroidMediaBuffer * buffer)
{
    return buffer->m_timestamp;
}

uint64_t droid_media_buffer_get_frame_number(DroidMediaBuffer * buffer)
{
    return buffer->m_frameNumber;
}

DroidMediaRect droid_media_buffer_get_crop_rect(DroidMediaBuffer * buffer)
{
    DroidMediaRect rect;
    rect.left = buffer->m_crop.left;
    rect.right = buffer->m_crop.right;
    rect.top = buffer->m_crop.top;
    rect.bottom = buffer->m_crop.bottom;

    return rect;
}

uint32_t droid_media_buffer_get_width(DroidMediaBuffer * buffer)
{
    return buffer->width;
}

uint32_t droid_media_buffer_get_height(DroidMediaBuffer * buffer)
{
    return buffer->height;
}

const void *droid_media_buffer_get_handle(DroidMediaBuffer *buffer)
{
    return buffer->handle;
}

void droid_media_buffer_get_info(DroidMediaBuffer *buffer, DroidMediaBufferInfo *info)
{
    info->width = buffer->width;
    info->height = buffer->height;
    info->transform = buffer->m_transform;
    info->scaling_mode = buffer->m_scalingMode;
    info->timestamp = buffer->m_timestamp;
    info->frame_number = buffer->m_frameNumber;
    info->crop_rect = droid_media_buffer_get_crop_rect(buffer);
    info->format = buffer->format;
    info->stride = buffer->stride;
}

};
