/*
Copyright (c) 2011, Code Aurora Forum. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of Code Aurora Forum, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include "camera.h"
#include "mm_camera_dbg.h"
#include <time.h>
#include "mm_camera_interface2.h"

#define MM_CAMERA_PROFILE 1

struct file;
struct inode;
struct vm_area_struct;

/*===========================================================================
 * FUNCTION    - do_mmap -
 *
 * DESCRIPTION:  retured virtual addresss
 *==========================================================================*/
uint8_t *mm_camera_do_mmap(uint32_t size, int *pmemFd)
{
  void *ret; /* returned virtual address */
  int  pmem_fd = open("/dev/pmem_adsp", O_RDWR|O_SYNC);

  if (pmem_fd < 0) {
    CDBG("do_mmap: Open device /dev/pmem_adsp failed!\n");
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
    CDBG("do_mmap: pmem mmap() failed: %s (%d)\n", strerror(errno), errno);
    return NULL;
  }

  CDBG("do_mmap: pmem mmap fd %d ptr %p len %u\n", pmem_fd, ret, size);

  *pmemFd = pmem_fd;
  return(uint8_t *)ret;
}

/*===========================================================================
 * FUNCTION    - do_munmap -
 *
 * DESCRIPTION:
 *==========================================================================*/
int mm_camera_do_munmap(int pmem_fd, void *addr, size_t size)
{
  int rc;

	if (pmem_fd <= 0) {
		CDBG("%%s:invalid fd=%d\n", __func__, pmem_fd);
		return -1;
	}
  size = (size + 4095) & (~4095);

  CDBG("munmapped size = %d, virt_addr = 0x%p\n",
    size, addr);

  rc = (munmap(addr, size));

  close(pmem_fd);

  CDBG("do_mmap: pmem munmap fd %d ptr %p len %u rc %d\n", pmem_fd, addr,
    size, rc);

  return rc;
}

/*============================================================
   FUNCTION mm_camera_dump_image
   DESCRIPTION:
==============================================================*/
int mm_camera_dump_image(void *addr, uint32_t size, char *filename)
{
  int file_fd = open(filename, O_RDWR | O_CREAT, 0777);

  if (file_fd < 0) {
    CDBG_HIGH("%s: cannot open file\n", __func__);
		return -1;
	} else
    write(file_fd, addr, size);
  close(file_fd);
	CDBG("%s: %s, size=%d\n", __func__, filename, size);
	return 0;
}

uint32_t mm_camera_get_msm_frame_len(cam_format_t fmt_type, 
																		 camera_mode_t mode, int w, int h, 
																		 uint32_t *y_off, 
																		 uint32_t *cbcr_off, 
																		 mm_camera_pad_type_t cbcr_pad)
{
	uint32_t len = 0;
	
	 
	*y_off = 0;
	*cbcr_off = 0;	
	switch(fmt_type) {
	case CAMERA_YUV_420_NV12:
	case CAMERA_YUV_420_NV21:
		if(CAMERA_MODE_3D == mode) {
			len = (uint32_t)(PAD_TO_2K(w*h)*3/2);
			*y_off = 0;
			*cbcr_off = PAD_TO_WORD(w*h);
		} else if(CAMERA_MODE_2D == mode) {
			if(MM_CAMERA_PAD_2K == cbcr_pad) {
				len = (uint32_t)(PAD_TO_2K(w*h)+PAD_TO_2K(w*h/2));
				*y_off = 0;
				*cbcr_off = PAD_TO_2K(w*h);
			} else {
			len = (uint32_t)(w*h*3/2);
			*y_off = 0;
			*cbcr_off = PAD_TO_WORD(w*h);
		}
		}
		break;
	case CAMERA_BAYER_SBGGR10:
		len = (uint32_t)PAD_TO_WORD(w*h);
		*y_off = 0;
		*cbcr_off = PAD_TO_WORD(w*h);
		break;
	case CAMERA_YUV_420_NV21_ADRENO:
	default:
		break;
	}
	CDBG("%s:fmt=%d,mode=%d,w=%d,h=%d,frame_len=%d\n", __func__,fmt_type,mode,w,h,len);
	return len;
}

void mm_camera_util_profile(const char *str)
{
  #if (MM_CAMERA_PROFILE)
  struct timespec cur_time;

  clock_gettime(CLOCK_REALTIME, &cur_time);
  CDBG_HIGH("PROFILE %s: %ld.%09ld\n", str,
    cur_time.tv_sec, cur_time.tv_nsec);
  #endif
}


