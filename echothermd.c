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
#include <fcntl.h>
#include <errno.h>

#define PORT 8888
#define BUFFER_SIZE 1024
#define LOCK_FILE "/tmp/echothermd.lock"

volatile int running = 1;

// Signal handler for SIGTERM
void handle_sigterm(int sig) {    
    syslog (LOG_NOTICE, "Received SIGTERM, shutting down daemon...\n");            
    //remove our lock file
    remove(LOCK_FILE);
    closelog();     
    running = 0;
}
   
static void start_daemon()
{
    pid_t pid;
    
    // Fork off the parent process
    pid = fork();
    
    // An error occurred 
    if (pid < 0)
        exit(EXIT_FAILURE);
    
    // Success: Let the parent terminate
    if (pid > 0)
        exit(EXIT_SUCCESS);
    
    // On success: The child process becomes session leader
    if (setsid() < 0)
        exit(EXIT_FAILURE);
    
    // Catch, ignore and handle signals     
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    
    // Fork off for the second time
    pid = fork();
    
    // An error occurred 
    if (pid < 0)
        exit(EXIT_FAILURE);
    
    // Success: Let the parent terminate 
    if (pid > 0)
        exit(EXIT_SUCCESS);
    
    // Set new file permissions
    umask(0);
    
    // Change the working directory to the root directory 
    // or another appropriated directory 
    chdir("/");
    
    // Close all open file descriptors 
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        close (x);
    }
    
    // Open the log file 
    openlog ("echothermd", LOG_PID, LOG_DAEMON);
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

// Function to check if lock file exists
int check_lock() {
    int fd;

    fd = (access(LOCK_FILE, F_OK) != -1);  //1 if exists
  
    if (fd != 1) {
        //file doesn't exist       
        int wfd = open(LOCK_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (wfd < 0) {            
            return -1;
        }       
        // Close the file descriptor
        close(wfd);        
        return 0;
    }       
}

int is_port_available(int port) {
    int sockfd;
    struct sockaddr_in addr;
    int result;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Set up the sockaddr_in structure
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    // Attempt to bind the socket to the port
    result = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (result < 0) {
        if (errno == EADDRINUSE) {
            // Port is already in use
            close(sockfd);
            return 0;
        } else {
            // Error occurred
            perror("bind");
            close(sockfd);
            return -1;
        }
    }

    // Port is available
    close(sockfd);
    return 1;
}

int main(int argc, char *argv[]) 
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    int valread;
    int lock_fd;
    struct sigaction sa;    

    // Set up the SIGTERM handler
    sa.sa_handler = handle_sigterm;
    sa.sa_flags = 0;  // or SA_RESTART
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }

    if (argc == 2) {
        if (strcmp(argv[1], "--kill") == 0) {
            // Execute a system command (e.g., killall echothermd)
            printf("Killing the existing instance...\nPlease run ./echothermd again if you wish to restart the daemon.\n");
            system("kill $(pgrep echothermd)");
            exit(EXIT_SUCCESS);

        }
    }
    //output startup info
    //No idea why, but you cannot do a printf without downstream affects, e.g. causing the socket below misbehaving. strange...
    const char *startup = "Starting EchoTherm daemon, v0.01 © EchoMAV, LLC 2024\nLogging output to /var/log/syslog\n";
    write(1, startup, strlen(startup));
    
    // Using the FILE_LOCK to know if this system is already running
    if (check_lock()) {
        printf("Error: another instance of the program is already running.\n");
        printf("To shutdown, run ./echothermd --kill\n");
        exit(EXIT_FAILURE);
    }

    if (is_port_available(PORT) != 1)
    {
        printf("Error: port %d is not available for binding...\n", PORT);
        exit(EXIT_FAILURE);
    }
        
    //start the daemon    
    start_daemon();    
    
    syslog (LOG_NOTICE, "EchoTherm Daemon Started.");

    //TODO: add seek camera init code here

    // create a socket to listen for commands
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
    while (running) {      
        // Accept an incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {            
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // Read commands from the connected client
        while ((valread = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
            buffer[valread] = '\0';
            //syslog (LOG_NOTICE, "Received command: %s\n", buffer);
            parse_command(buffer);
        }       
              
        close(new_socket);
    }

    // Close the server socket
    close(server_fd);
       
    closelog();
    
    return EXIT_SUCCESS;
}