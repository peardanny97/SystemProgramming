#include <stdio.h>
#include "csapp.h"

/* Recommended max cache sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


typedef struct {
    int port;
    char method[MAXLINE];
    char uri[MAXLINE];
    char version[MAXLINE];
    char hostname[MAXLINE];
    char path[MAXLINE];
    char content[MAXLINE];
} HttpRequest;

void doit(int fd);
int parseRequest(char *buf, HttpRequest *request);
void sendRequest(int clientfd, HttpRequest *request);
void rcvResponse(int connfd, int clientfd, HttpRequest *request);
void *thread(void *vargp);


/* For Cache */

typedef struct _node {
    char *key;
    char *value;
    struct _node *prev;
    struct _node *next;
} node;

typedef struct _Cache{
    node *head;
    node *tail;
    size_t size;
} Cache;

node *tail, *head;
Cache *cache;
void cacheInit();
void cacheInsert(char *key, char *value);
int cacheHit(char *key, char *value);
void destroyCache();

/* For Cache end */


int main(int argc, char **argv)
{
    printf("%s", user_agent_hdr);
    int connfd, listenfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;  // for concurrent proxy server

    /* check comman-line args*/
    if (argc != 2) {
        printf("error");
        fprintf(stderr, "Usage: %s <port>\n",argv[0]);
        exit(1);
    }

    cacheInit();
    listenfd = Open_listenfd(argv[1]);  // get port to make listen file discriptor

    while(1){
        clientlen = sizeof(clientaddr);
        if((connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen)) < -1 ){
            continue;  // keep trying until accepted
        }
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        int *connfdp = malloc(sizeof(int));
        *connfdp = connfd;  // make pointer for connfd to pass by reference to thread

        Pthread_create(&tid, NULL, thread, (void*)connfdp);

    }
    destroyCache();
    return 0;
}

void doit(int connfd){
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host_hdr[MAXLINE] = "Host: ";
    HttpRequest request;
    rio_t rio;
    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    if(parseRequest(buf, &request) <0)
        return;
    /*
        start of http content, method will be GET
        GET /research.highlights HHTTP/1.0
    */
    sprintf(request.content, "GET %s HTTP/1.0\r\n", request.path);

    printf("hostname: %s\n",request.hostname);
    printf("port: %d\n\n",request.port);

    // read line by line by rio_readlineb
    while(rio_readlineb(&rio, buf, MAXLINE) > 0){
        printf("buf is: %s",buf);
        if(strcmp(buf, "\r\n") == 0){
            // last line of request HDR
            strcat(request.content, buf);
            break;
        }
        else if(strstr(buf, "Host:")){
            // Host: www.snu.ac.kr
            strcat(host_hdr, request.hostname);
            char portStr[10];
            sprintf(portStr, ":%d", request.port);
            strcat(host_hdr, portStr);
            strcat(host_hdr, "\r\n");
            //sprintf(host_hdr, "Host: %s\r\n", request.hostname);
            strcat(request.content, host_hdr);
        }
        else if(strstr(buf, "User-Agent:")){
            strcat(request.content, user_agent_hdr);
        }
        else if(strstr(buf, "Proxy-Connection:")){
            strcat(request.content, "Proxy-Connection: close\r\n");
        }
        else if(strstr(buf, "Connection:")){
            strcat(request.content, "Connection: close\r\n");
        }

    }
    /* send HttpRequest to server */
    printf("final Http content\n");
    printf("%s",request.content);

    char portStr[10];
    sprintf(portStr, "%d", request.port);


    char cacheBuf[MAX_OBJECT_SIZE];

    /* if cache hit no need to call server */
    if(cacheHit(request.content, cacheBuf)){
        Rio_writen(connfd, cacheBuf, strlen(cacheBuf));
        return;
    }
    else{
        int clientfd = open_clientfd(request.hostname, portStr);
        if(clientfd < 0){
            printf("Error connecting server\n");
            return;
        }
        sendRequest(clientfd, &request);
        rcvResponse(connfd, clientfd, &request);
    }
    return;
}

/*
    parse http request line
    first parse by <method> <uri> <version>
    uri can be either ht
*/
int parseRequest(char *buf, HttpRequest *request){

    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", request->method, request->uri, request->version);
    if(strcasecmp(request->method, "GET")){
        printf("method is %s only GET is possible\n",request->method);
        return -1;
    }
    request->port = 80;  // default port 80
    strcat(request->path, "/");  // default path /

    char *url = strstr(request->uri, "http://");  // check if uri starts with http://
    if(url){                                      // if url is not null
        url = url + 7;                            // neglect http:// part
        if(strchr(url, ':')){                     // if url has : there will be port #
            sscanf(url, "%[^:]:%i%s", request->hostname, &request->port, request->path);
            //subtract ":" from hostname
        }
        else{
            sscanf(url, "%[^/]%s", request->hostname, request->path);
            //subtract "/" from hostname
        }
    }
    return 0;
}

void sendRequest(int clientfd, HttpRequest *request){
    Rio_writen(clientfd, request->content, strlen(request->content));
}

void rcvResponse(int connfd, int clientfd, HttpRequest *request){
    char buf[MAXLINE];
    char cacheBuf[MAX_OBJECT_SIZE];

    rio_t rio;
    ssize_t n, cur_size;
    Rio_readinitb(&rio, clientfd);
    /* need to read line by line to cache by object size */
    cur_size = 0;
    while((n=rio_readlineb(&rio, buf, MAXLINE)) > 0){
        cur_size += n;
        if (cur_size < MAX_OBJECT_SIZE){
            // make object until it reaches maximum object size
            strcat(cacheBuf, buf);
        }
        Rio_writen(connfd, buf, n);
    }

    cacheInsert(request->content, cacheBuf);
//    n = Rio_readnb(&rio, buf, MAX_CACHE_SIZE);
//    Rio_writen(connfd, buf, n);
}

void *thread(void *vargp){
    int connfd = *(int *)(vargp);
    Pthread_detach(pthread_self());
    Free(vargp);  // free used vargp
    doit(connfd);
    Close(connfd);
    return NULL;
}

/* functions for cache */

void cacheInit(){
    cache = malloc(sizeof(Cache));
}

void cacheInsert(char *key, char *value){
    node *temp;
    size_t new_size = strlen(key) + strlen(value) + sizeof(temp);
    cache->size = cache->size + new_size;
    // if cache is not empty but new size is bigger than maximum cache size, delete tail (LRU)
    while((cache->tail != NULL) && (cache->size > MAX_CACHE_SIZE)){
        temp = cache->tail;
        ssize_t temp_size = strlen(temp->key) + strlen(temp->value) + sizeof(temp);
        cache->size = cache->size - temp_size;  // subtract tail's node size
        cache->tail = temp->prev;
        temp->prev->next = NULL;
        // free temp node's elements then free temp
        free(temp->key);
        free(temp->value);
        free(temp);
    }
    // make new node
    temp = (node*)malloc(sizeof(node));
    temp->key = (char *)malloc(strlen(key)+1);
    temp->value = (char *)malloc(strlen(value)+1);
    strcpy(temp->key, key);
    strcpy(temp->value, value);

    temp->prev = NULL;
    temp->next = cache->head;
    if(cache->head == NULL){
        // no elements in Cache
        cache->tail = temp;
    }
    else{
        cache->head->prev = temp;
    }
    cache->head = temp;
}

int cacheHit(char *key, char *value){
    node *temp = cache->head;
    /* search through cache */
    while(temp != NULL){
        if(strcmp(temp->key, key)==0){
            // same key is found, move to head as LRU
            if(temp != cache->head){
                // temp is not at head
                if(temp == cache->tail){
                    // temp is tail
                    cache->tail = temp->prev;
                }
                else{
                    temp->next->prev = temp->prev;
                }
                temp->prev->next = temp->next;
                temp->next = cache->head;
                cache->head->prev = temp;
                temp->prev = NULL;
                cache->head = temp;
            }
            strcpy(value, temp->value);
            return 1;  // cache hit!
        }
        temp = temp->next;
    }
    return 0;  // cache miss!
}

void destroyCache(){
    node *cur, *temp;
    cur = cache->head;
    while(cur != NULL){
        temp = cur->next;
        free(cur->key);
        free(cur->value);
        free(cur);
        cur = temp;
    }
}
