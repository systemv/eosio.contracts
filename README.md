# rentbw modeling tool

![](./utilization.png)

mkdir build

cd build

cmake -DBUILD_TESTS=true ..

make -j

./tests/unit_test -t eosio_system_rentbw_tests/model_tests

or just

./tests/unit_test

to use gnuplot to plot the results from project directory:

sh plot.sh
