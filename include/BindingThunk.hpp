#pragma once
#include "Common.hpp"

enum class EBindingThunkType
{
    Default,                     // expects the signature of ToFn to match InReturnType(BindParamType*, InArgs...) and Signature to match InReturnType(InArgs...)
    WithRegisterContext, // expects the signature of ToFn to match InReturnType(BindParamType*, InArgs...) and Signature to match InReturnType(InArgs...)
    WithArgumentContext          // expects the signature of ToFn to match void(BindParamType*, Context), Signature will be assigned accordingly
};

FThunkPtr GenerateBindingThunk(void* ToFn, void* BindParam, FuncSignature Signature, EBindingThunkType Type = EBindingThunkType::Default);

template<typename BindParamType, typename InReturnType, typename... InArgs>
FThunkPtr GenerateBindingThunk(InReturnType(*ToFn)(BindParamType*, InArgs...), BindParamType* BindParam, const EBindingThunkType BindingType = EBindingThunkType::Default) {
    return GenerateBindingThunk(reinterpret_cast<void*>(ToFn),
        reinterpret_cast<void*>(BindParam),
        FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>(),
        BindingType);
}