#pragma once
#include "Common.hpp"
#include "Context.hpp"

enum class EBindingThunkType
{
    Default,
    Argument,
    Register,
    ArgumentAndRegister,
};

// Public binding modes:
// - Default: bound parameter only
// - Argument: bound parameter + ArgumentContext
// - Register: bound parameter + RegisterContextStack pairing
// - ArgumentAndRegister: bound parameter + ArgumentContext carrying RegisterContext
FThunkPtr GenerateBindingThunk(void* ToFn, void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Default, bool bLogAssembly = false);

template<typename BindParamType, typename InReturnType, typename... InArgs>
FThunkPtr GenerateBindingThunk(InReturnType(*ToFn)(BindParamType*, InArgs...), BindParamType* BindParam, const EBindingThunkType BindingType = EBindingThunkType::Default, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void*>(ToFn),
        reinterpret_cast<void*>(BindParam),
        FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>(),
        BindingType,
        bLogAssembly);
}

FThunkPtr GenerateBindingThunk(void(*ToFn)(void*, ArgumentContext&), void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Argument, bool bLogAssembly = false);

template<typename BindParamType, typename InReturnType, typename... InArgs>
FThunkPtr GenerateBindingThunk(void(*ToFn)(BindParamType*, ArgumentContext&), BindParamType* BindParam, EBindingThunkType Type = EBindingThunkType::Argument, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void(*)(void*, ArgumentContext &)>(ToFn), BindParam, FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>(), Type, bLogAssembly);
}
