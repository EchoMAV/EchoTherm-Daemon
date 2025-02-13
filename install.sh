#! /bin/bash

if [[ $(id -u) != 0 ]]; then
  echo "The EchoTherm installation script must be run as root"
  exit 1
fi

package_available() { #determine if the specified package-name exists in a local repository for the current dpkg architecture
  local package="$(awk -F: '{print $1}' <<<"$1")"
  local dpkg_arch="$(awk -F: '{print $2}' <<<"$2")"
  [ -z "$dpkg_arch" ] && dpkg_arch="$(dpkg --print-architecture)"
  [ -z "$package" ] && error "package_available(): no package name specified!"
  local output="$(apt-cache policy "$package":"$dpkg_arch" | grep "Candidate:")"
  if [ -z "$output" ]; then
    return 1
  elif echo "$output" | grep -q "Candidate: (none)"; then
    return 1
  else
    return 0
  fi
}

# exit on any error
set -e

echo "Installing EchoTherm Software Package..."
echo ""
echo "Software compontents include:"
echo "echothermd - The EchoTherm Daemon which monitors for and controls the EchoTherm Camera"
echo "echotherm - The EchoTherm user application which allows changing settings such as color pallete, shutter mode, etc."
echo "V4L2 Loopback - Takes frames from the camera and generates a Video4Linux device, so the video can be viewed or used in a gstreamer/ffmpeg pipeline"
echo ""
arch=$(uname -m)
if [[ $arch == x86_64* ]]; then
    echo "Installing on x86_64 architecture..."
elif [[ $arch == aarch64* ]]; then
    echo "Installing on aarch64 architecture..."
else
    echo "Error: Unrecognized architecture, exiting."
    exit 1
fi
# install all the stuff you'll need (dkms, cmake, boost, gstreamer)
echo "Installing support files, please verify complete installation and no errors"
apt update
apt install -y \
    v4l-utils \
    curl \
    dkms \
    cmake \
    libboost-all-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-tools \
    gstreamer1.0-x \
    gstreamer1.0-alsa \
    gstreamer1.0-gl \
    gstreamer1.0-gtk3 \
    gstreamer1.0-qt5 \
    gstreamer1.0-pulseaudio \
    libopencv-dev

# Nvidia Jetpack packages newer opencv differently than Ubuntu and has a bug where libopencv-dev is missing a required dependency (a new package that does not exist in Ubuntu called libopencv). Make sure to install this package if it exists.
package_available && apt install -y libopencv

echo "Review logs to verify complete installation"
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
cmake -B . -S ..
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
echo ""
echo "Installation complete!"
echo ""
echo "To use the EchoTherm Camera, first run echothermd (EchoTherm Daemon) which will monitor for camera connections and stream the video to /dev/${deviceId}"
echo ""
echo "You then use the echotherm app to interact with the camera (change color palettes, etc.), use the echotherm app, run echotherm --help"
echo ""
echo "For more information, refer to https://https://github.com/EchoMAV/EchoTherm-Daemon/"
