#! /bin/bash
echo "Installing EchoTherm camera dependencies and V4L Loopback device..."
echo ""
arch=$(uname -m)
if [[ $arch == x86_64* ]]; then
    echo "Installing on x86_64 architecture"
elif [[ $arch == aarch64* ]]; then
    echo "Installing on aarch64 architecture"
else
    echo "Error: Unrecognized architecture, exiting."
    exit 1
fi
# install all the stuff you'll need (dkms, cmake, boost, gstreamer)
apt update
apt install -y dkms cmake libboost-all-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
# download the v4l2loopback stuff and place it into /usr/src
version=0.12.5
curl -L https://github.com/umlaeute/v4l2loopback/archive/v${version}.tar.gz | tar xvz -C /usr/src
# copy thermal libs and include files
if [[ $arch == x86_64* ]]; then
    cp lib/x86_64-linux-gnu/* /usr/local/lib
else
    cp lib/aarch64-linux-gnu/* /usr/local/lib
fi
cp -r include/* /usr/local/include
# copy the device rules
cp driver/udev/10-seekthermal.rules /etc/udev/rules.d
# reload your library path
ldconfig
# reload your device rules
udevadm control --reload 
# clean up the build directory if it exists
rm -rf build
mkdir build
cd build
# build the application and install it
cmake ..
make
make install
# build and install the v4l2loopback module
dkms add -m v4l2loopback -v ${version} --force 2>/dev/null
dkms build -m v4l2loopback -v ${version} --force 2>/dev/null
dkms install -m v4l2loopback -v ${version} --force 2>/dev/null
# ensure the module loads on startup
echo "v4l2loopback" > /etc/modules-load.d/v4l2loopback.conf
# clean up the v4l2loopback source
rm -rf /usr/src/v4l2loopback-${version}

# load the module
modprobe v4l2loopback

echo "Getting v4l2 device number..."
deviceId=$(ls /sys/devices/virtual/video4linux | head -n 1)
deviceNumber=${deviceId: -1}
echo "Got v4l2 device Number ${deviceNumber}"
#rename the device
#unloads the v4l2loopback to it can be renamed

modprobe v4l2loopback -r
echo "Renaming v4l2loopback to EchoTherm: Video Loopback..."
modprobe v4l2loopback video_nr=$deviceNumber card_label="EchoTherm: Video Loopback"
echo "Installation complete!"
