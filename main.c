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

#define MAX_CONNECTIONS 5

//int TCP_init(){
    /*TCP listener socket setup*/
/*    struct sockaddr_in InternetSockAddr;
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
}*/


struct Connection
{
    int fd;
    struct sockaddr fd_addr;
    socklen_t fd_addr_length;
    char* options;
    int num_options;
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
        "Content-Type: text/plain\r\n"
        "Server is at maximum connection limit. Please try again later."
        "\r\n");
        return 2;
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
//    TCP_Listener = TCP_init();
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

    /*Accept TCP connection*/

/*    struct sockaddr CliSock;
    int CliSockAddrLen = sizeof(CliSock);
    int CliSockFD = accept(InternetSocketfd,(struct sockaddr *)&CliSock, (socklen_t *)&CliSockAddrLen);
    if(CliSockFD == -1){
        int errval = errno;
        printf("CliSockFD accept() error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return -1;
    }*/
    /*Open HTTP connection*/
/*    int WebPageOpenBuffSize = 1024;
    char WebPageOpenBuff[WebPageOpenBuffSize];
    int WebPageoffset = 0;
    WebPageoffset += snprintf(WebPageOpenBuff, WebPageOpenBuffSize,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: keep-alive\r\n"
        "\r\n");
    if(send(CliSockFD, WebPageOpenBuff, WebPageoffset, 0) < 0){
        printf("Send failed");
        return -1;
        }
 */   //printf("%.*c", WebPageOpenBuffSize - WebPageoffset, WebPageOpenBuff); // this was sending '@' for some reason

    /*Lets try sending a html file*/
/*    if(send(CliSockFD, HTMLFileBuff, file_info.st_size, 0) < 0){
        int errval = errno;
        printf("CliSockFD CANBuff send error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return -1;
        }
*/
    struct Connection connections[MAX_CONNECTIONS];
    int num_of_connections = 0;

    int c_handle = handle_connections(&InternetSocketfd,connections,&num_of_connections,HTMLFileBuff,&file_info);


    /*CAN interface connection*/
    int can_sockfd;
    int can_nbytes;

    struct sockaddr_can sockaddr; //has members: sa_family_t can_family, int can_ifindex, and a union

    struct ifreq ifr; //interface request variable

    struct can_frame frame;

    can_sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if(can_sockfd<0){
        perror("Error while opening socket");
        return -1;
    }

    strcpy(ifr.ifr_name, "vcan0"); //this copies "can0" to ifr.ifr_name. not sure why not just set ifr_name = "can0" just following orders

    ioctl(can_sockfd, SIOCGIFINDEX, &ifr); // asks the system for the index of the interface who's name is in ifr.ifr_name

    /*Bind up that socket to the CAN interface baby!*/
    sockaddr.can_family = AF_CAN;
    sockaddr.can_ifindex = ifr.ifr_ifindex;

    if(bind(can_sockfd, (const struct sockaddr *) &sockaddr, sizeof(sockaddr)) == !0){
        printf("bind failed");
        return -1;
    }
    /*read from bound CAN socket*/
    int CANSendBuffSize = 50;
    char CANSendBuff[CANSendBuffSize];
    int CANBuffOffset = 0;

    for(int i=0; i<100; i++){
        CANBuffOffset = 0;
        can_nbytes = read(can_sockfd, &frame, sizeof(struct can_frame));
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

        /*Send CAN frames to CliSockFD*/
        
        if(send(connections[0].fd, CANSendBuff,/* CANSendBuffSize - */CANBuffOffset, 0) < 0){
        int errval = errno;
        printf("CliSockFD CANBuff send error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return -1;
        }
        printf("%.*s\n",CANBuffOffset,CANSendBuff);
    }
}
