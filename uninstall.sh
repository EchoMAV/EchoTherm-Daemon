#! /bin/bash
/usr/local/bin/echothermd --kill
rm /usr/local/bin/echothermd
rm /usr/local/bin/echotherm
#rm /etc/udev/rules.d/10-seekthermal.rules
rm -rf /usr/local/include/seekframe
rm -rf /usr/local/include/seekcamera
rm /usr/local/lib/libseekcamera.so.4.4
rm /usr/local/lib/libseekcamera.so