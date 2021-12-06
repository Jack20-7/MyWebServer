src = $(wildcard ./*.cpp)
target = $(patsubst %.cpp,%,$(src))

All:webServer

webServer:$(src)
		g++ $^ -o $@ -lpthread -g -I ./inc/ 
clean:
		-rm -rf webServer
.PHONY:All,clean
