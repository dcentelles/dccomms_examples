#!/bin/bash

scriptName=$(basename $0)
echo $scriptName
pid=$$

basedir=$(readlink -f $1)
echo "base dir: $basedir"

plotdir=$basedir/plots

rm -rf $plotdir

dataplotdir=$plotdir/dataplot

mkdir -p $dataplotdir

throughputdir=$dataplotdir/throughput
jittersddir=$dataplotdir/jittersd
jitteravgdir=$dataplotdir/jitteravg
end2endsddir=$dataplotdir/end2endsd
end2endavgdir=$dataplotdir/end2endavg

mkdir -p $throughputdir
mkdir -p $jittersddir
mkdir -p $jitteravgdir
mkdir -p $end2endsddir
mkdir -p $end2endavgdir

minvalue=-999999999
maxvalue=999999999

maxjitter=$minvalue
minjitter=$maxvalue

maxjitterSd=$minvalue
minjitterSd=$maxvalue

maxend2end=$minvalue
minend2end=$maxvalue

maxend2endSd=$minvalue
minend2endSd=$maxvalue

maxthroughput=$minvalue
minthroughput=$maxvalue

for ratedir in $basedir/*
do
	if [ ! -d $ratedir ]
	then
		continue
	fi
	apprate=$(basename $ratedir)
	for pktsizedir in $ratedir/*
	do
		if [ ! -d $pktsizedir ]
		then
			continue
		fi

		pktsize=$(basename $pktsizedir)
		for protodir in $pktsizedir/*
		do
			if [ ! -d $protodir ]
			then
				continue
			fi
			for flowdir in $protodir/results/*.log
			do

				if [ ! -d $flowdir ]
				then
					continue
				fi

				resdir=$flowdir
				node=${flowdir%%.*}; node=${node%%_s100}; node=${node##/*/}
				proto=$(basename $protodir)
				echo $apprate - $pktsize - $proto - $node

				#another way to get apprate and pktsize:
				#apprate=$(cat $resdir/notes.txt | cut -f1 -d' ')
				#pktsize=$(cat $resdir/notes.txt | cut -f2 -d' ')

				#### END 2 END ####
				end2endAvg=$(cat $resdir/end2end | cut -f 1 -d$'\t')
				end2endSd=$(cat $resdir/end2end | cut -f 2 -d$'\t')

				if [[ $(echo $end2endAvg'<'$minend2end | bc -l) -eq 1 ]]
				then
					minend2end=$end2endAvg 
				fi
				if [[ $(echo $end2endAvg'>'$maxend2end | bc -l) -eq 1 ]]
				then
					maxend2end=$end2endAvg 
				fi
					if [[ $(echo $end2endSd'<'$minend2endSd | bc -l) -eq 1 ]]
				then
					minend2endSd=$end2endSd 
				fi
				if [[ $(echo $end2endSd'>'$maxend2endSd | bc -l) -eq 1 ]]
				then
					maxend2endSd=$end2endSd 
				fi
				echo -e "$proto\t$apprate\t$pktsize\t$end2endSd" >> $end2endsddir/${node}.dat
				echo -e "$proto\t$apprate\t$pktsize\t$end2endAvg" >> $end2endavgdir/${node}.dat

				#### JITTER ####
				jitterAvg=$(cat $resdir/jitter | cut -f 1 -d$'\t')
				jitterSd=$(cat $resdir/jitter | cut -f 2 -d$'\t')

				if [[ $(echo $jitterAvg'<'$minjitter | bc -l) -eq 1 ]]
				then
					minjitter=$jitterAvg 
				fi
				
				if [[ $(echo $jitterAvg'>'$maxjitter | bc -l) -eq 1 ]]
				then
					maxjitter=$jitterAvg 
				fi
					if [[ $(echo $jitterSd'<'$minjitterSd | bc -l) -eq 1 ]]
				then
					minjitterSd=$jitterSd 
				fi
				
				if [[ $(echo $jitterSd'>'$maxjitterSd | bc -l) -eq 1 ]]
				then
					maxjitterSd=$jitterSd 
				fi

				echo -e "$proto\t$apprate\t$pktsize\t$jitterAvg" >> $jitteravgdir/${node}.dat
				echo -e "$proto\t$apprate\t$pktsize\t$jitterSd" >> $jittersddir/${node}.dat
				#### THROUGHPUT ####
				throughput=$(cat $resdir/throughput)
					
				if [[ $(echo $throughput'<'$minthroughput | bc -l) -eq 1 ]]
				then
					minthroughput=$throughput
				fi

				if [[ $(echo $throughput'>'$maxthroughput | bc -l) -eq 1 ]]
				then
					maxthroughput=$throughput 
				fi

				echo -e "$proto\t$apprate\t$pktsize\t$throughput" >> $throughputdir/${node}.dat
			done
		done
	done
done

