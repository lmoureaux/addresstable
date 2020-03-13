#include <array>
#include <iomanip>
#include <iostream>
#include <chrono>

#include "libmemsvc.h"

int main(int, char **)
{
    memsvc_handle_t handle;
    if (memsvc_open(&handle) != 0) {
        std::cout << "Failed to open: " << memsvc_get_last_error(handle) << std::endl;
        return 1;
    }

    for (int k = 0; k < 10; ++k) {
        int n = 1 << k;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < n; ++i) {
            std::array<std::uint32_t, 1> data;
            if (__builtin_expect(memsvc_read(
                        handle, 0x64000000, data.size(), data.data()) != 0,
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
            << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / n
            << " ns"
            << std::endl;
    }

    memsvc_close(&handle);
    return 0;
}
