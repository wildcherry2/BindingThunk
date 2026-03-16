#pragma once
#include <cstdint>
#include <type_traits>
#include <cstddef>

namespace RC::Thunk {

namespace Detail {
    template<typename T>
    concept ScalarOrReference = std::is_scalar_v<T> || std::is_reference_v<T>;

    template<typename T>
    concept ClassOrUnion = std::is_class_v<T> || std::is_union_v<T>;

    template<typename T, size_t MinimumBits = 1, size_t MaximumBits = 64>
    inline constexpr auto IsSizeOfTPowerOfTwo() -> bool {
        constexpr auto Size = sizeof(T) * 8;
        return (Size >= MinimumBits) && (Size <= MaximumBits) && ((Size & (Size - 1)) == 0);
    }

    template<typename T>
    struct AlwaysFalse : std::false_type {};

#if defined(_WIN32)
    template<typename T>
    concept ClassOrUnionValueActuallyPassedByValue = ClassOrUnion<T> && IsSizeOfTPowerOfTwo<T, 8>();

    template<typename T>
    concept ClassOrUnionValueActuallyPassedByReference = !ClassOrUnionValueActuallyPassedByValue<T>;

    template<typename T>
    concept ReturnedByValue = std::is_reference_v<T>
                                || (std::is_scalar_v<T> && sizeof(T) <= sizeof(uint64_t))
                                || (ClassOrUnion<T> && std::is_standard_layout_v<T> && std::is_trivial_v<T> && std::is_aggregate_v<T> && !std::is_polymorphic_v<T> && IsSizeOfTPowerOfTwo<T, 1>());

    template<typename T>
    concept ReturnedByReference = !ReturnedByValue<T>;

    template<typename T>
    struct AsmJitCompatArgNotSpecialized {
        using Type = std::conditional_t<ScalarOrReference<T> || ClassOrUnionValueActuallyPassedByValue<T>, T,
                        std::conditional_t<ClassOrUnionValueActuallyPassedByReference<T>, T&, AlwaysFalse<T>>>;
        static_assert(!std::is_same_v<Type, AlwaysFalse<T>>, "User-specialized AsmJitCompatArg is not supported! Make sure it's coerced into a pointer, reference, or scalar!");
    };

    template<typename T>
    struct AsmJitCompatRetNotSpecialized {
        using Type = std::conditional_t<std::is_void_v<T> || ReturnedByValue<T>, T, AlwaysFalse<T>>;
        static_assert(!std::is_same_v<Type, AlwaysFalse<T>>, "User-specialized AsmJitCompatRet is not supported! Make sure it's coerced into a pointer, reference, or scalar!");
    };
#else
    template<typename T>
    concept ClassOrUnionValueActuallyPassedByValue = false;

    template<typename T>
    concept ClassOrUnionValueActuallyPassedByReference = false;

    template<typename T>
    concept ReturnedByValue = false;

    template<typename T>
    concept ReturnedByReference = !ReturnedByValue<T>;

    template<typename T>
    struct AsmJitCompatArgNotSpecialized {
        using Type = AlwaysFalse<T>;
        static_assert(!std::is_same_v<Type, AlwaysFalse<T>>, "AsmJit-based template signature deduction is only supported on Windows. Build FuncSignature manually on this platform.");
    };

    template<typename T>
    struct AsmJitCompatRetNotSpecialized {
        using Type = AlwaysFalse<T>;
        static_assert(!std::is_same_v<Type, AlwaysFalse<T>>, "AsmJit-based template signature deduction is only supported on Windows. Build FuncSignature manually on this platform.");
    };
#endif
}

#if defined(_WIN32)
template<typename T>
struct AsmJitCompatArg {
    using Type = std::conditional_t<Detail::ScalarOrReference<T> || Detail::ClassOrUnionValueActuallyPassedByValue<T>, T,
                    std::conditional_t<Detail::ClassOrUnionValueActuallyPassedByReference<T>, T&, Detail::AlwaysFalse<T>>>;
    static_assert(!std::is_same_v<Type, Detail::AlwaysFalse<T>>, "T is not a supported type for argument type deduction! Manually make FuncSignature instead or specialize if you know it's passed by reference, pointer, or value!");
};

template<typename T>
struct AsmJitCompatRet {
    using Type = std::conditional_t<Detail::ReturnedByValue<T>, T, Detail::AlwaysFalse<T>>;
    static_assert(!std::is_same_v<Type, Detail::AlwaysFalse<T>>, "T is not a supported type for return type deduction! Manually make FuncSignature instead or specialize if you know it's returned by value!");
};

template<>
struct AsmJitCompatRet<void> {
    using Type = void;
};
#else
template<typename T>
struct AsmJitCompatArg {
    using Type = Detail::AlwaysFalse<T>;
    static_assert(!std::is_same_v<Type, Detail::AlwaysFalse<T>>, "AsmJit-based template signature deduction is only supported on Windows. Build FuncSignature manually on this platform.");
};

template<typename T>
struct AsmJitCompatRet {
    using Type = Detail::AlwaysFalse<T>;
    static_assert(!std::is_same_v<Type, Detail::AlwaysFalse<T>>, "AsmJit-based template signature deduction is only supported on Windows. Build FuncSignature manually on this platform.");
};
#endif

template<typename T>
using AsmJitCompatArgV = Detail::AsmJitCompatArgNotSpecialized<typename AsmJitCompatArg<T>::Type>::Type;

template<typename T>
using AsmJitCompatRetV = Detail::AsmJitCompatRetNotSpecialized<typename AsmJitCompatRet<T>::Type>::Type;

}