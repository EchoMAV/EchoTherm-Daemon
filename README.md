## EchoTherm Daemon Implementation

The idea here is that an EchoTherm Daemon is started which manages the camera, looks for connects/disconnects, and creates the V4L loopback device  

The user interacts with the daemon using the "echotherm" application, which communicates with the Daemon using a simple socket on port 9182. 

### Installation
Run
```
sudo ./install.sh
```
The installation will place echotherd and echotherm in `/usr/local/bin` so they can be run from anywhere.

### Uninstallation
Run
```
sudo ./uninstall.sh
```

### Daemon - echothermd.c
This is the daemon implementation to run
```
echothermd
```
this should start a background process. It will write status to the system log which can be viewed using /var/log/syslog, *or* with the journal on systemd-based operating systems.  

To view syslog
```
sudo cat /var/log/syslog   # to view the full log
sudo tail -f /var/log/syslog  # to tail the syslog
```
To view journal logs
```
journalctl -t echothermd  #to view the full log
journalctl -ft echothermd   #to tail the log

To kill the daemon
```
./echothermd --kill
```
The daemon uses a lock file placed in `/tmp/echothermd.lock` to keep track of the daemon running or not  

#### echothermd Allowed options
```
  --help                    Produce this message
  --kill                    Kill the existing instance
  --loopbackDeviceName arg  Choose the initial loopback device name (default=/dev/video0)
  --colorPalette arg        Choose the initial color palette
                            COLOR_PALETTE_WHITE_HOT =  0 (default)
                            COLOR_PALETTE_BLACK_HOT =  1
                            COLOR_PALETTE_SPECTRA   =  2
                            COLOR_PALETTE_PRISM     =  3
                            COLOR_PALETTE_TYRIAN    =  4
                            COLOR_PALETTE_IRON      =  5
                            COLOR_PALETTE_AMBER     =  6
                            COLOR_PALETTE_HI        =  7
                            COLOR_PALETTE_GREEN     =  8
                            COLOR_PALETTE_USER_0    =  9
                            COLOR_PALETTE_USER_1    = 10
                            COLOR_PALETTE_USER_2    = 11
                            COLOR_PALETTE_USER_3    = 12
                            COLOR_PALETTE_USER_4    = 13
  --shutterMode arg         Choose the initial shutter mode
                            negative = manual
                            zero     = auto (default)
                            positive = number of seconds between shutter events
  --frameFormat arg         Choose the initial frame format
                            FRAME_FORMAT_CORRECTED               =  0x04
                            FRAME_FORMAT_PRE_AGC                 =  0x08
                            FRAME_FORMAT_THERMOGRAPHY_FLOAT      =  0x10
                            FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 =  0x20
                            FRAME_FORMAT_GRAYSCALE               =  0x40
                            FRAME_FORMAT_COLOR_ARGB8888          =  0x80
                            FRAME_FORMAT_COLOR_RGB565            =  0x100
                            FRAME_FORMAT_COLOR_AYUV              =  0x200
                            FRAME_FORMAT_COLOR_YUY2              =  0x400 (default)
                            
  --pipelineMode arg        Choose the initial pipeline mode
                            PIPELINE_LITE       = 0
                            PIPELINE_LEGACY     = 1
                            PIPELINE_PROCESSED  = 2 (default)
                            Note that in PIPELINE_PROCESSED, sharpen, 
                            flat scene, and gradient filters are disabled
  --sharpenFilterMode arg   Choose the initial state of the sharpen filter
                            zero     = disabled (default)
                            non-zero = enabled
  --flatSceneFilterMode arg Choose the initial state of the flat scene filter
                            zero     = disabled (default)
                            non-zero = enabled
  --gradientFilterMode arg  Choose the initial state of the gradient filter
                            zero     = disabled (default)
                            non-zero = enabled
```


### User application - echotherm.c
This is the application the user can use to interact with the daemon. To build and run
```
g++ -o echotherm echotherm.c
./echotherm --colorPalette 1 --shutterMode 3
```
If you'd like to be able to run this from anywhere
```
sudo mv echotherm /usr/local/bin/
# now you can run it from any directory as echotherm --colorPalette <number> --shutterMode <number>
```

The above will send socket commands to the daemon process, and it is expected (todo) that the daemon will then interact with the camera api to make these settings apply

#### echotherm Allowed options
```
  --help                    Produce this message
  --shutter                 Trigger the shutter
  --colorPalette arg        Choose the color palette
                            COLOR_PALETTE_WHITE_HOT =  0
                            COLOR_PALETTE_BLACK_HOT =  1
                            COLOR_PALETTE_SPECTRA   =  2
                            COLOR_PALETTE_PRISM     =  3
                            COLOR_PALETTE_TYRIAN    =  4
                            COLOR_PALETTE_IRON      =  5
                            COLOR_PALETTE_AMBER     =  6
                            COLOR_PALETTE_HI        =  7
                            COLOR_PALETTE_GREEN     =  8
                            COLOR_PALETTE_USER_0    =  9
                            COLOR_PALETTE_USER_1    = 10
                            COLOR_PALETTE_USER_2    = 11
                            COLOR_PALETTE_USER_3    = 12
                            COLOR_PALETTE_USER_4    = 13
  --shutterMode arg         Choose the shutter mode
                            negative = manual
                            zero     = auto
                            positive = number of seconds between shutter events
  --pipelineMode arg        Choose the pipeline mode
                            PIPELINE_LITE       = 0
                            PIPELINE_LEGACY     = 1
                            PIPELINE_PROCESSED  = 2
                            Note that in PIPELINE_PROCESSED, sharpen, 
                            flat scene, and gradient filters are disabled
  --sharpenFilterMode arg   Choose the state of the sharpen filter
                            zero     = disabled
                            non-zero = enabled
  --flatSceneFilterMode arg Choose the state of the flat scene filter
                            zero     = disabled
                            non-zero = enabled
  --gradientFilterMode arg  Choose the state of the gradient filter
                            zero     = disabled
                            non-zero = enabled
```


## TO DO
- [ ] Support thermography
- [ ] Support ability to change loopbackDeviceName and frameFormat mid-stream
- [ ] Add any other commands which may be relevant
