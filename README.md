# BindingThunk
Utility that allows you to generate bound functions at runtime without functors, with additional options for packing unbound arguments into a structure and saving/restoring register state.

## What does this library solve?
`std::function` and `std::bind` both offer an approach to binding arguments to a function through a functor. Those are useful if you're dealing with a use case that accepts `std::function` types, but if you're in a situation where you need a plain function pointer, you're out of luck.
* You cannot directly cast an `std::function` or the result of an `std::bind` expression to a function pointer.
* `std::function` exposes the `std::function::target` function, but to get a meaningful pointer you must provide the correct pointer type as a template argument, and even if you get a valid target, you can't just call it without the bound `std::function` if it captures any state. As a functor, the call operator is a member function, so it would be like calling a member function without the object's `this` pointer.
* Similarly, bound lambdas are also treated as functors with similar semantics to std::function with respect to casting and attempting to call the cast result. By the standard, attempting to get call `std::function::target` on a bound lambda will always produce `nullptr`, and attempting to cast the bound lambda to a raw function pointer will always be undefined behavior at best, and a compiler error at worst.
  * Casting an _unbound_ lambda is generally fine, though not very useful.

This is consistent with C++'s design; no code is generated at runtime, but in order to know the address of the bound data, it would have to be generated at runtime. That's what BindingThunk is for.

## General Use Cases
Generally, a thunk can be generated to bind a single pointer to the first argument position. This can theoretically be a pointer to anything, though the most useful option is to bind a `this` pointer to a member function, effectively making a free-function version of a member function that can be passed anywhere.<br><br>
Thunks can also be generated that both bind a pointer, pack unbound arguments a structure, call the target function with the structure and bound argument, and return the return value that's set in the structure. These `ArgumentContext`-based thunks can be used to 'normalize' the arguments of various functions and redirect function calls to a single function type.<br><br>
For any generated thunk, the bound argument must outlive the thunk for safe use. A generated thunk is returned as a `std::unique_ptr` with a specialized deleter that cleans it up. Calling a generated thunk requires calling `get` on the returned `std::unique_ptr` and casting it to the correct function pointer type. If you're wondering why we don't carry over template parameters that cut out the need to cast, it's primarily to make the pointer easier to pass around in different environments.

## Build Instructions
This project uses CMake presets and expects a C++23-capable toolchain plus Ninja.

To build the default static library in Debug:
```bash
cmake --preset debug-static
cmake --build --preset debug-static
```

To build the static library with unit tests:
```bash
cmake --preset debug-static-tests
cmake --build --preset debug-static-tests
ctest --preset debug-static-tests
```

Other available presets include `debug-static-example`, `debug-shared`, `debug-shared-tests`, `debug-shared-example`, `release-static`, and `release-shared`.

## Platform-Specific and General Quirks
### General:
* If you use `ArgumentContext`, then you must retrieve arguments passed by reference as a pointer. So, if you have a thunk signature like `void Fn(int& Ref, double RandomArg)` which calls a bound function like `void Target(void* BoundValue, ArgumentContext& Context)`, then you'd retrieve the `Ref` argument with `Context.GetArgumentAs<int*>(0)` and use it like a pointer after verifying that it didn't return an error.

### Windows:
* Unwind data is generated according to [Microsoft's documentation](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170). Needed for: 
  * Proper exception handling; dumps may not generate properly without unwind data
  * Proper debugging support; debuggers may not show the stack frames above the bound functions without it. It should be noted that some debuggers (like LLDB) don't know how to read dynamic unwind data and may not show frames above the bound function regardless, but many others (like VS' debugger) can handle it properly.
* Another quirk with `ArgumentContext` regards how odd-sized or large user-defined structs passed by value are acquired through `GetArgumentAs` in Windows. [According to Microsoft, user-defined structs and unions that have a power of two size >= 8 bits and <= 64 bits are passed as if they were integers of the same size, otherwise they're passed by reference to a caller-allocated copy, and if they're greater than 64 bits, they're also passed by reference](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170#parameter-passing:~:text=Structs,aligned). For large structures > 64 bits, it always must be a reference, but for smaller structures, we use those rules strictly to determine if we should treat it as a reference and make a copy. However, the compiler may decide to pad a type to make it fit the requirement and pass it by value, or it may just pass it by value anyways. Similar to the return-by-value quirk (see below), the only way to tell is to look at the disassembly for the type in question and confirm if it's passed by reference or value based on its usage, or experiment. If the rules say it should be passed by reference, but it looks like it's being passed by value instead, make an `AsmJitCompatArg` specialization with the `Type` alias set to something that's always passed by value, such as `uint64_t`. In this case, you must make the specialization since that type trait is what's used to test the type.
* Using the templated thunk generator functions will fail if the return type can't be returned by value. What I mean by this is 'when the function returns, does the return value go directly into a register or do I write the return value to a passed in reference?' Many 'common' types, such as builtin types, pointers, and references won't have a problem, but it can be difficult to tell if a return type will actually be returned by value. [Microsoft vaguely mentions the type traits for determining whether a return type is by value](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170#return-values:~:text=User,RAX,-%2E), though they fail to give a modern C++ type_traits equivalent for figuring it out. Additionally, the compiler may make arbitrary decisions to make a type return by value if it can fit in a register, even if it violates the rules Microsoft outlines. Another problem is that when a type is returned by reference, [it augments the ABI signature of the function](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170#return-values:~:text=Otherwise,RAX) to include the reference to write the return value to as the first argument, and it must also return that reference in a register (`RAX` in Linux/Windows). So, the current implementation's behavior is to use those type traits that Microsoft mentioned to determine if a type is returned by value. This means that if a return type passes those type trait checks, then it's safe to assume it's returned by value (because in order to be returned by value at all, it must be less than 64 bits, but the type traits are more restrictive). If it doesn't pass those type trait checks, then it will intentionally fail to compile because we can't be sure if it returns by value or reference. You have a few choices at that point:
  1. Look at the disassembly and determine if the compiler returns by value or by reference. If it returns by value, then create an `AsmJitCompatRet` specialization for the type and set its `Type` alias to something that also returns by value, such as `uint64_t`. If it returns by reference, either manually build the `ABISignature` yourself, setting the first argument as an `Integral` type and the return value as an `Integral` type, then fill out the rest of your arguments (preferred) or make a C++ signature that matches the ABI-level reality of the return type (so if you have a function like `SomeBigStruct Fn()` you'd feed the generator function a signature like `SomeBigStruct& Fn(SomeBigStruct&)`).
    * If you generally want to avoid defining an `AsmJitCompatRet` specialization, you can also just build the `ABISignature` yourself.
  2. Do (i) but experimentally, meaning instead of looking at the disassembly to determine if it's by value or reference, you just try both and see what works.
### Linux:
* You can't use the convenient template-based builders for generating thunks. This is due to how the System V calling convention allows a single language-level argument to occupy multiple ABI-level argument slots, along with other differences that are difficult to model in C++. For instance, a 16 byte structure passed by value as the first argument would have its first 8 bytes passed into `RDI`, and the last 8 bytes passed into `RSI`, which isn't easily modeled with C++ type transformations. You must create an `ABISignature` object manually and set up its argument and return value types. Also note that, in this case, `ArgumentContext` would retrieve the argument in pieces. So calling `Context.GetArgumentAs<SixteenByteStruct>(0)` would return the first 8 bytes and calling `Context.GetArgumentAs<int*>(1)` would return the last 8 bytes. To make an `ABISignature` for the 16 byte structure, you would do:
```c++
using namespace BindingThunk;
ABISignature signature{};
signature.SetArgumentSlot(0, ABISignature::ArgumentType::Integral);
signature.SetArgumentSlot(1, ABISignature::ArgumentType::Integral);
...
```
* This has not generally been tested in a Linux environment. 
