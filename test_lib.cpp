#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080

void http_read_cb(evutil_socket_t fd, short events, void *arg) {
    char buf[1024];
    int len = recv(fd, buf, sizeof(buf) - 1, 0);
    if (len <= 0) {
        close(fd);
        event_free((struct event *)arg);
        return;
    }
    buf[len] = '\0';
    printf("接收到消息：%s\n", buf);

    const char *response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 13\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n"
                           "Hello, World!";
    send(fd, response, strlen(response), 0);

    close(fd);
    event_free((struct event *)arg);
}

void accept_conn_cb(evutil_socket_t listener, short event, void *arg) {
    struct event_base *base = (struct event_base *)arg;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept(listener, (struct sockaddr *)&ss, &slen);
    if (fd < 0) {
        perror("accept");
    } else if (fd > FD_SETSIZE) {
        close(fd);
    } else {
        struct event *ev = event_new(NULL, -1, 0, NULL, NULL);
        event_assign(ev, base, fd, EV_READ | EV_PERSIST, http_read_cb, (void *)ev);
        event_add(ev, NULL);
    }
}

int main() {
    struct event_base *base;
    struct event *listener_event;
    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(PORT);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return -1;
    }

    evutil_make_socket_nonblocking(listener);
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(listener, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return -1;
    }

    if (listen(listener, 1024) < 0) {
        perror("listen");
        return -1;
    }

    base = event_base_new();

    listener_event = event_new(base, listener, EV_READ | EV_PERSIST, accept_conn_cb, (void *)base);

    event_add(listener_event, NULL);

    event_base_dispatch(base);

    event_free(listener_event);
    event_base_free(base);
    close(listener);

    return 0;
}