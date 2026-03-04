FLAGS = -DDEBUG -DLOG_LEVEL=LOG_DEBUG

all: clean server subscriber

server:
	g++ -o server server.cpp serverlib.cpp subscriberlib.cpp $(FLAGS)

subscriber:
	g++ -o subscriber subscriber.cpp subscriberlib.cpp serverlib.cpp $(FLAGS)

clean:
	rm -f server subscriber