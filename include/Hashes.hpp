#pragma once
#include <functional>
#include <asmjit/x86.h>

template<>
struct std::hash<asmjit::Operand> {
    using argument_type = asmjit::Operand;
    using result_type = size_t;

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