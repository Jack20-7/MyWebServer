# MyWebServer
这个web服务器是我在大二上学期看完游双老师的高性能服务器编程后，在他给的服务器代码基础上加上了CGI功能
这个服务器的并发模型是半同步半反应堆模型，事件处理模式是Proactor模式，通过线程池来处理客户端逻辑的。
