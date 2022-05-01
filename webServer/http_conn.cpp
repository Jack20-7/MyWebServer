/* ************************************************************************
> File Name:     http_conn.cpp
> Author:        巫成洋
> Created Time:  2021年11月24日 星期三 10时30分03秒
> Description:   
 ************************************************************************/
#include"http_conn.h"

//定义一些http响应报文的原因短语和主体
const char * ok_200_title="OK";

const char * error_400_title="Bad Request";
const char * error_400_from="Your request has bad syntax or is inherently impossible to sastisfy.\n";

const char * error_403_title="Forbidden";
const char * error_403_from="You do not have permission to get file from this server.\n";

const char * error_404_title="Not Found";
const char * error_404_from="The requestd file was not found on this server.\n";

const char * error_500_title="Internal Error";
const char * error_500_from="There was an unusual problem serving the requested file.\n";

const char * doc_root="/home/xq";//网址的根目录
//将文件描述符设置为非阻塞
int setnonblocking(int fd)
{
   int old_option=fcntl(fd,F_GETFL);
   int new_option=old_option|O_NONBLOCK;
   fcntl(fd,F_SETFL,new_option);
   return old_option;
}
//将fd在epollfd中进行注册
void addfd(int epollfd,int fd,bool one_shot)
{
    struct epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;
    if(one_shot)
    {
        event.events|=EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}
void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}
//修改fd在epollfd中注册的事件
void modfd(int epollfd,int fd,int ev)
{
   struct epoll_event event;
   event.data.fd=fd;
   event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
   epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int http_conn::m_user_count=0;
int http_conn::m_epollfd=-1;

void http_conn::close_conn(bool real_close)
{
    if(real_close&&(m_sockfd!=-1))
    {
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_count--;
    }
}
//用于初始化连接
void http_conn::init(int sockfd,sockaddr_in & addr)
{
   m_sockfd=sockfd;
   m_address=addr;
   //下面两行用于调试，避免TIME_WAIT状态，使用市应该注释掉
   int reuse=1;
   setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

   addfd(m_epollfd,m_sockfd,true);
   m_user_count++;

   init();
   printf("http_conn object init\n");
}

void http_conn::init()
{

    m_check_state=CHECK_STATE_REQUESTLINE;//初始化主状态机的状态
    m_linger=false;

    m_method=GET;
    m_url=0;
    m_version=0;
    m_content_length=0;
    m_host=0;
    m_start_line=0;
    m_checked_idx=0;
    m_read_idx=0;
    m_write_idx=0;
    m_content_idx=0;
    m_cgi_content_idx=0;
    query_string=0;
    m_cgi=0;

    memset(m_read_buf,0,READ_BUFFER_SIZE);
    memset(m_write_buf,0,WRITE_BUFFER_SIZE);
    memset(m_real_file,0,FILENAME_LEN);
    memset(m_cgi_buf,0,4096);
}
//从状态机用于判断是否收到完整行的函数
http_conn::LINE_STATUS http_conn::parse_line()
{
   char temp;
   for(;m_checked_idx<m_read_idx;++m_checked_idx)
   {
       temp=m_read_buf[m_checked_idx];
       if(temp=='\r')
       {
          if((m_checked_idx+1)==m_read_idx)
          {
              return LINE_OPEN;
          }
          else if((m_read_buf[m_checked_idx+1])=='\n')//读取到了完整的行
          {
              printf("parse_line finish\n");
              int i=m_start_line;
              for(;i<m_checked_idx;++i)
              {
                printf("%c",m_read_buf[i]);
              }
              printf("\n");
              //将行的末尾改为\0
              m_read_buf[m_checked_idx++]='\0';
              m_read_buf[m_checked_idx++]='\0';
              return LINE_OK;
          }
          return LINE_BAD;//语法错误


       }
       else if(temp=='\n')
       {
           if((m_checked_idx>1)&&(m_read_buf[m_checked_idx-1]=='\r'))
           {
              printf("parse_line finish\n");
              int i=m_start_line;
              for(;i<m_checked_idx;++i)
              {
                printf("%c",m_read_buf[i]);
              }
              printf("\n");
               m_read_buf[m_checked_idx-1]='\0';
               m_read_buf[m_checked_idx++]='\0';
               return LINE_OK;
           }
           return LINE_BAD;
       }
   }
       return LINE_OPEN;//表示还需要继续读取
}
//用于主线程进行调用，非阻塞的将数据从socket
//读到用户的读取缓冲区
bool http_conn::read()
{
    if(m_read_idx>=READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read=0;
    while(true)
    {
        bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(bytes_read==-1)
        {
            //表示没有数据可读了，读完了
            if(errno==EAGAIN||errno==EWOULDBLOCK)
            {
                
                printf("read() finish\n");
                break;
            }
            return false;//读取失败
        }
        else if(bytes_read==0)
        {
            return false;
        }
        m_read_idx+=bytes_read;

    }
    return true;
}
//分析请求行
http_conn::HTTP_CODE http_conn::parse_request_line(char * text)
{
    m_url=strpbrk(text," \t");
    if(!m_url)
    {
        printf("NO method\n");
        return BAD_REQUEST;
    }
    *m_url++='\0';

    char * method=text;
     //只能处理get和post方法
    if(strcasecmp(method,"GET")==0)
    {
        printf("http method is GET\n");
        m_method=GET;
    }
    else if(strcasecmp(method,"POST")==0)
    {
        printf("http method is POST\n");
        m_cgi=1;
        m_method=POST;
    }
    else
    {
        printf("method error\n");
        return BAD_REQUEST;
    }

    m_url+=strspn(m_url," \t");//将m_url移出空格

    m_version=strpbrk(m_url," \t");
    if(!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++='\0';
    m_version+=strspn(m_version," \t");

    if(strcasecmp(m_version,"HTTP/1.1")!=0)
    {
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url,"http://",7)==0)
    {
        m_url+=7;
        m_url=strchr(m_url,'/');
    }
    
    if(!m_url||m_url[0]!='/')
    {
        return BAD_REQUEST;
    }

    //对GET进行处理
    if(strcasecmp(method,"GET")==0)
    {
        //判断URI是否携带参数
        query_string=m_url;
        while((*query_string!='?')&&(*query_string!='\0'))
        {
            query_string++;
        }

        if(*query_string=='?')
        {
            m_cgi=1;
            *query_string='\0';
            query_string++;
        }
    }
    m_check_state=CHECK_STATE_HEADER;
    return NO_REQUEST;
}
//分析报文首部字段
http_conn::HTTP_CODE http_conn::parse_headers(char * text,int index)
{
    //表述首部字段解析完毕
    if(text[0]=='\0')
    {
      printf("headers parse finish \n");
       if(m_content_length!=0)
       {
           m_content_idx=index;//记录报文主体的开始下标
           m_check_state=CHECK_STATE_CONTENT;
           return NO_REQUEST;
       }
       return GET_REQUEST;
    }
    else if(strncasecmp(text,"Connection:",11)==0)
    {
        text+=11;
        text+=strspn(text," \t");
        if(strcasecmp(text,"keep-alive")==0)
        {
            m_linger=true;
        }
    }
    else if(strncasecmp(text,"Content-Length:",15)==0)
    {
        text+=15;
        text+=strspn(text," \t");
        m_content_length=atol(text);
    }
    else if(strncasecmp(text,"Host:",5)==0)
    {
        text+=5;
        text+=strspn(text," \t");
        m_host=text;
    }
    else
    {
        printf("oop! unknow header %s\n",text);
    }
    return NO_REQUEST;
}
//分析请求报文主体，只是判断是否读取完
http_conn::HTTP_CODE http_conn::parse_content(char * text)
{
    if(m_read_idx>=(m_content_length+m_checked_idx))
    {
        text[m_content_length]='\0';
        int i=m_content_idx;
        for(;i<m_read_idx;++i)
        {
          printf("%c",m_read_buf[i]);
        }
        printf("\n");
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
//主状态机函数，用来调用上面三个函数分析请求报文
http_conn::HTTP_CODE http_conn::process_read()
{
   LINE_STATUS line_status=LINE_OK;
   HTTP_CODE ret=NO_REQUEST;
   char * text=0;

   while(((m_check_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||((line_status=parse_line())==LINE_OK))
   {
       text=get_line();
       int tmpIndex=m_start_line;
       m_start_line=m_checked_idx;
       //下面进行分析
       switch(m_check_state)
       {
           case CHECK_STATE_REQUESTLINE:
               {
                 printf("parse status line\n");
                 ret=parse_request_line(text);
                 if(ret==BAD_REQUEST)
                 {
                     return BAD_REQUEST;
                 }
                 break;
               }
           case CHECK_STATE_HEADER:
               {
                   printf("parse header\n");
                   ret=parse_headers(text,tmpIndex);
                   if(ret==BAD_REQUEST)
                   {
                       return BAD_REQUEST;
                   }
                   else if(ret==GET_REQUEST)
                   {
                       return do_request();
                   }
                   break;
               }
            case CHECK_STATE_CONTENT:
               {
                   printf("parse content\n");
                   ret=parse_content(text);
                   if(ret==GET_REQUEST)
                   {
                       return do_request();
                   }
                   line_status=LINE_OPEN;
                   break;
               }
             default:
               {
                   return INTERNAL_ERROR;
               }
       }
   }
   return NO_REQUEST;
}
//下面这个函数是我们通过主状态机分析完http请求报文后调用来对文件属性进行判断，并且判断是否执行cgi函数
http_conn::HTTP_CODE http_conn::do_request()
{
    //将文件路径及名称结合
    strcpy(m_real_file,doc_root);
    int len=strlen(doc_root);
    strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
    printf("file path:%s\n",m_real_file);

    if(stat(m_real_file,&m_file_stat)<0)
    {
        return NO_RESOURCE;
    }

    if(!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    //目标文件是一个目录
    if(S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    //判断目标文件是否是一个可执行文件
    if((m_file_stat.st_mode & S_IXUSR)||(m_file_stat.st_mode & S_IXGRP)||(m_file_stat.st_mode & S_IXOTH))
    {
        m_cgi=1;
    }
    
    //这里根据m_cgi的值判断是否需要执行cgi程序
    if(m_cgi==1)
    {
       printf("execute_cgi\n");
       return  execute_cgi();
    }
    printf("normal file\n");
    //执行普通的文件操作
    int fd=open(m_real_file,O_RDONLY);
    //通过mmap函数将文件映射到进行的虚拟地址空间中区
    m_file_address=(char *)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);

    close(fd);
    return FILE_REQUEST;   
}
//cgi程序
http_conn::HTTP_CODE http_conn::execute_cgi()
{
    //创建两个管道，用于子进程于父进程传输数据
  int cgi_output[2]; 
  int cgi_input[2];
  pid_t pid;
  int status;

  //建立管道
  if(pipe(cgi_output)<0)
  {
      return INTERNAL_ERROR;
  }
  if(pipe(cgi_input)<0)
  {
      return INTERNAL_ERROR;
  }
      const char * method;
      if(m_method==POST)
      {
        method="POST";
      }
      else if(m_method==GET)
      {
        method="GET";
      }

  //创建子进程来执行可执行程序
  if((pid=fork())<0)
  {
      return INTERNAL_ERROR;
  }

  //首先，在子进程中
  if(pid==0)
  {
      printf("child proc start\n");
      //用来进行环境遍历的处理
      char meth_env[255];
      char query_env[255];
      char length_env[255];

      //我们将子进程的标准输出重定向到cgi_out的输入端
      //子进程的标准输入重定向到cgi_input的输出端
      printf("child proc dup\n");
      dup2(cgi_output[1],1); 
      dup2(cgi_input[0],0);

      //在关闭管道的其他两端
      close(cgi_output[0]);
      close(cgi_input[1]);

      //在进行环境变量的设置

	   sprintf(meth_env, "REQUEST_METHOD=%s", method);
	   putenv(meth_env);



	 if (strcasecmp(method, "GET") == 0) {
		  //存储QUERY_STRING
		sprintf(query_env, "QUERY_STRING=%s", query_string);
		putenv(query_env);
      }
	  else {   /* POST */
			//存储CONTENT_LENGTH
		sprintf(length_env, "CONTENT_LENGTH=%d", m_content_length);
		 putenv(length_env);
		  }
      //执行可执行文件
      execl(m_real_file,m_real_file,NULL);
      exit(0);
  }
  else//父进程中
  {
      printf("father proc start\n");
     close(cgi_output[1]);
     close(cgi_input[0]);
     char c;
     if(strcasecmp(method,"POST")==0)
     {
        for(int i=0;i<m_content_length;++i)
        {
            ::write(cgi_input[1],&m_read_buf[m_content_idx+i],1);
            printf("%c",m_read_buf[m_content_idx+i]);
        }
     }
       printf("\n");
         //从管道中读取子进程返回的数据
         while(::read(cgi_output[0],&c,1)>0)
         {
            printf("%c",c);
            m_cgi_buf[m_cgi_content_idx++]=c;
         }
         
     //关闭管道
     close(cgi_output[0]);
     close(cgi_input[1]);
     //为了避免僵尸进程，等待子进程结束
     waitpid(pid,&status,0);
     
  }
  return CGI_REQUEST; 
}

void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address=0;
    }
}

//下面这个函数用于主线程调用，将输出缓冲区中的数据进行发送
bool http_conn::write()
{
    int temp=0;
    int bytes_have_send=0;//已经发送的字节
    int bytes_to_send=m_write_idx;//还要发送的字节数
    if(bytes_to_send==0)
    {
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }

    while(1)
    {
        temp=writev(m_sockfd,m_iv,m_iv_count);
        if(temp<=-1)
        {
            //如果是发送缓冲区已满
            if(errno==EAGAIN)
            {
                //等下一轮在进行发送
              modfd(m_epollfd,m_sockfd,EPOLLOUT);
              return true;
            }
            //发送出错的话
            unmap();
            return false;
        }
        bytes_have_send+=temp;
        bytes_to_send-=temp;

        if(bytes_have_send>=bytes_to_send)
        {
            printf("write() correct\n");
            unmap();
            //判断是长连接还是短链接
            if(m_linger)
            {
               init();
               modfd(m_epollfd,m_sockfd,EPOLLIN);
               return true;
            }
            else
            {
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return false;
            }
        }
    }
}
//下面这个函数用于将参数发送到socket的发送缓冲区中
bool http_conn::add_response(const char * format,...)
{
    if(m_write_idx>=WRITE_BUFFER_SIZE)
    {
        return false;
    }

    va_list arg_list;
    va_start(arg_list,format);

    int len=vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);

    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx))
    {
        return false;
    }
    m_write_idx+=len;
    va_end(arg_list);
    return true;
}
//将状态行写入发送缓冲区
bool http_conn::add_status_line(int status,const char * title)
{
   return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}
//将头部字段写入发送缓冲区
bool http_conn::add_headers(int content_len)
{
   add_content_length(content_len);
   add_linger();
   add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
   return  add_response("Content-Length: %d\r\n",content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n",(m_linger==true)?"keep-alive":"close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s","\r\n");
}

bool http_conn::add_content(const char * content)
{
    return add_response("%s",content);
}

//根据主状态机返回的状态，将数据弄到发送缓冲区
bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
     case INTERNAL_ERROR:
         {
             add_status_line(500,error_500_title);
             add_headers(strlen(error_500_from));
             if(!add_content(error_500_from))
             {
                 return false;
             }
             break;
         }
     case BAD_REQUEST:
         {
             add_status_line(400,error_400_title);
             add_headers(strlen(error_400_from));
             if(!add_content(error_400_from))
             {
                 return false;
             }
             break;
         }
     case NO_RESOURCE :
         {
             add_status_line(404,error_404_title);
             add_headers(strlen(error_404_from));
             if(!add_content(error_404_from))
             {
                 return false;
             }
             break;
         }
     case FORBIDDEN_REQUEST:
         {
             add_status_line(403,error_403_title);
             add_headers(strlen(error_403_from));
             if(!add_content(error_403_from))
             {
                 return false;
             }
             break;
         }
     case FILE_REQUEST:
         {
             add_status_line(200,ok_200_title);
             if(m_file_stat.st_size!=0)
             {
                 add_headers(m_file_stat.st_size);
                 m_iv[0].iov_base=m_write_buf;
                 m_iv[0].iov_len=m_write_idx;
                 m_iv[1].iov_base=m_file_address;
                 m_iv[1].iov_len=m_file_stat.st_size;

                 m_iv_count=2;
                 return true;
             }
             else
             {
                 const char * ok_string="<html><body></body></html>";
                 add_headers(strlen(ok_string));
                 if(!add_content(ok_string))
                 {
                     return false;
                 }
             }
         }
     case CGI_REQUEST:
         {
           printf("cgi return \n");
           add_status_line(200,ok_200_title);
           add_content_length(m_cgi_content_idx);
           add_linger();
           m_iv[0].iov_base=m_write_buf;
           m_iv[0].iov_len=m_write_idx;
           m_iv[1].iov_base=m_cgi_buf;
           m_iv[1].iov_len=m_cgi_content_idx;

           m_iv_count=2;

           return true;
         }
     default:
         {
             return false;
         }
    }
   m_iv[0].iov_base=m_write_buf;
   m_iv[0].iov_len=m_write_idx;
   m_iv_count=1;
   return true;
}
void http_conn::process()
{
    HTTP_CODE read_ret=process_read();
    if(read_ret==NO_REQUEST)
    {
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return ;
    }
    bool write_ret=process_write(read_ret);
    while(!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}

