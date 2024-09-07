#! /bin/bash
SUDO=$(test ${EUID} -ne 0 && which sudo)
arch=$(uname -m)
if [[ $arch == x86_64* ]]; then
    echo "installing on x86_64 architecture"
elif [[ $arch == aarch64* ]]; then
    echo "installing on aarch64 architecture"
else
    echo "unrecognized architecture"
    exit 1
fi
# install all the stuff you'll need (dkms, cmake, boost, gstreamer)
$SUDO apt update
$SUDO apt install -y dkms cmake libboost-all-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
# download the v4l2loopback stuff and place it into /usr/src
version=0.12.5
curl -L https://github.com/umlaeute/v4l2loopback/archive/v${version}.tar.gz | tar xvz -C /usr/src
# copy thermal libs and include files
if [[ $arch == x86_64* ]]; then
    $SUDO cp lib/x86_64-linux-gnu/* /usr/local/lib
else
    $SUDO cp lib/aarch64-linux-gnu/* /usr/local/lib
fi
$SUDO cp -r include/* /usr/local/include
# copy the device rules
$SUDO cp driver/udev/10-seekthermal.rules /etc/udev/rules.d
# reload your library path
$SUDO ldconfig
# reload your device rules
$SUDO udevadm control --reload 
# clean up the build directory if it exists
$SUDO rm -rf build
$SUDO mkdir build
$SUDO cd build
# build the application and install it
cmake ..
$SUDO make
$SUDO make install
# build and install the v4l2loopback module
$SUDO dkms add -m v4l2loopback -v ${version} --force
$SUDO dkms build -m v4l2loopback -v ${version} --force
$SUDO dkms install -m v4l2loopback -v ${version} --force
# ensure the module loads on startup
$SUDO echo "v4l2loopback" > /etc/modules-load.d/v4l2loopback.conf
# clean up the v4l2loopback source
$SUDO rm -rf /usr/src/v4l2loopback-${version}

# this is not working
echo "Getting device ID"
deviceId=$(ls /sys/devices/virtual/video4linux | head -n 1)
deviceNumber=${deviceId: -1}

#rename the device
#unloads the v4l2loopback
echo "Unloading v4l2loopback"
$SUDO modprobe v4l2loopback -r
echo "Renaming v4l2loopback"
$SUDO modprobe v4l2loopback video_nr=$deviceNumber card_label="EchoTherm Video Loopback Device"
