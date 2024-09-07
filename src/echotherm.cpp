#include <arpa/inet.h>
// #include <netinet/in.h>
#include <boost/program_options.hpp>
#include <iostream>

namespace
{
    constexpr static inline auto const n_port = 9182;

    bool _openSocket(int *p_socketFileDescriptor)
    {
        bool returnVal = true;
        do
        {
            // open the socket
            if ((*p_socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1)
            {
                std::cerr << "Socket creation error: " << std::strerror(errno) << std::endl;
                returnVal = false;
                break;
            }
            struct sockaddr_in socketAddress
            {
            };
            std::memset(&socketAddress, 0, sizeof(socketAddress));
            socketAddress.sin_family = AF_INET;
            socketAddress.sin_port = htons(n_port);
            socketAddress.sin_addr.s_addr = INADDR_ANY;
            // Convert IPv4 and IPv6 addresses from text to binary form
            switch (inet_pton(AF_INET, "127.0.0.1", &socketAddress.sin_addr))
            {
            case 0:
                std::cerr << "Invalid address/ Address not supported" << std::endl;
                returnVal = false;
                break;
            case -1:
                std::cerr << "inet_pton failed: " << std::strerror(errno) << std::endl;
                returnVal = false;
                break;
            default:
                break;
            }
            if (!returnVal)
            {
                break;
            }
            // connect the socket to the address specified
            if (connect(*p_socketFileDescriptor, (struct sockaddr *)&socketAddress, sizeof(socketAddress)) == -1)
            {
                std::cerr << "connect failed: " << std::strerror(errno) << std::endl;
                returnVal = false;
                break;
            }
        } while (false);
        return returnVal;
    }

    void _sendCommands(boost::program_options::variables_map const &vm, int const socketFileDescriptor)
    {
#if 0
        //not supported because of crashing issues
        if (vm.count("loopbackDeviceName"))
        {
            std::string const parameterStr = vm["loopbackDeviceName"].as<std::string>();
            std::string const commandStr = "LOOPBACKDEVICENAME " + parameterStr + '|';
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to change loopback device name to " << parameterStr << std::endl;
        }
        if (vm.count("frameFormat"))
        {
            std::string const parameterStr = vm["frameFormat"].as<std::string>();
            std::string const commandStr = "FORMAT " + parameterStr + '|';
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to change loopback device name to " << parameterStr << std::endl;
        }
#endif
        if (vm.count("colorPalette"))
        {
            std::string const parameterStr = vm["colorPalette"].as<std::string>();
            std::string const commandStr = "PALETTE " + parameterStr + '|';
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to change color palette to " << parameterStr << std::endl;
        }
        if (vm.count("shutterMode"))
        {
            std::string const parameterStr = vm["shutterMode"].as<std::string>();
            std::string const commandStr = "SHUTTERMODE " + parameterStr + '|';
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to change shutter mode to " << parameterStr << std::endl;
        }
        if (vm.count("pipelineMode"))
        {
            std::string const parameterStr = vm["pipelineMode"].as<std::string>();
            std::string const commandStr = "PIPELINEMODE " + parameterStr + '|';
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to change pipeline mode to " << parameterStr << std::endl;
        }
        if (vm.count("sharpenFilterMode"))
        {
            std::string const parameterStr = vm["sharpenFilterMode"].as<std::string>();
            std::string const commandStr = "SHARPEN " + parameterStr + '|';
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to change sharpen filter to " << parameterStr << std::endl;
        }
        if (vm.count("gradientFilterMode"))
        {
            std::string const parameterStr = vm["gradientFilterMode"].as<std::string>();
            std::string const commandStr = "GRADIENT " + parameterStr + '|';
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to change gradient filter to " << parameterStr << std::endl;
        }
        if (vm.count("flatSceneFilterMode"))
        {
            std::string const parameterStr = vm["flatSceneFilterMode"].as<std::string>();
            std::string const commandStr = "FLATSCENE " + parameterStr + '|';
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to change flat scene filter to " << parameterStr << std::endl;
        }
        if (vm.count("shutter"))
        {
            std::string const commandStr = "SHUTTER|";
            send(socketFileDescriptor, commandStr.c_str(), commandStr.length(), 0);
            std::cout << "Sent command to trigger shutter" << std::endl;
        }
    }
}

int main(int argc, char *argv[])
{
    int returnCode = EXIT_SUCCESS;
    int socketFileDescriptor = -1;
    do
    {
        if (system("pgrep echothermd > /dev/null 2>&1"))
        {
            std::cerr << "Error, the EchoTherm daemon is not running, please start it first with echothermd." << std::endl;
            returnCode = EXIT_FAILURE;
            break;
        }
        boost::program_options::options_description desc("Allowed options");
        desc.add_options()("help", "Produce this message");
        desc.add_options()("shutter", "Trigger the shutter");

        desc.add_options()("colorPalette", boost::program_options::value<std::string>(),
                           "Choose the color palette\n"
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
                           "Choose the shutter mode\n"
                           "negative = manual\n"
                           "zero     = auto\n"
                           "positive = number of seconds between shutter events");
#if 0
        //Not supported because of crashing issues
        desc.add_options()("loopbackDeviceName", boost::program_options::value<std::string>(),
                           "Choose the loopback device name");
        desc.add_options()("frameFormat", boost::program_options::value<std::string>(),
                           "Choose the frame format\n"
                           "FRAME_FORMAT_CORRECTED               = 0x04\n"
                           "FRAME_FORMAT_PRE_AGC                 = 0x08\n"
                           "FRAME_FORMAT_THERMOGRAPHY_FLOAT      = 0x10\n"
                           "FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 0x20\n"
                           "FRAME_FORMAT_GRAYSCALE               = 0x40\n"
                           "FRAME_FORMAT_COLOR_ARGB8888          = 0x80\n"
                           "FRAME_FORMAT_COLOR_RGB565            = 0x100\n"
                           "FRAME_FORMAT_COLOR_AYUV              = 0x200\n"
                           "FRAME_FORMAT_COLOR_YUY2              = 0x400\n");
#endif
        desc.add_options()("pipelineMode", boost::program_options::value<std::string>(),
                           "Choose the pipeline mode\n"
                           "PIPELINE_LITE       = 0\n"
                           "PIPELINE_LEGACY     = 1\n"
                           "PIPELINE_PROCESSED  = 2\n"
                           "Note that in PIPELINE_PROCESSED, sharpen, flat scene, and gradient filters are disabled");
        desc.add_options()("sharpenFilterMode", boost::program_options::value<std::string>(),
                           "Choose the state of the sharpen filter\n"
                           "zero     = disabled\n"
                           "non-zero = enabled");
        desc.add_options()("flatSceneFilterMode", boost::program_options::value<std::string>(),
                           "Choose the state of the flat scene filter\n"
                           "zero     = disabled\n"
                           "non-zero = enabled");
        desc.add_options()("gradientFilterMode", boost::program_options::value<std::string>(),
                           "Choose the state of the gradient filter\n"
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
        if (!_openSocket(&socketFileDescriptor))
        {
            returnCode = EXIT_FAILURE;
            break;
        }
        _sendCommands(vm, socketFileDescriptor);
    } while (false);
    if (socketFileDescriptor != -1)
    {
        close(socketFileDescriptor);
    }
    return returnCode;
}
