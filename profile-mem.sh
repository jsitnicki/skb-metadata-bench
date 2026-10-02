#!/bin/bash
perf stat -C 12-17 -e cycles,instructions,LLC-loads,LLC-load-misses -- sleep 10
perf stat -C 12-17 -e LLC-stores,LLC-store-misses,L1-dcache-loads -- sleep 10
