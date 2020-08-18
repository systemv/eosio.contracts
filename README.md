# rentbw modeling tool

![](./utilization.png)

## building the tool
### script version
In the home directory

[./build.sh](./build.sh) -e ~/eosio/2.0 -c ~/eosio/2.0/eosio.cdt -t

### manual build steps
make sure you have EOSIO and EOSIO.cdt in your path

mkdir build

cd build

cmake -DBUILD_TESTS=true ..

make -j

## running the test/scenarios

./tests/unit_test -t eosio_system_rentbw_tests/model_tests

or just

./tests/unit_test

to use gnuplot to plot the results from project directory:

sh [./plot.sh](plot.sh)
