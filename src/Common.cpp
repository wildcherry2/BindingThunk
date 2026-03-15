#include "Common.hpp"

#include <ranges>
#include <string_view>
#include <iostream>
#include <format>
#include <algorithm>
#include <cstdlib>
#include <mutex>

#if defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace RC::Thunk {

static FThunkError MakeThunkError(const EThunkErrorCode Code, std::string Message) {
    return FThunkError { Code, std::move(Message) };
}

static uint32_t AlignThunkStackAllocation(const uint32_t RawStackAllocation, const uint32_t PushBytes) {
    const auto Misalignment = (PushBytes + RawStackAllocation) % 16;
    return RawStackAllocation + ((8 + 16 - Misalignment) % 16);
}

#if defined(_WIN64)
enum : uint8_t {
    UWOP_PUSH_NONVOL = 0,
    UWOP_ALLOC_LARGE = 1,
    UWOP_ALLOC_SMALL = 2,
    UWOP_SET_FPREG = 3,
    UWOP_SAVE_NONVOL = 4,
    UWOP_SAVE_XMM128 = 8,
    UWOP_SAVE_XMM128_FAR = 9,
};

struct FWindowsUnwindInfoHeader {
    uint8_t VersionAndFlags{};
    uint8_t SizeOfProlog{};
    uint8_t CountOfCodes{};
    uint8_t FrameRegisterAndOffset{};
};

struct FWindowsUnwindCodeSlot {
    uint8_t CodeOffset{};
    uint8_t UnwindOpAndOpInfo{};
};

struct FWindowsUnwindOperation {
    uint8_t CodeOffset{};
    uint8_t UnwindOp{};
    uint8_t OpInfo{};
    uint16_t ExtraSlots[2]{};
    uint8_t ExtraSlotCount{};
};

static_assert(sizeof(FWindowsUnwindInfoHeader) == 4);
static_assert(sizeof(FWindowsUnwindCodeSlot) == 2);

struct FRegisteredThunkWindowsUnwind {
    void* Thunk{};
    RUNTIME_FUNCTION FunctionEntry{};
    FRegisteredThunkWindowsUnwind* Next{};
};

static std::mutex GRegisteredThunkWindowsUnwindMutex{};
static FRegisteredThunkWindowsUnwind* GRegisteredThunkWindowsUnwindHead{};

static constexpr uint8_t PackUnwindVersionAndFlags(const uint8_t Version, const uint8_t Flags) {
    return static_cast<uint8_t>((Version & 0x07u) | ((Flags & 0x1Fu) << 3));
}

static constexpr uint8_t PackFrameRegisterAndOffset(const uint8_t FrameRegister, const uint8_t FrameOffsetScaled) {
    return static_cast<uint8_t>((FrameRegister & 0x0Fu) | ((FrameOffsetScaled & 0x0Fu) << 4));
}

static constexpr uint8_t PackUnwindOpAndInfo(const uint8_t UnwindOp, const uint8_t OpInfo) {
    return static_cast<uint8_t>((UnwindOp & 0x0Fu) | ((OpInfo & 0x0Fu) << 4));
}

static FWindowsUnwindOperation MakeUnwindOperation(const uint8_t CodeOffset, const uint8_t UnwindOp, const uint8_t OpInfo) {
    return FWindowsUnwindOperation {
        .CodeOffset = CodeOffset,
        .UnwindOp = UnwindOp,
        .OpInfo = OpInfo,
    };
}

static FWindowsUnwindOperation MakeUnwindOperationWithSlot(
    const uint8_t CodeOffset,
    const uint8_t UnwindOp,
    const uint8_t OpInfo,
    const uint16_t ExtraSlot
) {
    auto Operation = MakeUnwindOperation(CodeOffset, UnwindOp, OpInfo);
    Operation.ExtraSlots[0] = ExtraSlot;
    Operation.ExtraSlotCount = 1;
    return Operation;
}

static FWindowsUnwindOperation MakeUnwindOperationWithSplitOperand(
    const uint8_t CodeOffset,
    const uint8_t UnwindOp,
    const uint8_t OpInfo,
    const uint32_t Operand
) {
    auto Operation = MakeUnwindOperation(CodeOffset, UnwindOp, OpInfo);
    Operation.ExtraSlots[0] = static_cast<uint16_t>(Operand & 0xffffu);
    Operation.ExtraSlots[1] = static_cast<uint16_t>(Operand >> 16);
    Operation.ExtraSlotCount = 2;
    return Operation;
}

template<typename T>
static void AppendStructBytes(std::vector<std::byte>& Buffer, const T& Value) {
    const auto* Bytes = reinterpret_cast<const std::byte*>(&Value);
    Buffer.insert(Buffer.end(), Bytes, Bytes + sizeof(T));
}

static std::vector<std::byte> BuildWindowsUnwindInfo(
    const std::vector<FWindowsUnwindOperation>& Operations,
    const uint8_t FrameRegister = 0,
    const uint8_t FrameOffsetScaled = 0,
    const uint8_t Flags = 0
) {
    if (Operations.empty()) return {};

    uint8_t PrologSize = 0;
    uint8_t CodeSlots = 0;
    for (const auto& Operation : Operations) {
        PrologSize = std::max(PrologSize, Operation.CodeOffset);
        CodeSlots += static_cast<uint8_t>(1 + Operation.ExtraSlotCount);
    }

    std::vector<std::byte> Buffer{};
    Buffer.reserve(4 + (CodeSlots + (CodeSlots & 1)) * 2);
    AppendStructBytes(Buffer, FWindowsUnwindInfoHeader {
        .VersionAndFlags = PackUnwindVersionAndFlags(1, Flags),
        .SizeOfProlog = PrologSize,
        .CountOfCodes = CodeSlots,
        .FrameRegisterAndOffset = PackFrameRegisterAndOffset(FrameRegister, FrameOffsetScaled),
    });

    for (auto It = Operations.rbegin(); It != Operations.rend(); ++It) {
        AppendStructBytes(Buffer, FWindowsUnwindCodeSlot {
            .CodeOffset = It->CodeOffset,
            .UnwindOpAndOpInfo = PackUnwindOpAndInfo(It->UnwindOp, It->OpInfo),
        });

        for (uint8_t SlotIndex = 0; SlotIndex < It->ExtraSlotCount; ++SlotIndex) {
            AppendStructBytes(Buffer, It->ExtraSlots[SlotIndex]);
        }
    }

    if (CodeSlots & 1) {
        Buffer.push_back(std::byte {});
        Buffer.push_back(std::byte {});
    }

    return Buffer;
}

static std::optional<FThunkError> RegisterThunkWindowsUnwind(
    void* Thunk,
    CodeHolder& Code,
    const FThunkWindowsRuntimeInfo& WindowsRuntimeInfo
) {
    const auto BeginOffset = static_cast<uint32_t>(Code.label_offset(WindowsRuntimeInfo.BeginLabel));
    const auto EndOffset = static_cast<uint32_t>(Code.label_offset(WindowsRuntimeInfo.EndLabel));
    const auto UnwindOffset = static_cast<uint32_t>(Code.label_offset(WindowsRuntimeInfo.UnwindInfoLabel));

    if (BeginOffset != 0) {
        return MakeThunkError(
            EThunkErrorCode::WindowsUnwindRegistrationFailed,
            "Windows unwind registration requires the function begin label to be at offset 0."
        );
    }

    if (EndOffset <= BeginOffset || UnwindOffset <= EndOffset) {
        return MakeThunkError(
            EThunkErrorCode::WindowsUnwindRegistrationFailed,
            "Windows unwind registration encountered malformed function or unwind offsets."
        );
    }

    auto* Registration = static_cast<FRegisteredThunkWindowsUnwind*>(std::malloc(sizeof(FRegisteredThunkWindowsUnwind)));
    if (!Registration) {
        return MakeThunkError(
            EThunkErrorCode::WindowsUnwindRegistrationFailed,
            "Failed to allocate Windows unwind metadata for the generated thunk."
        );
    }

    Registration->Thunk = Thunk;
    Registration->FunctionEntry.BeginAddress = BeginOffset;
    Registration->FunctionEntry.EndAddress = EndOffset;
    Registration->FunctionEntry.UnwindData = UnwindOffset;
    Registration->Next = nullptr;

    if (!RtlAddFunctionTable(&Registration->FunctionEntry, 1, reinterpret_cast<DWORD64>(Thunk))) {
        std::free(Registration);
        return MakeThunkError(
            EThunkErrorCode::WindowsUnwindRegistrationFailed,
            "RtlAddFunctionTable failed for the generated thunk."
        );
    }

    std::scoped_lock Lock { GRegisteredThunkWindowsUnwindMutex };
    Registration->Next = GRegisteredThunkWindowsUnwindHead;
    GRegisteredThunkWindowsUnwindHead = Registration;

    return std::nullopt;
}

static uint8_t GpPushInstructionSize(const uint32_t RegId) {
    return RegId < 8 ? 1 : 2;
}

static uint8_t GpMovInstructionSize(const uint32_t DstRegId, const uint32_t SrcRegId) {
    return (DstRegId >= 8 || SrcRegId >= 8) ? 4 : 3;
}

static uint8_t GpLeaFromRspInstructionSize(const uint32_t DstRegId, const uint32_t Offset) {
    const uint8_t RexBytes = DstRegId >= 8 ? 1 : 0;
    constexpr uint8_t OpcodeBytes = 1;
    constexpr uint8_t ModRmAndSibBytes = 2;

    if (Offset == 0) return static_cast<uint8_t>(RexBytes + OpcodeBytes + ModRmAndSibBytes);
    if (Offset <= 0x7f) return static_cast<uint8_t>(RexBytes + OpcodeBytes + ModRmAndSibBytes + 1);
    return static_cast<uint8_t>(RexBytes + OpcodeBytes + ModRmAndSibBytes + 4);
}

static uint8_t SubRspInstructionSize(const uint32_t Allocation) {
    return Allocation <= 0x7f ? 4 : 7;
}

static uint8_t VecStoreInstructionSize(const uint32_t VecRegId, const uint32_t Offset) {
    const uint8_t RexBytes = VecRegId >= 8 ? 1 : 0;
    const uint8_t OpcodeBytes = 2;

    if (Offset == 0) return static_cast<uint8_t>(RexBytes + OpcodeBytes + 2);
    if (Offset <= 0x7f) return static_cast<uint8_t>(RexBytes + OpcodeBytes + 3);
    return static_cast<uint8_t>(RexBytes + OpcodeBytes + 6);
}
#endif

auto GetJitRuntime()-> JitRuntime&
{
    static JitRuntime JitRuntime{};
    return JitRuntime;
}

void InitializeCodeHolder(CodeHolder& Code, const bool bLogAssembly) {
    Code.set_logger(bLogAssembly ? GetLogger() : nullptr);
    Code.set_error_handler(GetErrorHandler());
    Code.init(GetJitRuntime().environment(), GetJitRuntime().cpu_features());
}

class UE4SSJitErrorHandler : public ErrorHandler
{
    public: ~UE4SSJitErrorHandler() noexcept override = default;

    void handle_error(asmjit::Error err, const char *message, asmjit::BaseEmitter *origin) override {
        std::cerr << std::format("AsmJit error {}: {}", static_cast<uint32_t>(err), message) << std::endl;
    }
};

class UE4SSJitLogger : public asmjit::Logger
{
    public: ~UE4SSJitLogger() noexcept override = default;

    asmjit::Error _log(const char* data, size_t size) noexcept override
    {
        std::string_view AsStringView{ data, size };
        std::cout << AsStringView;
        std::cout.flush();
        return asmjit::kErrorOk;
    }
};

auto GetErrorHandler()-> ErrorHandler* {
    static UE4SSJitErrorHandler JitErrorHandler{};
    return &JitErrorHandler;
}

auto GetLogger()-> Logger* {
    static UE4SSJitLogger JitLogger{};
    return &JitLogger;
}

void FThunkDeleter::operator()(void* Thunk) const noexcept
{
#if defined(_WIN64)
    {
        std::scoped_lock Lock { GRegisteredThunkWindowsUnwindMutex };
        FRegisteredThunkWindowsUnwind** Current = &GRegisteredThunkWindowsUnwindHead;
        while (*Current) {
            if ((*Current)->Thunk == Thunk) {
                auto* Registration = *Current;
                *Current = Registration->Next;
                RtlDeleteFunctionTable(&Registration->FunctionEntry);
                std::free(Registration);
                break;
            }

            Current = &(*Current)->Next;
        }
    }
#endif
    return (void)GetJitRuntime().release(Thunk);
}

FuncArgInfo::FuncArgInfo(const FuncSignature& Signature)
{
    _Detail.init(Signature, GetJitRuntime().environment());
    _GpRegMask = _Detail.used_regs(asmjit::RegGroup::kGp);
    _VecRegMask = _Detail.used_regs(asmjit::RegGroup::kVec);
    _Signature = Signature;
}

const std::vector<asmjit::FuncValue>& FuncArgInfo::GetArguments() noexcept {
    if (_ArgVals) return *_ArgVals;
    _ArgVals = std::vector<asmjit::FuncValue>{};
    for (uint32_t ArgPackIndex = 0; ArgPackIndex < asmjit::Globals::kMaxFuncArgs; ++ArgPackIndex) {
        const auto& Pack = Detail().arg_packs()[ArgPackIndex];
        if (Pack.count() == 0) break;
        for (uint32_t PackIndex = 0; PackIndex < Pack.count(); ++PackIndex) {
            const auto& SrcArg = Pack[PackIndex];
            if (SrcArg.is_assigned()) {
                _ArgVals->push_back(SrcArg);
            }
        }
    }
    return *_ArgVals;
}

const std::vector<asmjit::FuncValue>& FuncArgInfo::GetReturnValues() noexcept {
    if (_RetVals) return *_RetVals;
    _RetVals = std::vector<asmjit::FuncValue>{};
    for (uint32_t PackIndex = 0; PackIndex < Detail().ret_pack().count(); ++PackIndex) {
        const auto& DestArg = Detail().ret_pack()[PackIndex];
        if (DestArg.is_assigned()) {
            _RetVals->push_back(DestArg);
        }
    }
    return *_RetVals;
}

asmjit::RegMask FuncArgInfo::GpRegMask() const noexcept { return _GpRegMask; }
asmjit::RegMask FuncArgInfo::VecRegMask() const noexcept { return _VecRegMask; }

const std::vector<Gp> & FuncArgInfo::GetArgumentIntegralRegisters() noexcept {
    if (_IntArgRegs) return *_IntArgRegs;
    _IntArgRegs = std::vector<Gp>{};
    for (int I = 0; I < 32; ++I) {
        if ((1 << I) & _GpRegMask) _IntArgRegs->push_back(asmjit::x86::gpq(I));
    }
    return *_IntArgRegs;
}

const std::vector<Vec> & FuncArgInfo::GetArgumentFloatingRegisters() noexcept {
    if (_VecArgRegs) return *_VecArgRegs;
    _VecArgRegs = std::vector<Vec>{};
    for (int I = 0; I < 32; ++I) {
        if ((1 << I) & _VecRegMask) _VecArgRegs->push_back(asmjit::x86::xmm(I));
    }
    return *_VecArgRegs;
}

const asmjit::FuncSignature& FuncArgInfo::Signature() const noexcept { return _Signature; }
const asmjit::FuncDetail& FuncArgInfo::Detail() const noexcept { return _Detail; }

inline const asmjit::CallConv& GetCallingConvention() {
    static asmjit::CallConv CallConv = [] {
        asmjit::CallConv Convention{};
        Convention.init(asmjit::CallConvId::kCDecl, GetJitRuntime().environment());
        return Convention;
    }();

    return CallConv;
}

const std::vector<Gp>& GetPlatformNonVolatileGpRegs() {
    static std::vector<Gp> Regs = [] {
        const auto& Conv = GetCallingConvention();
        std::vector<Gp> Regs {};
        auto Preserved = Conv.preserved_regs(asmjit::RegGroup::kGp);
        for (int I = 0; I < 32; ++I) {
            if (((1 << I) & Preserved) && I != Gp::kIdSp) Regs.push_back(asmjit::x86::gpq(I));
        }
        return Regs;
    }();

    return Regs;
}

const std::vector<Vec>& GetPlatformNonVolatileVecRegs() {
    static std::vector<Vec> Regs = [] {
        const auto& Conv = GetCallingConvention();
        std::vector<Vec> Regs {};
        auto Preserved = Conv.preserved_regs(asmjit::RegGroup::kVec);
        for (int I = 0; I < 32; ++I) {
            if ((1 << I) & Preserved) Regs.push_back(asmjit::x86::xmm(I));
        }
        return Regs;
    }();

    return Regs;
}

size_t GetPlatformStackSpaceForNonVolatileRegs() {
    return (GetPlatformNonVolatileGpRegs().size() * 8) + (GetPlatformNonVolatileVecRegs().size() * 16);
}

Gp GetPlatformGpScratchReg() {
    static Gp Reg = [] {
        const auto& Conv = GetCallingConvention();
        auto RegsMask = ~(Conv.preserved_regs(asmjit::RegGroup::kGp) | Conv.passed_regs(asmjit::RegGroup::kGp));
        for (int I = 0; I < 32; ++I) {
            if (((1 << I) & RegsMask) && I != Gp::kIdAx) return asmjit::x86::gpq(I);
        }
        return Gp {};
    }();

    return Reg;
}

Vec GetPlatformXmmScratchReg() {
    static Vec Reg = [] {
        const auto& Conv = GetCallingConvention();
        auto RegsMask = ~(Conv.preserved_regs(asmjit::RegGroup::kVec) | Conv.passed_regs(asmjit::RegGroup::kVec));
        for (int I = 0; I < 32; ++I) {
            if ((1 << I) & RegsMask) return asmjit::x86::xmm(I);
        }
        return Vec {};
    }();

    return Reg;
}

FManualThunkFrameState EmitManualThunkProlog(Assembler& TheAssembler, FManualThunkFramePlan Plan) {
    using namespace asmjit::x86;

    FManualThunkFrameState State{};
    State.PushBytes = static_cast<uint32_t>(Plan.PushedGpRegs.size() * sizeof(uint64_t));
    State.StackAllocation = AlignThunkStackAllocation(Plan.RawStackAllocation, State.PushBytes);
    State.Plan = std::move(Plan);

#if defined(_WIN64)
    std::vector<FWindowsUnwindOperation> UnwindCodes{};
    const auto PrologStart = TheAssembler.offset();
    const auto PrologOffset = [&]() -> uint8_t {
        return static_cast<uint8_t>(TheAssembler.offset() - PrologStart);
    };
#endif

    for (const auto& Register : State.Plan.PushedGpRegs) {
        TheAssembler.push(Register);
#if defined(_WIN64)
        UnwindCodes.push_back(MakeUnwindOperation(
            PrologOffset(),
            UWOP_PUSH_NONVOL,
            static_cast<uint8_t>(Register.id())
        ));
#endif
    }

    if (State.StackAllocation != 0) {
        TheAssembler.sub(rsp, State.StackAllocation);
#if defined(_WIN64)
        if (State.StackAllocation <= 128) {
            UnwindCodes.push_back(MakeUnwindOperation(
                PrologOffset(),
                UWOP_ALLOC_SMALL,
                static_cast<uint8_t>((State.StackAllocation - 8) / 8)
            ));
        }
        else if (State.StackAllocation <= 0xffff * 8u) {
            UnwindCodes.push_back(MakeUnwindOperationWithSlot(
                PrologOffset(),
                UWOP_ALLOC_LARGE,
                0,
                static_cast<uint16_t>(State.StackAllocation / 8)
            ));
        }
        else {
            UnwindCodes.push_back(MakeUnwindOperationWithSplitOperand(
                PrologOffset(),
                UWOP_ALLOC_LARGE,
                1,
                State.StackAllocation
            ));
        }
#endif
    }

    for (size_t Index = 0; Index < State.Plan.SavedVecRegs.size(); ++Index) {
        const auto Offset = State.Plan.SavedVecOffset + static_cast<uint32_t>(Index * sizeof(Xmm));
        TheAssembler.movdqu(ptr(rsp, static_cast<int32_t>(Offset)), State.Plan.SavedVecRegs[Index]);
#if defined(_WIN64)
        if ((Offset % 16) != 0) {
            UnwindCodes.clear();
            break;
        }

        const auto ShortUnwindOffset = Offset / 16;
        const auto RequiresFarEncoding = ShortUnwindOffset > 0xffff;
        if (RequiresFarEncoding) {
            UnwindCodes.push_back(MakeUnwindOperationWithSplitOperand(
                PrologOffset(),
                UWOP_SAVE_XMM128_FAR,
                static_cast<uint8_t>(State.Plan.SavedVecRegs[Index].id()),
                Offset
            ));
        }
        else {
            UnwindCodes.push_back(MakeUnwindOperationWithSlot(
                PrologOffset(),
                UWOP_SAVE_XMM128,
                static_cast<uint8_t>(State.Plan.SavedVecRegs[Index].id()),
                static_cast<uint16_t>(ShortUnwindOffset)
            ));
        }
#endif
    }

#if defined(_WIN64)
    State.WindowsUnwindInfo = BuildWindowsUnwindInfo(UnwindCodes);
#endif
    return State;
}

void EmitManualThunkEpilog(Assembler& TheAssembler, const FManualThunkFrameState& FrameState) {
    using namespace asmjit::x86;

    for (size_t Index = FrameState.Plan.SavedVecRegs.size(); Index > 0; --Index) {
        const auto Offset = FrameState.Plan.SavedVecOffset + static_cast<uint32_t>((Index - 1) * sizeof(Xmm));
        TheAssembler.movdqu(FrameState.Plan.SavedVecRegs[Index - 1], ptr(rsp, static_cast<int32_t>(Offset)));
    }

    if (FrameState.StackAllocation != 0) {
        TheAssembler.add(rsp, FrameState.StackAllocation);
    }

    for (size_t Index = FrameState.Plan.PushedGpRegs.size(); Index > 0; --Index) {
        TheAssembler.pop(FrameState.Plan.PushedGpRegs[Index - 1]);
    }
}

void EmitManualThunkWindowsUnwindInfo(Assembler& TheAssembler, const FManualThunkFrameState& FrameState, const asmjit::Label UnwindInfoLabel) {
#if defined(_WIN64)
    if (!FrameState.HasWindowsUnwindInfo()) return;
    TheAssembler.align(asmjit::AlignMode::kData, 4);
    TheAssembler.bind(UnwindInfoLabel);
    TheAssembler.embed(FrameState.WindowsUnwindInfo.data(), FrameState.WindowsUnwindInfo.size());
#else
    (void)TheAssembler;
    (void)FrameState;
    (void)UnwindInfoLabel;
#endif
}

#if defined(_WIN64)
std::vector<std::byte> BuildWindowsUnwindInfoForFuncFrame(const asmjit::FuncFrame& Frame) {
    using namespace asmjit;

    if (Frame.has_indirect_branch_protection()) return {};
    if (Frame.has_dynamic_alignment()) return {};
    if (Frame.is_avx_enabled() || Frame.is_avx512_enabled()) return {};
    if (Frame.saved_regs(RegGroup::kMask) != 0) return {};
    if (Frame.saved_regs(RegGroup::kX86_MM) != 0) return {};
    if (Frame.has_sa_reg_id()) {
        const auto SaRegId = Frame.sa_reg_id();
        if (SaRegId != asmjit::x86::Gp::kIdSp && !(Frame.has_preserved_fp() && SaRegId == asmjit::x86::Gp::kIdBp)) {
            return {};
        }
    }

    std::vector<FWindowsUnwindOperation> UnwindCodes{};
    uint32_t PrologOffset = 0;
    uint8_t FrameRegister = 0;
    uint8_t FrameOffset = 0;
    RegMask SavedGp = Frame.saved_regs(RegGroup::kGp);

    if (Frame.has_preserved_fp()) {
        const auto FrameOffsetFromSp = Frame.sa_offset_from_sp();
        if ((FrameOffsetFromSp % 16) != 0 || (FrameOffsetFromSp / 16) > 15) {
            return {};
        }

        SavedGp &= ~asmjit::Support::bit_mask<RegMask>(asmjit::x86::Gp::kIdBp);
        PrologOffset += GpPushInstructionSize(asmjit::x86::Gp::kIdBp);
        UnwindCodes.push_back(MakeUnwindOperation(
            static_cast<uint8_t>(PrologOffset),
            UWOP_PUSH_NONVOL,
            asmjit::x86::Gp::kIdBp
        ));

        PrologOffset += FrameOffsetFromSp == 0
            ? GpMovInstructionSize(asmjit::x86::Gp::kIdBp, asmjit::x86::Gp::kIdSp)
            : GpLeaFromRspInstructionSize(asmjit::x86::Gp::kIdBp, FrameOffsetFromSp);
        UnwindCodes.push_back(MakeUnwindOperation(
            static_cast<uint8_t>(PrologOffset),
            UWOP_SET_FPREG,
            0
        ));
        FrameRegister = asmjit::x86::Gp::kIdBp;
        FrameOffset = static_cast<uint8_t>(FrameOffsetFromSp / 16);
    }

    asmjit::Support::BitWordIterator<RegMask> GpIt(SavedGp);
    while (GpIt.has_next()) {
        const auto RegId = GpIt.next();
        PrologOffset += GpPushInstructionSize(RegId);
        UnwindCodes.push_back(MakeUnwindOperation(
            static_cast<uint8_t>(PrologOffset),
            UWOP_PUSH_NONVOL,
            static_cast<uint8_t>(RegId)
        ));
    }

    if (const auto StackAdjustment = Frame.stack_adjustment(); StackAdjustment != 0) {
        PrologOffset += SubRspInstructionSize(StackAdjustment);
        if (StackAdjustment <= 128) {
            UnwindCodes.push_back(MakeUnwindOperation(
                static_cast<uint8_t>(PrologOffset),
                UWOP_ALLOC_SMALL,
                static_cast<uint8_t>((StackAdjustment - 8) / 8)
            ));
        }
        else if (StackAdjustment <= 0xffff * 8u) {
            UnwindCodes.push_back(MakeUnwindOperationWithSlot(
                static_cast<uint8_t>(PrologOffset),
                UWOP_ALLOC_LARGE,
                0,
                static_cast<uint16_t>(StackAdjustment / 8)
            ));
        }
        else {
            UnwindCodes.push_back(MakeUnwindOperationWithSplitOperand(
                static_cast<uint8_t>(PrologOffset),
                UWOP_ALLOC_LARGE,
                1,
                StackAdjustment
            ));
        }
    }

    const auto SavedVec = Frame.saved_regs(RegGroup::kVec);
    uint32_t VecOffset = Frame.extra_reg_save_offset();

    asmjit::Support::BitWordIterator<RegMask> VecIt(SavedVec);
    while (VecIt.has_next()) {
        const auto RegId = VecIt.next();
        if ((VecOffset % 16) != 0) return {};
        PrologOffset += VecStoreInstructionSize(RegId, VecOffset);
        const auto ShortUnwindOffset = VecOffset / 16;
        const auto RequiresFarEncoding = ShortUnwindOffset > 0xffff;
        if (RequiresFarEncoding) {
            UnwindCodes.push_back(MakeUnwindOperationWithSplitOperand(
                static_cast<uint8_t>(PrologOffset),
                UWOP_SAVE_XMM128_FAR,
                static_cast<uint8_t>(RegId),
                VecOffset
            ));
        }
        else {
            UnwindCodes.push_back(MakeUnwindOperationWithSlot(
                static_cast<uint8_t>(PrologOffset),
                UWOP_SAVE_XMM128,
                static_cast<uint8_t>(RegId),
                static_cast<uint16_t>(ShortUnwindOffset)
            ));
        }
        VecOffset += Frame.save_restore_reg_size(RegGroup::kVec);
    }

    return BuildWindowsUnwindInfo(UnwindCodes, FrameRegister, FrameOffset);
}
#endif

FThunkResult AddThunkToRuntime(CodeHolder& Code, const char* JitAddErrorMessage) {
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != asmjit::Error::kOk) {
        return std::unexpected(MakeThunkError(EThunkErrorCode::JitAddFailed, JitAddErrorMessage));
    }

    return FThunkPtr { Temp };
}

#if defined(_WIN64)
FThunkResult AddThunkToRuntime(CodeHolder& Code, const char* JitAddErrorMessage, const FThunkWindowsRuntimeInfo* WindowsRuntimeInfo) {
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != asmjit::Error::kOk) {
        return std::unexpected(MakeThunkError(EThunkErrorCode::JitAddFailed, JitAddErrorMessage));
    }

    if (WindowsRuntimeInfo) {
        if (auto RegistrationError = RegisterThunkWindowsUnwind(Temp, Code, *WindowsRuntimeInfo)) {
            (void)GetJitRuntime().release(Temp);
            return std::unexpected(std::move(*RegistrationError));
        }
    }

    return FThunkPtr { Temp };
}
#endif

}
