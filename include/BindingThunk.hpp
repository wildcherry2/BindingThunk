#pragma once
#include "Common.hpp"
#include "Context.hpp"

namespace BindingThunk {

// Binds a pointer to a function at runtime.
// ToFn is the target function. It's first parameter is expected to be the type of BindParam.
// BindParam is the pointer to bind to ToFn's first parameter. It's lifetime must be the same as or exceed the generated thunk.
// SourceSignature is, effectively, the signature you want the thunk to be called with. It must have the same return type as ToFn, and all arguments except for the bound argument.
// Type is the type of thunk to generate. This overload only accepts Default and Register.
// bLogAssembly, when true, outputs the generated assembly to the logging function set through SetLogFn, or the default std::cout logger.
THUNK_API FThunkResult GenerateBindingThunk(void* ToFn, void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Default, bool bLogAssembly = false);

// Binds a pointer to a function at runtime.
// ToFn is the target function. It's first parameter is expected to be the type of BindParam.
// BindParam is the pointer to bind to ToFn's first parameter. It's lifetime must be the same as or exceed the generated thunk.
// Type is the type of thunk to generate. This overload only accepts Default and Register.
// bLogAssembly, when true, outputs the generated assembly to the logging function set through SetLogFn, or the default std::cout logger.
template<typename BindParamType, typename InReturnType, typename... InArgs>
THUNK_API FThunkResult GenerateBindingThunk(InReturnType(*ToFn)(BindParamType*, InArgs...), BindParamType* BindParam, const EBindingThunkType BindingType = EBindingThunkType::Default, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void*>(ToFn),
        reinterpret_cast<void*>(BindParam),
        FuncSignature::build<AsmJitCompatRetV<InReturnType>, AsmJitCompatArgV<InArgs>...>(),
        BindingType,
        bLogAssembly);
}

// Binds a pointer to a function at runtime and all other arguments to an ArgumentContext. The 0th argument in an ArgumentContext is the first unbound argument.
// ToFn is the target function. It must match the signature exactly.
// BindParam is the pointer to bind to ToFn's first parameter. It's lifetime must be the same as or exceed the generated thunk.
// SourceSignature is, effectively, the signature you want the thunk to be called with. Minimally, it must have a void return type set.
// Type is the type of thunk to generate. This overload only accepts Argument and ArgumentAndRegister. todo enforce through templates
// bLogAssembly, when true, outputs the generated assembly to the logging function set through SetLogFn, or the default std::cout logger.
THUNK_API FThunkResult GenerateBindingThunk(void(*ToFn)(void*, ArgumentContext&), void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Argument, bool bLogAssembly = false);

// Binds a pointer to a function at runtime and all other arguments to an ArgumentContext. The 0th argument in an ArgumentContext is the first unbound argument.
// ToFn is the target function. It must match the signature exactly.
// BindParam is the pointer to bind to ToFn's first parameter. It's lifetime must be the same as or exceed the generated thunk.
// Type is the type of thunk to generate. This overload only accepts Argument and ArgumentAndRegister. todo enforce through templates
// bLogAssembly, when true, outputs the generated assembly to the logging function set through SetLogFn, or the default std::cout logger.
template<typename BindParamType, typename InReturnType, typename... InArgs>
THUNK_API FThunkResult GenerateBindingThunk(void(*ToFn)(BindParamType*, ArgumentContext&), BindParamType* BindParam, EBindingThunkType Type = EBindingThunkType::Argument, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void(*)(void*, ArgumentContext &)>(ToFn), BindParam, FuncSignature::build<AsmJitCompatRetV<InReturnType>, AsmJitCompatArgV<InArgs>...>(), Type, bLogAssembly);
}

}
