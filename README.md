# BindingThunk
C++23 utility that allows you to generate bound functions at runtime without functors, with additional options for packing unbound arguments into a structure and saving/restoring register state.

## What does this library solve?
Lambdas, `std::function`, and `std::bind` offer an approach to binding arguments to a function through a functor. Those are useful if you're dealing with a use case that accepts `std::function` types, but if you're in a situation where you need a plain function pointer, you're out of luck.
* You cannot directly cast an `std::function` or the result of an `std::bind` expression to a function pointer.
* `std::function` exposes the `std::function::target` function, but to get a meaningful pointer you must provide the correct pointer type as a template argument, and even if you get a valid target, you can't just call it without the bound `std::function` if it captures any state. As a functor, the call operator is a member function, so it would be like calling a member function without the object's `this` pointer.
* Similarly, bound lambdas are also treated as functors with similar semantics to std::function with respect to casting and attempting to call the cast result. By the standard, attempting to get call `std::function::target` on a bound lambda will always produce `nullptr`, and attempting to cast the bound lambda to a raw function pointer will always be a compiler error at best and undefined behavior/crash at worst.
  * Casting an _unbound_ lambda is generally fine, though not very useful.

This is consistent with C++'s design; no code is generated at runtime, but in order to know the address of the bound data, it would have to be generated at runtime. That's what BindingThunk is for.

## General Use Cases
Generally, a thunk can be generated to bind a single pointer to the first argument position. This can theoretically be a pointer to anything, though the most useful option is to bind a `this` pointer to a member function, effectively making a free-function version of a member function that can be passed anywhere. Note that most examples are simpler free-function bindings. See [Member Function Bindings](#member-function-bindings) for more information.<br><br>
Thunks can also be generated that bind a pointer, pack unbound arguments a structure, call the target function with the structure and bound argument, and return the return value that's set in the structure. These `ArgumentContext`-based thunks can be used to 'normalize' the arguments of various functions and redirect function calls to a single function type.<br><br>
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

## Thunk Types and Usage
The library adds two primary functions, `GenerateBindingThunk` and `GenerateRestoreThunk`. Each function has various overloads for different kinds of thunk generation and template-based syntactic sugar. There are 4 different kinds of thunks that can be generated through `GenerateBindingThunk`, and some of those types can be paired with a thunk from `GenerateRestoreThunk` for special forwarding. Currently, you choose which type of thunk to generate through overload choice and an `EBindingThunkType` value. Note that there's nothing stopping you from taking the address of a stack-allocated variable and binding it, just make sure it outlives the generated thunk.
### `EBindingThunk::Default`: Binds the provided pointer argument to the provided function.
Suppose you have a function `void Fn(int* BindArg, double ArgTwo, bool ArgThree)`.<br><br>To bind a pointer declared as `int* Arg = new int(10)`, you'd call `auto Thunk = GenerateBindingThunk<EBindingThunkType::Default, int*, void, double, bool>(Fn, Arg)`.<br><br>To invoke the bound `Thunk`, you would do `reinterpret_cast<void(*)(double, bool)>(Thunk.get())(1.25, true)`, which will call `Fn` as if with `Fn(Arg, 1.25, true)`. Also note that in most cases the template parameters `GenerateBindingThunk` are optional, except the `EBindingThunk` flag; they are just included for illustration.
### `EBindingThunk::Argument`: Binds the provided pointer argument to a provided function that takes the remaining arguments in an `ArgumentContext` reference.
In many ways, this is similar to a C-style varargs function, but it allows structured, random access with a return value that's set within the structure rather than returned by the `return` keyword, though the thunk itself must be generated with a known argument list.<br><br>
Suppose you have a function `void Fn(int* BindArg, ArgumentContext& Context)`, and you want to make a thunk that's callable as `int(*)(double FloatingArg, bool BoolArg, int IntArg)` and invokes `Fn` to run it. Note that this also means that `Fn` must set a return value, otherwise it's the bitwise-equivalent of a default-constructed `uint64_t`, which in this case is `0`, but that may not be valid for other return types.<br><br>
To bind a function with the same `Arg` pointer given in the `EBindingThunk::Default` example, you'd call `auto Thunk = GenerateBindingThunk<EBindingThunkType::Argument, int*, int, double, bool, int>(Fn, Arg)`. For this overload, all template parameters are required since we can't infer what you want the thunk to be called with based on the signature of `Fn`.<br><br>
To invoke the bound `Thunk`, you would do `int Result = reinterpret_cast<int(*)(double, bool, int)>(Thunk.get())(1.25, true, 42)`.<br><br>
This type of thunk also has a corresponding optional _restore_ thunk that can be generated with the same signature. Restore thunks generally restore the arguments and registers to their original values, then call a function with the same signature. This is used in more advanced scenarios.
### `EBindingThunk::Register`: Binds the provided pointer argument to the provided function, and saves all non-argument registers to a thread-local stack.
From a C++ point-of-view, this is identical to `EBindingThunk::Default`. From an ABI point-of-view, this can be paired with a restore thunk, which, in this case, restores all non-argument registers from the thread-local stack. This is also used in more advanced scenarios for ABI stability, or for reading raw register values at the point where the thunk was called (but this probably shouldn't be used as part of a midhook).
### `EBindingThunk::Argument | EBindingThunk::Register`: Binds the provided pointer argument to a provided function that takes the remaining arguments in an `ArgumentContext` reference and saves all non-argument registers to a thread-local stack.
Similar story to the plain `EBindingThunk::Register`; for C++, it's identical to `EBindingThunk::Argument`, but can be paired with a restore thunk for advanced scenarios for ABI stability or reading raw register values (this also probably shouldn't be used as part of a midhook).

## Member Function Bindings
With the templated `GenerateBindingThunk` functions, you can easily generate bound member functions by supplying the member function pointer as a template parameter. So with this class: 
```c++
class TestClass {
    virtual void TestMemberFunction(int, bool, double);
    virtual void TestMemberFunctionArgContext(ArgumentContext&);
};

TestClass instance{};
```
you'd do `auto normalPtr = GenerateBindingThunk<&TestClass::TestMemberFunction>(&instance)` to bind the normal `TestMemberFunction` function or `auto argPtr = GenerateBindingThunk<&TestClass::TestMemberFunctionArgContext, EBindingThunkType::Default, float, double, int>(&instance)` to bind an `ArgumentContext`-based member function with a return type of `float` and 2 arguments of `double` and `int`. Then, to call them, you'd do `reinterpret_cast<void(*)(int, bool, double)>(normalPtr.get())(1, false, 2.0)` or `reinterpret_cast<float(*)(double, int)>(argPtr.get())(1.0, 2)`.

Note that without the templated helpers (`ABISignature`-based), you should bind to a static function that takes the `this` pointer with the other arguments and forward them to the member function. This is what's done by the templated helpers. The reason is so that  pointers-to-member-function types aren't plain pointers per the C++ standard, so they need to be handled by the compiler. Theoretically, you might get away without a static invoker if there is no vtable, or maybe even if the function isn't virtual (compiler-dependent), but it wouldn't be very stable, and the compiler optimizes the static function away anyway. 

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
