#pragma once

#include "BindingThunk.hpp"
#include "RestoreThunk.hpp"

namespace RC::Thunk {

template<typename FnType>
FnType ThunkCast(const FThunkPtr& Thunk) {
    return reinterpret_cast<FnType>(Thunk.get());
}

}
