#include "rmcl/correction/SphereCorrectorOptix.hpp"

#include "rmcl/correction/optix/SphereCorrectProgramRW.hpp"
#include "rmcl/correction/optix/SphereCorrectProgramSW.hpp"

#include "rmcl/correction/optix/CorrectionDataOptix.hpp"

#include <rmagine/math/math_batched.cuh>

#include <rmcl/math/math_batched.cuh>


// DEBUG
#include <rmagine/util/prints.h>


namespace rm = rmagine;

namespace rmcl 
{

SphereCorrectorOptix::SphereCorrectorOptix(
    rmagine::OptixMapPtr map)
:Base(map)
{
    programs.resize(2);
    programs[0].reset(new SphereCorrectProgramRW(map));
    programs[1].reset(new SphereCorrectProgramSW(map));

    // CUDA_CHECK( cudaStreamCreate(&m_stream) );
    m_svd.reset(new rm::SVDCuda(Base::m_stream));

    // default params
    CorrectionParams params;
    setParams(params);
}

void SphereCorrectorOptix::setParams(
    const CorrectionParams& params)
{
    rm::Memory<CorrectionParams, rm::RAM> params_ram(1);
    params_ram[0] = params;
    m_params = params_ram;
}

void SphereCorrectorOptix::setInputData(
    const rmagine::Memory<float, rmagine::VRAM_CUDA>& ranges)
{
    m_ranges = ranges;
}

CorrectionResults<rm::VRAM_CUDA> SphereCorrectorOptix::correct(
    const rm::Memory<rm::Transform, rm::VRAM_CUDA>& Tbms) const
{
    // std::cout << "Start correction." << std::endl;
    CorrectionResults<rm::VRAM_CUDA> res;
    res.Ncorr.resize(Tbms.size());
    res.Tdelta.resize(Tbms.size());

    if(Tbms.size() == 0)
    {
        return res;
    }

    rm::Memory<rm::Vector, rm::VRAM_CUDA> m1(Tbms.size());
    rm::Memory<rm::Vector, rm::VRAM_CUDA> m2(Tbms.size());
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA> Cs(Tbms.size());

    // TODO how to make this dynamic somehow
    constexpr unsigned int POSE_SWITCH = 1024 * 8;
    // constexpr unsigned int POSE_SWITCH = 0;

    if(Tbms.size() > POSE_SWITCH)
    {
        // scanwise parallelization
        computeMeansCovsSW(Tbms, m1, m2, Cs, res.Ncorr);
    } else {
        // raywise parallelization
        computeMeansCovsRW(Tbms, m1, m2, Cs, res.Ncorr);
    }

    rm::Memory<rm::Matrix3x3, rm::RAM> Cs_ram(Cs.size());
    Cs_ram = Cs;

    // Singular value decomposition
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA> Us(Cs.size());
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA> Vs(Cs.size());
    m_svd->calcUV(Cs, Us, Vs);
    auto Rs = rm::multNxN(Us, rm::transpose(Vs));
    auto ts = rm::subNxN(m1, rm::multNxN(Rs, m2));

    rm::pack(Rs, ts, res.Tdelta);

    return res;
}

/// PRIVATE
void SphereCorrectorOptix::computeMeansCovsRW(
    const rm::Memory<rm::Transform, rm::VRAM_CUDA>& Tbm,
    rm::Memory<rm::Vector, rm::VRAM_CUDA>& m1, // from, dataset
    rm::Memory<rm::Vector, rm::VRAM_CUDA>& m2, // to, model
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA>& Cs,
    rm::Memory<unsigned int, rm::VRAM_CUDA>& Ncorr
    ) const
{
    size_t scanSize = m_width * m_height;
    // size_t scanSize = m_model_ram->width * m_model_ram->height;
    size_t Nrays = Tbm.size() * scanSize;

    rm::Memory<unsigned int,    rm::VRAM_CUDA> corr_valid(Nrays);
    rm::Memory<rm::Vector, rm::VRAM_CUDA> model_points(Nrays);
    rm::Memory<rm::Vector, rm::VRAM_CUDA> dataset_points(Nrays);

    rm::Memory<SphereCorrectionDataRW, rm::RAM> mem(1);
    mem->model = m_model.raw();
    mem->ranges = m_ranges.raw();
    mem->Tsb = m_Tsb.raw();
    mem->Tbm = Tbm.raw();
    mem->Nposes = Tbm.size();
    mem->params = m_params.raw();
    mem->handle = m_map->as.handle;
    mem->corr_valid = corr_valid.raw();
    mem->model_points = model_points.raw();
    mem->dataset_points = dataset_points.raw();

    rm::Memory<SphereCorrectionDataRW, rm::VRAM_CUDA> d_mem(1);
    copy(mem, d_mem, Base::m_stream);

    rm::OptixProgramPtr program = programs[0];

    OPTIX_CHECK( optixLaunch(
        program->pipeline, 
        Base::m_stream, 
        reinterpret_cast<CUdeviceptr>(d_mem.raw()), 
        sizeof( SphereCorrectionDataRW ), 
        &program->sbt,
        m_width, // width Xdim
        m_height, // height Ydim
        Tbm.size()// depth Zdim
        ));

    cudaStreamSynchronize(m_stream);

    rm::sumBatched(corr_valid, Ncorr);
    meanBatched(dataset_points, corr_valid, Ncorr, m1);
    meanBatched(model_points, corr_valid, Ncorr, m2);

    covFancyBatched(model_points, m2, 
            dataset_points, m1, 
            corr_valid, Ncorr, Cs);
}

void SphereCorrectorOptix::computeMeansCovsSW(
    const rm::Memory<rm::Transform, rm::VRAM_CUDA>& Tbm,
    rm::Memory<rm::Vector, rm::VRAM_CUDA>& m1,
    rm::Memory<rm::Vector, rm::VRAM_CUDA>& m2,
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA>& Cs,
    rm::Memory<unsigned int, rm::VRAM_CUDA>& Ncorr
    ) const
{
    m1.resize(Tbm.size());
    m2.resize(Tbm.size());
    Cs.resize(Tbm.size());
    Ncorr.resize(Tbm.size());

    // Create Optix Memory structure
    rm::Memory<SphereCorrectionDataSW, rm::RAM_CUDA> mem(1);
    mem->model = m_model.raw();
    mem->ranges = m_ranges.raw();
    mem->Tbm = Tbm.raw();
    mem->Tsb = m_Tsb.raw();
    mem->Nposes = Tbm.size();
    mem->params = m_params.raw();
    mem->handle = m_map->as.handle;
    mem->C = Cs.raw();
    mem->m1 = m1.raw(); // Sim
    mem->m2 = m2.raw(); // Real
    mem->Ncorr = Ncorr.raw();

    rm::Memory<SphereCorrectionDataSW, rm::VRAM_CUDA> d_mem(1);
    copy(mem, d_mem, m_stream);

    rm::OptixProgramPtr program = programs[1];

    OPTIX_CHECK( optixLaunch(
        program->pipeline, 
        m_stream, 
        reinterpret_cast<CUdeviceptr>(d_mem.raw()), 
        sizeof( SphereCorrectionDataSW ), 
        &program->sbt, 
        Tbm.size(),
        1,
        1
        ));

    cudaStreamSynchronize(m_stream);
}

} // namespace rmcl