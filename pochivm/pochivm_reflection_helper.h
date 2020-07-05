#pragma once

// TODO: header guard

#include "constexpr_array_concat_helper.h"
#include "for_each_primitive_type.h"

// Returns something like
//    const char *__pochivm_stringify_type__() [T = ###### ]
// where ###### is the interesting part
// WARNING: this breaks down when called outside a function.
//
template<typename T>
constexpr const char* __pochivm_stringify_type__()
{
    const char* const p = __PRETTY_FUNCTION__;
    return p;
}

// When v is a function pointer or member function pointer, returns something like
//    const char *__pochivm_stringify_value__() [v = &###### ]
// where ###### is the interesting part
// WARNING: this breaks down when called outside a function.
//
template<auto v>
constexpr const char* __pochivm_stringify_value__()
{
    const char* const p = __PRETTY_FUNCTION__;
    return p;
}

namespace PochiVM
{

namespace ReflectionHelper
{

// remove_param_type_ref<T>::type
// Transform a C++-type to a primitive type that we support by removing refs (but does not drop cv-qualifier)
//    Transform reference to pointer (e.g. 'int&' becomes 'int*')
//    Transform non-primitive pass-by-value parameter to pointer (e.g. 'std::string' becomes 'std::string*)
//    Lockdown rvalue-reference (e.g. 'int&&')
//
template<typename T>
struct remove_param_type_ref {
    // Non-primitive pass-by-value parameter, or pass-by-reference parameter, becomes a pointer
    //
    using type = typename std::add_pointer<T>::type;
};

// Primitive types are unchanged
//
#define F(t) template<> struct remove_param_type_ref<t> { using type = t; };
FOR_EACH_PRIMITIVE_TYPE
#undef F

#define F(t) template<> struct remove_param_type_ref<const t> { using type = const t; };
FOR_EACH_PRIMITIVE_TYPE
#undef F

// Pointer types are unchanged
//
template<typename T>
struct remove_param_type_ref<T*> {
    using type = T*;
};

// void type is unchanged
//
template<>
struct remove_param_type_ref<void> {
    using type = void;
};

template<>
struct remove_param_type_ref<const void> {
    using type = const void;
};

// Lockdown rvalue-reference parameter
//
template<typename T>
struct remove_param_type_ref<T&&> {
    static_assert(sizeof(T) == 0, "Function with rvalue-reference parameter is not supported!");
};

// recursive_remove_cv<T>::type
//    Drop const-qualifier recursively ('const int* const* const' becomes 'int**')
//    Lockdown volatile-qualifier
//
// It assumes that T is a type generated from remove_param_type_ref.
//
template<typename T>
struct recursive_remove_cv {
    using type = T;
};

template<typename T>
struct recursive_remove_cv<const T> {
    using type = typename recursive_remove_cv<T>::type;
};

template<typename T>
struct recursive_remove_cv<volatile T> {
    static_assert(sizeof(T) == 0, "Function with volatile parameter is not supported!");
};

template<typename T>
struct recursive_remove_cv<const volatile T> {
    static_assert(sizeof(T) == 0, "Function with volatile parameter is not supported!");
};

template<typename T>
struct recursive_remove_cv<T*> {
    using type = typename std::add_pointer<
                        typename recursive_remove_cv<T>::type
                 >::type;
};

// is_converted_pointer_type<T>::value
//    true if it is a parameter converted from a reference or a pass-by-value non-primitive type to a pointer
//
template<typename T>
struct is_converted_reference_type : std::integral_constant<bool,
    !std::is_same<typename remove_param_type_ref<T>::type, T>::value
> {};

// Whether the conversion is non-trivial (changes LLVM prototype)
// We make pass-by-value non-primitive-type parameter a pointer, which changes LLVM prototype
// The reference-to-pointer conversion does not, because C++ Itanium ABI specifies
// that reference should be passed as if it were a pointer to the object in ABI.
//
template<typename T>
struct is_nontrivial_arg_conversion : std::integral_constant<bool,
     !std::is_reference<T>::value && is_converted_reference_type<T>::value
> {};

template<typename T>
struct arg_transform_helper
{
    using RemovedRefArgType = typename remove_param_type_ref<T>::type;
    using RemovedCvArgType = typename recursive_remove_cv<RemovedRefArgType>::type;
    using ApiArgType = typename std::conditional<
            is_converted_reference_type<T>::value /*cond*/,
            typename std::remove_pointer<RemovedCvArgType>::type /*true*/,
            RemovedCvArgType /*false*/>::type;
    static const bool isApiArgVariable = is_converted_reference_type<T>::value;
};

template<typename R, typename... Args>
struct function_typenames_helper_internal
{
    static const size_t numArgs = sizeof...(Args);

    using ReturnType = R;
    using ApiReturnType = typename arg_transform_helper<ReturnType>::ApiArgType;

    template<size_t i> using ArgType = typename std::tuple_element<i, std::tuple<Args...>>::type;
    template<size_t i> using RemovedRefArgType = typename arg_transform_helper<ArgType<i>>::RemovedRefArgType;
    template<size_t i> using ApiArgType = typename arg_transform_helper<ArgType<i>>::ApiArgType;

    template<size_t i>
    static constexpr bool isApiArgTypeVariable() { return arg_transform_helper<ArgType<i>>::isApiArgVariable; }

    static constexpr bool isApiReturnTypeVariable() { return std::is_reference<ReturnType>::value; }

    template<size_t i>
    static constexpr bool isArgNontriviallyConverted() { return is_nontrivial_arg_conversion<ArgType<i>>::value; }

    static constexpr bool isRetValNontriviallyConverted() { return is_nontrivial_arg_conversion<ReturnType>::value; }

    template<size_t n, typename Enable = void>
    struct build_original_typenames_array_internal
    {
        static constexpr std::array<const char*, n+1> get()
        {
            return PochiVM::AstTypeHelper::constexpr_std_array_concat(
                    build_original_typenames_array_internal<n-1>::get(),
                    std::array<const char*, 1>{
                            __pochivm_stringify_type__<ArgType<n-1>>() });
        }
    };

    template<size_t n>
    struct build_original_typenames_array_internal<n, typename std::enable_if<(n == 0)>::type>
    {
        static constexpr std::array<const char*, n+1> get()
        {
            return std::array<const char*, 1>{ __pochivm_stringify_type__<ReturnType>() };
        }
    };

    static const char* const* get_original_ret_and_param_typenames()
    {
        static constexpr std::array<const char*, numArgs + 1> data =
                build_original_typenames_array_internal<numArgs>::get();
        return data.data();
    }

    template<size_t n, typename Enable = void>
    struct build_api_typenames_array_internal
    {
        static constexpr std::array<std::pair<const char*, bool>, n+1> get()
        {
            return PochiVM::AstTypeHelper::constexpr_std_array_concat(
                    build_api_typenames_array_internal<n-1>::get(),
                    std::array<std::pair<const char*, bool>, 1>{
                            std::make_pair(
                                    __pochivm_stringify_type__<ApiArgType<n-1>>(),
                                    isApiArgTypeVariable<n-1>() /*isVar*/)
                    });
        }
    };

    template<size_t n>
    struct build_api_typenames_array_internal<n, typename std::enable_if<(n == 0)>::type>
    {
        static constexpr std::array<std::pair<const char*, bool>, n+1> get()
        {
            return std::array<std::pair<const char*, bool>, 1>{
                    std::make_pair(__pochivm_stringify_type__<ApiReturnType>(),
                                   isApiReturnTypeVariable() /*isVar*/) };
        }
    };

    static const std::pair<const char*, bool>* get_api_ret_and_param_typenames()
    {
        static constexpr std::array<std::pair<const char*, bool>, numArgs + 1> data =
                build_api_typenames_array_internal<numArgs>::get();
        return data.data();
    }

    template<size_t n, typename Enable = void>
    struct is_wrapper_fn_required_internal
    {
        static constexpr bool get()
        {
            return isArgNontriviallyConverted<n-1>() || is_wrapper_fn_required_internal<n-1>::get();
        }
    };

    template<size_t n>
    struct is_wrapper_fn_required_internal<n, typename std::enable_if<(n == 0)>::type>
    {
        static constexpr bool get()
        {
            return isRetValNontriviallyConverted();
        }
    };

    constexpr static bool is_wrapper_fn_required()
    {
        return is_wrapper_fn_required_internal<numArgs>::get();
    }
};

template<typename T>
struct function_typenames_helper
{
    static_assert(sizeof(T) == 0, "T must be a a pointer to a free function or a static or non-static class method");
};

struct return_nullptr_class_typename
{
    static const char* get_class_typename()
    {
        return nullptr;
    }
};

// A static class method or a free function takes following optional qualifications:
//     except(optional) attr(optional)
// We support 'except', and lockdown 'attr'.
//
template<typename R, typename... Args>
struct function_typenames_helper<R(*)(Args...)>
    : function_typenames_helper_internal<R, Args...>
    , return_nullptr_class_typename
{
    static bool is_noexcept() { return false; }
    static bool is_const() { return false; }
};

template<typename R, typename... Args>
struct function_typenames_helper<R(*)(Args...) noexcept>
    : function_typenames_helper_internal<R, Args...>
    , return_nullptr_class_typename
{
    static bool is_noexcept() { return true; }
    static bool is_const() { return false; }
};

template<typename T>
struct class_name_helper_internal
{
    static const char* get_class_typename()
    {
        return __pochivm_stringify_type__<T>();
    }
};

// A non-static class method takes following optional qualifications:
//     cv(optional) ref(optional) except(optional) attr(optional)
// We support 'cv' and 'except', and lockdown 'ref' and 'attr'.
// ref '&&' is not supportable for same reason as rvalue-ref parameters.
// ref '&' seems not a problem, but just for simplicity also lock it down for now.
//
template<typename R, typename C, typename... Args>
struct function_typenames_helper<R(C::*)(Args...)>
    : function_typenames_helper_internal<R, Args...>
    , class_name_helper_internal<C>
{
    using ClassType = C;
    static bool is_noexcept() { return false; }
    static bool is_const() { return false; }
};

template<typename R, typename C, typename... Args>
struct function_typenames_helper<R(C::*)(Args...) noexcept>
    : function_typenames_helper_internal<R, Args...>
    , class_name_helper_internal<C>
{
    using ClassType = C;
    static bool is_noexcept() { return true; }
    static bool is_const() { return false; }
};

template<typename R, typename C, typename... Args>
struct function_typenames_helper<R(C::*)(Args...) const>
    : function_typenames_helper_internal<R, Args...>
    , class_name_helper_internal<C>
{
    using ClassType = C;
    static bool is_noexcept() { return false; }
    static bool is_const() { return true; }
};

template<typename R, typename C, typename... Args>
struct function_typenames_helper<R(C::*)(Args...) const noexcept>
    : function_typenames_helper_internal<R, Args...>
    , class_name_helper_internal<C>
{
    using ClassType = C;
    static bool is_noexcept() { return true; }
    static bool is_const() { return true; }
};

template<int... >
struct tpl_sequence {};

template<int N, int... S>
struct gen_tpl_sequence : gen_tpl_sequence<N-1, N-1, S...> {};

template<int... S>
struct gen_tpl_sequence<0, S...>
{
    using type = tpl_sequence<S...>;
};

// function_wrapper_helper<fnPtr>::wrapperFn
//    For functions with non-primitive parameter or return values,
//    we generate a wrapper function which wraps it with primitive parameters and return values.
//
//    The transformation rule is the following:
//    (1) If the function is a class member function, it is first converted to a free function
//        by appending the 'this' object as the first parameter.
//    (2) (a) If the return value is a by-value non-primitive type, the return value becomes void,
//            and a pointer-type is appended as the first parameter.
//             The return value is then constructed-in-place at the given address using in-place new.
//        (b) If the return value is by-reference, it becomes a pointer type.
//        (c) If the return value is a primitive type, it is unchanged.
//    (3) Each parameter which is a by-value non-primitive type or a by-reference type becomes a pointer.
//
// function_wrapper_helper<fnPtr>::isTrivial()
//    Return true if the transformation is trivial (so the transformation is not needed)
//
template<auto fnPtr>
class function_wrapper_helper
{
private:
    using FnType = decltype(fnPtr);
    using FnTypeInfo = function_typenames_helper<FnType>;
    using SeqType = typename gen_tpl_sequence<FnTypeInfo::numArgs>::type;

    template<int k> using InType = typename FnTypeInfo::template RemovedRefArgType<k>;
    template<int k> using OutType = typename FnTypeInfo::template ArgType<k>;

    // object type parameter (passed in by value)
    //
    template<int k, typename Enable = void>
    struct convert_param_internal
    {
        static_assert(std::is_same<InType<k>, typename std::add_pointer<OutType<k>>::type>::value
                              && !std::is_reference<OutType<k>>::value, "wrong specialization");

        static OutType<k>& get(InType<k> v)
        {
            return *v;
        }
    };

    // reference type parameter
    //
    template<int k>
    struct convert_param_internal<k, typename std::enable_if<(
                                             std::is_reference<OutType<k>>::value)>::type>
    {
        static_assert(std::is_lvalue_reference<OutType<k>>::value,
                      "function returning rvalue reference is not supported");
        static_assert(std::is_same<InType<k>, typename std::add_pointer<OutType<k>>::type>::value,
                      "InType should be the pointer type");

        static OutType<k> get(InType<k> v)
        {
            return *v;
        }
    };

    // primitive type or pointer type parameter
    //
    template<int k>
    struct convert_param_internal<k, typename std::enable_if<(
            !is_converted_reference_type<OutType<k>>::value)>::type>
    {
        static_assert(std::is_same<InType<k>, OutType<k>>::value, "wrong specialization");

        static OutType<k> get(InType<k> v)
        {
            return v;
        }
    };

    template<int k, typename T>
    static OutType<k> convert_param(T input)
    {
        return convert_param_internal<k>::get(std::get<k>(input));
    }

    template<typename F2, typename Enable = void>
    struct call_fn_helper
    {
        template<typename R2, int... S, typename... Args>
        static R2 call(tpl_sequence<S...> /*unused*/, Args... args)
        {
            return fnPtr(convert_param<S>(std::forward_as_tuple(args...))...);
        }
    };

    template<typename F2>
    struct call_fn_helper<F2, typename std::enable_if<(std::is_member_function_pointer<F2>::value)>::type>
    {
        template<typename R2, int... S, typename C, typename... Args>
        static R2 call(tpl_sequence<S...> /*unused*/, C c, Args... args)
        {
            return (c->*fnPtr)(convert_param<S>(std::forward_as_tuple(args...))...);
        }
    };

    template<typename R2, typename... Args>
    static R2 call_fn_impl(Args... args)
    {
        return call_fn_helper<FnType>::template call<R2>(SeqType(), args...);
    }

    // If the function returns a non-primitive object type, change it to a pointer as a special parameter
    //
    template<typename R2, typename Enable = void>
    struct call_fn_wrapper
    {
        using R2NoConst = typename std::remove_const<R2>::type;

        template<typename... Args>
        static void call(R2NoConst* ret, Args... args)
        {
            new (ret) R2NoConst(call_fn_impl<R2NoConst>(args...));
        }

        template<typename F2, typename Enable2 = void>
        struct wrapper_fn_ptr
        {
            template<int N, int... S>
            struct impl : impl<N-1, N-1, S...> { };

            template<int... S>
            struct impl<0, S...>
            {
                using type = void(*)(R2NoConst*, InType<S>...);
                static constexpr type value = call<InType<S>...>;
            };
        };

        template<typename F2>
        struct wrapper_fn_ptr<F2, typename std::enable_if<(std::is_member_function_pointer<F2>::value)>::type>
        {
            template<int N, int... S>
            struct impl : impl<N-1, N-1, S...> { };

            template<int... S>
            struct impl<0, S...>
            {
                using ClassTypeStar = typename std::add_pointer<typename FnTypeInfo::ClassType>::type;
                using type = void(*)(R2NoConst*, ClassTypeStar, InType<S>...);
                static constexpr type value = call<ClassTypeStar, InType<S>...>;
            };
        };
    };

    // If the function returns a primitive or pointer type or void, return directly
    //
    template<typename R2>
    struct call_fn_wrapper<R2, typename std::enable_if<(
            !is_converted_reference_type<R2>::value)>::type>
    {
        template<typename... Args>
        static R2 call(Args... args)
        {
            return call_fn_impl<R2>(args...);
        }

        template<typename F2, typename Enable2 = void>
        struct wrapper_fn_ptr
        {
            template<int N, int... S>
            struct impl : impl<N-1, N-1, S...> { };

            template<int... S>
            struct impl<0, S...>
            {
                using type = R2(*)(InType<S>...);
                static constexpr type value = call<InType<S>...>;
            };
        };

        template<typename F2>
        struct wrapper_fn_ptr<F2, typename std::enable_if<(std::is_member_function_pointer<F2>::value)>::type>
        {
            template<int N, int... S>
            struct impl : impl<N-1, N-1, S...> { };

            template<int... S>
            struct impl<0, S...>
            {
                using ClassTypeStar = typename std::add_pointer<typename FnTypeInfo::ClassType>::type;
                using type = R2(*)(ClassTypeStar, InType<S>...);
                static constexpr type value = call<ClassTypeStar, InType<S>...>;
            };
        };
    };

    // if the function returns lvalue reference, return a pointer instead
    //
    template<typename R2>
    struct call_fn_wrapper<R2, typename std::enable_if<(std::is_reference<R2>::value)>::type>
    {
        static_assert(std::is_lvalue_reference<R2>::value, "function returning rvalue reference is not supported");

        using R2Star = typename std::add_pointer<R2>::type;

        template<typename... Args>
        static R2Star call(Args... args)
        {
            return &call_fn_impl<R2>(args...);
        }

        template<typename F2, typename Enable2 = void>
        struct wrapper_fn_ptr
        {
            template<int N, int... S>
            struct impl : impl<N-1, N-1, S...> { };

            template<int... S>
            struct impl<0, S...>
            {
                using type = R2Star(*)(InType<S>...);
                static constexpr type value = call<InType<S>...>;
            };
        };

        template<typename F2>
        struct wrapper_fn_ptr<F2, typename std::enable_if<(std::is_member_function_pointer<F2>::value)>::type>
        {
            template<int N, int... S>
            struct impl : impl<N-1, N-1, S...> { };

            template<int... S>
            struct impl<0, S...>
            {
                using ClassTypeStar = typename std::add_pointer<typename FnTypeInfo::ClassType>::type;
                using type = R2Star(*)(ClassTypeStar, InType<S>...);
                static constexpr type value = call<ClassTypeStar, InType<S>...>;
            };
        };
    };

    using WrapperFnPtrStructType =
            typename call_fn_wrapper<typename FnTypeInfo::ReturnType>::
                    template wrapper_fn_ptr<FnType>::
                            template impl<FnTypeInfo::numArgs>;

public:
    // The generated function that wraps the fnPtr, hiding all the non-trivial parameters and return values
    //
    using WrapperFnPtrType = typename WrapperFnPtrStructType::type;
    static constexpr WrapperFnPtrType wrapperFn = WrapperFnPtrStructType::value;

    // Whether the wrapper is required. It is required if the wrapped function actually
    // contains at least one non-trivial parameter or return value
    //
    static constexpr bool isWrapperFnRequired = FnTypeInfo::is_wrapper_fn_required();

    // Whether the wrapped function returns a non-primitive type by value.
    // In that case, the ret value is changed to void, and a pointer is added to the first parameter,
    // into which the return value will be in-place constructed.
    // This is essentially the same as the C++ ABI StructRet transform, except we do it in C++ and unconditionally
    // for all non-primitive return types (normally C++ ABI only do this if the returned struct is large)
    //
    static constexpr bool isSretTransformed = FnTypeInfo::isRetValNontriviallyConverted();
};

template <typename MethPtr>
void* GetClassMethodPtrHelper(MethPtr p)
{
    union U { MethPtr meth; void* ptr; };
    return (reinterpret_cast<U*>(&p))->ptr;
}

// get_function_pointer_address(t)
// Returns the void* address of t, where t must be a pointer to a free function or a static or non-static class method
//
template<typename T, typename Enable = void>
struct function_pointer_address_helper
{
    static_assert(sizeof(T) == 0, "T must be a pointer to a free function or a static or non-static class method");
};

template<typename T>
struct function_pointer_address_helper<T, typename std::enable_if<
        std::is_member_function_pointer<T>::value>::type>
{
    static void* get(T t)
    {
        return GetClassMethodPtrHelper(t);
    }
};

template<typename T>
struct function_pointer_address_helper<T, typename std::enable_if<
        std::is_pointer<T>::value && std::is_function<typename std::remove_pointer<T>::type>::value >::type>
{
    static void* get(T t)
    {
        return reinterpret_cast<void*>(t);
    }
};

template<typename T>
void* get_function_pointer_address(T t)
{
    return function_pointer_address_helper<T>::get(t);
}

enum class FunctionType
{
    FreeFn,
    StaticMemberFn,
    NonStaticMemberFn
};

struct RawFnTypeNamesInfo
{
    RawFnTypeNamesInfo(FunctionType fnType,
                       size_t numArgs,
                       const std::pair<const char*, bool>* apiRetAndArgTypenames,
                       const char* const* originalRetAndArgTypenames,
                       const char* classTypename,
                       const char* fnName,
                       void* fnAddress,
                       bool isConst,
                       bool isNoExcept,
                       bool isUsingWrapper,
                       bool isWrapperUsingSret,
                       void* wrapperFnAddress)
          : m_fnType(fnType)
          , m_numArgs(numArgs)
          , m_apiRetAndArgTypenames(apiRetAndArgTypenames)
          , m_originalRetAndArgTypenames(originalRetAndArgTypenames)
          , m_classTypename(classTypename)
          , m_fnName(fnName)
          , m_fnAddress(fnAddress)
          , m_isConst(isConst)
          , m_isNoExcept(isNoExcept)
          , m_isUsingWrapper(isUsingWrapper)
          , m_isWrapperUsingSret(isWrapperUsingSret)
          , m_wrapperFnAddress(wrapperFnAddress)
    {
        if (!isUsingWrapper) { ReleaseAssert(!isWrapperUsingSret); }
    }

    FunctionType m_fnType;
    // The number of arguments this function takes, not counting 'this' and potential sret
    //
    size_t m_numArgs;
    // The typenames of return value and args
    //    [0] is the typename of ret
    //    [1, m_numArgs] is the typename of the ith argument
    // original: the original C++ defintion
    // transformed: our transformed definition into our supported typesystem
    //
    const std::pair<const char*, bool>* m_apiRetAndArgTypenames;
    const char* const* m_originalRetAndArgTypenames;
    // nullptr if it is a free function or a static class method,
    // otherwise the class typename
    //
    const char* m_classTypename;
    // the name of the function
    //
    const char* m_fnName;
    // the address of the function
    //
    void* m_fnAddress;
    // Whether the function has 'const' attribute (must be member function)
    //
    bool m_isConst;
    // Whether the function has 'noexcept' attribute
    //
    bool m_isNoExcept;

    // Whether we must call into the wrapper function instead of the original function
    //
    bool m_isUsingWrapper;
    // Whether the wrapper function is using the 'sret' attribute
    //
    bool m_isWrapperUsingSret;
    // The address of the wrapper function
    //
    void* m_wrapperFnAddress;
};

// get_raw_fn_typenames_info<t>::get()
// Returns the RawFnTypeNamesInfo struct for function pointer 't'.
// 't' must be a pointer to a free function or a static or non-static class method.
//
template<auto t>
struct get_raw_fn_typenames_info
{
    using fnInfo = function_typenames_helper<decltype(t)>;
    using wrapper = function_wrapper_helper<t>;
    using wrappedFnType = typename wrapper::WrapperFnPtrType;

    static constexpr wrappedFnType wrapperFn = wrapper::wrapperFn;

    static const char* get_function_name()
    {
        return __pochivm_stringify_value__<t>();
    }

    static RawFnTypeNamesInfo get(FunctionType fnType)
    {
        if (fnType == FunctionType::StaticMemberFn || fnType == FunctionType::FreeFn)
        {
            if (!(std::is_pointer<decltype(t)>::value &&
                  std::is_function<typename std::remove_pointer<decltype(t)>::type>::value))
            {
                fprintf(stderr, "The provided parameter is not a function pointer!\n");
                abort();
            }
        }
        else
        {
            if (!std::is_member_pointer<decltype(t)>::value)
            {
                fprintf(stderr, "The provided parameter is not a member function pointer!\n");
                abort();
            }
        }

        return RawFnTypeNamesInfo(fnType,
                                  fnInfo::numArgs,
                                  fnInfo::get_api_ret_and_param_typenames(),
                                  fnInfo::get_original_ret_and_param_typenames(),
                                  fnInfo::get_class_typename(),
                                  get_function_name(),
                                  get_function_pointer_address(t),
                                  fnInfo::is_const(),
                                  fnInfo::is_noexcept(),
                                  wrapper::isWrapperFnRequired,
                                  wrapper::isSretTransformed,
                                  reinterpret_cast<void*>(wrapperFn));
    }
};

}   // namespace ReflectionHelper

void __pochivm_report_info__(ReflectionHelper::RawFnTypeNamesInfo*);

template<auto t>
void RegisterFreeFn()
{
    ReflectionHelper::RawFnTypeNamesInfo info =
            ReflectionHelper::get_raw_fn_typenames_info<t>::get(ReflectionHelper::FunctionType::FreeFn);
    __pochivm_report_info__(&info);
}

template<auto t>
void RegisterMemberFn()
{
    ReflectionHelper::RawFnTypeNamesInfo info =
            ReflectionHelper::get_raw_fn_typenames_info<t>::get(ReflectionHelper::FunctionType::NonStaticMemberFn);
    __pochivm_report_info__(&info);
}

template<auto t>
void RegisterStaticMemberFn()
{
    ReflectionHelper::RawFnTypeNamesInfo info =
            ReflectionHelper::get_raw_fn_typenames_info<t>::get(ReflectionHelper::FunctionType::StaticMemberFn);
    __pochivm_report_info__(&info);
}

}   // namespace PochiVM