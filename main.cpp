#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <ctype.h>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sstream>
#include <fstream>
#include "http_parser.h"
#include <signal.h>


#define DEFAULT_BUFFER_SIZE 1024

using namespace std;

int on_message_begin(http_parser* _) {
    (void)_;
    printf("\n***MESSAGE BEGIN***\n\n");
    return 0;
}

int on_headers_complete(http_parser* _) {
    (void)_;
    printf("\n***HEADERS COMPLETE***\n\n");
    return 0;
}

int on_message_complete(http_parser* _) {
    (void)_;
    printf("\n***MESSAGE COMPLETE***\n\n");
    return 0;
}

int on_url(http_parser* parser, const char* at, size_t length) {
    strncpy((char *) parser->data, at + 1, length - 1);
    return 0;
}

int on_header_field(http_parser* _, const char* at, size_t length) {
    (void)_;
    printf("Header field: %.*s\n", (int)length, at);
    return 0;
}

int on_header_value(http_parser* _, const char* at, size_t length) {
    (void)_;
    printf("Header value: %.*s\n", (int)length, at);
    return 0;
}

int on_body(http_parser* _, const char* at, size_t length) {
    (void)_;
    printf("Body: %.*s\n", (int)length, at);
    return 0;
}

int on_status(http_parser* _, const char* at, size_t length) {
    (void)_;
    printf("status: %.*s\n", (int)length, at);
    return 0;
}


void doprocessing (int sock) {
    ssize_t recved;
    char buffer[DEFAULT_BUFFER_SIZE];
    bzero(buffer, DEFAULT_BUFFER_SIZE);
    recved = read(sock, buffer, DEFAULT_BUFFER_SIZE);

    if (recved < 0) {
        perror("ERROR reading from socket");
        exit(1);
    }

    http_parser_settings in_settings;
    memset(&in_settings, 0, sizeof(in_settings));
    in_settings.on_message_begin = 0;
    in_settings.on_url = on_url;
    in_settings.on_header_field = 0;
    in_settings.on_header_value = 0;
    in_settings.on_headers_complete = 0;
    in_settings.on_body = 0;
    in_settings.on_message_complete = 0;
    
    char* in_parser_buffer = new char[255];
    memset(in_parser_buffer, 0, 255);
    http_parser in_parser;
    in_parser.data = in_parser_buffer;
    http_parser_init(&in_parser, HTTP_REQUEST);
    size_t nparsed = http_parser_execute(&in_parser, &in_settings, buffer, recved);
    if (nparsed != (size_t) recved) {
        cout << "FAIL!!!" << endl;
    }

    //form the result
    std::stringstream response;

    char buf[1024] = {0};
    FILE *f;

    int i;
    for (i = 0; in_parser_buffer[i] != 0; i++) {
        int c = in_parser_buffer[i];
        if (!isalnum(c) && c != '.')
            break;
    }
    in_parser_buffer[i] = 0;

    f = fopen(in_parser_buffer, "r");

    free(in_parser_buffer);

    if (f == NULL) {

        response << "HTTP/1.1 404 ERROR\r\n"
                "Version: HTTP/1.1\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: 0 \r\n\r\n";

    } else {
        fread(buf, 1, 1023, f);
        fclose(f);

        response << "HTTP/1.1 200 OK\r\nVersion: HTTP/1.1\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: "  << strlen(buf) <<  "\r\n\r\n" << string(buf);
    }

    int n = write(sock, response.str().c_str(), response.str().length());

    if (n < 0) {
        perror("ERROR writing to socket\n");
        exit(1);
    }

}

int main(int argc, char** argv) {
    string ip;
    uint16_t port = 0;
    string dir;
    signal(SIGCHLD, SIG_IGN);
    int opt;

    while ((opt = getopt(argc, argv, "h:p:d:")) != -1) {
        switch (opt) {
            case 'h':
                ip = optarg;
                break;
            case 'p':
                port = (uint16_t) atoi(optarg);
                break;
            case 'd':
                dir = optarg;
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: -h <ip> -p <port> -d <directory>\n");
                exit(EXIT_FAILURE);
        }
    }

    //starting a daemon

    int process_id = fork();

    if (process_id < 0) {
        cout << "Fork failed\n" << endl;
        exit(1);
    }

    //parent process. KILL HIM!!
    if (process_id > 0) {
        cout << "I am parent and I am going to die\n";
        exit(0);
    }

    umask(0);
    int sid = setsid();
    if(sid < 0) {
        // Return failure
        exit(1);
    }


    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    // signal(SIGCHLD, SIG_IGN);
    chdir(dir.c_str());
    signal(SIGCHLD, SIG_IGN);
    //now lets start the server socket
    int sock_fd, newsock_fd, cli_len;
    struct sockaddr_in serv_addr, cli_addr;
    int pid;

    /* First call to socket() function */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    /* Initialize socket structure */
    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    serv_addr.sin_port = htons(port);

    /* Now bind the host address using bind() call.*/
    if (bind(sock_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    /* Now start listening for the clients, here
       * process will go in sleep mode and will wait
       * for the incoming connection
    */

    cli_len = sizeof(cli_addr);
    listen(sock_fd, 100);


    while (1) {
        newsock_fd = accept(sock_fd, (struct sockaddr *) &cli_addr, (socklen_t*) &cli_len);

        if (newsock_fd < 0) {
            perror("ERROR on accept");
            exit(1);
        }

        /* Create child process */
        pid = fork();

        if (pid < 0) {
            perror("ERROR on fork");
            exit(1);
        }

        if (pid == 0) {
            /* This is the client process */
            setsid();
            close(sock_fd);

            doprocessing(newsock_fd);
            close(newsock_fd);
            exit(0);
        }
        else {
            close(newsock_fd);
        }

    } /* end of while */




    return 0;
}