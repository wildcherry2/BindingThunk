#pragma once

#include "BindingThunk.hpp"
#include "RestoreThunk.hpp"

template<typename FnType>
FnType ThunkCast(const FThunkPtr& Thunk) {
    return reinterpret_cast<FnType>(Thunk.get());
}
