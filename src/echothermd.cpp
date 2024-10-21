#include <netinet/in.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>

#include <csignal>
#include <cstring>
#include <charconv>
#include <iostream>
#include <regex>
#include <filesystem>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include "EchoThermCamera.h"

namespace
{
    constexpr static inline auto const n_defaultlLoopbackDeviceName = "/dev/video0";
    constexpr static inline auto const n_defaultColorPalette = 0;        // COLOR_PALETTE_WHITE_HOT
    constexpr static inline auto const n_defaultShutterMode = 0;         // SHUTTER_MODE_AUTO
    constexpr static inline auto const n_defaultFrameFormat = 128;       // FRAME_FORMAT_COLOR_ARGB8888
    constexpr static inline auto const n_defaultSharpenFilterMode = 0;   // DISABLED
    constexpr static inline auto const n_defaultGradientFilterMode = 0;  // DISABLED
    constexpr static inline auto const n_defaultFlatSceneFilterMode = 0; // DISABLED
    constexpr static inline auto const n_defaultPipelineMode = 2;        // PIPELINE_PROCESSED
    constexpr static inline auto const n_defaultMaxZoom = 16.0;

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
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), *p_double);
        return ec;
    }

    void _handleTerminationSignal(int signal)
    {
        syslog(LOG_NOTICE, "Received killing signal %d, shutting down daemon...", signal);
        remove(np_lockFile);
        closelog();
        n_running = false;
    }

    bool _setTerminationSignalAction()
    {
        auto returnVal = true;
        for (auto signal : np_catchTheseSignals)
        {
            struct sigaction signalAction
            {
            };
            std::memset(&signalAction, 0, sizeof(signalAction));
            signalAction.sa_handler = _handleTerminationSignal;
            sigemptyset(&signalAction.sa_mask);
            if (sigaction(signal, &signalAction, nullptr) == -1)
            {
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

    int _startDaemon()
    {
        int returnCode = -1;
        do
        {
            // Fork off the parent process
            if (auto const processId = fork(); processId == -1)
            {
                syslog(LOG_ERR, "Failed to fork: %m");
                returnCode = EXIT_FAILURE;
                break;
            }
            else if (processId > 0)
            {
                // let the parent terminate
                returnCode = EXIT_SUCCESS;
                break;
            }
            // On success: The child process becomes session leader
            if (setsid() == -1)
            {
                syslog(LOG_ERR, "setsid failed: %m");
                returnCode = EXIT_FAILURE;
                break;
            }
            // Catch, ignore and handle signals
            signal(SIGCHLD, SIG_IGN);
            signal(SIGHUP, SIG_IGN);
            // Fork off for the second time
            if (auto const processId = fork(); processId == -1)
            {
                syslog(LOG_ERR, "Failed to fork: %m");
                returnCode = EXIT_FAILURE;
                break;
            }
            else if (processId > 0)
            {
                // let the parent terminate
                returnCode = EXIT_SUCCESS;
                break;
            }
            // Set new file permissions
            umask(0);
            // Change the working directory to the root directory
            // or another appropriated directory
            chdir("/");
            // Close all open file descriptors
            for (auto x = sysconf(_SC_OPEN_MAX); x >= 0; --x)
            {
                close(x);
            }
        } while (false);
        return returnCode;
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
                syslog(LOG_NOTICE, "SHUTTER");
                np_camera->triggerShutter();
            }
            else if (strcmp(p_token, "MAXZOOM") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "MAXZOOM command received, but no number was provided.");
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
                        syslog(LOG_NOTICE, "MAXZOOM: %f", number);
                        np_camera->setMaxZoom(number);
                    }
                }
            }
            else if (strcmp(p_token, "ZOOMRATE") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "ZOOMRATE command received, but no number was provided.");
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
                        syslog(LOG_NOTICE, "ZOOMRATE: %f", number);
                        np_camera->setZoomRate(number);
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
                        syslog(LOG_NOTICE, "ZOOM: %f", number);
                        np_camera->setZoom(number);
                    }
                }
            }
            else if (strcmp(p_token, "MAXZOOM") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "MAXZOOM command received, but no number was provided.");
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
                        syslog(LOG_NOTICE, "MAXZOOM: %f", number);
                        np_camera->setMaxZoom(number);
                    }
                }
            }
            else if (strcmp(p_token, "GETZOOM") == 0)
            {
                syslog(LOG_NOTICE, "GETZOOM");
                response = np_camera->getZoom();
            }
            else if (strcmp(p_token, "STATUS") == 0)
            {
                syslog(LOG_NOTICE, "STATUS");
                response = np_camera->getStatus();
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
                        syslog(LOG_NOTICE, "PALETTE: %d", number);
                        np_camera->setColorPalette(number);
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
                        syslog(LOG_NOTICE, "SHUTTERMODE: %d", number);
                        np_camera->setShutterMode(number);
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
                        syslog(LOG_NOTICE, "PIPELINEMODE: %d", number);
                        np_camera->setPipelineMode(number);
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
                        syslog(LOG_NOTICE, "SHARPEN: %d", number);
                        np_camera->setSharpenFilter(number);
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
                        syslog(LOG_NOTICE, "FLATSCENE: %d", number);
                        np_camera->setFlatSceneFilter(number);
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
                        syslog(LOG_NOTICE, "GRADIENT: %d", number);
                        np_camera->setGradientFilter(number);
                    }
                }
            }
            else if (strcmp(p_token, "STARTRECORDING") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "STARTRECORDING command received, but no file path was specified.");
                }
                else
                {
                    std::filesystem::path filePath = _desanitizeString(p_token);
                    syslog(LOG_NOTICE, "STARTRECORDING: %s", filePath.string().c_str());
                    response = np_camera->startRecording(filePath);
                }
            }
            else if (strcmp(p_token, "STOPRECORDING") == 0)
            {
                syslog(LOG_NOTICE, "STOPRECORDING");
                response=np_camera->stopRecording();
            }
            else if (strcmp(p_token, "TAKESCREENSHOT") == 0)
            {
                if ((p_token = strtok(nullptr, " ")) == nullptr)
                {
                    syslog(LOG_NOTICE, "TAKESCREENSHOT command received, but no file path was specified.");
                }
                else
                {
                    std::filesystem::path filePath = _desanitizeString(p_token);
                    syslog(LOG_NOTICE, "TAKESCREENSHOT: %s", filePath.string().c_str());
                    response = np_camera->takeScreenshot(filePath);
                }
            }
#if 0
            //Not supported because of crashing issues
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
                        np_camera->setFrameFormat(number);
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
                    np_camera->setLoopbackDeviceName(p_token);
                }
            }
#endif
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
        std::string loopbackDeviceName = n_defaultlLoopbackDeviceName;
        int colorPalette = n_defaultColorPalette;
        int shutterMode = n_defaultShutterMode;
        int frameFormat = n_defaultFrameFormat;
        int pipelineMode = n_defaultPipelineMode;
        int sharpenFilterMode = n_defaultSharpenFilterMode;
        int gradientFilterMode = n_defaultGradientFilterMode;
        int flatSceneFilterMode = n_defaultFlatSceneFilterMode;
        double maxZoom = n_defaultMaxZoom;
        if (vm.count("loopbackDeviceName"))
        {
            loopbackDeviceName = vm["loopbackDeviceName"].as<std::string>();
        }
        if (vm.count("colorPalette"))
        {
            _parseInt(vm["colorPalette"].as<std::string>(), &colorPalette);
        }
        if (vm.count("maxZoom"))
        {
            _parseDouble(vm["maxZoom"].as<std::string>(), &maxZoom);
        }
        if (vm.count("shutterMode"))
        {
            _parseInt(vm["shutterMode"].as<std::string>(), &shutterMode);
        }
        if (vm.count("frameFormat"))
        {
            _parseInt(vm["frameFormat"].as<std::string>(), &frameFormat);
        }
        if (vm.count("pipelineMode"))
        {
            _parseInt(vm["pipelineMode"].as<std::string>(), &pipelineMode);
        }
        if (vm.count("sharpenFilterMode"))
        {
            _parseInt(vm["sharpenFilterMode"].as<std::string>(), &sharpenFilterMode);
        }
        if (vm.count("gradientFilterMode"))
        {
            _parseInt(vm["gradientFilterMode"].as<std::string>(), &gradientFilterMode);
        }
        if (vm.count("flatSceneFilterMode"))
        {
            _parseInt(vm["flatSceneFilterMode"].as<std::string>(), &flatSceneFilterMode);
        }
        syslog(LOG_NOTICE, "loopbackDeviceName = %s", loopbackDeviceName.c_str());
        syslog(LOG_NOTICE, "colorPalette = %d", colorPalette);
        syslog(LOG_NOTICE, "maxZoom = %f", maxZoom);
        syslog(LOG_NOTICE, "shutterMode = %d", shutterMode);
        syslog(LOG_NOTICE, "frameFormat = %d", frameFormat);
        syslog(LOG_NOTICE, "pipelineMode = %d", pipelineMode);
        syslog(LOG_NOTICE, "sharpenFilterMode = %d", sharpenFilterMode);
        syslog(LOG_NOTICE, "gradientFilterMode = %d", gradientFilterMode);
        syslog(LOG_NOTICE, "flatSceneFilterMode = %d", flatSceneFilterMode);

        np_camera = std::make_unique<EchoThermCamera>();
        np_camera->setLoopbackDeviceName(loopbackDeviceName);
        np_camera->setColorPalette(colorPalette);
        np_camera->setShutterMode(shutterMode);
        np_camera->setFrameFormat(frameFormat);
        np_camera->setPipelineMode(pipelineMode);
        np_camera->setSharpenFilter(sharpenFilterMode);
        np_camera->setGradientFilter(gradientFilterMode);
        np_camera->setFlatSceneFilter(flatSceneFilterMode);
        np_camera->setMaxZoom(maxZoom);

        return np_camera->start();
    }
}

int main(int argc, char *argv[])
{
    int clientFileDescriptor = -1;
    int epollFileDescriptor = -1;
    int serverFileDescriptor = -1;
    int returnCode = EXIT_SUCCESS;
    do
    {
        // Open the log file
        openlog(np_logName, LOG_PID, LOG_DAEMON);
        if (!_setTerminationSignalAction())
        {
            returnCode = EXIT_FAILURE;
            break;
        }
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
                           "FRAME_FORMAT_THERMOGRAPHY_FLOAT      = 0x10  (not yet implemented)\n"
                           "FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 0x20  (not yet implemented)\n"
                           "FRAME_FORMAT_GRAYSCALE               = 0x40\n"
                           "FRAME_FORMAT_COLOR_ARGB8888          = 0x80  (default)\n"
                           "FRAME_FORMAT_COLOR_RGB565            = 0x100 (not yet implemented)\n"
                           "FRAME_FORMAT_COLOR_AYUV              = 0x200 (not yet implemented)\n"
                           "FRAME_FORMAT_COLOR_YUY2              = 0x400 (not yet implemented)\n");
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
        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            returnCode = EXIT_SUCCESS;
            break;
        }
        n_isDaemon = (bool)vm.count("daemon");
        if (vm.count("kill"))
        {
            syslog(LOG_NOTICE, "Killing the existing instance...\nPlease run echothermd again if you wish to restart the daemon.");
            remove(np_lockFile);
            closelog();
            n_running = false;
            returnCode = system("pkill -9 echothermd");
            break;
        }
        std::cout << "\nStarting EchoTherm daemon, v1.0.0 ©EchoMAV, LLC 2024\nTo view log output, journalctl -t echothermd\nTo tail log output, journalctl -ft echothermd" << std::endl;
        syslog(LOG_NOTICE, "Starting EchoTherm daemon, v1.0.0 ©EchoMAV, LLC 2024");
        // Check that another instance isn't already running by checking for a lock file
        if (!_checkLock())
        {
            if (!n_isDaemon)
            {
                std::cerr << "Error: another instance of the program is already running OR the /tmp/echothermd.lock is still in place from a previous call to a non-daemon process of echothermd. .\nTo fix this, run echothermd --kill" << std::endl;
            }
            syslog(LOG_ERR, "Error: another instance of the program is already running OR the /tmp/echothermd.lock is still in place from a previous call to a non-daemon process of echothermd. .\nTo fix this, run echothermd --kill");
            returnCode = EXIT_FAILURE;
            break;
        }
        if (!_isPortAvailable(n_port))
        {
            syslog(LOG_ERR, "Error: port %d is not available for binding...", n_port);
            returnCode = EXIT_FAILURE;
            break;
        }
        if (n_isDaemon && (returnCode = _startDaemon()) >= 0)
        {
            // either something went wrong or this is the parent process
            break;
        }
        returnCode = EXIT_SUCCESS;
        syslog(LOG_NOTICE, "EchoTherm Daemon Started.");
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

        // buffer to accept epoll events
        struct epoll_event p_events[n_maxEpollEvents];
        constexpr uint32_t const sizeofAddress = (uint32_t)sizeof(socketAddress);
        while (n_running && returnCode != EXIT_FAILURE)
        {
            // look for new socket events
            auto const numEvents = epoll_wait(epollFileDescriptor, p_events, n_maxEpollEvents, -1);
            if (numEvents == -1)
            {
                syslog(LOG_ERR, "epoll_wait failed: %m");
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
    // remove(np_lockFile);
    // closelog();
    // n_running = false;
    return returnCode;
}
