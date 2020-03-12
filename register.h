#ifndef REGISTER_H
#define REGISTER_H

#include <sstream>
#include <stdexcept>

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
struct Register
{
    /// \brief The address of the register.
    std::uint32_t *address;

    /// \brief The mask of the register.
    std::uint32_t mask;

    /// \brief Whether the register can be read from.
    bool canRead;

    /// \brief Whether the register can be written to.
    bool canWrite;

    inline void operator<<(std::uint32_t value) const;

    inline void operator>>(std::uint32_t &value) const;
};

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
void Register::operator<<(std::uint32_t value) const
{
    /*
     * Assumptions:
     *  - The mask has no hole
     *  - A writeable, masked register is also readable
     */
    // Check that we can write
    if (__builtin_expect(!canWrite, false)) {
        std::stringstream ss;
        ss << "Cannot write to register at address " << address;
        throw std::logic_error(ss.str());
    }
    volatile std::uint32_t *ptr = address;
    if (mask == 0xffffffff) { // Shortcut
        *ptr = value;
    } else {
        int shift = __builtin_ctz(mask);
        // Check bounds
        if (__builtin_expect((value & ~(mask >> shift)) != 0, false)) {
            std::stringstream ss;
            ss << std::hex << "Value 0x" << value
               << " out of bounds for register at address " << address
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
 *
 * \throws std::logic_error if the register is not readable.
 */
void Register::operator>>(std::uint32_t &value) const
{
    /* Assumption: The mask has no hole */
    if (__builtin_expect(!canRead, false)) {
        std::stringstream ss;
        ss << "Cannot read from register at address " << address;
        throw std::logic_error(ss.str());
    }
    volatile std::uint32_t *ptr = address;
    int shift = __builtin_ctz(mask);
    value = (*ptr & mask) >> shift;
}

#endif // REGISTER_H
