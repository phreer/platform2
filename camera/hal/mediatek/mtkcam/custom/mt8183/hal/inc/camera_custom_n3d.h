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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_N3D_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_N3D_H_
//
#include "inc/camera_custom_types.h"
//

/*******************************************************************************
 * N3D sensor position
 ******************************************************************************/
typedef struct customSensorPos_N3D_s {
  MUINT32 uSensorPos;
} customSensorPos_N3D_t;

customSensorPos_N3D_t const& getSensorPosN3D();

/*******************************************************************************
 * Return enable/disable flag of N3D to ISP
 ******************************************************************************/
MBOOL get_N3DFeatureFlag(void);  // cotta : added for N3D

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_N3D_H_
