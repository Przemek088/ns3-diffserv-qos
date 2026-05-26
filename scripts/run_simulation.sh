#!/bin/bash

step=40
total=1000 # Sum of queue lengths
min_value=40

: >results

for ef in $(seq $min_value $step $((total - 2 * min_value))); do
    for af in $(seq $min_value $step $((total - ef - min_value))); do
        be=$((total - ef - af))
        echo "Running with EF=$ef, AF=$af, BE=$be"
        ./ns3 run "scratch/network --ef=$ef --af=$af --be=$be" >/dev/null
    done
done

echo "All simulations completed."
