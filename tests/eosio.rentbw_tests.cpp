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
#include <time.h>

#include "eosio.system_tester.hpp"
#include "csvwriter.hpp"
#include "csv.h"

#define PICOJSON_USE_INT64

#include "picojson.hpp"

#define GENERATE_CSV true
#define CSV_FILENAME "model_tests.csv"
#define CFG_FILENAME "_model_config.json"
#define INP_FILENAME "_rentbw_input.csv"

inline constexpr int64_t rentbw_frac = 1'000'000'000'000'000ll; // 1.0 = 10^15
inline constexpr int64_t stake_weight = 100'000'000'0000ll;     // 10^12

inline constexpr int64_t endstate_weight_ratio = 1'000'000'000'000'0ll; // 0.1 = 10^13

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
   int64_t fee;
};
FC_REFLECT(rentbw_state_resource,                                                                           //
           (version)(weight)(weight_ratio)(assumed_stake_weight)(initial_weight_ratio)(target_weight_ratio) //
           (initial_timestamp)(target_timestamp)(exponent)(decay_secs)(min_price)(max_price)(utilization)   //
           (adjusted_utilization)(utilization_timestamp)(fee))

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
   int64_t  timeOffset = 0; 
 
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
                         << "before_reserve.net"
                         << "after_reserve.net"
                         << "before_reserve.cpu"
                         << "after_reserve.cpu"
                         << "net.frac"
                         << "net.delta"
                         << "cpu.frac"
                         << "cpu.delta"
                         << "fee"
                         << "net.weight"
                         << "net.weight_ratio"
                         << "net.assumed_stake_weight"
                         << "net.initial_weight_ratio"
                         << "net.target_weight_ratio"
                         << "net.initial_timestamp"
                         << "net.target_timestamp"
                         << "net.exponent"
                         << "net.decay_secs"
                         << "net.min_price"
                         << "net.max_price"
                         << "net.utilization"
                         << "net.adjusted_utilization"
                         << "net.utilization_timestamp"
                         << "cpu.weight"
                         << "cpu.weight_ratio"
                         << "cpu.assumed_stake_weight"
                         << "cpu.initial_weight_ratio"
                         << "cpu.target_weight_ratio"
                         << "cpu.initial_timestamp"
                         << "cpu.target_timestamp"
                         << "cpu.exponent"
                         << "cpu.decay_secs"
                         << "cpu.min_price"
                         << "cpu.max_price"
                         << "cpu.utilization"
                         << "cpu.adjusted_utilization"
                         << "cpu.utilization_timestamp"
                         << "function";


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

   template <typename F>
   rentbw_config make_config_from_file(const string &fname, F g)
   {
      rentbw_config config;

      stringstream ss;
      ifstream f;
      unsigned int i;

      // Read Json file
      f.open(fname, ios::binary);
      if (!f.is_open())
      {
         ilog("Unable to find model configuration file, using default");
         return make_config(g);
      }
      ss << f.rdbuf();
      f.close();

      // Parse Json data
      picojson::value v;
      ss >> v;
      string err = picojson::get_last_error();
      if (!err.empty())
      {
         cerr << err << endl;
      }

      picojson::object &o = v.get<picojson::object>()["cpu"].get<picojson::object>();

      config.net.current_weight_ratio = v.get("net").get("current_weight_ratio").get<int64_t>();
      config.net.target_weight_ratio = v.get("net").get("target_weight_ratio").get<int64_t>();
      config.net.assumed_stake_weight = v.get("net").get("assumed_stake_weight").get<int64_t>();
      config.net.target_timestamp = control->head_block_time() + fc::days(v.get("net").get("target_timestamp").get<int64_t>());
      config.net.exponent = v.get("net").get("exponent").get<int64_t>();
      config.net.decay_secs = v.get("net").get("decay_secs").get<int64_t>();
      config.net.min_price = asset::from_string(v.get("net").get("min_price").get<string>());
      config.net.max_price = asset::from_string(v.get("net").get("max_price").get<string>());
      config.cpu.current_weight_ratio = v.get("cpu").get("current_weight_ratio").get<int64_t>();
      config.cpu.target_weight_ratio = v.get("cpu").get("target_weight_ratio").get<int64_t>();
      
      config.cpu.assumed_stake_weight = v.get("cpu").get("assumed_stake_weight").get<int64_t>();
      config.cpu.target_timestamp = control->head_block_time() + fc::days(v.get("cpu").get("target_timestamp").get<int64_t>());
      config.cpu.exponent = v.get("cpu").get("exponent").get<int64_t>();
      config.cpu.decay_secs = v.get("cpu").get("decay_secs").get<int64_t>();
      config.cpu.min_price = asset::from_string(v.get("cpu").get("min_price").get<string>());
      config.cpu.max_price = asset::from_string(v.get("cpu").get("max_price").get<string>());

      config.rent_days = v.get("rent_days").get<int64_t>();
      config.min_rent_fee = asset::from_string(v.get("min_rent_fee").get<string>());

      g(config);
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
                     const asset &expected_fee, int64_t expected_net, int64_t expected_cpu, uint16_t max = 0)
   {
      auto before_payer = get_account_info(payer);
      auto before_receiver = get_account_info(receiver);
      auto before_reserve = get_account_info(N(eosio.reserv));
      auto before_state = get_state();
      try {
         if (max > 0) {
            rentbwexec(config::system_account_name, max);
         }
         else {
            rentbw(payer, receiver, days, net_frac, cpu_frac, expected_fee);
         }
      }
      catch (const fc::exception& ex)
      {
         edump((ex.to_detail_string()));
         return;
      }
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

         csv.newRow() << last_block_time() - timeOffset            
                      << before_state.net.assumed_stake_weight
                      << before_state.net.weight_ratio / double(rentbw_frac)
                      << before_state.net.weight
                      << float(before_reserve.net / 10000.0)
                      << float(after_reserve.net / 10000.0)
                      << float(before_reserve.cpu / 10000.0)
                      << float(after_reserve.cpu / 10000.0)
                      << net_frac
                      << float((after_receiver.net - before_receiver.net) / 10000.0)
                      << cpu_frac
                      << float((after_receiver.cpu - before_receiver.cpu) / 10000.0)
                      << float((before_payer.liquid - after_payer.liquid).get_amount() / 10000.0) 
                      << after_state.net.weight
                      << after_state.net.weight_ratio
                      << after_state.net.assumed_stake_weight
                      << after_state.net.initial_weight_ratio
                      << after_state.net.target_weight_ratio
                      << after_state.net.initial_timestamp.sec_since_epoch()
                      << after_state.net.target_timestamp.sec_since_epoch()
                      << after_state.net.exponent
                      << after_state.net.decay_secs
                      << after_state.net.min_price.to_string()
                      << after_state.net.max_price.to_string()
                      << after_state.net.utilization
                      << after_state.net.adjusted_utilization
                      << after_state.net.utilization_timestamp.sec_since_epoch()
                      << after_state.cpu.weight
                      << after_state.cpu.weight_ratio
                      << after_state.cpu.assumed_stake_weight
                      << after_state.cpu.initial_weight_ratio
                      << after_state.cpu.target_weight_ratio
                      << after_state.cpu.initial_timestamp.sec_since_epoch()
                      << after_state.cpu.target_timestamp.sec_since_epoch()
                      << after_state.cpu.exponent
                      << after_state.cpu.decay_secs
                      << after_state.cpu.min_price.to_string()
                      << after_state.cpu.max_price.to_string()
                      << after_state.cpu.utilization
                      << after_state.cpu.adjusted_utilization
                      << after_state.cpu.utilization_timestamp.sec_since_epoch()
                      << (max > 0 ? "rentbwexec" : "rentbw");
      }

      if (payer != receiver)
      {
         BOOST_REQUIRE_EQUAL(before_payer.ram, after_payer.ram);
         BOOST_REQUIRE_EQUAL(before_payer.net, after_payer.net);
         BOOST_REQUIRE_EQUAL(before_payer.cpu, after_payer.cpu);
         BOOST_REQUIRE_EQUAL(before_receiver.liquid, after_receiver.liquid);
      }
   }

   void produce_blocks_date(const char *str, std::string function)
   {
      static std::chrono::system_clock::time_point cursor = std::chrono::system_clock::from_time_t(last_block_time());


      std::tm tm = {};
      ::strptime(str, "%m/%d/%Y %H:%M:%S", &tm);
      time_t  ttp = std::mktime(&tm);
         
      auto utc_field = *std::gmtime(&ttp);
      time_t  ttpc = std::mktime(&utc_field);
      time_t  timezone = ttpc - ttp;

      // csv time in gmt
      time_t  csvtime = ttp - timezone;

      // if first run - calculate offset
      if (timeOffset == 0) {
         timeOffset = last_block_time() - csvtime; // account for blocktime
         cursor     = std::chrono::system_clock::from_time_t(last_block_time() - timeOffset);
      } 

      auto tp = std::chrono::system_clock::from_time_t(ttp - timezone);
      auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp - cursor).count();

      //cout << "Time difference: " << std::chrono::system_clock::to_time_t(cursor) << " -- " << std::chrono::system_clock::to_time_t(tp);

      cursor = tp;
    
      if (time_diff > 500)
      {
         produce_block(fc::milliseconds(time_diff) - fc::milliseconds(500));
      }
      else if (function == "rentbw") {
         produce_block();
      }      
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

   BOOST_REQUIRE_EQUAL("", configbw(make_config_from_file(CFG_FILENAME, [&](auto &config) {

                       })));

   auto net_weight = stake_weight;
   auto cpu_weight = stake_weight;

   start_rex();
   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("1.0000"),
                                 false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));

   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("5000000.0000"));

   io::CSVReader<9> in(INP_FILENAME);
   in.read_header(io::ignore_extra_column, "datetime", "function", "payer", "receiver", "days", "net_frac", "cpu_frac", "max_payment", "queue_max");

   std::string datetime, function, payer, receiver;
   uint32_t days;
   int64_t net_frac, cpu_frac;
   std::string max_payment;
   uint16_t queue_max;

   std::chrono::system_clock::time_point cursor;
   bool first_timepoint = true;

   while (in.read_row(datetime, function, payer, receiver, days, net_frac, cpu_frac, max_payment, queue_max))
   {
      if (function == "rentbwexec" || function == "rentbw")
      {
         produce_blocks_date(datetime.c_str(), function);

         account_name payer_name = string_to_name(payer);
         account_name receiver_name = string_to_name(receiver);

         check_rentbw(payer_name, receiver_name,
                      days, net_frac, cpu_frac, asset::from_string(max_payment + " TST"), 0, 0, function == "rentbwexec" ? queue_max : 0);
      }
      else
      {
         //
      }
      
   }
}
FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
