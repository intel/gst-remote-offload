SERVER_HOSTNAME="localhost"
SERVER_PORT=4953
COMMS="tcp"
COMMSPARAMS="--host=$SERVER_HOSTNAME --port=$SERVER_PORT"

gst-launch-1.0  videotestsrc ! remoteoffloadbin.\(comms="$COMMS" commsparam="$COMMSPARAMS" queue \) ! videoconvert ! ximagesink
