/**
 * Copyright (c) 2021, University Osnabrück
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University Osnabrück nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL University Osnabrück BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * OptixCorrectionData.hpp
 *
 *  Created on: Jul 17, 2021
 *      Author: Alexander Mock
 */

#ifndef RMCL_OPTIX_CORRECTION_DATA_HPP
#define RMCL_OPTIX_CORRECTION_DATA_HPP

#include <optix.h>
#include <cuda_runtime.h>

#include <rmagine/types/sensor_models.h>
#include <rmagine/math/types.h>

#include "rmcl/correction/CorrectionParams.hpp"


namespace rmcl {

/**
 * @brief Data for scanwise results
 * 
 */
struct OptixCorrectionDataSW
{
    // inputs
    const rmagine::SphericalModel*  model;
    const float*                    ranges;
    const rmagine::Transform*       Tsb; // Sensor to Base Transform
    const rmagine::Transform*       Tbm; // Base to Map transforms
    unsigned int                    Nposes;
    const CorrectionParams*         params;
    // handle
    OptixTraversableHandle          handle;
    // outputs
    rmagine::Matrix3x3*             C; // C between 1 and 2
    rmagine::Vector*                m1; // from
    rmagine::Vector*                m2; // to
    unsigned int*                   Ncorr;
};

/**
 * @brief Data for raywise results
 * 
 */
struct OptixCorrectionDataRW
{
    // inputs
    const rmagine::SphericalModel*  model;
    const float*                    ranges;
    const rmagine::Transform*       Tsb; // Sensor to Base Transform
    const rmagine::Transform*       Tbm; // Base to Map transforms
    unsigned int                    Nposes;
    const CorrectionParams*         params;
    // handle
    OptixTraversableHandle          handle;
    // outputs
    unsigned int*                   corr_valid;
    rmagine::Vector*                model_points; // nearest points on mesh
    rmagine::Vector*                dataset_points;
};

} // namespace rmcl

#endif // RMCL_OPTIX_CORRECTION_DATA_HPP