SERVER_HOSTNAME="localhost"
SERVER_PORT=4953
COMMS="tcp"
COMMSPARAMS="--host=$SERVER_HOSTNAME --port=$SERVER_PORT"
FILENAME="${VIDEO_EXAMPLES_DIR}/Pexels_Videos_4786_960x540.mp4"

gst-launch-1.0  filesrc location=$FILENAME ! qtdemux ! decodebin ! tee name=t ! queue name=q0 t. ! queue name=q1  remoteoffloadbin.\(comms="$COMMS" commsparam="$COMMSPARAMS" q0. ! queue name=q2 q1. ! queue name=q3 \) q2. ! videoconvert ! ximagesink q3. ! videoconvert ! ximagesink
