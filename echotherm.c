#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <getopt.h>

#define PORT 8888

int main(int argc, char *argv[]) {

    int sock = 0;
    struct sockaddr_in serv_addr;    
    char buffer[1024] = {0};
    int status;
    int palette = -1;
    int shuttermode = -1;
    int opt;
    char paletteCommand[50];
    char shutterCommand[50];

    // Define long options
    static struct option long_options[] = {
        {"palette", required_argument, 0, 'p'},
        {"shuttermode", required_argument, 0, 's'},
        {"help", 0, 0, 'h'},
        {0, 0, 0, 0}
    };

    //fist lets make sure echothermd is running
    //might not be a bad idea to verify that port is open for listening too
    status = system("pgrep echothermd > /dev/null 2>&1");
    if (status != 0) {
        printf("Error, the EchoTherm daemon is not running, please start it first with ./echothermd.\n");
        exit(EXIT_FAILURE);
    }

    //now parse the command line arguments --palette X and --shuttermode X    
    while ((opt = getopt_long(argc, argv, "p:s:h:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                palette = atoi(optarg);
                sprintf(paletteCommand, "PALETTE %d", palette);
                break;
            case 's':
                shuttermode = atoi(optarg);
                sprintf(shutterCommand, "SHUTTERMODE %d", shuttermode);
                break;
            case 'h':                               
                printf("\nUsage: echotherm --palette <number> or --shuttermode <number>\n\n\t--palette\tColor palette: Options 1 (Black Hot), 2 (White Hot)...TBD\n\t--shuttermode\tShutter Mode: 1 (auto), 2 (every 10s), 3 (every 20s), ...TBD\n\n");
                exit(EXIT_SUCCESS);
                break;
            default:
                fprintf(stderr, "Usage: %s --palette <number> or --shuttermode <number> is required\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Check if at least one of the arguments is provided
    if (palette == -1 && shuttermode == -1) {
        fprintf(stderr, "Error: Either --palette or --shuttermode must be provided.\n");
        exit(EXIT_FAILURE);
    }

    // Create and connect to echothermd socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    // Process arguments
    if (palette != -1) {
        //send palette command        
        send(sock, paletteCommand, strlen(paletteCommand), 0);
        printf("Changed the IR color palette to mode: %d\n", palette);
    }

    if (shuttermode != -1) {
        //send shutter command       
        send(sock, shutterCommand, strlen(shutterCommand), 0);
        printf("Changing the Shutter Mode to: %d\n", shuttermode);
    }
   
    // Close the socket
    close(sock);

    exit(EXIT_SUCCESS);

}
