#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <sstream>

#define PORT 8888

int main(int argc, char* argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    int status;
    int palette = -1;
    int shuttermode = -1;
    int opt;
    std::string paletteCommand;
    std::string shutterCommand;

    // Define long options
    static struct option long_options[] = {
        {"palette", required_argument, 0, 'p'},
        {"shuttermode", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // First, let's make sure echothermd is running
    // Might not be a bad idea to verify that port is open for listening too
    status = system("pgrep echothermd > /dev/null 2>&1");
    if (status != 0) {
        std::cerr << "Error, the EchoTherm daemon is not running, please start it first with ./echothermd." << std::endl;
        exit(EXIT_FAILURE);
    }

    // Now parse the command line arguments --palette X and --shuttermode X
    while ((opt = getopt_long(argc, argv, "p:s:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                palette = std::stoi(optarg);
                paletteCommand = "PALETTE " + std::to_string(palette);
                break;
            case 's':
                shuttermode = std::stoi(optarg);
                shutterCommand = "SHUTTERMODE " + std::to_string(shuttermode);
                break;
            case 'h':
                std::cout << "\nUsage: echotherm --palette <number> or --shuttermode <number>\n\n\t--palette\tColor palette: Options 1 (Black Hot), 2 (White Hot)...TBD\n\t--shuttermode\tShutter Mode: 1 (auto), 2 (every 10s), 3 (every 20s), ...TBD\n\n";
                exit(EXIT_SUCCESS);
                break;
            default:
                std::cerr << "Usage: " << argv[0] << " --palette <number> or --shuttermode <number> is required" << std::endl;
                exit(EXIT_FAILURE);
        }
    }

    // Check if at least one of the arguments is provided
    if (palette == -1 && shuttermode == -1) {
        std::cerr << "Error: Either --palette or --shuttermode must be provided." << std::endl;
        exit(EXIT_FAILURE);
    }

    // Create and connect to echothermd socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "\nSocket creation error" << std::endl;
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        std::cerr << "\nInvalid address/ Address not supported" << std::endl;
        return -1;
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "\nConnection Failed" << std::endl;
        return -1;
    }

    // Process arguments
    if (palette != -1) {
        // Send palette command
        send(sock, paletteCommand.c_str(), paletteCommand.length(), 0);
        std::cout << "Changed the IR color palette to mode: " << palette << std::endl;
    }

    if (shuttermode != -1) {
        // Send shutter command
        send(sock, shutterCommand.c_str(), shutterCommand.length(), 0);
        std::cout << "Changing the Shutter Mode to: " << shuttermode << std::endl;
    }

    // Close the socket
    close(sock);

    return EXIT_SUCCESS;
}
