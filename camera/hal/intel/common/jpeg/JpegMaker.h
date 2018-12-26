/*
 * Copyright (C) 2014-2017 Intel Corporation
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

#ifndef _CAMERA3_HAL_JPEG_MAKER_H_
#define _CAMERA3_HAL_JPEG_MAKER_H_

#include "ImgEncoder.h"
#include "JpegMakerCore.h"

namespace android {
namespace camera2 {

/**
 * \class JpegMaker
 * A wrapper based on JpegMakerCore for handling legacy Android IPU platforms
 * before IPU5.
 */
class JpegMaker : public JpegMakerCore {
public: /* Methods */
    explicit JpegMaker(int cameraid);
    virtual ~JpegMaker();
    status_t setupExifWithMetaData(ImgEncoder::EncodePackage & package,
        ExifMetaData& metaData,
        const Camera3Request& request);
    status_t makeJpeg(ImgEncoder::EncodePackage & package, std::shared_ptr<CameraBuffer> dest = nullptr);
};
} /* namespace camera2 */
} /* namespace android */
#endif  // _CAMERA3_HAL_JPEG_MAKER_H_
