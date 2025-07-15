#!/bin/bash

echo "" > tempo.txt

for i in 3 5 7; do
  for j in 3 5 7; do
    for k in 1 2 3 4; do
      echo "===== Execução com parâmetros: $i $j $k =====" >> tempo.txt
      { time ./tfso-Pthreads borboleta.bmp cinza.bmp med${i}${j}${k}.bmp lap${i}${j}${k}.bmp $i $j $k > /dev/null ; } 2>> tempo.txt
      echo "" >> tempo.txt
    done
  done
done
