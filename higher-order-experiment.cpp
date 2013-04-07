#include <iostream>
#include <chrono>
#include "higher-order.hpp"
#include "QPBO.h"
#include "gen-random.hpp"
#include "submodular-flow.hpp"
#include "submodular-ibfs.hpp"

int main(int argc, char **argv) {
    typedef int64_t REAL;
    typedef HigherOrderEnergy<REAL, 4> HOE;
    typedef typename HOE::NodeId NodeId;
    typedef std::chrono::system_clock::time_point TimePt;
    typedef std::chrono::duration<double> Duration;

    const NodeId n = 2000;
    const size_t m = 2000;
    const size_t k = 4;

    TimePt ibfs_start = std::chrono::system_clock::now();

    SubmodularIBFS ibfs;
    GenRandom(ibfs, n, k, m, (REAL)100, (REAL)800, (REAL)1600, 0);
    ibfs.Solve();

    Duration ibfs_time = std::chrono::system_clock::now() - ibfs_start;


    TimePt ho_start = std::chrono::system_clock::now();
    HOE ho;

    GenRandom(ho, n, k, m, (REAL)100, (REAL)800, (REAL)1600, 0);


    QPBO<REAL> qr(n, 0);
    ho.ToQuadratic(qr);
    qr.Solve();

    Duration ho_time = std::chrono::system_clock::now() - ho_start;

    TimePt sf_start = std::chrono::system_clock::now();

    SubmodularFlow sf;
    GenRandom(sf, n, k, m, (REAL)100, (REAL)800, (REAL)1600, 0);
    sf.PushRelabel();
    sf.ComputeMinCut();

    Duration sf_time = std::chrono::system_clock::now() - sf_start;

    size_t labeled = 0;
    size_t ones = 0;
    for (NodeId i = 0; i < n; ++i) {
        int label = ibfs.GetLabel(i);
        int sf_label = sf.GetLabel(i);
        if (label != qr.GetLabel(i)) {
            std::cout << "**WARNING: Different labels at pixel " << i << "**";
            std::cout << "\t" << qr.GetLabel(i) << "\t" << label << "\t" << sf_label << "\n";
        }
        if (label >= 0)
            labeled++;
        if (label == 1)
            ones++;
    }
    std::cout << "Labeled:     " << labeled << "\n";
    std::cout << "Ones:        " << ones << "\n";
    std::cout << "Energy:      " << sf.ComputeEnergy() << "\n";
    std::cout << "QR Energy:   " << qr.ComputeTwiceEnergy() << "\n";
    std::cout << "IBFS Energy: " << ibfs.ComputeEnergy() << "\n";
    std::cout << "HO time:     " << ho_time.count() << " seconds\n";
    std::cout << "SF time:     " << sf_time.count() << " seconds\n";
    std::cout << "IBFS time:   " << ibfs_time.count() << " seconds\n";
    
    ASSERT(qr.ComputeTwiceEnergy() == sf.ComputeEnergy()*2);
    ASSERT(qr.ComputeTwiceEnergy() == ibfs.ComputeEnergy()*2);

    return 0;
}
