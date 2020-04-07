#!/bin/sh

set -e


if [ $# -lt 2 ]
then
    echo "Usage: $0 TOOL OUTDIR DEST"
    exit 1
fi

TOOL=$1
OUTDIR=`realpath $2`
DEST=$3

if [ $TOOL != "iperf3" ] && [ $TOOL != "pschedular" ]
then
    echo "TOOL must be one of [iperf3, pschedular]"
    exit 1
fi


CURDIR=`pwd`
CC_ALGO=`sysctl -n net.ipv4.tcp_congestion_control`


for flows in 1 2 5
do
    DIR=$OUTDIR/$flows-flows

    if [ ! -e $DIR ]
    then
        mkdir $DIR
    fi
    
    cd $DIR

    for cc in dumb reno vegas bbr
    do
        sysctl net.ipv4.tcp_congestion_control=$cc

        case $TOOL in
            "iperf3" ) iperf3 -t 60 -c $DEST -P $flows --json > $DIR/$cc.json;;
            "pschedular" ) pscheduler task --tool iperf3 --format=json --quiet throughput -d PT1M -P $flows --dest $DEST > $DIR/$cc.json;;
            * ) echo "Unrecognized tool"; exit 1;;
        esac
    done
done
