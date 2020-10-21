#include "fastinterp_ast_helper.hpp"
#include "codegen_context.hpp"
#include "destructor_helper.h"

namespace PochiVM
{

FastInterpSnippet WARN_UNUSED FIGenerateDestructorSequenceUntilScope(AstNodeBase* boundaryScope)
{
    FastInterpSnippet result { nullptr, nullptr };
    auto rit = thread_llvmContext->m_scopeStack.rbegin();
    while (rit != thread_llvmContext->m_scopeStack.rend())
    {
        const std::vector<DestructorIREmitter*>& vec = rit->second;
        for (auto rit2 = vec.rbegin(); rit2 != vec.rend(); rit2++)
        {
            AstVariable* e = assert_cast<AstVariable*>(*rit2);
            FastInterpSnippet snippet = e->GetFastInterpDestructorSnippet();
            result = result.AddContinuation(snippet);
        }
        if (rit->first == boundaryScope)
        {
            break;
        }
        rit++;
    }
    TestAssertImp(boundaryScope != nullptr, rit != thread_llvmContext->m_scopeStack.rend() && rit->first == boundaryScope);
    TestAssertImp(boundaryScope == nullptr, rit == thread_llvmContext->m_scopeStack.rend());
    return result;
}

}   // namespace PochiVM