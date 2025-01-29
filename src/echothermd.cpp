#include <netinet/in.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <csignal>
#include <cstring>
#include <charconv>
#include <iostream>
#include <regex>
#include <filesystem>
#include <signal.h>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "EchoThermCamera.h"

namespace
{
    // Note: defaults can be overriden from command line, so they cant be const
    static std::string n_defaultlLoopbackDeviceName("/dev/video0");
    static auto n_defaultColorPalette = 0;        // COLOR_PALETTE_WHITE_HOT
    static auto n_defaultShutterMode = 0;         // SHUTTER_MODE_AUTO
    static auto n_defaultFrameFormat = 0x80; // FRAME_FORMAT_COLOR_ARGB8888
    static auto n_defaultRadiometricFrameFormat = 0x20; // FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6
        //note: frameFormat and radiometricFormat or "or"ed to become the activeFormat when camera started
    static auto n_defaultSharpenFilterMode = 0;   // DISABLED
    static auto n_defaultGradientFilterMode = 0;  // DISABLED
    static auto n_defaultFlatSceneFilterMode = 0; // DISABLED
    static auto n_defaultPipelineMode = 2;        // PIPELINE_PROCESSED
    static auto n_defaultMaxZoom = 16.0;

    constexpr static inline auto const n_bufferSize = 1024;
    constexpr static inline auto const np_lockFile = "/tmp/echothermd.lock";
    constexpr static inline auto const np_logName = "echothermd";
    constexpr static inline auto const n_port = 9182;
    constexpr static inline auto const n_maxEpollEvents = 10;
    volatile bool n_running = true;
    bool n_isDaemon = false;

    constexpr static inline int np_catchTheseSignals[]{
        // SIGABRT,
        // SIGFPE,
        // SIGILL,
        // SIGINT,
        // SIGQUIT,
        // SIGSEGV,
        SIGTERM,
        // SIGKILL, // can not be caught blocked or ignored
        // SIGTSTP,
    };

    std::unique_ptr<EchoThermCamera> np_camera;

    std::string _desanitizeString(std::string const &input)
    {
        std::string output = input;
        if (output.size() >= 3)
        {
            std::regex r("%[0-9A-F]{2}");
            size_t dynamicLength = output.size() - 2;
            for (size_t i = 0; i < dynamicLength; ++i)
            {
                std::string haystack = output.substr(i, 3);
                std::smatch sm;
                if (std::regex_match(haystack, sm, r))
                {
                    haystack = haystack.replace(0, 1, "0x");
                    std::string const rc = {(char)std::stoi(haystack, nullptr, 16)};
                    output = output.replace(std::begin(output) + i, std::begin(output) + i + 3, rc);
                }
                dynamicLength = output.size() - 2;
            }
        }
        return output;
    }

    std::errc _parseInt(std::string str, int *p_int)
    {
        // first, trim the string
        // then try to parse as an int
        // if that fails, parse it as a boolean and convert to an int
        boost::trim(str);
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), *p_int);
        if (ec != std::errc{})
        {
            std::transform(std::begin(str), std::end(str), std::begin(str), ::tolower);
            std::istringstream is(str);
            bool b;
            is >> std::boolalpha >> b;
            if (is.good())
            {
                *p_int = (int)b;
                ec = std::errc{};
            }
            else
            {
                ec = std::errc::invalid_argument;
            }
        }
        return ec;
    }

    std::errc _parseDouble(std::string str, double *p_double)
    {
        // first, trim the string
        // then try to parse as an double
        boost::trim(str);
        try
        {
            *p_double = boost::lexical_cast<double>(str);
        }
        catch(const std::exception& e)
        {
            return std::errc::invalid_argument;
        }
        return std::errc{};

        //removed because it didn't work on ubuntu 20
        //auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), *p_double);
        //return ec;
    }

    void _handleSignal(int signal)
    {
        syslog(LOG_NOTICE, "Received signal(%d) ", signal);
        switch( signal ){   
            case SIGTERM:
                syslog(LOG_NOTICE, "Terminate process...");         
                if( np_camera ){
                    syslog(LOG_NOTICE, "closing session...");
                   np_camera->_closeSession();
                }
                n_running = false;
                // we should exit normally/nice othewise kill will be called to force it

                // remove the lockfile 
                remove(np_lockFile);
                closelog();
                break;
            default:
                syslog(LOG_NOTICE, "Warning: Unhandled signal");
        }      
    }

    bool _setSignalAction()
    {
        auto returnVal = true;
        for (auto signal : np_catchTheseSignals)
        {
            struct sigaction signalAction
            {                
            };

            std::memset(&signalAction, 0, sizeof(signalAction));
            signalAction.sa_handler = _handleSignal;
            sigemptyset(&signalAction.sa_mask);
            if (sigaction(signal, &signalAction, nullptr) == -1)
            {
                std::cout << "sigaction failed for signal " << signal << std::endl;
                syslog(LOG_ERR, "sigaction failed for signal %d: %m", signal);
                returnVal = false;
                break;
            }
        }
        return returnVal;
    }

    bool _checkLock()
    {
        auto returnVal = true;
        // first try to access the lock file
        if (access(np_lockFile, F_OK) != -1)
        {
            // File exists
            returnVal = false;
        }
        // try to create the lock file
        else if (auto const lockFileDescriptor = open(np_lockFile, O_CREAT | O_TRUNC | O_WRONLY, 0644); lockFileDescriptor == -1)
        {
            // can't create the lock file
            syslog(LOG_ERR, "Unable to create the lock file %s: %m", np_lockFile);
            returnVal = false;
        }
        else
        {
            // close the lock file
            close(lockFileDescriptor);
        }
        return returnVal;
    }

    bool _isPortAvailable(int const port)
    {
        auto returnVal = true;
        // open a socket
        auto const socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
        if (socketFileDescriptor == -1)
        {
            syslog(LOG_ERR, "Unable to create socket with port = %d: %m", port);
            returnVal = false;
        }
        else
        {
            syslog(LOG_NOTICE, "Opening socket with port = %d. socketFileDescriptor=%d", port, socketFileDescriptor);
            struct sockaddr_in socketAddress;
            std::memset(&socketAddress, 0, sizeof(socketAddress));
            socketAddress.sin_family = AF_INET;
            socketAddress.sin_addr.s_addr = INADDR_ANY;
            socketAddress.sin_port = htons(port);
            // bind the socket to an address that accepts all incoming messages
            if (bind(socketFileDescriptor, (struct sockaddr *)&socketAddress, sizeof(socketAddress)) == -1 && errno != EADDRINUSE)
            {
                syslog(LOG_ERR, "Unable to bind socket with port = %d: %m", port);
                returnVal = false;
            }
        }
        close(socketFileDescriptor);
        return returnVal;
    }

    // standard daemon creator
    // creates a fork of a fork to create a grandchild that is isolated from terminal
    // the caller and child processes are terminated leaving the orphaned process to run in background
    int _startDaemon()
    {
       // First fork
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Failed to fork: %s", strerror(errno));
            return -1;
        }
        if (pid > 0) {
            // Parent process
            return 1; // should return to exit program, we indicate parent returned
                      // this kills the initial process that loads the daemon
        }
        // Create new session
        if (setsid() < 0) {
            syslog(LOG_ERR, "Failed to create new session: %s", strerror(errno));
            return -1;
        }
        // Second fork
        pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Failed second fork: %s", strerror(errno));
            return -1;
        }
        if (pid > 0) {
            // Parent of second fork
            // exit(2) ; // we return and let the main catch this and exits process without continuing
            return 2;    
        }
        // the grandchild is the one we want to continue, totally detacted from console
        // Set new file permissions
        umask(0);

        // Change working directory
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "Failed to change directory: %s", strerror(errno));
            return -1;
        }
        // Close and redirect file descriptors
        for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
            close(x);
        }

        // Redirecting Standard File Descriptors (STDIN, STDOUT, STDERR) to /dev/null
        int fd = open("/dev/null", O_RDWR);
        if (fd < 0) {
            syslog(LOG_ERR, "Failed to open /dev/null: %s", strerror(errno));
            return -1;
        }

        dup2(fd, STDIN_FILENO); //Prevents reading from an invalid input.
        dup2(fd, STDOUT_FILENO);///Prevents printing unwanted messages to the terminal.
        dup2(fd, STDERR_FILENO);//Prevents logging errors to the terminal
        if (fd > STDERR_FILENO) {  //it's closed to prevent file descriptor leaks.
            close(fd);
        }
        return 0;
    }

    std::string _parseCommand(char const *const p_command)
    {
        // Tokenize the command string
        std::string response = "";
        if (auto const *p_token = strtok(const_cast<char *>(p_command), " "); p_token)
        {
            // Check for specific commands and extract numbers
            if (strcmp(p_token, "SHUTTER") == 0)
            {
                if( np_camera ){
                    syslog(LOG_NOTICE, "SHUTTER");
                    np_camera->triggerShutter();
                }
                else{
                    syslog(LOG_ERR, "Unable to trigger shutter: camera object does not exist");
                }
            }
            else if (strcmp(p_token, "MAXZOOM") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_ERR, "MAXZOOM command received, but no number was provided.");
                }
                else
                {
                    double number = 0.0;
                    auto errorCode = _parseDouble(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {                        
                        syslog(LOG_ERR, "MAXZOOM cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "MAXZOOM cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "set MAXZOOM: %f", number);
                            np_camera->setMaxZoom(number);                       
                        }
                        else{ // if camera not created yet, it sets the default to start with
                            syslog(LOG_INFO, "Set default MaxZoom: %.2f" , number);
                            n_defaultMaxZoom = number;
                        }
                    }
                }
            }
            else if (strcmp(p_token, "ZOOMRATE") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_ERR, "ZOOMRATE command received, but no number was provided.");
                }
                else
                {
                    double number = 0.0;
                    auto errorCode = _parseDouble(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "ZOOMRATE cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "ZOOMRATE cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera){
                            syslog(LOG_NOTICE, "set ZOOMRATE: %f", number);
                            np_camera->setZoomRate(number);
                        }
                        else{
                            syslog(LOG_INFO, "Set default ZoomRate: %.2f" , number);
                        }
                    }
                }
            }
            else if (strcmp(p_token, "ZOOM") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "ZOOM command received, but no number was provided.");
                }
                else
                {
                    double number = 0.0;
                    auto errorCode = _parseDouble(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "ZOOM cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "ZOOM cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "set ZOOM: %f", number);
                            np_camera->setZoom(number);
                        }
                        else{
                            syslog(LOG_ERR, "Unable to get zoom: camera object does not exist");            
                        }
                    }
                }
            }
            else if (strcmp(p_token, "GETZOOM") == 0)
            {
                if( np_camera ){
                    syslog(LOG_NOTICE, "GETZOOM");
                    response = np_camera->getZoom();
                }
                else{
                    syslog(LOG_ERR, "Unable to get zoom: camera object does not exist");
                }
            }
            else if (strcmp(p_token, "STATUS") == 0)
            {
                if( np_camera ){
                    syslog(LOG_NOTICE, "STATUS");
                    response = np_camera->getStatus();
                }
                else{
                    syslog(LOG_ERR, "Unable to get status: camera object does not exist");
                }
            }
            else if (strcmp(p_token, "PALETTE") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "PALETTE command received, but no number was provided.");
                }
                else
                {
                    int number = 0;
                    auto errorCode = _parseInt(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "PALETTE cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "PALETTE cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "set PALETTE: %d", number);
                            np_camera->setColorPalette(number);
                        }
                        else{
                            syslog(LOG_INFO, "Set default Palette: %d" , number);
                            n_defaultColorPalette = number;
                        }
                    }
                }
            }
            else if (strcmp(p_token, "SHUTTERMODE") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "SHUTTERMODE command received, but no number was provided.");
                }
                else
                {
                    int number = 0;
                    auto errorCode = _parseInt(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "SHUTTERMODE cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "SHUTTERMODE cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "SHUTTERMODE: %d", number);
                            np_camera->setShutterMode(number);
                        }
                        else{
                            syslog(LOG_INFO, "Set default ShutterMode: %d" , number);
                            n_defaultShutterMode = number;        
                        }
                    }
                }
            }
            else if (strcmp(p_token, "PIPELINEMODE") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "PIPELINEMODE command received, but no number was provided.");
                }
                else
                {
                    int number = 0;
                    auto errorCode = _parseInt(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "PIPELINEMODE cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "PIPELINEMODE cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "PIPELINEMODE: %d", number);
                            np_camera->setPipelineMode(number);
                        }
                        else{
                            syslog(LOG_INFO, "Set default pipelineMode: %d" , number);
                            n_defaultPipelineMode = number; 
                        }
                    }
                }
            }
            else if (strcmp(p_token, "SHARPEN") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "SHARPEN command received, but no number was provided.");
                }
                else
                {
                    int number = 0;
                    auto errorCode = _parseInt(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "SHARPEN cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "SHARPEN cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "SHARPEN: %d", number);
                            np_camera->setSharpenFilter(number);
                        }
                        else{
                            syslog(LOG_INFO, "Set default sharpenFilter: %d" , number);
                            n_defaultSharpenFilterMode = number;
                        }
                    }
                }
            }
            else if (strcmp(p_token, "FLATSCENE") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "FLATSCENE command received, but no number was provided.");
                }
                else
                {
                    int number = 0;
                    auto errorCode = _parseInt(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "FLATSCENE cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "FLATSCENE cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "set FLATSCENE: %d", number);
                            np_camera->setFlatSceneFilter(number);
                        }
                        else{
                            syslog(LOG_INFO, "Set default flatSceneMode: %d" , number);
                            n_defaultFlatSceneFilterMode = number;
                        }
                    }
                }
            }
            else if (strcmp(p_token, "GRADIENT") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "GRADIENT command received, but no number was provided.");
                }
                else
                {
                    int number = 0;
                    auto errorCode = _parseInt(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "GRADIENT cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "GRADIENT cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "set GRADIENT: %d", number);
                            np_camera->setGradientFilter(number);
                        }
                        else{
                            syslog(LOG_INFO, "Set default gadientFilterMode: %d" , number);
                            n_defaultGradientFilterMode = number;
                        }
                    }
                }
            }
            else if (strcmp(p_token, "STARTRECORDING") == 0)
            {
                bool hasFilePath = true;
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_INFO, "STARTRECORDING command received, but no file path was specified.");
                    syslog(LOG_INFO, "will use the default home path and filename.");
                    hasFilePath = false;
                }
                if( np_camera ){
                    if( !hasFilePath ){
                        const char* home = std::getenv("HOME");
                        if( home ){
                            std::filesystem::path home_dir(home); 
                            auto now = std::chrono::system_clock::now();
                            auto utc_time = std::chrono::system_clock::to_time_t(now);
                            std::stringstream ss;
                            ss << "Video_" << std::put_time(std::gmtime(&utc_time), "%Y_%m_%d_%H_%M_%S") << ".mp4";
                            std::filesystem::path file_path = home_dir / ss.str();
                            syslog(LOG_INFO, "saving to: %s" , file_path.string().c_str() );
                            response = np_camera->startRecording(file_path);
                        }
                    }
                    else{
                        std::filesystem::path filePath = _desanitizeString(p_token);
                        syslog(LOG_INFO, "using: %s", filePath.string().c_str());
                        response = np_camera->startRecording(filePath);                   
                    }
                }
                else{
                    syslog(LOG_ERR, "Unable to start recording: camera object does not exist");
                }
            }
            else if (strcmp(p_token, "STOPRECORDING") == 0)
            {
                if( np_camera ){
                    syslog(LOG_NOTICE, "STOPRECORDING");
                    response=np_camera->stopRecording();
                }
                else{
                    syslog(LOG_ERR, "Unable to stop recording: camera object does not exist");
                }
            }
            else if (strcmp(p_token, "TAKESCREENSHOT") == 0)
            {
                bool hasFilePath = true;
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_INFO, "TAKESCREENSHOT command received, but no file path was specified.");
                    syslog(LOG_INFO, "will use the default home path and filename.");
                    hasFilePath = false;
                }
                if( np_camera ){
                    if( !hasFilePath ){
                        const char* home = std::getenv("HOME");
                        if( home ){
                            std::filesystem::path home_dir(home); 
                            auto now = std::chrono::system_clock::now();
                            auto utc_time = std::chrono::system_clock::to_time_t(now);
                            std::stringstream ss;
                            ss << "Frame_" << std::put_time(std::gmtime(&utc_time), "%Y_%m_%d_%H_%M_%S") << ".jpeg";
                            std::filesystem::path file_path = home_dir / ss.str();
                            syslog(LOG_INFO, "saving to: %s" , file_path.string().c_str() );
                            response = np_camera->takeScreenshot(file_path);
                        }
                    }
                    else{
                        std::filesystem::path filePath = _desanitizeString(p_token);
                        syslog(LOG_INFO, "using: %s", filePath.string().c_str());
                        response = np_camera->takeScreenshot(filePath);                   
                    }
                }
                else{
                    syslog(LOG_ERR, "Unable to take sceen shot: camera object does not exist");
                }
                
            }
            else if (strcmp(p_token, "SETRADIOMETRICFRAMEFORMAT") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "SETRADIOMETRICFRAMEFORMAT command received, but no format specified.");
                }
                else
                {
                    int format = 0;
                    auto errorCode = _parseInt(p_token, &format);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "FORMAT cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "FORMAT cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        if( np_camera ){
                            syslog(LOG_NOTICE, "SETRADIOMETRICFRAMEFORMAT: %d", format );
                            np_camera->setRadiometricFrameFormat(format);
                        }
                        else{
                            syslog(LOG_INFO, "Set default radiometicFrameFormat: %d" , format);
                            n_defaultRadiometricFrameFormat = format;
                        }
                    }
                }
            }
            else if (strcmp(p_token, "TAKERADIOMETRICSCREENSHOT") == 0)
            {
                std::filesystem::path filePath;           
                // Get the next token (possible filename)
                p_token = strtok(nullptr, " ");   
                if (p_token == nullptr || strlen(p_token) == 0)
                {
                    syslog(LOG_NOTICE, "TAKERADIOMETRICSCREENSHOT: No file path specified, will use RadiometricData_UTC as default");
                    filePath = "";  // emplty will force default filename to be RadiometricData_[UTC].csv
                }
                else
                {
                    filePath = _desanitizeString(p_token);
                    syslog(LOG_NOTICE, "TAKERADIOMETRICSCREENSHOT: File path set to %s", filePath.string().c_str());
                }
                if( np_camera ){
                    response = np_camera->takeRadiometricScreenshot(filePath);
                }
                else{
                    syslog(LOG_ERR, "Unable to take radiometric screen shot: camera object does not exist");
                }
            }           
            else if (strcmp(p_token, "FORMAT") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "FORMAT command received, but no number was provided.");
                }
                else
                {
                    int number = 0;
                    auto errorCode = _parseInt(p_token, &number);
                    if (errorCode == std::errc::invalid_argument)
                    {
                        syslog(LOG_ERR, "FORMAT cannot be set to %s because it is not a number.", p_token);
                    }
                    else if (errorCode == std::errc::result_out_of_range)
                    {
                        syslog(LOG_ERR, "FORMAT cannot be set to %s because it is out of range.", p_token);
                    }
                    else
                    {
                        syslog(LOG_NOTICE, "FORMAT: %d", number);
                        if( np_camera ){
                            //TODO 
                            //Not supported because of crashing issues, we can still set in daemon as default
                            //np_camera->setFrameFormat(number);
                        }
                        else{
                            syslog(LOG_INFO, "Set default frameFormat: %d" , number);
                            n_defaultFrameFormat = number;
                        }
                    }
                }
            }
            else if (strcmp(p_token, "LOOPBACKDEVICENAME") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "LOOPBACKDEVICENAME command received, but no string was provided.");
                }
                else
                {
                    if( np_camera ){
                        //TODO 
                        //Not supported because of crashing issues, we can still set in daemon as default
                        //np_camera->setLoopbackDeviceName(p_token);
                    }
                    else{
                        syslog(LOG_INFO, "Set default loopbackDevicename: %s" , p_token);
                        n_defaultlLoopbackDeviceName = p_token;
                    }
                }
            }
            else
            {
                syslog(LOG_ERR, "Unknown command: %s", p_token);
            }
        }
        return response;
    }

    bool _setNonBlocking(int const socketFileDescriptor)
    {
        // set the socket to not block
        auto returnVal = true;
        auto const flags = fcntl(socketFileDescriptor, F_GETFL, 0);
        if (flags == -1)
        {
            syslog(LOG_NOTICE, "fcntl get: %m");
            returnVal = false;
        }
        else if (fcntl(socketFileDescriptor, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            syslog(LOG_NOTICE, "fcntl set: %m");
            returnVal = false;
        }
        return returnVal;
    }

    void _handleClient(int clientFileDescriptor)
    {
        // receive commands delimeted by "|", tokenize commands and then parse each command/value pair
        constexpr auto const *const p_delimiter = "|";
        char p_buffer[n_bufferSize];
        int valRead = read(clientFileDescriptor, p_buffer, n_bufferSize);
        if (valRead > 0)
        {
            const char *p_commands[n_maxEpollEvents] = {nullptr};
            int commandCount = 0;
            p_buffer[valRead] = '\0';
            char *p_input = nullptr;
            do
            {
                p_input = strdup(p_buffer);
                auto *p_token = strtok(p_input, p_delimiter);
                while (p_token && commandCount < n_maxEpollEvents)
                {
                    p_commands[commandCount++] = strdup(p_token);
                    p_token = strtok(nullptr, p_delimiter);
                }
                for (int commandIndex = 0; commandIndex < commandCount; ++commandIndex)
                {
                    std::string const response = _parseCommand(p_commands[commandIndex]);
                    if (!response.empty())
                    {
                        char const *const p_response = response.c_str();
                        send(clientFileDescriptor, p_response, strlen(p_response), 0);
                    }
                }
            } while (false);
            if (p_input)
            {
                free(p_input);
                p_input = nullptr;
            }
            for (int commandIndex = 0; commandIndex < commandCount; ++commandIndex)
            {
                if (p_commands[commandIndex])
                {
                    free((void *)p_commands[commandIndex]);
                    p_commands[commandIndex] = nullptr;
                }
            }
        }
        else if (valRead == 0)
        {
            close(clientFileDescriptor);
        }
        else
        {
            syslog(LOG_ERR, "unable to read client commands: %m");
        }
    }

    bool _initializeCamera(boost::program_options::variables_map const &vm)
    {
        syslog(LOG_NOTICE, "Initialize camera, startup parameters...");
        std::string loopbackDeviceName = n_defaultlLoopbackDeviceName;
        if( loopbackDeviceName.size() ==0 ){
            syslog(LOG_ERR, "no loopback name defined");
            return false ;
        }
        int colorPalette = n_defaultColorPalette;
        int shutterMode = n_defaultShutterMode;
        int frameFormat = n_defaultFrameFormat;
        int radiometricFrameFormat = n_defaultRadiometricFrameFormat; 
        // note: frameFormat and radiometricFormat are "or"ed to become the activeFormat in Camera start
        int pipelineMode = n_defaultPipelineMode;
        int sharpenFilterMode = n_defaultSharpenFilterMode;
        int gradientFilterMode = n_defaultGradientFilterMode;
        int flatSceneFilterMode = n_defaultFlatSceneFilterMode;
        double maxZoom = n_defaultMaxZoom;

        // Note: overrides for defaults were set by the parser in main  

        syslog(LOG_NOTICE, "loopbackDeviceName = %s", loopbackDeviceName.c_str());
        syslog(LOG_NOTICE, "colorPalette = %d", colorPalette);
        syslog(LOG_NOTICE, "maxZoom = %f", maxZoom);
        syslog(LOG_NOTICE, "shutterMode = %d", shutterMode);
        syslog(LOG_NOTICE, "frameFormat = %d (0x%X)", frameFormat,frameFormat);
        syslog(LOG_NOTICE, "radiometricFrameFormat = %d (0x%X)", radiometricFrameFormat,radiometricFrameFormat);
        syslog(LOG_NOTICE, "pipelineMode = %d", pipelineMode);
        syslog(LOG_NOTICE, "sharpenFilterMode = %d", sharpenFilterMode);
        syslog(LOG_NOTICE, "gradientFilterMode = %d", gradientFilterMode);
        syslog(LOG_NOTICE, "flatSceneFilterMode = %d", flatSceneFilterMode);

        np_camera = std::make_unique<EchoThermCamera>();
        if( np_camera == nullptr ){
           syslog(LOG_ERR, "Unable to create camera object"); 
           return false;
        }
        np_camera->setLoopbackDeviceName(loopbackDeviceName);
        np_camera->setColorPalette(colorPalette);
        np_camera->setShutterMode(shutterMode);
        np_camera->setFrameFormat(frameFormat);
        np_camera->setRadiometricFrameFormat(radiometricFrameFormat);
        np_camera->setPipelineMode(pipelineMode);
        np_camera->setSharpenFilter(sharpenFilterMode);
        np_camera->setGradientFilter(gradientFilterMode);
        np_camera->setFlatSceneFilter(flatSceneFilterMode);
        np_camera->setMaxZoom(maxZoom);

        syslog(LOG_NOTICE, "Starting camera...");
        return np_camera->start();
    }
}

int is_process_running(pid_t pid) {
    if (kill(pid, 0) == 0) {
        return 1; // Process is running
    } else if (errno == ESRCH) {
        return 0; // Process is not running
    } else {
        return -1; // Error occurred
    }
}

// Function to get all child PIDs recursively
void getChildProcesses(int parent_pid, std::vector<int>& children) {
    char command[50];
    snprintf(command, sizeof(command), "pgrep -P %d", parent_pid);

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        std::cerr << "Failed to run pgrep" << std::endl;
        return;
    }

    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        int child_pid = std::stoi(buffer);
        children.push_back(child_pid);
        getChildProcesses(child_pid, children);  // Recursively get sub-children
    }
    pclose(pipe);
}

int terminate_process(pid_t pid, int waitSeconds) {
    if (kill(pid, SIGTERM) == -1) {
        return 2;  // Failed to send SIGTERM
    }
    time_t start_time = time(NULL);
    int elapsedTime = 0;
    while ( elapsedTime < waitSeconds ) {
        std::cout << "\r - (" << (waitSeconds - elapsedTime) << ")" << std::flush;
        int result = is_process_running(pid);
        if (result == 0) {
            return 0;  // Process not running, success
        } 
        else{
            if (result == -1) {
               return 3;  // Error occurred
            }
            // else running
        }        
        sleep(1);
        elapsedTime = time(NULL) - start_time;
    }
    return 1;  // Timeout occurred
}

int kill_process(pid_t pid, int waitSeconds) {
    
    if (kill(pid, SIGKILL) == -1) {
        return 2;  // Failed to send SIGKILL
    }
    time_t start_time = time(NULL);
    int elapsedTime = 0;
    while ( elapsedTime < waitSeconds) {
        std::cout << "\r - (" << (waitSeconds - elapsedTime) << ")" << std::flush;
        int result = is_process_running(pid); 
        if (result == 0) {
            return 0;  // Process terminated, success
        } 
        else{ 
            if (result == -1) {
              return 3;  // Error occurred
            }
            // else still running
        }
        sleep(1);
        elapsedTime = time(NULL) - start_time;
    }
    return 1;  // Timeout occurred
}

int killOtherInstances(const char* processName) {

    std::vector<int> pids;
    char buffer[128];
    pid_t thisPid = getpid();

    // Step 1: Get all PIDs of the process using 'pgrep'
    FILE* pipe = popen(("pgrep " + std::string(processName)).c_str(), "r");
    if (!pipe) {
        std::cerr << "Failed to run pgrep" << std::endl;
        return 0;
    }

    // Step 2: Read PIDs from the command output
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        int pid = std::stoi(buffer);
        if( pid != thisPid ){ // all but this one
            pids.push_back(pid);
        }
    }
    pclose(pipe);

    if( pids.size() == 0 ){
        std::cout << "No other echothermd() processes found to kill" << std::endl ;
        return 0 ;
    }
    if( pids.size() == 1){
        std::cout << "Killing echothermd(), please wait..." << std::endl;
    }
    else{
        std::cout << "Killing (" << pids.size() << ") echothermd() processes, please wait..." << std::endl;
    }

    // Iterate through the list and kill other instances
    for (int pid : pids) {

        std::vector<int> child_pids;
        getChildProcesses(pid, child_pids);

        for (int child_pid : child_pids) {
            if( child_pid == thisPid ) continue ; // dont worry about ourself, we exit when complete
            std::cout << "Terminating child process(" << child_pid << ")\n";
            int result = terminate_process(child_pid,15); // wait up to 15 secs          
            if( result != 0 ){// timeout or other error, just kill it
                std::cout << " - Terminate request failed(" << result << ")\n" << std::flush;
                std::cout << "Kill child process:\n" << std::flush;
                result = kill_process(child_pid,5); // wait 5
                if( result != 0){
                    std::cout << "\r - Error " << std::endl << std::flush; // not the flush was added to get \r working correctly
                }
                else{
                    std::cout << "\r - Success" << std::endl << std::flush;
                } 
            }
            else{
                std::cout << "\r - Success" << std::endl << std::flush;
            }
        }

        std::cout << "Terminating process(" << pid << ")\n" << std::flush;;    
        int result = terminate_process(pid,15); // wait up to 15 secs          
        if( result != 0 ){// timeout or other error, just kill it
            std::cout << " - Terminate request failed(" << result << ")\n" << std::flush;;
            std::cout << "Kill process:\n" << std::flush;
            result = kill_process(pid,5); // wait 5 secs 
            if( result != 0){
                std::cout << "\r - Error(" << result << ")\n" << std::flush;                            }
            else{
                std::cout << "\r - Success" << std::endl << std::flush;;
            }    
        }   
        else{
            std::cout << "\r - Success" << std::endl << std::flush;;
        } 
    }
    std::cout << std::endl;

    return 0; // return value is not significant we are terminating anyway
}

int main(int argc, char *argv[])
{  
    int clientFileDescriptor = -1;
    int epollFileDescriptor = -1;
    int serverFileDescriptor = -1;
    int returnCode = EXIT_SUCCESS;
    
    bool isDaemonProcess = false;
    do
    {
        // Open the log file
        openlog(np_logName, LOG_PID, LOG_DAEMON);

        boost::program_options::options_description desc("Allowed options");
        desc.add_options()("help", "Produce this message");
        desc.add_options()("daemon", "Start the process as a daemon");
        desc.add_options()("kill", "Kill the existing instance");

        desc.add_options()("maxZoom", boost::program_options::value<std::string>(),
                           "Set the maximum zoom (a floating point number)");
        desc.add_options()("loopbackDeviceName", boost::program_options::value<std::string>(),
                           "Choose the initial loopback device name");
        desc.add_options()("colorPalette", boost::program_options::value<std::string>(),
                           "Choose the initial color palette\n"
                           "COLOR_PALETTE_WHITE_HOT =  0\n"
                           "COLOR_PALETTE_BLACK_HOT =  1\n"
                           "COLOR_PALETTE_SPECTRA   =  2\n"
                           "COLOR_PALETTE_PRISM     =  3\n"
                           "COLOR_PALETTE_TYRIAN    =  4\n"
                           "COLOR_PALETTE_IRON      =  5\n"
                           "COLOR_PALETTE_AMBER     =  6\n"
                           "COLOR_PALETTE_HI        =  7\n"
                           "COLOR_PALETTE_GREEN     =  8\n"
                           "COLOR_PALETTE_USER_0    =  9\n"
                           "COLOR_PALETTE_USER_1    = 10\n"
                           "COLOR_PALETTE_USER_2    = 11\n"
                           "COLOR_PALETTE_USER_3    = 12\n"
                           "COLOR_PALETTE_USER_4    = 13");
        desc.add_options()("shutterMode", boost::program_options::value<std::string>(),
                           "Choose the initial shutter mode\n"
                           "negative = manual\n"
                           "zero     = auto\n"
                           "positive = number of seconds between shutter events");
        desc.add_options()("frameFormat", boost::program_options::value<std::string>(),
                           "Choose the initial frame format\n"
                           "FRAME_FORMAT_CORRECTED               = 0x04  (not yet implemented)\n"
                           "FRAME_FORMAT_PRE_AGC                 = 0x08  (not yet implemented)\n"
                           //"FRAME_FORMAT_THERMOGRAPHY_FLOAT      = 0x10\n" these set in seperate option
                           //"FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 0x20\n"
                           "FRAME_FORMAT_GRAYSCALE               = 0x40\n"
                           "FRAME_FORMAT_COLOR_ARGB8888          = 0x80  (default)\n"
                           "FRAME_FORMAT_COLOR_RGB565            = 0x100 (not yet implemented)\n"
                           "FRAME_FORMAT_COLOR_AYUV              = 0x200 (not yet implemented)\n"
                           "FRAME_FORMAT_COLOR_YUY2              = 0x400 (not yet implemented)");
        desc.add_options()("setRadiometricFrameFormat", boost::program_options::value<std::string>(),
                           "Choose the initial radiometric frame format\n"
                           "FRAME_FORMAT_THERMOGRAPHY_FLOAT      = 0x10\n"
                           "FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 0x20 (default)");                           
        desc.add_options()("pipelineMode", boost::program_options::value<std::string>(),
                           "Choose the initial pipeline mode\n"
                           "PIPELINE_LITE       = 0\n"
                           "PIPELINE_LEGACY     = 1\n"
                           "PIPELINE_PROCESSED  = 2\n"
                           "Note that in PIPELINE_PROCESSED, sharpen, flat scene, and gradient filters are disabled");
        desc.add_options()("sharpenFilterMode", boost::program_options::value<std::string>(),
                           "Choose the initial state of the sharpen filter\n"
                           "zero     = disabled\n"
                           "non-zero = enabled");
        desc.add_options()("flatSceneFilterMode", boost::program_options::value<std::string>(),
                           "Choose the initial state of the flat scene filter\n"
                           "zero     = disabled\n"
                           "non-zero = enabled");
        desc.add_options()("gradientFilterMode", boost::program_options::value<std::string>(),
                           "Choose the initial state of the gradient filter\n"
                           "zero     = disabled\n"
                           "non-zero = enabled");
        boost::program_options::variables_map vm;
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
        boost::program_options::notify(vm);

        // the only 2 commands you may run without daemon are kill and help, the rest assume we are starting
        if (vm.count("help")){
            std::cout << desc << std::endl; // dump options and exit
            returnCode = EXIT_SUCCESS;
            break;
        }

        if (vm.count("kill")){            
            syslog(LOG_NOTICE, "Killing instance(s) of echothermd...\nPlease run echothermd again if you wish to restart the daemon.");
            // this kills the daemon running in the background
            // this tells them to terminate and exit, will wait to exit else will force termination
            returnCode = killOtherInstances("echothermd"); // we will exit ourselves normally

            remove(np_lockFile);
            sync(); // finish writing log
            closelog();
            n_running = false;
            // we exit normally
            break;
        }
        n_isDaemon = (bool)vm.count("daemon");

        if(!n_isDaemon ){ 
            syslog(LOG_ERR, "Error: Starting without daemon option.\n");
            std::cout << "Starting echothermd without the --daemon option specified. Exiting..\n";
            std::cout << "Start with echothermd --daemon [option(s)] to set default parameters\n";
            std::cout << "then use the echotherm [option] for general control while running\n";
            break;
        }
    
        syslog(LOG_NOTICE, "Daemon checking commandline for default settings...");
        
        // if we get here assume we want to start a Daemon
        // starting daemmon application we can add defaults to command line 

        //Commandline start option use the same parseCommand as socket is using
        // if the camera does not exist it set the defaults for camera initialize to use

        //not supported because of crashing issues
        // doesnt crash now but does not work if named differently
        if (vm.count("loopbackDeviceName"))
        {
            std::string const parameterStr = vm["loopbackDeviceName"].as<std::string>();
            std::string const commandStr = "LOOPBACKDEVICENAME " + parameterStr;
            _parseCommand(commandStr.c_str());
        }
        if (vm.count("frameFormat"))
        {
            std::string const parameterStr = vm["frameFormat"].as<std::string>();
            std::string const commandStr = "FORMAT " + parameterStr ;
            _parseCommand(commandStr.c_str());
        }

        if (vm.count("maxZoom"))
        {
            std::string const parameterStr = vm["maxZoom"].as<std::string>();
            std::string const commandStr = "MAXZOOM " + parameterStr;
            _parseCommand(commandStr.c_str());
        }
        if (vm.count("colorPalette"))
        {
            std::string const parameterStr = vm["colorPalette"].as<std::string>();
            std::string const commandStr = "PALETTE " + parameterStr;
            _parseCommand(commandStr.c_str());
        }
        if (vm.count("shutterMode"))
        {
            std::string const parameterStr = vm["shutterMode"].as<std::string>();
            std::string const commandStr = "SHUTTERMODE " + parameterStr;
            _parseCommand(commandStr.c_str());
        }
        if (vm.count("pipelineMode"))
        {
            std::string const parameterStr = vm["pipelineMode"].as<std::string>();
            std::string const commandStr = "PIPELINEMODE " + parameterStr;
            _parseCommand(commandStr.c_str());
        }
        if (vm.count("sharpenFilterMode"))
        {
            std::string const parameterStr = vm["sharpenFilterMode"].as<std::string>();
            std::string const commandStr = "SHARPEN " + parameterStr;
            _parseCommand(commandStr.c_str());
        }
        if (vm.count("gradientFilterMode"))
        {
            std::string const parameterStr = vm["gradientFilterMode"].as<std::string>();
            std::string const commandStr = "GRADIENT " + parameterStr;
            _parseCommand(commandStr.c_str());
        }
        if (vm.count("flatSceneFilterMode"))
        {
            std::string const parameterStr = vm["flatSceneFilterMode"].as<std::string>();
            std::string const commandStr = "FLATSCENE " + parameterStr;
            _parseCommand(commandStr.c_str());
        }
        if (vm.count("shutterMode"))
        {
            std::string const parameterStr = vm["shutterMode"].as<std::string>();
            std::string const commandStr = "SHUTTERMODE " + parameterStr;
            _parseCommand(commandStr.c_str());

        }
        if (vm.count("setRadiometricFrameFormat"))
        {
            std::string const parameterStr = vm["setRadiometricFrameFormat"].as<std::string>();
            std::string const commandStr = "SETRADIOMETRICFRAMEFORMAT " + parameterStr;
            _parseCommand(commandStr.c_str());
        }

        //=====================================================================

        std::cout << "\nStarting EchoTherm daemon, v1.1.0 ©EchoMAV, LLC 2024\n";
        std::cout << "To view log output, journalctl -t echothermd\nTo tail log output, journalctl -ft echothermd" << std::endl;
        syslog(LOG_NOTICE, "\nStarting EchoTherm daemon, v1.0.1 ©EchoMAV, LLC 2024");
   
        // Check that another instance isn't already running by checking for a lock file
        if (!_checkLock())
        {
            if (!n_isDaemon)
            {
                std::cout << "Error: another instance of the program is already running OR the /tmp/echothermd.lock is still in place from a previous call to a non-daemon process of echothermd. .\nTo fix this, run echothermd --kill" << std::endl;
            }
            syslog(LOG_ERR, "Error: another instance of the program is already running OR the /tmp/echothermd.lock is still in place from a previous call to a non-daemon process of echothermd. .\nTo fix this, run echothermd --kill");
            std::cout << "Error: another instance of the program is already running OR the /tmp/echothermd.lock\nis still in place from a previous call to a non-daemon process of echothermd. .\nTo fix this, run echothermd --kill\n";

            returnCode = EXIT_FAILURE;
            break;
        }
        std::cout << "\n";

        if (!_setSignalAction()) // waited until now to install because this is only for daemon, other versions just exit
        {
            std::cout << "Failed to install the signal handler\n";
            returnCode = EXIT_FAILURE;
            break;
        }

        if (!_isPortAvailable(n_port))
        {
            syslog(LOG_ERR, "Error: port %d is not available for binding...", n_port);
            returnCode = EXIT_FAILURE;
            break;
        }
        
        if (n_isDaemon) {
            returnCode = _startDaemon();
            switch (returnCode) {
                case -1:
                    // Error occurred
                    std::cerr << "Error: Failed to start daemon" << std::endl;
                    break;            
                case 1:
                    // Parent process, exit successfully, normal behaviour
                    #ifdef DEBUG
                        std::cout << "Parent process exiting" << std::endl;
                    #endif
                    break;   
                case 2:
                    // Child process, exit successfully, normal behaviour
                    #ifdef DEBUG
                        std::cout << "Child process exiting" << std::endl;
                    #endif
                    break;                             
                case 0: // grandchild
                    // Successfully daemonized, continue with initialization
                    std::cout << "daemon created" << std::endl;
                    isDaemonProcess = true ;
                    break;
                default:
                    // Unexpected return value
                    std::cerr << "Error: Unexpected return from start_daemon()" << std::endl;
                    break;
            }
        }
        if( !isDaemonProcess || returnCode != 0 ){
            // one of the otherprocess that need to terminate
            break ;
        }// else allow it to initialize normally

        returnCode = EXIT_SUCCESS;

        if (!_initializeCamera(vm))
        {
            syslog(LOG_ERR, "Unable to start camera.");
            returnCode = EXIT_FAILURE;
            break;
        }
        // Create a socket to listen for commands
        serverFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFileDescriptor == -1)
        {
            syslog(LOG_ERR, "Unable to create socket: %m");
            returnCode = EXIT_FAILURE;
            break;
        }
        syslog(LOG_NOTICE, "Opening socket...");
        int opt = 1;
        // set options at the socket level (SOL_SOCKET)
        // local address and port reuse is allowed (SO_REUSEADDR)
        if (setsockopt(serverFileDescriptor, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1)
        {
            syslog(LOG_ERR, "setsocketopt failed: %m");
            returnCode = EXIT_FAILURE;
            break;
        }
        struct sockaddr_in socketAddress
        {
        };
        std::memset(&socketAddress, 0, sizeof(socketAddress));
        socketAddress.sin_family = AF_INET;
        socketAddress.sin_port = htons(n_port);
        socketAddress.sin_addr.s_addr = INADDR_ANY;
        // bind the socket to an address that accepts all incoming messages
        if (bind(serverFileDescriptor, (struct sockaddr *)&socketAddress, sizeof(socketAddress)) == -1)
        {
            syslog(LOG_ERR, "bind failed: %m");
            returnCode = EXIT_FAILURE;
            break;
        }

        // Set the server socket to non-blocking mode
        if (!_setNonBlocking(serverFileDescriptor))
        {
            returnCode = EXIT_FAILURE;
            break;
        }
        // tell the socket to listen for connections
        if (listen(serverFileDescriptor, 3) == -1)
        {
            syslog(LOG_ERR, "listen failed: %m");
            returnCode = EXIT_FAILURE;
            break;
        }
        syslog(LOG_NOTICE, "Listening on port %d...", n_port);
        // Create an epoll instance
        epollFileDescriptor = epoll_create1(0);
        if (epollFileDescriptor == -1)
        {
            syslog(LOG_ERR, "Unable to create epoll instance: %m");
            returnCode = EXIT_FAILURE;
            break;
        }
        // Add the server socket to the epoll instance
        struct epoll_event epollEvent;
        std::memset(&epollEvent, 0, sizeof(epollEvent));
        epollEvent.events = EPOLLIN;
        epollEvent.data.fd = serverFileDescriptor;
        if (epoll_ctl(epollFileDescriptor, EPOLL_CTL_ADD, serverFileDescriptor, &epollEvent) == -1)
        {
            syslog(LOG_ERR, "epoll_ctl failed: %m");
            returnCode = EXIT_FAILURE;
            break;
        }

        if(n_running && returnCode != EXIT_FAILURE){
            std::cout << "ready\n";
        }

        // buffer to accept epoll events
        struct epoll_event p_events[n_maxEpollEvents];
        constexpr uint32_t const sizeofAddress = (uint32_t)sizeof(socketAddress);
        while (n_running && returnCode != EXIT_FAILURE)
        {
            // look for new socket events
            auto const numEvents = epoll_wait(epollFileDescriptor, p_events, n_maxEpollEvents, -1);
            if (numEvents == -1)
            {          
                if (errno == EINTR){
                    // This is an expected interruption, likely due to termination signal
                   syslog(LOG_INFO, "epoll_wait interrupted, likely due to termination signal");
                }
                else{
                    syslog(LOG_ERR, "epoll_wait failed: %m");
                }
            } 

            for (int eventIndex = 0; eventIndex < numEvents; ++eventIndex)
            {
                if (p_events[eventIndex].data.fd == serverFileDescriptor)
                {
                    // Handle new connection
                    clientFileDescriptor = accept(serverFileDescriptor, (struct sockaddr *)&socketAddress, (socklen_t *)&sizeofAddress);
                    if (clientFileDescriptor == -1)
                    {
                        syslog(LOG_NOTICE, "accept failed: %m");
                    }
                    else
                    {
                        if (!_setNonBlocking(clientFileDescriptor))
                        {
                            returnCode = EXIT_FAILURE;
                            break;
                        }
                        std::memset(&epollEvent, 0, sizeof(epollEvent));
                        epollEvent.events = EPOLLIN | EPOLLET;
                        epollEvent.data.fd = clientFileDescriptor;
                        if (epoll_ctl(epollFileDescriptor, EPOLL_CTL_ADD, clientFileDescriptor, &epollEvent) == -1)
                        {
                            syslog(LOG_ERR, "epoll_ctl failed: %m");
                            close(clientFileDescriptor);
                            clientFileDescriptor = -1;
                        }
                    }
                }
                else
                {
                    // handle data from a connected client
                    _handleClient(p_events[eventIndex].data.fd);
                }
            }
            // TODO process other stuff in the loop here!!!
        }
    } while (false);
    
    if( isDaemonProcess ){
        //syslog(LOG_NOTICE, "Exiting daemon(%d)...\n", getpid());
        if (serverFileDescriptor != -1)
        {
            close(serverFileDescriptor);
        }
        if (epollFileDescriptor != -1)
        {
            close(epollFileDescriptor);
        }
        if (clientFileDescriptor != -1)
        {
            close(clientFileDescriptor);
        }
    }
    sync();
    //remove(np_lockFile);
    //closelog();
    //n_running = false;
    syslog(LOG_NOTICE, "Exit(%d)...\n", getpid());
    return returnCode;
}
