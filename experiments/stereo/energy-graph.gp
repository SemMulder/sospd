set terminal pdfcairo
set out "REPLACE_FILE-energy.pdf"
set xlabel "Time (seconds)"
set ylabel "Energy"
set xrange [0:60]

plot  \
    "results/REPLACE_FILE-reduction-1.stats" using 3:5 title "FGBZ-Fusion" with linespoints, \
    "results/REPLACE_FILE-hocr-1.stats" using 3:5 title "HOCR-Fusion" with linespoints, \
    "results/REPLACE_FILE-spd-alpha-0.stats" using 3:5 title "SoSPD-Fusion UB" with linespoints, \
    "results/REPLACE_FILE-spd-alpha-height-0.stats" using 3:5 title "SoSPD-Best-Fusion UB" with linespoints, \
    "results/REPLACE_FILE-spd-alpha-1.stats" using 3:5 title "SoSPD-Fusion" with linespoints, \
    "results/REPLACE_FILE-spd-alpha-height-1.stats" using 3:5 title "SoSPD-Best-Fusion" with linespoints
