/*
*  EchoTherm Daemon
*  Opens a socket connection on the designated port and waits for PALETTE X and SHUTTERMODE X commands
*/
    
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8888
#define BUFFER_SIZE 1024
   
static void start_daemon()
{
    pid_t pid;
    
    /* Fork off the parent process */
    pid = fork();
    
    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);
    
     /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);
    
    /* On success: The child process becomes session leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);
    
    /* Catch, ignore and handle signals */
    /*TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    
    /* Fork off for the second time*/
    pid = fork();
    
    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);
    
    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);
    
    /* Set new file permissions */
    umask(0);
    
    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    chdir("/");
    
    /* Close all open file descriptors */
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        close (x);
    }
    
    /* Open the log file */
    openlog ("echoThermd", LOG_PID, LOG_DAEMON);
}

void parse_command(char *command) {
    // Tokenize the command string
    char *token = strtok(command, " ");
    
     // Check for specific commands and extract numbers
    if (token != NULL) {
        if (strcmp(token, "PALETTE") == 0) {
            // Expecting a number after "PALETTE"
            token = strtok(NULL, " ");
            if (token != NULL) {
                int number = atoi(token); 
                syslog (LOG_NOTICE,"PALETTE: %d\n", number);

                //todo change the palette

            } else {
                syslog (LOG_NOTICE, "PALETTE command received, but no number was provided.\n");
            }
        } else if (strcmp(token, "SHUTTERMODE") == 0) {            
            token = strtok(NULL, " ");
            if (token != NULL) {
                int number = atoi(token);
                syslog (LOG_NOTICE, "SHUTTERMODE: %d\n", number);
                
                //todo, change the shuttermode

            } else {
                syslog (LOG_NOTICE, "SHUTTERMODE command received, but no number was provided.\n");
            }
        } else {
            printf("Unknown command: %s\n", token);
        }
    }
}

int main()
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    int valread;

    //start the daemon
    start_daemon();    
    
    syslog (LOG_NOTICE, "EchoTherm Daemon Started.");

    //TODO: add seek camera loopback code here


    // now create a socket to listen for commands
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        syslog (LOG_ERR, "socket failed");
        exit(EXIT_FAILURE);
    }
    else
    {
        syslog (LOG_NOTICE, "Opening socket...");
    }

    // Forcefully attaching socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        syslog (LOG_ERR,"setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        syslog (LOG_ERR, "bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_fd, 3) < 0) {
        syslog (LOG_ERR, "listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    syslog (LOG_NOTICE, "Listening on port %d...\n", PORT);

    // Loop to accept connections indefinitely
    while (1) {
        syslog (LOG_NOTICE, "Waiting for a connection...\n");

        // Accept an incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        syslog (LOG_NOTICE, "Connection accepted...\n");

        // Read commands from the connected client
        while ((valread = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
            buffer[valread] = '\0';
            syslog (LOG_NOTICE, "Received command: %s\n", buffer);

            parse_command(buffer);
        }

        if (valread < 0) {
            syslog (LOG_ERR, "read");
        }

        // Close the client socket after the connection ends
        close(new_socket);
    }

    // Close the server socket (this will never be reached in this example)
    close(server_fd);
    
   
    syslog (LOG_NOTICE, "test daemon terminated.");
    closelog();
    
    return EXIT_SUCCESS;
}