#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "socket_bytes_handlers.h"
char buffer[4096];
char *temp;
int port;
Listener_Socket ls;

char *STAT_CODE_200 = "OK\n";
char *STAT_CODE_201 = "Created\n";
char *ERR_CODE_400 = "Bad Request\n";
char *ERR_CODE_403 = "Forbidden\n";
char *ERR_CODE_404 = "Not Found\n";
char *ERR_CODE_500 = "Internal Server Error\n";
char *ERR_CODE_501 = "Not Implemented\n";
char *ERR_CODE_505 = "Version Not Supported\n";

// Writing from buffer into file
int bufferWriteAlgorithm(char *buff, int fd, int totalBytes) {
    int totalWritten = 0;
    while (totalWritten < totalBytes) {
        int w = write(fd, buff + totalWritten, totalBytes - totalWritten);
        if (w == -1) {
            return -1;
        }
        totalWritten += w;
    }
    return totalWritten;
}

// Sends response to the socket in specific format
void sendResponse(int socket, char *statusCode, char *messageBody, unsigned long bytes) {
    dprintf(socket, "HTTP/1.1 %s\r\nContent-Length: %lu\r\n\r\n", statusCode, bytes);
    if (strcmp(messageBody, "") != 0) {
        write_n_bytes(socket, messageBody, strlen(messageBody));
    }
}

bool validHeader(char *b, int ind, int total) {
    while (ind < total) {
        int x = 0;
        while (x <= 128 && ind < total) {
            char character = b[ind];
            // fprintf(stderr, "%c\n", character);

            if (character == ':' || character == '\r') {
                break;
            } else if (!((character == 45) || (character == 46)
                           || (character >= 48 && character <= 57)
                           || (character >= 97 && character <= 122)
                           || (character >= 65 && character <= 90))) {
                // fprintf(stderr, "Invalid Character: %c", character);
                return false;
            }
            ind++;
            x++;
        }
        ind++;

        if (x > 128 && x != 0) {
            // fprintf(stderr, "False 2\n");
            return false;
        }

        if (x == 0 && b[ind] == '\n') {
            // fprintf(stderr, "True 3\n");
            return true;
        }

        while (b[ind] == ' ' && ind < total) {
            // fprintf(stderr, "Makes it here\n");
            ind++;
        }

        int y = 0;
        while (y <= 128 && ind < total) {
            if (b[ind] == '\r') {
                break;
            }
            char character = b[ind];
            if (!(character >= 32 && character <= 126)) {
                // fprintf(stderr, "False 4\n");
                return false;
            }
            ind++;
            y++;
        }

        if (y > 128 && y != 0) {
            // fprintf(stderr, "False 5\n");
            return false;
        }

        ind++;

        if (b[ind] != '\n') {
            return false;
        }
        ind++;

        if ((x == 0 && y != 0) || (x != 0 && y == 0)) {
            // fprintf(stderr, "False 6\n");
            return false;
        }
        if (y == 0 && x == 0) {
            break;
        }
    }
    // fprintf(stderr, "True 7\n");
    return true;
}

ssize_t read_until(int in, char buf[], size_t nbytes) {
    size_t total = 0;
    int pos = 0; // Buffer index
    char readChar;

    regex_t temp;
    int ret = regcomp(&temp, "\r\n\r\n", 0);
    ret = regexec(&temp, buf, 0, NULL, 0);

    while (ret != 0) {
        total += read(in, &readChar, 1);
        // fprintf(stderr, "%c\n", readChar);
        buf[pos] = readChar;
        pos++;
        ret = regexec(&temp, buf, 0, NULL, 0);

        if (total >= nbytes) {
            return total;
        }
    }
    return total;
}

int main(int argc, char **argv) {
    // Check if amount of args for port number is valid
    if (argc != 2) {
        fprintf(stderr, "Invalid Port");
        exit(1);
    }
    port = strtol(argv[1], &temp, 10);

    // Check port within valid range
    if (port <= 0 || port >= 65536) {
        fprintf(stderr, "Invalid Port");
        exit(1);
    }

    // Listen to socket at port
    int rc_li = listener_init(&ls, port);

    // Check if port could be listened
    if (rc_li == -1) {
        fprintf(stderr, "Invalid Port");
        exit(1);
    }

    int socket;
    int readBytes;
    int index;
    int counter;
    int regexCode;
    int contentLength;
    char method[9];
    char URI[65];
    char version[10];
    regex_t rules;

    // Main server loop
    while (true) {
        // Accept the listener socket
        socket = listener_accept(&ls);

        // Read 2048 bytes from socket into buffer

        readBytes = read_until(socket, buffer, 2048);

        if (readBytes == 0) {
            close(socket);
            continue;
        }
        // Check if bytes could be read
        if (readBytes == -1) {
            fprintf(stderr, "%d\n", errno);
            exit(1);
        }

        // fprintf(stderr, "Request Message: %s\n", buffer);

        // Read method from buffer
        index = 0;
        counter = 0;
        while (buffer[index] != ' ' && counter < 8) {
            method[counter] = buffer[index];
            index++;
            counter++;
        }
        index++;
        method[counter] = '\0';

        // Check if method is valid
        if (buffer[index] != '/') {
            sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
            close(socket);
            continue;
        }

        regcomp(&rules, "^[A-Za-z0-9\\.\\-]*$", 0);
        regexCode = regexec(&rules, method, 0, NULL, 0);

        if (regexCode != 0) {
            sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
            close(socket);
            regfree(&rules);
            continue;
        }

        // fprintf(stderr, "METHOD: %s\n", method);

        // Read URI from buffer
        counter = 0;
        index++;
        while (buffer[index] != ' ' && counter < 64) {
            URI[counter] = buffer[index];
            index++;
            counter++;
        }
        index++;
        URI[counter] = '\0';

        // Check if URI is valid
        if (strlen(URI) < 2 || buffer[index] != 'H') {
            // // fprintf(stderr, "%lu, %c, %s", strlen(URI), buffer[index-1], URI);
            sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
            close(socket);
            continue;
        }

        regexCode = regexec(&rules, URI, 0, NULL, 0);
        if (regexCode != 0) {
            sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
            close(socket);
            regfree(&rules);
            continue;
        }

        // fprintf(stderr, "URI: %s\n", URI);

        // Read version from buffer
        counter = 0;
        while (buffer[index] != '\r' && counter < 9) {
            version[counter] = buffer[index];
            index++;
            counter++;
        }
        index++;
        version[counter] = '\0';

        // Check version fits regex-format
        regfree(&rules);
        regcomp(&rules, "HTTP\\/[0-9]\\.[0-9]$", 0);
        regexCode = regexec(&rules, version, 0, NULL, 0);
        if (regexCode != 0) {
            sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
            close(socket);
            regfree(&rules);
            continue;
        }

        regfree(&rules);

        // Check if version is specifically 1.1
        if (strcmp(version, "HTTP/1.1") != 0) {
            sendResponse(socket, "505 Version Not Supported", ERR_CODE_505, strlen(ERR_CODE_505));
            close(socket);
            continue;
        }

        // fprintf(stderr, "Version: %s\n", version);

        // Header handling
        char *header = strstr(buffer, "Content-Length:");
        contentLength = 0;

        if (header != NULL) {
            char buffer2[129];
            int offset = 0;
            counter = 16;
            while (counter < readBytes) {
                if (header[counter] == '\r') {
                    break;
                }
                buffer2[offset] = header[counter];
                offset++;
                counter++;
            }
            buffer2[offset] = '\0';
            contentLength = strtol(buffer2, &temp, 10);
        }

        // fprintf(stderr, "Content-Length: %d\n", contentLength);

        if (buffer[index] != '\n') {
            sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
            close(socket);
            continue;
        }

        if (!validHeader(buffer, index + 1, readBytes)) {
            sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
            close(socket);
            continue;
        }
        if (strcmp(method, "GET") == 0) {
            int fd = open(URI, O_RDONLY);
            struct stat f1;
            int checker = fstat(fd, &f1);
            fprintf(stderr, "Checker: %d\n", checker);

            if (fd == -1) {
                if (errno == EACCES) {
                    sendResponse(socket, "403 Forbidden", ERR_CODE_403, strlen(ERR_CODE_403));
                    close(socket);
                    continue;
                } else {
                    sendResponse(socket, "404 Not Found", ERR_CODE_404, strlen(ERR_CODE_404));
                    close(socket);
                    continue;
                }
            }

            if (S_ISDIR(f1.st_mode)) {
                sendResponse(socket, "403 Forbidden", ERR_CODE_403, strlen(ERR_CODE_403));
                close(socket);
                continue;
            }
            sendResponse(socket, "200 OK", "", f1.st_size);
            pass_n_bytes(fd, socket, f1.st_size);
            close(fd);
        } else if (strcmp(method, "PUT") == 0) {
            int fd = open(URI, O_WRONLY | O_TRUNC, 0400 | S_IWUSR);
            if (fd == -1) {
                fd = open(URI, O_WRONLY | O_CREAT | O_TRUNC, 0400 | S_IWUSR);
                struct stat f1;
                fstat(fd, &f1);

                if (fd == -1) {
                    if (S_ISDIR(f1.st_mode) || errno == EACCES) {
                        sendResponse(socket, "403 Forbidden", ERR_CODE_403, strlen(ERR_CODE_403));
                        close(socket);
                        continue;
                    }
                    sendResponse(
                        socket, "500 Internal Server Error", ERR_CODE_500, strlen(ERR_CODE_500));
                    close(socket);
                    continue;
                }

                char *body = strstr(buffer, "\r\n\r\n");
                if (body == NULL) {
                    sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
                    close(socket);
                    continue;
                }
                if (strcmp(body, "\r\n\r\n\0") != 0) {
                    contentLength -= bufferWriteAlgorithm(body + 4, fd, contentLength);
                }
                if (pass_n_bytes(socket, fd, contentLength) == -1) {
                    sendResponse(
                        socket, "500 Internal Server Error", ERR_CODE_500, strlen(ERR_CODE_500));
                    close(socket);
                    continue;
                }
                sendResponse(socket, "201 Created", STAT_CODE_201, strlen(STAT_CODE_201));
            } else {
                char *body = strstr(buffer, "\r\n\r\n");
                if (body == NULL) {
                    sendResponse(socket, "400 Bad Request", ERR_CODE_400, strlen(ERR_CODE_400));
                    close(socket);
                    continue;
                }
                if (strcmp(body, "\r\n\r\n\0") != 0) {
                    contentLength -= bufferWriteAlgorithm(body + 4, fd, contentLength);
                }
                if (pass_n_bytes(socket, fd, contentLength) == -1) {
                    sendResponse(
                        socket, "500 Internal Server Error", ERR_CODE_500, strlen(ERR_CODE_500));
                    close(socket);
                    continue;
                }
                sendResponse(socket, "200 OK", STAT_CODE_200, strlen(STAT_CODE_200));
            }
            close(fd);
        } else {
            sendResponse(socket, "501 Not Implemented", ERR_CODE_501, strlen(ERR_CODE_501));
            close(socket);
            continue;
        }
        close(socket);
    }
    return 0;
}
