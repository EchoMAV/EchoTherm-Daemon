# EchoTherm Software Package

The EchoTherm software package consists of three major components
- The EchoTherm Daemon (echothermd)
- The EchoTherm app (echotherm)
- Video4Linux loopback device

Each component is described below.  

### Installation
To build and install, first clone this repository, then run the install script:
```
git clone https://github.com/EchoMAV/EchoTherm-Daemon.git
cd EchoTherm-Daemon
sudo ./install.sh
```
> [!IMPORTANT]  
> The installation script will place `echothermd` and `echotherm` in `/usr/local/bin`.

## EchoTherm Daemon

EchoTherm Daemon `echothermd` must be started before the EchoTherm camera can be used. This background process (daemon) runs continuously, manages camera connects and disconnects, and inteprepts and implements commands coming from the user application (echotherm). It also sends RGB camera frames to the Video4Linux loopback device so that the EchoTherm output can easily be ingested by commong media frameworks such as gstreamer and ffmpeg.  

To start the echotherm daemon:
```
echothermd
```
This will start a background process. It will write status to the system log which can be viewed at /var/log/syslog, *or* on to the journal systemd-based operating systems.  

For journal-based OS (most common), then to view `echothermd` journal logs using:
```
journalctl -t echothermd    #to view the full log
journalctl -ft echothermd   #to tail the log
```
To view syslog (non journal-based OS):
```
sudo cat /var/log/syslog      #to view the full log
sudo tail -f /var/log/syslog  #to tail the syslog
```
To kill the daemon:
```
echothermd --kill
```
> [!NOTE]  
> The daemon uses a lock file placed in `/tmp/echothermd.lock` to keep track of the daemon running or not. Typically this file is managed automatically by `echothermd`
  
> [!TIP]
> In some applications, the user may wish to run echothermd as part of a system service which starts automatically upon boot.

#### echothermd Allowed options
`echothermd` may be started with the (optional) startup options.  

Below is an example `echothermd` command to startup the EchoTherm Daemon and set the initial Color Palette to Hi and the Shutter Mode to Auto:
```
echothermd --colorPalette 7 --shutterMode 0
```
The full list of available startup options:
```
  --help                    Produce this message
  --kill                    Kill the existing instance
  --loopbackDeviceName arg  Choose the initial loopback device name (default=/dev/video0 if available on the system)
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

## EchoTherm App 

The user interacts with the daemon using the `echotherm` application, which communicates using a socket on port 9182. 

Below is an example `echotherm` command to set the Color Palette to Black Hot and the Shutter Mode to Auto:
```
echotherm --colorPalette 1 --shutterMode 0
```
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
  --shutterMode arg         Choose the shutter mode, where the ar
                            -1  = Manual (use --shutter argument for future shutter control)
                            0   = Auto (Camera will activate the shutter as needed to optimize quality)
                            > 0 = Time-based shutter, number of seconds between shutter events
  --pipelineMode arg        Choose the pipeline mode
                            PIPELINE_LITE       = 0
                            PIPELINE_LEGACY     = 1
                            PIPELINE_PROCESSED  = 2 (Recommneded)
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

## Video For Linux LoopBack
To identify the EchoTherm V4L Loopback device:
```
v4l2-ctl --list-devices  #find the device named EchoTherm: Video Loopback
```
### Using with Gstreamer
The example below ingests the V4L source into a gstreamer pipeline and streams it to an IP address (RTP UDP) using the v4l2h264enc encoder element. The pipeline below was tested on a Raspberry Pi 4. Other embeddded systems may have other hardware optimized encoders, or other software encoders. The fields `{Device id}`, `{IP Address}` and `{Port}` should be changed for your use case. The bitrate below is shown at 2000 kbps (2000000 bps), and can also be changed for your usecase.
```
gst-launch-1.0 v4l2src device={Device id} io-mode=mmap ! "video/x-raw,format=(string)I420,width=(int)320,height=(int)256,framerate=(fraction)27/1" ! v4l2h264enc extra-controls="controls,video_bitrate=2000000" ! "video/x-h264,level=(string)4.2" ! rtph264pay config-interval=1 pt=96 ! udpsink host={IP Address} port={Port} sync=false
```

## Uninstall
```
sudo ./uninstall.sh
```

## Running as a service
To run echothermd as a service on a Linux system using systemd, you need to create a systemd service unit file. This file will define how the echothermd service should be started, stopped, and managed.

1. Create the systemd service unit file: Create a new file named echothermd.service in the /etc/systemd/system/ directory.
```
sudo nano /etc/systemd/system/echothermd.service
```
2. Define the service configuration: Add the following content to the echothermd.service file. Adjust the paths and options as necessary for your specific setup.
```[Unit]
Description=EchoTherm Daemon
After=network.target

[Service]
ExecStart=/usr/local/bin/echothermd
Restart=always
User=nobody
Group=nogroup

[Install]
WantedBy=multi-user.target
```
Description: A brief description of the service.  
After: Specifies the service dependencies.  
ExecStart: The command to start the echothermd service. Adjust the path to the actual location of the echothermd binary.  
Restart: Specifies the restart policy. always means the service will be restarted if it stops.  
User and Group: The user and group under which the service will run. Adjust as necessary.  
WantedBy: Specifies the target to which this service should be added.  

3. Reload systemd to recognize the new service:
```
sudo systemctl daemon-reload
```
4. Enable the service to start on boot:
```
sudo systemctl enable echothermd
```
5. Start the service
```
sudo systemctl start echothermd
```
6. Check the status of the service:
```
sudo systemctl status echothermd
```

## TO DO
- [ ] Support thermography snapshots

