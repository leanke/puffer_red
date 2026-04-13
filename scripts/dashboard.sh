#!/bin/bash
# Move cursor to top-left and overwrite instead of clearing (no flicker)
tput civis  # hide cursor
trap 'tput cnorm; exit' INT TERM
while true; do
  printf '\033[H'
  cat /tmp/puffer_dashboard.txt 2>/dev/null
  printf '\033[J'  # clear any leftover lines below
  sleep 0.50
done
