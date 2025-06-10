//define __USE_MISC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h> // used for error messages
#include <net/if.h> // used fot struct ifreq(interface request) to configure network rescourses
#include <sys/socket.h> //includes main socket functions such as socket(), bind(),etc
#include <sys/ioctl.h> //used for ioctl() to controll network device (e.g., to set CAN mode)
#include <linux/can.h> // Defines CAN-specific structures and constants (e.g., struct can_frame)
#include <linux/can/raw.h> //Defines constants and structures specific to RAW CAN sockets

/*http server*/
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>

/*threads*/
#include <pthread.h>

#define MAX_CONNECTIONS 3

struct Connection
{
    int fd;
    struct sockaddr fd_addr;
    socklen_t fd_addr_length;
    char* options;
    int num_options;
};

struct threaded_func_args
{
    int* TCP_listener_socket;
    struct Connection* connections;
    int* num_of_connections;
    void* HTMLFileBuff;
    struct stat* HTML_File_Info;
};

int handle_connections(int* TCP_listener_socket,struct Connection* connections,int* num_of_connections,void* HTMLFileBuff,struct stat* HTML_File_Info){
    //load a new socket endpoint and attatch it to connections[]
    int connection_num = *num_of_connections; // this will set our index to 1 if there is 1 element at connections[0]
    if(connection_num < 0){
        printf("connection_num invalid");
        return -1;
    }

    struct Connection connection = {0};
    connection.fd = accept(*TCP_listener_socket,&connection.fd_addr,&connection.fd_addr_length);
    if(connection.fd == -1){
        int errval = errno;
        printf("connection.fd accept() error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return -1;
    }
    connections[connection_num] = connection;

    //send index.html
    int WebPageOpenBuffSize = 1024;
    char WebPageOpenBuff[WebPageOpenBuffSize];
    int WebPageoffset = 0;
    if(connection_num >= MAX_CONNECTIONS - 1){
        printf("Connections at max. Not accepting new connections!!\n");
        WebPageoffset += snprintf(WebPageOpenBuff, WebPageOpenBuffSize,
        "HTTP/1.1 503 Service Unavalible\r\n"
        "Content-Type: text/plain charset=utf-8\r\n"
        "Server is at maximum connection limit. Please try again later."
        "\r\n");
        if(send(connections[connection_num].fd, WebPageOpenBuff, WebPageoffset, 0) < 0){
            int errval = errno;
            printf("HTML send error:\n");
            printf("%i: %s\n", errval, strerror(errval));
            return -1;
        }
        if(shutdown(connections[connection_num].fd, SHUT_RDWR) !=0){
            printf("socket shutdown failed\n");
            return -1;
        }
        connections[connection_num] = (struct Connection){0};
        return 0;
    }
    WebPageoffset += snprintf(WebPageOpenBuff, WebPageOpenBuffSize,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: keep-alive\r\n"
        "\r\n");
    if(send(connections[connection_num].fd, WebPageOpenBuff, WebPageoffset, 0) < 0){
        int errval = errno;
        printf("HTML send error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return -1;
    }

    /*Lets try sending a html file*/
    if(send(connections[connection_num].fd, HTMLFileBuff,HTML_File_Info->st_size, 0) < 0){
        int errval = errno;
        printf("HTML send error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return -1;
        }
    (*num_of_connections)++;
    return 0;
}

int CAN_message_handler(int* CAN_socket_fd,struct Connection* connections,int* num_of_connections){
        /*read from bound CAN socket*/
        int can_nbytes;
        int CANSendBuffSize = 50;
        char CANSendBuff[CANSendBuffSize];
        int CANBuffOffset = 0;
        struct can_frame frame;

        for(int i=0; i<1; i++){
            CANBuffOffset = 0;
            can_nbytes = read(*CAN_socket_fd, &frame, sizeof(struct can_frame));
            if(can_nbytes<0){
                perror("CAN frame read error");
                return -1;
            }

            CANBuffOffset += snprintf(CANSendBuff + CANBuffOffset, CANSendBuffSize - CANBuffOffset, "ID: 0x%03X Data: ", frame.can_id);

            for(int j=0; j<frame.can_dlc; j++){
                CANBuffOffset += snprintf(CANSendBuff + CANBuffOffset, CANSendBuffSize - CANBuffOffset,"%02X ", frame.data[j]);
                //printf("%02X ", frame.data[j]);
            }

            CANBuffOffset += snprintf(CANSendBuff + CANBuffOffset, CANBuffOffset, "  __  \r");
            //printf("Bytes:%i, Frame: %s",offset, CANSendBuff);

            /*Send CAN frames to all connections*/
            for(int k=0; k<(*num_of_connections); k++){
                if(send(connections[0].fd, CANSendBuff,/* CANSendBuffSize - */CANBuffOffset, 0) < 0){
            int errval = errno;
            printf("CliSockFD CANBuff send error:\n");
            printf("%i: %s\n", errval, strerror(errval));
            return -1;
            }
            }
            
            printf("%.*s\n",CANBuffOffset,CANSendBuff);
        }
}

void* connection_handler_threadfunc(void* args){
    struct threaded_func_args my_args = *((struct threaded_func_args*)args);
    while(1){
        int c_handle = handle_connections(my_args.TCP_listener_socket,my_args.connections,my_args.num_of_connections,my_args.HTMLFileBuff,my_args.HTML_File_Info);
    }
    return NULL;
}

int main(){
    /*load a index.html*/
    const char *indexHTMLPath = "index.html";
    int indexHTML = open(indexHTMLPath, O_RDONLY);
    if (indexHTML == -1) {
        perror("Failed to open file");
        return 1;
    }
    struct stat file_info;
    char *HTMLFileBuff;
    ssize_t HTMLFileBytesRead;
    if (fstat(indexHTML, &file_info) == -1) {
        perror("fstat failed");
        close(indexHTML);
        return 1;
    }
    HTMLFileBuff = malloc(file_info.st_size);
    if(HTMLFileBuff == NULL){
        perror("Failed to malloc() HTMLFileBuff");
        close(indexHTML);
        return 1;
    }
    HTMLFileBytesRead = read(indexHTML,HTMLFileBuff,file_info.st_size);
    if(HTMLFileBytesRead == -1){
        perror("Failed to read file");
        free(HTMLFileBuff);
        close(indexHTML);
        return 1;
    }
    printf("HTML file is %i bytes long\n",file_info.st_size);

    /*TCP listener socket setup*/
    struct sockaddr_in InternetSockAddr;
    InternetSockAddr.sin_family = AF_INET;
    InternetSockAddr.sin_port = htons(8080);
    InternetSockAddr.sin_addr.s_addr = INADDR_ANY; //listens for connections from any IP address

    int InternetSocketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(InternetSocketfd == -1){
        int errval = errno;
        printf("Look Here --> int InternetSocketFD = socket()\n");
        printf("%s\n",strerror(errval));
        return -1;
    }

    if(bind(InternetSocketfd, (const struct sockaddr *) &InternetSockAddr, sizeof(InternetSockAddr)) == -1){
        int errval = errno;
        printf("Binding error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return -1;
    }

    if(listen(InternetSocketfd,5) !=0){
        int errval = errno;
        printf("listen() error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return 1;
    }

    struct Connection connections[MAX_CONNECTIONS];
    int num_of_connections = 0;
    /*start my connection handling thread*/
    struct threaded_func_args* threadedFuncArgs = malloc(sizeof(struct threaded_func_args));
    if(!threadedFuncArgs){
        printf("malloc error\n");
        return 1;
    }
    threadedFuncArgs->TCP_listener_socket = &InternetSocketfd;
    threadedFuncArgs->connections = connections;
    threadedFuncArgs->num_of_connections = &num_of_connections;
    threadedFuncArgs->HTMLFileBuff = HTMLFileBuff;
    threadedFuncArgs->HTML_File_Info = &file_info;

    pthread_t thread;
    if(pthread_create(&thread,NULL,connection_handler_threadfunc,(void*)threadedFuncArgs) !=0){
        printf("thread creation error");
        free(threadedFuncArgs);
        return 1;
    }

    /*CAN interface connection*/
    int CAN_sockfd;

    struct sockaddr_can CANsockaddr; //has members: sa_family_t can_family, int can_ifindex, and a union

    struct ifreq ifr; //interface request variable

    CAN_sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if(CAN_sockfd<0){
        perror("Error while opening socket");
        return -1;
    }

    strcpy(ifr.ifr_name, "vcan0"); //this copies "can0" to ifr.ifr_name. not sure why not just set ifr_name = "can0" just following orders

    ioctl(CAN_sockfd, SIOCGIFINDEX, &ifr); // asks the system for the index of the interface who's name is in ifr.ifr_name

    /*Bind up that socket to the CAN interface baby!*/
    CANsockaddr.can_family = AF_CAN;
    CANsockaddr.can_ifindex = ifr.ifr_ifindex;

    if(bind(CAN_sockfd, (const struct sockaddr *) &CANsockaddr, sizeof(struct sockaddr_can)) == !0){
        printf("bind failed");
        return -1;
    }
    /*start the message handler*/
    while(1){
        while(num_of_connections > 0){
            int CAN_handler = CAN_message_handler(&CAN_sockfd,connections,&num_of_connections);
            
        }
    }
    return 0;
}
