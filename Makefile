CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread
INCLUDES := -I.

LDFLAGS := -pthread

# Use system SFML (installed via sudo apt-get install libsfml-dev)
# SFML_LIBS := -lsfml-graphics -lsfml-window -lsfml-system

# Common sources
COMMON_SRCS := protocol.cpp db_client.cpp
COMMON_OBJS := $(COMMON_SRCS:.cpp=.o)

# DB server
DB_SRCS := db_server.cpp db_main.cpp
DB_OBJS := $(DB_SRCS:.cpp=.o)

# Lobby
LOBBY_SRCS := lobby_server.cpp lobby_main.cpp
LOBBY_OBJS := $(LOBBY_SRCS:.cpp=.o)

# Game server
GAME_SRCS := game_server.cpp game_server_main.cpp
GAME_OBJS := $(GAME_SRCS:.cpp=.o)

# Client
CLIENT_SRCS := client.cpp client_main.cpp
CLIENT_OBJS := $(CLIENT_SRCS:.cpp=.o)

.PHONY: all clean

all: db_server lobby_server game_server # client

db_server: $(COMMON_OBJS) $(DB_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

lobby_server: $(COMMON_OBJS) $(LOBBY_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

game_server: $(COMMON_OBJS) $(GAME_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

# client: $(COMMON_OBJS) $(CLIENT_OBJS)
#  	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS) $(SFML_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f *.o db_server lobby_server game_server client
