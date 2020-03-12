#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <vector>

#include "registerstruct.h"
#include "settings.h"

int main(int, char **)
{
    std::cout << "*** struct ***" << std::endl;

    std::cout << "Loading took 0 ns" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    std::uint32_t *sum = 0;
    for (int i = 0; i < 1000; ++i) {
        for (auto &oh : GEM_AMC.OH.OH) {
            for (auto &vfat : oh.GEB.VFATS.VFAT) {
                for (auto &ch : vfat.VFATChannels.ChanReg) {
                    sum += (std::ptrdiff_t) ch.PULSE.address; // Make sure it's a side effect
                    sum += (std::ptrdiff_t) ch.PULSE.mask;
                }
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::size_t n = GEM_AMC.OH.OH.size()
        * GEM_AMC.OH.OH[0].GEB.VFATS.VFAT.size()
        * GEM_AMC.OH.OH[0].GEB.VFATS.VFAT[0].VFATChannels.ChanReg.size();

    std::cout << "Lookup took "
              << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / n / 1000
              << " ns/lookup/register"
              << std::endl;
    std::cout << "Control: " << sum << std::endl;
    return 0;
}
