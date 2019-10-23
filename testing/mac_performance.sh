#!/bin/bash

scriptName=$(basename $0)
scriptPath=$(realpath $0)
echo $scriptName
pid=$$

datarate=$1
size=$2
npkts=$3
testduration=$4
protocol=$5
propSpeed=$6
devDelay=2
maxRange=100

echo "prop speed arg: $propSpeed"
if [ "$propSpeed" == "ac" ]
then
	propSpeed=1500
	uwsimPropSpeed="0.666"
else
	propSpeed=300000000
	uwsimPropSpeed="3.333333333e-06"
fi

echo "Data rate: $datarate"
echo "Pkt size: $size"
echo "npkts: $npkts"
echo "testduration: $testduration"
echo "protocol: $protocol"
echo "prop speed: $propSpeed"

basedir=$(realpath ${7}/)
debug=$8
scene=$9
maxBackoffSlots=${10}

echo "BASEDIR: $basedir"
bindir="../build/" #TODO: as argument
resultsdir=$basedir/results
rawlogdir=$basedir/rawlog

rm -rf $resultsdir $rawlogdir
if [ "$debug" == "debug" ]
then
	echo "debug"
else
if [ "$debug" == "nodebug" ]
then
	echo "no debug"
fi
fi

rm -rf /dev/mqueue/*
mkdir -p $resultsdir
mkdir -p $rawlogdir


kill -9 $(ps aux | grep "bash .*$scriptName" | awk -v mpid=$pid '{ if(mpid != $2) print $2}') > /dev/null 2>&1

###################################################
###################################################

awktime='
BEGIN{\
	"cat \""dateref"\" | date -u -f - +%s" | getline t0
}
{\
	where = match($0, "[0-9]+\\/[0-9]+\\/[0-9]+ [0-9]+:[0-9]+:[0-9]+\\.[0-9]+")
	if(where != 0)
	{
		time=substr($0, RSTART, RLENGTH)
        where = match($0, patt)
	    if(where != 0)
	    {
		    f = "date -u -d \""time"\" +%s" 
		    f | getline seconds
		    close(f)
		    f = "date -u -d \""time"\" +%N"
		    f | getline nanos
		    close(f)
		    seconds = (seconds - t0)
		    tt = seconds + nanos / 1e9
		    f = "date -u -d \""time"\" +%s.%9N"
		    f | getline tt2
		    close(f)
		    printf("%.9f\t%d\t%d\n", tt, $7, $9)
        }
	} 
}
END{\
}
'

awkgap='
BEGIN{\
	samples = 0
	sum = 0
	sum2 = 0
}
{\
	cont += 1
	dif = $2 - old2
	if(dif==0) 
		dif = 1
	if(dif > 0) 
	{ 
		if(cont > 1) 
		{
			gap = ($1 - old1) / dif
			printf("%d\t%.9f\n", $2, gap)
			sum += gap
			sum2 += gap * gap
			samples += 1
		} 
		old1 = $1; old2 = $2
	}
}
END{\
	avg = sum / samples
	var = (sum2 - (avg * sum)) / (samples)
	sd = sqrt(var)
	printf("%.9f\t%.9f\t%.9f\n", avg, sd, var);
}'

awkjitter='
BEGIN{\
	samples = 0
	sum = 0
	sum2 = 0
}
{\
	if (samples > 0)
	{
		gap = ($2 - old2)
		gap = gap < 0 ? -gap : gap
		printf("%d\t%.9f\n", $1, gap)
		sum += gap
		sum2 += gap * gap
	}
	samples += 1
	old2 = $2
}
END{\
	avg = sum / samples
	var = (sum2 - (avg * sum)) / (samples)
	sd = sqrt(var)
	printf("%.9f\t%.9f\t%.9f\n", avg, sd, var);
}'

awkthroughput='
BEGIN{\
	nbytes = 0
}
{\
	nbytes += $3
}
END{\
	printf("%d", nbytes)
}'

awkavg='
BEGIN{\
	samples = 0
	sum = 0
	sum2 = 0
}
{\
	value = $col
	sum += value
	sum2 += value * value
	samples += 1
}
END{\
	avg = sum / samples
	var = (sum2 - (avg * sum)) / (samples)
	sd = sqrt(var)
	printf("%.9f\t%.9f\t%.9f\n", avg, sd, var);
}'

txRaw='
BEGIN{\
    nbytes = 0
    lines = 0
}
{\
    where = match($0, "MAC TX -- .*"devname".* Size: (.*)$", arr)
    if(where != 0)
    {
        nbytes += arr[1]
        lines += 1;
    }
}
END{\
    printf("totalTxBytes=%d\n", nbytes);
    printf("totalTxPackets=%d\n", lines)
}'


txRawDcMac='
BEGIN{\
    nbytes = 0
    lines = 0
}
{\

    where = match($0, "TX -- .*"devname".* Size: (.*)$", arr)
    if(where != 0)
    {
	nbytes += arr[1]
        lines += 1
    }
}
END{\
    printf("totalTxBytes=%d\n", nbytes);
    printf("totalTxPackets=%d\n", lines)
}'

colScript='
BEGIN{\
    nbytes = 0
    lines = 0
}
{\
    where = match($0, "COL -- .*"devname".* Size: (.*)$", arr)
    if(where != 0)
    {
	nbytes += arr[1]
        lines += 1
    }
}
END{\
    printf("totalColBytes=%d\n", nbytes);
    printf("totalColPackets=%d\n", lines)
}'



colScriptDcMac=$colScript

###################################################
###################################################

now=$(date -u +%s)
daterefsecs=$now
dateref=$(date -u -R -d @$daterefsecs)
datereffile=$basedir/dateref
echo "dateref: $dateref"
echo $dateref > $datereffile
echo "$datereffile content: $(cat $datereffile)"

rosrunproc=$(cat rosrunpid 2> /dev/null)
sim=$(cat simpid 2> /dev/null)
kill -9 $rosrunproc > /dev/null 2> /dev/null
kill -9 $sim > /dev/null 2> /dev/null

sleep 5s

localscenesdir=./scenes/
scenesdir=$(rospack find uwsim)/data/scenes
uwsimlog=$(realpath $basedir/uwsimnet.log)
uwsimlograw=$(realpath $basedir/uwsim.log.raw)

tmplscene=$localscenesdir/$scene.xml
echo "TMPL SCENE: $tmplscene"
if [ "$protocol" == "dcmac" ]
then
	scene=$scenesdir/$protocol.xml
	cp $tmplscene $scene
	cd ../modules/umci
	gitrev=$(git rev-parse --short HEAD)
	cd -
	library=../build/modules/umci/libumci_${gitrev}.so
	library=$(realpath ${library})
	library=$(echo "$library" | sed 's/\//\\\//g')
	echo $library
	pktbuilder="DcMacPacketBuilder"
	libpath="<libPath>$library<\/libPath>"
else
	scene=$scenesdir/$protocol.xml
	pktbuilder="VariableLength2BPacketBuilder"
	sed "s/<name><\/name>/<name>$protocol<\/name>/g" $tmplscene > $scene
	libpath=""
fi

sed -i "s/<maxDistance>15<\/maxDistance>/<maxDistance>$maxRange<\/maxDistance>/g" $scene
sed -i "s/<maxBackoffSlots><\/maxBackoffSlots>/<maxBackoffSlots>$maxBackoffSlots<\/maxBackoffSlots>/g" $scene
sed -i "s/propSpeedValue/${uwsimPropSpeed}/g" $scene
sed -i "s/packetbuilder/${pktbuilder}/g" $scene
sed -i "s/builderlibpath/${libpath}/g" $scene
uwsimlogpath=$(echo "$uwsimlog" | sed 's/\//\\\//g')
sed -i "s/<logToFile><\/logToFile>/<logToFile>$uwsimlogpath<\/logToFile>/g" $scene

loglevel=""
if [ "$debug" == "debug" ]
then
	echo "debug"
	loglevel="debug"
else
	echo "no debug"
	loglevel="info"
	if [ "$debug" == "logdebug" ]
	then
		sed -i "s/>info</>debug</g" $scene
	fi
	cat $scene
	if [ "$protocol" == "dcmac" ]
	then
		rosrun uwsim uwsim --configfile $scene --dataPath $(rospack find uwsim)/data/scenes/ --disableShaders 2>&1 | tee $uwsimlograw & 
	else
		NS_LOG="AquaSimMac=all|prefix_time:AquaSimSFama=all|prefix_time:AquaSimAloha=all|prefix_time" rosrun uwsim uwsim --configfile $scene --dataPath $(rospack find uwsim)/data/scenes/ --disableShaders 2>&1 | tee $uwsimlograw & 
	fi


	sleep 5s
	rosrunproc=$!
fi

sim=$(ps aux | grep "uwsim_binary" | awk -v mpid=$pid '
BEGIN{\
 	found=0; 
}
{\
	if(mpid != $2)
	{ 
		where = match($0, "grep")
		if(where == 0)
		{
			if(!found)
			{
				pid=$2;
				found=1;
			}
			else
			{
				if($2 < pid)
					pid=$2;
			}
		}
	}
}
END{\
	print pid
}'
)
echo "ROSRUN: $rosrunproc ; SIM: $sim"

echo $rosrunproc > rosrunpid
echo $sim > simpid
sleep 40s

mnpkts=0
mpktsize=20
mdatarate=0

if [ "$protocol" == "dcmac" ]
then
	colScript=$colScriptDcMac
	txRaw=$txRawDcMac

	echo "tx0"
	tx0applog="$rawlogdir/bluerov2_s100.log"
	${bindir}/example4 --tx-packet-size $size --num-packets $npkts --devDelay $devDelay  --node-name bluerov2_s100 --dcmac --add 1 --dstadd 0 --data-rate $datarate --log-file "$tx0applog" --ms-start 10000 --propSpeed $propSpeed -l $loglevel --maxRange $maxRange &
	tx0=$!

	echo "tx1"
	tx1applog="$rawlogdir/bluerov2_f_s100.log"
	${bindir}/example4 --tx-packet-size $size --num-packets $npkts --devDelay $devDelay --node-name bluerov2_f_s100 --dcmac --add 2 --dstadd 0 --data-rate $datarate --log-file "$tx1applog" --ms-start 10000 --propSpeed $propSpeed -l $loglevel --maxRange $maxRange &
	tx1=$!

	echo "tx2"
	tx2applog="$rawlogdir/bluerov2_f2_s100.log"
	${bindir}/example4 --tx-packet-size $size --num-packets $npkts --devDelay $devDelay --node-name bluerov2_f2_s100 --dcmac --add 3 --dstadd 0 --data-rate $datarate --log-file "$tx2applog" --ms-start 10000 --propSpeed $propSpeed -l $loglevel --maxRange $maxRange &
	tx2=$!

	echo "tx3"
	tx3applog="$rawlogdir/bluerov2_f3_s100.log"
	${bindir}/example4 --tx-packet-size $size --num-packets $npkts --devDelay $devDelay --node-name bluerov2_f3_s100 --dcmac --add 4 --dstadd 0 --data-rate $datarate --log-file "$tx3applog" --ms-start 10000 --propSpeed $propSpeed -l $loglevel --maxRange $maxRange &
	tx3=$!

	echo "base"
	baseapplog="$rawlogdir/g500_s100.log"
	${bindir}/example4 --tx-packet-size $mpktsize --num-packets $mnpkts --devDelay $devDelay --node-name g500_s100 --dcmac --master --add 0 --dstadd 1 --data-rate $mdatarate --log-file "$baseapplog" --ms-start 0 --propSpeed $propSpeed -l $loglevel --maxRange $maxRange & 
	base=$!

else

	echo "tx0"
	tx0applog="$rawlogdir/bluerov2_s100.log"
	${bindir}/example4 --tx-packet-size $size --num-packets $npkts --node-name bluerov2_s100 --dstadd 0 --data-rate $datarate --log-file "$tx0applog" --ms-start 10000 &
	tx0=$!

	echo "tx1"
	tx1applog="$rawlogdir/bluerov2_f_s100.log"
	${bindir}/example4 --tx-packet-size $size --num-packets $npkts --node-name bluerov2_f_s100 --dstadd 0 --data-rate $datarate --log-file "$tx1applog" --ms-start 10000 &
	tx1=$!

	echo "tx2"
	tx2applog="$rawlogdir/bluerov2_f2_s100.log"
	${bindir}/example4 --tx-packet-size $size --num-packets $npkts --node-name bluerov2_f2_s100 --dstadd 0 --data-rate $datarate --log-file "$tx2applog" --ms-start 10000 &
	tx2=$!

	echo "tx3"
	tx3applog="$rawlogdir/bluerov2_f3_s100.log"
	${bindir}/example4 --tx-packet-size $size --num-packets $npkts --node-name bluerov2_f3_s100 --dstadd 0 --data-rate $datarate --log-file "$tx3applog" --ms-start 10000 &
	tx3=$!

	echo "base"
	baseapplog="$rawlogdir/g500_s100.log"
	${bindir}/example4 --tx-packet-size $mpktsize --num-packets $mnpkts --node-name g500_s100 --dstadd 1 --data-rate $mdatarate --log-file "$baseapplog" --ms-start 0 & 
	base=$!

fi

sleep ${testduration}s

echo "SIGINT programs..."
kill -s INT $tx0 > /dev/null 2> /dev/null
kill -s INT $tx1 > /dev/null 2> /dev/null
kill -s INT $tx2 > /dev/null 2> /dev/null
kill -s INT $tx3 > /dev/null 2> /dev/null
kill -s INT $base > /dev/null 2> /dev/null
kill -s INT $rosrunproc > /dev/null 2> /dev/null
kill -s INT $sim > /dev/null 2> /dev/null

sleep 10s

echo "SIGTERM programs..."
kill -s TERM $rosrunproc > /dev/null 2> /dev/null
kill -s TERM $sim > /dev/null 2> /dev/null

sleep 10s

echo "kill -9 programs..."
kill -9 $(ps aux | grep "bash .*$scriptName" | awk -v mpid=$pid '{ if(mpid != $2) print $2}') > /dev/null 2>&1
kill -9 $tx0 > /dev/null 2> /dev/null
kill -9 $tx1 > /dev/null 2> /dev/null
kill -9 $tx2 > /dev/null 2> /dev/null
kill -9 $tx3 > /dev/null 2> /dev/null
kill -9 $base > /dev/null 2> /dev/null
kill -9 $rosrunproc > /dev/null 2> /dev/null
kill -9 $sim > /dev/null 2> /dev/null

sleep 5s

for pair in $tx0applog:$baseapplog:1 $tx1applog:$baseapplog:2 $tx2applog:$baseapplog:3 $tx3applog:$baseapplog:4
do
	echo "PAIR: $pair"
	txapplog="$(cut -d':' -f1 <<< $pair)"
	rxapplog="$(cut -d':' -f2 <<< $pair)"
	srcaddr="$(cut -d':' -f3 <<< $pair)"

	txname=$(basename $txapplog)
	rxname=$(basename $rxapplog)
	subresdir=$resultsdir/${txname}_${rxname}
	mkdir -p $subresdir
	cd $subresdir

	echo "TX trace"
	cat $txapplog | grep "TX" | awk -v dateref=$datereffile -v patt="TO 0" "$awktime" > tx.tr
	
	echo "TX ipg"
	cat tx.tr | awk "$awkgap" > ipg.tr.tmp 
	cat ipg.tr.tmp | head -n -1 > ipg.tr
	cat ipg.tr.tmp | tail -n 1 > ipg
	
	echo "RX trace"
	cat $rxapplog | grep "RX" | awk -v dateref=$datereffile -v patt="FROM $srcaddr" "$awktime" > rx_raw.tr
	cat rx_raw.tr | uniq --skip-chars=15 > rx.tr
	
	echo "RX ipg (IAT)"
	cat rx.tr | awk "$awkgap" > iat.tr.tmp
	cat iat.tr.tmp | head -n -1 > iat.tr
	cat iat.tr.tmp | tail -n 1 > iat
	
	rm -f *.tmp
	
	echo "Throughput:" | tee -a genresults
	tini=$(cat tx.tr | head -n 1 | cut -f 1 -d$'\t')
	tend=$(cat rx.tr | tail -n 1 | cut -f 1 -d$'\t')
	#echo "TINI: $tini ; TEND: $tend"
	elapsed=$(bc <<< "scale=9; $tend - $tini")
	echo -e "\telapsed time: $elapsed s" | tee -a genresults
	
	nbytes=$(cat rx.tr | awk "$awkthroughput")

	txdevname="${txname%.*}"
	echo "############## $txdevname"
	declare $(awk -v devname=$txdevname "$txRaw" $uwsimlog)
	declare $(awk -v devname=$txdevname "$colScript" $uwsimlog)

	efficiency=$(bc <<< "scale=9; $nbytes / $totalTxBytes * 100")
#	if [ "$protocol" != "dcmac" ]
#	then
#		efficiency2=$(bc <<< "scale=9; $nbytes / $totalTxBytes2 * 100")
#	fi

	echo -e "\tbytes received  ---------  $nbytes bytes" | tee -a genresults
	throughput=$(bc <<< "scale=9; $nbytes / $elapsed")
	echo -e "\tThroughput  -------------  $throughput B/s" | tee -a genresults
	echo $throughput > throughput
	echo -e "\tTotal tx bytes ----------- $totalTxBytes" | tee -a genresults
	echo -e "\tTotal tx packets --------- $totalTxPackets" | tee -a genresults
	echo -e "\tTotal collisioned bytes -- $totalColBytes" | tee -a genresults
	echo -e "\tTotal collisioned pkts --- $totalColPackets" | tee -a genresults
	echo -e "\tEfficiency --------------- $efficiency" | tee -a genresults
#	if [ "$protocol" != "dcmac" ]
#	then
#		echo -e "\tEfficiency2 -------------- $efficiency2" | tee -a genresults
#	fi
	
	############### END 2 END ################3
	rm -f end2end.tr
	
	curlinenum=1
	
	while read line
	do
		t1=$(echo "$line" | cut -f 1 -d$'\t')
		seq=$(echo "$line" | cut -f 2 -d$'\t')
		#echo "T1: $t1 - SEQ: $seq"
		found=false
		while [[ $found = false ]]
		do
			lineb=$(sed -n "$curlinenum p" < tx.tr)
			#echo "cur line: $lineb"
			if [[ $lineb ]]
			then
				t0=$(echo "$lineb" | cut -f 1 -d$'\t')
				seqb=$(echo "$lineb" | cut -f 2 -d$'\t')
				if [ $seq -eq $seqb ]
				then
					end2end=$(bc <<< "scale=9; $t1 - $t0")
					#echo "T1: $t1 --- T0: $t0 --- END2END: $end2end"
					echo -e "$seq\t$end2end" >> end2end.tr
					found=true
				fi
			else
				echo "THIS IS NOT POSSILE: $curlinenum" >> ERRORS.log
				break
			fi
			((curlinenum++))
		done
	done < rx.tr
	echo "End To End:" | tee -a genresults
	cat end2end.tr | awk -v col=2 "$awkavg" > end2end
	cat end2end | tee -a genresults

	cat end2end.tr | awk "$awkjitter" > jitter.tr.tmp
	cat jitter.tr.tmp | head -n -1 > jitter.tr
	cat jitter.tr.tmp | tail -n 1 > jitter
	cat jitter | tee -a genresults
	rm -f *.tmp
done
cd $basedir
echo "Moving remaining log files..."
sleep 4s
mv $uwsimlog $resultsdir
mv $uwsimlograw $resultsdir
mv $scene $resultsdir
mv $datereffile $resultsdir
mv $rawlogdir $resultsdir
cp $scriptPath $resultsdir
echo $* > $resultsdir/notes.txt
echo "Exit"

