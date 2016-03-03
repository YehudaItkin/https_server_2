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


ssize_t
sock_fd_write(int sock, void *buf, ssize_t buflen, int fd)
{
    ssize_t  size;
    struct msghdr msg;
    struct iovec iov;
    union {
        struct cmsghdr  cmsghdr;
        char control[CMSG_SPACE(sizeof (int))];
    } cmsgu;
    struct cmsghdr  *cmsg;

    iov.iov_base = buf;
    iov.iov_len = buflen;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (fd != -1) {
        msg.msg_control = cmsgu.control;
        msg.msg_controllen = sizeof(cmsgu.control);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(sizeof (int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;

        // printf ("passing fd %d\n", fd);
        *((int *) CMSG_DATA(cmsg)) = fd;
    } else {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        // printf ("not passing fd\n");
    }

    size = sendmsg(sock, &msg, 0);

    if (size < 0)
        perror ("sendmsg");
    return size;
}

ssize_t
sock_fd_read(int sock, void *buf, ssize_t bufsize, int *fd)
{
    ssize_t     size;

    if (fd) {
        struct msghdr   msg;
        struct iovec    iov;
        union {
            struct cmsghdr  cmsghdr;
            char control[CMSG_SPACE(sizeof (int))];
        } cmsgu;
        struct cmsghdr  *cmsg;

        iov.iov_base = buf;
        iov.iov_len = bufsize;

        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cmsgu.control;
        msg.msg_controllen = sizeof(cmsgu.control);
        size = recvmsg (sock, &msg, 0);
        if (size < 0) {
            perror ("recvmsg");
            exit(1);
        }
        cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
            if (cmsg->cmsg_level != SOL_SOCKET) {
                fprintf (stderr, "invalid cmsg_level %d\n",
                         cmsg->cmsg_level);
                exit(1);
            }
            if (cmsg->cmsg_type != SCM_RIGHTS) {
                fprintf (stderr, "invalid cmsg_type %d\n",
                         cmsg->cmsg_type);
                exit(1);
            }

            *fd = *((int *) CMSG_DATA(cmsg));
            //printf ("received fd %d\n", *fd);
        } else
            *fd = -1;
    } else {
        size = read (sock, buf, bufsize);
        if (size < 0) {
            perror("read");
            exit(1);
        }
    }
    return size;
}

int on_url(http_parser* parser, const char* at, size_t length) {
    strncpy((char *) parser->data, at + 1, length - 1);
    return 0;
}


void worker(int sock) {
    ssize_t recved;
    int fd;
    char buffer[DEFAULT_BUFFER_SIZE];
    bzero(buffer, DEFAULT_BUFFER_SIZE);

    //parser setting
    http_parser_settings in_settings;
    memset(&in_settings, 0, sizeof(in_settings));
    in_settings.on_message_begin = 0;
    in_settings.on_url = on_url;
    in_settings.on_header_field = 0;
    in_settings.on_header_value = 0;
    in_settings.on_headers_complete = 0;
    in_settings.on_body = 0;
    in_settings.on_message_complete = 0;

    http_parser in_parser;
    http_parser_init(&in_parser, HTTP_REQUEST);

    char *in_parser_buffer = new char[255];

    sleep(1);
    char buf[16];

    while (1) {
        int size = sock_fd_read(sock, buf, 16, &fd);

        if (size <= 0)
            break;

        recved = read(fd, buffer, DEFAULT_BUFFER_SIZE);
        if (recved < 0) {
            // cout << "read failed" << endl;
        }

        memset(in_parser_buffer, 0, 255);
        in_parser.data = in_parser_buffer;
        size_t nparsed = http_parser_execute(&in_parser, &in_settings, buffer, recved);

        if (nparsed != (size_t) recved) {
            // cout << "FAIL!!!" << endl;
            close(fd);
            continue;
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
                    "Content-Length: " << strlen(buf) << "\r\n\r\n" << string(buf);
        }
        if (fd != -1) {
            // cout << response.str() << endl;
            int n = write(fd, response.str().c_str(), response.str().length());

            if (n < 0)
                perror("ERROR writing to socket\n");
            close(fd);
        }
    }
    free(in_parser_buffer);
}


void master(int sock, int sock_fd) {
    int newsock_fd;
    struct sockaddr_in cli_addr;
    int cli_len = sizeof(cli_addr);

    while (1) {
        newsock_fd = accept(sock_fd, (struct sockaddr *) &cli_addr, (socklen_t *) &cli_len);

        if (newsock_fd < 0) {
            perror("ERROR on accept");
            exit(1);
        }
        const char *buf = "0";
        sock_fd_write(sock, (void*) buf, 1, newsock_fd);
    }

}
int main(int argc, char** argv) {
    string ip;
    uint16_t port = 0;
    string dir;
    signal(SIGCHLD, SIG_IGN);
    int opt;
    int deamonize = 1;

    while ((opt = getopt(argc, argv, "h:p:d:b:")) != -1) {
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
            case 'b':
                deamonize = atoi(optarg);
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: -h <ip> -p <port> -d <directory>\n");
                exit(EXIT_FAILURE);
                break;
        }
    }

    if (deamonize) {
        //starting a daemon
        signal(SIGCHLD, SIG_IGN);
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
        if (sid < 0) {
            // Return failure
            exit(1);
        }


        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    chdir(dir.c_str());
    //now lets start the server socket
    int sock_fd;
    struct sockaddr_in serv_addr;
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

    listen(sock_fd, 100);

    //Black descriptor magic
    int sv[2];

    int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, sv);
    if (res < 0) {
        perror("Socket pairing failed!");
        exit(1);
    }

    signal(SIGCHLD, SIG_IGN);
    pid = fork();
    if (pid < 0) {
        cout << "Fork failed!" << endl;
        exit(1);
    }

    switch (pid) {
        case 0:
            close(sv[0]);
            worker(sv[1]);
            break;

        default:
            close(sv[1]);
            master(sv[0], sock_fd);
            break;
    };





    return 0;
}