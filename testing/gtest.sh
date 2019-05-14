#!/bin/bash

#./gtest.sh
#	 test0
#	 20
#	 "aloha sfama dcmac"
#	 "20 50"
#	 "200 300"

scriptName=$(basename $0)
echo $scriptName
pid=$$

rbasedir=$1
npkts=$2
protos=$3
sizes=$4
datarates=$5
propSpeed=$6

if [ ! -d $rbasedir ]
then
	echo "creating $rbasedir"
	mkdir -p $rbasedir
fi
basedir=$(readlink -f $rbasedir)
launchdir=$(pwd)

stopntp="timedatectl set-ntp 0"
echo $stopntp
eval $stopntp
sleep 2s

for rate in $datarates
do
	rateDir=$basedir/$rate
	mkdir -p $rateDir
	echo "Current app. data rate: $rate bps"
	for size in $sizes
	do
		pktSize=$size
		echo "Current packet size: $pktSize bytes"
		pktsizedir=$rateDir/$pktSize
		mkdir -p $pktsizedir
		for proto in $protos
		do
			echo "Current protocol: $proto"
			protodir=$pktsizedir/$proto
			mkdir -p $protodir
			secs=$(echo "($npkts*$pktSize*8)/$rate + 120" | bc -l)
			echo "sim time: $secs"
			./mac_performance.sh $rate $pktSize $npkts $secs $proto $propSpeed $protodir
		done
	done
done
