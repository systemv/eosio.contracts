# rentbw modeling tool

![](./utilization.png)

eosio branch: release/2.0.x
eosio.cdt branch: eosio-cdt-2.1-staging-c

mkdir build

cd build

cmake -DBUILD_TESTS=true ..

make -j

from the project folder execute:

sh run_and_plot.sh model_config.json rentbw_input.csv csv_output.csv

