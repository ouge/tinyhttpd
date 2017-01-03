#include <stdio.h>

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
    
    // 创建套接字
    httpd = socket(AF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY: 通配地址
    if ((setsockopt(httpd, SOL_SOCKET, 
                SO_REUSEADDR, &
                , sizeof(on))) < 0)
    {
        error_die("setsockopt failed");
    }
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
    
    numchars = get_line(client, buf, sizeof(buf)); // 从与客户端连接的套接字中读一行到buf中

    // 将buf中从开头到第一个空格为止的内容拷贝到method中，末尾添上‘/0’
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';


    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;
    
    
    i = 0;
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

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

    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if (stat(path, &st) == -1)
    {
        while ((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR)
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

