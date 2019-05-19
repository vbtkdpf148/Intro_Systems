/*
 * Proxy Lab
 * By : Kun Woo Yoo
 * Andrew ID : kunwooy
 *
 * Explanation:
 * In the main routine, the program initializes the cache,
 * creates a new pthread and executes the request from the client.
 * proxy_process is called to parse the request from the client
 * and resend the request according to our standards.
 *
 * Cache is also implemented (see cache.c for more details) using
 * doubly-linked list. Race conditions are controlled using semaphore,
 * and when the client ask for the data which is in the cache,
 * we don't resend the request to the server and provide the content
 * in our cache instead. If not cached, execute the request and
 * save the resulting payload in the cache.
 */


#include "csapp.h"
#include "cache.h"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Basic constants */
static const char *header_user_agent = "Mozilla/5.0"
                                    " (X11; Linux x86_64; rv:45.0)"
                                    " Gecko/20100101 Firefox/45.0\r\n";
static const char *connection_str = "Connection: close\r\n";
static const char *proxy_str = "Proxy-Connection: close\r\n";

/* Global variables for cache */
extern cache_p *cache_first; // ptr to first page in cache
extern cache_p *cache_last; // ptr to last page in cache
extern volatile unsigned long cache_size; // total size of cache
extern volatile int readcnt; // for semaphore
extern sem_t mutex, w; // semaphore variable

/* Function prototype */
void process_proxy(int fd);
void *new_thread(void *fdptr);
int parse_uri(char *uri, char *host, char *path, char *port);
void new_request_hdr(rio_t *rp, char *hdr, char *host);
int is_included(char *line);

int main(int argc, char** argv) {
    int listenfd, *fdptr;
    SA addr;
    socklen_t len = (socklen_t) sizeof(SA);
    pthread_t tid;
    char *port = argv[1];

    // fisrt check if listening port is specified
    if (argc != 2) {
        printf("Invalid input\n");
        exit(0);
    }

    // ignore SIGPIPE
    Signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(port);

    // initiate cache
    init_cache();

    while (1) {
        fdptr = Malloc(sizeof(int));
        *fdptr = Accept(listenfd, &addr, &len);
        Pthread_create(&tid, NULL, new_thread, fdptr);
    }

    return 0;
}

/*
 * process_proxy takes in a file descriptor as its input,
 * and processes the request as the proxy, including
 * the request parsing, sending request to the server,
 * and forwarding reply to the client.
 */
void process_proxy(int fd) {
    char get[MAXLINE], uri[MAXLINE], vers[MAXLINE];
    char host[MAXLINE], path[MAXLINE], port[MAXLINE];
    char request[MAXLINE], request_hdr[MAXLINE];
    char line[MAXLINE]; // for line intakes
    int serverfd, len;
    cache_p *page;
    cache_p *target;
    rio_t rio;
    int is_cached = 0; // indicator for cache
    char payload[MAX_OBJECT_SIZE];
    unsigned long total_length = 0;

    // Read in the first line of request
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, line, MAXLINE);

    // divide request into parts and parse
    sscanf(line, "%s %s %s", get, uri, vers);
    if (!parse_uri(uri, host, path, port)) {
        printf("Invalid uri!\n");
        return;
    }

    // only support GET
    if (strncmp(get, "GET", sizeof("GET"))) {
        printf("Only GET request accepted\n");
        return;
    }

    // now, check cache
    P(&mutex);
    readcnt++;

    // if first in
    if (readcnt == 1) {
        P(&w);
    }
    V(&mutex);

    // now check if contained in cache
    if ((page = is_hit(uri)) != NULL) {
        // send the payload to client
        Rio_writen(fd, page->payload, page->size);

        // for LRU implementation, delete the page and add to last
        delete_page(page, 0);
        add_page(page);
        is_cached = 1;
    }

    P(&mutex);
    readcnt--;
    if (!readcnt) {
        V(&w);
    }
    V(&mutex);
    if (is_cached) {
        return;
    }

    // make new request
    strcpy(request, "GET /");
    strcat(request, path);
    strcat(request, " HTTP/1.0\r\n");
    new_request_hdr(&rio, request_hdr, host);

    // now send request
    if ((serverfd = Open_clientfd(host, port)) < 0) { // error
        return;
    }

    Rio_writen(serverfd, request, strlen(request));
    Rio_writen(serverfd, request_hdr, strlen(request_hdr));

    // now listen from server
    while ((len = Rio_readn(serverfd, line, MAXLINE)) > 0) {

        // add the page to the buffer of MAX_OBJECT_SIZE
        total_length += len;
        if (total_length <= MAX_OBJECT_SIZE) {
            strcat(payload, line);
        }

        Rio_writen(fd, line, len); // forward to client
    }

    // now, save the content to cache if size is appropriate
    if (total_length <= MAX_OBJECT_SIZE) {
        // make new page
        page = new_page(uri, payload, total_length);
        P(&w);

        target = cache_first;
        while ((cache_size + total_length) > MAX_CACHE_SIZE) {
            delete_page(target, 1);
            target = cache_first;
        }
        add_page(page);
    }
    V(&w);

    Close(serverfd);

    return;
}

/*
 * new_thread is a thread handling routine that is passed onto
 * pthread_create to execute new request.
 */
void *new_thread(void *fdptr) {

    // detach itself for proper reaping
    Pthread_detach(pthread_self());

    int clientfd = *((int *) fdptr);
    Free(fdptr); // because fdptr was alloc'ed thru malloc

    // ignore SIGPIPE and proceed with request
    Signal(SIGPIPE, SIG_IGN);
    process_proxy(clientfd);

    // once done, close clientfd
    Close(clientfd);

    return NULL;
}

/*
 * parse_uri parses the given uri and saves the
 * appropriate hostname, pathname and port number to the given
 * input pointers.
 */
int parse_uri(char *uri, char *host, char *path, char *port) {

    char *start; // start ptr for extracting host, path and port
    char *end; // end ptr

    // because all uri's start with http://
    if (strncmp(uri, "http://", 7) != 0) {
        strcpy(host, "\0");
        return 0;
    }

    start = uri + 7;
    // search until it encounters one of space, ":", "/", "\r", "\n", "\0"
    end = strpbrk(start, "/:\r\n\0 ");

    // and copy the result into host
    int len = end - start;
    strncpy(host, start, len);
    host[len] = '\0'; // mark the end of string

    // check whether port number is specified
    if (*end == ':') {
        start = end + 1;
        end = strpbrk(start, "/\r\n\0 ");
        len = end - start;
        strncpy(port, start, len);
        port[len] = '\0';
    } else {
        port = "80";
    }

    // now extract path
    start = strchr(start, '/'); // in case port number was specified

    if (start == NULL) { // no path specified
        path[0] = '\0';
        return 1;
    }

    start++;
    strcpy(path, start);
    return 1;
}

/*
 * new_request_hdr takes in the request line by line
 * and forms new request header that will actually be sent
 */
void new_request_hdr(rio_t *rp, char *hdr, char *host) {
    char buf[MAXLINE];
    int host_given = 0;

    // read in the request
    while (Rio_readlineb(rp, buf, MAXLINE)) {

        if (!strncmp(buf, "\r\n", sizeof("\r\n")))
            break;

        if (is_included(buf)) // do nothing
            continue;

        else if (strstr(buf, "Host:")) {
            host_given = 1;
            strcat(hdr, buf);
        }

        else { // any additional requests
            strcat(hdr, buf);
        }

        Rio_readlineb(rp, buf, MAXLINE);
    }

    // check if the host was given, add one if not
    if (!host_given) {
        sprintf(buf, "Host: %s\r\n", host);
        strcat(hdr, buf);
    }

    // add necessary elements
    strcat(hdr, header_user_agent);
    strcat(hdr, connection_str);
    strcat(hdr, proxy_str);
    strcat(hdr, "\r\n"); // end of request
    return;
}


/*
 * is_included takes a line of string as an input,
 * checks whether user-agent, connection or proxy-connection is
 * included in the line
 */
int is_included(char *line) {

    return (strstr(line, "User-Agent") || strstr(line, "Connection:")
        || strstr(line, "Proxy-Connection:"));
}
