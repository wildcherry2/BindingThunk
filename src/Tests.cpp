#include "Tests.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

static FThunkPtr GRestoreThunk{};

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
    Binder->value = static_cast<int>(Context.GetArgumentAs<uint64_t>(0));
    Binder->referenced = *Context.GetArgumentAs<int*>(1);
    Binder->pointed = *Context.GetArgumentAs<int*>(2);
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
    Binder->value = static_cast<int>(Context.GetArgumentAs<uint64_t>(0));
    Binder->referenced = *Context.GetArgumentAs<int*>(1);
    Binder->pointed = *Context.GetArgumentAs<int*>(2);
    Binder->floatingValue = Context.GetArgumentAs<double>(3);
    Binder->floatingReferenced = *Context.GetArgumentAs<double*>(4);
    Binder->floatingPointed = *Context.GetArgumentAs<double*>(5);
    Context.SetReturnValue(ThunkCast<int*(*)(ArgumentContext&)>(GRestoreThunk)(Context));
}

TEST(BindingThunkTests, DefaultNoArgsScalarReturn) {
    DefaultNoArgBinder Binder{};

    auto BindThunk = GenerateBindingThunk(&DefaultNoArgCallback, &Binder, EBindingThunkType::Default);
    ASSERT_NE(BindThunk, nullptr);

    auto BoundFn = ThunkCast<int(*)()>(BindThunk);
    EXPECT_EQ(BoundFn(), 41);
    EXPECT_EQ(Binder.calls, 1);
}

TEST(BindingThunkTests, DefaultSmallMixedArgumentsAndPointerReturn) {
    DefaultSmallBinder Binder{};
    int ReferenceValue = 5;
    int PointedValue = 7;

    auto BindThunk = GenerateBindingThunk(&DefaultSmallCallback, &Binder, EBindingThunkType::Default);
    ASSERT_NE(BindThunk, nullptr);

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

TEST(BindingThunkTests, RegisterBindingRestoresLargeMixedSignature) {
    RegisterLargeState State{};
    GRegisterLargeState = &State;
    int ReferenceValue = 11;
    int PointedValue = 13;
    double FloatingReference = 2.5;
    double FloatingPointed = 3.75;

    GRestoreThunk = GenerateRestoreThunk(&OriginalRegisterLarge, EBindingThunkType::Register);
    auto BindThunk = GenerateBindingThunk(&RegisterLargeCallback, &State, EBindingThunkType::Register);
    ASSERT_NE(GRestoreThunk, nullptr);
    ASSERT_NE(BindThunk, nullptr);

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

TEST(BindingThunkTests, ArgumentBindingNoArgsExposesContextMetadata) {
    ArgumentNoArgBinder Binder{};

    GRestoreThunk = GenerateRestoreThunk(&OriginalArgumentNoArgs, EBindingThunkType::Argument);
    auto BindThunk = GenerateBindingThunk<ArgumentNoArgBinder, int64_t>(&ArgumentNoArgCallback, &Binder, EBindingThunkType::Argument);
    ASSERT_NE(GRestoreThunk, nullptr);
    ASSERT_NE(BindThunk, nullptr);

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

    GRestoreThunk = GenerateRestoreThunk(&OriginalArgumentSmall, EBindingThunkType::Argument);
    auto BindThunk = GenerateBindingThunk<ArgumentSmallBinder, int64_t, int, int&, int*>(
        &ArgumentSmallCallback, &Binder, EBindingThunkType::Argument);
    ASSERT_NE(GRestoreThunk, nullptr);
    ASSERT_NE(BindThunk, nullptr);

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

TEST(BindingThunkTests, ArgumentAndRegisterBindingRestoresLargeMixedSignature) {
    ArgumentRegisterLargeBinder Binder{};
    GArgumentRegisterLargeBinder = &Binder;
    int ReferenceValue = 10;
    int PointedValue = 11;
    double FloatingReference = 2.5;
    double FloatingPointed = 3.5;

    GRestoreThunk = GenerateRestoreThunk(&OriginalArgumentRegisterLarge, EBindingThunkType::ArgumentAndRegister);
    auto BindThunk = GenerateBindingThunk<ArgumentRegisterLargeBinder, int*, int, int&, int*, double, double&, double*>(
        &ArgumentRegisterLargeCallback, &Binder, EBindingThunkType::ArgumentAndRegister);
    ASSERT_NE(GRestoreThunk, nullptr);
    ASSERT_NE(BindThunk, nullptr);

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

    EXPECT_THROW((GenerateBindingThunk(&DefaultNoArgCallback, &Binder, EBindingThunkType::Argument)), std::invalid_argument);
    EXPECT_THROW((GenerateBindingThunk(&DefaultNoArgCallback, &Binder, EBindingThunkType::ArgumentAndRegister)), std::invalid_argument);
}

TEST(BindingThunkTests, ArgumentCallbacksRejectNonArgumentModes) {
    ArgumentNoArgBinder Binder{};

    EXPECT_THROW((GenerateBindingThunk<ArgumentNoArgBinder, int64_t>(&ArgumentNoArgCallback, &Binder, EBindingThunkType::Default)), std::invalid_argument);
    EXPECT_THROW((GenerateBindingThunk<ArgumentNoArgBinder, int64_t>(&ArgumentNoArgCallback, &Binder, EBindingThunkType::Register)), std::invalid_argument);
}

TEST(BindingThunkTests, DefaultModeCannotGenerateRestoreThunk) {
    EXPECT_THROW((GenerateRestoreThunk(&OriginalArgumentNoArgs, EBindingThunkType::Default)), std::invalid_argument);
}
