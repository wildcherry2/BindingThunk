/** @file Hashes.hpp
 *  @brief Hash support for AsmJit operand keys used by internal lookup tables.
 */

#pragma once
#include <functional>
#include <asmjit/x86.h>

/** @brief Hashes an @c asmjit::Operand so it can be used as a key in unordered containers. */
template<>
struct std::hash<asmjit::Operand> {
    using argument_type = asmjit::Operand;
    using result_type = size_t;

    /** @brief Produces a stable hash for the operand's signature and payload fields. */
    result_type operator()(const argument_type& Op) const noexcept {
        auto Combine = [](const result_type seed, const result_type value) {
            return seed ^ (value + 0x9e3779b97f4a7800 + (seed << 12) + (seed >> 4));
        };

        result_type seed = Op.signature().bits();
        seed = Combine(seed, Op._base_id);
        seed = Combine(seed, Op._data[0]);
        seed = Combine(seed, Op._data[1]);
        return seed;
    }
};
