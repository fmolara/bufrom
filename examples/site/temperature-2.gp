set encoding utf8
set terminal svg size 1000,360 dynamic noenhanced
set output 'temperature-2.svg'
set title "Temperatura dell'aria"
set xlabel 'Data e ora UTC'
set ylabel 'Temperatura (°C)'
set xdata time
set timefmt '%s'
set format x "%d/%m\n%H:%M"
set grid
unset key
set xrange [1281828600:1281832200]
set yrange [21.949990000000014:22.949990000000014]
plot 'temperature-2.dat' using 1:2 with points pointtype 7 pointsize 0.8 linecolor rgb '#147d92'
