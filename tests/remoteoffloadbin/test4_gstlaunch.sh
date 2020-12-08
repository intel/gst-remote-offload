SERVER_HOSTNAME="localhost"
SERVER_PORT=4953
COMMS="tcp"
COMMSPARAMS="--host=$SERVER_HOSTNAME --port=$SERVER_PORT"
FILENAME="${VIDEO_EXAMPLES_DIR}/Pexels_Videos_4786_960x540.mp4"

gst-launch-1.0  filesrc location=$FILENAME ! qtdemux ! decodebin !  queue name=q0  ! remoteoffloadbin.\(comms="$COMMS" commsparam="$COMMSPARAMS" tee name=t ! queue name=q1 t. ! queue name=q2 \) q1. ! videoconvert ! ximagesink q2. ! videoconvert ! ximagesink
