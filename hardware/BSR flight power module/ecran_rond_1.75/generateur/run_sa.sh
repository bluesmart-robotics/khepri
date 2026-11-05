#!/bin/bash
# 2 cœurs : deux recuits à la fois
cd /home/claude/work/gen_ecran
for pair in "141 142" "143 144"; do
  for s in $pair; do python3 placer.py /home/claude/work/out/ecran_rond_1.75/design_export.json $s 500000 > /home/claude/work/out/placer_$s.log 2>&1 & done
  wait
done
for s in 141 142 143 144; do echo "== seed $s"; grep -h "seed\|chevauch\|hors" /home/claude/work/out/placer_$s.log; python3 pcbgen.py /home/claude/work/out/ecran_rond_1.75 /home/claude/work/out/placement_big_$s.json 2>&1 | grep -v "^PCB" | tr '\n' ' '; echo; done
