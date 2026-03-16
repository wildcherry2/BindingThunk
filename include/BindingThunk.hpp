#pragma once
#include "Common.hpp"
#include "Context.hpp"

namespace RC::Thunk {

// Binds a pointer to a function at runtime.
THUNK_API FThunkResult GenerateBindingThunk(void* ToFn, void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Default, bool bLogAssembly = false);

template<typename BindParamType, typename InReturnType, typename... InArgs>
THUNK_API FThunkResult GenerateBindingThunk(InReturnType(*ToFn)(BindParamType*, InArgs...), BindParamType* BindParam, const EBindingThunkType BindingType = EBindingThunkType::Default, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void*>(ToFn),
        reinterpret_cast<void*>(BindParam),
        FuncSignature::build<AsmJitCompatRetV<InReturnType>, AsmJitCompatArgV<InArgs>...>(),
        BindingType,
        bLogAssembly);
}

THUNK_API FThunkResult GenerateBindingThunk(void(*ToFn)(void*, ArgumentContext&), void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Argument, bool bLogAssembly = false);

template<typename BindParamType, typename InReturnType, typename... InArgs>
THUNK_API FThunkResult GenerateBindingThunk(void(*ToFn)(BindParamType*, ArgumentContext&), BindParamType* BindParam, EBindingThunkType Type = EBindingThunkType::Argument, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void(*)(void*, ArgumentContext &)>(ToFn), BindParam, FuncSignature::build<AsmJitCompatRetV<InReturnType>, AsmJitCompatArgV<InArgs>...>(), Type, bLogAssembly);
}

}