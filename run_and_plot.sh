
if [ $# -eq 0 ]; then
    echo "Usage: run_and_plot model_config.json rentbw_input.csv csv_output.csv"
    exit 1
fi

cp $1 _model_config.json
cp $2 _rentbw_input.csv

build/tests/unit_test -t eosio_system_rentbw_tests/model_tests

cp model_tests.csv $3 

echo "
  set datafile separator ';'
  set style line 1 lt 1 lc rgb '#A00000' lw 2 pt 7 ps 1
  set style line 2 lt 1 lc rgb '#00A000' lw 2 pt 11 ps 1
  
set style line 81 lt 0 lc rgb '#808080' lw 0.5

# Draw the grid lines for both the major and minor tics
set grid xtics
set grid ytics
set grid mxtics
set grid mytics

# Put the grid behind anything drawn and use the linestyle 81
set grid back ls 81

  set key autotitle columnhead
  plot '$3' using 1:24 w lp ls 1, '' using 1:25 w lp ls 2
" | gnuplot --persist
