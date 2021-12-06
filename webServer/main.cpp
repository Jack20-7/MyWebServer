/* ************************************************************************
> File Name:     main.cpp
> Author:        巫成洋
> Created Time:  2021年11月25日 星期四 14时38分07秒
> Description:   
 ************************************************************************/
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<cassert>
#include<sys/epoll.h>

#include"locker.h"
#include"threadpool.h"
#include"http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000


extern int addfd(int epollfd,int fd,bool one_shot);
extern int removefd(int epollfd,int fd);
//注册信号的函数
void addsig(int sig,void (handler)(int),bool restart=true)
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;

    if(restart)
    {
        sa.sa_flags|=SA_RESTART;
    }

    sigfillset(&sa.sa_mask);

    assert(sigaction(sig,&sa,NULL)!=-1);
}

//显示错误的函数
void show_error(int connfd,const char * info)
{
    printf("%s",info);
    send(connfd,info,strlen(info),0);

    close(connfd);
}
void error_handling(const char * message)
{
    fputs(message,stderr);
    fputc('\n',stderr);
    exit(1);
}

int main(int argc,char * argv[])
{
    if(argc<2)
    {
        printf("usage : %s ip_address port_number\n",basename(argv[0]));
        return -1;
    }
   
    int serv_sock,clnt_sock;
    struct sockaddr_in serv_adr,clnt_adr;
    socklen_t clnt_adr_sz;

    //忽略信号SIGPIPE
    addsig(SIGPIPE,SIG_IGN);

    //创建线程池
    threadpool<http_conn>* pool =NULL;
    try
    {
        pool=new threadpool<http_conn>;
    }
    catch(...)
    {
        return 1;
    }
    //创建客户端数组
    //每一个http_conn对象对应着一个客户端连接
    http_conn * users=new http_conn[MAX_FD];
    assert(users);

    int user_count=0;
    //创建监听套接字
    serv_sock=socket(PF_INET,SOCK_STREAM,0);
    //将套接字属性设置为长连接
    struct linger tmp={1,0};
    setsockopt(serv_sock,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

    memset(&serv_adr,0,sizeof(serv_adr));
    serv_adr.sin_family=AF_INET;
    serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
    serv_adr.sin_port=htons(atoi(argv[1]));
    if(bind(serv_sock,(struct sockaddr *)&serv_adr,sizeof(serv_adr))==-1)
    {
        error_handling("bind() error");
    }
    
    if(listen(serv_sock,5)==-1)
    {
        error_handling("listen() error");
    }
    //用于存储内核监控到的事件和对应的文件描述符
    struct epoll_event * ep_events=(struct epoll_event *)malloc(sizeof(struct epoll_event)*MAX_EVENT_NUMBER);

    //创建一个例程
    int epollfd=epoll_create(5);
    addfd(epollfd,serv_sock,false);
    http_conn::m_epollfd=epollfd;

    while(true)
    {
        //让内核进行检测
        int number=epoll_wait(epollfd,ep_events,MAX_EVENT_NUMBER,-1);

        if((number<0)&&(errno!=EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        for(int i=0;i<number;++i)
        {
            int sockfd=ep_events[i].data.fd;
            //如果是监听套接字接收到消息
            if(sockfd==serv_sock)
            {
                clnt_adr_sz=sizeof(clnt_adr);
                clnt_sock=accept(serv_sock,(struct sockaddr *)&clnt_adr,&clnt_adr_sz);
                if(clnt_sock<0)
                {
                    printf("errno is: %d\n",errno);
                    continue;
                }
                //连接的客户端数量超出界限
                if(http_conn::m_user_count>=MAX_FD)
                {
                    show_error(clnt_sock,"Internal server busy");
                    continue;
                }
                
                printf("new client id:%d\n",clnt_sock);
                //初始化连接
                users[clnt_sock].init(clnt_sock,clnt_adr);
            }
            else if(ep_events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR))
            {
                //关闭连接
                users[clnt_sock].close_conn();
            }
            //连接套接字发送读就是事件，证明客户端发来http请求报文
            else if(ep_events[i].events & EPOLLIN)
            {
                //将数据读入该对应的读取缓存并将该任务放入请求队列让线程池中的线程进行处理
                if(users[clnt_sock].read())
                {
                    printf("new task arrived....\n");
                   pool->append(users+clnt_sock);
                }
                else
                {
                    users[clnt_sock].close_conn();
                }
            }
            else if(ep_events[i].events & EPOLLOUT)
            {
                 printf("server will return data\n");
                //连接套接字发生写就绪事件
                if(!users[clnt_sock].write())
                {
                    users[clnt_sock].close_conn();
                }

            }
            else
            {

            }
        }
        
      }
    close(epollfd);
    close(serv_sock);
    delete [] users;
    delete pool;
    return 0;
    }
