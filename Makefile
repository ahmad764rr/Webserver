NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
INCLUDES = -Iinclude

SRC_DIR = src
OBJ_DIR = build

SRCS = \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/Config.cpp \
	$(SRC_DIR)/WebServer.cpp \
	$(SRC_DIR)/CgiHandler.cpp \
	$(SRC_DIR)/http/HttpRequest.cpp \
	$(SRC_DIR)/http/HttpResponse.cpp

OBJS = \
	$(OBJ_DIR)/main.o \
	$(OBJ_DIR)/Config.o \
	$(OBJ_DIR)/WebServer.o \
	$(OBJ_DIR)/CgiHandler.o \
	$(OBJ_DIR)/http/HttpRequest.o \
	$(OBJ_DIR)/http/HttpResponse.o

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/main.o: $(SRC_DIR)/main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/Config.o: $(SRC_DIR)/Config.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/WebServer.o: $(SRC_DIR)/WebServer.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/CgiHandler.o: $(SRC_DIR)/CgiHandler.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/http/HttpRequest.o: $(SRC_DIR)/http/HttpRequest.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/http/HttpResponse.o: $(SRC_DIR)/http/HttpResponse.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
