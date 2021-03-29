#!/bin/bash

rm -rf logs/*

test1() {
counter=1
	while [ $counter -le 10 ]
	do
		echo "Test $counter:"
		./tests/simple_test.sh 1024 0 0
		((counter++))
	done
}

test2() {
	echo "packets_retransmitted" > only_loss.csv
	losses='30'
	for loss in $losses
	do
		echo "Testing $loss%"
		counter=1
		while [ $counter -le 1 ]
		do
			echo "Test $counter for $loss% loss"
			./tests/simple_test.sh 150000 $loss 0
			grep -i "packets_retransmitted" sender.log | cut -d ',' -f 2 >> only_loss.csv
			mv sender.log logs/sender-$loss-$counter.log
			mv receiver.log logs/receiver-$loss-$counter.log
			((counter++))
		done
	done
}

test2
