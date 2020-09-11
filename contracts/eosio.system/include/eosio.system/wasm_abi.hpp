#pragma once

#include <eosio/action.hpp>
#include <eosio/from_json.hpp>
#include <eosio/to_bin.hpp>
#include <eosio/to_json.hpp>

extern "C" void __wasm_call_ctors();

namespace eosio
{

    template <typename... Ts>
    struct type_list
    {
    };

    template <typename Arg0, typename... Args, typename JsonStream, typename BinStream>
    void args_json_to_bin(type_list<Arg0, Args...>, JsonStream &json_stream, BinStream &bin_stream)
    {
        if constexpr (is_ignore_v<std::decay_t<Arg0>>)
        {
            args_json_to_bin(type_list<typename std::decay_t<Arg0>::type, Args...>{}, json_stream, bin_stream);
        }
        else
        {
            std::decay_t<Arg0> obj{};
            from_json(obj, json_stream);
            to_bin(obj, bin_stream);
            args_json_to_bin(type_list<Args...>{}, json_stream, bin_stream);
        }
    }

    template <typename JsonStream, typename BinStream>
    void args_json_to_bin(type_list<>, JsonStream &json_stream, BinStream &bin_stream)
    {
    }

    template <typename C, typename R, typename... Args, typename JsonStream, typename BinStream>
    void args_json_to_bin(R (C::*)(Args...), JsonStream &json_stream, BinStream &bin_stream)
    {
        json_stream.get_start_array();
        args_json_to_bin(type_list<Args...>{}, json_stream, bin_stream);
        json_stream.get_end_array();
    }

    template <typename C, typename R, typename... Args, typename JsonStream, typename BinStream>
    void args_json_to_bin(R (C::*)(Args...) const, JsonStream &json_stream, BinStream &bin_stream)
    {
        json_stream.get_start_array();
        args_json_to_bin(type_list<Args...>{}, json_stream, bin_stream);
        json_stream.get_end_array();
    }

    template <typename Arg0, typename... Args, typename BinStream, typename JsonStream>
    void args_bin_to_json(type_list<Arg0, Args...>, bool need_comma, BinStream &bin_stream,
                          JsonStream &json_stream)
    {
        if constexpr (is_ignore_v<std::decay_t<Arg0>>)
        {
            args_bin_to_json(type_list<typename std::decay_t<Arg0>::type, Args...>{}, need_comma, bin_stream,
                             json_stream);
        }
        else
        {
            std::decay_t<Arg0> obj{};
            if (need_comma)
            {
                json_stream.write(',');
            }
            from_bin(obj, bin_stream);
            to_json(obj, json_stream);
            args_bin_to_json(type_list<Args...>{}, true, bin_stream, json_stream);
        }
    }

    template <typename BinStream, typename JsonStream>
    void args_bin_to_json(type_list<>, bool, BinStream &bin_stream, JsonStream &json_stream)
    {
    }

    template <typename C, typename R, typename... Args, typename BinStream, typename JsonStream>
    void args_bin_to_json(R (C::*)(Args...), BinStream &bin_stream, JsonStream &json_stream)
    {
        json_stream.write('[');
        args_bin_to_json(type_list<Args...>{}, false, bin_stream, json_stream);
        json_stream.write(']');
    }

    template <typename C, typename R, typename... Args, typename BinStream, typename JsonStream>
    void args_bin_to_json(R (C::*)(Args...) const, BinStream &bin_stream, JsonStream &json_stream)
    {
        json_stream.write('[');
        args_bin_to_json(type_list<Args...>{}, false, bin_stream, json_stream);
        json_stream.write(']');
    }

    template <typename C, typename R, typename... Args, typename BinStream, typename JsonStream>
    void ret_bin_to_json(R (C::*)(Args...), BinStream &bin_stream, JsonStream &json_stream)
    {
        if constexpr (std::is_void_v<R>)
        {
            json_stream.write("null", 4);
        }
        else
        {
            std::decay_t<R> obj{};
            from_bin(obj, bin_stream);
            to_json(obj, json_stream);
        }
    }

    template <typename C, typename R, typename... Args, typename BinStream, typename JsonStream>
    void ret_bin_to_json(R (C::*)(Args...) const, BinStream &bin_stream, JsonStream &json_stream)
    {
        if constexpr (std::is_void_v<R>)
        {
            json_stream.write("null", 4);
        }
        else
        {
            std::decay_t<R> obj{};
            from_bin(obj, bin_stream);
            to_json(obj, json_stream);
        }
    }

    struct action_args_json_to_bin_result
    {
        name short_name;
        std::vector<char> bin;
    };

    template <typename Contract, typename JsonStream>
    action_args_json_to_bin_result action_args_json_to_bin_impl(JsonStream &json_stream)
    {
        json_stream.get_start_array();
        action_args_json_to_bin_result result;
        vector_stream bin_stream{result.bin};
        auto action = json_stream.get_string();
        bool found = false;
        eosio_for_each_action((Contract *)nullptr, [&](const char *name, eosio::name short_name, auto get_action_fn) {
            if (!found && action == name)
            {
                found = true;
                result.short_name = short_name;
                args_json_to_bin((decltype(get_action_fn((Contract *)nullptr))) nullptr, json_stream, bin_stream);
            }
        });
        check(found, "action not found");
        json_stream.get_end_array();
        json_stream.get_end();
        return result;
    }

    template <typename Contract, typename BinStream>
    std::vector<char> action_args_bin_to_json_impl(BinStream &bin_stream)
    {
        std::vector<char> result;
        vector_stream json_stream{result};
        auto short_name = from_bin<name>(bin_stream);
        json_stream.write("{\"short_name\":", 14);
        to_json(short_name, json_stream);
        bool found = false;
        eosio_for_each_action((Contract *)nullptr, [&](const char *name, eosio::name sn, auto get_action_fn) {
            if (!found && short_name == sn)
            {
                found = true;
                json_stream.write(",\"long_name\":", 13);
                to_json(name, json_stream);
                json_stream.write(",\"args\":", 8);
                args_bin_to_json((decltype(get_action_fn((Contract *)nullptr))) nullptr, bin_stream, json_stream);
            }
        });
        check(found, "action not found");
        json_stream.write('}');
        return result;
    }

    template <typename Contract, typename BinStream>
    std::vector<char> action_ret_bin_to_json_impl(BinStream &bin_stream)
    {
        std::vector<char> result;
        vector_stream json_stream{result};
        auto short_name = from_bin<name>(bin_stream);
        json_stream.write("{\"short_name\":", 14);
        to_json(short_name, json_stream);
        bool found = false;
        eosio_for_each_action((Contract *)nullptr, [&](const char *name, eosio::name sn, auto get_action_fn) {
            if (!found && short_name == sn)
            {
                found = true;
                json_stream.write(",\"long_name\":", 13);
                to_json(name, json_stream);
                json_stream.write(",\"return_value\":", 16);
                ret_bin_to_json((decltype(get_action_fn((Contract *)nullptr))) nullptr, bin_stream, json_stream);
            }
        });
        check(found, "action not found");
        json_stream.write('}');
        return result;
    }

#define EOSIO_DECLARE_ACTIONS_INTERNAL_WRAPPER(DUMMY, ACT) \
    using ACT = eosio::action_wrapper<#ACT##_h, &__contract_class::ACT, __contract_account>;

#define EOSIO_DECLARE_ACTIONS_INTERNAL_DISPATCH(DUMMY, ACT)                                                 \
    case eosio::hash_name(#ACT):                                                                            \
        executed = eosio::execute_action(eosio::name(receiver), eosio::name(code), &__contract_class::ACT); \
        break;

#define EOSIO_DECLARE_ACTIONS_INTERNAL_JSON_ACTION_LIST_ITEM(DUMMY, ACT) ",\"" #ACT "\""

#define EOSIO_DECLARE_ACTIONS_INTERNAL_JSON_ACTION_LIST(ACT, ...) \
    "\"" #ACT "\"" EOSIO_MAP_REUSE_ARG0(EOSIO_DECLARE_ACTIONS_INTERNAL_JSON_ACTION_LIST_ITEM, dummy, __VA_ARGS__)

#define EOSIO_DECLARE_ACTIONS_REFLECT_ITEM(DUMMY, ACT) \
    f(#ACT, #ACT##_h,                                  \
      [](auto p) -> decltype(&std::decay_t<decltype(*p)>::ACT) { return &std::decay_t<decltype(*p)>::ACT; });

#define EOSIO_DECLARE_ACTIONS(CONTRACT_CLASS, CONTRACT_ACCOUNT, ...)                              \
    namespace actions                                                                             \
    {                                                                                             \
        static constexpr auto __contract_account = CONTRACT_ACCOUNT;                              \
        using __contract_class = CONTRACT_CLASS;                                                  \
        EOSIO_MAP_REUSE_ARG0(EOSIO_DECLARE_ACTIONS_INTERNAL_WRAPPER, dummy, __VA_ARGS__)          \
                                                                                                  \
        inline void eosio_apply(uint64_t receiver, uint64_t code, uint64_t action)                \
        {                                                                                         \
            eosio::check(code == receiver, "notifications not supported by dispatcher");          \
            bool executed = false;                                                                \
            switch (action)                                                                       \
            {                                                                                     \
                EOSIO_MAP_REUSE_ARG0(EOSIO_DECLARE_ACTIONS_INTERNAL_DISPATCH, dummy, __VA_ARGS__) \
            }                                                                                     \
            eosio::check(executed == true, "unknown action");                                     \
        }                                                                                         \
    }                                                                                             \
    inline constexpr const char eosio_json_action_list[] =                                        \
        "[" EOSIO_DECLARE_ACTIONS_INTERNAL_JSON_ACTION_LIST(__VA_ARGS__) "]";                     \
    template <typename F>                                                                         \
    constexpr void eosio_for_each_action(CONTRACT_CLASS *, F f)                                   \
    {                                                                                             \
        EOSIO_MAP_REUSE_ARG0(EOSIO_DECLARE_ACTIONS_REFLECT_ITEM, dummy, __VA_ARGS__)              \
    }

#define DEFINE_EOSIO_WASM_ABI(CONTRACT_CLASS)                                                     \
    extern "C" void initialize() { __wasm_call_ctors(); }                                         \
    extern "C" void get_actions() { return eosio::set_output_data_zstr(eosio_json_action_list); } \
    extern "C" void action_args_json_to_bin()                                                     \
    {                                                                                             \
        auto json = eosio::get_input_data_str();                                                  \
        eosio::json_token_stream json_stream(json.data());                                        \
        auto result = eosio::action_args_json_to_bin_impl<CONTRACT_CLASS>(json_stream);           \
        eosio::set_output_data(&result.short_name.value, sizeof(result.short_name.value));        \
        eosio::set_output_data(result.bin);                                                       \
    }                                                                                             \
    extern "C" void action_args_bin_to_json()                                                     \
    {                                                                                             \
        auto bin = eosio::get_input_data();                                                       \
        eosio::input_stream bin_stream(bin);                                                      \
        auto result = eosio::action_args_bin_to_json_impl<CONTRACT_CLASS>(bin_stream);            \
        eosio::set_output_data(result);                                                           \
    }                                                                                             \
    extern "C" void action_ret_bin_to_json()                                                      \
    {                                                                                             \
        auto bin = eosio::get_input_data();                                                       \
        eosio::input_stream bin_stream(bin);                                                      \
        auto result = eosio::action_ret_bin_to_json_impl<CONTRACT_CLASS>(bin_stream);             \
        eosio::set_output_data(result);                                                           \
    }

} // namespace eosio
