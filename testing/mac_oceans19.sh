#!/bin/bash

scriptName=$(basename $0)
scriptPath=$(realpath $0)
echo $scriptName
pid=$$

imgSize=$1
controlSize=$2
imgDatarate=$3
controlDatarate=$4
basedir=$5
imgNumPkts=$6
protocol=$7

acMaxRange=100
rfMaxRange=15
controlDatarate2=$(echo "$controlDatarate*2" | bc)

imgDuration=$(echo "$imgNumPkts * $imgSize*8 / $imgDatarate" | bc -l)
controlNumPkts=$(echo "$imgDuration / ($controlSize * 8 / $controlDatarate)" | bc)
controlNumPkts2=$(echo "$imgDuration / ($controlSize * 8 / $controlDatarate2)" | bc)
testduration=$(echo "$imgDuration + 60" | bc -l)

basedir=$(realpath ${basedir}/)
echo "BASEDIR: $basedir"
bindir="../build/" #TODO: as argument
resultsdir=$basedir/results
rawlogdir=$basedir/rawlog

rm -rf $resultsdir $rawlogdir
rm -rf /dev/mqueue/*

mkdir -p $resultsdir
mkdir -p $rawlogdir

echo $* > $resultsdir/notes.txt
echo "control pkt size: $controlSize" | tee -a $resultsdir/notes
echo "image pkt size: $imgSize" | tee -a $resultsdir/notes
echo "num. image pkts: $imgNumPkts" | tee -a $resultsdir/notes
echo "num. control pkts: $controlNumPkts" | tee -a $resultsdir/notes
echo "num. control pkts 2: $controlNumPkts2" | tee -a $resultsdir/notes
echo "test duration: $testduration" | tee -a $resultsdir/notes
echo "control datarate: $controlDatarate" | tee -a $resultsdir/notes
echo "control datarate2: $controlDatarate2" | tee -a $resultsdir/notes
echo "img datarate: $imgDatarate" | tee -a $resultsdir/notes




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

if [ "$protocol" == "mac" ]
then
	tmplscene=$localscenesdir/netsim_oceans19.xml
else
	tmplscene=$localscenesdir/netsim_oceans19_nomac.xml
fi
scene=$scenesdir/oceans19.xml
cp $tmplscene $scene

uwsimlogpath=$(echo "$uwsimlog" | sed 's/\//\\\//g')
sed -i "s/<logToFile>uwsimnet.log<\/logToFile>/<logToFile>$uwsimlogpath<\/logToFile>/g" $scene

cat $scene

#NS_LOG="AquaSimMac=all|prefix_time:AquaSimSFama=all|prefix_time:AquaSimAloha=all|prefix_time" rosrun uwsim uwsim --configfile $scene --dataPath $(rospack find uwsim)/data/scenes/ --disableShaders 2>&1 | tee $uwsimlograw & 

sleep 5s
rosrunproc=$!
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

colScript=$colScriptDcMac
txRaw=$txRawDcMac

echo "leader"
leaderapplog="$rawlogdir/leader.log"
${bindir}/example4 --tx-packet-size $controlSize --num-packets $controlNumPkts2 --node-name comms_leader --add 5 --dstadd 4 --data-rate $controlDatarate2 --log-file "$leaderapplog" --ms-start 10000 -l debug&
#${bindir}/example4 --tx-packet-size $controlSize --num-packets 0 --node-name comms_leader --add 5 --dstadd 4 --data-rate $controlDatarate2 --log-file "$leaderapplog" --ms-start 10000 -l debug&
leader=$!

echo "follower"
followerapplog="$rawlogdir/follower.log"
${bindir}/example4 --tx-packet-size $controlSize --num-packets $controlNumPkts2 --node-name comms_follower --add 4 --dstadd 5 --data-rate $controlDatarate2 --log-file "$followerapplog" --ms-start 10000 -l debug&
#${bindir}/example4 --tx-packet-size $controlSize --num-packets 0 --node-name comms_follower --add 4 --dstadd 5 --data-rate $controlDatarate2 --log-file "$followerapplog" --ms-start 10000 -l debug&
follower=$!

echo "support"
supportapplog="$rawlogdir/support.log"
${bindir}/example4 --tx-packet-size $imgSize --num-packets $imgNumPkts --node-name comms_support --add 3 --dstadd 0 --data-rate $imgDatarate --log-file "$supportapplog" --ms-start 10000 -l debug&
#${bindir}/example4 --tx-packet-size $imgSize --num-packets 0 --node-name comms_support --add 3 --dstadd 0 --data-rate $imgDatarate --log-file "$supportapplog" --ms-start 10000 -l debug&
support=$!

echo "leader_ac"
leaderapplog_ac="$rawlogdir/leader_ac.log"
${bindir}/example4 --tx-packet-size $controlSize --num-packets $controlNumPkts --node-name comms_leader_ac --add 2 --dstadd 0 --data-rate $controlDatarate --log-file "$leaderapplog_ac" --ms-start 10000 -l debug&
#${bindir}/example4 --tx-packet-size $controlSize --num-packets 1 --node-name comms_leader_ac --add 2 --dstadd 0 --data-rate $controlDatarate --log-file "$leaderapplog_ac" --ms-start 10000 -l debug&
leader_ac=$!

echo "master"
masterapplog="$rawlogdir/master.log"
${bindir}/twinbot_master --tx-packet-size $controlSize --num-packets $controlNumPkts --node-name comms_master --add 0  --data-rate $controlDatarate --log-file "$masterapplog" --ms-start 10000 -l debug & 
#${bindir}/twinbot_master --tx-packet-size $controlSize --num-packets 1 --node-name comms_master --add 0  --data-rate $controlDatarate --log-file "$masterapplog" --ms-start 10000 -l debug & 
#${bindir}/example4 --tx-packet-size $controlSize --num-packets $controlNumPkts --node-name comms_master --add 0 --dstadd 2 --data-rate $controlDatarate --log-file "$masterapplog" --ms-start 10000 -l debug & 
master=$!

sleep ${testduration}s

echo "SIGINT programs..."
kill -s INT $leader > /dev/null 2> /dev/null
kill -s INT $leader_ac > /dev/null 2> /dev/null
kill -s INT $follower > /dev/null 2> /dev/null
kill -s INT $support > /dev/null 2> /dev/null
kill -s INT $master > /dev/null 2> /dev/null
#kill -s INT $rosrunproc > /dev/null 2> /dev/null
kill -s INT $sim > /dev/null 2> /dev/null

sleep 10s

echo "SIGTERM programs..."
#kill -s TERM $rosrunproc > /dev/null 2> /dev/null
kill -s TERM $sim > /dev/null 2> /dev/null

sleep 10s

echo "kill -9 programs..."
kill -9 $(ps aux | grep "bash .*$scriptName" | awk -v mpid=$pid '{ if(mpid != $2) print $2}') > /dev/null 2>&1
kill -9 $leader > /dev/null 2> /dev/null
kill -9 $leader_ac > /dev/null 2> /dev/null
kill -9 $follower > /dev/null 2> /dev/null
kill -9 $support > /dev/null 2> /dev/null
kill -9 $master > /dev/null 2> /dev/null
#kill -9 $rosrunproc > /dev/null 2> /dev/null
kill -9 $sim > /dev/null 2> /dev/null

sleep 5s


for pair in $leaderapplog:$followerapplog:5:4 $followerapplog:$leaderapplog:4:5 $supportapplog:$masterapplog:3:0 $masterapplog:$leaderapplog_ac:0:2 $masterapplog:$supportapplog:0:3 $leaderapplog_ac:$masterapplog:2:0
do
	echo "PAIR: $pair"
	txapplog="$(cut -d':' -f1 <<< $pair)"
	rxapplog="$(cut -d':' -f2 <<< $pair)"
	srcaddr="$(cut -d':' -f3 <<< $pair)"
	dstaddr="$(cut -d':' -f4 <<< $pair)"

	txname=$(basename $txapplog)
	rxname=$(basename $rxapplog)
	subresdir=$resultsdir/${txname}_${rxname}
	mkdir -p $subresdir
	cd $subresdir

	echo "TX trace"
	cat $txapplog | grep "TX" | awk -v dateref=$datereffile -v patt="TO $dstaddr" "$awktime" > tx.tr
	
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

	txdevname="comms_${txname%.*}"
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

echo "Exit"

