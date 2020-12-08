**HDDLSrc/Sink**

hddlsrc and hddlsink is a set of GStreamer plugin which receives data from IA to the target platform, and sends the computed data back to IA. Tested workload include encode/decode/transcode as well as GVA metadata.

The build instructions can be found [here](https://gitlab.devtools.intel.com/OWR/IoTG/GMS/Yocto/Graphics/Media/hddl_streamer/-/tree/streaming_mode_poc/#build-instructions)

By default, the HDDLsrc/sink plugin library should be generated in <build_dir>/gstreamer-1.0/libgsthddl.so. 

Hddlsrc/sink plugin can be only used from an GStreamer application. Using gst-launch-1.0 command won't work.

Sample applications can be found at the samples folder.

To compile the sample applications, use the following command:

`gcc kmb_app.c  -o kmb_app -L<build_dir>/gstreamer-1.0/ -lgsthddl -I<source_dir>/extensions/autonomous_mode/` `pkg-config --cflags --libs gstreamer-1.0` `-Wall`

If you have questions or concerns, feel free to file tickets in the [issues page](https://gitlab.devtools.intel.com/OWR/IoTG/GMS/Yocto/Graphics/Media/hddl_streamer/issues)  .
