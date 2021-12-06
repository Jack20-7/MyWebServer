/* ************************************************************************
> File Name:     http_conn.h
# Author:         巫成洋
> Created Time:  Tue 23 Nov 2021 07:33:26 PM CST
> Description:   
 ************************************************************************/
#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include"locker.h"

#define STDIN 0
#define STDOUT 1
//封装该类用来表示客户端的连接.一个该对象表示一个客户端连接,用来作为任务存放在线程池的请求队列中
class http_conn
{
public:
    //文件名的最大长度
    static const int FILENAME_LEN=200;
    //读缓冲区的大小
    static const int READ_BUFFER_SIZE=2048;
    //写缓冲区的大小
    static const int WRITE_BUFFER_SIZE=1024;
    //http请求报文使用的方法
    enum METHOD {
        GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,
        CONNECT,PATCH};
    //主状态机所处于的状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE=0,//正在解析请求行
        CHECK_STATE_HEADER,//正在解析首部字段
        CHECK_STATE_CONTENT//正在解析报文主体
    };
    //服务器处理HTTP请求的可能结果
    enum HTTP_CODE{
        NO_REQUEST,GET_REQUEST,BAD_REQUEST,
        NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,
        CGI_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION
    };
    //从状态机的状态
    enum LINE_STATUS{
        LINE_OK=0,LINE_BAD,LINE_OPEN
    };
public:
    http_conn()
    {

    }
    ~http_conn()
    {

    }
public:
    //用于主线程调用，初始化客户端连接
    void init(int sockfd,sockaddr_in & addr);
    //关闭连接
    void close_conn(bool real_close=true);
    //线程池中工作线程调用的函数，用于处理客户端逻辑
    void process();
    //用于主线程调用，将数据从套接字接收缓冲区复制到用户缓冲区
    bool read();
    //用于主线程调用，将用户缓冲区中的数据复制到套接字的发送缓冲区
    bool write();
private:
    //初始化一些连接数据
    void init();
    //主状态机入口函数，用于分析http请求数据包
    HTTP_CODE process_read();
    //用于将根据分析结果将http响应报文发送到套接字发送缓冲区
    bool process_write(HTTP_CODE ret);

    //下面的函数是主状态机调用的用于分析请求报文的函数
    HTTP_CODE parse_request_line(char * text);//分析请求行
    HTTP_CODE parse_headers(char * text,int index);//用于分析报文首部字段
    HTTP_CODE parse_content(char * text);//用于分析报文主体
    HTTP_CODE do_request();
    HTTP_CODE execute_cgi();//执行cgi程序
    //返回接收缓冲区中的一行数据
    char * get_line()
    {
        return m_read_buf+m_start_line;
    }
    //从状态机函数，用于判断是否接收到完整的行(请求行+首部字段)
    LINE_STATUS parse_line();
    //下面是process_write调用来将http响应报文中的数据发送到套接字发送缓存的函数
    void unmap();
    //将传入的参数发送到发送缓存
    bool add_response(const char * format,...);
    //将响应报文主体发送到发送缓存
    bool add_content(const char * content);

    bool add_content_length(int content_len);
    
    bool add_status_line(int status,const char * title);

    bool add_headers(int content_length);

    bool add_linger();

    bool add_blank_line();

public:
    //我们将所以的文件描述符都注册到同一个内核注册表中，所以将其设置为static
    static int m_epollfd;

    static int m_user_count;//连接的客户端的数量

private:
   int m_sockfd;//与客户端连接的套接字
   sockaddr_in m_address;//客户端套接字的地址信息
   char m_read_buf[READ_BUFFER_SIZE];//读取缓冲
   int m_read_idx;
   int m_checked_idx;//当前正在解析的字符下标
   int m_start_line;//解析的行的起始位置

   char m_write_buf[WRITE_BUFFER_SIZE];//写入缓冲区
   int m_write_idx;//写缓存区中待发送的字节数

   char m_cgi_buf[4096];//cig程序处理客户端请求后返回的数据存储地
   int m_content_idx;//报文主体起始位置
   int m_cgi_content_idx;

   CHECK_STATE m_check_state;//主状态机的状态
   METHOD m_method;//请求方法

   char m_real_file[FILENAME_LEN];//用来存储目标文件的完整路径

   char * m_url;//指向请求uri的起始位置
   char * m_version;//指向请求行的版本号
   char * m_host;
   char * query_string;//如果get方法存在参数的话，指向参数的起始位置

   int m_cgi;//判断是否执行cgi程序

   int m_content_length;//报文主体的长度

   bool m_linger;//长连接和短链接

   char * m_file_address;//客户请求文件被mmap映射到的虚拟内存的起始地址

   struct stat m_file_stat;

   struct iovec m_iv[2];

   int m_iv_count;
};
#endif

