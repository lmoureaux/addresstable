#include <chrono>
#include <iostream>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "registerstruct.h"

/**
 * \brief Switches a register (sub)tree to a different \a generator.
 *
 * The \a generator of a register tree defines its contents. The default tree
 * uses the \ref RegisterGenerator, which populates the tree with \ref Register
 * objects. By switching to a different generator, custom types and values can
 * be used.
 *
 * Given a generator class \c Generator, this function can be used as follows:
 * ~~~~{.cc}
 * Generator g;
 * auto &tree = switchGenerator(GEM_AMC, g);
 * ~~~~
 * The returned tree reproduces the hierarchy of \c GEM_AMC using the new
 * generator.
 *
 * Generators are created by defining a class with a public call operator,
 * <tt>operator()(uint32_t addr, uint32_t mask, bool read, bool write)</tt>.
 * When creating the tree, the call operator is executed for each register and
 * its return value is stored at the corresponding location in the tree.
 *
 * The following generator assigns an index to every register:
 * ~~~~{.cc}
 * struct IndexGenerator
 * {
 *     std::size_t count = 0;
 *     constexpr std::size_t operator()(std::uint32_t addr, std::uint32_t mask,
 *                                      bool read, bool write)
 *     {
 *         return count++;
 *     }
 * };
 * ~~~~
 */
template<class T, class Generator>
constexpr auto switchGenerator(const T &, Generator &gen)
    -> typename T::template _M_self<Generator>
{
    return typename T::template _M_self<Generator>(gen);
}

struct CollectAddressesGenerator
{
    struct void_t{};

    std::vector<std::uint32_t> &addresses;

    void_t operator()(std::uint32_t addr,
                      std::uint32_t mask,
                      bool read,
                      bool write)
    {
        addresses.push_back(addr);
        return {};
    }
};

template<class T>
void collectAddresses(const T &t, std::vector<std::uint32_t> &vec)
{
    CollectAddressesGenerator gen{vec};
    switchGenerator(t, gen);
}

struct IndexGenerator
{
    std::size_t count = 0;
    constexpr std::size_t operator()(std::uint32_t addr,
                                     std::uint32_t mask,
                                     bool read,
                                     bool write)
    {
        return count++;
    }
};

template<class T>
constexpr std::size_t countRegisters(const T &t)
{
    IndexGenerator gen{};
    switchGenerator<T, IndexGenerator>(t, gen);
    return gen.count;
}

int main(int, char **)
{
    std::cout << "*** struct ***" << std::endl;

    std::cout << "Loading took 0 ns" << std::endl;

    std::cout << "There are "
              << countRegisters(GEM_AMC)
              << " registers, and "
              << countRegisters(GEM_AMC.OH)
              << " in the OH subtree alone."
              << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    std::uint32_t sum = 0;
    for (int i = 0; i < 1000; ++i) {
        for (auto &oh : GEM_AMC.OH.OH) {
            for (auto &vfat : oh.GEB.VFATS.VFAT) {
                for (auto &ch : vfat.VFATChannels.ChanReg) {
                    sum += ch.PULSE.address; // Make sure it's a side effect
                    sum += ch.PULSE.mask;
                }
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::size_t n = GEM_AMC.OH.OH.size()
        * GEM_AMC.OH.OH[0].GEB.VFATS.VFAT.size()
        * GEM_AMC.OH.OH[0].GEB.VFATS.VFAT[0].VFATChannels.ChanReg.size();

    std::cout << "Lookup took "
              << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1000 / n / 1000
              << " ps/lookup/register"
              << std::endl;

    std::vector<std::uint32_t> x;
    x.resize(256<<10);
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i) {
        collectAddresses(GEM_AMC.OH, x);
    }
    end = std::chrono::high_resolution_clock::now();

    std::cout << std::accumulate(x.begin(), x.end(), 0uL) << std::endl;

    std::cout << "Vector took "
              << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1000 / (x.size())
              << " ps/collect/register"
              << std::endl;
    std::cout << "Control: " << sum << std::endl;
    return 0;
}
