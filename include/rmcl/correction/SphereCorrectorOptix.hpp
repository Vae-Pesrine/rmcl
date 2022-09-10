#ifndef RMCL_SPHERE_CORRECTOR_OPTIX_HPP
#define RMCL_SPHERE_CORRECTOR_OPTIX_HPP

#include <rmagine/map/OptixMap.hpp>
#include <rmagine/types/sensor_models.h>
#include <rmagine/math/types.h>
#include <rmagine/math/SVDCuda.hpp>
#include <rmagine/simulation/SphereSimulatorOptix.hpp>

#include "CorrectionResults.hpp"
#include "CorrectionParams.hpp"

#include <memory>

namespace rmcl 
{

class SphereCorrectorOptix 
: public rmagine::SphereSimulatorOptix
{
public:
    using Base = rmagine::SphereSimulatorOptix;

    SphereCorrectorOptix();

    SphereCorrectorOptix(rmagine::OptixMapPtr map);

    void setParams(
        const CorrectionParams& params);

    void setInputData(
        const rmagine::MemoryView<float, rmagine::VRAM_CUDA>& ranges
    );

    CorrectionResults<rmagine::VRAM_CUDA> correct(
        const rmagine::MemoryView<rmagine::Transform, rmagine::VRAM_CUDA>& Tbms
    ) const;

    void compute_covs(
        const rmagine::MemoryView<rmagine::Transform, rmagine::VRAM_CUDA>& Tbms,
        rmagine::MemoryView<rmagine::Vector, rmagine::VRAM_CUDA>& ms,
        rmagine::MemoryView<rmagine::Vector, rmagine::VRAM_CUDA>& ds,
        rmagine::MemoryView<rmagine::Matrix3x3, rmagine::VRAM_CUDA>& Cs,
        rmagine::MemoryView<unsigned int, rmagine::VRAM_CUDA>& Ncorr
    ) const;

    void compute_covs(
        const rmagine::MemoryView<rmagine::Transform, rmagine::VRAM_CUDA>& Tbms,
        CorrectionPreResults<rmagine::VRAM_CUDA>& res
    ) const;

    CorrectionPreResults<rmagine::VRAM_CUDA> compute_covs(
        const rmagine::MemoryView<rmagine::Transform, rmagine::VRAM_CUDA>& Tbms
    ) const;
    

    struct Timings
    {   
        double sim = 0.0;
        double red = 0.0;
        double svd = 0.0;
    };

    Timings benchmark(
        const rmagine::MemoryView<rmagine::Transform, rmagine::VRAM_CUDA>& Tbms, 
        size_t Ntests = 100);

protected:
    rmagine::Memory<float, rmagine::VRAM_CUDA> m_ranges;
    rmagine::Memory<CorrectionParams, rmagine::VRAM_CUDA> m_params;

    rmagine::SVDCudaPtr m_svd;

private:
    void computeMeansCovsRW(
        const rmagine::MemoryView<rmagine::Transform, rmagine::VRAM_CUDA>& Tbm,
        rmagine::MemoryView<rmagine::Vector, rmagine::VRAM_CUDA>& m1, // from, dataset
        rmagine::MemoryView<rmagine::Vector, rmagine::VRAM_CUDA>& m2, // to, model
        rmagine::MemoryView<rmagine::Matrix3x3, rmagine::VRAM_CUDA>& Cs,
        rmagine::MemoryView<unsigned int, rmagine::VRAM_CUDA>& Ncorr
        ) const;

    void computeMeansCovsSW(
        const rmagine::MemoryView<rmagine::Transform, rmagine::VRAM_CUDA>& Tbm,
        rmagine::MemoryView<rmagine::Vector, rmagine::VRAM_CUDA>& m1,
        rmagine::MemoryView<rmagine::Vector, rmagine::VRAM_CUDA>& m2,
        rmagine::MemoryView<rmagine::Matrix3x3, rmagine::VRAM_CUDA>& Cs,
        rmagine::MemoryView<unsigned int, rmagine::VRAM_CUDA>& Ncorr
        ) const;
};

using SphereCorrectorOptixPtr = std::shared_ptr<SphereCorrectorOptix>;

} // namespace rmcl

#endif // RMCL_SPHERE_CORRECTOR_OPTIX_HPP