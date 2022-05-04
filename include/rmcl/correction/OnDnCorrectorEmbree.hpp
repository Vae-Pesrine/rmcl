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
 * EmbreeCorrector.hpp
 *
 *  Created on: Jul 17, 2021
 *      Author: Alexander Mock
 */


#ifndef RMCL_CORRECTOR_ONDN_EMBREE_HPP
#define RMCL_CORRECTOR_ONDN_EMBREE_HPP

#include <memory>

// rmagine deps
#include <rmagine/map/EmbreeMap.hpp>
#include <rmagine/types/sensor_models.h>
#include <rmagine/math/SVD.hpp>
#include <rmagine/simulation/OnDnSimulatorEmbree.hpp>

#include "CorrectionResults.hpp"
#include "CorrectionParams.hpp"

namespace rmcl {

/**
 * @brief EmbreeCorrector computes robot pose corrections in robot frame on CPU.
 * 
 * Required information to set:
 * - Sensor Model: OnDnModel
 * - Sensor Data: Scanner Ranges
 * - Transformation: Sensor to Base
 * 
 * TODO: inherit from rmagine::OnDnSimulatorEmbree ???
 */
class OnDnCorrectorEmbree 
: public rmagine::OnDnSimulatorEmbree
{
public:
    /**
     * @brief Correct Sensor data towards a given map
     * 
     * @param mesh 
     */
    using Base = rmagine::OnDnSimulatorEmbree;
    using Base::Base;

    void setParams(
        const CorrectionParams& params);

    void setInputData(
        const rmagine::Memory<float, rmagine::RAM>& ranges);

    /**
     * @brief Correct one ore multiple Poses towards the map
     * 
     * @param Tbm Poses represented as transformations (rmagine::Transform)
     * @return Memory<Transform, RAM> Correction in robots base coordinates
     */
    CorrectionResults<rmagine::RAM> correct(
        const rmagine::Memory<rmagine::Transform, rmagine::RAM>& Tbms
    );

protected:
    rmagine::Memory<float, rmagine::RAM> m_ranges;

    CorrectionParams m_params;

    // TODO: currently unused
    rmagine::SVDPtr m_svd;
};

using OnDnCorrectorEmbreePtr = std::shared_ptr<OnDnCorrectorEmbree>;

} // namespace rmcl

#endif // RMCL_CORRECTOR_ONDN_EMBREE_HPP