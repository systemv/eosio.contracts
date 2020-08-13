#include <Runtime/Runtime.h>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/wast_to_wasm.hpp>
#include <fc/log/logger.hpp>
#include <iostream>
#include <sstream>
#include <fstream>

#include "eosio.system_tester.hpp"
#include "csvwriter.hpp"

#define GENERATE_CSV true
#define CSV_FILENAME "model_tests.csv"

inline constexpr int64_t rentbw_frac = 1'000'000'000'000'000ll; // 1.0 = 10^15
inline constexpr int64_t stake_weight = 100'000'000'0000ll;     // 10^12

struct rentbw_config_resource
{
   fc::optional<int64_t> current_weight_ratio = {};
   fc::optional<int64_t> target_weight_ratio = {};
   fc::optional<int64_t> assumed_stake_weight = {};
   fc::optional<time_point_sec> target_timestamp = {};
   fc::optional<double> exponent = {};
   fc::optional<uint32_t> decay_secs = {};
   fc::optional<asset> min_price = {};
   fc::optional<asset> max_price = {};
};
FC_REFLECT(rentbw_config_resource,                                                             //
           (current_weight_ratio)(target_weight_ratio)(assumed_stake_weight)(target_timestamp) //
           (exponent)(decay_secs)(min_price)(max_price))

struct rentbw_config
{
   rentbw_config_resource net = {};
   rentbw_config_resource cpu = {};
   fc::optional<uint32_t> rent_days = {};
   fc::optional<asset> min_rent_fee = {};
};
FC_REFLECT(rentbw_config, (net)(cpu)(rent_days)(min_rent_fee))

struct rentbw_state_resource
{
   uint8_t version;
   int64_t weight;
   int64_t weight_ratio;
   int64_t assumed_stake_weight;
   int64_t initial_weight_ratio;
   int64_t target_weight_ratio;
   time_point_sec initial_timestamp;
   time_point_sec target_timestamp;
   double exponent;
   uint32_t decay_secs;
   asset min_price;
   asset max_price;
   int64_t utilization;
   int64_t adjusted_utilization;
   time_point_sec utilization_timestamp;
};
FC_REFLECT(rentbw_state_resource,                                                                           //
           (version)(weight)(weight_ratio)(assumed_stake_weight)(initial_weight_ratio)(target_weight_ratio) //
           (initial_timestamp)(target_timestamp)(exponent)(decay_secs)(min_price)(max_price)(utilization)   //
           (adjusted_utilization)(utilization_timestamp))

struct rentbw_state
{
   uint8_t version;
   rentbw_state_resource net;
   rentbw_state_resource cpu;
   uint32_t rent_days;
   asset min_rent_fee;
};
FC_REFLECT(rentbw_state, (version)(net)(cpu)(rent_days)(min_rent_fee))

using namespace eosio_system;

struct rentbw_tester : eosio_system_tester
{
   CSVWriter csv;

   rentbw_tester()
   {
      create_accounts_with_resources({N(eosio.reserv)});

      if (GENERATE_CSV)
      {
         CSVWriter header;
         header.newRow() << "last_block_time"
                         << "before_state.net.assumed_stake_weight"
                         << "before_state.net.weight_ratio"
                         << "before_state.net.weight"
                         << "before_receiver.net"
                         << "after_receiver.net"
                         << "after_receiver.net-before_receiver.net"
                         << "before_payer.liquid-after_payer.liquid"
                         << "before_reserve.net"
                         << "after_reserve.net"
                         << "before_reserve.cpu"
                         << "after_reserve.cpu"

                         << "weight"
                         << "weight_ratio"
                         << "assumed_stake_weight"
                         << "initial_weight_ratio"
                         << "target_weight_ratio"
                         << "initial_timestamp"
                         << "target_timestamp"
                         << "exponent"
                         << "decay_secs"
                         << "min_price"
                         << "max_price"
                         << "utilization"
                         << "adjusted_utilization"
                         << "utilization_timestamp";

         header.writeToFile(CSV_FILENAME);
      }
   }

   ~rentbw_tester()
   {
      if (GENERATE_CSV)
      {
         csv.writeToFile(CSV_FILENAME, true);
      }
   }

   void start_rex()
   {
      create_account_with_resources(N(rexholder111), config::system_account_name, core_sym::from_string("1.0000"),
                                    false);
      transfer(config::system_account_name, N(rexholder111), core_sym::from_string("1001.0000"));
      BOOST_REQUIRE_EQUAL("", stake(N(rexholder111), N(rexholder111), core_sym::from_string("500.0000"),
                                    core_sym::from_string("500.0000")));
      create_account_with_resources(N(proxyaccount), config::system_account_name, core_sym::from_string("1.0000"),
                                    false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));
      BOOST_REQUIRE_EQUAL("",
                          push_action(N(proxyaccount), N(regproxy), mvo()("proxy", "proxyaccount")("isproxy", true)));
      BOOST_REQUIRE_EQUAL("", vote(N(rexholder111), {}, N(proxyaccount)));
      BOOST_REQUIRE_EQUAL("", push_action(N(rexholder111), N(deposit),
                                          mvo()("owner", "rexholder111")("amount", asset::from_string("1.0000 TST"))));
      BOOST_REQUIRE_EQUAL("", push_action(N(rexholder111), N(buyrex),
                                          mvo()("from", "rexholder111")("amount", asset::from_string("1.0000 TST"))));
   }

   template <typename F>
   rentbw_config make_config(F f)
   {
      rentbw_config config;

      config.net.current_weight_ratio = rentbw_frac;
      config.net.target_weight_ratio = rentbw_frac / 100;
      config.net.assumed_stake_weight = stake_weight;
      config.net.target_timestamp = control->head_block_time() + fc::days(100);
      config.net.exponent = 2;
      config.net.decay_secs = fc::days(1).to_seconds();
      config.net.min_price = asset::from_string("0.0000 TST");
      config.net.max_price = asset::from_string("1000000.0000 TST");

      config.cpu.current_weight_ratio = rentbw_frac;
      config.cpu.target_weight_ratio = rentbw_frac / 100;
      config.cpu.assumed_stake_weight = stake_weight;
      config.cpu.target_timestamp = control->head_block_time() + fc::days(100);
      config.cpu.exponent = 2;
      config.cpu.decay_secs = fc::days(1).to_seconds();
      config.cpu.min_price = asset::from_string("0.0000 TST");
      config.cpu.max_price = asset::from_string("1000000.0000 TST");

      config.rent_days = 30;
      config.min_rent_fee = asset::from_string("1.0000 TST");

      f(config);
      return config;
   }

   rentbw_config make_config()
   {
      return make_config([](auto &) {});
   }

   template <typename F>
   rentbw_config make_default_config(F f)
   {
      rentbw_config config;
      f(config);
      return config;
   }

   action_result configbw(const rentbw_config &config)
   {
      // Verbose solution needed to work around bug in abi_serializer that fails if optional values aren't explicitly
      // specified with a null value.

      auto optional_to_variant = [](const auto &v) -> fc::variant {
         return (!v ? fc::variant() : fc::variant(*v));
      };

      auto resource_conf_vo = [&optional_to_variant](const rentbw_config_resource &c) {
         return mvo("current_weight_ratio", optional_to_variant(c.current_weight_ratio))("target_weight_ratio", optional_to_variant(c.target_weight_ratio))("assumed_stake_weight", optional_to_variant(c.assumed_stake_weight))("target_timestamp", optional_to_variant(c.target_timestamp))("exponent", optional_to_variant(c.exponent))("decay_secs", optional_to_variant(c.decay_secs))("min_price", optional_to_variant(c.min_price))("max_price", optional_to_variant(c.max_price));
      };

      auto conf = mvo("net", resource_conf_vo(config.net))("cpu", resource_conf_vo(config.cpu))("rent_days", optional_to_variant(config.rent_days))("min_rent_fee", optional_to_variant(config.min_rent_fee));

      //idump((fc::json::to_pretty_string(conf)));
      return push_action(config::system_account_name, N(configrentbw), mvo()("args", std::move(conf)));

      // If abi_serializer worked correctly, the following is all that would be needed:
      //return push_action(config::system_account_name, N(configrentbw), mvo()("args", config));
   }

   action_result rentbwexec(name user, uint16_t max)
   {
      return push_action(user, N(rentbwexec), mvo()("user", user)("max", max));
   }

   action_result rentbw(const name &payer, const name &receiver, uint32_t days, int64_t net_frac, int64_t cpu_frac,
                        const asset &max_payment)
   {
      return push_action(payer, N(rentbw),
                         mvo()("payer", payer)("receiver", receiver)("days", days)("net_frac", net_frac)(
                             "cpu_frac", cpu_frac)("max_payment", max_payment));
   }

   rentbw_state get_state()
   {
      vector<char> data = get_row_by_account(config::system_account_name, {}, N(rent.state), N(rent.state));
      return fc::raw::unpack<rentbw_state>(data);
   }

   struct account_info
   {
      int64_t ram = 0;
      int64_t net = 0;
      int64_t cpu = 0;
      asset liquid;
   };

   account_info get_account_info(account_name acc)
   {
      account_info info;
      control->get_resource_limits_manager().get_account_limits(acc, info.ram, info.net, info.cpu);
      info.liquid = get_balance(acc);
      return info;
   };

   void check_rentbw(const name &payer, const name &receiver, uint32_t days, int64_t net_frac, int64_t cpu_frac,
                     const asset &expected_fee, int64_t expected_net, int64_t expected_cpu)
   {
      auto before_payer = get_account_info(payer);
      auto before_receiver = get_account_info(receiver);
      auto before_reserve = get_account_info(N(eosio.reserv));
      auto before_state = get_state();
      BOOST_REQUIRE_EQUAL("", rentbw(payer, receiver, days, net_frac, cpu_frac, asset::from_string("300000.0000 TST")));
      auto after_payer = get_account_info(payer);
      auto after_receiver = get_account_info(receiver);
      auto after_reserve = get_account_info(N(eosio.reserv));
      auto after_state = get_state();

      if (GENERATE_CSV)
      {
         ilog("before_state.net.assumed_stake_weight:    ${x}", ("x", before_state.net.assumed_stake_weight));
         ilog("before_state.net.weight_ratio:            ${x}",
              ("x", before_state.net.weight_ratio / double(rentbw_frac)));
         ilog("before_state.net.assumed_stake_weight:    ${x}", ("x", before_state.net.assumed_stake_weight));
         ilog("before_state.net.weight:                  ${x}", ("x", before_state.net.weight));

         ilog("before_receiver.net:                      ${x}", ("x", before_receiver.net));
         ilog("after_receiver.net:                       ${x}", ("x", after_receiver.net));
         ilog("after_receiver.net - before_receiver.net: ${x}", ("x", after_receiver.net - before_receiver.net));
         ilog("expected_net:                             ${x}", ("x", expected_net));
         ilog("before_payer.liquid - after_payer.liquid: ${x}", ("x", before_payer.liquid - after_payer.liquid));
         ilog("expected_fee:                             ${x}", ("x", expected_fee));

         ilog("before_reserve.net:                       ${x}", ("x", before_reserve.net));
         ilog("after_reserve.net:                        ${x}", ("x", after_reserve.net));
         ilog("before_reserve.cpu:                       ${x}", ("x", before_reserve.cpu));
         ilog("after_reserve.cpu:                        ${x}", ("x", after_reserve.cpu));

         csv.newRow() << last_block_time()
                      << before_state.net.assumed_stake_weight
                      << before_state.net.weight_ratio / double(rentbw_frac)
                      << before_state.net.weight
                      << before_receiver.net
                      << after_receiver.net
                      << after_receiver.net - before_receiver.net
                      << before_payer.liquid - after_payer.liquid
                      << before_reserve.net
                      << after_reserve.net
                      << before_reserve.cpu
                      << after_reserve.cpu

                      << get_state().cpu.weight
                      << get_state().cpu.weight_ratio
                      << get_state().cpu.assumed_stake_weight
                      << get_state().cpu.initial_weight_ratio
                      << get_state().cpu.target_weight_ratio
                      << get_state().cpu.initial_timestamp.sec_since_epoch()
                      << get_state().cpu.target_timestamp.sec_since_epoch()
                      << get_state().cpu.exponent
                      << get_state().cpu.decay_secs
                      << get_state().cpu.min_price.to_string()
                      << get_state().cpu.max_price.to_string()
                      << get_state().cpu.utilization
                      << get_state().cpu.adjusted_utilization
                      << get_state().cpu.utilization_timestamp.sec_since_epoch();
      }

      if (payer != receiver)
      {
         BOOST_REQUIRE_EQUAL(before_payer.ram, after_payer.ram);
         BOOST_REQUIRE_EQUAL(before_payer.net, after_payer.net);
         BOOST_REQUIRE_EQUAL(before_payer.cpu, after_payer.cpu);
         BOOST_REQUIRE_EQUAL(before_receiver.liquid, after_receiver.liquid);
      }
      BOOST_REQUIRE_EQUAL(before_receiver.ram, after_receiver.ram);
      BOOST_REQUIRE_EQUAL(after_receiver.net - before_receiver.net, expected_net);
      BOOST_REQUIRE_EQUAL(after_receiver.cpu - before_receiver.cpu, expected_cpu);
      //   BOOST_REQUIRE_EQUAL(before_payer.liquid - after_payer.liquid, expected_fee);

      BOOST_REQUIRE_EQUAL(before_reserve.net - after_reserve.net, expected_net);
      BOOST_REQUIRE_EQUAL(before_reserve.cpu - after_reserve.cpu, expected_cpu);
      BOOST_REQUIRE_EQUAL(after_state.net.utilization - before_state.net.utilization, expected_net);
      BOOST_REQUIRE_EQUAL(after_state.cpu.utilization - before_state.cpu.utilization, expected_cpu);
   }
};

template <typename A, typename B, typename D>
bool near(A a, B b, D delta)
{
   if (abs(a - b) <= delta)
      return true;
   elog("near: ${a} ${b}", ("a", a)("b", b));
   return false;
}

BOOST_AUTO_TEST_SUITE(eosio_system_rentbw_tests)

BOOST_FIXTURE_TEST_CASE(model_tests, rentbw_tester)
try
{
   produce_block();

   BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
                          config.net.current_weight_ratio = rentbw_frac;
                          config.net.target_weight_ratio = rentbw_frac;
                          config.net.assumed_stake_weight = stake_weight;
                          config.net.exponent = 1;
                          config.net.min_price = asset::from_string("1000000.0000 TST");
                          config.net.max_price = asset::from_string("1000000.0000 TST");

                          config.cpu.current_weight_ratio = rentbw_frac;
                          config.cpu.target_weight_ratio = rentbw_frac;
                          config.cpu.assumed_stake_weight = stake_weight;
                          config.cpu.exponent = 1;
                          config.cpu.min_price = asset::from_string("1000000.0000 TST");
                          config.cpu.max_price = asset::from_string("1000000.0000 TST");

                          config.rent_days = 30;
                          config.min_rent_fee = asset::from_string("1.0000 TST");
                       })));

   BOOST_REQUIRE_EQUAL("", configbw(make_default_config([&](auto &config) {
                          // weight = stake_weight
                          config.net.current_weight_ratio = rentbw_frac / 2;
                          config.net.target_weight_ratio = rentbw_frac / 2;

                          // weight = stake_weight
                          config.cpu.current_weight_ratio = rentbw_frac / 2;
                          config.cpu.target_weight_ratio = rentbw_frac / 2;
                       })));

   auto net_weight = stake_weight;
   auto cpu_weight = stake_weight;

   start_rex();
   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("1.0000"),
                                 false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));

   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("5000000.0000"));

   // 10%, 20%
   for (int i = 0; i < 3; i++)
   {
      check_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, rentbw_frac * .1, rentbw_frac * .2,
                   asset::from_string("300000.0000 TST"), net_weight * .1, cpu_weight * .2);
      produce_block(fc::days(10) - fc::milliseconds(500));
   }

   produce_block(fc::days(60) - fc::milliseconds(500));
   BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 10));

   // 2%, 2%
   for (int m = 0; m < 6; m++)
   {
      // rent every day during 15 days
      for (int j = 1; j < 15; j++)
      {
         check_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, rentbw_frac * .02, rentbw_frac * .02,
                      asset::from_string("40000.0000 TST"), net_weight * .02, cpu_weight * .02);
         produce_block(fc::days(1) - fc::milliseconds(500));
      }
      produce_block(fc::days(30) - fc::milliseconds(500));
      BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
   }   
}
FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
