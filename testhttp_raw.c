#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "err.h"

#define METHOD_LEN 3
#define HTTP_VERSION_LEN 8
#define BUFFER_SIZE 8192   // 8 kb

#define CORRECT_RESPONSE "HTTP/1.1 200 OK\r\n"
#define CORRECT_RESPONSE_LEN 17

#define CHUNKED_MSG "Transfer-Encoding: chunked\r\n"

int check_address(char *arg) {
    int i = 0;

    while ((arg[i] != '\0') && (arg[i] != ':')) {
        i++;
    }

    if ((arg[i] == '\0') || (i == 0)) {
        fatal("wrong first argument");
    }

    return i;
}

int check_port_num(char *arg, int start) {
    int i = start;

    while (arg[i] != '\0') {
        if ((arg[i] < '0') || (arg[i] > '9')) {
            fatal("wrong port number");
        }
        i++;
    }

    if (i == start) {
        fatal("wrong port number");
    }

    return (i - start);
}

int get_host_len(char *arg) {
    int i = 0;

    while ((arg[i] != '\0') && (arg[i] != '/')) {
        i++;
    }

    if (arg[i] == '\0') {
        fatal("wrong host");
    }

    return i;
}

int get_target_len(char *arg, int start) {
    int i = start;

    while (arg[i] != '\0') {
        i++;
    }

    if (i == start) {
        fatal("wrong target");
    }

    return (i - start);
}

int get_file_size_in_chars(FILE *file) {
    fseek(file, 0L, SEEK_END);
    int size = ftell(file);
    rewind(file);
    return (size / sizeof(char));
}

void get_cookies_header(FILE *file, char *cookies) {
    char *line = NULL;
    size_t size = 0;
    int line_size = 0;

    while ((line_size = getline(&line, &size, file)) != -1) {
        if (line[line_size - 1] == '\n') {
            line[line_size - 1] = ';';
        }
        strcat(cookies, line);

        if (line[line_size - 1] != ';') {
            strcat(cookies, ";");
        }
        strcat(cookies, " ");
    }

    char *last_space = strrchr(cookies, ' ');
    *last_space = '\0';
}

char *build_request(char *address, char *file_name) {
    int address_len = strlen(address);
    char address_suff[address_len + 1];
    
    if (sscanf(address, "http://%s", address_suff) != 1) {
        if (sscanf(address, "https://%s", address_suff) != 1) {
            fatal("wrong third argument");
        }
    }

    FILE *file = fopen(file_name, "r");
    if (file == NULL) {
        fatal("couldn't open file %s", file_name);
    }

    int file_size = get_file_size_in_chars(file);
    char cookies[file_size + ((file_size + 1) / 3)];
    get_cookies_header(file, cookies);

    fclose(file);

    int host_len = get_host_len(address_suff);
    int target_len = get_target_len(address_suff, host_len);
    char host[host_len];
    char target[target_len];
    char *request = NULL;
    int request_len = (3 + 1 + target_len + 1 + 8 + 1 + 5 + host_len + 2 + 18 + 4 + file_size + ((file_size + 1) / 3));

    if (sscanf(address_suff, "%[^/]%s", host, target) != 2) {
        fatal("wrong third argument");
    }

    request = (char *) malloc(request_len * sizeof(char));
    if (request == NULL) {
        syserr("malloc");
    }

    strcat(request, "GET ");
    strcat(request, target);
    strcat(request, " HTTP/1.1\r\n");
    strcat(request, "Host: ");
    strcat(request, host);
    strcat(request, "\r\n");
    strcat(request, "Cookie: ");
    strcat(request, cookies);
    strcat(request, "\r\n");
    strcat(request, "Connection: close\r\n");
    strcat(request, "\r\n");
    
    return request;
}

// Wywoływać dopiero, gdy już nie będzie nam potrzebny header.
void print_cookies(char *header) {
    char *cookie = strcasestr(header, "Set-Cookie");
    char *end_of_cookie;

    while (cookie != NULL) {
        cookie += 12;
        end_of_cookie = cookie;
        while ((*end_of_cookie != ';') && (*end_of_cookie != '\r')) {
            end_of_cookie++;
        }
        *end_of_cookie = '\0';
        printf("%s\n", cookie);
        cookie = end_of_cookie + 1;
        cookie = strcasestr(cookie, "Set-Cookie");
    }
}

void generate_report(char *header) {
    if (strncmp(header, CORRECT_RESPONSE, CORRECT_RESPONSE_LEN) != 0) {
        char *info_end = strstr(header, "\r\n");
        *info_end = '\0';
        printf("%s\n", header);
    } else {
        if (strcasestr(header, CHUNKED_MSG) != NULL) {
            print_cookies(header);
            printf("chunked\n");
        } else {
            print_cookies(header);
            printf("normal");
        }
        
        //printf("%s\n", header);

        // char *number = strstr(buffer, "\r\n\r\n");
        // number += 4;
        // char *end = strstr(number, "\r\n");
        // *end = '\0';
        // printf("%s", number);

        // int size = 0;
        // sscanf(buffer, "\r\n\r\n%d", &size);
        // printf("%d\n", size);        
        
    }
}

int main(int argc, char *argv[]) {
    int sock, err, address_len, port_num_len;
    char *address = NULL, *port_str = NULL, *request = NULL;
    char buffer[BUFFER_SIZE];
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;
    ssize_t send_len, rcv_len;

    if (argc != 4) {
        fatal("wrong number of arguments");
    }

    address_len = check_address(argv[1]);
    port_num_len = check_port_num(argv[1], address_len + 1);

    address = (char *) malloc((address_len * sizeof(char)) + 1);
    if (address == NULL) {
        syserr("malloc");
    }

    port_str = (char *) malloc((port_num_len * sizeof(char)) + 1);
    if (port_str == NULL) {
        syserr("malloc");
    }

    if (sscanf(argv[1], "%[^:]:%s", address, port_str) != 2) {
        fatal("wrong first argument");
    }

    // Upewnij się, że struktura jest wyzerowana.
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;

    err = getaddrinfo(address, port_str, &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) {
        syserr("getaddrinfo: %s", gai_strerror(err));
    } else if (err != 0) {
        fatal("getaddrinfo: %s", gai_strerror(err));
    }

    sock = socket(addr_result->ai_family, addr_result->ai_socktype,
                  addr_result->ai_protocol);
    if (sock < 0) {
        syserr("socket");
    }

    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        syserr("connect");        
    }

    freeaddrinfo(addr_result);

    request = build_request(argv[3], argv[2]);
    send_len = strlen(request);
    if (write(sock, request, send_len) != send_len) {
        syserr("partial / failed write");
    }

    memset(buffer, 0, sizeof(buffer));
    rcv_len = read(sock, buffer, sizeof(buffer) - 1);
    if (rcv_len < 0) {
        syserr("read");
    }

    char *end_of_header = strstr(buffer, "\r\n\r\n");
    end_of_header += 3;
    *end_of_header = '\0';

    //printf("%s", buffer);
    generate_report(buffer);
    
    // while ((rcv_len = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
    //     printf("%s\n", buffer);
    //     memset(buffer, 0, sizeof(buffer) - 1);
    // }

    // read(sock, buffer, sizeof(buffer) - 1);
    // printf("%s", buffer);


    free(address);
    free(port_str);
    free(request);

    return 0;
}
