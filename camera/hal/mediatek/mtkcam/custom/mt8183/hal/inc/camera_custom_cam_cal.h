/*
 * Copyright (C) 2019 MediaTek Inc.
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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_CAM_CAL_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_CAM_CAL_H_

#define CAM_CAL_ERR_NO_DEVICE 0x8FFFFFFF
#define CAM_CAL_ERR_NO_CMD 0x1FFFFFFF
#define CAM_CAL_ERR_NO_DATA                                                  \
  (CAM_CAL_ERR_NO_SHADING | CAM_CAL_ERR_NO_3A_GAIN | CAM_CAL_ERR_NO_3D_GEO | \
   CAM_CAL_ERR_NO_VERSION | CAM_CAL_ERR_NO_PARTNO)

#define CAM_CAL_LSC_DATA_SIZE_SLIM_LSC1_8x9 144    // (8x9x2)XMUINT32(4)
#define CAM_CAL_LSC_DATA_SIZE_SLIM_LSC1_16x16 512  // (16x16x2)XMUINT32(4)

#define CAM_CAL_INFO_IN_COMM_PARTNO_1 35
#define CAM_CAL_INFO_IN_COMM_PARTNO_2 36
#define CAM_CAL_INFO_IN_COMM_PARTNO_3 37
#define CAM_CAL_INFO_IN_COMM_PARTNO_4 38
#define CAM_CAL_INFO_IN_COMM_PARTNO_5 39

#define CAM_CAL_PART_NUMBERS_COUNT 6
#define LSC_DATA_BYTES 4
#define CAM_CAL_PART_NUMBERS_COUNT_BYTE \
  (CAM_CAL_PART_NUMBERS_COUNT * LSC_DATA_BYTES)

#define CAM_CAL_SINGLE_AWB_COUNT_BYTE (0x08)

#define CUSTOM_CAM_CAL_ROTATION_0_DEGREE 0
#define CUSTOM_CAM_CAL_ROTATION_180_DEGREE 1

#define CUSTOM_CAM_CAL_COLOR_SHIFT_00 0
#define CUSTOM_CAM_CAL_COLOR_SHIFT_01 1
#define CUSTOM_CAM_CAL_COLOR_SHIFT_10 2
#define CUSTOM_CAM_CAL_COLOR_SHIFT_11 3

#define CAM_CAL_AWB_BITEN (0x01 << 0)
#define CAM_CAL_AF_BITEN (0x01 << 1)
#define CAM_CAL_NONE_BITEN (0x00)

#if 1
typedef enum {
  CAM_CAL_NONE_LSC = (unsigned char)0x0,
  CAM_CAL_SENOR_LSC = (unsigned char)0x1,
  CAM_CAL_MTK_LSC = (unsigned char)0x2
} CAM_CAL_LSC_VER_ENUM;
#endif

typedef enum { CAM_CAL_NONE = 0, CAM_CAL_USED } CAM_CAL_TYPE_ENUM;

CAM_CAL_TYPE_ENUM CAM_CALInit(void);
unsigned int CAM_CALDeviceName(char* DevName);

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_CAM_CAL_H_
