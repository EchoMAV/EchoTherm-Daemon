#! /bin/bash

if [[ $(id -u) != 0 ]]; then
  echo "The EchoTherm uninstallation script must be run as root"
  exit 1
fi

# exit on any error
set -e

/usr/local/bin/echothermd --kill || true
rm -f /usr/local/bin/echothermd
rm -f /usr/local/bin/echotherm
#rm -f /etc/udev/rules.d/10-seekthermal.rules
rm -rf /usr/local/include/seekframe
rm -rf /usr/local/include/seekcamera
rm -f /usr/local/lib/libseekcamera.so.4.4
rm -f /usr/local/lib/libseekcamera.so