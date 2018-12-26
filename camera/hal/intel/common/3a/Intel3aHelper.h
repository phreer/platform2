/*
 * Copyright (C) 2017 Intel Corporation
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

#ifndef AAA_INTEL3AHELPER_H_
#define AAA_INTEL3AHELPER_H_

#include <ia_aiq.h>
#include <ia_coordinate.h>

namespace android {
namespace camera2 {

#define MAX_COLOR_CONVERSION_MATRIX 3
class Intel3aHelper {
public:
    // Static debug dumpers
    static void dumpStatInputParams(const ia_aiq_statistics_input_params* sip);
    static void dumpAfInputParams(const ia_aiq_af_input_params* afCfg);
    static void dumpAeInputParams(const ia_aiq_ae_input_params &aeInput);
    static void dumpAeResult(const ia_aiq_ae_results* aeResult);
    static void dumpAfResult(const ia_aiq_af_results* afResult);
    static void dumpAwbResult(const ia_aiq_awb_results* awbResult);
    static void dumpRGBSGrids(const ia_aiq_rgbs_grid **rgbs_grids, int gridCount);
    static void dumpPaResult(const ia_aiq_pa_results* paResult);
    static void dumpSaResult(const ia_aiq_sa_results* saResult);
};

} /* namespace camera2 */
} /* namespace android */
#endif  // AAA_INTEL3AHELPER_H_
