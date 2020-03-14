#include <array>
#include <iomanip>
#include <iostream>
#include <chrono>

#include "memhub.h"

static memsvc_handle_t handle;

int main(int, char **) {

    if (memhub_open(&handle) != 0) {
        std::cout << "Failed to open: " << memsvc_get_last_error(handle) << std::endl;
        return 1;
    }

    const std::size_t blocksize = 1;
    for (int k = 0; k < 16; ++k) {
        int n = 1 << k;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < n; ++i) {
            std::array<std::uint32_t, blocksize> data;
            if (__builtin_expect(memhub_read(handle, 0x66400008, data.size(), data.data()), 
                                 false)) {
                std::cout << "Failed to read: " << memsvc_get_last_error(handle) << std::endl;
                return 1;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::cout
            << std::setw(10)
            << std::right
            << n
            << " "
            << std::setw(5)
            << std::right
            << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / n / blocksize
            << " ns"
            << std::endl;
    }

    memhub_close(&handle);
    return 0;
}
