#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <ctype.h>
#include "err.h"

#define BUFFER_SIZE 8192 // 8 kb
#define COOKIES_BUFFER_SIZE 4096 // 4kb
#define SET_COOKIE_LEN 11 // Długość nagłówka 'Set-Cookie:'.

#define OK_RESPONSE "HTTP/1.1 200 OK"
#define OK_RESPONSE_LEN 15

/**
 * Daje w wyniku długość adresu serwera, z którego będziemy ściągać zasób.
 */
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

/**
 * Daje w wyniku długość wskazania zasobu do pobrania.
 */
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

/**
 * Buduje nagłówek z ciasteczkami ('Cookie') na podstawie podanego pliku.
 */
void get_cookies_header(FILE *file, char *cookies) {
    char *line = NULL;
    size_t size = 0, sum = 0;
    int line_size = 0;

    while ((line_size = getline(&line, &size, file)) != -1) {
        if (line[line_size - 1] == '\n') {
            line[line_size - 1] = ';';
            sum += (line_size + 1); // Długość ciasteczka + spacja.
        } else {
            sum += (line_size + 2); // Długość ciasteczka + średnik + spacja.
        }

        if (sum > (COOKIES_BUFFER_SIZE - 1)) {
            // Więcej ciasteczek nie zmieści się do bufora.
            break;
        }

        strcat(cookies, line);
        if (line[line_size - 1] != ';') {
            strcat(cookies, ";");
        }
        strcat(cookies, " ");
    }

    char *last_space = strrchr(cookies, ' ');
    if (last_space != NULL) {
        *last_space = '\0';
    }

    free(line);
}

/**
 * Buduje zapytanie, które następnie wysyłane jest do serwera.
 */
char *build_request(char *address, char *file_name) {
    size_t address_len = strlen(address);
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

    char cookies[COOKIES_BUFFER_SIZE];
    memset(cookies, 0, sizeof(cookies));
    get_cookies_header(file, cookies);

    if (fclose(file)) {
        syserr("close file");
    }

    int host_len = get_host_len(address_suff);
    int target_len = get_target_len(address_suff, host_len);
    char host[host_len + 1];
    char target[target_len + 1];
    char *request = NULL;
    
    if (sscanf(address_suff, "%[^/]%s", host, target) != 2) {
        fatal("wrong third argument");
    }

    request = (char *) malloc(BUFFER_SIZE * sizeof(char));
    if (request == NULL) {
        syserr("malloc");
    }
    request[0] = '\0';

    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\n", target, host);
    if (cookies[0] != 0) {
        strcat(request, "Cookie: ");
        strcat(request, cookies);
        strcat(request, "\r\n");
    }
    strcat(request, "Connection: close\r\n");
    strcat(request, "\r\n");
    
    printf("%s\n", request);

    return request;
}

/**
 * Wypisuje na standardowe wyjście otrzymane w odpowiedzi ciasteczka.
 */
void print_cookies(char *header) {
    char *cookie = strcasestr(header, "Set-Cookie");
    char *end_of_cookie = NULL;
    char sign;

    while (cookie != NULL) {
        cookie += SET_COOKIE_LEN;
        // Przesuń za białe znaki.
        while ((*cookie != '\0') && (isspace(*cookie) != 0)) {
            cookie++;
        }
        end_of_cookie = cookie;
        while ((*end_of_cookie != ';') && (*end_of_cookie != '\r')) {
            end_of_cookie++;
        }
        sign = *end_of_cookie;
        *end_of_cookie = '\0';
        printf("%s\n", cookie);
        *end_of_cookie = sign;
        cookie = end_of_cookie + 1;
        cookie = strcasestr(cookie, "Set-Cookie");
    }
}

/**
 * Sprawdza, czy podany ciąg znaków jest liczbą szesnastkową.
 */
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
        // Napis str jest pusty.
        return false;
    }

    if ((str[i] == '\r') && (str[i + 1] != '\n')) {
        // Po znaku '\r' nie ma znaku nowej linii.
        return false;
    }

    if (str[i] == ';') {
        ptr = strchr(str, '\n');
        ptr--;
        if (*ptr != '\r') {
            // Linia nie kończy się CRLF.
            return false;
        }
    }

    return true;
}

/**
 * Wczytuje odbieraną zawartość strony.
 */
char *read_content(char *buff_content_part, int sock) {
    size_t sum = strlen(buff_content_part) + 1;
    size_t content_len = (3 * sum) / 2;
    ssize_t rcv_len = 0;
    char *content = NULL;
    char buffer[BUFFER_SIZE];

    content = (char *) malloc(content_len * sizeof(char));
    if (content == NULL) {
        syserr("malloc");
    }
    content[0] = '\0'; // Żeby strcat nie miał problemów.

    buffer[0] = '\0';
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

/**
 * Parsuje odebraną zawartość strony (chunked).
 */
char *parse_chunked_content(char *content) {
    size_t content_len = strlen(content) + 1;
    size_t chunk_size = 0, sum = 0;
    char *parsed_content = NULL;
    char *start = NULL, *end = NULL;

    parsed_content = (char *) malloc(content_len * sizeof(char));
    if (parsed_content == NULL) {
        syserr("malloc");
    }
    parsed_content[0] = '\0'; // Żeby strcat nie miał problemów.

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
            // Dopóki rozmiar jest mniejszy od podanego rozmiaru chunka.
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

/**
 * Generuje raport: woła funkcje odpowiedzialne za czytanie zawartości strony,
 * wypisywanie ciasteczek i ewentualnie parsowanie odpowiedzi chunked.
 */
void generate_report(char *buffer, int sock) {
    char *encoding = NULL;
    char *end_of_header = strstr(buffer, "\r\n\r\n");
    end_of_header += 3;
    *end_of_header = '\0'; // Żeby wyszukiwać pola tylko w headerze.

    if (strncmp(buffer, OK_RESPONSE, OK_RESPONSE_LEN) != 0) {
        // Status odpowiedzi jest inny niż 200 OK.
        char *info_end = strstr(buffer, "\r\n");
        *info_end = '\0';
        printf("%s\n", buffer);
    } else {
        char *content = read_content(end_of_header + 1, sock);
        print_cookies(buffer);
        if ((encoding = strcasestr(buffer, "Transfer-Encoding:")) != NULL) {  
            char *new_line = strchr(encoding, '\n');
            *new_line = '\0';
            if (strcasestr(encoding, "chunked") != NULL) {
                char *parsed = parse_chunked_content(content);
                free(content);
                content = parsed;
            }
            *new_line = '\n';
        }
        printf("Dlugosc zasobu: %lu\n", strlen(content));
        free(content);
    }
}

/**
 * Ustawia znak końca napisu na znaku ':' i daje w wyniku wskaźnik
 * na pierwszy znak portu.
 */
char *get_port_num(char *arg) {
    char *result = arg;

    while ((*result != '\0') && (*result != ':')) {
        result++;
    }

    if ((*result == '\0') || (*(result + 1) == '\0')) {
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
    free(request);

    memset(buffer, 0, sizeof(buffer));
    rcv_len = read(sock, buffer, sizeof(buffer) - 1);
    if (rcv_len < 0) {
        syserr("read");
    }

    generate_report(buffer, sock);

    if (close(sock) < 0) {
        syserr("close");
    }

    return 0;
}
