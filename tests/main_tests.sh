#!/bin/bash

rm -rf logs/*

test1() {
counter=1
	while [ $counter -le 10 ]
	do
		echo "Test $counter:"
		./tests/simple_test.sh 1024 0 0 0
		((counter++))
	done
}

test2() {
	echo "packets_retransmitted" > only_loss.csv
	rate=10
	delay=200
	echo "Testing $rate% $delay ms"

	counter=1
	echo "key, value" > csv/csv-sender-loss.csv
	echo "key, value" > csv/csv-receiver-loss.csv
	while [ $counter -le 10 ]
	do
		echo "Test $counter for $rate% loss"
		./tests/simple_test.sh 79200 $rate 0 0
		grep -i "packets_retransmitted" sender.log | cut -d ',' -f 2 >> only_loss.csv
		cat sender.csv >> csv/csv-sender-loss.csv
		cat receiver.csv >> csv/csv-receiver-loss.csv
		mv sender.log logs/sender-loss-$counter.log
		mv receiver.log logs/receiver-loss-$counter.log
		((counter++))
	done

	counter=1
	echo "key, value" > csv/csv-sender-cut.csv
	echo "key, value" > csv/csv-receiver-cut.csv
	while [ $counter -le 10 ]
	do
		echo "Test $counter for $delay ms delay"
		./tests/simple_test.sh 79200 0 $delay 0
		grep -i "packets_retransmitted" sender.log | cut -d ',' -f 2 >> only_loss.csv
		cat sender.csv >> csv/csv-sender-cut.csv
		cat receiver.csv >> csv/csv-receiver-cut.csv
		mv sender.log logs/sender-cut-$counter.log
		mv receiver.log logs/receiver-cut-$counter.log
		((counter++))
	done

	counter=1
	echo "key, value" > csv/csv-sender-err.csv
	echo "key, value" > csv/csv-receiver-err.csv
	while [ $counter -le 10 ]
	do
		echo "Test $counter for $rate% errors"
		./tests/simple_test.sh 79200 0 0 $rate
		grep -i "packets_retransmitted" sender.log | cut -d ',' -f 2 >> only_loss.csv
		cat sender.csv >> csv/csv-sender-err.csv
		cat receiver.csv >> csv/csv-receiver-err.csv
		mv sender.log logs/sender-err-$counter.log
		mv receiver.log logs/receiver-err-$counter.log
		((counter++))
	done

	counter=1
	echo "key, value" > csv/csv-sender-all.csv
	echo "key, value" > csv/csv-receiver-all.csv
	while [ $counter -le 10 ]
	do
		echo "Test $counter for $rate% all and delay $delay"
		./tests/simple_test.sh 79200 $rate $delay $rate
		grep -i "packets_retransmitted" sender.log | cut -d ',' -f 2 >> only_loss.csv
		cat sender.csv >> csv/csv-sender-all.csv
		cat receiver.csv >> csv/csv-receiver-all.csv
		mv sender.log logs/sender-all-$counter.log
		mv receiver.log logs/receiver-all-$counter.log
		((counter++))
	done

}

test2
