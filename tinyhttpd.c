#include <stdio.h>

// 处理从套接字上监听到的一个HTTP请求
void accept_request(int);

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
