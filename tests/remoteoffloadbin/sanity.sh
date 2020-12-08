#!/bin/bash
if [ -z "$1" ]
then
  echo "usage: sanity.sh testNum(1-12) numStreams(default: 1)"
  exit
fi

testNum=$1
if [ $testNum -lt 1 ]
then
  echo "testNum must be in the range 1 to 12"
  exit
fi

if [ $testNum -gt 12 ]
then
  echo "testNum must be in the range 1 to 12"
  exit
fi

echo "TestNum = $testNum"

if [ -z "$2" ]
then
  nstreams=1
else
  nstreams=$2
fi

echo "nstreams=$nstreams"


if [ $testNum -lt 5 ]; then
  vid=SampleVideo_640x480_1mb.mp4
  parse=h264parse
  decode="sublaunch launch-string=\"vaapih264dec\" ! identity"
elif [ $testNum -lt 9 ]; then
  vid=SampleVideo_640x480_1mb_h265.mp4
  parse=h265parse
  decode="sublaunch launch-string=\"vaapih265dec\" ! identity"
else
  vid=SampleVideo_640x480_1mb_jpeg.mp4
  parse=jpegparse
  decode="sublaunch launch-string=\"vaapijpegdec\" ! identity"
fi

n=$(( (${testNum}-1) % 4))
if [ $n -eq 0 ]; then
  postdecode="capsfilter caps=\"video/x-raw(memory:VASurface)\""
  outputsave="filesink"
  extension=".raw"
elif [ $n -eq 1 ]; then
  postdecode="sublaunch launch-string=\"vaapijpegenc\" ! capsfilter caps=\"image/jpeg\""
  outputsave="qtmux ! filesink"
  extension=".mp4"
elif [ $n -eq 2 ]; then
  postdecode="sublaunch launch-string=\"vaapijpegenc\" ! capsfilter caps=\"image/jpeg\""
  outputsave="filesink"
  extension=".jpegstream"
else
  postdecode="sublaunch launch-string=\"vaapijpegenc\" ! capsfilter caps=\"image/jpeg\""
  outputsave="multifilesink"
  extension="_%d.jpeg"
fi

input=`dirname "$0"`/${vid}
echo "gst-launch-1.0 filesrc location=${input} ! qtdemux ! ${parse} ! tee name=t \\" > sanity.run
i=0
while [ $i -lt $nstreams ]; do
echo "t. ! queue name=q${i} \\" >> sanity.run
((i++))
done

echo "remoteoffloadbin.\( \\" >> sanity.run

i=0
while [ $i -lt $nstreams ]; do
echo "q${i}. ! $decode ! $postdecode name=cf${i} \\" >> sanity.run
((i++))
done

echo "\) \\" >> sanity.run

i=0
while [ $i -lt $nstreams ]; do
echo "cf${i}. ! bps name=bpsstr${i} !  $outputsave location=./out${i}${extension} \\" >> sanity.run
((i++))
done

echo " " >> sanity.run

echo "Launch Command:"
cat ./sanity.run
source ./sanity.run
