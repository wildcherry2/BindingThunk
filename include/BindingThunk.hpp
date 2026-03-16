#pragma once
#include "Common.hpp"
#include "Context.hpp"

namespace RC::Thunk {

enum class EBindingThunkType
{
    Default, // Generates a simple binding thunk.
    Argument, // Generates a binding thunk that compacts unbound arguments into an ArgumentContext. Generate a RestoreThunk to unwrap the ArgumentContext to call a function with an identical SourceSignature.
    Register, // Generates a binding thunk that saves all non-argument registers to a side channel stack, and restores all non-argument registers to call a function in a corresponding RestoreThunk.
    ArgumentAndRegister, // Combination of Argument and Register options. Compacts unbound arguments into an ArgumentContext and saves non-argument registers.
};

// Binds a pointer to a function at runtime.
THUNK_API FThunkResult GenerateBindingThunk(void* ToFn, void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Default, bool bLogAssembly = false);

template<typename BindParamType, typename InReturnType, typename... InArgs>
FThunkResult GenerateBindingThunk(InReturnType(*ToFn)(BindParamType*, InArgs...), BindParamType* BindParam, const EBindingThunkType BindingType = EBindingThunkType::Default, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void*>(ToFn),
        reinterpret_cast<void*>(BindParam),
        FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>(),
        BindingType,
        bLogAssembly);
}

THUNK_API FThunkResult GenerateBindingThunk(void(*ToFn)(void*, ArgumentContext&), void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Argument, bool bLogAssembly = false);

template<typename BindParamType, typename InReturnType, typename... InArgs>
FThunkResult GenerateBindingThunk(void(*ToFn)(BindParamType*, ArgumentContext&), BindParamType* BindParam, EBindingThunkType Type = EBindingThunkType::Argument, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void(*)(void*, ArgumentContext &)>(ToFn), BindParam, FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>(), Type, bLogAssembly);
}

}
