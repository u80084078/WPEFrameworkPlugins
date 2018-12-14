Bluetooth Plug-in is used to scan, pair & connect bluetooth devices such as remote control & audio devices.

# Prerequisites

    Enable the Bluetooth plug-in in wpeframework(BR2_PACKAGE_WPEFRAMEWORK_BLUETOOTH).

# Build

    1. configure
        Eg: for Rpi-3:- make raspberrypi3_wpe_ml_defconfig
    2. build
        make

# Testing

    Open Controller UI and select Bluetooth Tab.
 
# Note

    1. To enable BT in RPi-3, need modification in cmdline.txt & config.txt, and is reflected automatically in a freshbuild. If trying in an already existing build, reconfigure rpi-firmware to reflect the same.
    2. For audio playback, gst-launch is used.
       Eg: gst-launch-1.0 playbin uri=file:///song.wav audio_sink="alsasink"
