#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"


// 处理从套接字上监听到的一个HTTP请求
void accept_request(void *);

// 返回给客户端这是一个错误请求
void bad_request(int);

// 读取服务器上某个文件写到socket套接字
void cat(int, FILE*);

// 处理执行cgi程序时发生的错误
void cannot_execute(int);

// 把错误信息写到perror并退出
void error_die(const char*);

// cgi处理程序
void execute_cgi(int, const char*, const char*, const char*);

// 读取套接字的一行
int get_line(int, char*, int);

// 把HTTP响应的头部写到套接字
void headers(int, const char*);

// 没找到路径名
void not_found(int);

// 调用cat把服务器文件返回给浏览器
void serve_file(int, const char*);

// 初始化httpd服务
int startup(u_short*);

// 返回给浏览器表明收到的HTTP请求所用的method不被支持
void unimplemented(int);


int main(void)
{
    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while(1)
    {
        client_sock = accept(server_sock,
                (struct sockaddr*)&client_name,
                &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        if (pthread_create(&newthread, NULL,
                    (void*)accept_request, (void*)&client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}


int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    httpd = socket(AF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY: 通配地址
    if ((setsockopt(httpd, SOL_SOCKET,
                SO_REUSEADDR, &on, sizeof(on))) < 0)
        error_die("setsockopt failed");

    if (bind(httpd, (struct sockaddr*)&name, sizeof(name)) < 0)
        error_die("bind");

    // 如果传入的端口号是0，程序会随机分配一个监听端口，并用port指向该值
    if (*port == 0)
    {
        int namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr*)&name, (socklen_t*)&namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }

    if (listen(httpd, 5) < 0)
        error_die("listen");

    return(httpd);
}


void error_die(const char* sc)
{
    perror(sc);
    exit(1);
}


void accept_request(void *arg)
{
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i = 0, j = 0;
    struct stat st;
    int cgi = 0;

    char* query_string = NULL;

    // 一个HTTP请求报文：请求行、请求头部、空行、请求数据
    // 请求行：请求方法字段(get/post)、URL字段、HTTP协议版本字段，如：GET /index.html HTTP/1.1
    //
    numchars = get_line(client, buf, sizeof(buf)); // 从与客户端连接的套接字中读一行到buf中

    // 将方法字段保存到method中
    while (!isspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';

    // 请求方法不支持
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    //
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    // 解析URL字段，保存在url[]中
    i = 0;
    while (isspace(buf[j]) && (j < numchars))
        j++;
    while (!isspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    // 如果是GET方法，解析URL结尾?后的的参数值，保存在query_string中
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    // 将url地址加上请求地址的主页索引，保存在path[]中
    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    // 如果路径不存在，
    if (stat(path, &st) == -1)
    {
        while ((numchars > 0) && strcmp("\n", buf)) // 忽略请求头部
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    //如果路径存在，提供文件或调用CGI程序处理
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR) // 文件类型为目录时末尾添加/index.html
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH))
            cgi = 1;
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);

    }

    close(client);
}

void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void serve_file(int client, const char* filename)
{
    FILE* resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

void headers(int client, const char* filename)
{
    char buf[1024];
    (void)filename;

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void cat(int client, FILE* resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf,sizeof(buf), resource);
    }
}


void execute_cgi(int client, const char* path,
        const char* method, const char* query_string)
{
    char buf[1025];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int cotent_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0)
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&buf[16])
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (bad_request == -1)
        {
            bad_request(client);
            return;
        }
    }
    else
    {
    }

    if (pipe(cgi_output) < 0)
    {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0)
    {
        cannot_execute(client);
        return;
    }

    if ((pid = fork()) < 0)
    {
        cannot_execute(client);
        return;
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    if (pid == 0)
    {
       char meth_env[255];
    }
    else
    {

    }
}




