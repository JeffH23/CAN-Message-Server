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
#include <signal.h>//signal handling
/*http server*/
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>

/*threads*/
#include <pthread.h>

#define MAX_CONNECTIONS 5

struct Connection
{
    int fd;
    struct sockaddr fd_addr;
    socklen_t fd_addr_length;
    int options;
    int num_options;
};

struct threaded_func_args
{
    int* TCP_listener_socket;
    struct ConnectionList** connection_list;
    int* num_of_connections;
    void* HTMLFileBuff;
    struct stat* HTML_File_Info;
};

struct ConnectionList
{
    struct Connection connection;
    struct ConnectionList* NextNode;
};

// Function to ignore SIGPIPE
void ignore_sigpipe(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN; // Ignore SIGPIPE
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

int socket_cleanup(struct ConnectionList** connection_list_head,int fd,int* num_of_connections){
    //note to future self: the only reason i passed this pointer to a pointer is so i can set what the original pointer points to
    //cause apperently you cant do that by just: pointer = new_pointer. it looks like it works untill the function returns
    //then the original value is in *pointer. tried, and failed to:connection_list_head = connection_list_head->NextNode;
    (*num_of_connections) --;
    if((*connection_list_head)->connection.fd == fd){//this closes the file disripter if it is on the first node of the linked list
        if(close((*connection_list_head)->connection.fd) !=0){
            printf("close(connection) failed\n");
            return -1;
        }
        if((*connection_list_head)->NextNode == NULL){
            struct Connection connection = {0};
            (*connection_list_head)->connection = connection;
        }
        *connection_list_head = (*connection_list_head)->NextNode;
        return 0;
    }
    struct ConnectionList* current_node = (*connection_list_head)->NextNode;
    struct ConnectionList* previous_node = *connection_list_head;
    while(current_node->connection.fd != fd && current_node->NextNode != NULL){
        previous_node = current_node;
        current_node = current_node->NextNode;
    }
    if(current_node->connection.fd != fd){
        printf("the socket that needs to be closed is not in the list");
        return -1;
    }
    if(close(current_node->connection.fd) !=0){
        printf("close(connection) failed\n");
        return -1;
    }
    previous_node->NextNode = current_node->NextNode;
    free(current_node);
    return 0;
}

int decode_options(char* receive_buffer,int receive_message_length){
    //decode requested options and return the number of that option
    //return 0 = raw TCP connection, 1 = load web page, 2 = expects ServerSentEvents, 3 = expects data for specific IDs(unimplemented)
    if(receive_message_length == 0){
        //raw TCP connection
        return 0;
    }
    if(memcmp(receive_buffer, "GET / ",6) == 0){
        //we received a "GET / "
        return 1;
    }
    if(memcmp(receive_buffer, "GET /events",11) == 0){
        //we received a "GET /events"
        return 2;
    }
    if(memcmp(receive_buffer, "GET /",6) == 0){
        //we received a "GET /" to a unhandled URI
        return -1;
    }
    return -1;//no idea what came in
}

int handle_connections(int* TCP_listener_socket,int* num_of_connections,void* HTMLFileBuff,struct stat* HTML_File_Info, struct ConnectionList** connection_list_head){
    //load a new socket endpoint and attatch link it to the list of connections
    if((*num_of_connections) < 0){
        printf("connection_num invalid");
        return -1;
    }
    //initialize a new connection
    struct Connection connection = {0};
    connection.fd = accept(*TCP_listener_socket,&connection.fd_addr,&connection.fd_addr_length);
    if(connection.fd == -1){
        int errval = errno;
        printf("connection.fd accept() error:\n");
        printf("%i: %s\n", errval, strerror(errval));
        return -1;
    }
    printf("Accepted client fd: %d\n", connection.fd);

    struct ConnectionList* current_connection_list_node = *connection_list_head;
    while(current_connection_list_node->NextNode != NULL){//find the end of the linked list
        current_connection_list_node = current_connection_list_node->NextNode;
    }
    
    if((*num_of_connections) == 0){// this if statement just assigns the connection struct to the first node on the linked list
        (*connection_list_head)->connection = connection;
    }else{// this handles all nodes after the first one
        //new node for linked list
        struct ConnectionList* new_connection = malloc(sizeof(struct ConnectionList)); // allocate memory for the new node
        if(new_connection == NULL){
            printf("Malloc Failed");
            return -1;
        }
        new_connection->connection = connection;//populate the new node
        new_connection->NextNode = NULL;
        current_connection_list_node->NextNode = new_connection;//link the new node
        current_connection_list_node = current_connection_list_node->NextNode;//set the current node to the address of the new node
    }

    //prepare for incoming request
    int receive_buffer_size = 1024;
    char receive_buffer[receive_buffer_size];
    int received_message_length = recv(connection.fd,(void*)receive_buffer,receive_buffer_size*sizeof(char),0);
    if(received_message_length == -1){
        perror("receive_message failed");
        return -1;
    }
    int connection_options = decode_options(receive_buffer, received_message_length);

    int WebPageOpenBuffSize = 1024;
    char WebPageOpenBuff[WebPageOpenBuffSize];
    int WebPageoffset = 0;
    //Stop accepting new connections at MAX_CONNECTIONS
    if((*num_of_connections) > MAX_CONNECTIONS - 2){//hopefully prevents us writing one past the end of connections[]
        printf("Connections at max. Not accepting new connections!!\n");
        //reject HTTP connections
        if(connection_options == 1){//send 503 status code if we reveive a HTTP "GET / "request
            WebPageoffset += snprintf(WebPageOpenBuff, WebPageOpenBuffSize,
            "HTTP/1.1 503 Service Unavalible\r\n"
            "Content-Type: text/plain charset=utf-8\r\n"
            "Server is at maximum connection limit. Please try again later."
            "\r\n");
            if(send(current_connection_list_node->connection.fd, WebPageOpenBuff, WebPageoffset, 0) < 0){
                int errval = errno;
                int ByeByeSocket = socket_cleanup(connection_list_head,current_connection_list_node->connection.fd,num_of_connections);
                printf("HTML send error:\n");
                printf("%i: %s\n", errval, strerror(errval));
                return -1;
            }
        }
        //reject raw TCP connections
        if(connection_options == 0){
        int TCP_msg_buff_size = 28;
        char TCP_msg_buffer[] = "Server at maximum capacity\n";
        if(send(current_connection_list_node->connection.fd, TCP_msg_buffer, TCP_msg_buff_size, 0) < 0){
                int errval = errno;
                int ByeByeSocket = socket_cleanup(connection_list_head,current_connection_list_node->connection.fd,num_of_connections);
                printf("TCP reject letter send error:\n");
                printf("%i: %s\n", errval, strerror(errval));
                return -1;
            }
        }
        if(shutdown(current_connection_list_node->connection.fd, SHUT_RDWR) !=0){
            printf("socket shutdown failed\n");
            return -1;
        }
        int ByeByeSocket = socket_cleanup(connection_list_head,current_connection_list_node->connection.fd,num_of_connections);
        return 0;
    }
    //welcome the new connection
    if(connection_options == 1){// send the webpage if we received a HTTP GET request
        WebPageoffset += snprintf(WebPageOpenBuff, WebPageOpenBuffSize,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: keep-alive\r\n"
            "\r\n");
        if(send(current_connection_list_node->connection.fd, WebPageOpenBuff, WebPageoffset, 0) < 0){
            int errval = errno;
            int ByeByeSocket = socket_cleanup(connection_list_head,current_connection_list_node->connection.fd,num_of_connections);
            printf("HTML send error:\n");
            printf("%i: %s\n", errval, strerror(errval));
            return -1;
        }

        /*Lets try sending a html file*/
        if(send(current_connection_list_node->connection.fd, HTMLFileBuff,HTML_File_Info->st_size, 0) < 0){
            int errval = errno;
            int ByeByeSocket = socket_cleanup(connection_list_head,current_connection_list_node->connection.fd,num_of_connections);
            printf("HTML send error:\n");
            printf("%i: %s\n", errval, strerror(errval));
            return -1;
        }
    }
    //increment the number of connections
    (*num_of_connections)++;
    return 0;
}

int CAN_message_handler(int* CAN_socket_fd,struct ConnectionList** connection_list_head,int* num_of_connections){
        /*read from bound CAN socket*/
        int can_nbytes;
        int CANSendBuffSize = 50;
        char CANSendBuff[CANSendBuffSize];
        int CANBuffOffset = 0;
        struct can_frame frame;

        for(int i=0; i<1; i++){// useless for loop as long as main() calls in a while(1) loop
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
            struct ConnectionList* current_connection_list_node = *connection_list_head;
            for(int k=0; k<(*num_of_connections); k++){
                if(send(current_connection_list_node->connection.fd, CANSendBuff,/* CANSendBuffSize - */CANBuffOffset, 0) < 0){
                    int errval = errno;
                    int ByeByeSocket = socket_cleanup(connection_list_head,current_connection_list_node->connection.fd,num_of_connections);
                    printf("CliSockFD CANBuff send error:\n");
                    printf("%i: %s\n", errval, strerror(errval));
                    return -1;
                }
                //procede to the next node in our linked list
                if(current_connection_list_node->NextNode!=NULL){
                    current_connection_list_node = current_connection_list_node->NextNode;
                }
            }
            
            //printf("%.*s\n",CANBuffOffset,CANSendBuff);
        }
}

void* connection_handler_threadfunc(void* args){
    struct threaded_func_args my_args = *((struct threaded_func_args*)args);
    while(1){
        int c_handle = handle_connections(my_args.TCP_listener_socket,my_args.num_of_connections,my_args.HTMLFileBuff,my_args.HTML_File_Info,my_args.connection_list);
    }
    return NULL;
}

int main(){
    ignore_sigpipe();
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

    struct ConnectionList* connection_linked_list = malloc(sizeof(struct ConnectionList));
    if(connection_linked_list == NULL){
        printf("malloc failed");
        return -1;
    }
    int num_of_connections = 0;
    /*start my connection handling thread*/
    struct threaded_func_args* threadedFuncArgs = malloc(sizeof(struct threaded_func_args));
    if(!threadedFuncArgs){
        printf("malloc error\n");
        return 1;
    }
    threadedFuncArgs->TCP_listener_socket = &InternetSocketfd;
    threadedFuncArgs->connection_list = &connection_linked_list;
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
            int CAN_handler = CAN_message_handler(&CAN_sockfd,&connection_linked_list,&num_of_connections);
            
        }
    }
    return 0;
}
