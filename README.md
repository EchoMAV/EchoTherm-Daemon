## EchoTherm Daemon Implementation

The idea here is that an EchoTherm Daemon is started which manages the camera, looks for connects/disconnects, and creates the V4L loopback device  

The user interacts with the daemon using the "echotherm" application, which communicates with the Daemon using a simple socket.  

### Daemon - echothermd.c
This is the daemon implementation to build and run  
```
gcc -o echothermd echothermd.c
./echothermd
```
this should start a background process. It will write status to the system log which can be viewed using 
```
sudo tail -f /var/log/syslog
```
To kill the daemon
```
sudo kill $(pgrep echothermd)
```

### User application - echotherm.c
This is the application the user can use to interact with the daemon. To build and run
```
gcc -o echotherm echotherm.c
./echotherm --palette 1 --shuttermode 3
```
If you'd like to be able to run this from anywhere
```
sudo mv echotherm /usr/local/bin/
# now you can run it from any directory as echotherm --palette <number> --shuttermode <number>
```

The above will send socket commands to the daemon process, and it is expected (todo) that the daemon will then interact with the camera api to make these settings apply

## TODO
- Add --help option to echotherm which explains what the pallet and shuttermode options do and what the modes mean
- Implement the camera startup, v4l2 loopback, support software, etc. into echothermd.c
- Test to make sure the v4l2 loopback is resiliant and starts up when the daemon is started and usb device is plugged/unplugged
- Implement the palette and shuttermode commands within echothermd.c using the seek api
- Add any other commands which may be relevant
