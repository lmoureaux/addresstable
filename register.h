#ifndef REGISTER_H
#define REGISTER_H

#include <sstream>
#include <stdexcept>
#include <type_traits>

/**
 * \brief Represents a hardware register.
 *
 * Registers are named values in device memory that are mapped to an address in
 * the CPU memory space. They can be no wider than 32 bits, and sometimes a
 * register covers only part of a 32 bits word. Which bits are part of the
 * register is defined through a mask. Registers also come with read and write
 * permissions: not all registers can be read from or written to.
 *
 * Reading values from, and writing values to, registers is done through
 * \ref operator>> and \ref operator<<. Streaming operators were chosen to
 * remind the user that reads and writes are expensive operations.
 */
struct RegisterBase
{
    /// \brief The address of the register.
    std::uint32_t address;

    explicit constexpr RegisterBase(std::uint32_t address): address(address)
    {}
};

/**
 * \brief Represents a read-only register.
 */
struct RORegister: public RegisterBase
{
    /// \brief The mask of the register.
    std::uint32_t mask;

    explicit constexpr RORegister(std::uint32_t address, std::uint32_t mask):
        RegisterBase(address),
        mask(mask)
    {}
};

/**
 * \brief Represents a write-only register.
 */
struct WORegister: public RegisterBase
{
    explicit constexpr WORegister(std::uint32_t address): RegisterBase(address)
    {}
};

/**
 * \brief Represents a read-write register.
 */
struct RWRegister: public RegisterBase
{
    /// \brief The mask of the register.
    std::uint32_t mask;

    explicit constexpr RWRegister(std::uint32_t address, std::uint32_t mask):
        RegisterBase(address),
        mask(mask)
    {}
};

#if 0
Docs may be useful one day
/**
 * \brief Writes a value to a register.
 *
 * Complexity: one write.
 */
void WORegister::operator<<(std::uint32_t value) const
{
    volatile std::uint32_t *ptr = nullptr;
    ptr += (std::ptrdiff_t) address;
    *ptr = value;
}

/**
 * \brief Writes a value to a register.
 *
 * The mask is handled automatically. Example:
 *
 *     existing value in hw  00011101
 *     mask                  00111100
 *     value                 00001001
 *     ------------------------------
 *     new value in hw       00100101
 *
 * Complexity: one write if the mask is `0xffffffff`, one read and one write
 * otherwise.
 *
 * \throws std::logic_error if the register is not writeable.
 * \throws std::domain_error if `value` is too wide for the mask.
 */
void RWRegister::operator<<(std::uint32_t value) const
{
    volatile std::uint32_t *ptr = nullptr;
    ptr += (std::ptrdiff_t) address;

    if (mask == 0xffffffff) { // Shortcut
        *ptr = value;
    } else {
        // Assumption: The mask has no hole
        int shift = __builtin_ctz(mask);
        // Check bounds
        if (__builtin_expect((value & ~(mask >> shift)) != 0, false)) {
            std::stringstream ss;
            ss << std::hex << "Value 0x" << value
               << " out of bounds for register at address 0x" << address
               << " with mask 0x" << mask;
            throw std::domain_error(ss.str());
        }
        std::uint32_t old = *ptr;
        old ^= old & mask; // Reset bits that can be touched
        *ptr = old | (value << shift);
    }
}

/**
 * \brief Reads a value from a register.
 *
 * The mask is handled automatically. Example:
 *
 *     existing value in hw  00011101
 *     mask                  00111100
 *     ------------------------------
 *     new value in hw       00000111
 *
 * Complexity: one read.
 */
void RORegister::operator>>(std::uint32_t &value) const
{
    volatile std::uint32_t *ptr = nullptr;
    ptr += (std::ptrdiff_t) address;

    // Assumption: The mask has no hole
    int shift = __builtin_ctz(mask);
    value = (*ptr & mask) >> shift;
}
#endif

struct RegisterGenerator
{
    constexpr RWRegister operator()(std::uint32_t addr,
                                    std::uint32_t mask,
                                    std::true_type,
                                    std::true_type) const
    {
        return RWRegister(addr, mask);
    }

    constexpr RORegister operator()(std::uint32_t addr,
                                    std::uint32_t mask,
                                    std::true_type,
                                    std::false_type) const
    {
        return RORegister(addr, mask);
    }

    constexpr WORegister operator()(std::uint32_t addr,
                                    std::uint32_t mask,
                                    std::false_type,
                                    std::true_type) const
    {
        return WORegister(addr);
    }
};

#endif // REGISTER_H
