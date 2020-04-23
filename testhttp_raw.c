#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>
#include "err.h"

#define METHOD_LEN 3
#define HTTP_VERSION_LEN 8
#define BUFFER_SIZE 8192 // 8 kb

#define CORRECT_RESPONSE "HTTP/1.1 200 OK"
#define CORRECT_RESPONSE_LEN 15

#define CHUNKED_MSG "Transfer-Encoding: chunked"

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

    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\n", target, host);
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

bool is_hex_number(char *str) {
    int i = 0;
    char *ptr = NULL;

    while ((str[i] != '\0') && (str[i] != '\r') && (str[i] != ';')) {
        if ((str[i] >= '0' && str[i] <= '9')
            || (str[i] >= 'a' && str[i] <= 'f')
            || (str[i] >= 'A' && str[i] <= 'F')) {
            i++;
        } else {
            return false;
        }
    }

    if (i == 0) {
        return false;
    }

    if ((str[i] == '\r') && (str[i + 1] != '\n')) {
        return false;
    }

    if (str[i] == ';') {
        ptr = strchr(str, '\n');
        ptr--;
        if (*ptr != '\r') {
            return false;
        }
    }

    return true;
}

char *read_content(char *buff_content_part, int sock) {
    size_t content_len = 0;
    size_t sum = strlen(buff_content_part) + 1;
    ssize_t rcv_len = 0;
    char *content = NULL;
    char buffer[BUFFER_SIZE];

    strcpy(buffer, buff_content_part);
    do {
        if (rcv_len < 0) {
            syserr("read");
        }

        sum += rcv_len;
        if (content_len < sum) {
            content_len = (3 * sum) / 2;
            content = (char *) realloc(content, content_len * sizeof(char));
            if (content == NULL) {
                syserr("realloc");
            }
        }
        strcat(content, buffer);
        memset(buffer, 0, sizeof(buffer));
    } while ((rcv_len = read(sock, buffer, sizeof(buffer) - 1)) > 0);

    return content;
}

char *parse_chunked_content(char *content) {
    size_t content_len = strlen(content) + 1;
    size_t chunk_size = 0, sum = 0;
    char *parsed_content = NULL;
    char *start = NULL, *end = NULL;

    parsed_content = (char *) malloc(content_len * sizeof(char));
    if (parsed_content == NULL) {
        syserr("malloc");
    }
    parsed_content[0] = '\0';

    start = content;
    while ((end = strstr(start, "\r\n")) != NULL) {
        if (!is_hex_number(start)) {
            fatal("wrong content");
        }

        chunk_size = strtoul(start, &start, 16);
        if (chunk_size == 0) {
            break;
        }
        sum += chunk_size;

        start = end + 2;
        end = start;
        end = strstr(end, "\r\n");
        while ((end - start) < (ssize_t) chunk_size) {
            end += 2;
            end = strstr(end, "\r\n");
        }

        *end = '\0';
        strcat(parsed_content, start);
        *end = '\r';
        start = end + 2;
    }

    printf("%lu\n", sum);

    return parsed_content;
}

void generate_report(char *buffer, int sock) {
    char *end_of_header = strstr(buffer, "\r\n\r\n");
    end_of_header++;
    *end_of_header = '\0';

    if (strncmp(buffer, CORRECT_RESPONSE, CORRECT_RESPONSE_LEN) != 0) {
        char *info_end = strstr(buffer, "\r\n");
        *info_end = '\0';
        printf("%s\n", buffer);
    } else {
        char *content = read_content(end_of_header + 3, sock);
        print_cookies(buffer);
        if (strcasestr(buffer, CHUNKED_MSG) != NULL) {  
            // moze tu byc jakis error z valgrinda
            char *parsed = parse_chunked_content(content);
            free(content);
            content = parsed;
        }
        printf("Dlugosc zasobu: %lu\n", strlen(content));
    }
}

char *get_port_num(char *arg) {
    char *result = arg;

    while ((*result != '\0') && (*result != ':')) {
        result++;
    }

    if (*result == '\0') {
        fatal("port number missing");
    }
    *result = '\0';

    return (result + 1);
}

int main(int argc, char *argv[]) {
    int sock, err;
    char *request = NULL;
    char buffer[BUFFER_SIZE];
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;
    ssize_t send_len, rcv_len;

    if (argc != 4) {
        fatal("wrong number of arguments");
    }

    // Upewnij się, że struktura jest wyzerowana.
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;

    err = getaddrinfo(argv[1], get_port_num(argv[1]), &addr_hints, &addr_result);
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

    generate_report(buffer, sock);
    
    free(request);

    return 0;
}
