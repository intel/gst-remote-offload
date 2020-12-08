# GStreamer Remote Offload and HDDLSrc/HDDLSink plugin
Framework for offloading gstreamer sub-pipelines to a remote server/target

## Getting Started
Please follow the below instructions to build & use this framework.

## Prerequisites
* GStreamer framework 1.14 or above

* (Optional) GVA (Intel Gstreamer Video Analytics) Plugins -- Required to build meta serialization support for *GstGVATensorMeta* & *GstGVAJsonMeta*:
  * Use [GVA Release 2020.2](https://github.com/opencv/gst-video-analytics/releases/tag/v1.0.0)
  * Set **GVA_HOME** env variable to the top level source directory of GVA plugins.
    This project assumes that it will find a build at **${GVA_HOME}/build/**

    example:
    ```
    export GVA_HOME=/home/user/gva/gst-video-analytics/
    ```
  * Note that GVA Plugins have a dependency on Intel(R) OpenVINO(TM) toolkit.

* (Optional) XLink PCIe components -- Required if you intend to use **KeemBay** as the remote target via the "xlink" or "hddl" comms-type.
   * Follow these steps:
      1. Obtain the kmb-xlink-pcie-host-driver-dkms-xxxx.deb package. This is usually stored alongside the Yocto BKC build artifacts.
      2. Install the package using the following commands:
      ```
      # Remove any previously installed packages
      $ sudo apt remove kmb-xlink-pcie-host-driver-dkms
      # Install the debian package
      $ sudo apt install kmb-xlink-pcie-host-driver-dkms-xxxx.deb
      ```
      3. For runtime-setup, execute the following commands once per IA-host boot:
      ```
      $ sudo modprobe mxlk
      $ sudo modprobe xlink
      $ sudo chmod 666 /dev/xlnk
      ```
* (Optional) HDDLUnite -- Required if you intend to use **KeemBay** as the remote target via the "hddl" comms-type.
  * Follow these steps:
    1. Build / Install an HDDLUnite release, following the instuctions [here](https://gitlab.devtools.intel.com/kmb_hddl/hddlunite)
    2. Assuming HDDLUnite is installed at /opt/intel/hddlunite/, set the following *HddlUnite_DIR* environment variable as the following:
    ```
    $ export HddlUnite_DIR=/opt/intel/hddlunite/lib/cmake/HddlUnite
    ```
    (Please adjust above path accordingly if HDDLUnite is installed somewhere else)


## Build instructions
1. Create an install directory someplace where you'd like to install it. i.e.
   ```
   mkdir /path/to/gst-remoteoffload-install
   ```
2. Create a build directory within top level src directory (i.e. hddl_streamer/):
    ```
    mkdir build
    cd build
    ```
3. Run cmake, specifying install path:
    ```
    cmake -DCMAKE_INSTALL_PREFIX=/path/to/gst-remoteoffload-install ..
    ```
4. Build it:
   ```
   make -j 8
   ```
5. Install it:
   ```
   make install
   ```
   
## HDDLSrc and HDDLSink Plugin

The documentation for HDDLSrc and HDDLSrc Plugin can be accessed [here](https://gitlab.devtools.intel.com/OWR/IoTG/GMS/Yocto/Graphics/Media/hddl_streamer/-/tree/streaming_mode_poc/extensions%2Fautonomous_mode/README.md)

## How to get started using the GStreamer Remote Offload Framework:

* Environment setup
  * To set up the proper environment (PATH, LD_LIBRARY_PATH, GST_PLUGIN_PATH, etc.), source the setup script that was installed during the previous *make install* step:
    ```
    source /path/to/gst-remoteoffload-install/share/gst-remote-offload/scripts/setup_env.sh
    ```
    For convenience, it's recommended to add this to automatically sourced shell scripts, such as .bashrc.

 * Starting the Remote Offload Server
   * Before running GStreamer *client-side* pipelines that incorporate **remoteoffloadbin**, the server that will host GStreamer subpipeline execution needs to be started. The startup sequence for the server depends on the *comms*-type of the server (xlink, HDDLUnite, etc.). Please follow the relevant instructions for the *comms*-type that you'd like to use.
      * **xlink** -- The xlink *comms*-type is specific to **KeemBay** targets right now. Run the following set of instructions on the **KeemBay**-side (i.e. within a Yocto shell) to start the xlink server process:
      ```
      # Note: It's assumed that the Remote Offload Framework has been installed to /usr here.
      #  Adjust if necessary.
      $ source /usr/share/gst-remote-offload/scripts/setup_env.sh
      $ source /usr/share/gst-video-analytics/scripts/setup_env.sh
      $ export SIPP_FIRST_SHAVE=3
      $ export USE_SIPP=1
      $ export VPU_FIRMWARE_FILE="vpu_nvr.bin"
      $ export GST_DEBUG=2,remoteoffload*:4,bps:4
      $ gst_offload_xlink_server
      ```
      * **hddl** (i.e. HDDLUnite) -- For the HDDLUnite *comms*-type, the server is a running component of the *hddl_device_server* (on ARM-side), and *hddl_scheduler_server* (on client-side). The process to start the HDDLUnite processes should be followed from [here](https://gitlab.devtools.intel.com/kmb_hddl/hddlunite).

 * **remoteoffloadbin** usage:
   * The primary use of the GStreamer Remote Offload Framework is through a single helper element, called **remoteoffloadbin**. It is exposed to the user like any other GStreamer element. It can be inspected using `gst-inspect-1.0 remoteoffloadbin`, and uses in GStreamer pipelines via `gst-launch-1.0`, the direct C-API, etc.
   * The *remoteoffloadbin* is a *GstBin*-like element, for which the user assembles the desired subpipeline that they would like to offload to a particular remote target / server. All data transfers, serialization, synchronization happen "automagically" by the internal Remote Offload Framework. For example, let's say that the user wants to offload an H264->H265 transcode operation to a *hddl* server, the following is the gst-launch-1.0 command that they could use:
   ```
   $ gst-launch-1.0 filesrc location=input.264 ! h264parse ! remoteoffloadbin.\( comms="hddl" vaapih264dec ! queue ! vaapih265enc \) ! filesink location=out.265
   ```
   In the above example, the subpipeline *vaapih264dec ! queue ! vaapih265enc* will be *offloaded* to the server process. Note the *comms* property of *remoteoffloadbin* is being set to "hddl"

  * The environment variable, **GST_REMOTEOFFLOAD_DEFAULT_COMMS**, can control what the *remoteoffloadbin's* "comms" property is defaulted to. Similarly, the default "commsparam" property can be set using the **GST_REMOTEOFFLOAD_DEFAULT_COMMSPARAM** environment variable. They are of course optional (one can always explicitly set the "comms" / "commsparam" property at runtime), but they are provided for convenience. The following is the equivalent of the first example, but leveraging these environment variables (note that these properties are no longer explicitly set within gst-launch-1.0 command):
   ```
   $ export GST_REMOTEOFFLOAD_DEFAULT_COMMS="hddl"
   $ gst-launch-1.0 filesrc location=input.264 ! h264parse ! remoteoffloadbin.\( vaapih264dec ! queue ! vaapih265enc \) ! filesink location=out.265
   ```
   Note that these environment variables only control what the "comms" / "commsparam" are *defaulted* to. If a user/application explicitly sets this property (i.e. on gst-launch-1.0 command, or programatically via g_object_set ), the value set by these environment variables will be overridden.
  * The "comms" property of **remoteoffloadbin** can be set to one of the following, assuming that the underlying *comms*-type extension has been built, and that the corresponding server is running.
    * **xlink** -- Offload pipeline to a running XLink Server (**KeemBay** only).
    * **hddl** -- Offload pipeline to an HDDL2 device, via HDDLUnite.
    * **dummy** -- Only used for debug & internal development. This will offload a subpipeline as another GStreamer pipeline within the client-side running process.

## Tips & Tricks
  * **Saving Remote GStreamer Logs** -- The **remoteoffloadbin** has a property, "remote-gst-debug-log-location", which can be used to specify location to save remote log messages to. For example:
    ```
    gst-launch-1.0 videotestsrc num-buffers=300 ! remoteoffloadbin.\( remote-gst-debug-log-location="./remoteoffload.log" queue ! videoconvert  \) ! ximagesink
    ```
    In this example, the log messages are written to a file, "remoteoffload.log". If set to "stdout", log messages will be directed to STDOUT. If set to "stderr", log messages will be directed to STDERR.

  * **Using GStreamer elements that *only* reside on the remote target** -- In typical use-cases, elements that are added to the **remoteoffloadbin** reside in *both* the client's environment, as well as the remote server's environment. There may be rare cases in which there exists an element that *only* exists in the server's environment, and the user would like to specify it's usage within *remoteoffloadbin*. There is a special *helper* GStreamer element that is built & installed as part of the Remote Offload Framework that is designed to address this need. The name of this element is *sublaunch*. It exposes a property called "launch-string" that can be set to a *gst-launch-1.0*-style string. When used within a **remoteoffloadbin** it allows the user to specify an element, or *subpipeline* of elements to be added / linked during the NULL-to-READY state transition on the remote-side. Here is an example of it's usage:
    ```
    gst-launch-1.0 videotestsrc num-buffers=300 ! remoteoffloadbin.\( queue ! sublaunch launch-string="someremoteelement prop1=x prop2=y" ! videoconvert  \) ! ximagesink

  * **Passing input model & JSON files to GVA elements** -- The remote offload stack, by default, installs custom property handlers for GVA elements to aid in file transfer of required parameters. The *gvadetect*, *gvaclassify*, and *gvainference* expose a "model" property to the user, which should be set as a filesystem path to an OpenVINO model (.xml or .blob). Likewise, these elements expose another property, "model-proc", which can be set to the location of a JSON file. When GVA element(s) are added to the **remoteoffloadbin**, the underlying remote offload stack will take care of transferring the described files to the target, and setting up the remote-running GVA element(s) on behalf of the user. For example:
    ```
    gst-launch-1.0 ... ! gvadetect model=/some/user/path/model.xml model-proc=/some/user/path/file.json ...
     ```
    In the above example, the **model.xml** and **file.json** files will automatically be transferred to the target, and passed to the GVA element(s) "reconstructed" there.

    In the case where a user wants to set the "model" and/or "model-proc" properties to a file that already resides on the remote target's filesystem, they can use the prefix, **remotefilesystem:**, as a hint to the remote offload stack. For example:
    ```
    gst-launch-1.0 ... ! gvadetect model=remotefilesystem:/some/remote/path/model.xml model-proc=remotefilesystem:/some/remote/path/file.json ...
     ```

##  Running the gst-check tests
  * The gst-check tests can be run from the client-side build directory. Make sure to set GST_REMOTEOFFLOAD_DEFAULT_COMMS / GST_REMOTEOFFLOAD_DEFAULT_COMMSPARAM environment variables appropriately. You can simply run `ctest --verbose`


## Known Issues
   * Adding "decodebin" element, or other dynamic "autoplugger" elements, to a remoteoffloadbin is problematic. Since the bin is serialized & sent to the waiting remoteoffloadpipeline instance during NULL->READY pipeline state transition, it's
 dynamic pads have not been added / linked yet. Instead of decodebin, try to use actual
 elements that would have been created & added by the autoplugger. (e.g. h264parse + vaapih264dec ).

   * The following gst-check tests are known to be failing:
   (none)

## Reporting Bugs and Feature Requests
  Report bugs and requests [on the issues page](https://gitlab.devtools.intel.com/OWR/IoTG/GMS/Yocto/Graphics/Media/hddl_streamer/issues)

  When submitting bugs, please provide both host & server terminal logs with
  GST_DEBUG set to:
  ```
  export GST_DEBUG=2,remoteoffload*:4
  ```


