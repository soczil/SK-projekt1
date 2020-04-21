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
    memset(cookies, 0, sizeof(cookies));
    get_cookies_header(file, cookies);

    fclose(file);

    int host_len = get_host_len(address_suff);
    int target_len = get_target_len(address_suff, host_len);
    char host[host_len + 1];
    char target[target_len + 1];
    char *request = NULL;
    int request_len = 8192;
    
    if (sscanf(address_suff, "%[^/]%s", host, target) != 2) {
        fatal("wrong third argument");
    }

    request = (char *) malloc(request_len * sizeof(char));
    if (request == NULL) {
        syserr("malloc");
    }

    // sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\nCookie: %s\r\nConnection: close\r\n\r\n", target, host, cookies);
    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\n", target, host);
    // strcat(request, "GET ");
    // strcat(request, target);
    // strcat(request, " HTTP/1.1\r\n");
    // strcat(request, "Host: ");
    // strcat(request, host);
    // strcat(request, "\r\n");
    if (file_size > 0) {
        strcat(request, "Cookie: ");
        strcat(request, cookies);
        strcat(request, "\r\n");
    }
    strcat(request, "Connection: close\r\n");
    strcat(request, "\r\n");
    
    return request;
}

// Wywoływać dopiero, gdy już nie będzie nam potrzebny header.
void print_cookies(char *header) {
    char *cookie = strcasestr(header, "Set-Cookie");
    char *end_of_cookie = NULL;
    int cookie_counter = 0;
    char cookies[strlen(header)]; // Jakies lepsze oszacowanie moze!!!!!!!!

    memset(cookies, 0, sizeof(cookies));
    while (cookie != NULL) {
        cookie_counter++;
        cookie += 12;   // JAKIS DEFINE!!!!!!!!!!
        end_of_cookie = cookie;
        while ((*end_of_cookie != ';') && (*end_of_cookie != '\r')) {
            end_of_cookie++;
        }
        *end_of_cookie = '\0';
        strcat(cookies, cookie);
        strcat(cookies, "\n");
        cookie = end_of_cookie + 1;
        cookie = strcasestr(cookie, "Set-Cookie");
    }

    printf("%d\n", cookie_counter);
    if (cookie_counter > 0) {
        printf("%s", cookies);
    }
}

size_t count_size_chunked(char *content, int sock) {
    char buffer[BUFFER_SIZE];
    size_t chunk_size = 0, sum = 0;
    ssize_t rcv_len = 0;
    char *number_start = NULL, *number_end = NULL;
    strcpy(buffer, content);

    do {
        if (rcv_len < 0) {
            syserr("read");
        }

        number_end = buffer;
        while ((number_start = strstr(number_end, "\r\n")) != NULL) {
            number_start += 2;
            chunk_size = strtoul(number_start, &number_end, 16);
            if (chunk_size == 0) {
                break;
            }

            sum += chunk_size;
            number_end = strchr(number_end, '\n');
        }

        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, "\r\n");
    } while ((rcv_len = read(sock, buffer + 2, sizeof(buffer) - 3)));

    return sum;
}

void generate_report(char *buffer, int sock) {
    char *end_of_header = strstr(buffer, "\r\n\r\n");
    end_of_header++;
    *end_of_header = '\0';
    //puts(buffer);

    if (strncmp(buffer, CORRECT_RESPONSE, CORRECT_RESPONSE_LEN) != 0) {
        char *info_end = strstr(buffer, "\r\n");
        *info_end = '\0';
        printf("%s\n", buffer);
    } else {
        if (strcasestr(buffer, CHUNKED_MSG) != NULL) {
            print_cookies(buffer);
            printf("Dlugosc zasobu: %lu\n", count_size_chunked(end_of_header + 1, sock));
        } else {
            print_cookies(buffer);
            printf("normal");
        }
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

    //printf("%s", buffer);
    generate_report(buffer, sock);
    
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
