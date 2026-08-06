#!/bin/bash
cd "$(dirname "$0")/../ns-allinone-3.40/ns-3.40"
SEEDS="1 2 3 4 5 6 7 8 9 10"

for sim in fat-tree spine-leaf; do
  for r in ecmp spray; do
    vals=""
    for s in $SEEDS; do
      t=$(python3 ns3 run "${sim}-simulation --routing=$r --seed=$s" 2>&1 \
          | grep -E "TCP" | grep -E ":50[0-9][0-9] " \
          | awk '{sum+=$(NF-1); n++} END{if(n)printf "%.1f", sum/n; else print "NA"}')
      vals="$vals $t"
    done
    echo "$sim $r:$vals" \
      | awk '{printf "%s %s:", $1,$2;
              m=0;c=0;for(i=3;i<=NF;i++){m+=$i;c++} m/=c;
              v=0;for(i=3;i<=NF;i++)v+=($i-m)^2; v=sqrt(v/c);
              printf "  mean=%.1f  std=%.1f\n", m, v}'
  done
done
