#include "Tests.hpp"

#include <gtest/gtest.h>

#include <csetjmp>
#include <cstring>
#include <cstdint>
#include <memory>

#if defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

using namespace RC::Thunk;

static FThunkPtr GRestoreThunk{};

static std::jmp_buf GRegisterContextStackJumpBuffer{};
static const char* GRegisterContextStackFatalMessage{};

static void ThrowingRegisterContextStackFatalHandler(const char* Message) {
    GRegisterContextStackFatalMessage = Message;
    std::longjmp(GRegisterContextStackJumpBuffer, 1);
}

static std::unique_ptr<std::byte[]> MakeArgumentContextStorage(const size_t ArgumentsCount) {
    auto Storage = std::make_unique<std::byte[]>(ArgumentContext::ArgumentContextNonVariableSize + (ArgumentContext::ArgumentSize * ArgumentsCount));
    std::memset(Storage.get(), 0, ArgumentContext::ArgumentContextNonVariableSize + (ArgumentContext::ArgumentSize * ArgumentsCount));
    const auto ArgumentsCountValue = static_cast<uint64_t>(ArgumentsCount);
    std::memcpy(Storage.get() + ArgumentContext::ArgsCountOffset, &ArgumentsCountValue, sizeof(ArgumentsCountValue));
    return Storage;
}

static ArgumentContext& GetArgumentContext(std::unique_ptr<std::byte[]>& Storage) {
    return *reinterpret_cast<ArgumentContext*>(Storage.get());
}

template<typename T>
static T ReadArgumentContextField(const ArgumentContext& Context, const uint64_t Offset) {
    auto Raw = *reinterpret_cast<const uint64_t*>(reinterpret_cast<const std::byte*>(&Context) + Offset);
    return std::bit_cast<T>(Raw);
}

template<typename T>
static void WriteArgumentContextField(ArgumentContext& Context, const uint64_t Offset, const T& Value) {
    static_assert(sizeof(T) <= sizeof(uint64_t), "Only types convertible to uint64_t supported!");
    uint64_t Raw{};
    std::memcpy(&Raw, &Value, sizeof(T));
    std::memcpy(reinterpret_cast<std::byte*>(&Context) + Offset, &Raw, sizeof(Raw));
}

static std::string NarrowForTest(const std::wstring_view Message) {
    std::string Narrow{};
    Narrow.reserve(Message.size());
    for (const auto Character : Message) {
        Narrow.push_back(static_cast<char>(Character));
    }
    return Narrow;
}

static std::ostream& operator<<(std::ostream& Stream, const std::wstring& Message) {
    Stream << NarrowForTest(Message);
    return Stream;
}

template<typename T>
static T GetArgumentValueOrFail(const ArgumentContext& Context, const uint64_t Index) {
    const auto Result = Context.GetArgumentAs<T>(Index);
    EXPECT_TRUE(Result.has_value());
    if (!Result.has_value()) {
        return T {};
    }
    return Result.value();
}

struct FScopedLogFunctionOverride {
    LogFn LogFunction = GetLogFunction();
    LogFn ErrorLogFunction = GetErrorLogFunction();

    ~FScopedLogFunctionOverride() {
        SetLogFunction(std::move(LogFunction));
        SetErrorLogFunction(std::move(ErrorLogFunction));
    }
};

#if defined(_WIN64)
static PRUNTIME_FUNCTION LookupThunkRuntimeFunction(const void* Thunk) {
    DWORD64 ImageBase{};
    return RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(Thunk), &ImageBase, nullptr);
}
#endif

struct DefaultNoArgBinder {
    int calls{};
};

static int DefaultNoArgCallback(DefaultNoArgBinder* Binder) {
    ++Binder->calls;
    return 41;
}

struct DefaultSmallBinder {
    int calls{};
    int lastValue{};
    int lastReference{};
    int lastPointed{};
    int output{99};
};

static int* DefaultSmallCallback(DefaultSmallBinder* Binder, int Value, int& ReferenceValue, int* PointerValue) {
    ++Binder->calls;
    Binder->lastValue = Value;
    Binder->lastReference = ReferenceValue;
    Binder->lastPointed = *PointerValue;
    ReferenceValue += 10;
    *PointerValue += 20;
    return &Binder->output;
}

struct DefaultSimpleXmmBinder {
    int calls{};
    double first{};
    float second{};
};

static float DefaultSimpleXmmCallback(DefaultSimpleXmmBinder* Binder, double First, float Second) {
    ++Binder->calls;
    Binder->first = First;
    Binder->second = Second;
    return static_cast<float>(First + Second);
}

struct DefaultComplexBinder {
    int calls{};
    int a{};
    int b{};
    int c{};
    intptr_t d{};
    void* e{};
};

static float DefaultComplexCallback(DefaultComplexBinder* Binder, int A, int& B, int* C, intptr_t D, void* E) {
    ++Binder->calls;
    Binder->a = A;
    Binder->b = B;
    Binder->c = *C;
    Binder->d = D;
    Binder->e = E;
    B += 3;
    *C += 4;
    return static_cast<float>(A + B + *C + D + reinterpret_cast<uintptr_t>(E));
}

struct DefaultComplexFloatIntBinder {
    int calls{};
    int first{};
    float second{};
    int third{};
    int fourth{};
    int fifth{};
};

static int DefaultComplexFloatIntCallback(DefaultComplexFloatIntBinder* Binder, int First, float Second, int Third, int Fourth, int Fifth) {
    ++Binder->calls;
    Binder->first = First;
    Binder->second = Second;
    Binder->third = Third;
    Binder->fourth = Fourth;
    Binder->fifth = Fifth;
    return First + static_cast<int>(Second) + Third + Fourth + Fifth;
}

struct DefaultComplexVoidBinder {
    int calls{};
    int a{};
    int b{};
    int c{};
    int d{};
    float e{};
};

static void DefaultComplexVoidCallback(DefaultComplexVoidBinder* Binder, int A, int B, int C, int D, float E) {
    ++Binder->calls;
    Binder->a = A;
    Binder->b = B;
    Binder->c = C;
    Binder->d = D;
    Binder->e = E;
}

struct RegisterLargeState {
    int callbackCalls{};
    int originalCalls{};
    bool sawRegisterContext{};
    int value{};
    int referenced{};
    int pointed{};
    double floatingValue{};
    double floatingReferenced{};
    double floatingPointed{};
};

static RegisterLargeState* GRegisterLargeState{};

static int64_t OriginalRegisterLarge(int Value, int& ReferenceValue, int* PointerValue, double FloatingValue, double& FloatingReference, double* FloatingPointer) {
    EXPECT_NE(GRegisterLargeState, nullptr);
    if (!GRegisterLargeState) return 0;
    ++GRegisterLargeState->originalCalls;
    EXPECT_EQ(Value, 7);
    EXPECT_EQ(ReferenceValue, 11);
    EXPECT_EQ(*PointerValue, 13);
    EXPECT_DOUBLE_EQ(FloatingValue, 1.25);
    EXPECT_DOUBLE_EQ(FloatingReference, 2.5);
    EXPECT_DOUBLE_EQ(*FloatingPointer, 3.75);

    ReferenceValue += 2;
    *PointerValue += 3;
    FloatingReference += 1.5;
    *FloatingPointer += 2.5;
    return static_cast<int64_t>(Value + ReferenceValue + *PointerValue + FloatingValue + FloatingReference + *FloatingPointer);
}

static int64_t RegisterLargeCallback(RegisterLargeState* State, int Value, int& ReferenceValue, int* PointerValue, double FloatingValue, double& FloatingReference, double* FloatingPointer) {
    ++State->callbackCalls;
    State->sawRegisterContext = RegisterContextStack::Top() != nullptr;
    State->value = Value;
    State->referenced = ReferenceValue;
    State->pointed = *PointerValue;
    State->floatingValue = FloatingValue;
    State->floatingReferenced = FloatingReference;
    State->floatingPointed = *FloatingPointer;
    return ThunkCast<int64_t(*)(int, int&, int*, double, double&, double*)>(GRestoreThunk)(
        Value, ReferenceValue, PointerValue, FloatingValue, FloatingReference, FloatingPointer);
}

struct RegisterStackShiftState {
    int callbackCalls{};
    int originalCalls{};
    int a{};
    int b{};
    int c{};
    int d{};
};

static RegisterStackShiftState* GRegisterStackShiftState{};

static float OriginalRegisterStackShift(int A, int B, int C, int D) {
    EXPECT_NE(GRegisterStackShiftState, nullptr);
    if (!GRegisterStackShiftState) return 0.0f;
    ++GRegisterStackShiftState->originalCalls;
    EXPECT_EQ(A, 1);
    EXPECT_EQ(B, 2);
    EXPECT_EQ(C, 3);
    EXPECT_EQ(D, 4);
    return static_cast<float>(A + B + C + D);
}

static float RegisterStackShiftCallback(RegisterStackShiftState* State, int A, int B, int C, int D) {
    ++State->callbackCalls;
    State->a = A;
    State->b = B;
    State->c = C;
    State->d = D;
    return ThunkCast<float(*)(int, int, int, int)>(GRestoreThunk)(A, B, C, D);
}

struct RegisterXmmState {
    int callbackCalls{};
    int originalCalls{};
    double first{};
    float second{};
};

static RegisterXmmState* GRegisterXmmState{};

static float OriginalRegisterXmm(double First, float Second) {
    EXPECT_NE(GRegisterXmmState, nullptr);
    if (!GRegisterXmmState) return 0.0f;
    ++GRegisterXmmState->originalCalls;
    EXPECT_DOUBLE_EQ(First, 2.25);
    EXPECT_FLOAT_EQ(Second, 3.5f);
    return static_cast<float>(First + Second);
}

static float RegisterXmmCallback(RegisterXmmState* State, double First, float Second) {
    ++State->callbackCalls;
    State->first = First;
    State->second = Second;
    return ThunkCast<float(*)(double, float)>(GRestoreThunk)(First, Second);
}

struct ArgumentNoArgBinder {
    int calls{};
    bool hasRegisterContext{};
    bool hasReturnValue{};
    uint64_t argsCount{};
};

static int64_t OriginalArgumentNoArgs() {
    return 123;
}

static void ArgumentNoArgCallback(ArgumentNoArgBinder* Binder, ArgumentContext& Context) {
    ++Binder->calls;
    Binder->hasRegisterContext = Context.HasRegisterContext();
    Binder->hasReturnValue = Context.HasReturnValue();
    Binder->argsCount = Context.GetArgumentsCount();
    Context.SetReturnValue(ThunkCast<int64_t(*)(ArgumentContext&)>(GRestoreThunk)(Context));
}

struct ArgumentSmallBinder {
    int calls{};
    bool hasRegisterContext{};
    bool hasReturnValue{};
    uint64_t argsCount{};
    int value{};
    int referenced{};
    int pointed{};
    int originalCalls{};
};

static ArgumentSmallBinder* GArgumentSmallBinder{};

static int64_t OriginalArgumentSmall(int Value, int& ReferenceValue, int* PointerValue) {
    EXPECT_NE(GArgumentSmallBinder, nullptr);
    if (!GArgumentSmallBinder) return 0;
    ++GArgumentSmallBinder->originalCalls;
    EXPECT_EQ(Value, 3);
    EXPECT_EQ(ReferenceValue, 5);
    EXPECT_EQ(*PointerValue, 7);

    ReferenceValue += 4;
    *PointerValue += 6;
    return Value + ReferenceValue + *PointerValue;
}

static void ArgumentSmallCallback(ArgumentSmallBinder* Binder, ArgumentContext& Context) {
    ++Binder->calls;
    Binder->hasRegisterContext = Context.HasRegisterContext();
    Binder->hasReturnValue = Context.HasReturnValue();
    Binder->argsCount = Context.GetArgumentsCount();
    Binder->value = static_cast<int>(GetArgumentValueOrFail<uint64_t>(Context, 0));
    Binder->referenced = *GetArgumentValueOrFail<int*>(Context, 1);
    Binder->pointed = *GetArgumentValueOrFail<int*>(Context, 2);
    Context.SetReturnValue(ThunkCast<int64_t(*)(ArgumentContext&)>(GRestoreThunk)(Context));
}

struct ArgumentRegisterLargeBinder {
    int calls{};
    bool hasRegisterContext{};
    bool hasReturnValue{};
    uint64_t argsCount{};
    int value{};
    int referenced{};
    int pointed{};
    double floatingValue{};
    double floatingReferenced{};
    double floatingPointed{};
    int originalCalls{};
};

static ArgumentRegisterLargeBinder* GArgumentRegisterLargeBinder{};

static int* OriginalArgumentRegisterLarge(int Value, int& ReferenceValue, int* PointerValue, double FloatingValue, double& FloatingReference, double* FloatingPointer) {
    EXPECT_NE(GArgumentRegisterLargeBinder, nullptr);
    if (!GArgumentRegisterLargeBinder) return nullptr;
    ++GArgumentRegisterLargeBinder->originalCalls;
    EXPECT_EQ(Value, 9);
    EXPECT_EQ(ReferenceValue, 10);
    EXPECT_EQ(*PointerValue, 11);
    EXPECT_DOUBLE_EQ(FloatingValue, 1.5);
    EXPECT_DOUBLE_EQ(FloatingReference, 2.5);
    EXPECT_DOUBLE_EQ(*FloatingPointer, 3.5);

    ReferenceValue += 1;
    *PointerValue += 2;
    FloatingReference += 3.5;
    *FloatingPointer += 4.5;
    return PointerValue;
}

static void ArgumentRegisterLargeCallback(ArgumentRegisterLargeBinder* Binder, ArgumentContext& Context) {
    ++Binder->calls;
    Binder->hasRegisterContext = Context.HasRegisterContext();
    Binder->hasReturnValue = Context.HasReturnValue();
    Binder->argsCount = Context.GetArgumentsCount();
    Binder->value = static_cast<int>(GetArgumentValueOrFail<uint64_t>(Context, 0));
    Binder->referenced = *GetArgumentValueOrFail<int*>(Context, 1);
    Binder->pointed = *GetArgumentValueOrFail<int*>(Context, 2);
    Binder->floatingValue = GetArgumentValueOrFail<double>(Context, 3);
    Binder->floatingReferenced = *GetArgumentValueOrFail<double*>(Context, 4);
    Binder->floatingPointed = *GetArgumentValueOrFail<double*>(Context, 5);
    Context.SetReturnValue(ThunkCast<int*(*)(ArgumentContext&)>(GRestoreThunk)(Context));
}

struct ArgumentFloatStackBinder {
    int calls{};
    int originalCalls{};
    int a{};
    int b{};
    int c{};
    int d{};
    float e{};
};

static ArgumentFloatStackBinder* GArgumentFloatStackBinder{};

static float OriginalArgumentFloatStack(int A, int B, int C, int D, float E) {
    EXPECT_NE(GArgumentFloatStackBinder, nullptr);
    if (!GArgumentFloatStackBinder) return 0.0f;
    ++GArgumentFloatStackBinder->originalCalls;
    EXPECT_EQ(A, 1);
    EXPECT_EQ(B, 2);
    EXPECT_EQ(C, 3);
    EXPECT_EQ(D, 4);
    EXPECT_FLOAT_EQ(E, 5.5f);
    return static_cast<float>(A + B + C + D) + E;
}

static void ArgumentFloatStackCallback(ArgumentFloatStackBinder* Binder, ArgumentContext& Context) {
    ++Binder->calls;
    Binder->a = static_cast<int>(GetArgumentValueOrFail<uint64_t>(Context, 0));
    Binder->b = static_cast<int>(GetArgumentValueOrFail<uint64_t>(Context, 1));
    Binder->c = static_cast<int>(GetArgumentValueOrFail<uint64_t>(Context, 2));
    Binder->d = static_cast<int>(GetArgumentValueOrFail<uint64_t>(Context, 3));
    Binder->e = std::bit_cast<float>(static_cast<uint32_t>(GetArgumentValueOrFail<uint64_t>(Context, 4)));

    const auto ReturnValue = ThunkCast<float(*)(ArgumentContext&)>(GRestoreThunk)(Context);
    Context.SetReturnValue(static_cast<uint64_t>(std::bit_cast<uint32_t>(ReturnValue)));
}

static void UnsupportedArgumentCallback(void*, ArgumentContext&) {}

TEST(BindingThunkTests, DefaultNoArgsScalarReturn) {
    DefaultNoArgBinder Binder{};

    auto BindThunkResult = GenerateBindingThunk(&DefaultNoArgCallback, &Binder, EBindingThunkType::Default);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<int(*)()>(BindThunk);
    EXPECT_EQ(BoundFn(), 41);
    EXPECT_EQ(Binder.calls, 1);
}

TEST(ContextTests, GetArgumentAsReadsRawBits) {
    auto Storage = MakeArgumentContextStorage(1);
    auto& Context = GetArgumentContext(Storage);

    WriteArgumentContextField(Context, ArgumentContext::ArgsOffset, 0x123456789abcdef0ULL);

    const auto Result = Context.GetArgumentAs<uint64_t>(0);
    ASSERT_TRUE(Result.has_value());
    EXPECT_EQ(Result.value(), 0x123456789abcdef0ULL);
}

TEST(ContextTests, GetArgumentAsSupportsQualifiedScalarTypes) {
    auto Storage = MakeArgumentContextStorage(1);
    auto& Context = GetArgumentContext(Storage);
    const double Value = 6.25;

    WriteArgumentContextField(Context, ArgumentContext::ArgsOffset, Value);

    const auto Result = Context.GetArgumentAs<double>(0);
    ASSERT_TRUE(Result.has_value());
    EXPECT_DOUBLE_EQ(Result.value(), 6.25);
}

TEST(ContextTests, GetArgumentAsReadsScalarAndPointerArgumentsFromRawStorage) {
    auto Storage = MakeArgumentContextStorage(2);
    auto& Context = GetArgumentContext(Storage);
    int64_t Scalar = 44;
    int Pointed = 17;
    int* Pointer = &Pointed;

    WriteArgumentContextField(Context, ArgumentContext::ArgsOffset, Scalar);
    WriteArgumentContextField(Context, ArgumentContext::ArgsOffset + ArgumentContext::ArgumentSize, Pointer);

    const auto ScalarResult = Context.GetArgumentAs<int64_t>(0);
    const auto PointerResult = Context.GetArgumentAs<int*>(1);
    ASSERT_TRUE(ScalarResult.has_value());
    ASSERT_TRUE(PointerResult.has_value());
    EXPECT_EQ(ScalarResult.value(), 44);
    EXPECT_EQ(PointerResult.value(), &Pointed);
}

TEST(ContextTests, GetArgumentAsPreservesPointerQualifiers) {
    auto Storage = MakeArgumentContextStorage(1);
    auto& Context = GetArgumentContext(Storage);
    volatile int64_t Value = 91;
    const volatile int64_t* Pointer = &Value;

    WriteArgumentContextField(Context, ArgumentContext::ArgsOffset, Pointer);

    const auto Result = Context.GetArgumentAs<const volatile int64_t*>(0);
    ASSERT_TRUE(Result.has_value());
    EXPECT_EQ(Result.value(), Pointer);
}

TEST(ContextTests, GetArgumentAsReturnsOutOfBoundsErrorForInvalidIndex) {
    auto Storage = MakeArgumentContextStorage(1);
    auto& Context = GetArgumentContext(Storage);

    const auto Result = Context.GetArgumentAs<uint64_t>(1);

    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error(), EThunkErrorCode::ArgumentContextOutOfBoundsArgumentIndex);
}

TEST(ContextTests, SetReturnValueSupportsRawAndTypedWrites) {
    auto Storage = MakeArgumentContextStorage(0);
    auto& Context = GetArgumentContext(Storage);
    int Value = 13;

    Context.SetReturnValue(0xfeedfacecafebeefULL);
    EXPECT_EQ(ReadArgumentContextField<uint64_t>(Context, ArgumentContext::ReturnValueOffset), 0xfeedfacecafebeefULL);

    Context.SetReturnValue(&Value);
    EXPECT_EQ(ReadArgumentContextField<int*>(Context, ArgumentContext::ReturnValueOffset), &Value);
}

TEST(ContextTests, RegisterContextStackUnderflowInvokesFatalHandler) {
    ResetRegisterContextStackFatalHandler();
    SetRegisterContextStackFatalHandler(&ThrowingRegisterContextStackFatalHandler);
    GRegisterContextStackFatalMessage = nullptr;

    if (setjmp(GRegisterContextStackJumpBuffer) == 0) {
        RegisterContextStack::Pop();
        FAIL() << "Expected RegisterContextStack::Pop to invoke the fatal handler.";
    }

    ASSERT_NE(GRegisterContextStackFatalMessage, nullptr);
    EXPECT_STREQ(GRegisterContextStackFatalMessage, "RegisterContextStack: Stack underflow");
    ResetRegisterContextStackFatalHandler();
}

TEST(ContextTests, RegisterContextStackTopOnEmptyInvokesFatalHandler) {
    ResetRegisterContextStackFatalHandler();
    SetRegisterContextStackFatalHandler(&ThrowingRegisterContextStackFatalHandler);
    GRegisterContextStackFatalMessage = nullptr;

    if (setjmp(GRegisterContextStackJumpBuffer) == 0) {
        static_cast<void>(RegisterContextStack::Top());
        FAIL() << "Expected RegisterContextStack::Top to invoke the fatal handler.";
    }

    ASSERT_NE(GRegisterContextStackFatalMessage, nullptr);
    EXPECT_STREQ(GRegisterContextStackFatalMessage, "RegisterContextStack: Stack empty");
    ResetRegisterContextStackFatalHandler();
}

TEST(ContextTests, RegisterContextStackOverflowInvokesFatalHandler) {
    ResetRegisterContextStackFatalHandler();
    SetRegisterContextStackFatalHandler(&ThrowingRegisterContextStackFatalHandler);
    GRegisterContextStackFatalMessage = nullptr;

    if (setjmp(GRegisterContextStackJumpBuffer) == 0) {
        RegisterContext Context{};
        for (int Index = 0; Index < 257; ++Index) {
            RegisterContextStack::Push(&Context);
        }
        FAIL() << "Expected RegisterContextStack::Push to invoke the fatal handler.";
    }

    ASSERT_NE(GRegisterContextStackFatalMessage, nullptr);
    EXPECT_STREQ(GRegisterContextStackFatalMessage, "RegisterContextStack: Stack overflow");
    ResetRegisterContextStackFatalHandler();
}

TEST(CommonTests, GetLoggerIsStableAndInitializeCodeHolderUsesIt) {
    auto* Logger = GetAsmJitLogger();
    ASSERT_NE(Logger, nullptr);
    EXPECT_EQ(GetAsmJitLogger(), Logger);

#if defined(THUNK_SHARED)
    GTEST_SKIP() << "InitializeCodeHolder is not exercised across a shared-library boundary.";
#else
    CodeHolder CodeWithLogger{};
    InitializeCodeHolder(CodeWithLogger, true);
    EXPECT_EQ(CodeWithLogger.logger(), Logger);

    CodeHolder CodeWithoutLogger{};
    InitializeCodeHolder(CodeWithoutLogger, false);
    EXPECT_EQ(CodeWithoutLogger.logger(), nullptr);
#endif
}

TEST(CommonTests, SetLogFunctionOverridesGetterAndAsmJitLoggerOutput) {
    FScopedLogFunctionOverride Scope {};
    std::wstring Captured{};

    SetLogFunction([&Captured](const std::wstring_view Message) {
        Captured.append(Message);
    });

    const auto Logger = GetLogFunction();
    ASSERT_TRUE(static_cast<bool>(Logger));
    Logger(L"manual-log-line");
    EXPECT_EQ(Captured, L"manual-log-line");

    Captured.clear();
    constexpr char Message[] = "jit-log-line";
    EXPECT_EQ(GetAsmJitLogger()->log(Message, sizeof(Message) - 1), asmjit::kErrorOk);
    EXPECT_EQ(Captured, L"jit-log-line");
}

TEST(CommonTests, SetErrorLogFunctionOverridesGetterAndAsmJitErrorHandlerOutput) {
    FScopedLogFunctionOverride Scope {};
    std::wstring Captured{};

    SetErrorLogFunction([&Captured](const std::wstring_view Message) {
        Captured.append(Message);
    });

    const auto Logger = GetErrorLogFunction();
    ASSERT_TRUE(static_cast<bool>(Logger));
    Logger(L"manual-error-line");
    EXPECT_EQ(Captured, L"manual-error-line");

    Captured.clear();
    GetAsmJitErrorHandler()->handle_error(asmjit::Error::kInvalidState, "jit-error-line", nullptr);
    EXPECT_NE(Captured.find(L"AsmJit error "), std::wstring::npos);
    EXPECT_NE(Captured.find(L"jit-error-line"), std::wstring::npos);
}

TEST(CommonTests, GetLoggerWritesToStdout) {
    testing::internal::CaptureStdout();
    constexpr char Message[] = "jit-log-line";
    auto Error = GetAsmJitLogger()->log(Message, sizeof(Message) - 1);
    auto Output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(Error, asmjit::kErrorOk);
    EXPECT_EQ(Output, "jit-log-line\n");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(BindingThunkTests, DefaultSmallMixedArgumentsAndPointerReturn) {
    DefaultSmallBinder Binder{};
    int ReferenceValue = 5;
    int PointedValue = 7;

    auto BindThunkResult = GenerateBindingThunk(&DefaultSmallCallback, &Binder, EBindingThunkType::Default);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<int*(*)(int, int&, int*)>(BindThunk);
    auto* Returned = BoundFn(3, ReferenceValue, &PointedValue);

    EXPECT_EQ(Returned, &Binder.output);
    EXPECT_EQ(Binder.calls, 1);
    EXPECT_EQ(Binder.lastValue, 3);
    EXPECT_EQ(Binder.lastReference, 5);
    EXPECT_EQ(Binder.lastPointed, 7);
    EXPECT_EQ(ReferenceValue, 15);
    EXPECT_EQ(PointedValue, 27);
}

TEST(BindingThunkTests, DefaultSimpleShiftUsesXmmRegisters) {
    DefaultSimpleXmmBinder Binder{};

    auto BindThunkResult = GenerateBindingThunk(&DefaultSimpleXmmCallback, &Binder, EBindingThunkType::Default);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<float(*)(double, float)>(BindThunk);
    auto ReturnValue = BoundFn(1.25, 2.5f);

    EXPECT_FLOAT_EQ(ReturnValue, 3.75f);
    EXPECT_EQ(Binder.calls, 1);
    EXPECT_DOUBLE_EQ(Binder.first, 1.25);
    EXPECT_FLOAT_EQ(Binder.second, 2.5f);
}

TEST(BindingThunkTests, GenerateSimpleShiftRejectsSourceWithNoExtraDestinationArgument) {
    auto SourceSignature = FuncSignature::build<int, int>();
    auto DestinationSignature = SourceSignature;
    FuncArgInfo SourceInfo{SourceSignature};
    FuncArgInfo DestinationInfo{DestinationSignature};

    auto Result = GenerateSimpleShift(nullptr, nullptr, SourceInfo, DestinationInfo, false);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::InvalidSignature);
}

TEST(BindingThunkTests, GenerateSimpleShiftRejectsMismatchedDestinationArgumentTypes) {
    auto SourceSignature = FuncSignature::build<int, int>();
    auto DestinationSignature = FuncSignature::build<int, void*, float>();
    FuncArgInfo SourceInfo{SourceSignature};
    FuncArgInfo DestinationInfo{DestinationSignature};

    auto Result = GenerateSimpleShift(nullptr, nullptr, SourceInfo, DestinationInfo, false);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::InvalidSignature);
}

TEST(BindingThunkTests, DefaultComplexShiftHandlesStackArgumentsAndFloatReturn) {
    DefaultComplexBinder Binder{};
    int ReferenceValue = 2;
    int PointedValue = 3;
    void* PointerValue = reinterpret_cast<void*>(5);

    auto BindThunkResult = GenerateBindingThunk(&DefaultComplexCallback, &Binder, EBindingThunkType::Default);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<float(*)(int, int&, int*, intptr_t, void*)>(BindThunk);
    auto ReturnValue = BoundFn(1, ReferenceValue, &PointedValue, 4, PointerValue);

    EXPECT_FLOAT_EQ(ReturnValue, 22.0f);
    EXPECT_EQ(Binder.calls, 1);
    EXPECT_EQ(Binder.a, 1);
    EXPECT_EQ(Binder.b, 2);
    EXPECT_EQ(Binder.c, 3);
    EXPECT_EQ(Binder.d, 4);
    EXPECT_EQ(Binder.e, PointerValue);
    EXPECT_EQ(ReferenceValue, 5);
    EXPECT_EQ(PointedValue, 7);
}

TEST(BindingThunkTests, DefaultComplexShiftHandlesFloatArgumentsAndIntegerReturn) {
    DefaultComplexFloatIntBinder Binder{};

    auto BindThunkResult = GenerateBindingThunk(&DefaultComplexFloatIntCallback, &Binder, EBindingThunkType::Default);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<int(*)(int, float, int, int, int)>(BindThunk);
    auto ReturnValue = BoundFn(1, 2.5f, 3, 4, 5);

    EXPECT_EQ(ReturnValue, 15);
    EXPECT_EQ(Binder.calls, 1);
    EXPECT_EQ(Binder.first, 1);
    EXPECT_FLOAT_EQ(Binder.second, 2.5f);
    EXPECT_EQ(Binder.third, 3);
    EXPECT_EQ(Binder.fourth, 4);
    EXPECT_EQ(Binder.fifth, 5);
}

TEST(BindingThunkTests, DefaultComplexShiftHandlesVoidReturn) {
    DefaultComplexVoidBinder Binder{};

    auto BindThunkResult = GenerateBindingThunk(&DefaultComplexVoidCallback, &Binder, EBindingThunkType::Default);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<void(*)(int, int, int, int, float)>(BindThunk);
    BoundFn(1, 2, 3, 4, 5.5f);

    EXPECT_EQ(Binder.calls, 1);
    EXPECT_EQ(Binder.a, 1);
    EXPECT_EQ(Binder.b, 2);
    EXPECT_EQ(Binder.c, 3);
    EXPECT_EQ(Binder.d, 4);
    EXPECT_FLOAT_EQ(Binder.e, 5.5f);
}

TEST(BindingThunkTests, RegisterBindingRestoresLargeMixedSignature) {
    RegisterLargeState State{};
    GRegisterLargeState = &State;
    int ReferenceValue = 11;
    int PointedValue = 13;
    double FloatingReference = 2.5;
    double FloatingPointed = 3.75;

    auto RestoreThunkResult = GenerateRestoreThunk(&OriginalRegisterLarge, EBindingThunkType::Register);
    ASSERT_TRUE(RestoreThunkResult.has_value()) << RestoreThunkResult.error().Message;
    GRestoreThunk = std::move(RestoreThunkResult.value());
    auto BindThunkResult = GenerateBindingThunk(&RegisterLargeCallback, &State, EBindingThunkType::Register);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<int64_t(*)(int, int&, int*, double, double&, double*)>(BindThunk);
    auto ReturnValue = BoundFn(7, ReferenceValue, &PointedValue, 1.25, FloatingReference, &FloatingPointed);

    EXPECT_EQ(ReturnValue, 47);
    EXPECT_EQ(State.callbackCalls, 1);
    EXPECT_EQ(State.originalCalls, 1);
    EXPECT_TRUE(State.sawRegisterContext);
    EXPECT_EQ(State.value, 7);
    EXPECT_EQ(State.referenced, 11);
    EXPECT_EQ(State.pointed, 13);
    EXPECT_DOUBLE_EQ(State.floatingValue, 1.25);
    EXPECT_DOUBLE_EQ(State.floatingReferenced, 2.5);
    EXPECT_DOUBLE_EQ(State.floatingPointed, 3.75);
    EXPECT_EQ(ReferenceValue, 13);
    EXPECT_EQ(PointedValue, 16);
    EXPECT_DOUBLE_EQ(FloatingReference, 4.0);
    EXPECT_DOUBLE_EQ(FloatingPointed, 6.25);

    GRestoreThunk.reset();
    GRegisterLargeState = nullptr;
}

TEST(BindingThunkTests, RegisterBindingMovesGpRegisterArgumentsToStackAndReturnsFloat) {
    RegisterStackShiftState State{};
    GRegisterStackShiftState = &State;

    auto RestoreThunkResult = GenerateRestoreThunk(&OriginalRegisterStackShift, EBindingThunkType::Register);
    ASSERT_TRUE(RestoreThunkResult.has_value()) << RestoreThunkResult.error().Message;
    GRestoreThunk = std::move(RestoreThunkResult.value());
    auto BindThunkResult = GenerateBindingThunk(&RegisterStackShiftCallback, &State, EBindingThunkType::Register);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<float(*)(int, int, int, int)>(BindThunk);
    auto ReturnValue = BoundFn(1, 2, 3, 4);

    EXPECT_FLOAT_EQ(ReturnValue, 10.0f);
    EXPECT_EQ(State.callbackCalls, 1);
    EXPECT_EQ(State.originalCalls, 1);
    EXPECT_EQ(State.a, 1);
    EXPECT_EQ(State.b, 2);
    EXPECT_EQ(State.c, 3);
    EXPECT_EQ(State.d, 4);

    GRestoreThunk.reset();
    GRegisterStackShiftState = nullptr;
}

TEST(BindingThunkTests, RegisterBindingHandlesMultipleXmmArgumentsAndFloatReturn) {
    RegisterXmmState State{};
    GRegisterXmmState = &State;

    auto RestoreThunkResult = GenerateRestoreThunk(&OriginalRegisterXmm, EBindingThunkType::Register);
    ASSERT_TRUE(RestoreThunkResult.has_value()) << RestoreThunkResult.error().Message;
    GRestoreThunk = std::move(RestoreThunkResult.value());
    auto BindThunkResult = GenerateBindingThunk(&RegisterXmmCallback, &State, EBindingThunkType::Register);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<float(*)(double, float)>(BindThunk);
    auto ReturnValue = BoundFn(2.25, 3.5f);

    EXPECT_FLOAT_EQ(ReturnValue, 5.75f);
    EXPECT_EQ(State.callbackCalls, 1);
    EXPECT_EQ(State.originalCalls, 1);
    EXPECT_DOUBLE_EQ(State.first, 2.25);
    EXPECT_FLOAT_EQ(State.second, 3.5f);

    GRestoreThunk.reset();
    GRegisterXmmState = nullptr;
}

TEST(BindingThunkTests, ArgumentBindingNoArgsExposesContextMetadata) {
    ArgumentNoArgBinder Binder{};

    auto RestoreThunkResult = GenerateRestoreThunk(&OriginalArgumentNoArgs, EBindingThunkType::Argument);
    ASSERT_TRUE(RestoreThunkResult.has_value()) << RestoreThunkResult.error().Message;
    GRestoreThunk = std::move(RestoreThunkResult.value());
    auto BindThunkResult = GenerateBindingThunk<ArgumentNoArgBinder, int64_t>(&ArgumentNoArgCallback, &Binder, EBindingThunkType::Argument);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<int64_t(*)()>(BindThunk);
    EXPECT_EQ(BoundFn(), 123);
    EXPECT_EQ(Binder.calls, 1);
    EXPECT_FALSE(Binder.hasRegisterContext);
    EXPECT_TRUE(Binder.hasReturnValue);
    EXPECT_EQ(Binder.argsCount, 0);

    GRestoreThunk.reset();
}

TEST(BindingThunkTests, ArgumentBindingRestoresSmallMixedSignature) {
    ArgumentSmallBinder Binder{};
    GArgumentSmallBinder = &Binder;
    int ReferenceValue = 5;
    int PointedValue = 7;

    auto RestoreThunkResult = GenerateRestoreThunk(&OriginalArgumentSmall, EBindingThunkType::Argument);
    ASSERT_TRUE(RestoreThunkResult.has_value()) << RestoreThunkResult.error().Message;
    GRestoreThunk = std::move(RestoreThunkResult.value());
    auto BindThunkResult = GenerateBindingThunk<ArgumentSmallBinder, int64_t, int, int&, int*>(
        &ArgumentSmallCallback, &Binder, EBindingThunkType::Argument);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<int64_t(*)(int, int&, int*)>(BindThunk);
    auto ReturnValue = BoundFn(3, ReferenceValue, &PointedValue);

    EXPECT_EQ(ReturnValue, 25);
    EXPECT_EQ(Binder.calls, 1);
    EXPECT_EQ(Binder.originalCalls, 1);
    EXPECT_FALSE(Binder.hasRegisterContext);
    EXPECT_TRUE(Binder.hasReturnValue);
    EXPECT_EQ(Binder.argsCount, 3);
    EXPECT_EQ(Binder.value, 3);
    EXPECT_EQ(Binder.referenced, 5);
    EXPECT_EQ(Binder.pointed, 7);
    EXPECT_EQ(ReferenceValue, 9);
    EXPECT_EQ(PointedValue, 13);

    GRestoreThunk.reset();
    GArgumentSmallBinder = nullptr;
}

TEST(BindingThunkTests, ArgumentBindingRestoresStackFloatArgumentAndFloatReturn) {
    ArgumentFloatStackBinder Binder{};
    GArgumentFloatStackBinder = &Binder;

    auto RestoreThunkResult = GenerateRestoreThunk(&OriginalArgumentFloatStack, EBindingThunkType::Argument);
    ASSERT_TRUE(RestoreThunkResult.has_value()) << RestoreThunkResult.error().Message;
    GRestoreThunk = std::move(RestoreThunkResult.value());
    auto BindThunkResult = GenerateBindingThunk<ArgumentFloatStackBinder, float, int, int, int, int, float>(
        &ArgumentFloatStackCallback, &Binder, EBindingThunkType::Argument);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<float(*)(int, int, int, int, float)>(BindThunk);
    auto ReturnValue = BoundFn(1, 2, 3, 4, 5.5f);

    EXPECT_FLOAT_EQ(ReturnValue, 15.5f);
    EXPECT_EQ(Binder.calls, 1);
    EXPECT_EQ(Binder.originalCalls, 1);
    EXPECT_EQ(Binder.a, 1);
    EXPECT_EQ(Binder.b, 2);
    EXPECT_EQ(Binder.c, 3);
    EXPECT_EQ(Binder.d, 4);
    EXPECT_FLOAT_EQ(Binder.e, 5.5f);

    GRestoreThunk.reset();
    GArgumentFloatStackBinder = nullptr;
}

TEST(BindingThunkTests, ArgumentAndRegisterBindingRestoresLargeMixedSignature) {
    ArgumentRegisterLargeBinder Binder{};
    GArgumentRegisterLargeBinder = &Binder;
    int ReferenceValue = 10;
    int PointedValue = 11;
    double FloatingReference = 2.5;
    double FloatingPointed = 3.5;

    auto RestoreThunkResult = GenerateRestoreThunk(&OriginalArgumentRegisterLarge, EBindingThunkType::ArgumentAndRegister);
    ASSERT_TRUE(RestoreThunkResult.has_value()) << RestoreThunkResult.error().Message;
    GRestoreThunk = std::move(RestoreThunkResult.value());
    auto BindThunkResult = GenerateBindingThunk<ArgumentRegisterLargeBinder, int*, int, int&, int*, double, double&, double*>(
        &ArgumentRegisterLargeCallback, &Binder, EBindingThunkType::ArgumentAndRegister);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;
    auto BindThunk = std::move(BindThunkResult.value());

    auto BoundFn = ThunkCast<int*(*)(int, int&, int*, double, double&, double*)>(BindThunk);
    auto* ReturnValue = BoundFn(9, ReferenceValue, &PointedValue, 1.5, FloatingReference, &FloatingPointed);

    EXPECT_EQ(ReturnValue, &PointedValue);
    EXPECT_EQ(Binder.calls, 1);
    EXPECT_EQ(Binder.originalCalls, 1);
    EXPECT_TRUE(Binder.hasRegisterContext);
    EXPECT_TRUE(Binder.hasReturnValue);
    EXPECT_EQ(Binder.argsCount, 6);
    EXPECT_EQ(Binder.value, 9);
    EXPECT_EQ(Binder.referenced, 10);
    EXPECT_EQ(Binder.pointed, 11);
    EXPECT_DOUBLE_EQ(Binder.floatingValue, 1.5);
    EXPECT_DOUBLE_EQ(Binder.floatingReferenced, 2.5);
    EXPECT_DOUBLE_EQ(Binder.floatingPointed, 3.5);
    EXPECT_EQ(ReferenceValue, 11);
    EXPECT_EQ(PointedValue, 13);
    EXPECT_DOUBLE_EQ(FloatingReference, 6.0);
    EXPECT_DOUBLE_EQ(FloatingPointed, 8.0);

    GRestoreThunk.reset();
    GArgumentRegisterLargeBinder = nullptr;
}

TEST(BindingThunkTests, PlainCallbacksRejectArgumentModes) {
    DefaultNoArgBinder Binder{};

    auto ArgumentResult = GenerateBindingThunk(&DefaultNoArgCallback, &Binder, EBindingThunkType::Argument);
    EXPECT_FALSE(ArgumentResult.has_value());
    EXPECT_EQ(ArgumentResult.error().Code, EThunkErrorCode::InvalidBindingType);

    auto ArgumentAndRegisterResult = GenerateBindingThunk(&DefaultNoArgCallback, &Binder, EBindingThunkType::ArgumentAndRegister);
    EXPECT_FALSE(ArgumentAndRegisterResult.has_value());
    EXPECT_EQ(ArgumentAndRegisterResult.error().Code, EThunkErrorCode::InvalidBindingType);
}

TEST(BindingThunkTests, PlainCallbacksRejectInvalidBindingMode) {
    DefaultNoArgBinder Binder{};

    auto Result = GenerateBindingThunk(&DefaultNoArgCallback, &Binder, static_cast<EBindingThunkType>(99));
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::InvalidBindingType);
}

TEST(BindingThunkTests, ArgumentCallbacksRejectNonArgumentModes) {
    ArgumentNoArgBinder Binder{};

    auto DefaultResult = GenerateBindingThunk<ArgumentNoArgBinder, int64_t>(&ArgumentNoArgCallback, &Binder, EBindingThunkType::Default);
    EXPECT_FALSE(DefaultResult.has_value());
    EXPECT_EQ(DefaultResult.error().Code, EThunkErrorCode::InvalidBindingType);

    auto RegisterResult = GenerateBindingThunk<ArgumentNoArgBinder, int64_t>(&ArgumentNoArgCallback, &Binder, EBindingThunkType::Register);
    EXPECT_FALSE(RegisterResult.has_value());
    EXPECT_EQ(RegisterResult.error().Code, EThunkErrorCode::InvalidBindingType);
}

TEST(BindingThunkTests, ArgumentCallbacksRejectInvalidBindingMode) {
    ArgumentNoArgBinder Binder{};

    auto Result = GenerateBindingThunk<ArgumentNoArgBinder, int64_t>(&ArgumentNoArgCallback, &Binder, static_cast<EBindingThunkType>(99));
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::InvalidBindingType);
}

TEST(BindingThunkTests, DefaultModeCannotGenerateRestoreThunk) {
    auto Result = GenerateRestoreThunk(&OriginalArgumentNoArgs, EBindingThunkType::Default);
    EXPECT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::InvalidBindingType);
}

#if defined(_WIN64)
TEST(BindingThunkTests, RegisterRestoreThunkRegistersWindowsUnwindInfo) {
    auto RestoreThunkResult = GenerateRestoreThunk(&OriginalRegisterStackShift, EBindingThunkType::Register);
    ASSERT_TRUE(RestoreThunkResult.has_value()) << RestoreThunkResult.error().Message;

    auto RestoreThunk = std::move(RestoreThunkResult.value());
    const auto Address = RestoreThunk.get();
    ASSERT_NE(LookupThunkRuntimeFunction(Address), nullptr);

    RestoreThunk.reset();
    EXPECT_EQ(LookupThunkRuntimeFunction(Address), nullptr);
}

TEST(BindingThunkTests, ArgumentBindingThunkRegistersWindowsUnwindInfo) {
    ArgumentNoArgBinder Binder{};
    auto BindThunkResult = GenerateBindingThunk<ArgumentNoArgBinder, int64_t>(&ArgumentNoArgCallback, &Binder, EBindingThunkType::Argument);
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;

    auto BindThunk = std::move(BindThunkResult.value());
    const auto Address = BindThunk.get();
    ASSERT_NE(LookupThunkRuntimeFunction(Address), nullptr);

    BindThunk.reset();
    EXPECT_EQ(LookupThunkRuntimeFunction(Address), nullptr);
}

TEST(BindingThunkTests, ComplexBindingThunkRegistersWindowsUnwindInfo) {
    DefaultComplexBinder Binder{};
    auto BindThunkResult = GenerateBindingThunk<DefaultComplexBinder, float, int, int&, int*, intptr_t, void*>(
        &DefaultComplexCallback,
        &Binder,
        EBindingThunkType::Default
    );
    ASSERT_TRUE(BindThunkResult.has_value()) << BindThunkResult.error().Message;

    auto BindThunk = std::move(BindThunkResult.value());
    const auto Address = BindThunk.get();
    ASSERT_NE(LookupThunkRuntimeFunction(Address), nullptr);

    BindThunk.reset();
    EXPECT_EQ(LookupThunkRuntimeFunction(Address), nullptr);
}
#endif

TEST(BindingThunkTests, RestoreThunkRejectsInvalidBindingMode) {
    auto Result = GenerateRestoreThunk(&OriginalArgumentNoArgs, static_cast<EBindingThunkType>(99));
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::InvalidBindingType);
}

TEST(BindingThunkTests, ArgumentRestoreThunkRejectsUnsupportedRegisterArgumentTypes) {
    auto Signature = FuncSignature::build<void>();
    Signature.add_arg(asmjit::TypeId::kMmx64);

    auto Result = GenerateRestoreThunk(reinterpret_cast<void*>(&OriginalArgumentNoArgs), Signature, EBindingThunkType::Argument);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::UnsupportedType);
}

TEST(BindingThunkTests, ArgumentRestoreThunkRejectsUnsupportedStackArgumentTypes) {
    auto Signature = FuncSignature::build<void>();
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kMmx64);

    auto Result = GenerateRestoreThunk(reinterpret_cast<void*>(&OriginalArgumentNoArgs), Signature, EBindingThunkType::Argument);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::UnsupportedType);
}

TEST(BindingThunkTests, DefaultComplexShiftRejectsUnsupportedArgumentTypes) {
    auto Signature = FuncSignature::build<int>();
    Signature.add_arg(asmjit::TypeId::kMask32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);

    auto Result = GenerateBindingThunk(reinterpret_cast<void*>(&DefaultComplexFloatIntCallback), nullptr, Signature, EBindingThunkType::Default);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::UnsupportedType);
}

TEST(BindingThunkTests, DefaultComplexShiftRejectsUnsupportedReturnTypes) {
    auto Signature = FuncSignature::build<void>();
    Signature.set_ret(asmjit::TypeId::kMask32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);

    auto Result = GenerateBindingThunk(reinterpret_cast<void*>(&DefaultComplexFloatIntCallback), nullptr, Signature, EBindingThunkType::Default);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::UnsupportedType);
}

TEST(BindingThunkTests, ArgumentBindingRejectsUnsupportedRegisterArgumentTypes) {
    auto Signature = FuncSignature::build<int>();
    Signature.add_arg(asmjit::TypeId::kMmx64);

    auto Result = GenerateBindingThunk(&UnsupportedArgumentCallback, nullptr, Signature, EBindingThunkType::Argument);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::UnsupportedType);
}

TEST(BindingThunkTests, ArgumentBindingRejectsUnsupportedStackArgumentTypes) {
    auto Signature = FuncSignature::build<int>();
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kInt32);
    Signature.add_arg(asmjit::TypeId::kMmx64);

    auto Result = GenerateBindingThunk(&UnsupportedArgumentCallback, nullptr, Signature, EBindingThunkType::Argument);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::UnsupportedType);
}

TEST(BindingThunkTests, ArgumentBindingRejectsUnsupportedReturnTypes) {
    auto Signature = FuncSignature::build<void>();
    Signature.set_ret(asmjit::TypeId::kMask32);

    auto Result = GenerateBindingThunk(&UnsupportedArgumentCallback, nullptr, Signature, EBindingThunkType::Argument);
    ASSERT_FALSE(Result.has_value());
    EXPECT_EQ(Result.error().Code, EThunkErrorCode::UnsupportedType);
}
