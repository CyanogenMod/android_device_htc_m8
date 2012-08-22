/****************************************************************************
 ****************************************************************************
 ***
 ***   This header was automatically generated from a Linux kernel header
 ***   of the same name, to make information necessary for userspace to
 ***   call into the kernel available to libc.  It contains only constants,
 ***   structures, and macros generated from the original header, and thus,
 ***   contains no copyrightable information.
 ***
 ***   To edit the content of this header, modify the corresponding
 ***   source file (e.g. under external/kernel-headers/original/) then
 ***   run bionic/libc/kernel/tools/update_all.py
 ***
 ***   Any manual change here will be lost the next time this script will
 ***   be run. You've been warned!
 ***
 ****************************************************************************
 ****************************************************************************/
#ifndef _MSM_KGSL_H
#define _MSM_KGSL_H
#define KGSL_VERSION_MAJOR 3
#ifndef KGSL_VERSION_MINOR
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define KGSL_VERSION_MINOR 8
#endif
#define KGSL_CONTEXT_SAVE_GMEM 1
#define KGSL_CONTEXT_NO_GMEM_ALLOC 2
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define KGSL_CONTEXT_SUBMIT_IB_LIST 4
#define KGSL_CONTEXT_CTX_SWITCH 8
#define KGSL_CONTEXT_PREAMBLE 16
#define KGSL_MEMFLAGS_GPUREADONLY 0x01000000
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define KGSL_FLAGS_NORMALMODE 0x00000000
#define KGSL_FLAGS_SAFEMODE 0x00000001
#define KGSL_FLAGS_INITIALIZED0 0x00000002
#define KGSL_FLAGS_INITIALIZED 0x00000004
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define KGSL_FLAGS_STARTED 0x00000008
#define KGSL_FLAGS_ACTIVE 0x00000010
#define KGSL_FLAGS_RESERVED0 0x00000020
#define KGSL_FLAGS_RESERVED1 0x00000040
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define KGSL_FLAGS_RESERVED2 0x00000080
#define KGSL_FLAGS_SOFT_RESET 0x00000100
#define KGSL_CLK_SRC 0x00000001
#define KGSL_CLK_CORE 0x00000002
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define KGSL_CLK_IFACE 0x00000004
#define KGSL_CLK_MEM 0x00000008
#define KGSL_CLK_MEM_IFACE 0x00000010
#define KGSL_CLK_AXI 0x00000020
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define KGSL_MAX_PWRLEVELS 5
#define KGSL_CONVERT_TO_MBPS(val)   (val*1000*1000U)
enum kgsl_deviceid {
 KGSL_DEVICE_3D0 = 0x00000000,
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 KGSL_DEVICE_YAMATO = KGSL_DEVICE_3D0,
 KGSL_DEVICE_2D0 = 0x00000001,
 KGSL_DEVICE_2D1 = 0x00000002,
 KGSL_DEVICE_MAX = 0x00000003
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
enum kgsl_user_mem_type {
 KGSL_USER_MEM_TYPE_PMEM = 0x00000000,
 KGSL_USER_MEM_TYPE_ASHMEM = 0x00000001,
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 KGSL_USER_MEM_TYPE_ADDR = 0x00000002,
 KGSL_USER_MEM_TYPE_ION = 0x00000003,
 KGSL_USER_MEM_TYPE_MAX = 0x00000004,
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_devinfo {
 unsigned int device_id;
 unsigned int chip_id;
 unsigned int mmu_enabled;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int gmem_gpubaseaddr;
 unsigned int gpu_id;
 unsigned int gmem_sizebytes;
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_devmemstore {
 volatile unsigned int soptimestamp;
 unsigned int sbz;
 volatile unsigned int eoptimestamp;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int sbz2;
 volatile unsigned int ts_cmp_enable;
 unsigned int sbz3;
 volatile unsigned int ref_wait_ts;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int sbz4;
 unsigned int current_context;
 unsigned int sbz5;
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define KGSL_DEVICE_MEMSTORE_OFFSET(field)   offsetof(struct kgsl_devmemstore, field)
enum kgsl_timestamp_type {
 KGSL_TIMESTAMP_CONSUMED = 0x00000001,
 KGSL_TIMESTAMP_RETIRED = 0x00000002,
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 KGSL_TIMESTAMP_MAX = 0x00000002,
};
enum kgsl_property_type {
 KGSL_PROP_DEVICE_INFO = 0x00000001,
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 KGSL_PROP_DEVICE_SHADOW = 0x00000002,
 KGSL_PROP_DEVICE_POWER = 0x00000003,
 KGSL_PROP_SHMEM = 0x00000004,
 KGSL_PROP_SHMEM_APERTURES = 0x00000005,
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 KGSL_PROP_MMU_ENABLE = 0x00000006,
 KGSL_PROP_INTERRUPT_WAITS = 0x00000007,
 KGSL_PROP_VERSION = 0x00000008,
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_shadowprop {
 unsigned int gpuaddr;
 unsigned int size;
 unsigned int flags;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
struct kgsl_pwrlevel {
 unsigned int gpu_freq;
 unsigned int bus_freq;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int io_fraction;
};
struct kgsl_version {
 unsigned int drv_major;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int drv_minor;
 unsigned int dev_major;
 unsigned int dev_minor;
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_ibdesc {
 unsigned int gpuaddr;
 void *hostptr;
 unsigned int sizedwords;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int ctrl;
};
#define KGSL_IOC_TYPE 0x09
struct kgsl_device_getproperty {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int type;
 void *value;
 unsigned int sizebytes;
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define IOCTL_KGSL_DEVICE_GETPROPERTY   _IOWR(KGSL_IOC_TYPE, 0x2, struct kgsl_device_getproperty)
struct kgsl_device_regread {
 unsigned int offsetwords;
 unsigned int value;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
#define IOCTL_KGSL_DEVICE_REGREAD   _IOWR(KGSL_IOC_TYPE, 0x3, struct kgsl_device_regread)
struct kgsl_device_waittimestamp {
 unsigned int timestamp;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int timeout;
};
#define IOCTL_KGSL_DEVICE_WAITTIMESTAMP   _IOW(KGSL_IOC_TYPE, 0x6, struct kgsl_device_waittimestamp)
struct kgsl_ringbuffer_issueibcmds {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int drawctxt_id;
 unsigned int ibdesc_addr;
 unsigned int numibs;
 unsigned int timestamp;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int flags;
};
#define IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS   _IOWR(KGSL_IOC_TYPE, 0x10, struct kgsl_ringbuffer_issueibcmds)
struct kgsl_cmdstream_readtimestamp {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int type;
 unsigned int timestamp;
};
#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_OLD   _IOR(KGSL_IOC_TYPE, 0x11, struct kgsl_cmdstream_readtimestamp)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define IOCTL_KGSL_CMDSTREAM_READTIMESTAMP   _IOWR(KGSL_IOC_TYPE, 0x11, struct kgsl_cmdstream_readtimestamp)
struct kgsl_cmdstream_freememontimestamp {
 unsigned int gpuaddr;
 unsigned int type;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int timestamp;
};
#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP   _IOW(KGSL_IOC_TYPE, 0x12, struct kgsl_cmdstream_freememontimestamp)
#define IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP_OLD   _IOR(KGSL_IOC_TYPE, 0x12, struct kgsl_cmdstream_freememontimestamp)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_drawctxt_create {
 unsigned int flags;
 unsigned int drawctxt_id;
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define IOCTL_KGSL_DRAWCTXT_CREATE   _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
struct kgsl_drawctxt_destroy {
 unsigned int drawctxt_id;
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define IOCTL_KGSL_DRAWCTXT_DESTROY   _IOW(KGSL_IOC_TYPE, 0x14, struct kgsl_drawctxt_destroy)
struct kgsl_map_user_mem {
 int fd;
 unsigned int gpuaddr;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int len;
 unsigned int offset;
 unsigned int hostptr;
 enum kgsl_user_mem_type memtype;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int reserved;
};
#define IOCTL_KGSL_MAP_USER_MEM   _IOWR(KGSL_IOC_TYPE, 0x15, struct kgsl_map_user_mem)
struct kgsl_sharedmem_from_pmem {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 int pmem_fd;
 unsigned int gpuaddr;
 unsigned int len;
 unsigned int offset;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
#define IOCTL_KGSL_SHAREDMEM_FROM_PMEM   _IOWR(KGSL_IOC_TYPE, 0x20, struct kgsl_sharedmem_from_pmem)
struct kgsl_sharedmem_free {
 unsigned int gpuaddr;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
#define IOCTL_KGSL_SHAREDMEM_FREE   _IOW(KGSL_IOC_TYPE, 0x21, struct kgsl_sharedmem_free)
struct kgsl_cff_user_event {
 unsigned char cff_opcode;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int op1;
 unsigned int op2;
 unsigned int op3;
 unsigned int op4;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int op5;
 unsigned int __pad[2];
};
#define IOCTL_KGSL_CFF_USER_EVENT   _IOW(KGSL_IOC_TYPE, 0x31, struct kgsl_cff_user_event)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_gmem_desc {
 unsigned int x;
 unsigned int y;
 unsigned int width;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int height;
 unsigned int pitch;
};
struct kgsl_buffer_desc {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 void *hostptr;
 unsigned int gpuaddr;
 int size;
 unsigned int format;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int pitch;
 unsigned int enabled;
};
struct kgsl_bind_gmem_shadow {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int drawctxt_id;
 struct kgsl_gmem_desc gmem_desc;
 unsigned int shadow_x;
 unsigned int shadow_y;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 struct kgsl_buffer_desc shadow_buffer;
 unsigned int buffer_id;
};
#define IOCTL_KGSL_DRAWCTXT_BIND_GMEM_SHADOW   _IOW(KGSL_IOC_TYPE, 0x22, struct kgsl_bind_gmem_shadow)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_sharedmem_from_vmalloc {
 unsigned int gpuaddr;
 unsigned int hostptr;
 unsigned int flags;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
#define IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC   _IOWR(KGSL_IOC_TYPE, 0x23, struct kgsl_sharedmem_from_vmalloc)
#define IOCTL_KGSL_SHAREDMEM_FLUSH_CACHE   _IOW(KGSL_IOC_TYPE, 0x24, struct kgsl_sharedmem_free)
struct kgsl_drawctxt_set_bin_base_offset {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int drawctxt_id;
 unsigned int offset;
};
#define IOCTL_KGSL_DRAWCTXT_SET_BIN_BASE_OFFSET   _IOW(KGSL_IOC_TYPE, 0x25, struct kgsl_drawctxt_set_bin_base_offset)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
enum kgsl_cmdwindow_type {
 KGSL_CMDWINDOW_MIN = 0x00000000,
 KGSL_CMDWINDOW_2D = 0x00000000,
 KGSL_CMDWINDOW_3D = 0x00000001,
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 KGSL_CMDWINDOW_MMU = 0x00000002,
 KGSL_CMDWINDOW_ARBITER = 0x000000FF,
 KGSL_CMDWINDOW_MAX = 0x000000FF,
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_cmdwindow_write {
 enum kgsl_cmdwindow_type target;
 unsigned int addr;
 unsigned int data;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
#define IOCTL_KGSL_CMDWINDOW_WRITE   _IOW(KGSL_IOC_TYPE, 0x2e, struct kgsl_cmdwindow_write)
struct kgsl_gpumem_alloc {
 unsigned long gpuaddr;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 size_t size;
 unsigned int flags;
};
#define IOCTL_KGSL_GPUMEM_ALLOC   _IOWR(KGSL_IOC_TYPE, 0x2f, struct kgsl_gpumem_alloc)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct kgsl_cff_syncmem {
 unsigned int gpuaddr;
 unsigned int len;
 unsigned int __pad[2];
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
#define IOCTL_KGSL_CFF_SYNCMEM   _IOW(KGSL_IOC_TYPE, 0x30, struct kgsl_cff_syncmem)
struct kgsl_timestamp_event {
 int type;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 unsigned int timestamp;
 unsigned int context_id;
 void *priv;
 size_t len;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
#define IOCTL_KGSL_TIMESTAMP_EVENT   _IOW(KGSL_IOC_TYPE, 0x31, struct kgsl_timestamp_event)
#define KGSL_TIMESTAMP_EVENT_GENLOCK 1
struct kgsl_timestamp_event_genlock {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 int handle;
};
#endif

