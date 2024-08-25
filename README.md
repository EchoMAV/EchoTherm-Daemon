## EchoTherm Daemon Implementation

The idea here is that an EchoTherm Daemon is started which manages the camera, looks for connects/disconnects, and creates the V4L loopback device  

The user interacts with the daemon using the "echotherm" application, which communicates with the Daemon using a simple socket.  

### Damon - echothermd.c
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
The above will send socket commands to the daemon process, and it is expected (todo) that the daemon will then interact with the camera api to make these settings apply

