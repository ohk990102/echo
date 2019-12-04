CXXFLAGS =
LDLIBS = -lpthread

all: echo_server echo_client

debug: CXXFLAGS += -DDEBUG -g
debug: echo_server echo_client

echo_server: echo_server.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS)	$(LDLIBS)

echo_client: echo_client.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS)	$(LDLIBS)

clean:
	rm -rf echo_server echo_client

.PHONY: all clean