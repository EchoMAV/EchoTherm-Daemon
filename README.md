# EchoTherm 320 Software Package

The EchoTherm camera is a 320x240 pixel ultra-miniature and ultra-light weight thermal camera. It is ideal for use in situations where size, weight and cost are driving factors, such as small drones. The EchoTherm Software Package is a collection of utilities to to enable the use the EchoTherm camera on Linux distributions running on ARM64 or x86_64 architectures.

| Specifications                   | Description                                  |
|----------------------------------|----------------------------------------------|
| Microbolometer                   | Uncooled Vanadium Oxide                      |
| Pixel Pitch                      | 12 Microns                                   |
| Spectral Response                | 7.8 - 14 microns                             |
| Sensor Resolution (Array Format) | 320 (h) x 240 (v)                            |
| Frame Rate                       | 27 Hz                                        |
| Imaging Range                    | -40 C to 330 C                               |
| Sensor Sensitivity               | 65 mK (typical), <100 mK (max) @ 25°C        |
| Non-Uniformity Correction (NUC)  | Automatic or programmable (internal shutter) |
| Video Output Interfaces          | V4L loopback via USB                         |
| Supply Voltage                   | 3.3V to 5.0V                                 |
| Power                            | 300mW                                        |

The EchoTherm software package consists of three major components:
- The EchoTherm Daemon (echothermd)
- The EchoTherm User Application (echotherm)
- Video4Linux Loopback Device

Each component is described below.  



## Installation
To build and install, first clone this repository, then run the install script:
```
git clone https://github.com/EchoMAV/EchoTherm-Daemon.git
cd EchoTherm-Daemon
sudo ./install.sh
```
> [!IMPORTANT]  
> The installation script will place `echothermd` and `echotherm` in `/usr/local/bin`.

## Quick Start
1. Install the software per the instructions above.
1. Reboot you machine, do not you plug in the EchoTherm camera.
2. Run the echotherm daemon:
```
echothermd --daemon
```
3. Plug in the EchoTherm camera via USB
4. Verify the camera is detected via:
```
echotherm --status
```
5. List your VideoForLinux devices to identify the camera endpoint:
```
v4l2-ctl --list-devices  
```
> [!NOTE]  
> Take note of the device endpoint (e.g. `/dev/video0`) and use it in the next step as {Device id}. Depending on your machine setup, the device may be called >`Dummy video device` or `EchoTherm Loopback device`  

example response:
Dummy video device (0x0000) (platform:v4l2loopback-000):
	/dev/video0

6. View the video on your desktop:
```
gst-launch-1.0 v4l2src device={Device id} ! videoconvert ! autovideosink

example with device specfied
gst-launch-1.0 v4l2src device=/dev/video0 ! videoconvert ! autovideosink

```
7. In another terminal window, use the `echotherm` app to change camera settings, e.g. to change the color palette:
```
echotherm --colorPalette 4
```
Congrats! You now have a functional camera. Continue reading to learn more details about the echothermd and echotherm apps and other gstreamer implementations for headless/streaming applications.

## EchoTherm Daemon
current version 1.1.0

EchoTherm Daemon `echothermd` must be started before the EchoTherm camera can be used. This background process (daemon) runs continuously, manages camera connects and disconnects, and inteprepts and implements commands coming from the user application `echotherm`. It also sends video camera frames (YUY2 pixel format by default) to the Video4Linux loopback device so that the EchoTherm colorized video output can easily be ingested by common media frameworks such as [gstreamer](https://gstreamer.freedesktop.org/) and [ffmpeg](https://www.ffmpeg.org/).  

To start the echotherm daemon:
```
echothermd --daemon
```
Running `echothermd` with the `--daemon` argument will fork a background process. Because it is a background process, log information will not be avilable on the console, however log data will be written to the system log which can be viewed using either the system journal (journalctl) or at /var/log/syslog, depending if your system is journal-based or not. Journal-based systems are the most modern/common. See the instructions below for how to view the `echothermd` logs.

For journal-based OS (most common), to view `echothermd` journal logs use:
```
journalctl -t echothermd    #to view the full log
journalctl -ft echothermd   #to tail the log
```
To view syslog on non-journal-based OSes:
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
  
although echothermd --kill is the perferred manner to stop the daemon, it closes and terminates nice and not an abrupt kill
you can use a command line 
pkill -9 echothermd
Note: stopping the daemon in this manner may require you to wait for the system to terminate connections before you can restart the daemon again.

> [!TIP]
> In some applications, the user may wish to run `echothermd` as part of a system service which starts automatically upon boot. Instructions below provide guidance for how to implement `echothermd` as a service.

#### echothermd Allowed Options
`echothermd` may be started with the (optional) startup options. 
options can be stacked when initiating the daemon to set default values 

Below is an example `echothermd` command to startup the EchoTherm Daemon and set the initial Color Palette to **Hi** and the Shutter Mode to **Auto**:
```
echothermd --daemon --colorPalette 7 --shutterMode 0

hint: use the logging functions to monitor startup
```
The full list of available startup options:
v1.1.0
Allowed options:
  --help                          Produce this message
  --daemon                        Start the process as a daemon
  --kill                          Kill the existing instance
  --maxZoom arg                   Set the maximum zoom (a floating point
                                  number)
  --loopbackDeviceName arg        Choose the initial loopback device name
  --colorPalette arg              Choose the initial color palette
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
  --shutterMode arg               Choose the initial shutter mode
                                  negative = manual
                                  zero     = auto
                                  positive = number of seconds between shutter
                                  events
  --frameFormat arg               Choose the initial frame format
                                  FRAME_FORMAT_CORRECTED               = 0x04
                                  (not yet implemented)
                                  FRAME_FORMAT_PRE_AGC                 = 0x08
                                  (not yet implemented)
                                  FRAME_FORMAT_GRAYSCALE               = 0x40
                                  FRAME_FORMAT_COLOR_ARGB8888          = 0x80
                                  (default)
                                  FRAME_FORMAT_COLOR_RGB565            = 0x100
                                  (not yet implemented)
                                  FRAME_FORMAT_COLOR_AYUV              = 0x200
                                  (not yet implemented)
                                  FRAME_FORMAT_COLOR_YUY2              = 0x400
                                  (not yet implemented)
  --setRadiometricFrameFormat arg Choose the initial radiometric frame format
                                  FRAME_FORMAT_THERMOGRAPHY_FLOAT      = 0x10
                                  FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 0x20
                                  (default)
  --pipelineMode arg              Choose the initial pipeline mode
                                  PIPELINE_LITE       = 0
                                  PIPELINE_LEGACY     = 1
                                  PIPELINE_PROCESSED  = 2
                                  Note that in PIPELINE_PROCESSED, sharpen,
                                  flat scene, and gradient filters are disabled
  --sharpenFilterMode arg         Choose the initial state of the sharpen
                                  filter
                                  zero     = disabled
                                  non-zero = enabled
  --flatSceneFilterMode arg       Choose the initial state of the flat scene
                                  filter
                                  zero     = disabled
                                  non-zero = enabled
  --gradientFilterMode arg        Choose the initial state of the gradient
                                  filter
                                  zero     = disabled
                                  non-zero = enabled
```

> [!NOTE]  
> If you run `echothermd` without `--daemon` and terminate the program with `Ctrl+C`, then you should run `echotermd --kill` afterwards to clean up the `/tmp/echothermd.lock` file
echothermd --kill will indicate the status of any background processes found and terminated

## EchoTherm App 
current version 1.1.0

The `echotherm` application is how the user can interact with the camera while it is running. This allows runtime changes including color palette, shutter modes and other options shown below. The `echotherm` app communicates with `echothermd` using a socket on port 9182. 

Below is an example `echotherm` command to set the Color Palette to **Black Hot** and the Shutter Mode to **Auto**:
```
echotherm --colorPalette 1 --shutterMode 0
```
#### echotherm Allowed Options
```
ver 1.1.0
  --help                          Produce this message
  --shutter                       Trigger the shutter
  --status                        Get the status of the camera
  --startRecording arg            Begin recording to a specified file
                                  (currently only .mp4)
  --stopRecording                 Stop recording to a file
  --takeScreenshot arg            Save a screenshot of the current frame to a
                                  file
  --takeRadiometricScreenshot arg Save radiometric data to a file (name
                                  optional) else defaults to
                                  Radiometric_[UTC].csv)
  --setRadiometricFrameFormat arg Set radiometric data format
                                  THERMOGRAPHY_FIXED_10_6 = 32 (default)
                                  THERMOGRAPHY_FLOAT = 16
  --zoomRate arg                  Choose the zoom rate (a floating point
                                  number)
                                  negative = zooming out
                                  zero     = not changing zoom
                                  positive = zooming in
  --zoom arg                      Instantly set the current zoom (a floating
                                  point number)
  --maxZoom arg                   Set the maximum zoom (a floating point
                                  number)
  --getZoom                       Get a string indicating current zoom
                                  parameters
  --colorPalette arg              Choose the color palette
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
  --shutterMode arg               Choose the shutter mode
                                  negative = manual
                                  zero     = auto
                                  positive = number of seconds between shutter
                                  events
  --pipelineMode arg              Choose the pipeline mode
                                  PIPELINE_LITE       = 0
                                  PIPELINE_LEGACY     = 1
                                  PIPELINE_PROCESSED  = 2
                                  Note that in PIPELINE_PROCESSED, sharpen,
                                  flat scene, and gradient filters are disabled
  --sharpenFilterMode arg         Choose the state of the sharpen filter
                                  zero     = disabled
                                  non-zero = enabled
  --flatSceneFilterMode arg       Choose the state of the flat scene filter
                                  zero     = disabled
                                  non-zero = enabled
  --gradientFilterMode arg        Choose the state of the gradient filter
                                  zero     = disabled
                                  non-zero = enabled
```

## Video For Linux LoopBack
To identify the EchoTherm V4L Loopback device:
```
v4l2-ctl --list-devices  #find the device named EchoTherm: Video Loopback
```
example response:
Dummy video device (0x0000) (platform:v4l2loopback-000):
	/dev/video0

## Gstreamer Examples

### Example 1
The example below ingests the V4L source and displays it in a desktop window
```
gst-launch-1.0 v4l2src device={Device id} ! videoconvert ! autovideosink
```
example with device specfied
gst-launch-1.0 v4l2src device=/dev/video0 ! videoconvert ! autovideosink

### Example 2
The example below ingests the V4L source into a gstreamer pipeline and streams it to an IP address (RTP UDP) using the x264enc encoder element. This encoding is compatible with common UAV Ground Control Software packages. The pipeline below was tested on a Raspberry Pi 4 for stability. Other devices may have hardware-optimized encoders, or other software encoders which will also work. The fields `{Device id}`, `{IP Address}`, `{Port}` and `{Bitrate}' should be changed for your use case. Typical bitrates are 500-1500 kbps, and can also be changed for your use case. Generally, you can use a low bitrate (500-1000 Kbps) with excellent results because of the small 320x256 frame size.
```
gst-launch-1.0 v4l2src device={Device id} ! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast bitrate={Bitrate in Kbps} ! rtph264pay config-interval=1 pt=96 ! udpsink host={IP ADddress} port={Port} sync=false
```

## Uninstall
```
sudo ./uninstall.sh
```

## Running as a Service
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

# Video output:
```
v1.1.0 
The echotherm interface can be used to start and stop video recording
echotherm --startRecording [arg]

the argument is the filepath you wish to save the video file as (optional)
note: currently only mp4 encoding is supported so this file must have the extension of .mp4

if no file name is given, the system will automatically create a Video_UTC.mp4 is the current HOME directory

to stop the video recording you have to issue the command
echotherm --stopRecording
or
echothermd --kill

 *Warning: repeated use of this function will create multiple files, it is up to the user to clean them up!

# Screenshot:
```
v1.1.0 
The echotherm interface can be used to capture a frame of the stream to a file
echotherm --takeScreenSnapshot [arg]

the argument is the filepath you wish to save the image file as (optional)
note: jpeg is the default but other formats should be supported

if no file name is given, the system will automatically create a Frame_UTC.jpeg is the current HOME directory

 *Warning: repeated use of this function will create multiple files, it is up to the user to clean them up!


# Radiometric output:
```
v1.1.0    
Option to save Radiometric temperture data to a file
(available once echothermd has started)

This command is treated as a snapshot function, once triggered it captures one frame of data and then reset.
Output available in 2 formats
    FRAME_FORMAT_THERMOGRAPHY_FLOAT
      This generates row and column data this is in at .1f floating format ( 1 decimal place)
      It represents each pixel as degree C.
    FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 - (default)
      This generates row and column data that is in the at 10.6f format ( 6 decimal places)
      It represents each pixel as degree C = value / 64 - 40 
Thermometic data pipeline runs in parallel with frame data pipeline.
Data will be saved in a comma delimited file (csv) and can be parsed with standard programs
Usage: 
echotherm -takeRadiometricScreenshot [arg]
  arg is an optional path/file to save file to 
  if arg given as path/file a file is created with that path, filename only it will be created in the HOME directory.
     calls with same filename will overwrite existing file
  if arg not given, it will automatically create an unique file in the current user's HOME/ directory
  The default filename is Radiometric_[UTC].csv .. where UTC is data_time stamp that prevents files from overwritting themselves
  *Warning: repeated use of this function will create multiple files, it is up to the user to clean them up!

Data format:
    data in file is comma delimited text 
    presented in 3 sections:
  File Info:
    filename
    frame number
    timestamp
  Header Infomation
    technical information about frame and sensor
  Data
    Rows - horizontal data
    Cols - vertical data 
    Data representing the temperature of each pixel in deg C
    Given row by row of columns

## TO DO


