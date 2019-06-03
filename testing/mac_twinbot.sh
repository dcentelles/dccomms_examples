#!/bin/bash

scriptName=$(basename $0)
scriptPath=$(realpath $0)
echo $scriptName
pid=$$

imgSize=$1
controlSize=$2
imgDatarate=$3
controlDatarate=$4
basedir=$(realpath ${5}/)
imgNumPkts=$6
protocol=$7
devDelay=2
longLinkMaxRange=100
shortLinkMaxRange=15
longLinkChannel=0
shortLinkChannel=1
devBitRate=1800

if [ "$shortLinkChannel" -eq 1 ]
then
	shortLinkPropSpeed=300000000
else
	shortLinkPropSpeed=1500
fi

if [ "$longLinkChannel" -eq 1 ]
then
	longLinkPropSpeed=300000000
else
	longLinkPropSpeed=1500
fi

controlDatarate0=$(echo "$controlDatarate/4" | bc)
controlDatarate2=$(echo "$controlDatarate*2" | bc)

imgDuration=$(echo "$imgNumPkts * $imgSize*8 / $imgDatarate" | bc -l)
controlNumPkts0=$(echo "$imgDuration / ($controlSize * 8 / $controlDatarate0)" | bc)
controlNumPkts=$(echo "$imgDuration / ($controlSize * 8 / $controlDatarate)" | bc)
controlNumPkts2=$(echo "$imgDuration / ($controlSize * 8 / $controlDatarate2)" | bc)
testduration=$(echo "$imgDuration + 30" | bc -l)

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
echo "num. control pkts 0: $controlNumPkts0" | tee -a $resultsdir/notes
echo "num. control pkts 1: $controlNumPkts" | tee -a $resultsdir/notes
echo "num. control pkts 2: $controlNumPkts2" | tee -a $resultsdir/notes
echo "test duration: $testduration" | tee -a $resultsdir/notes
echo "control datarate 0: $controlDatarate0" | tee -a $resultsdir/notes
echo "control datarate 1: $controlDatarate" | tee -a $resultsdir/notes
echo "control datarate 2: $controlDatarate2" | tee -a $resultsdir/notes
echo "img datarate: $imgDatarate" | tee -a $resultsdir/notes
echo "protocol: $protocol" | tee -a $resultsdir/notes
echo "long link channel: $longLinkChannel" | tee -a $resultsdir/notes
echo "short link channel: $shortLinkChannel" | tee -a $resultsdir/notes
echo "long link range: $longLinkMaxRange" | tee -a $resultsdir/notes
echo "short link range: $shortLinkMaxRange" | tee -a $resultsdir/notes

kill -9 $(ps aux | grep "bash .*$scriptName" | awk -v mpid=$pid '{ if(mpid != $2) print $2}') > /dev/null 2>&1

sleep 1s
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

if [ "$protocol" == "dcmac" ]
then
	txRaw='
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
else
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
fi

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

scene=$scenesdir/$protocol.xml
if [ "$protocol" == "nomac" ]
then
	tmplscene=$localscenesdir/twinbot.xml
	cp $tmplscene $scene
else
	tmplscene=$localscenesdir/twinbot.xml
	cp $tmplscene $scene
	if [ "$protocol" == "dcmac" ]
	then
		gitrev=$(git rev-parse --short HEAD)
		library=../build/libdccomms_examples_${gitrev}_packets.so
		library=$(realpath ${library})
		library=$(echo "$library" | sed 's/\//\\\//g')
		echo $library
		pktbuilder="DcMacPacketBuilder"
		libpath="<libPath>$library<\/libPath>"
	else
		sed "s/<name><\/name>/<name>$protocol<\/name>/g" $tmplscene > $scene
		pktbuilder="VariableLength2BPacketBuilder"
		libpath=""
	fi
fi

sed -i "s/packetbuilder/${pktbuilder}/g" $scene
sed -i "s/builderlibpath/${libpath}/g" $scene
sed -i "s/longLinkMaxRange/${longLinkMaxRange}/g" $scene
sed -i "s/shortLinkMaxRange/${shortLinkMaxRange}/g" $scene
sed -i "s/shortLinkChannel/${shortLinkChannel}/g" $scene
sed -i "s/longLinkChannel/${longLinkChannel}/g" $scene
uwsimlogpath=$(echo "$uwsimlog" | sed 's/\//\\\//g')
sed -i "s/<logToFile><\/logToFile>/<logToFile>$uwsimlogpath<\/logToFile>/g" $scene

cat $scene

if [ "$protocol" == "nomac" ] || [ "$protocol" == "dcmac" ]
then
	rosrun uwsim uwsim --configfile $scene --dataPath $(rospack find uwsim)/data/scenes/ 2>&1 | tee $uwsimlograw & 
else
	NS_LOG="AquaSimMac=all|prefix_time:AquaSimSFama=all|prefix_time:AquaSimAloha=all|prefix_time" rosrun uwsim uwsim --configfile $scene --dataPath $(rospack find uwsim)/data/scenes/ 2>&1 | tee $uwsimlograw & 
fi

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
sleep 35s


if [ "$protocol" == "dcmac" ]
then
	if [ $longLinkChannel -eq $shortLinkChannel ]
	then
		leaderAddr=1
		followerAddr=2
		supportAddr=3
		explorer0Addr=4
		explorer1Addr=5
		explorer2Addr=6
		explorer3Addr=7
		buoyAddr=0
		shortLinkMaxNodes=6
		longLinkMaxNodes=6
		shortLinkMaster=""
		longLinkMaster="--master"
	else
		leaderAddr=1
		followerAddr=2
		supportAddr=0
		explorer0Addr=1
		explorer1Addr=2
		explorer2Addr=3
		explorer3Addr=4
		buoyAddr=0
		shortLinkMaxNodes=2
		longLinkMaxNodes=4
		shortLinkMaster="--master"
		longLinkMaster="--master"
	fi

	##  SHORT LINKS
	echo "follower"
	followerapplog="$rawlogdir/follower.log"
	${bindir}/example4 \
		--tx-packet-size $controlSize \
		--num-packets $controlNumPkts2 \
		--devDelay $devDelay \
		--dcmac \
		--maxnodes $shortLinkMaxNodes \
		--propSpeed $shortLinkPropSpeed \
		--node-name comms_follower \
		--add $followerAddr \
		--dstadd $leaderAddr \
		--data-rate $controlDatarate2 \
		--log-file "$followerapplog" \
		--devBitRate $devBitRate \
		-l debug \
		--ms-start 10000 &

	follower=$!

	echo "leader"
	leaderapplog="$rawlogdir/leader.log"
	${bindir}/example4 \
		--tx-packet-size $controlSize \
		--num-packets $controlNumPkts2 \
		--devDelay $devDelay \
		--dcmac \
		--maxnodes $shortLinkMaxNodes \
		--propSpeed $shortLinkPropSpeed \
		--node-name comms_leader \
		--add $leaderAddr \
		--dstadd $followerAddr \
		--data-rate $controlDatarate2 \
		--log-file "$leaderapplog" \
		--devBitRate $devBitRate \
		-l debug \
		--ms-start 10000 &
	
	leader=$!

	echo "SHORT LINK MASTER: $shortLinkMaster"
	echo "support"
	supportapplog="$rawlogdir/support.log"
	${bindir}/example4 \
		--tx-packet-size $controlSize \
		--num-packets $controlNumPkts \
		--devDelay $devDelay \
		--dcmac \
		--master \
		--maxnodes $shortLinkMaxNodes \
		--propSpeed $shortLinkPropSpeed \
		--node-name comms_support \
		--add $supportAddr \
		--dstadd $leaderAddr \
		--data-rate $controlDatarate \
		--log-file "$supportapplog" \
		--devBitRate $devBitRate \
		-l debug \
		--ms-start 10000 &

	support=$!

	## LONG LINKS
	echo "buoy"
	buoyapplog="$rawlogdir/buoy.log"
	${bindir}/example4 \
		--tx-packet-size $controlSize \
		--num-packets $controlNumPkts0 \
		--devDelay $devDelay \
		--dcmac \
		--maxnodes $longLinkMaxNodes \
		--master \
		--log-file "$buoyapplog" \
		--propSpeed $longLinkPropSpeed \
		--add $buoyAddr \
		--node-name comms_buoy \
		--dstadd $explorer0Addr \
		--data-rate $controlDatarate0 \
		--devBitRate $devBitRate \
		--l debug \
		--ms-start 10000 &
	
	buoy=$!
		
	echo "explorer0"
	explorer0applog="$rawlogdir/explorer0.log"
	${bindir}/example4 \
		--tx-packet-size $imgSize \
		--num-packets $imgNumPkts \
		--devDelay $devDelay \
		--dcmac \
		--maxnodes $longLinkMaxNodes \
		--propSpeed $longLinkPropSpeed \
		--node-name comms_explorer0 \
		--add $explorer0Addr \
		--dstadd $buoyAddr \
		--data-rate $imgDatarate \
		--log-file "$explorer0applog" \
		--devBitRate $devBitRate \
		-l debug \
		--ms-start 10000 &

	explorer0=$!

	echo "explorer1"
	explorer1applog="$rawlogdir/explorer1.log"
	${bindir}/example4 \
		--tx-packet-size $imgSize \
		--num-packets $imgNumPkts \
		--node-name comms_explorer1 \
		--devDelay $devDelay \
		--dcmac \
		--maxnodes $longLinkMaxNodes \
		--propSpeed $longLinkPropSpeed \
		--add $explorer1Addr \
		--dstadd $buoyAddr \
		--data-rate $imgDatarate \
		--log-file "$explorer1applog" \
		--devBitRate $devBitRate \
		-l debug \
		--ms-start 10000 &

	explorer1=$!

	echo "explorer2"
	explorer2applog="$rawlogdir/explorer2.log"
	${bindir}/example4 \
		--tx-packet-size $imgSize \
		--num-packets $imgNumPkts \
		--node-name comms_explorer2 \
		--devDelay $devDelay \
		--dcmac \
		--maxnodes $longLinkMaxNodes \
		--propSpeed $longLinkPropSpeed \
		--add $explorer2Addr \
		--dstadd $buoyAddr \
		--data-rate $imgDatarate \
		--log-file "$explorer2applog" \
		--devBitRate $devBitRate \
		-l debug \
		--ms-start 10000 &

	explorer2=$!

	echo "explorer3"
	explorer3applog="$rawlogdir/explorer3.log"
	${bindir}/example4 \
		--tx-packet-size $imgSize \
		--num-packets $imgNumPkts \
		--node-name comms_explorer3 \
		--devDelay $devDelay \
		--dcmac \
		--maxnodes $longLinkMaxNodes \
		--propSpeed $longLinkPropSpeed \
		--add $explorer3Addr \
		--dstadd $buoyAddr \
		--data-rate $imgDatarate \
		--log-file "$explorer3applog" \
		--devBitRate $devBitRate \
		-l debug \
		--ms-start 10000 &

	explorer3=$!

else
	leaderAddr=2
	followerAddr=3
	supportAddr=1
	explorer0Addr=10
	explorer1Addr=11
	explorer2Addr=12
	explorer3Addr=13
	buoyAddr=0

	##  SHORT LINKS
	echo "follower"
	followerapplog="$rawlogdir/follower.log"
	${bindir}/example4 \
		--tx-packet-size $controlSize \
		--num-packets $controlNumPkts2 \
		--node-name comms_follower \
		--add $followerAddr \
		--dstadd $leaderAddr \
		--data-rate $controlDatarate2 \
		--log-file "$followerapplog" \
		--ms-start 10000 &
	follower=$!

	echo "leader"
	leaderapplog="$rawlogdir/leader.log"
	${bindir}/example4 \
		--tx-packet-size $controlSize \
		--num-packets $controlNumPkts2 \
		--node-name comms_leader \
		--add $leaderAddr \
		--dstadd $followerAddr \
		--data-rate $controlDatarate2 \
		--log-file "$leaderapplog" \
		--ms-start 10000 &
	leader=$!

	echo "support"
	supportapplog="$rawlogdir/support.log"
	${bindir}/example4 \
		--tx-packet-size $controlSize \
		--num-packets $controlNumPkts \
		--node-name comms_support \
		--add $supportAddr \
		--dstadd $leaderAddr \
		--data-rate $controlDatarate \
		--log-file "$supportapplog" \
		--ms-start 10000 &
	support=$!

	## LONG LINKS
	echo "buoy"
	buoyapplog="$rawlogdir/buoy.log"
	${bindir}/example4 \
		--tx-packet-size $controlSize \
		--num-packets $controlNumPkts0 \
		--node-name comms_buoy \
		--add $buoyAddr \
		--dstadd $explorer0Addr \
		--data-rate $controlDatarate0 \
		--log-file "$buoyapplog" \
		--ms-start 10000 &
	buoy=$!
		
	echo "explorer0"
	explorer0applog="$rawlogdir/explorer0.log"
	${bindir}/example4 \
		--tx-packet-size $imgSize \
		--num-packets $imgNumPkts \
		--node-name comms_explorer0 \
		--add $explorer0Addr \
		--dstadd $buoyAddr \
		--data-rate $imgDatarate \
		--log-file "$explorer0applog" \
		--ms-start 10000 &
	explorer0=$!

	echo "explorer1"
	explorer1applog="$rawlogdir/explorer1.log"
	${bindir}/example4 \
		--tx-packet-size $imgSize \
		--num-packets $imgNumPkts \
		--node-name comms_explorer1 \
		--add $explorer1Addr \
		--dstadd $buoyAddr \
		--data-rate $imgDatarate \
		--log-file "$explorer1applog" \
		--ms-start 10000 &
	explorer1=$!

	echo "explorer2"
	explorer2applog="$rawlogdir/explorer2.log"
	${bindir}/example4 \
		--tx-packet-size $imgSize \
		--num-packets $imgNumPkts \
		--node-name comms_explorer2 \
		--add $explorer2Addr \
		--dstadd $buoyAddr \
		--data-rate $imgDatarate \
		--log-file "$explorer2applog" \
		--ms-start 10000 &
	explorer2=$!

	echo "explorer3"
	explorer3applog="$rawlogdir/explorer3.log"
	${bindir}/example4 \
		--tx-packet-size $imgSize \
		--num-packets $imgNumPkts \
		--node-name comms_explorer3 \
		--add $explorer3Addr \
		--dstadd $buoyAddr \
		--data-rate $imgDatarate \
		--log-file "$explorer3applog" \
		--ms-start 10000 &
	explorer3=$!

fi


sleep ${testduration}s

echo "SIGINT programs..."
kill -s INT $leader > /dev/null 2> /dev/null
kill -s INT $follower > /dev/null 2> /dev/null
kill -s INT $support > /dev/null 2> /dev/null
kill -s INT $buoy > /dev/null 2> /dev/null
kill -s INT $explorer0 > /dev/null 2> /dev/null
kill -s INT $explorer1 > /dev/null 2> /dev/null
kill -s INT $explorer2 > /dev/null 2> /dev/null
kill -s INT $explorer3 > /dev/null 2> /dev/null
kill -s INT $rosrunproc > /dev/null 2> /dev/null
kill -s INT $sim > /dev/null 2> /dev/null

sleep 10s

echo "SIGTERM programs..."
kill -s TERM $rosrunproc > /dev/null 2> /dev/null
kill -s TERM $sim > /dev/null 2> /dev/null

sleep 10s

echo "kill -9 programs..."
kill -9 $(ps aux | grep "bash .*$scriptName" | awk -v mpid=$pid '{ if(mpid != $2) print $2}') > /dev/null 2>&1
kill -9 $leader > /dev/null 2> /dev/null
kill -9 $follower > /dev/null 2> /dev/null
kill -9 $support > /dev/null 2> /dev/null
kill -9 $buoy > /dev/null 2> /dev/null
kill -9 $explorer0 > /dev/null 2> /dev/null
kill -9 $explorer1 > /dev/null 2> /dev/null
kill -9 $explorer2 > /dev/null 2> /dev/null
kill -9 $explorer3 > /dev/null 2> /dev/null
kill -9 $rosrunproc > /dev/null 2> /dev/null
kill -9 $sim > /dev/null 2> /dev/null

sleep 10s
flows=( \
$leaderapplog:$followerapplog:$leaderAddr:$followerAddr \
$followerapplog:$leaderapplog:$followerAddr:$leaderAddr \
$supportapplog:$leaderapplog:$supportAddr:$leaderAddr \
$buoyapplog:$explorer0applog:$buoyAddr:$explorer0Addr \
$explorer0applog:$buoyapplog:$explorer0Addr:$buoyAddr \
$explorer1applog:$buoyapplog:$explorer1Addr:$buoyAddr \
$explorer2applog:$buoyapplog:$explorer2Addr:$buoyAddr \
$explorer3applog:$buoyapplog:$explorer3Addr:$buoyAddr \
)
#https://www.cyberciti.biz/faq/bash-for-loop-array/
for pair in ${flows[@]}
#for pair in $leaderapplog:$followerapplog:$leaderAddr:$followerAddr $followerapplog:$leaderapplog:$followerAddr:$leaderAddr $leaderapplog:$masterapplog:1:0 $supportapplog:$masterapplog:2:0 $masterapplog:$leaderapplog:0:1 $masterapplog:$supportapplog:0:2
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

	if [ -z "$nbytes" ] || [ $(echo "$nbytes < 0" | bc -l) -eq 1 ]; then nbytes=0; fi
	if [ -z "$throughput" ] || [ $(echo "$throughput < 0" | bc -l) -eq 1 ]; then throughput=0; fi
	if [ -z "$totalTxBytes" ] || [ $(echo "$totalTxBytes < 0" | bc -l) -eq 1 ]; then totalTxBytes=0; fi
	if [ -z "$totalTxPackets" ] || [ $(echo "$totalTxPackets < 0" | bc -l) -eq 1 ]; then totalTxPackets=0; fi
	if [ -z "$totalColBytes" ] || [ $(echo "$totalColBytes < 0" | bc -l) -eq 1 ]; then totalColBytes=0; fi
	if [ -z "$totalColPackets" ] || [ $(echo "$totalColPackets < 0" | bc -l) -eq 1 ]; then totalColPackets=0; fi
	if [ -z "$efficiency" ] || [ $(echo "$efficiency < 0" | bc -l) -eq 1 ]; then efficiency=0; fi

	echo -e "\tbytes received  ---------  $nbytes bytes" | tee -a genresults
	throughput=$(bc <<< "scale=9; $nbytes / $elapsed")
	echo -e "\tThroughput  -------------  $throughput B/s" | tee -a genresults
	echo $throughput > throughput
	echo -e "\tTotal tx bytes ----------- $totalTxBytes" | tee -a genresults
	echo -e "\tTotal tx packets --------- $totalTxPackets" | tee -a genresults
	echo -e "\tTotal collisioned bytes -- $totalColBytes" | tee -a genresults
	echo -e "\tTotal collisioned pkts --- $totalColPackets" | tee -a genresults
	echo -e "\tEfficiency --------------- $efficiency" | tee -a genresults
	
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

