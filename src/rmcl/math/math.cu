#include "rmcl/math/math.cuh"

#include <rmagine/math/math.cuh>

using namespace rmagine;
namespace rm = rmagine;


namespace rmcl 
{

CorrectionCuda::CorrectionCuda()
:m_svd(new SVDCuda)
{

}

CorrectionCuda::CorrectionCuda(SVDCudaPtr svd)
:m_svd(svd) 
{

}


void CorrectionCuda::correction_from_covs(
    const MemoryView<Vector, VRAM_CUDA>& ms,
    const MemoryView<Vector, VRAM_CUDA>& ds,
    const MemoryView<Matrix3x3, VRAM_CUDA>& Cs,
    const MemoryView<unsigned int, VRAM_CUDA>& Ncorr,
    MemoryView<Transform, VRAM_CUDA>& Tdelta) const
{
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA> Us(Cs.size());
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA> Vs(Cs.size());

    m_svd->calcUV(Cs, Us, Vs);
    rm::transposeInplace(Vs);

    auto Rs = rm::multNxN(Us, Vs);
    auto ts = rm::subNxN(ds, rm::multNxN(Rs, ms));

    rm::pack(Rs, ts, Tdelta);
}

void CorrectionCuda::correction_from_covs(
    const MemoryView<Vector, VRAM_CUDA>& ms,
    const MemoryView<Vector, VRAM_CUDA>& ds,
    const MemoryView<Matrix3x3, VRAM_CUDA>& Cs,
    const MemoryView<unsigned int, VRAM_CUDA>& Ncorr,
    MemoryView<Quaternion, VRAM_CUDA>& Rdelta,
    MemoryView<Vector, VRAM_CUDA>& tdelta) const
{
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA> Us(Cs.size());
    rm::Memory<rm::Matrix3x3, rm::VRAM_CUDA> Vs(Cs.size());

    m_svd->calcUV(Cs, Us, Vs);
    rm::transposeInplace(Vs);

    rm::multNxN(Us, Vs, Rdelta);
    rm::subNxN(ds, rm::multNxN(Rdelta, ms), tdelta);
}

void CorrectionCuda::correction_from_covs(
    const CorrectionPreResults<VRAM_CUDA>& pre_res,
    MemoryView<Transform, VRAM_CUDA>& Tdelta) const
{
    correction_from_covs(pre_res.ms, pre_res.ds, pre_res.Cs, pre_res.Ncorr, Tdelta);
}

Memory<Transform, VRAM_CUDA> CorrectionCuda::correction_from_covs(
    const CorrectionPreResults<VRAM_CUDA>& pre_res) const
{
    Memory<Transform, VRAM_CUDA> Tdelta(pre_res.ms.size());
    correction_from_covs(pre_res, Tdelta);
    return Tdelta;
}

// weighted average by
// - number of correspondences
// - fixed weights
// TODO: more than two


// template<typename T>
// __global__ void weighted_average_kernel(
//     const T* a, const unsigned int* Na,
//     const T* b, const unsigned int* Nb,
//     const unsigned int N, // elements
//     T* c, unsigned int* Nc)
// {
//     const unsigned int id = blockIdx.x * blockDim.x + threadIdx.x;
//     if(id < N)
//     {
//         const unsigned int Ncorr_ = Na[id] + Nb[id];
//         const float Ncorrf = static_cast<float>(Ncorr_);
//         const float wa = static_cast<float>(Na[id]) / Ncorrf;
//         const float wb = static_cast<float>(Nb[id]) / Ncorrf;

//         c[id] = a[id] * wa + b[id] * wb;
//         Nc[id] = Ncorr_;
//     }
// }

__global__ void weighted_average_kernel(
    const Vector* ms1, const Vector* ds1, const Matrix3x3* Cs1, const unsigned int* Ncorr1,
    const Vector* ms2, const Vector* ds2, const Matrix3x3* Cs2, const unsigned int* Ncorr2,
    const unsigned int N, // Nelements
    Vector* ms, Vector* ds, Matrix3x3* Cs, unsigned int* Ncorr)
{
    const unsigned int pid = blockIdx.x * blockDim.x + threadIdx.x;
    if(pid < N)
    {
        const unsigned int Ncorr_ = Ncorr1[pid] + Ncorr2[pid];
        const float Ncorrf = static_cast<float>(Ncorr_);
        float w1 = static_cast<float>(Ncorr1[pid]) / Ncorrf;
        float w2 = static_cast<float>(Ncorr2[pid]) / Ncorrf;

        ms[pid] = ms1[pid] * w1 + ms2[pid] * w2;
        ds[pid] = ds1[pid] * w1 + ds2[pid] * w2;
        Cs[pid] = Cs1[pid] * w1 + Cs2[pid] * w2;
        Ncorr[pid] = Ncorr_;
    }
}


void weighted_average(
    const MemoryView<Vector, VRAM_CUDA>& ms1,
    const MemoryView<Vector, VRAM_CUDA>& ds1,
    const MemoryView<Matrix3x3, VRAM_CUDA>& Cs1,
    const MemoryView<unsigned int, VRAM_CUDA>& Ncorr1,
    const MemoryView<Vector, VRAM_CUDA>& ms2,
    const MemoryView<Vector, VRAM_CUDA>& ds2,
    const MemoryView<Matrix3x3, VRAM_CUDA>& Cs2,
    const MemoryView<unsigned int, VRAM_CUDA>& Ncorr2,
    MemoryView<Vector, VRAM_CUDA>& ms,
    MemoryView<Vector, VRAM_CUDA>& ds,
    MemoryView<Matrix3x3, VRAM_CUDA>& Cs,
    MemoryView<unsigned int, VRAM_CUDA>& Ncorr)
{
    constexpr unsigned int blockSize = 64;
    const unsigned int gridSize = (ms1.size() + blockSize - 1) / blockSize;

    weighted_average_kernel<<<gridSize, blockSize>>>(
        ms1.raw(), ds1.raw(), Cs1.raw(), Ncorr1.raw(),
        ms2.raw(), ds2.raw(), Cs2.raw(), Ncorr2.raw(),
        ms1.size(),
        ms.raw(), ds.raw(), Cs.raw(), Ncorr.raw()
    );
}

__global__ void weighted_average_kernel(
    const Vector* ms1, const Vector* ds1, const Matrix3x3* Cs1, const unsigned int* Ncorr1, const float w1,
    const Vector* ms2, const Vector* ds2, const Matrix3x3* Cs2, const unsigned int* Ncorr2, const float w2,
    const unsigned int N, // Nelements
    Vector* ms, Vector* ds, Matrix3x3* Cs, unsigned int* Ncorr)
{
    const unsigned int pid = blockIdx.x * blockDim.x + threadIdx.x;
    if(pid < N)
    {
        const unsigned int Ncorr_ = Ncorr1[pid] + Ncorr2[pid];

        ms[pid] = ms1[pid] * w1 + ms2[pid] * w2;
        ds[pid] = ds1[pid] * w1 + ds2[pid] * w2;
        Cs[pid] = Cs1[pid] * w1 + Cs2[pid] * w2;
        Ncorr[pid] = Ncorr_;
    }
}

void weighted_average(
    const MemoryView<Vector, VRAM_CUDA>& ms1,
    const MemoryView<Vector, VRAM_CUDA>& ds1,
    const MemoryView<Matrix3x3, VRAM_CUDA>& Cs1,
    const MemoryView<unsigned int, VRAM_CUDA>& Ncorr1,
    float w1,
    const MemoryView<Vector, VRAM_CUDA>& ms2,
    const MemoryView<Vector, VRAM_CUDA>& ds2,
    const MemoryView<Matrix3x3, VRAM_CUDA>& Cs2,
    const MemoryView<unsigned int, VRAM_CUDA>& Ncorr2,
    float w2,
    MemoryView<Vector, VRAM_CUDA>& ms,
    MemoryView<Vector, VRAM_CUDA>& ds,
    MemoryView<Matrix3x3, VRAM_CUDA>& Cs,
    MemoryView<unsigned int, VRAM_CUDA>& Ncorr)
{
    constexpr unsigned int blockSize = 64;
    const unsigned int gridSize = (ms1.size() + blockSize - 1) / blockSize;

    weighted_average_kernel<<<gridSize, blockSize>>>(
        ms1.raw(), ds1.raw(), Cs1.raw(), Ncorr1.raw(), w1,
        ms2.raw(), ds2.raw(), Cs2.raw(), Ncorr2.raw(), w2,
        ms1.size(),
        ms.raw(), ds.raw(), Cs.raw(), Ncorr.raw()
    );
}

void weighted_average(
    const std::vector<MemoryView<Vector, VRAM_CUDA> >& model_means,
    const std::vector<MemoryView<Vector, VRAM_CUDA> >& dataset_means,
    const std::vector<MemoryView<Matrix3x3, VRAM_CUDA> >& covs,
    const std::vector<MemoryView<unsigned int, VRAM_CUDA> >& Ncorrs,
    MemoryView<Vector, VRAM_CUDA>& ms,
    MemoryView<Vector, VRAM_CUDA>& ds,
    MemoryView<Matrix3x3, VRAM_CUDA>& Cs,
    MemoryView<unsigned int, VRAM_CUDA>& Ncorr)
{
    copy(model_means[0], ms);
    copy(dataset_means[0], ds);
    copy(covs[0], Cs);
    copy(Ncorrs[0], Ncorr);

    for(size_t i=1; i<model_means.size(); i++)
    {
        weighted_average(
            model_means[i], dataset_means[i], covs[i], Ncorrs[i],
            ms, ds, Cs, Ncorr,
            ms, ds, Cs, Ncorr);
    }
}

void weighted_average(
    const std::vector<MemoryView<Vector, VRAM_CUDA> >& model_means,
    const std::vector<MemoryView<Vector, VRAM_CUDA> >& dataset_means,
    const std::vector<MemoryView<Matrix3x3, VRAM_CUDA> >& covs,
    const std::vector<MemoryView<unsigned int, VRAM_CUDA> >& Ncorrs,
    const std::vector<float>& weights,
    MemoryView<Vector, VRAM_CUDA>& ms,
    MemoryView<Vector, VRAM_CUDA>& ds,
    MemoryView<Matrix3x3, VRAM_CUDA>& Cs,
    MemoryView<unsigned int, VRAM_CUDA>& Ncorr)
{
    ms = model_means[0];
    ds = dataset_means[0];
    Cs = covs[0];
    Ncorr = Ncorrs[0];

    float w = weights[0];

    for(size_t i=1; i<model_means.size(); i++)
    {
        weighted_average(
            model_means[i], dataset_means[i], covs[i], Ncorrs[i], weights[i],
            ms, ds, Cs, Ncorr, w,
            ms, ds, Cs, Ncorr);
        w = 1.0;
    }
}

void weighted_average(
    const std::vector<CorrectionPreResults<VRAM_CUDA> >& pre_results,
    CorrectionPreResults<VRAM_CUDA>& pre_results_combined)
{
    // source: to fuse
    std::vector<MemoryView<Vector, VRAM_CUDA> > ms;
    std::vector<MemoryView<Vector, VRAM_CUDA> > ds;
    std::vector<MemoryView<Matrix3x3, VRAM_CUDA> > Cs;
    std::vector<MemoryView<unsigned int, VRAM_CUDA> > Ncorrs;

    for(size_t i = 0; i < pre_results.size(); i++)
    {
        ms.push_back(pre_results[i].ms);
        ds.push_back(pre_results[i].ds);
        Cs.push_back(pre_results[i].Cs);
        Ncorrs.push_back(pre_results[i].Ncorr);
    }

    weighted_average(ms, ds, Cs, Ncorrs, 
        pre_results_combined.ms, pre_results_combined.ds, pre_results_combined.Cs, pre_results_combined.Ncorr);

}

CorrectionPreResults<VRAM_CUDA> weighted_average(
    const std::vector<CorrectionPreResults<VRAM_CUDA> >& pre_results)
{
    CorrectionPreResults<rmagine::VRAM_CUDA> res;
    size_t Nposes = pre_results[0].Cs.size();

    res.ms.resize(Nposes);
    res.ds.resize(Nposes);
    res.Cs.resize(Nposes);
    res.Ncorr.resize(Nposes);

    weighted_average(pre_results, res);

    return res;
}

void weighted_average(
    const std::vector<CorrectionPreResults<VRAM_CUDA> >& pre_results,
    const std::vector<float>& weights,
    CorrectionPreResults<VRAM_CUDA>& pre_results_combined)
{
    // source: to fuse
    std::vector<MemoryView<Vector, VRAM_CUDA> > ms;
    std::vector<MemoryView<Vector, VRAM_CUDA> > ds;
    std::vector<MemoryView<Matrix3x3, VRAM_CUDA> > Cs;
    std::vector<MemoryView<unsigned int, VRAM_CUDA> > Ncorrs;

    for(size_t i = 0; i < pre_results.size(); i++)
    {
        ms.push_back(pre_results[i].ms);
        ds.push_back(pre_results[i].ds);
        Cs.push_back(pre_results[i].Cs);
        Ncorrs.push_back(pre_results[i].Ncorr);
    }

    weighted_average(ms, ds, Cs, Ncorrs, weights,
        pre_results_combined.ms, pre_results_combined.ds, pre_results_combined.Cs, pre_results_combined.Ncorr);
}

CorrectionPreResults<VRAM_CUDA> weighted_average(
    const std::vector<CorrectionPreResults<VRAM_CUDA> >& pre_results,
    const std::vector<float>& weights)
{
    CorrectionPreResults<rmagine::VRAM_CUDA> res;
    size_t Nposes = pre_results[0].Cs.size();

    res.ms.resize(Nposes);
    res.ds.resize(Nposes);
    res.Cs.resize(Nposes);
    res.Ncorr.resize(Nposes);

    weighted_average(pre_results, weights, res);

    return res;
}

} // namespace rmcl