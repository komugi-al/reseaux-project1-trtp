#!/usr/bin/env python3

import pandas
import os

if not os.path.exists('graphes'):
    os.makedirs('graphes')

types = ['all']

for i,t in enumerate(types):
    senderCsv = pandas.read_csv("csv/csv-sender-"+t+".csv")
    receiverCsv = pandas.read_csv("csv/csv-receiver-"+t+".csv")
    maxVal = 0
    meanSender = senderCsv.groupby(["key"]).mean()
    meanReceiver = receiverCsv.groupby(["key"]).mean()
    stdSender = senderCsv.groupby(["key"]).std()
    stdReceiver = receiverCsv.groupby(["key"]).std()
    meanSender.to_csv ('csv/meanSender'+t+'.csv', index = True, header=False)
    meanReceiver.to_csv ('csv/meanReceiver'+t+'.csv', index = True, header=False)
    stdSender.to_csv ('csv/stdSender'+t+'.csv', index = True, header=False)
    stdReceiver.to_csv ('csv/stdReceiver'+t+'.csv', index = True, header=False)

