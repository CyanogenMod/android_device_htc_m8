/*
** Copyright 2008, Google Inc.
** Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_QCAMERA_STREAM_H
#define ANDROID_HARDWARE_QCAMERA_STREAM_H


#include <utils/threads.h>

#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <utils/threads.h>

#include "QCameraHWI.h"
#include "QCameraHWI_Mem.h"

extern "C" {

#include <camera.h>
#include <camera_defs_i.h>
#include <mm_camera_interface2.h>

#define DEFAULT_STREAM_WIDTH 320
#define DEFAULT_STREAM_HEIGHT 240

#define MM_CAMERA_CH_PREVIEW_MASK    (0x01 << MM_CAMERA_CH_PREVIEW)
#define MM_CAMERA_CH_VIDEO_MASK      (0x01 << MM_CAMERA_CH_VIDEO)
#define MM_CAMERA_CH_SNAPSHOT_MASK   (0x01 << MM_CAMERA_CH_SNAPSHOT)

static uint8_t *mm_do_mmap(uint32_t size, int *pmemFd)
{
  void *ret; /* returned virtual address */
  int  pmem_fd = open("/dev/pmem_adsp", O_RDWR|O_SYNC);

  if (pmem_fd < 0) {
    LOGE("do_mmap: Open device /dev/pmem_adsp failed!\n");
    return NULL;
  }

  /* to make it page size aligned */
  size = (size + 4095) & (~4095);

  ret = mmap(NULL,
    size,
    PROT_READ  | PROT_WRITE,
    MAP_SHARED,
    pmem_fd,
    0);

  if (ret == MAP_FAILED) {
    LOGE("do_mmap: pmem mmap() failed: %s (%d)\n", strerror(errno), errno);
    return NULL;
  }

  LOGE("do_mmap: pmem mmap fd %d ptr %p len %u\n", pmem_fd, ret, size);

  *pmemFd = pmem_fd;
  return(uint8_t *)ret;
}

} /* extern C*/


namespace android {

class QCameraHardwareInterface;

class StreamQueue {
private:
    Mutex mQueueLock;
    Condition mQueueWait;
    bool mInitialized;

    //Vector<struct msm_frame *> mContainer;
    Vector<void *> mContainer;
public:
    StreamQueue();
    virtual ~StreamQueue();
    bool enqueue(void *element);
    void flush();
    void* dequeue();
    void init();
    void deinit();
    bool isInitialized();
bool isEmpty();
};


class QCameraStream { //: public virtual RefBase{

public:
    bool mInit;
    bool mActive;

    virtual status_t    init();
    virtual status_t    start();
    virtual void        stop();
    virtual void        release();

    virtual void        setHALCameraControl(QCameraHardwareInterface* ctrl);

    virtual void        usedData(void*);
    virtual void        newData(void*);
    virtual void*       getUsedData();

    //static status_t     openChannel(mm_camera_t *, mm_camera_channel_type_t ch_type);
    virtual status_t    initChannel(mm_camera_t *native_camera, uint32_t ch_type_mask);
    virtual status_t    deinitChannel(mm_camera_t *native_camera, mm_camera_channel_type_t ch_type);
    virtual void releaseRecordingFrame(const sp<IMemory>& mem)
    {
      ;
    }
    virtual status_t getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize)
    {
      return NO_ERROR;
    }
    virtual void prepareHardware()
    {
      ;
    }
    virtual sp<IMemoryHeap> getHeap() const{return NULL;}
    virtual status_t    initDisplayBuffers(){return NO_ERROR;}
    virtual sp<IMemoryHeap> getRawHeap() const {return NULL;}
    QCameraStream();
    virtual             ~QCameraStream();
    QCameraHardwareInterface*  mHalCamCtrl;
private:
   StreamQueue mBusyQueue;
   StreamQueue mFreeQueue;
};

/*
*   Record Class
*/
class QCameraStream_record : public QCameraStream {
public:
  status_t    init();
  status_t    start() ;
  void        stop()  ;
  void        release() ;

  static QCameraStream*  createInstance(mm_camera_t *, camera_mode_t);
  static void            deleteInstance(QCameraStream *p);

  QCameraStream_record() {};
  virtual             ~QCameraStream_record();

  status_t processRecordFrame(void *data);
  status_t initEncodeBuffers();
  status_t getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize);
  sp<IMemoryHeap> getHeap() const;

  void releaseRecordingFrame(const sp<IMemory>& mem);
  void debugShowVideoFPS() const;


private:
  QCameraStream_record(mm_camera_t *, camera_mode_t);

  mm_camera_t         *mmCamera;
  cam_ctrl_dimension_t dim;
  camera_mode_t        myMode;
  int                  open_flag;
  bool mDebugFps;

  mm_camera_reg_buf_t mRecordBuf;
  int record_frame_len;

  sp<PmemPool> mRecordHeap;
  struct msm_frame *recordframes;
  uint32_t record_offset[VIDEO_BUFFER_COUNT];
  Mutex mRecordFreeQueueLock;
  Vector<mm_camera_ch_data_buf_t> mRecordFreeQueue;

};


#if 0
class QCameraStream_noneZSL : public QCameraStream {
public:
    status_t    init(mm_camera_reg_buf_t*);
    status_t    start() ;
    void        stop()  ;
    void        release() ;

    void        usedData(void*);
    void        newData(void*);
    void*       getUsedData();

    void        useData(void*);

    static QCameraStream*  createInstance(mm_camera_t *, camera_mode_t);
    static void            deleteInstance(QCameraStream *p);

    QCameraStream_noneZSL() {};
    virtual             ~QCameraStream_noneZSL();

    status_t processPreviewFrame(void *data);

private:
    QCameraStream_noneZSL(mm_camera_t *, camera_mode_t);

    mm_camera_t         *mmCamera;
    bool                mActive;
    int8_t              my_id;
    mm_camera_op_mode_type_t op_mode;
    cam_ctrl_dimension_t dim;
    camera_mode_t        myMode;
    int                  open_flag;

    /*Channel Type : Preview, Video, Snapshot*/
    int8_t ch_type;

};

#endif
class QCameraStream_preview : public QCameraStream {
public:
    status_t    init();
    status_t    start() ;
    void        stop()  ;
    void        release() ;

    void        usedData(void*);
    void        newData(void*);
    void*       getUsedData();

    void        useData(void*);

    static QCameraStream*  createInstance(mm_camera_t *, camera_mode_t);
  static void            deleteInstance(QCameraStream *p);

    QCameraStream_preview() {};
    virtual             ~QCameraStream_preview();
    status_t initDisplayBuffers();
    status_t processPreviewFrame(mm_camera_ch_data_buf_t *frame);
    friend class QCameraHardwareInterface;

private:
                        QCameraStream_preview(mm_camera_t *, camera_mode_t);

    mm_camera_t *mmCamera;
    camera_mode_t myMode;
    int8_t              my_id;
    mm_camera_op_mode_type_t op_mode;
    cam_ctrl_dimension_t dim;
    int                  open_flag;
    mm_camera_reg_buf_t mDisplayBuf;
    mm_cameara_stream_buf_t mDisplayStreamBuf;

};

#if 0
class QCameraStream_ZSL : public QCameraStream_noneZSL {
public:
    status_t    init(mm_camera_reg_buf_t*);
    //status_t    start() ;
    //void        stop()  ;
    void        release() ;
    static sp<QCameraStream> createInstance(mm_camera_t *, int);

private:
                        QCameraStream_ZSL(mm_camera_t *, camera_mode_t);
    virtual             ~QCameraStream_ZSL();
}; // QCameraStream_ZSL
#endif

/* Snapshot Class - handle data flow*/
class QCameraStream_Snapshot : public QCameraStream {
public:
    status_t    init();
    status_t    start();
    void        stop();
    void        release();
    void        prepareHardware();
    static QCameraStream* createInstance(mm_camera_t *, camera_mode_t);
    static void deleteInstance(QCameraStream *p);

    /* Member functions for static callbacks */
    void receiveRawPicture(mm_camera_ch_data_buf_t* recvd_frame);
    void receiveCompleteJpegPicture(jpeg_event_t event);
    void receiveJpegFragment(uint8_t *ptr, uint32_t size);
    void deinit(bool have_to_release);
    sp<IMemoryHeap> getRawHeap() const;
    int getSnapshotState();
    /*Temp: Bikas: to be removed once event handling is enabled in mm-camera*/
    void runSnapshotThread(void *data);

    /* public members */
    mm_camera_t *mmCamera;

private:
    QCameraStream_Snapshot(mm_camera_t *, camera_mode_t);
	virtual ~QCameraStream_Snapshot();

    /* snapshot related private members */
    status_t initSnapshot(int num_of_snapshots);
    status_t initRawSnapshot(int num_of_snapshots);
    status_t initZSLSnapshot(void);
    status_t cancelPicture();
    void notifyShutter(common_crop_t *crop,
                       bool play_shutter_sound);
    status_t initSnapshotBuffers(cam_ctrl_dimension_t *dim,
                                 int num_of_buf);
    status_t initRawSnapshotBuffers(cam_ctrl_dimension_t *dim,
                                    int num_of_buf);
    status_t deinitRawSnapshotBuffers(void);
    status_t deinitSnapshotBuffers(void);
    status_t initRawSnapshotChannel(cam_ctrl_dimension_t* dim,
                                    mm_camera_raw_streaming_type_t type);
    status_t initSnapshotChannel(cam_ctrl_dimension_t *dim);
    status_t takePictureRaw(void);
    status_t takePictureJPEG(void);
    status_t takePictureZSL(void);
    void deinitSnapshotChannel(mm_camera_channel_type_t);
    status_t configSnapshotDimension(cam_ctrl_dimension_t* dim);
    status_t encodeData(mm_camera_ch_data_buf_t* recvd_frame);
    status_t encodeDisplayAndSave(mm_camera_ch_data_buf_t* recvd_frame);
    void handleError();
    void setSnapshotState(int state);
    bool isZSLMode();


    /* Member variables */
    int mSnapshotFormat;
    int mPictureWidth;
    int mPictureHeight;
    int mPostviewWidth;
    int mPostviewHeight;
    int mThumbnailWidth;
    int mThumbnailHeight;
    int mSnapshotState;
    int mJpegOffset;
    int mNumOfSnapshot;
    camera_mode_t myMode;
    sp<AshmemPool> mJpegHeap;
    /*TBD:Bikas: This is defined in HWI too.*/
    sp<PmemPool>  mDisplayHeap;
    sp<PmemPool>  mRawHeap;
    sp<PmemPool>  mPostviewHeap;
    sp<PmemPool>  mRawSnapShotHeap;
    
    mm_camera_ch_data_buf_t *mCurrentFrameEncoded;
    mm_cameara_stream_buf_t mSnapshotStreamBuf;
    mm_cameara_stream_buf_t mPostviewStreamBuf;
    StreamQueue mSnapshotQueue;
}; // QCameraStream_Snapshot


}; // namespace android

#endif
