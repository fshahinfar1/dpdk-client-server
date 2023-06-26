#! /bin/bash

FILE=$1
START=$(grep -n -e "----" $FILE  | cut -d ":" -f 1 | head -n 1)
START=$((START + 1))
END=$(grep -n -e "----" $FILE  | cut -d ":" -f 1 | tail -n 1)
COUNT=$(echo "$END - $START" | bc)

DATA=( $(cat $FILE | tail -n +$START | head -n $COUNT | sort -n) )

SUM=0
for x in ${DATA[@]}; do
  SUM=$(( SUM + x ))
done
MEAN=$(( SUM / COUNT ))

MEDIAN=$(( COUNT / 2 ))
NN=$(( COUNT * 99 / 100 ))


echo "Median: ${DATA[$MEDIAN]}"
echo "Mean: $MEAN"
echo "99: ${DATA[$NN]}"
