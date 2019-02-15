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
#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_FLASH_NVRAM_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_FLASH_NVRAM_H_

#include <stddef.h>
#include "MediaTypes.h"
#include "CFG_Camera_File_Max_Size.h"

#define NVRAM_CUSTOM_FLASH_REVISION 7511001

/*******************************************************************************
 * FLASH NVRAM
 ******************************************************************************/
#define FLASH_LV_INDEX_UNIT (10)  // 1.0 LV
#define FLASH_LV_INDEX_MIN (0)    // LV 0
#define FLASH_LV_INDEX_MAX (18)   // LV 18
#define FLASH_LV_INDEX_NUM ((FLASH_LV_INDEX_MAX - FLASH_LV_INDEX_MIN) + 1)

// Flash AWB tuning parameter
typedef struct {
  //=== Foreground and Background definition ===
  MUINT32 ForeGroundPercentage;  // >50   default: 9
  MUINT32 BackGroundPercentage;  // <50   default: 95

  //=== Table to decide foreground weight (m_FG_Weight) ===
  // Th1 < Th2 < Th3 < Th4
  // FgPercentage_Thx_Val < 2000
  MUINT32 FgPercentage_Th1;      // default: 2
  MUINT32 FgPercentage_Th2;      // default: 5
  MUINT32 FgPercentage_Th3;      // default: 10
  MUINT32 FgPercentage_Th4;      // default: 15
  MUINT32 FgPercentage_Th1_Val;  // default: 200
  MUINT32 FgPercentage_Th2_Val;  // default: 250
  MUINT32 FgPercentage_Th3_Val;  // default: 300
  MUINT32 FgPercentage_Th4_Val;  // default: 350

  //=== Location weighting map ===//
  // Th1 < Th2 < Th3 < Th4
  // location_map_val1 <= location_map_val2 <= location_map_val3 <=
  // location_map_val4 < 500
  MUINT32 location_map_th1;   // default: 10
  MUINT32 location_map_th2;   // default: 20
  MUINT32 location_map_th3;   // default: 40
  MUINT32 location_map_th4;   // default: 50
  MUINT32 location_map_val1;  // default: 100
  MUINT32 location_map_val2;  // default: 110
  MUINT32 location_map_val3;  // default: 130
  MUINT32 location_map_val4;  // default: 150

  //=== Decide foreground Weighting ===//
  // FgBgTbl_Y0 <= 2000
  MUINT32 SelfTuningFbBgWeightTbl;  // default: 0
  MUINT32 FgBgTbl_Y0;
  MUINT32 FgBgTbl_Y1;
  MUINT32 FgBgTbl_Y2;
  MUINT32 FgBgTbl_Y3;
  MUINT32 FgBgTbl_Y4;
  MUINT32 FgBgTbl_Y5;

  //=== Decide luminance weight === //
  // YPrimeWeightTh[i] <= 256
  // YPrimeWeight[i] <= 10
  MUINT32 YPrimeWeightTh[5];  // default: {5,9,11,13,15}
  MUINT32 YPrimeWeight[4];    // default: {0, 0.1, 0.3, 0.5, 0.7}

  AWB_GAIN_T FlashPreferenceGain[FLASH_LV_INDEX_NUM];
} FLASH_AWB_TUNING_PARAM_T;

#define FLASH_CUSTOM_MAX_DUTY_NUM_HT (40)  // Note, related to NVRAM spec
#define FLASH_CUSTOM_MAX_DUTY_NUM_LT (40)  // Note, related to NVRAM spec
#define FLASH_CUSTOM_MAX_DUTY_NUM \
  (FLASH_CUSTOM_MAX_DUTY_NUM_HT * FLASH_CUSTOM_MAX_DUTY_NUM_LT)

#define FLASH_DUTY_NUM FLASH_CUSTOM_MAX_DUTY_NUM

typedef struct {
  AWB_GAIN_T
  flashWBGain[FLASH_CUSTOM_MAX_DUTY_NUM];  // Flash AWB calibration data
} FLASH_AWB_CALIBRATION_DATA_STRUCT, *PFLASH_AWB_CALIBRATION_DATA_STRUCT;

// Flash AWB NVRAM structure
typedef struct {
  FLASH_AWB_TUNING_PARAM_T rTuningParam;  // Flash AWB tuning parameter
} FLASH_AWB_NVRAM_T;

//==============================
// flash nvram
//==============================

enum {
  e_NVRAM_AE_SCENE_DEFAULT = -2,
};

typedef struct {
  int yTarget;                      // 188 (10bit)
  int fgWIncreaseLevelbySize;       // 10
  int fgWIncreaseLevelbyRef;        // 0
  int ambientRefAccuracyRatio;      // 5  5/256=2%
  int flashRefAccuracyRatio;        // 1   1/256=0.4%
  int backlightAccuracyRatio;       // 18 18/256=7%
  int backlightUnderY;              //  40 (10-bit)
  int backlightWeakRefRatio;        // 32  32/256=12.5%
  int safetyExp;                    // 33322
  int maxUsableISO;                 // 680
  int yTargetWeight;                // 0 base:256
  int lowReflectanceThreshold;      // 13  13/256=5%
  int flashReflectanceWeight;       // 0 base:256
  int bgSuppressMaxDecreaseEV;      // 2EV
  int bgSuppressMaxOverExpRatio;    // 6  6/256=2%
  int fgEnhanceMaxIncreaseEV;       // 5EV
  int fgEnhanceMaxOverExpRatio;     // 6  10/256=2%
  int isFollowCapPline;             // 0 for auto mode, 1 for others
  int histStretchMaxFgYTarget;      // 266 (10bit)
  int histStretchBrightestYTarget;  // 328 (10bit)
  int fgSizeShiftRatio;             // 0 0/256=0%
  int backlitPreflashTriggerLV;     // 90 (unit:0.1EV)
  int backlitMinYTarget;            // 100 (10bit)
  int minstameanpass;               // 80 (10bit)
  int yDecreEVTarget;               // 188 (default)
  int yFaceTarget;                  // 188 (default)
  int cfgFlashPolicy;               // 5 (default)
} NVRAM_FLASH_TUNING_PARA;

typedef struct {
  int exp;
  int afe_gain;
  int isp_gain;
  int distance;
  int16_t yTab[FLASH_CUSTOM_MAX_DUTY_NUM];  // x128
} NVRAM_FLASH_CCT_ENG_TABLE;

typedef struct {
  // torch, video
  int torchDuty;
  int torchDutyEx[20];
  // AF
  int afDuty;
  // pf, mf
  // normal bat setting
  int pfDuty;
  int mfDutyMax;
  int mfDutyMin;
  // low bat setting
  int IChangeByVBatEn;
  int vBatL;  // mv
  int pfDutyL;
  int mfDutyMaxL;
  int mfDutyMinL;
  // burst setting
  int IChangeByBurstEn;
  int pfDutyB;
  int mfDutyMaxB;
  int mfDutyMinB;
  // high current setting, set the duty at about 1A. when I is larget, notify
  // system to reduce modem power, cpu ...etc
  int decSysIAtHighEn;
  int dutyH;
} NVRAM_FLASH_ENG_LEVEL;

typedef struct {
  // torch, video
  int torchDuty;
  int torchDutyEx[20];

  // AF
  int afDuty;

  // pf, mf
  // normal bat setting
  int pfDuty;
  int mfDutyMax;
  int mfDutyMin;
  // low bat setting
  int pfDutyL;
  int mfDutyMaxL;
  int mfDutyMinL;
  // burst setting
  int pfDutyB;
  int mfDutyMaxB;
  int mfDutyMinB;
} NVRAM_FLASH_ENG_LEVEL_LT;  // low color temperature

typedef enum {
  FLASH_CHOOSE_WARM,
  FLASH_CHOOSE_COLD,
} EFLASH_CHOOSE_TYPE;

typedef struct {
  int toleranceEV_pos;
  int toleranceEV_neg;

  int XYWeighting;

  bool useAwbPreferenceGain;

  int envOffsetIndex[4];
  int envXrOffsetValue[4];
  int envYrOffsetValue[4];

  MINT32 VarianceTolerance;
  EFLASH_CHOOSE_TYPE ChooseColdOrWarm;
} NVRAM_DUAL_FLASH_TUNING_PARA;

typedef struct {
  NVRAM_FLASH_TUNING_PARA tuningPara;
  NVRAM_DUAL_FLASH_TUNING_PARA dualTuningPara;
  NVRAM_FLASH_ENG_LEVEL engLevel;
  NVRAM_FLASH_ENG_LEVEL_LT engLevelLT;
} FLASH_AE_NVRAM_T, *PFLASH_AE_NVRAM_T;

typedef struct {
  NVRAM_FLASH_CCT_ENG_TABLE engTab;
  AWB_GAIN_T
  flashWBGain[FLASH_CUSTOM_MAX_DUTY_NUM];  // Flash AWB calibration data
} FLASH_CALIBRATION_NVRAM_T, *PFLASH_CALIBRATION_NVRAM_T;

#define FlASH_AE_NUM_2 (4)
#define FlASH_AWB_NUM_2 (4)
#define FlASH_CALIBRATION_NUM_2 (4)

typedef union {
  struct {
    UINT32 u4Version;
    UINT32 SensorId;
    FLASH_AE_NVRAM_T Flash_AE[FlASH_AE_NUM_2];
    FLASH_AWB_NVRAM_T Flash_AWB[FlASH_AWB_NUM_2];
  };
  UINT8 temp[MAXIMUM_NVRAM_CAMERA_DEFECT_FILE_SIZE];
} NVRAM_CAMERA_STROBE_STRUCT, *PNVRAM_CAMERA_STROBE_STRUCT;

typedef union {
  struct {
    UINT32 u4Version;
    UINT32 SensorId;
    FLASH_CALIBRATION_NVRAM_T Flash_Calibration[FlASH_CALIBRATION_NUM_2];
  };
  UINT8 temp[MAXIMUM_NVRAM_CAMERA_FLASH_CALIBRATION_FILE_SIZE];
} NVRAM_CAMERA_FLASH_CALIBRATION_STRUCT,
    *PNVRAM_CAMERA_FLASH_CALIBRATION_STRUCT;

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_FLASH_NVRAM_H_
