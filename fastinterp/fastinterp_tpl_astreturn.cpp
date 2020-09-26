#define POCHIVM_INSIDE_FASTINTERP_TPL_CPP

#include "fastinterp_tpl_helper.h"
#include "fastinterp_tpl_operandshape_helper.h"

namespace PochiVM
{

// Simple 'return' statement shape of 'return OSC'
//
struct FISimpleReturnImpl
{
    template<typename ReturnType, typename OscIndexType, OperandShapeCategory osc>
    static constexpr bool cond()
    {
        // If return type is 'void', must specify dummy osc = COMPLEX and oscIndex = int32_t
        //
        if (std::is_same<ReturnType, void>::value)
        {
            return std::is_same<OscIndexType, int32_t>::value && osc == OperandShapeCategory::COMPLEX;
        }
        if (!OperandShapeCategoryHelper::cond<OscIndexType, osc>()) { return false; }
        return true;
    }

    template<typename ReturnType, typename OscIndexType, OperandShapeCategory osc>
    static InterpControlSignal f() noexcept
    {
        if constexpr(!std::is_same<ReturnType, void>::value)
        {
            ReturnType value = OperandShapeCategoryHelper::get_0_1<ReturnType, OscIndexType, osc>();
            *GetLocalVarAddress<ReturnType>(0 /*offset*/) = value;
        }
        return InterpControlSignal::Return;
    }

    static auto metavars()
    {
        return CreateMetaVarList(
                    CreateTypeMetaVar("returnType"),
                    CreateTypeMetaVar("oscIndexType"),
                    CreateEnumMetaVar<OperandShapeCategory::X_END_OF_ENUM>("osc")
        );
    }
};

}   // namespace PochiVM

// build_fast_interp_lib.cpp JIT entry point
//
extern "C"
void __pochivm_build_fast_interp_library__()
{
    using namespace PochiVM;
    RegisterBoilerplate<FISimpleReturnImpl>();
}