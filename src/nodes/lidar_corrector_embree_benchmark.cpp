#include <iostream>
#include <rmagine/util/synthetic.h>
#include <rmagine/util/StopWatch.hpp>
#include <rmagine/map/embree/embree_shapes.h>
#include <rmagine/types/sensors.h>
#include <rmagine/math/math.h>
#include <rmagine/util/prints.h>

#include <rmcl/correction/SphereCorrectorEmbree.hpp>

#include <fstream>


using namespace rmagine;
using namespace rmcl;

bool find_abc(unsigned int& A, unsigned int& B, unsigned int C)
{
    A = static_cast<unsigned int>( sqrt(C) );
    B = A;

    unsigned int res = A * B;

    while(res != C && B > 0 && A <= C)
    {
        if(res > C)
        {
            B--;
        } else {
            A++;
        }
        res = A * B;
    }

    return (B > 0 && A <= C && res == C);
}

EmbreeMapPtr make_sphere_map(unsigned int Nfaces)
{
    // N = Nfaces / 2

    // A*B = N
    // 
    // A = N / B
    // B = N / A
    // A, B must be integer. N / B must be possible

    if(Nfaces % 2 == 1)
    {
        throw std::runtime_error("Nfaces must be even");
    }


    size_t N = Nfaces / 2;

    unsigned int A, B;
    if(!find_abc(A, B, N) )
    {
        throw std::runtime_error("Could not find integers for A*B=N");
    }

    

    EmbreeMeshPtr sphere_mesh = std::make_shared<EmbreeSphere>(A, B);
    sphere_mesh->commit();
    std::cout << A << "x" << B << " * 2 = " << Nfaces << " = " << sphere_mesh->faces().size() << std::endl;
    EmbreeScenePtr sphere_scene = std::make_shared<EmbreeScene>();
    sphere_scene->add(sphere_mesh);
    sphere_scene->commit();
    return std::make_shared<EmbreeMap>(sphere_scene);
}

int main(int argc, char** argv)
{
    // TODO: somewhere is memory that is not be freed when the scene is overwritten

    size_t Nfaces = atoi(argv[1]);
    size_t Nposes = atoi(argv[2]);
    size_t Nruns = 10;


    std::cout << "Running benchmark with " << Nfaces << " faces and " << Nposes << " poses" << std::endl;

    EmbreeMapPtr sphere_map_tmp = make_sphere_map(Nfaces);
    
    SphereCorrectorEmbree correct(sphere_map_tmp);
    correct.setTsb(Transform::Identity());

    // return 0;

    auto model = vlp16_900();
    model.range.min = 0.0;
    correct.setModel(model);
    
    using ResultT = Bundle<
        Ranges<RAM>
    >;

    ResultT sim_res;
    sim_res.ranges.resize(model.size());
    
    Memory<Transform, RAM> T_dest(1);
    T_dest[0] = Transform::Identity();


    StopWatch sw;
    double el;

    Memory<Transform, RAM> T_curr(Nposes);
    for(size_t i=0; i<T_curr.size(); i++)
    {
        T_curr[i] = Transform::Identity();
        T_curr[i].t.z += 0.2;
    }

    // simulate the data that would be recorded at destination
    correct.simulate(T_dest, sim_res);
    correct.setInputData(sim_res.ranges);


    // timings:
    // - 0: measure outer timings
    // - 1: measure inner timings

    int timings = 1;

    if(timings == 0)
    {
        double el_total = 0.0;
        for(size_t i=0; i<Nruns; i++)
        {
            sw();
            auto corr_res = correct.correct(T_curr);
            el = sw();
            el_total += el;
            T_curr = multNxN(T_curr, corr_res.Tdelta);
            // std::cout << i << ": " << T_curr[0].t.z << std::endl;
        }

        std::cout << "- runtime: " << std::endl;
        std::cout << Nfaces << "," << Nposes << "," << el_total/static_cast<double>(Nruns) << std::endl;
    } else if(timings == 1) {
        auto bres = correct.benchmark(T_curr, Nruns);
        std::cout << "Absolute:"  << std::endl;
        std::cout << "- Sim: " << bres.sim << std::endl;
        std::cout << "- Red: " << bres.red << std::endl;
        std::cout << "- SVD: " << bres.svd << std::endl;

        double total = bres.sim + bres.red + bres.svd;
        std::cout << "- Total: " << total <<  std::endl;
        std::cout << "- Relative: " << std::endl;
        std::cout << bres.sim / total << "," << bres.red / total << "," << bres.svd / total << std::endl;
        
    }

    

    
    // TIMINGS 0

    // AMOCKHOME
    // #faces, #poses, correction runtime 
    // 100000,1000,0.18301
    // 500000,1000,0.203063
    // 1000000,1000,0.201213
    // 2000000,1000,0.23414
    // 3000000,1000,0.341999
    // 4000000,1000,0.350003
    // 6000000,1000,0.419774
    // 8000000,1000,0.448595
    // 10000000,1000,0.455784


    // TIMINGS 1

    // AMOCKHOME
    // 
    // args: 1000000 1000
    // Absolute:
    // - Sim: 2.35949
    // - Red: 0.0771303
    // - SVD: 0.000365363
    // - Total: 2.43698
    // - Relative: 
    // 0.9682,0.0316499,0.000149924



    return 0;
}